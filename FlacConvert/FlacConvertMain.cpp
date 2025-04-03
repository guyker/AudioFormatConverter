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
#include "AppSettings.h"
#include "CommonUtils.h"

//#include <iostream>
//#include <fcntl.h>
//#include <io.h>
//#include "WindowsHelpers.h"

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
fs::path _TMPDirectory{  };

enum ConvertActionEnum { NullEnum, ConverEnum, CreateJSONEnum, CreateDBFromFolderEnum, ProcessJSONEnum, PopulateJsonToDBEnum };


void waitForKeyPress() {
#ifdef _WIN32
    _getch(); // Windows: Use _getch
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
}



#include <string>
#include <stdexcept>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include "WindowsHelpers.h"
#else
#include <cstdlib>
#endif

namespace FileExplorer {
    void openDirectory(const std::filesystem::path& path) {
#ifdef _WIN32
        std::wstring wPath = path.wstring();
        HINSTANCE result = ShellExecuteW(
            nullptr, L"open", wPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL
        );
        if (reinterpret_cast<intptr_t>(result) <= 32) {
            throw std::runtime_error("Failed to open directory on Windows: " + path.string());
        }
#else
        std::string p = path.string();
        // Try xdg-open first
        std::string command = "xdg-open \"" + p + "\"";
        if (std::system(command.c_str()) != 0) {
            // Fallback to common file managers
            command = "nautilus \"" + p + "\" 2>/dev/null || dolphin \"" + p + "\" 2>/dev/null";
            if (std::system(command.c_str()) != 0) {
                throw std::runtime_error("Failed to open directory on Linux: " + p);
            }
        }
#endif
    }
}






