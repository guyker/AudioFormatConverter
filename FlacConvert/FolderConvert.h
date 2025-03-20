#pragma once

#include <string>
#include <filesystem>
#include <vector>

class FolderConvert
{
public:

    int ConverAllDirectories(const std::filesystem::path& directory, bool bAsync = false);
    int ScanAudioFiles(std::tuple<int, long, long long>& scanInfo, const std::filesystem::path& directory, bool bAsync = false);


    static bool IsFileConvertable(std::filesystem::path pathName);



private:
    std::string const _TargetFileType{ ".flac" };

    static std::wstring ToLower(const std::wstring& str);
};

