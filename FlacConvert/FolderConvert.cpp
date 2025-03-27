#include "FolderConvert.h"
#include "MediaConvertionTask.h"
#include "MediaConvertionAsyncTask.h"

#include <iostream>
#include <algorithm>
#include <set>
#include "CommonUtils.h"
#include "MediaInformation.h"
#include "MediaTrack.h"

using namespace std;

namespace fs = std::filesystem;




int FolderConvert::ScanAudioFiles(ScanInfo& scanInfo, const std::filesystem::path& directory, bool bAsync)
{
    std::wstring entryPath{ directory.wstring() };
    //std::wcout << std::endl << L"Scanning Dictionary: " << entryPath << std::endl;

    // Recursively process all subdirectories within the specified directory
    std::filesystem::directory_iterator directoryIT;
    try {
        if (!fs::exists(directory) || !fs::is_directory(directory)) {
            std::cerr << directory << " - Error: Directory does not exist or is not valid.\n";
            return -1;
        }

        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_directory()) {
				scanInfo.folders_count++;
            }
            else if (!entry.is_regular_file()) {
                scanInfo.not_regular_file_count++;
            }
            else if (!entry.path().has_extension()) {
                scanInfo.file_with_no_extension_count++;
            }
            else if (!MediaTrack::IsFileConvertable(entry.path())) {
                scanInfo.not_convertable_file_count++;
            }
            else
            {
                scanInfo.convertable_files_size += entry.file_size();
                scanInfo.convertable_file_count += 1;

                scanInfo.mediaList.push_back(entry);
            }
        }
    }
    catch (const fs::filesystem_error& e) {
        std::cerr << "ScanAudioFiles: Filesystem error: " << e.what() << std::endl;

        return -1;
    }

    return 0;
}


int FolderConvert::ConverAudioFolder(const std::filesystem::path& directory, const ScanInfo scanInfo, bool bAsync)
{
    std::vector<std::shared_ptr<MediaConvertionTask>> tasksVector;

    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        if (entry.is_directory()) {
            int ret = ConverAudioFolder(entry.path(), scanInfo, bAsync);
            if (ret == -1) {
                std::cout << "***ERROR*** returned from ConverAudioFiles" << std::endl;
                return -1;
            }
        }
    }


    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {

        if (entry.is_regular_file() && entry.path().has_extension())
        {
            if (entry.path().has_extension() && MediaTrack::IsFileConvertable(entry.path())) {

                fs::path targetPath = entry.path();
                targetPath.replace_extension(GetTargetFileType());

                fs::path targetTMPPath = entry.path();
                fs::path targetTMPFileName = targetTMPPath.stem() += fs::path{ "_TMP" };
                targetTMPPath.replace_filename(targetTMPFileName);
                targetTMPPath += GetTargetFileType();

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


















int FolderConvert::ConverAudioFolder2(const std::filesystem::path& directory, const ScanInfo scanInfo, bool bAsync)
{
    std::vector<std::shared_ptr<MediaConvertionTask>> tasksVector;

    std::vector<MediaConvertionTask> AlbumConvertTask;

    std::vector<std::shared_ptr<std::vector<MediaConvertionTask>>> tasksVector2;




    for (auto& mediaFile : scanInfo.mediaList) {
        fs::path targetPath = mediaFile.path();
        targetPath.replace_extension(GetTargetFileType());

        fs::path targetTMPPath = mediaFile.path();
        fs::path targetTMPFileName = targetTMPPath.stem() += fs::path{ "_TMP" };
        targetTMPPath.replace_filename(targetTMPFileName);
        targetTMPPath += GetTargetFileType();

        //     fs::path targetTMPPath = _TMPDirectory += entry.path().stem() += fs::path{ "_TMP" } += fs::path{ _TargetFileType };

        std::shared_ptr<MediaConvertionTask> pTask = bAsync ? std::make_shared<MediaConvertionAsyncTask>(mediaFile.path(), targetPath, targetTMPPath) : std::make_shared<MediaConvertionTask>(mediaFile.path(), targetPath, targetTMPPath);
        tasksVector.push_back(pTask);
    }



    //process all files in current directory
    int processStatus{ 0 };
	for (auto& task : tasksVector) {
		task->Run();
		if (task->GetStatus() == -1) {
			std::cerr << "***ERROR*** Media convert error: " << task->GetStatus() << std::endl;
		}
	}
    for (auto& task : tasksVector) {
		task->PostRun();
        if (task->GetStatus() != 0)
        {
            std::cerr << "***STOP*** error found: " << task->GetStatus() << std::endl;
        }
    }

    for (auto& item : tasksVector)
    {
        if (item->GetStatus() != 0)
        {
            std::cerr << "***STOP***2 error found: " << item->GetStatus() << std::endl;
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



