#include "ffmpeg.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include <utility>
#include <spdlog/spdlog.h>
#include "PlatformUtils.h"
#include <cmath>

namespace FFmpeg {

    struct RunLogSink
    {
        explicit RunLogSink(std::string stage) : _stage(std::move(stage)) {}

        void Add(std::string level, std::string component, std::string message)
        {
            std::lock_guard lock(_mutex);
            _logs.push_back({ {}, _stage, std::move(level),
                std::move(component), std::move(message) });
        }

        std::vector<FFmpegLogItem> TakeLogs()
        {
            std::lock_guard lock(_mutex);
            auto logs = std::move(_logs);
            _logs.clear();
            return logs;
        }

    private:
        std::string _stage;
        std::vector<FFmpegLogItem> _logs;
        std::mutex _mutex;
    };

    namespace {
    thread_local ScopedLogCapture* activeLogCapture = nullptr;
    std::mutex runLogCaptureMutex;
    std::shared_ptr<RunLogSink> activeRunLogCapture;
    }

    ScopedLogCapture::ScopedLogCapture(const std::filesystem::path& filePath, std::string stage)
        : _filePath(CommonUtils::path_to_utf8(filePath)),
          _stage(std::move(stage)),
          _previous(activeLogCapture)
    {
        activeLogCapture = this;
    }

    ScopedLogCapture::~ScopedLogCapture()
    {
        if (activeLogCapture == this) {
            activeLogCapture = _previous;
        }
    }

    void ScopedLogCapture::Add(FFmpegLogItem item)
    {
        std::lock_guard lock(_mutex);
        _logs.push_back(std::move(item));
    }

    std::vector<FFmpegLogItem> ScopedLogCapture::TakeLogs()
    {
        std::lock_guard lock(_mutex);
        auto logs = std::move(_logs);
        _logs.clear();
        return logs;
    }

    ScopedRunLogCapture::ScopedRunLogCapture(std::string stage)
        : _sink(std::make_shared<RunLogSink>(std::move(stage)))
    {
        std::lock_guard lock(runLogCaptureMutex);
        _previous = activeRunLogCapture;
        activeRunLogCapture = _sink;
    }

    ScopedRunLogCapture::~ScopedRunLogCapture()
    {
        std::lock_guard lock(runLogCaptureMutex);
        if (activeRunLogCapture == _sink) {
            activeRunLogCapture = _previous;
        }
    }

    std::vector<FFmpegLogItem> ScopedRunLogCapture::TakeLogs()
    {
        return _sink->TakeLogs();
    }

    void initialize_ffmpeg_logging() {
        av_log_set_callback(scoped_ffmpeg_log_callback);
    }



    void scoped_ffmpeg_log_callback(void* avcl, int level, const char* fmt, va_list vl) {
        if (level > AV_LOG_WARNING || level > av_log_get_level()) {
            return;
        }
        if (fmt == nullptr) {
            spdlog::error("FFmpeg log callback received a null format string.");
            return;
        }

        va_list sizeArgs;
        va_copy(sizeArgs, vl);
        const int length = std::vsnprintf(nullptr, 0, fmt, sizeArgs);
        va_end(sizeArgs);
        if (length < 0) {
            spdlog::error("Could not format an FFmpeg diagnostic.");
            return;
        }
        std::vector<char> buffer(static_cast<std::size_t>(length) + 1);
        va_list formatArgs;
        va_copy(formatArgs, vl);
        std::vsnprintf(buffer.data(), buffer.size(), fmt, formatArgs);
        va_end(formatArgs);
        std::string message(buffer.data(), static_cast<std::size_t>(length));
        if (!message.empty() && message.back() == '\n') {
            message.pop_back();
        }
        const char* itemName = avcl == nullptr ? nullptr : av_default_item_name(avcl);
        const std::string component = itemName == nullptr ? "unknown" : itemName;
        const std::string severity =
            level <= AV_LOG_ERROR ? "error" : "warning";
        if (activeLogCapture != nullptr) {
            const auto context = activeLogCapture->_filePath + " [" + activeLogCapture->_stage + "]";
            if (severity == "error") {
                spdlog::error("FFmpeg [{}]: {}: {}", context, component, message);
            }
            else {
                spdlog::warn("FFmpeg [{}]: {}: {}", context, component, message);
            }
            activeLogCapture->Add({
                activeLogCapture->_filePath,
                activeLogCapture->_stage,
                severity,
                component,
                std::move(message)
            });
            return;
        }

        std::shared_ptr<RunLogSink> runLogCapture;
        {
            std::lock_guard lock(runLogCaptureMutex);
            runLogCapture = activeRunLogCapture;
        }
        if (runLogCapture) {
            if (severity == "error") {
                spdlog::error("FFmpeg [unattributed]: {}: {}", component, message);
            }
            else {
                spdlog::warn("FFmpeg [unattributed]: {}: {}", component, message);
            }
            runLogCapture->Add(severity, component, std::move(message));
        }
        else if (severity == "error") {
            spdlog::error("FFmpeg [{}]: {}", component, message);
        }
        else {
            spdlog::warn("FFmpeg [{}]: {}", component, message);
        }
    }

