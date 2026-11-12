#include "FolderConvert.h"
#include "MediaConvertionTask.h"
#include "MediaConvertionAsyncTask.h"
#include "FlacMetadata.h"

#include <iostream>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <fstream>
#include <iterator>
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <set>
#include <map>
#include "CommonUtils.h"
#include "MediaInformation.h"
#include "MediaTrack.h"
#include <spdlog/spdlog.h>

using namespace std;

namespace fs = std::filesystem;

namespace
{
bool samePathComponent(const fs::path& left, const fs::path& right)
{
#ifdef _WIN32
    const auto& leftValue = left.native();
    const auto& rightValue = right.native();
    return CompareStringOrdinal(
        leftValue.c_str(), static_cast<int>(leftValue.size()),
        rightValue.c_str(), static_cast<int>(rightValue.size()), TRUE) == CSTR_EQUAL;
#else
    return left == right;
#endif
}

bool isSameOrChildPath(const fs::path& candidate, const fs::path& base)
{
    auto candidatePart = candidate.begin();
    auto basePart = base.begin();
    for (; basePart != base.end(); ++basePart, ++candidatePart) {
        if (candidatePart == candidate.end()) {
            return false;
        }
        if (!samePathComponent(*candidatePart, *basePart)) {
            return false;
        }
    }
    return true;
}

struct CaseInsensitivePathLess
{
    bool operator()(const fs::path& left, const fs::path& right) const
    {
#ifdef _WIN32
        const auto& leftValue = left.native();
        const auto& rightValue = right.native();
        return CompareStringOrdinal(
            leftValue.c_str(), static_cast<int>(leftValue.size()),
            rightValue.c_str(), static_cast<int>(rightValue.size()), TRUE) == CSTR_LESS_THAN;
#else
        return left < right;
#endif
    }
};

fs::path makeTemporaryOutputPath(const fs::path& targetPath, std::uint64_t sequence)
{
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
    const auto processId = _getpid();
#else
    const auto processId = getpid();
#endif
    fs::path filename = fs::path(".afc-convert-" + std::to_string(processId) + "-" +
        std::to_string(timestamp) + "-" + std::to_string(sequence));
    filename += targetPath.extension();
    return targetPath.parent_path() / filename;
}

constexpr const char* auditFilename = "conversion-audit.json";
constexpr const char* markerFilename = ".conversion-in-progress";

bool writeTextOutput(const fs::path& target, std::string_view contents, bool overwrite)
{
    const auto temporary = makeTemporaryOutputPath(target, 0);
    std::error_code ec;
    if (fs::exists(temporary, ec) || ec) {
        spdlog::error("Cannot reserve temporary text output: {}", CommonUtils::path_to_utf8(temporary));
        return false;
    }
    const auto cleanup = [&] {
        fs::remove(temporary, ec);
        if (ec) spdlog::warn("Cannot remove incomplete text output: {}", ec.message());
    };
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    stream.flush();
    const bool written = static_cast<bool>(stream);
    stream.close();
    if (!written || stream.fail()) {
        spdlog::error("Cannot write text output: {}", CommonUtils::path_to_utf8(temporary));
        cleanup();
        return false;
    }
    std::ifstream verification(temporary, std::ios::binary);
    const std::string actual{ std::istreambuf_iterator<char>(verification), std::istreambuf_iterator<char>() };
    const bool verified = verification && actual == contents;
    verification.close();
    if (!verified) {
        spdlog::error("Text output verification failed: {}", CommonUtils::path_to_utf8(temporary));
        cleanup();
        return false;
    }
    return MediaConvertionTask::InstallOutputFile(temporary, target, overwrite);
}

struct FileAudit {
    const PlannedFileConversion* file;
    const char* kind;
    std::string status{ "not-attempted" };
    PreservationAudit::Result resources;
};

bool writeAudit(const FolderConversionPlan& plan, const fs::path& output,
    const std::vector<FileAudit>& files, const std::string& errorType)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    const auto text = [&](const char* key, const std::string& value) {
        writer.Key(key);
        writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
    };
    writer.StartObject();
    writer.Key("schemaVersion"); writer.Int(1);
    text("source", CommonUtils::path_to_utf8(plan.input));
    text("output", CommonUtils::path_to_utf8(output));
    text("target", plan.options.target == AudioTarget::Mp3 ? "MP3" : "FLAC");
    text("metadataSidecars", plan.options.sidecarFormat == MetadataSidecarFormat::Json ? "json" :
        plan.options.sidecarFormat == MetadataSidecarFormat::Binary ? "binary" : "none");
    text("status", errorType.empty() ? "completed" : "failed");
    text("errorType", errorType);
    std::size_t installedAudio = 0, installedCompanions = 0;
    for (const auto& entry : files) {
        if (entry.status == "installed") {
            if (std::string_view(entry.kind) == "audio") ++installedAudio;
            else ++installedCompanions;
        }
    }
    writer.Key("counts"); writer.StartObject();
    writer.Key("plannedAudio"); writer.Uint64(plan.audio.size());
    writer.Key("plannedCompanions"); writer.Uint64(plan.companions.size());
    writer.Key("installedAudio"); writer.Uint64(installedAudio);
    writer.Key("installedCompanions"); writer.Uint64(installedCompanions);
    writer.Key("skippedAudio"); writer.Uint64(plan.skippedAudio.size());
    writer.Key("plannedDirectories"); writer.Uint64(plan.directories.size());
    writer.Key("skippedUnchangedDirectories"); writer.Uint64(plan.skippedDirectories.size());
    writer.EndObject();
    writer.Key("skippedUnchangedDirectories"); writer.StartArray();
    for (const auto& skipped : plan.skippedDirectories) {
        writer.StartObject();
        text("source", CommonUtils::path_to_utf8(skipped.source));
        writer.Key("files"); writer.Uint64(skipped.files);
        writer.Key("audioFiles"); writer.Uint64(skipped.audioFiles);
        writer.EndObject();
    }
    writer.EndArray();
    writer.Key("skippedAudio"); writer.StartArray();
    for (const auto& skipped : plan.skippedAudio) {
        const auto path = CommonUtils::path_to_utf8(skipped.lexically_relative(plan.input));
        writer.String(path.data(), static_cast<rapidjson::SizeType>(path.size()));
    }
    writer.EndArray();
    writer.Key("files"); writer.StartArray();
    for (const auto& entry : files) {
        writer.StartObject();
        text("source", CommonUtils::path_to_utf8(entry.file->source.lexically_relative(plan.input)));
        text("output", CommonUtils::path_to_utf8(entry.file->target.lexically_relative(plan.output)));
        text("kind", entry.kind);
        text("status", entry.status);
        const bool audio = std::string_view(entry.kind) == "audio";
        if (audio && plan.options.sidecarFormat != MetadataSidecarFormat::None &&
            FlacMetadata::AppliesTo(entry.file->source, plan.options)) {
            const auto archive = FlacMetadata::SidecarPath(
                entry.file->target.lexically_relative(plan.output), plan.options.sidecarFormat);
            text("metadataArchive", CommonUtils::path_to_utf8(archive));
        }
        writer.Key("preservationChecked"); writer.Bool(!entry.resources.entries.empty());
        writer.Key("preservationPassed"); writer.Bool(!entry.resources.entries.empty() && entry.resources.ok);
        writer.Key("resources"); writer.StartArray();
        for (const auto& resource : entry.resources.entries) {
            writer.StartObject();
            text("status", PreservationAudit::StatusName(resource.status));
            text("resource", resource.resource);
            text("name", resource.name);
            text("detail", resource.detail);
            writer.Key("fatal"); writer.Bool(resource.fatal);
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();
    }
    writer.EndArray(); writer.EndObject();
    return writeTextOutput(output / auditFilename,
        std::string_view(buffer.GetString(), buffer.GetSize()), true);
}

void logAudit(const PlannedFileConversion& file, const PreservationAudit::Result& audit)
{
    std::size_t differences = 0;
    for (const auto& entry : audit.entries) {
        if (entry.status != PreservationAudit::Status::Preserved &&
            entry.status != PreservationAudit::Status::SourceOnly) {
            ++differences;
            spdlog::warn("Preservation {} [{} / {}]: {}", PreservationAudit::StatusName(entry.status),
                entry.resource, entry.name, entry.detail);
        }
    }
    spdlog::info("Installed '{}': {} preservation checks, {} differences; see {}.",
        CommonUtils::path_to_utf8(file.target), audit.entries.size(), differences, auditFilename);
}
}




