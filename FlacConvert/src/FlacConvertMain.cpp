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
#include <map>
#include <memory>
#include <system_error>
#include <any>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

extern "C" {
#include <libavutil/sha.h>
}

#include "AlbumCollection.h"
#include "CommandLine.h"
#include "FolderConvert.h"
#include "FlacMetadata.h"
#include "DuplicateReview.h"

#include "MediaInformation.h"

#include "MediaConvertionTask.h"
#include "MediaConvertionAsyncTask.h"
#include "AppSettings.h"
#include "CommonUtils.h"


#include "PlatformUtils.h"

#include "ffmpeg.h"

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

bool isSameOrChildPath(const fs::path& candidate, const fs::path& base);

fs::path makeScanStagingDirectory(const fs::path& sourceRoot)
{
    std::error_code ec;
    const auto tempRoot = fs::temp_directory_path(ec);
    if (ec) throw fs::filesystem_error("Cannot locate a temporary directory", ec);
    const auto canonicalTempRoot = fs::weakly_canonical(tempRoot, ec);
    if (ec) throw fs::filesystem_error("Cannot validate the temporary directory", ec);
    const auto canonicalSource = fs::weakly_canonical(sourceRoot, ec);
    if (ec) throw fs::filesystem_error("Cannot validate the source library", ec);
    if (isSameOrChildPath(canonicalTempRoot, canonicalSource)) {
        throw std::runtime_error("The system temporary directory overlaps the source library.");
    }

    static std::atomic_uint64_t sequence{ 0 };
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        const auto candidate = tempRoot / ("afc-scan-" + std::to_string(timestamp) + "-" +
            std::to_string(++sequence) + "-" + std::to_string(attempt));
        if (!fs::create_directory(candidate, ec)) {
            if (ec) throw fs::filesystem_error("Cannot create scan staging directory", candidate, ec);
            continue;
        }
#ifndef _WIN32
        fs::permissions(candidate, fs::perms::owner_all, fs::perm_options::replace, ec);
        if (ec) {
            std::error_code cleanupError;
            fs::remove(candidate, cleanupError);
            throw fs::filesystem_error("Cannot secure scan staging directory", candidate, ec);
        }
#endif
        const auto canonicalCandidate = fs::weakly_canonical(candidate, ec);
        if (ec || isSameOrChildPath(canonicalCandidate, canonicalSource) ||
            isSameOrChildPath(canonicalSource, canonicalCandidate)) {
            std::error_code cleanupError;
            fs::remove(candidate, cleanupError);
            throw std::runtime_error("Scan staging directory overlaps the source library.");
        }
        return candidate;
    }
    throw std::runtime_error("Cannot reserve a unique scan staging directory.");
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
    auto filename = CommonUtils::path_to_utf8(entry.path().filename());
    std::ranges::transform(filename, filename.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return filename.starts_with("media_out_") &&
        MediaTrack::FileExtension(entry.path()) == u8".json";
}

struct ScanManifestBatch
{
    std::string file;
    std::uint64_t sizeBytes{ 0 };
    std::string sha256;
};

struct ScanManifestArchive
{
    std::string sourcePath;
    std::string file;
    std::uint64_t sizeBytes{ 0 };
    std::string sha256;
};

struct ScanManifestData
{
    std::string id;
    std::vector<ScanManifestBatch> batches;
    std::vector<ScanManifestArchive> archives;
    std::map<std::string, std::size_t> unsupportedFormats;
};

std::optional<std::string> sha256File(const fs::path& path)
{
    std::unique_ptr<AVSHA, decltype(&av_free)> hash(av_sha_alloc(), &av_free);
    if (!hash || av_sha_init(hash.get(), 256) < 0) return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    std::array<std::uint8_t, 64 * 1024> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            av_sha_update(hash.get(), buffer.data(), static_cast<unsigned int>(count));
        }
    }
    if (!input.eof()) return std::nullopt;
    std::array<std::uint8_t, 32> digest{};
    av_sha_final(hash.get(), digest.data());
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

