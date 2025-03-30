#pragma once

#include <filesystem>
#include <future>

#include "MediaInformation.h"
#include "MediaTrack.h"

struct AlbumInfo
{
	std::filesystem::directory_entry path;
	std::vector<MediaTrack> trackList;
};


using SimilarDirectoryEntryList = std::vector<std::tuple <std::wstring, std::wstring>>;

using MediaLoadingFuture = std::future<std::tuple<MediaInformation, std::wstring>>;


//using TrackInfoList = std::vector<std::tuple<std::filesystem::path, long long, MediaInformation, std::string>>;
using TrackInfoList = std::vector<MediaTrack>;

//using EntryFileTuple = std::tuple <std::filesystem::directory_entry, TrackInfoList>;
//using EntryFileTuple = AlbumInfo;

using DirectoryContentEntryList = std::vector<AlbumInfo>;

