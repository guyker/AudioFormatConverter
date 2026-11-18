



#include <filesystem>
#include <array>
#include <vector>
#include <string>
#include <string_view>
#include <execution>
#include <mutex>
#include <iostream>
#include <map>
#include <set>
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <ranges>
#include <format>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/error.h"

#include <iostream>

#include "AlbumCollection.h"
#include "CatalogDatabase.h"
#include "BoundedTaskRunner.h"
#include "MediaTrack.h"
#include "FolderConvert.h"
#include "JsonUtils.h"
#include "PlatformUtils.h"

#include "CommonUtils.h"
#include "DecodeAnalyzer.h"
#include "MediaRecordJson.h"
#include "TagLibReader.h"

#include "sqlite3.h"


namespace fs = std::filesystem;
using namespace rapidjson;

namespace {

FFprobeOutput makeLegacyProjection(const MediaRecord& record)
{
    FFprobeOutput output;
    output.format.filename = record.relativePath;
    output.format.fs_file_size = record.sizeBytes;
    if (record.sizeBytes <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        output.format.file_size = static_cast<std::int64_t>(record.sizeBytes);
    }
    output.format.format_name = record.format;
    output.format.duration = record.durationSeconds;
    if (record.bitRateKbps && *record.bitRateKbps > 0) {
        output.format.bit_rate = static_cast<std::int64_t>(*record.bitRateKbps) * 1000;
    }
    output.format.nb_streams = 1;
    JsonUtils::Tags tags;
    for (const auto& [key, values] : record.tags) {
        if (values.empty()) continue;
        std::string legacyKey = key;
        if (key == "artists") legacyKey = "artist";
        else if (key == "albumArtists") legacyKey = "album_artist";
        else if (key == "trackNumber") legacyKey = "track";
        else if (key == "discNumber") legacyKey = "disc";
        std::string joined;
        for (const auto& value : values) {
            if (!joined.empty()) joined += ";";
            joined += value;
        }
        tags[legacyKey] = std::move(joined);
    }
    output.format.tags = std::move(tags);

    Stream stream;
    stream.index = 0;
    stream.codec_name = record.format;
    stream.codec_type = "audio";
    if (record.sampleRateHz) stream.sample_rate = std::to_string(*record.sampleRateHz);
    stream.channels = record.channels;
    stream.bits_per_sample = record.declaredBitsPerSample;
    if (record.bitRateKbps) {
        stream.bit_rate = static_cast<std::int64_t>(*record.bitRateKbps) * 1000;
    }
    output.streams.push_back(std::move(stream));
    return output;
}

bool cacheRecordMatches(const std::filesystem::path& path, const MediaRecord& record)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size != record.sizeBytes || !record.modifiedUtc) return false;
    const auto modifiedUtc = CommonUtils::fileModificationTimeUtc(path);
    return modifiedUtc && *modifiedUtc == *record.modifiedUtc;
}

int scanTierRank(const std::string& tier)
{
    if (tier == "analysis") return 2;
    if (tier == "integrity") return 1;
    return 0;
}

struct ParsedNumberTag
{
    int number{ 0 };
    std::optional<int> total;
};

std::optional<ParsedNumberTag> parseNumberTag(std::string_view value)
{
    const auto slash = value.find('/');
    const auto numberText = value.substr(0, slash);
    int number = 0;
    const auto numberResult = std::from_chars(
        numberText.data(), numberText.data() + numberText.size(), number);
    if (numberResult.ec != std::errc{} || numberResult.ptr != numberText.data() + numberText.size() ||
        number <= 0) {
        return std::nullopt;
    }

    ParsedNumberTag parsed{ number, std::nullopt };
    if (slash != std::string_view::npos) {
        const auto totalText = value.substr(slash + 1);
        int total = 0;
        const auto totalResult = std::from_chars(
            totalText.data(), totalText.data() + totalText.size(), total);
        if (totalResult.ec != std::errc{} ||
            totalResult.ptr != totalText.data() + totalText.size() || total < number) {
            return std::nullopt;
        }
        parsed.total = total;
    }
    return parsed;
}

void addAlbumFinding(TrackInfoList& tracks, const std::string& code, const std::string& evidence)
{
    for (auto& track : tracks) {
        if (!track.catalogRecord) continue;
        const auto exists = std::ranges::any_of(track.catalogRecord->findings,
            [&](const CatalogFinding& finding) { return finding.code == code; });
        if (!exists) {
            track.catalogRecord->findings.push_back({ code, "warning", "high", evidence });
        }
    }
}

bool isAlbumFindingCode(std::string_view code)
{
    static constexpr std::array albumFindingCodes{
        std::string_view("DUPLICATE_TRACK_NUMBER"),
        std::string_view("TRACK_NUMBER_GAP"),
        std::string_view("INCONSISTENT_TRACK_TOTAL"),
        std::string_view("TRACK_TOTAL_MISMATCH"),
        std::string_view("DISC_NUMBER_GAP"),
        std::string_view("INCONSISTENT_DISC_TOTAL"),
        std::string_view("DISC_TOTAL_MISMATCH")
    };
    return code.starts_with("INCONSISTENT_ALBUM_TAG_") ||
        std::ranges::any_of(albumFindingCodes, [&](std::string_view candidate) {
            return code == candidate;
        });
}

void applyScanTier(const std::filesystem::path& path, MediaRecord& record)
{
    const auto tier = AppSettingsJson::AppSetting()->ScanTier;
    record.scanTier = tier;
    if (tier == "catalog") return;
    DecodeAnalyzer::Analyze(path, record, tier == "analysis");
    if (tier == "analysis") {
        record.analysisStatus = record.signal.status == "failed" ? "failed" : "partial";
    }
}

void validateAlbumTags(TrackInfoList& tracks)
{
    for (auto& track : tracks) {
        if (!track.catalogRecord) continue;
        std::erase_if(track.catalogRecord->findings, [](const CatalogFinding& finding) {
            return isAlbumFindingCode(finding.code);
        });
    }

    static constexpr std::array fields{
        std::string_view("album"), std::string_view("albumArtists"),
        std::string_view("releaseDate"), std::string_view("label")
    };
    for (const auto field : fields) {
        std::set<std::string> values;
        for (const auto& track : tracks) {
            if (!track.catalogRecord) continue;
            const auto tag = track.catalogRecord->tags.find(std::string(field));
            if (tag == track.catalogRecord->tags.end()) continue;
            for (const auto& value : tag->second) {
                if (!value.empty()) values.insert(value);
            }
        }
        if (values.size() < 2) continue;

        const auto findingCode = "INCONSISTENT_ALBUM_TAG_" + std::string(field);
        for (auto& track : tracks) {
            if (!track.catalogRecord) continue;
            const auto& findings = track.catalogRecord->findings;
            if (std::ranges::any_of(findings, [&](const CatalogFinding& finding) {
                    return finding.code == findingCode;
                })) {
                continue;
            }
            track.catalogRecord->findings.push_back({
                findingCode,
                "warning",
                "high",
                "Different normalized '" + std::string(field) +
                    "' values were found among tracks in this album folder."
            });
        }
    }

    std::map<int, std::set<int>> trackNumbersByDisc;
    std::map<int, std::set<int>> trackTotalsByDisc;
    std::set<int> discNumbers;
    std::set<int> discTotals;
    std::set<int> discsWithUnnumberedTracks;
    bool duplicateTrackNumber = false;
    for (const auto& track : tracks) {
        if (!track.catalogRecord) continue;
        int discNumber = 1;
        if (const auto discs = track.catalogRecord->tags.find("discNumber");
            discs != track.catalogRecord->tags.end() && !discs->second.empty()) {
            if (const auto parsed = parseNumberTag(discs->second.front())) {
                discNumber = parsed->number;
                if (parsed->total) discTotals.insert(*parsed->total);
            }
        }
        discNumbers.insert(discNumber);
        bool hasValidTrackNumber = false;
        if (const auto numbers = track.catalogRecord->tags.find("trackNumber");
            numbers != track.catalogRecord->tags.end()) {
            for (const auto& value : numbers->second) {
                if (const auto parsed = parseNumberTag(value)) {
                    hasValidTrackNumber = true;
                    if (!trackNumbersByDisc[discNumber].insert(parsed->number).second) {
                        duplicateTrackNumber = true;
                    }
                    if (parsed->total) {
                        trackTotalsByDisc[discNumber].insert(*parsed->total);
                    }
                }
            }
        }
        if (!hasValidTrackNumber) discsWithUnnumberedTracks.insert(discNumber);
    }

    if (duplicateTrackNumber) {
        addAlbumFinding(tracks, "DUPLICATE_TRACK_NUMBER",
            "More than one track in the same disc has the same track number.");
    }
    bool trackNumberGap = false;
    for (const auto& [disc, numbers] : trackNumbersByDisc) {
        (void)disc;
        if (numbers.size() < 2) continue;
        for (auto number = *numbers.begin(); number < *numbers.rbegin();) {
            ++number;
            if (!numbers.contains(number)) {
                trackNumberGap = true;
                break;
            }
        }
    }
    if (trackNumberGap) {
        addAlbumFinding(tracks, "TRACK_NUMBER_GAP",
            "Track numbering has a gap between the lowest and highest number on a disc.");
    }

    bool inconsistentTrackTotals = false;
    bool trackTotalMismatch = false;
    for (const auto& [disc, totals] : trackTotalsByDisc) {
        inconsistentTrackTotals = inconsistentTrackTotals || totals.size() > 1;
        if (totals.size() == 1 && !discsWithUnnumberedTracks.contains(disc)) {
            trackTotalMismatch = trackTotalMismatch ||
                trackNumbersByDisc[disc].size() != static_cast<std::size_t>(*totals.begin());
        }
    }
    if (inconsistentTrackTotals) {
        addAlbumFinding(tracks, "INCONSISTENT_TRACK_TOTAL",
            "Tracks on the same disc declare different track totals.");
    }
    else if (trackTotalMismatch) {
        addAlbumFinding(tracks, "TRACK_TOTAL_MISMATCH",
            "The number of uniquely numbered tracks on a disc differs from its declared total.");
    }

    bool discNumberGap = false;
    if (discNumbers.size() > 1) {
        for (auto number = *discNumbers.begin(); number < *discNumbers.rbegin();) {
            ++number;
            if (!discNumbers.contains(number)) {
                discNumberGap = true;
                break;
            }
        }
    }
    if (discNumberGap) {
        addAlbumFinding(tracks, "DISC_NUMBER_GAP",
            "Disc numbering has a gap between the lowest and highest disc number.");
    }
    if (discTotals.size() > 1) {
        addAlbumFinding(tracks, "INCONSISTENT_DISC_TOTAL",
            "Tracks declare different totals for the number of discs.");
    }
    else if (discTotals.size() == 1 &&
        discNumbers.size() != static_cast<std::size_t>(*discTotals.begin())) {
        addAlbumFinding(tracks, "DISC_TOTAL_MISMATCH",
            "The number of distinct discs differs from the declared disc total.");
    }
}

}



