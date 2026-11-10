#include "PreservationAudit.h"

#include "CommonUtils.h"
#include "FFmpegWrapper.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

bool PreservationAudit::VerifyCopy(const std::filesystem::path& source, const std::filesystem::path& copy)
{
    std::ifstream left(source, std::ios::binary);
    std::ifstream right(copy, std::ios::binary);
    if (!left || !right) {
        spdlog::error("Cannot open files for companion verification");
        return false;
    }
    std::array<char, 8192> a{}, b{};
    do {
        left.read(a.data(), a.size());
        right.read(b.data(), b.size());
        if (left.bad() || right.bad() || left.gcount() != right.gcount() ||
            !std::equal(a.begin(), a.begin() + left.gcount(), b.begin())) {
            spdlog::error("Copied companion differs from its source or cannot be read");
            return false;
        }
    } while (left.gcount() != 0);
    if (!left.eof() || !right.eof()) {
        spdlog::error("Companion verification did not reach end of both files");
        return false;
    }
    return true;
}

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace
{
struct ParseError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct Tag
{
    std::string key;
    std::string value;
};

using TagMap = std::map<std::string, std::vector<std::string>>;

struct Picture
{
    std::vector<std::uint8_t> payload;
    std::string mime;
    std::string description;
    std::uint32_t type{ 0 };
    std::uint32_t width{ 0 };
    std::uint32_t height{ 0 };
    std::uint32_t depth{ 0 };
    std::uint32_t colors{ 0 };
    AVCodecID codec{ AV_CODEC_ID_NONE };
    bool supported{ false };
    bool descriptorsVerified{ true };
    bool nativeDimensions{ true };
};

struct RawBlock
{
    std::uint8_t type{ 0 };
    std::vector<std::uint8_t> data;
};

struct NativeMetadata
{
    std::vector<Tag> comments;
    std::vector<Picture> pictures;
    std::vector<RawBlock> opaqueBlocks;
    std::string vendor;
    bool hasVendor{ false };
    std::size_t streamInfoCount{ 0 };
    std::size_t seekTableCount{ 0 };
};

struct Chapter
{
    double start{ 0.0 };
    double end{ 0.0 };
    TagMap tags;
};

struct FormatContextCloser
{
    void operator()(AVFormatContext* context) const noexcept
    {
        if (context != nullptr) {
            avformat_close_input(&context);
        }
    }
};

struct Media
{
    std::unique_ptr<AVFormatContext, FormatContextCloser> context;
    AVCodecID audioCodec{ AV_CODEC_ID_NONE };
    bool isFlac{ false };
    bool nativeFlac{ false };
    TagMap tags;
    std::vector<Picture> pictures;
    std::vector<Chapter> chapters;
};

std::string ffmpegError(int error)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    if (av_strerror(error, buffer.data(), buffer.size()) < 0) {
        return "FFmpeg error " + std::to_string(error);
    }
    return buffer.data();
}

std::string asciiLower(std::string value)
{
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

std::string canonicalKey(const std::string& key)
{
    auto lowered = asciiLower(key);
    // These are deliberately the only cross-container spelling aliases.
    if (lowered == "track" || lowered == "tracknumber") {
        return "tracknumber";
    }
    if (lowered == "disc" || lowered == "discnumber") {
        return "discnumber";
    }
    if (lowered == "date" || lowered == "year") {
        return "date";
    }
    if (lowered == "albumartist" || lowered == "album_artist") {
        return "albumartist";
    }
    if (lowered == "comment" || lowered == "description") {
        return "comment";
    }
    if (lowered == "totaltracks" || lowered == "tracktotal") {
        return "tracktotal";
    }
    if (lowered == "totaldiscs" || lowered == "disctotal") {
        return "disctotal";
    }
    return lowered;
}

void addTag(TagMap& tags, const std::string& key, const std::string& value)
{
    tags[canonicalKey(key)].push_back(value);
}

TagMap makeTagMap(const std::vector<Tag>& tags)
{
    TagMap result;
    for (const auto& tag : tags) {
        addTag(result, tag.key, tag.value);
    }
    for (auto& [key, values] : result) {
        (void)key;
        std::sort(values.begin(), values.end());
    }
    return result;
}

TagMap readDictionary(const AVDictionary* dictionary)
{
    TagMap result;
    const AVDictionaryEntry* entry = nullptr;
    while ((entry = av_dict_get(dictionary, "", entry, AV_DICT_IGNORE_SUFFIX)) != nullptr) {
        addTag(result, entry->key, entry->value);
    }
    for (auto& [key, values] : result) {
        (void)key;
        std::sort(values.begin(), values.end());
    }
    return result;
}

std::vector<std::uint8_t> readExact(std::ifstream& input, std::uint32_t size,
    std::uint64_t fileSize, std::uint64_t& position)
{
    if (size > 0x00ffffffU || position > fileSize ||
        static_cast<std::uint64_t>(size) > fileSize - position) {
        throw ParseError("FLAC metadata block exceeds the file or format size limit");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (size != 0 && !input.read(reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(size))) {
        throw ParseError("short read in FLAC metadata block");
    }
    position += size;
    return bytes;
}

std::uint32_t readBigEndian32(const std::vector<std::uint8_t>& bytes, std::size_t& offset)
{
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw ParseError("truncated FLAC metadata field");
    }
    const auto value = (static_cast<std::uint32_t>(bytes[offset]) << 24) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
        static_cast<std::uint32_t>(bytes[offset + 3]);
    offset += 4;
    return value;
}

std::uint32_t readLittleEndian32(const std::vector<std::uint8_t>& bytes, std::size_t& offset)
{
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw ParseError("truncated Vorbis comment field");
    }
    const auto value = static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    offset += 4;
    return value;
}

