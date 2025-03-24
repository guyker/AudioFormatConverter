#pragma once

#include <filesystem>
#include <vector>
#include <tuple>

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


namespace fs = std::filesystem;
using namespace rapidjson;


struct MediaDirectoryElement
{
	bool isActive{ true };
	std::string mediaPath{};
	std::string resultPath{};
};

struct AppSettingsJson
{
	std::string Version {"1.0.0"};
	std::string OutputPath{};
	std::vector<MediaDirectoryElement> MediaDirectoryList{};



	void saveToFile(const std::string& filename) const;
	std::string toJson() const;


	static AppSettingsJson GetDefaultSettings()
	{
		auto eerr = new MediaDirectoryElement{ true, "\\\\?\\R:\\tmp\\24", "MediaResult_flac_result.json" };

		AppSettingsJson appSettingsJson
		{	
			"1.0.0",
			"\\\\?\\R:\\tmp\\24",
			{
				{true, "\\\\?\\R:\\tmp\\24", "MediaResult_flac_result.json"},
				{true, "\\\\?\\R:\\tmp\\24", "MediaResult_24_rdy.json"}
			}
		};

		return appSettingsJson;
	}
};

class AppSettings
{
public:

	static void LoadAppSettings();
};
