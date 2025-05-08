



#include <filesystem>
#include <vector>
#include <string>
#include <execution>
#include <mutex>
#include <iostream>
#include <map>
#include <algorithm>
#include <ranges>
#include <format>
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

#include "SQLite/sqlite-amalgamation/sqlite3.h"


namespace fs = std::filesystem;
using namespace rapidjson;




CommonUtils::Generator<MediaAlbumListPtr> AlbumCollection::LoadAlbumsCo(std::filesystem::path albumCollectionDirPath, bool bIncludeMetadata, size_t batchSize) {

    std::chrono::steady_clock::time_point startTimePoint = std::chrono::steady_clock::now();
    //std::chrono::steady_clock::time_point endLoadTime = startLoadTime;
    spdlog::info("===Loading album collection from storage===");

    // Map to collect tracks per folder
    std::map<fs::path, TrackInfoList> albumMap;

    spdlog::info("First pass: Find all folders, Please wait (might take a few minutes)...");

    long long folderCount = 0;
    // First pass: Find all folders - Discove folders
    for (const auto& entry : fs::recursive_directory_iterator(
        albumCollectionDirPath, fs::directory_options::skip_permission_denied)) {
        // Progress update
        std::cout << "\rScanning folders, Please wait... " << ++folderCount;

        auto relativePath = fs::relative(entry.path(), albumCollectionDirPath);
        auto depth = std::distance(relativePath.begin(), relativePath.end());
        if (depth > AppSettingsJson::AppSetting()->RecursionDirectorySearchDepth) {
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
        for (const auto& entry : fs::directory_iterator(
            folderPath, fs::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file() && MediaTrack::IsFileAcceptedAudioFile(entry)) {
                uintmax_t fileSize = fs::file_size(entry.path());
                trackList.push_back({ entry.path().filename().wstring(), fileSize, FFprobeOutput{}, std::nullopt, std::nullopt });
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
                    ImportMetadata(albumListPtr, albumIndex, albumMap.size(), AppSettingsJson::AppSetting()->UseAsyncFFmpegCalls);
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
            ImportMetadata(albumListPtr, albumIndex, albumMap.size(), AppSettingsJson::AppSetting()->UseAsyncFFmpegCalls);
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

                        trackList.push_back({ entry.path().filename().wstring(), fileSize, FFprobeOutput{}, std::nullopt, std::nullopt });
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

        auto nAlbums = ImportMetadata(albumListPtr, 0, 0, AppSettingsJson::AppSetting()->UseAsyncFFmpegCalls);

        spdlog::info("Third pass: Completed, ffmpeg issues: {} [{}]", FFmpeg::get_ffmpeg_logs().size(), CommonUtils::GetDurationinString(startTimePoint, std::chrono::steady_clock::now()));
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
        std::cerr << "Error iterating " << rootPath.string() << ": " << e.what() << '\n';
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
            std::cerr << "Error accessing " << path.string() << ": " << e.what() << '\n';
        }
        });

    return { contents.folders.size(), contents.files.size() };
}




//currently I have a list of async objects that fills up when I scan each album, at the end of each album, I wait for the completion of all tracks, I want something similar but at the Album list level.in another words I want to improve performance by keeping the track processing queue lenght to N(pre defince number)

//Load all media media information from the preloaded album list (albumList)
size_t AlbumCollection::ImportMetadata(std::shared_ptr<DirectoryContentEntryList> albumListPtr, size_t currentCount, size_t totalCount, bool bAsync)
{
	if (albumListPtr == nullptr || albumListPtr->empty())
	{
		return 0;
	}

    size_t albumCount = 0;
       
    for (auto& [albumPath, trackList] : *albumListPtr)
    {
        std::string name = "N/A";
        if (albumPath.is_directory() && albumPath.path().has_filename())
        {
            name = CommonUtils::utf8string_to_string(albumPath.path().filename().generic_u8string());
        }
        else
        {
			spdlog::error("Error: Album path is not a directory or does not have a filename. " + CommonUtils::utf8string_to_string(albumPath.path().generic_u8string()));
        }
        std::shared_ptr<CommonUtils::ProgressBarInfo> subProgressInfoPtr{ nullptr };
        if (AppSettingsJson::AppSetting()->AlbumsSplitThreshold < totalCount)
        {
            subProgressInfoPtr = std::make_shared<CommonUtils::ProgressBarInfo>("Processing...", albumCount++, albumListPtr->size(), name, 20);
        }
        auto progressInfoPtr = std::make_shared<CommonUtils::ProgressBarInfo>("Processing...", currentCount++, totalCount, name, 20, subProgressInfoPtr);
        CommonUtils::show_progress_bar(progressInfoPtr, CommonUtils::ProgressBarType::Progress);

        //Album tracks list holder 
        std::vector<std::tuple<MediaLoadingFuture, FFprobeOutput&, std::optional<std::wstring>>> asyncFutureList;

        for (auto& [trackName, size, mediaInfo, mediaInfoString, lastError] : trackList)
        {
            std::filesystem::path trackPath = albumPath.path() / std::filesystem::path(trackName);

            if (MediaTrack::IsValidMedia(trackPath)) {
                auto path2Fixed = trackPath.lexically_normal().native();

                if (bAsync)
                {
                    auto miFuture = std::async(std::launch::async, MediaTrack::ReadMediaInfoFromJsonFile, path2Fixed);
                    asyncFutureList.push_back({ std::move(miFuture), mediaInfo, mediaInfoString });

                }
                else
                {
                    auto [mi_ret, jsonString_ret] = MediaTrack::ReadMediaInfoFromJsonFile(path2Fixed);
                    mediaInfoString = jsonString_ret;
                    mediaInfo = mi_ret;
                    CommonUtils::show_progress_bar(progressInfoPtr, CommonUtils::ProgressBarType::SubProgress);
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
    
    auto subProgressInfoPtr = std::make_shared<CommonUtils::ProgressBarInfo>("Processing...", albumCount, albumListPtr->size(), "Completed.", 20);
    auto progressInfoPtr = std::make_shared<CommonUtils::ProgressBarInfo>("Processing batch...", currentCount, totalCount, "Batch Completed.", 20, subProgressInfoPtr);
    CommonUtils::show_progress_bar(progressInfoPtr, CommonUtils::ProgressBarType::Complete);

    return albumCount;
}

void SaveAlbumsToJSON_FFmpegError(std::filesystem::path outPath)
{
    std::vector<FFmpeg::FFmpegLogItem>& ffmpegErrorList = FFmpeg::get_ffmpeg_logs();

    if (ffmpegErrorList.empty())
    {
        return;
    }

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

	ffmpegErrorList.clear();

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

 //   std::cout << "JSON file created successfully as tracks.json" << std::endl;
}

std::list<MediaTrack> SaveAlbumsToJSON_LasrErrorList;

void SaveAlbumsToJSON_LasrError(std::list<MediaTrack>& mediaTracks, std::filesystem::path outPath)
{
    if (SaveAlbumsToJSON_LasrErrorList.empty()) {
		return;
    }

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

	SaveAlbumsToJSON_LasrErrorList.clear();

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

 //   std::cout << "JSON file created successfully as tracks.json" << std::endl;
}



bool AlbumCollection::SaveAlbumsToJSON(std::shared_ptr<DirectoryContentEntryList> albumListPtr, std::filesystem::path path)
{
    rapidjson::Document mediaDoc;
    mediaDoc.SetArray(); // Top-level array for albums
    rapidjson::Document::AllocatorType& allocator = mediaDoc.GetAllocator();

    for (auto [albumPath, trackList] : *albumListPtr)
    {
        // Create album object
        rapidjson::Value albumObj(rapidjson::kObjectType);

        // Add AlbumName
        std::string albumPathString = CommonUtils::utf8string_to_string(albumPath.path().u8string());
        std::string albumFolderString = CommonUtils::utf8string_to_string(albumPath.path().filename().u8string());
        
        rapidjson::Value albumPathStringVal;
        albumPathStringVal.SetString(albumPathString.c_str(), static_cast<SizeType>(albumPathString.size()), allocator);
        albumObj.AddMember("Album Path", albumPathStringVal, allocator);

        rapidjson::Value albumFolderStringVal;
        albumFolderStringVal.SetString(albumFolderString.c_str(), static_cast<SizeType>(albumFolderString.size()), allocator);
        albumObj.AddMember("Folder", albumFolderStringVal, allocator);

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

                std::string jsonString{};
				if (mediaInfoString.has_value())
				{
                    jsonString = PlatformUtils::wstringToUtf8_ver2(mediaInfoString.value());
				}
				else
				{
					jsonString = MediaTrack::toJsonString(item.formatInfo);
				}

              //  std::string utf8Json = PlatformUtils::wstringToUtf8_ver2(mediaInfoString);
                trackDoc.Parse(jsonString.c_str());
                if (trackDoc.HasParseError()) {
                    // Get error code and message
                    rapidjson::ParseErrorCode errorCode = trackDoc.GetParseError();
                    const char* errorMsg = rapidjson::GetParseError_En(errorCode);
                    size_t errorOffset = trackDoc.GetErrorOffset();
                    item.LastErroString = errorMsg;
                    SaveAlbumsToJSON_LasrErrorList.push_back(item);
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
            // Add Tracks to album
            albumObj.AddMember("Tracks", trackMediaArray, allocator);

            // Add album object to top-level array (no key)
            mediaDoc.PushBack(albumObj, allocator);
        }
    }

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


    fs::path parent = path.parent_path();
    if (!parent.empty() && !fs::exists(parent)) {
        // 2. Create all levels of directories
        std::error_code ec;
        if (!fs::create_directories(parent, ec)) {
            std::cerr << "Failed to create directories: " << ec.message() << "\n";
            return false;
        }
    }

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


    SaveAlbumsToJSON_LasrError(SaveAlbumsToJSON_LasrErrorList, path);
    SaveAlbumsToJSON_FFmpegError(path);


    return true;
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

    //Albums
    int iAlbumCount = 0;
    spdlog::info("");
	auto albumListSize = doc.GetArray().Capacity();
    for (const auto& albumVal : doc.GetArray()) {
        TrackInfoList trackList;

        // Validate that albumVal is an object with required fields
        if (!albumVal.IsObject()) {
            spdlog::error("Album entry {} is not an object", iAlbumCount);
            continue;
        }
        if (!albumVal.HasMember("Album Path") || !albumVal["Album Path"].IsString()) {
            spdlog::error("Album entry {} missing or invalid Album Path", iAlbumCount);
            continue;
        }
        if (!albumVal.HasMember("Folder") || !albumVal["Folder"].IsString()) {
            spdlog::error("Album entry {} missing or invalid Folder", iAlbumCount);
            continue;
        }
        if (!albumVal.HasMember("Tracks") || !albumVal["Tracks"].IsArray()) {
            spdlog::error("Album entry {} missing or invalid Tracks array", iAlbumCount);
            continue;
        }

        std::string albumPathStr = albumVal["Album Path"].GetString();
        std::wstring albumpath = CommonUtils::utf8ToWstring(albumPathStr);

        std::string albumFolderStr = albumVal["Folder"].GetString();
        std::wstring albumFolder = CommonUtils::utf8ToWstring(albumFolderStr);


        // Updated logging to reflect new album name source
        std::cout << "\33[2K\r";  // Clear entire line and move cursor to start
        std::cout << "\33[A\33[2K\r";  // Move up 1 line, clear it, and return to start
        //auto albumLogStr = std::format(L"Album [{}]: {}", ++iAlbumCount, albumpath);
        spdlog::info("Loading albums... Album {}/{}", ++iAlbumCount, albumListSize);


        const auto& mediaTrackList = albumVal["Tracks"].GetArray();
        for (SizeType i = 0; i < mediaTrackList.Size(); i++) {
            FFprobeOutput mi = MediaTrack::ParseFFprobeInformation(mediaTrackList[i]);
            //Derive track filename from FFprobeOutput or use fallback
            std::wstring trackFilename = mi.format.filename.empty() ?
                L"track_" + std::to_wstring(i) :
                std::filesystem::path(CommonUtils::utf8ToWstring(mi.format.filename)).filename().wstring();
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
            albumListPtr->push_back({ entry, trackList });
        }

    }

    return albumListPtr;
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

bool CompareTags(std::vector<MediaTrack>& tList1, std::vector<MediaTrack>& tList2, const std::string tagName)
{
    if (!(tList1.size() > 0 && tList2.size() > 0))
    {
        return false;
    }

    //if (tList1.size() != tList2.size())
    //{
    //    return false;
    //}

    auto& tags1 = tList1[0].formatInfo.format.tags;
    auto& tags2 = tList2[0].formatInfo.format.tags;

    if (!tags1.has_value() || !tags2.has_value())
    {
        return false;
    }

    const std::string* val1 = JsonUtils::getValueByKey(*tags1, tagName);
    const std::string* val2 = JsonUtils::getValueByKey(*tags2, tagName);

    if (val1 == nullptr || val2 == nullptr)
    {
        return false;
    }

    return compareLex(*val1, *val2) == 0;



    if (tList1.size() != tList2.size())
    {
        return false;
    }

    for (int i = 0; i < tList1.size(); i++)
    {
        auto& tags1 = tList1[i].formatInfo.format.tags;
        auto& tags2 = tList2[i].formatInfo.format.tags;

        if (!tags1.has_value() || !tags2.has_value())
        {
            return false;
        }
        
        const std::string* val1 = JsonUtils::getValueByKey(*tags1, tagName);
        const std::string* val2 = JsonUtils::getValueByKey(*tags2, tagName);

        if (val1 == nullptr || val2 == nullptr)
        {
            return false;
        }

        if (*val1 != *val2)
        {
            return false;
        }
    }


    return true;
}


void AlbumCollection::SortAlbums(std::shared_ptr<std::vector<MediaAlbum>> albumListPtr,
                                 const std::vector<std::pair<SortBy, bool>>& criteria)
{
    std::ranges::stable_sort(*albumListPtr, [&](auto& a, auto& b) {
        const auto& [dirEntryA, tracksA] = a;
        const auto& [dirEntryB, tracksB] = b;

        int result = 0;
        for (const auto& [criterion, ascending] : criteria) {
            switch (criterion) {
            case SortBy::AlbumName: {
                const auto& nameA = a.path.path().filename().wstring();
                const auto& nameB = b.path.path().filename().wstring();
                result = nameA.compare(nameB);
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
                result = static_cast<int>(a.trackList.size()) - static_cast<int>(b.trackList.size());
                break;

            default:
                break;
            }

//            return result;
//            if (result != 0)
//                return (result < 0) == ascending;
        }

        return result; // Considered equal for all criteria
        });
}

//void AlbumCollection::SortAlbums(std::shared_ptr<DirectoryContentEntryList> albumListPtr, bool ascending)
//{
//    std::ranges::stable_sort(*albumListPtr, SortByTracks<SortOrder::Ascending>{});
//}




SimilarDirectoryEntryList AlbumCollection::FindDuplicateAlbums(std::shared_ptr<DirectoryContentEntryList> albumListPtr) {
    auto logger = spdlog::get("console");
    if (!logger) logger = spdlog::stdout_color_mt("console");

    SimilarDirectoryEntryList duplicatedAlbumList;
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
            logger->debug("Skipping group with {} albums ({} tracks)", group.size(), trackCount);
            continue;
        }
        logger->debug("Checking {} albums with {} tracks", group.size(), trackCount);
        auto dupAlbums = FindDuplicationInGroup(albumListPtr, group);
        duplicatedAlbumList.insert(duplicatedAlbumList.end(), dupAlbums.begin(), dupAlbums.end());
    }

    logger->info("Total duplicate groups: {}", duplicatedAlbumList.size());
    return duplicatedAlbumList;
}



SimilarDirectoryEntryList AlbumCollection::FindDuplicationInGroup(std::shared_ptr<DirectoryContentEntryList> albumListPtr, const std::vector<DirectoryContentEntryList::const_iterator>& group) {
    //auto logger = spdlog::get("console");
    //if (!logger) {
    //    logger = spdlog::stdout_color_mt("console");
    //}

    SimilarDirectoryEntryList duplicatedAlbumList;

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
    std::map<AlbumKey, std::vector<std::pair<fs::directory_entry, std::vector<double>>>> albumGroups;

    for (const auto& it : group) {
        if (it == albumListPtr->end()) {
            spdlog::warn("Invalid iterator in group");
            continue;
        }
        const auto& [dirEntry, trackList] = *it;
        if (trackList.empty()) {
            spdlog::warn("Empty track list for album at iterator index {}", std::distance(albumListPtr->cbegin(), it));
            continue;
        }

        // Extract album name with robust error handling
        std::string albumName = "unknown_album_" + std::to_string(std::distance(albumListPtr->cbegin(), it));
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
                    spdlog::warn("Invalid characters in album name: {}", dirEntry.path().string());
                    albumName = "sanitized_album_" + std::to_string(std::distance(albumListPtr->cbegin(), it));
                }
            }
            else {
                spdlog::warn("Non-existent or invalid directory: {}", dirEntry.path().string());
            }
        }
        catch (const fs::filesystem_error& e) {
            spdlog::error("Filesystem error accessing path: {} ({})", dirEntry.path().string(), e.what());
        }
        catch (const std::exception& e) {
            spdlog::error("Unexpected error accessing filename: {} ({})", dirEntry.path().string(), e.what());
        }
        catch (...) {
            spdlog::error("Unknown error accessing filename for iterator index {}", std::distance(albumListPtr->cbegin(), it));
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
            spdlog::debug("Metadata error for track: {} ({})", (dirEntry.path() / trackName).string(), lastError.value());
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
                    spdlog::info("Duplicate albums: {} and {} (album: {}, artist: {})",
                        CommonUtils::utf8string_to_string(dir1.path().u8string()), 
                        CommonUtils::utf8string_to_string(dir2.path().u8string()),
                        key.album.empty() ? "(unknown)" : key.album,
                        key.artist.empty() ? "(unknown)" : key.artist);

                    duplicatedAlbumList.push_back({ dir1.path().wstring(), dir2.path().wstring() });
                }
            }
        }
    }

    spdlog::info("Found {} duplicate groups in group of {} albums", duplicatedAlbumList.size(), group.size());

    return duplicatedAlbumList;
}


bool AlbumCollection::ExportToDatabase(std::shared_ptr<DirectoryContentEntryList> albums, std::filesystem::path databasePath)
{
    sqlite3* db = nullptr;
    int rc = sqlite3_open(databasePath.generic_string().c_str(), &db);
    if (rc != SQLITE_OK) {
        spdlog::error("Cannot open database: {}", sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }

    rc = sqlite3_exec(db, MediaTrack::GetCreateTableSQL().c_str(), nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("Failed to create TracksDB table: {}", sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }

    rc = sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("BEGIN TRANSACTION failed: {}", sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db, MediaTrack::GetInsertSQLStatement().c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("Failed to prepare insert statement: {}", sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }

    for (const auto& [dirPath, trackList] : *albums) {
        std::wstring albumPath = dirPath.path().wstring();
        for (const auto& track : trackList) {
            if (!MediaTrack::ExportToDatabase(stmt, albumPath, track)) {
                sqlite3_finalize(stmt);
                sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
                sqlite3_close(db);
                return false;
            }
        }
    }

    sqlite3_finalize(stmt);
    rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("COMMIT failed: {}", sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    return true;
}

