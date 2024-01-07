#include "AlbumCollection.h"

namespace fs = std::filesystem;
using namespace rapidjson;


//AlbumCollection::AlbumCollection(std::filesystem::path& dirPath) : _AlbumCollectionDirPath(dirPath)
//{
//}

AlbumCollection::AlbumCollection(DirectoryContentEntryList const& albumList) : _AlbumList{ albumList }
{
}

AlbumCollection::AlbumCollection(DirectoryContentEntryList && albumList) : _AlbumList{ albumList }
{
}


void AlbumCollection::Clear()
{
    _AlbumList.clear();
}


bool AlbumCollection::LoadAlbumCollection(std::filesystem::path albumCollectionDirPath)
{
    std::wcout << L"Processing new collection: " << albumCollectionDirPath.generic_wstring() << std::endl;

    //Scan directory and load all tracks location
    LoadFolderNamesListRecrusive(albumCollectionDirPath, 9);

    return true;
}

//Load all all albumes and tracks into _fileList
bool AlbumCollection::LoadAlbumCollectionWithMetadata(std::filesystem::path albumCollectionDirPath, std::filesystem::path& outDirPath)
{

    //Scan directory and load all tracks location
    LoadAlbumCollection(albumCollectionDirPath);

    //For each loaded Albunm/Track, load/reload all media information 
    RefreshAlbumCollectionMediaInformation();

    //Save Media Information ingo a JSON file
    SaveAlbumCollectionToJSONFile(outDirPath);

    return _AlbumList.size() > 0;
}



TrackInfoList AlbumCollection::LoadFolderNamesListRecrusive(std::filesystem::path path, int depth)
{
    //Empty list to store all potential tracks under the current directory (path)
    TrackInfoList currentDirTrackList;

    if (depth == 0)
    {
        return currentDirTrackList;
    }

    //Album tracks list holder 
 //   rapidjson::Value trackMediaArray(rapidjson::kArrayType);

    if (fs::exists(path)) {
        for (const fs::directory_entry& entry : fs::directory_iterator(path)) {
            if (entry.is_directory()) {
                //Scan directory and return the list of files under the directory entry (one level).
                auto trackList = LoadFolderNamesListRecrusive(entry.path(), depth - 1);
                if (trackList.size() > 0)
                {
                    //push the track list
                    _AlbumList.push_back({ entry, trackList });
                }
            }
            else {
                if (entry.is_regular_file())
                {
                    auto hasExtension = entry.path().has_extension();
                    auto fileEextension = entry.path().extension();
                    std::wstring entryPath{ entry.path().wstring() };
                    if (entry.path().has_extension() && (fileEextension == ".flac" || fileEextension == ".mp3")) {

                        auto path2Fixed = entry.path().lexically_normal().native();
                        long long fileSize = fs::file_size(path2Fixed);

                        //auto mediaInfoFile = AlbumCollection::CreateMediaInfoFile(path2Fixed);
                        //if (!mediaInfoFile.empty() && fs::exists(mediaInfoFile))
                        //{
                        //    std::ifstream file(mediaInfoFile);
                        //    std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

                        //    //Add track to Album List
                        //    currentDirTrackList.push_back({ name, fileSize, MediaInformation{}, json});
                        //}


                        auto folderName = entry.path().filename();
                        currentDirTrackList.push_back({ folderName, fileSize, MediaInformation{}, std::string{} });
                    }
                }
            }
        }
    }

    return currentDirTrackList;
}



bool TryFindMemberTag(auto jsonObject, auto name)
{
    if (jsonObject.FindMember(name) != jsonObject.MemberEnd())
    {
        return true;
    }

    return false;
}

auto TryGetObjectMember(auto jsonObject, auto name)
{
    if (TryFindMemberTag(jsonObject, name))
    {
        return jsonObject[name].GetObj();
    }

    return  nullptr;
}

auto TryGetStringMember(auto jsonObject, auto name)
{
    if (TryFindMemberTag(jsonObject, name))
    {
        return jsonObject[name].GetString();
    }

    return  "***n/a***";
}

auto TryGetIntMember(auto jsonObject, auto name)
{
    if (TryFindMemberTag(jsonObject, name))
    {
        return jsonObject[name].GetInt();
    }

    return -1;
}

long TryGetLongMember(auto jsonObject, auto name)
{
    if (TryFindMemberTag(jsonObject, name))
    {
        return std::stol(jsonObject[name].GetString());
    }

    return -1;
}