std::string readText(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
    std::uint32_t length, const char* description)
{
    if (offset > bytes.size() || length > bytes.size() - offset) {
        throw ParseError(std::string("truncated ") + description);
    }
    if (length == 0) {
        return {};
    }
    const auto* begin = reinterpret_cast<const char*>(bytes.data() + offset);
    std::string value(begin, begin + length);
    offset += length;
    return value;
}

void parseVorbisComments(const std::vector<std::uint8_t>& bytes, NativeMetadata& metadata)
{
    std::size_t offset = 0;
    const auto vendorSize = readLittleEndian32(bytes, offset);
    metadata.vendor = readText(bytes, offset, vendorSize, "Vorbis vendor string");
    metadata.hasVendor = true;
    const auto count = readLittleEndian32(bytes, offset);
    if (count > (bytes.size() - offset) / 4) {
        throw ParseError("Vorbis comment count exceeds the block size");
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto length = readLittleEndian32(bytes, offset);
        const auto text = readText(bytes, offset, length, "Vorbis comment");
        const auto separator = text.find('=');
        if (separator == std::string::npos || separator == 0) {
            throw ParseError("invalid Vorbis comment key/value");
        }
        metadata.comments.push_back({ text.substr(0, separator), text.substr(separator + 1) });
    }
}

AVCodecID codecForMime(const std::string& mime)
{
    const auto lowered = asciiLower(mime);
    if (lowered == "image/jpeg" || lowered == "image/jpg") return AV_CODEC_ID_MJPEG;
    if (lowered == "image/png") return AV_CODEC_ID_PNG;
    if (lowered == "image/gif") return AV_CODEC_ID_GIF;
    if (lowered == "image/tiff") return AV_CODEC_ID_TIFF;
    if (lowered == "image/bmp" || lowered == "image/x-ms-bmp") return AV_CODEC_ID_BMP;
    if (lowered == "image/webp") return AV_CODEC_ID_WEBP;
    if (lowered == "image/jxl" || lowered == "image/jpeg-xl") return AV_CODEC_ID_JPEGXL;
    return AV_CODEC_ID_NONE;
}

bool supportedPictureCodec(AVCodecID codec)
{
    switch (codec) {
    case AV_CODEC_ID_MJPEG:
    case AV_CODEC_ID_PNG:
    case AV_CODEC_ID_GIF:
    case AV_CODEC_ID_TIFF:
    case AV_CODEC_ID_BMP:
    case AV_CODEC_ID_WEBP:
    case AV_CODEC_ID_JPEGXL:
        return true;
    default:
        return false;
    }
}

void parsePicture(const std::vector<std::uint8_t>& bytes, NativeMetadata& metadata)
{
    std::size_t offset = 0;
    Picture picture;
    picture.type = readBigEndian32(bytes, offset);
    const auto mimeSize = readBigEndian32(bytes, offset);
    picture.mime = readText(bytes, offset, mimeSize, "FLAC picture MIME type");
    const auto descriptionSize = readBigEndian32(bytes, offset);
    picture.description = readText(bytes, offset, descriptionSize, "FLAC picture description");
    picture.width = readBigEndian32(bytes, offset);
    picture.height = readBigEndian32(bytes, offset);
    picture.depth = readBigEndian32(bytes, offset);
    picture.colors = readBigEndian32(bytes, offset);
    const auto imageSize = readBigEndian32(bytes, offset);
    if (offset > bytes.size() || imageSize > bytes.size() - offset) {
        throw ParseError("truncated FLAC picture payload");
    }
    picture.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + imageSize));
    picture.codec = codecForMime(picture.mime);
    picture.supported = supportedPictureCodec(picture.codec);
    metadata.pictures.push_back(std::move(picture));
}

