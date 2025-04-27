#pragma once

#include <filesystem>
#include <vector>
#include <tuple>
#include <codecvt>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <stdexcept>

// RapidJSON headers
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/prettywriter.h" // For formatted output

//#include "rapidjson/rapidjson.h" 
//#include "rapidjson/document.h"
//#include "rapidjson/istreamwrapper.h"
//#include "rapidjson/writer.h"
//#include "rapidjson/stringbuffer.h"
//#include "rapidjson/ostreamwrapper.h"
//#include "rapidjson/stringbuffer.h"
//
//
//#include "rapidjson/document.h"
//#include "rapidjson/writer.h"
//#include "rapidjson/stringbuffer.h"


namespace fs = std::filesystem;
using namespace rapidjson;

struct FLACEncodingSettings
{
	std::wstring ffmpeg_exe_name;
	std::wstring ffmpeg_convert_24bit_flac;
	std::wstring ffmpeg_get_metadata_tags;
};

struct MediaDirectoryElement
{
	bool isActive{ true };
	std::string mediaPath{};
	std::optional<std::string> mediaName{};

	std::string getMediaJsonPath(std::string outDir) const
	{
		std::string fileName = mediaName.value_or(std::string("data")) + ".json";
		std::filesystem::path path = std::filesystem::path(outDir) / (std::string("media_out_") + fileName);

		return path.string();
	}

	std::string getMediaDBPath(std::string outDir) const
	{
		std::string fileName = mediaName.value_or(std::string("data")) + ".db";
		std::filesystem::path path = std::filesystem::path(outDir) / (std::string("media_out_") + fileName);

		return path.string();
	}
};


struct AppSettingsJson
{
	static constexpr bool isCustomAppConfigPath = true;

	static constexpr const char* DefaultTMPDirectory = "c:\\tmp" ;
	static constexpr const char* DefaultOutDirectory = "c:\\tmp";
//	static constexpr const char* DefaultConfigDirectory = isCustomAppConfigPath ? DefaultOutDirectory : nullptr;

	static constexpr const char* PersistentFileName = "AppSettingsJsonPersistent.txt";
	static constexpr const char* DefaultConfigFileName = "config.json";
	static constexpr const char* DefaultDatabaseFileName = "all_albums.db";

	//APP Setting properties - START

	std::string Version{ "1.0.0" };
	
	std::string OutDirectory{};
	std::string DatabaseFileName{};

	bool UseAsyncFFmpegCalls { true };
	bool UseFFmpegLibraryAPI { true };
	int MinMatchingTracksForDuplicate{ 1 };
	int SizeMatchPercentageThreshold{ 3 }; // tracks are identical if the size difference is less than 3%
	int RecursionDirectorySearchDepth{ 10 }; // max depth for recursive search
	int AlbumsSplitThreshold{ 2000 }; // max number of album in each chunk (this help with memory usage and performance)

	FLACEncodingSettings FLACSettings{};
	std::vector<MediaDirectoryElement> MediaDirectoryList{};

	//APP Setting properties - END


	bool loadFromFile(const std::string& filename);
	void saveToFile(const std::string& filename) const;
	std::string toJsonString() const;


	static std::shared_ptr<AppSettingsJson> AppSetting();

	static AppSettingsJson GetDefaultSettings()
	{
		AppSettingsJson appSettingsJson
		{	
			"1.0.0",
			AppSettingsJson::DefaultOutDirectory,
			AppSettingsJson::DefaultDatabaseFileName,
			true, //UseAsyncFFmpegCalls
			true, //UseFFmpegLibraryAPI
			1, //MinMatchingTracksForDuplicate
			3, //SizeMatchPercentageThreshold
			10, //RecursionDirectorySearchDepth
			2000, //AlbumsSplitThreshold
			{
				 L"ffmpeg",
				 L"-c:v copy -sample_fmt s16 -ar 44100 -y -v warning -stats",
				 L"-c:v copy -sample_fmt s16 -ar 44100 -y -v warning -stats",				
			},
			{
				{true, "\\\\?\\R:\\tmp\\24\\flac", "flac"},
				{true, "\\\\?\\R:\\tmp\\24\\mp3", "mp3"}
			},
		};

		return appSettingsJson;
	}

private:
	static std::shared_ptr<AppSettingsJson> AppSettingsInstance;

};

