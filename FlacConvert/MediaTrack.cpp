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

#include "JsonUtils.h"


//returns media information (json string and media objec) from a media file (on file system)
std::tuple<FFprobeOutput, std::optional<std::wstring>> MediaTrack::ReadMediaInfoFromJsonFile(std::filesystem::path mediaFilePath)
{
    try
    {
        if (AppSettingsJson::AppSetting()->UseFFmpegLibraryAPI)
        {
            auto mediaInfo = FFmpeg::GetFFprobeMetadataAPI(mediaFilePath);
            //return std::make_tuple(mediaInfo, CommonUtils::utf8ToWstring(toJsonString(mediaInfo)));
            return std::make_tuple(mediaInfo, std::nullopt);
        }
        else
        {
            auto jsonString = FFmpeg::GetJsonMetadataShell(mediaFilePath);
            auto mediaInfo = MediaTrack::ParseFFprobeInformation(jsonString);
            return std::make_tuple(mediaInfo, jsonString);
        }
    }
    catch (const std::exception& ex) {
		spdlog::error("Error in ReadMediaInfoFromJsonFile: ", ex.what());
        //try
        //{
        //    auto mediaInfo = FFmpeg::GetFFprobeMetadataShell(mediaFilePath);
        //    spdlog::info("Fixed by shell execution (ReadMediaInfoFromJsonFile-#2)");
        //    return std::make_tuple(mediaInfo, CommonUtils::utf8ToWstring(toJsonString(mediaInfo)));
        //}
        //catch (const std::exception& ex) {
        //    spdlog::error("Error (ReadMediaInfoFromJsonFile-#2): ", ex.what());
        //}
    }

    return std::make_tuple(FFprobeOutput{}, std::nullopt);
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


#include "MediaTrack.h"
#include "PlatformUtils.h"
#include <spdlog/spdlog.h>


const std::string MediaTrack::GetCreateTableSQL() {

    std::string createTableSQL = std::format(
        R"(
        CREATE TABLE IF NOT EXISTS TracksDB (
            ID INTEGER PRIMARY KEY,
            album_path TEXT NOT NULL,
            nb_streams INTEGER,
            nb_programs INTEGER,
            nb_stream_groups INTEGER,
            format_name TEXT,
            format_long_name TEXT,
            start_time INTEGER,
            duration REAL,
            size TEXT,
            bit_rate INTEGER,
            probe_score INTEGER,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            {} TEXT,
            stream1_index INTEGER,
            stream1_codec_name TEXT,
            stream1_codec_type TEXT,
            stream1_sample_rate TEXT,
            stream1_channels INTEGER,
            stream1_channel_layout TEXT,
            stream1_bit_rate INTEGER,
            stream1_frame_size INTEGER,
            stream1_duration REAL,
            stream1_start_time INTEGER,
            stream1_tag1 TEXT,
            stream2_index INTEGER,
            stream2_codec_name TEXT,
            stream2_codec_type TEXT,
            stream2_sample_rate TEXT,
            stream2_channels INTEGER,
            stream2_channel_layout TEXT,
            stream2_bit_rate INTEGER,
            stream2_frame_size INTEGER,
            stream2_duration REAL,
            stream2_start_time INTEGER,
            stream2_tag1 TEXT
        )
    )",
        MediaTrackConstants::kFormatTag_album,
        MediaTrackConstants::kFormatTag_artist,
        MediaTrackConstants::kFormatTag_album_artist,
        MediaTrackConstants::kFormatTag_genre,
        MediaTrackConstants::kFormatTag_disc,
        MediaTrackConstants::kFormatTag_title,
        MediaTrackConstants::kFormatTag_track,
        MediaTrackConstants::kFormatTag_track_total,
        MediaTrackConstants::kFormatTag_date,
        MediaTrackConstants::kFormatTag_comment,
        MediaTrackConstants::kFormatTag_publisher,
        MediaTrackConstants::kFormatTag_encoder,
        MediaTrackConstants::kFormatTag_encoded_by,
        MediaTrackConstants::kFormatTag_organization,
        MediaTrackConstants::kFormatTag_composer,
        MediaTrackConstants::kFormatTag_copyright,
        MediaTrackConstants::kFormatTag_album_dynamic_range,
        MediaTrackConstants::kFormatTag_dynamic_range,
        MediaTrackConstants::kFormatTag_label,
        MediaTrackConstants::kFormatTag_year
    );

	return createTableSQL;
}

