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
	std::string OutputPath{};
	std::vector<MediaDirectoryElement> MediaDirectoryList{};



	void saveToFile(const std::string& filename) const;
	std::string toJson() const;
};

class AppSettings
{
public:

	static void LoadAppSettings();
};
