#include "FlacMetadata.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <spdlog/spdlog.h>
#include "CommonUtils.h"

bool FlacMetadata::Save(const std::filesystem::path& source, const std::filesystem::path& sidecar,
    const std::filesystem::path& temporaryInput, bool& preparedInput)
{
    preparedInput = false;
    std::ifstream input(source, std::ios::binary);
    std::ofstream output(sidecar, std::ios::binary | std::ios::trunc);
    const auto fail = [&](const std::string& reason) {
        spdlog::error("Cannot preserve FLAC metadata for '{}': {}",
            CommonUtils::path_to_utf8(source), reason);
        return false;
    };
    if (!input || !output) {
        return fail("cannot open source or temporary metadata sidecar");
    }
    std::array<char, 8192> buffer{};
    const auto copyBytes = [&](std::uint64_t remaining) {
        while (remaining != 0) {
            const auto count = static_cast<std::streamsize>((std::min)(
                remaining, static_cast<std::uint64_t>(buffer.size())));
            if (!input.read(buffer.data(), count) || !output.write(buffer.data(), count)) {
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
        output.write(reinterpret_cast<const char*>(id3.data()), id3.size());
        if (!copyBytes(size) || !input.read(header.data(), header.size())) {
            return fail("truncated leading ID3 tag");
        }
        spdlog::info("Preserved original leading ID3 tag in the metadata sidecar.");
    }
    if (std::string(header.data(), header.size()) != "fLaC") {
        return fail("MP3 conversion requires native FLAC, not Ogg FLAC or headerless audio");
    }
    output.write(header.data(), header.size());
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
        output.write(header.data(), header.size());
        if (!copyBytes(remaining)) {
            return fail("metadata block read/write failed");
        }
        if (type == 2 || type == 3 || type == 5 || type >= 7) {
            spdlog::info("Retained original FLAC block type {} in sidecar; "
                "this structure is not directly transferable to MP3.", type);
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
        spdlog::info("Preserved original trailing ID3v1/APE metadata in the sidecar.");
    }
    output.close();
    if (!output) {
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
    spdlog::info("Preserved all original FLAC metadata blocks in a sidecar (including repeated tags, "
        "pictures, application data, cue sheets and source technical metadata). "
        "MP3 tags may be normalized; FLAC seek tables and source checksums do not describe the MP3 audio.");
    return true;
}
