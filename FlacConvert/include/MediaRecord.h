#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct RawMetadataTag
{
    std::string scope;
    std::string key;
    std::string value;
    std::uint32_t ordinal{ 0 };
    std::optional<std::string> frame;
    std::optional<std::string> description;
    std::optional<std::string> language;
    std::optional<int> encoding;
};

struct CatalogPicture
{
    std::optional<int> type;
    std::optional<std::string> mime;
    std::optional<std::string> description;
    std::optional<std::uint32_t> width;
    std::optional<std::uint32_t> height;
    std::uint64_t sizeBytes{ 0 };
};

struct CatalogChapter
{
    std::optional<std::int64_t> id;
    std::optional<std::string> timeBase;
    std::optional<std::int64_t> start;
    std::optional<double> startSeconds;
    std::optional<std::int64_t> end;
    std::optional<double> endSeconds;
    std::map<std::string, std::vector<std::string>> tags;
};

struct IntegrityResult
{
    std::string status{ "not_run" };
    std::optional<std::uint64_t> decodedSamples;
    std::uint64_t decodeErrors{ 0 };
    std::optional<std::int64_t> firstErrorPosition;
    std::optional<bool> flacMd5Matches;
    std::optional<std::string> fileSha256;
    std::optional<std::string> pcmSha256;
    std::optional<std::string> error;
};

struct AudioSignalResult
{
    std::string status{ "not_run" };
    std::optional<std::string> method;
    std::map<std::string, double> measurements;
    std::optional<std::uint64_t> clippedSamples;
    std::optional<int> usedBits;
    std::optional<std::string> usedBitsStatus;
    std::optional<double> leadingSilenceSeconds;
    std::optional<double> trailingSilenceSeconds;
};

struct CatalogFinding
{
    std::string code;
    std::string severity;
    std::string confidence;
    std::string evidence;
};

struct MediaDiagnostic
{
    std::string stage;
    std::string source;
    std::string code;
    std::string severity;
    std::string message;
};

struct ScanDiagnostic
{
    std::string albumPath;
    std::optional<std::string> filePath;
    MediaDiagnostic diagnostic;
    std::optional<std::string> affectedPath;
};

struct MediaRecord
{
    int schemaVersion{ 2 };
    std::string relativePath;
    std::uint64_t sizeBytes{ 0 };
    std::optional<std::string> modifiedUtc;
    std::string format;
    std::optional<double> durationSeconds;
    std::optional<int> bitRateKbps;
    std::optional<int> sampleRateHz;
    std::optional<int> channels;
    std::optional<int> declaredBitsPerSample;
    std::optional<std::uint64_t> totalSamples;
    std::optional<std::string> flacMd5;
    std::optional<std::string> tagVendor;
    std::map<std::string, std::vector<std::string>> tags;
    std::vector<RawMetadataTag> rawTags;
    std::vector<CatalogPicture> pictures;
    std::vector<CatalogChapter> chapters;
    std::string tagSupport{ "unsupported" };
    std::string nativeInventorySupport{ "unsupported" };
    std::string chapterSupport{ "unsupported" };
    std::string decodeSupport{ "not_run" };
    std::string analysisStatus{ "not_run" };
    std::string scanTier{ "catalog" };
    IntegrityResult integrity;
    AudioSignalResult signal;
    std::vector<CatalogFinding> findings;
    std::vector<MediaDiagnostic> diagnostics;
    std::optional<std::string> error;
};
