// FlacConvert.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <filesystem>
#include <array>
#include <algorithm>
#include <mutex>
#include <future>

#include <chrono>
#include <thread>


#include <iomanip>
#include <iostream>


#include <cassert>
#include <exception>

#include <sstream>
#include <string>
#include <any>

#include "AlbumCollection.h"
#include "FolderConvert.h"

#include "MediaInformation.h"

#include "MediaConvertionTask.h"
#include "MediaConvertionAsyncTask.h"

namespace fs = std::filesystem;



fs::path _TMPDirectory{  };


#include <iostream>
#include <fcntl.h>
#include <io.h>




int ConvertMediaTracksToNotmalFLAC(const fs::path& dirName)
{
    _TMPDirectory = std::filesystem::temp_directory_path();

    std::string userSourcePath;
 
    std::wcout << "Using " << dirName << std::endl;

    auto startTime = std::chrono::steady_clock::now();

    std::tuple<int, long, long long> scanInfo{ 0, 0, 0};

    FolderConvert fc;

    int retStatus = fc.GetFilesData(scanInfo, dirName);

    //wait
    //std::wcout << std::endl << "Press Enter to Continue..." << std::endl;
    //std::getchar();

    auto& [retStatus2, nFiles, nFilesSize] = scanInfo;

    std::wcout << std::endl << "======================" << std::endl;
    std::wcout << "Total Files:" << nFiles << std::endl;
    std::wcout << "Total Size:" << nFilesSize << std::endl;

    

    auto ret = fc.ConverAllDirectories(dirName, false);
    if (ret == -1) {
        std::wcout << "***STOP*** ConverAllDirectories" << std::endl;
        return -1;
    }

    std::wcout << std::endl << L"Success!!!" << std::endl;


       //log total execution time (millis)
    auto endTime = std::chrono::steady_clock::now();
    std::wcout << "-->### Total processing time(milliseconds) : "
        << std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count()
        << " ms" << std::endl;


    return 0;
}

#include "WindowsHelpers.h"



