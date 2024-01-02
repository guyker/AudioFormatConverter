#include "AlbumCollection.h"

namespace fs = std::filesystem;
using namespace rapidjson;


AlbumCollection::AlbumCollection(std::filesystem::path& dirPath, std::filesystem::path& outDirPath) : _AlbumCollectionDirPath(dirPath), _OutDirPth(outDirPath)
{
}




//std::filesystem::path& path, std::filesystem::path& mediaResultPath

//Load all all albumes and tracks into _fileList
bool AlbumCollection::LoadAlbumCollection()
{

    //Scan directory and load all tracks location
    LoadFolderNamesListRecrusive(_AlbumCollectionDirPath, 9);


    //For each loaded Albunm/Track, load/reload all media information 
    RefreshAlbumCollectionMediaInformation();


    //Save Media Information ingo a JSON file
    fs::path mediaResultPath{ "R:\\24\\MediaResult.json" };
    //fs::path mediaResultPath{ "M:\\tmp\\MediaResult.json" };
    SaveAlbumCollectionToJSONFile(mediaResultPath);

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
    rapidjson::Value trackMediaArray(rapidjson::kArrayType);

    if (fs::exists(path)) {
        for (const fs::directory_entry& entry : fs::directory_iterator(path)) {
            auto folderName = entry.path().filename();
            auto name = folderName.generic_wstring();

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

                        auto mediaInfoFile = GetMediaInfoFile(path2Fixed);
                        if (!mediaInfoFile.empty() && fs::exists(mediaInfoFile))
                        {
                            std::ifstream file(mediaInfoFile);
                            std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

                            //Add track to Album List
                            currentDirTrackList.push_back({ name, fileSize, MediaInformation{}, json });



                            ////Add track to JSON List
                            //auto trakMEdiaInfo = GetJSONDoc(mediaInfoFile);
                            //Value valueCopy;
                            //valueCopy.CopyFrom(trakMEdiaInfo["format"], _MediaInfoDocument.GetAllocator());
                            //trackMediaArray.PushBack(valueCopy, _MediaInfoDocument.GetAllocator());
                        }

                    }
                }
            }
        }

        //if (trackMediaArray.Size() > 0)
        //{
        //    try
        //    {
        //        //track list exists add album
        //        std::string name = path.generic_string();
        //        Value key(name.c_str(), _MediaInfoDocument.GetAllocator());
        //        _MediaInfoDocument.AddMember(key, trackMediaArray, _MediaInfoDocument.GetAllocator());
        //        //_MediaInfoDocument.AddMember(key, "trackMediaArray", _MediaInfoDocument.GetAllocator());
        //    }
        //    catch (...)
        //    {
        //        int i = 0;
        //    }
        //}
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

MediaInformation AlbumCollection::ParseMediaInformationFromJSON(std::string jsonString)
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
   //     if (formatTag != nullptr)
        {
            mediaInfo.filename = TryGetStringMember(formatTag, "filename");
            mediaInfo.format_name = TryGetStringMember(formatTag, "format_name");
            mediaInfo.format_long_name = TryGetStringMember(formatTag, "format_long_name");
            mediaInfo.start_time = TryGetStringMember(formatTag, "start_time");
            mediaInfo.duration = TryGetLongMember(formatTag, "duration");
            mediaInfo.size = TryGetStringMember(formatTag, "size");
            mediaInfo.bit_rate = TryGetStringMember(formatTag, "bit_rate");
            mediaInfo.probe_score = TryGetIntMember(formatTag, "probe_score");

            bool bTags = TryFindMemberTag(formatTag, "tags");

            if (formatTag.FindMember("tags") != formatTag.MemberEnd())
            {
                auto tags = formatTag["tags"].GetObj();

                mediaInfo.tags.album = TryGetStringMember(tags, "album");
                mediaInfo.tags.artist = TryGetStringMember(tags, "artist");
                mediaInfo.tags.album_artist = TryGetStringMember(tags, "album_artist");
                mediaInfo.tags.comment = TryGetStringMember(tags, "comment");
                mediaInfo.tags.genre = TryGetStringMember(tags, "genre");
                mediaInfo.tags.publisher = TryGetStringMember(tags, "publisher");
                mediaInfo.tags.title = TryGetStringMember(tags, "title");
                mediaInfo.tags.track = TryGetStringMember(tags, "track");
                mediaInfo.tags.date = TryGetStringMember(tags, "date");

                //static int th{ 100000 };

                //if (std::stoi(mi.bit_rate) < th)
                //{
                //    int i = 0;
                //}
            }
        }
    }

    return mediaInfo;
}



