#include "DecodeAnalyzer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

extern "C" {
#include <libavutil/md5.h>
#include <libavutil/sha.h>
}

#include "CommonUtils.h"
#include "FFmpegWrapper.h"
#include "PlatformUtils.h"
#include "ffmpeg.h"

namespace {

struct FormatCloser
{
    void operator()(AVFormatContext* context) const
    {
        if (context != nullptr) avformat_close_input(&context);
    }
};

struct CodecCloser
{
    void operator()(AVCodecContext* context) const
    {
        avcodec_free_context(&context);
    }
};

struct PacketCloser
{
    void operator()(AVPacket* packet) const
    {
        av_packet_free(&packet);
    }
};

struct FrameCloser
{
    void operator()(AVFrame* frame) const
    {
        av_frame_free(&frame);
    }
};

using FormatPtr = std::unique_ptr<AVFormatContext, FormatCloser>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecCloser>;
using PacketPtr = std::unique_ptr<AVPacket, PacketCloser>;
using FramePtr = std::unique_ptr<AVFrame, FrameCloser>;

std::string ffmpegError(int result)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> message{};
    av_strerror(result, message.data(), message.size());
    return message.data();
}

std::string hexDigest(const std::uint8_t* bytes, std::size_t size)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2);
    for (std::size_t index = 0; index < size; ++index) {
        result.push_back(digits[bytes[index] >> 4]);
        result.push_back(digits[bytes[index] & 0x0f]);
    }
    return result;
}

std::optional<std::string> hashFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    auto* rawHash = av_sha_alloc();
    if (rawHash == nullptr) return std::nullopt;
    std::unique_ptr<AVSHA, decltype(&av_free)> hash(rawHash, &av_free);
    if (av_sha_init(hash.get(), 256) < 0) return std::nullopt;
    std::array<std::uint8_t, 64 * 1024> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()),
            static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            av_sha_update(hash.get(), buffer.data(), static_cast<unsigned int>(count));
        }
    }
    if (!input.eof()) return std::nullopt;
    std::array<std::uint8_t, 32> digest{};
    av_sha_final(hash.get(), digest.data());
    return hexDigest(digest.data(), digest.size());
}

struct ChannelStats
{
    long double sum{ 0 };
    long double sumSquares{ 0 };
    double peak{ 0 };
    std::uint64_t clipped{ 0 };
    std::uint64_t zeroCrossings{ 0 };
    double previous{ 0 };
    bool hasPrevious{ false };
};

std::optional<double> parseFiniteDouble(const char* value)
{
    if (value == nullptr) return std::nullopt;
    const std::string text(value);
    double parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        !std::isfinite(parsed)) {
        return std::nullopt;
    }
    return parsed;
}

class EbuR128Filter
{
public:
    ~EbuR128Filter()
    {
        avfilter_graph_free(&_graph);
    }

