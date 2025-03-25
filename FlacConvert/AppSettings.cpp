#include "AppSettings.h"
#include "AlbumCollection.h"

constexpr const char* OUTPUT_PATH = "\\\\?\\R:\\tmp\\24";

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
    doc.AddMember("OutputPath", rapidjson::Value(OutputPath.c_str(), allocator), allocator);

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
        mediaArray.PushBack(mediaObj, allocator);
    }
    doc.AddMember("MediaDirectoryList", mediaArray, allocator);

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
void AppSettingsJson::loadFromFile(const std::string& filename) {
    
    std::ifstream ifs(filename);
    if (!ifs) {
        std::cerr << "Error: Cannot open file for reading: " << filename << std::endl;
        return;
    }

    rapidjson::IStreamWrapper isw(ifs);
    rapidjson::Document doc;
    doc.ParseStream(isw);

    if (doc.HasParseError()) {
        std::cerr << "Error: Failed to parse JSON file." << std::endl;
        return;
    }

    if (doc.HasMember("Version") && doc["Version"].IsString()) {
        Version = doc["Version"].GetString();
    }
    if (doc.HasMember("OutputPath") && doc["OutputPath"].IsString()) {
        OutputPath = doc["OutputPath"].GetString();
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
            MediaDirectoryList.push_back(media);
        }
    }
}


//
//std::string AppSettingsJson::toJsonString() const {
//
//    Document doc;
//    doc.SetObject();
//    Document::AllocatorType& allocator = doc.GetAllocator();
//
//    // Add 'name' field
//    doc.AddMember("Output", Value(OutputPath.c_str(), allocator), allocator);
//
//    // Add 'elements' array
//    Value elementsArray(kArrayType);
//    for (const auto& elem : MediaDirectoryList) {
//        Value obj(kObjectType);
//        obj.AddMember("IsActive", elem.isActive, allocator);
//        obj.AddMember("NediaPath", Value(elem.mediaPath.c_str(), allocator), allocator);
//        obj.AddMember("ResultPath", Value(elem.resultPath.c_str(), allocator), allocator);
//        elementsArray.PushBack(obj, allocator);
//    }
//
//    doc.AddMember("Folder List", elementsArray, allocator);
//
//    // Convert JSON to string
//    StringBuffer buffer;
//    Writer<StringBuffer> writer(buffer);
//    doc.Accept(writer);
//
//    return buffer.GetString();
//}
//
//// Function to save JSON to a file
//void AppSettingsJson::saveToFile(const std::string& filename) const {
//    std::ofstream file(filename);
//    if (file.is_open()) {
//        file << toJsonString();
//        file.close();
//    }
//}
//
//void AppSettings::LoadAppSettings()
//{
// //   AppSettingsJson appConfigData;
//
//    //rapidjson::Document mediaDoc;
//    //mediaDoc.SetObject();
//
//    //for (auto mediaElement : appConfigData.MediaDirectoryList)
//    //{
//    //    rapidjson::Value trackMediaArray(rapidjson::kArrayType);
//    //}
//
//
//
//    //for (auto [albumPath, resultFileName] : appConfigData.MediaDirectoryList)
//    //{
//    //    //Album tracks list holder 
//    //    rapidjson::Value trackMediaArray(rapidjson::kArrayType);
//
//    //    for (auto [trackName, size, mediaInfo, mediaInfoString] : trackList)
//    //    {
//    //        std::filesystem::path trackPath = albumPath.path() / std::filesystem::path(trackName);
//
//    //        auto hasExtension = trackPath.has_extension();
//    //        auto fileEextension = trackPath.extension();
//    //        // std::wstring entryPath{ trackPath.wstring() };
//    //        if (trackPath.has_extension() && (fileEextension == ".flac" || fileEextension == ".mp3")) {
//
//    //            rapidjson::Document trackDoc;
//    //            trackDoc.Parse(mediaInfoString.c_str());
//    //            if (trackDoc.HasParseError()) {
//    //                std::cerr << "Error parsing JSON: " << trackDoc.GetParseError() << std::endl;
//    //            }
//    //            else
//    //            {
//    //                Value valueCopy;
//    //                valueCopy.CopyFrom(trackDoc["format"], mediaDoc.GetAllocator());
//    //                trackMediaArray.PushBack(valueCopy, mediaDoc.GetAllocator());
//    //            }
//    //        }
//    //    }
//
//    //    if (trackMediaArray.Size() > 0)
//    //    {
//    //        try
//    //        {
//    //            //track list exists add album
//    //            std::string name = albumPath.path().generic_string();
//    //            Value key(name.c_str(), mediaDoc.GetAllocator());
//    //            mediaDoc.AddMember(key, trackMediaArray, mediaDoc.GetAllocator());
//    //        }
//    //        catch (...)
//    //        {
//    //            int i = 0;
//    //        }
//    //    }
//    //}
//
//
//    //if (fs::exists(path)) {
//    //    std::error_code ec;
//    //    if (fs::remove(path, ec)) {
//    //    }
//    //}
//
//    //StringBuffer buffer;
//    //Writer<StringBuffer> writer(buffer);
//    //mediaDoc.Accept(writer);
//    //const char* json = buffer.GetString();
//
//    //// Save the JSON string to a file
//    //std::ofstream file(path);
//    //if (file.is_open()) {
//    //    file << json;
//    //    file.close();
//
//    //    //std::wcout << std::endl << std::format(L"====> Document saved to: {}", path.generic_wstring()) << std::endl;
//    //}
//    //else {
//    //    std::cerr << "Unable to open file for writing" << std::endl;
//    //    return false;
//    //}
//
//    //return true;
//}

