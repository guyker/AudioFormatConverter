#pragma once

#include <string>
#include <filesystem>
#include <vector>


namespace fs = std::filesystem;
using EntryFileTuple = std::tuple <fs::directory_entry, std::vector<std::wstring>>;
using EntryFileList = std::vector<std::tuple <fs::directory_entry, std::vector<std::wstring>>>;

class FolderCompare
{

public:

	std::vector<std::wstring> GetFolderNamesList2(std::filesystem::path path, int depth = -1);
	std::vector<std::wstring> GetFolderNamesList(std::filesystem::path path, bool recrusive = false);

	void sort();
	void findDuplicates();

	std::vector<std::wstring> Compare(std::filesystem::path pathA, std::filesystem::path pathB);

	EntryFileList _fileList;
	int _SimilarDirs{ 0 };
};

