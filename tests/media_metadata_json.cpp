#include "MediaTrack.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main() {
    try {
        constexpr auto input = R"({
            "format": {
                "nb_stream_groups": 3,
                "nb_chapters": 2,
                "packet_size": "4096"
            },
            "streams": [{
                "index": 0,
                "codec_type": "audio",
                "sample_rate": "44100",
                "start_time": 0.25,
                "time_base": "1/44100"
            }],
            "chapters": [{
                "id": 1,
                "time_base": "1/1000",
                "start": 0,
                "start_time": 0.0,
                "end": 1250,
                "end_time": 1.25,
                "tags": {"title": "Chapter"}
            }]
        })";

        rapidjson::Document inputDocument;
        inputDocument.Parse(input);
        require(!inputDocument.HasParseError(), "Test JSON did not parse");

        const auto metadata = MediaTrack::ParseFFprobeInformation(inputDocument);
        require(metadata.format.nb_stream_groups == 3, "Unsigned stream group count was lost");
        require(metadata.format.nb_chapters == 2, "Unsigned chapter count was lost");
        require(metadata.format.packet_size == 4096, "String-valued unsigned integer was lost");
        require(metadata.streams.size() == 1 && metadata.streams[0].start_time == 0.25,
            "Numeric stream start time was not parsed");
        require(metadata.chapters.size() == 1 && metadata.chapters[0].start_time == "0.000000" &&
            metadata.chapters[0].end_time == "1.250000", "Numeric chapter times were not preserved");

        const auto serialized = MediaTrack::toJsonString(metadata);
        rapidjson::Document outputDocument;
        outputDocument.Parse(serialized.c_str());
        require(!outputDocument.HasParseError(), "Serialized metadata was not valid JSON");
        const auto roundTripped = MediaTrack::ParseFFprobeInformation(outputDocument);
        require(roundTripped.format.nb_stream_groups == 3 && roundTripped.format.nb_chapters == 2,
            "Unsigned format fields did not round-trip");
        require(roundTripped.streams.size() == 1 && roundTripped.streams[0].time_base == "1/44100",
            "Stream rational fields did not round-trip");
        require(roundTripped.chapters.size() == 1 && roundTripped.chapters[0].tags->at("title") == "Chapter",
            "Chapter tags did not round-trip");

        std::string controlCharacters;
        for (int value = 0; value <= 0x1f; ++value) {
            controlCharacters.push_back(static_cast<char>(value));
        }
        FFprobeOutput controlMetadata;
        controlMetadata.format.tags = JsonUtils::Tags{{controlCharacters, controlCharacters}};
        controlMetadata.streams.emplace_back();
        const auto controlJson = MediaTrack::toJsonString(controlMetadata);
        rapidjson::Document controlDocument;
        controlDocument.Parse(controlJson.data(), controlJson.size());
        require(!controlDocument.HasParseError(), "Control characters made serialized metadata invalid");
        const auto controlRoundTrip = MediaTrack::ParseFFprobeInformation(controlDocument);
        require(controlRoundTrip.format.tags &&
            controlRoundTrip.format.tags->at(controlCharacters) == controlCharacters,
            "Control characters in metadata tag keys or values did not round-trip");
        require(!controlDocument["format"].HasMember("nb_streams") &&
            !controlDocument["streams"][0].HasMember("index"),
            "Unavailable metadata values were serialized as fabricated defaults");
        require(controlDocument.HasMember("audio_analysis") &&
            controlDocument["audio_analysis"]["status"] == "not_run",
            "Audio analysis status was not explicitly marked as not run");

        std::string settingsError;
        auto settings = AppSettingsJson::LoadForCommandLine(std::nullopt, settingsError);
        require(settings != nullptr, "Could not initialize test settings");
        require(settings->toJsonString().find("UseFFmpegLibraryAPI") == std::string::npos,
            "Deprecated UseFFmpegLibraryAPI setting was serialized");
        const auto missingFile = std::filesystem::temp_directory_path() /
            ("afc-metadata-error-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".flac");
        const auto failedRead = MediaTrack::ReadMetadataInfoFromFile(missingFile);
        require(failedRead.error.has_value(), "Metadata read error was discarded");
        require(failedRead.error->find("Failed to open input") != std::string::npos,
            "Metadata read did not preserve the FFmpeg failure reason");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
