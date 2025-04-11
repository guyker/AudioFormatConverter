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
	std::wstring ffmpeg_arguments;
	std::wstring ffmpeg_arguments2;
	std::wstring ffmpeg_arguments3;
};

struct MediaDirectoryElement
{
	bool isActive{ true };
	std::string mediaPath{};
	std::string resultPath{};
	std::string dbPath{};
};

struct AppSettingsJson
{
	static constexpr bool isCustomAppConfigPath = true;

	static constexpr const char* DefaultWorkingDirectory = "\\\\?\\R:\\tmp\\24";
	static constexpr const char* DefaultConfigDirectory = isCustomAppConfigPath ? DefaultWorkingDirectory : nullptr;

	static constexpr const char* DefaultConfigFileName = "config.json";
	static constexpr const char* DefaultDatabaseFileName = "all_albums.db";

	//APP Setting properties - START

	std::string Version{ "1.0.0" };
	
	std::string WorkingDirectory{};
	std::string DatabaseFileName{};

	int MinMatchingTracksForDuplicate{ 1 };
	int SizeMatchPercentageThreshold{ 3 }; // tracks are identical if the size difference is less than 3%
	int RecursionDirectorySearchDepth{ 10 }; // max depth for recursive search

	FLACEncodingSettings FLACSettings{};
	std::vector<MediaDirectoryElement> MediaDirectoryList{};

	//APP Setting properties - END


	bool loadFromFile(const std::string& filename);
	void saveToFile(const std::string& filename) const;
	std::string toJsonString() const;


	static std::shared_ptr<AppSettingsJson> AppSetting();

	static AppSettingsJson GetDefaultSettings()
	{
		auto eerr = new MediaDirectoryElement{ true, "\\\\?\\R:\\tmp\\24", "MediaResult_flac_result.json" };

		AppSettingsJson appSettingsJson
		{	
			"1.0.0",
			AppSettingsJson::DefaultWorkingDirectory,
			AppSettingsJson::DefaultDatabaseFileName,
			1, //MinMatchingTracksForDuplicate
			3, //SizeMatchPercentageThreshold
			10, //RecursionDirectorySearchDepth
			//FLAC settings
			{
				 L"ffmpeg",
				 L"-c:v copy -sample_fmt s16 -ar 44100 -y -v warning -stats"
			},

			//MediaDirectoryList
			{
				{true, "\\\\?\\R:\\tmp\\24\\flac", "\\\\?\\R:\\tmp\\24\\MediaResult_flac.json", "\\\\?\\R:\\tmp\\24\\MediaResult_flac.db"},
				{true, "\\\\?\\R:\\tmp\\24\\mp3", "\\\\?\\R:\\tmp\\24\\MediaResult_mp3.json", "\\\\?\\R:\\tmp\\24\\MediaResult_mp3.db"}
				//    {"\\\\?\\M:\\tmp\\24_rdy", outputPath / "MediaResult_24_rdy.json"},
				//    {"\\\\?\\M:\\music\\Classical\\Albums\\24bit", outputPath / "MediaResult_classical_24.json"},
				//    ////{"\\\\?\\M:\\music\\Classical\\Albums\\XRCD", outputPath / "MediaResult_classical_album_XRCD_.json"},
				//    {"\\\\?\\M:\\music\\Classical\\Albums\\flac", outputPath / "MediaResult_classical_album_flac_.json"},
				//    //{"\\\\?\\M:\\music\\Classical\\Albums\\mp3", outputPath / "MediaResult_classical_album_mp3_.json"},
				//    {"\\\\?\\M:\\music\\Classical\\Albums\\AlbumSets_MultiCover", outputPath / "MediaResult_classical_AlbumSets_MultiCover.json"},
				//    //{"\\\\?\\M:\\music\\Classical\\Albums\\AlbumSets_OneCover", outputPath / "MediaResult_AlbumSets_OneCover.json"},
			},
		};

		return appSettingsJson;
	}

private:
	static std::shared_ptr<AppSettingsJson> AppSettingsInstance;

};