CommonUtils::Generator<MediaAlbumListPtr> AlbumCollection::LoadAlbumsCo(std::filesystem::path albumCollectionDirPath, bool bIncludeMetadata, size_t batchSize, ScanDiscoveryCounts* counts) {

    std::chrono::steady_clock::time_point startTimePoint = std::chrono::steady_clock::now();
    //std::chrono::steady_clock::time_point endLoadTime = startLoadTime;
    spdlog::info("===Loading album collection from storage===");

    // Map to collect tracks per folder
    std::map<fs::path, TrackInfoList> albumMap;
    albumMap[albumCollectionDirPath];

    spdlog::info("First pass: Find all folders, Please wait (might take a few minutes)...");

    long long folderCount = 0;
    // First pass: Find all folders - Discove folders
    for (const auto& entry : fs::recursive_directory_iterator(albumCollectionDirPath)) {
        // Progress update
        std::cout << "\rScanning folders, Please wait... " << ++folderCount;

        auto relativePath = fs::relative(entry.path(), albumCollectionDirPath);
        auto depth = std::distance(relativePath.begin(), relativePath.end());
        if (depth > AppSettingsJson::AppSetting()->RecursionDirectorySearchDepth) {
            spdlog::error("Recursion rirectory search depth reached, depth: {}", depth);
            continue;
        }
        if (entry.is_directory()) {
            albumMap[entry.path()];
        }
    }

    //std::cout << "\rScanning folders, Please wait... Completed, " << folderCount << " folders found" << std::endl;
    std::cout << "\r";
    spdlog::info("Scanning folders, Please wait... Completed, {} folders found", folderCount);
    spdlog::info("First pass: Completed, found {} folders [{}]", albumMap.size(), CommonUtils::GetDurationinString(startTimePoint, std::chrono::steady_clock::now()));
    startTimePoint = std::chrono::steady_clock::now();

    int albumCount = 0;
    spdlog::info("Second pass: Collect tracks one level deep...");
    // Second pass: Collect files one level deep
    std::cout << "Scanning albums, Please wait... ";
    for (auto& [folderPath, trackList] : albumMap) {
        for (const auto& entry : fs::directory_iterator(folderPath)) {
            if (entry.is_regular_file() && MediaTrack::IsFileAcceptedAudioFile(entry)) {
                if (counts) {
                    if (trackList.empty()) ++counts->albums;
                    ++counts->tracks;
                }
                uintmax_t fileSize = fs::file_size(entry.path());
                trackList.push_back({ entry.path().filename(), fileSize, FFprobeOutput{}, std::nullopt, std::nullopt });
            }
        }
        // Progress update
        if (!trackList.empty()) {
            std::cout << "\33[2K\r";  // Clear entire line and move cursor to start
            std::cout << "Scanning albums, Please wait... " << ++albumCount;
        }
    }

    std::cout << "\33[2K\r";  // Clear entire line and move cursor to start
    spdlog::info("Scanning albums, Please wait... Completed, {} albums found ", albumCount);
    spdlog::info("Second pass: Completed, found {} albums [{}]", albumCount, CommonUtils::GetDurationinString(startTimePoint, std::chrono::steady_clock::now()));
    startTimePoint = std::chrono::steady_clock::now();

    // Convert map to std::shared_ptr<std::vector<MediaAlbum>> in batches
    MediaAlbumListPtr albumListPtr = std::make_shared<std::vector<MediaAlbum>>();
    albumListPtr->reserve(batchSize); // Optimize allocation
    int totalBatchCount = albumCount / AppSettingsJson::AppSetting()->AlbumsSplitThreshold;
    int batchCount = 0;

    //erase all empty folders
    for (auto it = albumMap.begin(); it != albumMap.end(); ) {
        if (it->second.size() == 0) { // Replace with appropriate check
            it = albumMap.erase(it);
        }
        else {
            ++it;
        }
    }

    if (albumMap.size() > AppSettingsJson::AppSetting()->AlbumsSplitThreshold)
    {
        spdlog::info("Converting albums to batches of {}...", batchSize);
    }
    size_t albumIndex = 0;
    for (auto& [albumPath, trackList] : albumMap) {
        if (!trackList.empty()) {
            albumListPtr->emplace_back(fs::directory_entry(albumPath), std::move(trackList));
            if (albumListPtr->size() >= batchSize) {
                if (bIncludeMetadata) {
                    spdlog::info("Collecting metadata for batch {}/{}...", batchCount + 1, totalBatchCount);
                    ImportMetadata(albumListPtr, albumIndex, albumMap.size(), AppSettingsJson::AppSetting()->UseAsyncMetadataReads);
                }
                albumIndex += batchSize;
                spdlog::info("Yielding batch {} with {} albums", ++batchCount, albumListPtr->size());
                co_yield albumListPtr;
                albumListPtr = std::make_shared<std::vector<MediaAlbum>>();
                albumListPtr->reserve(batchSize);
            }
        }
    }

    // Yield and process final batch
    if (!albumListPtr->empty()) {
        if (bIncludeMetadata) {
            spdlog::info("Collecting metadata for final batch {}/{}...", batchCount + 1, totalBatchCount);
            ImportMetadata(albumListPtr, albumIndex, albumMap.size(), AppSettingsJson::AppSetting()->UseAsyncMetadataReads);
        }
        if (albumMap.size() > AppSettingsJson::AppSetting()->AlbumsSplitThreshold)
        {
            spdlog::info("Yielding final batch {} with {} albums", ++batchCount, albumListPtr->size());
        }
        co_yield albumListPtr;
    }

    spdlog::info("Album conversion completed, {} batches [{}]", batchCount, CommonUtils::GetDurationinString(startTimePoint, std::chrono::steady_clock::now()));
}

