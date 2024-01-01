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

    FolderCompare fc;
    fc.GetFolderNamesList2(dirPath, 9);

    fc.SaveMediaInfoDocument(outDir);

    fc.sort();
    fc.findDuplicates_old();

    return true;
}

AlbumList ReadMediaInfoJsonFile()
{
    FolderCompare fc;
    fs::path mediaResultPath{ "M:\\tmp\\MediaResult.json" };
    auto mediaInfoList = fc.LoadMediaInfoDocument(mediaResultPath);



    return mediaInfoList;
}



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

#include "SQLite/sqlite-amalgamation/sqlite3.h"

int TESTSQL()
{
    sqlite3* db;
    int rc = sqlite3_open("example.db", &db);

    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return rc;
    }

    // Execute SQL statements
    rc = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS test (id INTEGER PRIMARY KEY, name TEXT, age INTEGER);", 0, 0, 0);

    if (rc != SQLITE_OK) {
        std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return rc;
    }

    // Insert data
    rc = sqlite3_exec(db, "INSERT INTO test VALUES (1, 'John', 25);", 0, 0, 0);

    if (rc != SQLITE_OK) {
        std::cerr << "Cannot insert data: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return rc;
    }

    // Query data
    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db, "SELECT id, name, age FROM test;", -1, &stmt, 0);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        int age = sqlite3_column_int(stmt, 2);

        std::cout << "ID: " << id << ", Name: " << name << ", Age: " << age << std::endl;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return 0;
}



int main()
{
    TESTSQL();
    return 0;

    enum Action { ConverEnum, CreateJSONEnum, ProcessJSONEnum };
    
    Action action = ConverEnum; //STATIC ACTION SELECTOR


    if (action == ConverEnum)
    {
        //=========CONVERT 24BIT to FLAC
        fs::path dirName{ "M:\\tmp\\24" };
        ConvertMediaTracksToNotmalFLAC(dirName);
    }
    else if (action == CreateJSONEnum)
    {
        //========= SCAN
        fs::path ourDir{ "M:\\tmp\\MediaResult.json" };

        //fs::path pathA{ "\\\\?\\R:\\24" };
        fs::path pathA{ "\\\\?\\M:\\tmp\\24" };
        //fs::path pathA{ "\\\\?\\M:\\music\\Rock-Pop\\Rock\\[misc]\\Bartees Strange" };
          //fs::path pathA{ "\\\\?\\M:\\music\\Rock-Pop\\Rock\\Albums" };
          //fs::path pathA{ "\\\\?\\M:\\tmp\\24_rdy" };
            //fs::path pathA{ "\\\\?\\M:\\music\\Classical\\Albums" };
            //fs::path pathA{ "\\\\?\\M:\\music\\Jazz" };
            //fs::path pathA{ "\\\\?\\M:\\music\\Classical\\Sets" };
          //fs::path pathA{ "\\\\?\\M:\\music\\Classical\\Albums\\24bit" };
          //fs::path pathB{ "E:\\VM-Share\\ut2\\DONE" };
        CreateMediaInfoJsonFile(pathA, ourDir);
        //fs::path path{ "\\\\?\\M:\\music\\Rock-Pop\\Rock\\[misc]\\Bartees Strange" };
        //fs::path mediaResultPath{ "M:\\tmp\\MediaResult.json" };
        //AlbumCollection ac(path, mediaResultPath);

        //auto mediaInfoList = AlbumCollection::ReadAlbumCollectionFromJSON(mediaResultPath);


    }
    else if (action == ProcessJSONEnum)
    {
        auto mediaInfoList = ReadMediaInfoJsonFile();
        FolderCompare fc;
        fc.SortByNumberOfTracks(mediaInfoList);
        AlbumList duplicatedAlbumList = fc.GetDuplicatedAlbums(mediaInfoList);
    }


    return 0;
}



