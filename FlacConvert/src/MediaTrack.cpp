
#include <iostream>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <array>
#include <cstdlib>
#include <cwchar>
#include <algorithm>
#include <map>
#include <filesystem>
#include <fstream>
#include <codecvt>
#include <charconv>
#include <cctype>
#include <cmath>
#include <format>
#include <limits>

#include <spdlog/spdlog.h>

#include "MediaTrack.h"
#include "rapidjson/error/en.h"
#include "ffmpeg.h"
#include "PlatformUtils.h"
#include "JsonUtils.h"
#include "AppSettings.h"

namespace {

template <typename T>
std::optional<T> parseIntegerMember(const rapidjson::Value& object, const char* name)
{
    if (auto value = JsonUtils::tryParseMember<T>(object, name)) {
        return value;
    }
    if constexpr (std::is_unsigned_v<T>) {
        if (auto signedValue = JsonUtils::tryParseMember<int64_t>(object, name);
            signedValue && *signedValue >= 0 &&
            static_cast<uint64_t>(*signedValue) <= std::numeric_limits<T>::max()) {
            return static_cast<T>(*signedValue);
        }
    }
    const auto text = JsonUtils::tryParseMember<std::string>(object, name);
    if (!text) {
        return std::nullopt;
    }
    T value{};
    const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), value);
    if (error == std::errc{} && end == text->data() + text->size()) {
        return value;
    }
    return std::nullopt;
}

std::optional<double> parseDoubleMember(const rapidjson::Value& object, const char* name)
{
    if (auto value = JsonUtils::tryParseMember<double>(object, name)) {
        return value;
    }
    if (auto value = parseIntegerMember<int64_t>(object, name)) {
        return static_cast<double>(*value);
    }
    const auto text = JsonUtils::tryParseMember<std::string>(object, name);
    if (!text) {
        return std::nullopt;
    }
    double value = 0.0;
    const auto [end, error] = std::from_chars(
        text->data(), text->data() + text->size(), value, std::chars_format::general);
    if (error == std::errc{} && end == text->data() + text->size() && std::isfinite(value)) {
        return value;
    }
    return std::nullopt;
}

std::optional<std::string> parseStringMember(const rapidjson::Value& object, const char* name)
{
    if (auto value = JsonUtils::tryParseMember<std::string>(object, name)) {
        return value;
    }
    if (auto value = parseIntegerMember<int64_t>(object, name)) {
        return std::to_string(*value);
    }
    if (auto value = parseDoubleMember(object, name)) {
        return std::to_string(*value);
    }
    return std::nullopt;
}

}

bool MediaTrack::IsFileAcceptedAudioFile(std::filesystem::path filePath)
{
    if (!filePath.has_extension()) return false;
    auto extension = filePath.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    const auto settings = AppSettingsJson::AppSetting();
    return std::ranges::find(settings->CatalogExtensions, extension) !=
        settings->CatalogExtensions.end();
}

//returns media information (json string and media objec) from a media file (on file system)
MetadataReadResult MediaTrack::ReadMetadataInfoFromFile(
    std::filesystem::path mediaFilePath, const bool bIncludeAudioQualityMetrics)
{
    if (bIncludeAudioQualityMetrics) {
        static std::once_flag analysisWarning;
        std::call_once(analysisWarning, [] {
            spdlog::warn("ExtraAudioQualityMetrics is currently unsupported and ignored; audio analysis is deferred.");
        });
    }

    try
    {
        std::string error;
        auto mediaInfo = FFmpeg::GetFFprobeMetadataAPI(
            mediaFilePath, bIncludeAudioQualityMetrics, &error);
        return { std::move(mediaInfo), std::nullopt,
            error.empty() ? std::nullopt : std::optional<std::string>{std::move(error)} };
    }
    catch (const std::exception& ex) {
        const std::string error = ex.what();
        spdlog::error("Metadata extraction failed for '{}': {}",
            CommonUtils::path_to_utf8(mediaFilePath), error);
        return { {}, std::nullopt, error };
    }
}



