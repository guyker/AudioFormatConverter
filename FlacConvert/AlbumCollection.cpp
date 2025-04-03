
#include <ranges>
#include <algorithm> 

#include "AlbumCollection.h"
#include "FolderConvert.h"
#include "JsonUtils.h"
#include "MediaTrack.h"

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
                    currentDirTrackList.push_back({ fileName, fileSize, MediaInformation{}, std::wstring{L"{}"}});
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
        std::vector<std::tuple<MediaLoadingFuture, MediaInformation&, std::wstring&>> asyncFutureList;

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
			auto& mediaTags = mediaTrackList[i];
            if (mediaTags.IsObject() && mediaTags.HasMember("format"))
            {
                MediaInformation mi{ MediaTrack::ParseMediaInformation(mediaTags["format"]) };
                if (bBasicDataOnly)
                {
                    trackList.push_back({ mi.format.filename, std::stol(mi.format.size), mi, L"{}" });
                }
                else
                {
                    trackList.push_back({ mi.format.filename, std::stol(mi.format.size), mi, json });
                }
            }
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


                        auto minSize = (std::min)(mediaInfo1.format.duration, mediaInfo2.format.duration);
                        auto maxSize = (std::max)(mediaInfo1.format.duration, mediaInfo2.format.duration);

                        long long diffPercentage = (long)100 * (maxSize - minSize) / maxSize;

                        if (diffPercentage > sizeMatchPercentageThreshold)
                        {
                            bPotentialSimilar = false;
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



bool AlbumCollection::SaveToDatabase(std::filesystem::path path)
{
    const std::string dbPath{ path.generic_string() };

    sqlite3* db;
    int rc = sqlite3_open(dbPath.c_str(), &db);

    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return rc;
    }

    // Execute SQL statements

    rc = sqlite3_exec(db, "DROP TABLE IF EXISTS AlbumListA;", 0, 0, 0);

    rc = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS AlbumListA ("
        "ID INTEGER PRIMARY KEY, "
        "album_name TEXT, "
        "filename TEXT, "
        "format_name TEXT, "
        "format_long_name TEXT, "
        "codec_type TEXT, "
        "start_time TEXT, "
        "duration INTEGER, "
        "size TEXT, "
        "bit_rate TEXT, "
        "probe_score INTEGER, "
        "album TEXT, "
        "artist TEXT, "
        "album_artist TEXT, "
        "comment TEXT, "
        "genre TEXT, "
        "publisher TEXT, "
        "title TEXT, "
        "track TEXT, "
        "date TEXT); ", 0, 0, 0);

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

            std::string queryString = std::format(
                "INSERT OR REPLACE INTO AlbumListA VALUES (null, \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\");",
                CommonUtils::wstringToUtf8(albumPath),
                CommonUtils::wstringToUtf8(trackName.wstring()),

                mediaInfo.format.format_name,
                mediaInfo.format.format_long_name,
                mediaInfo.format.codec_type,
                mediaInfo.format.start_time,
                mediaInfo.format.duration,
                mediaInfo.format.size,
                mediaInfo.format.bit_rate,
                mediaInfo.format.probe_score,
                CommonUtils::wstringToUtf8(mediaInfo.format.tags.album),
                CommonUtils::wstringToUtf8(mediaInfo.format.tags.artist),
                CommonUtils::wstringToUtf8(mediaInfo.format.tags.album_artist),
                CommonUtils::wstringToUtf8(mediaInfo.format.tags.comment),
                CommonUtils::wstringToUtf8(mediaInfo.format.tags.genre),
                CommonUtils::wstringToUtf8(mediaInfo.format.tags.publisher),
                CommonUtils::wstringToUtf8(mediaInfo.format.tags.title),
                CommonUtils::wstringToUtf8(mediaInfo.format.tags.track),
                CommonUtils::wstringToUtf8(mediaInfo.format.tags.date));


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
