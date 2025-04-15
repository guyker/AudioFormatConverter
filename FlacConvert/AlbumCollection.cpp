

#include <vector>
#include <map>
#include <algorithm>
#include <ranges>
#include <format>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/stringbuffer.h"
#include <iostream>

#include "AlbumCollection.h"
#include "MediaTrack.h"
#include "FolderConvert.h"
#include "JsonUtils.h"
#include "PlatformUtils.h"

#include "CommonUtils.h"
#include "FFmpeg.h"

namespace fs = std::filesystem;
using namespace rapidjson;



//Load album collection from a directory into _AlbumList
// 1. loads album and media tracs
// 2. [Optionally] load metadat for individual tracks
bool AlbumCollection::LoadAlbumCollection(std::filesystem::path albumCollectionDirPath, bool bIncludeMetadata)
{
    auto startTime = std::chrono::steady_clock::now();
    
    spdlog::info("Scanning collection...");

    //Scan directory and load all tracks location
    LoadAlbumCollectionRecursively(albumCollectionDirPath, AppSettingsJson::AppSetting()->RecursionDirectorySearchDepth);

    auto endLoadTime = std::chrono::steady_clock::now();    
    spdlog::info("Completed, Found {} Albums, processing time: [{}ms]", _AlbumList.size(), std::chrono::duration_cast<std::chrono::milliseconds>(endLoadTime - startTime).count());;

	if (bIncludeMetadata)
	{
		//Load metadata for all media files in the album collection
		spdlog::info("Loading Albums metadata... ");
		auto nAlbums = LoadAllMetadata(false); //load media metadate
		auto endLoadMEtadataTime = std::chrono::steady_clock::now();
		spdlog::info("Completed (Loading Albums metadata) Found {} Albums, processing time: [{}ms]", nAlbums, std::chrono::duration_cast<std::chrono::milliseconds>(endLoadMEtadataTime - endLoadTime).count());
	}

    return true;
}




TrackInfoList AlbumCollection::LoadAlbumCollectionRecursively(std::filesystem::path path, int depth)
{
    //Empty list to store all potential tracks under the current directory (path)
    TrackInfoList currentDirTrackList;

    if (depth == 0) // reached max recursive depth
    {
        return currentDirTrackList;
    }

    if (fs::exists(path)) {
        for (const fs::directory_entry& entry : fs::directory_iterator(path)) {
            if (entry.is_directory()) {
                //Scan directory and return the list of files under the directory entry (one level).

                auto trackList = LoadAlbumCollectionRecursively(entry.path(), depth - 1);

                //check if the current folder has at least one track and add it to a new Album
                if (trackList.size() > 0)
                {
                    _AlbumList.push_back({ entry, trackList });
                }
            }
            else {
                if (MediaTrack::IsFileAcceptedAudioFile(entry))
                {
                    auto path2Fixed = entry.path().lexically_normal().native();
                    uintmax_t fs_fileSize;
                    try {
                        fs_fileSize = fs::file_size(path2Fixed);
                    }
                    catch (const fs::filesystem_error& e) {
						spdlog::error("Error getting file size for {}: {}", PlatformUtils::WideToUTF8(path2Fixed), e.what());
                        fs_fileSize = 0; // Default value or skip
                    }
                    auto fileName = entry.path().filename();
                    currentDirTrackList.push_back({ fileName, fs_fileSize, FFprobeOutput{}, std::wstring{L"{}"}});
                }
            }
        }
    }
    else
    {
        std::cout << std::format("***Error: Media library not found: : {}", path.string()) << std::endl;
    }

    return currentDirTrackList;
}



