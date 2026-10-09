#include "FolderConvert.h"
#include "MediaConvertionTask.h"
#include "MediaConvertionAsyncTask.h"

#include <iostream>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <chrono>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <set>
#include "CommonUtils.h"
#include "MediaInformation.h"
#include "MediaTrack.h"
#include <spdlog/spdlog.h>

using namespace std;

namespace fs = std::filesystem;

namespace
{
bool samePathComponent(const fs::path& left, const fs::path& right)
{
#ifdef _WIN32
    const auto& leftValue = left.native();
    const auto& rightValue = right.native();
    return CompareStringOrdinal(
        leftValue.c_str(), static_cast<int>(leftValue.size()),
        rightValue.c_str(), static_cast<int>(rightValue.size()), TRUE) == CSTR_EQUAL;
#else
    return left == right;
#endif
}

bool isSameOrChildPath(const fs::path& candidate, const fs::path& base)
{
    auto candidatePart = candidate.begin();
    auto basePart = base.begin();
    for (; basePart != base.end(); ++basePart, ++candidatePart) {
        if (candidatePart == candidate.end()) {
            return false;
        }
        if (!samePathComponent(*candidatePart, *basePart)) {
            return false;
        }
    }
    return true;
}

struct CaseInsensitivePathLess
{
    bool operator()(const fs::path& left, const fs::path& right) const
    {
#ifdef _WIN32
        const auto& leftValue = left.native();
        const auto& rightValue = right.native();
        return CompareStringOrdinal(
            leftValue.c_str(), static_cast<int>(leftValue.size()),
            rightValue.c_str(), static_cast<int>(rightValue.size()), TRUE) == CSTR_LESS_THAN;
#else
        return left < right;
#endif
    }
};

fs::path makeTemporaryOutputPath(const fs::path& targetPath, std::uint64_t sequence)
{
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
    const auto processId = _getpid();
#else
    const auto processId = getpid();
#endif
    fs::path filename = fs::path(".afc-convert-" + std::to_string(processId) + "-" +
        std::to_string(timestamp) + "-" + std::to_string(sequence));
    filename += targetPath.extension();
    return targetPath.parent_path() / filename;
}
}




int FolderConvert::ScanAudioFiles(ScanInfo& scanInfo, const std::filesystem::path& directory, bool bAsync)
{
    // Recursively process all subdirectories within the specified directory
    std::filesystem::directory_iterator directoryIT;
    try {
        if (!fs::exists(directory) || !fs::is_directory(directory)) {
            spdlog::error("Error: Directory does not exist or is not valid: {}",
                CommonUtils::path_to_utf8(directory));
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
        spdlog::error("ScanAudioFiles: Filesystem error: {}", e.what());

        return -1;
    }

    return 0;
}

int FolderConvert::ConvertAudioFolderToOutput(
    const fs::path& inputDirectory,
    const fs::path& outputDirectory,
    bool overwrite)
{
    std::error_code ec;
    const fs::path input = fs::weakly_canonical(inputDirectory, ec);
    if (ec || !fs::is_directory(input, ec) || ec) {
        spdlog::error("Input is not a readable directory: {}", CommonUtils::path_to_utf8(inputDirectory));
        return -1;
    }

    const fs::path output = fs::weakly_canonical(outputDirectory, ec);
    if (ec) {
        spdlog::error("Cannot resolve output directory '{}': {}", CommonUtils::path_to_utf8(outputDirectory), ec.message());
        return -1;
    }
    if (isSameOrChildPath(output, input)) {
        spdlog::error("Output directory must not be the input directory or one of its descendants.");
        return -1;
    }

    ScanInfo scanInfo{};
    if (ScanAudioFiles(scanInfo, input) != 0) {
        return -1;
    }
    if (scanInfo.mediaList.empty()) {
        spdlog::warn("No convertible audio files found under '{}'.", CommonUtils::path_to_utf8(input));
        return 0;
    }

    struct Conversion {
        fs::path source;
        fs::path target;
        fs::path temporary;
    };
    std::vector<Conversion> conversions;
    conversions.reserve(scanInfo.mediaList.size());
    std::set<fs::path, CaseInsensitivePathLess> plannedTargets;
    static std::atomic_uint64_t nextTemporaryId{ 0 };

    for (const auto& entry : scanInfo.mediaList) {
        const fs::path source = entry.path();
        const fs::path relativePath = fs::relative(source, input, ec);
        if (ec) {
            spdlog::error("Cannot resolve path relative to input directory: {}", CommonUtils::path_to_utf8(source));
            return -1;
        }

        fs::path target = output / relativePath;
        target.replace_extension(GetTargetFileType());
        const fs::path canonicalSource = fs::weakly_canonical(source, ec);
        if (ec) {
            spdlog::error("Cannot resolve source path '{}': {}", CommonUtils::path_to_utf8(source), ec.message());
            return -1;
        }
        const fs::path canonicalTarget = fs::weakly_canonical(target, ec);
        if (ec) {
            spdlog::error("Cannot resolve destination path '{}': {}", CommonUtils::path_to_utf8(target), ec.message());
            return -1;
        }
        if (canonicalSource == canonicalTarget) {
            spdlog::error("Refusing to overwrite a source file: {}", CommonUtils::path_to_utf8(source));
            return -1;
        }
        if (!plannedTargets.insert(canonicalTarget).second) {
            spdlog::error("Multiple source files map to the same destination: {}", CommonUtils::path_to_utf8(target));
            return -1;
        }
        if (!overwrite && fs::exists(target, ec)) {
            spdlog::error("Destination already exists: {}", CommonUtils::path_to_utf8(target));
            return -1;
        }
        if (ec) {
            spdlog::error("Cannot inspect destination '{}': {}", CommonUtils::path_to_utf8(target), ec.message());
            return -1;
        }

        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            spdlog::error("Cannot create output directory '{}': {}", CommonUtils::path_to_utf8(target.parent_path()), ec.message());
            return -1;
        }

        conversions.push_back({ source, target, makeTemporaryOutputPath(target, ++nextTemporaryId) });
    }

    for (const auto& conversion : conversions) {
        auto task = std::make_shared<MediaConvertionTask>(
            conversion.source, conversion.target, conversion.temporary, overwrite);
        if (task->Run() != 0 || task->PostRun() != 0) {
            return -1;
        }
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
                spdlog::error("***ERROR*** returned from ConverAudioFiles");
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
                
                spdlog::warn("---Skipping: {}", CommonUtils::utf8string_to_string(entry.path().u8string()));
            }
        }
        else {
            if (entry.is_directory()) {
				//spdlog::info("Skipped directory: {}", CommonUtils::utf8string_to_string(entry.path().u8string()));
            }
            else
            {
                spdlog::error("***Unknown entry: {}", CommonUtils::utf8string_to_string(entry.path().u8string()));
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
            spdlog::error("***Processing item error found: {}", item->GetStatus());
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
			spdlog::error("***ERROR*** Media convert error: {}", task->GetStatus());
		}
	}
	for (auto& task : tasksVector) {
		task->PostRun();
		if (task->GetStatus() != 0)
		{
			spdlog::error("***STOP*** error found: {}", task->GetStatus());
		}
	}

    for (auto& item : tasksVector)
    {
        if (item->GetStatus() != 0)
        {
            spdlog::error("***STOP***2 error found: {}", item->GetStatus());
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
