#pragma once

#include <string>
#include <filesystem>
#include <vector>


struct ScanInfo {
    long folders_count;

    long not_convertable_file_count;
    long not_regular_file_count;
    long file_with_no_extension_count;
    long convertable_file_count;


    long long convertable_files_size;
};


class FolderConvert
{
public:

    static int ConverAudioFiles(const std::filesystem::path& directory, const ScanInfo scanInfo, bool bAsync = false);
    static int ScanAudioFiles(ScanInfo& scanInfo, const std::filesystem::path& directory, bool bAsync = false);


    static bool IsFileConvertable(std::filesystem::path pathName);
    static bool IsFileAcceptedAudioFile(std::filesystem::path filePath);


private:

    static constexpr std::string_view GetTargetFileType() {
        return ".flac";
    }
   
    static std::wstring ToLower(const std::wstring& str);
};

