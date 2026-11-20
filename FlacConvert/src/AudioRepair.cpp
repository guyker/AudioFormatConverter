#include "AudioRepair.h"

#include "CommonUtils.h"
#include "MediaConvertionTask.h"
#include "PlatformUtils.h"
#include "PreservationAudit.h"
#include "RepairRules.h"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
namespace fs = std::filesystem;

using FixList = std::vector<std::pair<std::string, std::size_t>>;

std::string utf8(const fs::path& path)
{
    return CommonUtils::path_to_utf8(path);
}

FixList fixesOf(const RepairRules::Analysis& analysis)
{
    FixList fixes;
    for (const auto& finding : analysis.findings) {
        if (finding.fix) fixes.emplace_back(finding.rule, finding.count);
    }
    return fixes;
}

std::string describeFixes(const FixList& fixes)
{
    std::string result;
    for (const auto& [rule, count] : fixes) {
        if (!result.empty()) result += ", ";
        result += rule + " x" + std::to_string(count);
    }
    return result;
}

std::atomic_uint64_t temporarySequence{ 0 };

fs::path temporaryPath(const fs::path& target)
{
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
    const auto processId = _getpid();
#else
    const auto processId = getpid();
#endif
    return target.parent_path() / (".afc-repair-" + std::to_string(processId) + "-" +
        std::to_string(timestamp) + "-" + std::to_string(++temporarySequence) + ".tmp");
}

fs::path ioPath(const fs::path& path, const fs::path& outputRoot)
{
#ifdef _WIN32
    if (path.is_absolute()) return PlatformUtils::WindowsLongPath(path);
    return PlatformUtils::WindowsLongPath(outputRoot / path);
#else
    (void)outputRoot;
    return path;
#endif
}

bool samePathComponent(const fs::path& left, const fs::path& right)
{
#ifdef _WIN32
    const auto& a = left.native();
    const auto& b = right.native();
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
        b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
#else
    return left == right;
#endif
}

bool isSameOrChildPath(const fs::path& candidate, const fs::path& base)
{
    auto candidatePart = candidate.begin();
    for (auto basePart = base.begin(); basePart != base.end(); ++basePart, ++candidatePart) {
        if (candidatePart == candidate.end() || !samePathComponent(*candidatePart, *basePart)) {
            return false;
        }
    }
    return true;
}