    bool Initialize(const AVFrame& frame, std::string& error)
    {
        const auto* formatName = av_get_sample_fmt_name(
            static_cast<AVSampleFormat>(frame.format));
        if (formatName == nullptr || frame.sample_rate <= 0 ||
            frame.ch_layout.nb_channels <= 0) {
            error = "Decoder format is not supported by the EBU R128 filter.";
            return false;
        }
        std::array<char, 128> layout{};
        int result = av_channel_layout_describe(&frame.ch_layout, layout.data(), layout.size());
        if (result < 0) {
            error = "Cannot describe decoder channel layout: " + ffmpegError(result);
            return false;
        }
        _graph = avfilter_graph_alloc();
        if (_graph == nullptr) {
            error = "Cannot allocate the EBU R128 filter graph.";
            return false;
        }
        const std::string arguments = "time_base=1/" + std::to_string(frame.sample_rate) +
            ":sample_rate=" + std::to_string(frame.sample_rate) +
            ":sample_fmt=" + formatName +
            ":channels=" + std::to_string(frame.ch_layout.nb_channels) +
            ":channel_layout=" + layout.data();
        result = avfilter_graph_create_filter(&_source,
            avfilter_get_by_name("abuffer"), "source", arguments.c_str(), nullptr, _graph);
        if (result < 0) {
            error = "Cannot initialize the EBU R128 filter source: " + ffmpegError(result);
            return false;
        }
        AVFilterContext* ebur128 = nullptr;
        result = avfilter_graph_create_filter(&ebur128,
            avfilter_get_by_name("ebur128"), "ebur128",
            "peak=true:metadata=1:framelog=quiet", nullptr, _graph);
        if (result < 0) {
            error = "Cannot initialize the EBU R128 filter: " + ffmpegError(result);
            return false;
        }
        result = avfilter_graph_create_filter(&_sink,
            avfilter_get_by_name("abuffersink"), "sink", nullptr, nullptr, _graph);
        if (result < 0) {
            error = "Cannot initialize the EBU R128 filter sink: " + ffmpegError(result);
            return false;
        }
        result = avfilter_link(_source, 0, ebur128, 0);
        if (result >= 0) result = avfilter_link(ebur128, 0, _sink, 0);
        if (result >= 0) result = avfilter_graph_config(_graph, nullptr);
        if (result < 0) {
            error = "Cannot configure the EBU R128 filter graph: " + ffmpegError(result);
            return false;
        }
        _initialized = true;
        return true;
    }

    bool AddFrame(const AVFrame& frame, AudioSignalResult& signal, std::string& error)
    {
        const int result = av_buffersrc_add_frame_flags(
            _source, const_cast<AVFrame*>(&frame), AV_BUFFERSRC_FLAG_KEEP_REF);
        if (result < 0) {
            error = "Cannot submit decoded samples to the EBU R128 filter: " + ffmpegError(result);
            return false;
        }
        return Drain(signal, error);
    }

    bool Finish(AudioSignalResult& signal, std::string& error)
    {
        if (!_initialized) return false;
        const int result = av_buffersrc_add_frame_flags(_source, nullptr, 0);
        if (result < 0 && result != AVERROR_EOF) {
            error = "Cannot flush the EBU R128 filter: " + ffmpegError(result);
            return false;
        }
        return Drain(signal, error);
    }

private:
    bool Drain(AudioSignalResult& signal, std::string& error)
    {
        FramePtr filtered(av_frame_alloc());
        if (!filtered) {
            error = "Cannot allocate EBU R128 output frame.";
            return false;
        }
        while (true) {
            const int result = av_buffersink_get_frame(_sink, filtered.get());
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return true;
            if (result < 0) {
                error = "Cannot read EBU R128 output: " + ffmpegError(result);
                return false;
            }
            static constexpr std::array keys{
                std::pair{"lavfi.r128.I", "integratedLufs"},
                std::pair{"lavfi.r128.LRA", "loudnessRangeLU"},
                std::pair{"lavfi.r128.true_peak", "truePeakDbtp"}
            };
            for (const auto& [sourceKey, targetKey] : keys) {
                if (auto* entry = av_dict_get(filtered->metadata, sourceKey, nullptr, 0)) {
                    if (const auto value = parseFiniteDouble(entry->value)) {
                        signal.measurements[targetKey] = *value;
                    }
                }
            }
            av_frame_unref(filtered.get());
        }
    }

    AVFilterGraph* _graph{ nullptr };
    AVFilterContext* _source{ nullptr };
    AVFilterContext* _sink{ nullptr };
    bool _initialized{ false };
};

