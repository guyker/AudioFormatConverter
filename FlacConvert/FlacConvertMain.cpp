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
#include "FolderCompare.h"
#include "FolderConvert.h"

#include "MediaInformation.h"

#include "MediaConvertionTask.h"
#include "MediaConvertionAsyncTask.h"

namespace fs = std::filesystem;



fs::path _TMPDirectory{  };


#include <iostream>
#include <fcntl.h>
#include <io.h>


bool CreateMediaInfoJsonFile(fs::path dirPath, fs::path outDir)
{

    //FolderCompare fc;
    //fc.GetFolderNamesList2(dirPath, 9);

    //fc.SaveMediaInfoDocument(outDir);
    //fc.SaveMediaInfoDocumentToDB("all_albums.db");

    //fc.sort();
    //fc.findDuplicates_old();

    return true;
}



//
//
//AlbumList ReadMediaInfoJsonFile()
//{
//    //FolderCompare fc;
//    fs::path mediaResultPath{ "M:\\tmp\\MediaResult.json" };
////    auto mediaInfoList = fc.LoadMediaInfoDocument(mediaResultPath);
//
//
//
//    //return mediaInfoList;
//
//    return AlbumList{};
//}



int ConvertMediaTracksToNotmalFLAC(fs::path& dirName)
{
    //   _setmode(_fileno(stdout), _O_U16TEXT);
    auto ret = _setmode(_fileno(stdout), _O_U16TEXT);

    _TMPDirectory = std::filesystem::temp_directory_path();

    std::string userSourcePath;
    //    std::wcout << "Enter source directory [" << sourceDirectory << "]: " << std::endl;
     //   std::cin >> userSourcePath;
    std::wcout << "Using " << dirName << std::endl;

    auto startTime = std::chrono::steady_clock::now();

    std::tuple<int, long, long> scanInfo{ 0, 0, 0, };

    FolderConvert fc;

    int retStatus = fc.GetFilesData(scanInfo, dirName);

    //wait
    std::wcout << std::endl << "Press Enter to Continue..." << std::endl;
    std::getchar();

    auto& [retStatus2, nFiles, nFilesSize] = scanInfo;

    std::wcout << std::endl << "======================" << std::endl;
    std::wcout << "Total Files:" << nFiles << std::endl;
    std::wcout << "Total Size:" << nFilesSize << std::endl;

    //  return 0;

      // while (true) {
    ret = fc.ConverAllDirectories(dirName, false);
    if (ret == -1) {
        std::wcout << "***STOP*** ConverAllDirectories" << std::endl;
        return -1;
    }

    std::wcout << "Success!!!" << std::endl;
    // }

       //log total execution time (millis)
    auto endTime = std::chrono::steady_clock::now();
    std::wcout << "-->### Total processing time(milliseconds) : "
        << std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count()
        << " ms" << std::endl;


    return 0;
}



int main()
{
    enum Action { ConverEnum, CreateJSONEnum, ProcessJSONEnum };
    
    Action action = CreateJSONEnum; //STATIC ACTION SELECTOR


    if (action == ConverEnum)
    {
        //=========CONVERT 24BIT to FLAC
        fs::path dirName{ "M:\\tmp\\24" };
        ConvertMediaTracksToNotmalFLAC(dirName);
    }
    else if (action == CreateJSONEnum)
    {
        //========= SCAN
        //fs::path ourDir{ "M:\\tmp\\MediaResult.json" };
        
        fs::path outDir{ "R:\\24\\MediaResult.json" };
        fs::path pathA{ "\\\\?\\R:\\24" };

        //fs::path outDir{ "\\\\?\\M:\\tmp\\MediaResult.json" };
        //fs::path pathA{ "\\\\?\\M:\\tmp\\jazz" };
        //fs::path pathA{ "\\\\?\\M:\\music\\Rock-Pop\\Rock\\[misc]\\Bartees Strange" };
        // 
      //fs::path pathA{ "\\\\?\\M:\\music\\Rock-Pop\\Rock\\[misc]\\Bartees Strange" };
          //fs::path pathA{ "\\\\?\\M:\\music\\Rock-Pop\\Rock\\Albums" };
          //fs::path pathA{ "\\\\?\\M:\\tmp\\24_rdy" };
            //fs::path pathA{ "\\\\?\\M:\\music\\Classical\\Albums" };
            //fs::path pathA{ "\\\\?\\M:\\music\\Jazz" };
            //fs::path pathA{ "\\\\?\\M:\\music\\Classical\\Sets" };
          //fs::path pathA{ "\\\\?\\M:\\music\\Classical\\Albums\\24bit" };
          //fs::path pathB{ "E:\\VM-Share\\ut2\\DONE" };


        AlbumCollection ac(pathA, outDir);
        if (ac.LoadAlbumCollection())
        {
            ac.RefreshAlbumCollectionMediaInformation();
            ac.SaveAlbumCollectionToJSONFile(outDir);


            AlbumCollection ac2(AlbumCollection::LoadAlbumCollectionFromJSON(outDir));

        }


    }
    else if (action == ProcessJSONEnum)
    {
        //auto mediaInfoList = ReadMediaInfoJsonFile();
        //FolderCompare fc;
        //fc.SortByNumberOfTracks(mediaInfoList);
        //AlbumList duplicatedAlbumList = fc.GetDuplicatedAlbums(mediaInfoList);
    }


    return 0;
}



