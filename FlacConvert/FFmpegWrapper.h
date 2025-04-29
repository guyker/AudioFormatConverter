#pragma once

// FFmpegWrapper.h
#ifndef FFMPEG_WRAPPER_H
#define FFMPEG_WRAPPER_H
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/avutil.h>
#include <libavutil/log.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
}
#endif