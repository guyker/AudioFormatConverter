// FlacConvert.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <atomic>
#include <cstdint>
#include <cctype>
#include <filesystem>
#include <array>
#include <algorithm>
#include <mutex>
#include <future>

#include <chrono>
#include <thread>


#include <iomanip>
#include <iostream>


#include <cassert>
#include <exception>

#include <sstream>
#include <string>
#include <set>
#include <system_error>
#include <any>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>


#include "AlbumCollection.h"
#include "CommandLine.h"
#include "FolderConvert.h"

#include "MediaInformation.h"

#include "MediaConvertionTask.h"
#include "MediaConvertionAsyncTask.h"
#include "AppSettings.h"
#include "CommonUtils.h"


#include "PlatformUtils.h"

#include "FFmpeg.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;


enum class ConvertActionEnum { NullEnum, ConverEnum, CreateJSONEnum, CreateDBFromFolderEnum, ProcessJSONEnum, PopulateJsonToDBEnum, ConvertMp3Enum, ExitEnum };


namespace
{
std::string pathToUtf8(const fs::path& path)
{
    return CommonUtils::path_to_utf8(path);
}

fs::path pathFromUtf8(const std::string& path)
{
    return CommonUtils::path_from_utf8(path);
}

fs::path uniqueSiblingPath(const fs::path& path, const char* marker)
{
    static std::atomic_uint64_t sequence{ 0 };
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
    const auto processId = _getpid();
#else
    const auto processId = getpid();
#endif
    fs::path filename = ".afc";
    filename += fs::path(marker);
    filename += fs::path(std::to_string(processId) + "-" + std::to_string(timestamp) +
        "-" + std::to_string(++sequence));
    return path.parent_path() / filename;
}

bool isSameOrChildPath(const fs::path& candidate, const fs::path& base)
{
    auto candidatePart = candidate.begin();
    auto basePart = base.begin();
    for (; basePart != base.end(); ++basePart, ++candidatePart) {
        if (candidatePart == candidate.end()) {
            return false;
        }
#ifdef _WIN32
        const auto& candidateValue = candidatePart->native();
        const auto& baseValue = basePart->native();
        if (CompareStringOrdinal(
                candidateValue.c_str(), static_cast<int>(candidateValue.size()),
                baseValue.c_str(), static_cast<int>(baseValue.size()), TRUE) != CSTR_EQUAL) {
#else
        if (*candidatePart != *basePart) {
#endif
            return false;
        }
    }
    return true;
}

std::string directoryNameUtf8(const fs::path& directory)
{
    const auto name = directory.lexically_normal().filename();
    return name.empty() ? "library" : pathToUtf8(name);
}

bool isGeneratedJsonOutput(const fs::directory_entry& entry, std::error_code& ec)
{
    if (!entry.is_regular_file(ec) || ec) {
        return false;
    }
    const auto filename = CommonUtils::ToLower(entry.path().filename().wstring());
    return filename.starts_with(L"media_out_") &&
        CommonUtils::ToLower(entry.path().extension().wstring()) == L".json";
}

bool installGeneratedFiles(const fs::path& stagedDirectory, const fs::path& targetDirectory)
{
    std::error_code ec;
    const bool hadTargetDirectory = fs::exists(targetDirectory, ec);
    if (ec) {
        spdlog::error("Cannot inspect existing output '{}': {}", pathToUtf8(targetDirectory), ec.message());
        return false;
    }
    if (hadTargetDirectory) {
        const bool isDirectory = fs::is_directory(targetDirectory, ec);
        if (ec) {
            spdlog::error("Cannot inspect existing output '{}': {}", pathToUtf8(targetDirectory), ec.message());
            return false;
        }
        if (!isDirectory) {
            spdlog::error("Refusing to replace a non-directory output path '{}'.", pathToUtf8(targetDirectory));
            return false;
        }
    }
    fs::create_directories(targetDirectory, ec);
    if (ec) {
        spdlog::error("Cannot create output directory '{}': {}", pathToUtf8(targetDirectory), ec.message());
        return false;
    }

    auto removeCreatedDirectory = [&]() {
        if (!hadTargetDirectory) {
            std::error_code cleanupError;
            fs::remove(targetDirectory, cleanupError);
            if (cleanupError) {
                spdlog::warn("Cannot remove incomplete output directory '{}': {}",
                    pathToUtf8(targetDirectory), cleanupError.message());
            }
        }
    };

    const fs::directory_iterator end;
    auto collectGeneratedFiles = [&](const fs::path& directory, std::vector<fs::path>& files) {
        fs::directory_iterator iterator(directory, ec);
        for (; !ec && iterator != end; iterator.increment(ec)) {
            if (isGeneratedJsonOutput(*iterator, ec)) {
                files.push_back(iterator->path());
            }
            if (ec) {
                return false;
            }
        }
        return !ec;
    };

    std::vector<fs::path> previousCandidates;
    if (!collectGeneratedFiles(targetDirectory, previousCandidates)) {
        spdlog::error("Cannot enumerate previous JSON outputs in '{}': {}", pathToUtf8(targetDirectory), ec.message());
        removeCreatedDirectory();
        return false;
    }

    std::vector<fs::path> stagedFiles;
    if (!collectGeneratedFiles(stagedDirectory, stagedFiles)) {
        spdlog::error("Cannot enumerate staged scan outputs in '{}': {}", pathToUtf8(stagedDirectory), ec.message());
        removeCreatedDirectory();
        return false;
    }
    if (stagedFiles.empty()) {
        spdlog::error("No generated JSON files are available to install from '{}'.", pathToUtf8(stagedDirectory));
        removeCreatedDirectory();
        return false;
    }

    const fs::path backupDirectory = uniqueSiblingPath(targetDirectory, ".backup-");
    fs::create_directory(backupDirectory, ec);
    if (ec) {
        spdlog::error("Cannot create output backup directory '{}': {}", pathToUtf8(backupDirectory), ec.message());
        removeCreatedDirectory();
        return false;
    }

    std::vector<fs::path> previousFiles;
    std::vector<fs::path> installedFiles;
    auto rollback = [&]() {
        bool rollbackFailed = false;
        for (const auto& installed : installedFiles) {
            std::error_code rollbackError;
            fs::remove(installed, rollbackError);
            if (rollbackError) {
                spdlog::error("Cannot remove incomplete output '{}': {}", pathToUtf8(installed), rollbackError.message());
                rollbackFailed = true;
            }
        }
        for (const auto& previous : previousFiles) {
            std::error_code rollbackError;
            fs::rename(backupDirectory / previous.filename(), previous, rollbackError);
            if (rollbackError) {
                spdlog::error("Cannot restore previous output '{}': {}", pathToUtf8(previous), rollbackError.message());
                rollbackFailed = true;
            }
        }
        if (rollbackFailed) {
            spdlog::error("Preserving output backup for recovery at '{}'.", pathToUtf8(backupDirectory));
        }
        else {
            std::error_code cleanupError;
            fs::remove_all(backupDirectory, cleanupError);
            if (cleanupError) {
                spdlog::warn("Cannot clean output backup '{}': {}", pathToUtf8(backupDirectory), cleanupError.message());
            }
        }
        removeCreatedDirectory();
    };

    for (const auto& previous : previousCandidates) {
        fs::rename(previous, backupDirectory / previous.filename(), ec);
        if (ec) {
            spdlog::error("Cannot preserve previous JSON output '{}': {}", pathToUtf8(previous), ec.message());
            rollback();
            return false;
        }
        previousFiles.push_back(previous);
    }

    for (const auto& stagedFile : stagedFiles) {
        const fs::path installed = targetDirectory / stagedFile.filename();
        fs::rename(stagedFile, installed, ec);
        if (ec) {
            spdlog::error("Cannot install generated JSON output '{}': {}", pathToUtf8(installed), ec.message());
            rollback();
            return false;
        }
        installedFiles.push_back(installed);
    }

    fs::remove_all(backupDirectory, ec);
    if (ec) {
        spdlog::warn("New JSON output installed, but old output cleanup failed for '{}': {}",
            pathToUtf8(backupDirectory), ec.message());
    }
    return true;
}

std::vector<fs::path> findJsonFiles(const fs::path& directory)
{
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(directory, ec) || ec) {
        return files;
    }

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file() || CommonUtils::ToLower(entry.path().extension().wstring()) != L".json") {
            continue;
        }
        const auto filename = CommonUtils::ToLower(entry.path().filename().wstring());
        if (filename.starts_with(L"media_out_") &&
            !filename.ends_with(L"_error.json")) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<fs::path> findConfiguredJsonFiles(
    const std::vector<MediaDirectoryElement>& mediaList,
    const fs::path& outputRoot)
{
    std::vector<fs::path> files;
    for (const auto& mediaEntry : mediaList) {
        auto generator = mediaEntry.getMediaJsonPathCo(outputRoot);
        while (generator.resume()) {
            files.push_back(generator.value());
        }
    }
    return files;
}
}

