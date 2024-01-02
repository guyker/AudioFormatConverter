#pragma once

#if 0

#include <string>
#include <filesystem>
#include <vector>

#include "MediaInformation.h"

#include "rapidjson/rapidjson.h" 
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/stringbuffer.h"


namespace fs = std::filesystem;

using MediaInfoList = std::vector<MediaInformation>;
using AlbumList = std::vector<std::tuple<std::string, MediaInfoList>>;

using FileInfoList = std::vector<std::tuple<std::wstring, long long>> ;
//using FileList2 = std::vector<MediaInformation>;
using EntryFileTuple = std::tuple <fs::directory_entry, FileInfoList>;

//using FileInfoList = std::vector<MediaInformation>;
//using FileList = std::vector<std::tuple<std::wstring, long long>> ;
//using EntryFileTuple = std::tuple <fs::directory_entry, FileInfoList>;

using DirectoryContentEntryList = std::vector<EntryFileTuple>;

//using SimilarDirectoryEntryList = std::vector<std::tuple <fs::directory_entry, fs::directory_entry>>;
using SimilarDirectoryEntryList = std::vector<std::tuple <std::wstring, std::wstring>>;


constexpr int SimilarPercentageTriggerValue { 5 };
constexpr int MinNumberOfFilesInFolderToCompare { 4 };


class FolderCompare
{

public:
	FolderCompare();
	FileInfoList GetFolderNamesList2(std::filesystem::path path, int depth = -1);
	std::vector<std::wstring> GetFolderNamesList(std::filesystem::path path, bool recrusive = false);

	std::filesystem::path GetMediaInfoFile(std::filesystem::path path);
	rapidjson::Document GetJSONDoc(std::filesystem::path path);

	void SortByNumberOfTracks(AlbumList& albumList);
	AlbumList GetDuplicatedAlbums(AlbumList& albumList);
	void FindDuplicationInGroup(AlbumList& albumList, AlbumList::iterator firstIt, AlbumList::iterator lastIt);


	void sort();
	void findDuplicates_old();

	std::vector<std::wstring> Compare(std::filesystem::path pathA, std::filesystem::path pathB);
	bool Compare(EntryFileTuple entry1, EntryFileTuple entry2);

	void FindDuplicationInGroup_old(DirectoryContentEntryList::iterator firstIt, DirectoryContentEntryList::iterator lastIt);
	void OpenDirectoryInExplorer(std::wstring dirName);

	DirectoryContentEntryList _fileList;
	SimilarDirectoryEntryList _SimilarDirectories;
	int _SimilarDirs{ 0 };

	bool SaveMediaInfoDocumentToDB(std::filesystem::path path);
	bool SaveMediaInfoDocument(std::filesystem::path path);
	AlbumList LoadMediaInfoDocument(std::filesystem::path path);

	//rapidjson::Document::AllocatorType allocator;
	rapidjson::Document _MediaInfoDocument;
	//rapidjson::Value _AlbumMediaArray{ rapidjson::kArrayType };

private:
};

#endif