int main()
{

    enum Action { NullEnum, ConverEnum, CreateJSONEnum, ProcessJSONEnum, PopulateJsonToDBEnum };

    Action action{ NullEnum };
    const fs::path databaseFileName{ "all_albums.db" };

    std::cout << "Select run option [1 - Re/Convert FLAC, 2 - Scan directories, 3 - Get Duplicates, 4 - Update DB]" << std::endl;
    char input = getchar();
    switch (input)
    {
    case '1':
        action = ConverEnum;
        break;
    case '2':
        action = CreateJSONEnum;
        break;
    case '3':
        action = ProcessJSONEnum;
        break;
    case '4':
        action = PopulateJsonToDBEnum;
        break;
    default:
        std::cout << "Selection Error: " << input;
        return 0;
        break;
    }


#if 1
  //  action = CreateJSONEnum;
      fs::path outputPath{ "\\\\?\\M:\\tmp\\mediaDB" };
      
      //std::vector<std::tuple<fs::path, fs::path>> mediaDirectoryList = {
      //    {"\\\\?\\M:\\tmp\\jazz", outputPath / "MediaResult.json"}
      //};


      std::vector<std::tuple<fs::path, fs::path>> mediaDirectoryList = {
        {"\\\\?\\M:\\tmp\\24", outputPath / "XXXX"},
      };

    //std::vector<std::tuple<fs::path, fs::path>> mediaDirectoryList = {
    //    {"\\\\?\\M:\\tmp\\24_rdy", outputPath / "MediaResult_24_rdy.json"},
    //    {"\\\\?\\M:\\music\\Classical\\Albums\\24bit", outputPath / "MediaResult_classical_24.json"},
    //    ////{"\\\\?\\M:\\music\\Classical\\Albums\\XRCD", outputPath / "MediaResult_classical_album_XRCD_.json"},
    //    {"\\\\?\\M:\\music\\Classical\\Albums\\flac", outputPath / "MediaResult_classical_album_flac_.json"},
    //    //{"\\\\?\\M:\\music\\Classical\\Albums\\mp3", outputPath / "MediaResult_classical_album_mp3_.json"},
    //    {"\\\\?\\M:\\music\\Classical\\Albums\\AlbumSets_MultiCover", outputPath / "MediaResult_classical_AlbumSets_MultiCover.json"},
    //    //{"\\\\?\\M:\\music\\Classical\\Albums\\AlbumSets_OneCover", outputPath / "MediaResult_AlbumSets_OneCover.json"},
    //};

#else
  //  action = CreateJSONEnum;
    const fs::path outputPath{ "\\\\?\\R:\\24" };

    //std::vector<std::tuple<fs::path, fs::path>> mediaDirectoryList = { 
    //    {"\\\\?\\R:\\24", outputPath / "MediaResult.json"}
    //};

    std::vector<std::tuple<fs::path, fs::path>> mediaDirectoryList = { 
        {"\\\\?\\R:\\24", outputPath / "MediaResult_classical_24.json"},
        {"\\\\?\\R:\\24", outputPath / "MediaResult_24_rdy.json"}
    };
#endif


    fs::path databasePath = outputPath / databaseFileName;

    if (action == ConverEnum)
    {
            //=========CONVERT 24BIT to FLAC
        for (auto& [mediaPath, jsonPath] : mediaDirectoryList)
        {
            ConvertMediaTracksToNotmalFLAC(mediaPath);
        }

    }
    else if (action == CreateJSONEnum)
    {
        for (auto& [mediaPath, jsonPath] : mediaDirectoryList)
        {
            auto startTime = std::chrono::steady_clock::now();
            std::cout << std::format("===>Processing new collection: {}...", mediaPath.generic_string()) << std::endl;

            AlbumCollection ac;
            ac.LoadAlbumCollection(mediaPath); //load albume list from directory path
            ac.SortByNumberOfTracks();         // sort by album size - optional
            auto nAlbums = ac.RefreshAlbumCollectionMediaInformation(true); //load media metadate
            ac.SaveAlbumCollectionToJSONFile(jsonPath); // save to json

            auto endTime = std::chrono::steady_clock::now();
            std::cout << std::format("<===Processing time for {} [{} Albums] = {}ms", mediaPath.generic_string(), nAlbums, std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count()) << std::endl;
            std::cout << std::endl;
        }
    }
    else if (action == ProcessJSONEnum)
    {
        DirectoryContentEntryList medialList;
        for (auto& [mediaPath, jsonPath] : mediaDirectoryList)
        {
            std::wcout << std::format(L"Processing: {}", jsonPath.generic_wstring()) << std::endl;

            auto const& accumulatedList = AlbumCollection::LoadAlbumCollectionFromJSON(jsonPath, true);
            medialList.insert(medialList.end(), accumulatedList.begin(), accumulatedList.end());
        }

        AlbumCollection ac(std::move(medialList));
        // ***by know medialList should contain an empty list***

        ac.SortByNumberOfTracks();
        auto dupList = ac.FindDuplicatedAlbums();


        auto iCount = dupList.size();
        int iCurrent = 0;
        for (auto entry : dupList)
        {
            iCurrent++;
            auto [dir1, dir2] = entry;

            
            std::wcout << std::format(L"[{}/{}] - {}", iCurrent, iCount, dir1) << std::endl;
            std::wcout << std::format(L"[{}/{}] - {}", iCurrent, iCount, dir2) << std::endl << std::endl;
            
            //auto userSelection = std::getchar();
            WindowsHelpers::OpenDirectoryInExplorer(dir1);
            WindowsHelpers::OpenDirectoryInExplorer(dir2);

            iCount--;
        }
    }
    else if (action == PopulateJsonToDBEnum)
    {
        for (auto& [mediaPath, jsonPath] : mediaDirectoryList)
        {
            AlbumCollection ac(AlbumCollection::LoadAlbumCollectionFromJSON(jsonPath));
            ac.SaveMediaInfoDocumentToDB(databasePath);
        }
    }


    return 0;
}



