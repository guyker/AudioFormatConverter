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
#include <optional>
#include <stdexcept>

#include "CommonUtils.h"
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

	std::string outputName() const
	{
		std::string name = mediaName.value_or(std::string("data"));
		constexpr std::size_t maxUtf8Bytes = 180;
		if (name.size() > maxUtf8Bytes) {
			std::size_t length = maxUtf8Bytes;
			while (length > 0 &&
				(static_cast<unsigned char>(name[length]) & 0xC0) == 0x80) {
				--length;
			}
			name.resize(length);
		}
		return name;
	}

	CommonUtils::Generator<std::filesystem::path> getMediaJsonPathCo(std::filesystem::path outDir) const
	{
		int index = 0;
		while (++index < 1000) {
			auto path = getMediaJsonPath(outDir, index);
			if (fs::exists(path)) {
				co_yield path;
			}
			else
			{
				co_return;
			}
		}

		co_return;
	}

	std::filesystem::path getMediaJsonPath(const std::filesystem::path& outDir, int count) const
	{
		std::string fileName = outputName();
		if (count > 0)
		{
			fileName += std::string("_") + std::to_string(count);
		}
		fileName += ".json";
		return outDir / CommonUtils::path_from_utf8(mediaName.value_or("")) /
			CommonUtils::path_from_utf8(std::string("media_out_") + fileName);
	}

	std::filesystem::path getMediaDBPath(const std::filesystem::path& outDir) const
	{
		std::string fileName = outputName() + ".db";
		return outDir / CommonUtils::path_from_utf8(mediaName.value_or("")) /
			CommonUtils::path_from_utf8(std::string("media_out_") + fileName);
	}
};



struct AppSettingsJson
{
	static constexpr bool isCustomAppConfigPath = true;

	static constexpr const char* DefaultTMPDirectory = "c:\\tmp" ;
	static std::string DefaultOutDirectory()
	{
#ifdef _WIN32
		return "C:\\tmp";
#else
		return CommonUtils::utf8string_to_string(std::filesystem::temp_directory_path().u8string());
#endif
	}

	static constexpr const char* PersistentFileName = "AppSettingsJsonPersistent.txt";
	static constexpr const char* DefaultConfigFileName = "config.json";
	static constexpr const char* DefaultDatabaseFileName = "all_albums.db";

	//APP Setting properties - START

	std::string Version{ "1.0.0" };
	
	std::string OutDirectory{};
	std::string DatabaseFileName{};

	int LogLevel { 1 };
	bool UseAsyncFFmpegCalls { true };
	bool UseFFmpegLibraryAPI { true };
	bool ExtraAudioQualityMetrics{ false };
	int MinMatchingTracksForDuplicate{ 1 };
	int SizeMatchPercentageThreshold{ 3 }; // tracks are identical if the size difference is less than 3%
	int RecursionDirectorySearchDepth{ 10 }; // max depth for recursive search
	int AlbumsSplitThreshold{ 2000 }; // max number of album in each chunk (this help with memory usage and performance)

	FLACEncodingSettings FLACSettings{};
	std::vector<MediaDirectoryElement> MediaDirectoryList{};

	//APP Setting properties - END


	bool loadFromFile(const std::filesystem::path& filename);
	void saveToFile(const std::filesystem::path& filename) const;
	std::string toJsonString() const;


	static std::shared_ptr<AppSettingsJson> AppSetting();
	static std::shared_ptr<AppSettingsJson> LoadForCommandLine(
		const std::optional<std::filesystem::path>& explicitConfigPath,
		std::string& errorMessage);

	static AppSettingsJson GetDefaultSettings()
	{
		AppSettingsJson appSettingsJson
		{	
			"1.0.0",
			AppSettingsJson::DefaultOutDirectory(),
			AppSettingsJson::DefaultDatabaseFileName,
			1,
			true, //UseAsyncFFmpegCalls
			true, //UseFFmpegLibraryAPI
			false, //ExtraAudioQualityMetrics
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