NativeMetadata readNativeFlac(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw ParseError("cannot open native FLAC source/output");
    }
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        throw ParseError("cannot determine native FLAC file size");
    }
    const auto fileSize = static_cast<std::uint64_t>(end);
    input.seekg(0, std::ios::beg);
    std::uint64_t position = 0;

    std::array<std::uint8_t, 10> prefix{};
    if (fileSize >= 3) {
        if (!input.read(reinterpret_cast<char*>(prefix.data()), 3)) {
            throw ParseError("cannot read native FLAC prefix");
        }
        position = 3;
    }
    if (fileSize >= 3 && prefix[0] == 'I' && prefix[1] == 'D' && prefix[2] == '3') {
        if (fileSize < prefix.size() || !input.read(
            reinterpret_cast<char*>(prefix.data() + 3), 7)) {
            throw ParseError("truncated leading ID3 tag");
        }
        position = prefix.size();
        if (prefix[3] < 2 || prefix[3] > 4 || (prefix[5] & 0x0f) != 0) {
            throw ParseError("invalid leading ID3 tag header");
        }
        std::uint64_t tagSize = 0;
        for (std::size_t index = 6; index < 10; ++index) {
            if ((prefix[index] & 0x80) != 0) {
                throw ParseError("invalid leading ID3 tag size");
            }
            tagSize = (tagSize << 7) | prefix[index];
        }
        if (prefix[3] == 4 && (prefix[5] & 0x10) != 0) {
            tagSize += 10;
        }
        if (tagSize > fileSize - position) {
            throw ParseError("leading ID3 tag exceeds file size");
        }
        position += tagSize;
        input.seekg(static_cast<std::streamoff>(position), std::ios::beg);
        if (!input) {
            throw ParseError("cannot seek past leading ID3 tag");
        }
    }
    else {
        input.seekg(0, std::ios::beg);
        position = 0;
    }

    std::array<char, 4> marker{};
    if (fileSize - position < marker.size() ||
        !input.read(marker.data(), static_cast<std::streamsize>(marker.size()))) {
        throw ParseError("missing native FLAC marker");
    }
    position += marker.size();
    if (std::string(marker.data(), marker.size()) != "fLaC") {
        throw ParseError("native FLAC marker is not present after any leading ID3 tag");
    }

    NativeMetadata metadata;
    bool last = false;
    bool first = true;
    std::uint64_t totalBlockBytes = 0;
    std::size_t blockCount = 0;
    while (!last) {
        std::array<std::uint8_t, 4> header{};
        if (fileSize - position < header.size() ||
            !input.read(reinterpret_cast<char*>(header.data()),
                static_cast<std::streamsize>(header.size()))) {
            throw ParseError("truncated FLAC metadata block header");
        }
        position += header.size();
        last = (header[0] & 0x80) != 0;
        const auto type = static_cast<std::uint8_t>(header[0] & 0x7f);
        const auto size = (static_cast<std::uint32_t>(header[1]) << 16) |
            (static_cast<std::uint32_t>(header[2]) << 8) |
            static_cast<std::uint32_t>(header[3]);
        if ((first && (type != 0 || size != 34)) || (!first && type == 0) || type == 127) {
            throw ParseError("invalid FLAC metadata block sequence");
        }
        first = false;
        if (++blockCount > 1000000 || totalBlockBytes > (512ULL * 1024ULL * 1024ULL) - size) {
            throw ParseError("FLAC metadata exceeds the parser safety limit");
        }
        totalBlockBytes += size;
        auto bytes = readExact(input, size, fileSize, position);
        switch (type) {
        case 0:
            ++metadata.streamInfoCount;
            break;
        case 1:
            break; // Padding is intentionally not audited.
        case 2:
            metadata.opaqueBlocks.push_back({ type, std::move(bytes) });
            break;
        case 3:
            ++metadata.seekTableCount;
            break;
        case 4:
            parseVorbisComments(bytes, metadata);
            break;
        case 5:
            metadata.opaqueBlocks.push_back({ type, std::move(bytes) });
            break;
        case 6:
            parsePicture(bytes, metadata);
            break;
        default:
            metadata.opaqueBlocks.push_back({ type, std::move(bytes) });
            break;
        }
    }
    return metadata;
}

bool hasNativeFlacMarker(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw ParseError("cannot open FLAC file to identify its container");
    }
    std::array<std::uint8_t, 10> prefix{};
    if (!input.read(reinterpret_cast<char*>(prefix.data()), 3)) {
        return false;
    }
    if (prefix[0] == 'f' && prefix[1] == 'L' && prefix[2] == 'a') {
        input.seekg(0, std::ios::beg);
        std::array<char, 4> marker{};
        return input.read(marker.data(), static_cast<std::streamsize>(marker.size())) &&
            std::string(marker.data(), marker.size()) == "fLaC";
    }
    if (prefix[0] != 'I' || prefix[1] != 'D' || prefix[2] != '3') {
        return false;
    }
    if (!input.read(reinterpret_cast<char*>(prefix.data() + 3), 7) ||
        prefix[3] < 2 || prefix[3] > 4 || (prefix[5] & 0x0f) != 0) {
        throw ParseError("invalid or truncated leading ID3 tag header");
    }
    std::uint64_t tagSize = 0;
    for (std::size_t index = 6; index < 10; ++index) {
        if ((prefix[index] & 0x80) != 0) {
            throw ParseError("invalid leading ID3 tag size");
        }
        tagSize = (tagSize << 7) | prefix[index];
    }
    if (prefix[3] == 4 && (prefix[5] & 0x10) != 0) {
        tagSize += 10;
    }
    if (tagSize > static_cast<std::uint64_t>((std::numeric_limits<std::streamoff>::max)()) - 10) {
        throw ParseError("leading ID3 tag offset exceeds the supported file range");
    }
    input.seekg(static_cast<std::streamoff>(10 + tagSize), std::ios::beg);
    std::array<char, 4> marker{};
    return input && input.read(marker.data(), static_cast<std::streamsize>(marker.size())) &&
        std::string(marker.data(), marker.size()) == "fLaC";
}

void addDictionaryTags(const AVDictionary* dictionary, TagMap& tags)
{
    const auto values = readDictionary(dictionary);
    for (const auto& [key, occurrences] : values) {
        auto& destination = tags[key];
        destination.insert(destination.end(), occurrences.begin(), occurrences.end());
        std::sort(destination.begin(), destination.end());
    }
}

TagMap readAllTags(const AVFormatContext* context, int audioStreamIndex)
{
    TagMap tags;
    addDictionaryTags(context->metadata, tags);
    if (audioStreamIndex >= 0 &&
        static_cast<unsigned int>(audioStreamIndex) < context->nb_streams) {
        addDictionaryTags(context->streams[audioStreamIndex]->metadata, tags);
    }
    return tags;
}

