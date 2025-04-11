
#include <ranges>
#include <algorithm> 
#include <format>

#include "AlbumCollection.h"
#include "FolderConvert.h"
#include "JsonUtils.h"
#include "PlatformUtils.h"
#include "MediaTrack.h"

#include "CommonUtils.h"
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace rapidjson;




//constexpr auto CLEAR_LINE{ L"\x1b[H\x1b[J" };
//std::vector<std::wstring> CommonUtils::ProgressCircleChars = { L"⏳", L"⏰", L"⏱️", L"⏲️", L"⏳", L"⏰", L"⏱️", L"⏲️" };

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
		auto nAlbums = LoadAllMetadata(true); //load media metadate
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
    int progressIndex = 0;
    
   
    for (auto& [albumPath, trackList] : _AlbumList)
    {
        CommonUtils::show_circular_progress("processing...");

//        std::cout << "\r\033[K";
        //std::wcout << std::format(L"{} Processing [{}/{}]: {}", ProgressCircleChars[progressIndex], ++albumCount, _AlbumList.size(), albumPath.path().generic_wstring());
        std::string str{ "???" };
        try
        {
            //str = RemoveSpecialCharacter(albumPath.path().generic_string());
            str = albumPath.path().generic_string();
        }
        catch (...) {
			spdlog::error("Error converting path: {}", PlatformUtils::WideToUTF8(albumPath.path()));
        }

//        std::cout << std::format("{} Processing [{}/{}]: {}", CommonUtils::ProgressCircleChars[progressIndex], ++albumCount, _AlbumList.size(), str);
        progressIndex = (progressIndex + 1) % CommonUtils::ProgressCircleChars.size();

        //Album tracks list holder 
        std::vector<std::tuple<MediaLoadingFuture, FFprobeOutput&, std::wstring&>> asyncFutureList;

        for (auto& [trackName, size, mediaInfo, mediaInfoString] : trackList)
        {
            std::filesystem::path trackPath = albumPath.path() / std::filesystem::path(trackName);

            if (MediaTrack::IsValidMedia(trackPath)) {
                auto path2Fixed = trackPath.lexically_normal().native();

                if (bAsync)
                {
                    auto miFuture = std::async(std::launch::async, MediaTrack::ReadMediaInfoFromFile, path2Fixed);

                    //miFuture.get();
                    asyncFutureList.push_back({ std::move(miFuture), mediaInfo, mediaInfoString });

                    //if (asyncFutureList.size() > 4)
                    //{
                    //    for (auto& [furure_ret, mediaInfo, mediaInfoString] : asyncFutureList)
                    //    {
                    //        auto [mediaInfo_ret, mediaInfoString_ret] = furure_ret.get();
                    //        mediaInfo = mediaInfo_ret;
                    //        mediaInfoString = mediaInfoString_ret;
                    //    }
                    //    asyncFutureList.clear();
                    //}
                }
                else
                {
                    //fs::path outfilePath{ trackName / fs::path("_" + TMP_MEDIA_JSON_FILE_NAME) };
                    auto [mi_ret, jsonString_ret] = MediaTrack::ReadMediaInfoFromFile(path2Fixed);
                    mediaInfoString = jsonString_ret;
                    mediaInfo = mi_ret;
                }
            }
        }

        for (auto& [furure_ret, mediaInfo, mediaInfoString] : asyncFutureList)
        {
            auto [mediaInfo_ret, mediaInfoString_ret] = furure_ret.get();
            mediaInfo = mediaInfo_ret;
            mediaInfoString = mediaInfoString_ret;
        }

      //  std::wcout << "\r\033[K";
    }

	std::cout << std::endl;

    return _AlbumList.size();
}


