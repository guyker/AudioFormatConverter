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

#include <iostream>
#include <fcntl.h>
#include <io.h>
#include "WindowsHelpers.h"
#include "AppSettings.h"


namespace fs = std::filesystem;
fs::path _TMPDirectory{  };

enum ConvertActionEnum { NullEnum, ConverEnum, CreateJSONEnum, CreateDBFromFolderEnum, ProcessJSONEnum, PopulateJsonToDBEnum };

int ConvertFLACToFLAC(std::vector<std::tuple<fs::path, fs::path>> mediaDirectoryList)
{
    std::wcout << "Processing " << mediaDirectoryList.size() << " directories" << std::endl;

    for (auto& [mediaPath, jsonPath] : mediaDirectoryList)
    {
        std::wcout << "Scanning: " << mediaPath << "..." << std::endl;

        auto startTime = std::chrono::steady_clock::now();

        ScanInfo scanInfo{};
        int retStatus = FolderConvert::ScanAudioFiles(scanInfo, mediaPath);

        std::wcout << "Files: " <<
            scanInfo.convertable_file_count << " (Not Regular: " <<
            scanInfo.not_regular_file_count << ", No extension: " <<
            scanInfo.file_with_no_extension_count << ", Not convertable: " <<
            scanInfo.not_convertable_file_count << ", Folders: " <<
            scanInfo.folders_count << ")" << std::endl;
        std::wcout << "Total Size:" << scanInfo.convertable_files_size << std::endl;
        std::wcout << std::endl << "======================" << std::endl;




        //std::wcout << std::endl << "Press Enter to Continue..." << std::endl;
        //std::getchar();

        auto ret = FolderConvert::ConverAudioFolder(mediaPath, scanInfo, false);
        if (ret == -1) {
            std::wcout << "***ERROR*** ConverAudioFiles" << std::endl;
            return -1;
        }

        std::wcout << std::endl << L"Success!!!" << std::endl;

        //log total execution time (millis)
        auto endTime = std::chrono::steady_clock::now();
        std::wcout << "-->### Total processing time(milliseconds) : "
            << std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count()
            << " ms" << std::endl;
    }

    return 0;
}


int ScanFolderAndCreateJSON(std::vector<std::tuple<fs::path, fs::path>> mediaDirectoryList)
{
    for (auto& [mediaPath, jsonPath] : mediaDirectoryList)
    {
        auto startTime = std::chrono::steady_clock::now();
        std::cout << std::format("===>Processing new collection: {}...", mediaPath.generic_string()) << std::endl;

        AlbumCollection ac;
        ac.LoadAlbumCollection(mediaPath); //load albume list from directory path
        ac.SortByNumberOfTracks();         // sort by album size - optional
        auto nAlbums = ac.ExportMediaInformationToDB(true); //load media metadate
        ac.SaveAlbumCollectionToJSONFile(jsonPath); // save to json

        auto endTime = std::chrono::steady_clock::now();
        std::cout << std::format("<===Processing time for {} [{} Albums] = {}ms", mediaPath.generic_string(), nAlbums, std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count()) << std::endl;
        std::cout << std::endl;
    }

    return 0;
}

int ScanFolderAndCreateDB(std::vector<std::tuple<fs::path, fs::path>> mediaDirectoryList)
{
    for (auto& [mediaPath, jsonPath] : mediaDirectoryList)
    {
        auto startTime = std::chrono::steady_clock::now();
        std::cout << std::format("===>Processing new collection (DB): {}...", mediaPath.generic_string()) << std::endl;

        AlbumCollection ac;
        ac.LoadAlbumCollection(mediaPath); //load albume list from directory path
        ac.SortByNumberOfTracks();         // sort by album size - optional

        //            auto nAlbums = ac.RefreshAlbumCollectionMediaInformation(true); //load media metadate
        auto nAlbums = ac.ExportMediaInformationToDB(true); //load media metadate

        ac.SaveAlbumCollectionToJSONFile(jsonPath); // save to json

        auto endTime = std::chrono::steady_clock::now();
        std::cout << std::format("<===Processing time for {} [{} Albums] = {}ms", mediaPath.generic_string(), nAlbums, std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count()) << std::endl;
        std::cout << std::endl;
    }

    return 0;
}