MediaInformation AlbumCollection::ParseMediaInfoFromJsonString(std::string jsonString)
{

    MediaInformation mediaInfo;

    rapidjson::Document doc;
    doc.Parse(jsonString.c_str());

    if (doc.HasParseError()) {
        std::cerr << "Error parsing JSON: " << doc.GetParseError() << std::endl;

        return mediaInfo;
    }


    if (doc.IsObject())
    {
        auto docObject = doc.GetObj();
        auto formatTag = docObject["format"].GetObj();

        return MediaInformation { AlbumCollection::ParseMediaInformation(formatTag) };        
    }

    return mediaInfo;
}


//Load all media media information from the preloaded album list (_AlbumList)
bool AlbumCollection::RefreshAlbumCollectionMediaInformation()
{
    int albumCount = 0;
    for (auto& [albumPath, trackList] : _AlbumList)
    {
        std::wcout << L"Processing [" << ++albumCount << "/" << _AlbumList.size() << "]: " << albumPath.path() << std::endl;

        //Album tracks list holder 
        rapidjson::Value trackMediaArray(rapidjson::kArrayType);

        std::vector<std::tuple<MediaLoadingFuture, MediaInformation&, std::string&>> asyncFutureList;

        for (auto& [trackName, size, mediaInfo, mediaInfoString] : trackList)
        {
            std::filesystem::path trackPath = albumPath.path() / std::filesystem::path(trackName);

            auto hasExtension = trackPath.has_extension();
            auto fileEextension = trackPath.extension();
            if (trackPath.has_extension() && (fileEextension == ".flac" || fileEextension == ".mp3")) {
                auto path2Fixed = trackPath.lexically_normal().native();
                long long fileSize = fs::file_size(path2Fixed);

                bool bAsync = true;
                if (bAsync)
                {
                    fs::path outfilePath{ trackName / fs::path("_" + TMP_MEDIA_JSON_FILE_NAME) };
                    auto&& miFuture = std::async(std::launch::async, AlbumCollection::GetMediaInfoFromMediaFile, path2Fixed);

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
                    fs::path outfilePath{ trackName / fs::path("_" + TMP_MEDIA_JSON_FILE_NAME) };
                    auto [mi_ret, jsonString_ret] = AlbumCollection::GetMediaInfoFromMediaFile(path2Fixed);
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

    }

    return true;
}



bool AlbumCollection::SaveAlbumCollectionToJSONFile(std::filesystem::path path)
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
                trackDoc.Parse(mediaInfoString.c_str());
                if (trackDoc.HasParseError()) {
                    std::cerr << "Error parsing JSON: " << trackDoc.GetParseError() << std::endl;                    
                }
                else
                {
                    Value valueCopy;
                    valueCopy.CopyFrom(trackDoc["format"], mediaDoc.GetAllocator());
                    trackMediaArray.PushBack(valueCopy, mediaDoc.GetAllocator());
                }
            }
        }

        if (trackMediaArray.Size() > 0)
        {
            try
            {
                //track list exists add album
                std::string name = albumPath.path().generic_string();
                Value key(name.c_str(), mediaDoc.GetAllocator());
                mediaDoc.AddMember(key, trackMediaArray, mediaDoc.GetAllocator());
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
        std::cout << "Document saved to 'output.json'" << std::endl;
    }
    else {
        std::cerr << "Unable to open file for writing" << std::endl;
        return false;
    }

    return true;
}


//ststic function that parses a json metadata JSON and returns an instance of MediaInformation 
MediaInformation AlbumCollection::ParseMediaInformation(auto formatTag)
{
    MediaInformation mi;

    mi.filename = TryGetStringMember(formatTag, "filename");
    mi.format_name = TryGetStringMember(formatTag, "format_name");
    mi.format_long_name = TryGetStringMember(formatTag, "format_long_name");
    mi.start_time = TryGetStringMember(formatTag, "start_time");
    mi.duration = std::stol(TryGetStringMember(formatTag, "duration"));
    mi.size = TryGetStringMember(formatTag, "size");
    mi.bit_rate = TryGetStringMember(formatTag, "bit_rate");
    mi.probe_score = TryGetIntMember(formatTag, "probe_score");

    if (formatTag.FindMember("tags") != formatTag.MemberEnd())
    {
        auto tags = formatTag["tags"].GetObj();

        mi.tags.album = TryGetStringMember(tags, "album");
        mi.tags.artist = TryGetStringMember(tags, "artist");
        mi.tags.album_artist = TryGetStringMember(tags, "album_artist");
        mi.tags.comment = TryGetStringMember(tags, "comment");
        mi.tags.genre = TryGetStringMember(tags, "genre");
        mi.tags.publisher = TryGetStringMember(tags, "publisher");
        mi.tags.title = TryGetStringMember(tags, "title");
        mi.tags.track = TryGetStringMember(tags, "track");
        mi.tags.date = TryGetStringMember(tags, "date");
    }

    return mi;
}

//ststic function that loads album list from a Json file and returns a DirectoryContentEntryList object
DirectoryContentEntryList AlbumCollection::LoadAlbumCollectionFromJSON(std::filesystem::path& path)
{
    DirectoryContentEntryList albumList;

    if (!fs::exists(path)) {

        return albumList;
    }

    std::ifstream file(path);
    // Read the entire file into a string 
    std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    rapidjson::Document doc;

    // Parse the JSON data 
    doc.Parse(json.c_str());

    // Check for parse errors 
    if (doc.HasParseError()) {
        std::cerr << "Error parsing JSON: "
            << doc.GetParseError() << std::endl;

        return albumList;
    }

    bool isObject = doc.IsObject();
    auto jsonObject = doc.GetObj();


    //using TrackInfoList = std::vector<std::tuple<std::wstring, long long, MediaInformation, std::string>>;
    //using EntryFileTuple = std::tuple <std::filesystem::directory_entry, TrackInfoList>;
    //using DirectoryContentEntryList = std::vector<EntryFileTuple>;


    //Albums
    for (auto itr = jsonObject.begin(); itr != jsonObject.end(); itr++)
    {
        TrackInfoList trackList;
        //MediaInfoList mediaInfoList;
        std::string albumName = itr->name.GetString();
        auto mediaTrackList = itr->value.GetArray();
        for (int i = 0; i < mediaTrackList.Size(); i++)
        {
            if (mediaTrackList[i].IsObject())
            {
                MediaInformation mi{ AlbumCollection::ParseMediaInformation(mediaTrackList[i].GetObj()) };
                trackList.push_back({ mi.filename, std::stol(mi.size), mi, json });
            }
        }

        if (trackList.size() > 0)
        {
            //_AlbumList.push_back({ entry, trackList });
            std::filesystem::directory_entry entry{ albumName };
            albumList.push_back({ entry, trackList });
        }

    }

    return albumList;
}


//returns a json document from a json file (on file system) - from path
rapidjson::Document AlbumCollection::GetJSONDoc(std::filesystem::path mediaFilePath)
{
    rapidjson::Document doc;
    std::ifstream file(mediaFilePath);
    std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    doc.Parse(json.c_str());
    if (doc.HasParseError()) {
        std::cerr << "Error parsing JSON: "
            << doc.GetParseError() << std::endl;

        return nullptr;
    }

    return doc;
}

//returns media information (json string and media objec) from a media file (on file system)
std::tuple<MediaInformation, std::string> AlbumCollection::GetMediaInfoFromMediaFile(std::filesystem::path mediaFilePath)
{
    std::size_t hashNumber = std::hash<std::wstring>{}(mediaFilePath);
    auto tmpFile = "tmp_json_media_" + std::to_string(hashNumber) + ".json";

    auto outPath = AlbumCollection::CreateMediaInfoFile(mediaFilePath, tmpFile);
    auto mi = AlbumCollection::ParseMediaInfoFromJsonFile(outPath);
    
    std::ifstream file(outPath);
    std::string jsonString((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    if (fs::exists(outPath)) {
        std::error_code ec;
        if (fs::remove(outPath, ec)) {
        }
    }


    return std::make_tuple(mi, jsonString);
}

//create a media file (on filesystem) from a media track
std::filesystem::path AlbumCollection::CreateMediaInfoFile(std::filesystem::path mediaFilePath, std::filesystem::path outFile)
{
    using namespace std::string_literals;


    int status = 0;

    auto tmpPath = fs::temp_directory_path();
    //fs::path tmpFilePath{ tmpPath.generic_wstring() + L"\\media_info.json"s };
    fs::path tmpFilePath{ tmpPath / outFile };


    std::wstring cmdExecNameW{ L"ffprobe -v quiet -print_format json -show_format "s };
    //std::wstring commandW{ cmdExecNameW + L"'"s + mediaFilePath.generic_wstring() + L"'"s  + L" > '"s + tmpFilePath.generic_wstring() + L"'"s};
    std::wstring commandW{ cmdExecNameW + L"\""s + mediaFilePath.generic_wstring() + L"\""s + L" > \""s + tmpFilePath.generic_wstring() + L"\""s };

    //std::wstring commandW{ cmdExecNameW + LR"( -i ")"s + _sourcePath.generic_wstring() + LR"(" )"s + convertParamsW + L"'" + _targetTMPPath.generic_wstring() + L"'" };

    rapidjson::Document jsonDoc = nullptr;

    try {
        //std::wcout << L"Getting media info:: " << mediaFilePath.generic_wstring() << std::endl;

        if (fs::exists(tmpFilePath)) {
            std::error_code ec;
            if (fs::remove(tmpFilePath, ec)) {
            }
        }

        status = _wsystem(commandW.c_str());

        if (status == 0)
        {
            jsonDoc = AlbumCollection::GetJSONDoc(tmpFilePath);


            //if (fs::exists(tmpFilePath)) {
            //    std::error_code ec;
            //    if (fs::remove(tmpFilePath, ec)) {
            //    }
            //}

            return tmpFilePath;
        }
    }
    catch (const std::exception& ex) {
        std::wcout << " ### COMMAND INFO EXCEOTION :" << mediaFilePath.generic_wstring() << std::endl << ex.what() << std::endl;

    }

    return std::filesystem::path{};;
}

//parse jsonstring and return a media object
MediaInformation AlbumCollection::ParseMediaInfoFromJsonFile(std::filesystem::path jsonMediaInfoPath)
{
    MediaInformation mediaInfo;

    //auto mediaInfoFile = AlbumCollection::CreateMediaInfoFile(path2Fixed);
    if (!jsonMediaInfoPath.empty() && fs::exists(jsonMediaInfoPath))
    {
        std::ifstream file(jsonMediaInfoPath);
        std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        mediaInfo = ParseMediaInfoFromJsonString(json);
    }

    return mediaInfo;
}


//-------------COMPARE


void AlbumCollection::SortByNumberOfTracks()
{
    std::ranges::stable_sort(_AlbumList, [](auto& album1, auto& album2) {
        auto [albumName1, trackList1] = album1;
        auto [albumName2, trackList2] = album2;

        return trackList2.size() > trackList1.size();
        });
}


SimilarDirectoryEntryList& AlbumCollection::GetDuplicatedAlbums()
{
    return _DuplicatedAlbumList;
}

SimilarDirectoryEntryList& AlbumCollection::CreateDuplicatedAlbums()
{
    auto& albumList = _AlbumList;

    _DuplicatedAlbumList.clear();

    if (albumList.size() < 2)
    {
        return _DuplicatedAlbumList;
    }

    auto firstIt = albumList.begin();
    auto secondIt = firstIt;
    secondIt++;

    while (firstIt != albumList.end() && secondIt != albumList.end())
    {
        bool bFound = false;
        auto& [dirEntry1, fileList1] = *firstIt;
        auto& [dirEntry2, fileList2] = *secondIt;

        //auto dirEntry1 = std::get<0>(*firstIt);
        //auto dirEntry2 = std::get<0>(*secondIt);

        //auto fileList1 = std::get<1>(*firstIt);
        //auto fileList2 = std::get<1>(*secondIt);

        auto pushedEndGroupIt = secondIt;
        int itemsInGroup{ 0 };
        auto fileList1Seize{ fileList1.size() };
        while (secondIt != albumList.end() && fileList1.size() == fileList2.size())
        {
            pushedEndGroupIt = secondIt;
            auto& [dirEntry2, fileList2] = *secondIt;

            secondIt++;
            bFound = true;
            itemsInGroup++;
        }

        secondIt = pushedEndGroupIt;

        auto firstIndex = std::ranges::distance(albumList.cbegin(), firstIt);
        auto lastIndex = std::ranges::distance(albumList.cbegin(), secondIt);

        if (bFound)
        {
            FindDuplicationInGroup(albumList, firstIt, secondIt);
            firstIt = secondIt;;
            secondIt++;
        }
        else
        {
            firstIt++;
            secondIt++;
        }
    }

    return _DuplicatedAlbumList;
}


void AlbumCollection::FindDuplicationInGroup(DirectoryContentEntryList& albumList, DirectoryContentEntryList::iterator firstIt, DirectoryContentEntryList::iterator lastIt)
{
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

                        auto minSize = std::min(mediaInfo1.duration, mediaInfo2.duration);
                        auto maxSize = std::max(mediaInfo1.duration, mediaInfo2.duration);

                        auto diff = maxSize - minSize;
                        long long result = (long)100 * diff / maxSize;

                        if (result > SimilarPercentageTriggerValue)
                        {
                            bPotentialSimilar = false;
                        }
                    }

                    if (bPotentialSimilar)
                    {
                        _DuplicatedAlbumList.push_back({ albumName1.path().generic_wstring(), albumName2.path().generic_wstring() });
                    }
                }
            }
            currentIt++;
        }
    }
}




