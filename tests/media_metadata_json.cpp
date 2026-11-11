#include "MediaTrack.h"

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
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