    FFprobeOutput GetFFprobeMetadataAPI(
        const std::filesystem::path filePath, [[maybe_unused]] const bool bIncludeAudioQualityMetrics,
        std::string* errorMessage)
    {
        // Initialize FFmpeg (not needed in newer versions, but safe to call)
        // av_register_all();

        FFprobeOutput output;

        // Get the UTF-8 encoded string. Depending on your implementation,
        // u8string() might return std::string or std::u8string.
#ifdef _WIN32
        std::wstring widePath = PlatformUtils::WindowsLongPath(filePath);
        std::string utf8Path = PlatformUtils::WideToUTF8(widePath);
#else
        std::string utf8Path = filePath.string();
#endif

        //AVFormatContext* fmt_ctx = nullptr;
        AVFormatContext* fmt_ctx = avformat_alloc_context();
        if (!fmt_ctx) {
            const std::string error = "Failed to allocate AVFormatContext";
            if (errorMessage) {
                *errorMessage = error;
            }
            spdlog::error("{} for '{}'", error, CommonUtils::path_to_utf8(filePath));
            return output;
        }

        // Set probesize before opening
        fmt_ctx->probesize = 10 * 1024 * 1024; // 10 MB
        fmt_ctx->max_analyze_duration = 5 * AV_TIME_BASE; // 5 seconds

        int ret = avformat_open_input(&fmt_ctx, utf8Path.c_str(), nullptr, nullptr);
        if (ret < 0) {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            const std::string error = "Failed to open input: " + std::string(errbuf) +
                " (" + std::to_string(ret) + ")";
            if (errorMessage) {
                *errorMessage = error;
            }
            spdlog::error("{}: {}", CommonUtils::path_to_utf8(filePath), error);
            return output;
        }

        ret = avformat_find_stream_info(fmt_ctx, nullptr);
        if (ret < 0) {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            const std::string error = "Could not find stream info: " + std::string(errbuf) +
                " (" + std::to_string(ret) + ")";
            if (errorMessage) {
                *errorMessage = error;
            }
            spdlog::error("{}: {}", CommonUtils::path_to_utf8(filePath), error);
            avformat_close_input(&fmt_ctx);
            return output;
        }

        // Format section
        output.format = FFmpeg::GetFormatInformation(fmt_ctx, filePath);

        // Streams section
        output.streams = FFmpeg::GetStreamInformation(fmt_ctx);

        output.chapters.reserve(fmt_ctx->nb_chapters);
        for (unsigned int i = 0; i < fmt_ctx->nb_chapters; ++i) {
            const AVChapter* sourceChapter = fmt_ctx->chapters[i];
            Chapter chapter;
            chapter.id = sourceChapter->id;
            chapter.time_base = std::to_string(sourceChapter->time_base.num) + "/" +
                std::to_string(sourceChapter->time_base.den);
            if (sourceChapter->start != AV_NOPTS_VALUE) {
                chapter.start = sourceChapter->start;
                chapter.start_time = std::to_string(sourceChapter->start * av_q2d(sourceChapter->time_base));
            }
            if (sourceChapter->end != AV_NOPTS_VALUE) {
                chapter.end = sourceChapter->end;
                chapter.end_time = std::to_string(sourceChapter->end * av_q2d(sourceChapter->time_base));
            }
            if (sourceChapter->metadata) {
                JsonUtils::Tags chapterTags;
                AVDictionaryEntry* tag = nullptr;
                while ((tag = av_dict_get(sourceChapter->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
                    chapterTags[tag->key] = tag->value;
                }
                chapter.tags = std::move(chapterTags);
            }
            output.chapters.push_back(std::move(chapter));
        }

        avformat_close_input(&fmt_ctx);
        return output;
    }


    void log_warning(const char* message) {
        spdlog::warn("{}", message);
    }
    void log_info(const char* message) {
        spdlog::info("{}", message);
    }

    std::shared_ptr<AudioAnalysisInfo> analyze_audio_recording(AVFormatContext* fmt_ctx)
    {
        std::shared_ptr<AudioAnalysisInfo> info = std::make_shared<AudioAnalysisInfo>();
        //AudioAnalysisInfo info = { 0, 0, 0, 0, 0, 0, 0};
        //AudioAnalysisInfo info;

        if (!fmt_ctx) {
            log_warning("Format context is null.");
            return info;
        }

        AVStream* audio_stream = nullptr;
        int audio_stream_index = -1;

        // Find first audio stream
        for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i) {
            if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                audio_stream = fmt_ctx->streams[i];
                audio_stream_index = i;
                break;
            }
        }

        if (!audio_stream) {
            log_warning("No audio stream found.");
            return info;
        }

        AVCodecParameters* codecpar = audio_stream->codecpar;
        const AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            log_warning("Decoder not found.");
            return info;
        }

