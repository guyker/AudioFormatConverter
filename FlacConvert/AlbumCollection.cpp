
#include <ranges>
#include <algorithm> 

#include "AlbumCollection.h"
#include "FolderConvert.h"
#include "JsonUtils.h"
#include "MediaTrack.h"

#include "CommonUtils.h"

namespace fs = std::filesystem;
using namespace rapidjson;




//constexpr auto CLEAR_LINE{ L"\x1b[H\x1b[J" };



bool AlbumCollection::LoadAlbumCollection(std::filesystem::path albumCollectionDirPath, bool bIncludeMetadata)
{
    auto startTime = std::chrono::steady_clock::now();
    std::cout << "Scanning collection... " << std::endl;

    //Scan directory and load all tracks location
    LoadAlbumCollectionRecursively(albumCollectionDirPath, 9);

    auto endTime = std::chrono::steady_clock::now();
    std::cout << std::format("Completed, Albums: {} [{}ms]", _AlbumList.size(), std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count()) << std::endl;

    if (bIncludeMetadata)
    {
        std::cout << "Loading Albums metadata... " << std::endl;
        auto nAlbums = ImportMetadataFromMediaFiles(true); //load media metadate
        std::cout << "Completed (Loading Albums metadata) Found " << nAlbums << " Albums" << std::endl;
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
                    long long fileSize = fs::file_size(path2Fixed);

                    auto fileName = entry.path().filename();
                    currentDirTrackList.push_back({ fileName, fileSize, FFprobeOutput{}, std::wstring{L"{}"}});
                }
            }
        }
    }
    else
    {
     //   std::cout << std::format("***Error: Media library not found: : {}", path) << std::endl;
    }

    return currentDirTrackList;
}





//std::mutex mtx;


