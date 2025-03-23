#pragma once

#include <string>
#include <filesystem>
#include <vector>

class FolderConvert
{
public:

    static int ConverAudioFiles(const std::filesystem::path& directory, bool bAsync = false);
    static int ScanAudioFiles(std::tuple<int, long, long long>& scanInfo, const std::filesystem::path& directory, bool bAsync = false);


    static bool IsFileConvertable(std::filesystem::path pathName);
    static bool IsFileAcceptedAudioFile(std::filesystem::path filePath);


private:

    static constexpr std::string_view GetTargetFileType() {
        return ".flac";
    }
   
    static std::wstring ToLower(const std::wstring& str);
};

