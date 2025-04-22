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

	// load albums from directory / Directly from media files FLAC/MP3
	// bIncludeMetadata = true - load media metadata / Calling LoadAllMetadata
	MediaAlbumListPtr LoadAlbumCollection(std::filesystem::path albumCollectionDirPath, bool bIncludeMetadata = false);

	//Import/update Media Metadata from the current loaded Album Collection (get JSON from media files)
	//Normally this function should be called after LoadAlbumCollection
	size_t LoadAllMetadata(std::shared_ptr<DirectoryContentEntryList> albumListPtr, bool bAsync = true);


	//Export albums tracks information to a Database
	bool SaveToSQLDatabase(std::shared_ptr<DirectoryContentEntryList> albumListPtr, std::filesystem::path path);

	// load albums from a Json file
	std::shared_ptr<DirectoryContentEntryList> RestoreAlbumCollectionFromJSON(std::filesystem::path dirPath);
	//Save album list and metadata to JSON file
	bool SaveAlbumsAsJSON(std::shared_ptr<DirectoryContentEntryList> albumListPtr, std::filesystem::path path);



	//compare
	void SortByNumberOfTracks(std::shared_ptr<std::vector<MediaAlbum>>, bool ascending = true);
	SimilarDirectoryEntryList FindDuplicatedAlbums(std::shared_ptr<DirectoryContentEntryList> albumListPtr);

	
private:
		
	//sort and find duplications
	SimilarDirectoryEntryList FindDuplicationInGroup(std::shared_ptr<DirectoryContentEntryList> albumListPtr, const std::vector<DirectoryContentEntryList::const_iterator>& group);

	//private Helpers
	std::pair<long long, long long> GetNumberOfItemsInFolder(std::filesystem::path path, int depth);

	
	//DirectoryContentEntryList _AlbumList;
};