int ScanFolderProcessJSONAndFindDuplicates(std::vector<std::tuple<fs::path, fs::path>> mediaDirectoryList)
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

    return 0;
}

int ExportJSONToDB(std::vector<std::tuple<fs::path, fs::path>> mediaDirectoryList, fs::path databasePath)
{

    for (auto& [mediaPath, jsonPath] : mediaDirectoryList)
    {
        AlbumCollection ac(AlbumCollection::LoadAlbumCollectionFromJSON(jsonPath));
        ac.SaveMediaInfoDocumentToDB(databasePath);
    }

    return 0;
}

ConvertActionEnum GetUserAction()
{
    ConvertActionEnum action{ NullEnum };

    std::cout << "Select run option [1 - Re/Convert FLAC, 2 - Scan directories, 3 - Get Duplicates, 4 - Update DB, XX 5 - Scan DIR to DB, ]" << std::endl;
    char input = getchar();
    switch (input)
    {
    case '1':
        action = ConverEnum;
        break;
    case '2':
        action = CreateJSONEnum;
        break;
    case '5':
        action = CreateDBFromFolderEnum;
        break;
    case '3':
        action = ProcessJSONEnum;
        break;
    case '4':
        action = PopulateJsonToDBEnum;
        break;
    default:
        std::cout << "Selection Error: " << input;
        action = NullEnum;
        break;
    }

	return action;
}


std::shared_ptr<AppSettingsJson> LoadConfiguration()
{
    std::filesystem::path configPath;

    //Get configuration file path from current directory
    if (AppSettingsJson::DefaultConfigDirectory == nullptr || *AppSettingsJson::DefaultConfigDirectory == '\0')
    {
        std::filesystem::path currentPath = std::filesystem::current_path();
        configPath = currentPath / AppSettingsJson::DefaultConfigFileName;
    }
	else
	{
		configPath = fs::path(AppSettingsJson::DefaultConfigDirectory) / fs::path(AppSettingsJson::DefaultConfigFileName);
	}
   

    std::cout << "Configuration file path: " << configPath << std::endl;

	if (fs::exists(configPath))
	{
        std::shared_ptr<AppSettingsJson> appSettingPtr = std::make_shared<AppSettingsJson>();
		appSettingPtr->loadFromFile(configPath.string());

        return appSettingPtr;
	}
    else
    {
		std::cout << "Configuration file not found - Generating default config file, please update settings in config file and run again" << std::endl;

        auto defaultSettings = AppSettingsJson::GetDefaultSettings();
        auto str = defaultSettings.toJsonString();
        defaultSettings.saveToFile(configPath.string());
    }

	return nullptr;
}   


int main()
{
    auto appSettingPtr = LoadConfiguration();
    if (appSettingPtr == nullptr)
    {
        std::cout << "Failed to load Configuration File" << std::endl;

		return -1;
    }


    const fs::path databaseFileName{ "all_albums.db" };

#if 0
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
    const fs::path outputPath{ "\\\\?\\R:\\tmp\\24" };

    //std::vector<std::tuple<fs::path, fs::path>> mediaDirectoryList = { 
    //    {"\\\\?\\R:\\24", outputPath / "MediaResult.json"}
    //};

    std::vector<std::tuple<fs::path, fs::path>> mediaDirectoryList = { 
        {"\\\\?\\R:\\tmp\\24", outputPath / "MediaResult_flac_result.json"},
        //{"\\\\?\\R:\\tmp\24", outputPath / "MediaResult_24_rdy.json"}
    };
#endif

    fs::path databasePath = outputPath / databaseFileName;

    auto action = GetUserAction();
    switch (action)
    {
    case ConverEnum:
        ConvertFLACToFLAC(mediaDirectoryList);
        break;
    case CreateJSONEnum:
        ScanFolderAndCreateJSON(mediaDirectoryList);
        break;
    case CreateDBFromFolderEnum:
        ScanFolderAndCreateDB(mediaDirectoryList);
        break;
    case ProcessJSONEnum:
        ScanFolderProcessJSONAndFindDuplicates(mediaDirectoryList);
        break;
    case PopulateJsonToDBEnum:
        ExportJSONToDB(mediaDirectoryList, databasePath);
        break;
    default:
        std::cout << "Acrtion not found: " << action;
        break;
    }

    return 0;
}



