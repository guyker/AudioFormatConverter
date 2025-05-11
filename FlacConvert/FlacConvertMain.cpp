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

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>


#include "AlbumCollection.h"
#include "FolderConvert.h"

#include "MediaInformation.h"

#include "MediaConvertionTask.h"
#include "MediaConvertionAsyncTask.h"
#include "AppSettings.h"
#include "CommonUtils.h"


#include "PlatformUtils.h"

#include "FFmpeg.h"

namespace fs = std::filesystem;


enum ConvertActionEnum { NullEnum, ConverEnum, CreateJSONEnum, CreateDBFromFolderEnum, ProcessJSONEnum, PopulateJsonToDBEnum };


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
    //auto logger = spdlog::get("console");
    //if (!logger) {
    //    logger = spdlog::stdout_color_mt("console");
    //}
  //  auto logger = spdlog::stdout_color_mt("console");

    for (auto& mediaEntry : mediaDirectoryList)
    {
        auto startTime = std::chrono::steady_clock::now();
        spdlog::info("-----------");
        spdlog::info("Scan Start=============>ScanFolderAndCreateJSON - Processing new collection: \"{}\"", mediaEntry.mediaPath);

        AlbumCollection ac;

        try {
            auto generator = mediaEntry.getMediaJsonPathCo(AppSettingsJson::AppSetting()->OutDirectory);
            while (generator.resume()) {
                std::error_code ec;
                fs::remove(generator.value(), ec);

                CommonUtils::deleteFilesWithSamePrefix(generator.value());
            }

            // Load albums in batches
            int count = 0;
            for (auto albumListPtr : ac.LoadAlbumsCo(mediaEntry.mediaPath, true, AppSettingsJson::AppSetting()->AlbumsSplitThreshold)) {
                spdlog::info("Received batch with {} albums", albumListPtr->size());
                ac.SortAlbums(albumListPtr, { { SortBy::AlbumArtist, true } }); // sort - optional
                ac.SaveAlbumsToJSON(albumListPtr, mediaEntry.getMediaJsonPath(AppSettingsJson::AppSetting()->OutDirectory, ++count)); // save to json
            }
        }
        catch (const std::filesystem::filesystem_error& e) {
            spdlog::error("Filesystem error: {}", e.what());
            return 1;
        }
        catch (const std::exception& e) {
            spdlog::error("General error: {}", e.what());
            return 1;
        }
        catch (...) {
            spdlog::error("Unknown error");
            return 1;
        }


        auto endTime = std::chrono::steady_clock::now();
        
        spdlog::info("<===ScanFolderAndCreateJSON - Processing time: {}ms, - \"{}\"",
            std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count(), mediaEntry.mediaPath);
    }

    return 0;
}


int ScanFolderProcessJSONAndFindDuplicates(std::vector<MediaDirectoryElement> mediaDirectoryList)
{
	AlbumCollection albumCollection;
	std::shared_ptr<DirectoryContentEntryList> albumListPtr = std::make_shared<DirectoryContentEntryList>();

    for (auto& mediaEntry : mediaDirectoryList)
    {
        auto generator = mediaEntry.getMediaJsonPathCo(AppSettingsJson::AppSetting()->OutDirectory);
        while (generator.resume()) {
			auto mediaJsonPartPath = generator.value();
            if (fs::exists(mediaJsonPartPath)) {
                std::cout << "Processing: {}" << mediaJsonPartPath << std::endl;

                auto albumListChunk = albumCollection.LoadAlbumsFromJSON(mediaJsonPartPath);
                if (!albumListChunk->empty()) {
                    albumListPtr->insert(albumListPtr->end(), albumListChunk->begin(), albumListChunk->end());
                }
            }
            else
            {
				spdlog::error("Error: Json file not found: {}", mediaJsonPartPath);
            }
        }

    }

    spdlog::info("Finding duplicated albums...");
    albumCollection.SortAlbums(albumListPtr, { { SortBy::TrackCount, true } });
    auto dupList = albumCollection.FindDuplicateAlbums(albumListPtr);

    auto iCount = dupList.size();
    spdlog::info("Found {} duplicated albums", iCount);
    int iCurrent = 0;
    for (auto entry : dupList)
    {
        iCurrent++;
        auto [dir1, dir2] = entry;


        std::wcout << std::format(L"[{}/{}] - {}", iCurrent, iCount, dir1) << std::endl;
        std::wcout << std::format(L"[{}/{}] - {}", iCurrent, iCount, dir2) << std::endl << std::endl;

        PlatformUtils::waitForKeyPress();
        PlatformUtils::OpenDirectoryInExplorer(dir1);
        PlatformUtils::OpenDirectoryInExplorer(dir2);

        iCount--;
    }

    return 0;
}

