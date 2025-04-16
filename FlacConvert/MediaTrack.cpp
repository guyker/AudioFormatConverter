#include "MediaTrack.h"




#include <iostream>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <array>
#include <cstdlib>
#include <cwchar>

#include <iostream>
#include <string>
#include <map>

#include <string>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <windows.h> // For Windows path handling

#include "FFmpeg.h"

#include "PlatformUtils.h"
#include <spdlog/spdlog.h>



std::string toJsonString(const FFprobeOutput& output) {
    std::ostringstream json;

    json << "{\n";

    // Format section
    //if (output.format)
    {
        json << "  \"format\": {\n";
        const Format& fmt = output.format;

        json << "    \"filename\": \"" << JsonUtils::escapeJsonString(fmt.filename) << "\",\n";
        json << "    \"nb_streams\": " << fmt.nb_streams << ",\n";
        json << "    \"format_name\": \"" << JsonUtils::escapeJsonString(fmt.format_name) << "\",\n";
        json << "    \"format_long_name\": \"" << JsonUtils::escapeJsonString(fmt.format_long_name) << "\"";

        if (fmt.duration) {
            json << ",\n    \"duration\": \"" << std::fixed << std::setprecision(3) << *fmt.duration << "\"";
        }
        if (fmt.bit_rate) {
            json << ",\n    \"bit_rate\": \"" << *fmt.bit_rate << "\"";
        }
        if (fmt.start_time) {
            json << ",\n    \"start_time\": \"" << *fmt.start_time << "\"";
        }
        if (fmt.probe_score) {
            json << ",\n    \"probe_score\": " << fmt.probe_score;
        }
        if (fmt.file_size) {
            json << ",\n    \"size\": \"" << *fmt.file_size << "\"";
        }
        if (fmt.tags) {
            json << ",\n    \"tags\": {\n";
            bool first = true;
            for (const auto& [key, value] : fmt.tags.value()) {
                if (!first) json << ",\n";
                json << "      \"" << JsonUtils::escapeJsonString(key) << "\": \"" << JsonUtils::escapeJsonString(value) << "\"";
                first = false;
            }
            json << "\n    }";
        }
        json << "\n  }";
    }

    // Streams section
    if (!output.streams.empty()) {
        json << ",\n";
        json << "  \"streams\": [\n";
        for (size_t i = 0; i < output.streams.size(); ++i) {
            const Stream& s = output.streams[i];
            json << "    {\n";
            json << "      \"index\": " << s.index;
            if (s.codec_name) {
                json << ",\n      \"codec_name\": \"" << JsonUtils::escapeJsonString(*s.codec_name) << "\"";
            }
            if (s.codec_type) {
                json << ",\n      \"codec_type\": \"" << JsonUtils::escapeJsonString(*s.codec_type) << "\"";
            }
            if (s.sample_rate) {
                json << ",\n      \"sample_rate\": \"" << JsonUtils::escapeJsonString(*s.sample_rate) << "\"";
            }
            if (s.channels) {
                json << ",\n      \"channels\": " << *s.channels;
            }
            if (s.channel_layout) {
                json << ",\n      \"channel_layout\": \"" << JsonUtils::escapeJsonString(*s.channel_layout) << "\"";
            }
            if (s.bit_rate) {
                json << ",\n      \"bit_rate\": \"" << *s.bit_rate << "\"";
            }
            if (s.bits_per_sample) {
                json << ",\n      \"bits_per_sample\": " << *s.bits_per_sample;
            }
            if (s.frame_size) {
                json << ",\n      \"frame_size\": " << *s.frame_size;
            }
            if (s.duration) {
                json << ",\n      \"duration\": \"" << JsonUtils::escapeJsonString(*s.duration) << "\"";
            }
            if (s.start_time) {
                json << ",\n      \"start_time\": \"" << *s.start_time << "\"";
            }
            if (s.nb_frames) {
                json << ",\n      \"nb_frames\": \"" << *s.nb_frames << "\"";
            }
            if (s.tags) {
                json << ",\n      \"tags\": {\n";
                bool first = true;
                for (const auto& [key, value] : s.tags.value()) {
                    if (!first) json << ",\n";
                    json << "        \"" << JsonUtils::escapeJsonString(key) << "\": \"" << JsonUtils::escapeJsonString(value) << "\"";
                    first = false;
                }
                json << "\n      }";
            }
            json << "\n    }";
            if (i < output.streams.size() - 1) json << ",";
            json << "\n";
        }
        json << "  ]";
    }

    json << "\n}";
    return json.str();
}


//returns media information (json string and media objec) from a media file (on file system)
std::tuple<FFprobeOutput, std::wstring> MediaTrack::ReadMediaInfoFromFile(std::filesystem::path mediaFilePath)
{
    try
    {
        if (AppSettingsJson::AppSetting()->UseFFmpegLibraryAPI)
        {
            auto mediaInfo = FFmpeg::GetFFprobeMetadataAPI(mediaFilePath);
            return std::make_tuple(mediaInfo, CommonUtils::utf8ToWstring(toJsonString(mediaInfo)));
        }
        else
        {
            auto mediaInfo = FFmpeg::GetFFprobeMetadataShell(mediaFilePath);
            return std::make_tuple(mediaInfo, CommonUtils::utf8ToWstring(toJsonString(mediaInfo)));
        }
    }
    catch (const std::exception& ex) {
		spdlog::error("Error (ReadMediaInfoFromFile): ", ex.what());
        //try
        //{
        //    auto mediaInfo = FFmpeg::GetFFprobeMetadataShell(mediaFilePath);
        //    spdlog::info("Fixed by shell execution (ReadMediaInfoFromFile-#2)");
        //    return std::make_tuple(mediaInfo, CommonUtils::utf8ToWstring(toJsonString(mediaInfo)));
        //}
        //catch (const std::exception& ex) {
        //    spdlog::error("Error (ReadMediaInfoFromFile-#2): ", ex.what());
        //}
    }

    return std::make_tuple(FFprobeOutput{}, L"{}");
}



