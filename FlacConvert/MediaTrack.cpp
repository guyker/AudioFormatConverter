#include "MediaTrack.h"



MediaInformation MediaTrack::ParseMediaTrack(std::wstring jsonString)
{

    MediaInformation mediaInfo;

    rapidjson::Document doc;
    std::string utf8Json = CommonUtils::wstringToUtf8(jsonString);
    doc.Parse(utf8Json.c_str());

    if (doc.HasParseError()) {
        std::cerr << "Error parsing JSON: " << doc.GetParseError() << std::endl;

        return mediaInfo;
    }


    if (doc.IsObject() && doc.HasMember("format"))
    {
        return MediaInformation{ MediaTrack::ParseMediaInformation(doc["format"]) };
    }

    return mediaInfo;
}
