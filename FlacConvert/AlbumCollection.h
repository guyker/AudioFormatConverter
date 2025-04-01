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

	
private:

	static std::tuple<MediaInformation, std::wstring> ReadMediaInfoFromFile(std::filesystem::path mediaFilePath);

	static std::wstring ExtractMediaInformationFromFile(std::filesystem::path mediaFilePath, std::filesystem::path outFile);

	

	//sort and find duplications
	SimilarDirectoryEntryList FindDuplicationInGroup(DirectoryContentEntryList& albumList, DirectoryContentEntryList::iterator firstIt, DirectoryContentEntryList::iterator lastIt);

	//private Helpers
	TrackInfoList LoadAlbumFromCurrentFolder(std::filesystem::path path, int depth);

	
	DirectoryContentEntryList _AlbumList;
};

