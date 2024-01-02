#pragma once

#include <string>
#include <filesystem>
#include <vector>

class FolderConvert
{
public:

    int ConverAllDirectories(const std::filesystem::path& directory, bool bAsync = false);
    int GetFilesData(std::tuple<int, long, long>& scanInfo, const std::filesystem::path& directory, bool bAsync = false);


    static bool IsFileConvertable(std::filesystem::path pathName);
    static bool IsFileCollectable(std::filesystem::path pathName);

    //std::string const _SourceFileType1{ ".flac" };
    //std::string const _SourceFileType2{ ".dsf" };
    //std::string const _SourceFileType3{ ".dff" };
    //std::string const _SourceFileType4{ ".dsd" };
    //std::string const _SourceFileType5{ ".wv" };
    //std::string const _SourceFileType6{ ".wav" };
    //std::string const _SourceFileType7{ ".m2ts" };

    std::string const _TargetFileType{ ".flac" };

    int _nDictionary{ 0 };
};

