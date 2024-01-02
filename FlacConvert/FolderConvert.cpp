#include "FolderConvert.h"
#include "MediaConvertionTask.h"
#include "MediaConvertionAsyncTask.h"

#include <iostream>
#include <algorithm>

namespace fs = std::filesystem;



bool FolderConvert::IsFileConvertable(std::filesystem::path pathName)
{
    static std::vector<std::string> _ConvertableFileTypeList = { ".FLAC", ".flac", ".ape", ".dsf", ".dff", ".dsd", ".wv", ".wav", ".m2ts" , ".m4a" };

    auto it = std::find_if(_ConvertableFileTypeList.begin(), _ConvertableFileTypeList.end(), [&](auto item) {return item == pathName; });
    if (it != _ConvertableFileTypeList.end())
    {
        return true;
    }

    return false;
}

bool FolderConvert::IsFileCollectable(std::filesystem::path pathName)
{
    static std::vector<std::string> _ConvertableFileTypeList = { ".FLAC", ".flac", ".mp3" };

    auto it = std::find_if(_ConvertableFileTypeList.begin(), _ConvertableFileTypeList.end(), [&](auto item) {return item == pathName; });
    if (it != _ConvertableFileTypeList.end())
    {
        return true;
    }

    return false;
}

int FolderConvert::GetFilesData(std::tuple<int, long, long>& scanInfo, const std::filesystem::path& directory, bool bAsync)
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
            if (entry.path().has_extension() && IsFileConvertable(fileEextension)) {

                nDictionartFilesSize += entry.file_size();

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

int FolderConvert::ConverAllDirectories(const std::filesystem::path& directory, bool bAsync)
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
            if (entry.path().has_extension() && IsFileConvertable(fileEextension)) {

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
        std::cout << "***STOP*** error found: " << (*found)->GetStatus() << std::endl;
        return -1;
    }


    return 0;
}


