#pragma once

#include "FFmpegWrapper.h"

#include <vector>
#include <string>
#include <cstdarg>
#include <filesystem>
#include <memory>
#include <mutex>
#include "MediaInformation.h"

namespace FFmpeg
{

    struct RunLogSink;

    struct FFmpegLogItem {
        std::string filePath;
        std::string stage;
        std::string level;
        std::string component;
        std::string message;
    };

    class ScopedLogCapture
    {
    public:
        ScopedLogCapture(const std::filesystem::path& filePath, std::string stage);
        ~ScopedLogCapture();
        ScopedLogCapture(const ScopedLogCapture&) = delete;
        ScopedLogCapture& operator=(const ScopedLogCapture&) = delete;

        std::vector<FFmpegLogItem> TakeLogs();

    private:
        friend void scoped_ffmpeg_log_callback(void* avcl, int level, const char* fmt, va_list vl);
        void Add(FFmpegLogItem item);

        std::string _filePath;
        std::string _stage;
        std::vector<FFmpegLogItem> _logs;
        std::mutex _mutex;
        ScopedLogCapture* _previous{ nullptr };
    };

    class ScopedRunLogCapture
    {
    public:
        explicit ScopedRunLogCapture(std::string stage);
        ~ScopedRunLogCapture();
        ScopedRunLogCapture(const ScopedRunLogCapture&) = delete;
        ScopedRunLogCapture& operator=(const ScopedRunLogCapture&) = delete;

        std::vector<FFmpegLogItem> TakeLogs();

    private:
        std::shared_ptr<RunLogSink> _sink;
        std::shared_ptr<RunLogSink> _previous;
    };

    void initialize_ffmpeg_logging();
    void scoped_ffmpeg_log_callback(void* avcl, int level, const char* fmt, va_list vl);

    FFprobeOutput GetFFprobeMetadataAPI(const std::filesystem::path filePath,
        const bool bIncludeAudioQualityMetrics = false, std::string* errorMessage = nullptr);

    Format GetFormatInformation(AVFormatContext* fmt_ctx, const std::filesystem::path filePath);
    std::vector<Stream> GetStreamInformation(AVFormatContext* fmt_ctx);

    AudioMetrics  analyze_audio_metrics(AVFormatContext* fmt_ctx);
    std::shared_ptr<AudioAnalysisInfo>  analyze_audio_recording(AVFormatContext* fmt_ctx);
}
