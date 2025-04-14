


#include "ffmpeg.h"
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include <mutex>
#include <spdlog/spdlog.h>

namespace ffmpeg {

    // Global log storage (thread-safe)
    static std::vector<std::string> ffmpeg_logs;
    static std::mutex ffmpeg_log_mutex;

    void initialize_ffmpeg_logging() {
        // Clear any existing logs
        {
            std::lock_guard<std::mutex> lock(ffmpeg_log_mutex);
            ffmpeg_logs.clear();
        }

        // Set custom callback
    //     av_log_set_callback(ffmpeg_log_callback);

         // Set log level: AV_LOG_QUIET to suppress all, or AV_LOG_ERROR for errors only
          //av_log_set_level(AV_LOG_QUIET); // No console output
          // Alternatively: av_log_set_level(AV_LOG_ERROR); // Capture errors only
    }


    void ffmpeg_log_callback(void* avcl, int level, const char* fmt, va_list vl) {
        if (level > ::av_log_get_level()) {
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

        // Store in vector (thread-safe)
        {
            std::lock_guard<std::mutex> lock(ffmpeg_log_mutex);
            ffmpeg_logs.push_back(message);
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

    std::vector<std::string> get_ffmpeg_logs() {
        std::lock_guard<std::mutex> lock(ffmpeg_log_mutex);
        return ffmpeg_logs;
    }

    void clear_ffmpeg_logs() {
        std::lock_guard<std::mutex> lock(ffmpeg_log_mutex);
        ffmpeg_logs.clear();
    }

}