namespace
{
enum class MenuConfirmation { Run, Back, Exit, Error };
constexpr int ReturnToMenu = -2;

struct ManualOperationSummary {
    const char* unit{ "work items" };
    std::size_t completed{ 0 };
    ConversionProgress conversion;
    std::vector<fs::path> outputs;
    const char* outputDescription{ nullptr };
    bool started{ false };
    std::chrono::steady_clock::time_point startTime;

    void Start()
    {
        started = true;
        startTime = std::chrono::steady_clock::now();
    }

    void Print(int result) const
    {
        const auto totalCompleted = completed + conversion.converted;
        const char* status = result == 0 ? "Completed" :
            totalCompleted != 0 || conversion.copied != 0 ? "Partially completed" : "Failed";
        const auto elapsed = started ? std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count() : 0;
        std::cout << "\nCompletion summary: " << status
            << "\n  Completed: " << totalCompleted << ' ' << unit
            << "; copied: " << conversion.copied << " companion files"
            << "; failed: " << (result == 0 ? 0 : 1)
            << (started ? " work items" : " preflight checks")
            << "\n  Elapsed: " << elapsed << " ms\n";
        for (const auto& output : outputs) {
            std::cout << "  Output location: " << pathToUtf8(output) << '\n';
        }
        if (outputDescription) {
            std::cout << "  Output: " << outputDescription << '\n';
        }
        if (!started) {
            std::cout << "  No action was started.\n";
        }
        else if (result != 0) {
            std::cout << "  Stopped at the first error; unattempted work is not counted as failed.\n"
                "  Failure does not imply rollback of the whole operation; inspect outputs and errors above.\n";
        }
    }
};

std::string trimMenuResponse(const std::string& response)
{
    const auto first = response.find_first_not_of(" \t\r");
    const auto last = response.find_last_not_of(" \t\r");
    return first == std::string::npos ? std::string{} : response.substr(first, last - first + 1);
}

MenuConfirmation ConfirmManualAction(const char* runToken, const char* cancellationMessage)
{
    std::cout << "\nPreview only; no output has been written. File state may change before execution.\n"
        << "Type " << runToken << " to run, BACK for the menu, EXIT to quit,\n"
        "or press Enter to cancel [Cancel]: " << std::flush;
    std::string response;
    if (!std::getline(std::cin, response)) {
        if (!std::cin.eof()) {
            spdlog::error("Cannot read operation preview confirmation.");
            return MenuConfirmation::Error;
        }
    }
    else {
        response = trimMenuResponse(response);
        if (response == runToken) {
            return MenuConfirmation::Run;
        }
        if (response == "BACK") {
            std::cout << "Back to menu. No action was started.\n";
            return MenuConfirmation::Back;
        }
    }
    std::cout << cancellationMessage << '\n';
    return MenuConfirmation::Exit;
}

MenuConfirmation ConfirmPreview()
{
    return ConfirmManualAction("RUN", "Cancelled. No action was started.");
}

int cancelledMenuResult(MenuConfirmation confirmation)
{
    return confirmation == MenuConfirmation::Back ? ReturnToMenu :
        confirmation == MenuConfirmation::Error ? 1 : 0;
}

MenuConfirmation ReadManualSetting(const char* prompt, std::string& response)
{
    std::cout << prompt << std::flush;
    if (!std::getline(std::cin, response)) {
        if (!std::cin.eof()) {
            spdlog::error("Cannot read MP3 conversion settings.");
            return MenuConfirmation::Error;
        }
        std::cout << "Cancelled. No action was started.\n";
        return MenuConfirmation::Exit;
    }
    response = trimMenuResponse(response);
    std::transform(response.begin(), response.end(), response.begin(),
        [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
    if (response == "BACK") {
        std::cout << "Back to menu. No action was started.\n";
        return MenuConfirmation::Back;
    }
    if (response == "EXIT") {
        std::cout << "Cancelled. No action was started.\n";
        return MenuConfirmation::Exit;
    }
    return MenuConfirmation::Run;
}

MenuConfirmation ConfigureManualMp3(ConversionOptions& options, bool& overwrite)
{
    std::cout << "\nMP3 settings: Enter keeps the defaults; BACK returns to the menu; EXIT quits.\n";
    for (;;) {
        std::string response;
        const auto choice = ReadManualSetting("VBR quality [0] (0 = highest, 9 = smallest): ", response);
        if (choice != MenuConfirmation::Run) {
            return choice;
        }
        if (response.empty()) {
            break;
        }
        if (response.size() == 1 && response[0] >= '0' && response[0] <= '9') {
            options.mp3Quality = response[0] - '0';
            break;
        }
        std::cout << "Invalid quality. Enter one number from 0 to 9.\n";
    }
    const auto readBoolean = [](const char* prompt, bool& value) {
        for (;;) {
            std::string response;
            const auto choice = ReadManualSetting(prompt, response);
            if (choice != MenuConfirmation::Run) {
                return choice;
            }
            if (response.empty() || response == "NO") {
                value = false;
                return MenuConfirmation::Run;
            }
            if (response == "YES") {
                value = true;
                return MenuConfirmation::Run;
            }
            std::cout << "Invalid selection. Enter YES or NO.\n";
        }
    };
    const auto downmix = readBoolean(
        "Allow multichannel audio to be downmixed to stereo [NO] (YES/NO): ", options.downmixToStereo);
    if (downmix != MenuConfirmation::Run) {
        return downmix;
    }
    return readBoolean(
        "Overwrite existing MP3, metadata sidecars and companions [NO] (YES/NO): ", overwrite);
}

fs::path absoluteDisplayPath(const fs::path& path)
{
    return fs::absolute(path).lexically_normal();
}

void ShowConversionPreview(const FolderConversionPlan& plan)
{
    std::cout << "\nOperation preview: "
        << (plan.options.target == AudioTarget::Mp3 ? "FLAC to MP3" : "FLAC conversion") << '\n'
        << "  Input: " << pathToUtf8(plan.input) << '\n'
        << "  Output: " << pathToUtf8(plan.output) << '\n'
        << "  Audio files: " << plan.audio.size()
        << "; companions: " << plan.companions.size()
        << "; metadata sidecars: " << (plan.options.target == AudioTarget::Mp3 ? plan.audio.size() : 0) << '\n'
        << "  Existing destinations: " << plan.existingTargets.size()
        << "; duplicate destinations: " << plan.collisions.size() << '\n'
        << "  Overwrite: " << (plan.overwrite ? "enabled" : "disabled") << '\n';
    for (const auto& target : plan.existingTargets) {
        std::cout << "    Existing: " << pathToUtf8(target) << '\n';
    }
    for (const auto& target : plan.collisions) {
        std::cout << "    COLLISION: " << pathToUtf8(target) << '\n';
    }
}
}

MenuConfirmation ConfirmSourceReplacement(const std::vector<MediaDirectoryElement>& libraries);

int ConvertFLACToFLAC(const std::vector<MediaDirectoryElement>& libraries, ManualOperationSummary& summary)
{
    summary.unit = "audio files";
    if (libraries.empty()) {
        spdlog::error("No active media libraries configured for FLAC conversion.");
        return 1;
    }
    std::vector<PlannedFileConversion> files;
    const auto pathLess = [](const fs::path& left, const fs::path& right) {
#ifdef _WIN32
        const auto& a = left.native();
        const auto& b = right.native();
        return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
            b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
#else
        return left < right;
#endif
    };
    std::set<fs::path, decltype(pathLess)> targetKeys(pathLess);
    std::size_t collisions = 0;
    std::cout << "\nOperation preview: in-place FLAC conversion\n"
        "  Quality: 16-bit / 44.1 kHz, triangular dithering; embedded artwork omitted.\n"
        "  Overwrite: enabled; existing FLAC sources may be replaced.\n";
    for (const auto& library : libraries) {
        const auto sourceRoot = absoluteDisplayPath(pathFromUtf8(library.mediaPath));
        summary.outputs.push_back(sourceRoot);
        ScanInfo scan{};
        if (FolderConvert::ScanAudioFiles(scan, sourceRoot) != 0) {
            return 1;
        }
        std::size_t existing = 0;
        for (const auto& entry : scan.mediaList) {
            auto target = entry.path();
            target.replace_extension(".flac");
            std::error_code ec;
            const auto key = fs::weakly_canonical(target, ec);
            if (ec) {
                spdlog::error("Cannot resolve destination: {}", ec.message());
                return 1;
            }
            const bool targetExists = fs::exists(target, ec);
            if (ec || (targetExists && !fs::is_regular_file(target, ec)) || ec) {
                spdlog::error("Cannot use FLAC destination: {}", pathToUtf8(target));
                return 1;
            }
            existing += targetExists ? 1 : 0;
            if (!targetKeys.insert(key).second) {
                ++collisions;
                std::cout << "    COLLISION: " << pathToUtf8(target) << '\n';
            }
            auto temporary = uniqueSiblingPath(target, ".convert-");
            temporary += ".flac";
            files.push_back({ entry.path(), target, temporary });
        }
        std::cout << "  Input/output: " << pathToUtf8(sourceRoot)
            << "\n  Audio files: " << scan.mediaList.size()
            << "; existing destinations to replace: " << existing << '\n';
    }
    std::cout << "  Total audio files: " << files.size() << "; duplicate destinations: " << collisions
        << "\n  Source quality is verified during conversion (minimum 16-bit / 44.1 kHz).\n";
    if (collisions != 0) {
        spdlog::error("Conflicting FLAC destinations; no conversion was started.");
        return 1;
    }
    const auto confirmation = ConfirmSourceReplacement(libraries);
    if (confirmation != MenuConfirmation::Run) {
        return cancelledMenuResult(confirmation);
    }
    summary.Start();
    for (const auto& file : files) {
        MediaConvertionTask task(file.source, file.target, file.temporary, true);
        if (task.Run() != 0 || task.PostRun() != 0) {
            return 1;
        }
        ++summary.conversion.converted;
    }
    return 0;
}

int ConvertFLACToMP3(const std::vector<MediaDirectoryElement>& mediaDirectoryList, const fs::path& outputRoot,
    ManualOperationSummary& summary)
{
    summary.unit = "audio files";
    if (mediaDirectoryList.empty()) {
        spdlog::error("No active media libraries configured for FLAC to MP3 conversion.");
        return 1;
    }
    ConversionOptions options{ AudioTarget::Mp3, 0, false };
    bool overwrite = false;
    const auto settingsChoice = ConfigureManualMp3(options, overwrite);
    if (settingsChoice != MenuConfirmation::Run) {
        return cancelledMenuResult(settingsChoice);
    }
    struct LibraryConversion {
        fs::path source;
        fs::path target;
        fs::path canonicalSource;
        fs::path canonicalTarget;
    };
    std::vector<LibraryConversion> conversions;
    for (const auto& mediaEntry : mediaDirectoryList) {
        const fs::path source = pathFromUtf8(mediaEntry.mediaPath);
        const fs::path name = pathFromUtf8(
            mediaEntry.mediaName && !mediaEntry.mediaName->empty()
                ? *mediaEntry.mediaName : directoryNameUtf8(source));
        if (name.empty() || name == "." || name == ".." || name.has_root_path() || name.has_parent_path()) {
            spdlog::error("Library name must be a single directory name: '{}'.", pathToUtf8(name));
            return 1;
        }
        std::error_code ec;
        const fs::path canonicalSource = fs::weakly_canonical(source, ec);
        if (ec || !fs::is_directory(canonicalSource, ec) || ec) {
            spdlog::error("Library path is not a readable directory: '{}'.", pathToUtf8(source));
            return 1;
        }
        const fs::path target = outputRoot / "MP3" / name;
        summary.outputs.push_back(absoluteDisplayPath(target));
        const fs::path canonicalTarget = fs::weakly_canonical(target, ec);
        if (ec) {
            spdlog::error("Cannot resolve MP3 output directory: {}", ec.message());
            return 1;
        }
        // Convert via the configured paths; canonical (possibly UNC) paths are only for overlap checks.
        conversions.push_back({ source, target, canonicalSource, canonicalTarget });
    }
    for (std::size_t i = 0; i < conversions.size(); ++i) {
        for (std::size_t j = 0; j < conversions.size(); ++j) {
            if (isSameOrChildPath(conversions[i].canonicalTarget, conversions[j].canonicalSource) ||
                isSameOrChildPath(conversions[j].canonicalSource, conversions[i].canonicalTarget) ||
                (i != j && isSameOrChildPath(conversions[i].canonicalTarget, conversions[j].canonicalTarget))) {
                spdlog::error("MP3 output overlaps a source library or another output: '{}'.",
                    pathToUtf8(conversions[i].target));
                return 1;
            }
        }
    }
    spdlog::info("FLAC to MP3: VBR quality {}, overwrite {}, multichannel stereo downmix {}.",
        options.mp3Quality, overwrite ? "enabled" : "disabled",
        options.downmixToStereo ? "enabled" : "disabled");
    std::vector<FolderConversionPlan> plans;
    bool blocked = false;
    for (const auto& conversion : conversions) {
        FolderConversionPlan plan;
        if (FolderConvert::PrepareConversion(conversion.source, conversion.target, overwrite, options, plan) != 0) {
            return 1;
        }
        ShowConversionPreview(plan);
        blocked = blocked || !plan.collisions.empty() || (!overwrite && !plan.existingTargets.empty());
        plans.push_back(std::move(plan));
    }
    if (blocked) {
        spdlog::error("MP3 destination conflicts; no conversion was started.");
        return 1;
    }
    std::cout << "  Quality: LAME VBR " << options.mp3Quality << "; "
        << (options.downmixToStereo ? "multichannel stereo downmix enabled" : "mono/stereo only")
        << "; 44.1/48 kHz retained, other accepted rates -> 48 kHz.\n"
        "  Source quality/channel properties are verified during conversion.\n";
    const auto confirmation = ConfirmPreview();
    if (confirmation != MenuConfirmation::Run) {
        return cancelledMenuResult(confirmation);
    }
    summary.Start();
    for (const auto& plan : plans) {
        if (FolderConvert::ExecuteConversion(plan, &summary.conversion) != 0) {
            return 1;
        }
    }
    return 0;
}

int ScanFolderAndCreateJSON(std::vector<MediaDirectoryElement> mediaDirectoryList, bool bIncludeMetadata,
    const fs::path& outputRoot, bool preview = false, ManualOperationSummary* summary = nullptr)
{
    if (summary) {
        summary->unit = "album JSON files";
    }
    std::vector<std::vector<MediaAlbumListPtr>> preparedAlbums;
    if (preview) {
        if (mediaDirectoryList.empty()) {
            spdlog::error("No active media libraries configured for scanning.");
            return 1;
        }
        std::vector<fs::path> targets;
        for (auto& library : mediaDirectoryList) {
            const auto source = absoluteDisplayPath(pathFromUtf8(library.mediaPath));
            if (!library.mediaName || library.mediaName->empty()) {
                library.mediaName = directoryNameUtf8(source);
            }
            const auto name = pathFromUtf8(*library.mediaName);
            if (name.empty() || name == "." || name == ".." || name.has_root_path() || name.has_parent_path()) {
                spdlog::error("Library name must be a single directory name: '{}'.", *library.mediaName);
                return 1;
            }
            const auto target = absoluteDisplayPath(outputRoot / name);
            if (summary) {
                summary->outputs.push_back(target);
            }
            std::error_code ec;
            const auto key = fs::weakly_canonical(target, ec);
            if (ec) {
                spdlog::error("Cannot resolve scan destination: {}", ec.message());
                return 1;
            }
            for (const auto& other : mediaDirectoryList) {
                const auto sourceKey = fs::weakly_canonical(pathFromUtf8(other.mediaPath), ec);
                if (ec || isSameOrChildPath(key, sourceKey) || isSameOrChildPath(sourceKey, key)) {
                    spdlog::error("Scan destination overlaps an active source library: '{}'.", pathToUtf8(target));
                    return 1;
                }
            }
            if (std::any_of(targets.begin(), targets.end(), [&](const fs::path& other) {
                    return isSameOrChildPath(key, other) || isSameOrChildPath(other, key);
                })) {
                spdlog::error("Scan library output collision: '{}'.", pathToUtf8(target));
                return 1;
            }
            targets.push_back(key);
            std::size_t previous = 0;
            const bool targetExists = fs::exists(target, ec);
            if (ec || (targetExists && !fs::is_directory(target, ec)) || ec) {
                spdlog::error("Cannot inspect scan destination: '{}'.", pathToUtf8(target));
                return 1;
            }
            if (targetExists) {
                for (const auto& file : fs::directory_iterator(target)) {
                    if (isGeneratedJsonOutput(file, ec)) {
                        ++previous;
                    }
                    if (ec) {
                        spdlog::error("Cannot inspect existing JSON: {}", ec.message());
                        return 1;
                    }
                }
            }
            std::vector<MediaAlbumListPtr> batches;
            std::size_t albumCount = 0;
            std::size_t tracks = 0;
            AlbumCollection collection;
            for (auto albums : collection.LoadAlbumsCo(
                source, false, AppSettingsJson::AppSetting()->AlbumsSplitThreshold)) {
                albumCount += albums->size();
                for (const auto& album : *albums) {
                    tracks += album.trackList.size();
                }
                batches.push_back(albums);
            }
            std::cout << "\nOperation preview: scan to JSON\n"
                << "  Input: " << pathToUtf8(source) << '\n'
                << "  Output: " << pathToUtf8(target) << '\n'
                << "  Albums: " << albumCount << "; tracks: " << tracks
                << "; JSON batches to write: " << batches.size() << '\n'
                << "  Existing generated JSON files to replace: " << previous << '\n'
                << "  Overwrite: generated JSON only; databases and sources unchanged.\n";
            if (batches.empty()) {
                spdlog::error("No albums found; no output will be replaced.");
                return 1;
            }
            preparedAlbums.push_back(std::move(batches));
        }
        const auto confirmation = ConfirmPreview();
        if (confirmation != MenuConfirmation::Run) {
            return cancelledMenuResult(confirmation);
        }
        if (summary) {
            summary->Start();
        }
    }
    std::size_t libraryIndex = 0;
    for (auto& mediaEntry : mediaDirectoryList)
    {
        auto startTime = std::chrono::steady_clock::now();
        const fs::path libraryPath = pathFromUtf8(mediaEntry.mediaPath);
        if (!mediaEntry.mediaName.has_value() || mediaEntry.mediaName->empty()) {
            mediaEntry.mediaName = directoryNameUtf8(libraryPath);
        }
        const fs::path libraryName = pathFromUtf8(mediaEntry.mediaName.value());
        if (libraryName.empty() || libraryName == "." || libraryName == ".." ||
            libraryName.has_root_path() || libraryName.has_parent_path()) {
            spdlog::error("Library name must be a single directory name: '{}'.", mediaEntry.mediaName.value());
            return 1;
        }
        const fs::path targetDirectory = outputRoot / libraryName;
        std::error_code ec;
        const fs::path canonicalLibrary = fs::weakly_canonical(libraryPath, ec);
        if (ec || !fs::is_directory(canonicalLibrary, ec) || ec) {
            spdlog::error("Library path is not a readable directory: {}", pathToUtf8(libraryPath));
            return 1;
        }
        const fs::path canonicalTarget = fs::weakly_canonical(targetDirectory, ec);
        if (ec) {
            spdlog::error("Cannot resolve scan output '{}': {}", pathToUtf8(targetDirectory), ec.message());
            return 1;
        }
        if (isSameOrChildPath(canonicalTarget, canonicalLibrary) ||
            isSameOrChildPath(canonicalLibrary, canonicalTarget)) {
            spdlog::error("Scan output directory must not overlap the source library: '{}'.", pathToUtf8(targetDirectory));
            return 1;
        }
        const fs::path stagingRoot = uniqueSiblingPath(targetDirectory, ".staging-");
        const fs::path stagedDirectory = stagingRoot / libraryName;
        fs::create_directories(stagedDirectory, ec);
        if (ec) {
            spdlog::error("Cannot create staging directory '{}': {}", pathToUtf8(stagedDirectory), ec.message());
            return 1;
        }

        AlbumCollection ac;
        int count = 0;
        try {
            const auto writeBatch = [&](const MediaAlbumListPtr& albumListPtr) {
                spdlog::info("Received batch with {} albums", albumListPtr->size());
                ac.SortAlbums(albumListPtr, { { SortBy::AlbumArtist, true } });
                if (!ac.SaveAlbumsToJSON(albumListPtr, mediaEntry.getMediaJsonPath(stagingRoot, ++count))) {
                    throw std::runtime_error("Failed to write an album JSON file.");
                }
            };
            if (preview) {
                for (const auto& albums : preparedAlbums[libraryIndex]) {
                    if (bIncludeMetadata) {
                        ac.ImportMetadata(albums, 0, albums->size(), AppSettingsJson::AppSetting()->UseAsyncFFmpegCalls);
                    }
                    writeBatch(albums);
                }
                preparedAlbums[libraryIndex].clear();
            }
            else {
                for (auto albums : ac.LoadAlbumsCo(
                    libraryPath, bIncludeMetadata, AppSettingsJson::AppSetting()->AlbumsSplitThreshold)) {
                    writeBatch(albums);
                }
            }
        }
        catch (const std::filesystem::filesystem_error& e) {
            spdlog::error("Filesystem error: {}", e.what());
            fs::remove_all(stagingRoot, ec);
            return 1;
        }
        catch (const std::exception& e) {
            spdlog::error("General error: {}", e.what());
            fs::remove_all(stagingRoot, ec);
            return 1;
        }
        if (count == 0) {
            spdlog::error("No albums found; preserving existing output at '{}'.", pathToUtf8(targetDirectory));
            fs::remove_all(stagingRoot, ec);
            return 1;
        }
        if (!installGeneratedFiles(stagedDirectory, targetDirectory)) {
            fs::remove_all(stagingRoot, ec);
            return 1;
        }
        if (summary) {
            summary->completed += count;
        }
        fs::remove_all(stagingRoot, ec);
        spdlog::info("Wrote {} JSON part(s) to '{}'; scan took {} ms.",
            count,
            pathToUtf8(targetDirectory),
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count());
        ++libraryIndex;
    }

    return 0;
}





// Case-insensitive comparator (from your previous context)
struct CaseInsensitiveCompare {
    bool operator()(const std::string& lhs, const std::string& rhs) const {
        return std::lexicographical_compare(
            lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
            [](char a, char b) { return std::tolower(a) < std::tolower(b); }
        );
    }
};

// Lexicographical comparison (adapted from your wstring compareLex)
int compareLex2(const std::string& a, const std::string& b) {
    std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (ca < cb) return -1;
        else if (ca > cb) return 1;
    }
    if (a.size() < b.size()) return -1;
    if (a.size() > b.size()) return 1;
    return 0;
}


// Function to compare and print two Tags objects in a table
std::string compareAndPrintTagsTable(const JsonUtils::Tags& tags1, const JsonUtils::Tags& tags2) {
    std::ostringstream oss;

    // Collect all unique keys
    std::vector<std::string> allKeys;
    for (const auto& [key, _] : tags1) allKeys.push_back(key);
    for (const auto& [key, _] : tags2) {
        if (!tags1.contains(key)) allKeys.push_back(key);
    }
    std::ranges::sort(allKeys, [](const auto& a, const auto& b) { return compareLex2(a, b) < 0; });
    allKeys.erase(std::unique(allKeys.begin(), allKeys.end()), allKeys.end());

    // Determine column widths
    constexpr size_t keyWidth = 25;    // Increased for long tag names
    constexpr size_t valueWidth = 40;  // Increased for long values
    constexpr size_t statusWidth = 15;
    const std::string separator = std::string(keyWidth + valueWidth * 2 + statusWidth + 7, '-');

    // Print table header
    auto printHeader = [&](std::ostream& os) {
        os << std::left
            << std::setw(keyWidth) << "Key"
            << " | " << std::setw(valueWidth) << "Tags 1 Value"
            << " | " << std::setw(valueWidth) << "Tags 2 Value"
            << " | " << std::setw(statusWidth) << "Status" << "\n";
        os << separator << "\n";
        };
    printHeader(std::cout);
    printHeader(oss);

    // Print table rows
    bool hasDifferences = false;
    for (const auto& key : allKeys) {
        auto it1 = tags1.find(key);
        auto it2 = tags2.find(key);

        std::string value1 = it1 != tags1.end() ? it1->second : "-";
        std::string value2 = it2 != tags2.end() ? it2->second : "-";
        std::string status;

        if (it1 != tags1.end() && it2 != tags2.end()) {
            if (it1->second == it2->second) {
                status = "Same";
            }
            else {
                status = "Different";
                hasDifferences = true;
            }
        }
        else if (it1 != tags1.end()) {
            status = "Only in Tags1";
            hasDifferences = true;
        }
        else {
            status = "Only in Tags2";
            hasDifferences = true;
        }

        // Truncate long values for display
        if (value1.length() > valueWidth - 2) value1 = value1.substr(0, valueWidth - 5) + "...";
        if (value2.length() > valueWidth - 2) value2 = value2.substr(0, valueWidth - 5) + "...";

        auto printRow = [&](std::ostream& os) {
            os << std::left
                << std::setw(keyWidth) << key
                << " | " << std::setw(valueWidth) << value1
                << " | " << std::setw(valueWidth) << value2
                << " | " << std::setw(statusWidth) << status << "\n";
            };
        printRow(std::cout);
        printRow(oss);
    }

    // Footer
    auto printFooter = [&](std::ostream& os) {
        os << separator << "\n";
        os << (hasDifferences ? "Differences found\n" : "No differences found\n");
        };
    printFooter(std::cout);
    printFooter(oss);

    return oss.str();
}

int ScanFolderProcessJSONAndFindDuplicates(const std::vector<fs::path>& jsonFiles, bool openDuplicateFolders,
    ManualOperationSummary* summary = nullptr)
{
	AlbumCollection albumCollection;
	std::shared_ptr<DirectoryContentEntryList> albumListPtr = std::make_shared<DirectoryContentEntryList>();

    for (const auto& jsonFile : jsonFiles) {
        spdlog::info("Processing: {}", pathToUtf8(jsonFile));
        auto albumListChunk = albumCollection.LoadAlbumsFromJSON(jsonFile);
        if (!albumListChunk) {
            spdlog::error("Failed to load album JSON: {}", pathToUtf8(jsonFile));
            return 1;
        }
        albumListPtr->insert(albumListPtr->end(), albumListChunk->begin(), albumListChunk->end());
    }
    if (jsonFiles.empty()) {
        spdlog::error("No generated album JSON files found.");
        return 1;
    }

    //Step 2
	//sort albums by track count, find duplicates in the collection
    spdlog::info("Finding duplicated albums...");
    albumCollection.SortAlbums(albumListPtr, { { SortBy::TrackCount, true } });
    auto dupList = albumCollection.FindDuplicateAlbums(albumListPtr);


    std::vector<std::tuple<MediaAlbum, MediaAlbum>> similarAlbumList;


	//Step 3
	//compare audio quality of the duplicated albums
    auto iCount = dupList.size();
    spdlog::info("Found {} duplicated albums", iCount);
    int iCurrent = 0;
    for (auto entry : dupList)
    {
        bool bSimilarAudioMetrics = false;
        iCurrent++;
        auto [dir1, dir2] = entry;

        const bool bIncludeAudioQualityMetrics = AppSettingsJson::AppSetting()->ExtraAudioQualityMetrics;
        try {
            std::vector<MediaLoadingFuture> asyncFutureList1;
            std::vector<MediaLoadingFuture> asyncFutureList2;
            for (const auto& entry : fs::recursive_directory_iterator(dir1)) {
                //if (fs::is_regular_file(entry.status())) {
                if (MediaTrack::IsValidMedia(entry)) {

                    auto miFuture = std::async(std::launch::async, MediaTrack::ReadMetadataInfoFromFile, entry.path(), bIncludeAudioQualityMetrics);
                    asyncFutureList1.push_back(std::move(miFuture));
                }
            }
            for (const auto& entry : fs::recursive_directory_iterator(dir2)) {
                if (MediaTrack::IsValidMedia(entry)) {
                    auto miFuture = std::async(std::launch::async, MediaTrack::ReadMetadataInfoFromFile, entry.path(), bIncludeAudioQualityMetrics);
                    asyncFutureList2.push_back(std::move(miFuture));
                }
            }

         //   std::vector<MediaTrack> mediaInfoList1;
         //   std::vector<MediaTrack> mediaInfoList2;
			MediaAlbum mediaAlbum1;
			MediaAlbum mediaAlbum2;
            uintmax_t trackNumber = 0;
            for (auto& furure_ret : asyncFutureList1)
            {
                auto [mediaInfo_ret, mediaInfoString_ret] = furure_ret.get();
				mediaAlbum1.trackList.push_back(MediaTrack{ std::filesystem::path{}, ++trackNumber, mediaInfo_ret });
             //   mediaInfoList1.push_back(MediaTrack{ std::filesystem::path{}, ++trackNumber, mediaInfo_ret });

            }
            trackNumber = 0;
            for (auto& furure_ret : asyncFutureList2)
            {
                auto [  mediaInfo_ret, mediaInfoString_ret] = furure_ret.get();
                mediaAlbum2.trackList.push_back(MediaTrack{ std::filesystem::path{}, ++trackNumber, mediaInfo_ret });
                //   mediaInfoList2.push_back(MediaTrack{ std::filesystem::path{}, ++trackNumber, mediaInfo_ret });
            }

			similarAlbumList.push_back(std::make_tuple(mediaAlbum1, mediaAlbum2));

            bSimilarAudioMetrics = MediaTrack::CompareAudioTracks(mediaAlbum1.trackList, mediaAlbum2.trackList);
//			spdlog::info("Audio quality matches: {}", bSimilarAudioMetrics ? "Similar metrics" : "Different quality");

            if (!bSimilarAudioMetrics)
            {
                spdlog::info("Audio Metrics:\n{}", MediaTrack::GenerateComparisonReport(mediaAlbum1.trackList, mediaAlbum2.trackList));
            }


			//Print album size check
            bool btime_different = false;
            std::ostringstream oss;
            oss << "Album size check (album1/2): ";


			auto tagCompareResult = std::string{"N/A"};
            std::string encoder1{};
            std::string encoder2{};

            for (size_t i = 0; i < mediaAlbum1.trackList.size(); i++) {
                const auto tSize1 = mediaAlbum1.trackList[i].formatInfo.format.duration.value_or(-1);
                const auto tSize2 = mediaAlbum2.trackList[i].formatInfo.format.duration.value_or(-1);

                // Format with 2 decimal places
                if (tSize1 != tSize2)
                {
                    oss << std::fixed << std::setprecision(2);
                    oss << (i + 1) << ": " << tSize1 << "/" << tSize2 << " [" << (tSize2 - tSize1) << "] ";

                    auto tags1 = mediaAlbum1.trackList[i].formatInfo.format.tags.value_or(JsonUtils::Tags{});
                    auto tags2 = mediaAlbum2.trackList[i].formatInfo.format.tags.value_or(JsonUtils::Tags{});

					tagCompareResult = compareAndPrintTagsTable(tags1, tags2);

                    if (encoder1.empty() && tags1.contains("encoder")) {
                        encoder1 = tags1.at("encoder");
                    }
                    if (encoder2.empty() && tags2.contains("encoder")) {
                        encoder2 = tags2.at("encoder");
                    }

                    btime_different = true; // You can re-add the conditional check if needed
                }
            }

            if (btime_different) {
                spdlog::info("{}", oss.str());
            }

            spdlog::info("encoder1: {} encoder1: {}", encoder1.c_str(), encoder2.c_str());

            spdlog::info("Potentially duplicated albums [{}/{}]:\n\tAlbum1: \"{}\"\n\tAlbum2: \"{}\"",
                iCurrent, dupList.size(), CommonUtils::wstring_to_utf8(dir1), CommonUtils::wstring_to_utf8(dir2));

		//	spdlog::info("{}", tagCompareResult.c_str());

          //  if (!bSimilarAudioMetrics)
            if (openDuplicateFolders) {
                PlatformUtils::OpenDirectory(fs::path(dir1));
                PlatformUtils::OpenDirectory(fs::path(dir2));
            }

            iCount--;
            if (summary) {
                ++summary->completed;
            }
        }
        catch (const fs::filesystem_error& e) {
            spdlog::error("Error getting track information: {}", e.what());
            return 1;
        }
        catch (const std::exception& e) {
            spdlog::error("Error processing duplicate pair: {}", e.what());
            return 1;
        }
    }

    return 0;
}

int ExportJSONToDB(const std::vector<MediaDirectoryElement>& mediaDirectoryList, ManualOperationSummary& summary)
{
    summary.unit = "JSON batches imported";
    auto appSettingPtr = AppSettingsJson::AppSetting();

    AlbumCollection albumCollection;

    struct ImportPlan {
        fs::path database;
        std::vector<fs::path> json;
    };
    std::vector<ImportPlan> plans;
    std::vector<fs::path> targets;
    for (const auto& mediaEntry : mediaDirectoryList) {
        const auto name = pathFromUtf8(mediaEntry.mediaName.value_or(""));
        if (name == "." || name == ".." || name.has_root_path() || name.has_parent_path()) {
            spdlog::error("Library name must be a single directory name for database import.");
            return 1;
        }
        ImportPlan plan{ absoluteDisplayPath(mediaEntry.getMediaDBPath(pathFromUtf8(appSettingPtr->OutDirectory))), {} };
        summary.outputs.push_back(plan.database);
        auto generator = mediaEntry.getMediaJsonPathCo(pathFromUtf8(appSettingPtr->OutDirectory));
        while (generator.resume()) {
            plan.json.push_back(absoluteDisplayPath(generator.value()));
        }
        std::error_code ec;
        const auto key = fs::weakly_canonical(plan.database, ec);
        if (ec || std::any_of(targets.begin(), targets.end(), [&](const fs::path& other) {
                return isSameOrChildPath(key, other) && isSameOrChildPath(other, key);
            })) {
            spdlog::error("Unresolvable or colliding database output: '{}'.", pathToUtf8(plan.database));
            return 1;
        }
        targets.push_back(key);
        const bool exists = fs::exists(plan.database, ec);
        if (ec || (exists && !fs::is_regular_file(plan.database, ec)) || ec) {
            spdlog::error("Cannot use database output: '{}'.", pathToUtf8(plan.database));
            return 1;
        }
        std::cout << "\nOperation preview: import JSON into SQLite\n"
            << "  JSON input files: " << plan.json.size() << '\n';
        for (const auto& json : plan.json) {
            std::cout << "    Input: " << pathToUtf8(json) << '\n';
        }
        std::cout << "  Output: " << pathToUtf8(plan.database)
            << "\n  Database: " << (exists ? "existing records will be updated" : "new file")
            << "\n  Overwrite: imported records; source audio and JSON unchanged.\n";
        if (plan.json.empty()) {
            spdlog::error("No generated JSON input for this library; run option 2 first.");
            return 1;
        }
        plans.push_back(std::move(plan));
    }
    if (plans.empty()) {
        spdlog::error("No active libraries configured for database import.");
        return 1;
    }
    const auto confirmation = ConfirmPreview();
    if (confirmation != MenuConfirmation::Run) {
        return cancelledMenuResult(confirmation);
    }
    summary.Start();
    for (const auto& plan : plans) {
        for (const auto& json : plan.json) {
            spdlog::info("Processing: {}", pathToUtf8(json));
            const auto albums = albumCollection.LoadAlbumsFromJSON(json);
            if (!albums) {
                spdlog::error("Failed to read JSON: '{}'.", pathToUtf8(json));
                return 1;
            }
            albumCollection.SortAlbums(albums, { { SortBy::AlbumName, true } });
            if (!albumCollection.ExportToDatabase(albums, plan.database)) {
                return 1;
            }
            ++summary.completed;
        }
    }

    return 0;
}

int ImportJsonDirectoryToDatabase(const fs::path& jsonDirectory, const fs::path& outputRoot)
{
    std::error_code ec;
    const fs::path canonicalJsonDirectory = fs::weakly_canonical(jsonDirectory, ec);
    if (ec || !fs::is_directory(canonicalJsonDirectory, ec) || ec) {
        spdlog::error("JSON input path is not a directory: {}", pathToUtf8(jsonDirectory));
        return 1;
    }
    const auto jsonFiles = findJsonFiles(canonicalJsonDirectory);
    if (jsonFiles.empty()) {
        spdlog::error("No generated album JSON files found in '{}'.", pathToUtf8(jsonDirectory));
        return 1;
    }

    auto albums = std::make_shared<DirectoryContentEntryList>();
    AlbumCollection albumCollection;
    for (const auto& jsonFile : jsonFiles) {
        auto part = albumCollection.LoadAlbumsFromJSON(jsonFile);
        if (!part) {
            spdlog::error("Failed to load album JSON: {}", pathToUtf8(jsonFile));
            return 1;
        }
        albums->insert(albums->end(), part->begin(), part->end());
    }
    MediaDirectoryElement library{};
    library.mediaName = directoryNameUtf8(canonicalJsonDirectory);
    const fs::path databasePath = library.getMediaDBPath(outputRoot);
    fs::create_directories(databasePath.parent_path(), ec);
    if (ec) {
        spdlog::error("Cannot create database directory '{}': {}", pathToUtf8(databasePath.parent_path()), ec.message());
        return 1;
    }

    spdlog::info("Importing {} JSON file(s) into '{}'.", jsonFiles.size(), pathToUtf8(databasePath));
    return albumCollection.ExportToDatabase(albums, databasePath) ? 0 : 1;
}

MenuConfirmation ConfirmSourceReplacement(const std::vector<MediaDirectoryElement>& libraries)
{
    if (libraries.empty()) {
        spdlog::error("No active media libraries configured for FLAC conversion.");
        return MenuConfirmation::Error;
    }
    std::cout << "\nWARNING: option 1 writes FLAC output into the source libraries.\n"
        "Existing matching FLAC files will be replaced after successful encoding.\n"
        "Output is 16-bit / 44.1 kHz with dithering; embedded artwork is omitted.\n"
        "Non-FLAC sources remain, but matching .flac destinations may be overwritten.\n"
        "Back up your library before proceeding.\n\nAffected active libraries:\n";
    for (const auto& library : libraries) {
        std::cout << "  " << library.mediaPath;
        if (library.mediaName && !library.mediaName->empty()) {
            std::cout << " [" << *library.mediaName << "]";
        }
        std::cout << '\n';
    }
    return ConfirmManualAction("REPLACE",
        "Cancelled. No conversion was started; source files are unchanged.");
}

namespace
{
struct MenuEntry {
    char number;
    ConvertActionEnum action;
    const char* label;
    const char* details;
};

constexpr std::array<MenuEntry, 6> MenuEntries{ {
    { '1', ConvertActionEnum::ConverEnum,
        "Re/Convert to FLAC [WARNING: replaces existing FLAC files]",
        "      Input: supported audio files in the active source libraries, recursively.\n"
        "      Output: 16-bit / 44.1 kHz .flac files beside the source files, with dithering.\n"
        "      Changes: overwrites matching FLAC files after successful encoding;\n"
        "               non-FLAC sources remain. Audio-only mapping omits embedded artwork.\n"
        "               Back up your library first; resampling/depth reduction loses information.\n"
        "      Rejects sources below 16-bit / 44.1 kHz or whose depth/rate cannot be verified.\n"
        "      Running requires REPLACE, not a generic RUN confirmation.\n" },
    { '2', ConvertActionEnum::CreateJSONEnum, "Scan directories and create metadata JSON",
        "      Input: audio libraries; reads track tags and technical metadata.\n"
        "      Output: OutDirectory/<mediaName>/media_out_<mediaName>_<part>.json.\n"
        "      Changes: replaces the previous generated JSON set after a successful scan;\n"
        "               source audio, existing databases and unrelated files are unchanged.\n"
        "      <mediaName> defaults to the source folder name; <part> is a numbered batch.\n" },
    { '3', ConvertActionEnum::ProcessJSONEnum, "Find duplicate albums",
        "      Input: generated JSON from option 2; source folders must remain accessible\n"
        "             for track metadata/quality comparisons.\n"
        "      Output: duplicate reports in the console; opens matching album folders.\n"
        "      Changes: does not delete duplicates or change audio, JSON or databases.\n" },
    { '4', ConvertActionEnum::PopulateJsonToDBEnum, "Import metadata JSON into SQLite",
        "      Input: generated JSON from option 2 for each active library.\n"
        "      Output: OutDirectory/<mediaName>/media_out_<mediaName>.db.\n"
        "      Changes: creates/updates database records; source audio and JSON are unchanged.\n"
        "               Does not scan audio directly; preview shows the configured filenames.\n" },
    { '5', ConvertActionEnum::CreateDBFromFolderEnum, "Scan directories directly to SQLite [NOT IMPLEMENTED]",
        "      Input/Output/Changes: none; selection 5 runs no action.\n"
        "      Use option 2, then option 4 instead.\n" },
    { '6', ConvertActionEnum::ConvertMp3Enum, "Convert FLAC to MP3 [sources unchanged]",
        "      Input: native FLAC files in the active source libraries, recursively.\n"
        "      Output: OutDirectory/MP3/<mediaName>/ with relative paths preserved:\n"
        "              MP3 (LAME VBR quality 0 by default), .mp3.flac-metadata sidecars,\n"
        "              and unchanged non-audio companion files (artwork, booklets, etc.).\n"
        "      Changes: sources unchanged; existing destinations fail unless overwrite is enabled.\n"
        "               Supported tags/pictures/chapters go into ID3v2.4; original metadata\n"
        "               is retained in sidecars. MP3 audio is lossy. Multichannel audio\n"
        "               is rejected unless stereo downmix is explicitly enabled;\n"
        "               44.1/48 kHz retained, other accepted rates become 48 kHz.\n"
        "      Rejects sources below 16-bit / 44.1 kHz or whose depth/rate cannot be verified.\n"
        "      <mediaName> defaults to the source folder name.\n"
        "      Prompts configure quality (0-9), stereo downmix and overwrite before preview.\n"
        "      Defaults: VBR 0, no downmix, no overwrite. RUN confirms the selected settings.\n" }
} };

void ShowActionDetails(const MenuEntry& entry)
{
    std::cout << "\n" << entry.number << " - " << entry.label << '\n' << entry.details;
}
}

ConvertActionEnum GetUserAction()
{
    std::cout << "\nSelect run option\n"
        "Uses active configured libraries. Details and a preview follow selection.\n\n";
    for (const auto& entry : MenuEntries) {
        std::cout << "  " << entry.number << " - " << entry.label << '\n';
    }
    std::cout << "  0 - Exit without running an action\n"
        "  ? - Help: input, output and changes for all options\n\n";
    for (;;) {
        std::cout << "Enter option [0-6 or ?], then press Enter: " << std::flush;
        std::string input;
        if (!std::getline(std::cin, input)) {
            if (std::cin.eof()) {
                return ConvertActionEnum::ExitEnum;
            }
            spdlog::error("Cannot read menu selection from standard input.");
            return ConvertActionEnum::NullEnum;
        }
        input = trimMenuResponse(input);
        if (input.size() != 1) {
            std::cout << "Invalid selection. Enter one option number.\n";
            continue;
        }
        if (input == "0") {
            return ConvertActionEnum::ExitEnum;
        }
        if (input == "?") {
            std::cout << "\nAll actions use active MediaDirectoryList libraries; OutDirectory is the output root.\n";
            for (const auto& entry : MenuEntries) {
                ShowActionDetails(entry);
            }
            continue;
        }
        const auto entry = std::find_if(MenuEntries.begin(), MenuEntries.end(),
            [&](const MenuEntry& option) { return option.number == input[0]; });
        if (entry == MenuEntries.end()) {
            std::cout << "Invalid selection. Enter a number from 0 to 6, or ? for help.\n";
            continue;
        }
        ShowActionDetails(*entry);
        if (entry->action == ConvertActionEnum::CreateDBFromFolderEnum) {
            std::cout << "Option 5 is not implemented. Use option 2, then option 4.\n";
            continue;
        }
        return entry->action;
    }
}

void init_logger(spdlog::level::level_enum level = spdlog::level::debug)
{
    //spdlog::set_default_logger(spdlog::stdout_color_mt("console"));

    auto console_logger = spdlog::stdout_color_mt("console");

    // Set log level globally (e.g., to debug)
    console_logger->set_level(level);

    // Optional: Set a pattern (e.g., with color and timestamp)
    //console_logger->set_pattern("[%H:%M:%S %z] [%^%l%$] %v");
    console_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%^%l%$] %v");

