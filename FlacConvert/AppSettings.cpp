#include "AppSettings.h"
#include "AlbumCollection.h"
#include "PlatformUtils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <format>
#include <string>
#include <system_error>

#include <spdlog/spdlog.h>

std::shared_ptr<AppSettingsJson> AppSettingsJson::AppSettingsInstance = nullptr;

// ----------------------------------------------------------------------
// Helpers for persistence (just the directory, not the full filename)
// ----------------------------------------------------------------------

namespace fs = std::filesystem;

void saveLastDir(const fs::path& lastDir, const fs::path& persistentFile) {
    std::ofstream out(persistentFile, std::ios::trunc);
    if (!out) {
        spdlog::error("Failed to write persistent file '{}',", persistentFile.string());
    }
    else {
        out << lastDir.string();
    }
}

std::optional<fs::path> loadLastDir(const fs::path& persistentFile) {
    std::ifstream in(persistentFile);
    if (!in) return std::nullopt;
    std::string dir;
    std::getline(in, dir);
    if (dir.empty()) return std::nullopt;
    return fs::path(dir);
}

// ----------------------------------------------------------------------
// Prompt the user once for a directory containing config.json
// ----------------------------------------------------------------------

fs::path promptForConfigDir(const fs::path& persistentFile) {
    while (true) {
        std::cout << "Please enter the directory containing '"
            << AppSettingsJson::DefaultConfigFileName << "': "
            << std::flush;

        std::string input;
        std::getline(std::cin, input);

        if (input.empty()) {
            spdlog::warn("No input provided; try again.");
            continue;
        }

        fs::path dir{ input };
        fs::path candidate = dir / AppSettingsJson::DefaultConfigFileName;

        std::error_code ec;
        if (fs::exists(candidate, ec) && !ec) {
            // save only the directory for next time
            saveLastDir(dir, persistentFile);
            return dir;
        }

        spdlog::error("Config file not found at '{}'; please retry.", candidate.string());
    }
}

// ----------------------------------------------------------------------
// Locate the config directory (possibly via persisted choice)
// ----------------------------------------------------------------------

fs::path findConfigDir() {
    fs::path persistentFile = fs::current_path() / AppSettingsJson::PersistentFileName;

    // 1) Try persisted directory
    if (auto last = loadLastDir(persistentFile)) {
        fs::path candidate = *last / AppSettingsJson::DefaultConfigFileName;
        std::error_code ec;
        if (fs::exists(candidate, ec) && !ec) {
            spdlog::info("Using persisted config directory '{}',", last->string());
            return *last;
        }
        spdlog::warn(
            "Persisted directory '{}' no longer contains '{}', falling back to prompt",
            last->string(), AppSettingsJson::DefaultConfigFileName);
    }

    // 2) Ask the user until we get a valid one
    return promptForConfigDir(persistentFile);
}

void save_string(const std::string& value, const fs::path& path) {
    std::ofstream out{ path, std::ios::binary };
    out << value;
}

std::string load_string(const fs::path& path) {
    std::ifstream in{ path, std::ios::binary };
    return std::string{ std::istreambuf_iterator<char>(in), {} };
}

std::shared_ptr<AppSettingsJson> AppSettingsJson::AppSetting() {
    if (AppSettingsInstance != nullptr) {
        return AppSettingsInstance;
    }

    fs::path configDir = findConfigDir();
    fs::path configPath = configDir / AppSettingsJson::DefaultConfigFileName;

    bool isAppSettingLoaded = fs::exists(configPath);
    if (isAppSettingLoaded) {
        auto appSettingPtr = std::make_shared<AppSettingsJson>();
        isAppSettingLoaded = appSettingPtr->loadFromFile(configPath.string());
        if (isAppSettingLoaded) {
            AppSettingsInstance = appSettingPtr;
        }
        else {
            std::cerr << "Error: Failed to parse configuration file: " << configPath << std::endl;
        }
    }
    else {
        std::cout << "Configuration file not found - Generating default config file, please update settings in config file and run again" << std::endl;
    }

    if (!isAppSettingLoaded) {
        std::cout << std::endl << std::format("Would you like to create a new configuration file with defaults under {}? Y/N ", configPath.generic_string()) << std::endl;
        char input = std::toupper(getchar());
        if (input == 'Y') {
            auto defaultSettings = AppSettingsJson::GetDefaultSettings();
            defaultSettings.saveToFile(configPath.string());
            AppSettingsInstance = std::make_shared<AppSettingsJson>(defaultSettings);
        }
        else {
            std::cout << "Ignored, please edit and fix " << configPath << std::endl;
        }
    }

    return AppSettingsInstance;
}

// Convert UTF-8 string to wide string
std::wstring UTF8ToWide(const std::string& str) {
    return std::wstring(str.begin(), str.end());
}

