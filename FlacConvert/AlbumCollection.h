#pragma once

#include <filesystem>


#include <iostream>

#include <future>
#include <vector>
#include <algorithm>
#include <ranges>
#include <functional>

#include "MediaInformation.h"

#include <forward_list>
#include <iterator>
#include <vector>

#include <fstream> 
#include <iostream> 



#include "rapidjson/rapidjson.h" 
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/stringbuffer.h"


#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"



using SimilarDirectoryEntryList = std::vector<std::tuple <std::wstring, std::wstring>>;

using MediaLoadingFuture = std::future<std::tuple<MediaInformation, std::string>>;
using TrackInfoList = std::vector<std::tuple<std::filesystem::path, long long, MediaInformation, std::string>>;
using EntryFileTuple = std::tuple <std::filesystem::directory_entry, TrackInfoList>;
using DirectoryContentEntryList = std::vector<EntryFileTuple>;


constexpr int SimilarPercentageTriggerValue{ 3 };
constexpr int MinNumberOfFilesInFolderToCompare{ 4 };

const std::string TMP_MEDIA_JSON_FILE_NAME{"media_info.json"};

class AlbumCollection
{
public:

	AlbumCollection() = default;
	AlbumCollection(DirectoryContentEntryList const & albumList);
	AlbumCollection(DirectoryContentEntryList&& albumList);


	//Load album list or ddirectory structure of the albums from the file system
	bool LoadAlbumCollection(std::filesystem::path albumCollectionDirPath);
	bool LoadAlbumCollectionWithMetadata(std::filesystem::path albumCollectionDirPath, std::filesystem::path& outDirPath);

	//Load/Reload album tracks metadata information
	size_t RefreshAlbumCollectionMediaInformation(bool bAsync = true);

	//Save album list and metadata to JSON file1
	bool SaveAlbumCollectionToJSONFile(std::filesystem::path path);


	void Clear();

	static DirectoryContentEntryList LoadAlbumCollectionFromJSON(std::filesystem::path& dirPath, bool bBasicDataOnly = false);


	//compare
	void SortByNumberOfTracks();
	SimilarDirectoryEntryList FindDuplicatedAlbums();
	//SimilarDirectoryEntryList& GetDuplicatedAlbums();

	//DB
	bool SaveMediaInfoDocumentToDB(std::filesystem::path path);
	
private:
	//static Helpers
	static MediaInformation ParseMediaInformation(auto formatTag);
	static rapidjson::Document GetJSONDoc(std::filesystem::path path);

	//uses: CreateMediaInfoFile - to create json file
	//      ParseMediaInfoFromJsonFile - to convert json file to info object
	static std::tuple<MediaInformation, std::string> GetMediaInfoFromMediaFile(std::filesystem::path mediaFilePath);

	static std::filesystem::path CreateMediaInfoFile(std::filesystem::path mediaFilePath, std::filesystem::path outFile);

	//static std::string GetMediaInfoJsonString(std::filesystem::path mediaFilePath, std::filesystem::path outFile);
	static MediaInformation ParseMediaInfoFromJsonFile(std::filesystem::path jsonMediaInfoPath);
	static MediaInformation ParseMediaInfoFromJsonString(std::string jsonString);


	//sort and find duplications
	SimilarDirectoryEntryList FindDuplicationInGroup(DirectoryContentEntryList& albumList, DirectoryContentEntryList::iterator firstIt, DirectoryContentEntryList::iterator lastIt);
	//void OpenDirectoryInExplorer(std::wstring dirName);


	//private Helpers
	TrackInfoList LoadFolderNamesListRecrusive(std::filesystem::path path, int depth);

	 

//	std::filesystem::path _AlbumCollectionDirPath;
//	std::filesystem::path _OutDirPth;

	DirectoryContentEntryList _AlbumList;
//	SimilarDirectoryEntryList _DuplicatedAlbumList;
};