int FolderConvert::ScanAudioFiles(ScanInfo& scanInfo, const std::filesystem::path& directory, bool bAsync)
{
    // Recursively process all subdirectories within the specified directory
    std::filesystem::directory_iterator directoryIT;
    try {
        if (!fs::exists(directory) || !fs::is_directory(directory)) {
            spdlog::error("Error: Directory does not exist or is not valid: {}",
                CommonUtils::path_to_utf8(directory));
            return -1;
        }

        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_directory()) {
				scanInfo.folders_count++;
            }
            else if (!entry.is_regular_file()) {
                scanInfo.not_regular_file_count++;
            }
            else if (!entry.path().has_extension()) {
                scanInfo.file_with_no_extension_count++;
            }
            else if (!MediaTrack::IsFileConvertable(entry.path())) {
                scanInfo.not_convertable_file_count++;
            }
            else
            {
                scanInfo.convertable_files_size += entry.file_size();
                scanInfo.convertable_file_count += 1;

                scanInfo.mediaList.push_back(entry);
            }
        }
    }
    catch (const fs::filesystem_error& e) {
        spdlog::error("ScanAudioFiles: Filesystem error: {}", e.what());

        return -1;
    }

    return 0;
}

int FolderConvert::ConvertAudioFolderToOutput(
    const fs::path& inputDirectory,
    const fs::path& outputDirectory,
    bool overwrite,
    ConversionOptions options)
{
    FolderConversionPlan plan;
    if (PrepareConversion(inputDirectory, outputDirectory, overwrite, options, plan) != 0) {
        return -1;
    }
    return ExecuteConversion(plan);
}

