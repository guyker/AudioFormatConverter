#include "CommonUtils.h"
#include "MediaTrack.h"
#include "ffmpeg.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Link with the Debug application objects except FlacConvertMain.obj and the FFmpeg libraries.
// Arguments are generated 48 kHz stereo WAV fixtures: a 997 Hz FFmpeg sine and silence.

static void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

static std::string render_progress(std::size_t total, std::size_t count, int width = 20,
    std::shared_ptr<CommonUtils::ProgressBarInfo> subProgress = nullptr) {
    auto progress = std::make_shared<CommonUtils::ProgressBarInfo>();
    progress->size = total;
    progress->count = count;
    progress->bar_size = width;
    progress->subProgressInfoPtr = std::move(subProgress);
    std::ostringstream output;
    struct RestoreOutput {
        std::streambuf* original;
        ~RestoreOutput() { std::cout.rdbuf(original); }
    } restore{ std::cout.rdbuf(output.rdbuf()) };
    CommonUtils::render_progress_bar(progress, CommonUtils::ProgressBarType::Progress, false);
    return output.str();
}

static void check_log_callback_context() {
    static const AVClass testClass = [] {
        AVClass value{};
        value.class_name = "WarningRegressionContext";
        return value;
    }();
    struct Context {
        const AVClass* av_class;
    } context{ &testClass };

    FFmpeg::initialize_ffmpeg_logging();
    std::array<std::vector<FFmpeg::FFmpegLogItem>, 2> capturedLogs;
    std::array<std::thread, 2> workers;
    for (std::size_t index = 0; index < workers.size(); ++index) {
        workers[index] = std::thread([&, index] {
            const auto filePath = "opaque-context-" + std::to_string(index);
            FFmpeg::ScopedLogCapture capture(filePath, "test");
            av_log(&context, AV_LOG_WARNING, "opaque context regression %zu\n", index);
            capturedLogs[index] = capture.TakeLogs();
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    for (std::size_t index = 0; index < capturedLogs.size(); ++index) {
        require(capturedLogs[index].size() == 1,
            "Concurrent FFmpeg diagnostic was not captured exactly once");
        const auto& log = capturedLogs[index].front();
        require(log.filePath == "opaque-context-" + std::to_string(index) &&
            log.stage == "test" &&
            log.component == "WarningRegressionContext" &&
            log.message == "opaque context regression " + std::to_string(index),
            "Concurrent FFmpeg diagnostic context was misattributed");
    }
}

static void check_unscoped_log_callback_context() {
    static const AVClass testClass = [] {
        AVClass value{};
        value.class_name = "UnscopedWarningRegressionContext";
        return value;
    }();
    struct Context {
        const AVClass* av_class;
    } context{ &testClass };

    FFmpeg::ScopedRunLogCapture capture("scan");
    std::thread worker([&] {
        av_log(&context, AV_LOG_WARNING, "unscoped callback regression\n");
    });
    worker.join();

    const auto logs = capture.TakeLogs();
    require(logs.size() == 1, "Unscoped FFmpeg callback log was not captured exactly once");
    require(logs.front().filePath.empty() && logs.front().stage == "scan" &&
        logs.front().component == "UnscopedWarningRegressionContext" &&
        logs.front().message == "unscoped callback regression",
        "Unscoped FFmpeg callback log was assigned incorrect file context");
}

static void check_audio_comparison_without_analysis() {
    MediaTrack first;
    MediaTrack second;
    Stream left;
    left.codec_type = "audio";
    left.codec_name = "flac";
    left.sample_rate = "44100";
    left.channels = 2;
    Stream right = left;
    right.sample_rate = "48000";
    first.formatInfo.streams.push_back(left);
    second.formatInfo.streams.push_back(right);
    require(!MediaTrack::CompareAudioTracks({first}, {second}),
        "Different stream properties were ignored without optional analysis");
    const auto report = MediaTrack::GenerateComparisonReport({first}, {second});
    require(report.find("44100") != std::string::npos && report.find("48000") != std::string::npos,
        "Basic sample rates were missing from the comparison report");
}

static void check_recording(const char* filename, bool silent) {
    AVFormatContext* context = nullptr;
    require(avformat_open_input(&context, filename, nullptr, nullptr) >= 0, "Open fixture failed");
    const int streamResult = avformat_find_stream_info(context, nullptr);
    if (streamResult < 0) {
        avformat_close_input(&context);
        throw std::runtime_error("Read fixture streams failed");
    }
    const auto metadata = FFmpeg::GetFormatInformation(context, filename);
    const auto analysis = FFmpeg::analyze_audio_recording(context);
    avformat_close_input(&context);
    require(metadata.nb_streams == 1, "Fixture stream count was lost");
#if LIBAVFORMAT_VERSION_MAJOR >= 61
    require(metadata.nb_stream_groups == 0, "Fixture stream group count was lost");
#else
    require(!metadata.nb_stream_groups, "Unavailable stream group count was fabricated");
#endif
    require(analysis != nullptr, "Recording analysis failed");
    require(analysis->total_samples > 0, "No samples analyzed");
    require(std::isfinite(analysis->rms_amplitude), "Non-finite RMS");
    if (silent) {
        require(analysis->rms_amplitude == 0.0f, "Silence RMS changed");
    } else {
        require(std::abs(analysis->rms_amplitude - 0.088388f) < 0.003f, "Sine RMS changed");
    }
}

int main(int argc, char** argv) {
    try {
        require(argc == 3, "Expected generated 48 kHz stereo sine and silence WAV paths");
        const auto largeTotal = static_cast<std::size_t>((std::numeric_limits<int>::max)()) + 1;
        require(render_progress(largeTotal, largeTotal / 4).find("25%") != std::string::npos,
            "Large progress count narrowed");
        require(render_progress(0, 0).find("0%") != std::string::npos, "Empty progress failed");
        for (std::size_t count = 0; count <= 100; ++count) {
            const auto expected = std::to_string((count / 5) * 5) + "%";
            require(render_progress(100, count).find("] " + expected) != std::string::npos,
                "Ordinary progress percentage changed");
        }
        require(render_progress(10, 20).find("100%") != std::string::npos, "Over-count not clamped");
        require(render_progress(10, 5, 0).find("0%") != std::string::npos, "Zero width failed");
        require(render_progress(10, 5, -1).find("0%") != std::string::npos, "Negative width failed");
        const auto maximum = (std::numeric_limits<std::size_t>::max)();
        require(render_progress(maximum, maximum).find("100%") != std::string::npos,
            "Maximum progress overflowed");
        require(render_progress(maximum, maximum - 1).find("100%") == std::string::npos,
            "Incomplete progress rounded to completion");
        auto subProgress = std::make_shared<CommonUtils::ProgressBarInfo>();
        subProgress->size = largeTotal;
        subProgress->count = largeTotal / 2;
        require(render_progress(10, 5, 20, subProgress).find("Current Batch:      [==========          ] 50%")
            != std::string::npos, "Large sub-progress count narrowed");
        subProgress->size = 0;
        subProgress->count = 0;
        require(render_progress(10, 5, 20, subProgress).find("Current Batch:      [                    ] 0%")
            != std::string::npos, "Empty sub-progress failed");
        for (int frame = 0; frame < 8; ++frame) {
            const auto text = render_progress(10, 5, 20, subProgress);
            require(std::ranges::all_of(text, [](char c) { return static_cast<unsigned char>(c) < 0x80; }),
                "Progress output contains non-ASCII characters");
            require(text.find('\x1b') == std::string::npos, "Progress emitted VT sequences without terminal control");
        }
        check_log_callback_context();
        check_unscoped_log_callback_context();
        check_audio_comparison_without_analysis();
        check_recording(argv[1], false);
        check_recording(argv[2], true);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
