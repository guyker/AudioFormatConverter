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

#include "MediaConvertionTask.h"
#include "MediaConvertionAsyncTask.h"

namespace fs = std::filesystem;

//fs::path sourcePath = fs::path("M:")/"tmp"/"24";
//fs::path sourcePath = fs::path("M:\\tmp\\24\\MMM");// \\15 DSD Tracks of J.S.Bach 2018");
//fs::path sourcePath = fs::path("M:\\tmp\\24_new_files\\_DSD");
fs::path _SourceDirectory{ "M:\\tmp\\24" };

fs::path _TMPDirectory{  };


std::string const _SourceFileType1{ ".flac" };
std::string const _SourceFileType2{ ".dsf" };
std::string const _SourceFileType3{ ".dff" };
std::string const _SourceFileType4{ ".dsd" };
std::string const _SourceFileType5{ ".wv" };
std::string const _SourceFileType6{ ".wav" };
std::string const _SourceFileType7{ ".m2ts" };

std::string const _TargetFileType{ ".flac" };



int _nDictionary{ 0 };


#include <iostream>
#include <fcntl.h>
#include <io.h>

int GetFilesData(std::tuple<int, long, long>& scanInfo, const std::filesystem::path& directory, bool bAsync = false)
{
    _nDictionary++;
    
    std::wstring entryPath{ directory.wstring() };
    std::wcout << std::endl << L"Scanning Dictionary (" << _nDictionary << "): " << entryPath << std::endl;


    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        if (entry.is_directory()) {
            GetFilesData(scanInfo, entry.path());
        }
    }

    auto& [retStatus, nFiles, nFilesSize] { scanInfo };


    auto nDictionaryFiles{ 0 };
    long long nDictionartFilesSize{ 0 };
    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        auto hasExtension = entry.path().has_extension();
        auto fileEextension = entry.path().extension();

        if (entry.is_regular_file()) 
        {
            std::wstring entryPath{ entry.path().wstring() };
            if (entry.path().has_extension() &&
                ((fileEextension == _SourceFileType1) || (fileEextension == _SourceFileType2) || 
                 (fileEextension == _SourceFileType3) || (fileEextension == _SourceFileType4) ||
                 (fileEextension == _SourceFileType5) || (fileEextension == _SourceFileType6) || (fileEextension == _SourceFileType7))) {

                nDictionartFilesSize += entry.file_size() ;

//                std::wcout << L"File (" << ++nDictionaryFiles << "):" << entryPath << std::endl;
            }
            else
            {
//                std::wcout << L"SKIP: " << entryPath << std::endl;
            }
        }
        else if (entry.is_directory()) {

        }
        else
        {
            std::wcout << L"***UUNKNOWN ENTRY: " << entryPath << std::endl;
        }
    }

    nDictionartFilesSize /= (1024 * 1024);

    std::wcout << L"----Total dictionaty Size: " << nDictionartFilesSize << L"Mb" << std::endl;

    nFiles += nDictionaryFiles;
    nFilesSize += nDictionartFilesSize;
  //  return { retStatus, nFiles + nDictionaryFiles, nFilesSize + nDictionartFilesSize };

    return 0;
}

