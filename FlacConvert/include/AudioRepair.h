#pragma once

#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace AudioRepair
{
struct Finding
{
    std::filesystem::path source;
    std::size_t duplicateComments{ 0 };
    std::string status;
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
    std::size_t duplicateComments{ 0 };
    std::string status{ "not-attempted" };
};

struct Plan
{
    std::filesystem::path input;
    std::filesystem::path output;
    bool overwrite{ false };
    std::size_t inspectedFiles{ 0 };
    std::vector<std::filesystem::path> repairedFolders;
    std::vector<PlannedFile> files;
    std::vector<Finding> findings;
    std::vector<std::filesystem::path> conflicts;
};

int Prepare(const std::filesystem::path& input, const std::filesystem::path& output,
    bool overwrite, Plan& plan);
int Execute(Plan& plan, std::size_t* repaired = nullptr, std::size_t* copied = nullptr);
}
