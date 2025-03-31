
#include <format>

#include "AppSettings.h"
#include "AlbumCollection.h"

std::shared_ptr<AppSettingsJson> AppSettingsJson::AppSettingsInstance = nullptr;

//std::shared_ptr<AppSettingsJson> AppSettingsJson::GetDefaultSettings()
//{
//	std::shared_ptr<AppSettingsJson> appSettingPtr = std::make_shared<AppSettingsJson>();
//	appSettingPtr->Version = "1.0.0";
//	appSettingPtr->WorkingDirectory = OUTPUT_PATH;
//	appSettingPtr->DatabaseFileName = "all_albums.db";
//	appSettingPtr->FLACSettings.ffmpeg_exe_name = L"ffmpeg";
//	appSettingPtr->FLACSettings.ffmpeg_arguments = L"-c:v copy -sample_fmt s16 -ar 44100 -y -v warning -stats";
//	MediaDirectoryElement mediaElement;
//	mediaElement.isActive = true;
//	mediaElement.mediaPath = OUTPUT_PATH;
//	mediaElement.resultPath = OUTPUT_PATH;
//	appSettingPtr->MediaDirectoryList.push_back(mediaElement);
//	return appSettingPtr;
//}

std::shared_ptr<AppSettingsJson> AppSettingsJson::AppSetting()
{
    if (AppSettingsInstance != nullptr)
    {
		return AppSettingsInstance;
    }

    std::filesystem::path configPath;

    //Get configuration file path from current directory
    if (AppSettingsJson::DefaultConfigDirectory == nullptr || *AppSettingsJson::DefaultConfigDirectory == '\0')
    {
        std::filesystem::path currentPath = std::filesystem::current_path();
        configPath = currentPath / AppSettingsJson::DefaultConfigFileName;
    }
    else
    {
        configPath = fs::path(AppSettingsJson::DefaultConfigDirectory) / fs::path(AppSettingsJson::DefaultConfigFileName);
    }


    std::cout << "Loading configuration file: " << configPath << std::endl;

    bool isAppSettingLoaded = fs::exists(configPath);
    if (isAppSettingLoaded)
    {
        std::shared_ptr<AppSettingsJson> appSettingPtr = std::make_shared<AppSettingsJson>();
        isAppSettingLoaded = appSettingPtr->loadFromFile(configPath.string());
		if (isAppSettingLoaded)
        {
            AppSettingsInstance = appSettingPtr;
		}
        else
        {
            std::cerr << "Error: Failed to parse configuration file: " << configPath << std::endl;
        }
    }
    else
    {
        std::cout << "Configuration file not found - Generating default config file, please update settings in config file and run again" << std::endl;
    }

	if (!isAppSettingLoaded)
    {        
        std::cout << std::endl << std::format("Would you like to create a new configuration file with defaults under {} ? Y/N ", configPath.generic_string()) << std::endl;
        char input = getchar();
        switch (input)
        {
        case 'y':
        case 'Y':
        {
            auto defaultSettings = AppSettingsJson::GetDefaultSettings();
            auto str = defaultSettings.toJsonString();
            defaultSettings.saveToFile(configPath.string());
        }
            break;
        default:
            std::cout << "Ignored, please edit and fix " << configPath << std::endl;
            break;
        }

    }

    return AppSettingsInstance;
}


// Convert wide string to UTF-8 for JSON compatibility
std::string WideToUTF8(const std::wstring& wstr) {
    std::string str(wstr.begin(), wstr.end());
    return str;
}

// Convert UTF-8 string to wide string
std::wstring UTF8ToWide(const std::string& str) {
    return std::wstring(str.begin(), str.end());
}

