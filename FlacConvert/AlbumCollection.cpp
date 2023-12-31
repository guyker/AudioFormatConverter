#include "AlbumCollection.h"

//#include <filesystem>
//#include <vector>
//
//#include "MediaInformation.h"
//
//#include "rapidjson/rapidjson.h" 
//#include "rapidjson/document.h"
//#include "rapidjson/istreamwrapper.h"
//#include "rapidjson/writer.h"
//#include "rapidjson/stringbuffer.h"
//#include "rapidjson/ostreamwrapper.h"
//#include "rapidjson/stringbuffer.h"

namespace fs = std::filesystem;
using namespace rapidjson;


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


AlbumList AlbumCollection::ReadAlbumCollectionFromJSON(std::filesystem::path path)
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


AlbumCollection::AlbumCollection(std::filesystem::path& path, std::filesystem::path& mediaResultPath)
{
    _MediaInfoDocument.SetObject();
    auto ret = GetFolderNamesList2(path, 9);


    if (fs::exists(mediaResultPath)) {
        std::error_code ec;
        if (fs::remove(mediaResultPath, ec)) {
        }
    }

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    _MediaInfoDocument.Accept(writer);
    const char* json = buffer.GetString();

    // Save the JSON string to a file
    std::ofstream file(mediaResultPath);
    if (file.is_open()) {
        file << json;
        file.close();
        std::cout << "Document saved to 'output.json'" << std::endl;
    }
    else {
        std::cerr << "Unable to open file for writing" << std::endl;
    }

    return;
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

FileInfoList AlbumCollection::GetFolderNamesList2(std::filesystem::path path, int depth)
{
    FileInfoList folderList;

    if (depth == 0)
    {
        return folderList;
    }


    rapidjson::Value trackMediaArray(rapidjson::kArrayType);

    for (const fs::directory_entry& entry : fs::directory_iterator(path)) {
        auto folderName = entry.path().filename();
        auto name = folderName.generic_wstring();

        if (entry.is_directory()) {

            //rapidjson::Value myArray(rapidjson::kArrayType);
            //rapidjson::Document::AllocatorType& allocator = _MediaInfoDocument.GetAllocator();

            //Value valueCopy;
            ////valueCopy.CopyFrom(doc["format"], _MediaInfoDocument.GetAllocator());
            //std::string key = "Album-" + std::to_string(index++);
            //Value keyValue(key.c_str(), _MediaInfoDocument.GetAllocator());
            //_MediaInfoDocument.AddMember(keyValue, valueCopy, _MediaInfoDocument.GetAllocator());


            auto directoryfileList = GetFolderNamesList2(entry.path(), depth - 1);
            if (directoryfileList.size() > 0)
            {

                _fileList.push_back({ entry, directoryfileList });
            }
        }
        else {
            if (entry.is_regular_file())
            {
                auto hasExtension = entry.path().has_extension();
                auto fileEextension = entry.path().extension();
                std::wstring entryPath{ entry.path().wstring() };
                if (entry.path().has_extension() && (fileEextension == ".flac" || fileEextension == ".mp3")) {

                    //auto [file1Name, fileSize1] = *it1;
                    //auto [file2Name, fileSize2] = *it2;

             //       fs::path path1{ dirName1 + L"/" + file1Name };


                    //auto path1Fixed = path1.lexically_normal().native();
                    auto path2Fixed = entry.path().lexically_normal().native();
                    long long fileSize = fs::file_size(path2Fixed);

                    auto mediaInfoFile = GetMediaInfoFile(path2Fixed);
                    if (!mediaInfoFile.empty())
                    {
                        folderList.push_back({ name, fileSize });

                        //    MediaInformation mi;
                     //   mi.JSONDoc = GetJSONDoc(mediaInfoFile);


                        auto trakMEdiaInfo = GetJSONDoc(mediaInfoFile);

                        Value valueCopy;
                        valueCopy.CopyFrom(trakMEdiaInfo["format"], _MediaInfoDocument.GetAllocator());
                        //Value keyValue(key.c_str(), _MediaInfoDocument.GetAllocator());


                        trackMediaArray.PushBack(valueCopy, _MediaInfoDocument.GetAllocator());

                    }

                }
            }
        }
    }

    if (trackMediaArray.Size() > 0)
    {
        try
        {
            //track list exists add album
            std::string name = path.generic_string();
            Value key(name.c_str(), _MediaInfoDocument.GetAllocator());
            _MediaInfoDocument.AddMember(key, trackMediaArray, _MediaInfoDocument.GetAllocator());
            //_MediaInfoDocument.AddMember(key, "trackMediaArray", _MediaInfoDocument.GetAllocator());
        }
        catch (...)
        {
            int i = 0;
        }
    }


    return folderList;
}