std::optional<std::string> sha256Text(const std::string& value)
{
    std::unique_ptr<AVSHA, decltype(&av_free)> hash(av_sha_alloc(), &av_free);
    if (!hash || av_sha_init(hash.get(), 256) < 0) return std::nullopt;
    av_sha_update(hash.get(), reinterpret_cast<const std::uint8_t*>(value.data()),
        static_cast<unsigned int>(value.size()));
    std::array<std::uint8_t, 32> digest{};
    av_sha_final(hash.get(), digest.data());
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

bool isLowerHex(const std::string& value)
{
    return std::ranges::all_of(value, [](unsigned char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}

bool isSafeManifestBatchName(const std::string& name)
{
    return name.starts_with("media_out_") && name.ends_with(".json") &&
        fs::path(name).filename() == fs::path(name) &&
        name.find_first_of("/\\") == std::string::npos;
}

bool isSafeManifestArchiveName(const std::string& name)
{
    constexpr std::string_view prefix = "scan_metadata_";
    constexpr std::string_view suffix = ".flac-metadata";
    return name.size() == prefix.size() + 64 + suffix.size() &&
        name.starts_with(prefix) && name.ends_with(suffix) &&
        isLowerHex(name.substr(prefix.size(), 64)) &&
        name.find_first_of("/\\") == std::string::npos;
}

bool parseScanManifest(const fs::path& path, ScanManifestData& manifest)
{
    std::error_code ec;
    const auto status = fs::symlink_status(path, ec);
    if (ec || !fs::is_regular_file(status)) {
        spdlog::error("Scan manifest is not a regular file: '{}'.", pathToUtf8(path));
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        spdlog::error("Cannot open scan manifest '{}'.", pathToUtf8(path));
        return false;
    }
    rapidjson::Document document;
    rapidjson::IStreamWrapper stream(input);
    document.ParseStream(stream);
    if (document.HasParseError() || !document.IsObject() ||
        !document.HasMember("schema") || !document["schema"].IsString() ||
        std::string(document["schema"].GetString()) != "AudioFormatConverter.ScanManifest" ||
        !document.HasMember("schemaVersion") || !document["schemaVersion"].IsInt() ||
        document["schemaVersion"].GetInt() != 1 ||
        !document.HasMember("scanId") || !document["scanId"].IsString() ||
        document["scanId"].GetStringLength() == 0 ||
        !document.HasMember("batches") || !document["batches"].IsArray() ||
        document["batches"].Empty() ||
        !document.HasMember("archives") || !document["archives"].IsArray()) {
        spdlog::error("Invalid scan manifest '{}'.", pathToUtf8(path));
        return false;
    }

    ScanManifestData parsed;
    parsed.id = std::string(document["scanId"].GetString(),
        document["scanId"].GetStringLength());
    std::set<std::string> names;
    for (const auto& batch : document["batches"].GetArray()) {
        if (!batch.IsObject() || !batch.HasMember("file") || !batch["file"].IsString() ||
            !batch.HasMember("sizeBytes") || !batch["sizeBytes"].IsUint64() ||
            !batch.HasMember("sha256") || !batch["sha256"].IsString()) {
            spdlog::error("Invalid batch entry in scan manifest '{}'.", pathToUtf8(path));
            return false;
        }
        ScanManifestBatch item{
            std::string(batch["file"].GetString(), batch["file"].GetStringLength()),
            batch["sizeBytes"].GetUint64(),
            std::string(batch["sha256"].GetString(), batch["sha256"].GetStringLength())
        };
        if (!isSafeManifestBatchName(item.file) || item.sha256.size() != 64 ||
            !isLowerHex(item.sha256) || !names.insert(item.file).second) {
            spdlog::error("Unsafe or duplicate batch reference in scan manifest '{}'.", pathToUtf8(path));
            return false;
        }
        parsed.batches.push_back(std::move(item));
    }
    names.clear();
    for (const auto& archive : document["archives"].GetArray()) {
        if (!archive.IsObject() || !archive.HasMember("sourcePath") ||
            !archive["sourcePath"].IsString() || !archive.HasMember("file") ||
            !archive["file"].IsString() || !archive.HasMember("sizeBytes") ||
            !archive["sizeBytes"].IsUint64() || !archive.HasMember("sha256") ||
            !archive["sha256"].IsString()) {
            spdlog::error("Invalid archive entry in scan manifest '{}'.", pathToUtf8(path));
            return false;
        }
        ScanManifestArchive item{
            std::string(archive["sourcePath"].GetString(), archive["sourcePath"].GetStringLength()),
            std::string(archive["file"].GetString(), archive["file"].GetStringLength()),
            archive["sizeBytes"].GetUint64(),
            std::string(archive["sha256"].GetString(), archive["sha256"].GetStringLength())
        };
        if (!isSafeManifestArchiveName(item.file) || item.sha256.size() != 64 ||
            !isLowerHex(item.sha256) || !names.insert(item.file).second) {
            spdlog::error("Unsafe or duplicate archive reference in scan manifest '{}'.", pathToUtf8(path));
            return false;
        }
        parsed.archives.push_back(std::move(item));
    }
    if (document.HasMember("unsupportedFormats")) {
        if (!document["unsupportedFormats"].IsArray()) {
            spdlog::error("Invalid unsupportedFormats in scan manifest '{}'.", pathToUtf8(path));
            return false;
        }
        for (const auto& format : document["unsupportedFormats"].GetArray()) {
            if (!format.IsObject() || !format.HasMember("format") || !format["format"].IsString() ||
                !format.HasMember("count") || !format["count"].IsUint64() ||
                format["count"].GetUint64() > (std::numeric_limits<std::size_t>::max)()) {
                spdlog::error("Invalid unsupported format entry in scan manifest '{}'.", pathToUtf8(path));
                return false;
            }
            parsed.unsupportedFormats.emplace(
                std::string(format["format"].GetString(), format["format"].GetStringLength()),
                static_cast<std::size_t>(format["count"].GetUint64()));
        }
    }
    manifest = std::move(parsed);
    return true;
}

bool writeScanManifest(const fs::path& stagedDirectory, ScanManifestData& manifest)
{
    std::error_code ec;
    for (fs::directory_iterator it(stagedDirectory, ec), end; !ec && it != end; it.increment(ec)) {
        if (!isGeneratedJsonOutput(*it, ec)) continue;
        if (ec) break;
        const auto size = fs::file_size(it->path(), ec);
        const auto digest = ec ? std::nullopt : sha256File(it->path());
        if (ec || !digest) {
            spdlog::error("Cannot inventory staged scan batch '{}'.", pathToUtf8(it->path()));
            return false;
        }
        manifest.batches.push_back({
            pathToUtf8(it->path().filename()), size, *digest
        });
    }
    if (ec || manifest.batches.empty()) {
        spdlog::error("Cannot inventory all staged scan batches: {}",
            ec ? ec.message() : "no batches were written");
        return false;
    }
    std::ranges::sort(manifest.batches, {}, &ScanManifestBatch::file);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("schema"); writer.String("AudioFormatConverter.ScanManifest");
    writer.Key("schemaVersion"); writer.Int(1);
    writer.Key("scanId"); writer.String(manifest.id.data(),
        static_cast<rapidjson::SizeType>(manifest.id.size()));
    writer.Key("batchIntegrity");
    writer.String("SHA-256 of every staged media_out_*.json file; consumers must match the complete set.");
    writer.Key("archivePolicy");
    writer.String("Exact native FLAC metadata bytes only; non-native formats are listed as unsupported.");
    writer.Key("batches"); writer.StartArray();
    for (const auto& batch : manifest.batches) {
        writer.StartObject();
        writer.Key("file"); writer.String(batch.file.data(),
            static_cast<rapidjson::SizeType>(batch.file.size()));
        writer.Key("sizeBytes"); writer.Uint64(batch.sizeBytes);
        writer.Key("sha256"); writer.String(batch.sha256.data(),
            static_cast<rapidjson::SizeType>(batch.sha256.size()));
        writer.EndObject();
    }
    writer.EndArray();
    writer.Key("archives"); writer.StartArray();
    for (const auto& archive : manifest.archives) {
        writer.StartObject();
        writer.Key("sourcePath"); writer.String(archive.sourcePath.data(),
            static_cast<rapidjson::SizeType>(archive.sourcePath.size()));
        writer.Key("file"); writer.String(archive.file.data(),
            static_cast<rapidjson::SizeType>(archive.file.size()));
        writer.Key("sizeBytes"); writer.Uint64(archive.sizeBytes);
        writer.Key("sha256"); writer.String(archive.sha256.data(),
            static_cast<rapidjson::SizeType>(archive.sha256.size()));
        writer.EndObject();
    }
    writer.EndArray();
    writer.Key("unsupportedFormats"); writer.StartArray();
    for (const auto& [format, count] : manifest.unsupportedFormats) {
        writer.StartObject();
        writer.Key("format"); writer.String(format.data(),
            static_cast<rapidjson::SizeType>(format.size()));
        writer.Key("count"); writer.Uint64(count);
        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();

    const auto path = stagedDirectory / "scan-manifest.json";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
    output.close();
    if (!output) {
        spdlog::error("Cannot write scan manifest '{}'.", pathToUtf8(path));
        return false;
    }
    return true;
}

bool validateScanManifest(const fs::path& inputDirectory,
    const std::vector<fs::path>& jsonFiles, const DirectoryContentEntryList* albums)
{
    const auto path = inputDirectory / "scan-manifest.json";
    std::error_code ec;
    const auto status = fs::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory ||
        (!ec && status.type() == fs::file_type::not_found)) {
        if (albums != nullptr && std::ranges::any_of(*albums, [](const MediaAlbum& album) {
                return album.scanManifestId.has_value();
            })) {
            spdlog::error("Catalog batches refer to a missing scan manifest.");
            return false;
        }
        return true;
    }
    if (ec || !fs::is_regular_file(status)) {
        spdlog::error("Cannot safely read scan manifest '{}'.", pathToUtf8(path));
        return false;
    }
    ScanManifestData manifest;
    if (!parseScanManifest(path, manifest)) return false;
    std::map<std::string, const fs::path*> actual;
    for (const auto& file : jsonFiles) {
        std::error_code parentError;
        const bool isInInputDirectory = fs::equivalent(
            file.parent_path(), inputDirectory, parentError);
        if (parentError || !isInInputDirectory ||
            !actual.emplace(pathToUtf8(file.filename()), &file).second) {
            spdlog::error("Duplicate or unexpected JSON batch path '{}'.", pathToUtf8(file));
            return false;
        }
    }
    if (actual.size() != manifest.batches.size()) {
        spdlog::error("Scan batch set is incomplete or contains unlisted files.");
        return false;
    }
    for (const auto& batch : manifest.batches) {
        const auto found = actual.find(batch.file);
        if (found == actual.end()) {
            spdlog::error("Scan batch '{}' is missing.", batch.file);
            return false;
        }
        const auto size = fs::file_size(*found->second, ec);
        const auto digest = ec ? std::nullopt : sha256File(*found->second);
        if (ec || !digest || size != batch.sizeBytes || *digest != batch.sha256) {
            spdlog::error("Scan batch '{}' does not match its manifest.", batch.file);
            return false;
        }
    }
    for (const auto& archive : manifest.archives) {
        const auto archivePath = inputDirectory / pathFromUtf8(archive.file);
        const auto archiveStatus = fs::symlink_status(archivePath, ec);
        if (ec || !fs::is_regular_file(archiveStatus)) {
            spdlog::error("Metadata archive '{}' is missing or unsafe.", archive.file);
            return false;
        }
        const auto size = fs::file_size(archivePath, ec);
        const auto digest = ec ? std::nullopt : sha256File(archivePath);
        if (ec || !digest || size != archive.sizeBytes || *digest != archive.sha256) {
            spdlog::error("Metadata archive '{}' does not match its manifest.", archive.file);
            return false;
        }
    }
    if (albums != nullptr) {
        for (const auto& album : *albums) {
            if (!album.scanManifestId || *album.scanManifestId != manifest.id) {
                spdlog::error("Album '{}' has a missing or inconsistent scan manifest identity.",
                    pathToUtf8(album.path.path()));
                return false;
            }
        }
    }
    return true;
}

bool archiveBatchMetadata(const MediaAlbumListPtr& albums, const fs::path& libraryRoot,
    const fs::path& stagedDirectory, ScanManifestData& manifest,
    std::vector<ScanDiagnostic>& diagnostics)
{
    for (const auto& album : *albums) {
        for (const auto& track : album.trackList) {
            const auto source = album.path.path() / track.trackPath;
            if (!MediaTrack::IsFileAcceptedAudioFile(source)) continue;
            const auto fail = [&](const std::string& message) {
                spdlog::error("Cannot archive metadata for '{}': {}", pathToUtf8(source), message);
                diagnostics.push_back({
                    pathToUtf8(album.path.path()), pathToUtf8(source),
                    { "metadata_archive", "flac", DiagnosticCode::ScanMetadataArchiveFailed,
                        DiagnosticSeverity::Error, message }
                });
                return false;
            };
            if (!track.catalogRecord) return fail("Track has no catalog record.");
            std::string extension = pathToUtf8(source.extension());
            std::ranges::transform(extension, extension.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            std::string format = track.catalogRecord->format;
            std::ranges::transform(format, format.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (extension != ".flac" || format != "flac") {
                const auto unsupported = format.empty() ? extension : format;
                ++manifest.unsupportedFormats[unsupported];
                continue;
            }
            const auto relativePath = source.lexically_relative(libraryRoot);
            if (relativePath.empty() || relativePath.is_absolute() ||
                std::ranges::any_of(relativePath, [](const fs::path& component) {
                    return component == "..";
                })) {
                return fail("Track path is not inside the scanned library.");
            }
            const auto sourcePath = pathToUtf8(relativePath);
            const auto pathHash = sha256Text(sourcePath);
            if (!pathHash) return fail("Cannot calculate a stable archive name.");
            const auto archiveName = "scan_metadata_" + *pathHash + ".flac-metadata";
            const auto stagedArchive = stagedDirectory / archiveName;
            bool preparedInput = false;
            if (!FlacMetadata::Save(source, stagedArchive, MetadataSidecarFormat::Binary,
                    "", {}, preparedInput) || preparedInput ||
                !FlacMetadata::Verify(source, stagedArchive, MetadataSidecarFormat::Binary)) {
                return fail("Native FLAC metadata archive creation or verification failed.");
            }
            std::error_code ec;
            const auto size = fs::file_size(stagedArchive, ec);
            const auto digest = ec ? std::nullopt : sha256File(stagedArchive);
            if (ec || !digest) return fail("Cannot inventory the verified metadata archive.");
            manifest.archives.push_back({ sourcePath, archiveName, size, *digest });
        }
    }
    return true;
}

struct ScanReport
{
    std::string runId;
    std::string status;
    std::string scanTier;
    std::string sourceRoot;
    std::string outputDirectory;
    ScanDiscoveryCounts discovery;
    std::size_t albumsSerialized{ 0 };
    std::size_t tracksSerialized{ 0 };
    std::size_t batchesStaged{ 0 };
    std::size_t batchesInstalled{ 0 };
    std::vector<ScanDiagnostic> diagnostics;
    std::int64_t durationMs{ 0 };
};

std::string serializeScanReport(const ScanReport& report)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("schemaVersion");
    writer.Int(2);
    writer.Key("runId");
    writer.String(report.runId.data(), static_cast<rapidjson::SizeType>(report.runId.size()));
    writer.Key("status");
    writer.String(report.status.data(), static_cast<rapidjson::SizeType>(report.status.size()));
    writer.Key("scanTier");
    writer.String(report.scanTier.data(), static_cast<rapidjson::SizeType>(report.scanTier.size()));
    writer.Key("sourceRoot");
    writer.String(report.sourceRoot.data(), static_cast<rapidjson::SizeType>(report.sourceRoot.size()));
    writer.Key("outputDirectory");
    writer.String(report.outputDirectory.data(),
        static_cast<rapidjson::SizeType>(report.outputDirectory.size()));
    writer.Key("counts");
    writer.StartObject();
    writer.Key("albumsDiscovered");
    writer.Uint64(report.discovery.albums);
    writer.Key("tracksDiscovered");
    writer.Uint64(report.discovery.tracks);
    writer.Key("albumsSerialized");
    writer.Uint64(report.albumsSerialized);
    writer.Key("tracksSerialized");
    writer.Uint64(report.tracksSerialized);
    writer.Key("batchesStaged");
    writer.Uint64(report.batchesStaged);
    writer.Key("batchesInstalled");
    writer.Uint64(report.batchesInstalled);
    writer.Key("diagnostics");
    writer.Uint64(report.diagnostics.size());
    writer.EndObject();
    writer.Key("durationMs");
    writer.Int64(report.durationMs);
    writer.Key("diagnostics");
    writer.StartArray();
    for (const auto& entry : report.diagnostics) {
        writer.StartObject();
        if (entry.albumPath) {
            writer.Key("albumPath");
            writer.String(entry.albumPath->data(),
                static_cast<rapidjson::SizeType>(entry.albumPath->size()));
        }
        if (entry.filePath) {
            writer.Key("filePath");
            writer.String(entry.filePath->data(),
                static_cast<rapidjson::SizeType>(entry.filePath->size()));
        }
        if (entry.affectedPath) {
            writer.Key("affectedPath");
            writer.String(entry.affectedPath->data(),
                static_cast<rapidjson::SizeType>(entry.affectedPath->size()));
        }
        if (entry.nativeErrorCode) {
            writer.Key("nativeErrorCode");
            writer.Int(*entry.nativeErrorCode);
        }
        if (entry.nativeErrorCategory) {
            writer.Key("nativeErrorCategory");
            writer.String(entry.nativeErrorCategory->data(),
                static_cast<rapidjson::SizeType>(entry.nativeErrorCategory->size()));
        }
        writer.Key("stage");
        writer.String(entry.diagnostic.stage.data(),
            static_cast<rapidjson::SizeType>(entry.diagnostic.stage.size()));
        writer.Key("source");
        writer.String(entry.diagnostic.source.data(),
            static_cast<rapidjson::SizeType>(entry.diagnostic.source.size()));
        writer.Key("code");
        writer.String(entry.diagnostic.code.data(),
            static_cast<rapidjson::SizeType>(entry.diagnostic.code.size()));
        writer.Key("severity");
        writer.String(DiagnosticSeverityName(entry.diagnostic.severity));
        writer.Key("message");
        writer.String(entry.diagnostic.message.data(),
            static_cast<rapidjson::SizeType>(entry.diagnostic.message.size()));
        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();
    if (!writer.IsComplete()) {
        throw std::runtime_error("Could not serialize scan diagnostics report.");
    }
    return std::string(buffer.GetString(), buffer.GetSize());
}

bool installScanReport(const fs::path& targetDirectory, const fs::path& libraryRoot,
    const ScanReport& report,
    PlatformUtils::ScopedCurrentDirectory* existingDirectoryAnchor = nullptr)
{
    PlatformUtils::ScopedCurrentDirectory ownedDirectoryAnchor;
    auto* outputDirectory = existingDirectoryAnchor != nullptr
        ? existingDirectoryAnchor : &ownedDirectoryAnchor;
    if (existingDirectoryAnchor == nullptr) {
        if (!outputDirectory->EnterValidatedDirectory(targetDirectory, libraryRoot, true)) {
            spdlog::error("Cannot safely enter scan report output '{}'.", pathToUtf8(targetDirectory));
            return false;
        }
    }
    else if (!outputDirectory->IsAnchored(targetDirectory)) {
        spdlog::error("Scan report output is no longer anchored at '{}'.", pathToUtf8(targetDirectory));
        return false;
    }
    const auto temporaryName =
        uniqueSiblingPath(targetDirectory / "scan-report.json", ".scan-report-").filename();
    const auto temporary = outputDirectory->ResolvePath(temporaryName);
    const auto target = outputDirectory->ResolvePath("scan-report.json");
    bool written = false;
    try {
        const auto json = serializeScanReport(report);
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("Could not open temporary scan report.");
        }
        file.write(json.data(), static_cast<std::streamsize>(json.size()));
        file.close();
        if (!file) {
            throw std::runtime_error("Could not write or close temporary scan report.");
        }
        written = true;
    }
    catch (const std::exception& error) {
        spdlog::error("Cannot write scan report: {}", error.what());
    }
    const bool anchored = outputDirectory->IsAnchored(targetDirectory);
    const bool installed = written && anchored &&
        MediaConvertionTask::InstallOutputFile(temporary, target, true);
    if (!installed) {
        std::error_code ec;
        fs::remove(temporary, ec);
    }
    if (!anchored) {
        spdlog::error("Scan report output directory changed during writing.");
        return false;
    }
    if (existingDirectoryAnchor == nullptr && !outputDirectory->Restore()) {
        spdlog::error("Cannot restore the working directory after scan report writing.");
        return false;
    }
    return installed;
}

bool writeFallbackScanReport(const ScanReport& report,
    const std::vector<MediaDirectoryElement>& libraries)
{
    try {
        std::error_code ec;
        const auto temporaryRoot = fs::weakly_canonical(fs::temp_directory_path(), ec);
        if (ec) throw fs::filesystem_error("Cannot validate temporary directory", ec);
        for (const auto& library : libraries) {
            const auto source = fs::weakly_canonical(pathFromUtf8(library.mediaPath), ec);
            if (ec || isSameOrChildPath(temporaryRoot, source)) {
                spdlog::error("Cannot safely write a fallback report outside all source libraries.");
                return false;
            }
        }
        const auto source = pathFromUtf8(report.sourceRoot);
        const auto directory = makeScanStagingDirectory(source);
        const auto canonicalDirectory = fs::weakly_canonical(directory, ec);
        bool safe = !ec;
        for (const auto& library : libraries) {
            const auto libraryRoot = fs::weakly_canonical(pathFromUtf8(library.mediaPath), ec);
            if (ec || isSameOrChildPath(canonicalDirectory, libraryRoot) ||
                isSameOrChildPath(libraryRoot, canonicalDirectory)) {
                safe = false;
                break;
            }
        }
        if (!safe || !installScanReport(directory, source, report)) {
            fs::remove(directory, ec);
            spdlog::error("Cannot save the fallback scan report.");
            return false;
        }
        spdlog::error("Fallback scan report saved to '{}'.", pathToUtf8(directory / "scan-report.json"));
        return true;
    }
    catch (const std::exception& error) {
        spdlog::error("Cannot save the fallback scan report: {}", error.what());
        return false;
    }
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
    if (!validateScanManifest(stagedDirectory, stagedFiles, nullptr)) {
        spdlog::error("Staged scan files do not match their manifest.");
        removeCreatedDirectory();
        return false;
    }

    ScanManifestData stagedManifest;
    const auto stagedManifestPath = stagedDirectory / "scan-manifest.json";
    if (!parseScanManifest(stagedManifestPath, stagedManifest)) {
        removeCreatedDirectory();
        return false;
    }
    std::set<std::string> previousNames;
    for (const auto& file : previousCandidates) {
        previousNames.insert(pathToUtf8(file.filename()));
    }

    const auto previousManifestPath = targetDirectory / "scan-manifest.json";
    const auto previousManifestStatus = fs::symlink_status(previousManifestPath, ec);
    if (ec == std::errc::no_such_file_or_directory ||
        (!ec && previousManifestStatus.type() == fs::file_type::not_found)) {
        ec.clear();
    }
    else if (ec || !fs::is_regular_file(previousManifestStatus)) {
        spdlog::error("Refusing to replace an unsafe prior scan manifest '{}'.",
            pathToUtf8(previousManifestPath));
        removeCreatedDirectory();
        return false;
    }
    else {
        ScanManifestData previousManifest;
        if (!parseScanManifest(previousManifestPath, previousManifest)) {
            removeCreatedDirectory();
            return false;
        }
        previousCandidates.push_back(previousManifestPath);
        previousNames.insert("scan-manifest.json");
        for (const auto& archive : previousManifest.archives) {
            const auto previousArchivePath = targetDirectory / pathFromUtf8(archive.file);
            const auto archiveStatus = fs::symlink_status(previousArchivePath, ec);
            if (ec == std::errc::no_such_file_or_directory ||
                (!ec && archiveStatus.type() == fs::file_type::not_found)) {
                ec.clear();
                continue;
            }
            if (ec || !fs::is_regular_file(archiveStatus)) {
                spdlog::error("Refusing to replace unsafe prior metadata archive '{}'.",
                    pathToUtf8(previousArchivePath));
                removeCreatedDirectory();
                return false;
            }
            previousCandidates.push_back(previousArchivePath);
            previousNames.insert(archive.file);
        }
    }

    for (const auto& archive : stagedManifest.archives) {
        const auto stagedArchivePath = stagedDirectory / pathFromUtf8(archive.file);
        const auto targetArchivePath = targetDirectory / pathFromUtf8(archive.file);
        const auto targetStatus = fs::symlink_status(targetArchivePath, ec);
        if (ec == std::errc::no_such_file_or_directory ||
            (!ec && targetStatus.type() == fs::file_type::not_found)) {
            ec.clear();
        }
        else if (ec || !fs::is_regular_file(targetStatus) ||
            !previousNames.contains(archive.file)) {
            spdlog::error("Refusing to overwrite an unrelated metadata archive '{}'.",
                pathToUtf8(targetArchivePath));
            removeCreatedDirectory();
            return false;
        }
        stagedFiles.push_back(stagedArchivePath);
    }
    stagedFiles.push_back(stagedManifestPath);

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
        const auto temporary = uniqueSiblingPath(targetDirectory, ".scan-install-");
        if (!fs::copy_file(stagedFile, temporary, fs::copy_options::none, ec) || ec) {
            spdlog::error("Cannot stage generated JSON output '{}': {}",
                pathToUtf8(installed), ec ? ec.message() : "temporary path already exists");
            rollback();
            return false;
        }
        if (!MediaConvertionTask::InstallOutputFile(temporary, installed, false)) {
            std::error_code cleanupError;
            fs::remove(temporary, cleanupError);
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
        if (!entry.is_regular_file() || MediaTrack::FileExtension(entry.path()) != u8".json") {
            continue;
        }
        auto filename = CommonUtils::path_to_utf8(entry.path().filename());
        std::ranges::transform(filename, filename.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (filename.starts_with("media_out_") &&
            !filename.ends_with("_error.json")) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::map<std::string, MediaRecord> loadCatalogCache(
    const fs::path& directory, const fs::path& libraryRoot)
{
    std::map<std::string, MediaRecord> cache;
    const auto files = findJsonFiles(directory);
    if (files.empty()) {
        validateScanManifest(directory, files, nullptr);
        return cache;
    }

    const auto normalizedRoot = fs::absolute(libraryRoot).lexically_normal();
    AlbumCollection collection;
    auto allAlbums = std::make_shared<DirectoryContentEntryList>();
    for (const auto& file : files) {
        const auto albums = collection.LoadAlbumsFromJSON(file);
        if (!albums) {
            spdlog::warn("Ignoring the incremental metadata cache because '{}' could not be read.",
                pathToUtf8(file));
            return {};
        }
        allAlbums->insert(allAlbums->end(), albums->begin(), albums->end());
    }
    if (!validateScanManifest(directory, files, allAlbums.get())) {
        spdlog::warn("Ignoring the incremental metadata cache because its scan manifest is incomplete.");
        return {};
    }
    if (!collection.ReassembleAlbums(allAlbums)) {
        spdlog::warn("Ignoring the incremental metadata cache because its albums are incomplete.");
        return {};
    }
    for (const auto& album : *allAlbums) {
        for (const auto& track : album.trackList) {
            if (!track.catalogRecord || !track.libraryRoot ||
                fs::absolute(*track.libraryRoot).lexically_normal() != normalizedRoot) {
                spdlog::warn("Ignoring the incremental metadata cache because its library identity does not match.");
                return {};
            }
            const auto sourcePath = fs::absolute(
                album.path.path() / track.trackPath).lexically_normal();
            if (!cache.emplace(pathToUtf8(sourcePath), *track.catalogRecord).second) {
                spdlog::warn("Ignoring the incremental metadata cache because it contains duplicate file identities.");
                return {};
            }
        }
    }
    return cache;
}

std::vector<fs::path> findConfiguredJsonFiles(
    const std::vector<MediaDirectoryElement>& mediaList,
    const fs::path& outputRoot)
{
    std::vector<fs::path> files;
    for (const auto& mediaEntry : mediaList) {
        const auto name = mediaEntry.mediaName && !mediaEntry.mediaName->empty()
            ? *mediaEntry.mediaName : directoryNameUtf8(pathFromUtf8(mediaEntry.mediaPath));
        const auto jsonFiles = findJsonFiles(outputRoot / pathFromUtf8(name));
        files.insert(files.end(), jsonFiles.begin(), jsonFiles.end());
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
    bool reviewStopped{ false };
    std::size_t movedAlbums{ 0 };
    std::size_t skippedDuplicatePairs{ 0 };
    std::chrono::steady_clock::time_point startTime;

    void Start()
    {
        started = true;
        startTime = std::chrono::steady_clock::now();
    }

    void Print(int result) const
    {
        const auto totalCompleted = completed + conversion.converted;
        const char* status = result == 0 ? (reviewStopped ? "Stopped by user" : "Completed") :
            totalCompleted != 0 || conversion.copied != 0 ? "Partially completed" : "Failed";
        const auto elapsed = started ? std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count() : 0;
        std::cout << "\nCompletion summary: " << status
            << "\n  Completed: " << totalCompleted << ' ' << unit
            << "; copied: " << conversion.copied << " files unchanged"
            << "; failed: " << (result == 0 ? 0 : 1)
            << (started ? " work items" : " preflight checks")
            << "\n  Elapsed: " << elapsed << " ms\n";
        for (const auto& output : outputs) {
            std::cout << "  Output location: " << pathToUtf8(output) << '\n';
        }
        if (outputDescription) {
            std::cout << "  Output: " << outputDescription << '\n';
        }
        if (movedAlbums || skippedDuplicatePairs) {
            std::cout << "  Albums moved to deleted: " << movedAlbums
                << "; pairs skipped after moves: " << skippedDuplicatePairs << '\n';
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

const char* sidecarFormatName(MetadataSidecarFormat format)
{
    return format == MetadataSidecarFormat::Json ? "JSON (.flac-metadata.json)" :
        format == MetadataSidecarFormat::Binary ? "binary (.flac-metadata)" : "none";
}

// Applies MetadataSidecars from the configuration; an optional policy asks the user (default YES).
MenuConfirmation ConfigureMenuSidecars(ConversionOptions& options)
{
    const auto settings = AppSettingsJson::AppSetting();
    const MetadataSidecarSettings sidecars = settings ? settings->MetadataSidecars : MetadataSidecarSettings{};
    const auto policy = sidecars.PolicyFor(options.target);
    std::string error;
    options.sidecarFormat = sidecars.Resolve(options.target, false, error).value_or(MetadataSidecarFormat::None);
    if (policy != MetadataSidecarPolicy::Optional) {
        return MenuConfirmation::Run;
    }
    for (;;) {
        std::string response;
        const std::string prompt = std::string("Write original-metadata sidecars, format ") +
            sidecarFormatName(sidecars.Format) + " [YES] (YES/NO): ";
        const auto choice = ReadManualSetting(prompt.c_str(), response);
        if (choice != MenuConfirmation::Run) {
            return choice;
        }
        if (response.empty() || response == "YES") {
            return MenuConfirmation::Run;
        }
        if (response == "NO") {
            options.sidecarFormat = MetadataSidecarFormat::None;
            return MenuConfirmation::Run;
        }
        std::cout << "Invalid selection. Enter YES or NO.\n";
    }
}

void ShowConversionPreview(const FolderConversionPlan& plan)
{
    const bool sidecars = plan.options.sidecarFormat != MetadataSidecarFormat::None;
    std::cout << "\nOperation preview: "
        << (plan.options.target == AudioTarget::Mp3 ? "FLAC to MP3" : "FLAC conversion") << '\n'
        << "  Input: " << pathToUtf8(plan.input) << '\n'
        << "  Output: " << pathToUtf8(plan.output) << '\n'
        << "  Audio files: " << plan.audio.size()
        << (plan.options.target == AudioTarget::Mp3 ? "; companions: " : "; files copied unchanged: ")
        << plan.companions.size()
        << "; metadata sidecars: " << (sidecars ? std::count_if(plan.audio.begin(), plan.audio.end(),
            [&](const PlannedFileConversion& file) {
                return FlacMetadata::AppliesTo(file.source, plan.options);
            }) : 0)
        << " (" << sidecarFormatName(plan.options.sidecarFormat) << ")\n"
        << "  Existing destinations: " << plan.existingTargets.size()
        << "; duplicate destinations: " << plan.collisions.size() << '\n'
        << "  Overwrite: " << (plan.overwrite ? "enabled" : "disabled") << '\n';
    if (plan.options.target == AudioTarget::Flac) {
        std::cout << "  Source directories written: " << plan.directories.size()
            << "; unchanged folders skipped (audio, no conversions): " << plan.skippedDirectories.size() << '\n';
        for (const auto& skipped : plan.skippedDirectories) {
            std::cout << "    Skipped: " << pathToUtf8(skipped.source) << " (" << skipped.files << " files)\n";
        }
    }
    for (const auto& target : plan.existingTargets) {
        std::cout << "    Existing: " << pathToUtf8(target) << '\n';
    }
    for (const auto& target : plan.collisions) {
        std::cout << "    COLLISION: " << pathToUtf8(target) << '\n';
    }
}
}

int ConvertConfiguredLibraries(const std::vector<MediaDirectoryElement>& mediaDirectoryList,
    const fs::path& outputRoot, ManualOperationSummary& summary, ConversionOptions options, bool overwrite)
{
    summary.unit = "audio files";
    if (mediaDirectoryList.empty()) {
        spdlog::error("No active media libraries configured for conversion.");
        return 1;
    }
    const bool mp3 = options.target == AudioTarget::Mp3;
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
        const fs::path target = outputRoot / (mp3 ? "MP3" : "FLAC") / name;
        summary.outputs.push_back(absoluteDisplayPath(target));
        const fs::path canonicalTarget = fs::weakly_canonical(target, ec);
        if (ec) {
            spdlog::error("Cannot resolve conversion output directory: {}", ec.message());
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
                spdlog::error("Conversion output overlaps a source library or another output: '{}'.",
                    pathToUtf8(conversions[i].target));
                return 1;
            }
        }
    }
    spdlog::info("Conversion target: {}; overwrite {}.", mp3 ? "MP3" : "FLAC",
        overwrite ? "enabled" : "disabled");
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
        spdlog::error("Conversion destination conflicts; no conversion was started.");
        return 1;
    }
    if (mp3) {
        std::cout << "  Quality: LAME VBR " << options.mp3Quality << "; "
        << (options.downmixToStereo ? "multichannel stereo downmix enabled" : "mono/stereo only")
        << "; 44.1/48 kHz retained, other accepted rates -> 48 kHz.\n"
        "  Source quality/channel properties are verified during conversion.\n";
    } else {
        std::cout << "  Quality: 16-bit / 44.1 kHz, triangular dithering; channel count retained.\n"
            "  Tags and supported pictures mapped; original native FLAC metadata retained in sidecars\n"
            "  when enabled (otherwise any metadata the FLAC cannot carry stops the conversion).\n"
            "  Written folders keep every file; other audio (e.g. MP3) beside conversions is copied unchanged.\n"
            "  Folders with audio but nothing to convert are skipped and listed in conversion-audit.json.\n"
            "  Sources unchanged. Source quality is verified during conversion.\n";
    }
    const auto confirmation = ConfirmPreview();
    if (confirmation != MenuConfirmation::Run) {
        return cancelledMenuResult(confirmation);
    }
    summary.Start();
    for (std::size_t index = 0; index < plans.size(); ++index) {
        const int result = FolderConvert::ExecuteConversion(plans[index], &summary.conversion);
        if (!summary.conversion.actualOutput.empty())
            summary.outputs[index] = summary.conversion.actualOutput;
        if (result != 0) {
            return 1;
        }
    }
    return 0;
}

int ConvertFLACToFLAC(const std::vector<MediaDirectoryElement>& libraries, const fs::path& outputRoot,
    ManualOperationSummary& summary)
{
    ConversionOptions options{};
    const auto sidecarChoice = ConfigureMenuSidecars(options);
    if (sidecarChoice != MenuConfirmation::Run) {
        return cancelledMenuResult(sidecarChoice);
    }
    return ConvertConfiguredLibraries(libraries, outputRoot, summary, options, false);
}

int ConvertFLACToMP3(const std::vector<MediaDirectoryElement>& libraries, const fs::path& outputRoot,
    ManualOperationSummary& summary)
{
    ConversionOptions options{ AudioTarget::Mp3, 0, false };
    bool overwrite = false;
    auto settingsChoice = ConfigureManualMp3(options, overwrite);
    if (settingsChoice == MenuConfirmation::Run) {
        settingsChoice = ConfigureMenuSidecars(options);
    }
    if (settingsChoice != MenuConfirmation::Run) {
        return cancelledMenuResult(settingsChoice);
    }
    return ConvertConfiguredLibraries(libraries, outputRoot, summary, options, overwrite);
}

int ScanFolderAndCreateJSON(std::vector<MediaDirectoryElement> mediaDirectoryList, bool bIncludeMetadata,
    const fs::path& outputRoot, bool preview = false, ManualOperationSummary* summary = nullptr,
    bool fullRescan = false)
{
    if (summary) {
        summary->unit = "album JSON files";
    }
    const auto reportPreflight = [&](const fs::path& source, const fs::path& target,
        const std::string& message, const fs::path& affected, const std::error_code& error = {}) {
        ScanReport report;
        report.runId = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        report.status = "failed";
        report.scanTier = AppSettingsJson::AppSetting()->ScanTier;
        report.sourceRoot = pathToUtf8(source.empty() ? fs::path(".") : source);
        report.outputDirectory = pathToUtf8(target.empty() ? fs::path(".") : target);
        report.diagnostics.push_back({
            std::nullopt, std::nullopt,
            { "preflight", "filesystem", DiagnosticCode::ScanPreflightFailed,
                DiagnosticSeverity::Error, message },
            affected.empty() ? std::nullopt : std::optional<std::string>(pathToUtf8(affected)),
            error ? std::optional<int>(error.value()) : std::nullopt,
            error ? std::optional<std::string>(error.category().name()) : std::nullopt
        });
        writeFallbackScanReport(report, mediaDirectoryList);
    };
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
                reportPreflight(source, outputRoot, "Library name must be a single directory name.", name);
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
                reportPreflight(source, target, "Cannot resolve scan destination: " + ec.message(), target, ec);
                return 1;
            }
            for (const auto& other : mediaDirectoryList) {
                const auto sourceKey = fs::weakly_canonical(pathFromUtf8(other.mediaPath), ec);
                if (ec || isSameOrChildPath(key, sourceKey) || isSameOrChildPath(sourceKey, key)) {
                    spdlog::error("Scan destination overlaps an active source library: '{}'.", pathToUtf8(target));
                    reportPreflight(source, target, "Scan destination overlaps an active source library.", target, ec);
                    return 1;
                }
            }
            if (std::any_of(targets.begin(), targets.end(), [&](const fs::path& other) {
                    return isSameOrChildPath(key, other) || isSameOrChildPath(other, key);
                })) {
                spdlog::error("Scan library output collision: '{}'.", pathToUtf8(target));
                reportPreflight(source, target, "Scan library output collision.", target);
                return 1;
            }
            targets.push_back(key);
            std::size_t previous = 0;
            const bool targetExists = fs::exists(target, ec);
            if (ec || (targetExists && !fs::is_directory(target, ec)) || ec) {
                spdlog::error("Cannot inspect scan destination: '{}'.", pathToUtf8(target));
                reportPreflight(source, target, "Cannot inspect scan destination.", target, ec);
                return 1;
            }
            if (targetExists) {
                for (fs::directory_iterator it(target, ec), end; !ec && it != end; it.increment(ec)) {
                    if (isGeneratedJsonOutput(*it, ec)) {
                        ++previous;
                    }
                }
                if (ec) {
                    spdlog::error("Cannot inspect existing JSON: {}", ec.message());
                    reportPreflight(source, target, "Cannot inspect existing JSON: " + ec.message(),
                        target, ec);
                    return 1;
                }
            }
            std::vector<MediaAlbumListPtr> batches;
            std::size_t albumCount = 0;
            std::size_t tracks = 0;
            AlbumCollection collection;
            try {
                for (auto albums : collection.LoadAlbumsCo(
                    source, false, AppSettingsJson::AppSetting()->AlbumsSplitThreshold)) {
                    albumCount += albums->size();
                    for (const auto& album : *albums) {
                        tracks += album.trackList.size();
                    }
                    batches.push_back(albums);
                }
            }
            catch (const fs::filesystem_error& error) {
                spdlog::error("Scan preview discovery failed: {}", error.what());
                reportPreflight(source, target, error.what(), error.path1(), error.code());
                return 1;
            }
            std::cout << "\nOperation preview: scan to JSON\n"
                << "  Input: " << pathToUtf8(source) << '\n'
                << "  Output: " << pathToUtf8(target) << '\n'
                << "  Diagnostics: " << pathToUtf8(target / "scan-report.json") << '\n'
                << "  Albums: " << albumCount << "; tracks: " << tracks
                << "; metadata batches: " << batches.size() << '\n'
                << "  JSON output count is determined during serialization.\n"
                << "  Existing generated JSON files to replace: " << previous << '\n'
                << "  Overwrite: generated JSON only; databases and sources unchanged.\n";
            if (batches.empty()) {
                spdlog::error("No albums found; no output will be replaced.");
                reportPreflight(source, target, "No albums found; no output will be replaced.", source);
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
        const auto preflightFailure = [&](const std::string& message, const fs::path& affected,
            const std::error_code& error = {}) {
            reportPreflight(libraryPath, outputRoot / libraryName, message, affected, error);
        };
        if (libraryName.empty() || libraryName == "." || libraryName == ".." ||
            libraryName.has_root_path() || libraryName.has_parent_path()) {
            spdlog::error("Library name must be a single directory name: '{}'.", mediaEntry.mediaName.value());
            preflightFailure("Library name must be a single directory name.", libraryName);
            return 1;
        }
        std::error_code ec;
        const fs::path targetDirectory = fs::absolute(outputRoot / libraryName, ec);
        if (ec) {
            spdlog::error("Cannot resolve scan output directory: {}", ec.message());
            preflightFailure("Cannot resolve scan output directory: " + ec.message(), outputRoot, ec);
            return 1;
        }
        const fs::path libraryIoPath = fs::absolute(libraryPath, ec);
        if (ec) {
            spdlog::error("Cannot resolve library path: {}", ec.message());
            preflightFailure("Cannot resolve library path: " + ec.message(), libraryPath, ec);
            return 1;
        }
        const fs::path canonicalLibrary = fs::weakly_canonical(libraryIoPath, ec);
        if (ec || !fs::is_directory(canonicalLibrary, ec) || ec) {
            spdlog::error("Library path is not a readable directory: {}", pathToUtf8(libraryPath));
            preflightFailure("Library path is not a readable directory.", libraryPath, ec);
            return 1;
        }
        const fs::path canonicalTarget = fs::weakly_canonical(targetDirectory, ec);
        if (ec) {
            spdlog::error("Cannot resolve scan output '{}': {}", pathToUtf8(targetDirectory), ec.message());
            preflightFailure("Cannot resolve scan output: " + ec.message(), targetDirectory, ec);
            return 1;
        }
        if (isSameOrChildPath(canonicalTarget, canonicalLibrary) ||
            isSameOrChildPath(canonicalLibrary, canonicalTarget)) {
            spdlog::error("Scan output directory must not overlap the source library: '{}'.", pathToUtf8(targetDirectory));
            preflightFailure("Scan output directory overlaps the source library.", targetDirectory);
            return 1;
        }
        ScanReport scanReport;
        scanReport.runId = std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count());
        scanReport.status = "failed";
        scanReport.scanTier = AppSettingsJson::AppSetting()->ScanTier;
        scanReport.sourceRoot = pathToUtf8(libraryIoPath);
        scanReport.outputDirectory = pathToUtf8(targetDirectory);
        FFmpeg::ScopedRunLogCapture unscopedFfmpegLogs("scan");
        const auto appendUnscopedFfmpegLogs = [&] {
            for (const auto& log : unscopedFfmpegLogs.TakeLogs()) {
                scanReport.diagnostics.push_back({
                    std::nullopt,
                    std::nullopt,
                    { log.stage, "ffmpeg", DiagnosticCode::FfmpegLogUnattributed, ParseDiagnosticSeverity(log.level).value(),
                        log.component + ": " + log.message },
                    std::nullopt
                });
            }
        };
        const auto writeFailureReport = [&](
            PlatformUtils::ScopedCurrentDirectory* outputAnchor) {
            appendUnscopedFfmpegLogs();
            scanReport.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            if (!installScanReport(
                    targetDirectory, libraryIoPath, scanReport, outputAnchor)) {
                spdlog::error("Scan diagnostics could not be saved to '{}'.",
                    pathToUtf8(targetDirectory / "scan-report.json"));
                scanReport.diagnostics.push_back({
                    std::nullopt, std::nullopt,
                    { "report", "filesystem", DiagnosticCode::ScanReportWriteFailed,
                        DiagnosticSeverity::Error, "Could not install the scan report at its normal location." },
                    pathToUtf8(targetDirectory / "scan-report.json")
                });
                writeFallbackScanReport(scanReport, mediaDirectoryList);
            }
        };
        const auto settings = AppSettingsJson::AppSetting();
        const bool useIncrementalCache = settings->Incremental && !fullRescan;
        const auto cachedRecords = useIncrementalCache
            ? loadCatalogCache(targetDirectory, libraryIoPath)
            : std::map<std::string, MediaRecord>{};
        if (useIncrementalCache) {
            spdlog::info("Incremental scan cache contains {} catalog record(s).",
                cachedRecords.size());
        }
        fs::path stagingRoot;
        try {
            stagingRoot = makeScanStagingDirectory(libraryIoPath);
        }
        catch (const fs::filesystem_error& error) {
            spdlog::error("Cannot create scan staging directory: {}", error.what());
            const auto affected = !error.path1().empty() ? error.path1() : error.path2();
            scanReport.diagnostics.push_back({
                std::nullopt, std::nullopt,
                { "discovery", "filesystem", DiagnosticCode::ScanStagingFailed, DiagnosticSeverity::Error, error.what() },
                affected.empty() ? std::nullopt : std::optional<std::string>(pathToUtf8(affected)),
                error.code().value(), error.code().category().name()
            });
            writeFailureReport(nullptr);
            return 1;
        }
        catch (const std::exception& error) {
            spdlog::error("Cannot create scan staging directory: {}", error.what());
            scanReport.diagnostics.push_back({
                std::nullopt,
                std::nullopt,
                { "discovery", "filesystem", DiagnosticCode::ScanStagingFailed, DiagnosticSeverity::Error, error.what() }
            });
            writeFailureReport(nullptr);
            return 1;
        }
        const auto cleanupStaging = [&] {
            std::error_code cleanupError;
            fs::remove_all(stagingRoot, cleanupError);
            if (cleanupError) {
                spdlog::warn("Cannot clean scan staging directory '{}': {}",
                    pathToUtf8(stagingRoot), cleanupError.message());
            }
        };
        const fs::path stagedDirectory = stagingRoot / libraryName;
        fs::create_directories(stagedDirectory, ec);
        if (ec) {
            spdlog::error("Cannot create staging directory '{}': {}", pathToUtf8(stagedDirectory), ec.message());
            scanReport.diagnostics.push_back({
                std::nullopt,
                std::nullopt,
                { "serialization", "filesystem", DiagnosticCode::StagingDirectoryFailed, DiagnosticSeverity::Error, ec.message() },
                pathToUtf8(stagedDirectory), ec.value(), ec.category().name()
            });
            writeFailureReport(nullptr);
            cleanupStaging();
            return 1;
        }

        AlbumCollection ac;
        ScanManifestData scanManifest;
        scanManifest.id = scanReport.runId;
        int count = 0;
        try {
            const auto writeBatch = [&](const MediaAlbumListPtr& albumListPtr) {
                spdlog::info("Received batch with {} albums", albumListPtr->size());
                if (bIncludeMetadata) {
                    ac.ImportMetadata(albumListPtr, 0, albumListPtr->size(),
                        settings->UseAsyncMetadataReads,
                        useIncrementalCache ? &cachedRecords : nullptr);
                }
                for (auto& album : *albumListPtr) {
                    album.scanManifestId = scanManifest.id;
                }
                ac.SortAlbums(albumListPtr, { { SortBy::AlbumArtist, true } });
                std::ofstream output;
                fs::path outputPath;
                std::uint64_t outputBytes = 0;
                std::size_t outputAlbums = 0;
                const auto reportWriteFailure = [&](const std::string& message,
                    const std::optional<fs::path>& albumPath = std::nullopt) {
                    scanReport.diagnostics.push_back({
                        albumPath ? std::optional<std::string>(pathToUtf8(*albumPath)) : std::nullopt,
                        std::nullopt,
                        { "serialization", "filesystem", DiagnosticCode::StagedJsonWriteFailed,
                            DiagnosticSeverity::Error, message },
                        pathToUtf8(outputPath)
                    });
                };
                const auto finishOutput = [&] {
                    if (!output.is_open()) return true;
                    output.put(']');
                    output.close();
                    if (!output) {
                        spdlog::error("Failed to write or close staged album JSON.");
                        reportWriteFailure("Failed to write or close staged album JSON.");
                        return false;
                    }
                    ++scanReport.batchesStaged;
                    outputAlbums = 0;
                    outputBytes = 0;
                    return true;
                };
                const auto writeAlbum = [&](const fs::path& albumPath,
                    const std::optional<fs::path>& trackPath, const std::string& json,
                    std::size_t trackCount, bool lastFragment) {
                    const auto albumBytes = static_cast<std::uint64_t>(json.size());
                    const auto target = settings->JsonBatchTargetBytes;
                    if (target > 0 && (target < 2 || albumBytes > target - 2)) {
                        spdlog::error("Track record exceeds the configured JSON byte limit: '{}'.",
                            pathToUtf8(trackPath.value_or(albumPath)));
                        scanReport.diagnostics.push_back({
                            pathToUtf8(albumPath),
                            trackPath ? std::optional<std::string>(pathToUtf8(*trackPath)) : std::nullopt,
                            { "serialization", "catalog", DiagnosticCode::TrackExceedsBatchLimit,
                                DiagnosticSeverity::Error, "Track record cannot fit the JSON batch byte limit." }
                        });
                        return false;
                    }
                    if (outputAlbums > 0 && target > 0 &&
                        (outputBytes >= target || albumBytes >= target - outputBytes ||
                            target - outputBytes - albumBytes < 2)) {
                        if (!finishOutput()) return false;
                    }
                    if (!output.is_open()) {
                        outputPath = mediaEntry.getMediaJsonPath(stagingRoot, ++count);
                        output.open(outputPath, std::ios::binary | std::ios::trunc);
                        if (!output) {
                            spdlog::error("Cannot open staged JSON '{}'.", pathToUtf8(outputPath));
                            reportWriteFailure("Cannot open staged album JSON.", albumPath);
                            return false;
                        }
                        output.put('[');
                        outputBytes = 1;
                    }
                    if (outputAlbums > 0) {
                        output.put(',');
                        ++outputBytes;
                    }
                    output.write(json.data(), static_cast<std::streamsize>(json.size()));
                    if (!output) {
                        spdlog::error("Cannot write staged album JSON for '{}'.", pathToUtf8(albumPath));
                        reportWriteFailure("Cannot write staged album JSON.", albumPath);
                        return false;
                    }
                    outputBytes += albumBytes;
                    ++outputAlbums;
                    scanReport.tracksSerialized += trackCount;
                    if (lastFragment) ++scanReport.albumsSerialized;
                    return true;
                };
                const auto writeAlbumStream = [&](const fs::path& albumPath,
                    const AlbumCollection::AlbumJsonStreamWriter& serializer,
                    std::size_t trackCount) {
                    if (!output.is_open()) {
                        outputPath = mediaEntry.getMediaJsonPath(stagingRoot, ++count);
                        output.open(outputPath, std::ios::binary | std::ios::trunc);
                        if (!output) {
                            spdlog::error("Cannot open staged JSON '{}'.", pathToUtf8(outputPath));
                            reportWriteFailure("Cannot open staged album JSON.", albumPath);
                            return false;
                        }
                        output.put('[');
                        outputBytes = 1;
                    }
                    if (outputAlbums > 0) output.put(',');
                    if (!output || !serializer(output) || !output) {
                        spdlog::error("Cannot stream staged album JSON for '{}'.", pathToUtf8(albumPath));
                        reportWriteFailure("Cannot stream staged album JSON.", albumPath);
                        return false;
                    }
                    ++outputAlbums;
                    scanReport.tracksSerialized += trackCount;
                    ++scanReport.albumsSerialized;
                    return true;
                };
                if (!ac.SaveAlbumsToJSON(albumListPtr, libraryIoPath, writeAlbum,
                    settings->JsonBatchTargetBytes, pathToUtf8(stagingRoot.filename()),
                    &scanReport.diagnostics, writeAlbumStream) || !finishOutput()) {
                    throw std::runtime_error("Failed to write an album JSON file.");
                }
                if (!archiveBatchMetadata(
                        albumListPtr, libraryIoPath, stagedDirectory,
                        scanManifest, scanReport.diagnostics)) {
                    throw std::runtime_error("Failed to create a verified native FLAC metadata archive.");
                }
            };
            if (preview) {
                for (const auto& albums : preparedAlbums[libraryIndex]) {
                    scanReport.discovery.albums += albums->size();
                    for (const auto& album : *albums) {
                        scanReport.discovery.tracks += album.trackList.size();
                    }
                }
                for (const auto& albums : preparedAlbums[libraryIndex]) {
                    writeBatch(albums);
                }
                preparedAlbums[libraryIndex].clear();
            }
            else {
                for (auto albums : ac.LoadAlbumsCo(
                    libraryIoPath, false, settings->AlbumsSplitThreshold, &scanReport.discovery)) {
                    writeBatch(albums);
                }
            }
        }
        catch (const std::filesystem::filesystem_error& e) {
            spdlog::error("Filesystem error: {}", e.what());
            const auto affected = !e.path1().empty() ? e.path1() : e.path2();
            std::error_code typeError;
            const bool isTrackPath = !affected.empty() &&
                fs::is_regular_file(affected, typeError) && !typeError &&
                MediaTrack::IsFileAcceptedAudioFile(affected);
            scanReport.diagnostics.push_back({
                isTrackPath ? std::optional<std::string>(pathToUtf8(affected.parent_path())) : std::nullopt,
                isTrackPath ? std::optional<std::string>(pathToUtf8(affected)) : std::nullopt,
                { "scan", "filesystem", DiagnosticCode::ScanFilesystemFailed, DiagnosticSeverity::Error, e.what() },
                affected.empty() ? std::nullopt
                    : std::optional<std::string>(pathToUtf8(affected)),
                e.code().value(), e.code().category().name()
            });
            writeFailureReport(nullptr);
            cleanupStaging();
            return 1;
        }
        catch (const std::exception& e) {
            spdlog::error("General error: {}", e.what());
            scanReport.diagnostics.push_back({
                std::nullopt,
                std::nullopt,
                { "scan", "catalog", DiagnosticCode::ScanFailed, DiagnosticSeverity::Error, e.what() }
            });
            writeFailureReport(nullptr);
            cleanupStaging();
            return 1;
        }
        if (count == 0) {
            spdlog::error("No albums found; preserving existing output at '{}'.", pathToUtf8(targetDirectory));
            scanReport.diagnostics.push_back({
                std::nullopt,
                std::nullopt,
                { "discovery", "catalog", DiagnosticCode::NoAlbumsFound, DiagnosticSeverity::Error,
                    "No albums found; existing generated JSON was preserved." }
            });
            writeFailureReport(nullptr);
            cleanupStaging();
            return 1;
        }
        for (const auto& [format, unsupportedCount] : scanManifest.unsupportedFormats) {
            scanReport.diagnostics.push_back({
                std::nullopt, std::nullopt,
                { "metadata_archive", "catalog", "metadata_archive_unsupported",
                    DiagnosticSeverity::Warning,
                    "Byte-exact metadata archives are native-FLAC-only; " +
                        std::to_string(unsupportedCount) + " track(s) in format '" + format +
                        "' were not archived." }
            });
        }
        if (!writeScanManifest(stagedDirectory, scanManifest)) {
            scanReport.diagnostics.push_back({
                std::nullopt, std::nullopt,
                { "manifest", "filesystem", DiagnosticCode::ScanManifestInvalid,
                    DiagnosticSeverity::Error, "Could not write a complete scan manifest." }
            });
            writeFailureReport(nullptr);
            cleanupStaging();
            return 1;
        }
        PlatformUtils::ScopedCurrentDirectory targetDirectoryAnchor;
        if (!targetDirectoryAnchor.EnterValidatedDirectory(
                targetDirectory, libraryIoPath, true)) {
            scanReport.diagnostics.push_back({
                std::nullopt,
                std::nullopt,
                { "installation", "filesystem", DiagnosticCode::OutputDirectoryUnavailable, DiagnosticSeverity::Error,
                    "Cannot safely enter the scan output directory." }
            });
            writeFailureReport(nullptr);
            cleanupStaging();
            return 1;
        }
        const auto anchoredTargetDirectory = targetDirectoryAnchor.ResolvePath(".");
        const bool installed = installGeneratedFiles(stagedDirectory, anchoredTargetDirectory);
        const bool targetStillAnchored = targetDirectoryAnchor.IsAnchored(targetDirectory);
        if (!installed || !targetStillAnchored) {
            if (!targetStillAnchored) {
                spdlog::error("Scan output directory changed during installation.");
            }
            scanReport.diagnostics.push_back({
                std::nullopt,
                std::nullopt,
                { "installation", "filesystem", DiagnosticCode::GeneratedJsonInstallFailed, DiagnosticSeverity::Error,
                    targetStillAnchored
                        ? "Could not install generated JSON files."
                        : "Scan output directory changed during installation." }
            });
            writeFailureReport(targetStillAnchored ? &targetDirectoryAnchor : nullptr);
            if (!targetDirectoryAnchor.Restore()) {
                spdlog::error("Cannot restore the working directory after scan installation.");
            }
            cleanupStaging();
            return 1;
        }
        if (summary) {
            summary->completed += count;
        }
        scanReport.batchesInstalled = scanReport.batchesStaged;
        appendUnscopedFfmpegLogs();
        scanReport.status = "completed";
        scanReport.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        if (!installScanReport(
                targetDirectory, libraryIoPath, scanReport, &targetDirectoryAnchor)) {
            spdlog::warn("Scan completed, but its diagnostics report could not be updated.");
            scanReport.diagnostics.push_back({
                std::nullopt, std::nullopt,
                { "report", "filesystem", DiagnosticCode::ScanReportWriteFailed,
                    DiagnosticSeverity::Error, "Could not install the scan report at its normal location." },
                pathToUtf8(targetDirectory / "scan-report.json")
            });
            writeFallbackScanReport(scanReport, mediaDirectoryList);
        }
        if (!targetDirectoryAnchor.Restore()) {
            spdlog::error("Cannot restore the working directory after scan installation.");
        }
        cleanupStaging();
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
    ManualOperationSummary* summary = nullptr, const DuplicateReview::Options* review = nullptr)
{
	AlbumCollection albumCollection;
	std::shared_ptr<DirectoryContentEntryList> albumListPtr = std::make_shared<DirectoryContentEntryList>();

    if (jsonFiles.empty()) {
        spdlog::error("No generated album JSON files found.");
        return 1;
    }
    struct LibraryInput
    {
        std::vector<fs::path> files;
        DirectoryContentEntryList albums;
    };
    std::map<fs::path, LibraryInput> libraries;
    for (const auto& jsonFile : jsonFiles) {
        spdlog::info("Processing: {}", pathToUtf8(jsonFile));
        auto albumListChunk = albumCollection.LoadAlbumsFromJSON(jsonFile);
        if (!albumListChunk) {
            spdlog::error("Failed to load album JSON: {}", pathToUtf8(jsonFile));
            return 1;
        }
        for (const auto& album : *albumListChunk) {
            for (const auto& track : album.trackList) {
                if (track.catalogRecord &&
                    (!track.catalogRecord->sampleRateHz || !track.catalogRecord->channels)) {
                    spdlog::error("Duplicate input has no usable audio properties: '{}'.",
                        pathToUtf8(album.path.path() / track.trackPath));
                    return 1;
                }
            }
        }
        auto& library = libraries[jsonFile.parent_path()];
        library.files.push_back(jsonFile);
        library.albums.insert(library.albums.end(), albumListChunk->begin(), albumListChunk->end());
    }
    for (const auto& [directory, library] : libraries) {
        if (!validateScanManifest(directory, library.files, &library.albums)) {
            return 1;
        }
        albumListPtr->insert(albumListPtr->end(), library.albums.begin(), library.albums.end());
    }
    if (!albumCollection.ReassembleAlbums(albumListPtr)) {
        return 1;
    }

    //Step 2
	//sort albums by track count, find duplicates in the collection
    spdlog::info("Finding duplicated albums...");
    albumCollection.SortAlbums(albumListPtr, { { SortBy::TrackCount, true } });
    auto dupList = albumCollection.FindDuplicateAlbums(albumListPtr);


    spdlog::info("Found {} duplicated albums", dupList.size());
    std::vector<fs::path> albumKeys;
    if (review) {
        for (const auto& album : *albumListPtr) {
            std::error_code ec;
            auto key = fs::weakly_canonical(album.path.path(), ec);
            if (ec) {
                spdlog::error("Cannot resolve duplicate album '{}': {}",
                    pathToUtf8(album.path.path()), ec.message());
                return 1;
            }
            albumKeys.push_back(std::move(key));
        }
    }
    std::vector<bool> movedAlbums(albumListPtr->size(), false);
    int iCurrent = 0;
    for (const auto& entry : dupList)
    {
        iCurrent++;
        const auto [albumIndex1, albumIndex2] = entry;
        if (movedAlbums[albumIndex1] || movedAlbums[albumIndex2]) {
            spdlog::info("Skipping duplicate pair {}/{} because an album was moved to deleted.",
                iCurrent, dupList.size());
            if (summary) ++summary->skippedDuplicatePairs;
            continue;
        }
        const auto* album1 = &albumListPtr->at(albumIndex1);
        const auto* album2 = &albumListPtr->at(albumIndex2);
        const auto& dir1 = album1->path.path();
        const auto& dir2 = album2->path.path();

        const bool bIncludeAudioQualityMetrics = AppSettingsJson::AppSetting()->ExtraAudioQualityMetrics;
        try {
            const auto trackOrder = [](const MediaTrack* left, const MediaTrack* right) {
                const auto leftDuration = left->formatInfo.format.duration.value_or(-1);
                const auto rightDuration = right->formatInfo.format.duration.value_or(-1);
                if (leftDuration != rightDuration) return leftDuration < rightDuration;
                const auto title = [](const MediaTrack* track) {
                    if (!track->formatInfo.format.tags) return std::string{};
                    const auto& tags = *track->formatInfo.format.tags;
                    const auto found = tags.find("title");
                    return found == tags.end() ? std::string{} : found->second;
                };
                const auto leftTitle = title(left);
                const auto rightTitle = title(right);
                if (leftTitle != rightTitle) return leftTitle < rightTitle;
                return left->trackPath < right->trackPath;
            };
            const auto reProbe = [&](const MediaAlbum& album,
                std::vector<std::optional<MediaTrack>>& results, std::size_t& currentCount) {
                std::vector<const MediaTrack*> tracks;
                std::set<fs::path> scannedNames;
                for (const auto& track : album.trackList) {
                    if (track.trackPath.empty() || track.trackPath.has_root_path() ||
                        track.trackPath.has_parent_path() || track.trackPath == "." ||
                        track.trackPath == ".." || !scannedNames.insert(track.trackPath).second) {
                        throw std::runtime_error("Invalid or repeated track path in duplicate catalog.");
                    }
                    tracks.push_back(&track);
                }
                std::ranges::sort(tracks, trackOrder);
                for (const auto& current : fs::directory_iterator(album.path.path())) {
                    if (MediaTrack::IsValidMedia(current)) {
                        ++currentCount;
                        if (!scannedNames.contains(current.path().filename())) {
                            spdlog::warn("Track added since scan; not compared: '{}'.",
                                pathToUtf8(current.path()));
                        }
                    }
                }
                std::vector<MediaLoadingFuture> futures(tracks.size());
                results.resize(tracks.size());
                for (std::size_t i = 0; i < tracks.size(); ++i) {
                    const auto path = album.path.path() / tracks[i]->trackPath;
                    std::error_code ec;
                    const auto status = fs::status(path, ec);
                    if (ec == std::errc::no_such_file_or_directory || (!ec && !fs::exists(status))) {
                        spdlog::warn("Scanned track is missing: '{}'.", pathToUtf8(path));
                        continue;
                    }
                    if (ec || !fs::is_regular_file(status)) {
                        throw std::runtime_error("Cannot access scanned track '" + pathToUtf8(path) +
                            "': " + (ec ? ec.message() : "not a regular file"));
                    }
                    if (tracks[i]->catalogRecord) {
                        const auto size = fs::file_size(path);
                        const auto modified = CommonUtils::fileModificationTimeUtc(path);
                        const auto& record = *tracks[i]->catalogRecord;
                        if (size != record.sizeBytes ||
                            (modified && record.modifiedUtc && modified != record.modifiedUtc)) {
                            spdlog::warn("Scanned track changed since scan: '{}'; results use current metadata.",
                                pathToUtf8(path));
                        }
                    }
                    futures[i] = std::async(std::launch::async, MediaTrack::ReadMetadataInfoFromFile,
                        path, bIncludeAudioQualityMetrics);
                }
                for (std::size_t i = 0; i < tracks.size(); ++i) {
                    if (!futures[i].valid()) continue;
                    auto result = futures[i].get();
                    if (result.error || !std::ranges::any_of(result.mediaInfo.streams,
                        [](const Stream& stream) { return stream.codec_type == "audio"; })) {
                        throw std::runtime_error("Duplicate comparison could not read '" +
                            pathToUtf8(album.path.path() / tracks[i]->trackPath) + "': " +
                            result.error.value_or("No audio stream found."));
                    }
                    results[i] = MediaTrack{ tracks[i]->trackPath, 0, std::move(result.mediaInfo) };
                }
            };
            std::vector<std::optional<MediaTrack>> tracks1;
            std::vector<std::optional<MediaTrack>> tracks2;
            std::size_t count1 = 0, count2 = 0;
            reProbe(*album1, tracks1, count1);
            reProbe(*album2, tracks2, count2);
            if (count1 != count2) {
                spdlog::warn("Album track count differs: {} vs {}", count1, count2);
            }
            const auto pairedCount = (std::max)(tracks1.size(), tracks2.size());
            for (std::size_t i = 0; i < pairedCount; ++i) {
                if (i >= tracks1.size() || i >= tracks2.size() || !tracks1[i] || !tracks2[i]) {
                    spdlog::warn("Track missing in one of the albums. Scanned position: {}", i + 1);
                    continue;
                }
                spdlog::info("Comparing scanned tracks: '{}' and '{}'.",
                    pathToUtf8(dir1 / tracks1[i]->trackPath),
                    pathToUtf8(dir2 / tracks2[i]->trackPath));
                const std::vector<MediaTrack> left{ *tracks1[i] };
                const std::vector<MediaTrack> right{ *tracks2[i] };
                if (!MediaTrack::CompareAudioTracks(left, right)) {
                    spdlog::info("Audio Metrics for scanned track {}:\n{}",
                        i + 1, MediaTrack::GenerateComparisonReport(left, right));
                }
                const auto duration1 = left.front().formatInfo.format.duration;
                const auto duration2 = right.front().formatInfo.format.duration;
                if (duration1 && duration2 && *duration1 != *duration2) {
                    spdlog::info("Album duration check (album1/2), track {}: {} / {}",
                        i + 1, *duration1, *duration2);
                }
                compareAndPrintTagsTable(
                    left.front().formatInfo.format.tags.value_or(JsonUtils::Tags{}),
                    right.front().formatInfo.format.tags.value_or(JsonUtils::Tags{}));
            }

            spdlog::info("Potentially duplicated albums [{}/{}]:\n\tAlbum1: \"{}\"\n\tAlbum2: \"{}\"",
                iCurrent, dupList.size(), pathToUtf8(dir1), pathToUtf8(dir2));

            if (openDuplicateFolders && !review) {
                PlatformUtils::OpenDirectory(dir1);
                PlatformUtils::OpenDirectory(dir2);
            }

            if (summary) {
                ++summary->completed;
            }
            if (review) {
                const auto decision = DuplicateReview::ReviewPair(dir1, dir2,
                    review->libraryRoots, review->outputRoot, openDuplicateFolders, std::cin, std::cout);
                if (decision.movedAlbum != 0) {
                    const auto& movedKey = albumKeys[
                        decision.movedAlbum == 1 ? albumIndex1 : albumIndex2];
                    for (std::size_t i = 0; i < albumKeys.size(); ++i) {
                        if (DuplicateReview::IsSameOrChildPath(albumKeys[i], movedKey)) movedAlbums[i] = true;
                    }
                    if (summary) {
                        ++summary->movedAlbums;
                        summary->outputs.push_back(decision.destination);
                    }
                }
                if (decision.action == DuplicateReview::Action::Error) return 1;
                if (decision.action == DuplicateReview::Action::Stop) {
                    if (summary) summary->reviewStopped = true;
                    spdlog::info("Duplicate review stopped by user; remaining pairs were not processed.");
                    return 0;
                }
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
        fs::path libraryRoot;
        std::vector<fs::path> json;
    };
    std::vector<ImportPlan> plans;
    std::vector<fs::path> targets;
    for (const auto& mediaEntry : mediaDirectoryList) {
        auto normalizedEntry = mediaEntry;
        if (!normalizedEntry.mediaName || normalizedEntry.mediaName->empty()) {
            normalizedEntry.mediaName = directoryNameUtf8(pathFromUtf8(normalizedEntry.mediaPath));
        }
        const auto name = pathFromUtf8(normalizedEntry.mediaName.value_or(""));
        if (name == "." || name == ".." || name.has_root_path() || name.has_parent_path()) {
            spdlog::error("Library name must be a single directory name for database import.");
            return 1;
        }
        ImportPlan plan{
            absoluteDisplayPath(normalizedEntry.getMediaDBPath(pathFromUtf8(appSettingPtr->OutDirectory))),
            pathFromUtf8(mediaEntry.mediaPath),
            {}
        };
        summary.outputs.push_back(plan.database);
        for (const auto& json : findJsonFiles(
            pathFromUtf8(appSettingPtr->OutDirectory) / pathFromUtf8(normalizedEntry.mediaName.value()))) {
            plan.json.push_back(absoluteDisplayPath(json));
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
        auto albums = std::make_shared<DirectoryContentEntryList>();
        for (const auto& json : plan.json) {
            spdlog::info("Processing: {}", pathToUtf8(json));
            const auto part = albumCollection.LoadAlbumsFromJSON(json);
            if (!part) {
                spdlog::error("Failed to read JSON: '{}'.", pathToUtf8(json));
                return 1;
            }
            albums->insert(albums->end(), part->begin(), part->end());
        }
        if (!validateScanManifest(plan.json.front().parent_path(), plan.json, albums.get())) {
            return 1;
        }
        if (!albumCollection.ReassembleAlbums(albums)) {
            return 1;
        }
        albumCollection.SortAlbums(albums, { { SortBy::AlbumName, true } });
        if (!albumCollection.ExportToDatabase(albums, plan.database, plan.libraryRoot)) {
            return 1;
        }
        summary.completed += plan.json.size();
    }

    return 0;
}

int ImportJsonDirectoryToDatabase(const fs::path& jsonDirectory, const fs::path& outputRoot)
{
    std::error_code ec;
    const fs::path inputDirectory = fs::absolute(jsonDirectory, ec);
    if (ec || !fs::is_directory(inputDirectory, ec) || ec) {
        spdlog::error("JSON input path is not a directory: {}", pathToUtf8(jsonDirectory));
        return 1;
    }
    const auto jsonFiles = findJsonFiles(inputDirectory);
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
    if (!validateScanManifest(inputDirectory, jsonFiles, albums.get())) {
        return 1;
    }
    if (!albumCollection.ReassembleAlbums(albums)) {
        return 1;
    }
    MediaDirectoryElement library{};
    fs::path nameDirectory = inputDirectory.lexically_normal();
    if (nameDirectory.filename().empty() && nameDirectory.has_relative_path()) {
        nameDirectory = nameDirectory.parent_path();
    }
    library.mediaName = directoryNameUtf8(nameDirectory);
    const fs::path databasePath = library.getMediaDBPath(outputRoot);
    spdlog::info("Importing {} JSON file(s) into '{}'.", jsonFiles.size(), pathToUtf8(databasePath));
    return albumCollection.ExportToDatabase(albums, databasePath) ? 0 : 1;
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
        "Convert to FLAC [sources unchanged]",
        "      Input: all regular files and directories in the active source libraries, recursively.\n"
        "      Output: OutDirectory/FLAC/<mediaName>, retaining relative folder structure.\n"
        "              16-bit / 44.1 kHz FLAC with dithering, mapped tags/pictures, original FLAC\n"
        "              metadata sidecars per MetadataSidecars (binary/JSON; disabled/optional/required);\n"
        "              other files in written folders are copied unchanged.\n"
        "              Folders with audio but nothing to convert are skipped and listed in the audit.\n"
        "      Changes: source files are untouched; existing output files are rejected.\n"
        "               Resampling/depth reduction loses information in the output audio.\n"
        "      Audit: conversion-audit.json lists preservation differences and installation states.\n"
        "             Newly created failed library folders receive an error prefix; existing folders stay.\n"
        "      Rejects sources below 16-bit / 44.1 kHz or whose depth/rate cannot be verified.\n"
        "      RUN confirms execution; the first error stops processing without whole-batch rollback.\n" },
    { '2', ConvertActionEnum::CreateJSONEnum, "Scan directories and create metadata JSON",
        "      Input: audio libraries; reads track tags and technical metadata.\n"
        "      Output: OutDirectory/<mediaName>/media_out_<mediaName>_<part>.json.\n"
        "              scan-report.json records run status and path-linked diagnostics.\n"
        "      Changes: replaces the previous generated JSON set after a successful scan;\n"
        "               a failed scan preserves the previous generated JSON and writes diagnostics;\n"
        "               source audio, existing databases and unrelated files are unchanged.\n"
        "      <mediaName> defaults to the source folder name; <part> is a numbered batch.\n" },
    { '3', ConvertActionEnum::ProcessJSONEnum, "Find duplicate albums",
        "      Input: generated JSON from option 2; source folders must remain accessible\n"
        "             for track metadata/quality comparisons.\n"
        "      Output: duplicate reports in the console; opens matching album folders.\n"
        "      Changes: pauses after each pair; an explicitly confirmed choice moves a whole\n"
        "               album to OutDirectory/deleted. JSON and databases are not updated.\n" },
    { '4', ConvertActionEnum::PopulateJsonToDBEnum, "Import metadata JSON into SQLite",
        "      Input: generated JSON from option 2 for each active library.\n"
        "      Output: OutDirectory/<mediaName>/catalog-v2.sqlite.\n"
        "      Changes: creates/updates database records; source audio and JSON are unchanged.\n"
        "               Does not scan audio directly; preview shows the configured filenames.\n" },
    { '5', ConvertActionEnum::CreateDBFromFolderEnum, "Scan directories directly to SQLite [NOT IMPLEMENTED]",
        "      Input/Output/Changes: none; selection 5 runs no action.\n"
        "      Use option 2, then option 4 instead.\n" },
    { '6', ConvertActionEnum::ConvertMp3Enum, "Convert FLAC to MP3 [sources unchanged]",
        "      Input: native FLAC files in the active source libraries, recursively.\n"
        "      Output: OutDirectory/MP3/<mediaName>/ with relative paths preserved:\n"
        "              MP3 (LAME VBR quality 0 by default), .mp3.flac-metadata sidecars\n"
        "              (or .json; per MetadataSidecars), and unchanged non-audio companion files.\n"
        "      Changes: sources unchanged; existing destinations fail unless overwrite is enabled.\n"
        "               Supported tags/pictures/chapters go into ID3v2.4; original metadata\n"
        "               is retained in sidecars. MP3 audio is lossy. Multichannel audio\n"
        "               is rejected unless stereo downmix is explicitly enabled;\n"
        "               44.1/48 kHz retained, other accepted rates become 48 kHz.\n"
        "      Rejects sources below 16-bit / 44.1 kHz or whose depth/rate cannot be verified.\n"
        "      <mediaName> defaults to the source folder name.\n"
        "      Audit: conversion-audit.json lists preservation differences and installation states.\n"
        "             Newly created failed library folders receive an error prefix; existing folders stay.\n"
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
    spdlog::info("Database output: OutDirectory/<mediaName>/catalog-v2.sqlite");

    spdlog::info("----------------");

    spdlog::info("Log Level: {}", AppSettingsJson::AppSetting()->LogLevel);
    spdlog::info("Use Async Metadata Reads: {}", AppSettingsJson::AppSetting()->UseAsyncMetadataReads);
    spdlog::info("Metadata Scan Max Threads: {}", AppSettingsJson::AppSetting()->MetadataScanMaxThreads);
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
            return ConvertFLACToFLAC(mediaList, pathFromUtf8(appSettingPtr->OutDirectory), summary);

        case ConvertActionEnum::CreateJSONEnum: //2
            return ScanFolderAndCreateJSON(mediaList, true, pathFromUtf8(appSettingPtr->OutDirectory), true, &summary);

        case ConvertActionEnum::ProcessJSONEnum: //3
        {
            summary.unit = "duplicate pairs processed";
            summary.outputDescription = "console reports and matching album folders (see reports above).";
            DuplicateReview::Options review{ pathFromUtf8(appSettingPtr->OutDirectory), {} };
            for (const auto& library : mediaList) {
                review.libraryRoots.push_back(absoluteDisplayPath(pathFromUtf8(library.mediaPath)));
            }
            const auto jsonFiles = findConfiguredJsonFiles(mediaList, pathFromUtf8(appSettingPtr->OutDirectory));
            std::cout << "\nOperation preview: find duplicate albums\n  JSON input files: " << jsonFiles.size() << '\n';
            for (const auto& json : jsonFiles) {
                std::cout << "    Input: " << pathToUtf8(absoluteDisplayPath(json)) << '\n';
            }
            std::cout << "  Output: console reports and opened matching album folders.\n"
                "  Pauses after each pair: Enter continues; STOP ends review.\n"
                "  Optional: choose album 1 or 2, then type MOVE to move its entire folder to:\n"
                "    " << pathToUtf8(review.outputRoot / "deleted") << "\n"
                "  Moves copy and verify all files before removing originals; no destination is overwritten.\n"
                "  JSON/databases remain unchanged; run option 2 after moving albums.\n"
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
            return ScanFolderProcessJSONAndFindDuplicates(jsonFiles, true, &summary, &review);
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
        case CommandAction::Convert: {
            std::error_code ec;
            const auto absoluteInput = fs::absolute(inputPath, ec).lexically_normal();
            const auto name = absoluteInput.filename().empty()
                ? absoluteInput.parent_path().filename() : absoluteInput.filename();
            if (ec || name.empty() || name == "." || name == "..") {
                spdlog::error("Conversion input must have a library folder name.");
                return 1;
            }
            std::string sidecarError;
            const auto sidecarFormat = settings->MetadataSidecars.Resolve(
                options.conversion.target, options.noMetadataSidecar, sidecarError);
            if (!sidecarFormat) {
                std::cerr << "Configuration error: " << sidecarError << '\n';
                return 1;
            }
            options.conversion.sidecarFormat = *sidecarFormat;
            return FolderConvert::ConvertAudioFolderToOutput(
                inputPath, outputDirectory / (options.conversion.target == AudioTarget::Mp3 ? "MP3" : "FLAC") / name,
                options.overwrite, options.conversion) == 0 ? 0 : 1;
        }

        case CommandAction::Scan: {
            if (options.scanTierSpecified) {
                settings->ScanTier = options.scanTier;
            }
            // Scan through the given path, as the menu does; canonicalizing would expand 8.3 names and turn
            // mapped drives into UNC paths in the catalog.
            fs::path scanRoot = absoluteDisplayPath(inputPath);
            if (scanRoot.filename().empty() && scanRoot.has_relative_path()) {
                scanRoot = scanRoot.parent_path();
            }
            MediaDirectoryElement library{};
            library.mediaPath = pathToUtf8(scanRoot);
            library.mediaName = directoryNameUtf8(scanRoot);
            return ScanFolderAndCreateJSON(
                { library }, true, outputDirectory, false, nullptr, options.fullRescan);
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