std::string streamText(const AVStream* stream, const char* key)
{
    const auto* value = av_dict_get(stream->metadata, key, nullptr, 0);
    return value == nullptr ? std::string{} : value->value;
}

std::vector<Picture> readAttachedPictures(const AVFormatContext* context)
{
    std::vector<Picture> pictures;
    for (unsigned int index = 0; index < context->nb_streams; ++index) {
        const auto* stream = context->streams[index];
        if (stream == nullptr ||
            (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0) {
            continue;
        }
        if (stream->codecpar == nullptr) {
            throw ParseError("attached-picture stream has no codec parameters");
        }
        const auto& packet = stream->attached_pic;
        if (packet.size < 0 || (packet.size > 0 && packet.data == nullptr)) {
            throw ParseError("invalid attached-picture packet");
        }
        Picture picture;
        picture.codec = stream->codecpar->codec_id;
        picture.mime = avcodec_get_name(picture.codec);
        picture.description = streamText(stream, "title");
        if (picture.description.empty()) {
            picture.description = streamText(stream, "comment");
        }
        picture.type = 0;
        picture.descriptorsVerified = false;
        picture.nativeDimensions = false;
        picture.width = stream->codecpar->width > 0
            ? static_cast<std::uint32_t>(stream->codecpar->width) : 0;
        picture.height = stream->codecpar->height > 0
            ? static_cast<std::uint32_t>(stream->codecpar->height) : 0;
        if (packet.size > 0) {
            picture.payload.assign(packet.data, packet.data + packet.size);
        }
        picture.supported = supportedPictureCodec(picture.codec);
        pictures.push_back(std::move(picture));
    }
    return pictures;
}

void readMp3PictureDescriptors(const std::filesystem::path& path, std::vector<Picture>& pictures)
{
    std::ifstream input(path, std::ios::binary);
    std::array<unsigned char, 10> header{};
    if (!input.read(reinterpret_cast<char*>(header.data()), header.size()) ||
        std::string(reinterpret_cast<char*>(header.data()), 3) != "ID3" ||
        header[3] != 4 || header[5] != 0) {
        return; // Other ID3 encodings remain explicitly NotVerified.
    }
    const auto synchsafe = [](const unsigned char* bytes) -> std::uint32_t {
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index) {
            if (bytes[index] & 0x80) throw ParseError("invalid ID3v2.4 frame size");
            value = (value << 7) | bytes[index];
        }
        return value;
    };
    const auto size = synchsafe(header.data() + 6);
    if (size > 64U * 1024U * 1024U) throw ParseError("ID3 metadata exceeds the parser safety limit");
    std::vector<unsigned char> bytes(size);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size))
        throw ParseError("truncated ID3v2.4 metadata");
    std::vector<bool> used(pictures.size(), false);
    for (std::size_t offset = 0; offset + 10 <= bytes.size() && bytes[offset] != 0;) {
        const auto frameSize = synchsafe(bytes.data() + offset + 4);
        const bool apic = std::string(reinterpret_cast<char*>(bytes.data() + offset), 4) == "APIC";
        const bool plain = bytes[offset + 8] == 0 && bytes[offset + 9] == 0;
        offset += 10;
        if (frameSize > bytes.size() - offset) throw ParseError("ID3 frame exceeds its tag");
        const auto end = offset + frameSize;
        if (apic && plain && frameSize != 0 && (bytes[offset] == 0 || bytes[offset] == 3)) {
            const auto encoding = bytes[offset];
            const auto begin = offset + 1;
            auto mimeEnd = begin;
            while (mimeEnd < end && bytes[mimeEnd] != 0) ++mimeEnd;
            if (mimeEnd + 2 > end) throw ParseError("truncated APIC MIME/type");
            const std::string mime(bytes.begin() + begin, bytes.begin() + mimeEnd);
            const auto type = bytes[mimeEnd + 1];
            const auto descriptionBegin = mimeEnd + 2;
            auto descriptionEnd = descriptionBegin;
            while (descriptionEnd < end && bytes[descriptionEnd] != 0) ++descriptionEnd;
            if (descriptionEnd == end) throw ParseError("truncated APIC description");
            std::string description;
            for (auto index = descriptionBegin; index < descriptionEnd; ++index) {
                const auto character = bytes[index];
                if (encoding == 0 && character >= 128) {
                    description += static_cast<char>(0xc0 | (character >> 6));
                    description += static_cast<char>(0x80 | (character & 0x3f));
                } else description += static_cast<char>(character);
            }
            const auto payloadBegin = descriptionEnd + 1;
            for (std::size_t index = 0; index < pictures.size(); ++index) {
                auto& picture = pictures[index];
                if (!used[index] && picture.payload.size() == end - payloadBegin &&
                    std::equal(picture.payload.begin(), picture.payload.end(), bytes.begin() + payloadBegin)) {
                    used[index] = true;
                    picture.mime = mime;
                    picture.type = type;
                    picture.description = description;
                    picture.descriptorsVerified = true;
                    break;
                }
            }
        }
        offset = end;
    }
}

std::vector<Chapter> readChapters(const AVFormatContext* context)
{
    std::vector<Chapter> chapters;
    chapters.reserve(context->nb_chapters);
    for (unsigned int index = 0; index < context->nb_chapters; ++index) {
        const auto* chapter = context->chapters[index];
        if (chapter == nullptr) {
            continue;
        }
        Chapter item;
        if (chapter->time_base.num <= 0 || chapter->time_base.den <= 0) {
            throw ParseError("chapter has an invalid time base");
        }
        const double scale = av_q2d(chapter->time_base);
        item.start = static_cast<double>(chapter->start) * scale;
        item.end = static_cast<double>(chapter->end) * scale;
        if (!std::isfinite(item.start) || !std::isfinite(item.end)) {
            throw ParseError("chapter has a non-finite start/end time");
        }
        item.tags = readDictionary(chapter->metadata);
        chapters.push_back(std::move(item));
    }
    return chapters;
}

