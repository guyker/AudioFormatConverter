#pragma once

#include <string>
#include <filesystem>
#include <vector>


namespace fs = std::filesystem;

using FileList = std::vector<std::tuple<std::wstring, long long>>;
using EntryFileTuple = std::tuple <fs::directory_entry, FileList>;
using DirectoryContentEntryList = std::vector<EntryFileTuple>;

//using SimilarDirectoryEntryList = std::vector<std::tuple <fs::directory_entry, fs::directory_entry>>;
using SimilarDirectoryEntryList = std::vector<std::tuple <std::wstring, std::wstring>>;


constexpr int SimilarPercentageTriggerValue { 5 };


class FolderCompare
{

public:

	FileList GetFolderNamesList2(std::filesystem::path path, int depth = -1);
	std::vector<std::wstring> GetFolderNamesList(std::filesystem::path path, bool recrusive = false);

	void sort();
	void findDuplicates();

	std::vector<std::wstring> Compare(std::filesystem::path pathA, std::filesystem::path pathB);
	bool Compare(EntryFileTuple entry1, EntryFileTuple entry2);

	void FindDuplicationInGroup(DirectoryContentEntryList::iterator firstIt, DirectoryContentEntryList::iterator lastIt);
	void OpenDirectoryInExplorer(std::wstring dirName);

	DirectoryContentEntryList _fileList;
	SimilarDirectoryEntryList _SimilarDirectories;
	int _SimilarDirs{ 0 };

private:
};

