#pragma once

#include <string>
#include <filesystem>
#include <vector>


namespace fs = std::filesystem;
//using EntryFileTuple = std::tuple <fs::directory_entry, std::vector<std::wstring>>;
using DirectoryContentEntryList = std::vector<std::tuple <fs::directory_entry, std::vector<std::wstring>>>;

using SimilarDirectoryEntryList = std::vector<std::tuple <fs::directory_entry, fs::directory_entry>>;

class FolderCompare
{

public:

	std::vector<std::wstring> GetFolderNamesList2(std::filesystem::path path, int depth = -1);
	std::vector<std::wstring> GetFolderNamesList(std::filesystem::path path, bool recrusive = false);

	void sort();
	void findDuplicates();

	std::vector<std::wstring> Compare(std::filesystem::path pathA, std::filesystem::path pathB);

	DirectoryContentEntryList _fileList;
	SimilarDirectoryEntryList _SimilarDirectories;
	int _SimilarDirs{ 0 };
};