//Load all media media information from the preloaded album list (_AlbumList)
bool AlbumCollection::RefreshAlbumCollectionMediaInformation()
{
    for (auto& [albumPath, trackList] : _AlbumList)
    {
        //Album tracks list holder 
        rapidjson::Value trackMediaArray(rapidjson::kArrayType);

        for (auto& [trackName, size, mediaInfo, mediaInfoString] : trackList)
        {
            std::filesystem::path trackPath = albumPath.path() / std::filesystem::path(trackName);

            auto hasExtension = trackPath.has_extension();
            auto fileEextension = trackPath.extension();
            // std::wstring entryPath{ trackPath.wstring() };
            if (trackPath.has_extension() && (fileEextension == ".flac" || fileEextension == ".mp3")) {
                auto path2Fixed = trackPath.lexically_normal().native();
                long long fileSize = fs::file_size(path2Fixed);

                auto mediaInfoFile = GetMediaInfoFile(path2Fixed);
                if (!mediaInfoFile.empty() && fs::exists(mediaInfoFile))
                {
                    std::ifstream file(mediaInfoFile);
                    std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    
                    mediaInfoString = json;
                    mediaInfo = ParseMediaInformationFromJSON(json);

                    int i = 0;
                }
                else
                {
                    int i = 0;
                }
            }
        }
    }

    return true;
}



bool AlbumCollection::SaveAlbumCollectionToJSONFile(std::filesystem::path& path)
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

                //Add track to JSON List
                //auto trakMEdiaInfo = GetJSONDoc(mediaInfoFile);


                rapidjson::Document trackDoc;
                trackDoc.Parse(mediaInfoString.c_str());
                if (trackDoc.HasParseError()) {
                    std::cerr << "Error parsing JSON: " << trackDoc.GetParseError() << std::endl;

                    return false;
                }

                Value valueCopy;
                valueCopy.CopyFrom(trackDoc["format"], mediaDoc.GetAllocator());
                trackMediaArray.PushBack(valueCopy, mediaDoc.GetAllocator());
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









// LEFT OVERS
// LEFT OVERS
// LEFT OVERS
// LEFT OVERS





AlbumList AlbumCollection::ReadAlbumCollectionFromJSON(std::filesystem::path& path)
{
    AlbumList albumList;



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

    //Albums
    for (auto itr = jsonObject.begin(); itr != jsonObject.end(); itr++)
    {
        MediaInfoList mediaInfoList;
        std::string albumName = itr->name.GetString();
        auto mediaTrackList = itr->value.GetArray();
        for (int i = 0; i < mediaTrackList.Size(); i++)
        {
            auto formatTag = mediaTrackList[i].GetObj();

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

                static int th{ 100000 };

                if (std::stoi(mi.bit_rate) < th)
                {
                    int i = 0;
                }

            }

            mediaInfoList.push_back(mi);
        }

        albumList.push_back(std::make_tuple(albumName, mediaInfoList));
    }


    //rapidjson::Value const valueCopy = itr;
//        valueCopy.CopyFrom(item.GetObj(), _MediaInfoDocument.GetAllocator());
//            _MediaInfoDocument.AddMember("item1", item, _MediaInfoDocument.GetAllocator());

    return albumList;
}
 


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


std::filesystem::path AlbumCollection::GetMediaInfoFile(std::filesystem::path mediaFilePath)
{
    using namespace std::string_literals;


    int status = 0;

    auto tmpPath = fs::temp_directory_path();
    fs::path tmpFilePath{ tmpPath.generic_wstring() + L"\\media_info.json"s };


    std::wstring cmdExecNameW{ L"ffprobe -v quiet -print_format json -show_format "s };
    //std::wstring commandW{ cmdExecNameW + L"'"s + mediaFilePath.generic_wstring() + L"'"s  + L" > '"s + tmpFilePath.generic_wstring() + L"'"s};
    std::wstring commandW{ cmdExecNameW + L"\""s + mediaFilePath.generic_wstring() + L"\""s + L" > \""s + tmpFilePath.generic_wstring() + L"\""s };

    //std::wstring commandW{ cmdExecNameW + LR"( -i ")"s + _sourcePath.generic_wstring() + LR"(" )"s + convertParamsW + L"'" + _targetTMPPath.generic_wstring() + L"'" };

    rapidjson::Document jsonDoc = nullptr;

    try {
        std::wcout << L"Getting media info:: " << mediaFilePath.generic_wstring() << std::endl;

        if (fs::exists(tmpFilePath)) {
            std::error_code ec;
            if (fs::remove(tmpFilePath, ec)) {
            }
        }

        status = _wsystem(commandW.c_str());

        if (status == 0)
        {
            jsonDoc = GetJSONDoc(tmpFilePath);


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