        AVCodecContext* codec_ctx = avcodec_alloc_context3(decoder);
        if (!codec_ctx) {
            log_warning("Failed to allocate codec context.");
            return info;
        }

        avcodec_parameters_to_context(codec_ctx, codecpar);
        if (avcodec_open2(codec_ctx, decoder, nullptr) < 0) {
            log_warning("Failed to open codec.");
            avcodec_free_context(&codec_ctx);
            return info;
        }

        // Channel layouts (FFmpeg 6+)
        AVChannelLayout in_layout = codec_ctx->ch_layout;
        AVChannelLayout out_layout;
        av_channel_layout_default(&out_layout, 1);  // Mono output

        enum AVSampleFormat in_fmt = codec_ctx->sample_fmt;
        enum AVSampleFormat out_fmt = AV_SAMPLE_FMT_FLT;
        int in_rate = codec_ctx->sample_rate;
        int out_rate = 16000;

        SwrContext* swr = nullptr;
        int ret = swr_alloc_set_opts2(
            &swr,
            &out_layout, out_fmt, out_rate,
            &in_layout, in_fmt, in_rate,
            0, nullptr
        );

        if (ret < 0 || !swr || swr_init(swr) < 0) {
            log_warning("Failed to initialize resampler.");
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
        const char* analysisError = nullptr;

        while (!analysisError && av_read_frame(fmt_ctx, pkt) >= 0) {
            if (pkt->stream_index != audio_stream_index) {
                av_packet_unref(pkt);
                continue;
            }

            if (avcodec_send_packet(codec_ctx, pkt) >= 0) {
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    const int out_samples = swr_get_out_samples(swr, frame->nb_samples);
                    if (out_samples < 0) {
                        analysisError = "Failed to determine resampler output capacity.";
                        break;
                    }

                    uint8_t* out_buf[1];
                    out_buf[0] = static_cast<uint8_t*>(av_malloc_array(
                        static_cast<std::size_t>(out_samples), sizeof(float)));
                    if (!out_buf[0]) {
                        analysisError = "Failed to allocate resampler output buffer.";
                        break;
                    }

                    int converted = swr_convert(
                        swr,
                        out_buf, out_samples,
                        (const uint8_t**)frame->extended_data,
                        frame->nb_samples
                    );
                    if (converted < 0) {
                        av_free(out_buf[0]);
                        analysisError = "Failed to resample audio recording.";
                        break;
                    }

                    float* samples = reinterpret_cast<float*>(out_buf[0]);
                    for (int i = 0; i < converted; ++i) {
                        float s = samples[i];
                        float abs_s = std::abs(s);

                        // Verify values
                        if (abs_s > 1.0f) {
                            info->samples_too_big++;
                            //log_warning("Sample value > 1.0 detected! Clamping to 1.0.");
                        }
                        if (abs_s < 0.0f) {
                            info->samples_negative++;
                            //log_warning("Negative absolute sample detected! Setting to 0.");
                            abs_s = 0.0f;
                        }

                        // Clamp values
                        abs_s = std::clamp(abs_s, 0.0f, 1.0f);

                        max_amp = std::max<float>(max_amp, abs_s);
                        min_amp = std::min<float>(min_amp, std::max<float>(abs_s, 1e-4f));

                        sum_squares += static_cast<double>(s) * static_cast<double>(s);
                        total++;

                        if (abs_s >= 0.999f) {
                            clipped++;
                        }

                        //Noise [NEW]
                        // 
                        // Inside the loop (after sample processing):
                        float noise_energy_sum = 0.0f;
                        int64_t noise_sample_count = 0;
                        const float noise_threshold = 0.01f; // Anything below this is considered noise-level

                        // Add this inside the sample processing loop (after abs_s = clamp...):
                        if (abs_s < noise_threshold) {
                            noise_energy_sum += static_cast<float>(s * s);
                            noise_sample_count++;
                        }

                        // ... After the existing RMS, DR, clipping analysis
                        info->noise_floor_rms = (noise_sample_count > 0)
                            ? std::sqrt(noise_energy_sum / noise_sample_count)
                            : 0.0f;

                        // Optional SNR estimation (Signal-to-Noise Ratio in dB)
                        info->estimated_snr_db = (info->noise_floor_rms > 0.0f)
                            ? 20.0f * std::log10(info->rms_amplitude / (info->noise_floor_rms + 1e-12f))
                            : 0.0f;

                        // Noise indicators
                        if (info->noise_floor_rms > 0.02f && info->estimated_snr_db < 20.0f) {
                            log_warning("High noise floor or low SNR detected. Possible background noise.");
                            info->high_noise_comb = true;
                        }
                        else
                        {
                            info->high_noise_comb = false;
                        }
                        if (info->noise_floor_rms < 1e-4f) {
                            info->low_noise_floor_rms = true;
                            //log_info("Very low noise floor detected – clean signal.");
                        }
                        else
                        {
                            info->low_noise_floor_rms = false;
                        }
                    }

                    av_free(out_buf[0]);
                }
            }

            av_packet_unref(pkt);
        }

