#pragma once

#include <string>
#include <string_view>
#include <cstddef>
#include <filesystem>
#include <vector>
#include "ConversionOptions.h"

namespace fs = std::filesystem;

struct ScanInfo {
    long folders_count;

    long not_convertable_file_count;
    long not_regular_file_count;
    long file_with_no_extension_count;
    long convertable_file_count;

    long long convertable_files_size;

    std::vector<fs::directory_entry> mediaList;
};

struct PlannedFileConversion {
    fs::path source;
    fs::path target;
    fs::path temporary;
};

struct FolderConversionPlan {
    fs::path input;
    fs::path output;
    bool overwrite{ false };
    ConversionOptions options;
    std::vector<PlannedFileConversion> audio;
    std::vector<PlannedFileConversion> companions;
    std::vector<fs::path> existingTargets;
    std::vector<fs::path> collisions;
};

struct ConversionProgress {
    std::size_t converted{ 0 };
    std::size_t copied{ 0 };
};

class FolderConvert
{
public:

    static int ScanAudioFiles(ScanInfo& scanInfo, const std::filesystem::path& directory, bool bAsync = false);
    static int ConvertAudioFolderToOutput(
        const std::filesystem::path& inputDirectory,
        const std::filesystem::path& outputDirectory,
        bool overwrite,
        ConversionOptions options = {});
    static int PrepareConversion(
        const fs::path& input, const fs::path& output, bool overwrite,
        ConversionOptions options, FolderConversionPlan& plan);
    // Optional counters accumulate across plans, only after successful installation.
    static int ExecuteConversion(const FolderConversionPlan& plan, ConversionProgress* progress = nullptr);
    
    static int ConverAudioFolder(const std::filesystem::path& directory, const ScanInfo scanInfo, bool bAsync);
    //static int ConverAudioFolder(const std::filesystem::path& directory, const ScanInfo scanInfo, bool bAsync = false);
    static int ConverAudioFolder2(const std::filesystem::path& directory, const ScanInfo scanInfo, bool bAsync = false);

private:

    static constexpr std::string_view GetTargetFileType() {
        return ".flac";
    }   
};