double readSample(const AVFrame& frame, AVSampleFormat format, int channel, int sample)
{
    const auto* data = frame.extended_data;
    const bool planar = av_sample_fmt_is_planar(format) != 0;
    const auto baseFormat = av_get_packed_sample_fmt(format);
    const int plane = planar ? channel : 0;
    const int channels = frame.ch_layout.nb_channels;
    const int index = planar ? sample : sample * channels + channel;
    switch (baseFormat) {
    case AV_SAMPLE_FMT_U8:
        return (static_cast<const std::uint8_t*>(static_cast<const void*>(data[plane]))[index] - 128) / 128.0;
    case AV_SAMPLE_FMT_S16:
        return static_cast<const std::int16_t*>(static_cast<const void*>(data[plane]))[index] / 32768.0;
    case AV_SAMPLE_FMT_S32:
        return static_cast<const std::int32_t*>(static_cast<const void*>(data[plane]))[index] / 2147483648.0;
    case AV_SAMPLE_FMT_S64:
        return static_cast<double>(static_cast<const std::int64_t*>(static_cast<const void*>(data[plane]))[index] /
            9223372036854775808.0L);
    case AV_SAMPLE_FMT_FLT:
        return static_cast<const float*>(static_cast<const void*>(data[plane]))[index];
    case AV_SAMPLE_FMT_DBL:
        return static_cast<const double*>(static_cast<const void*>(data[plane]))[index];
    default:
        return std::numeric_limits<double>::quiet_NaN();
    }
}

std::optional<std::int64_t> readIntegerSample(const AVFrame& frame,
    AVSampleFormat format, int channel, int sample)
{
    const auto* data = frame.extended_data;
    const bool planar = av_sample_fmt_is_planar(format) != 0;
    const auto baseFormat = av_get_packed_sample_fmt(format);
    const int plane = planar ? channel : 0;
    const int channels = frame.ch_layout.nb_channels;
    const int index = planar ? sample : sample * channels + channel;
    switch (baseFormat) {
    case AV_SAMPLE_FMT_U8:
        return static_cast<std::int64_t>(
            static_cast<const std::uint8_t*>(static_cast<const void*>(data[plane]))[index]) - 128;
    case AV_SAMPLE_FMT_S16:
        return static_cast<const std::int16_t*>(static_cast<const void*>(data[plane]))[index];
    case AV_SAMPLE_FMT_S32:
        return static_cast<const std::int32_t*>(static_cast<const void*>(data[plane]))[index];
    case AV_SAMPLE_FMT_S64:
        return static_cast<std::int64_t*>(static_cast<void*>(data[plane]))[index];
    default:
        return std::nullopt;
    }
}

bool isFullScaleIntegerSample(std::int64_t sample, int sampleBits, int declaredBits)
{
    const int usedBits = declaredBits > 0 && declaredBits <= sampleBits
        ? declaredBits : sampleBits;
    const int shift = sampleBits - usedBits;
    const std::int64_t normalized = shift == 0 ? sample : sample >> shift;
    if (usedBits == 64) {
        return normalized == (std::numeric_limits<std::int64_t>::min)() ||
            normalized == (std::numeric_limits<std::int64_t>::max)();
    }
    const auto minimum = -(std::int64_t{ 1 } << (usedBits - 1));
    const auto maximum = (std::int64_t{ 1 } << (usedBits - 1)) - 1;
    return normalized == minimum || normalized == maximum;
}

void updateHash(AVSHA* hash, const std::uint8_t* data, std::size_t size)
{
    while (size > 0) {
        const auto chunk = static_cast<unsigned int>(std::min<std::size_t>(
            size, std::numeric_limits<unsigned int>::max()));
        av_sha_update(hash, data, chunk);
        data += chunk;
        size -= chunk;
    }
}