int ConverAllDirectories(const std::filesystem::path& directory, bool bAsync = false)
{
    std::vector<std::shared_ptr<MediaConvertionTask>> tasksVector;

    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        if (entry.is_directory()) {
            int ret = ConverAllDirectories(entry.path(), bAsync);
            if (ret == -1) {
                std::cout << "***ERROR*** returned from ConverAllDirectories" << std::endl;
                return -1;
            }
        }
    }


    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        auto hasExtension = entry.path().has_extension();
        auto fileEextension = entry.path().extension();

        const fs::path sourcePath = entry.path();

        if (entry.is_regular_file())
        {
            if (entry.path().has_extension() &&
                ((fileEextension == _SourceFileType1) || (fileEextension == _SourceFileType2) || 
                    (fileEextension == _SourceFileType3) || (fileEextension == _SourceFileType4) ||
                    (fileEextension == _SourceFileType5) || (fileEextension == _SourceFileType6) || (fileEextension == _SourceFileType7))) {

                fs::path targetPath = entry.path();
                targetPath.replace_extension(_TargetFileType);

                fs::path targetTMPPath = entry.path();
                fs::path targetTMPFileName = targetTMPPath.stem() += fs::path{ "_TMP" };
                targetTMPPath.replace_filename(targetTMPFileName);
                targetTMPPath += _TargetFileType;

           //     fs::path targetTMPPath = _TMPDirectory += entry.path().stem() += fs::path{ "_TMP" } += fs::path{ _TargetFileType };

                std::shared_ptr<MediaConvertionTask> pTask = bAsync ? std::make_shared<MediaConvertionAsyncTask>(sourcePath, targetPath, targetTMPPath) : std::make_shared<MediaConvertionTask>(sourcePath, targetPath, targetTMPPath);
                tasksVector.push_back(pTask);
            }
            else
            {
                std::wcout << L"---Skipping: " << sourcePath << std::endl;
            }
        }
        else {
            if (entry.is_directory()) {
            }
            else
            {
                std::wcout << L"***UUNKNOWN ENTRY: " << sourcePath << std::endl;
            }
        }
    }

    int saa = tasksVector.size();

    //process all files in current directory
    int processStatus{ 0 };
    std::for_each(tasksVector.begin(), tasksVector.end(), [](auto& f)
        {
            f->Run();
            if (f->GetStatus() == -1)
            {
                
            }
        });
    std::for_each(tasksVector.begin(), tasksVector.end(), [](auto& f) { f->PostRun(); });

    auto found = std::find_if(tasksVector.begin(), tasksVector.end(), [](auto& f) { return f->GetStatus() == -1; });
    if (found != tasksVector.end()) {
        std::cout << "***STOP*** error found: " << (*found)->GetStatus() <<  std::endl;
        return -1;
    }


    return 0;
}


int main()
{

  //  fs::path pathA{ "\\\\?\\R:\\24" };
  //  //fs::path pathA{ "\\\\?\\M:\\tmp\\24_rdy" };
  //  fs::path pathA2{ "\\\\?\\M:\\tmp\\24" };
  //  fs::path pathB{ "\\\\?\\M:\\music\\Classical\\Albums\\ex24bit" };
  // // fs::path pathB{ "E:\\VM-Share\\ut2\\DONE" };

  //  auto dirNameA = pathA.generic_wstring();

  //  FolderCompare fc;
  //  fc.GetFolderNamesList2(pathA, 9);
  //  //fc.GetFolderNamesList2(pathB, 9);
  ////  fc.GetFolderNamesList2(pathB, 9);
  //  fc.sort();
  //  fc.findDuplicates();

  //  auto retRESULT = fc._fileList;

  //  return 0;

 //   _setmode(_fileno(stdout), _O_U16TEXT);
    auto ret = _setmode(_fileno(stdout), _O_U16TEXT);

    _TMPDirectory = std::filesystem::temp_directory_path();

    std::string userSourcePath;
    //    std::wcout << "Enter source directory [" << sourceDirectory << "]: " << std::endl;
     //   std::cin >> userSourcePath;
    std::wcout << "Using " << _SourceDirectory << std::endl;

    auto startTime = std::chrono::steady_clock::now();

    std::tuple<int, long, long> scanInfo {0, 0, 0, };

    int retStatus = GetFilesData(scanInfo, _SourceDirectory);

    //wait
    std::wcout << std::endl << "Press Enter to Continue..." << std::endl;
    std::getchar();

    auto& [retStatus2, nFiles, nFilesSize] = scanInfo;

    std::wcout << std::endl << "======================" << std::endl;
    std::wcout << "Total Files:" << nFiles << std::endl;
    std::wcout << "Total Size:" << nFilesSize << std::endl;

  //  return 0;

    // while (true) {
    ret = ConverAllDirectories(_SourceDirectory, false);
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


