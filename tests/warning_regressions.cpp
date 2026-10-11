#include "CommonUtils.h"
#include "ffmpeg.h"

#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

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
    CommonUtils::show_progress_bar(progress);
    return output.str();
}

static void check_recording(const char* filename, bool silent) {
    AVFormatContext* context = nullptr;
    require(avformat_open_input(&context, filename, nullptr, nullptr) >= 0, "Open fixture failed");
    const int streamResult = avformat_find_stream_info(context, nullptr);
    if (streamResult < 0) {
        avformat_close_input(&context);
        throw std::runtime_error("Read fixture streams failed");
    }
    const auto analysis = FFmpeg::analyze_audio_recording(context);
    avformat_close_input(&context);
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
        check_recording(argv[1], false);
        check_recording(argv[2], true);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