bool appendFrameSamples(const AVFrame& frame, AVSampleFormat format,
    AVSHA* pcmHash, AVMD5* flacHash, int declaredBits,
    std::vector<ChannelStats>& stats, std::uint64_t& sampleCount,
    std::uint64_t& integerOrMask, int& integerSampleBits,
    std::uint64_t& firstAudibleSample, std::uint64_t& lastAudibleSample,
    long double& stereoProductSum, bool analyzeSignal,
    std::string& error)
{
    const int channels = frame.ch_layout.nb_channels;
    const int bytesPerSample = av_get_bytes_per_sample(format);
    if (channels <= 0 || channels != static_cast<int>(stats.size()) || bytesPerSample <= 0) {
        error = "Decoder returned an unsupported sample format or channel layout.";
        return false;
    }
    const bool planar = av_sample_fmt_is_planar(format) != 0;
    const auto baseFormat = av_get_packed_sample_fmt(format);
    const bool integerFormat = baseFormat == AV_SAMPLE_FMT_U8 ||
        baseFormat == AV_SAMPLE_FMT_S16 || baseFormat == AV_SAMPLE_FMT_S32 ||
        baseFormat == AV_SAMPLE_FMT_S64;
    if (integerFormat) integerSampleBits = bytesPerSample * 8;
    std::vector<std::uint8_t> interleaved;
    std::vector<std::uint8_t> flacPcmBytes;
    const auto totalBytes = static_cast<std::size_t>(frame.nb_samples) *
        static_cast<std::size_t>(channels) * static_cast<std::size_t>(bytesPerSample);
    interleaved.reserve(totalBytes);
    if (flacHash != nullptr && declaredBits > 0) {
        flacPcmBytes.reserve(static_cast<std::size_t>(frame.nb_samples) *
            static_cast<std::size_t>(channels) *
            static_cast<std::size_t>((declaredBits + 7) / 8));
    }
    for (int sample = 0; sample < frame.nb_samples; ++sample) {
        std::array<double, 2> stereoValues{};
        bool audible = false;
        for (int channel = 0; channel < channels; ++channel) {
            if (analyzeSignal) {
                const double value = readSample(frame, format, channel, sample);
                if (!std::isfinite(value)) {
                    error = "Decoder returned a non-finite or unsupported sample.";
                    return false;
                }
                auto& channelStats = stats[static_cast<std::size_t>(channel)];
                if (channel < static_cast<int>(stereoValues.size())) {
                    stereoValues[static_cast<std::size_t>(channel)] = value;
                }
                audible = audible || std::abs(value) >= 0.001;
                channelStats.sum += value;
                channelStats.sumSquares += static_cast<long double>(value) * value;
                channelStats.peak = std::max(channelStats.peak, std::abs(value));
                if (integerFormat) {
                    const auto integerSample = readIntegerSample(frame, format, channel, sample);
                    if (!integerSample) {
                        error = "Decoder returned an unsupported integer sample format.";
                        return false;
                    }
                    if (isFullScaleIntegerSample(
                        *integerSample, bytesPerSample * 8, declaredBits)) {
                        ++channelStats.clipped;
                    }
                }
                else if (std::abs(value) >= 1.0) {
                    ++channelStats.clipped;
                }
                if (channelStats.hasPrevious &&
                    ((channelStats.previous < 0 && value >= 0) ||
                        (channelStats.previous > 0 && value <= 0))) {
                    ++channelStats.zeroCrossings;
                }
                channelStats.previous = value;
                channelStats.hasPrevious = true;
            }

            const auto* source = frame.extended_data[planar ? channel : 0] +
                static_cast<std::ptrdiff_t>((planar ? sample : sample * channels + channel) *
                    bytesPerSample);
            interleaved.insert(interleaved.end(), source, source + bytesPerSample);

            if (flacHash != nullptr && integerFormat && declaredBits > 0 &&
                declaredBits <= integerSampleBits) {
                const auto raw = readIntegerSample(frame, format, channel, sample);
                if (!raw) continue;
                const int shift = integerSampleBits - declaredBits;
                const std::int64_t normalized = shift == 0 ? *raw : (*raw >> shift);
                integerOrMask |= static_cast<std::uint64_t>(normalized);
                const auto unsignedSample = static_cast<std::uint64_t>(normalized);
                for (int byte = 0; byte < (declaredBits + 7) / 8; ++byte) {
                    const auto valueByte = static_cast<std::uint8_t>(
                        (unsignedSample >> (byte * 8)) & 0xff);
                    flacPcmBytes.push_back(valueByte);
                }
            }
        }
        if (analyzeSignal && channels >= 2) {
            stereoProductSum += static_cast<long double>(stereoValues[0]) * stereoValues[1];
        }
        if (analyzeSignal && audible) {
            const auto absoluteSample = sampleCount + static_cast<std::uint64_t>(sample);
            firstAudibleSample = std::min(firstAudibleSample, absoluteSample);
            lastAudibleSample = absoluteSample;
        }
    }
    updateHash(pcmHash, interleaved.data(), interleaved.size());
    if (!flacPcmBytes.empty()) {
        av_md5_update(flacHash, flacPcmBytes.data(), flacPcmBytes.size());
    }
    sampleCount += static_cast<std::uint64_t>(frame.nb_samples);
    return true;
}