bool openMedia(const std::filesystem::path& path, Media& media, std::string& error)
{
    AVFormatContext* raw = nullptr;
    const auto utf8Path = CommonUtils::path_to_utf8(path);
    const int openResult = avformat_open_input(&raw, utf8Path.c_str(), nullptr, nullptr);
    if (openResult < 0) {
        error = "cannot open media with FFmpeg: " + ffmpegError(openResult);
        if (raw != nullptr) {
            avformat_close_input(&raw);
        }
        return false;
    }
    media.context.reset(raw);
    const int infoResult = avformat_find_stream_info(media.context.get(), nullptr);
    if (infoResult < 0) {
        error = "cannot read media stream information: " + ffmpegError(infoResult);
        return false;
    }
    int audioStreamIndex = -1;
    for (unsigned int index = 0; index < media.context->nb_streams; ++index) {
        const auto* stream = media.context->streams[index];
        if (stream != nullptr && stream->codecpar != nullptr &&
            stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex = static_cast<int>(index);
            media.audioCodec = stream->codecpar->codec_id;
            break;
        }
    }
    if (audioStreamIndex < 0) {
        error = "media has no readable audio stream";
        return false;
    }
    media.isFlac = media.audioCodec == AV_CODEC_ID_FLAC;
    media.nativeFlac = media.isFlac && hasNativeFlacMarker(path);
    media.tags = readAllTags(media.context.get(), audioStreamIndex);
    media.chapters = readChapters(media.context.get());
    if (!media.nativeFlac) {
        media.pictures = readAttachedPictures(media.context.get());
    }
    return true;
}

void addEntry(PreservationAudit::Result& result, PreservationAudit::Status status,
    std::string resource, std::string name, std::string detail, bool fatal = false)
{
    if (fatal) {
        result.ok = false;
        spdlog::error("Preservation audit {} [{} / {}]: {}",
            PreservationAudit::StatusName(status), resource, name, detail);
    }
    result.entries.push_back({ status, std::move(resource), std::move(name),
        std::move(detail), fatal });
}

void addFatalNotVerified(PreservationAudit::Result& result, const std::string& resource,
    const std::string& name, const std::string& detail)
{
    addEntry(result, PreservationAudit::Status::NotVerified, resource, name, detail, true);
}

bool isGeneratedTag(const std::string& key)
{
    return key == "encoder" || key == "encoded_by" || key == "vendor";
}

bool isAudioDerivedTag(const std::string& key)
{
    return key.find("replaygain") != std::string::npos ||
        key.compare(0, 5, "r128_") == 0;
}

void compareTags(PreservationAudit::Result& result, const Media& source, const Media& output,
    const NativeMetadata* sourceNative, const NativeMetadata* outputNative,
    bool outputIsMp3, bool verifiedArchive)
{
    TagMap sourceTags = source.tags;
    TagMap outputTags = output.tags;
    if (sourceNative != nullptr) {
        sourceTags = makeTagMap(sourceNative->comments);
    }
    if (outputNative != nullptr) {
        outputTags = makeTagMap(outputNative->comments);
    }
    const auto spellings = [](const Media& media, const NativeMetadata* native) {
        std::map<std::string, std::set<std::string>> names;
        if (native != nullptr) {
            for (const auto& tag : native->comments)
                names[canonicalKey(tag.key)].insert(asciiLower(tag.key));
        } else {
            const auto collect = [&](const AVDictionary* dictionary) {
                const AVDictionaryEntry* tag = nullptr;
                while ((tag = av_dict_get(dictionary, "", tag, AV_DICT_IGNORE_SUFFIX)) != nullptr)
                    names[canonicalKey(tag->key)].insert(asciiLower(tag->key));
            };
            collect(media.context->metadata);
            for (unsigned int index = 0; index < media.context->nb_streams; ++index) {
                const auto* stream = media.context->streams[index];
                if (stream != nullptr && stream->codecpar != nullptr &&
                    stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                    collect(stream->metadata);
                    break;
                }
            }
        }
        return names;
    };
    const auto sourceSpellings = spellings(source, sourceNative);
    const auto outputSpellings = spellings(output, outputNative);

    if (sourceNative != nullptr && sourceNative->hasVendor && outputNative != nullptr &&
        outputNative->hasVendor && sourceNative->vendor != outputNative->vendor) {
        addEntry(result, PreservationAudit::Status::Normalized, "tag-vendor", "Vorbis vendor",
            "Encoder/vendor text differs; this is an informational generated-field change.");
    }

    std::set<std::string> keys;
    for (const auto& [key, values] : sourceTags) {
        (void)values;
        keys.insert(key);
    }
    for (const auto& [key, values] : outputTags) {
        (void)values;
        keys.insert(key);
    }

    for (const auto& key : keys) {
        const auto sourceFound = sourceTags.find(key);
        const auto outputFound = outputTags.find(key);
        const std::vector<std::string> empty;
        const auto& sourceValues = sourceFound == sourceTags.end() ? empty : sourceFound->second;
        const auto& outputValues = outputFound == outputTags.end() ? empty : outputFound->second;
        const bool equal = sourceValues == outputValues;
        const bool repeated = sourceValues.size() > 1 || outputValues.size() > 1;

        if (isGeneratedTag(key)) {
            if (!equal) {
                addEntry(result, PreservationAudit::Status::Normalized, "tags", key,
                    "Generated encoder/vendor metadata differs; informational only.");
            }
            continue;
        }

        if (equal && outputIsMp3 && repeated) {
            if (verifiedArchive) {
                addEntry(result, PreservationAudit::Status::SidecarOnly, "tags", key,
                    "FFmpeg's MP3 dictionary cannot prove repeated ID3 frame multiplicity; "
                    "the original occurrences are retained in the verified archive.");
            }
            else {
                addEntry(result, PreservationAudit::Status::NotVerified, "tags", key,
                    "FFmpeg's MP3 dictionary cannot prove repeated ID3 frame multiplicity.", true);
            }
            continue;
        }

        if (equal) {
            std::string detail = "All observed occurrences match.";
            if (isAudioDerivedTag(key)) {
                detail += " ReplayGain/R128 data is copied metadata and may be stale after audio transformation.";
                spdlog::warn("Preservation audit [tags / {}]: ReplayGain/R128 may be stale after transformation.", key);
            }
            const bool aliasChanged = sourceSpellings.at(key) != outputSpellings.at(key);
            if (aliasChanged) detail += " Container tag-key alias changed; values are unchanged.";
            addEntry(result, aliasChanged ? PreservationAudit::Status::Normalized :
                PreservationAudit::Status::Preserved, "tags", key, std::move(detail));
            continue;
        }

        const std::string detail = sourceValues.empty()
            ? "Output contains tag occurrences not present in the source."
            : (outputValues.empty() ? "One or more source tag occurrences are absent from output."
                : "Tag values or occurrence counts differ.");
        if (sourceValues.empty()) {
            addEntry(result, PreservationAudit::Status::Normalized, "tags", key, detail);
        }
        else if (verifiedArchive) {
            addEntry(result, PreservationAudit::Status::SidecarOnly, "tags", key,
                detail + " Original FLAC metadata is available in the verified archive.");
        }
        else {
            addEntry(result, PreservationAudit::Status::MissingOrChanged, "tags", key, detail, true);
        }
    }
}

