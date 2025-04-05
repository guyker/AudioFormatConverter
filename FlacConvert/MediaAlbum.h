#pragma once

#include <filesystem>
#include <future>

#include "MediaInformation.h"
#include "MediaTrack.h"

struct MediaAlbum
{
	std::filesystem::directory_entry path;
	std::vector<MediaTrack> trackList;
};


using SimilarDirectoryEntryList = std::vector<std::tuple <std::wstring, std::wstring>>;

using MediaLoadingFuture = std::future<std::tuple<FFprobeOutput, std::wstring>>;


using TrackInfoList = std::vector<MediaTrack>;


using DirectoryContentEntryList = std::vector<MediaAlbum>;