bool writeReport(const AudioRepair::Plan& plan, const fs::path& reportPath,
    const std::string& status, const std::string& error)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("schemaVersion"); writer.Int(2);
    writer.Key("source"); writer.String(utf8(plan.input).c_str());
    writer.Key("output"); writer.String(utf8(plan.output).c_str());
    writer.Key("status"); writer.String(status.c_str());
    writer.Key("error"); writer.String(error.c_str());
    writer.Key("rules"); writer.StartArray();
    for (const auto& rule : RepairRules::Catalog()) {
        if (plan.rules.empty() || plan.rules.contains(rule.id)) writer.String(rule.id.c_str());
    }
    writer.EndArray();
    writer.Key("inspectedFiles"); writer.Uint64(plan.inspectedFiles);
    writer.Key("repairedFiles");
    writer.Uint64(static_cast<std::uint64_t>(std::count_if(plan.files.begin(), plan.files.end(),
        [](const AudioRepair::PlannedFile& file) { return file.repair && file.status == "installed"; })));
    writer.Key("copiedFiles");
    writer.Uint64(static_cast<std::uint64_t>(std::count_if(plan.files.begin(), plan.files.end(),
        [](const AudioRepair::PlannedFile& file) { return !file.repair && file.status == "installed"; })));
    writer.Key("findings"); writer.StartArray();
    for (const auto& finding : plan.findings) {
        writer.StartObject();
        writer.Key("source"); writer.String(utf8(finding.source.lexically_relative(plan.input)).c_str());
        writer.Key("rule"); writer.String(finding.rule.c_str());
        writer.Key("status"); writer.String(finding.status.c_str());
        writer.Key("count"); writer.Uint64(finding.count);
        writer.Key("detail"); writer.String(finding.detail.c_str());
        writer.EndObject();
    }
    writer.EndArray();
    writer.Key("files"); writer.StartArray();
    for (const auto& file : plan.files) {
        writer.StartObject();
        writer.Key("source"); writer.String(utf8(file.source.lexically_relative(plan.input)).c_str());
        writer.Key("output"); writer.String(utf8(file.target.lexically_relative(plan.output)).c_str());
        writer.Key("action"); writer.String(file.repair ? "repair" : "copy");
        writer.Key("status"); writer.String(file.status.c_str());
        if (file.repair) {
            writer.Key("appliedRules"); writer.StartArray();
            for (const auto& [rule, count] : file.fixes) {
                writer.StartObject();
                writer.Key("rule"); writer.String(rule.c_str());
                writer.Key("count"); writer.Uint64(count);
                writer.EndObject();
            }
            writer.EndArray();
            writer.Key("impact"); writer.StartObject();
            writer.Key("audio"); writer.String("Byte-identical audio data range; not decoded or re-encoded.");
            writer.Key("metadata");
            writer.String("Only the metadata named by the applied rules changed; see the matching findings.");
            writer.EndObject();
        }
        else {
            writer.Key("impact");
            writer.String("File copied byte-for-byte; no audio or metadata changes.");
        }
        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();

    const auto temporary = temporaryPath(reportPath);
    std::error_code ec;
    if (fs::exists(ioPath(temporary, plan.output), ec) || ec) return false;
    std::ofstream output(ioPath(temporary, plan.output), std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
    output.flush();
    const bool success = static_cast<bool>(output);
    output.close();
    if (!success || output.fail()) return false;
    std::ifstream verify(ioPath(temporary, plan.output), std::ios::binary);
    const std::string actual{ std::istreambuf_iterator<char>(verify), std::istreambuf_iterator<char>() };
    const bool reportMatches = !verify.bad() &&
        actual == std::string(buffer.GetString(), buffer.GetSize());
    verify.close();
    if (!reportMatches) return false;
    if (!MediaConvertionTask::InstallOutputFile(ioPath(temporary, plan.output),
            ioPath(reportPath, plan.output), plan.overwrite)) {
        return false;
    }
    return true;
}
}