//Helper functions to parse a member from a JSON object
//Parse the format section of the FFprobe output
bool TryParseFFprobeFormat(const Value& doc, FFprobeOutput &mediaInfo)
{
    if (doc.HasMember("format") && doc["format"].IsObject()) {
        Format& format = mediaInfo.format;

        const auto& formatTag = doc["format"];
        if (auto value = JsonUtils::tryParseMember<std::string>(formatTag, "filename")) { format.filename = *value; }
        if (auto value = parseIntegerMember<int64_t>(formatTag, "fs_file_size"); value && *value >= 0) {
            format.fs_file_size = static_cast<uint64_t>(*value);
        }
        if (auto value = parseIntegerMember<int64_t>(formatTag, "file_size")) { format.file_size = *value; }
        else if (auto value = parseIntegerMember<int64_t>(formatTag, "size")) { format.file_size = *value; }
        if (auto value = JsonUtils::tryParseMember<std::string>(formatTag, "url")) { format.url = *value; }

        if (auto value = parseIntegerMember<int>(formatTag, "ctx_flags")) { format.ctx_flags = *value; }
        if (auto value = parseIntegerMember<int>(formatTag, "nb_streams")) { format.nb_streams = *value; }
        if (auto value = parseIntegerMember<int>(formatTag, "nb_programs")) { format.nb_programs = *value; }
        if (auto value = parseIntegerMember<unsigned int>(formatTag, "nb_stream_groups")) { format.nb_stream_groups = *value; }
        if (auto value = parseIntegerMember<unsigned int>(formatTag, "nb_chapters")) { format.nb_chapters = *value; }

        if (auto value = JsonUtils::tryParseMember<std::string>(formatTag, "codec_name")) { format.audio_codec_name = *value; }
        if (auto value = JsonUtils::tryParseMember<std::string>(formatTag, "format_name")) { format.format_name = *value; }
        if (auto value = JsonUtils::tryParseMember<std::string>(formatTag, "format_long_name")) { format.format_long_name = *value; }
        if (auto value = parseDoubleMember(formatTag, "start_time")) { format.start_time = *value; }
        if (auto value = parseDoubleMember(formatTag, "duration")) { format.duration = *value; }
        if (auto value = parseIntegerMember<int64_t>(formatTag, "bit_rate")) { format.bit_rate = *value; }
        if (auto value = parseIntegerMember<unsigned int>(formatTag, "packet_size")) { format.packet_size = *value; }
        if (auto value = parseIntegerMember<int>(formatTag, "max_delay")) { format.max_delay = *value; }
        if (auto value = parseIntegerMember<int>(formatTag, "flags")) { format.flags = *value; }
        if (auto value = parseIntegerMember<int>(formatTag, "probe_score")) { format.probe_score = *value; }

        if (auto value = parseIntegerMember<int>(formatTag, "audio_codec_id")) { format.audio_codec_id = static_cast<AVCodecID>(*value); }

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
    bool parsed = false;

    if (doc.HasMember("streams") && doc["streams"].IsArray()) {
        for (const auto& s : doc["streams"].GetArray()) {
            if (!s.IsObject()) {
                continue;
            }
            Stream stream;
            stream.index = parseIntegerMember<int>(s, "index");
            stream.codec_name = parseStringMember(s, "codec_name");
            stream.codec_long_name = parseStringMember(s, "codec_long_name");
            stream.codec_type = parseStringMember(s, "codec_type");
            stream.codec_time_base = parseStringMember(s, "codec_time_base");
            stream.codec_tag_string = parseStringMember(s, "codec_tag_string");
            stream.codec_tag = parseStringMember(s, "codec_tag");
            stream.sample_fmt = parseStringMember(s, "sample_fmt");
            stream.sample_rate = parseStringMember(s, "sample_rate");
            stream.channels = parseIntegerMember<int>(s, "channels");
            stream.channel_layout = parseStringMember(s, "channel_layout");
            stream.bit_rate = parseIntegerMember<int64_t>(s, "bit_rate");
            stream.bits_per_sample = parseIntegerMember<int>(s, "bits_per_sample");
            stream.frame_size = parseIntegerMember<int>(s, "frame_size");
            stream.nb_frames = parseIntegerMember<int64_t>(s, "nb_frames");
            stream.r_frame_rate = parseStringMember(s, "r_frame_rate");
            stream.avg_frame_rate = parseStringMember(s, "avg_frame_rate");
            stream.time_base = parseStringMember(s, "time_base");
            stream.start_pts = parseIntegerMember<int64_t>(s, "start_pts");
            stream.start_time = parseDoubleMember(s, "start_time");
            stream.duration_ts = parseIntegerMember<int64_t>(s, "duration_ts");
            stream.duration = parseStringMember(s, "duration");
            if (s.HasMember("disposition") && s["disposition"].IsObject()) {
                const auto& jsonDisposition = s["disposition"];
                Disposition disposition;
                const auto parseFlag = [&jsonDisposition](const char* name, int& flag) {
                    if (auto value = parseIntegerMember<int>(jsonDisposition, name)) {
                        flag = *value;
                    }
                };
                parseFlag("default", disposition.default_stream);
                parseFlag("dub", disposition.dub);
                parseFlag("original", disposition.original);
                parseFlag("comment", disposition.comment);
                parseFlag("lyrics", disposition.lyrics);
                parseFlag("karaoke", disposition.karaoke);
                parseFlag("forced", disposition.forced);
                parseFlag("hearing_impaired", disposition.hearing_impaired);
                parseFlag("visual_impaired", disposition.visual_impaired);
                parseFlag("clean_effects", disposition.clean_effects);
                parseFlag("attached_pic", disposition.attached_pic);
                parseFlag("timed_thumbnails", disposition.timed_thumbnails);
                stream.disposition = disposition;
            }
            if (s.HasMember("tags") && s["tags"].IsObject()) {
                stream.tags = JsonUtils::GetKeyValueMap(s["tags"]);
            }
            mediaInfo.streams.push_back(stream);
			parsed = true;
        }
    }

    return parsed;
}

bool TryParseFFprobeChapters(const Value& doc, FFprobeOutput& mediaInfo)
{
    if (!doc.HasMember("chapters") || !doc["chapters"].IsArray()) {
        return false;
    }

    for (const auto& value : doc["chapters"].GetArray()) {
        if (!value.IsObject()) {
            continue;
        }
        Chapter chapter;
        if (auto id = parseIntegerMember<int64_t>(value, "id")) { chapter.id = *id; }
        if (auto timeBase = parseStringMember(value, "time_base")) { chapter.time_base = *timeBase; }
        if (auto start = parseIntegerMember<int64_t>(value, "start")) { chapter.start = *start; }
        if (auto startTime = parseStringMember(value, "start_time")) { chapter.start_time = *startTime; }
        if (auto end = parseIntegerMember<int64_t>(value, "end")) { chapter.end = *end; }
        if (auto endTime = parseStringMember(value, "end_time")) { chapter.end_time = *endTime; }
        if (value.HasMember("tags") && value["tags"].IsObject()) {
            chapter.tags = JsonUtils::GetKeyValueMap(value["tags"]);
        }
        mediaInfo.chapters.push_back(std::move(chapter));
    }
    return true;
}

FFprobeOutput MediaTrack::ParseFFprobeInformation(const Value& doc)
{
    FFprobeOutput mediaInfo;
    if (!doc.IsObject()) {
        return mediaInfo;
    }

    TryParseFFprobeFormat(doc, mediaInfo);
    TryParseFFprobeStreams(doc, mediaInfo);
    TryParseFFprobeChapters(doc, mediaInfo);

    return mediaInfo;
}

std::string MediaTrack::toJsonString(const FFprobeOutput& output) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    const auto writeString = [&writer](const char* key, const std::string& value) {
        if (value.size() > (std::numeric_limits<rapidjson::SizeType>::max)()) {
            throw std::length_error("Metadata string is too large to serialize as JSON.");
        }
        writer.Key(key);
        writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
    };
    const auto writeOptionalString = [&writeString](const char* key,
        const std::optional<std::string>& value) {
        if (value) {
            writeString(key, *value);
        }
    };
    const auto writeTags = [&writer](const char* key, const JsonUtils::Tags& tags) {
        writer.Key(key);
        writer.StartObject();
        for (const auto& [tagKey, tagValue] : tags) {
            if (tagKey.size() > (std::numeric_limits<rapidjson::SizeType>::max)() ||
                tagValue.size() > (std::numeric_limits<rapidjson::SizeType>::max)()) {
                throw std::length_error("Metadata tag is too large to serialize as JSON.");
            }
            writer.Key(tagKey.data(), static_cast<rapidjson::SizeType>(tagKey.size()));
            writer.String(tagValue.data(), static_cast<rapidjson::SizeType>(tagValue.size()));
        }
        writer.EndObject();
    };

    writer.StartObject();
    writer.Key("audio_analysis");
    writer.StartObject();
    writer.Key("status");
    writer.String("not_run");
    writer.EndObject();

    const Format& format = output.format;
    writer.Key("format");
    writer.StartObject();
    if (!format.filename.empty()) writeString("filename", format.filename);
    if (format.fs_file_size) {
        writer.Key("fs_file_size");
        writer.Uint64(*format.fs_file_size);
    }
    if (format.file_size) {
        writer.Key("file_size");
        writer.Int64(*format.file_size);
    }
    writeOptionalString("url", format.url);
    if (format.audio_codec_id && *format.audio_codec_id != AV_CODEC_ID_NONE) {
        writer.Key("audio_codec_id");
        writer.Int(static_cast<int>(*format.audio_codec_id));
        writeOptionalString("codec_name", format.audio_codec_name);
    }
    writeOptionalString("format_name", format.format_name);
    writeOptionalString("format_long_name", format.format_long_name);
    if (format.ctx_flags) {
        writer.Key("ctx_flags");
        writer.Int(*format.ctx_flags);
    }
    if (format.nb_streams) {
        writer.Key("nb_streams");
        writer.Int(*format.nb_streams);
    }
    if (format.nb_programs) {
        writer.Key("nb_programs");
        writer.Int(*format.nb_programs);
    }
    if (format.nb_stream_groups) {
        writer.Key("nb_stream_groups");
        writer.Uint(*format.nb_stream_groups);
    }
    if (format.nb_chapters) {
        writer.Key("nb_chapters");
        writer.Uint(*format.nb_chapters);
    }
    if (format.start_time) {
        writer.Key("start_time");
        writer.Double(*format.start_time);
    }
    if (format.duration) {
        writer.Key("duration");
        writer.Double(*format.duration);
    }
    if (format.bit_rate) {
        writer.Key("bit_rate");
        writer.Int64(*format.bit_rate);
    }
    if (format.packet_size) {
        writer.Key("packet_size");
        writer.Uint(*format.packet_size);
    }
    if (format.max_delay) {
        writer.Key("max_delay");
        writer.Int(*format.max_delay);
    }
    if (format.flags) {
        writer.Key("flags");
        writer.Int(*format.flags);
    }
    if (format.probe_score) {
        writer.Key("probe_score");
        writer.Int(*format.probe_score);
    }
    if (format.tags) writeTags("tags", *format.tags);
    writer.EndObject();

    if (!output.streams.empty()) {
        writer.Key("streams");
        writer.StartArray();
        for (const Stream& stream : output.streams) {
            writer.StartObject();
            if (stream.index) {
                writer.Key("index");
                writer.Int(*stream.index);
            }
            writeOptionalString("codec_name", stream.codec_name);
            writeOptionalString("codec_long_name", stream.codec_long_name);
            writeOptionalString("codec_type", stream.codec_type);
            writeOptionalString("codec_time_base", stream.codec_time_base);
            writeOptionalString("codec_tag_string", stream.codec_tag_string);
            writeOptionalString("codec_tag", stream.codec_tag);
            writeOptionalString("sample_fmt", stream.sample_fmt);
            writeOptionalString("sample_rate", stream.sample_rate);
            if (stream.channels) {
                writer.Key("channels");
                writer.Int(*stream.channels);
            }
            writeOptionalString("channel_layout", stream.channel_layout);
            if (stream.bit_rate) {
                writer.Key("bit_rate");
                writer.Int64(*stream.bit_rate);
            }
            if (stream.bits_per_sample) {
                writer.Key("bits_per_sample");
                writer.Int(*stream.bits_per_sample);
            }
            if (stream.frame_size) {
                writer.Key("frame_size");
                writer.Int(*stream.frame_size);
            }
            if (stream.nb_frames) {
                writer.Key("nb_frames");
                writer.Int64(*stream.nb_frames);
            }
            writeOptionalString("r_frame_rate", stream.r_frame_rate);
            writeOptionalString("avg_frame_rate", stream.avg_frame_rate);
            writeOptionalString("time_base", stream.time_base);
            if (stream.start_pts) {
                writer.Key("start_pts");
                writer.Int64(*stream.start_pts);
            }
            if (stream.start_time) {
                writer.Key("start_time");
                writer.Double(*stream.start_time);
            }
            if (stream.duration_ts) {
                writer.Key("duration_ts");
                writer.Int64(*stream.duration_ts);
            }
            writeOptionalString("duration", stream.duration);
            if (stream.disposition) {
                const Disposition& disposition = *stream.disposition;
                writer.Key("disposition");
                writer.StartObject();
                writer.Key("default"); writer.Bool(disposition.default_stream != 0);
                writer.Key("dub"); writer.Bool(disposition.dub != 0);
                writer.Key("original"); writer.Bool(disposition.original != 0);
                writer.Key("comment"); writer.Bool(disposition.comment != 0);
                writer.Key("lyrics"); writer.Bool(disposition.lyrics != 0);
                writer.Key("karaoke"); writer.Bool(disposition.karaoke != 0);
                writer.Key("forced"); writer.Bool(disposition.forced != 0);
                writer.Key("hearing_impaired"); writer.Bool(disposition.hearing_impaired != 0);
                writer.Key("visual_impaired"); writer.Bool(disposition.visual_impaired != 0);
                writer.Key("clean_effects"); writer.Bool(disposition.clean_effects != 0);
                writer.Key("attached_pic"); writer.Bool(disposition.attached_pic != 0);
                writer.Key("timed_thumbnails"); writer.Bool(disposition.timed_thumbnails != 0);
                writer.EndObject();
            }
            if (stream.tags) writeTags("tags", *stream.tags);
            writer.EndObject();
        }
        writer.EndArray();
    }

    if (!output.chapters.empty()) {
        writer.Key("chapters");
        writer.StartArray();
        for (const Chapter& chapter : output.chapters) {
            writer.StartObject();
            if (chapter.id) {
                writer.Key("id");
                writer.Int64(*chapter.id);
            }
            writeOptionalString("time_base", chapter.time_base);
            if (chapter.start) {
                writer.Key("start");
                writer.Int64(*chapter.start);
            }
            writeOptionalString("start_time", chapter.start_time);
            if (chapter.end) {
                writer.Key("end");
                writer.Int64(*chapter.end);
            }
            writeOptionalString("end_time", chapter.end_time);
            if (chapter.tags) writeTags("tags", *chapter.tags);
            writer.EndObject();
        }
        writer.EndArray();
    }
    writer.EndObject();

    if (!writer.IsComplete()) {
        throw std::runtime_error("Failed to serialize metadata JSON.");
    }
    return std::string(buffer.GetString(), buffer.GetSize());
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


bool MediaTrack::CompareAudioTracks(const std::vector<MediaTrack>& mediaInfoList1, const std::vector<MediaTrack>& mediaInfoList2)
{
    bool bAllSame{ true };

    size_t minSize = std::min(mediaInfoList1.size(), mediaInfoList2.size());

    for (size_t i = 0; i < minSize; ++i) {
        const auto& info1 = mediaInfoList1[i].formatInfo;
        const auto& info2 = mediaInfoList2[i].formatInfo;

        //spdlog::info("Comparing Track #{}, \"{}\" vs \"{}\"", i + 1, info1.format.filename, info2.format.filename);

        if (!info1.audio_metrics.has_value() || !info2.audio_metrics.has_value()) {
            spdlog::warn("  Skipped: Audio analysis was not run.");
            continue;
        }

        const auto& m1 = info1.audio_metrics.value();
        const auto& m2 = info2.audio_metrics.value();

        bool different = false;

        if (m1.codec_name != m2.codec_name) {
            spdlog::warn("  Codec mismatch: {} vs {}", m1.codec_name, m2.codec_name);
            different = true;
        }

        if (m1.sample_rate != m2.sample_rate) {
            spdlog::warn("  Sample rate mismatch: {} Hz vs {} Hz", m1.sample_rate, m2.sample_rate);
            different = true;
        }

        if (m1.channels != m2.channels) {
            spdlog::warn("  Channels mismatch: {} vs {}", m1.channels, m2.channels);
            different = true;
        }

        if (m1.bitrate != m2.bitrate) {
            spdlog::warn("  Bitrate mismatch: {} vs {}", m1.bitrate, m2.bitrate);
            different = true;
        }

        if (m1.is_lossless != m2.is_lossless) {
            spdlog::warn("  Lossless flag mismatch: {} vs {}", (m1.is_lossless ? "Yes" : "No"), (m2.is_lossless ? "Yes" : "No"));
            different = true;
        }

        if (m1.is_high_quality != m2.is_high_quality) {
            spdlog::warn("  Quality flag mismatch: {} vs {}", (m1.is_high_quality ? "High" : "Low"), (m2.is_high_quality ? "High" : "Low"));
            different = true;
        }

        if (!different) {
//            spdlog::info(" Audio quality matches.\n");
        }

        // Optionally compare audio_quality fields
        if (info1.audio_quality && info2.audio_quality) {
            const auto& q1 = info1.audio_quality.value();
            const auto& q2 = info2.audio_quality.value();

            if (std::abs(q1.dynamic_range_db - q2.dynamic_range_db) > 1.0f) {
                spdlog::warn("  Dynamic range differs: {} dB vs {} dB", q1.dynamic_range_db, q2.dynamic_range_db);

                different = true;
            }

            if (std::abs(q1.peak_amplitude - q2.peak_amplitude) > 0.05f) {
                spdlog::warn("  Peak amplitude differs: {} vs {}", q1.peak_amplitude, q2.peak_amplitude);

                different = true;
            }

            // Add other checks as needed
        }

        if (different) {
            bAllSame = false;
        }
    }

    // Check for extra tracks
    if (mediaInfoList1.size() != mediaInfoList2.size()) {
        spdlog::warn("\nDifferent number of tracks: {} vs {}", mediaInfoList1.size(), mediaInfoList2.size());

        bAllSame = false;
    }

    return bAllSame;
}

std::string MediaTrack::GenerateComparisonReport(
    const std::vector<MediaTrack>& mediaInfoList1,
    const std::vector<MediaTrack>& mediaInfoList2)
{
    std::ostringstream report;
    report << std::left << std::setw(30) << "Metric"
        << std::setw(35) << "Album 1"
        << std::setw(35) << "Album 2"
        << "Match\n";
    report << std::string(110, '-') << '\n';

    size_t maxSize = std::max(mediaInfoList1.size(), mediaInfoList2.size());

    for (size_t i = 0; i < maxSize; ++i) {
        report << "Track #" << i + 1 << "\n";

        if (i >= mediaInfoList1.size() || i >= mediaInfoList2.size()) {
            report << "  Track missing in one of the albums.\n\n";
            continue;
        }

        const auto& info1 = mediaInfoList1[i].formatInfo;
        const auto& info2 = mediaInfoList2[i].formatInfo;

        if (!info1.audio_metrics.has_value() || !info2.audio_metrics.has_value()) {
            report << "  Audio analysis not run; metric comparison skipped.\n\n";
            continue;
        }

        const auto& m1 = info1.audio_metrics.value();
        const auto& m2 = info2.audio_metrics.value();

        auto compare = [&](const std::string& label, const auto& val1, const auto& val2) {
            bool match = (val1 == val2);
            report << std::setw(30) << label
                << std::setw(35) << val1
                << std::setw(35) << val2
                << (match ? "Yes" : "No") << "\n";
            return match;
            };

        compare("Codec", m1.codec_name, m2.codec_name);
        compare("Sample Rate (Hz)", m1.sample_rate, m2.sample_rate);
        compare("Channels", m1.channels, m2.channels);
        compare("Bitrate", m1.bitrate, m2.bitrate);
        compare("Lossless", m1.is_lossless ? "Yes" : "No", m2.is_lossless ? "Yes" : "No");
        compare("High Quality", m1.is_high_quality ? "Yes" : "No", m2.is_high_quality ? "Yes" : "No");

        if (info1.audio_quality && info2.audio_quality) {
            const auto& q1 = info1.audio_quality.value();
            const auto& q2 = info2.audio_quality.value();

            auto float_compare = [&](const std::string& label, float v1, float v2, float tolerance) {
                bool match = std::abs(v1 - v2) <= tolerance;
                report << std::setw(30) << label
                    << std::setw(35) << std::fixed << std::setprecision(2) << v1
                    << std::setw(35) << std::fixed << std::setprecision(2) << v2
                    << (match ? "Yes" : "No") << "\n";
                return match;
                };

            float_compare("Dynamic Range (dB)", q1.dynamic_range_db, q2.dynamic_range_db, 1.0f);
            float_compare("Peak Amplitude", q1.peak_amplitude, q2.peak_amplitude, 0.05f);
        }

        report << std::string(110, '-') << "\n";
    }

    if (mediaInfoList1.size() != mediaInfoList2.size()) {
        report << "\nNOTE: Album track count differs: "
            << mediaInfoList1.size() << " vs " << mediaInfoList2.size() << "\n";
    }

    return report.str();
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
            start_time REAL,
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
            stream1_bits_per_sample INTEGER,
            stream1_bit_rate INTEGER,
            stream1_frame_size INTEGER,
            stream1_duration REAL,
            stream1_start_time REAL,
            stream1_tag1 TEXT,
            stream2_index INTEGER,
            stream2_codec_name TEXT,
            stream2_codec_type TEXT,
            stream2_sample_rate TEXT,
            stream2_channels INTEGER,
            stream2_channel_layout TEXT,
            stream2_bits_per_sample INTEGER,
            stream2_bit_rate INTEGER,
            stream2_frame_size INTEGER,
            stream2_duration REAL,
            stream2_start_time REAL,
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
    const auto& trackName = track.trackPath;
    const auto& size = track.fs_fileSize;
    const auto& mediaInfo = track.formatInfo;
    std::string albumPathUtf8 = PlatformUtils::wstringToUtf8_ver2(albumPath);

    std::optional<std::string> stream1Tag1; // Placeholder for stream tags
    std::optional<std::string> stream2Tag1;

    int bindIndex = 1;
    const auto bindOptionalInteger = [&](const auto& value) {
        if (value) {
            sqlite3_bind_int64(stmt, bindIndex++, static_cast<sqlite3_int64>(*value));
        }
        else {
            sqlite3_bind_null(stmt, bindIndex++);
        }
    };
    const auto bindOptionalDouble = [&](const std::optional<double>& value) {
        if (value) {
            sqlite3_bind_double(stmt, bindIndex++, *value);
        }
        else {
            sqlite3_bind_null(stmt, bindIndex++);
        }
    };
    const auto bindOptionalText = [&](const std::optional<std::string>& value) {
        if (value) {
            sqlite3_bind_text(stmt, bindIndex++, value->c_str(), -1, SQLITE_TRANSIENT);
        }
        else {
            sqlite3_bind_null(stmt, bindIndex++);
        }
    };
    sqlite3_bind_null(stmt, bindIndex++); // id
    sqlite3_bind_text(stmt, bindIndex++, albumPathUtf8.c_str(), -1, SQLITE_TRANSIENT);
    bindOptionalInteger(mediaInfo.format.file_size);
    bindOptionalInteger(mediaInfo.format.nb_streams);
    bindOptionalInteger(mediaInfo.format.nb_programs);
    bindOptionalInteger(mediaInfo.format.nb_stream_groups);
    bindOptionalText(mediaInfo.format.format_name);
    bindOptionalText(mediaInfo.format.format_long_name);
    bindOptionalDouble(mediaInfo.format.start_time);
    bindOptionalDouble(mediaInfo.format.duration);
    bindOptionalInteger(mediaInfo.format.bit_rate);
    bindOptionalInteger(mediaInfo.format.probe_score);

    static const JsonUtils::Tags emptyTags;
    const JsonUtils::Tags& tagsObj = mediaInfo.format.tags ? *mediaInfo.format.tags : emptyTags;
    auto getTagValue = [](const JsonUtils::Tags& map, const std::string& key) -> const char* {
        const auto it = map.find(key);
        return it != map.end() ? it->second.c_str() : nullptr;
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

    if (auto stream1 = mediaInfo.streams.size() > 0 ? std::optional<Stream>{mediaInfo.streams[0]} : std::nullopt; stream1.has_value()) {
        bindOptionalInteger(stream1->index);
        bindOptionalText(stream1->codec_name);
        bindOptionalText(stream1->codec_type);
        bindOptionalText(stream1->sample_rate);
        bindOptionalInteger(stream1->channels);
        bindOptionalText(stream1->channel_layout);
        bindOptionalInteger(stream1->bits_per_sample);
        bindOptionalInteger(stream1->bit_rate);
        bindOptionalInteger(stream1->frame_size);
        bindOptionalText(stream1->duration);
        bindOptionalDouble(stream1->start_time);
        bindOptionalText(stream1Tag1);
    }
    else {
        for (int i = 0; i < 11; ++i) sqlite3_bind_null(stmt, bindIndex++);
    }

    if (auto stream2 = mediaInfo.streams.size() > 1 ? std::optional<Stream>{mediaInfo.streams[1]} : std::nullopt; stream2.has_value()) {
        bindOptionalInteger(stream2->index);
        bindOptionalText(stream2->codec_name);
        bindOptionalText(stream2->codec_type);
        bindOptionalText(stream2->sample_rate);
        bindOptionalInteger(stream2->channels);
        bindOptionalText(stream2->channel_layout);
        bindOptionalInteger(stream2->bits_per_sample);
        bindOptionalInteger(stream2->bit_rate);
        bindOptionalInteger(stream2->frame_size);
        bindOptionalText(stream2->duration);
        bindOptionalDouble(stream2->start_time);
        bindOptionalText(stream2Tag1);
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
