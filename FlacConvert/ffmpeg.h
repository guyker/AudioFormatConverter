#pragma once

#include "FFmpegWrapper.h"

#include <vector>
#include <string>
#include <cstdarg>
#include "MediaInformation.h"

namespace FFmpeg
{
    // Struct to hold audio quality information
    struct AudioQualityInfo {
        std::string codec_name;
        int sample_rate;       // in Hz
        int channels;          // number of channels
        int64_t bitrate;       // in bits per second
        bool is_lossless;      // FLAC = true, MP3 = false
        bool is_high_quality;  // Simple quality estimation
    };

    struct AudioAnalysisInfo {
        float peak_amplitude;       // 0.0 - 1.0
        float rms_amplitude;        // Root mean square
        float dynamic_range_db;     // in decibels
        int clipped_samples;        // how many samples clipped
        int total_samples;          // total processed
    };

    struct FFmpegLogItem {
        std::string url;
        std::string level;
        std::string message;
    };

    void initialize_ffmpeg_logging();
    void ffmpeg_log_callback(void* avcl, int level, const char* fmt, va_list vl);
    std::vector<FFmpegLogItem> get_ffmpeg_logs();
    void clear_ffmpeg_logs();

    FFprobeOutput GetFFprobeMetadataAPI(const std::filesystem::path filePath);
    FFprobeOutput GetFFprobeMetadataShell(const std::filesystem::path filePath);
    std::wstring GetJsonMetadataShell(std::filesystem::path filePath);

    Format GetFormatInformation(AVFormatContext* fmt_ctx, const std::filesystem::path filePath);
    std::vector<Stream> GetStreamInformation(AVFormatContext* fmt_ctx);

    AudioQualityInfo analyze_audio_quality(AVFormatContext* fmt_ctx);
}
