#include "FolderCompare.h"

#include <windows.h> 

#include <iostream>

#include <vector>
#include <algorithm>
#include <ranges>
#include <functional>

#include "MediaInformation.h"

#include <forward_list>
#include <iterator>
#include <vector>

#include <fstream> 
#include <iostream> 



#include "rapidjson/rapidjson.h" 
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/stringbuffer.h"


#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "FolderConvert.h"


//#include <mongoc/client.hpp>
//#include <mongocxx/uri.hpp>

namespace fs = std::filesystem;
using namespace std;
using namespace rapidjson;


void FolderCompare::OpenDirectoryInExplorer(std::wstring dirName)
{
        
        //using namespace std::string_literals;
        //COMMAND

        //std::wstring cmdExecNameW{ L"ffmpeg" };
        //std::wstring convertParamsW{ L"-c:v copy -sample_fmt s16 -ar 44100 -y -v warning -stats"s };
        //std::wstring commandW{ cmdExecNameW + LR"( -i ")"s + _sourcePath.generic_wstring() + LR"(" )"s + convertParamsW + LR"( ")"s + _targetTMPPath.generic_wstring() + LR"(")"s };

        auto dirNameConv1 = dirName.substr(4, dirName.length() - 4);
        std::wstring dirNameConv2{};
        for (auto c : dirNameConv1)
        {
            if (c == '/')
            {
                dirNameConv2 += '\\';
            }
            else
            {
                dirNameConv2 += c;
            }
        }

        std::wstring cmdExecNameW{ L"explorer.exe /e '" };
        std::wstring commandW{ cmdExecNameW + dirNameConv2 + L"'"};


        try {
         //   _wsystem(commandW.c_str());

            ShellExecute(NULL, NULL, dirNameConv2.c_str(), NULL, NULL, SW_SHOWNORMAL);

        }
        catch (const std::exception& ex) {
        }

}

bool FolderCompare::Compare(EntryFileTuple entry1, EntryFileTuple entry2)
{
    auto dir1 = std::get<0>(entry1);
    auto dir2 = std::get<0>(entry2);
    auto list1 = std::get<1>(entry1);
    auto list2 = std::get<1>(entry2);

    //auto list1FileList = std::get<0>(list1);
    //auto list2FileLiet = std::get<0>(list2);
    //auto list1FileSize = std::get<1>(list1);
    //auto list2FileSize = std::get<1>(list2);


    auto it1 = list1.cbegin();
    auto it2 = list2.cbegin();
    bool bPotentialIdentical = true;

    if (list1.size() == list2.size() && list1.size() >= MinNumberOfFilesInFolderToCompare )
    {

        try
        {

            while (bPotentialIdentical && it1 != list1.cend())
            {

                auto dirName1 = dir1.path().generic_wstring();
                auto dirName2 = dir2.path().generic_wstring();

                //auto& fileInfo1 = *it1;
                //auto& fileInfo2 = *it2;

                //auto fileSize1 = fileInfo1.FileSize;
                //auto fileSize2 = fileInfo2.FileSize;

                auto [file1Name, fileSize1] = *it1;
                auto [file2Name, fileSize2] = *it2;

                fs::path path1{ dirName1 + L"/" + file1Name };
                fs::path path2{ dirName2 + L"/" + file2Name };

                //fs::path path1{ dirName1 + L"/" + fileInfo1.FilePath };
                //fs::path path2{ dirName2 + L"/" + fileInfo2.FilePath };

                auto path1Fixed = path1.lexically_normal().native();
                auto path2Fixed = path2.lexically_normal().native();



                long long minSize = min(fileSize1, fileSize2);
                long long maxSize = max(fileSize1, fileSize2);
                long long diff = maxSize - minSize;

                long long result = (long)100 * diff / maxSize;

                //if (std::labs(fileSize1 - fileSize2) > 10000000)
                if (result > SimilarPercentageTriggerValue)
                {
                    bPotentialIdentical = false;
                }

                it1++;
                it2++;
            }
        }
        catch (std::exception ex)
        {
            int ii = 9;
            bPotentialIdentical = false;
        }
    }
    else
    {
        bPotentialIdentical = false;
    }


    return bPotentialIdentical;

}