bool samePayload(const Picture& left, const Picture& right)
{
    return left.payload == right.payload;
}

bool sameDescriptor(const Picture& left, const Picture& right)
{
    return left.mime == right.mime && left.description == right.description &&
        left.type == right.type && left.width == right.width &&
        left.height == right.height && left.depth == right.depth &&
        left.colors == right.colors;
}

void comparePictures(PreservationAudit::Result& result, const std::vector<Picture>& sourcePictures,
    const std::vector<Picture>& outputPictures, bool verifiedArchive)
{
    std::vector<bool> used(outputPictures.size(), false);
    for (std::size_t sourceIndex = 0; sourceIndex < sourcePictures.size(); ++sourceIndex) {
        const auto& sourcePicture = sourcePictures[sourceIndex];
        if (!sourcePicture.supported) {
            bool matched = false;
            for (std::size_t outputIndex = 0; outputIndex < outputPictures.size(); ++outputIndex) {
                if (!used[outputIndex] && samePayload(sourcePicture, outputPictures[outputIndex])) {
                    used[outputIndex] = true;
                    matched = true;
                    break;
                }
            }
            addEntry(result, matched ? PreservationAudit::Status::Preserved :
                (verifiedArchive ? PreservationAudit::Status::SidecarOnly : PreservationAudit::Status::MissingOrChanged),
                "picture-payload", "unsupported picture[" + std::to_string(sourceIndex) + "]",
                matched ? "Exact unsupported payload remains in output." :
                (verifiedArchive ? "Unsupported payload retained in verified metadata archive." :
                    "Unsupported payload absent from output and no verified archive exists."),
                !matched && !verifiedArchive);
            continue;
        }
        std::size_t match = outputPictures.size();
        std::size_t payloadMatch = outputPictures.size();
        std::size_t descriptorMatch = outputPictures.size();
        for (std::size_t outputIndex = 0; outputIndex < outputPictures.size(); ++outputIndex) {
            if (!used[outputIndex] && samePayload(sourcePicture, outputPictures[outputIndex])) {
                if (payloadMatch == outputPictures.size()) {
                    payloadMatch = outputIndex;
                }
                if (descriptorMatch == outputPictures.size() &&
                    sourcePicture.type == outputPictures[outputIndex].type &&
                    sourcePicture.mime == outputPictures[outputIndex].mime &&
                    sourcePicture.description == outputPictures[outputIndex].description) {
                    descriptorMatch = outputIndex;
                }
                if (sameDescriptor(sourcePicture, outputPictures[outputIndex])) {
                    match = outputIndex;
                    break;
                }
            }
        }
        if (match == outputPictures.size()) {
            match = descriptorMatch != outputPictures.size() ? descriptorMatch : payloadMatch;
        }
        if (match == outputPictures.size()) {
            addEntry(result, PreservationAudit::Status::MissingOrChanged, "picture-payload",
                "picture[" + std::to_string(sourceIndex) + "]",
                "Supported image payload is absent or changed; exact payload preservation is required.",
                true);
            continue;
        }
        used[match] = true;
        const auto& outputPicture = outputPictures[match];
        addEntry(result, PreservationAudit::Status::Preserved, "picture-payload",
            "picture[" + std::to_string(sourceIndex) + "]", "Exact image payload bytes match.");
        if (!sourcePicture.descriptorsVerified || !outputPicture.descriptorsVerified) {
            addEntry(result, PreservationAudit::Status::NotVerified, "picture-descriptor",
                "picture[" + std::to_string(sourceIndex) + "]",
                "Picture type/MIME/description are not fully exposed for this container/ID3 encoding.");
            continue;
        }
        const bool nativeDimensions = sourcePicture.nativeDimensions && outputPicture.nativeDimensions;
        if (!nativeDimensions) {
            addEntry(result, PreservationAudit::Status::SourceOnly, "picture-dimensions",
                "picture[" + std::to_string(sourceIndex) + "]",
                "This container does not store FLAC picture dimensions/depth/palette fields; decoded image size is not an archived descriptor.");
        }
        if (sourcePicture.type != outputPicture.type ||
            sourcePicture.mime != outputPicture.mime ||
            sourcePicture.description != outputPicture.description ||
            (nativeDimensions && (sourcePicture.width != outputPicture.width ||
            sourcePicture.height != outputPicture.height ||
            sourcePicture.depth != outputPicture.depth ||
            sourcePicture.colors != outputPicture.colors))) {
            addEntry(result, PreservationAudit::Status::Normalized, "picture-descriptor",
                "picture[" + std::to_string(sourceIndex) + "]",
                "Image payload is byte-identical; differing fields:" +
                std::string(sourcePicture.type != outputPicture.type ? " type" : "") +
                (sourcePicture.mime != outputPicture.mime ? " MIME" : "") +
                (sourcePicture.description != outputPicture.description ? " description" : "") +
                (nativeDimensions && (sourcePicture.width != outputPicture.width || sourcePicture.height != outputPicture.height) ? " dimensions" : "") +
                (nativeDimensions && sourcePicture.depth != outputPicture.depth ? " depth" : "") +
                (nativeDimensions && sourcePicture.colors != outputPicture.colors ? " palette" : ""));
        }
        else {
            addEntry(result, PreservationAudit::Status::Preserved, "picture-descriptor",
                "picture[" + std::to_string(sourceIndex) + "]",
                "Exact image payload bytes and available descriptors match.");
        }
    }

    for (std::size_t outputIndex = 0; outputIndex < outputPictures.size(); ++outputIndex) {
        if (!used[outputIndex]) {
            addEntry(result, PreservationAudit::Status::Normalized, "picture-payload",
                "output-only picture[" + std::to_string(outputIndex) + "]",
                "Output contains an additional attached picture not matched to a supported source payload.");
        }
    }

}