//Load all media media information from the preloaded album list (_AlbumList)
size_t AlbumCollection::LoadAllMetadata(bool bAsync)
{
    int albumCount = 0;
       
    for (auto& [albumPath, trackList] : _AlbumList)
    {
        std::string name = "N/A";
        if (albumPath.is_directory() && albumPath.path().has_filename())
        {
            std::string name = PlatformUtils::WideToUTF8(albumPath.path().filename().wstring());
           // name = albumPath.path().filename().generic_string();
        }
        std::string progress = std::format("Processing... {}/{} - {}", ++albumCount, _AlbumList.size(), name);
        CommonUtils::show_circular_progress(progress);

        //Album tracks list holder 
        std::vector<std::tuple<MediaLoadingFuture, FFprobeOutput&, std::wstring&>> asyncFutureList;

        for (auto& [trackName, size, mediaInfo, mediaInfoString, lastError] : trackList)
        {
            std::filesystem::path trackPath = albumPath.path() / std::filesystem::path(trackName);

            if (MediaTrack::IsValidMedia(trackPath)) {
                auto path2Fixed = trackPath.lexically_normal().native();

                if (bAsync)
                {
                    auto miFuture = std::async(std::launch::async, MediaTrack::ReadMediaInfoFromFile, path2Fixed);
                    asyncFutureList.push_back({ std::move(miFuture), mediaInfo, mediaInfoString });

                }
                else
                {
                    auto [mi_ret, jsonString_ret] = MediaTrack::ReadMediaInfoFromFile(path2Fixed);
                    mediaInfoString = jsonString_ret;
                    mediaInfo = mi_ret;
                }
            }
        }

        if (bAsync)
        {
            for (auto& [furure_ret, mediaInfo, mediaInfoString] : asyncFutureList)
            {
                auto [mediaInfo_ret, mediaInfoString_ret] = furure_ret.get();
                mediaInfo = mediaInfo_ret;
                mediaInfoString = mediaInfoString_ret;
            }
        }
    }

	std::cout << std::endl;

    return _AlbumList.size();
}

void SaveAlbumsAsJSONFile_FFmpegError(std::filesystem::path outPath)
{
    std::vector<FFmpeg::FFmpegLogItem> ffmpegErrorList = FFmpeg::get_ffmpeg_logs();


    // Create a RapidJSON Document and set it as an array.
    Document document;
    document.SetArray();
    Document::AllocatorType& allocator = document.GetAllocator();

    //	spdlog::error("Error parsing JSON metadata track (adding ro error list): ", outPath.c_str());

    for (auto& item: ffmpegErrorList)
    {
        rapidjson::Value trackObj(rapidjson::kObjectType);

        // 1. Convert and add the file path (std::filesystem::path -> UTF-8 std::string).
        trackObj.AddMember("URL", rapidjson::Value(item.url.c_str(), allocator), allocator);
        trackObj.AddMember("Error", rapidjson::Value(item.level.c_str(), allocator), allocator);
        trackObj.AddMember("Message", rapidjson::Value(item.message.c_str(), allocator), allocator);

        document.PushBack(trackObj, allocator);
    }

    // Create a StringBuffer to hold the JSON output.
    StringBuffer buffer;
    // Use PrettyWriter for formatted output (you can use Writer for compact output).
    PrettyWriter<StringBuffer> writer(buffer);
    document.Accept(writer);

    // Write the JSON string to a file.
    std::ofstream outFile(outPath.string() + "_ffmpeg_error.json");
    if (!outFile) {
        spdlog::error("Error: could not open file for writing.");
        return;
    }
    outFile << buffer.GetString();
    outFile.close();

    std::cout << "JSON file created successfully as tracks.json" << std::endl;
}

std::list<MediaTrack> SaveAlbumsAsJSON_LasrError;

void SaveAlbumsAsJSONFile_LasrError(std::list<MediaTrack> mediaTracks, std::filesystem::path outPath)
{

    // Create a RapidJSON Document and set it as an array.
    Document document;
    document.SetArray();
    Document::AllocatorType& allocator = document.GetAllocator();

//	spdlog::error("Error parsing JSON metadata track (adding ro error list): ", outPath.c_str());

	for (auto& track : mediaTracks)
	{
        rapidjson::Value trackObj(rapidjson::kObjectType);

        // 1. Convert and add the file path (std::filesystem::path -> UTF-8 std::string).
        std::string fileNameStr = track.trackPath.string();
        trackObj.AddMember("Track name", rapidjson::Value(fileNameStr.c_str(), allocator), allocator);

        std::string trackPathStr = track.formatInfo.format.filename.c_str();
        trackObj.AddMember("Path", rapidjson::Value(trackPathStr.c_str(), allocator), allocator);

        // 2. Add the file size.
        trackObj.AddMember("File Size", static_cast<unsigned long long>(track.fs_fileSize), allocator);
        trackObj.AddMember("format_duration", static_cast<unsigned long long>(track.formatInfo.format.duration.value_or(0.0)), allocator);
        trackObj.AddMember("format_format_name", rapidjson::Value(track.formatInfo.format.format_name.c_str(), allocator), allocator);
        
        trackObj.AddMember("Error Reason", rapidjson::Value(track.LastErroString.value_or("***NO FOUND***").c_str(), allocator), allocator);

        // 3. Convert and add the FFprobeOutput (media information / tags).
        //rapidjson::Value formatInfoObj = FFprobeOutputToJson(track.formatInfo, allocator);
        //trackObj.AddMember("formatInfo", formatInfoObj, allocator);

        //// 4. Convert the wide string (mediaInfoString) to UTF-8 and add it.
        //std::string mediaInfoUtf8 = WStringToUTF8(track.mediaInfoString);
        //trackObj.AddMember("mediaInfoString", rapidjson::Value(mediaInfoUtf8.c_str(), allocator), allocator);

        // Add the track object to the JSON array.
        document.PushBack(trackObj, allocator);
	}

    // Create a StringBuffer to hold the JSON output.
    StringBuffer buffer;
    // Use PrettyWriter for formatted output (you can use Writer for compact output).
    PrettyWriter<StringBuffer> writer(buffer);
    document.Accept(writer);

    // Write the JSON string to a file.
    std::ofstream outFile(outPath.string() + "_error.json");
    if (!outFile) {
        spdlog::error("Error: could not open file for writing.");
        return;
    }
    outFile << buffer.GetString();
    outFile.close();

    std::cout << "JSON file created successfully as tracks.json" << std::endl;
}