MediaAlbumListPtr AlbumCollection::LoadAlbums(std::filesystem::path albumCollectionDirPath, bool bIncludeMetadata) {
    
    std::chrono::steady_clock::time_point startTimePoint = std::chrono::steady_clock::now();
	//std::chrono::steady_clock::time_point endLoadTime = startLoadTime;
    spdlog::info("===Loading album collection from storage===");

    // Clear existing albums
    //_AlbumList.clear();
	MediaAlbumListPtr albumListPtr = std::make_shared<std::vector<MediaAlbum>>();

    // Map to collect tracks per folder
    std::map<fs::path, TrackInfoList> albumMap;

    try {
        spdlog::info("First pass: Find all folders, Please wait (might take a few minutes)...");

        long long folderCount = 0;

		// First pass: Find all folders - Discove folders
        for (const auto& entry : fs::recursive_directory_iterator(
            albumCollectionDirPath, fs::directory_options::skip_permission_denied)) {

            //progress update
            std::cout << "\rScnning albums, Please wait... " << ++folderCount;

            try {
                auto relativePath = fs::relative(entry.path(), albumCollectionDirPath);
                auto depth = std::distance(relativePath.begin(), relativePath.end());
                if (depth > AppSettingsJson::AppSetting()->RecursionDirectorySearchDepth) {
                    spdlog::error("Recursion rirectory search depth reached, depth: {}", depth);
                    continue;
                }
                if (entry.is_directory()) {
                    albumMap[entry.path()];
                }
            }
            catch (const fs::filesystem_error& e) {
                spdlog::error("Error accessing {}: {}", CommonUtils::utf8string_to_string(entry.path().u8string()), e.what());
            }
        }
        std::cout << "\rScnning albums, Please wait... " << folderCount << " Completed" << std::endl;

        spdlog::info("First pass: Completed, found {} Folders [{}]", albumMap.size(), CommonUtils::GetDurationinString(startTimePoint, std::chrono::steady_clock::now()));
        startTimePoint = std::chrono::steady_clock::now();


        spdlog::info("Second pass: Collect tracks one level deep...");
        // Second pass: Collect files one level deep
        for (auto& [folderPath, trackList] : albumMap) {
            try {
                for (const auto& entry : fs::directory_iterator(
                    folderPath, fs::directory_options::skip_permission_denied)) {
                    if (entry.is_regular_file() && MediaTrack::IsFileAcceptedAudioFile(entry)) {
                        uintmax_t fileSize{ 0 };
                        try {
                            fileSize = fs::file_size(entry.path());
                        }
                        catch (const fs::filesystem_error& e) {
                            spdlog::error("Error getting file size for {}: {}", CommonUtils::utf8string_to_string(entry.path().u8string()), e.what());
                        }

                        trackList.push_back({ entry.path().filename(), fileSize, FFprobeOutput{}, std::nullopt, std::nullopt });
                    }
                }
            }
            catch (const fs::filesystem_error& e) {
                spdlog::error("Error iterating {}: {}",
                    CommonUtils::utf8string_to_string(folderPath.u8string()), e.what());
            }
        }

        // Convert map to std::shared_ptr<std::vector<MediaAlbum>>
        for (auto& [albumPath, trackList] : albumMap) {
            if (!trackList.empty()) {
                albumListPtr->emplace_back(fs::directory_entry(albumPath), std::move(trackList));
            }
        }

        spdlog::info("Second pass: Completed, {} Albums [{}]", albumListPtr->size(), CommonUtils::GetDurationinString(startTimePoint, std::chrono::steady_clock::now()));
        startTimePoint = std::chrono::steady_clock::now();

    }
    catch (const fs::filesystem_error& e) {
        spdlog::error("Error iterating {}: {}",
            CommonUtils::utf8string_to_string(albumCollectionDirPath.u8string()), e.what());
        return albumListPtr;
    }


    if (bIncludeMetadata) {
        spdlog::info("Third pass: Collecting Metadata...");

        auto nAlbums = ImportMetadata(albumListPtr, 0, 0, AppSettingsJson::AppSetting()->UseAsyncMetadataReads);

        spdlog::info("Third pass: Completed [{}]",
            CommonUtils::GetDurationinString(startTimePoint, std::chrono::steady_clock::now()));
        startTimePoint = std::chrono::steady_clock::now();
    }
    else
    {
        spdlog::warn("Third pass: [skipped]");
    }

    return albumListPtr;
}






std::pair<long long, long long> AlbumCollection::GetNumberOfItemsInFolder(std::filesystem::path rootPath, int depth)
{

    struct DirectoryContents {
        std::vector<fs::path> files;
        std::vector<fs::path> folders;
        std::mutex mutex;

        void add_file(const fs::path& path) {
            std::lock_guard<std::mutex> lock(mutex);
            files.push_back(path);
        }

        void add_folder(const fs::path& path) {
            std::lock_guard<std::mutex> lock(mutex);
            folders.push_back(path);
        }
    };

    DirectoryContents contents;
    std::vector<fs::path> entries;

    // Collect all entries first
    try {
        for (const auto& entry : fs::recursive_directory_iterator(
            rootPath, fs::directory_options::skip_permission_denied)) {
            entries.push_back(entry.path());
        }
    }
    catch (const fs::filesystem_error& e) {
        std::cerr << "Error iterating " << CommonUtils::path_to_utf8(rootPath) << ": " << e.what() << '\n';
    }

    // Process entries in parallel
    std::for_each(std::execution::par, entries.begin(), entries.end(), [&](const fs::path& path) {
        try {
            if (fs::is_regular_file(path)) {
                contents.add_file(path);
            }
            else if (fs::is_directory(path)) {
                contents.add_folder(path);
            }
        }
        catch (const fs::filesystem_error& e) {
            std::cerr << "Error accessing " << CommonUtils::path_to_utf8(path) << ": " << e.what() << '\n';
        }
        });

    return { contents.folders.size(), contents.files.size() };
}