// Prepare the insert statement once.
// Define the format string as a const char* literal
// Format at runtime (e.g., inside a function)
const std::string MediaTrack::GetInsertSQLStatement()
{
    std::string insertSQLStatement = std::format(
        R"(
        INSERT OR REPLACE INTO TracksDB (
            id, album_path, nb_streams, nb_programs, nb_stream_groups, format_name, format_long_name,
            start_time, duration, size, bit_rate, probe_score,
            {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
            stream1_index, stream1_codec_name, stream1_codec_type, stream1_sample_rate, stream1_channels, stream1_channel_layout, stream1_bit_rate, stream1_frame_size, stream1_duration, stream1_start_time, stream1_tag1,
            stream2_index, stream2_codec_name, stream2_codec_type, stream2_sample_rate, stream2_channels, stream2_channel_layout, stream2_bit_rate, stream2_frame_size, stream2_duration, stream2_start_time, stream2_tag1
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )",
        kFormatTag_album,
        kFormatTag_artist,
        kFormatTag_album_artist,
        kFormatTag_genre,
        kFormatTag_disc,
        kFormatTag_title,
        kFormatTag_track,
        kFormatTag_track_total,
        kFormatTag_date,
        kFormatTag_comment,
        kFormatTag_publisher,
        kFormatTag_encoder,
        kFormatTag_encoded_by,
        kFormatTag_organization,
        kFormatTag_composer,
        kFormatTag_copyright,
        kFormatTag_album_dynamic_range,
        kFormatTag_dynamic_range,
        kFormatTag_label,
        kFormatTag_year
    );

    return insertSQLStatement;
}

bool MediaTrack::ExportToDatabase(sqlite3_stmt* stmt, const std::wstring& albumPath, const MediaTrack& track) {
    const auto& [trackName, size, mediaInfo, mediaInfoString, lastError] = track;
    std::string albumPathUtf8 = PlatformUtils::wstringToUtf8_ver2(albumPath);

    std::optional<std::string> stream1Tag1; // Placeholder for stream tags
    std::optional<std::string> stream2Tag1;

    int bindIndex = 1;
    sqlite3_bind_null(stmt, bindIndex++); // id
    sqlite3_bind_text(stmt, bindIndex++, albumPathUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, bindIndex++, mediaInfo.format.nb_streams);
    sqlite3_bind_int(stmt, bindIndex++, mediaInfo.format.nb_programs);
    sqlite3_bind_int(stmt, bindIndex++, mediaInfo.format.nb_stream_groups);
    sqlite3_bind_text(stmt, bindIndex++, mediaInfo.format.format_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, bindIndex++, mediaInfo.format.format_long_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, bindIndex++, mediaInfo.format.start_time.value_or(0));
    sqlite3_bind_double(stmt, bindIndex++, mediaInfo.format.duration.value_or(0.0));
    sqlite3_bind_text(stmt, bindIndex++, mediaInfo.format.size.value_or("").c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, bindIndex++, mediaInfo.format.bit_rate.value_or(0));
    sqlite3_bind_int(stmt, bindIndex++, mediaInfo.format.probe_score);

    auto& tags = mediaInfo.format.tags;
    if (tags.has_value())
    {
		auto& tagsObj = tags.value();
        
        auto getValue = [](const JsonUtils::Tags& map, const std::string& key) -> std::string {
                auto it = map.find(key);
                return (it != map.end()) ? it->second : "";
            };

        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_album).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_artist).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_album_artist).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_genre).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_disc).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_title).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_track).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_track_total).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_date).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_comment).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_publisher).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_encoder).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_encoded_by).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_organization).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_composer).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_copyright).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_album_dynamic_range).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_dynamic_range).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_label).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getValue(tagsObj, kFormatTag_year).c_str(), -1, SQLITE_TRANSIENT);
    }

    if (auto stream1 = mediaInfo.streams.size() > 0 ? std::optional<Stream>{mediaInfo.streams[0]} : std::nullopt; stream1.has_value()) {
        sqlite3_bind_int(stmt, bindIndex++, stream1->index);
        sqlite3_bind_text(stmt, bindIndex++, stream1->codec_name.value_or("").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, stream1->codec_type.value_or("").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, stream1->sample_rate.value_or("").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, bindIndex++, stream1->channels.value_or(0));
        sqlite3_bind_text(stmt, bindIndex++, stream1->channel_layout.value_or("").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, bindIndex++, stream1->bit_rate.value_or(0));
        sqlite3_bind_int(stmt, bindIndex++, stream1->frame_size.value_or(0));
        sqlite3_bind_text(stmt, bindIndex++, stream1->duration.value_or("").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, bindIndex++, stream1->start_time.value_or(0));
        sqlite3_bind_text(stmt, bindIndex++, stream1Tag1.value_or("").c_str(), -1, SQLITE_TRANSIENT);
    }
    else {
        for (int i = 0; i < 11; ++i) sqlite3_bind_null(stmt, bindIndex++);
    }

    if (auto stream2 = mediaInfo.streams.size() > 1 ? std::optional<Stream>{mediaInfo.streams[1]} : std::nullopt; stream2.has_value()) {
        sqlite3_bind_int(stmt, bindIndex++, stream2->index);
        sqlite3_bind_text(stmt, bindIndex++, stream2->codec_name.value_or("").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, stream2->codec_type.value_or("").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, stream2->sample_rate.value_or("").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, bindIndex++, stream2->channels.value_or(0));
        sqlite3_bind_text(stmt, bindIndex++, stream2->channel_layout.value_or("").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, bindIndex++, stream2->bit_rate.value_or(0));
        sqlite3_bind_int(stmt, bindIndex++, stream2->frame_size.value_or(0));
        sqlite3_bind_text(stmt, bindIndex++, stream2->duration.value_or("").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, bindIndex++, stream2->start_time.value_or(0));
        sqlite3_bind_text(stmt, bindIndex++, stream2Tag1.value_or("").c_str(), -1, SQLITE_TRANSIENT);
    }
    else {
        for (int i = 0; i < 11; ++i) sqlite3_bind_null(stmt, bindIndex++);
    }

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        spdlog::error("Database error: {}", sqlite3_errmsg(sqlite3_db_handle(stmt)));
        return false;
    }

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    return true;
}



std::string MediaTrack::toJsonString(const FFprobeOutput& output) {
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
