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

//#include <mongoc/client.hpp>
//#include <mongocxx/uri.hpp>

namespace fs = std::filesystem;

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


void FolderCompare::FindDuplicationInGroup(DirectoryContentEntryList::iterator firstIt, DirectoryContentEntryList::iterator lastIt)
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


void FolderCompare::findDuplicates()
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
            FindDuplicationInGroup(firstIt, secondIt);
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
}


#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

rapidjson::Document FolderCompare::GetJSONDoc(std::filesystem::path mediaFilePath)
{
    using namespace std;
    using namespace rapidjson;

    std::ifstream file(mediaFilePath);

    // Read the entire file into a string 
    std::string json((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());

    rapidjson::Document doc;

    // Parse the JSON data 
    doc.Parse(json.c_str());

    // Check for parse errors 
    if (doc.HasParseError()) {
        cerr << "Error parsing JSON: "
            << doc.GetParseError() << endl;

        return nullptr;
    }
    
    auto formatTag = doc["format"].GetObj();


    std::string filename = TryGetStringMember(formatTag, "filename");
    std::string format_name = TryGetStringMember(formatTag, "format_name");
    std::string format_long_name = TryGetStringMember(formatTag, "format_long_name");
    std::string start_time = TryGetStringMember(formatTag, "start_time");
    std::string duration = TryGetStringMember(formatTag, "duration");
    std::string size = TryGetStringMember(formatTag, "size");
    std::string bit_rate = TryGetStringMember(formatTag, "bit_rate");
    int probe_score = TryGetIntMember(formatTag, "probe_score");


    //rapidjson::Document::AllocatorType& allocator = _MediaInfoDocument.GetAllocator();
    Value valueCopy;  
    valueCopy.CopyFrom(doc["format"], _MediaInfoDocument.GetAllocator());
    _MediaInfoDocument.AddMember("format", valueCopy, _MediaInfoDocument.GetAllocator());

    //_MediaInfoDocument.AddMember("filename", "format_name", _MediaInfoDocument.GetAllocator());

    //rapidjson::Document jsonSubDocument(&_MediaInfoDocument.GetAllocator());
    //formatTag.ToJson(jsonSubDocument);




    //auto tags = formatTag["tags"].GetObj();

    if (formatTag.FindMember("tags") != formatTag.MemberEnd())
    {
        auto tags = formatTag["tags"].GetObj();

        std::string album = TryGetStringMember(tags, "album");
        std::string artist = TryGetStringMember(tags, "artist");
        std::string album_artist = TryGetStringMember(tags, "album_artist");
        std::string comment = TryGetStringMember(tags, "comment");
        std::string genre = TryGetStringMember(tags, "genre");
        std::string publisher = TryGetStringMember(tags, "publisher");
        std::string title = TryGetStringMember(tags, "title");
        std::string track = TryGetStringMember(tags, "track");
        std::string date = TryGetStringMember(tags, "date");

        static int th{ 100000 };

        if (std::stoi(bit_rate) < th)
        {
            int i = 0;
        }

    }



    return doc;
}


bool FolderCompare::SaveMediaInfoDocument(std::filesystem::path path)
{
    using namespace rapidjson;

    Document _MediaInfoDocument2;
    _MediaInfoDocument2.SetObject();

    // Add some data to the document
    Value name;
    name.SetString("John", _MediaInfoDocument2.GetAllocator());
    _MediaInfoDocument2.AddMember("name", name, _MediaInfoDocument2.GetAllocator());

    Value age;
    age.SetInt(25);
    _MediaInfoDocument2.AddMember("age", age, _MediaInfoDocument2.GetAllocator());



    //StringBuffer buffer;
    //Writer<StringBuffer> writer(buffer);
    //_MediaInfoDocument2.Accept(writer);
    //const char* json = buffer.GetString();

    //// Save the JSON string to a file
    //std::ofstream file(path);
    //if (file.is_open()) {
    //    file << json;
    //    file.close();
    //    std::cout << "Document saved to 'output.json'" << std::endl;
    //}
    //else {
    //    std::cerr << "Unable to open file for writing" << std::endl;
    //    return 1;
    //}



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
        return 1;
    }








    //std::ofstream ofs{ R"(C:\Test\NewTest.json)" };
    //std::ofstream ofs{ path };
    //if (!ofs.is_open())
    //{
    //    std::cerr << "Could not open file for writing!\n";
    //    return EXIT_FAILURE;
    //}


    //rapidjson::OStreamWrapper osw{ ofs };
    //Writer<OStreamWrapper> jsonWriter{ osw };
    //_MediaInfoDocument.Accept(jsonWriter);





    return false;
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

    for (const fs::directory_entry& entry : fs::directory_iterator(path)) {
        auto folderName = entry.path().filename();
//        auto path = entry.path();
        auto name = folderName.generic_wstring();
        if (entry.is_directory()) {
            //auto folderName = entry.path().stem();
            //folderList.push_back(name);

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
                        MediaInformation mi;
                        mi.JSONDoc = GetJSONDoc(mediaInfoFile);
                        auto jsonDoc2 = GetJSONDoc(mediaInfoFile);


                        folderList.push_back({ name, fileSize});

                    }
                    //if (jsonDoc != nullptr)
                    //{

                    //}

                }
            }
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
