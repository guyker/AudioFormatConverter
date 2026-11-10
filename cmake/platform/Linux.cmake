find_package(PkgConfig REQUIRED)
pkg_check_modules(FFmpeg REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavfilter libavutil libswresample)
add_library(FFmpeg::FFmpeg ALIAS PkgConfig::FFmpeg)

find_program(FFMPEG_EXECUTABLE NAMES ffmpeg REQUIRED)
find_program(FFPROBE_EXECUTABLE NAMES ffprobe REQUIRED)