//Helper functions to parse a member from a JSON object
//Parse the format section of the FFprobe output
bool TryParseFFprobeFormat(const Value& doc, FFprobeOutput &mediaInfo)
{
    if (doc.HasMember("format") && doc["format"].IsObject()) {
        Format& format = mediaInfo.format;

        const auto& formatTag = doc["format"];

        if (auto filename = JsonUtils::tryParseMember<std::string>(formatTag, "filename")) { format.filename = *filename; }

        if (auto nb_streams = JsonUtils::tryParseMember<int>(formatTag, "nb_streams")) { format.nb_streams = *nb_streams; }
        if (auto nb_programs = JsonUtils::tryParseMember<int>(formatTag, "nb_programs")) { format.nb_programs = *nb_programs; }
        if (auto nb_stream_groups = JsonUtils::tryParseMember<int>(formatTag, "nb_stream_groups")) { format.nb_stream_groups = *nb_stream_groups; }

        if (auto format_name = JsonUtils::tryParseMember<std::string>(formatTag, "format_name")) { format.format_name = *format_name; }
        if (auto format_long_name = JsonUtils::tryParseMember<std::string>(formatTag, "format_long_name")) { format.format_long_name = *format_long_name; }
        if (auto start_time = JsonUtils::tryParseMember<std::optional<std::string>>(formatTag, "start_time")) { format.start_time = std::stoll(*start_time.value_or("0")); }
        if (auto duration = JsonUtils::tryParseMember<std::optional<std::string>>(formatTag, "duration")) { format.duration = std::stod(*duration.value_or("0")); }
        if (auto size = JsonUtils::tryParseMember<std::string>(formatTag, "size")) { format.size = *size; }
        if (auto bit_rate = JsonUtils::tryParseMember<std::string>(formatTag, "bit_rate")) { format.bit_rate = std::stoll(bit_rate.value_or("0")); }
        if (auto probe_score = JsonUtils::tryParseMember<int>(formatTag, "probe_score")) { format.probe_score = *probe_score; }

        if (formatTag.HasMember("tags") && formatTag["tags"].IsObject()) {
            const rapidjson::Value& jsonValue = formatTag["tags"];
            format.tags = JsonUtils::GetKeyValueMap(jsonValue);

            {
                //OPTIONAL - ADD popular / most used tags to direct fields
                //=============================================================
                auto tags = formatTag["tags"].GetObj();
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "album")) { mediaInfo.format_tags.album = *val;; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "artist")) { mediaInfo.format_tags.artist = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "album_artist")) { mediaInfo.format_tags.album_artist = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "genre")) { mediaInfo.format_tags.genre = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "disc")) { mediaInfo.format_tags.disc = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "title")) { mediaInfo.format_tags.title = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "track")) { mediaInfo.format_tags.track = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "track_total")) { mediaInfo.format_tags.track_total = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "date")) { mediaInfo.format_tags.date = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "comment")) { mediaInfo.format_tags.comment = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "publisher")) { mediaInfo.format_tags.publisher = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "encoder")) { mediaInfo.format_tags.encoder = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "encoded_by")) { mediaInfo.format_tags.encoded_by = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "organization")) { mediaInfo.format_tags.organization = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "composer")) { mediaInfo.format_tags.composer = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "copyright")) { mediaInfo.format_tags.copyright = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "album_dynamic_range")) { mediaInfo.format_tags.album_dynamic_range = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "dynamic_range")) { mediaInfo.format_tags.dynamic_range = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "label")) { mediaInfo.format_tags.label = *val; }
                if (auto val = JsonUtils::tryParseMember<std::wstring>(tags, "year")) { mediaInfo.format_tags.year = *val; }
            }
        }

        return true;
    }

    return false;
}

//Helper functions to parse a member from a JSON object
// Parse the streams section of the FFprobe output
bool TryParseFFprobeStreams(const Value& doc, FFprobeOutput& mediaInfo)
{
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
                stream.tags = JsonUtils::GetKeyValueMap(s["tags"]);
            }
            mediaInfo.streams.push_back(stream);
        }
    }

    return false;
}

FFprobeOutput MediaTrack::ParseFFprobeInformation(const Value& doc)
{
    FFprobeOutput mediaInfo;

    TryParseFFprobeFormat(doc, mediaInfo);
    TryParseFFprobeStreams(doc, mediaInfo);

    return mediaInfo;
}

FFprobeOutput MediaTrack::ParseFFprobeInformation(std::wstring jsonString)
{
    rapidjson::Document doc;
    std::string utf8Json = PlatformUtils::wstringToUtf8_ver2(jsonString);
    doc.Parse(utf8Json.c_str());

    if (doc.HasParseError()) {
        std::cerr << "Error parsing JSON: " << doc.GetParseError() << std::endl;

        return FFprobeOutput{};
    }

    auto mediaInfo = ParseFFprobeInformation(doc);

    return mediaInfo;
}