//Load all media media information from the preloaded album list (albumList)
size_t AlbumCollection::ImportMetadata(std::shared_ptr<DirectoryContentEntryList> albumListPtr,
    size_t currentCount, size_t totalCount, bool bAsync,
    const std::map<std::string, MediaRecord>* cachedRecords)
{
	if (albumListPtr == nullptr || albumListPtr->empty())
	{
		return 0;
	}
    size_t albumCount = 0;
       
    for (auto& album : *albumListPtr)
    {
        auto& albumPath = album.path;
        auto& trackList = album.trackList;
        std::string name = "N/A";
        if (albumPath.is_directory() && albumPath.path().has_filename())
        {
            name = CommonUtils::utf8string_to_string(albumPath.path().filename().generic_u8string());
        }
        else
        {
			spdlog::error("{}", CommonUtils::utf8string_to_string(albumPath.path().generic_u8string()));
        }
        std::shared_ptr<CommonUtils::ProgressBarInfo> subProgressInfoPtr{ nullptr };
        if (AppSettingsJson::AppSetting()->AlbumsSplitThreshold < totalCount)
        {
            subProgressInfoPtr = std::make_shared<CommonUtils::ProgressBarInfo>("Processing...", albumCount++, albumListPtr->size(), name, 20);
        }
        auto progressInfoPtr = std::make_shared<CommonUtils::ProgressBarInfo>("Processing...", currentCount++, totalCount, name, 20, subProgressInfoPtr);
        CommonUtils::show_progress_bar(progressInfoPtr, CommonUtils::ProgressBarType::Progress);

        struct MetadataTarget
        {
            std::filesystem::path path;
            MediaTrack* track;
        };
        std::vector<MetadataTarget> metadataTargets;
        metadataTargets.reserve(trackList.size());

        size_t reusedRecords = 0;
        for (auto& track : trackList)
        {
            track.diagnostics.clear();
            std::filesystem::path trackPath = albumPath.path() / track.trackPath;

            if (MediaTrack::IsValidMedia(trackPath)) {
                const auto cacheKey = CommonUtils::path_to_utf8(trackPath.lexically_normal());
                const auto cached = cachedRecords == nullptr
                    ? std::map<std::string, MediaRecord>::const_iterator{}
                    : cachedRecords->find(cacheKey);
                if (cachedRecords != nullptr && cached != cachedRecords->end() &&
                    cached->second.metadataInventoryVersion >= 1 &&
                    cached->second.relativePath == CommonUtils::path_to_utf8(track.trackPath) &&
                    scanTierRank(cached->second.scanTier) >=
                        scanTierRank(AppSettingsJson::AppSetting()->ScanTier) &&
                    cacheRecordMatches(trackPath, cached->second)) {
                    track.catalogRecord = cached->second;
                    track.LastErroString.reset();
                    track.formatInfo = makeLegacyProjection(cached->second);
                    ++reusedRecords;
                    continue;
                }
                metadataTargets.push_back({
                    trackPath.lexically_normal(),
                    &track
                });
            }
        }

        if (reusedRecords > 0) {
            spdlog::info("Reused {} unchanged metadata record(s) in album '{}'.",
                reusedRecords, CommonUtils::path_to_utf8(albumPath.path()));
        }
        if (!metadataTargets.empty()) {
            const auto settings = AppSettingsJson::AppSetting();
            if (bAsync) {
                const unsigned int hardwareConcurrency = std::thread::hardware_concurrency();
                const size_t automaticLimit = hardwareConcurrency == 0 ? 4 : hardwareConcurrency;
                const size_t threadLimit = settings->MetadataScanMaxThreads > 0
                    ? static_cast<size_t>(settings->MetadataScanMaxThreads)
                    : automaticLimit;
                BoundedTaskRunner::ForEach(metadataTargets.size(), threadLimit, [&](size_t targetIndex) {
                    auto& target = metadataTargets[targetIndex];
                    auto result = TagLibReader::Read(target.path,
                        CommonUtils::path_to_utf8(target.track->trackPath));
                    target.track->catalogRecord = std::move(result.record);
                    target.track->LastErroString = std::move(result.error);
                    target.track->diagnostics = std::move(result.diagnostics);
                    if (target.track->catalogRecord) {
                        target.track->catalogRecord->diagnostics.insert(
                            target.track->catalogRecord->diagnostics.end(),
                            std::make_move_iterator(target.track->diagnostics.begin()),
                            std::make_move_iterator(target.track->diagnostics.end()));
                        target.track->diagnostics.clear();
                        target.track->formatInfo = makeLegacyProjection(*target.track->catalogRecord);
                    }
                    else if (target.track->LastErroString && target.track->diagnostics.empty()) {
                        target.track->diagnostics.push_back({
                            "metadata", "taglib", DiagnosticCode::MetadataReadFailed, DiagnosticSeverity::Error,
                            *target.track->LastErroString
                        });
                    }
                });
            }
            else {
                for (auto& target : metadataTargets) {
                    auto result = TagLibReader::Read(target.path,
                        CommonUtils::path_to_utf8(target.track->trackPath));
                    target.track->catalogRecord = std::move(result.record);
                    target.track->LastErroString = std::move(result.error);
                    target.track->diagnostics = std::move(result.diagnostics);
                    if (target.track->catalogRecord) {
                        target.track->catalogRecord->diagnostics.insert(
                            target.track->catalogRecord->diagnostics.end(),
                            std::make_move_iterator(target.track->diagnostics.begin()),
                            std::make_move_iterator(target.track->diagnostics.end()));
                        target.track->diagnostics.clear();
                        target.track->formatInfo = makeLegacyProjection(*target.track->catalogRecord);
                    }
                    else if (target.track->LastErroString && target.track->diagnostics.empty()) {
                        target.track->diagnostics.push_back({
                            "metadata", "taglib", DiagnosticCode::MetadataReadFailed, DiagnosticSeverity::Error,
                            *target.track->LastErroString
                        });
                    }
                    CommonUtils::show_progress_bar(progressInfoPtr, CommonUtils::ProgressBarType::SubProgress);
                }
            }
        }

        const auto settings = AppSettingsJson::AppSetting();
        const int requestedTier = scanTierRank(settings->ScanTier);
        if (requestedTier > 0) {
            std::vector<MetadataTarget> decodeTargets;
            decodeTargets.reserve(trackList.size());
            for (auto& track : trackList) {
                if (!track.catalogRecord ||
                    scanTierRank(track.catalogRecord->scanTier) >= requestedTier) {
                    continue;
                }
                decodeTargets.push_back({
                    albumPath.path() / track.trackPath,
                    &track
                });
            }
            if (!decodeTargets.empty()) {
                const unsigned int hardwareConcurrency = std::thread::hardware_concurrency();
                const size_t automaticLimit = hardwareConcurrency <= 1
                    ? 1 : std::min<std::size_t>(4, hardwareConcurrency / 2);
                const size_t decodeLimit = settings->DecodeWorkers > 0
                    ? static_cast<size_t>(settings->DecodeWorkers)
                    : automaticLimit;
                BoundedTaskRunner::ForEach(decodeTargets.size(), decodeLimit,
                    [&](size_t targetIndex) {
                        auto& target = decodeTargets[targetIndex];
                        applyScanTier(target.path, *target.track->catalogRecord);
                        target.track->formatInfo =
                            makeLegacyProjection(*target.track->catalogRecord);
                    });
            }
        }
        validateAlbumTags(trackList);
    }
    
    auto subProgressInfoPtr = std::make_shared<CommonUtils::ProgressBarInfo>("Processing...", albumCount, albumListPtr->size(), "Completed.", 20);
    auto progressInfoPtr = std::make_shared<CommonUtils::ProgressBarInfo>("Processing batch...", currentCount, totalCount, "Batch Completed.", 20, subProgressInfoPtr);
    CommonUtils::show_progress_bar(progressInfoPtr, CommonUtils::ProgressBarType::Complete);

    return albumCount;
}


