#pragma once

#include "FFmpegWrapper.h"

#include <vector>
#include <string>
#include <cstdarg>
#include "MediaInformation.h"

namespace FFmpeg
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

    std::wstring ExtractMetadataFromMediaTrack(std::filesystem::path mediaFilePath, std::filesystem::path outFile);
    FFprobeOutput GetFFprobeMetadata(const std::filesystem::path filePath);
    Format GetFormatInformation(AVFormatContext* fmt_ctx, const std::filesystem::path filePath);
    std::vector<Stream> GetStreamInformation(AVFormatContext* fmt_ctx);
}