int FolderConvert::PrepareConversion(
    const fs::path& inputDirectory, const fs::path& outputDirectory, bool overwrite,
    ConversionOptions options, FolderConversionPlan& plan)
{
    plan = {};
    plan.overwrite = overwrite;
    plan.options = options;
    // Canonical paths are only for safety checks: on Windows they turn mapped drives into
    // UNC paths that may not be reachable, so files are read and written via the given paths.
    std::error_code ec;
    const fs::path inputRoot = fs::absolute(inputDirectory, ec);
    const fs::path input = ec ? fs::path{} : fs::weakly_canonical(inputDirectory, ec);
    if (ec || !fs::is_directory(inputRoot, ec) || ec) {
        spdlog::error("Input is not a readable directory: {}", CommonUtils::path_to_utf8(inputDirectory));
        return -1;
    }

    const fs::path outputRoot = fs::absolute(outputDirectory, ec);
    const fs::path output = ec ? fs::path{} : fs::weakly_canonical(outputDirectory, ec);
    if (ec) {
        spdlog::error("Cannot resolve output directory '{}': {}", CommonUtils::path_to_utf8(outputDirectory), ec.message());
        return -1;
    }
    if (isSameOrChildPath(output, input) || isSameOrChildPath(input, output)) {
        spdlog::error("Output directory must not be the input directory or one of its descendants.");
        return -1;
    }
    plan.input = inputRoot;
    plan.output = outputRoot;
    for (const auto* reserved : { auditFilename, markerFilename }) {
        if (fs::exists(inputRoot / reserved, ec) || ec ||
            (std::string_view(reserved) == markerFilename && fs::exists(outputRoot / reserved, ec)) || ec) {
            spdlog::error("Reserved conversion state filename already exists: {}", reserved);
            return -1;
        }
        const auto destination = outputRoot / reserved;
        const auto canonical = fs::weakly_canonical(destination, ec);
        if (ec || !isSameOrChildPath(canonical, output) || isSameOrChildPath(canonical, input)) {
            spdlog::error("Unsafe conversion state destination: {}", CommonUtils::path_to_utf8(destination));
            return -1;
        }
        if (fs::exists(destination, ec)) {
            if (!fs::is_regular_file(destination, ec) || ec) {
                spdlog::error("Conversion state destination is not a regular file: {}", reserved);
                return -1;
            }
            plan.existingTargets.push_back(destination);
        }
        if (ec) {
            spdlog::error("Cannot inspect conversion state destination: {}", ec.message());
            return -1;
        }
    }

    std::vector<fs::directory_entry> audio;
    std::vector<fs::directory_entry> companions;
    const bool completeTree = options.target == AudioTarget::Flac;
    // FLAC subtree classification; relative source directories, with the library root as the empty path.
    struct SubtreeCounts { std::size_t files{ 0 }, audioFiles{ 0 }, conversions{ 0 }; };
    std::vector<fs::path> directories;
    std::map<fs::path, SubtreeCounts> subtrees;
    {
        // Other audio formats are excluded only from the MP3 export.
        const std::set<std::u8string> audioExtensions{ u8".mp3", u8".aac", u8".ogg", u8".oga",
            u8".opus", u8".wma", u8".aif", u8".aiff", u8".alac", u8".mp4", u8".mka", u8".ac3",
            u8".m4b", u8".m4p", u8".mp2", u8".mp1", u8".au", u8".snd", u8".aifc", u8".caf",
            u8".tak", u8".tta", u8".shn", u8".amr" };
        const auto count = [&](const fs::path& source, bool isAudio, bool converted) {
            for (auto relative = source.lexically_relative(inputRoot).parent_path();;
                relative = relative.parent_path()) {
                auto& counts = subtrees[relative];
                ++counts.files;
                if (isAudio) ++counts.audioFiles;
                if (converted) ++counts.conversions;
                if (relative.empty()) break;
            }
        };
        try {
            for (const auto& entry : fs::recursive_directory_iterator(inputRoot)) {
                if (entry.is_directory()) {
                    if (completeTree) {
                        const auto relative = entry.path().lexically_relative(inputRoot);
                        const auto canonicalDirectory = fs::weakly_canonical(entry.path(), ec);
                        const auto expectedDirectory = input / relative;
                        if (ec || entry.is_symlink() ||
                            !isSameOrChildPath(canonicalDirectory, expectedDirectory) ||
                            !isSameOrChildPath(expectedDirectory, canonicalDirectory)) {
                            spdlog::error("Directory links cannot be classified or copied: {}",
                                CommonUtils::path_to_utf8(entry.path()));
                            return -1;
                        }
                        directories.push_back(relative);
                    }
                    continue;
                }
                if (!entry.is_regular_file()) {
                    if (completeTree) {
                        spdlog::error("Cannot preserve unsupported source filesystem entry: {}",
                            CommonUtils::path_to_utf8(entry.path()));
                        return -1;
                    }
                    continue;
                }
                const auto extension = MediaTrack::FileExtension(entry.path());
                if (MediaTrack::IsFileConvertable(entry.path())) {
                    if (!completeTree && extension != u8".flac") {
                        plan.skippedAudio.push_back(entry.path());
                    }
                    else {
                        audio.push_back(entry);
                        if (completeTree) count(entry.path(), true, true);
                    }
                }
                else {
                    const bool unsupportedAudio = audioExtensions.contains(extension);
                    if (!completeTree && unsupportedAudio) {
                        plan.skippedAudio.push_back(entry.path());
                    }
                    else {
                        companions.push_back(entry);
                        if (completeTree) count(entry.path(), unsupportedAudio, false);
                    }
                }
            }
        }
        catch (const fs::filesystem_error& error) {
            spdlog::error("Cannot enumerate files and directories: {}", error.what());
            return -1;
        }
    }
    if (audio.empty() && !completeTree) {
        spdlog::warn("No convertible audio files found under '{}'.", CommonUtils::path_to_utf8(inputRoot));
        return 0;
    }
    if (completeTree) {
        // Omit the topmost subtrees that contain audio but nothing to convert, so every written
        // folder is complete. Folders without audio stay with their written parent. A library
        // without conversions writes only its report.
        std::set<fs::path> skippedRoots;
        const auto isSkipped = [&](fs::path relative) {
            for (;; relative = relative.parent_path()) {
                if (skippedRoots.contains(relative)) return true;
                if (relative.empty()) return false;
            }
        };
        const auto root = subtrees[fs::path{}];
        if (root.conversions == 0) {
            skippedRoots.insert(fs::path{});
            plan.skippedDirectories.push_back({ fs::path("."), root.files, root.audioFiles });
        }
        else {
            // Path ordering visits each parent before its descendants.
            std::sort(directories.begin(), directories.end());
            for (const auto& directory : directories) {
                if (isSkipped(directory.parent_path())) continue;
                const auto found = subtrees.find(directory);
                if (found != subtrees.end() && found->second.conversions == 0 && found->second.audioFiles > 0) {
                    skippedRoots.insert(directory);
                    plan.skippedDirectories.push_back({ directory, found->second.files, found->second.audioFiles });
                }
            }
        }
        plan.directories.push_back(outputRoot);
        for (const auto& directory : directories) {
            if (!isSkipped(directory)) plan.directories.push_back(outputRoot / directory);
        }
        std::erase_if(companions, [&](const fs::directory_entry& entry) {
            return isSkipped(entry.path().lexically_relative(inputRoot).parent_path());
        });
    }

    plan.audio.reserve(audio.size());
    std::set<fs::path, CaseInsensitivePathLess> plannedTargets;
    plannedTargets.insert(fs::weakly_canonical(outputRoot / auditFilename, ec));
    if (!ec) plannedTargets.insert(fs::weakly_canonical(outputRoot / markerFilename, ec));
    if (ec) {
        spdlog::error("Cannot reserve conversion state destinations: {}", ec.message());
        return -1;
    }
    static std::atomic_uint64_t nextTemporaryId{ 0 };
    const auto preflight = [&](const fs::path& target, const fs::path& source) {
        const fs::path canonicalTarget = fs::weakly_canonical(target, ec);
        if (ec) {
            spdlog::error("Cannot resolve destination: {}", CommonUtils::path_to_utf8(target));
            return false;
        }
        if (!plannedTargets.insert(canonicalTarget).second) {
            plan.collisions.push_back(target);
        }
        const bool exists = fs::exists(target, ec);
        if (ec || (exists && !fs::is_regular_file(target, ec))) {
            spdlog::error("Cannot use destination (non-file or unreadable): {}", CommonUtils::path_to_utf8(target));
            return false;
        }
        if (exists) {
            plan.existingTargets.push_back(target);
        }
        if (exists && fs::equivalent(source, target, ec)) {
            spdlog::error("Refusing to overwrite a source file: {}", CommonUtils::path_to_utf8(target));
            return false;
        }
        if (ec || !isSameOrChildPath(canonicalTarget, output) ||
            isSameOrChildPath(canonicalTarget, input)) {
            spdlog::error("Unsafe destination: {}", CommonUtils::path_to_utf8(target));
            return false;
        }
        return true;
    };

    for (const auto& directory : plan.directories) {
        const auto canonical = fs::weakly_canonical(directory, ec);
        if (ec || !isSameOrChildPath(canonical, output) || isSameOrChildPath(canonical, input)) {
            spdlog::error("Unsafe output directory: {}", CommonUtils::path_to_utf8(directory));
            return -1;
        }
        if (!plannedTargets.insert(canonical).second) plan.collisions.push_back(directory);
        const bool exists = fs::exists(directory, ec);
        if (ec || (exists && !fs::is_directory(directory, ec)) || ec) {
            spdlog::error("Cannot preserve output directory: {}", CommonUtils::path_to_utf8(directory));
            return -1;
        }
    }

    for (const auto& entry : audio) {
        const fs::path source = entry.path();
        const fs::path relativePath = source.lexically_relative(inputRoot);
        if (relativePath.empty() || *relativePath.begin() == "..") {
            spdlog::error("Cannot resolve path relative to input directory: {}", CommonUtils::path_to_utf8(source));
            return -1;
        }

        fs::path target = outputRoot / relativePath;
        target.replace_extension(options.target == AudioTarget::Mp3 ? ".mp3" : ".flac");
        const fs::path canonicalSource = fs::weakly_canonical(source, ec);
        if (ec) {
            spdlog::error("Cannot resolve source path '{}': {}", CommonUtils::path_to_utf8(source), ec.message());
            return -1;
        }
        const fs::path canonicalTarget = fs::weakly_canonical(target, ec);
        if (ec) {
            spdlog::error("Cannot resolve destination path '{}': {}", CommonUtils::path_to_utf8(target), ec.message());
            return -1;
        }
        if (canonicalSource == canonicalTarget) {
            spdlog::error("Refusing to overwrite a source file: {}", CommonUtils::path_to_utf8(source));
            return -1;
        }
        if (!preflight(target, source) ||
            (options.sidecarFormat != MetadataSidecarFormat::None && FlacMetadata::AppliesTo(source, options) &&
                !preflight(FlacMetadata::SidecarPath(target, options.sidecarFormat), source))) {
            return -1;
        }

        plan.audio.push_back({ source, target, makeTemporaryOutputPath(target, ++nextTemporaryId) });
    }
    for (const auto& entry : companions) {
        const auto relative = entry.path().lexically_relative(inputRoot);
        if (relative.empty() || *relative.begin() == "..") {
            spdlog::error("Cannot resolve companion path: {}", CommonUtils::path_to_utf8(entry.path()));
            return -1;
        }
        const auto target = outputRoot / relative;
        if (!preflight(target, entry.path())) {
            return -1;
        }
        plan.companions.push_back({ entry.path(), target, makeTemporaryOutputPath(target, ++nextTemporaryId) });
    }
    return 0;
}

