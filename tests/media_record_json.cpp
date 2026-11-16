#include "MediaRecordJson.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include <rapidjson/document.h>
#include <rapidjson/schema.h>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

}

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "Expected catalog JSON Schema path");
        MediaRecord record;
        record.relativePath = "01 - Track.flac";
        record.sizeBytes = 1234;
        record.format = "flac";
        record.sampleRateHz = 48000;
        record.channels = 2;
        record.tags["artists"] = { "First", "Second" };
        record.chapters.push_back({
            1, "1/1000", 0, 0.0, 1250, 1.25, { { "title", { "Opening" } } }
        });
        record.scanTier = "analysis";
        record.integrity.status = "ok";
        record.integrity.decodedSamples = 96000;
        record.integrity.fileSha256 = std::string(64, 'a');
        record.integrity.pcmSha256 = std::string(64, 'b');
        record.analysisStatus = "partial";
        record.signal.measurements["channel0.rmsDbfs"] = -20.0;
        record.signal.usedBits = 16;
        record.signal.usedBitsStatus = "ok";
        record.findings.push_back({
            "ZERO_PADDED_HIRES", "warning", "high", "Observed 16 used bits in a 24-bit file."
        });
        record.chapterSupport = "supported";
        record.rawTags.push_back({
            "vorbis", "ARTIST", "First\u0001", 0,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt
        });

        const auto json = SerializeMediaRecord(record);
        rapidjson::Document document;
        document.Parse(json.data(), json.size());
        require(!document.HasParseError(), "Serialized media record is not valid JSON");
        require(!document["file"].HasMember("modifiedUtc"),
            "Unavailable modification time should be omitted");

        std::ifstream schemaFile(argv[1], std::ios::binary);
        require(schemaFile.is_open(), "Could not open catalog JSON Schema");
        std::ostringstream schemaText;
        schemaText << schemaFile.rdbuf();
        rapidjson::Document schemaDocument;
        schemaDocument.Parse(schemaText.str().c_str());
        require(!schemaDocument.HasParseError(), "Catalog JSON Schema is not valid JSON");

        const std::string catalogJson =
            "[{\"Album Path\":\"test\",\"Library Root\":\"test\",\"Folder\":\"Album\",\"Tracks\":[" +
            json + "]}]";
        rapidjson::Document catalogDocument;
        catalogDocument.Parse(catalogJson.data(), catalogJson.size());
        require(!catalogDocument.HasParseError(), "Catalog fixture is not valid JSON");
        rapidjson::SchemaDocument schema(schemaDocument);
        rapidjson::SchemaValidator validator(schema);
        require(catalogDocument.Accept(validator),
            "Serialized catalog record does not conform to the v2 JSON Schema");

        std::string error;
        const auto parsed = ParseMediaRecord(document, &error);
        require(parsed.has_value(), error.c_str());
        require(parsed->tags.at("artists").size() == 2,
            "Repeated normalized tag values were not preserved");
        require(parsed->rawTags.front().value == "First\u0001",
            "Raw tag control characters did not round-trip");
        require(parsed->chapters.size() == 1 &&
            parsed->chapters.front().tags.at("title").front() == "Opening",
            "Chapter timing or tags did not round-trip");
        require(parsed->scanTier == "analysis" &&
            parsed->integrity.decodedSamples == 96000 &&
            parsed->signal.measurements.at("channel0.rmsDbfs") == -20.0 &&
            parsed->findings.size() == 1,
            "Integrity and signal analysis fields did not round-trip");

        document["schemaVersion"].SetInt(3);
        require(!ParseMediaRecord(document, &error),
            "Unsupported catalog schema version should be rejected");

        document["schemaVersion"].SetInt(2);
        document["technical"]["sampleRateHz"].SetString("48000", document.GetAllocator());
        require(!ParseMediaRecord(document, &error),
            "Invalid optional technical types should be rejected, not silently omitted");
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