bool sameOpaqueBlock(const RawBlock& left, const RawBlock& right)
{
    return left.type == right.type && left.data == right.data;
}

void compareOpaqueBlocks(PreservationAudit::Result& result, const NativeMetadata& source,
    const NativeMetadata* output, bool verifiedArchive)
{
    if (verifiedArchive) {
        addEntry(result, PreservationAudit::Status::Preserved, "metadata-archive",
            "original FLAC metadata",
            "The original FLAC metadata archive was verified before this audit; it retains source blocks "
            "that are not transferable to the transformed audio.");
    }
    if (source.streamInfoCount != 0) {
        addEntry(result, PreservationAudit::Status::SourceOnly, "flac-block",
            "STREAMINFO (" + std::to_string(source.streamInfoCount) + ")",
            verifiedArchive
                ? "Audio technical metadata is regenerated and not transferable; original bytes remain in the verified archive."
                : "Audio technical metadata is regenerated for the transformed stream and is not transferable.");
    }
    if (source.seekTableCount != 0) {
        addEntry(result, PreservationAudit::Status::SourceOnly, "flac-block",
            "SEEKTABLE (" + std::to_string(source.seekTableCount) + ")",
            verifiedArchive
                ? "Source seek offsets describe the original audio and are not transferable; original bytes remain in the verified archive."
                : "Source seek offsets describe the original audio and are not transferable.");
    }
    if (source.opaqueBlocks.empty()) {
        return;
    }

    std::vector<bool> used(output == nullptr ? 0 : output->opaqueBlocks.size(), false);
    std::map<std::uint8_t, std::size_t> totals;
    std::map<std::uint8_t, std::size_t> exact;
    for (const auto& block : source.opaqueBlocks) {
        ++totals[block.type];
        if (output == nullptr) {
            continue;
        }
        for (std::size_t index = 0; index < output->opaqueBlocks.size(); ++index) {
            if (!used[index] && sameOpaqueBlock(block, output->opaqueBlocks[index])) {
                used[index] = true;
                ++exact[block.type];
                break;
            }
        }
    }
    for (const auto& [type, count] : totals) {
        const auto found = exact.find(type);
        const auto matched = found == exact.end() ? 0 : found->second;
        const auto name = "FLAC block type " + std::to_string(type) +
            " (" + std::to_string(count) + ")";
        if (matched == count) {
            addEntry(result, PreservationAudit::Status::Preserved, "flac-block", name,
                "All non-transferable source block payloads are also present byte-for-byte in output.");
        }
        else if (verifiedArchive) {
            addEntry(result, PreservationAudit::Status::SidecarOnly, "flac-block", name,
                "Non-transferable block is retained in the verified original-metadata archive.");
        }
        else {
            addEntry(result, PreservationAudit::Status::MissingOrChanged, "flac-block", name,
                "Non-transferable block is absent or changed and no verified archive exists.", true);
        }
    }
}

