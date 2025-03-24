#include "AppSettings.h"
#include "AlbumCollection.h"

constexpr const char* OUTPUT_PATH = "\\\\?\\R:\\tmp\\24";


std::string AppSettingsJson::toJson() const {

    Document doc;
    doc.SetObject();
    Document::AllocatorType& allocator = doc.GetAllocator();

    // Add 'name' field
    doc.AddMember("Output", Value(OutputPath.c_str(), allocator), allocator);

    // Add 'elements' array
    Value elementsArray(kArrayType);
    for (const auto& elem : MediaDirectoryList) {
        Value obj(kObjectType);
        obj.AddMember("IsActive", elem.isActive, allocator);
        obj.AddMember("NediaPath", Value(elem.mediaPath.c_str(), allocator), allocator);
        obj.AddMember("ResultPath", Value(elem.resultPath.c_str(), allocator), allocator);
        elementsArray.PushBack(obj, allocator);
    }

    doc.AddMember("Folder List", elementsArray, allocator);

    // Convert JSON to string
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

// Function to save JSON to a file
void AppSettingsJson::saveToFile(const std::string& filename) const {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << toJson();
        file.close();
    }
}

void AppSettings::LoadAppSettings()
{
    AppSettingsJson appConfigData;

    rapidjson::Document mediaDoc;
    mediaDoc.SetObject();

    for (auto mediaElement : appConfigData.MediaDirectoryList)
    {
        rapidjson::Value trackMediaArray(rapidjson::kArrayType);
    }



    //for (auto [albumPath, resultFileName] : appConfigData.MediaDirectoryList)
    //{
    //    //Album tracks list holder 
    //    rapidjson::Value trackMediaArray(rapidjson::kArrayType);

    //    for (auto [trackName, size, mediaInfo, mediaInfoString] : trackList)
    //    {
    //        std::filesystem::path trackPath = albumPath.path() / std::filesystem::path(trackName);

    //        auto hasExtension = trackPath.has_extension();
    //        auto fileEextension = trackPath.extension();
    //        // std::wstring entryPath{ trackPath.wstring() };
    //        if (trackPath.has_extension() && (fileEextension == ".flac" || fileEextension == ".mp3")) {

    //            rapidjson::Document trackDoc;
    //            trackDoc.Parse(mediaInfoString.c_str());
    //            if (trackDoc.HasParseError()) {
    //                std::cerr << "Error parsing JSON: " << trackDoc.GetParseError() << std::endl;
    //            }
    //            else
    //            {
    //                Value valueCopy;
    //                valueCopy.CopyFrom(trackDoc["format"], mediaDoc.GetAllocator());
    //                trackMediaArray.PushBack(valueCopy, mediaDoc.GetAllocator());
    //            }
    //        }
    //    }

    //    if (trackMediaArray.Size() > 0)
    //    {
    //        try
    //        {
    //            //track list exists add album
    //            std::string name = albumPath.path().generic_string();
    //            Value key(name.c_str(), mediaDoc.GetAllocator());
    //            mediaDoc.AddMember(key, trackMediaArray, mediaDoc.GetAllocator());
    //        }
    //        catch (...)
    //        {
    //            int i = 0;
    //        }
    //    }
    //}


    //if (fs::exists(path)) {
    //    std::error_code ec;
    //    if (fs::remove(path, ec)) {
    //    }
    //}

    //StringBuffer buffer;
    //Writer<StringBuffer> writer(buffer);
    //mediaDoc.Accept(writer);
    //const char* json = buffer.GetString();

    //// Save the JSON string to a file
    //std::ofstream file(path);
    //if (file.is_open()) {
    //    file << json;
    //    file.close();

    //    //std::wcout << std::endl << std::format(L"====> Document saved to: {}", path.generic_wstring()) << std::endl;
    //}
    //else {
    //    std::cerr << "Unable to open file for writing" << std::endl;
    //    return false;
    //}

    //return true;
}