int FolderConvert::ExecuteConversion(const FolderConversionPlan& plan, ConversionProgress* progress)
{
    if (!plan.collisions.empty() || (!plan.overwrite && !plan.existingTargets.empty())) {
        spdlog::error("Conversion plan has {} duplicate destinations and {} existing destinations; overwrite is {}.",
            plan.collisions.size(), plan.existingTargets.size(), plan.overwrite ? "enabled" : "disabled");
        return -1;
    }
    if (progress) progress->actualOutput = plan.output;
    if (plan.audio.empty() && plan.directories.empty()) return 0;
    std::vector<FileAudit> files;
    files.reserve(plan.audio.size() + plan.companions.size());
    for (const auto& file : plan.audio) files.push_back({ &file, "audio" });
    for (const auto& file : plan.companions) files.push_back({ &file, "companion" });
    std::error_code ec;
    fs::create_directories(plan.output.parent_path(), ec);
    if (ec) {
        spdlog::error("Cannot create output parent: {}", ec.message());
        return -1;
    }
    // create_directory's return value establishes ownership at execution, not preview time.
    const bool owned = fs::create_directory(plan.output, ec);
    if (ec) {
        spdlog::error("Cannot create library output: {}", ec.message());
        return -1;
    }
    fs::path actualOutput = plan.output;
    const auto finish = [&](std::string errorType) {
        bool reportOk = writeAudit(plan, actualOutput, files, errorType);
        if (!reportOk) errorType = "IO";
        if (errorType.empty() && reportOk) {
            if (fs::remove(actualOutput / markerFilename, ec) && !ec) return 0;
            spdlog::error("Cannot clear conversion-in-progress marker: {}", ec.message());
            errorType = "IO";
            reportOk = writeAudit(plan, actualOutput, files, errorType);
        }
        if (!errorType.empty() && owned) {
            const std::string prefix = errorType == "INCOMPLETE" ? "INCOMPLETE_" : "ERROR_" + errorType + "_";
            fs::path base = actualOutput.parent_path() / fs::path(prefix);
            base += actualOutput.filename().native();
            fs::path renamed = base;
            for (std::size_t suffix = 1; fs::exists(renamed, ec) && !ec; ++suffix) {
                renamed = base; renamed += "-" + std::to_string(suffix);
            }
            if (!ec) fs::rename(actualOutput, renamed, ec);
            if (ec) {
                spdlog::error("Cannot mark failed output folder '{}': {}",
                    CommonUtils::path_to_utf8(actualOutput), ec.message());
            } else {
                actualOutput = renamed;
                reportOk = writeAudit(plan, actualOutput, files, errorType);
                spdlog::error("Incomplete output retained at '{}'.", CommonUtils::path_to_utf8(actualOutput));
            }
        }
        if (progress) progress->actualOutput = actualOutput;
        const bool markerPresent = fs::exists(actualOutput / markerFilename, ec);
        spdlog::error("Conversion failed ({}); output '{}'; in-progress marker {}.",
            errorType, CommonUtils::path_to_utf8(actualOutput),
            markerPresent && !ec ? "retained" : "could not be confirmed");
        return -1;
    };
    if (!fs::create_directory(plan.output / markerFilename, ec)) {
        spdlog::error("Cannot reserve in-progress marker (possibly another active run): {}", ec.message());
        if (ec) return finish("IO");
        return -1;
    }
    std::size_t current = 0;
    for (const auto& skipped : plan.skippedDirectories) {
        spdlog::info("Skipped unchanged folder (audio, no conversions): {} ({} files)",
            CommonUtils::path_to_utf8(skipped.source), skipped.files);
    }
    try {
    for (const auto& directory : plan.directories) {
        fs::create_directories(directory, ec);
        if (ec) {
            spdlog::error("Cannot preserve source directory '{}': {}",
                CommonUtils::path_to_utf8(directory), ec.message());
            return finish("IO");
        }
    }
    for (const auto& conversion : plan.audio) {
        auto& record = files[current++];
        record.status = "failed";
        fs::create_directories(conversion.target.parent_path(), ec);
        if (ec) {
            spdlog::error("Cannot create output directory: {}", ec.message());
            return finish("IO");
        }
        auto task = std::make_shared<MediaConvertionTask>(
            conversion.source, conversion.target, conversion.temporary, plan.overwrite, plan.options);
        const int run = task->Run();
        record.resources = task->GetPreservationAudit();
        if (run != 0) return finish(task->GetFailureType());
        record.status = "audited";
        if (task->PostRun() != 0) {
            record.status = "failed";
            return finish(task->GetFailureType());
        }
        record.status = "installed";
        logAudit(conversion, record.resources);
        if (progress) {
            ++progress->converted;
        }
    }

    for (const auto& copy : plan.companions) {
        auto& record = files[current++];
        record.status = "failed";
        fs::create_directories(copy.target.parent_path(), ec);
        if (ec) {
            spdlog::error("Cannot create companion directory: {}", ec.message());
            return finish("IO");
        }
        if (fs::exists(copy.temporary, ec) || ec) {
            spdlog::error("Cannot reserve companion temporary path: {}", CommonUtils::path_to_utf8(copy.temporary));
            return finish("IO");
        }
        if (!fs::copy_file(copy.source, copy.temporary, fs::copy_options::none, ec)) {
            spdlog::error("Cannot copy companion '{}': {}", CommonUtils::path_to_utf8(copy.source), ec.message());
            std::error_code cleanupError;
            fs::remove(copy.temporary, cleanupError);
            if (cleanupError) {
                spdlog::warn("Cannot clean temporary companion '{}': {}",
                    CommonUtils::path_to_utf8(copy.temporary), cleanupError.message());
            }
            return finish("COPY");
        }
        if (!PreservationAudit::VerifyCopy(copy.source, copy.temporary)) {
            fs::remove(copy.temporary, ec);
            if (ec) spdlog::warn("Cannot remove unverified companion: {}", ec.message());
            return finish("COPY");
        }
        if (!MediaConvertionTask::InstallOutputFile(copy.temporary, copy.target, plan.overwrite)) {
            spdlog::error("Companion copy remains recoverable at '{}'.", CommonUtils::path_to_utf8(copy.temporary));
            return finish("IO");
        }
        record.status = "installed";
        record.resources.entries.push_back({ PreservationAudit::Status::Preserved,
            "companion", "bytes", "Source and temporary copy matched byte-for-byte before installation.", false });
        if (progress) {
            ++progress->copied;
        }
        spdlog::info("Copied {}file unchanged: {}",
            plan.options.target == AudioTarget::Mp3 ? "companion " : "",
            CommonUtils::path_to_utf8(copy.target));
    }

    }
    catch (const fs::filesystem_error& error) {
        spdlog::error("Conversion filesystem failure: {}", error.what());
        return finish("IO");
    }
    catch (const std::exception& error) {
        spdlog::error("Conversion interrupted: {}", error.what());
        return finish("INCOMPLETE");
    }
    for (auto& record : files) {
        if (record.status != "installed" || !fs::is_regular_file(record.file->target, ec) || ec) {
            spdlog::error("Expected installed output is missing: {}", CommonUtils::path_to_utf8(record.file->target));
            return finish("INCOMPLETE");
        }
        if (std::string_view(record.kind) == "audio" &&
            plan.options.sidecarFormat != MetadataSidecarFormat::None &&
            FlacMetadata::AppliesTo(record.file->source, plan.options)) {
            const auto sidecar = FlacMetadata::SidecarPath(record.file->target, plan.options.sidecarFormat);
            if (!FlacMetadata::Verify(record.file->source, sidecar, plan.options.sidecarFormat)) {
                record.resources.ok = false;
                record.resources.entries.push_back({ PreservationAudit::Status::MissingOrChanged,
                    "metadata-archive", "installed archive", "Final installed archive verification failed.", true });
                return finish("PRESERVATION");
            }
        }
    }
    for (const auto& directory : plan.directories) {
        if (!fs::is_directory(directory, ec) || ec) {
            spdlog::error("Expected output directory is missing: {}", CommonUtils::path_to_utf8(directory));
            return finish("INCOMPLETE");
        }
    }
    return finish({});
}