bool AlbumCollection::SaveAlbumsAsJSON(std::filesystem::path path)
{
    rapidjson::Document mediaDoc;
    mediaDoc.SetObject();

    for (auto [albumPath, trackList] : _AlbumList)
    {
        //Album tracks list holder 
        rapidjson::Value trackMediaArray(rapidjson::kArrayType);

        for (auto& item : trackList)
        {
            auto& [trackName, size, mediaInfo, mediaInfoString, lastError] = item;
            std::filesystem::path trackPath = albumPath.path() / std::filesystem::path(trackName);

            auto hasExtension = trackPath.has_extension();
            auto fileEextension = trackPath.extension();
            // std::wstring entryPath{ trackPath.wstring() };
            if (trackPath.has_extension() && (fileEextension == ".flac" || fileEextension == ".mp3")) {

                rapidjson::Document trackDoc;

                std::string utf8Json = PlatformUtils::wstringToUtf8_ver2(mediaInfoString);
                trackDoc.Parse(utf8Json.c_str());
                if (trackDoc.HasParseError()) {
                    // Get error code and message
                    rapidjson::ParseErrorCode errorCode = trackDoc.GetParseError();
                    const char* errorMsg = rapidjson::GetParseError_En(errorCode);
                    size_t errorOffset = trackDoc.GetErrorOffset();
                    item.LastErroString = errorMsg;
					SaveAlbumsAsJSON_LasrError.push_back(item);
                    spdlog::error("Error parsing JSON: ", errorMsg);
                    continue;
                }
                else
                {
					//Add media information to the track list
                    Value childValue;
                    childValue.CopyFrom(trackDoc, mediaDoc.GetAllocator()); // Copy childDoc into childValue using the parent allocator
                    trackMediaArray.PushBack(childValue, mediaDoc.GetAllocator());
                }
            }
        }

        if (trackMediaArray.Size() > 0)
        {
            try
            {
                ////track list exists add album
                //std::string name = albumPath.path().generic_string();
                //Value key(name.c_str(), mediaDoc.GetAllocator());
                //mediaDoc.AddMember(key, trackMediaArray, mediaDoc.GetAllocator());



                try
                {
                    //track list exists add album
                    std::wstring name = albumPath.path().wstring();
                    std::string utf8Key = PlatformUtils::wstringToUtf8_ver2(name);
                    

                    Value key(utf8Key.c_str(), mediaDoc.GetAllocator());
                    mediaDoc.AddMember(key, trackMediaArray, mediaDoc.GetAllocator());
                }
                catch (const std::exception& ex) {
                    std::wcout << " ### EXCEOTION parsing json from ffmpeg: " << albumPath.path() << std::endl << ex.what() << std::endl;
                }

            }
            catch (...)
            {
                int i = 0;
            }
        }
    }

	SaveAlbumsAsJSONFile_LasrError(SaveAlbumsAsJSON_LasrError, path);
    SaveAlbumsAsJSONFile_FFmpegError(path);

    if (fs::exists(path)) {
        std::error_code ec;
        if (!fs::remove(path, ec)) {
            spdlog::error("Failed to remove existing file: ", ec.message());
        }
    }

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    mediaDoc.Accept(writer);
    const char* json = buffer.GetString();

    // Save the JSON string to a file
    std::ofstream file(path);
    if (file.is_open()) {
        file << json;
        file.close();

        //std::wcout << std::endl << std::format(L"====> Document saved to: {}", path.generic_wstring()) << std::endl;
    }
    else {
        spdlog::error("Unable to open file for writing");
        return false;
    }

    return true;
}




