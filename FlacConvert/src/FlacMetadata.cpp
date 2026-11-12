#include "FlacMetadata.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <spdlog/spdlog.h>
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "CommonUtils.h"

namespace {
constexpr const char* kJsonSchema = "AudioFormatConverter.FlacMetadataSidecar";
constexpr int kJsonSchemaVersion = 1;
constexpr const char* kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encodeBase64(const std::string& bytes)
{
    std::string encoded;
    encoded.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t index = 0;
    for (; index + 2 < bytes.size(); index += 3) {
        const auto value = (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index])) << 16) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index + 1])) << 8) |
            static_cast<unsigned char>(bytes[index + 2]);
        for (int shift = 18; shift >= 0; shift -= 6) {
            encoded += kBase64Alphabet[(value >> shift) & 0x3f];
        }
    }
    const auto remaining = bytes.size() - index;
    if (remaining != 0) {
        std::uint32_t value = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index])) << 16;
        if (remaining == 2) {
            value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index + 1])) << 8;
        }
        encoded += kBase64Alphabet[(value >> 18) & 0x3f];
        encoded += kBase64Alphabet[(value >> 12) & 0x3f];
        encoded += remaining == 2 ? kBase64Alphabet[(value >> 6) & 0x3f] : '=';
        encoded += '=';
    }
    return encoded;
}

bool decodeBase64(const std::string& text, std::string& bytes)
{
    bytes.clear();
    if (text.size() % 4 != 0) {
        return false;
    }
    bytes.reserve(text.size() / 4 * 3);
    for (std::size_t index = 0; index < text.size(); index += 4) {
        std::uint32_t value = 0;
        int padding = 0;
        for (std::size_t offset = 0; offset < 4; ++offset) {
            const char character = text[index + offset];
            value <<= 6;
            if (character == '=') {
                // Padding is only valid in the last two positions of the final quantum.
                if (index + 4 != text.size() || offset < 2) {
                    return false;
                }
                ++padding;
                continue;
            }
            if (padding != 0) {
                return false;
            }
            const char* found = std::char_traits<char>::find(kBase64Alphabet, 64, character);
            if (found == nullptr) {
                return false;
            }
            value |= static_cast<std::uint32_t>(found - kBase64Alphabet);
        }
        bytes += static_cast<char>((value >> 16) & 0xff);
        if (padding < 2) {
            bytes += static_cast<char>((value >> 8) & 0xff);
        }
        if (padding < 1) {
            bytes += static_cast<char>(value & 0xff);
        }
    }
    return true;
}

