#pragma once

#include <filesystem>


#include <iostream>

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


using MediaInfoList = std::vector<MediaInformation>;
using AlbumList = std::vector<std::tuple<std::string, MediaInfoList>>;


//XXXX
using TrackInfoList = std::vector<std::tuple<std::wstring, long long, MediaInformation, std::string>>;
using EntryFileTuple = std::tuple <std::filesystem::directory_entry, TrackInfoList>;
using DirectoryContentEntryList = std::vector<EntryFileTuple>;


class AlbumCollection
{
	//static AlbumCollection&& CreateAlbumCollection(std::filesystem::path path);
public:

	AlbumCollection() = delete;
	AlbumCollection(std::filesystem::path& path, std::filesystem::path& outDirPath);

	bool LoadAlbumCollection();
	bool RefreshAlbumCollectionMediaInformation();

	bool SaveAlbumCollectionToJSONFile(std::filesystem::path& path);



private:

	MediaInformation ParseMediaInformationFromJSON(std::string jsonString);





	static AlbumList ReadAlbumCollectionFromJSON(std::filesystem::path& dirPath);

private:
	std::filesystem::path _AlbumCollectionDirPath;
	std::filesystem::path _OutDirPth;



	rapidjson::Document GetJSONDoc(std::filesystem::path path);
	std::filesystem::path GetMediaInfoFile(std::filesystem::path mediaFilePath);
	TrackInfoList LoadFolderNamesListRecrusive(std::filesystem::path path, int depth);

//	rapidjson::Document _MediaInfoDocument;
	DirectoryContentEntryList _AlbumList;
};