bool AlbumCollection::SaveAlbumsToJSON(std::shared_ptr<DirectoryContentEntryList> albumListPtr,
    const std::filesystem::path& libraryRoot,
    const AlbumJsonBatchWriter& writeAlbum,
    std::uint64_t byteLimit, const std::string& fragmentSet,
    std::vector<ScanDiagnostic>* diagnostics,
    const std::function<bool(const std::filesystem::path&,
        const AlbumJsonStreamWriter&, size_t)>& writeAlbumStream)
{
    bool batchHasErrors = false;

    const auto reportDiagnostic = [diagnostics](const std::filesystem::path& albumPath,
        const std::optional<std::filesystem::path>& trackPath,
        const MediaDiagnostic& diagnostic) {
        if (diagnostics != nullptr) {
            diagnostics->push_back({
                CommonUtils::path_to_utf8(albumPath),
                trackPath
                    ? std::optional<std::string>(CommonUtils::path_to_utf8(*trackPath))
                    : std::nullopt,
                diagnostic, std::nullopt,
                diagnostic.nativeErrorCode, diagnostic.nativeErrorCategory
            });
        }
    };

    for (auto& album : *albumListPtr)
    {
        auto& albumPath = album.path;
        auto& trackList = album.trackList;
        rapidjson::Document mediaDoc;
        auto& allocator = mediaDoc.GetAllocator();
        bool albumHasErrors = false;
        // Create album object
        rapidjson::Value albumObj(rapidjson::kObjectType);

        // Add AlbumName
        std::string albumPathString = CommonUtils::utf8string_to_string(albumPath.path().u8string());
        std::string albumFolderString = CommonUtils::utf8string_to_string(albumPath.path().filename().u8string());
        
        rapidjson::Value albumPathStringVal;
        albumPathStringVal.SetString(albumPathString.c_str(), static_cast<SizeType>(albumPathString.size()), allocator);
        albumObj.AddMember("Album Path", albumPathStringVal, allocator);
        const auto libraryRootString = CommonUtils::path_to_utf8(libraryRoot);
        rapidjson::Value libraryRootValue;
        libraryRootValue.SetString(libraryRootString.c_str(),
            static_cast<SizeType>(libraryRootString.size()), allocator);
        albumObj.AddMember("Library Root", libraryRootValue, allocator);

        rapidjson::Value albumFolderStringVal;
        albumFolderStringVal.SetString(albumFolderString.c_str(), static_cast<SizeType>(albumFolderString.size()), allocator);
        albumObj.AddMember("Folder", albumFolderStringVal, allocator);
        if (album.scanManifestId) {
            rapidjson::Value manifestIdValue;
            manifestIdValue.SetString(album.scanManifestId->data(),
                static_cast<SizeType>(album.scanManifestId->size()), allocator);
            albumObj.AddMember("Scan Manifest ID", manifestIdValue, allocator);
        }

        //Album tracks list holder 
        rapidjson::Value trackMediaArray(rapidjson::kArrayType);
        std::size_t serializableTracks = 0;
        std::uint64_t serializedTrackBytes = 0;

        for (auto& item : trackList)
        {
            auto& trackName = item.trackPath;
            auto& lastError = item.LastErroString;
            std::filesystem::path trackPath = albumPath.path() / std::filesystem::path(trackName);

            if (MediaTrack::IsFileAcceptedAudioFile(trackPath)) {
                if (lastError) {
                    spdlog::error("Metadata extraction failed for '{}': {}",
                        CommonUtils::path_to_utf8(trackPath), *lastError);
                    if (item.diagnostics.empty()) {
                        item.diagnostics.push_back({
                            "metadata", "taglib", DiagnosticCode::MetadataReadFailed, DiagnosticSeverity::Error, *lastError
                        });
                    }
                    for (const auto& diagnostic : item.diagnostics) {
                        reportDiagnostic(albumPath.path(), trackPath, diagnostic);
                    }
                    batchHasErrors = true;
                    albumHasErrors = true;
                    continue;
                }
                if (!item.catalogRecord || !item.catalogRecord->sampleRateHz ||
                    *item.catalogRecord->sampleRateHz <= 0 || !item.catalogRecord->channels ||
                    *item.catalogRecord->channels <= 0) {
                    spdlog::error("Metadata extraction produced no usable audio stream for '{}'.",
                        CommonUtils::path_to_utf8(trackPath));
                    for (const auto& diagnostic : item.diagnostics) {
                        reportDiagnostic(albumPath.path(), trackPath, diagnostic);
                    }
                    if (item.catalogRecord) {
                        for (const auto& diagnostic : item.catalogRecord->diagnostics) {
                            reportDiagnostic(albumPath.path(), trackPath, diagnostic);
                        }
                    }
                    const MediaDiagnostic diagnostic{
                        "metadata", "catalog", DiagnosticCode::AudioPropertiesMissing, DiagnosticSeverity::Error,
                        "Metadata extraction produced no usable sample-rate/channel data."
                    };
                    item.diagnostics.push_back(diagnostic);
                    reportDiagnostic(albumPath.path(), trackPath, diagnostic);
                    batchHasErrors = true;
                    albumHasErrors = true;
                    continue;
                }

                rapidjson::Document trackDoc;
                const std::string jsonString = SerializeMediaRecord(*item.catalogRecord);
                trackDoc.Parse(jsonString.data(), jsonString.size());
                if (trackDoc.HasParseError()) {
                    // Get error code and message
                    rapidjson::ParseErrorCode errorCode = trackDoc.GetParseError();
                    const char* errorMsg = rapidjson::GetParseError_En(errorCode);
                    size_t errorOffset = trackDoc.GetErrorOffset();
                    item.LastErroString = errorMsg;
                    const MediaDiagnostic diagnostic{
                        "serialization", "rapidjson", DiagnosticCode::RecordJsonInvalid, DiagnosticSeverity::Error,
                        std::string(errorMsg) + " at offset " + std::to_string(errorOffset)
                    };
                        for (const auto& recordDiagnostic : item.catalogRecord->diagnostics) {
                            reportDiagnostic(albumPath.path(), trackPath, recordDiagnostic);
                        }
                    item.catalogRecord->diagnostics.push_back(diagnostic);
                    reportDiagnostic(albumPath.path(), trackPath, diagnostic);
                    spdlog::error("Error parsing metadata JSON for '{}': {} at offset {}",
                        CommonUtils::path_to_utf8(trackPath), errorMsg, errorOffset);
                    batchHasErrors = true;
                    albumHasErrors = true;
                    continue;
                }
                else
                {
                    for (const auto& diagnostic : item.catalogRecord->diagnostics) {
                        reportDiagnostic(albumPath.path(), trackPath, diagnostic);
                    }
                    ++serializableTracks;
                    if (jsonString.size() > (std::numeric_limits<std::uint64_t>::max)() - serializedTrackBytes) {
                        spdlog::error("Album metadata size overflow.");
                        batchHasErrors = true;
                        albumHasErrors = true;
                        continue;
                    }
                    serializedTrackBytes += static_cast<std::uint64_t>(jsonString.size());
                }
            }
        }

        if (serializableTracks > 0)
        {
            if (albumHasErrors) {
                continue;
            }
            if (byteLimit == 0) {
                if (!writeAlbumStream) {
                    spdlog::error("No streaming JSON writer was provided for an unlimited batch.");
                    batchHasErrors = true;
                    continue;
                }
                const AlbumJsonStreamWriter serializer = [&](std::ostream& output) {
                    rapidjson::OStreamWrapper stream(output);
                    rapidjson::Writer<rapidjson::OStreamWrapper> writer(stream);
                    if (!writer.StartObject() ||
                        !writer.Key("Album Path") ||
                        !writer.String(albumPathString.data(),
                            static_cast<rapidjson::SizeType>(albumPathString.size())) ||
                        !writer.Key("Library Root") ||
                        !writer.String(libraryRootString.data(),
                            static_cast<rapidjson::SizeType>(libraryRootString.size())) ||
                        !writer.Key("Folder") ||
                        !writer.String(albumFolderString.data(),
                            static_cast<rapidjson::SizeType>(albumFolderString.size()))) {
                        return false;
                    }
                    if (album.scanManifestId &&
                        (!writer.Key("Scan Manifest ID") ||
                            !writer.String(album.scanManifestId->data(),
                                static_cast<rapidjson::SizeType>(album.scanManifestId->size())))) {
                        return false;
                    }
                    if (!writer.Key("Tracks") || !writer.StartArray()) return false;
                    for (const auto& track : trackList) {
                        const auto trackPath = albumPath.path() / track.trackPath;
                        if (!MediaTrack::IsFileAcceptedAudioFile(trackPath)) continue;
                        const auto trackJson = SerializeMediaRecord(*track.catalogRecord);
                        if (!writer.RawValue(trackJson.data(), trackJson.size(), rapidjson::kObjectType)) {
                            return false;
                        }
                    }
                    return writer.EndArray() && writer.EndObject() && static_cast<bool>(output);
                };
                if (!writeAlbumStream(albumPath.path(), serializer, serializableTracks)) {
                    batchHasErrors = true;
                }
                continue;
            }
            bool fragmentTracks = false;
            if (byteLimit > 0) {
                Value emptyAlbum;
                emptyAlbum.CopyFrom(albumObj, allocator);
                Value emptyTracks(kArrayType);
                emptyAlbum.AddMember("Tracks", emptyTracks, allocator);
                StringBuffer emptyBuffer;
                Writer<StringBuffer> emptyWriter(emptyBuffer);
                if (!emptyAlbum.Accept(emptyWriter)) {
                    spdlog::error("Cannot serialize album envelope '{}'.", albumPathString);
                    batchHasErrors = true;
                    continue;
                }
                const auto overhead = static_cast<std::uint64_t>(emptyBuffer.GetSize()) + 2;
                const auto commas = static_cast<std::uint64_t>(serializableTracks - 1);
                fragmentTracks = overhead > byteLimit || commas > byteLimit - overhead ||
                    serializedTrackBytes > byteLimit - overhead - commas;
                if (!fragmentTracks) {
                    for (const auto& track : trackList) {
                        if (!MediaTrack::IsFileAcceptedAudioFile(albumPath.path() / track.trackPath)) continue;
                        rapidjson::Document trackDoc;
                        const auto json = SerializeMediaRecord(*track.catalogRecord);
                        trackDoc.Parse(json.data(), json.size());
                        if (trackDoc.HasParseError()) {
                            spdlog::error("Cannot serialize track '{}'.",
                                CommonUtils::path_to_utf8(albumPath.path() / track.trackPath));
                            batchHasErrors = true;
                            albumHasErrors = true;
                            break;
                        }
                        Value value;
                        value.CopyFrom(trackDoc, allocator);
                        trackMediaArray.PushBack(value, allocator);
                    }
                    if (albumHasErrors) continue;
                }
            }
            if (fragmentTracks) {
                std::size_t index = 0;
                for (const auto& track : trackList) {
                    const auto trackPath = albumPath.path() / track.trackPath;
                    if (!MediaTrack::IsFileAcceptedAudioFile(trackPath)) continue;
                    rapidjson::Document fragment;
                    auto& fragmentAllocator = fragment.GetAllocator();
                    Value fragmentObject(kObjectType);
                    Value albumPathValue;
                    albumPathValue.SetString(albumPathString.c_str(),
                        static_cast<SizeType>(albumPathString.size()), fragmentAllocator);
                    fragmentObject.AddMember("Album Path", albumPathValue, fragmentAllocator);
                    Value rootValue;
                    rootValue.SetString(libraryRootString.c_str(),
                        static_cast<SizeType>(libraryRootString.size()), fragmentAllocator);
                    fragmentObject.AddMember("Library Root", rootValue, fragmentAllocator);
                    Value folderValue;
                    folderValue.SetString(albumFolderString.c_str(),
                        static_cast<SizeType>(albumFolderString.size()), fragmentAllocator);
                    fragmentObject.AddMember("Folder", folderValue, fragmentAllocator);
                    if (album.scanManifestId) {
                        Value manifestIdValue;
                        manifestIdValue.SetString(album.scanManifestId->data(),
                            static_cast<SizeType>(album.scanManifestId->size()), fragmentAllocator);
                        fragmentObject.AddMember("Scan Manifest ID", manifestIdValue, fragmentAllocator);
                    }
                    if (serializableTracks > 1) {
                        Value fragmentInfo(kObjectType);
                        fragmentInfo.AddMember("index", static_cast<std::uint64_t>(index), fragmentAllocator);
                        fragmentInfo.AddMember("count", static_cast<std::uint64_t>(serializableTracks), fragmentAllocator);
                        Value setValue;
                        setValue.SetString(fragmentSet.c_str(), static_cast<SizeType>(fragmentSet.size()), fragmentAllocator);
                        fragmentInfo.AddMember("set", setValue, fragmentAllocator);
                        fragmentObject.AddMember("Fragment", fragmentInfo, fragmentAllocator);
                    }
                    rapidjson::Document trackDoc;
                    const auto trackJson = SerializeMediaRecord(*track.catalogRecord);
                    trackDoc.Parse(trackJson.data(), trackJson.size());
                    if (trackDoc.HasParseError()) {
                        spdlog::error("Cannot serialize track '{}'.", CommonUtils::path_to_utf8(trackPath));
                        batchHasErrors = true;
                        break;
                    }
                    Value tracks(kArrayType);
                    Value trackValue;
                    trackValue.CopyFrom(trackDoc, fragmentAllocator);
                    tracks.PushBack(trackValue, fragmentAllocator);
                    fragmentObject.AddMember("Tracks", tracks, fragmentAllocator);
                    StringBuffer buffer;
                    Writer<StringBuffer> writer(buffer);
                    ++index;
                    if (!fragmentObject.Accept(writer) ||
                        !writeAlbum(albumPath.path(), trackPath,
                            std::string(buffer.GetString(), buffer.GetSize()), 1,
                            index == serializableTracks)) {
                        batchHasErrors = true;
                        break;
                    }
                }
                continue;
            }
            // Add Tracks to album
            albumObj.AddMember("Tracks", trackMediaArray, allocator);

            StringBuffer buffer;
            Writer<StringBuffer> writer(buffer);
            if (!albumObj.Accept(writer) ||
                !writeAlbum(albumPath.path(), std::nullopt,
                    std::string(buffer.GetString(), buffer.GetSize()), serializableTracks, true)) {
                batchHasErrors = true;
            }
        }
        else {
            const auto message =
                "Album has no serializable tracks from the configured catalog extensions.";
            spdlog::error("{} '{}'.", message, CommonUtils::path_to_utf8(albumPath.path()));
            const MediaDiagnostic diagnostic{
                "serialization", "catalog", DiagnosticCode::AlbumHasNoSerializableTracks, DiagnosticSeverity::Error, message
            };
            reportDiagnostic(albumPath.path(), std::nullopt, diagnostic);
            batchHasErrors = true;
        }
    }

    return !batchHasErrors;
}




