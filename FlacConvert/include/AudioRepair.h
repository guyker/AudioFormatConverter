#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Plans and executes metadata-only repairs (RepairRules) into a separate output tree; sources are never modified.
namespace AudioRepair
{
struct Finding
{
    std::filesystem::path source;
    std::string rule;
    // repairable, repaired, manual-review or unreadable.
    std::string status;
    std::size_t count{ 0 };
    std::string detail;
};

struct PlannedFile
{
    std::filesystem::path source;
    std::filesystem::path target;
    std::filesystem::path temporary;
    std::uintmax_t size{ 0 };
    std::filesystem::file_time_type modified;
    bool repair{ false };
    // Fix rule ids with their counts, re-checked against the source before writing.
    std::vector<std::pair<std::string, std::size_t>> fixes;
    std::string status{ "not-attempted" };
};

struct Plan
{
    std::filesystem::path input;
    std::filesystem::path output;
    bool overwrite{ false };
    bool dryRun{ false };
    std::size_t workers{ 4 };
    // Empty enables every rule.
    std::set<std::string> rules;
    std::size_t inspectedFiles{ 0 };
    std::vector<std::filesystem::path> repairedFolders;
    std::vector<PlannedFile> files;
    std::vector<Finding> findings;
    std::vector<std::filesystem::path> conflicts;
};

int Prepare(const std::filesystem::path& input, const std::filesystem::path& output,
    bool overwrite, const std::set<std::string>& rules, Plan& plan,
    std::size_t workers = 4, bool dryRun = false);
int Execute(Plan& plan, std::size_t* repaired = nullptr, std::size_t* copied = nullptr);
int PrintDryRunReport(const Plan& plan);
}