    // Set as the default logger
    spdlog::set_default_logger(console_logger);

}

int RunLegacyMenu()
{
    init_logger();

    spdlog::info("FFmpeg version: {}.{}.{}", LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO);
    spdlog::info("----------------");



#ifdef _WIN32
    SetConsoleCP(CP_UTF8);         // For input
    SetConsoleOutputCP(CP_UTF8); // For Unicode output
//    std::wcout.imbue(std::locale("en_US.utf8"));
    //SetConsoleCP(CP_UTF8);
    //// Enable ANSI escape codes in Windows Console
    //HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    //DWORD dwMode = 0;
    //GetConsoleMode(hOut, &dwMode);
    //dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    //SetConsoleMode(hOut, dwMode);
#endif

    auto appSettingPtr = AppSettingsJson::AppSetting();
    if (appSettingPtr == nullptr)
    {
        //std::cout << CommonUtils::getSymbolConstexpr("stop_sign") << "Failed to load Configuration File" << std::endl;
        spdlog::error("Failed to load Configuration File");

        return -1;
    }

    FFmpeg::initialize_ffmpeg_logging();


	std::vector<MediaDirectoryElement> mediaList;

    //std::cout << "App Settings: " << std::filesystem::path(AppSettingsJson::DefaultConfigDirectory) / AppSettingsJson::DefaultConfigFileName << std::endl;
    spdlog::info("Output root (OutDirectory): {}", appSettingPtr->OutDirectory);
    spdlog::info("Database output: OutDirectory/<mediaName>/media_out_<mediaName>.db");

    spdlog::info("----------------");

    spdlog::info("Log Level: {}", AppSettingsJson::AppSetting()->LogLevel);
    spdlog::info("Use Async FFmpeg Calls: {}", AppSettingsJson::AppSetting()->UseAsyncFFmpegCalls);
    spdlog::info("Use ffmpeg Library API: {}", AppSettingsJson::AppSetting()->UseFFmpegLibraryAPI);
    spdlog::info("Extra Audio Quality Metrics: {}", AppSettingsJson::AppSetting()->ExtraAudioQualityMetrics);
    spdlog::info("Albums Split Threshold: {}", AppSettingsJson::AppSetting()->AlbumsSplitThreshold);

    spdlog::info("----------------");

    spdlog::info("Media Libraries:");
	for (auto& mediaEntry : appSettingPtr->MediaDirectoryList)
	{
		auto activeFlag = mediaEntry.isActive ? "Active" : "Inactive";
        spdlog::info("{} - Media Path: {} [{}]", activeFlag, mediaEntry.mediaPath, mediaEntry.mediaName.value_or(""));

        if (mediaEntry.isActive)
        {
            mediaList.push_back(mediaEntry);
        }
	}

	spdlog::info("");


    const auto executeAction = [&](ConvertActionEnum action, ManualOperationSummary& summary) {
        switch (action)
        {
        case ConvertActionEnum::ConverEnum: //1
            return ConvertFLACToFLAC(mediaList, summary);

        case ConvertActionEnum::CreateJSONEnum: //2
            return ScanFolderAndCreateJSON(mediaList, true, pathFromUtf8(appSettingPtr->OutDirectory), true, &summary);

        case ConvertActionEnum::ProcessJSONEnum: //3
        {
            summary.unit = "duplicate pairs processed";
            summary.outputDescription = "console reports and matching album folders (see reports above).";
            const auto jsonFiles = findConfiguredJsonFiles(mediaList, pathFromUtf8(appSettingPtr->OutDirectory));
            std::cout << "\nOperation preview: find duplicate albums\n  JSON input files: " << jsonFiles.size() << '\n';
            for (const auto& json : jsonFiles) {
                std::cout << "    Input: " << pathToUtf8(absoluteDisplayPath(json)) << '\n';
            }
            std::cout << "  Output: console reports and opened matching album folders.\n"
                "  Overwrite: none; no files or database records are changed.\n"
                "  Track metadata is read from source folders during comparisons.\n";
            if (jsonFiles.empty()) {
                spdlog::error("No generated JSON input; run option 2 first.");
                return 1;
            }
            const auto confirmation = ConfirmPreview();
            if (confirmation != MenuConfirmation::Run) {
                return cancelledMenuResult(confirmation);
            }
            summary.Start();
            return ScanFolderProcessJSONAndFindDuplicates(jsonFiles, true, &summary);
        }

        case ConvertActionEnum::PopulateJsonToDBEnum: //4
            return ExportJSONToDB(mediaList, summary);

        case ConvertActionEnum::ExitEnum: //0 or end of input
            return 0;

        case ConvertActionEnum::ConvertMp3Enum: //6
            return ConvertFLACToMP3(mediaList, pathFromUtf8(appSettingPtr->OutDirectory), summary);

        default:
            spdlog::error("Action not found: {}", static_cast<int>(action));
            return 2;
        }
    };
    for (;;) {
        const auto action = GetUserAction();
        ManualOperationSummary summary;
        int result = 1;
        try {
            result = executeAction(action, summary);
        }
        catch (const fs::filesystem_error& error) {
            spdlog::error("Filesystem error: {}", error.what());
        }
        catch (const std::exception& error) {
            spdlog::error("Menu action failed: {}", error.what());
        }
        if (result != ReturnToMenu && (summary.started || result != 0)) {
            summary.Print(result);
        }
        if (result != ReturnToMenu) {
            return result;
        }
    }
}

