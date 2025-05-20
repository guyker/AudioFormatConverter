
#include <iostream>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <array>
#include <cstdlib>
#include <cwchar>
#include <map>
#include <filesystem>
#include <fstream>
#include <codecvt>

#include <spdlog/spdlog.h>

#include "MediaTrack.h"
#include "FFmpeg.h"
#include "PlatformUtils.h"
#include "JsonUtils.h"
#include "AppSettings.h"


//returns media information (json string and media objec) from a media file (on file system)
std::tuple<FFprobeOutput, std::optional<std::wstring>> MediaTrack::ReadMetadataInfoFromFile(std::filesystem::path mediaFilePath, const bool bIncludeAudioQualityMetrics)
{
    try
    {
        if (AppSettingsJson::AppSetting()->UseFFmpegLibraryAPI)
        {
            auto mediaInfo = FFmpeg::GetFFprobeMetadataAPI(mediaFilePath, bIncludeAudioQualityMetrics);
            //return std::make_tuple(mediaInfo, CommonUtils::utf8ToWstring(sonString(mediaInfo)));
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
		spdlog::error("Error in ReadMetadataInfoFromFile: ", ex.what());
        //try
        //{
        //    auto mediaInfo = FFmpeg::GetFFprobeMetadataShell(mediaFilePath);
        //    spdlog::info("Fixed by shell execution (ReadMetadataInfoFromFile-#2)");
        //    return std::make_tuple(mediaInfo, CommonUtils::utf8ToWstring(toJsonString(mediaInfo)));
        //}
        //catch (const std::exception& ex) {
        //    spdlog::error("Error (ReadMetadataInfoFromFile-#2): ", ex.what());
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
        if (auto value = JsonUtils::tryParseMember<std::string>(formatTag, "filename")) { format.filename = *value; }
        if (auto value = JsonUtils::tryParseMember<uint64_t>(formatTag, "fs_file_size")) { format.fs_file_size = value; }
        if (auto value = JsonUtils::tryParseMember<int64_t>(formatTag, "file_size")) { format.file_size = value; }
        if (auto value = JsonUtils::tryParseMember<std::string>(formatTag, "url")) { format.filename = *value; }

        if (auto value = JsonUtils::tryParseMember<int>(formatTag, "nb_streams")) { format.nb_streams = *value; }
        if (auto value = JsonUtils::tryParseMember<int>(formatTag, "nb_programs")) { format.nb_programs = *value; }
        if (auto value = JsonUtils::tryParseMember<int>(formatTag, "nb_stream_groups")) { format.nb_stream_groups = *value; }
        if (auto value = JsonUtils::tryParseMember<int>(formatTag, "nb_chapters")) { format.nb_chapters = *value; }

        if (auto format_name = JsonUtils::tryParseMember<std::string>(formatTag, "format_name")) { format.format_name = *format_name; }
        if (auto format_long_name = JsonUtils::tryParseMember<std::string>(formatTag, "format_long_name")) { format.format_long_name = *format_long_name; }
        if (auto start_time = JsonUtils::tryParseMember<std::optional<std::string>>(formatTag, "start_time")) { format.start_time = std::stoll(*start_time.value_or("0")); }
        if (auto duration = JsonUtils::tryParseMember<std::optional<std::string>>(formatTag, "duration")) { format.duration = std::stod(*duration.value_or("0")); }
        if (auto bit_rate = JsonUtils::tryParseMember<std::string>(formatTag, "bit_rate")) { format.bit_rate = std::stoll(bit_rate.value_or("0")); }
        if (auto probe_score = JsonUtils::tryParseMember<int>(formatTag, "probe_score")) { format.probe_score = *probe_score; }

        if (auto value = JsonUtils::tryParseMember<int>(formatTag, "audio_codec_id")) { format.audio_codec_id = static_cast<AVCodecID>(*value); }

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
            stream.codec_time_base = s.HasMember("codec_time_base") && s["codec_time_base"].IsString() ? std::optional<std::string>(s["codec_time_base"].GetString()) : std::nullopt;
            stream.codec_tag_string = s.HasMember("codec_tag_string") && s["codec_tag_string"].IsString() ? std::optional<std::string>(s["codec_tag_string"].GetString()) : std::nullopt;
            stream.codec_tag = s.HasMember("codec_tag") && s["codec_tag"].IsString() ? std::optional<std::string>(s["codec_tag"].GetString()) : std::nullopt;
            stream.sample_fmt = s.HasMember("sample_fmt") && s["sample_fmt"].IsString() ? std::optional<std::string>(s["sample_fmt"].GetString()) : std::nullopt;
            stream.sample_rate = s.HasMember("sample_rate") && s["sample_rate"].IsString() ? std::optional<std::string>(s["sample_rate"].GetString()) : std::nullopt;
            stream.channels = s.HasMember("channels") && s["channels"].IsInt() ? std::optional<int>(s["channels"].GetInt()) : std::nullopt;
            stream.channel_layout = s.HasMember("channel_layout") && s["channel_layout"].IsString() ? std::optional<std::string>(s["channel_layout"].GetString()) : std::nullopt;
            stream.bit_rate = s.HasMember("bit_rate") && s["bit_rate"].IsInt64() ? std::optional<int64_t>(s["bit_rate"].GetInt64()) : std::nullopt;
            stream.bits_per_sample = s.HasMember("bits_per_sample") && s["bits_per_sample"].IsInt() ? std::optional<int>(s["bits_per_sample"].GetInt()) : std::nullopt;
            stream.frame_size = s.HasMember("frame_size") && s["frame_size"].IsInt() ? std::optional<int>(s["frame_size"].GetInt()) : std::nullopt;
            stream.nb_frames = s.HasMember("nb_frames") && s["nb_frames"].IsInt64() ? std::optional<int64_t>(s["nb_frames"].GetInt64()) : std::nullopt;
            stream.r_frame_rate = s.HasMember("r_frame_rate") && s["r_frame_rate"].IsString() ? std::optional<std::string>(s["r_frame_rate"].GetString()) : std::nullopt;
            stream.avg_frame_rate = s.HasMember("avg_frame_rate") && s["avg_frame_rate"].IsString() ? std::optional<std::string>(s["avg_frame_rate"].GetString()) : std::nullopt;
            stream.time_base = s.HasMember("time_base") && s["time_base"].IsString() ? std::optional<std::string>(s["time_base"].GetString()) : std::nullopt;
            stream.start_pts = s.HasMember("start_pts") && s["start_pts"].IsInt64() ? std::optional<int64_t>(s["start_pts"].GetInt64()) : std::nullopt;
            stream.start_time = s.HasMember("start_time") && s["start_time"].IsInt64() ? std::optional<int64_t>(s["start_time"].GetInt64()) : std::nullopt;
            stream.duration_ts = s.HasMember("duration_ts") && s["duration_ts"].IsInt64() ? std::optional<int64_t>(s["duration_ts"].GetInt64()) : std::nullopt;
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

std::string MediaTrack::toJsonString(const FFprobeOutput& output) {
    std::ostringstream json;

    json << "{\n";

    // Format section
    //if (output.format)
    {
        if (output.audio_metrics)
        {
            json << "  \"audio_metrics\": {\n";
            const auto& audio_metrics = output.audio_metrics.value();
            json << "    \"codec_name\": \"" << JsonUtils::escapeJsonString(audio_metrics.codec_name) << "\",\n";
            json << "    \"sample_rate\": " << audio_metrics.sample_rate << ",\n";
            json << "    \"channels\": " << audio_metrics.channels << ",\n";
            json << "    \"bitrate\": " << audio_metrics.bitrate << ",\n";
            json << "    \"is_lossless\": " << audio_metrics.is_lossless << ",\n";
            json << "    \"is_high_quality\": " << audio_metrics.is_high_quality << "";
            json << "\n    },\n";
        }

        if (output.audio_quality) 
        {
            json << "  \"audio_quality\": {\n";
            const auto& audio_quality = output.audio_quality.value();
            json << "    \"peak_amplitude\": " << audio_quality.peak_amplitude << ",\n";
            json << "    \"rms_amplitude\": " << audio_quality.rms_amplitude << ",\n";
            json << "    \"dynamic_range_db\": " << audio_quality.dynamic_range_db << ",\n";
            json << "    \"clipped_samples\": " << audio_quality.clipped_samples << ",\n";
            json << "    \"total_samples\": " << audio_quality.total_samples << ",\n";
			json << "    \"noise_floor_rms\": \"" << audio_quality.noise_floor_rms << "\",\n";
			json << "    \"estimated_snr_db\": \"" << audio_quality.estimated_snr_db << "\",\n";

            json << "    \"low_noise_floor_rms\": \"" << (audio_quality.low_noise_floor_rms ? "true" : "false") << "\",\n";
            json << "    \"high_noise_comb\": \"" << (audio_quality.high_noise_comb ? "true" : "false") << "\",\n";

            json << "    \"samples_too_big\": " << audio_quality.samples_too_big << ",\n";
            json << "    \"samples_negative\": " << audio_quality.samples_negative << "";
            json << "\n    },\n";
        }

        json << "  \"format\": {\n";
        const Format& fmt = output.format;

        json << "    \"filename\": \"" << JsonUtils::escapeJsonString(fmt.filename) << "\",\n";
        
        json << "    \"fs_file_size\": " << fmt.fs_file_size.value_or(0) << ",\n";
        json << "    \"file_size\": " << fmt.file_size.value_or(-1) << ",\n";

        if (fmt.url.has_value()) {
            json << "    \"url\": \"" << JsonUtils::escapeJsonString(fmt.url.value()) << "\",\n";
        }

        json << "    \"audio_codec_id\": " << fmt.audio_codec_id << ",\n";
        if (fmt.audio_codec_id != AV_CODEC_ID_NONE && fmt.audio_codec_name.has_value())
        {
            json << "    \"codec_name\": \"" << JsonUtils::escapeJsonString(*fmt.audio_codec_name) << "\",\n";
        }

        json << "    \"format_name\": \"" << JsonUtils::escapeJsonString(fmt.format_name) << "\",\n";
        json << "    \"format_long_name\": \"" << JsonUtils::escapeJsonString(fmt.format_long_name) << "\",\n";

        json << "    \"ctx_flags\": " << fmt.ctx_flags << ",\n";
        json << "    \"nb_streams\": " << fmt.nb_streams << ",\n";
        json << "    \"nb_programs\": " << fmt.nb_programs << ",\n";
        json << "    \"nb_stream_groups\": " << fmt.nb_stream_groups << ",\n";
        json << "    \"nb_chapters\": " << fmt.nb_chapters;


        if (fmt.start_time) {
            json << ",\n    \"start_time\": \"" << *fmt.start_time << "\"";
        }
        if (fmt.duration) {
            json << ",\n    \"duration\": \"" << std::fixed << std::setprecision(3) << *fmt.duration << "\"";
        }
        if (fmt.bit_rate) {
            json << ",\n    \"bit_rate\": \"" << *fmt.bit_rate << "\"";
        }

        json << ",\n    \"packet_size\": \"" << fmt.packet_size << "\"";
        json << ",\n    \"max_delay\": \"" << fmt.max_delay << "\"";
        json << ",\n    \"flags\": \"" << fmt.flags << "\"";

        //if (fmt.probe_score) {
            json << ",\n    \"probe_score\": " << fmt.probe_score;
        //}

        
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
            if (s.codec_time_base) {
                json << ",\n      \"codec_time_base\": \"" << JsonUtils::escapeJsonString(*s.codec_time_base) << "\"";
            }
            if (s.codec_tag_string) {
                json << ",\n      \"codec_tag_string\": \"" << JsonUtils::escapeJsonString(*s.codec_tag_string) << "\"";
            }
            if (s.codec_tag) {
                json << ",\n      \"codec_tag\": \"" << JsonUtils::escapeJsonString(*s.codec_tag) << "\"";
            }
            if (s.sample_fmt) {
                json << ",\n      \"sample_fmt\": \"" << JsonUtils::escapeJsonString(*s.sample_fmt) << "\"";
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
            if (s.nb_frames) {
                json << ",\n      \"nb_frames\": \"" << *s.nb_frames << "\"";
            }
            if (s.r_frame_rate) {
                json << ",\n      \"r_frame_rate\": " << *s.r_frame_rate;
            }
            if (s.avg_frame_rate) {
                json << ",\n      \"avg_frame_rate\": " << *s.avg_frame_rate;
            }
            if (s.time_base) {
                json << ",\n      \"time_base\": " << *s.time_base;
            }
            if (s.start_pts) {
                json << ",\n      \"start_pts\": " << *s.start_pts;
            }
            if (s.start_time) {
                json << ",\n      \"start_time\": \"" << *s.start_time << "\"";
            }
            if (s.duration_ts) {
                json << ",\n      \"duration_ts\": " << *s.duration_ts;
            }
            if (s.duration) {
                json << ",\n      \"duration\": \"" << JsonUtils::escapeJsonString(*s.duration) << "\"";
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



const std::string MediaTrack::GetCreateTableSQL() {

    std::string createTableSQL = std::format(
        R"(
        CREATE TABLE IF NOT EXISTS TracksDB (
            ID INTEGER PRIMARY KEY,
            album_path TEXT NOT NULL,
            file_size INTEGER,
            nb_streams INTEGER,
            nb_programs INTEGER,
            nb_stream_groups INTEGER,
            format_name TEXT,
            format_long_name TEXT,
            start_time INTEGER,
            duration REAL,
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
            stream1_bits_per_sample TEXT,
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
            stream2_bits_per_sample TEXT,
            stream2_bit_rate INTEGER,
            stream2_frame_size INTEGER,
            stream2_duration REAL,
            stream2_start_time INTEGER,
            stream2_tag1 TEXT
        )
    )",
        MediaTrackConstants::kFormatTag_title,
        MediaTrackConstants::kFormatTag_artist,
        MediaTrackConstants::kFormatTag_album,
        MediaTrackConstants::kFormatTag_album_artist,
        MediaTrackConstants::kFormatTag_genre,
        MediaTrackConstants::kFormatTag_track,
        MediaTrackConstants::kFormatTag_track_total,
        MediaTrackConstants::kFormatTag_date,
        MediaTrackConstants::kFormatTag_year,
        MediaTrackConstants::kFormatTag_comment,
        MediaTrackConstants::kFormatTag_disc,

        MediaTrackConstants::kFormatTag_composer,
        MediaTrackConstants::kFormatTag_publisher,
        MediaTrackConstants::kFormatTag_label,
        MediaTrackConstants::kFormatTag_organization,
        MediaTrackConstants::kFormatTag_copyright,
        MediaTrackConstants::kFormatTag_encoder,
        MediaTrackConstants::kFormatTag_encoded_by,
        MediaTrackConstants::kFormatTag_album_dynamic_range,
        MediaTrackConstants::kFormatTag_dynamic_range
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
            id, album_path, file_size, nb_streams, nb_programs, nb_stream_groups, format_name, format_long_name,
            start_time, duration, bit_rate, probe_score,
            {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
            stream1_index, stream1_codec_name, stream1_codec_type, stream1_sample_rate, stream1_channels, stream1_channel_layout, stream1_bits_per_sample, stream1_bit_rate, stream1_frame_size, stream1_duration, stream1_start_time, stream1_tag1,
            stream2_index, stream2_codec_name, stream2_codec_type, stream2_sample_rate, stream2_channels, stream2_channel_layout, stream2_bits_per_sample, stream2_bit_rate, stream2_frame_size, stream2_duration, stream2_start_time, stream2_tag1
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )",
        MediaTrackConstants::kFormatTag_title,
        MediaTrackConstants::kFormatTag_artist,
        MediaTrackConstants::kFormatTag_album,
        MediaTrackConstants::kFormatTag_album_artist,
        MediaTrackConstants::kFormatTag_genre,
        MediaTrackConstants::kFormatTag_track,
        MediaTrackConstants::kFormatTag_track_total,
        MediaTrackConstants::kFormatTag_date,
        MediaTrackConstants::kFormatTag_year,
        MediaTrackConstants::kFormatTag_comment,
        MediaTrackConstants::kFormatTag_disc,

        MediaTrackConstants::kFormatTag_composer,
        MediaTrackConstants::kFormatTag_publisher,
        MediaTrackConstants::kFormatTag_label,
        MediaTrackConstants::kFormatTag_organization,
        MediaTrackConstants::kFormatTag_copyright,
        MediaTrackConstants::kFormatTag_encoder,
        MediaTrackConstants::kFormatTag_encoded_by,
        MediaTrackConstants::kFormatTag_album_dynamic_range,
        MediaTrackConstants::kFormatTag_dynamic_range
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
    sqlite3_bind_int64(stmt, bindIndex++, mediaInfo.format.file_size.value_or(-1));
    sqlite3_bind_int(stmt, bindIndex++, mediaInfo.format.nb_streams);
    sqlite3_bind_int(stmt, bindIndex++, mediaInfo.format.nb_programs);
    sqlite3_bind_int(stmt, bindIndex++, mediaInfo.format.nb_stream_groups);
    sqlite3_bind_text(stmt, bindIndex++, mediaInfo.format.format_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, bindIndex++, mediaInfo.format.format_long_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, bindIndex++, mediaInfo.format.start_time.value_or(0));
    sqlite3_bind_double(stmt, bindIndex++, mediaInfo.format.duration.value_or(0.0));
    sqlite3_bind_int64(stmt, bindIndex++, mediaInfo.format.bit_rate.value_or(0));
    sqlite3_bind_int(stmt, bindIndex++, mediaInfo.format.probe_score);

    auto& tags = mediaInfo.format.tags;
    if (tags.has_value())
    {
		auto& tagsObj = tags.value();
        
        auto getTagValue = [](const JsonUtils::Tags& map, const std::string& key) -> const char * {
                auto it = map.find(key);
                return (it != map.end()) ? it->second.c_str() : nullptr;
            };

        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_title), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_artist), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_album), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_album_artist), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_genre), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_track), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_track_total), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_date), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_year), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_comment), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_disc), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_composer), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_publisher), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_label), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_organization), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_copyright), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_encoder), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_encoded_by), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_album_dynamic_range), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, getTagValue(tagsObj, MediaTrackConstants::kFormatTag_dynamic_range), -1, SQLITE_TRANSIENT);
    }

    if (auto stream1 = mediaInfo.streams.size() > 0 ? std::optional<Stream>{mediaInfo.streams[0]} : std::nullopt; stream1.has_value()) {
        sqlite3_bind_int(stmt, bindIndex++, stream1->index);
        sqlite3_bind_text(stmt, bindIndex++, stream1->codec_name.value_or("").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, stream1->codec_type.value_or("").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, bindIndex++, stream1->sample_rate.value_or("").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, bindIndex++, stream1->channels.value_or(0));
        sqlite3_bind_text(stmt, bindIndex++, stream1->channel_layout.value_or("").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, bindIndex++, stream1->bits_per_sample.value_or(0));
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
        sqlite3_bind_int64(stmt, bindIndex++, stream2->bits_per_sample.value_or(0));
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




