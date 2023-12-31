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

    std::tuple<int, long, long> scanInfo{ 0, 0, 0, };

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




//========= SCAN
//fs::path outDir{ "M:\\tmp\\MediaResult.json" };
//fs::path outDir{ "R:\\24\\MediaResult.json" };
//fs::path pathA{ "\\\\?\\R:\\24" };

//fs::path outDir{ "\\\\?\\M:\\tmp\\MediaResult.json" };
//fs::path pathA{ "\\\\?\\M:\\tmp\\jazz" };
//fs::path pathA{ "\\\\?\\M:\\music\\Rock-Pop\\Rock\\[misc]" };
// 
//fs::path pathA{ "\\\\?\\M:\\music\\Rock-Pop\\Rock\\[misc]\\Bartees Strange" };
    //fs::path pathA{ "\\\\?\\M:\\music\\Rock-Pop\\Rock\\Albums" };
    //fs::path pathA{ "\\\\?\\M:\\tmp\\24_rdy" };
      //fs::path pathA{ "\\\\?\\M:\\music\\Classical\\Albums" };
      //fs::path pathA{ "\\\\?\\M:\\music\\Jazz" };
      //fs::path pathA{ "\\\\?\\M:\\music\\Classical\\Sets" };
    //fs::path pathA{ "\\\\?\\M:\\music\\Classical\\Albums\\24bit" };
    //fs::path pathB{ "E:\\VM-Share\\ut2\\DONE" };


int main()
{    
    auto ret = _setmode(_fileno(stdout), _O_U16TEXT);
    enum Action { ConverEnum, CreateJSONEnum, ProcessJSONEnum, PopulateJsonToDBEnum };

    Action action = CreateJSONEnum; //STATIC ACTION SELECTOR
    const fs::path databaseFileName{ "all_albums.db" };

#if 0
      fs::path outputPath{ "\\\\?\\M:\\tmp" };
      
      std::vector<std::tuple<fs::path, fs::path>> mediaDirectoryList = {
          {"\\\\?\\M:\\tmp\\jazz", outputPath / "MediaResult.json"}
      };

#else
    const fs::path outputPath{ "\\\\?\\R:\\24" };

    std::vector<std::tuple<fs::path, fs::path>> mediaDirectoryList = { 
        {"\\\\?\\R:\\24", outputPath / "MediaResult.json"}
    };
#endif


    fs::path databasePath = outputPath / databaseFileName;


    //wait
    //std::wcout << std::endl << "Press Enter to Continue..." << std::endl;
    //auto userSelection = std::getchar();
    //if (userSelection == '1')
    //{
    //    int i = 0;
    //}

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
            AlbumCollection ac;
            ac.LoadAlbumCollection(mediaPath); //load albume list from directory path
            ac.SortByNumberOfTracks();         // sort by album size - optional
            ac.RefreshAlbumCollectionMediaInformation(); //load media metadate
            ac.SaveAlbumCollectionToJSONFile(jsonPath); // save to json
        }
    }
    else if (action == ProcessJSONEnum)
    {
        DirectoryContentEntryList medialList;
        for (auto& [mediaPath, jsonPath] : mediaDirectoryList)
        {
            auto const& accumulatedList = AlbumCollection::LoadAlbumCollectionFromJSON(jsonPath);
            medialList.insert(medialList.end(), accumulatedList.begin(), accumulatedList.end());
        }

        AlbumCollection ac(std::move(medialList));
        // ***by know medialList should contain an empty list***

        ac.SortByNumberOfTracks();
        auto dupList = ac.FindDuplicatedAlbums();


        int iCount = dupList.size();
        int iCurrent = 0;
        for (auto entry : dupList)
        {
            iCurrent++;
            auto [dir1, dir2] = entry;

            
            std::wcout << std::format(L"[{}/{}] - {}", iCurrent, iCount, dir1) << std::endl;
            std::wcout << std::format(L"[{}/{}] - {}", iCurrent, iCount, dir2) << std::endl << std::endl;
            
            //auto userSelection = std::getchar();
            //WindowsHelpers::OpenDirectoryInExplorer(dir1);
            //WindowsHelpers::OpenDirectoryInExplorer(dir2);

            iCount--;
        }
    }
    else if (action == PopulateJsonToDBEnum)
    {
        for (auto& [mediaPath, jsonPath] : mediaDirectoryList)
        {
            AlbumCollection ac(AlbumCollection::LoadAlbumCollectionFromJSON_Full(jsonPath));
            ac.SaveMediaInfoDocumentToDB(databasePath);
        }
    }


    return 0;
}