// Convert object to JSON string
std::string AppSettingsJson::toJsonString() const {
    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();

    doc.AddMember("Version", rapidjson::Value(Version.c_str(), allocator), allocator);
    doc.AddMember("OutDirectory", rapidjson::Value(OutDirectory.c_str(), allocator), allocator);
    doc.AddMember("DatabaseFileName", rapidjson::Value(DatabaseFileName.c_str(), allocator), allocator);

    rapidjson::Value flacObj(rapidjson::kObjectType);
    flacObj.AddMember("ffmpeg_exe_name", rapidjson::Value(PlatformUtils::WideToUTF8(FLACSettings.ffmpeg_exe_name).c_str(), allocator), allocator);
    flacObj.AddMember("ffmpeg_convert_24bit_flac", rapidjson::Value(PlatformUtils::WideToUTF8(FLACSettings.ffmpeg_convert_24bit_flac).c_str(), allocator), allocator);
    flacObj.AddMember("ffmpeg_get_metadata_tags", rapidjson::Value(PlatformUtils::WideToUTF8(FLACSettings.ffmpeg_get_metadata_tags).c_str(), allocator), allocator);
    doc.AddMember("FLACSettings", flacObj, allocator);

    rapidjson::Value mediaArray(rapidjson::kArrayType);
    for (const auto& media : MediaDirectoryList) {
        rapidjson::Value mediaObj(rapidjson::kObjectType);
        mediaObj.AddMember("isActive", media.isActive, allocator);
        mediaObj.AddMember("mediaPath", rapidjson::Value(media.mediaPath.c_str(), allocator), allocator);
		if (media.mediaName.has_value()) {
			std::string mediaName = media.mediaName.value();
			mediaObj.AddMember("mediaName", rapidjson::Value(mediaName.c_str(), allocator), allocator);
		}
		else {
			mediaObj.AddMember("mediaName", rapidjson::Value().SetNull(), allocator);
		}
        mediaArray.PushBack(mediaObj, allocator);
    }
    doc.AddMember("MediaDirectoryList", mediaArray, allocator);

    doc.AddMember("UseAsyncFFmpegCalls", UseAsyncFFmpegCalls, allocator);
    doc.AddMember("UseFFmpegLibraryAPI", UseFFmpegLibraryAPI, allocator);
    doc.AddMember("MinMatchingTracksForDuplicate", MinMatchingTracksForDuplicate, allocator);
    doc.AddMember("SizeMatchPercentageThreshold", SizeMatchPercentageThreshold, allocator);
    doc.AddMember("RecursionDirectorySearchDepth", RecursionDirectorySearchDepth, allocator);
    doc.AddMember("AlbumsSplitThreshold", AlbumsSplitThreshold, allocator);

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
    if (doc.HasMember("OutDirectory") && doc["OutDirectory"].IsString()) {
        OutDirectory = doc["OutDirectory"].GetString();
    }
    if (doc.HasMember("DatabaseFileName") && doc["DatabaseFileName"].IsString()) {
        DatabaseFileName = doc["DatabaseFileName"].GetString();
    }

    if (doc.HasMember("FLACSettings") && doc["FLACSettings"].IsObject()) {
        const auto& flacObj = doc["FLACSettings"];
        if (flacObj.HasMember("ffmpeg_exe_name") && flacObj["ffmpeg_exe_name"].IsString()) {
            FLACSettings.ffmpeg_exe_name = UTF8ToWide(flacObj["ffmpeg_exe_name"].GetString());
        }
        if (flacObj.HasMember("ffmpeg_convert_24bit_flac") && flacObj["ffmpeg_convert_24bit_flac"].IsString()) {
            FLACSettings.ffmpeg_convert_24bit_flac = UTF8ToWide(flacObj["ffmpeg_convert_24bit_flac"].GetString());
        }
        if (flacObj.HasMember("ffmpeg_get_metadata_tags") && flacObj["ffmpeg_get_metadata_tags"].IsString()) {
            FLACSettings.ffmpeg_get_metadata_tags = UTF8ToWide(flacObj["ffmpeg_get_metadata_tags"].GetString());
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
            if (mediaObj.HasMember("mediaName") && mediaObj["mediaName"].IsString()) {
                media.mediaName = mediaObj["mediaName"].GetString();
            }
			else {
                try
                {
                    std::filesystem::path path(media.mediaPath);
                    if (std::filesystem::is_regular_file(path)) {
                        path = path.parent_path();
                    }
                    media.mediaName = path.filename().string();
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    spdlog::error("Error getting media name: {}", e.what());
                    media.mediaName = std::nullopt;
                }
			}


            MediaDirectoryList.push_back(media);
        }
    }

    if (doc.HasMember("UseAsyncFFmpegCalls") && doc["UseAsyncFFmpegCalls"].IsBool()) {
        UseAsyncFFmpegCalls = doc["UseAsyncFFmpegCalls"].GetBool();
    }
    if (doc.HasMember("UseFFmpegLibraryAPI") && doc["UseFFmpegLibraryAPI"].IsBool()) {
        UseFFmpegLibraryAPI = doc["UseFFmpegLibraryAPI"].GetBool();
    }
    if (doc.HasMember("MinMatchingTracksForDuplicate") && doc["MinMatchingTracksForDuplicate"].IsInt()) {
        MinMatchingTracksForDuplicate = doc["MinMatchingTracksForDuplicate"].GetInt();
    }
    if (doc.HasMember("SizeMatchPercentageThreshold") && doc["SizeMatchPercentageThreshold"].IsInt()) {
        SizeMatchPercentageThreshold = doc["SizeMatchPercentageThreshold"].GetInt();
    }
    if (doc.HasMember("RecursionDirectorySearchDepth") && doc["RecursionDirectorySearchDepth"].IsInt()) {
        RecursionDirectorySearchDepth = doc["RecursionDirectorySearchDepth"].GetInt();
    }
    if (doc.HasMember("AlbumsSplitThreshold") && doc["AlbumsSplitThreshold"].IsInt()) {
        AlbumsSplitThreshold = doc["AlbumsSplitThreshold"].GetInt();
    }
    
    return true;
}
