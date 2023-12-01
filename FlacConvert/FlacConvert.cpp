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

#include "MediaConvertionTask.h"
#include "MediaConvertionAsyncTask.h"

namespace fs = std::filesystem;

//fs::path sourcePath = fs::path("M:")/"tmp"/"24";
//fs::path sourcePath = fs::path("M:\\tmp\\24\\MMM");
fs::path sourcePath = fs::path("M:\\tmp\\24_new_files\\_DSD\\Arabella Steinbacher - Mozart_Violin Concertos 1 & 2 (2021) [DSD256_2.0]");
fs::path sourceDirectory{ sourcePath };


std::string const _SourceFileType1{ ".flac" };
std::string const _SourceFileType2{ ".dsf" };
std::string const _SourceFileType3{ ".dff" };
std::string const _TargetFileType{ ".flac" };

//const std::filesystem::path _TargetExtension{ "flac" };

int ConverAllDirectories(const std::filesystem::path& directory, bool bAsync = false)
{
    std::vector<std::shared_ptr<MediaConvertionTask>> tasksVector;

    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
//        std::cout << "reg dir: " << entry.is_directory() << std::endl;
//        std::cout << "reg file: " << entry.is_regular_file() << std::endl;

//        std::cout << entry.path().generic_wstring().c_str() << std::endl;

        auto hasExtension = entry.path().has_extension();
        auto fileEextension = entry.path().extension();

//        if (entry.is_regular_file() && entry.path().has_extension() && (fileEextension == ".flac")) {
        if (entry.is_regular_file() && entry.path().has_extension() && ((fileEextension == _SourceFileType1) || (fileEextension == _SourceFileType2) || (fileEextension == _SourceFileType3))) {

            const fs::path sourcePath = entry.path();

            fs::path targetPath = entry.path();
            targetPath.replace_extension(_TargetFileType);

            fs::path targetTMPPath = entry.path();
            fs::path targetTMPFileName = targetTMPPath.stem() += fs::path{ "_TMP" };
            targetTMPPath.replace_filename(targetTMPFileName);
            targetTMPPath += _TargetFileType;
            //fs::path targetFileName{ sourcePath.stem() += fs::path{"_tmp"} += sourcePath.extension() };

      //      fs::path targetFileName{ sourcePath.stem() += _TargetFileType };

        //    fs::path targetTMPFileName{ sourcePath.stem() += fs::path{"_tmp"} += _TargetFileType };
         //   fs::path targetTMPPath{ targetTMPFileName };


       //     

            //fs::path targetPath{ sourcePath };
            //targetPath.replace_filename(targetFileName);

            std::shared_ptr<MediaConvertionTask> pTask = bAsync ? std::make_shared<MediaConvertionAsyncTask>(sourcePath, targetPath, targetTMPPath) : std::make_shared<MediaConvertionTask>(sourcePath, targetPath, targetTMPPath);
            tasksVector.push_back(pTask);
        }
        else {
            if (entry.is_directory()) {
                int ret = ConverAllDirectories(entry.path(), bAsync);
                if (ret == -1) {
                    std::cout << "***STOP*** ConverAllDirectories" << std::endl;
                    return -1;
                }
            }
        }
    }

    int saa = tasksVector.size();

    //process all files in current directory
    std::for_each(tasksVector.begin(), tasksVector.end(), [](auto& f) { f->Run(); });
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


    std::string userSourcePath;
//    std::wcout << "Enter source directory [" << sourceDirectory << "]: " << std::endl;
 //   std::cin >> userSourcePath;
    std::wcout << "Using " << sourceDirectory << std::endl;

    auto startTime = std::chrono::steady_clock::now();

 // while (true) {
        int ret = ConverAllDirectories(sourceDirectory, false);
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
