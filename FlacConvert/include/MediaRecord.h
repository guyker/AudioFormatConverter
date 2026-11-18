#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>

namespace DiagnosticCode
{
    inline constexpr const char* MetadataReadFailed = "metadata_read_failed";
    inline constexpr const char* AudioPropertiesMissing = "audio_properties_missing";
    inline constexpr const char* RecordJsonInvalid = "record_json_invalid";
    inline constexpr const char* AlbumHasNoSerializableTracks = "album_has_no_serializable_tracks";
    inline constexpr const char* FfmpegLog = "ffmpeg_log";
    inline constexpr const char* FfmpegLogUnattributed = "ffmpeg_log_unattributed";
    inline constexpr const char* FfmpegProbeFailed = "ffmpeg_probe_failed";
    inline constexpr const char* DecodeFailed = "decode_failed";
    inline constexpr const char* DecodeUnsupported = "decode_unsupported";
    inline constexpr const char* InvalidChapterTime = "invalid_chapter_time";
    inline constexpr const char* InvertedChapterTime = "inverted_chapter_time";
    inline constexpr const char* FileSizeFailed = "file_size_failed";
    inline constexpr const char* TaglibOpenFailed = "taglib_open_failed";
    inline constexpr const char* TaglibReadFailed = "taglib_read_failed";
    inline constexpr const char* ScanStagingFailed = "scan_staging_failed";
    inline constexpr const char* StagingDirectoryFailed = "staging_directory_failed";
    inline constexpr const char* StagedJsonWriteFailed = "staged_json_write_failed";
    inline constexpr const char* ScanFilesystemFailed = "scan_filesystem_failed";
    inline constexpr const char* ScanFailed = "scan_failed";
    inline constexpr const char* NoAlbumsFound = "no_albums_found";
    inline constexpr const char* OutputDirectoryUnavailable = "output_directory_unavailable";
    inline constexpr const char* GeneratedJsonInstallFailed = "generated_json_install_failed";
    inline constexpr const char* AlbumExceedsBatchTarget = "album_exceeds_batch_target";
}

enum class DiagnosticSeverity { Error, Warning, Info };

inline const char* DiagnosticSeverityName(DiagnosticSeverity severity)
{
    switch (severity) {
    case DiagnosticSeverity::Error: return "error";
    case DiagnosticSeverity::Warning: return "warning";
    case DiagnosticSeverity::Info: return "info";
    }
    throw std::logic_error("Invalid diagnostic severity");
}

inline std::optional<DiagnosticSeverity> ParseDiagnosticSeverity(std::string_view severity)
{
    if (severity == "error") return DiagnosticSeverity::Error;
    if (severity == "warning") return DiagnosticSeverity::Warning;
    if (severity == "info") return DiagnosticSeverity::Info;
    return std::nullopt;
}

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
    DiagnosticSeverity severity{ DiagnosticSeverity::Error };
    std::string message;
    std::optional<int> nativeErrorCode;
    std::optional<std::string> nativeErrorCategory;
};

struct ScanDiagnostic
{
    std::optional<std::string> albumPath;
    std::optional<std::string> filePath;
    MediaDiagnostic diagnostic;
    std::optional<std::string> affectedPath;
    std::optional<int> nativeErrorCode;
    std::optional<std::string> nativeErrorCategory;
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