void fail(IntegrityResult& integrity, MediaRecord& record,
    std::string message, std::optional<std::int64_t> position = std::nullopt)
{
    integrity.status = "failed";
    integrity.decodeErrors = 1;
    integrity.firstErrorPosition = position;
    integrity.error = std::move(message);
    record.decodeSupport = "failed";
    if (record.scanTier == "analysis") {
        record.signal.status = "failed";
        record.analysisStatus = "failed";
    }
}

}

static void analyzeAudio(const std::filesystem::path& filePath,
    MediaRecord& record, bool includeSignalAnalysis)
{
    auto& integrity = record.integrity;
    integrity.status = "failed";
    if (const auto digest = hashFile(filePath)) {
        integrity.fileSha256 = *digest;
    }
    else {
        integrity.error = "Unable to read the complete source file for SHA-256.";
    }

#ifdef _WIN32
    const auto widePath = PlatformUtils::WindowsLongPath(filePath);
    const auto utf8Path = PlatformUtils::WideToUTF8(widePath);
#else
    const auto utf8Path = filePath.string();
#endif
    AVFormatContext* rawFormat = nullptr;
    int result = avformat_open_input(&rawFormat, utf8Path.c_str(), nullptr, nullptr);
    if (result < 0) {
        fail(integrity, record, "Cannot open audio stream: " + ffmpegError(result));
        return;
    }
    FormatPtr format(rawFormat);
    result = avformat_find_stream_info(format.get(), nullptr);
    if (result < 0) {
        fail(integrity, record, "Cannot read audio stream information: " + ffmpegError(result));
        return;
    }
    const int streamIndex = av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO,
        -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        fail(integrity, record, "No decodable audio stream was found: " + ffmpegError(streamIndex));
        return;
    }
    const AVStream* stream = format->streams[streamIndex];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (decoder == nullptr) {
        integrity.status = "unsupported";
        integrity.error = "No decoder is available for this audio codec.";
        record.decodeSupport = "unsupported";
        if (includeSignalAnalysis) {
            record.signal.status = "failed";
            record.analysisStatus = "failed";
        }
        return;
    }
    CodecPtr codec(avcodec_alloc_context3(decoder));
    if (!codec) {
        fail(integrity, record, "Unable to allocate decoder context.");
        return;
    }
    result = avcodec_parameters_to_context(codec.get(), stream->codecpar);
    if (result < 0) {
        fail(integrity, record, "Cannot configure audio decoder: " + ffmpegError(result));
        return;
    }
    codec->err_recognition = AV_EF_CRCCHECK | AV_EF_BITSTREAM | AV_EF_BUFFER | AV_EF_EXPLODE;
    result = avcodec_open2(codec.get(), decoder, nullptr);
    if (result < 0) {
        fail(integrity, record, "Cannot initialize audio decoder: " + ffmpegError(result));
        return;
    }
    PacketPtr packet(av_packet_alloc());
    FramePtr frame(av_frame_alloc());
    auto* rawPcmHash = av_sha_alloc();
    auto* rawFlacHash = av_md5_alloc();
    if (!packet || !frame || rawPcmHash == nullptr || rawFlacHash == nullptr ||
        av_sha_init(rawPcmHash, 256) < 0) {
        if (rawPcmHash != nullptr) av_free(rawPcmHash);
        if (rawFlacHash != nullptr) av_free(rawFlacHash);
        fail(integrity, record, "Unable to allocate decode analysis state.");
        return;
    }
    std::unique_ptr<AVSHA, decltype(&av_free)> pcmHash(rawPcmHash, &av_free);
    std::unique_ptr<AVMD5, decltype(&av_free)> flacHash(rawFlacHash, &av_free);
    av_md5_init(flacHash.get());

    const int channelCount = codec->ch_layout.nb_channels;
    if (channelCount <= 0) {
        fail(integrity, record, "Decoder reported an invalid channel count.");
        return;
    }
    std::vector<ChannelStats> stats(static_cast<std::size_t>(channelCount));
    std::uint64_t sampleCount = 0;
    std::uint64_t integerOrMask = 0;
    int integerSampleBits = 0;
    std::uint64_t firstAudibleSample = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t lastAudibleSample = 0;
    long double stereoProductSum = 0;
    int declaredBits = record.declaredBitsPerSample.value_or(
        stream->codecpar->bits_per_raw_sample);
    const bool isFlac = stream->codecpar->codec_id == AV_CODEC_ID_FLAC;
    const int md5Bits = isFlac ? declaredBits : 0;
    std::int64_t currentPosition = -1;
    std::string decodeError;
    EbuR128Filter signalFilter;
    bool signalFilterStarted = false;
    bool signalFilterAvailable = includeSignalAnalysis;
    std::string signalFilterError;

    auto receiveFrames = [&]() {
        while (true) {
            const int receiveResult = avcodec_receive_frame(codec.get(), frame.get());
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) return true;
            if (receiveResult < 0) {
                decodeError = "Audio decode failed: " + ffmpegError(receiveResult);
                return false;
            }
            if (includeSignalAnalysis && signalFilterAvailable) {
                if (!signalFilterStarted) {
                    signalFilterStarted = true;
                    signalFilterAvailable = signalFilter.Initialize(*frame, signalFilterError);
                }
                if (signalFilterAvailable &&
                    !signalFilter.AddFrame(*frame, record.signal, signalFilterError)) {
                    signalFilterAvailable = false;
                }
            }
            if (!appendFrameSamples(*frame, static_cast<AVSampleFormat>(frame->format),
                pcmHash.get(), isFlac ? flacHash.get() : nullptr, md5Bits, stats,
                sampleCount, integerOrMask, integerSampleBits, firstAudibleSample,
                lastAudibleSample, stereoProductSum, includeSignalAnalysis, decodeError)) {
                av_frame_unref(frame.get());
                return false;
            }
            av_frame_unref(frame.get());
        }
    };

    while ((result = av_read_frame(format.get(), packet.get())) >= 0) {
        if (packet->stream_index == streamIndex) {
            currentPosition = packet->pos >= 0 ? packet->pos : packet->pts;
            result = avcodec_send_packet(codec.get(), packet.get());
            if (result == AVERROR(EAGAIN)) {
                if (!receiveFrames()) {
                    av_packet_unref(packet.get());
                    break;
                }
                result = avcodec_send_packet(codec.get(), packet.get());
            }
            if (result < 0) {
                decodeError = "Decoder rejected an audio packet: " + ffmpegError(result);
                av_packet_unref(packet.get());
                break;
            }
            if (!receiveFrames()) {
                av_packet_unref(packet.get());
                break;
            }
        }
        av_packet_unref(packet.get());
    }
    if (result != AVERROR_EOF && decodeError.empty()) {
        decodeError = "Audio packet read failed: " + ffmpegError(result);
    }
    if (decodeError.empty()) {
        result = avcodec_send_packet(codec.get(), nullptr);
        if (result < 0 && result != AVERROR_EOF) {
            decodeError = "Decoder flush failed: " + ffmpegError(result);
        }
        else if (!receiveFrames()) {
            if (decodeError.empty()) decodeError = "Audio decode failed while flushing.";
        }
    }
    if (!decodeError.empty()) {
        fail(integrity, record, std::move(decodeError), currentPosition >= 0
            ? std::optional<std::int64_t>(currentPosition) : std::nullopt);
        return;
    }
    if (includeSignalAnalysis && signalFilterAvailable &&
        !signalFilter.Finish(record.signal, signalFilterError)) {
        signalFilterAvailable = false;
    }
    if (sampleCount == 0) {
        fail(integrity, record, "Decoder produced no audio samples.");
        return;
    }

    integrity.status = integrity.fileSha256 ? "ok" : "partial";
    integrity.decodedSamples = sampleCount;
    if (record.totalSamples && *record.totalSamples != sampleCount) {
        integrity.status = "failed";
        integrity.error = "Decoded sample count " + std::to_string(sampleCount) +
            " differs from declared sample count " + std::to_string(*record.totalSamples) + ".";
        record.findings.push_back({
            "SAMPLE_COUNT_MISMATCH", "error", "high", *integrity.error
        });
    }
    std::array<std::uint8_t, 32> pcmDigest{};
    av_sha_final(pcmHash.get(), pcmDigest.data());
    integrity.pcmSha256 = hexDigest(pcmDigest.data(), pcmDigest.size());
    if (isFlac && record.flacMd5 && declaredBits > 0 &&
        declaredBits <= integerSampleBits) {
        std::array<std::uint8_t, 16> decodedMd5{};
        av_md5_final(flacHash.get(), decodedMd5.data());
        const auto decodedDigest = hexDigest(decodedMd5.data(), decodedMd5.size());
        integrity.flacMd5Matches = decodedDigest == *record.flacMd5;
        if (!*integrity.flacMd5Matches) {
            spdlog::error("FLAC MD5 mismatch for '{}': decoded {}, declared {}.",
                CommonUtils::path_to_utf8(filePath), decodedDigest, *record.flacMd5);
            integrity.status = "failed";
            integrity.error = "Decoded PCM does not match the FLAC STREAMINFO MD5.";
        }
    }
    record.decodeSupport = integrity.status == "failed" ? "failed" : "verified";

    if (!includeSignalAnalysis) return;
    auto& signal = record.signal;
    signal.status = "partial";
    signal.method = signalFilterAvailable
        ? "decoded-sample-stats-v1; ebur128"
        : "decoded-sample-stats-v1";
    if (!signalFilterAvailable && !signalFilterError.empty()) {
        spdlog::warn("Signal filter unavailable for '{}': {}",
            CommonUtils::path_to_utf8(filePath), signalFilterError);
    }
    std::uint64_t totalClipped = 0;
    for (std::size_t channel = 0; channel < stats.size(); ++channel) {
        const auto& item = stats[channel];
        const auto prefix = "channel" + std::to_string(channel) + ".";
        const auto count = static_cast<long double>(sampleCount);
        const double rms = std::sqrt(static_cast<double>(item.sumSquares / count));
        const double rmsDb = rms > 0 ? 20.0 * std::log10(rms) :
            -std::numeric_limits<double>::infinity();
        const double peakDb = item.peak > 0 ? 20.0 * std::log10(item.peak) :
            -std::numeric_limits<double>::infinity();
        signal.measurements[prefix + "dcOffset"] =
            static_cast<double>(item.sum / count);
        signal.measurements[prefix + "rmsDbfs"] = std::isfinite(rmsDb) ? rmsDb : -120.0;
        signal.measurements[prefix + "samplePeakDbfs"] =
            std::isfinite(peakDb) ? peakDb : -120.0;
        signal.measurements[prefix + "zeroCrossings"] =
            static_cast<double>(item.zeroCrossings);
        totalClipped += item.clipped;
    }
    if (record.sampleRateHz && *record.sampleRateHz > 0) {
        const auto rate = static_cast<double>(*record.sampleRateHz);
        const auto first = firstAudibleSample == std::numeric_limits<std::uint64_t>::max()
            ? sampleCount : firstAudibleSample;
        const auto lastEnd = firstAudibleSample == std::numeric_limits<std::uint64_t>::max()
            ? 0 : lastAudibleSample + 1;
        signal.leadingSilenceSeconds = static_cast<double>(first) / rate;
        signal.trailingSilenceSeconds =
            static_cast<double>(sampleCount - std::min(sampleCount, lastEnd)) / rate;
        signal.measurements["leadingSilenceSeconds"] = *signal.leadingSilenceSeconds;
        signal.measurements["trailingSilenceSeconds"] = *signal.trailingSilenceSeconds;
        signal.measurements["silenceThresholdDbfs"] = -60.0;
    }
    if (stats.size() == 2) {
        const long double count = static_cast<long double>(sampleCount);
        const long double leftMean = stats[0].sum / count;
        const long double rightMean = stats[1].sum / count;
        const long double covariance = stereoProductSum / count - leftMean * rightMean;
        const long double leftVariance = stats[0].sumSquares / count - leftMean * leftMean;
        const long double rightVariance = stats[1].sumSquares / count - rightMean * rightMean;
        if (leftVariance > 0 && rightVariance > 0) {
            const double correlation = static_cast<double>(covariance /
                std::sqrt(leftVariance * rightVariance));
            signal.measurements["stereoCorrelation"] = correlation;
            if (correlation > 0.999) {
                record.findings.push_back({
                    "DUAL_MONO", "info", "high",
                    "Left/right decoded channel correlation is " + std::to_string(correlation) + "."
                });
            }
            else if (correlation < -0.999) {
                record.findings.push_back({
                    "PHASE_INVERTED_CHANNEL", "warning", "high",
                    "Left/right decoded channel correlation is " + std::to_string(correlation) + "."
                });
            }
        }
    }
    signal.clippedSamples = totalClipped;
    if (!isFlac) {
        signal.usedBitsStatus = "unsupported";
    }
    else if (integerSampleBits > 0 && declaredBits > 0 && declaredBits <= integerSampleBits &&
        integerOrMask != 0) {
        const int trailingZeroBits = std::countr_zero(integerOrMask);
        signal.usedBits = std::max(1, declaredBits - trailingZeroBits);
        signal.usedBitsStatus = "ok";
    }
    else if (integerSampleBits == 0) {
        signal.usedBitsStatus = "unsupported";
    }
    else {
        signal.usedBitsStatus = "unavailable";
    }
    if (signal.usedBits && record.declaredBitsPerSample &&
        *signal.usedBits < *record.declaredBitsPerSample) {
        record.findings.push_back({
            "ZERO_PADDED_HIRES", "warning", "high",
            "Observed integer samples use " + std::to_string(*signal.usedBits) +
                " of " + std::to_string(*record.declaredBitsPerSample) +
                " declared bits; this indicates zero-padded low bits, not effective resolution."
        });
    }
    if (totalClipped > 0) {
        record.findings.push_back({
            "CLIPPED_SAMPLES", "warning", "medium",
            std::to_string(totalClipped) + " decoded samples reached or exceeded full scale."
        });
    }
    const auto leadingSilence = signal.leadingSilenceSeconds.value_or(0.0);
    const auto trailingSilence = signal.trailingSilenceSeconds.value_or(0.0);
    if (leadingSilence >= 0.5 || trailingSilence >= 0.5) {
        record.findings.push_back({
            "LARGE_DIGITAL_SILENCE", "info", "medium",
            "Leading silence: " + std::to_string(leadingSilence) +
                " s; trailing silence: " + std::to_string(trailingSilence) +
                " s at a -60 dBFS threshold."
        });
    }
}

void DecodeAnalyzer::Analyze(const std::filesystem::path& filePath,
    MediaRecord& record, bool includeSignalAnalysis)
{
    FFmpeg::ScopedLogCapture logCapture(filePath, "decode");
    analyzeAudio(filePath, record, includeSignalAnalysis);
    for (const auto& log : logCapture.TakeLogs()) {
        record.diagnostics.push_back({
            log.stage, "ffmpeg", DiagnosticCode::FfmpegLog, ParseDiagnosticSeverity(log.level).value(),
            log.component + ": " + log.message
        });
    }
    if (record.integrity.status == "failed" && record.integrity.error) {
        record.diagnostics.push_back({
            "decode", "ffmpeg", DiagnosticCode::DecodeFailed, DiagnosticSeverity::Error, *record.integrity.error
        });
    }
    else if (record.integrity.status == "unsupported" && record.integrity.error) {
        record.diagnostics.push_back({
            "decode", "ffmpeg", DiagnosticCode::DecodeUnsupported, DiagnosticSeverity::Warning, *record.integrity.error
        });
    }
}