void FolderCompare::FindDuplicationInGroup_old(DirectoryContentEntryList::iterator firstIt, DirectoryContentEntryList::iterator lastIt)
{
    if (firstIt != lastIt && firstIt != _fileList.end() && lastIt != _fileList.end())
    {
        auto currentIt = firstIt;
        while (currentIt != lastIt)
        {
            auto currentIt2 = currentIt;
            while (currentIt2 != lastIt)
            {
                currentIt2++;

                auto dir1 = std::get<0>(*currentIt);
                auto dir2 = std::get<0>(*currentIt2);

                auto nameXX1 = dir1.path().generic_wstring();
                auto nameXX2 = dir2.path().generic_wstring();

                //chake//
                auto bPotentialSimilar = Compare(*currentIt, *currentIt2);
                if (bPotentialSimilar)
                {

                    _SimilarDirs++;
                    _SimilarDirectories.push_back({ dir1.path().generic_wstring(), dir2.path().generic_wstring() });
                }

//                firstIt++;
            }
            currentIt++;
        }
    }
}


void FolderCompare::findDuplicates_old()
{
    //fs::directory_entry, std::vector<std::wstring>
    
    //find ranges in _fileList

    if (_fileList.empty())
    {
        return;
    }


    //for (auto& item : _fileList)
    //{
    //    auto dir = std::get<0>(item);
    //    auto fileList = std::get<1>(item);
    //    
    //    auto dirName = dir.path().generic_string();

    //    int i = 0;
    //}

    auto firstIt = _fileList.begin();
    auto secondIt = firstIt;
    secondIt++;

    while (firstIt != _fileList.end() && secondIt != _fileList.end())
    {
        bool bFound = false;
        auto dirEntry1 = std::get<0>(*firstIt);
        auto dirEntry2 = std::get<0>(*secondIt);

        auto fileList1 = std::get<1>(*firstIt);
        auto fileList2 = std::get<1>(*secondIt);

        auto pushedEndGroupIt = secondIt;
        int itemsInGroup{ 0 };
        auto fileList1Seize{ fileList1.size() };
        while (secondIt != _fileList.end() && fileList1.size() == fileList2.size())
        {
            pushedEndGroupIt = secondIt;
            dirEntry2 = std::get<0>(*secondIt);
            fileList2 = std::get<1>(*secondIt);
            secondIt++;
            bFound = true;
            itemsInGroup++;
        }
        
        secondIt = pushedEndGroupIt;

      //  secondIt--;

        auto firstIndex = std::ranges::distance(_fileList.cbegin(), firstIt);
        auto lastIndex = std::ranges::distance(_fileList.cbegin(), secondIt);

        if (bFound)
        {
            FindDuplicationInGroup_old(firstIt, secondIt);
            firstIt = secondIt;;
            secondIt++;
        }
        else
        {
            firstIt++;
            secondIt++;
        }
    }

    int iCount = _SimilarDirectories.size();
    for (auto entry : _SimilarDirectories)
    {
        auto [dir1, dir2] = entry;

        OpenDirectoryInExplorer(dir1);
        OpenDirectoryInExplorer(dir2);

        iCount--;
    }
}