int RunWithArguments(const std::vector<const char*>& arguments)
{
#ifdef _WIN32
    if (GetConsoleCP() != CP_UTF8) {
        SetConsoleCP(CP_UTF8);
    }
    if (GetConsoleOutputCP() != CP_UTF8) {
        SetConsoleOutputCP(CP_UTF8);
    }
#endif

    if (arguments.size() == 1) {
        try {
            return RunLegacyMenu();
        }
        catch (const fs::filesystem_error& error) {
            spdlog::error("Filesystem error: {}", error.what());
            return 1;
        }
        catch (const std::exception& error) {
            spdlog::error("Menu action failed: {}", error.what());
            return 1;
        }
    }

    CommandLineOptions options;
    const int parseResult = ParseCommandLine(
        static_cast<int>(arguments.size()), arguments.data(), options);
    if (parseResult != 1) {
        return parseResult;
    }

    try {
        std::optional<fs::path> explicitConfigPath;
        if (options.configPathSpecified) {
            explicitConfigPath = pathFromUtf8(options.configPath);
        }
        std::string configurationError;
        const auto settings = AppSettingsJson::LoadForCommandLine(explicitConfigPath, configurationError);
        if (!settings) {
            std::cerr << "Configuration error: " << configurationError << '\n';
            return 1;
        }

        init_logger(static_cast<spdlog::level::level_enum>(settings->LogLevel));
        FFmpeg::initialize_ffmpeg_logging();

        const fs::path inputPath = pathFromUtf8(options.inputPath);
        const fs::path outputDirectory = options.outputDirectorySpecified
            ? pathFromUtf8(options.outputDirectory)
            : pathFromUtf8(settings->OutDirectory);

        switch (options.action) {
        case CommandAction::Convert:
            return FolderConvert::ConvertAudioFolderToOutput(
                inputPath, outputDirectory, options.overwrite, options.conversion) == 0 ? 0 : 1;

        case CommandAction::Scan: {
            std::error_code ec;
            const fs::path canonicalInput = fs::weakly_canonical(inputPath, ec);
            if (ec || !fs::is_directory(canonicalInput, ec) || ec) {
                spdlog::error("Library path is not a directory: {}", pathToUtf8(inputPath));
                return 1;
            }
            MediaDirectoryElement library{};
            library.mediaPath = pathToUtf8(canonicalInput);
            library.mediaName = directoryNameUtf8(canonicalInput);
            return ScanFolderAndCreateJSON({ library }, true, outputDirectory);
        }

        case CommandAction::Duplicates:
            if (!fs::is_directory(inputPath)) {
                spdlog::error("JSON input path is not a directory: {}", pathToUtf8(inputPath));
                return 1;
            }
            return ScanFolderProcessJSONAndFindDuplicates(
                findJsonFiles(inputPath), options.openDuplicateFolders);

        case CommandAction::DatabaseImport:
            if (!fs::is_directory(inputPath)) {
                spdlog::error("JSON input path is not a directory: {}", pathToUtf8(inputPath));
                return 1;
            }
            return ImportJsonDirectoryToDatabase(inputPath, outputDirectory);

        default:
            return 2;
        }
    }
    catch (const fs::filesystem_error& error) {
        spdlog::error("Filesystem error: {}", error.what());
        return 1;
    }
    catch (const std::exception& error) {
        spdlog::error("Command failed: {}", error.what());
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[])
{
    std::vector<std::string> utf8Arguments;
    utf8Arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        const auto argument = fs::path(argv[index]).u8string();
        utf8Arguments.emplace_back(reinterpret_cast<const char*>(argument.data()), argument.size());
    }

    std::vector<const char*> argumentPointers;
    argumentPointers.reserve(utf8Arguments.size());
    for (const auto& argument : utf8Arguments) {
        argumentPointers.push_back(argument.c_str());
    }
    return RunWithArguments(argumentPointers);
}
#else
int main(int argc, char* argv[])
{
    std::vector<const char*> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        arguments.push_back(argv[index]);
    }
    return RunWithArguments(arguments);
}
#endif