int ExportJSONToDB(std::vector<MediaDirectoryElement>  mediaDirectoryList)
{
    //guyguy review after loading changes

    auto appSettingPtr = AppSettingsJson::AppSetting();
    fs::path databasePath = fs::path(appSettingPtr->OutDirectory) / fs::path(appSettingPtr->DatabaseFileName);

    AlbumCollection albumCollection;

    for (auto& mediaEntry : mediaDirectoryList)
    {
        auto mediaDBPath = mediaEntry.getMediaDBPath(AppSettingsJson::AppSetting()->OutDirectory);
        spdlog::info("Creating Data Base: {}", mediaDBPath);
        spdlog::info("-------------------");


        auto generator = mediaEntry.getMediaJsonPathCo(AppSettingsJson::AppSetting()->OutDirectory);
        while (generator.resume()) {
			auto mediaJsonPartPath = generator.value();
            if (fs::exists(mediaJsonPartPath)) {
                spdlog::info("Processing: {}", mediaJsonPartPath);

                auto albumListPtr = albumCollection.LoadAlbumsFromJSON(mediaJsonPartPath);

                //    albumCollection.SortAlbums(albumListPtr, { { SortBy::AlbumArtist, true } });
                albumCollection.SortAlbums(albumListPtr, { { SortBy::AlbumName, true } });

                albumCollection.ExportToDatabase(albumListPtr, mediaDBPath);
            }
            else
			{
				spdlog::error("Error Exporting to DB: Json file not found: {}", mediaJsonPartPath);
			}   
        }
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

void init_logger()
{
    //spdlog::set_default_logger(spdlog::stdout_color_mt("console"));

    auto console_logger = spdlog::stdout_color_mt("console");

    // Set log level globally (e.g., to debug)
    console_logger->set_level(spdlog::level::debug); // Show all debug+ messages

    // Optional: Set a pattern (e.g., with color and timestamp)
    //console_logger->set_pattern("[%H:%M:%S %z] [%^%l%$] %v");
    console_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%^%l%$] %v");

    // Set as the default logger
    spdlog::set_default_logger(console_logger);

}

int main()
{
    init_logger();

    spdlog::info("FFmpeg version: {}.{}.{}", LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO);
    spdlog::info("----------------");



#ifdef _WIN32
#include <windows.h>
    SetConsoleCP(CP_UTF8);         // For input
    SetConsoleOutputCP(CP_UTF8); // For Unicode output
//    std::wcout.imbue(std::locale("en_US.utf8"));
    //SetConsoleCP(CP_UTF8);
    //// Enable ANSI escape codes in Windows Console
    //HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    //DWORD dwMode = 0;
    //GetConsoleMode(hOut, &dwMode);
    //dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    //SetConsoleMode(hOut, dwMode);
#endif

    auto appSettingPtr = AppSettingsJson::AppSetting();
    if (appSettingPtr == nullptr)
    {
        std::cout << CommonUtils::getSymbolConstexpr("stop_sign") << "Failed to load Configuration File" << std::endl;

        return -1;
    }

    FFmpeg::initialize_ffmpeg_logging();


	std::vector<MediaDirectoryElement> mediaList;

    //std::cout << "App Settings: " << std::filesystem::path(AppSettingsJson::DefaultConfigDirectory) / AppSettingsJson::DefaultConfigFileName << std::endl;
    spdlog::info("Working directory: {}", appSettingPtr->OutDirectory);
    spdlog::info("Database file name: {}", appSettingPtr->DatabaseFileName);

    spdlog::info("----------------");

    spdlog::info("Log Level: {}", AppSettingsJson::AppSetting()->LogLevel);
    spdlog::info("Use Async FFmpeg Calls: {}", AppSettingsJson::AppSetting()->UseAsyncFFmpegCalls);
    spdlog::info("Use ffmpeg Library API: {}", AppSettingsJson::AppSetting()->UseFFmpegLibraryAPI);
    spdlog::info("Extra Audio Quality Metrics: {}", AppSettingsJson::AppSetting()->ExtraAudioQualityMetrics);
    spdlog::info("Albums Split Threshold: {}", AppSettingsJson::AppSetting()->AlbumsSplitThreshold);

    spdlog::info("----------------");

    spdlog::info("Media Libraries:");
	for (auto& mediaEntry : appSettingPtr->MediaDirectoryList)
	{
		auto activeFlag = mediaEntry.isActive ? "Active" : "Inactive";
        spdlog::info("{} - Media Path: {} [{}]", activeFlag, mediaEntry.mediaPath, mediaEntry.mediaName.value_or(""));

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



