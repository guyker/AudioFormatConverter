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

#include "FolderCompare.h"
#include "FolderConvert.h"

#include "MediaInformation.h"

#include "MediaConvertionTask.h"
#include "MediaConvertionAsyncTask.h"

namespace fs = std::filesystem;


//fs::path _SourceDirectory{ "M:\\tmp\\24" };
fs::path _SourceDirectory{ "R:\\24" };

fs::path _TMPDirectory{  };


#include <iostream>
#include <fcntl.h>
#include <io.h>


bool mainDUPLICATIONS()
{
      //fs::path pathA{ "\\\\?\\R:\\24" };
      //fs::path pathA{ "\\\\?\\M:\\tmp\\24" };
    //fs::path pathA{ "\\\\?\\M:\\music\\Rock-Pop\\Rock\\[misc]\\Bartees Strange" };
    fs::path pathA{ "\\\\?\\M:\\music\\Rock-Pop\\Rock\\[misc]" };
    //fs::path pathA{ "\\\\?\\M:\\tmp\\24_rdy" };
      //fs::path pathA{ "\\\\?\\M:\\music\\Classical\\Albums" };
      //fs::path pathA{ "\\\\?\\M:\\music\\Jazz" };
      //fs::path pathA{ "\\\\?\\M:\\music\\Classical\\Sets" };
      //fs::path pathB{ "\\\\?\\M:\\music\\Classical\\Albums\\ex24bit" };
     //fs::path pathB{ "E:\\VM-Share\\ut2\\DONE" };

      auto dirNameA = pathA.generic_wstring();

      FolderCompare fc;

      fc.GetFolderNamesList2(pathA, 9);


      fs::path mediaResultPath{ "M:\\tmp\\MediaResult.json" };
      fc.SaveMediaInfoDocument(mediaResultPath);

      //fc.GetFolderNamesList2(pathB, 9);
    //  fc.GetFolderNamesList2(pathB, 9);
      fc.sort();
      fc.findDuplicates();

      return true;
}

int main()
{
    mainDUPLICATIONS();
    return 0;

 //   _setmode(_fileno(stdout), _O_U16TEXT);
    auto ret = _setmode(_fileno(stdout), _O_U16TEXT);

    _TMPDirectory = std::filesystem::temp_directory_path();

    std::string userSourcePath;
    //    std::wcout << "Enter source directory [" << sourceDirectory << "]: " << std::endl;
     //   std::cin >> userSourcePath;
    std::wcout << "Using " << _SourceDirectory << std::endl;

    auto startTime = std::chrono::steady_clock::now();

    std::tuple<int, long, long> scanInfo {0, 0, 0, };

    FolderConvert fc;

    int retStatus = fc.GetFilesData(scanInfo, _SourceDirectory);

    //wait
    std::wcout << std::endl << "Press Enter to Continue..." << std::endl;
    std::getchar();

    auto& [retStatus2, nFiles, nFilesSize] = scanInfo;

    std::wcout << std::endl << "======================" << std::endl;
    std::wcout << "Total Files:" << nFiles << std::endl;
    std::wcout << "Total Size:" << nFilesSize << std::endl;

  //  return 0;

    // while (true) {
    ret = fc.ConverAllDirectories(_SourceDirectory, false);
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


