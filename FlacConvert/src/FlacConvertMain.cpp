// FlacConvert.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <atomic>
#include <cstdint>
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


enum class ConvertActionEnum { NullEnum, ConverEnum, CreateJSONEnum, CreateDBFromFolderEnum, ProcessJSONEnum, PopulateJsonToDBEnum, ConvertMp3Enum };


int ConvertFLACToFLAC(std::vector<MediaDirectoryElement> mediaDirectoryList)
{
    spdlog::info("Processing {}  directories", mediaDirectoryList.size());

    for (auto& mediaEntry: mediaDirectoryList)
    {
        spdlog::info("Scanning: {}...", mediaEntry.mediaPath.c_str());

        auto startTime = std::chrono::steady_clock::now();

        ScanInfo scanInfo{};
        int retStatus = FolderConvert::ScanAudioFiles(scanInfo, CommonUtils::path_from_utf8(mediaEntry.mediaPath));

        spdlog::info("Total convertable files: {}, Not regular: {} , No extension : {}, Not convertable: {}, Folders: {}",
            scanInfo.convertable_file_count,
            scanInfo.not_regular_file_count,
            scanInfo.file_with_no_extension_count,
            scanInfo.not_convertable_file_count,
            scanInfo.folders_count);
        spdlog::info("Total Size: {}", scanInfo.convertable_files_size);
        spdlog::info("======================");

        //std::wcout << std::endl << "Press Enter to Continue..." << std::endl;
        //std::getchar();

        auto ret = FolderConvert::ConverAudioFolder(CommonUtils::path_from_utf8(mediaEntry.mediaPath), scanInfo, false);
        if (ret == -1) {
            spdlog::error("***ERROR*** ConverAudioFiles");
            return -1;
        }

        spdlog::info("\nSuccess.");

        //log total execution time (millis)
        auto endTime = std::chrono::steady_clock::now();
        spdlog::info("-->### Total processing time(milliseconds): {}ms",
            std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());
    }

    return 0;
}


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