int ConvertFLACToFLAC(std::vector<MediaDirectoryElement> mediaDirectoryList)
{
    std::wcout << "Processing " << mediaDirectoryList.size() << " directories" << std::endl;

    for (auto& mediaEntry: mediaDirectoryList)
    {
        std::wcout << "Scanning: " << mediaEntry.mediaPath.c_str() << "..." << std::endl;

        auto startTime = std::chrono::steady_clock::now();

        ScanInfo scanInfo{};
        int retStatus = FolderConvert::ScanAudioFiles(scanInfo, mediaEntry.mediaPath);

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

        auto ret = FolderConvert::ConverAudioFolder(mediaEntry.mediaPath, scanInfo, false);
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


int ScanFolderAndCreateJSON(std::vector<MediaDirectoryElement> mediaDirectoryList)
{
    for (auto& mediaEntry : mediaDirectoryList)
    {
        auto startTime = std::chrono::steady_clock::now();
        std::cout << std::format("===>Processing new collection: {}...", mediaEntry.mediaPath) << std::endl;

        AlbumCollection ac;
        ac.LoadAlbumCollection(mediaEntry.mediaPath, true); //load albume list from directory path
        ac.SortByNumberOfTracks();         // sort by album size - optional

    //    auto nAlbums = ac.ImportMetadataFromMediaFiles(true); //load media metadate

        //ac.ExportAlbumCollectionToJSONFile(mediaEntry.resultPath); // save to json

        auto endTime = std::chrono::steady_clock::now();
        std::cout << std::format("<===Processing time for {} = {}ms", mediaEntry.mediaPath, std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count()) << std::endl;
        std::cout << std::endl;
    }

    return 0;
}


int ScanFolderProcessJSONAndFindDuplicates(std::vector<MediaDirectoryElement> mediaDirectoryList)
{
    //guyguyguy\
    //verify this function after we changed the l;oading.......

    //DirectoryContentEntryList medialList;
	AlbumCollection albumCollection;
    for (auto& mediaEntry : mediaDirectoryList)
    {
        //std::wcout << std::format(L"Processing: {}", mediaEntry.resultPath) << std::endl;
        std::cout << "Processing: {}" << mediaEntry.resultPath << std::endl;


        bool resul = albumCollection.RestoreAlbumCollectionFromJSON(mediaEntry.resultPath, true);
//        auto const& accumulatedList = AlbumCollection::RestoreAlbumCollectionFromJSON(mediaEntry.resultPath, true);
//        medialList.insert(medialList.end(), accumulatedList.begin(), accumulatedList.end());
    }

    //AlbumCollection ac(std::move(medialList));
    //AlbumCollection ac;
	//ac.LoadAlbumCollection(medialList);
    // ***by know medialList should contain an empty list***

    albumCollection.SortByNumberOfTracks();
    auto dupList = albumCollection.FindDuplicatedAlbums();

    auto iCount = dupList.size();
    int iCurrent = 0;
    for (auto entry : dupList)
    {
        iCurrent++;
        auto [dir1, dir2] = entry;


        std::wcout << std::format(L"[{}/{}] - {}", iCurrent, iCount, dir1) << std::endl;
        std::wcout << std::format(L"[{}/{}] - {}", iCurrent, iCount, dir2) << std::endl << std::endl;

        waitForKeyPress();
        WindowsHelpers::OpenDirectoryInExplorer(dir1);
        WindowsHelpers::OpenDirectoryInExplorer(dir2);
//        FileExplorer::openDirectory(dir1);
//        FileExplorer::openDirectory(dir2);



        iCount--;
    }

    return 0;
}

int ExportJSONToDB(std::vector<MediaDirectoryElement>  mediaDirectoryList)
{
    //guyguy review after loading changes

    auto appSettingPtr = AppSettingsJson::AppSetting();
    fs::path databasePath = fs::path(appSettingPtr->WorkingDirectory) / fs::path(appSettingPtr->DatabaseFileName);

    AlbumCollection albumCollection;

    for (auto& mediaEntry : mediaDirectoryList)
    {
        auto result = albumCollection.RestoreAlbumCollectionFromJSON(mediaEntry.resultPath);
        albumCollection.SaveToDatabase(mediaEntry.dbPath);
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




int main()
{
    auto appSettingPtr = AppSettingsJson::AppSetting();
    if (appSettingPtr == nullptr)
    {
        std::cout << CommonUtils::getSymbolConstexpr("stop_sign") << "Failed to load Configuration File" << std::endl;

		return -1;
    }
  
	std::vector<MediaDirectoryElement> mediaList;

    std::cout << "App Settings: " << std::filesystem::path(AppSettingsJson::DefaultConfigDirectory) / AppSettingsJson::DefaultConfigFileName << std::endl;
    std::cout << "Working directory: " << appSettingPtr->WorkingDirectory << std::endl;
    std::cout << "Database file name: " << appSettingPtr->DatabaseFileName << std::endl;
    std::cout << "Media Libraries: " << std::endl;
	for (auto& mediaEntry : appSettingPtr->MediaDirectoryList)
	{
		auto activeFlag = mediaEntry.isActive ? "Active" : "Inactive";
		std::cout << activeFlag << " - Media Path: " << mediaEntry.mediaPath << " - " << mediaEntry.resultPath << std::endl;

        if (mediaEntry.isActive)
        {
            mediaList.push_back(mediaEntry);
        }
	}

    std::cout << std::endl;


    auto action = GetUserAction();
    switch (action)
    {
    case ConverEnum: //1
        ConvertFLACToFLAC(mediaList);
        break;

    case CreateJSONEnum: //2
        ScanFolderAndCreateJSON(mediaList);
        break;

	case ProcessJSONEnum: //3
        ScanFolderProcessJSONAndFindDuplicates(mediaList);
        break;

	case PopulateJsonToDBEnum: //4    
        ExportJSONToDB(mediaList);    
        break;

    case CreateDBFromFolderEnum: //5
        break;

    default:
        std::cout << "Acrtion not found: " << action;
        break;
    }

    return 0;
}



