

//extern "C" {
//    __declspec(dllimport) int av_log_get_level(void);
//}


#include "FFmpeg.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include <mutex>
#include <spdlog/spdlog.h>
#include "PlatformUtils.h"

//#pragma comment(lib, "avcodec.lib")
//#pragma comment(lib, "avformat.lib")
//#pragma comment(lib, "avutil.lib")
//#pragma comment(lib, "swresample.lib")  // Often required for avutil
//#pragma comment(lib, "swscale.lib")    // Often required for avutil
//#pragma comment(lib, "avfilter.lib")  // Often required for avutil
//#pragma comment(lib, "avdevice.lib") // Often required for avutil

namespace FFmpeg {

    // Global log storage (thread-safe)
    static std::vector<FFmpegLogItem> ffmpeg_logs;
    static std::mutex ffmpeg_log_mutex;

    void initialize_ffmpeg_logging() {
        // Clear any existing logs
        {
            std::lock_guard<std::mutex> lock(ffmpeg_log_mutex);
            ffmpeg_logs.clear();
        }

        // Set custom callback
        av_log_set_callback(ffmpeg_log_callback);

        // Set log level: AV_LOG_QUIET to suppress all, or AV_LOG_ERROR for errors only
        //av_log_set_level(AV_LOG_QUIET); // No console output
        //Alternatively: av_log_set_level(AV_LOG_ERROR); // Capture errors only
        //Alternatively: av_log_set_level(AV_LOG_WARNING); // Capture errors only
    }



    void ffmpeg_log_callback(void* avcl, int level, const char* fmt, va_list vl) {
        if (level > av_log_get_level()) {
            return; // Skip logs above set level
        }


        // Format the log message
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), fmt, vl);

        // Remove trailing newline for cleaner logging
        std::string message = buffer;
        if (!message.empty() && message.back() == '\n') {
            message.pop_back();
        }


        // Map level to string for readability
        const char* level_str = "UNKNOWN";
        switch (level) {
        case AV_LOG_PANIC:   level_str = "PANIC"; break;
        case AV_LOG_FATAL:   level_str = "FATAL"; break;
        case AV_LOG_ERROR:   level_str = "ERROR"; break;
        case AV_LOG_WARNING: level_str = "WARNING"; break;
        case AV_LOG_INFO:    level_str = "INFO"; break;
        case AV_LOG_VERBOSE: level_str = "VERBOSE"; break;
        case AV_LOG_DEBUG:   level_str = "DEBUG"; break;
        case AV_LOG_TRACE:   level_str = "TRACE"; break;
        }


        // Get context (e.g., filename)
        std::string context = "N/A";
        if (avcl) {
            // Assuming AVFormatContext for media files
            AVFormatContext* fmt_ctx = static_cast<AVFormatContext*>(avcl);
            if (fmt_ctx->url) {
                context = fmt_ctx->url;
            }
        }


        // Store in vector (thread-safe)
        {
            std::lock_guard<std::mutex> lock(ffmpeg_log_mutex);
            ffmpeg_logs.push_back(FFmpegLogItem { context,  level_str, message });
        }

        // Optionally forward to spdlog
        auto logger = spdlog::get("console");
        if (logger) {
            switch (level) {
            case AV_LOG_PANIC:
            case AV_LOG_FATAL:
                logger->critical("FFmpeg: {}", message);
                break;
            case AV_LOG_ERROR:
                logger->error("FFmpeg: {}", message);
                break;
            case AV_LOG_WARNING:
                logger->warn("FFmpeg: {}", message);
                break;
            case AV_LOG_INFO:
                logger->info("FFmpeg: {}", message);
                break;
            case AV_LOG_VERBOSE:
            case AV_LOG_DEBUG:
                logger->debug("FFmpeg: {}", message);
                break;
            default:
                break;
            }
        }
    }

    std::vector<FFmpegLogItem> get_ffmpeg_logs() {
        std::lock_guard<std::mutex> lock(ffmpeg_log_mutex);
        return ffmpeg_logs;
    }

    void clear_ffmpeg_logs() {
        std::lock_guard<std::mutex> lock(ffmpeg_log_mutex);
        ffmpeg_logs.clear();
    }




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


    //create a media file (on filesystem) from a media track
    std::wstring ExtractMetadataFromMediaTrack(std::filesystem::path mediaFilePath, std::filesystem::path outFile)
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

            std::wstring wide_output = getAudioMetadataJSON(f);
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


    FFprobeOutput GetFFprobeMetadata(const std::filesystem::path filePath) {
        // Initialize FFmpeg (not needed in newer versions, but safe to call)
        // av_register_all();

        FFprobeOutput output;

        // Get the UTF-8 encoded string. Depending on your implementation,
        // u8string() might return std::string or std::u8string.
        std::wstring widePath = filePath.wstring();
        //std::string utf8Path = WideStringToUTF8(widePath);
        std::string utf8Path = PlatformUtils::WideToUTF8(widePath);

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
        output.format = FFmpeg::GetFormatInformation(fmt_ctx, filePath);

        // Streams section
        output.streams = FFmpeg::GetStreamInformation(fmt_ctx);


        avformat_close_input(&fmt_ctx);
        return output;
    }
    Format GetFormatInformation(AVFormatContext* fmt_ctx, const std::filesystem::path filePath)
    { 
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
            JsonUtils::Tags format_tags;
            AVDictionaryEntry* tag = nullptr;
            while ((tag = av_dict_get(fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
                format_tags[tag->key] = tag->value;
            }
            fmt.tags = format_tags;
        }

        return fmt;
    }

    std::vector<Stream> GetStreamInformation(AVFormatContext* fmt_ctx)
    {
        std::vector<Stream> streamList;

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
                JsonUtils::Tags stream_tags;
                AVDictionaryEntry* tag = nullptr;
                while ((tag = av_dict_get(stream->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
                    stream_tags[tag->key] = tag->value;
                }
                s.tags = stream_tags;
            }

            streamList.push_back(s);
        }

        return streamList;
    }

}