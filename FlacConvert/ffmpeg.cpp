

#include "FFmpeg.h"

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
                if (fmt_ctx->streams == 0x0000000000000000 || fmt_ctx->iformat == nullptr || fmt_ctx->url == (char*)0x0000000100000000) // this is a workaround for the ffmpeg crash when accessing fmt_ctx->url or other null value
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
                context += std::string{ " (error accessing context)" } + e.what();
            }
        }

        // Store in vector (thread-safe)
        {
            std::lock_guard<std::mutex> lock(ffmpeg_log_mutex);
            ffmpeg_logs.push_back(FFmpegLogItem{ context,  level_str, message });
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

        // Audio Quality
        if (AppSettingsJson::AppSetting()->ExtraAudioQualityMetrics)
        {
            output.audio_metrics = analyze_audio_metrics(fmt_ctx);
            output.audio_quality = analyze_audio_recording(fmt_ctx);
        }

        avformat_close_input(&fmt_ctx);
        return output;
    }


    AudioAnalysisInfo analyze_audio_recording(AVFormatContext* fmt_ctx) {
        AudioAnalysisInfo info = {};

        AVStream* audio_stream = nullptr;
        int audio_stream_index = -1;

        // Find the first audio stream
        for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
            if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                audio_stream = fmt_ctx->streams[i];
                audio_stream_index = i;
                break;
            }
        }

        if (!audio_stream) return info;

        AVCodecParameters* codecpar = audio_stream->codecpar;
        const AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);
        AVCodecContext* codec_ctx = avcodec_alloc_context3(decoder);
        avcodec_parameters_to_context(codec_ctx, codecpar);
        avcodec_open2(codec_ctx, decoder, nullptr);

        // Define input/output layouts (FFmpeg 6+)
        AVChannelLayout in_layout = codec_ctx->ch_layout;
        AVChannelLayout out_layout;
        av_channel_layout_default(&out_layout, 1);  // Mono

        enum AVSampleFormat in_fmt = codec_ctx->sample_fmt;
        enum AVSampleFormat out_fmt = AV_SAMPLE_FMT_FLT;
        int in_rate = codec_ctx->sample_rate;
        int out_rate = 16000;

        // Allocate resampler context
        SwrContext* swr = nullptr;
        int ret = swr_alloc_set_opts2(
            &swr,
            &out_layout, out_fmt, out_rate,
            &in_layout, in_fmt, in_rate,
            0, nullptr
        );
        if (ret < 0 || !swr || swr_init(swr) < 0) {
            av_channel_layout_uninit(&out_layout);
            avcodec_free_context(&codec_ctx);
            return info;
        }

        AVPacket* pkt = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();

        float max_amp = 0.0f;
        float min_amp = 1.0f;
        double sum_squares = 0.0;
        int64_t clipped = 0;
        int64_t total = 0;

        // Process each packet in the stream
        while (av_read_frame(fmt_ctx, pkt) >= 0) {
            if (pkt->stream_index != audio_stream_index) {
                av_packet_unref(pkt);
                continue;
            }

            if (avcodec_send_packet(codec_ctx, pkt) >= 0) {
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    // Allocate buffer for resampling
                    int out_samples = av_rescale_rnd(
                        swr_get_delay(swr, codec_ctx->sample_rate) + frame->nb_samples,
                        out_rate, codec_ctx->sample_rate, AV_ROUND_UP);

                    uint8_t* out_buf[1];
                    out_buf[0] = (uint8_t*)av_malloc(out_samples * sizeof(float));

                    int converted = swr_convert(
                        swr,
                        out_buf, out_samples,
                        (const uint8_t**)frame->extended_data,
                        frame->nb_samples
                    );

                    float* samples = reinterpret_cast<float*>(out_buf[0]);

                    // Analyze the samples (clamp values and calculate)
                    for (int i = 0; i < converted; ++i) {
                        float s = samples[i];
                        float abs_s = std::abs(s);

                        // Clamp extreme values to avoid artifacts
                        abs_s = std::clamp(abs_s, 0.0f, 1.0f);

                        // Use std::max and std::min explicitly with the correct type
                        max_amp = std::max<float>(max_amp, abs_s);
                        min_amp = std::min<float>(min_amp, std::max<float>(abs_s, 1e-4f));  // Prevent zero min_amp

                        sum_squares += static_cast<double>(s) * static_cast<double>(s);
                        total++;

                        if (abs_s >= 0.999f) clipped++;  // Count clipped samples
                    }

                    av_free(out_buf[0]);
                }
            }

            av_packet_unref(pkt);
        }

        av_frame_free(&frame);
        av_packet_free(&pkt);
        swr_free(&swr);
        av_channel_layout_uninit(&out_layout);
        avcodec_free_context(&codec_ctx);

        // Finalize results
        info.peak_amplitude = max_amp;
        info.rms_amplitude = total > 0 ? std::sqrt(sum_squares / total) : 0.0f;
        info.dynamic_range_db = 20.0f * std::log10f((max_amp + 1e-9f) / (min_amp + 1e-9f));  // Prevent log(0)
        info.clipped_samples = static_cast<int>(clipped);
        info.total_samples = static_cast<int>(total);

        return info;
    }




    // Function to analyze the audio file
    AudioMetrics analyze_audio_metrics(AVFormatContext* fmt_ctx) {
        AudioMetrics  info = {};

        if (!fmt_ctx) {
            std::cerr << "Null format context\n";
            return info;
        }

        AVStream* audio_stream = nullptr;

        // Find the first audio stream
        for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
            if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                audio_stream = fmt_ctx->streams[i];
                break;
            }
        }

        if (!audio_stream) {
            std::cerr << "No audio stream found\n";
            return info;
        }

        AVCodecParameters* codecpar = audio_stream->codecpar;

        // Fill struct
        info.codec_name = avcodec_get_name(codecpar->codec_id);
        info.sample_rate = codecpar->sample_rate;
        //info.channels = codecpar->channels;
        info.channels = codecpar->ch_layout.nb_channels;
        info.bitrate = codecpar->bit_rate;  // Note: For FLAC, bitrate might be 0 sometimes

        // Determine if it's lossless
        if (codecpar->codec_id == AV_CODEC_ID_FLAC) {
            info.is_lossless = true;
        }
        else if (codecpar->codec_id == AV_CODEC_ID_MP3) {
            info.is_lossless = false;
        }
        else {
            // Other formats: default to false
            info.is_lossless = false;
        }

        // Basic estimation of high quality
        info.is_high_quality = true;

        // Rules (you can adjust):
        if (info.sample_rate < 44100) {
            info.is_high_quality = false;
        }
        if (info.channels < 2) {
            info.is_high_quality = false;
        }
        if (codecpar->codec_id == AV_CODEC_ID_MP3 && info.bitrate < 128000) {
            info.is_high_quality = false;
        }

        return info;
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

        fmt.nb_chapters = fmt_ctx->nb_stream_groups;


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
}
