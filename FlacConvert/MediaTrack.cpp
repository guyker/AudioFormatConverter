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



#include <iostream>
#include <string>
#include <map>
extern "C" {
#include <libavformat/avformat.h>
}

//std::tuple<FFprobeOutput, std::wstring> ReadMediaInfoFromFile(std::filesystem::path mediaFilePath)
//{
//
//}

std::string escapePath(const std::string& path) {
    std::string result;
    for (char c : path) {
        if (c == ' ') result += "%20";
        else if (c == '\'') result += "%27"; // Standard single quote
        else if (c == '’') result += "%E2%80%99"; // Smart quote (UTF-8: E2 80 99)
        else result += c;
    }
    return result;
}

std::string normalizePath(const std::string& path) {
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

#include <string>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <windows.h> // For Windows path handling
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}
std::string WideStringToUTF8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8Str(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &utf8Str[0], sizeNeeded, nullptr, nullptr);
    return utf8Str;
}

FFprobeOutput extractMetadata(const std::filesystem::path filePath) {
    FFprobeOutput output;

    // Get the UTF-8 encoded string. Depending on your implementation,
    // u8string() might return std::string or std::u8string.
    std::wstring widePath = filePath.wstring();
    std::string utf8Path = WideStringToUTF8(widePath);

    AVFormatContext* fmt_ctx = nullptr;
    int ret = avformat_open_input(&fmt_ctx, utf8Path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Failed to open " << utf8Path.c_str() << ": " << errbuf << " (" << ret << ")\n";
        return output;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream info\n";
        avformat_close_input(&fmt_ctx);
        return output;
    }

    // Format section
    Format fmt;
    fmt.filename = filePath.generic_string();
    fmt.nb_streams = fmt_ctx->nb_streams;
    fmt.format_name = fmt_ctx->iformat->name ? fmt_ctx->iformat->name : "";
    fmt.format_long_name = fmt_ctx->iformat->long_name ? fmt_ctx->iformat->long_name : "";
    if (fmt_ctx->duration != AV_NOPTS_VALUE) {
        fmt.duration = static_cast<double>(fmt_ctx->duration) / AV_TIME_BASE;
    }
    if (fmt_ctx->bit_rate > 0) {
        fmt.bit_rate = fmt_ctx->bit_rate;
    }
    if (fmt_ctx->start_time != AV_NOPTS_VALUE) {
        fmt.start_time = fmt_ctx->start_time;
    }
    fmt.probe_score = fmt_ctx->probe_score;

    // Optional: Get file size from filesystem
    try {
        fmt.file_size = std::filesystem::file_size(filePath);
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Failed to get file size: " << e.what() << "\n";
    }

    if (fmt_ctx->metadata) {
        Tags format_tags;
        AVDictionaryEntry* tag = nullptr;
        while ((tag = av_dict_get(fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
            format_tags[tag->key] = tag->value;
        }
        fmt.tags = format_tags;
    }
    output.format = fmt;

    // Streams section
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
        AVStream* stream = fmt_ctx->streams[i];
        Stream s;
        s.index = stream->index;
        AVCodecParameters* codecpar = stream->codecpar;
        s.codec_name = avcodec_get_name(codecpar->codec_id);
        switch (codecpar->codec_type) {
        case AVMEDIA_TYPE_AUDIO: s.codec_type = "audio"; break;
        case AVMEDIA_TYPE_VIDEO: s.codec_type = "video"; break;
        case AVMEDIA_TYPE_SUBTITLE: s.codec_type = "subtitle"; break;
        default: s.codec_type = "unknown"; break;
        }

        if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (codecpar->sample_rate > 0) {
                s.sample_rate = std::to_string(codecpar->sample_rate);
            }
            if (codecpar->ch_layout.nb_channels > 0) {
                s.channels = codecpar->ch_layout.nb_channels;
                char layout[64];
                av_channel_layout_describe(&codecpar->ch_layout, layout, sizeof(layout));
                s.channel_layout = layout;
            }
            if (codecpar->bit_rate > 0) {
                s.bit_rate = codecpar->bit_rate;
            }
            if (codecpar->bits_per_raw_sample > 0) {
                s.bits_per_sample = codecpar->bits_per_raw_sample;
            }
            if (codecpar->frame_size > 0) {
                s.frame_size = codecpar->frame_size;
            }
        }

        if (stream->duration != AV_NOPTS_VALUE) {
            s.duration = std::to_string(static_cast<double>(stream->duration) * av_q2d(stream->time_base));
        }
        if (stream->start_time != AV_NOPTS_VALUE) {
            s.start_time = stream->start_time;
        }
        if (stream->nb_frames > 0) {
            s.nb_frames = stream->nb_frames;
        }

        if (stream->metadata) {
            Tags stream_tags;
            AVDictionaryEntry* tag = nullptr;
            while ((tag = av_dict_get(stream->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
                stream_tags[tag->key] = tag->value;
            }
            s.tags = stream_tags;
        }

        output.streams.push_back(s);
    }

    avformat_close_input(&fmt_ctx);
    return output;
}



// Helper to escape JSON strings (basic version)
std::string escapeJsonString(const std::string& input) {
    std::ostringstream oss;
    for (char c : input) {
        switch (c) {
        case '"': oss << "\\\""; break;
        case '\\': oss << "\\\\"; break;
        case '\n': oss << "\\n"; break;
        case '\r': oss << "\\r"; break;
        case '\t': oss << "\\t"; break;
        default: oss << c; break;
        }
    }
    return oss.str();
}



#include <string>
#include <stdexcept>
#include <cstdint>
#include "PlatformUtils.h"

std::string wstringToStringUtf8(const std::wstring& wstr) {
    if (wstr.empty()) {
        return std::string();
    }

    std::string utf8;
    utf8.reserve(wstr.size() * 2); // Rough estimate for UTF-8 size

    for (size_t i = 0; i < wstr.size(); ++i) {
        uint32_t codepoint;
#if defined(_WIN32)
        // Windows: wchar_t is UTF-16 (2 bytes)
        wchar_t wc = wstr[i];
        if (wc >= 0xD800 && wc <= 0xDBFF) {
            // High surrogate
            if (i + 1 >= wstr.size()) {
                throw std::runtime_error("Incomplete UTF-16 surrogate pair");
            }
            wchar_t wc2 = wstr[++i];
            if (wc2 < 0xDC00 || wc2 > 0xDFFF) {
                throw std::runtime_error("Invalid UTF-16 surrogate pair");
            }
            codepoint = 0x10000 + ((wc - 0xD800) << 10) + (wc2 - 0xDC00);
        }
        else if (wc >= 0xDC00 && wc <= 0xDFFF) {
            throw std::runtime_error("Unexpected UTF-16 low surrogate");
        }
        else {
            codepoint = static_cast<uint32_t>(wc);
        }
#else
        // Linux/macOS: wchar_t is UTF-32 (4 bytes)
        codepoint = static_cast<uint32_t>(wstr[i]);
#endif

        // Convert codepoint to UTF-8
        if (codepoint <= 0x7F) {
            utf8 += static_cast<char>(codepoint);
        }
        else if (codepoint <= 0x7FF) {
            utf8 += static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
            utf8 += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else if (codepoint <= 0xFFFF) {
            utf8 += static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
            utf8 += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else if (codepoint <= 0x10FFFF) {
            utf8 += static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
            utf8 += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            utf8 += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else {
            throw std::runtime_error("Invalid Unicode codepoint");
        }
    }

    return utf8;
}

//// Convert wstring to string (assuming UTF-8)
//std::string wstringToString(const std::wstring& wstr) {
//    // Simple conversion; replace with proper UTF-8 conversion if needed
//    return std::string(wstr.begin(), wstr.end());
//}

std::string toJson(const FFprobeOutput& output) {
    std::ostringstream json;

    json << "{\n";

    // Format section
    //if (output.format)
    {
        json << "  \"format\": {\n";
        const Format& fmt = output.format;

        json << "    \"filename\": \"" << escapeJsonString(fmt.filename) << "\",\n";
        json << "    \"nb_streams\": " << fmt.nb_streams << ",\n";
        json << "    \"format_name\": \"" << escapeJsonString(fmt.format_name) << "\",\n";
        json << "    \"format_long_name\": \"" << escapeJsonString(fmt.format_long_name) << "\"";

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
                json << "      \"" << escapeJsonString(key) << "\": \"" << escapeJsonString(value) << "\"";
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
                json << ",\n      \"codec_name\": \"" << escapeJsonString(*s.codec_name) << "\"";
            }
            if (s.codec_type) {
                json << ",\n      \"codec_type\": \"" << escapeJsonString(*s.codec_type) << "\"";
            }
            if (s.sample_rate) {
                json << ",\n      \"sample_rate\": \"" << escapeJsonString(*s.sample_rate) << "\"";
            }
            if (s.channels) {
                json << ",\n      \"channels\": " << *s.channels;
            }
            if (s.channel_layout) {
                json << ",\n      \"channel_layout\": \"" << escapeJsonString(*s.channel_layout) << "\"";
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
                json << ",\n      \"duration\": \"" << escapeJsonString(*s.duration) << "\"";
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
                    json << "        \"" << escapeJsonString(key) << "\": \"" << escapeJsonString(value) << "\"";
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

std::map<std::string, std::string> extractTags(const std::string& filename) {
    std::map<std::string, std::string> tags;

    // Initialize FFmpeg (not needed in newer versions, but safe to call)
   // av_register_all();

    // Open the file
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, filename.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Could not open file: " << filename << "\n";
        return tags;
    }

    // Find stream info (populates metadata)
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream info\n";
        avformat_close_input(&fmt_ctx);
        return tags;
    }

    // Extract metadata tags
    AVDictionaryEntry* tag = nullptr;
    while ((tag = av_dict_get(fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        tags[tag->key] = tag->value;
    }

    // Clean up
    avformat_close_input(&fmt_ctx);
    return tags;
}
//std::map<std::string, std::string> returnVal = extractTags(mediaFilePath);


//returns media information (json string and media objec) from a media file (on file system)
std::tuple<FFprobeOutput, std::wstring> MediaTrack::ReadMediaInfoFromFile(std::filesystem::path mediaFilePath)
{

    try
    {        
        //return std::make_tuple(FFprobeOutput{}, L"{}");
        //


        //std::map<std::string, std::string> returnVal = extractTags(mediaFilePath.generic_string());

        auto mediaInfo = extractMetadata(mediaFilePath);
        auto jsonString2 = toJson(mediaInfo);
        //auto jsonString2 = MediaTrack::ExtractMetadataFromMediaTrack(mediaFilePath, tmpFile);

        return std::make_tuple(mediaInfo, CommonUtils::utf8ToWstring(jsonString2));

        std::size_t hashNumber = std::hash<std::wstring>{}(mediaFilePath);
        auto tmpFile = std::format("tmp_media_{}.json", hashNumber);

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
            resultTags[key] = itr->value.GetString();
        }
        else {
            // Handle non-string values (e.g., convert numbers to strings)
            // For ffprobe, tags are typically strings, but this is a fallback
            if (itr->value.IsNumber()) {
                resultTags[key] = std::to_string(itr->value.GetDouble());
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
            format.tags = parseTags(jsonValue);

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
                stream.tags = parseTags(s["tags"]);
            }
            mediaInfo.streams.push_back(stream);
        }
    }

    return false;
}

FFprobeOutput MediaTrack::ParseMediaTrack(const Value& doc)
{
    FFprobeOutput mediaInfo;

    TryParseFFprobeFormat(doc, mediaInfo);
    TryParseFFprobeStreams(doc, mediaInfo);

    return mediaInfo;
}

FFprobeOutput MediaTrack::ParseMediaTrack(std::wstring jsonString)
{
    rapidjson::Document doc;
    std::string utf8Json = PlatformUtils::wstringToUtf8_ver2(jsonString);
    doc.Parse(utf8Json.c_str());

    if (doc.HasParseError()) {
        std::cerr << "Error parsing JSON: " << doc.GetParseError() << std::endl;

        return FFprobeOutput{};
    }

    auto mediaInfo = ParseMediaTrack(doc);

    return mediaInfo;
}