// Writes the archive to output, or compares it with archive when archive is set.
bool Process(const std::filesystem::path& source, std::ostream* output, std::istream* archive,
    const std::filesystem::path& temporaryInput, bool& preparedInput)
{
    const bool verify = archive != nullptr;
    preparedInput = false;
    std::ifstream input(source, std::ios::binary);
    const auto fail = [&](const std::string& reason) {
        spdlog::error("Cannot preserve FLAC metadata for '{}': {}",
            CommonUtils::path_to_utf8(source), reason);
        return false;
    };
    if (!input || (verify ? !*archive : (output == nullptr || !*output))) {
        return fail("cannot open source or temporary metadata sidecar");
    }
    std::array<char, 8192> buffer{};
    const auto writeBytes = [&](const char* bytes, std::streamsize count) {
        if (!verify) {
            return static_cast<bool>(output->write(bytes, count));
        }
        std::array<char, 8192> actual{};
        if (count < 0 || count > static_cast<std::streamsize>(actual.size())) {
            return false;
        }
        return archive->read(actual.data(), count) &&
            std::equal(bytes, bytes + count, actual.data());
    };
    const auto copyBytes = [&](std::uint64_t remaining) {
        while (remaining != 0) {
            const auto count = static_cast<std::streamsize>((std::min)(
                remaining, static_cast<std::uint64_t>(buffer.size())));
            if (!input.read(buffer.data(), count) || !writeBytes(buffer.data(), count)) {
                return false;
            }
            remaining -= static_cast<std::uint64_t>(count);
        }
        return true;
    };
    std::array<char, 4> header{};
    if (!input.read(header.data(), header.size())) {
        return fail("missing FLAC marker");
    }
    if (std::string(header.data(), 3) == "ID3") {
        std::array<unsigned char, 10> id3{};
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(id3.data()), id3.size()) ||
            id3[3] < 2 || id3[3] > 4) {
            return fail("invalid leading ID3 tag");
        }
        std::uint64_t size = 0;
        for (std::size_t i = 6; i < 10; ++i) {
            if (id3[i] & 0x80) {
                return fail("invalid leading ID3 tag size");
            }
            size = (size << 7) | id3[i];
        }
        if (id3[3] == 4 && (id3[5] & 0x10)) {
            size += 10;
        }
        if (!writeBytes(reinterpret_cast<const char*>(id3.data()), id3.size())) {
            return fail("leading ID3 archive mismatch or write failure");
        }
        if (!copyBytes(size) || !input.read(header.data(), header.size())) {
            return fail("truncated leading ID3 tag");
        }
        if (!verify) {
            spdlog::debug("Archived original leading ID3 tag in the temporary metadata sidecar.");
        }
    }
    if (std::string(header.data(), header.size()) != "fLaC") {
        return fail("Original metadata preservation requires native FLAC, not Ogg FLAC or headerless audio");
    }
    if (!writeBytes(header.data(), header.size())) {
        return fail("FLAC marker archive mismatch or write failure");
    }
    bool last = false;
    bool first = true;
    while (!last) {
        if (!input.read(header.data(), header.size())) {
            return fail("truncated metadata block header");
        }
        const auto flags = static_cast<unsigned char>(header[0]);
        const int type = flags & 0x7f;
        last = (flags & 0x80) != 0;
        std::size_t remaining = (static_cast<unsigned char>(header[1]) << 16) |
            (static_cast<unsigned char>(header[2]) << 8) | static_cast<unsigned char>(header[3]);
        if ((first && (type != 0 || remaining != 34)) || (!first && type == 0) || type == 127) {
            return fail("invalid FLAC metadata block");
        }
        first = false;
        if (!writeBytes(header.data(), header.size())) {
            return fail("metadata header archive mismatch or write failure");
        }
        if (!copyBytes(remaining)) {
            return fail("metadata block read/write failed");
        }
        if (!verify && (type == 2 || type == 3 || type == 5 || type >= 7)) {
            spdlog::debug("Retained original FLAC block type {} in temporary sidecar; "
                "this source structure may not describe the converted audio.", type);
        }
    }
    const auto metadataEnd = input.tellg();
    input.seekg(0, std::ios::end);
    const auto fileEnd = input.tellg();
    if (metadataEnd < 0 || fileEnd < metadataEnd) {
        return fail("cannot determine metadata/file size");
    }
    auto trailerStart = fileEnd;
    if (fileEnd - metadataEnd >= 128) {
        input.seekg(fileEnd - std::streamoff(128));
        std::array<char, 3> tag{};
        if (!input.read(tag.data(), tag.size())) {
            return fail("cannot inspect trailing ID3v1 tag");
        }
        if (std::string(tag.data(), tag.size()) == "TAG") {
            trailerStart -= std::streamoff(128);
        }
    }
    if (trailerStart - metadataEnd >= 32) {
        input.seekg(trailerStart - std::streamoff(32));
        std::array<unsigned char, 32> footer{};
        if (!input.read(reinterpret_cast<char*>(footer.data()), footer.size())) {
            return fail("cannot inspect trailing APE tag");
        }
        if (std::string(reinterpret_cast<const char*>(footer.data()), 8) == "APETAGEX") {
            const auto littleEndian = [&](std::size_t offset) {
                std::uint64_t value = 0;
                for (std::size_t i = 0; i < 4; ++i) {
                    value |= static_cast<std::uint64_t>(footer[offset + i]) << (8 * i);
                }
                return value;
            };
            auto size = littleEndian(12);
            if (littleEndian(20) & 0x80000000) {
                size += 32;
            }
            if (size < 32 || size > static_cast<std::uint64_t>(trailerStart - metadataEnd)) {
                return fail("invalid trailing APE tag size");
            }
            trailerStart -= static_cast<std::streamoff>(size);
        }
    }
    if (trailerStart < fileEnd) {
        input.seekg(trailerStart);
        if (!copyBytes(static_cast<std::uint64_t>(fileEnd - trailerStart))) {
            return fail("cannot preserve trailing ID3v1/APE metadata");
        }
        if (!verify) {
            spdlog::debug("Archived original trailing ID3v1/APE metadata in the temporary sidecar.");
        }
    }
    if (verify) {
        if (archive->peek() != std::char_traits<char>::eof() || archive->bad()) {
            return fail("archive has additional bytes or cannot be read");
        }
        return true;
    }
    output->flush();
    if (!*output) {
        return fail("metadata sidecar write failed");
    }
    if (trailerStart < fileEnd) {
        // Raw FLAC demuxing treats appended tags as damaged audio under -xerror.
        // Decode a temporary byte-identical copy without those already-backed-up tags.
        std::ofstream cleanInput(temporaryInput, std::ios::binary | std::ios::trunc);
        input.seekg(0);
        auto remaining = static_cast<std::uint64_t>(trailerStart);
        while (remaining != 0) {
            const auto count = static_cast<std::streamsize>((std::min)(
                remaining, static_cast<std::uint64_t>(buffer.size())));
            if (!input.read(buffer.data(), count) || !cleanInput.write(buffer.data(), count)) {
                return fail("cannot prepare temporary input without appended tags");
            }
            remaining -= static_cast<std::uint64_t>(count);
        }
        cleanInput.close();
        if (!cleanInput) {
            return fail("temporary input write failed");
        }
        preparedInput = true;
    }
    spdlog::debug("Created temporary original-metadata archive; verification and installation are pending.");
    return true;
}