void compareChapters(PreservationAudit::Result& result, const std::vector<Chapter>& source,
    const std::vector<Chapter>& output, bool verifiedArchive)
{
    if (source.empty() && output.empty()) {
        return;
    }
    if (source.empty()) {
        addEntry(result, PreservationAudit::Status::Normalized, "chapters", "output-only chapter set",
            "Output contains additional chapters not present in the source.");
        return;
    }
    constexpr double timeToleranceSeconds = 0.15;
    bool matches = source.size() == output.size();
    std::size_t mismatchIndex = 0;
    if (matches) {
        for (std::size_t index = 0; index < source.size(); ++index) {
            if (std::abs(source[index].start - output[index].start) > timeToleranceSeconds ||
                std::abs(source[index].end - output[index].end) > timeToleranceSeconds ||
                source[index].tags != output[index].tags) {
                matches = false;
                mismatchIndex = index;
                break;
            }
        }
    }
    if (matches) {
        addEntry(result, PreservationAudit::Status::Preserved, "chapters",
            std::to_string(source.size()) + " chapter(s)",
            "Chapter dictionaries and start/end times match within 0.15 seconds.");
        return;
    }

    std::string detail = "Chapter count, title/dictionary, or timing differs";
    if (mismatchIndex < source.size() && mismatchIndex < output.size()) {
        detail += " at chapter " + std::to_string(mismatchIndex) +
            " (start/end tolerance is 0.15 seconds).";
    }
    else {
        detail += " (start/end tolerance is 0.15 seconds).";
    }
    if (verifiedArchive) {
        addEntry(result, PreservationAudit::Status::SidecarOnly, "chapters", "chapter set",
            detail + " Original FLAC metadata is retained in the verified archive.");
    }
    else {
        addEntry(result, PreservationAudit::Status::MissingOrChanged, "chapters", "chapter set",
            detail + " No verified archive exists.", true);
    }
}
}

namespace PreservationAudit
{
const char* StatusName(Status status)
{
    switch (status) {
    case Status::Preserved: return "Preserved";
    case Status::Normalized: return "Normalized";
    case Status::SidecarOnly: return "SidecarOnly";
    case Status::MissingOrChanged: return "MissingOrChanged";
    case Status::NotVerified: return "NotVerified";
    case Status::SourceOnly: return "SourceOnly";
    default: return "NotVerified";
    }
}

Result Compare(const std::filesystem::path& source,
    const std::filesystem::path& temporaryOutput, bool verifiedArchive)
{
    Result result;
    Media sourceMedia;
    Media outputMedia;
    std::string error;
    try {
        if (!openMedia(source, sourceMedia, error)) {
            addFatalNotVerified(result, "media", "source", error);
            return result;
        }
        if (!openMedia(temporaryOutput, outputMedia, error)) {
            addFatalNotVerified(result, "media", "temporary output", error);
            return result;
        }
        if (outputMedia.audioCodec == AV_CODEC_ID_MP3)
            readMp3PictureDescriptors(temporaryOutput, outputMedia.pictures);

        NativeMetadata sourceNativeStorage;
        NativeMetadata outputNativeStorage;
        const NativeMetadata* sourceNative = nullptr;
        const NativeMetadata* outputNative = nullptr;
        if (sourceMedia.nativeFlac) {
            sourceNativeStorage = readNativeFlac(source);
            sourceNative = &sourceNativeStorage;
        }
        if (outputMedia.nativeFlac) {
            outputNativeStorage = readNativeFlac(temporaryOutput);
            outputNative = &outputNativeStorage;
        }
        if (sourceNative != nullptr) {
            sourceMedia.pictures = std::move(sourceNativeStorage.pictures);
            const auto observedPictures = readAttachedPictures(sourceMedia.context.get());
            for (auto& picture : sourceMedia.pictures) {
                for (const auto& observed : observedPictures) {
                    if (samePayload(picture, observed) && observed.supported) {
                        picture.supported = true;
                        break;
                    }
                }
            }
        }
        if (outputNative != nullptr) {
            outputMedia.pictures = std::move(outputNativeStorage.pictures);
        }

        const bool hasVerifiedFlacArchive = verifiedArchive && sourceNative != nullptr;
        if (sourceNative == nullptr) {
            addEntry(result, Status::NotVerified, "opaque-metadata", "source container",
                "Original non-native-FLAC opaque metadata is not exposed by the available FFmpeg dictionaries; "
                "only observed fields, chapters and attached pictures are compared.");
        }
        compareTags(result, sourceMedia, outputMedia, sourceNative, outputNative,
            outputMedia.audioCodec == AV_CODEC_ID_MP3, hasVerifiedFlacArchive);
        comparePictures(result, sourceMedia.pictures, outputMedia.pictures, hasVerifiedFlacArchive);
        compareChapters(result, sourceMedia.chapters, outputMedia.chapters, hasVerifiedFlacArchive);
        if (sourceNative != nullptr) {
            compareOpaqueBlocks(result, *sourceNative, outputNative, hasVerifiedFlacArchive);
        }
    }
    catch (const ParseError& exception) {
        addFatalNotVerified(result, "media-metadata", "parser/stream data", exception.what());
    }
    catch (const std::filesystem::filesystem_error& exception) {
        addFatalNotVerified(result, "media", "filesystem", exception.what());
    }
    return result;
}
}