namespace AudioRepair
{
int Prepare(const fs::path& input, const fs::path& output, bool overwrite,
    const std::set<std::string>& rules, Plan& plan)
{
    plan = {};
    plan.rules = rules;
    std::error_code ec;
    plan.input = fs::absolute(input, ec).lexically_normal();
    if (ec || !fs::is_directory(plan.input, ec) || ec) {
        spdlog::error("Repair input is not a readable directory: '{}'.", utf8(input));
        return -1;
    }
    plan.output = fs::absolute(output, ec).lexically_normal();
    if (ec || plan.output.empty()) {
        spdlog::error("Cannot resolve repair output directory: {}", ec.message());
        return -1;
    }
    plan.overwrite = overwrite;
    const auto canonicalInput = fs::weakly_canonical(plan.input, ec);
    if (ec) {
        spdlog::error("Cannot resolve repair input: {}", ec.message());
        return -1;
    }
    const auto canonicalOutput = fs::weakly_canonical(plan.output, ec);
    if (ec || isSameOrChildPath(canonicalOutput, canonicalInput) ||
        isSameOrChildPath(canonicalInput, canonicalOutput)) {
        spdlog::error("Repair output overlaps its source library.");
        return -1;
    }

    std::set<fs::path> affected;
    std::map<fs::path, FixList> repairSources;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(plan.input)) {
            const auto status = entry.symlink_status();
            if (fs::is_symlink(status)) {
                if (entry.is_directory(ec) && !ec) {
                    spdlog::error("Repair input contains a directory link: '{}'.", utf8(entry.path()));
                    return -1;
                }
                continue;
            }
            if (!fs::is_regular_file(status) || !RepairRules::IsSupportedFile(entry.path())) continue;
            ++plan.inspectedFiles;
            try {
                const auto analysis = RepairRules::Analyze(entry.path(), plan.rules);
                for (const auto& finding : analysis.findings) {
                    plan.findings.push_back({ entry.path(), finding.rule,
                        finding.fix ? "repairable" : "manual-review", finding.count, finding.detail });
                }
                if (analysis.edit) {
                    auto folder = entry.path().parent_path().lexically_relative(plan.input);
                    if (folder == ".") folder.clear();
                    affected.insert(std::move(folder));
                    repairSources.emplace(entry.path(), fixesOf(analysis));
                }
            }
            catch (const std::exception& exception) {
                plan.findings.push_back({ entry.path(), "unreadable", "unreadable", 1, exception.what() });
            }
        }
    }
    catch (const fs::filesystem_error& exception) {
        spdlog::error("Cannot inspect repair input: {}", exception.what());
        return -1;
    }

    const bool rootAffected = affected.contains(fs::path{});
    for (auto it = affected.begin(); it != affected.end();) {
        bool covered = rootAffected && !it->empty();
        for (auto parent = it->parent_path(); !covered && !parent.empty(); parent = parent.parent_path()) {
            if (affected.contains(parent)) {
                covered = true;
                break;
            }
        }
        if (covered) it = affected.erase(it);
        else ++it;
    }
    plan.repairedFolders.assign(affected.begin(), affected.end());

    std::set<fs::path> directories;
    try {
        for (const auto& folder : plan.repairedFolders) {
            const auto root = folder.empty() ? plan.input : plan.input / folder;
            directories.insert(folder);
            for (const auto& entry : fs::recursive_directory_iterator(root)) {
                const auto status = entry.symlink_status();
                if (fs::is_symlink(status)) {
                    spdlog::error("Affected repair folder contains a link: '{}'.", utf8(entry.path()));
                    return -1;
                }
                const auto relative = entry.path().lexically_relative(plan.input);
                if (fs::is_directory(status)) {
                    directories.insert(relative);
                    continue;
                }
                if (!fs::is_regular_file(status)) {
                    spdlog::error("Affected repair folder contains a non-regular file: '{}'.", utf8(entry.path()));
                    return -1;
                }
                const auto size = entry.file_size(ec);
                const auto modified = entry.last_write_time(ec);
                if (ec) {
                    spdlog::error("Cannot inspect repair source '{}': {}", utf8(entry.path()), ec.message());
                    return -1;
                }
                const auto target = plan.output / relative;
                const auto canonicalTarget = fs::weakly_canonical(target, ec);
                if (ec || isSameOrChildPath(canonicalTarget, canonicalInput)) {
                    spdlog::error("Repair destination overlaps its source: '{}'.", utf8(target));
                    return -1;
                }
                const bool targetExists = fs::exists(target, ec);
                if (ec) {
                    spdlog::error("Cannot inspect repair destination '{}': {}", utf8(target), ec.message());
                    return -1;
                }
                if (targetExists) {
                    if (!overwrite) {
                        plan.conflicts.push_back(target);
                    }
                    else if (!fs::is_regular_file(target, ec) || ec) {
                        spdlog::error("Repair destination is not a regular file: '{}'.", utf8(target));
                        return -1;
                    }
                }
                const auto fixes = repairSources.find(entry.path());
                const bool repair = fixes != repairSources.end();
                plan.files.push_back({ entry.path(), target, {}, size, modified, repair,
                    repair ? fixes->second : FixList{}, "not-attempted" });
                plan.files.back().temporary = temporaryPath(target);
            }
        }
    }
    catch (const fs::filesystem_error& exception) {
        spdlog::error("Cannot prepare affected repair folders: {}", exception.what());
        return -1;
    }

    for (const auto& directory : directories) {
        const auto target = plan.output / directory;
        if (fs::exists(target, ec) && (!overwrite || !fs::is_directory(target, ec) || ec)) {
            plan.conflicts.push_back(target);
        }
        if (ec) {
            spdlog::error("Cannot inspect repair directory destination: {}", ec.message());
            return -1;
        }
    }
    const auto report = plan.output / "repair-report.json";
    if (fs::exists(report, ec) && !overwrite) plan.conflicts.push_back(report);
    if (ec) {
        spdlog::error("Cannot inspect repair report destination: {}", ec.message());
        return -1;
    }
    const auto marker = plan.output / ".repair-in-progress";
    if (fs::exists(marker, ec) || ec) {
        spdlog::error("Repair output has an existing in-progress marker or cannot be inspected.");
        return -1;
    }
    return 0;
}

