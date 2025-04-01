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

#include "CommonUtils.h"

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
#include <set>
#include <string>

#include <filesystem>
#include "MediaAlbum.h"

class AlbumCollection
{
public:

	AlbumCollection() = default;

	// load albums from directory
	bool LoadAlbumCollection(std::filesystem::path albumCollectionDirPath);


	// load albums from a Json file
	bool LoadAlbumCollectionFromJSON(std::filesystem::path dirPath, bool bBasicDataOnly = false);
	
	//Save album list and metadata to JSON file1
	bool SaveAlbumCollectionToJSONFile(std::filesystem::path path);


	//Export albums tracks information to a JSON file
	size_t SaveToJson(bool bAsync = true);
	
	//Export albums tracks information to a Database
	bool SaveToDatabase(std::filesystem::path path);



	//compare
	void SortByNumberOfTracks();
	SimilarDirectoryEntryList FindDuplicatedAlbums();
	//SimilarDirectoryEntryList& GetDuplicatedAlbums();

	
private:
	//static Helpers
	static rapidjson::Document GetJSONDoc(std::filesystem::path path);

	//uses: CreateMediaInfoFile - to create json file
	//      ParseMediaInfoFromJsonFile - to convert json file to info object
	static std::tuple<MediaInformation, std::wstring> GetMediaInfoFromMediaFile(std::filesystem::path mediaFilePath);

	static std::wstring CreateMediaInfoFile(std::filesystem::path mediaFilePath, std::filesystem::path outFile);

	//static std::string GetMediaInfoJsonString(std::filesystem::path mediaFilePath, std::filesystem::path outFile);
	//static MediaInformation ParseMediaInfoFromJsonFile(std::filesystem::path jsonMediaInfoPath);
	


	//sort and find duplications
	SimilarDirectoryEntryList FindDuplicationInGroup(DirectoryContentEntryList& albumList, DirectoryContentEntryList::iterator firstIt, DirectoryContentEntryList::iterator lastIt);
	//void OpenDirectoryInExplorer(std::wstring dirName);


	//private Helpers
	TrackInfoList LoadAlbumFromCurrentFolder(std::filesystem::path path, int depth);

	 

//	std::filesystem::path _AlbumCollectionDirPath;
//	std::filesystem::path _OutDirPth;

	DirectoryContentEntryList _AlbumList;
//	SimilarDirectoryEntryList _DuplicatedAlbumList;
};

