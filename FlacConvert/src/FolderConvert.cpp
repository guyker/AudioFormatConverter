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
    bool overwrite,
    ConversionOptions options)
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
    std::vector<fs::directory_entry> companions;
    if (options.target == AudioTarget::Mp3) {
        std::erase_if(scanInfo.mediaList, [](const fs::directory_entry& entry) {
            return MediaTrack::FileExtension(entry.path()) != u8".flac";
        });
        // Audio files of other formats are not companion documents or artwork.
        const std::set<std::u8string> audioExtensions{ u8".mp3", u8".aac", u8".ogg", u8".oga",
            u8".opus", u8".wma", u8".aif", u8".aiff", u8".alac", u8".mp4", u8".mka", u8".ac3",
            u8".m4b", u8".m4p", u8".mp2", u8".mp1", u8".au", u8".snd", u8".aifc", u8".caf",
            u8".tak", u8".tta", u8".shn", u8".amr" };
        try {
            for (const auto& entry : fs::recursive_directory_iterator(input)) {
                if (entry.is_regular_file() && !MediaTrack::IsFileConvertable(entry.path()) &&
                    !audioExtensions.contains(MediaTrack::FileExtension(entry.path()))) {
                    companions.push_back(entry);
                }
            }
        }
        catch (const fs::filesystem_error& error) {
            spdlog::error("Cannot enumerate companion files: {}", error.what());
            return -1;
        }
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
    std::vector<Conversion> copies;
    conversions.reserve(scanInfo.mediaList.size());
    std::set<fs::path, CaseInsensitivePathLess> plannedTargets;
    static std::atomic_uint64_t nextTemporaryId{ 0 };
    const auto preflight = [&](const fs::path& target, const fs::path& source) {
        const fs::path canonicalTarget = fs::weakly_canonical(target, ec);
        if (ec || !plannedTargets.insert(canonicalTarget).second) {
            spdlog::error("Cannot use destination (unresolvable or colliding): {}", CommonUtils::path_to_utf8(target));
            return false;
        }
        const bool exists = fs::exists(target, ec);
        if (ec || (!overwrite && exists) || (exists && !fs::is_regular_file(target, ec))) {
            spdlog::error("Cannot use destination (existing, non-file or unreadable): {}", CommonUtils::path_to_utf8(target));
            return false;
        }
        if (exists && fs::equivalent(source, target, ec)) {
            spdlog::error("Refusing to overwrite a source file: {}", CommonUtils::path_to_utf8(target));
            return false;
        }
        if (ec || !isSameOrChildPath(canonicalTarget, output) ||
            isSameOrChildPath(canonicalTarget, input)) {
            spdlog::error("Unsafe destination: {}", CommonUtils::path_to_utf8(target));
            return false;
        }
        return true;
    };

    for (const auto& entry : scanInfo.mediaList) {
        const fs::path source = entry.path();
        const fs::path relativePath = fs::relative(source, input, ec);
        if (ec) {
            spdlog::error("Cannot resolve path relative to input directory: {}", CommonUtils::path_to_utf8(source));
            return -1;
        }

        fs::path target = output / relativePath;
        target.replace_extension(options.target == AudioTarget::Mp3 ? ".mp3" : ".flac");
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
        fs::path sidecar = target;
        sidecar += ".flac-metadata";
        if (!preflight(target, source) ||
            (options.target == AudioTarget::Mp3 && !preflight(sidecar, source))) {
            return -1;
        }

        conversions.push_back({ source, target, makeTemporaryOutputPath(target, ++nextTemporaryId) });
    }
    for (const auto& entry : companions) {
        const auto relative = fs::relative(entry.path(), input, ec);
        if (ec) {
            spdlog::error("Cannot resolve companion path: {}", CommonUtils::path_to_utf8(entry.path()));
            return -1;
        }
        const auto target = output / relative;
        if (!preflight(target, entry.path())) {
            return -1;
        }
        copies.push_back({ entry.path(), target, makeTemporaryOutputPath(target, ++nextTemporaryId) });
    }

    for (const auto& conversion : conversions) {
        fs::create_directories(conversion.target.parent_path(), ec);
        if (ec) {
            spdlog::error("Cannot create output directory: {}", ec.message());
            return -1;
        }
        auto task = std::make_shared<MediaConvertionTask>(
            conversion.source, conversion.target, conversion.temporary, overwrite, options);
        if (task->Run() != 0 || task->PostRun() != 0) {
            return -1;
        }
    }

    for (const auto& copy : copies) {
        fs::create_directories(copy.target.parent_path(), ec);
        if (ec || !fs::copy_file(copy.source, copy.temporary, fs::copy_options::none, ec)) {
            spdlog::error("Cannot copy companion '{}': {}", CommonUtils::path_to_utf8(copy.source), ec.message());
            std::error_code cleanupError;
            fs::remove(copy.temporary, cleanupError);
            if (cleanupError) {
                spdlog::warn("Cannot clean temporary companion '{}': {}",
                    CommonUtils::path_to_utf8(copy.temporary), cleanupError.message());
            }
            return -1;
        }
        if (!MediaConvertionTask::InstallOutputFile(copy.temporary, copy.target, overwrite)) {
            spdlog::error("Companion copy remains recoverable at '{}'.", CommonUtils::path_to_utf8(copy.temporary));
            return -1;
        }
        spdlog::info("Copied companion file unchanged: {}", CommonUtils::path_to_utf8(copy.target));
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