bool readJsonArchive(const std::filesystem::path& sidecar, std::string& bytes)
{
    std::ifstream file(sidecar, std::ios::binary);
    const std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    const auto fail = [&](const char* reason) {
        spdlog::error("Invalid JSON metadata sidecar '{}': {}", CommonUtils::path_to_utf8(sidecar), reason);
        return false;
    };
    if (!file && !file.eof()) {
        return fail("cannot read file");
    }
    rapidjson::Document document;
    document.Parse(text.c_str(), text.size());
    if (document.HasParseError() || !document.IsObject()) {
        return fail("not a JSON object");
    }
    const auto schema = document.FindMember("schema");
    const auto version = document.FindMember("schemaVersion");
    const auto archive = document.FindMember("archive");
    if (schema == document.MemberEnd() || !schema->value.IsString() ||
        std::string(schema->value.GetString()) != kJsonSchema ||
        version == document.MemberEnd() || !version->value.IsInt() ||
        version->value.GetInt() != kJsonSchemaVersion ||
        archive == document.MemberEnd() || !archive->value.IsObject()) {
        return fail("unsupported schema");
    }
    const auto encoding = archive->value.FindMember("encoding");
    const auto size = archive->value.FindMember("size");
    const auto data = archive->value.FindMember("data");
    if (encoding == archive->value.MemberEnd() || !encoding->value.IsString() ||
        std::string(encoding->value.GetString()) != "base64" ||
        size == archive->value.MemberEnd() || !size->value.IsUint64() ||
        data == archive->value.MemberEnd() || !data->value.IsString()) {
        return fail("missing archive encoding, size or data");
    }
    if (!decodeBase64(std::string(data->value.GetString(), data->value.GetStringLength()), bytes) ||
        bytes.size() != size->value.GetUint64()) {
        return fail("archive data is not valid Base64 of the recorded size");
    }
    return true;
}
}