//--------------------DB



#include "SQLite/sqlite-amalgamation/sqlite3.h"



bool AlbumCollection::SaveMediaInfoDocumentToDB(std::filesystem::path path)
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

    rc = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS AlbumListA (ID INTEGER PRIMARY KEY, album_name TEXT, filename TEXT, format_name TEXT, format_long_name TEXT, start_time TEXT, duration INTEGER, size TEXT, bit_rate TEXT, probe_score INTEGER, album TEXT, artist TEXT, album_artist TEXT, comment TEXT, genre TEXT, publisher TEXT, title TEXT, track TEXT, date TEXT);", 0, 0, 0);

    if (rc != SQLITE_OK) {
        std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return rc;
    }


    for (auto [dirPath, trackList] : _AlbumList)
    {
      
        for (auto& [trackName, size, mediaInfo, mediaInfoString] : trackList)
        {
            //auto queryString = "INSERT INTO test1 VALUES (null, '" + std::string(albumPath) + std::string("', 'John', 25); ");
            //auto queryString = std::format("INSERT INTO AlbumListA VALUES (null, '{}', '{}', '{}', '{}', '{}', '{}', '{}', '{}', '{}', '{}', '{}', '{}', '{}', '{}', '{}', '{}', '{}', '{}');",

            auto albumPath = dirPath.path().generic_string();
            //std::string albumPath2{ dirPath.path().generic_string()};
            //auto trackName2 = trackName.generic_string();

            auto queryString = std::format("INSERT INTO AlbumListA VALUES (null, \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\", \"{}\");",
                albumPath, trackName.generic_string(),
                mediaInfo.format_name, mediaInfo.format_long_name,
                mediaInfo.start_time, mediaInfo.duration, mediaInfo.size, mediaInfo.bit_rate, mediaInfo.probe_score,
                mediaInfo.tags.album, mediaInfo.tags.artist, mediaInfo.tags.album_artist,
                mediaInfo.tags.comment, mediaInfo.tags.genre, mediaInfo.tags.publisher,
                mediaInfo.tags.title, mediaInfo.tags.track, mediaInfo.tags.date);



            std::string query = queryString;
            char* error_report;
            rc = sqlite3_exec(db, query.c_str(), 0, 0, &error_report);
            if (rc)
            {
                int iii = 0;
            }

            int t = 0;
        }
    }


    if (rc != SQLITE_OK) {
        std::cerr << "Cannot insert data: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return rc;
    }


    sqlite3_close(db);


    return 0;


    //const std::string dbPath{ path.generic_string() };

    //sqlite3* db;
    //int rc = sqlite3_open(dbPath.c_str(), &db);

    //if (rc != SQLITE_OK) {
    //    std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
    //    return rc;
    //}

    //// Execute SQL statements
    //rc = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS test (id INTEGER PRIMARY KEY, name TEXT, age INTEGER);", 0, 0, 0);

    //if (rc != SQLITE_OK) {
    //    std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
    //    sqlite3_close(db);
    //    return rc;
    //}

    //// Insert data
    //rc = sqlite3_exec(db, "INSERT INTO test VALUES (1, 'John', 25);", 0, 0, 0);

    //if (rc != SQLITE_OK) {
    //    std::cerr << "Cannot insert data: " << sqlite3_errmsg(db) << std::endl;
    //    sqlite3_close(db);
    //    return rc;
    //}

    //// Query data
    //sqlite3_stmt* stmt;
    //rc = sqlite3_prepare_v2(db, "SELECT id, name, age FROM test;", -1, &stmt, 0);

    //while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    //    int id = sqlite3_column_int(stmt, 0);
    //    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    //    int age = sqlite3_column_int(stmt, 2);

    //    std::cout << "ID: " << id << ", Name: " << name << ", Age: " << age << std::endl;
    //}

    //sqlite3_finalize(stmt);
    //sqlite3_close(db);

    return false;
}
