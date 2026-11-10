#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace PreservationAudit
{
bool VerifyCopy(const std::filesystem::path& source, const std::filesystem::path& copy);
enum class Status
{
    Preserved,
    Normalized,
    SidecarOnly,
    MissingOrChanged,
    NotVerified,
    SourceOnly
};

struct Entry
{
    Status status;
    std::string resource;
    std::string name;
    std::string detail;
    bool fatal{ false };
};

struct Result
{
    std::vector<Entry> entries;
    bool ok{ true };
};

// verifiedArchive must mean FlacMetadata::Verify succeeded for the archive that will be installed.
Result Compare(const std::filesystem::path& source,
    const std::filesystem::path& temporaryOutput, bool verifiedArchive);
const char* StatusName(Status status);
}
