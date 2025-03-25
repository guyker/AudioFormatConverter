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
	std::wstring ffmpeg_exe_name{ L"ffmpeg" };
	std::wstring ffmpeg_arguments{ L"-c:v copy -sample_fmt s16 -ar 44100 -y -v warning -stats" };
};

struct MediaDirectoryElement
{
	bool isActive{ true };
	std::string mediaPath{};
	std::string resultPath{};
};

struct AppSettingsJson
{
	static constexpr bool isCustomAppConfigPath = true;

	std::string Version{ "1.0.0" };
	
	static constexpr const char* DefaultWorkingDirectory = "\\\\?\\R:\\tmp\\24";
	static constexpr const char* DefaultConfigDirectory = isCustomAppConfigPath ? DefaultWorkingDirectory : nullptr;

	static constexpr const char* DefaultConfigFileName = "config.json";
	static constexpr const char* DefaultDatabaseFileName = "all_albums.db";

	std::string WorkingDirectory{};
	std::string DatabaseFileName{};
	std::vector<MediaDirectoryElement> MediaDirectoryList{};

	FLACEncodingSettings FLACSettings{};


	void loadFromFile(const std::string& filename);
	void saveToFile(const std::string& filename) const;
	std::string toJsonString() const;




	static AppSettingsJson GetDefaultSettings()
	{
		auto eerr = new MediaDirectoryElement{ true, "\\\\?\\R:\\tmp\\24", "MediaResult_flac_result.json" };

		AppSettingsJson appSettingsJson
		{	
			"1.0.0",
			AppSettingsJson::DefaultWorkingDirectory,
			AppSettingsJson::DefaultDatabaseFileName,
			{
				{true, "\\\\?\\R:\\tmp\\24\\flac_tracks1", "\\\\?\\R:\\tmp\\24\\MediaResult_flac_result1.json"},
				{true, "\\\\?\\R:\\tmp\\24\\flac_tracks2", "\\\\?\\R:\\tmp\\24\\MediaResult_flac_result2.json"}
				//    {"\\\\?\\M:\\tmp\\24_rdy", outputPath / "MediaResult_24_rdy.json"},
				//    {"\\\\?\\M:\\music\\Classical\\Albums\\24bit", outputPath / "MediaResult_classical_24.json"},
				//    ////{"\\\\?\\M:\\music\\Classical\\Albums\\XRCD", outputPath / "MediaResult_classical_album_XRCD_.json"},
				//    {"\\\\?\\M:\\music\\Classical\\Albums\\flac", outputPath / "MediaResult_classical_album_flac_.json"},
				//    //{"\\\\?\\M:\\music\\Classical\\Albums\\mp3", outputPath / "MediaResult_classical_album_mp3_.json"},
				//    {"\\\\?\\M:\\music\\Classical\\Albums\\AlbumSets_MultiCover", outputPath / "MediaResult_classical_AlbumSets_MultiCover.json"},
				//    //{"\\\\?\\M:\\music\\Classical\\Albums\\AlbumSets_OneCover", outputPath / "MediaResult_AlbumSets_OneCover.json"},
			}
		};

		return appSettingsJson;
	}

private:

};