void FolderCompare::FindDuplicationInGroup(AlbumList& albumList, AlbumList::iterator firstIt, AlbumList::iterator lastIt)
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

                auto [albumName1, trackList1] = *currentIt;
                auto [albumName2, trackList2] = *currentIt2;

                if (trackList1.size() == trackList2.size())
                {
                    bool bPotentialSimilar = true;
                    for (int i = 0; i < trackList1.size(); i++)
                    {
                        auto track1 = trackList1[i];
                        auto track2 = trackList2[i];

                        auto minSize = min(track1.duration, track2.duration);
                        auto maxSize = max(track1.duration, track2.duration);
                        auto diff = maxSize - minSize;

                        long long result = (long)100 * diff / maxSize;

                        //if (std::labs(fileSize1 - fileSize2) > 10000000)
                        if (result > SimilarPercentageTriggerValue)
                        {
                            bPotentialSimilar = false;
                        }
                    }

                    if (bPotentialSimilar)
                    {
                        int i = 0;
                        //    _SimilarDirectories.push_back({ dir1.path().generic_wstring(), dir2.path().generic_wstring() });
                    }
                }

                //auto dir1 = std::get<0>(*currentIt);
                //auto dir2 = std::get<0>(*currentIt2);

                //auto nameXX1 = dir1.path().generic_wstring();
                //auto nameXX2 = dir2.path().generic_wstring();

                ////chake//
                //auto bPotentialSimilar = Compare(*currentIt, *currentIt2);
                //if (bPotentialSimilar)
                //{

                //    _SimilarDirs++;
                //    _SimilarDirectories.push_back({ dir1.path().generic_wstring(), dir2.path().generic_wstring() });
                //}

            }
            currentIt++;
        }
    }
}