int FolderConvert::ConverAudioFolder(const std::filesystem::path& directory, const ScanInfo scanInfo, bool bAsync)
{
    std::vector<std::shared_ptr<MediaConvertionTask>> tasksVector;

    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        if (entry.is_directory()) {
            int ret = ConverAudioFolder(entry.path(), scanInfo, bAsync);
            if (ret == -1) {
                spdlog::error("***ERROR*** returned from ConverAudioFiles");
                return -1;
            }
        }
    }


    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {

        if (entry.is_regular_file() && entry.path().has_extension())
        {
            if (entry.path().has_extension() && MediaTrack::IsFileConvertable(entry.path())) {

                fs::path targetPath = entry.path();
                targetPath.replace_extension(GetTargetFileType());

                fs::path targetTMPPath = entry.path();
                fs::path targetTMPFileName = targetTMPPath.stem() += fs::path{ "_TMP" };
                targetTMPPath.replace_filename(targetTMPFileName);
                targetTMPPath += GetTargetFileType();

                //     fs::path targetTMPPath = _TMPDirectory += entry.path().stem() += fs::path{ "_TMP" } += fs::path{ _TargetFileType };

                std::shared_ptr<MediaConvertionTask> pTask = bAsync ? std::make_shared<MediaConvertionAsyncTask>(entry.path(), targetPath, targetTMPPath) : std::make_shared<MediaConvertionTask>(entry.path(), targetPath, targetTMPPath);
                tasksVector.push_back(pTask);
            }
            else
            {
                
                spdlog::warn("---Skipping: {}", CommonUtils::utf8string_to_string(entry.path().u8string()));
            }
        }
        else {
            if (entry.is_directory()) {
				//spdlog::info("Skipped directory: {}", CommonUtils::utf8string_to_string(entry.path().u8string()));
            }
            else
            {
                spdlog::error("***Unknown entry: {}", CommonUtils::utf8string_to_string(entry.path().u8string()));
            }
        }
    }

    //process all files in current directory
    int processStatus{ 0 };
    std::for_each(tasksVector.begin(), tasksVector.end(), [](auto& f) {
        f->Run();
        if (f->GetStatus() == -1)

        {



        }

        });

    std::for_each(tasksVector.begin(), tasksVector.end(), [](auto& f) { f->PostRun(); });


    for (auto& item : tasksVector)
    {
        if (item->GetStatus() != 0)
        {
            spdlog::error("***Processing item error found: {}", item->GetStatus());
            return -1;

        }
    }
    //auto found = std::find_if(tasksVector.begin(), tasksVector.end(), [](auto& f) { return f->GetStatus() == -1; });
    //if (found != tasksVector.end()) {
    //    std::cout << "***STOP*** error found: " << (*found)->GetStatus() << std::endl;
    //    return -1;
    //}


    return 0;
}


