int Execute(Plan& plan, std::size_t* repaired, std::size_t* copied)
{
    if (!plan.conflicts.empty()) {
        spdlog::error("Repair plan has {} destination conflicts.", plan.conflicts.size());
        return -1;
    }
    if (repaired) *repaired = 0;
    if (copied) *copied = 0;
    bool owned = false;
    PlatformUtils::ScopedCurrentDirectory outputDirectory;
    if (!outputDirectory.EnterValidatedDirectory(plan.output, plan.input, true, &owned)) {
        return -1;
    }
    (void)owned;
    const auto resolve = [&](const fs::path& path) {
#ifdef _WIN32
        return PlatformUtils::WindowsLongPath(path.is_absolute() ? path : plan.output / path);
#else
        return path.is_absolute() ? path : plan.output / path;
#endif
    };
    std::error_code ec;
    const auto marker = resolve(".repair-in-progress");
    if (!fs::create_directory(marker, ec)) {
        spdlog::error("Cannot reserve repair marker: {}.", ec ? ec.message() : "already exists");
        return -1;
    }

    std::string error;
    fs::path currentAlbum;
    for (auto& file : plan.files) {
        file.status = "processing";
        if (file.repair && file.source.parent_path() != currentAlbum) {
            currentAlbum = file.source.parent_path();
            spdlog::info("Repairing album folder: {}", utf8(currentAlbum));
        }
        if (file.repair) {
            spdlog::info("Checking repair candidate: {}",
                utf8(file.source.lexically_relative(plan.input)));
        }
        const auto relative = file.target.lexically_relative(plan.output);
        const auto temporary = file.temporary.lexically_relative(plan.output);
        if (relative.empty() || temporary.empty() || *relative.begin() == ".." ||
            *temporary.begin() == "..") {
            file.status = "failed";
            error = "repair destination escaped its output directory";
            break;
        }
        const auto sourceSize = fs::file_size(file.source, ec);
        const auto sourceModified = fs::last_write_time(file.source, ec);
        if (ec || sourceSize != file.size || sourceModified != file.modified) {
            file.status = "failed";
            error = "source changed after repair preview: " + utf8(file.source);
            break;
        }
        fs::create_directories(resolve(relative.parent_path()), ec);
        if (ec) {
            file.status = "failed";
            error = "cannot create repair output directory: " + ec.message();
            break;
        }
        if (file.repair) {
            spdlog::info("Staging repaired copy for '{}'.", utf8(file.target.lexically_relative(plan.output)));
        }
        const auto sourcePath = file.source;
        const auto temporaryPathname = resolve(temporary);
        bool verified = false;
        if (fs::exists(temporaryPathname, ec) || ec) {
            file.status = "failed";
            error = "repair temporary already exists or cannot be inspected: " + utf8(temporaryPathname);
            break;
        }
        std::optional<RepairRules::Edit> edit;
        if (file.repair) {
            try {
                const auto relativeSource = utf8(file.source.lexically_relative(plan.input));
                auto analysis = RepairRules::Analyze(sourcePath, plan.rules);
                if (!analysis.edit || fixesOf(analysis) != file.fixes) {
                    file.status = "failed";
                    error = "repair finding changed after preview: " + utf8(sourcePath);
                    break;
                }
                edit = std::move(analysis.edit);
                spdlog::info("Prepared metadata edit for '{}': {}.", relativeSource, describeFixes(file.fixes));
                RepairRules::Write(sourcePath, temporaryPathname, *edit);
                spdlog::info("Verifying audio and metadata preservation for '{}'.", relativeSource);
                std::string verificationError;
                verified = RepairRules::Verify(sourcePath, temporaryPathname, *edit, plan.rules, verificationError);
                if (!verified) {
                    file.status = "failed";
                    error = "repair verification failed for '" + relativeSource + "': " + verificationError;
                }
            }
            catch (const std::exception& exception) {
                file.status = "failed";
                error = "cannot repair '" + utf8(sourcePath) + "': " + exception.what();
            }
        }
        else {
            fs::copy_file(sourcePath, temporaryPathname, fs::copy_options::none, ec);
            if (!ec) {
                verified = PreservationAudit::VerifyCopy(sourcePath, temporaryPathname);
                if (!verified) {
                    file.status = "failed";
                    error = "unchanged file copy verification failed: " + utf8(sourcePath);
                }
            }
            else {
                file.status = "failed";
                error = "cannot copy '" + utf8(sourcePath) + "': " + ec.message();
            }
        }
        if (!verified) {
            fs::remove(temporaryPathname, ec);
            break;
        }
        if (!MediaConvertionTask::InstallOutputFile(temporaryPathname, resolve(relative), plan.overwrite)) {
            file.status = "failed";
            error = "cannot install verified repair output: " + utf8(file.target);
            break;
        }
        if (file.repair) {
            std::string verificationError;
            if (!RepairRules::Verify(sourcePath, resolve(relative), *edit, plan.rules, verificationError)) {
                file.status = "failed";
                error = "installed repair verification failed: " + verificationError;
                break;
            }
            file.status = "installed";
            for (auto& finding : plan.findings) {
                if (finding.source == file.source && finding.status == "repairable") finding.status = "repaired";
            }
            if (repaired) ++*repaired;
            spdlog::info("Repaired '{}' ({}); audio bytes are unchanged (no decode/re-encode) and only the "
                "metadata named by these rules changed.",
                utf8(file.source.lexically_relative(plan.input)), describeFixes(file.fixes));
        }
        else {
            file.status = "installed";
            if (copied) ++*copied;
            spdlog::debug("Copied unchanged repair-folder file: {}",
                utf8(file.source.lexically_relative(plan.input)));
        }
    }

    const bool attention = std::any_of(plan.findings.begin(), plan.findings.end(),
        [](const Finding& finding) { return finding.status == "manual-review" || finding.status == "unreadable"; });
    for (const auto& finding : plan.findings) {
        if (finding.status == "manual-review" || finding.status == "unreadable") {
            spdlog::warn("Repair finding [{}] {} '{}': {}", finding.status, finding.rule,
                utf8(finding.source.lexically_relative(plan.input)), finding.detail);
        }
    }
    const auto report = resolve("repair-report.json");
    const auto reportStatus = !error.empty() ? "failed" :
        attention ? "completed-with-findings" : plan.files.empty() ? "no-repairs-needed" : "completed";
    if (!writeReport(plan, report, reportStatus, error)) {
        error = error.empty() ? "cannot write or verify repair report" : error;
    }
    if (error.empty() && outputDirectory.IsAnchored(plan.output) &&
        fs::remove(marker, ec) && !ec) {
        if (attention) {
            spdlog::warn("Repair completed with findings requiring review; report '{}'.", utf8(report));
            return 1;
        }
        spdlog::info("Repair completed: {} repaired, {} copied; report '{}'.",
            repaired ? *repaired : 0, copied ? *copied : 0, utf8(report));
        return 0;
    }
    if (error.empty()) {
        error = ec ? ec.message() : "repair output directory changed during execution";
    }
    spdlog::error("Repair failed: {}; output and in-progress marker retained at '{}'.",
        error, utf8(plan.output));
    return -1;
}
}