AlbumList FolderCompare::GetDuplicatedAlbums(AlbumList& albumList)
{
    AlbumList dupList;
    if (albumList.size() < 2)
    {
        return dupList;
    }

    auto firstIt = albumList.begin();
    auto secondIt = firstIt;
    secondIt++;

    while (firstIt != albumList.end() && secondIt != albumList.end())
    {
        bool bFound = false;
        auto dirEntry1 = std::get<0>(*firstIt);
        auto dirEntry2 = std::get<0>(*secondIt);

        auto fileList1 = std::get<1>(*firstIt);
        auto fileList2 = std::get<1>(*secondIt);

        auto pushedEndGroupIt = secondIt;
        int itemsInGroup{ 0 };
        auto fileList1Seize{ fileList1.size() };
        while (secondIt != albumList.end() && fileList1.size() == fileList2.size())
        {
            pushedEndGroupIt = secondIt;
            dirEntry2 = std::get<0>(*secondIt);
            fileList2 = std::get<1>(*secondIt);
            secondIt++;
            bFound = true;
            itemsInGroup++;
        }

        secondIt = pushedEndGroupIt;

        //  secondIt--;

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

    int iCount = _SimilarDirectories.size();
    for (auto entry : _SimilarDirectories)
    {
        auto [dir1, dir2] = entry;

        OpenDirectoryInExplorer(dir1);
        OpenDirectoryInExplorer(dir2);

        iCount--;
    }
}

void FolderCompare::SortByNumberOfTracks(AlbumList& albumList)
{
    std::ranges::stable_sort(albumList, [](auto& album1, auto& album2) {
            auto [albumName1, trackList1] = album1;
            auto [albumName2, trackList2] = album2;

            return trackList2.size() > trackList1.size();
        });
}

void FolderCompare::sort()
{
    std::ranges::stable_sort(_fileList,
        [](auto& a, auto& b) {
            auto [dir1, list1] = a;
            auto [dir2, list2] = b;

            //auto dir1 = std::get<0>(a);
            //auto dir2 = std::get<0>(b);
            //auto list1 = std::get<1>(a);
            //auto list2 = std::get<1>(b);
           
            return list2.size() > list1.size();
        });
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


FolderCompare::FolderCompare()
{
    _MediaInfoDocument.SetObject();


    //rapidjson::Value myArray(rapidjson::kArrayType);
    //rapidjson::Document::AllocatorType& allocator = _MediaInfoDocument.GetAllocator();

    //Value valueCopy;
    ////valueCopy.CopyFrom(doc["format"], _MediaInfoDocument.GetAllocator());
    //std::string key = "Album-" + std::to_string(index++);
    //Value keyValue(key.c_str(), _MediaInfoDocument.GetAllocator());
    //_MediaInfoDocument.AddMember(keyValue, valueCopy, _MediaInfoDocument.GetAllocator());
}



rapidjson::Document FolderCompare::GetJSONDoc(std::filesystem::path mediaFilePath)
{
    rapidjson::Document doc;
    std::ifstream file(mediaFilePath);
    std::string json((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());

    doc.Parse(json.c_str());
    if (doc.HasParseError()) {
        cerr << "Error parsing JSON: "
            << doc.GetParseError() << endl;

        return nullptr;
    }

    return doc;
}


bool FolderCompare::SaveMediaInfoDocument(std::filesystem::path path)
{
    if (fs::exists(path)) {
        std::error_code ec;
        if (fs::remove(path, ec)) {
        }
    }

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    _MediaInfoDocument.Accept(writer);
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

AlbumList FolderCompare::LoadMediaInfoDocument(std::filesystem::path path)
{
    AlbumList albumList;

    

    if (!fs::exists(path)) {

        return albumList;
    }

    std::ifstream file(path);
    // Read the entire file into a string 
    std::string json((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());

    rapidjson::Document doc;

    // Parse the JSON data 
    doc.Parse(json.c_str());

    // Check for parse errors 
    if (doc.HasParseError()) {
        cerr << "Error parsing JSON: "
            << doc.GetParseError() << endl;

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


std::filesystem::path FolderCompare::GetMediaInfoFile(std::filesystem::path mediaFilePath)
{
    using namespace std::string_literals;


    int status = 0;

    auto tmpPath = fs::temp_directory_path();
    fs::path tmpFilePath{ tmpPath.generic_wstring() + L"\\media_info.json"s };


    std::wstring cmdExecNameW{ L"ffprobe -v quiet -print_format json -show_format "s};
    //std::wstring commandW{ cmdExecNameW + L"'"s + mediaFilePath.generic_wstring() + L"'"s  + L" > '"s + tmpFilePath.generic_wstring() + L"'"s};
    std::wstring commandW{ cmdExecNameW + L"\""s + mediaFilePath.generic_wstring() + L"\""s  + L" > \""s + tmpFilePath.generic_wstring() + L"\""s};

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

FileInfoList FolderCompare::GetFolderNamesList2(std::filesystem::path path, int depth)
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
                auto bPotentialConvertable = FolderConvert::IsFileConvertable(fileEextension);
                //if (entry.path().has_extension() && (fileEextension == ".flac" || fileEextension == ".mp3")) {
                if (entry.path().has_extension() && bPotentialConvertable) {

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
        catch(...)
        {
            int i = 0;
        }
    }


    return folderList;
}

//            folderList.push_back({ entry, { }});

std::vector<std::wstring> FolderCompare::GetFolderNamesList(std::filesystem::path path, bool recrusive)
{
    std::vector<std::wstring> folderList;
    
    for (const fs::directory_entry& entry : fs::directory_iterator(path)) {
        if (entry.is_directory()) {
            auto path = entry.path();
            //auto folderName = entry.path().stem();
            auto folderName = entry.path().filename();
            std::wstring name = folderName.generic_wstring();
            folderList.push_back(name);
        }
    }

    return folderList;
}


std::vector<std::wstring> FolderCompare::Compare(std::filesystem::path pathA, std::filesystem::path pathB)
{
    std::vector<std::wstring> folderListA = GetFolderNamesList(pathA);
    std::vector<std::wstring> folderListB = GetFolderNamesList(pathB);;
    std::ranges::sort(folderListA);
    std::ranges::sort(folderListB);

    std::vector<std::wstring> nonSimilarFolders;
    std::vector<std::wstring> similarFolders;

    std::ranges::set_difference(folderListA, folderListB, std::back_inserter(nonSimilarFolders));

    std::ranges::set_intersection(folderListA, folderListB, std::back_inserter(similarFolders));

    return folderListB;
}
