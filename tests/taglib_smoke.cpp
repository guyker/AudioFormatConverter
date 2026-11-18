#include <iostream>
#include <filesystem>
#include <ranges>
#include <stdexcept>
#include <string>

#include <taglib/fileref.h>
#include <taglib/flac/flacfile.h>
#include <taglib/ogg/xiphcomment.h>
#include "TagLibReader.h"

static void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main(int argc, char** argv) {
    try {
        require(argc == 3, "Expected generated FLAC and MP3 fixtures");
        const auto missing = std::filesystem::path(argv[1]).parent_path() / "missing-fixture.flac";
        const auto missingResult = TagLibReader::Read(missing, "missing-fixture.flac");
        require(!missingResult.record && missingResult.diagnostics.size() == 1 &&
            missingResult.diagnostics.front().nativeErrorCode.has_value() &&
            missingResult.diagnostics.front().nativeErrorCategory.has_value(),
            "Filesystem diagnostic did not retain the native error");
        {
            TagLib::FLAC::File file(argv[1]);
            require(file.isValid(), "TagLib could not open the FLAC fixture for tag setup");
            auto* comments = file.xiphComment(true);
            comments->addField("ARTIST", "First artist", false);
            comments->addField("ARTIST", "Second artist", false);
            require(file.save(), "Could not save repeated FLAC artist tags");
        }
        for (int index = 1; index < argc; ++index) {
            {
                TagLib::FileRef file(argv[index]);
                require(!file.isNull(), "TagLib could not open the fixture");
                require(file.tag() != nullptr, "TagLib did not provide a tag");
                require(file.tag()->title().to8Bit(true) == "TagLib smoke",
                    "TagLib did not read the generated title tag");
                require(file.audioProperties() != nullptr, "TagLib did not provide audio properties");
                require(file.audioProperties()->sampleRate() == 48000,
                    "TagLib did not read the generated sample rate");
            }

            const auto path = std::filesystem::path(argv[index]);
            const auto sizeBefore = std::filesystem::file_size(path);
            const auto modifiedBefore = std::filesystem::last_write_time(path);
            const auto result = TagLibReader::Read(path, "fixture");
            require(result.record.has_value(), "Catalog reader failed to read a TagLib fixture");
            require(std::filesystem::file_size(path) == sizeBefore &&
                std::filesystem::last_write_time(path) == modifiedBefore,
                "Catalog reader modified an input file");
            require(result.record->sampleRateHz == 48000,
                "Catalog reader did not preserve the sample rate");
            require(result.record->tags.contains("title") &&
                result.record->tags.at("title").front() == "TagLib smoke",
                "Catalog reader did not normalize the title tag");
            require(!result.record->rawTags.empty(),
                "Catalog reader did not retain the source tag representation");
            require(std::ranges::any_of(result.record->rawTags, [](const RawMetadataTag& tag) {
                return tag.scope == "ffmpeg.format" &&
                    (tag.key == "title" || tag.key == "TITLE") &&
                    tag.value == "TagLib smoke";
            }), "Catalog reader did not retain FFmpeg's container title metadata");
            if (index == 1) {
                require(result.record->tags.contains("artists") &&
                    result.record->tags.at("artists").size() == 2,
                    "Catalog reader did not preserve repeated FLAC tag values");
                require(result.record->pictures.size() == 1,
                    "Catalog reader did not retain FLAC picture metadata");
            }
            else {
                require(std::ranges::any_of(result.record->rawTags, [](const RawMetadataTag& tag) {
                    return tag.frame == "TIT2";
                }), "Catalog reader did not retain the MP3 ID3 frame identity");
                require(result.record->pictures.size() == 1,
                    "Catalog reader did not retain MP3 attached-picture metadata");
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
