#pragma once

#include <string>
#include <filesystem>
#include <vector>

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

class FolderConvert
{
public:

    static int ScanAudioFiles(ScanInfo& scanInfo, const std::filesystem::path& directory, bool bAsync = false);
    
    static int ConverAudioFolder(const std::filesystem::path& directory, const ScanInfo scanInfo, bool bAsync);
    //static int ConverAudioFolder(const std::filesystem::path& directory, const ScanInfo scanInfo, bool bAsync = false);
    static int ConverAudioFolder2(const std::filesystem::path& directory, const ScanInfo scanInfo, bool bAsync = false);

private:

    static constexpr std::string_view GetTargetFileType() {
        return ".flac";
    }   
};
