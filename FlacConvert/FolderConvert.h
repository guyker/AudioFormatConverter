#pragma once

#include <string>
#include <filesystem>
#include <vector>

class FolderConvert
{
public:

    int ConverAllDirectories(const std::filesystem::path& directory, bool bAsync = false);
    int GetFilesData(std::tuple<int, long, long long>& scanInfo, const std::filesystem::path& directory, bool bAsync = false);


    static bool IsFileConvertable(std::filesystem::path pathName);
    static bool IsFileCollectable(std::filesystem::path pathName);


    std::string const _TargetFileType{ ".flac" };

    int _nDictionary{ 0 };
};