//Load all media media information from the preloaded album list (_AlbumList)
size_t AlbumCollection::ImportMetadataFromMediaFiles(bool bAsync)
{
    int albumCount = 0;
    int progressIndex = 0;

    for (auto& [albumPath, trackList] : _AlbumList)
    {
        std::cout << "\r\033[K";
        //std::wcout << std::format(L"{} Processing [{}/{}]: {}", ProgressCircleChars[progressIndex], ++albumCount, _AlbumList.size(), albumPath.path().generic_wstring());
        std::string str{ "???" };
        try
        {
            //str = RemoveSpecialCharacter(albumPath.path().generic_string());
            str = albumPath.path().generic_string();
        }
        catch (...) {}
        std::cout << std::format("{} Processing [{}/{}]: {}", CommonUtils::ProgressCircleChars[progressIndex], ++albumCount, _AlbumList.size(), str);
        progressIndex = (progressIndex + 1) % CommonUtils::ProgressCircleChars.size();

        //Album tracks list holder 
        std::vector<std::tuple<MediaLoadingFuture, FFprobeOutput&, std::wstring&>> asyncFutureList;

        for (auto& [trackName, size, mediaInfo, mediaInfoString] : trackList)
        {
            std::filesystem::path trackPath = albumPath.path() / std::filesystem::path(trackName);

            if (MediaTrack::IsValidMedia(trackPath)) {
                auto path2Fixed = trackPath.lexically_normal().native();
              //  long long fileSize = fs::file_size(path2Fixed);

                if (bAsync)
                {
                  //  fs::path outfilePath{ trackName / fs::path("_" + TMP_MEDIA_JSON_FILE_NAME) };
                    //std::lock_guard<std::mutex> lock(mtx);
                    auto miFuture = std::async(std::launch::async, MediaTrack::ReadMediaInfoFromFile, path2Fixed);

                  //  miFuture.get();

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

    return _AlbumList.size();
}


bool AlbumCollection::ExportAlbumCollectionToJSONFile(std::filesystem::path path)
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
        if (fs::remove(path, ec)) {
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
bool AlbumCollection::RestoreAlbumCollectionFromJSON(std::filesystem::path path, bool bBasicDataOnly)
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
            auto mi = MediaTrack::ParseMediaTrack(mediaTrackList[i]);

            trackList.push_back({ mi.format.filename, std::stol(mi.format.size.value_or("0")), mi, L"{}" });

			//auto& mediaTags = mediaTrackList[i];
   //         auto jsonString = mediaTags.GetString();

   //         if (mediaTags.IsObject() && mediaTags.HasMember("format"))
   //         {
   //             FFprobeOutput mi{ MediaTrack::ParseMediaTrack(mediaTags["format"]) };

   //             if (bBasicDataOnly)
   //             {
   //                 trackList.push_back({ mi.format.filename, std::stol(mi.format.size.value_or("0")), mi, L"{}" });
   //             }
   //             else
   //             {
   //                 trackList.push_back({ mi.format.filename, std::stol(mi.format.size.value_or("0")), mi, json});
   //             }
   //         }
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


void AlbumCollection::SortByNumberOfTracks()
{
    std::ranges::stable_sort(_AlbumList, [](const auto& album1, const auto& album2) {
        const auto& [albumName1, trackList1] = album1;
        const auto& [albumName2, trackList2] = album2;

        return trackList2.size() > trackList1.size();
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


//CREATE TABLE albums(
//    id INTEGER PRIMARY KEY AUTOINCREMENT,
//    filename TEXT NOT NULL, --From Format.filename
//    duration TEXT, --From Format.duration
//    bit_rate TEXT, --From Format.bit_rate
//    format_name TEXT        -- From Format.format_name
//);
//
//CREATE TABLE tags(
//    id INTEGER PRIMARY KEY AUTOINCREMENT,
//    album_id INTEGER, --Foreign key to albums
//    key TEXT NOT NULL, --Tag name(e.g., "artist", "title")
//    value TEXT NOT NULL, --Tag value(e.g., "The Beatles", "Hey Jude")
//    FOREIGN KEY(album_id) REFERENCES albums(id)
//);
//
//CREATE TABLE streams(
//    id INTEGER PRIMARY KEY AUTOINCREMENT,
//    album_id INTEGER, --Foreign key to albums
//    index INTEGER, --From Stream.index
//    codec_name TEXT, --From Stream.codec_name
//    sample_rate TEXT, --From Stream.sample_rate
//    channels INTEGER, --From Stream.channels
//    FOREIGN KEY(album_id) REFERENCES albums(id)
//);

//void saveToSQLite(const FFprobeOutput& output, const std::string& dbPath) {
//    sqlite3* db;
//    int rc = sqlite3_open(dbPath.c_str(), &db);
//    if (rc) {
//        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << "\n";
//        return;
//    }
//
//    // Create tables
//    const char* createAlbums = "CREATE TABLE IF NOT EXISTS albums (id INTEGER PRIMARY KEY AUTOINCREMENT, filename TEXT NOT NULL, duration TEXT)";
//    const char* createTags = "CREATE TABLE IF NOT EXISTS tags (id INTEGER PRIMARY KEY AUTOINCREMENT, album_id INTEGER, key TEXT NOT NULL, value TEXT NOT NULL, FOREIGN KEY (album_id) REFERENCES albums(id))";
//    sqlite3_exec(db, createAlbums, nullptr, nullptr, nullptr);
//    sqlite3_exec(db, createTags, nullptr, nullptr, nullptr);
//
//    if (true) {
//        // Insert album
//        sqlite3_stmt* stmt;
//        const char* insertAlbum = "INSERT INTO albums (filename, duration) VALUES (?, ?)";
//        sqlite3_prepare_v2(db, insertAlbum, -1, &stmt, nullptr);
//        sqlite3_bind_text(stmt, 1, CommonUtils::wstringToUtf8(output.format.filename).c_str(), -1, SQLITE_STATIC);
//        sqlite3_bind_text(stmt, 2, std::to_string(output.format.duration.value_or(0)).c_str(), -1, SQLITE_STATIC);
//        sqlite3_step(stmt);
//        sqlite3_int64 albumId = sqlite3_last_insert_rowid(db);
//        sqlite3_finalize(stmt);
//
//        // Insert tags
//        if (output.format.tags && output.format.tags.has_value()) {
//            const char* insertTag = "INSERT INTO tags (album_id, key, value) VALUES (?, ?, ?)";
//            sqlite3_prepare_v2(db, insertTag, -1, &stmt, nullptr);
//            for (const auto& [key, value] : output.format.tags.value()) {
//                sqlite3_bind_int64(stmt, 1, albumId);
//                sqlite3_bind_text(stmt, 2, key.c_str(), -1, SQLITE_STATIC);
//                sqlite3_bind_text(stmt, 3, value.c_str(), -1, SQLITE_STATIC);
//                sqlite3_step(stmt);
//                sqlite3_reset(stmt);
//            }
//            sqlite3_finalize(stmt);
//        }
//    }
//
//    sqlite3_close(db);
//}

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
        "start_time TEXT, "
        "duration REAL, "
        "size TEXT, "
        "bit_rate TEXT, "
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
        "year TEXT); ",
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
            //std::string albumPath2{ dirPath.path().generic_string()};
            //auto trackName2 = trackName.generic_string();

            auto jsonString = CommonUtils::wstringToUtf8(mediaInfoString);
            std::string queryString = std::format(
                "INSERT OR REPLACE INTO TracksDB VALUES (null, \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\");",

                CommonUtils::wstringToUtf8(albumPath),
               // CommonUtils::wstringToUtf8(mediaInfo.format.filename),

                mediaInfo.format.nb_streams,
                mediaInfo.format.nb_programs,
                mediaInfo.format.nb_stream_groups,

                mediaInfo.format.format_name,
                mediaInfo.format.format_long_name,

                mediaInfo.format.start_time.value_or(""),
                mediaInfo.format.duration.value_or(0.0),
                mediaInfo.format.size.value_or(""),
                mediaInfo.format.bit_rate.value_or(""),
                mediaInfo.format.probe_score,

                CommonUtils::wstringToUtf8(mediaInfo.format_tags.album),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.artist),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.album_artist),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.genre),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.disc),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.title),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.track),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.track_total),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.date),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.comment),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.publisher),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.encoder),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.encoded_by),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.organization),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.composer),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.copyright),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.album_dynamic_range),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.dynamic_range),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.label),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.year)
            );



        const char* sql = R"(
            INSERT OR REPLACE INTO TracksDB (
                id, album_path, nb_streams, nb_programs, nb_stream_groups, format_name, format_long_name,
                start_time, duration, size, bit_rate, probe_score,
                album, artist, album_artist, genre, disc, title, track, track_total, date, comment,
                publisher, encoder, encoded_by, organization, composer, copyright,
                album_dynamic_range, dynamic_range, label, year
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
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
        sqlite3_bind_text(stmt, bindIndex++, mediaInfo.format.start_time.value_or("").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, bindIndex++, mediaInfo.format.duration.value_or(0.0));
        sqlite3_bind_text(stmt, bindIndex++, mediaInfo.format.size.value_or("").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, mediaInfo.format.bit_rate.value_or("").c_str(), -1, SQLITE_TRANSIENT);
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

        // Execute the statement
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            std::cerr << "***ERROR - Database error: " << sqlite3_errmsg(db) << "\n";
        }

        // Clean up
        sqlite3_finalize(stmt);

            //char* error_report;
            //rc = sqlite3_exec(db, queryString.c_str(), 0, 0, &error_report);
            //if (rc)
            //{
            //    std::cerr << "***ERROR - Database error: " << error_report << std::endl;
            //}
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




