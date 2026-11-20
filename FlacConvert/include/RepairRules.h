#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Metadata-only repair rules for native FLAC and MP3 files. Rules never change audio bytes: a repaired file is the
// rebuilt metadata head, the untouched source audio byte range and the rebuilt trailing metadata.
namespace RepairRules
{
struct RuleInfo
{
    std::string id;
    std::string format;
    bool canFix{ false };
    std::string description;
};

const std::vector<RuleInfo>& Catalog();
// The ID3v1 removal rule is excluded by default for compatibility with older players.
std::set<std::string> DefaultRules();
bool IsKnownRule(const std::string& id);
bool IsSupportedFile(const std::filesystem::path& path);

struct Finding
{
    std::string rule;
    bool fix{ false };
    std::size_t count{ 0 };
    std::string detail;
};

struct Edit
{
    std::vector<std::uint8_t> head;
    std::uint64_t audioOffset{ 0 };
    std::uint64_t audioSize{ 0 };
    std::vector<std::uint8_t> tail;
};

struct Analysis
{
    std::vector<Finding> findings;
    std::optional<Edit> edit;
};

// An empty rule set enables every rule. Throws std::runtime_error when the file cannot be parsed.
Analysis Analyze(const std::filesystem::path& path, const std::set<std::string>& enabled);
void Write(const std::filesystem::path& source, const std::filesystem::path& target, const Edit& edit);
// Confirms the output is exactly the planned edit, its audio range is byte-identical to the source and no enabled
// rule still finds anything to fix.
bool Verify(const std::filesystem::path& source, const std::filesystem::path& output, const Edit& edit,
    const std::set<std::string>& enabled, std::string& error);
}