int ConvertFLACToMP3(const std::vector<MediaDirectoryElement>& mediaDirectoryList, const fs::path& outputRoot)
{
    if (mediaDirectoryList.empty()) {
        spdlog::error("No active media libraries configured for FLAC to MP3 conversion.");
        return 1;
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
    const ConversionOptions options{ AudioTarget::Mp3, 0, false };
    spdlog::info("FLAC to MP3: VBR quality 0, no overwrite, no multichannel downmix. "
        "Use the convert subcommand for quality, overwrite or downmix options.");
    for (const auto& conversion : conversions) {
        spdlog::info("Converting '{}' to '{}'.", pathToUtf8(conversion.source), pathToUtf8(conversion.target));
        if (FolderConvert::ConvertAudioFolderToOutput(conversion.source, conversion.target, false, options) != 0) {
            return 1;
        }
    }
    return 0;
}

int ScanFolderAndCreateJSON(std::vector<MediaDirectoryElement> mediaDirectoryList, bool bIncludeMetadata, const fs::path& outputRoot)
{
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
            for (auto albumListPtr : ac.LoadAlbumsCo(
                libraryPath, bIncludeMetadata, AppSettingsJson::AppSetting()->AlbumsSplitThreshold)) {
                spdlog::info("Received batch with {} albums", albumListPtr->size());
                ac.SortAlbums(albumListPtr, { { SortBy::AlbumArtist, true } });
                if (!ac.SaveAlbumsToJSON(albumListPtr, mediaEntry.getMediaJsonPath(stagingRoot, ++count))) {
                    throw std::runtime_error("Failed to write an album JSON file.");
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
        fs::remove_all(stagingRoot, ec);
        spdlog::info("Wrote {} JSON part(s) to '{}'; scan took {} ms.",
            count,
            pathToUtf8(targetDirectory),
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count());
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

int ScanFolderProcessJSONAndFindDuplicates(const std::vector<fs::path>& jsonFiles, bool openDuplicateFolders)
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

int ExportJSONToDB(std::vector<MediaDirectoryElement>  mediaDirectoryList)
{
    //guyguy review after loading changes

    auto appSettingPtr = AppSettingsJson::AppSetting();
    fs::path databasePath = pathFromUtf8(appSettingPtr->OutDirectory) /
        pathFromUtf8(appSettingPtr->DatabaseFileName);

    AlbumCollection albumCollection;

    for (auto& mediaEntry : mediaDirectoryList)
    {
        auto mediaDBPath = mediaEntry.getMediaDBPath(pathFromUtf8(AppSettingsJson::AppSetting()->OutDirectory));
        spdlog::info("Creating Data Base: {}", pathToUtf8(mediaDBPath));
        spdlog::info("-------------------");


        auto generator = mediaEntry.getMediaJsonPathCo(pathFromUtf8(AppSettingsJson::AppSetting()->OutDirectory));
        while (generator.resume()) {
			auto mediaJsonPartPath = generator.value();
            if (fs::exists(mediaJsonPartPath)) {
                spdlog::info("Processing: {}", pathToUtf8(mediaJsonPartPath));

                auto albumListPtr = albumCollection.LoadAlbumsFromJSON(mediaJsonPartPath);

                //    albumCollection.SortAlbums(albumListPtr, { { SortBy::AlbumArtist, true } });
                albumCollection.SortAlbums(albumListPtr, { { SortBy::AlbumName, true } });

                albumCollection.ExportToDatabase(albumListPtr, mediaDBPath);
            }
            else
			{
				spdlog::error("Error Exporting to DB: Json file not found: {}", pathToUtf8(mediaJsonPartPath));
			}   
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

ConvertActionEnum GetUserAction()
{
    ConvertActionEnum action{ ConvertActionEnum::NullEnum };

    spdlog::info("Select run option [1 - Re/Convert FLAC, 2 - Scan directories, 3 - Get Duplicates, 4 - Update DB, XX 5 - Scan DIR to DB, 6 - Convert FLAC to MP3]");
    char input = getchar();
    switch (input)
    {
    case '1':
        action = ConvertActionEnum::ConverEnum;
        break;
    case '2':
        action = ConvertActionEnum::CreateJSONEnum;
        break;
    case '5':
        action = ConvertActionEnum::CreateDBFromFolderEnum;
        break;
    case '3':
        action = ConvertActionEnum::ProcessJSONEnum;
        break;
    case '4':
        action = ConvertActionEnum::PopulateJsonToDBEnum;
        break;
    case '6':
        action = ConvertActionEnum::ConvertMp3Enum;
        break;
    default:
        spdlog::error("Selection Error, unknown {}", input);
        action = ConvertActionEnum::NullEnum;
        break;
    }

	return action;
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
    spdlog::info("Working directory: {}", appSettingPtr->OutDirectory);
    spdlog::info("Database file name: {}", appSettingPtr->DatabaseFileName);

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


    auto action = GetUserAction();
    switch (action)
    {
    case ConvertActionEnum::ConverEnum: //1
        return ConvertFLACToFLAC(mediaList) == 0 ? 0 : 1;

	case ConvertActionEnum::CreateJSONEnum: //2
		return ScanFolderAndCreateJSON(mediaList, true, pathFromUtf8(appSettingPtr->OutDirectory));

	case ConvertActionEnum::ProcessJSONEnum: //3
		return ScanFolderProcessJSONAndFindDuplicates(
			findConfiguredJsonFiles(mediaList, pathFromUtf8(appSettingPtr->OutDirectory)), true);

	case ConvertActionEnum::PopulateJsonToDBEnum: //4    
        return ExportJSONToDB(mediaList) == 0 ? 0 : 1;

    case ConvertActionEnum::CreateDBFromFolderEnum: //5
        return 0;

    case ConvertActionEnum::ConvertMp3Enum: //6
        return ConvertFLACToMP3(mediaList, pathFromUtf8(appSettingPtr->OutDirectory));

    default:
        spdlog::error("Acrtion not found: {}", static_cast<int>(action));
        return 2;
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
        return RunLegacyMenu();
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