bool AlbumCollection::SaveToSQLDatabase_PRE(std::filesystem::path path)
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
        "start_time TEXT, "
        "duration REAL, "
        "size TEXT, "
        "bit_rate TEXT, "
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
        "year TEXT); ",
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
            //std::string albumPath2{ dirPath.path().generic_string()};
            //auto trackName2 = trackName.generic_string();

            auto jsonString = CommonUtils::wstringToUtf8(mediaInfoString);
            std::string queryString = std::format(
                "INSERT OR REPLACE INTO TracksDB VALUES (null, \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\");",

                CommonUtils::wstringToUtf8(albumPath),
                // CommonUtils::wstringToUtf8(mediaInfo.format.filename),

                mediaInfo.format.nb_streams,
                mediaInfo.format.nb_programs,
                mediaInfo.format.nb_stream_groups,

                mediaInfo.format.format_name,
                mediaInfo.format.format_long_name,

                mediaInfo.format.start_time.value_or(""),
                mediaInfo.format.duration.value_or(0.0),
                mediaInfo.format.size.value_or(""),
                mediaInfo.format.bit_rate.value_or(""),
                mediaInfo.format.probe_score,

                CommonUtils::wstringToUtf8(mediaInfo.format_tags.album),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.artist),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.album_artist),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.genre),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.disc),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.title),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.track),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.track_total),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.date),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.comment),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.publisher),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.encoder),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.encoded_by),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.organization),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.composer),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.copyright),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.album_dynamic_range),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.dynamic_range),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.label),
                CommonUtils::wstringToUtf8(mediaInfo.format_tags.year)
            );


            char* error_report;
            rc = sqlite3_exec(db, queryString.c_str(), 0, 0, &error_report);
            if (rc)
            {
                std::cerr << "***ERROR - Database error: " << error_report << std::endl;
            }
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