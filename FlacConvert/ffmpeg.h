#pragma once

#include <vector>
#include <string>

#include <libavutil/log.h>
#include <cstdarg>

namespace ffmpeg
{
    void initialize_ffmpeg_logging();
    void ffmpeg_log_callback(void* avcl, int level, const char* fmt, va_list vl);
    std::vector<std::string> get_ffmpeg_logs();
    void clear_ffmpeg_logs();
}
