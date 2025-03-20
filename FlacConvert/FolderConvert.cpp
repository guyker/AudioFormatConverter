#include "FolderConvert.h"
#include "MediaConvertionTask.h"
#include "MediaConvertionAsyncTask.h"

#include <iostream>
#include <algorithm>
#include <set>


using namespace std;

namespace fs = std::filesystem;


std::wstring FolderConvert::ToLower(const std::wstring& str) {
    std::wstring lowerStr = str;

    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::towlower);

    return lowerStr;
}

bool FolderConvert::IsFileConvertable(std::filesystem::path filePath)
{
    //L".mp3", L".wav", L".flac", L".aac", L".ogg", L".wma", L".m4a"
    static std::set<wstring> fileExtensionList = { L".flac", L".ape", L".dsf", L".dff", L".dsd", L".wv", L".wav", L".m2ts" , L".m4a" };
    auto fileExtension = ToLower(filePath.extension().wstring());

    return filePath.has_extension() && (fileExtensionList.find(filePath.extension().wstring()) != fileExtensionList.end());
}

bool FolderConvert::IsFileAcceptedAudioFile(std::filesystem::path filePath)
{
    static std::set<wstring> fileExtensionList = { L".flac", L".mp3" };
    auto fileExtension = ToLower(filePath.extension().wstring());

    return filePath.has_extension() && (fileExtensionList.find(filePath.extension().wstring()) != fileExtensionList.end());
}

int FolderConvert::ScanAudioFiles(std::tuple<int, long, long long>& scanInfo, const std::filesystem::path& directory, bool bAsync)
{
    std::wstring entryPath{ directory.wstring() };
    std::wcout << std::endl << L"Scanning Dictionary: " << entryPath << std::endl;

    // Recursively process all subdirectories within the specified directory
    std::filesystem::directory_iterator directoryIT;
    try {
        if (!fs::exists(directory) || !fs::is_directory(directory)) {
            std::cerr << directory << " - Error: Directory does not exist or is not valid.\n";
            return -1;
        }

        auto& [retStatus, nFiles, nFilesSize] { scanInfo };

        // int nFileCount{ 0 };
        // long long nDictionartFilesSize{ 0 };

        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().has_extension() && IsFileConvertable(entry.path())) {
          //      std::wcout << L"Found Audio File: " << entry.path().wstring() << std::endl;
                nFiles++;
                nFilesSize += entry.file_size();
            }
        }
    }
    catch (const fs::filesystem_error& e) {
        std::cerr << "ScanAudioFiles: Filesystem error: " << e.what() << std::endl;

        return -1;
    }

    return 0;
}

int FolderConvert::ConverAudioFiles(const std::filesystem::path& directory, bool bAsync)
{
    std::vector<std::shared_ptr<MediaConvertionTask>> tasksVector;

    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        if (entry.is_directory()) {
            int ret = ConverAudioFiles(entry.path(), bAsync);
            if (ret == -1) {
                std::cout << "***ERROR*** returned from ConverAudioFiles" << std::endl;
                return -1;
            }
        }
    }


    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {

        if (entry.is_regular_file() && entry.path().has_extension())
        {
            if (entry.path().has_extension() && IsFileConvertable(entry.path())) {

                fs::path targetPath = entry.path();
                targetPath.replace_extension(_TargetFileType);

                fs::path targetTMPPath = entry.path();
                fs::path targetTMPFileName = targetTMPPath.stem() += fs::path{ "_TMP" };
                targetTMPPath.replace_filename(targetTMPFileName);
                targetTMPPath += _TargetFileType;

                //     fs::path targetTMPPath = _TMPDirectory += entry.path().stem() += fs::path{ "_TMP" } += fs::path{ _TargetFileType };

                std::shared_ptr<MediaConvertionTask> pTask = bAsync ? std::make_shared<MediaConvertionAsyncTask>(entry.path(), targetPath, targetTMPPath) : std::make_shared<MediaConvertionTask>(entry.path(), targetPath, targetTMPPath);
                tasksVector.push_back(pTask);
            }
            else
            {
                std::wcout << L"---Skipping: " << entry.path() << std::endl;
            }
        }
        else {
            if (entry.is_directory()) {
            }
            else
            {
                std::wcout << L"***UUNKNOWN ENTRY: " << entry.path() << std::endl;
            }
        }
    }

    //process all files in current directory
    int processStatus{ 0 };
    std::for_each(tasksVector.begin(), tasksVector.end(), [](auto& f) {
        f->Run();
        if (f->GetStatus() == -1)

        {



        }

    });

    std::for_each(tasksVector.begin(), tasksVector.end(), [](auto& f) { f->PostRun(); });


    for (auto& item : tasksVector)
    {
        if (item->GetStatus() != 0)
        {
            std::cout << "***STOP*** error found: " << item->GetStatus() << std::endl;
            return -1;

        }
    }
    //auto found = std::find_if(tasksVector.begin(), tasksVector.end(), [](auto& f) { return f->GetStatus() == -1; });
    //if (found != tasksVector.end()) {
    //    std::cout << "***STOP*** error found: " << (*found)->GetStatus() << std::endl;
    //    return -1;
    //}


    return 0;
}


