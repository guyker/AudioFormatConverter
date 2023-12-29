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

/*lbumCollection&& AlbumCollection::CreateAlbumCollection(std::filesystem::path path)
{

	AlbumCollection ac;

	return std::move(ac);
}*/




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