        // Free resources
        av_frame_free(&frame);
        av_packet_free(&pkt);
        swr_free(&swr);
        av_channel_layout_uninit(&out_layout);
        avcodec_free_context(&codec_ctx);
        if (analysisError) {
            spdlog::error("{}", analysisError);
            return nullptr;
        }

        // Calculate results
        info->peak_amplitude = max_amp;
        info->rms_amplitude = total > 0 ? static_cast<float>(std::sqrt(sum_squares / total)) : 0.0f;
        info->dynamic_range_db = 20.0f * std::log10((max_amp + 1e-9f) / (min_amp + 1e-9f));
        info->clipped_samples = static_cast<int>(clipped);
        info->total_samples = static_cast<int>(total);

        // Post-analysis checks & warnings
        if (info->peak_amplitude > 1.0f) {
            log_warning("Peak amplitude > 1.0 detected after processing.");
        }
        if (info->dynamic_range_db > 120.0f) {
            log_warning("Dynamic range unusually high (>120 dB). Possible noise floor issues.");
        }
        if (info->rms_amplitude > 0.9f) {
            log_warning("RMS amplitude unusually high. Audio may be overly loud or distorted.");
        }
        if (info->clipped_samples > 0) {
            //fprintf(stderr, "Info: %d clipped samples detected.\n", info->clipped_samples);
        }