int FolderConvert::ConverAudioFolder2(const std::filesystem::path& directory, const ScanInfo scanInfo, bool bAsync)
{
    std::vector<std::shared_ptr<MediaConvertionTask>> tasksVector;

    std::vector<MediaConvertionTask> AlbumConvertTask;

    std::vector<std::shared_ptr<std::vector<MediaConvertionTask>>> tasksVector2;




    for (auto& mediaFile : scanInfo.mediaList) {
        fs::path targetPath = mediaFile.path();
        targetPath.replace_extension(GetTargetFileType());

        fs::path targetTMPPath = mediaFile.path();
        fs::path targetTMPFileName = targetTMPPath.stem() += fs::path{ "_TMP" };
        targetTMPPath.replace_filename(targetTMPFileName);
        targetTMPPath += GetTargetFileType();

        //     fs::path targetTMPPath = _TMPDirectory += entry.path().stem() += fs::path{ "_TMP" } += fs::path{ _TargetFileType };

        std::shared_ptr<MediaConvertionTask> pTask = bAsync ? std::make_shared<MediaConvertionAsyncTask>(mediaFile.path(), targetPath, targetTMPPath) : std::make_shared<MediaConvertionTask>(mediaFile.path(), targetPath, targetTMPPath);
        tasksVector.push_back(pTask);
    }



    //process all files in current directory
    int processStatus{ 0 };
	for (auto& task : tasksVector) {
		task->Run();
		if (task->GetStatus() == -1) {
			spdlog::error("***ERROR*** Media convert error: {}", task->GetStatus());
		}
	}
	for (auto& task : tasksVector) {
		task->PostRun();
		if (task->GetStatus() != 0)
		{
			spdlog::error("***STOP*** error found: {}", task->GetStatus());
		}
	}

    for (auto& item : tasksVector)
    {
        if (item->GetStatus() != 0)
        {
            spdlog::error("***STOP***2 error found: {}", item->GetStatus());
            return -1;

        }
    }


    //auto found = std::find_if(tasksVector.begin(), tasksVector.end(), [](auto& f) { return f->GetStatus() == -1; });
    //if (found != tasksVector.end()) {
    //    std::cout << "***STOP*** error found: " << (*found)->GetStatus() << std::endl;
    //    return -1;
    //}


    return 0;
}
