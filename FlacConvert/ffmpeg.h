#pragma once

#include "FFmpegWrapper.h"

#include <vector>
#include <string>
#include <cstdarg>

namespace ffmpeg
{
    struct FFmpegLogItem {
        std::string url;
        std::string level;
        std::string message;
    };


    void initialize_ffmpeg_logging();
    void ffmpeg_log_callback(void* avcl, int level, const char* fmt, va_list vl);
    std::vector<FFmpegLogItem> get_ffmpeg_logs();
    void clear_ffmpeg_logs();
}
