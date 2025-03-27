#pragma once

#include <set>
#include <string>
#include <filesystem>
#include <vector>
#include <tuple>

#include "CommonUtils.h"

namespace fs = std::filesystem;


struct MediaInformation
{
	std::string filename;
	int nb_streams;
	int nb_programs;
	int nb_stream_groups;

	std::string format_name;
	std::string format_long_name;
	std::string start_time;
	long duration;
	std::string size;
	std::string bit_rate;

	int probe_score;

	struct tags_t
	{
		std::string album;
		std::string disc;
		std::string album_dynamic_range;
		std::string dynamic_range;
		std::string artist;
		std::string album_artist;
		std::string composer;
		std::string copyright;

		std::string label;
		std::string year;

		std::string comment;
		std::string genre;
		std::string publisher;
		std::string title;
		std::string track;
		std::string track_total;
		std::string date;
		std::string encoder;
		std::string encoded_by;
		std::string organization;

	} tags;
};

struct TrackInfo
{
	std::filesystem::path trackPath;
	long long fileSize;
	MediaInformation formatInfo;
	std::string mediaInfoString;


	static bool IsFileConvertable(std::filesystem::path filePath)
	{
		//L".mp3", L".wav", L".flac", L".aac", L".ogg", L".wma", L".m4a"
		static std::set<std::wstring> fileExtensionList = { L".flac", L".ape", L".dsf", L".dff", L".dsd", L".wv", L".wav", L".m2ts" , L".m4a" };
		auto fileExtension = CommonUtils::ToLower(filePath.extension().wstring());

		return filePath.has_extension() && (fileExtensionList.find(filePath.extension().wstring()) != fileExtensionList.end());
	}

	static bool IsFileAcceptedAudioFile(std::filesystem::path filePath)
	{
		static std::set<std::wstring> fileExtensionList = { L".flac", L".mp3" };
		auto fileExtension = CommonUtils::ToLower(filePath.extension().wstring());

		return filePath.has_extension() && (fileExtensionList.find(filePath.extension().wstring()) != fileExtensionList.end());
	}

	static bool IsValidMedia(const fs::directory_entry& entry)
	{
		if (entry.is_regular_file() && entry.path().has_extension())
		{
			auto fileEextension = entry.path().extension();
			if (TrackInfo::IsFileAcceptedAudioFile(entry.path()))
			{
				return true;
			}
		}

		return false;
	}


};

struct AlbumInfo
{
	std::filesystem::directory_entry path;
	std::vector<TrackInfo> trackList;
};


using SimilarDirectoryEntryList = std::vector<std::tuple <std::wstring, std::wstring>>;

using MediaLoadingFuture = std::future<std::tuple<MediaInformation, std::string>>;


//using TrackInfoList = std::vector<std::tuple<std::filesystem::path, long long, MediaInformation, std::string>>;
using TrackInfoList = std::vector<TrackInfo>;

//using EntryFileTuple = std::tuple <std::filesystem::directory_entry, TrackInfoList>;
//using EntryFileTuple = AlbumInfo;

using DirectoryContentEntryList = std::vector<AlbumInfo>;

