#include "AudioRepair.h"

#include "BoundedTaskRunner.h"
#include "CommonUtils.h"
#include "MediaConvertionTask.h"
#include "PlatformUtils.h"
#include "PreservationAudit.h"
#include "RepairRules.h"
#include "RunLogging.h"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <system_error>
#include <thread>

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

class RepairProgress : public RunLogging::PhaseProgress
{
public:
    RepairProgress(std::string phase, std::optional<std::size_t> total = std::nullopt,
        std::string unit = "files")
        : PhaseProgress("Repair " + std::move(phase), total, std::move(unit))
    {
    }
};

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

std::string reportJson(const AudioRepair::Plan& plan, const std::string& status, const std::string& error)
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
    writer.Key("conflicts"); writer.StartArray();
    for (const auto& conflict : plan.conflicts) writer.String(utf8(conflict).c_str());
    writer.EndArray();
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
            writer.Key(plan.dryRun ? "plannedRules" : "appliedRules"); writer.StartArray();
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
            writer.String(plan.dryRun ?
                "The planned rules would change metadata; FLAC padding may also change." :
                "Metadata named by the applied rules changed; FLAC padding may also change.");
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
    return std::string(buffer.GetString(), buffer.GetSize());
}

bool writeReport(const AudioRepair::Plan& plan, const fs::path& reportPath,
    const std::string& status, const std::string& error)
{
    const auto content = reportJson(plan, status, error);
    const auto temporary = temporaryPath(reportPath);
    std::error_code ec;
    if (fs::exists(ioPath(temporary, plan.output), ec) || ec) return false;
    std::ofstream output(ioPath(temporary, plan.output), std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    const bool success = static_cast<bool>(output);
    output.close();
    if (!success || output.fail()) return false;
    std::ifstream verify(ioPath(temporary, plan.output), std::ios::binary);
    const std::string actual{ std::istreambuf_iterator<char>(verify), std::istreambuf_iterator<char>() };
    const bool reportMatches = !verify.bad() && actual == content;
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
    const std::set<std::string>& rules, Plan& plan, std::size_t workers, bool dryRun)
{
    spdlog::info("Repair source folder: '{}'.", utf8(input));
    plan = {};
    plan.rules = rules;
    plan.workers = workers;
    plan.dryRun = dryRun;
    if (workers == 0 || workers > 16) {
        spdlog::error("Repair workers must be between 1 and 16.");
        return -1;
    }
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
    std::vector<fs::path> candidates;
    RepairProgress discovery("discovery", std::nullopt, "entries");
    try {
        for (const auto& entry : fs::recursive_directory_iterator(plan.input)) {
            discovery.Advance();
            const auto status = entry.symlink_status();
            if (fs::is_symlink(status)) {
                if (entry.is_directory(ec) && !ec) {
                    spdlog::error("Repair input contains a directory link: '{}'.", utf8(entry.path()));
                    return -1;
                }
                continue;
            }
            if (!fs::is_regular_file(status) || !RepairRules::IsSupportedFile(entry.path())) continue;
            candidates.push_back(entry.path());
        }
    }
    catch (const fs::filesystem_error& exception) {
        spdlog::error("Cannot inspect repair input: {}", exception.what());
        return -1;
    }
    std::sort(candidates.begin(), candidates.end());
    discovery.Finish();
    struct Inspection {
        std::vector<Finding> findings;
        FixList fixes;
        bool repair{ false };
    };
    std::vector<Inspection> inspections(candidates.size());
    RepairProgress inspectionProgress("inspection", candidates.size());
    try {
        BoundedTaskRunner::ForEach(candidates.size(), workers, [&](std::size_t index) {
            const auto& path = candidates[index];
            auto& inspection = inspections[index];
            try {
                const auto analysis = RepairRules::Analyze(path, plan.rules);
                for (const auto& finding : analysis.findings) {
                    inspection.findings.push_back({ path, finding.rule,
                        finding.fix ? "repairable" : "manual-review", finding.count, finding.detail });
                }
                if (analysis.edit) {
                    inspection.repair = true;
                    inspection.fixes = fixesOf(analysis);
                }
            }
            catch (const std::exception& exception) {
                inspection.findings.push_back({ path, "unreadable", "unreadable", 1, exception.what() });
            }
            inspectionProgress.Advance();
        });
    }
    catch (const std::exception& exception) {
        spdlog::error("Cannot run repair inspections: {}", exception.what());
        return -1;
    }
    inspectionProgress.Finish();
    RepairProgress planning("planning", std::nullopt, "entries");
    plan.inspectedFiles = candidates.size();
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        auto& inspection = inspections[index];
        plan.findings.insert(plan.findings.end(), std::make_move_iterator(inspection.findings.begin()),
            std::make_move_iterator(inspection.findings.end()));
        if (inspection.repair) {
            auto folder = candidates[index].parent_path().lexically_relative(plan.input);
            if (folder == ".") folder.clear();
            affected.insert(std::move(folder));
            repairSources.emplace(candidates[index], std::move(inspection.fixes));
        }
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
                planning.Advance();
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
                if (ec) {
                    spdlog::error("Cannot inspect repair source '{}': {}", utf8(entry.path()), ec.message());
                    return -1;
                }
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
    std::sort(plan.files.begin(), plan.files.end(), [](const PlannedFile& left, const PlannedFile& right) {
        return left.source < right.source;
    });

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
    if (!dryRun && (fs::exists(marker, ec) || ec)) {
        spdlog::error("Repair output has an existing in-progress marker or cannot be inspected.");
        return -1;
    }
    planning.Finish();
    return 0;
}

int PrintDryRunReport(const Plan& plan)
{
    std::cout << reportJson(plan, "dry-run", {}) << '\n' << std::flush;
    if (!std::cout) {
        spdlog::error("Cannot write the dry-run report.");
        return 1;
    }
    const bool attention = std::any_of(plan.findings.begin(), plan.findings.end(),
        [](const Finding& finding) { return finding.status == "manual-review" || finding.status == "unreadable"; });
    return attention ? 1 : 0;
}

int Execute(Plan& plan, std::size_t* repaired, std::size_t* copied)
{
    if (plan.dryRun) {
        spdlog::error("A dry-run plan cannot be executed.");
        return -1;
    }
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

    spdlog::info("Repair source folder: '{}'.", utf8(plan.input));
    std::string error;
    struct StagedFile {
        std::optional<RepairRules::Edit> edit;
        std::string error;
        bool temporaryCreated{ false };
    };
    fs::path currentAlbum;
    RepairProgress execution("installation", plan.files.size(), "verified files installed");
    for (std::size_t first = 0; first < plan.files.size() && error.empty(); first += plan.workers) {
        const auto count = (std::min)(plan.workers, plan.files.size() - first);
        std::vector<StagedFile> staged(count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& file = plan.files[first + index];
            if (file.repair && file.source.parent_path() != currentAlbum) {
                currentAlbum = file.source.parent_path();
                spdlog::info("Repairing album folder: {}", utf8(currentAlbum));
            }
        }
        try {
            BoundedTaskRunner::ForEach(count, plan.workers, [&](std::size_t index) {
                auto& file = plan.files[first + index];
                auto& result = staged[index];
                file.status = "processing";
                const auto relative = file.target.lexically_relative(plan.output);
                const auto temporary = file.temporary.lexically_relative(plan.output);
                if (relative.empty() || temporary.empty() || *relative.begin() == ".." || *temporary.begin() == "..") {
                    result.error = "repair destination escaped its output directory";
                    return;
                }
                std::error_code fileError;
                const auto sourceSize = fs::file_size(file.source, fileError);
                if (fileError) {
                    result.error = "cannot inspect repair source: " + utf8(file.source) + ": " + fileError.message();
                    return;
                }
                const auto sourceModified = fs::last_write_time(file.source, fileError);
                if (fileError || sourceSize != file.size || sourceModified != file.modified) {
                    result.error = "source changed after repair preview: " + utf8(file.source);
                    return;
                }
                fs::create_directories(resolve(relative.parent_path()), fileError);
                if (fileError) {
                    result.error = "cannot create repair output directory: " + fileError.message();
                    return;
                }
                const auto temp = resolve(temporary);
                if (fs::exists(temp, fileError) || fileError) {
                    result.error = "repair temporary already exists or cannot be inspected: " + utf8(temp);
                    return;
                }
                try {
                    if (file.repair) {
                        spdlog::info("Checking repair candidate: {}", utf8(file.source.lexically_relative(plan.input)));
                        auto analysis = RepairRules::Analyze(file.source, plan.rules);
                        if (!analysis.edit || fixesOf(analysis) != file.fixes) {
                            result.error = "repair finding changed after preview: " + utf8(file.source);
                            return;
                        }
                        result.edit = std::move(analysis.edit);
                        result.temporaryCreated = true;
                        RepairRules::Write(file.source, temp, *result.edit);
                        std::string verificationError;
                        if (!RepairRules::Verify(file.source, temp, *result.edit, plan.rules, verificationError)) {
                            result.error = "repair verification failed for '" + utf8(file.source) + "': " + verificationError;
                        }
                    }
                    else {
                        result.temporaryCreated = true;
                        if (!fs::copy_file(file.source, temp, fs::copy_options::none, fileError)) {
                            result.error = "cannot copy '" + utf8(file.source) + "': " + fileError.message();
                            return;
                        }
                        if (!PreservationAudit::VerifyCopy(file.source, temp)) {
                            result.error = "unchanged file copy verification failed: " + utf8(file.source);
                        }
                    }
                }
                catch (const std::exception& exception) {
                    result.error = "cannot stage '" + utf8(file.source) + "': " + exception.what();
                }
            });
        }
        catch (const std::exception& exception) {
            error = "cannot run repair workers: " + std::string(exception.what());
        }
        for (std::size_t index = 0; index < count && error.empty(); ++index) {
            auto& file = plan.files[first + index];
            auto& result = staged[index];
            if (!result.error.empty()) {
                file.status = "failed";
                error = result.error;
                break;
            }
            const auto temp = resolve(file.temporary.lexically_relative(plan.output));
            const auto target = resolve(file.target.lexically_relative(plan.output));
            if (!MediaConvertionTask::InstallOutputFile(temp, target, plan.overwrite)) {
                file.status = "failed";
                error = "cannot install verified repair output: " + utf8(file.target);
                break;
            }
            result.temporaryCreated = false;
            if (file.repair) {
                std::string verificationError;
                if (!RepairRules::Verify(file.source, target, *result.edit, plan.rules, verificationError)) {
                    file.status = "failed";
                    error = "installed repair verification failed: " + verificationError;
                    break;
                }
                for (auto& finding : plan.findings) {
                    if (finding.source == file.source && finding.status == "repairable") finding.status = "repaired";
                }
                if (repaired) ++*repaired;
                spdlog::info("Repaired '{}' ({}); audio bytes are unchanged (no decode/re-encode).",
                    utf8(file.source.lexically_relative(plan.input)), describeFixes(file.fixes));
            }
            else if (copied) {
                ++*copied;
            }
            file.status = "installed";
            execution.Advance();
        }
        for (std::size_t index = 0; index < count; ++index) {
            auto& file = plan.files[first + index];
            if (staged[index].temporaryCreated) {
                std::error_code cleanupError;
                fs::remove(resolve(file.temporary.lexically_relative(plan.output)), cleanupError);
                if (cleanupError) spdlog::error("Cannot remove repair temporary '{}': {}",
                    utf8(file.temporary), cleanupError.message());
            }
            if (file.status == "processing") file.status = "not-attempted";
        }
    }
    execution.Finish(error.empty());

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