//ststic function that loads album list from a Json file and returns a DirectoryContentEntryList object
std::shared_ptr<DirectoryContentEntryList> AlbumCollection::LoadAlbumsFromJSON(std::filesystem::path path)
{
	std::shared_ptr<DirectoryContentEntryList> albumListPtr = std::make_shared<DirectoryContentEntryList>();

    if (!fs::exists(path)) {
        spdlog::error("**** no file - json file not found Error parsing JSON: ");
        return nullptr;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        spdlog::error("****Error file=null - parsing JSON: ");
        return nullptr;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string utf8_data = buffer.str();
    // Read the entire file into a string 
    //std::wstring json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    rapidjson::Document doc;

    // Parse the JSON data 
    doc.Parse(utf8_data.c_str());

    // Check for parse errors 
    if (doc.HasParseError()) {
        rapidjson::ParseErrorCode errorCode = doc.GetParseError();
        spdlog::error("Error parsing JSON: {}", rapidjson::GetParseError_En(errorCode));

        return nullptr;
    }

    //bool isObject = doc.IsObject();
    //auto jsonObject = doc.GetObj();
    // 
    //Check for array instead of object to match new JSON structure
    if (!doc.IsArray()) {
        spdlog::error("JSON root is not an array");
        return nullptr;
    }
    if (doc.Empty()) {
        spdlog::error("Album JSON contains no albums");
        return nullptr;
    }

    //Albums
    int iAlbumCount = 0;
    spdlog::info("");
	auto albumListSize = doc.GetArray().Capacity();
    for (const auto& albumVal : doc.GetArray()) {
        TrackInfoList trackList;

        // Validate that albumVal is an object with required fields
        if (!albumVal.IsObject()) {
            spdlog::error("Album entry {} is not an object", iAlbumCount);
            return nullptr;
        }
        if (!albumVal.HasMember("Album Path") || !albumVal["Album Path"].IsString()) {
            spdlog::error("Album entry {} missing or invalid Album Path", iAlbumCount);
            return nullptr;
        }
        if (!albumVal.HasMember("Folder") || !albumVal["Folder"].IsString()) {
            spdlog::error("Album entry {} missing or invalid Folder", iAlbumCount);
            return nullptr;
        }
        if (!albumVal.HasMember("Tracks") || !albumVal["Tracks"].IsArray()) {
            spdlog::error("Album entry {} missing or invalid Tracks array", iAlbumCount);
            return nullptr;
        }
        if (albumVal["Tracks"].Empty()) {
            spdlog::error("Album entry {} has no tracks", iAlbumCount);
            return nullptr;
        }
        std::optional<std::size_t> fragmentIndex;
        std::optional<std::size_t> fragmentCount;
        std::optional<std::string> fragmentSet;
        std::optional<std::string> scanManifestId;
        if (albumVal.HasMember("Scan Manifest ID")) {
            if (!albumVal["Scan Manifest ID"].IsString() ||
                albumVal["Scan Manifest ID"].GetStringLength() == 0) {
                spdlog::error("Album entry {} has invalid scan manifest identity.", iAlbumCount);
                return nullptr;
            }
            scanManifestId = std::string(albumVal["Scan Manifest ID"].GetString(),
                albumVal["Scan Manifest ID"].GetStringLength());
        }
        if (albumVal.HasMember("Fragment")) {
            const auto& fragment = albumVal["Fragment"];
            if (!fragment.IsObject() || !fragment.HasMember("index") ||
                !fragment["index"].IsUint64() || !fragment.HasMember("count") ||
                !fragment["count"].IsUint64() || fragment["count"].GetUint64() < 2 ||
                !fragment.HasMember("set") || !fragment["set"].IsString() ||
                fragment["set"].GetStringLength() == 0 ||
                fragment["count"].GetUint64() > (std::numeric_limits<std::size_t>::max)() ||
                fragment["index"].GetUint64() >= fragment["count"].GetUint64() ||
                albumVal["Tracks"].Size() != 1 ||
                !albumVal.HasMember("Library Root") || !albumVal["Library Root"].IsString()) {
                spdlog::error("Album entry {} has invalid fragment metadata.", iAlbumCount);
                return nullptr;
            }
            fragmentIndex = static_cast<std::size_t>(fragment["index"].GetUint64());
            fragmentCount = static_cast<std::size_t>(fragment["count"].GetUint64());
            fragmentSet = std::string(fragment["set"].GetString(), fragment["set"].GetStringLength());
        }

        std::string albumPathStr = albumVal["Album Path"].GetString();
        fs::path albumpath = CommonUtils::path_from_utf8(albumPathStr);
        std::optional<std::filesystem::path> libraryRoot;
        if (albumVal.HasMember("Library Root") && albumVal["Library Root"].IsString()) {
            libraryRoot = CommonUtils::path_from_utf8(albumVal["Library Root"].GetString());
        }


        // Updated logging to reflect new album name source
        std::cout << "\33[2K\r";  // Clear entire line and move cursor to start
        std::cout << "\33[A\33[2K\r";  // Move up 1 line, clear it, and return to start
        //auto albumLogStr = std::format(L"Album [{}]: {}", ++iAlbumCount, albumpath);
        spdlog::info("Loading albums... Album {}/{}", ++iAlbumCount, albumListSize);


        const auto& mediaTrackList = albumVal["Tracks"].GetArray();
        for (SizeType i = 0; i < mediaTrackList.Size(); i++) {
            if (!mediaTrackList[i].IsObject()) {
                spdlog::error("Track {} in album '{}' has invalid metadata structure",
                    i, albumPathStr);
                return nullptr;
            }
            if (mediaTrackList[i].HasMember("schemaVersion")) {
                if (!libraryRoot) {
                    spdlog::error("Schema-v2 album '{}' is missing Library Root; rescan is required.",
                        albumPathStr);
                    return nullptr;
                }
                std::string parseError;
                auto record = ParseMediaRecord(mediaTrackList[i], &parseError);
                if (!record) {
                    spdlog::error("Track {} in album '{}' has invalid catalog metadata: {}",
                        i, albumPathStr, parseError);
                    return nullptr;
                }
                std::filesystem::path relativePath = CommonUtils::path_from_utf8(record->relativePath);
                MediaTrack track{
                    relativePath,
                    record->sizeBytes,
                    makeLegacyProjection(*record),
                    std::nullopt,
                    std::nullopt
                };
                track.catalogRecord = std::move(record);
                track.libraryRoot = *libraryRoot;
                trackList.push_back(std::move(track));
                continue;
            }
            if (!mediaTrackList[i].HasMember("format") ||
                !mediaTrackList[i]["format"].IsObject()) {
                spdlog::error("Track {} in album '{}' has invalid metadata structure",
                    i, albumPathStr);
                return nullptr;
            }
            FFprobeOutput mi = MediaTrack::ParseFFprobeInformation(mediaTrackList[i]);
            if (!std::ranges::any_of(mi.streams, [](const Stream& stream) {
                    return stream.codec_type == "audio";
                })) {
                spdlog::error("Track {} in album '{}' has no audio stream metadata", i, albumPathStr);
                return nullptr;
            }
            //Derive track filename from FFprobeOutput or use fallback
            fs::path trackFilename = mi.format.filename.empty() ?
                fs::path("track_" + std::to_string(i)) :
                CommonUtils::path_from_utf8(mi.format.filename).filename();
            trackList.push_back({
                trackFilename,
                mi.format.fs_file_size.value_or(0),
                mi,
                std::nullopt, // mediaInfoString (empty, as JSON is parsed into FFprobeOutput)
                std::nullopt // lastError
                });
        }

        if (trackList.size() > 0) {
            std::filesystem::directory_entry entry{ albumpath };
            albumListPtr->push_back({
                entry, std::move(trackList), fragmentIndex, fragmentCount, fragmentSet, scanManifestId
            });
        }

    }

    return albumListPtr;
}

bool AlbumCollection::ReassembleAlbums(std::shared_ptr<DirectoryContentEntryList> albums)
{
    struct Group {
        std::optional<std::size_t> count;
        std::optional<std::string> set;
        std::map<std::size_t, MediaAlbum> fragments;
        std::vector<MediaAlbum> whole;
    };
    std::map<std::pair<fs::path, fs::path>, Group> groups;
    for (auto& album : *albums) {
        const auto root = album.trackList.front().libraryRoot.value_or(fs::path{}).lexically_normal();
        const auto key = std::make_pair(root, album.path.path().lexically_normal());
        auto& group = groups[key];
        if (!album.fragmentIndex) {
            if (group.count) {
                spdlog::error("Duplicate or mixed album fragments for '{}'.", CommonUtils::path_to_utf8(key.second));
                return false;
            }
            group.whole.push_back(std::move(album));
            continue;
        }
        if (!group.whole.empty() || !album.fragmentCount || !album.fragmentSet ||
            (group.count && *group.count != *album.fragmentCount) ||
            (group.set && *group.set != *album.fragmentSet)) {
            spdlog::error("Inconsistent album fragments for '{}'.", CommonUtils::path_to_utf8(key.second));
            return false;
        }
        group.count = album.fragmentCount;
        group.set = album.fragmentSet;
        if (!group.fragments.emplace(*album.fragmentIndex, std::move(album)).second) {
            spdlog::error("Duplicate album fragment for '{}'.", CommonUtils::path_to_utf8(key.second));
            return false;
        }
    }
    DirectoryContentEntryList result;
    for (auto& [key, group] : groups) {
        if (!group.whole.empty()) {
            for (auto& album : group.whole) result.push_back(std::move(album));
            continue;
        }
        if (!group.count || group.fragments.size() != *group.count) {
            spdlog::error("Missing album fragments for '{}'.", CommonUtils::path_to_utf8(key.second));
            return false;
        }
        MediaAlbum complete{ group.fragments.begin()->second.path, {} };
        std::set<fs::path> trackPaths;
        for (auto& [index, fragment] : group.fragments) {
            for (auto& track : fragment.trackList) {
                if (!track.catalogRecord || !track.libraryRoot ||
                    !trackPaths.insert(track.trackPath.lexically_normal()).second) {
                    spdlog::error("Invalid or duplicate track in album fragments for '{}'.",
                        CommonUtils::path_to_utf8(key.second));
                    return false;
                }
                complete.trackList.push_back(std::move(track));
            }
        }
        result.push_back(std::move(complete));
    }
    *albums = std::move(result);
    return true;
}

int compareLex(const char* a, const char* b) {
    // Walk both strings until we hit a difference or the end of one
    while (*a != '\0' && *b != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    // At this point either *a or *b (or both) is '\0', or they differ
    unsigned char ua = static_cast<unsigned char>(*a);
    unsigned char ub = static_cast<unsigned char>(*b);
    if (ua < ub)       return -1;
    else if (ua > ub)  return  1;
    else                return  0;
}

#include <string>
#include <cstddef>  // for std::size_t
#include <algorithm> // for std::min

int compareLex(const std::string& a, const std::string& b) {
    std::size_t n = std::min(a.size(), b.size());
    // Compare up to the length of the shorter string
    for (std::size_t i = 0; i < n; ++i) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (ca < cb)      return -1;
        else if (ca > cb) return  1;
    }
    // All equal up to n: shorter string is “less”
    if (a.size() < b.size()) return -1;
    if (a.size() > b.size()) return  1;
    return 0; // exact match
}

int compareLex(const std::wstring& a, const std::wstring& b) {
    std::size_t n = std::min(a.size(), b.size());
    // Compare up to the length of the shorter string
    for (std::size_t i = 0; i < n; ++i) {
        unsigned int ca = static_cast<unsigned int>(a[i]);
        unsigned int cb = static_cast<unsigned int>(b[i]);
        if (ca < cb)      return -1;
        else if (ca > cb) return  1;
    }
    // All equal up to n: shorter string is “less”
    if (a.size() < b.size()) return -1;
    if (a.size() > b.size()) return  1;
    return 0; // exact match
}

//-------------COMPARE

int CompareTags(const std::vector<MediaTrack>& firstTracks, const std::vector<MediaTrack>& secondTracks,
                const std::string& tagName)
{
    const auto findTag = [&tagName](const std::vector<MediaTrack>& tracks) -> const std::string* {
        if (tracks.empty() || !tracks.front().formatInfo.format.tags) {
            return nullptr;
        }
        return JsonUtils::getValueByKey(*tracks.front().formatInfo.format.tags, tagName);
    };
    const std::string* first = findTag(firstTracks);
    const std::string* second = findTag(secondTracks);
    if (first == nullptr || second == nullptr) {
        return first == second ? 0 : (first == nullptr ? -1 : 1);
    }
    return compareLex(*first, *second);
}


void AlbumCollection::SortAlbums(std::shared_ptr<std::vector<MediaAlbum>> albumListPtr,
                                 const std::vector<std::pair<SortBy, bool>>& criteria)
{
    std::ranges::stable_sort(*albumListPtr, [&criteria](const MediaAlbum& a, const MediaAlbum& b) {
        for (const auto& [criterion, ascending] : criteria) {
            int result = 0;
            switch (criterion) {
            case SortBy::AlbumName: {
                const auto nameA = CommonUtils::path_to_utf8(a.path.path().filename());
                const auto nameB = CommonUtils::path_to_utf8(b.path.path().filename());
                result = compareLex(nameA, nameB);
                break;
            }

            case SortBy::Artist:
                result = CompareTags(a.trackList, b.trackList, "artist");
                break;

            case SortBy::AlbumArtist:
                result = CompareTags(a.trackList, b.trackList, "album_artist");
                break;

            case SortBy::Year:
                result = CompareTags(a.trackList, b.trackList, "year");
                break;

            case SortBy::TrackCount:
                result = a.trackList.size() < b.trackList.size() ? -1 :
                    (a.trackList.size() > b.trackList.size() ? 1 : 0);
                break;

            default:
                break;
            }
            if (result != 0) {
                return ascending ? result < 0 : result > 0;
            }
        }

        return compareLex(a.path.path().native(), b.path.path().native()) < 0;
    });
}

//void AlbumCollection::SortAlbums(std::shared_ptr<DirectoryContentEntryList> albumListPtr, bool ascending)
//{
//    std::ranges::stable_sort(*albumListPtr, SortByTracks<SortOrder::Ascending>{});
//}




SimilarAlbumIndexPairs AlbumCollection::FindDuplicateAlbums(std::shared_ptr<DirectoryContentEntryList> albumListPtr) {
    auto logger = spdlog::get("console");
    if (!logger) logger = spdlog::stdout_color_mt("console");

    SimilarAlbumIndexPairs duplicatedAlbumList;
    if (albumListPtr->size() < 2) {
        logger->info("Album list too small: {}", albumListPtr->size());
        return duplicatedAlbumList;
    }

    auto appSettingPtr = AppSettingsJson::AppSetting();
    size_t minMatchingTracks = appSettingPtr->MinMatchingTracksForDuplicate;

    // Group by track count
    std::map<size_t, std::vector<DirectoryContentEntryList::const_iterator>> trackCountGroups;
    for (auto it = albumListPtr->begin(); it != albumListPtr->end(); ++it) {
        auto tracks = (*it).trackList.size();
        if (tracks >= minMatchingTracks) {
            trackCountGroups[tracks].push_back(it);
        }
    }

    // Process groups
    for (const auto& [trackCount, group] : trackCountGroups) {
        if (group.size() < 2) {
            logger->warn("Skipping group with {} albums ({} tracks)", group.size(), trackCount);
            continue;
        }
        logger->info("Checking {} albums with {} tracks", group.size(), trackCount);
        auto dupAlbums = FindDuplicationInGroup(albumListPtr, group);
        duplicatedAlbumList.insert(duplicatedAlbumList.end(), dupAlbums.begin(), dupAlbums.end());
    }

    logger->info("Total duplicate groups: {}", duplicatedAlbumList.size());
    return duplicatedAlbumList;
}



SimilarAlbumIndexPairs AlbumCollection::FindDuplicationInGroup(std::shared_ptr<DirectoryContentEntryList> albumListPtr, const std::vector<DirectoryContentEntryList::const_iterator>& group) {
    //auto logger = spdlog::get("console");
    //if (!logger) {
    //    logger = spdlog::stdout_color_mt("console");
    //}

    SimilarAlbumIndexPairs duplicatedAlbumList;

    if (group.size() < 2) {
        spdlog::warn("Group too small for duplicates: {} albums", group.size());
        return duplicatedAlbumList;
    }

    auto appSettingPtr = AppSettingsJson::AppSetting();
    double sizeMatchPercentageThreshold = appSettingPtr->SizeMatchPercentageThreshold;

    // Group albums by album and artist tags
    struct AlbumKey {
        std::string album, artist;
        bool operator<(const AlbumKey& other) const {
            return std::tie(album, artist) < std::tie(other.album, other.artist);
        }
    };
    std::map<AlbumKey, std::vector<std::pair<DirectoryContentEntryList::const_iterator,
        std::vector<double>>>> albumGroups;

    for (const auto& it : group) {
        if (it == albumListPtr->end()) {
            spdlog::warn("Invalid iterator in group");
            continue;
        }
        const auto& dirEntry = it->path;
        const auto& trackList = it->trackList;
        if (trackList.empty()) {
            spdlog::warn("Empty track list for album at iterator index {}", std::distance(albumListPtr->cbegin(), it));
            continue;
        }

        // Extract album name with robust error handling
        std::string albumName = "unknown_album_" + std::to_string(std::distance(albumListPtr->cbegin(), it));
        try {
            if (fs::exists(dirEntry.path()) && dirEntry.is_directory()) {
                // Use wstring to avoid UTF-8 conversion issues
                albumName = CommonUtils::path_to_utf8(dirEntry.path().filename());
                // Sanitize
                bool invalid = false;
                for (char c : albumName) {
                    if (static_cast<unsigned char>(c) < 0x20) {
                        invalid = true;
                        break;
                    }
                }
                if (invalid) {
                    spdlog::warn("Invalid characters in album name: {}",
                        CommonUtils::path_to_utf8(dirEntry.path()));
                    albumName = "sanitized_album_" + std::to_string(std::distance(albumListPtr->cbegin(), it));
                }
            }
            else {
                spdlog::warn("Non-existent or invalid directory: {}",
                    CommonUtils::path_to_utf8(dirEntry.path()));
            }
        }
        catch (const fs::filesystem_error& e) {
            spdlog::error("Filesystem error accessing path: {} ({})",
                CommonUtils::path_to_utf8(dirEntry.path()), e.what());
        }
        catch (const std::exception& e) {
            spdlog::error("Unexpected error accessing filename: {} ({})",
                CommonUtils::path_to_utf8(dirEntry.path()), e.what());
        }
        catch (...) {
            spdlog::error("Unknown error accessing filename for iterator index {}", std::distance(albumListPtr->cbegin(), it));
        }

        // Extract metadata from first track
        std::string artist;
        const auto& firstTrack = trackList[0];
        if (firstTrack.catalogRecord) {
            const auto& tags = firstTrack.catalogRecord->tags;
            if (const auto album = tags.find("album"); album != tags.end() && !album->second.empty()) {
                albumName = album->second.front();
            }
        }
        else if (firstTrack.formatInfo.format.tags &&
            firstTrack.formatInfo.format.tags->contains("album")) {
            albumName = firstTrack.formatInfo.format.tags->at("album");
        }
        std::set<std::string> albumArtists;
        std::set<std::string> artists;
        for (const auto& track : trackList) {
            if (track.catalogRecord) {
                for (const auto& [key, values] : track.catalogRecord->tags) {
                    auto* destination = key == "albumArtists" ? &albumArtists :
                        key == "artists" ? &artists : nullptr;
                    if (destination) {
                        for (const auto& value : values) {
                            if (!value.empty()) destination->insert(value);
                        }
                    }
                }
            }
            else if (track.formatInfo.format.tags) {
                const auto& tags = *track.formatInfo.format.tags;
                for (const auto& [key, destination] :
                    { std::pair{ "album_artist", &albumArtists }, std::pair{ "artist", &artists } }) {
                    const auto value = tags.find(key);
                    if (value != tags.end() && !value->second.empty()) destination->insert(value->second);
                }
            }
        }
        const auto& identity = albumArtists.empty() ? artists : albumArtists;
        for (const auto& value : identity) {
            artist += std::to_string(value.size()) + ":" + value;
        }
        const auto& trackName = firstTrack.trackPath;
        const auto& lastError = firstTrack.LastErroString;
        if (firstTrack.catalogRecord ||
            (firstTrack.formatInfo.format.tags &&
                firstTrack.formatInfo.format.tags->contains("album"))) {
            // Sanitize tags (avoid id3v2_priv.ÿþ)
            bool invalidAlbum = false;
            for (char c : albumName) {
                if (static_cast<unsigned char>(c) < 0x20) {
                    invalidAlbum = true;
                    break;
                }
            }
            if (invalidAlbum) {
                try {
                    albumName = CommonUtils::path_to_utf8(dirEntry.path().filename());
                }
                catch (...) {
                    albumName = "sanitized_album_" + std::to_string(std::distance(albumListPtr->cbegin(), it));
                }
            }
            for (char c : artist) {
                if (static_cast<unsigned char>(c) < 0x20) {
                    artist.clear();
                    break;
                }
            }
        }
        else if (lastError) {
            spdlog::error("Metadata error for track: {} ({})",
                CommonUtils::path_to_utf8(dirEntry.path() / trackName), lastError.value());
        }

        // Collect sorted track durations
        std::vector<double> durations;
        durations.reserve(trackList.size());
        for (const auto& track : trackList) {
            if (track.catalogRecord) {
                durations.push_back(track.catalogRecord->durationSeconds.value_or(0.0));
            }
            else {
                durations.push_back(track.formatInfo.format.duration.value_or(0.0));
            }
        }
        std::ranges::sort(durations);

        albumGroups[{albumName, artist}].emplace_back(it, std::move(durations));
    }

    // Find duplicates by comparing durations
    for (const auto& [key, albums] : albumGroups) {
        if (albums.size() < 2) continue;

        for (size_t i = 0; i < albums.size(); ++i) {
            for (size_t j = i + 1; j < albums.size(); ++j) {
                const auto& [album1, durations1] = albums[i];
                const auto& [album2, durations2] = albums[j];

                bool isDuplicate = durations1.size() == durations2.size();
                if (isDuplicate) {
                    for (size_t k = 0; k < durations1.size(); ++k) {
                        double d1 = durations1[k], d2 = durations2[k];
                        if (d1 == 0.0 || d2 == 0.0) {
                            if (d1 != d2) {
                                isDuplicate = false;
                                break;
                            }
                            continue;
                        }
                        double minD = std::min(d1, d2);
                        double maxD = std::max(d1, d2);
                        double diffPercentage = maxD > 0 ? 100 * (maxD - minD) / maxD : 0;
                        if (diffPercentage > sizeMatchPercentageThreshold) {
                            isDuplicate = false;
                            break;
                        }
                    }
                }

                if (isDuplicate) {
                    spdlog::info("Duplicate albums: {} and {} (album: {}, artist: {})",
                        CommonUtils::path_to_utf8(album1->path.path()),
                        CommonUtils::path_to_utf8(album2->path.path()),
                        key.album.empty() ? "(unknown)" : key.album,
                        key.artist.empty() ? "(unknown)" : key.artist);

                    duplicatedAlbumList.push_back({
                        static_cast<std::size_t>(std::distance(albumListPtr->cbegin(), album1)),
                        static_cast<std::size_t>(std::distance(albumListPtr->cbegin(), album2)) });
                }
            }
        }
    }

    spdlog::info("Found {} duplicate groups in group of {} albums", duplicatedAlbumList.size(), group.size());

    return duplicatedAlbumList;
}


bool AlbumCollection::ExportToDatabase(std::shared_ptr<DirectoryContentEntryList> albums,
    std::filesystem::path databasePath,
    const std::optional<std::filesystem::path>& libraryRoot)
{
    if (!albums) {
        spdlog::error("Cannot import a null album collection into SQLite.");
        return false;
    }
    return CatalogDatabase::Import(*albums, databasePath, libraryRoot);
}