bool FlacMetadata::AppliesTo(const std::filesystem::path& source, const ConversionOptions& options)
{
    if (options.target == AudioTarget::Mp3) {
        return true;
    }
    auto extension = source.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension == ".flac";
}

std::filesystem::path FlacMetadata::SidecarPath(std::filesystem::path audio, MetadataSidecarFormat format)
{
    switch (format) {
    case MetadataSidecarFormat::Binary:
        audio += ".flac-metadata";
        return audio;
    case MetadataSidecarFormat::Json:
        audio += ".flac-metadata.json";
        return audio;
    default:
        return {};
    }
}

bool FlacMetadata::Save(const std::filesystem::path& source, const std::filesystem::path& sidecar,
    MetadataSidecarFormat format, const std::string& readableMetadataJson,
    const std::filesystem::path& temporaryInput, bool& preparedInput)
{
    if (format == MetadataSidecarFormat::Binary) {
        std::ofstream output(sidecar, std::ios::binary | std::ios::trunc);
        if (!Process(source, &output, nullptr, temporaryInput, preparedInput)) {
            return false;
        }
        output.close();
        if (!output) {
            spdlog::error("Metadata sidecar write failed: {}", CommonUtils::path_to_utf8(sidecar));
            return false;
        }
        return true;
    }
    // None still parses the source so appended tags can be removed from the decoder input.
    std::ostringstream bytes(std::ios::binary);
    if (!Process(source, &bytes, nullptr, temporaryInput, preparedInput)) {
        return false;
    }
    if (format == MetadataSidecarFormat::None) {
        return true;
    }

    rapidjson::Document readable;
    if (!readableMetadataJson.empty()) {
        readable.Parse(readableMetadataJson.c_str(), readableMetadataJson.size());
        if (readable.HasParseError() || !readable.IsObject()) {
            spdlog::error("Cannot write JSON metadata sidecar for '{}': readable metadata is not a JSON object.",
                CommonUtils::path_to_utf8(source));
            return false;
        }
    }
    const auto archiveBytes = bytes.str();
    const auto encoded = encodeBase64(archiveBytes);
    const auto sourceName = CommonUtils::path_to_utf8(source.filename());
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("schema"); writer.String(kJsonSchema);
    writer.Key("schemaVersion"); writer.Int(kJsonSchemaVersion);
    writer.Key("sourceFileName");
    writer.String(sourceName.c_str(), static_cast<rapidjson::SizeType>(sourceName.size()));
    writer.Key("note");
    writer.String("'metadata' is a readable view of the source and is informational; "
        "'archive' holds the exact original FLAC metadata bytes and is what verification checks.");
    writer.Key("metadata");
    if (readableMetadataJson.empty()) {
        writer.Null();
    }
    else {
        readable.Accept(writer);
    }
    writer.Key("archive");
    writer.StartObject();
    writer.Key("encoding"); writer.String("base64");
    writer.Key("size"); writer.Uint64(archiveBytes.size());
    writer.Key("data"); writer.String(encoded.c_str(), static_cast<rapidjson::SizeType>(encoded.size()));
    writer.EndObject();
    writer.EndObject();

    std::ofstream output(sidecar, std::ios::binary | std::ios::trunc);
    output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
    output.close();
    if (!output) {
        spdlog::error("Metadata sidecar write failed: {}", CommonUtils::path_to_utf8(sidecar));
        return false;
    }
    return true;
}

bool FlacMetadata::Verify(const std::filesystem::path& source, const std::filesystem::path& sidecar,
    MetadataSidecarFormat format)
{
    bool unused = false;
    if (format == MetadataSidecarFormat::Binary) {
        std::ifstream archive(sidecar, std::ios::binary);
        return Process(source, nullptr, &archive, {}, unused);
    }
    if (format != MetadataSidecarFormat::Json) {
        return false;
    }
    std::string bytes;
    if (!readJsonArchive(sidecar, bytes)) {
        return false;
    }
    std::istringstream archive(bytes, std::ios::binary);
    return Process(source, nullptr, &archive, {}, unused);
}
