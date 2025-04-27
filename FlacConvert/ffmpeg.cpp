

//extern "C" {
//    __declspec(dllimport) int av_log_get_level(void);
//}


#include "FFmpeg.h"

#include <libavformat/avformat.h>


#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include <mutex>
#include <spdlog/spdlog.h>
#include "PlatformUtils.h"
#include "MediaTrack.h"
#include <regex>
#include <cmath>


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

		return; // Disable logging for now

		// Check if the log level is above the set level
        if (level > av_log_get_level()) {
            return; // Skip logs above set level
        }

        if (!fmt)
        {
            spdlog::error("ffmpeg_log_callback: fmt is null");
            return;
        }
        // Format the log message
        char buffer[1024];
		if (vsnprintf(buffer, sizeof(buffer), fmt, vl) < 0) {
			spdlog::error("ffmpeg_log_callback: vsnprintf failed");
			return;
		}

        // Remove trailing newline for cleaner logging
        std::string message = buffer;
        if (!message.empty() && message.back() == '\n') {
            message.pop_back();
        }



        // Map level to string for readability
        std::string level_str = "UNKNOWN";
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

            try
			{
				if (fmt_ctx->streams == 0x0000000000000000 || fmt_ctx->iformat == nullptr ||  fmt_ctx->url == (char*)0x0000000100000000) // this is a workaround for the ffmpeg crash when accessing fmt_ctx->url or other null value
				{
					context = "N/A - 0x0000000100000000";
				}
                else
                {
                    if (fmt_ctx->url) {
                        context = fmt_ctx->url;
                    }
                    else if (fmt_ctx->iformat && fmt_ctx->iformat->name) {
                        context = fmt_ctx->iformat->name;
                    }
                    else
                    {
                        if (fmt_ctx->oformat && fmt_ctx->oformat->name) {
                            context = fmt_ctx->oformat->name;
                        }
                        else
                        {
                            int i = 0;
                        }
                    }
                }
			}
            catch (const std::exception& e)
            {
				//this is a workaround for the ffmpeg crash when accessing fmt_ctx->url or other null value
				context += std::string{" (error accessing context)" } +  e.what();
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


    //create a media file (on filesystem) from a media track
    std::wstring GetJsonMetadataShell(std::filesystem::path mediaFilePath)
    {
        try
        {
            std::wstring f = mediaFilePath.generic_wstring();
            std::wstring command = L"ffprobe -v quiet -print_format json -show_format -show_streams -show_chapters \"" + f + L"\"";

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
            auto wide_output = CommonUtils::utf8ToWstring(result.str());

            return wide_output;

        }
        catch (const std::exception& e) {
            //std::wcout << " ### COMMAND INFO EXCEOTION :" << mediaFilePath.generic_wstring() << std::endl << e.what() << std::endl;
            spdlog::error("Error (GetJsonMetadataShell): ", e.what());
        }

        return L"**ERROR***";
    }


    FFprobeOutput GetFFprobeMetadataShell(const std::filesystem::path filePath)
    {
        auto jsonString = FFmpeg::GetJsonMetadataShell(filePath);
        auto mi = MediaTrack::ParseFFprobeInformation(jsonString);

        return mi;
    }



    float extractVolumeValue(const std::string& stats, const std::string& key) {
        std::regex pattern(key + ": (-?\\d+\\.?\\d*) dB");
        std::smatch matches;
        if (std::regex_search(stats, matches, pattern) && matches.size() > 1) {
            return std::stof(matches[1].str());
        }
        return NAN;
    }

    float computeAudioQualityScore(float mean_volume, float max_volume) {
        float clipping_penalty = (max_volume > -1.0f) ? 50.0f : 0.0f;
        float loudness_penalty = 0.0f;
        if (mean_volume < -30.0f) loudness_penalty = 30.0f;
        if (mean_volume > -5.0f)  loudness_penalty = 40.0f;
        float dynamic_range = max_volume - mean_volume;
        float dynamic_range_score = std::min<float>(30.0f, dynamic_range);
        float score = 100.0f - clipping_penalty - loudness_penalty + dynamic_range_score;
        return std::max<float>(0.0f, std::min<float>(100.0f, score));
    }


    extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
    }


    FFprobeOutput GetFFprobeMetadataAPI(const std::filesystem::path filePath)
    {
        // Initialize FFmpeg (not needed in newer versions, but safe to call)
        // av_register_all();

        FFprobeOutput output;

        // Get the UTF-8 encoded string. Depending on your implementation,
        // u8string() might return std::string or std::u8string.
        std::wstring widePath = filePath.wstring();
        //std::string utf8Path = WideStringToUTF8(widePath);
        std::string utf8Path = PlatformUtils::WideToUTF8(widePath);

        //AVFormatContext* fmt_ctx = nullptr;
        AVFormatContext* fmt_ctx = avformat_alloc_context();
        if (!fmt_ctx) {
            spdlog::error("Failed to allocate AVFormatContext");
            return output;
        }

        // Set probesize before opening
        fmt_ctx->probesize = 10 * 1024 * 1024; // 10 MB
        fmt_ctx->max_analyze_duration = 5 * AV_TIME_BASE; // 5 seconds

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



        uint8_t* params = NULL;
        av_opt_get(fmt_ctx, "mean_volume", AV_OPT_SEARCH_CHILDREN, &params);

		// Volume Information section
        GetFFprobeVolumeInformation(fmt_ctx);




        avformat_close_input(&fmt_ctx);
        return output;
    }




    Format GetFormatInformation(AVFormatContext* fmt_ctx, const std::filesystem::path filePath)
    { 
        // Format section
        Format fmt;
        if (filePath.has_filename())
        {
            fmt.filename = CommonUtils::utf8string_to_string(filePath.filename().u8string());
		}
		else
		{
			fmt.filename = CommonUtils::utf8string_to_string(filePath.u8string());
		}
        
        if (fmt_ctx->url)
        {
            fmt.url = fmt_ctx->url;
        }

        // Optional: Get file size from filesystem
        try {
            fmt.fs_file_size = std::filesystem::file_size(filePath);
        }
        catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Failed to get file size: " << e.what() << "\n";
        }
        
        int64_t file_size = avio_size(fmt_ctx->pb);
        if (file_size >= 0) {
            fmt.file_size = file_size;
        }

        fmt.ctx_flags = fmt_ctx->ctx_flags;
        fmt.nb_streams = fmt_ctx->nb_streams;

        fmt.nb_stream_groups = fmt_ctx->nb_stream_groups;
        
        fmt.nb_chapters	= fmt_ctx->nb_stream_groups;


        if (fmt_ctx->start_time != AV_NOPTS_VALUE) {
            fmt.start_time = fmt_ctx->start_time;
        }
        if (fmt_ctx->duration != AV_NOPTS_VALUE) {
            fmt.duration = static_cast<double>(fmt_ctx->duration) / AV_TIME_BASE;
        }
        if (fmt_ctx->bit_rate > 0) {
            fmt.bit_rate = fmt_ctx->bit_rate;
        }

        fmt.packet_size = fmt_ctx->packet_size;
        fmt.max_delay = fmt_ctx->max_delay;
        fmt.flags = fmt_ctx->flags;


     //   fmt.probesize = fmt_ctx->probesize;
     //   fmt.max_analyze_duration = fmt_ctx->max_analyze_duration;
        

        fmt.format_name = fmt_ctx->iformat->name ? fmt_ctx->iformat->name : "";
        fmt.format_long_name = fmt_ctx->iformat->long_name ? fmt_ctx->iformat->long_name : "";
        fmt.probe_score = fmt_ctx->probe_score;

        fmt.audio_codec_id = fmt_ctx->audio_codec_id;
        if (fmt.audio_codec_id != AV_CODEC_ID_NONE)
        {
            fmt.audio_codec_name = avcodec_get_name(fmt_ctx->audio_codec_id);
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


    extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
    }

    int GetFFprobeVolumeInformation(AVFormatContext* fmt_ctx)
    {
        int audio_stream_index = -1;
        for (int i = 0; i < fmt_ctx->nb_streams; i++) {
            if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                audio_stream_index = i;
                break;
            }
        }

        // Set up filter graph for volumedetect
        AVFilterGraph* filter_graph = avfilter_graph_alloc();
        AVFilterContext* buffer_src_ctx;
        AVFilterContext* buffer_sink_ctx;

        // Source filter (raw audio input)
        const AVFilter* buffer_src = avfilter_get_by_name("abuffer");
        char args[512];
        snprintf(args, sizeof(args),
            "time_base=1/44100:sample_rate=44100:sample_fmt=fltp:channel_layout=stereo");
        avfilter_graph_create_filter(&buffer_src_ctx, buffer_src, "in", args, nullptr, filter_graph);

        // Sink filter (to process volumedetect)
        const AVFilter* buffer_sink = avfilter_get_by_name("abuffersink");
        avfilter_graph_create_filter(&buffer_sink_ctx, buffer_sink, "out", nullptr, nullptr, filter_graph);

        // Add volumedetect filter
        AVFilterContext* volume_ctx;
        const AVFilter* volume_filter = avfilter_get_by_name("volumedetect");
        avfilter_graph_create_filter(&volume_ctx, volume_filter, "volumedetect", nullptr, nullptr, filter_graph);

        // Connect filters: in -> volumedetect -> out
        avfilter_link(buffer_src_ctx, 0, volume_ctx, 0);
        avfilter_link(volume_ctx, 0, buffer_sink_ctx, 0);

        // Configure the graph
        if (avfilter_graph_config(filter_graph, nullptr) < 0) {
            std::cerr << "Failed to configure filter graph" << std::endl;
            return 1;
        }

        // Process audio frames
        AVPacket packet;
        AVFrame* frame = av_frame_alloc();
        while (av_read_frame(fmt_ctx, &packet) >= 0) {
            if (packet.stream_index == audio_stream_index) {
                // Decode packet into frame
                // (You'll need a decoder context here; simplified for brevity)
                // Then push frame into the filter graph:
                // av_buffersrc_add_frame(buffer_src_ctx, frame);
            }
            av_packet_unref(&packet);
        }

        // Retrieve volume stats
        char* stats = avfilter_graph_dump(filter_graph, nullptr);
      //  std::cout << "Volume stats:\n" << (stats ? stats : "No stats") << std::endl;




        std::string stats_str(stats);
        std::regex mean_regex("mean_volume: (-?\\d+\\.?\\d*) dB");
        std::regex max_regex("max_volume: (-?\\d+\\.?\\d*) dB");
        std::smatch matches;

        if (std::regex_search(stats_str, matches, mean_regex)) {
            float mean_volume = std::stof(matches[1].str());
            printf("Mean Volume: %.2f dB\n", mean_volume);
        }

        if (std::regex_search(stats_str, matches, max_regex)) {
            float max_volume = std::stof(matches[1].str());
            printf("Peak Volume: %.2f dB\n", max_volume);
        }






        av_free(stats);

        // Cleanup
        av_frame_free(&frame);
        avfilter_graph_free(&filter_graph);

        return 0;
    }
}