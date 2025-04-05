#include "MediaTrack.h"



#include <iostream>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <array>
#include <cstdlib>
#include <cwchar>

#ifdef _WIN32
#include <windows.h>
// On Windows, use _wpopen/_pclose which accept wide strings.
#define popen _wpopen
#define pclose _pclose




std::wstring getAudioMetadataJSON(const std::wstring& filePath) {
    std::wstring command = L"ffprobe -v quiet -print_format json -show_format -show_streams -show_chapters \"" + filePath + L"\"";

#ifdef _WIN32
    command += L" 2>&1"; // Redirect stderr to stdout (Windows)
    FILE* pipe = _wpopen(command.c_str(), L"r");
#else
    command += L" 2>&1"; // Redirect stderr to stdout (Linux/macOS)
    FILE* pipe = popen(std::string(command.begin(), command.end()).c_str(), "r");
#endif

    if (!pipe) {
        std::wcerr << L"Failed to run ffprobe!" << std::endl;
        return L"";
    }

    std::ostringstream result;
    char buffer[2024];

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result << buffer;
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    // Convert UTF-8 JSON output to wstring
    //return CommonUtils::utf8ToWstring(result.str());
    return CommonUtils::utf8ToWstring(result.str());
}


// Run ffprobe using a wide-string command and capture its output as a wide string.
std::wstring runFFprobe(const std::wstring& filename) {

    auto ret = getAudioMetadataJSON(filename);

    return ret;

    //// Build the command (including quoting the filename)
    //std::wstring command = L"ffprobe -v quiet -print_format json -show_format -show_streams -show_chapters \"";
    //command += filename;
    //command += L"\"";

    //std::array<wchar_t, 2024> buffer;
    //std::wstring outputWide;
    //FILE* pipe = popen(command.c_str(), L"r");
    //if (!pipe)
    //    throw std::runtime_error("Failed to open pipe");
    //while (fgetws(buffer.data(), static_cast<int>(buffer.size()), pipe))
    //    outputWide += buffer.data();
    //pclose(pipe);
    //// Convert the wide-character output (assumed to be UTF‑16) to UTF‑8.
    //return WideToUTF8_2(outputWide);
}

#else  // Linux/Unix

#include <clocale>
#include <cwchar>

// On Linux, set the locale to a UTF‑8–compatible one and convert the wide filename using wcstombs.
std::string runFFprobe(const std::wstring& filename) {
    // Ensure that the locale is set to UTF‑8.
    std::setlocale(LC_CTYPE, "en_US.UTF-8");

    // Convert the wide filename to a UTF‑8 encoded narrow string.
    size_t len = std::wcstombs(nullptr, filename.c_str(), 0);
    if (len == static_cast<size_t>(-1))
        throw std::runtime_error("wcstombs conversion error");
    std::string narrowFilename(len, '\0');
    std::wcstombs(&narrowFilename[0], filename.c_str(), len);

    // Build the command.
    std::string command = "ffprobe -v quiet -print_format json -show_format -show_streams -show_chapters \"";
    command += narrowFilename;
    command += "\"";

    std::array<char, 1024> buffer;
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe)
        throw std::runtime_error("Failed to open pipe");
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
        output += buffer.data();
    pclose(pipe);
    return output;
}

#endif




//returns media information (json string and media objec) from a media file (on file system)
std::tuple<FFprobeOutput, std::wstring> MediaTrack::ReadMediaInfoFromFile(std::filesystem::path mediaFilePath)
{
    std::size_t hashNumber = std::hash<std::wstring>{}(mediaFilePath);
    auto tmpFile = std::format("tmp_media_{}.json", hashNumber);

    try
    {
        auto jsonString = MediaTrack::ExtractMetadataFromMediaTrack(mediaFilePath, tmpFile);
        auto mi = MediaTrack::ParseMediaTrack(jsonString);

        return std::make_tuple(mi, jsonString);
    }
    catch (const std::exception& ex) {
        std::wcout << " ### COMMAND INFO EXCEOTION :" << mediaFilePath.generic_wstring() << std::endl << ex.what() << std::endl;

    }

    return std::make_tuple(FFprobeOutput{}, L"{}");
}