        float clip_ratio = static_cast<float>(info->clipped_samples) / info->total_samples;
        if (clip_ratio > 0.0001f) {
            //std::cerr << "[WARN] High clipping: " << info->clipped_samples << " samples (" << (clip_ratio * 100.0f) << "%)\n";
        }

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

#if LIBAVFORMAT_VERSION_MAJOR >= 61
        fmt.nb_stream_groups = fmt_ctx->nb_stream_groups;
#endif

        fmt.nb_chapters = fmt_ctx->nb_chapters;


        if (fmt_ctx->start_time != AV_NOPTS_VALUE) {
            fmt.start_time = static_cast<double>(fmt_ctx->start_time) / AV_TIME_BASE;
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
            if (const AVCodecDescriptor* descriptor = avcodec_descriptor_get(stream->codecpar->codec_id);
                descriptor && descriptor->long_name) {
                s.codec_long_name = descriptor->long_name;
            }
            if (stream->codecpar->codec_tag != 0) {
                char codecTag[AV_FOURCC_MAX_STRING_SIZE]{};
                av_fourcc_make_string(codecTag, stream->codecpar->codec_tag);
                s.codec_tag_string = codecTag;
                s.codec_tag = std::format("0x{:08x}", stream->codecpar->codec_tag);
            }
            s.r_frame_rate = std::to_string(stream->r_frame_rate.num) + "/" +
                std::to_string(stream->r_frame_rate.den);
            s.avg_frame_rate = std::to_string(stream->avg_frame_rate.num) + "/" +
                std::to_string(stream->avg_frame_rate.den);
            s.time_base = std::to_string(stream->time_base.num) + "/" +
                std::to_string(stream->time_base.den);
            const auto flag = [stream](int value) { return (stream->disposition & value) != 0 ? 1 : 0; };
            s.disposition = Disposition{
                flag(AV_DISPOSITION_DEFAULT), flag(AV_DISPOSITION_DUB), flag(AV_DISPOSITION_ORIGINAL),
                flag(AV_DISPOSITION_COMMENT), flag(AV_DISPOSITION_LYRICS), flag(AV_DISPOSITION_KARAOKE),
                flag(AV_DISPOSITION_FORCED), flag(AV_DISPOSITION_HEARING_IMPAIRED),
                flag(AV_DISPOSITION_VISUAL_IMPAIRED), flag(AV_DISPOSITION_CLEAN_EFFECTS),
                flag(AV_DISPOSITION_ATTACHED_PIC), flag(AV_DISPOSITION_TIMED_THUMBNAILS)
            };
            AVCodecParameters* codecpar = stream->codecpar;
            s.codec_name = avcodec_get_name(codecpar->codec_id);
            if (codecpar->format != AV_SAMPLE_FMT_NONE) {
                if (const char* sampleFormat = av_get_sample_fmt_name(static_cast<AVSampleFormat>(codecpar->format))) {
                    s.sample_fmt = sampleFormat;
                }
            }
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
                else {
                    const int bitsPerSample = av_get_bits_per_sample(codecpar->codec_id);
                    if (bitsPerSample > 0) {
                        s.bits_per_sample = bitsPerSample;
                    }
                }
                if (codecpar->frame_size > 0) {
                    s.frame_size = codecpar->frame_size;
                }
            }

            if (stream->duration != AV_NOPTS_VALUE) {
                s.duration_ts = stream->duration;
                s.duration = std::to_string(static_cast<double>(stream->duration) * av_q2d(stream->time_base));
            }
            if (stream->start_time != AV_NOPTS_VALUE) {
                s.start_pts = stream->start_time;
                s.start_time = static_cast<double>(stream->start_time) * av_q2d(stream->time_base);
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