// Convert object to JSON string
std::string AppSettingsJson::toJsonString() const {
    rapidjson::Document doc;
    doc.SetObject();
    rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();

    doc.AddMember("Version", rapidjson::Value(Version.c_str(), allocator), allocator);
    doc.AddMember("WorkingDirectory", rapidjson::Value(WorkingDirectory.c_str(), allocator), allocator);
    doc.AddMember("DatabaseFileName", rapidjson::Value(DatabaseFileName.c_str(), allocator), allocator);

    // Serialize FLACEncodingSettings
    rapidjson::Value flacObj(rapidjson::kObjectType);
    flacObj.AddMember("ffmpeg_exe_name", rapidjson::Value(WideToUTF8(FLACSettings.ffmpeg_exe_name).c_str(), allocator), allocator);
    flacObj.AddMember("ffmpeg_arguments", rapidjson::Value(WideToUTF8(FLACSettings.ffmpeg_arguments).c_str(), allocator), allocator);
    doc.AddMember("FLACSettings", flacObj, allocator);

    // Serialize MediaDirectoryList
    rapidjson::Value mediaArray(rapidjson::kArrayType);
    for (const auto& media : MediaDirectoryList) {
        rapidjson::Value mediaObj(rapidjson::kObjectType);
        mediaObj.AddMember("isActive", media.isActive, allocator);
        mediaObj.AddMember("mediaPath", rapidjson::Value(media.mediaPath.c_str(), allocator), allocator);
        mediaObj.AddMember("resultPath", rapidjson::Value(media.resultPath.c_str(), allocator), allocator);
        mediaObj.AddMember("dbPath", rapidjson::Value(media.dbPath.c_str(), allocator), allocator);
        mediaArray.PushBack(mediaObj, allocator);
    }
    doc.AddMember("MediaDirectoryList", mediaArray, allocator);


    doc.AddMember("MinMatchingTracksForDuplicate", MinMatchingTracksForDuplicate, allocator);
    doc.AddMember("SizeMatchPercentageThreshold", SizeMatchPercentageThreshold, allocator);


    // Convert document to string
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

// Save JSON to a file
void AppSettingsJson::saveToFile(const std::string& filename) const {
    std::ofstream ofs(filename);
    if (!ofs) {
        std::cerr << "Error: Cannot open file for writing: " << filename << std::endl;
        return;
    }

    rapidjson::OStreamWrapper osw(ofs);
    rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(osw);

    rapidjson::Document doc;
    doc.Parse(toJsonString().c_str());
    doc.Accept(writer);

    ofs.close();
}

// Load JSON from a file
bool AppSettingsJson::loadFromFile(const std::string& filename) {
    
    std::ifstream ifs(filename);
    if (!ifs) {
        std::cerr << "Error: Cannot open file for reading: " << filename << std::endl;
        return false;
    }

    rapidjson::IStreamWrapper isw(ifs);
    rapidjson::Document doc;
    doc.ParseStream(isw);

    if (doc.HasParseError()) {
        std::cerr << "Error: Failed to parse JSON file." << std::endl;
        return false;
    }

    if (doc.HasMember("Version") && doc["Version"].IsString()) {
        Version = doc["Version"].GetString();
    }
    
    if (doc.HasMember("WorkingDirectory") && doc["WorkingDirectory"].IsString()) {
        WorkingDirectory = doc["WorkingDirectory"].GetString();
    }
    if (doc.HasMember("DatabaseFileName") && doc["DatabaseFileName"].IsString()) {
        DatabaseFileName = doc["DatabaseFileName"].GetString();
    }

    if (doc.HasMember("FLACSettings") && doc["FLACSettings"].IsObject()) {
        const auto& flacObj = doc["FLACSettings"];
        if (flacObj.HasMember("ffmpeg_exe_name") && flacObj["ffmpeg_exe_name"].IsString()) {
            FLACSettings.ffmpeg_exe_name = UTF8ToWide(flacObj["ffmpeg_exe_name"].GetString());
        }
        if (flacObj.HasMember("ffmpeg_arguments") && flacObj["ffmpeg_arguments"].IsString()) {
            FLACSettings.ffmpeg_arguments = UTF8ToWide(flacObj["ffmpeg_arguments"].GetString());
        }
    }

    if (doc.HasMember("MediaDirectoryList") && doc["MediaDirectoryList"].IsArray()) {
        MediaDirectoryList.clear();
        for (const auto& mediaObj : doc["MediaDirectoryList"].GetArray()) {
            MediaDirectoryElement media{};
            if (mediaObj.HasMember("isActive") && mediaObj["isActive"].IsBool()) {
                media.isActive = mediaObj["isActive"].GetBool();
            }
            if (mediaObj.HasMember("mediaPath") && mediaObj["mediaPath"].IsString()) {
                media.mediaPath = mediaObj["mediaPath"].GetString();
            }
            if (mediaObj.HasMember("resultPath") && mediaObj["resultPath"].IsString()) {
                media.resultPath = mediaObj["resultPath"].GetString();
            }
            if (mediaObj.HasMember("dbPath") && mediaObj["dbPath"].IsString()) {
                media.dbPath = mediaObj["dbPath"].GetString();
            }
            MediaDirectoryList.push_back(media);
        }
    }

    if (doc.HasMember("MinMatchingTracksForDuplicate") && doc["MinMatchingTracksForDuplicate"].IsInt()) {
        MinMatchingTracksForDuplicate = doc["MinMatchingTracksForDuplicate"].GetInt();
    }
    if (doc.HasMember("SizeMatchPercentageThreshold") && doc["SizeMatchPercentageThreshold"].IsInt()) {
        SizeMatchPercentageThreshold = doc["SizeMatchPercentageThreshold"].GetInt();
    }

    return true;
}