bool AlbumCollection::SaveAlbumsAsJSON(std::filesystem::path path)
{
    rapidjson::Document mediaDoc;
    mediaDoc.SetObject();

    for (auto [albumPath, trackList] : _AlbumList)
    {
        //Album tracks list holder 
        rapidjson::Value trackMediaArray(rapidjson::kArrayType);

        for (auto [trackName, size, mediaInfo, mediaInfoString] : trackList)
        {
            std::filesystem::path trackPath = albumPath.path() / std::filesystem::path(trackName);

            auto hasExtension = trackPath.has_extension();
            auto fileEextension = trackPath.extension();
            // std::wstring entryPath{ trackPath.wstring() };
            if (trackPath.has_extension() && (fileEextension == ".flac" || fileEextension == ".mp3")) {

                rapidjson::Document trackDoc;

                std::string utf8Json = CommonUtils::wstringToUtf8(mediaInfoString);
                trackDoc.Parse(utf8Json.c_str());
                if (trackDoc.HasParseError()) {
                    std::cerr << "Error parsing JSON: " << trackDoc.GetParseError() << std::endl;                    
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
                    std::string utf8Key = CommonUtils::wstringToUtf8(name);
                    

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


    if (fs::exists(path)) {
        std::error_code ec;
        if (!fs::remove(path, ec)) {
            std::cerr << "Failed to remove existing file: " << ec.message() << "\n";
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
        std::cerr << "Unable to open file for writing" << std::endl;
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
    std::string utf8Json = CommonUtils::wstringToUtf8(json);
    doc.Parse(utf8Json.c_str());

    // Check for parse errors 
    if (doc.HasParseError()) {
        std::cerr << "Error parsing JSON: "
            << doc.GetParseError() << std::endl;

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
    std::ranges::stable_sort(_AlbumList, [ascending](const auto& album1, const auto& album2) {
        const auto& [albumName1, trackList1] = album1;
        const auto& [albumName2, trackList2] = album2;

        return ascending ? trackList2.size() > trackList1.size() : trackList2.size() < trackList1.size();
    });
}



SimilarDirectoryEntryList AlbumCollection::FindDuplicatedAlbums()
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
            auto dupAlbums = FindDuplicationInGroup(_AlbumList, firstIt, secondIt);
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

SimilarDirectoryEntryList AlbumCollection::FindDuplicationInGroup(DirectoryContentEntryList& albumList, DirectoryContentEntryList::iterator firstIt, DirectoryContentEntryList::iterator lastIt)
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
                        auto& [trackName1, size1, mediaInfo1, mediaInfoString2] = trackList1[i];
                        auto& [trackName2, size2, mediaInfo2, mediaInfoString1] = trackList2[i];

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
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return rc;
    }

    // Execute SQL statements

    rc = sqlite3_exec(db, "DROP TABLE IF EXISTS TracksDB;", 0, 0, 0);

    rc = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS TracksDB ("
        "ID INTEGER PRIMARY KEY, "
        "album_path TEXT, "
        "nb_streams INTEGER, "
        "nb_programs INTEGER, "
        "nb_stream_groups INTEGER, "
        "format_name TEXT, "
        "format_long_name TEXT, "
        "start_time INTEGER, "
        "duration REAL, "
        "size TEXT, "
        "bit_rate INTEGER, "
        "probe_score INTEGER, "

        "album TEXT, "
        "artist TEXT, "
        "album_artist TEXT, "
        "genre TEXT, "
        "disc TEXT, "
        "title TEXT, "
        "track TEXT, "
        "track_total TEXT, "
        "date TEXT, "
        "comment TEXT, "
        "publisher TEXT, "
        "encoder TEXT, "
        "encoded_by TEXT, "
        "organization TEXT, "
        "composer TEXT, "
        "copyright TEXT, "
        "album_dynamic_range TEXT, "
        "dynamic_range TEXT, "
        "label TEXT, "
        "year TEXT, "
        //stream
        "stream0_index INTEGER, stream0_codec_name TEXT, stream0_sample_rate TEXT, stream0_channels INTEGER"
        ");",
        //stream

        0, 0, 0);






    if (rc != SQLITE_OK) {
        std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return rc;
    }


    for (auto [dirPath, trackList] : _AlbumList)
    {
        for (auto& [trackName, size, mediaInfo, mediaInfoString] : trackList)
        {
            std::wstring albumPath = dirPath.path().wstring();

            Stream stream = mediaInfo.streams.size() > 0 ? mediaInfo.streams[0] : Stream{};

            const char* sql = R"(
            INSERT OR REPLACE INTO TracksDB (
                id, album_path, nb_streams, nb_programs, nb_stream_groups, format_name, format_long_name,
                start_time, duration, size, bit_rate, probe_score,
                album, artist, album_artist, genre, disc, title, track, track_total, date, comment,
                publisher, encoder, encoded_by, organization, composer, copyright,
                album_dynamic_range, dynamic_range, label, year, stream0_index, stream0_codec_name, stream0_sample_rate, stream0_channels
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";

            sqlite3_stmt* stmt;
            int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
            if (rc != SQLITE_OK) {
                std::cerr << "Prepare failed: " << sqlite3_errmsg(db) << "\n";
                return rc;
            }

            // Bind parameters (1-based indexing)
            int bindIndex = 1;
            sqlite3_bind_null(stmt, bindIndex++); // id (auto-incremented)
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(albumPath).c_str(), -1, SQLITE_TRANSIENT);
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
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.album).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.artist).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.album_artist).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.genre).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.disc).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.title).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.track).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.track_total).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.date).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.comment).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.publisher).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.encoder).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.encoded_by).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.organization).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.composer).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.copyright).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.album_dynamic_range).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.dynamic_range).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.label).c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, CommonUtils::wstringToUtf8(mediaInfo.format_tags.year).c_str(), -1, SQLITE_TRANSIENT);

            //stream
            sqlite3_bind_int(stmt, bindIndex++, stream.index);
            sqlite3_bind_text(stmt, bindIndex++, stream.codec_name.value_or("").c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, bindIndex++, stream.sample_rate.value_or("").c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, bindIndex++, stream.channels.value_or(0));


            // Execute the statement
            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                std::cerr << "***ERROR - Database error: " << sqlite3_errmsg(db) << "\n";
            }

            // Clean up
            sqlite3_finalize(stmt);
        }
    }


    if (rc != SQLITE_OK) {
        std::cerr << "Cannot insert data: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return rc;
    }


    sqlite3_close(db);

    return true;
}