//create a media file (on filesystem) from a media track
std::wstring MediaTrack::ExtractMetadataFromMediaTrack(std::filesystem::path mediaFilePath, std::filesystem::path outFile)
{
    using namespace std::string_literals;


    int status = 0;

    auto tmpPath = fs::temp_directory_path();
    //fs::path tmpFilePath{ tmpPath.generic_wstring() + L"\\media_info.json"s };
    fs::path tmpFilePath{ tmpPath / outFile };


    std::wstring cmdExecNameW{ L"ffprobe -v quiet -print_format json -show_format -show_streams -show_chapters "s };
    std::wstring commandW{ cmdExecNameW + L"\""s + mediaFilePath.generic_wstring() + L"\""s + L" > \""s + tmpFilePath.generic_wstring() + L"\""s };

    //std::wstring commandW{ cmdExecNameW + LR"( -i ")"s + _sourcePath.generic_wstring() + LR"(" )"s + convertParamsW + L"'" + _targetTMPPath.generic_wstring() + L"'" };


    try
    {
        // Specify your file name as a wide string.
   //     std::wstring filename = L"input.mp3";
#ifdef _WIN32
        std::wstring f = mediaFilePath.generic_wstring();

        std::wstring wide_output = runFFprobe(f);
        // Convert wide output to UTF-8 narrow string for printing.
     //   std::cout << wide_output << std::endl;
#else
        std::string output = runFFprobe(filename);
        std::cout << output << std::endl;
#endif

        if (status == 0)
        {

            return wide_output;
        }

    }
    catch (const std::exception& e) {
        std::wcout << " ### COMMAND INFO EXCEOTION :" << mediaFilePath.generic_wstring() << std::endl << e.what() << std::endl;
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return L"**ERROR***";
}

// Parse Tags from a RapidJSON Value
Tags parseTags(const rapidjson::Value& jsonValue)
{
    Tags resultTags;
    // Iterate over the object's members using MemberIterator
    for (rapidjson::Value::ConstMemberIterator itr = jsonValue.MemberBegin(); itr != jsonValue.MemberEnd(); ++itr) {
        // Get the key (tag name) as a string
        std::string key = itr->name.GetString();

        // Get the value, ensure it's a string, and add to the map
        if (itr->value.IsString()) {
            resultTags.tags[key] = itr->value.GetString();
        }
        else {
            // Handle non-string values (e.g., convert numbers to strings)
            // For ffprobe, tags are typically strings, but this is a fallback
            if (itr->value.IsNumber()) {
                resultTags.tags[key] = std::to_string(itr->value.GetDouble());
            }
            else
            {
                int  i = 0;
            }
            // Add more conversions if needed (e.g., bool, null)
        }
    }

    return resultTags;
}

FFprobeOutput MediaTrack::ParseMediaTrack(const Value& doc)
{
    FFprobeOutput mediaInfo;

    if (doc.HasMember("format") && doc["format"].IsObject()) {
        Format& format = mediaInfo.format;

        const auto& formatTag = doc["format"];

        if (auto filename = JsonUtils::tryParseMember<std::wstring>(formatTag, "filename")) { format.filename = *filename; }

        if (auto nb_streams = JsonUtils::tryParseMember<int>(formatTag, "nb_streams")) { format.nb_streams = *nb_streams; }
        if (auto nb_programs = JsonUtils::tryParseMember<int>(formatTag, "nb_programs")) { format.nb_programs = *nb_programs; }
        if (auto nb_stream_groups = JsonUtils::tryParseMember<int>(formatTag, "nb_stream_groups")) { format.nb_stream_groups = *nb_stream_groups; }

        if (auto format_name = JsonUtils::tryParseMember<std::string>(formatTag, "format_name")) { format.format_name = *format_name; }
        if (auto format_long_name = JsonUtils::tryParseMember<std::string>(formatTag, "format_long_name")) { format.format_long_name = *format_long_name; }
        if (auto start_time = JsonUtils::tryParseMember<std::optional<std::string>>(formatTag, "start_time")) { format.start_time = *start_time; }
        if (auto duration = JsonUtils::tryParseMember<std::optional<std::string>>(formatTag, "duration")) { format.duration = std::stod(*duration.value_or("0")); }
        if (auto size = JsonUtils::tryParseMember<std::string>(formatTag, "size")) { format.size = *size; }
        if (auto bit_rate = JsonUtils::tryParseMember<std::string>(formatTag, "bit_rate")) { format.bit_rate = *bit_rate; }
        if (auto probe_score = JsonUtils::tryParseMember<int>(formatTag, "probe_score")) { format.probe_score = *probe_score; }

        if (formatTag.HasMember("tags") && formatTag["tags"].IsObject()) {
            const rapidjson::Value& jsonValue = formatTag["tags"];
            format.tags = parseTags(jsonValue);

            {
                //OPTIONAL - ADD popular / most used tags to direct fields
                //=============================================================
                auto tags = formatTag["tags"].GetObj();
                if (auto album = JsonUtils::tryParseMember<std::wstring>(tags, "album")) { mediaInfo.format_tags.album = *album; }
                if (auto disc = JsonUtils::tryParseMember<std::wstring>(tags, "disc")) { mediaInfo.format_tags.disc = *disc; }
                //if (auto album_dynamic_range = JsonUtils::tryParseMember<std::wstring>(tags, "album_dynamic_range")) { mi.format.tags.album_dynamic_range = *album_dynamic_range; }
                //if (auto dynamic_range = JsonUtils::tryParseMember<std::wstring>(tags, "dynamic_range")) { mi.format.tags.dynamic_range = *dynamic_range; }
                if (auto artist = JsonUtils::tryParseMember<std::wstring>(tags, "artist")) { mediaInfo.format_tags.artist = *artist; }
                if (auto album_artist = JsonUtils::tryParseMember<std::wstring>(tags, "album_artist")) { mediaInfo.format_tags.album_artist = *album_artist; }
                if (auto composer = JsonUtils::tryParseMember<std::wstring>(tags, "composer")) { mediaInfo.format_tags.composer = *composer; }
                //if (auto copyright = JsonUtils::tryParseMember<std::wstring>(tags, "copyright")) { mi.format.tags.copyright = *copyright; }
                //if (auto label = JsonUtils::tryParseMember<std::wstring>(tags, "label")) { mi.format.tags.label = *label; }
                if (auto year = JsonUtils::tryParseMember<std::wstring>(tags, "year")) { mediaInfo.format_tags.year = *year; }
                //if (auto comment = JsonUtils::tryParseMember<std::wstring>(tags, "comment")) { mi.format.tags.comment = *comment; }
                //if (auto genre = JsonUtils::tryParseMember<std::wstring>(tags, "genre")) { mi.format.tags.genre = *genre; }
                if (auto publisher = JsonUtils::tryParseMember<std::wstring>(tags, "publisher")) { mediaInfo.format_tags.publisher = *publisher; }
                if (auto title = JsonUtils::tryParseMember<std::wstring>(tags, "title")) { mediaInfo.format_tags.title = *title; }
                if (auto track = JsonUtils::tryParseMember<std::wstring>(tags, "track")) { mediaInfo.format_tags.track = *track; }
                if (auto track_total = JsonUtils::tryParseMember<std::wstring>(tags, "track_total")) { mediaInfo.format_tags.track_total = *track_total; }
                if (auto date = JsonUtils::tryParseMember<std::wstring>(tags, "date")) { mediaInfo.format_tags.date = *date; }
                if (auto encoder = JsonUtils::tryParseMember<std::wstring>(tags, "encoder")) { mediaInfo.format_tags.encoder = *encoder; }
                //if (auto encoded_by = JsonUtils::tryParseMember<std::wstring>(tags, "encoded_by")) { mi.format.tags.encoded_by = *encoded_by; }
                //if (auto organization = JsonUtils::tryParseMember<std::wstring>(tags, "organization")) { mi.format.tags.organization = *organization; }
            }
        }
    }

    if (doc.HasMember("streams") && doc["streams"].IsArray()) {
        for (const auto& s : doc["streams"].GetArray()) {
            Stream stream;
            stream.index = s.HasMember("index") && s["index"].IsInt() ? s["index"].GetInt() : 0;
            stream.codec_name = s.HasMember("codec_name") && s["codec_name"].IsString() ? std::optional<std::string>(s["codec_name"].GetString()) : std::nullopt;
            stream.codec_type = s.HasMember("codec_type") && s["codec_type"].IsString() ? std::optional<std::string>(s["codec_type"].GetString()) : std::nullopt;
            stream.sample_rate = s.HasMember("sample_rate") && s["sample_rate"].IsString() ? std::optional<std::string>(s["sample_rate"].GetString()) : std::nullopt;
            stream.channels = s.HasMember("channels") && s["channels"].IsInt() ? std::optional<int>(s["channels"].GetInt()) : std::nullopt;
            stream.duration = s.HasMember("duration") && s["duration"].IsString() ? std::optional<std::string>(s["duration"].GetString()) : std::nullopt;
            if (s.HasMember("tags") && s["tags"].IsObject()) {
                stream.tags = parseTags(s["tags"]);
            }
            mediaInfo.streams.push_back(stream);
        }
    }

    return mediaInfo;
}

FFprobeOutput MediaTrack::ParseMediaTrack(std::wstring jsonString)
{
    rapidjson::Document doc;
    std::string utf8Json = CommonUtils::wstringToUtf8(jsonString);
    doc.Parse(utf8Json.c_str());

    if (doc.HasParseError()) {
        std::cerr << "Error parsing JSON: " << doc.GetParseError() << std::endl;

        return FFprobeOutput{};
    }

    auto mediaInfo = ParseMediaTrack(doc);

    return mediaInfo;
}

FFprobeOutput MediaTrack::ParseFFprobeOutput(const Value& formatTag)
{
    FFprobeOutput mi;

    if (auto filename = JsonUtils::tryParseMember<std::wstring>(formatTag, "filename")) { mi.format.filename = *filename; }

    if (auto nb_streams = JsonUtils::tryParseMember<int>(formatTag, "nb_streams")) { mi.format.nb_streams = *nb_streams; }
    if (auto nb_programs = JsonUtils::tryParseMember<int>(formatTag, "nb_programs")) { mi.format.nb_programs = *nb_programs; }
    if (auto nb_stream_groups = JsonUtils::tryParseMember<int>(formatTag, "nb_stream_groups")) { mi.format.nb_stream_groups = *nb_stream_groups; }

    if (auto format_name = JsonUtils::tryParseMember<std::string>(formatTag, "format_name")) { mi.format.format_name = *format_name; }
    if (auto format_long_name = JsonUtils::tryParseMember<std::string>(formatTag, "format_long_name")) { mi.format.format_long_name = *format_long_name; }
    if (auto start_time = JsonUtils::tryParseMember<std::optional<std::string>>(formatTag, "start_time")) { mi.format.start_time = *start_time; }
    if (auto duration = JsonUtils::tryParseMember<std::optional<double>>(formatTag, "duration")) { mi.format.duration = *duration; }
    if (auto size = JsonUtils::tryParseMember<std::string>(formatTag, "size")) { mi.format.size = *size; }
    if (auto bit_rate = JsonUtils::tryParseMember<std::string>(formatTag, "bit_rate")) { mi.format.bit_rate = *bit_rate; }
    if (auto probe_score = JsonUtils::tryParseMember<int>(formatTag, "probe_score")) { mi.format.probe_score = *probe_score; }


    if (formatTag.FindMember("tags") != formatTag.MemberEnd())
    {
        auto tags = formatTag["tags"].GetObj();
        
        Tags result;
        if (formatTag["tags"].IsObject()) {
            const rapidjson::Value& jsonValue = formatTag["tags"];
            // Iterate over the object's members using MemberIterator
            for (rapidjson::Value::ConstMemberIterator itr = jsonValue.MemberBegin(); itr != jsonValue.MemberEnd(); ++itr) {
                // Get the key (tag name) as a string
                std::string key = itr->name.GetString();

                // Get the value, ensure it's a string, and add to the map
                if (itr->value.IsString()) {
                    result.tags[key] = itr->value.GetString();
                }
                else {
                    // Handle non-string values (e.g., convert numbers to strings)
                    // For ffprobe, tags are typically strings, but this is a fallback
                    if (itr->value.IsNumber()) {
                        result.tags[key] = std::to_string(itr->value.GetDouble());
                    }
                    else
                    {
                        int  i = 0;
                    }
                    // Add more conversions if needed (e.g., bool, null)
                }
            }
        }

        //add all custom tags into a MAP
        mi.format.tags = result;

        //OPTIONAL - ADD popular / most used tags to direct fields
        if (auto album = JsonUtils::tryParseMember<std::wstring>(tags, "album")) { mi.format_tags.album = *album; }
        if (auto disc = JsonUtils::tryParseMember<std::wstring>(tags, "disc")) { mi.format_tags.disc = *disc; }
        //if (auto album_dynamic_range = JsonUtils::tryParseMember<std::wstring>(tags, "album_dynamic_range")) { mi.format.tags.album_dynamic_range = *album_dynamic_range; }
        //if (auto dynamic_range = JsonUtils::tryParseMember<std::wstring>(tags, "dynamic_range")) { mi.format.tags.dynamic_range = *dynamic_range; }
        if (auto artist = JsonUtils::tryParseMember<std::wstring>(tags, "artist")) { mi.format_tags.artist = *artist; }
        if (auto album_artist = JsonUtils::tryParseMember<std::wstring>(tags, "album_artist")) { mi.format_tags.album_artist = *album_artist; }
        if (auto composer = JsonUtils::tryParseMember<std::wstring>(tags, "composer")) { mi.format_tags.composer = *composer; }
        //if (auto copyright = JsonUtils::tryParseMember<std::wstring>(tags, "copyright")) { mi.format.tags.copyright = *copyright; }
        //if (auto label = JsonUtils::tryParseMember<std::wstring>(tags, "label")) { mi.format.tags.label = *label; }
        if (auto year = JsonUtils::tryParseMember<std::wstring>(tags, "year")) { mi.format_tags.year = *year; }
        //if (auto comment = JsonUtils::tryParseMember<std::wstring>(tags, "comment")) { mi.format.tags.comment = *comment; }
        //if (auto genre = JsonUtils::tryParseMember<std::wstring>(tags, "genre")) { mi.format.tags.genre = *genre; }
        if (auto publisher = JsonUtils::tryParseMember<std::wstring>(tags, "publisher")) { mi.format_tags.publisher = *publisher; }
        if (auto title = JsonUtils::tryParseMember<std::wstring>(tags, "title")) { mi.format_tags.title = *title; }
        if (auto track = JsonUtils::tryParseMember<std::wstring>(tags, "track")) { mi.format_tags.track = *track; }
        if (auto track_total = JsonUtils::tryParseMember<std::wstring>(tags, "track_total")) { mi.format_tags.track_total = *track_total; }
        if (auto date = JsonUtils::tryParseMember<std::wstring>(tags, "date")) { mi.format_tags.date = *date; }
        if (auto encoder = JsonUtils::tryParseMember<std::wstring>(tags, "encoder")) { mi.format_tags.encoder = *encoder; }
        //if (auto encoded_by = JsonUtils::tryParseMember<std::wstring>(tags, "encoded_by")) { mi.format.tags.encoded_by = *encoded_by; }
        //if (auto organization = JsonUtils::tryParseMember<std::wstring>(tags, "organization")) { mi.format.tags.organization = *organization; }
    }

    return mi;
};