//ststic function that loads album list from a Json file and returns a DirectoryContentEntryList object
bool AlbumCollection::RestoreAlbumCollectionFromJSON(std::filesystem::path path)
{
  //  DirectoryContentEntryList albumList;

    if (!fs::exists(path)) {
        std::cout << "**** no file - json file not found Error parsing JSON: " << std::endl;
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cout << "****Error file=null - parsing JSON: " << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string utf8_data = buffer.str();
	std::wstring json = CommonUtils::utf8ToWstring(utf8_data);
    // Read the entire file into a string 
    //std::wstring json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    rapidjson::Document doc;

    // Parse the JSON data 
    std::string utf8Json = PlatformUtils::wstringToUtf8_ver2(json);
    doc.Parse(utf8Json.c_str());

    // Check for parse errors 
    if (doc.HasParseError()) {
        rapidjson::ParseErrorCode errorCode = doc.GetParseError();
        spdlog::error("Error parsing JSON: ", rapidjson::GetParseError_En(errorCode));

        return false;
    }

    bool isObject = doc.IsObject();
    auto jsonObject = doc.GetObj();


    //Albums
    int iAlbumCount = 0;
    for (auto itr = jsonObject.begin(); itr != jsonObject.end(); itr++)
    {
        TrackInfoList trackList;
        std::wstring albumName = CommonUtils::utf8ToWstring(itr->name.GetString());
        auto mediaTrackList = itr->value.GetArray();
        

        auto albumLogStr = std::format(L"Album [{}]: {}", ++iAlbumCount, albumName);
        //std::cout << albumLogStr << std::endl;
        std::wcout << albumLogStr << '\r';


        for (SizeType i = 0; i < mediaTrackList.Size(); i++)
        {
            FFprobeOutput mi = MediaTrack::ParseMediaTrack(mediaTrackList[i]);
            trackList.push_back({ mi.format.filename, CommonUtils::stringToUintmax(mi.format.size.value_or("0")), mi, L"{}" });
        }

        if (trackList.size() > 0)
        {
            //_AlbumList.push_back({ entry, trackList });
            std::filesystem::directory_entry entry{ albumName };
            _AlbumList.push_back({ entry, trackList });
        }

    }

    return true;
}







//-------------COMPARE


void AlbumCollection::SortByNumberOfTracks(bool ascending)
{
    std::ranges::stable_sort(_AlbumList, SortByTracks<SortOrder::Ascending>{});
}




SimilarDirectoryEntryList AlbumCollection::FindDuplicatedAlbums() {
    auto logger = spdlog::get("console");
    if (!logger) logger = spdlog::stdout_color_mt("console");

    SimilarDirectoryEntryList duplicatedAlbumList;
    if (_AlbumList.size() < 2) {
        logger->info("Album list too small: {}", _AlbumList.size());
        return duplicatedAlbumList;
    }

    auto appSettingPtr = AppSettingsJson::AppSetting();
    size_t minMatchingTracks = appSettingPtr->MinMatchingTracksForDuplicate;

    // Group by track count
    std::map<size_t, std::vector<DirectoryContentEntryList::const_iterator>> trackCountGroups;
    for (auto it = _AlbumList.begin(); it != _AlbumList.end(); ++it) {
        auto tracks = (*it).trackList.size();
        if (tracks >= minMatchingTracks) {
            trackCountGroups[tracks].push_back(it);
        }
    }

    // Process groups
    for (const auto& [trackCount, group] : trackCountGroups) {
        if (group.size() < 2) {
            logger->debug("Skipping group with {} albums ({} tracks)", group.size(), trackCount);
            continue;
        }
        logger->debug("Checking {} albums with {} tracks", group.size(), trackCount);
        auto dupAlbums = FindDuplicationInGroup(group);
        duplicatedAlbumList.insert(duplicatedAlbumList.end(), dupAlbums.begin(), dupAlbums.end());
    }

    logger->info("Total duplicate groups: {}", duplicatedAlbumList.size());
    return duplicatedAlbumList;
}



SimilarDirectoryEntryList AlbumCollection::FindDuplicationInGroup(const std::vector<DirectoryContentEntryList::const_iterator>& group) {
    auto logger = spdlog::get("console");
    if (!logger) {
        logger = spdlog::stdout_color_mt("console");
    }

    SimilarDirectoryEntryList duplicatedAlbumList;

    if (group.size() < 2) {
        logger->debug("Group too small for duplicates: {} albums", group.size());
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
    std::map<AlbumKey, std::vector<std::pair<fs::directory_entry, std::vector<double>>>> albumGroups;

    for (const auto& it : group) {
        if (it == _AlbumList.end()) {
            logger->warn("Invalid iterator in group");
            continue;
        }
        const auto& [dirEntry, trackList] = *it;
        if (trackList.empty()) {
            logger->warn("Empty track list for album at iterator index {}", std::distance(_AlbumList.cbegin(), it));
            continue;
        }

        // Extract album name with robust error handling
        std::string albumName = "unknown_album_" + std::to_string(std::distance(_AlbumList.cbegin(), it));
        try {
            if (fs::exists(dirEntry.path()) && dirEntry.is_directory()) {
                // Use wstring to avoid UTF-8 conversion issues
                std::wstring wName = dirEntry.path().filename().wstring();
                albumName = PlatformUtils::wstringToUtf8_ver2(wName); // Use your safe conversion
                // Sanitize
                bool invalid = false;
                for (char c : albumName) {
                    if (static_cast<unsigned char>(c) < 0x20) {
                        invalid = true;
                        break;
                    }
                }
                if (invalid) {
                    logger->warn("Invalid characters in album name: {}", dirEntry.path().string());
                    albumName = "sanitized_album_" + std::to_string(std::distance(_AlbumList.cbegin(), it));
                }
            }
            else {
                logger->warn("Non-existent or invalid directory: {}", dirEntry.path().string());
            }
        }
        catch (const fs::filesystem_error& e) {
            logger->error("Filesystem error accessing path: {} ({})", dirEntry.path().string(), e.what());
        }
        catch (const std::exception& e) {
            logger->error("Unexpected error accessing filename: {} ({})", dirEntry.path().string(), e.what());
        }
        catch (...) {
            logger->error("Unknown error accessing filename for iterator index {}", std::distance(_AlbumList.cbegin(), it));
        }

        // Extract metadata from first track
        std::string artist;
        const auto& [trackName, size, mediaInfo, mediaInfoString, lastError] = trackList[0];
        if (mediaInfo.format.tags && mediaInfo.format.tags->contains("album")) {
            albumName = mediaInfo.format.tags->at("album");
            if (mediaInfo.format.tags->contains("artist")) {
                artist = mediaInfo.format.tags->at("artist");
            }
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
                    albumName = PlatformUtils::wstringToUtf8_ver2(dirEntry.path().filename().wstring());
                }
                catch (...) {
                    albumName = "sanitized_album_" + std::to_string(std::distance(_AlbumList.cbegin(), it));
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
            logger->debug("Metadata error for track: {} ({})", (dirEntry.path() / trackName).string(), lastError.value());
        }

        // Collect sorted track durations
        std::vector<double> durations;
        durations.reserve(trackList.size());
        for (const auto& [tName, tSize, tMediaInfo, tJson, tError] : trackList) {
            durations.push_back(tMediaInfo.format.duration.value_or(0.0));
        }
        std::ranges::sort(durations);

        albumGroups[{albumName, artist}].emplace_back(dirEntry, std::move(durations));
    }

    // Find duplicates by comparing durations
    for (const auto& [key, albums] : albumGroups) {
        if (albums.size() < 2) continue;

        for (size_t i = 0; i < albums.size(); ++i) {
            for (size_t j = i + 1; j < albums.size(); ++j) {
                const auto& [dir1, durations1] = albums[i];
                const auto& [dir2, durations2] = albums[j];

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
                    logger->info("Duplicate albums: {} and {} (album: {}, artist: {})",
                        dir1.path().string(), dir2.path().string(),
                        key.album.empty() ? "(unknown)" : key.album,
                        key.artist.empty() ? "(unknown)" : key.artist);
                    duplicatedAlbumList.push_back({ dir1.path().wstring(), dir2.path().wstring() });
                }
            }
        }
    }

    logger->info("Found {} duplicate groups in group of {} albums", duplicatedAlbumList.size(), group.size());
    return duplicatedAlbumList;
}




SimilarDirectoryEntryList AlbumCollection::FindDuplicatedAlbums2()
{
    SimilarDirectoryEntryList duplicatedAlbumList;

    if (_AlbumList.size() < 2)
    {
        return duplicatedAlbumList;
    }

    auto firstIt = _AlbumList.begin();
    auto secondIt = firstIt;
    secondIt++;

    auto appSettingPtr = AppSettingsJson::AppSetting();
    auto minMatchingTracksForDuplicate = appSettingPtr->MinMatchingTracksForDuplicate;
    while (firstIt != _AlbumList.end() && secondIt != _AlbumList.end())
    {
        bool bFound = false;
        auto& [dirEntry1, fileList1] = *firstIt;
        auto& [dirEntry2, fileList2] = *secondIt;

        auto pushedEndGroupIt = secondIt;
        int itemsInGroup{ 0 };
        auto fileList1Seize{ fileList1.size() };
        while (secondIt != _AlbumList.end() && fileList1.size() == fileList2.size() && fileList1.size() >= minMatchingTracksForDuplicate)
        {
            pushedEndGroupIt = secondIt;
            auto& [dirEntry2, fileList2] = *secondIt;

            secondIt++;
            bFound = true;
            itemsInGroup++;
        }

        secondIt = pushedEndGroupIt;

        auto firstIndex = std::ranges::distance(_AlbumList.cbegin(), firstIt);
        auto lastIndex = std::ranges::distance(_AlbumList.cbegin(), secondIt);

        if (bFound)
        {
            auto dupAlbums = FindDuplicationInGroup2(_AlbumList, firstIt, secondIt);
            duplicatedAlbumList.insert(duplicatedAlbumList.end(), dupAlbums.begin(), dupAlbums.end());
            firstIt = secondIt;;
            secondIt++;
        }
        else
        {
            firstIt++;
            secondIt++;
        }
    }

    return duplicatedAlbumList;
}

SimilarDirectoryEntryList AlbumCollection::FindDuplicationInGroup2(DirectoryContentEntryList& albumList, DirectoryContentEntryList::iterator firstIt, DirectoryContentEntryList::iterator lastIt)
{
    auto appSettingPtr = AppSettingsJson::AppSetting();
    auto sizeMatchPercentageThreshold = appSettingPtr->SizeMatchPercentageThreshold;

    SimilarDirectoryEntryList duplicatedAlbumList;

    if (firstIt != lastIt && firstIt != albumList.end() && lastIt != albumList.end())
    {
        auto currentIt = firstIt;
        while (currentIt != lastIt)
        {
            auto currentIt2 = currentIt;
            while (currentIt2 != lastIt)
            {
                currentIt2++;

                auto& [albumName1, trackList1] = *currentIt;
                auto& [albumName2, trackList2] = *currentIt2;

                if (trackList1.size() == trackList2.size())
                {
                    bool bPotentialSimilar = true;
                    for (int i = 0; i < trackList1.size(); i++)
                    {
                        auto& [trackName1, size1, mediaInfo1, mediaInfoString1, lastError1] = trackList1[i];
                        auto& [trackName2, size2, mediaInfo2, mediaInfoString2, lastError2] = trackList2[i];

                        if (mediaInfo1.format.duration.has_value() && mediaInfo2.format.duration.has_value())
                        {
                            auto minSize = (std::min)(mediaInfo1.format.duration.value(), mediaInfo2.format.duration.value());
                            auto maxSize = (std::max)(mediaInfo1.format.duration.value(), mediaInfo2.format.duration.value());

                            double diffPercentage = 100 * (maxSize - minSize) / maxSize;

                            if (diffPercentage > sizeMatchPercentageThreshold)
                            {
                                bPotentialSimilar = false;
                            }
                        }
                        else
                        {
                            if (!mediaInfo1.format.duration.has_value() && !mediaInfo2.format.duration.has_value())
                            {
                                bPotentialSimilar = false;
                            }
                        }
                    }

                    if (bPotentialSimilar)
                    {
                        duplicatedAlbumList.push_back({ albumName1.path().generic_wstring(), albumName2.path().generic_wstring() });
                    }
                }
            }
            currentIt++;
        }
    }

    return duplicatedAlbumList;
}




//--------------------DB



#include "SQLite/sqlite-amalgamation/sqlite3.h"

bool AlbumCollection::SaveToSQLDatabase(std::filesystem::path path)
{
    const std::string dbPath{ path.generic_string() };

    sqlite3* db;
    int rc = sqlite3_open(dbPath.c_str(), &db);

    if (rc != SQLITE_OK) {
        spdlog::error("Cannot open database: ", sqlite3_errmsg(db));
        return rc;
    }

    // Execute SQL statements

    rc = sqlite3_exec(db, "DROP TABLE IF EXISTS TracksDB;", 0, 0, 0);

    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS TracksDB (
            ID INTEGER PRIMARY KEY,
            album_path TEXT NOT NULL,
            nb_streams INTEGER,
            nb_programs INTEGER,
            nb_stream_groups INTEGER,
            format_name TEXT,
            format_long_name TEXT,
            start_time INTEGER,
            duration REAL,
            size INTEGER,
            bit_rate INTEGER,
            probe_score INTEGER,
            album TEXT,
            artist TEXT,
            album_artist TEXT,
            genre TEXT,
            disc TEXT,
            title TEXT,
            track TEXT,
            track_total TEXT,
            date TEXT,
            comment TEXT,
            publisher TEXT,
            encoder TEXT,
            encoded_by TEXT,
            organization TEXT,
            composer TEXT,
            copyright TEXT,
            album_dynamic_range TEXT,
            dynamic_range TEXT,
            label TEXT,
            year TEXT,
            stream1_index INTEGER,
            stream1_codec_name TEXT,
            stream1_codec_type TEXT,
            stream1_sample_rate TEXT,
            stream1_channels INTEGER,
            stream1_channel_layout TEXT,
            stream1_bit_rate INTEGER,
            stream1_frame_size INTEGER,
            stream1_duration REAL,
            stream1_start_time INTEGER,
            stream1_tag1 TEXT,
            stream2_index INTEGER,
            stream2_codec_name TEXT,
            stream2_codec_type TEXT,
            stream2_sample_rate TEXT,
            stream2_channels INTEGER,
            stream2_channel_layout TEXT,
            stream2_bit_rate INTEGER,
            stream2_frame_size INTEGER,
            stream2_duration REAL,
            stream2_start_time INTEGER,
            stream2_tag1 TEXT
        )
    )";

    rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("Failed to create TracksDB table: ", sqlite3_errmsg(db));
        return rc;
    }
  

    //constexpr int NUM_FIELDS = 47;
    //static_assert(sizeof("?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,") - 1 == NUM_FIELDS, "Placeholder count mismatch");


    if (rc != SQLITE_OK) {
        spdlog::error("Cannot create table: ", sqlite3_errmsg(db));
        sqlite3_close(db);
        return rc;
    }


    for (auto [dirPath, trackList] : _AlbumList)
    {
        for (auto& [trackName, size, mediaInfo, mediaInfoString, lastError] : trackList)
        {
            std::wstring albumPath = dirPath.path().wstring();

            std::optional<Stream> stream1 = mediaInfo.streams.size() > 0 ? std::optional<Stream>{ mediaInfo.streams[0] } : std::nullopt;
            std::optional<std::string> stream1Tag1;
			//if (stream1.has_value() && stream1->tags.has_value())
			//{
			//	auto tags = stream1->tags.value();
			//	//if (tags.HasMember("language"))
			//	//{
			//	//	stream1Tag2 = tags["language"].GetString();
			//	//}


			//	auto tags2222  = tags[0];
   //             stream1Tag1 = std::optional<std::string>{ tags[0] };
			//}

            std::optional<Stream> stream2 = mediaInfo.streams.size() > 1 ? std::optional<Stream>{ mediaInfo.streams[1] } : std::nullopt;
            std::optional<std::string> stream2Tag1;
            //if (stream2.has_value() && stream2->tags.has_value())
            //{
            //    stream2Tag1 = std::optional<std::string>{ stream2->tags.value()[0] };
            //}

            const char* sql = R"(
        INSERT OR REPLACE INTO TracksDB (
            id, album_path, nb_streams, nb_programs, nb_stream_groups, format_name, format_long_name,
            start_time, duration, size, bit_rate, probe_score,
            album, artist, album_artist, genre, disc, title, track, track_total, date, comment,
            publisher, encoder, encoded_by, organization, composer, copyright,
            album_dynamic_range, dynamic_range, label, year,
            stream1_index, stream1_codec_name, stream1_codec_type, stream1_sample_rate, stream1_channels, stream1_channel_layout, stream1_bit_rate, stream1_frame_size, stream1_duration, stream1_start_time, stream1_tag1,
            stream2_index, stream2_codec_name, stream2_codec_type, stream2_sample_rate, stream2_channels, stream2_channel_layout, stream2_bit_rate, stream2_frame_size, stream2_duration, stream2_start_time, stream2_tag1
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

            sqlite3_stmt* stmt;
            int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
            if (rc != SQLITE_OK) {
                std::cout << "Prepare failed: " << sqlite3_errmsg(db) << "\n";
                sqlite3_close(db);
                return rc;
            }

            // Bind parameters (1-based indexing)
            int bindIndex = 1;
            sqlite3_bind_null(stmt, bindIndex++); // id (auto-incremented)
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(albumPath).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, bindIndex++, mediaInfo.format.nb_streams);
            sqlite3_bind_int(stmt, bindIndex++, mediaInfo.format.nb_programs);
            sqlite3_bind_int(stmt, bindIndex++, mediaInfo.format.nb_stream_groups);
            sqlite3_bind_text(stmt, bindIndex++, mediaInfo.format.format_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, mediaInfo.format.format_long_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, bindIndex++, mediaInfo.format.start_time.value_or(0));
            sqlite3_bind_double(stmt, bindIndex++, mediaInfo.format.duration.value_or(0.0));
            sqlite3_bind_text(stmt, bindIndex++, mediaInfo.format.size.value_or("").c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, bindIndex++, mediaInfo.format.bit_rate.value_or(0));
            sqlite3_bind_int(stmt, bindIndex++, mediaInfo.format.probe_score);

            // Bind tags
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.album).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.artist).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.album_artist).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.genre).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.disc).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.title).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.track).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.track_total).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.date).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.comment).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.publisher).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.encoder).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.encoded_by).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.organization).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.composer).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.copyright).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.album_dynamic_range).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.dynamic_range).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.label).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, PlatformUtils::wstringToUtf8_ver2(mediaInfo.format_tags.year).c_str(), -1, SQLITE_TRANSIENT);

            //stream
            if (stream1.has_value())
            {
                sqlite3_bind_int(stmt, bindIndex++, stream1.value().index);
                sqlite3_bind_text(stmt, bindIndex++, stream1.value().codec_name.value_or("").c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, bindIndex++, stream1.value().codec_type.value_or("").c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, bindIndex++, stream1.value().sample_rate.value_or("").c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, bindIndex++, stream1.value().channels.value_or(0));
                sqlite3_bind_text(stmt, bindIndex++, stream1.value().channel_layout.value_or("").c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, bindIndex++, stream1.value().bit_rate.value_or(0));
                sqlite3_bind_int(stmt, bindIndex++, stream1.value().frame_size.value_or(0));
                sqlite3_bind_text(stmt, bindIndex++, stream1.value().duration.value_or("").c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, bindIndex++, stream1.value().start_time.value_or(0));
                sqlite3_bind_text(stmt, bindIndex++, stream1Tag1.value_or("").c_str(), -1, SQLITE_TRANSIENT);
            }
            if (stream2.has_value())
            {
                sqlite3_bind_int(stmt, bindIndex++, stream2.value().index);
                sqlite3_bind_text(stmt, bindIndex++, stream2.value().codec_name.value_or("").c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, bindIndex++, stream2.value().codec_type.value_or("").c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, bindIndex++, stream2.value().sample_rate.value_or("").c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, bindIndex++, stream2.value().channels.value_or(0));
                sqlite3_bind_text(stmt, bindIndex++, stream2.value().channel_layout.value_or("").c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, bindIndex++, stream2.value().bit_rate.value_or(0));
                sqlite3_bind_int(stmt, bindIndex++, stream2.value().frame_size.value_or(0));
                sqlite3_bind_text(stmt, bindIndex++, stream2.value().duration.value_or("").c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, bindIndex++, stream2.value().start_time.value_or(0));
                sqlite3_bind_text(stmt, bindIndex++, stream2Tag1.value_or("").c_str(), -1, SQLITE_TRANSIENT);
            }

            // Execute the statement
            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                spdlog::error("***ERROR - Database error: ", sqlite3_errmsg(db));
                sqlite3_close(db);
                return false;
            }

            // Clean up
            sqlite3_finalize(stmt);
        }
    }


    if (rc != SQLITE_OK) {
        spdlog::error("Cannot insert data: ", sqlite3_errmsg(db));
        sqlite3_close(db);
        return rc;
    }


    sqlite3_close(db);

    return true;
}


