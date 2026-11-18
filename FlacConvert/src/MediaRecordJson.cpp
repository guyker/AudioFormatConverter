#include "MediaRecordJson.h"

#include <cmath>
#include <stdexcept>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace {

void writeString(rapidjson::Writer<rapidjson::StringBuffer>& writer,
    const char* key, const std::string& value)
{
    writer.Key(key);
    writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
}

template<typename T, typename Write>
void writeOptional(rapidjson::Writer<rapidjson::StringBuffer>& writer,
    const char* key, const std::optional<T>& value, Write writeValue)
{
    if (value) {
        writer.Key(key);
        writeValue(*value);
    }
}

std::optional<std::string> stringMember(const rapidjson::Value& object, const char* key)
{
    const auto member = object.FindMember(key);
    if (member == object.MemberEnd() || !member->value.IsString()) return std::nullopt;
    return std::string(member->value.GetString(), member->value.GetStringLength());
}

template<typename T, typename Read>
std::optional<T> optionalMember(const rapidjson::Value& object, const char* key, Read read)
{
    const auto member = object.FindMember(key);
    if (member == object.MemberEnd()) return std::nullopt;
    T value{};
    if (!read(member->value, value)) return std::nullopt;
    return value;
}

bool readRequiredString(const rapidjson::Value& object, const char* key, std::string& output)
{
    const auto value = stringMember(object, key);
    if (!value) return false;
    output = *value;
    return true;
}

template<typename Predicate>
bool optionalMemberHasType(const rapidjson::Value& object, const char* key, Predicate predicate)
{
    const auto member = object.FindMember(key);
    return member == object.MemberEnd() || predicate(member->value);
}

bool fail(std::string* error, const char* message)
{
    if (error != nullptr) *error = message;
    return false;
}

}

std::string SerializeMediaRecord(const MediaRecord& record)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("schemaVersion");
    writer.Int(record.schemaVersion);
    writer.Key("metadataInventoryVersion");
    writer.Int(record.metadataInventoryVersion);
    writer.Key("file");
    writer.StartObject();
    writeString(writer, "relativePath", record.relativePath);
    writer.Key("sizeBytes");
    writer.Uint64(record.sizeBytes);
    writeOptional(writer, "modifiedUtc", record.modifiedUtc,
        [&writer](const std::string& value) {
            writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
        });
    writeString(writer, "format", record.format);
    writer.EndObject();

    writer.Key("technical");
    writer.StartObject();
    writeOptional(writer, "durationSeconds", record.durationSeconds,
        [&writer](double value) { writer.Double(value); });
    writeOptional(writer, "bitRateKbps", record.bitRateKbps,
        [&writer](int value) { writer.Int(value); });
    writeOptional(writer, "sampleRateHz", record.sampleRateHz,
        [&writer](int value) { writer.Int(value); });
    writeOptional(writer, "channels", record.channels,
        [&writer](int value) { writer.Int(value); });
    writeOptional(writer, "declaredBitsPerSample", record.declaredBitsPerSample,
        [&writer](int value) { writer.Int(value); });
    writeOptional(writer, "totalSamples", record.totalSamples,
        [&writer](std::uint64_t value) { writer.Uint64(value); });
    writeOptional(writer, "flacMd5", record.flacMd5,
        [&writer](const std::string& value) {
            writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
        });
    writeOptional(writer, "tagVendor", record.tagVendor,
        [&writer](const std::string& value) {
            writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
        });
    writer.EndObject();

    writer.Key("tags");
    writer.StartObject();
    for (const auto& [key, values] : record.tags) {
        writer.Key(key.data(), static_cast<rapidjson::SizeType>(key.size()));
        writer.StartArray();
        for (const auto& value : values) {
            writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
        }
        writer.EndArray();
    }
    writer.EndObject();

    writer.Key("rawTags");
    writer.StartArray();
    for (const auto& tag : record.rawTags) {
        writer.StartObject();
        writeString(writer, "scope", tag.scope);
        writeString(writer, "key", tag.key);
        writeString(writer, "value", tag.value);
        writer.Key("ordinal");
        writer.Uint(tag.ordinal);
        writeOptional(writer, "frame", tag.frame,
            [&writer](const std::string& value) {
                writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
            });
        writeOptional(writer, "description", tag.description,
            [&writer](const std::string& value) {
                writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
            });
        writeOptional(writer, "language", tag.language,
            [&writer](const std::string& value) {
                writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
            });
        writeOptional(writer, "encoding", tag.encoding,
            [&writer](int value) { writer.Int(value); });
        writer.EndObject();
    }
    writer.EndArray();

    writer.Key("pictures");
    writer.StartArray();
    for (const auto& picture : record.pictures) {
        writer.StartObject();
        writeOptional(writer, "type", picture.type,
            [&writer](int value) { writer.Int(value); });
        writeOptional(writer, "mime", picture.mime,
            [&writer](const std::string& value) {
                writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
            });
        writeOptional(writer, "description", picture.description,
            [&writer](const std::string& value) {
                writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
            });
        writeOptional(writer, "width", picture.width,
            [&writer](std::uint32_t value) { writer.Uint(value); });
        writeOptional(writer, "height", picture.height,
            [&writer](std::uint32_t value) { writer.Uint(value); });
        writer.Key("sizeBytes");
        writer.Uint64(picture.sizeBytes);
        writer.EndObject();
    }
    writer.EndArray();

    writer.Key("chapters");
    writer.StartArray();
    for (const auto& chapter : record.chapters) {
        writer.StartObject();
        writeOptional(writer, "id", chapter.id,
            [&writer](std::int64_t value) { writer.Int64(value); });
        writeOptional(writer, "timeBase", chapter.timeBase,
            [&writer](const std::string& value) {
                writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
            });
        writeOptional(writer, "start", chapter.start,
            [&writer](std::int64_t value) { writer.Int64(value); });
        writeOptional(writer, "startSeconds", chapter.startSeconds,
            [&writer](double value) { writer.Double(value); });
        writeOptional(writer, "end", chapter.end,
            [&writer](std::int64_t value) { writer.Int64(value); });
        writeOptional(writer, "endSeconds", chapter.endSeconds,
            [&writer](double value) { writer.Double(value); });
        writer.Key("tags");
        writer.StartObject();
        for (const auto& [key, values] : chapter.tags) {
            writer.Key(key.data(), static_cast<rapidjson::SizeType>(key.size()));
            writer.StartArray();
            for (const auto& value : values) {
                writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
            }
            writer.EndArray();
        }
        writer.EndObject();
        writer.EndObject();
    }
    writer.EndArray();

    writer.Key("integrity");
    writer.StartObject();
    writeString(writer, "status", record.integrity.status);
    writeOptional(writer, "decodedSamples", record.integrity.decodedSamples,
        [&writer](std::uint64_t value) { writer.Uint64(value); });
    writer.Key("decodeErrors");
    writer.Uint64(record.integrity.decodeErrors);
    writeOptional(writer, "firstErrorPosition", record.integrity.firstErrorPosition,
        [&writer](std::int64_t value) { writer.Int64(value); });
    writeOptional(writer, "flacMd5Matches", record.integrity.flacMd5Matches,
        [&writer](bool value) { writer.Bool(value); });
    writeOptional(writer, "fileSha256", record.integrity.fileSha256,
        [&writer](const std::string& value) {
            writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
        });
    writeOptional(writer, "pcmSha256", record.integrity.pcmSha256,
        [&writer](const std::string& value) {
            writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
        });
    writeOptional(writer, "error", record.integrity.error,
        [&writer](const std::string& value) {
            writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
        });
    writer.EndObject();

    writer.Key("findings");
    writer.StartArray();
    for (const auto& finding : record.findings) {
        writer.StartObject();
        writeString(writer, "code", finding.code);
        writeString(writer, "severity", finding.severity);
        writeString(writer, "confidence", finding.confidence);
        writeString(writer, "evidence", finding.evidence);
        writer.EndObject();
    }
    writer.EndArray();

    if (!record.diagnostics.empty()) {
        writer.Key("diagnostics");
        writer.StartArray();
        for (const auto& diagnostic : record.diagnostics) {
            writer.StartObject();
            writeString(writer, "stage", diagnostic.stage);
            writeString(writer, "source", diagnostic.source);
            writeString(writer, "code", diagnostic.code);
            writeString(writer, "severity", DiagnosticSeverityName(diagnostic.severity));
            writeString(writer, "message", diagnostic.message);
            writer.EndObject();
        }
        writer.EndArray();
    }

    writer.Key("support");
    writer.StartObject();
    writeString(writer, "tags", record.tagSupport);
    writeString(writer, "nativeInventory", record.nativeInventorySupport);
    writeString(writer, "decode", record.decodeSupport);
    writeString(writer, "chapters", record.chapterSupport);
    writer.EndObject();

    writer.Key("audioAnalysis");
    writer.StartObject();
    writeString(writer, "status", record.analysisStatus);
    writeString(writer, "tier", record.scanTier);
    writer.Key("measurements");
    writer.StartObject();
    for (const auto& [key, measurement] : record.signal.measurements) {
        writer.Key(key.data(), static_cast<rapidjson::SizeType>(key.size()));
        writer.Double(measurement);
    }
    writer.EndObject();
    writeOptional(writer, "method", record.signal.method,
        [&writer](const std::string& value) {
            writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
        });
    writeOptional(writer, "clippedSamples", record.signal.clippedSamples,
        [&writer](std::uint64_t value) { writer.Uint64(value); });
    writeOptional(writer, "usedBits", record.signal.usedBits,
        [&writer](int value) { writer.Int(value); });
    writeOptional(writer, "usedBitsStatus", record.signal.usedBitsStatus,
        [&writer](const std::string& value) {
            writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
        });
    writer.EndObject();
    if (record.error) writeString(writer, "error", *record.error);
    writer.EndObject();
    if (!writer.IsComplete()) {
        throw std::runtime_error("Failed to serialize media catalog record.");
    }
    return std::string(buffer.GetString(), buffer.GetSize());
}

std::optional<MediaRecord> ParseMediaRecord(const rapidjson::Value& value, std::string* error)
{
    if (!value.IsObject()) {
        fail(error, "Media record must be an object.");
        return std::nullopt;
    }
    MediaRecord record;
    const auto schemaVersion = optionalMember<int>(value, "schemaVersion",
        [](const rapidjson::Value& member, int& output) {
            if (!member.IsInt()) return false;
            output = member.GetInt();
            return true;
        });
    if (!schemaVersion || *schemaVersion != 2) {
        fail(error, "Unsupported media record schemaVersion.");
        return std::nullopt;
    }
    record.schemaVersion = *schemaVersion;
    const auto inventoryVersion = value.FindMember("metadataInventoryVersion");
    if (inventoryVersion != value.MemberEnd()) {
        if (!inventoryVersion->value.IsInt() || inventoryVersion->value.GetInt() < 0) {
            fail(error, "Media record metadataInventoryVersion must be a non-negative integer.");
            return std::nullopt;
        }
        record.metadataInventoryVersion = inventoryVersion->value.GetInt();
    }
    const auto file = value.FindMember("file");
    const auto technical = value.FindMember("technical");
    const auto tags = value.FindMember("tags");
    const auto rawTags = value.FindMember("rawTags");
    const auto pictures = value.FindMember("pictures");
    const auto chapters = value.FindMember("chapters");
    const auto integrity = value.FindMember("integrity");
    const auto findings = value.FindMember("findings");
    const auto diagnostics = value.FindMember("diagnostics");
    const auto support = value.FindMember("support");
    const auto analysis = value.FindMember("audioAnalysis");
    if (file == value.MemberEnd() || !file->value.IsObject() ||
        technical == value.MemberEnd() || !technical->value.IsObject() ||
        tags == value.MemberEnd() || !tags->value.IsObject() ||
        rawTags == value.MemberEnd() || !rawTags->value.IsArray() ||
        pictures == value.MemberEnd() || !pictures->value.IsArray() ||
        chapters == value.MemberEnd() || !chapters->value.IsArray() ||
        support == value.MemberEnd() || !support->value.IsObject() ||
        analysis == value.MemberEnd() || !analysis->value.IsObject()) {
        fail(error, "Media record is missing a required object or array.");
        return std::nullopt;
    }
    if (integrity != value.MemberEnd() && !integrity->value.IsObject()) {
        fail(error, "Media record integrity must be an object.");
        return std::nullopt;
    }
    if (findings != value.MemberEnd() && !findings->value.IsArray()) {
        fail(error, "Media record findings must be an array.");
        return std::nullopt;
    }
    if (diagnostics != value.MemberEnd() && !diagnostics->value.IsArray()) {
        fail(error, "Media record diagnostics must be an array.");
        return std::nullopt;
    }
    if (!readRequiredString(file->value, "relativePath", record.relativePath) ||
        !readRequiredString(file->value, "format", record.format)) {
        fail(error, "Media record file is missing relativePath or format.");
        return std::nullopt;
    }
    const auto size = optionalMember<std::uint64_t>(file->value, "sizeBytes",
        [](const rapidjson::Value& member, std::uint64_t& output) {
            if (!member.IsUint64()) return false;
            output = member.GetUint64();
            return true;
        });
    if (!size) {
        fail(error, "Media record file.sizeBytes must be an unsigned integer.");
        return std::nullopt;
    }
    record.sizeBytes = *size;
    if (!optionalMemberHasType(file->value, "modifiedUtc",
        [](const rapidjson::Value& member) { return member.IsString(); })) {
        fail(error, "Media record file.modifiedUtc must be a string.");
        return std::nullopt;
    }
    record.modifiedUtc = stringMember(file->value, "modifiedUtc");

    if (!optionalMemberHasType(technical->value, "durationSeconds",
            [](const rapidjson::Value& member) { return member.IsNumber() && std::isfinite(member.GetDouble()); }) ||
        !optionalMemberHasType(technical->value, "bitRateKbps",
            [](const rapidjson::Value& member) { return member.IsInt(); }) ||
        !optionalMemberHasType(technical->value, "sampleRateHz",
            [](const rapidjson::Value& member) { return member.IsInt(); }) ||
        !optionalMemberHasType(technical->value, "channels",
            [](const rapidjson::Value& member) { return member.IsInt(); }) ||
        !optionalMemberHasType(technical->value, "declaredBitsPerSample",
            [](const rapidjson::Value& member) { return member.IsInt(); }) ||
        !optionalMemberHasType(technical->value, "totalSamples",
            [](const rapidjson::Value& member) { return member.IsUint64(); }) ||
        !optionalMemberHasType(technical->value, "flacMd5",
            [](const rapidjson::Value& member) { return member.IsString(); }) ||
        !optionalMemberHasType(technical->value, "tagVendor",
            [](const rapidjson::Value& member) { return member.IsString(); })) {
        fail(error, "Media record contains an invalid technical property type.");
        return std::nullopt;
    }
    record.durationSeconds = optionalMember<double>(technical->value, "durationSeconds",
        [](const rapidjson::Value& member, double& output) {
            if (!member.IsNumber()) return false;
            output = member.GetDouble();
            return std::isfinite(output);
        });
    record.bitRateKbps = optionalMember<int>(technical->value, "bitRateKbps",
        [](const rapidjson::Value& member, int& output) {
            if (!member.IsInt()) return false;
            output = member.GetInt();
            return true;
        });
    record.sampleRateHz = optionalMember<int>(technical->value, "sampleRateHz",
        [](const rapidjson::Value& member, int& output) {
            if (!member.IsInt()) return false;
            output = member.GetInt();
            return true;
        });
    record.channels = optionalMember<int>(technical->value, "channels",
        [](const rapidjson::Value& member, int& output) {
            if (!member.IsInt()) return false;
            output = member.GetInt();
            return true;
        });
    if ((record.sampleRateHz && *record.sampleRateHz <= 0) ||
        (record.channels && *record.channels <= 0)) {
        fail(error, "Sample rate and channel count must be positive when present.");
        return std::nullopt;
    }
    record.declaredBitsPerSample = optionalMember<int>(technical->value, "declaredBitsPerSample",
        [](const rapidjson::Value& member, int& output) {
            if (!member.IsInt()) return false;
            output = member.GetInt();
            return true;
        });
    record.totalSamples = optionalMember<std::uint64_t>(technical->value, "totalSamples",
        [](const rapidjson::Value& member, std::uint64_t& output) {
            if (!member.IsUint64()) return false;
            output = member.GetUint64();
            return true;
        });
    record.flacMd5 = stringMember(technical->value, "flacMd5");
    record.tagVendor = stringMember(technical->value, "tagVendor");

    if (integrity != value.MemberEnd()) {
        const auto status = stringMember(integrity->value, "status");
        if (!status || (*status != "ok" && *status != "partial" &&
            *status != "failed" && *status != "unsupported" && *status != "not_run")) {
            fail(error, "Media record integrity status is invalid.");
            return std::nullopt;
        }
        record.integrity.status = *status;
        if (!optionalMemberHasType(integrity->value, "decodedSamples",
                [](const rapidjson::Value& member) { return member.IsUint64(); }) ||
            !optionalMemberHasType(integrity->value, "decodeErrors",
                [](const rapidjson::Value& member) { return member.IsUint64(); }) ||
            !optionalMemberHasType(integrity->value, "firstErrorPosition",
                [](const rapidjson::Value& member) { return member.IsInt64(); }) ||
            !optionalMemberHasType(integrity->value, "flacMd5Matches",
                [](const rapidjson::Value& member) { return member.IsBool(); }) ||
            !optionalMemberHasType(integrity->value, "fileSha256",
                [](const rapidjson::Value& member) { return member.IsString(); }) ||
            !optionalMemberHasType(integrity->value, "pcmSha256",
                [](const rapidjson::Value& member) { return member.IsString(); }) ||
            !optionalMemberHasType(integrity->value, "error",
                [](const rapidjson::Value& member) { return member.IsString(); })) {
            fail(error, "Media record integrity contains an invalid property type.");
            return std::nullopt;
        }
        record.integrity.decodedSamples = optionalMember<std::uint64_t>(
            integrity->value, "decodedSamples",
            [](const rapidjson::Value& member, std::uint64_t& output) {
                if (!member.IsUint64()) return false;
                output = member.GetUint64();
                return true;
            });
        record.integrity.decodeErrors = optionalMember<std::uint64_t>(
            integrity->value, "decodeErrors",
            [](const rapidjson::Value& member, std::uint64_t& output) {
                if (!member.IsUint64()) return false;
                output = member.GetUint64();
                return true;
            }).value_or(0);
        record.integrity.firstErrorPosition = optionalMember<std::int64_t>(
            integrity->value, "firstErrorPosition",
            [](const rapidjson::Value& member, std::int64_t& output) {
                if (!member.IsInt64()) return false;
                output = member.GetInt64();
                return true;
            });
        record.integrity.flacMd5Matches = optionalMember<bool>(
            integrity->value, "flacMd5Matches",
            [](const rapidjson::Value& member, bool& output) {
                if (!member.IsBool()) return false;
                output = member.GetBool();
                return true;
            });
        record.integrity.fileSha256 = stringMember(integrity->value, "fileSha256");
        record.integrity.pcmSha256 = stringMember(integrity->value, "pcmSha256");
        record.integrity.error = stringMember(integrity->value, "error");
    }
    if (findings != value.MemberEnd()) {
        for (const auto& item : findings->value.GetArray()) {
            if (!item.IsObject()) {
                fail(error, "Each finding must be an object.");
                return std::nullopt;
            }
            CatalogFinding finding;
            if (!readRequiredString(item, "code", finding.code) ||
                !readRequiredString(item, "severity", finding.severity) ||
                !readRequiredString(item, "confidence", finding.confidence) ||
                !readRequiredString(item, "evidence", finding.evidence)) {
                fail(error, "Finding is missing code, severity, confidence, or evidence.");
                return std::nullopt;
            }
            if ((finding.severity != "error" && finding.severity != "warning" &&
                    finding.severity != "info") ||
                (finding.confidence != "low" && finding.confidence != "medium" &&
                    finding.confidence != "high")) {
                fail(error, "Finding severity or confidence is invalid.");
                return std::nullopt;
            }
            record.findings.push_back(std::move(finding));
        }
    }
    if (diagnostics != value.MemberEnd()) {
        for (const auto& item : diagnostics->value.GetArray()) {
            if (!item.IsObject()) {
                fail(error, "Each diagnostic must be an object.");
                return std::nullopt;
            }
            MediaDiagnostic diagnostic;
            std::string severity;
            if (!readRequiredString(item, "stage", diagnostic.stage) ||
                !readRequiredString(item, "source", diagnostic.source) ||
                !readRequiredString(item, "code", diagnostic.code) ||
                !readRequiredString(item, "severity", severity) ||
                !readRequiredString(item, "message", diagnostic.message)) {
                fail(error, "Diagnostic is missing stage, source, code, severity, or message.");
                return std::nullopt;
            }
            const auto parsedSeverity = ParseDiagnosticSeverity(severity);
            if (diagnostic.stage.empty() || diagnostic.source.empty() || diagnostic.code.empty() ||
                !parsedSeverity) {
                fail(error, "Diagnostic stage, source, code, or severity is invalid.");
                return std::nullopt;
            }
            diagnostic.severity = *parsedSeverity;
            record.diagnostics.push_back(std::move(diagnostic));
        }
    }

    for (auto member = tags->value.MemberBegin(); member != tags->value.MemberEnd(); ++member) {
        if (!member->value.IsArray()) {
            fail(error, "Each normalized tag value must be an array.");
            return std::nullopt;
        }
        auto& values = record.tags[std::string(member->name.GetString(), member->name.GetStringLength())];
        for (const auto& item : member->value.GetArray()) {
            if (!item.IsString()) {
                fail(error, "Normalized tag values must be strings.");
                return std::nullopt;
            }
            values.emplace_back(item.GetString(), item.GetStringLength());
        }
    }

    for (const auto& item : rawTags->value.GetArray()) {
        if (!item.IsObject()) {
            fail(error, "Each raw tag must be an object.");
            return std::nullopt;
        }
        RawMetadataTag tag;
        if (!readRequiredString(item, "scope", tag.scope) ||
            !readRequiredString(item, "key", tag.key) ||
            !readRequiredString(item, "value", tag.value)) {
            fail(error, "Raw tag is missing scope, key, or value.");
            return std::nullopt;
        }
        const auto ordinal = optionalMember<std::uint32_t>(item, "ordinal",
            [](const rapidjson::Value& member, std::uint32_t& output) {
                if (!member.IsUint()) return false;
                output = member.GetUint();
                return true;
            });
        if (!ordinal) {
            fail(error, "Raw tag ordinal must be an unsigned integer.");
            return std::nullopt;
        }
        tag.ordinal = *ordinal;
        if (!optionalMemberHasType(item, "frame",
                [](const rapidjson::Value& member) { return member.IsString(); }) ||
            !optionalMemberHasType(item, "description",
                [](const rapidjson::Value& member) { return member.IsString(); }) ||
            !optionalMemberHasType(item, "language",
                [](const rapidjson::Value& member) { return member.IsString(); }) ||
            !optionalMemberHasType(item, "encoding",
                [](const rapidjson::Value& member) { return member.IsInt(); })) {
            fail(error, "Raw tag contains an invalid optional property type.");
            return std::nullopt;
        }
        tag.frame = stringMember(item, "frame");
        tag.description = stringMember(item, "description");
        tag.language = stringMember(item, "language");
        tag.encoding = optionalMember<int>(item, "encoding",
            [](const rapidjson::Value& member, int& output) {
                if (!member.IsInt()) return false;
                output = member.GetInt();
                return true;
            });
        record.rawTags.push_back(std::move(tag));
    }

    for (const auto& item : pictures->value.GetArray()) {
        if (!item.IsObject()) {
            fail(error, "Each picture descriptor must be an object.");
            return std::nullopt;
        }
        CatalogPicture picture;
        if (!optionalMemberHasType(item, "type",
                [](const rapidjson::Value& member) { return member.IsInt(); }) ||
            !optionalMemberHasType(item, "mime",
                [](const rapidjson::Value& member) { return member.IsString(); }) ||
            !optionalMemberHasType(item, "description",
                [](const rapidjson::Value& member) { return member.IsString(); }) ||
            !optionalMemberHasType(item, "width",
                [](const rapidjson::Value& member) { return member.IsUint(); }) ||
            !optionalMemberHasType(item, "height",
                [](const rapidjson::Value& member) { return member.IsUint(); })) {
            fail(error, "Picture descriptor contains an invalid property type.");
            return std::nullopt;
        }
        picture.type = optionalMember<int>(item, "type",
            [](const rapidjson::Value& member, int& output) {
                if (!member.IsInt()) return false;
                output = member.GetInt();
                return true;
            });
        picture.mime = stringMember(item, "mime");
        picture.description = stringMember(item, "description");
        picture.width = optionalMember<std::uint32_t>(item, "width",
            [](const rapidjson::Value& member, std::uint32_t& output) {
                if (!member.IsUint()) return false;
                output = member.GetUint();
                return true;
            });
        picture.height = optionalMember<std::uint32_t>(item, "height",
            [](const rapidjson::Value& member, std::uint32_t& output) {
                if (!member.IsUint()) return false;
                output = member.GetUint();
                return true;
            });
        const auto pictureSize = optionalMember<std::uint64_t>(item, "sizeBytes",
            [](const rapidjson::Value& member, std::uint64_t& output) {
                if (!member.IsUint64()) return false;
                output = member.GetUint64();
                return true;
            });
        if (!pictureSize) {
            fail(error, "Picture descriptor sizeBytes must be an unsigned integer.");
            return std::nullopt;
        }
        picture.sizeBytes = *pictureSize;
        record.pictures.push_back(std::move(picture));
    }
    for (const auto& item : chapters->value.GetArray()) {
        if (!item.IsObject()) {
            fail(error, "Each chapter must be an object.");
            return std::nullopt;
        }
        CatalogChapter chapter;
        if (!optionalMemberHasType(item, "id",
                [](const rapidjson::Value& member) { return member.IsInt64(); }) ||
            !optionalMemberHasType(item, "timeBase",
                [](const rapidjson::Value& member) { return member.IsString(); }) ||
            !optionalMemberHasType(item, "start",
                [](const rapidjson::Value& member) { return member.IsInt64(); }) ||
            !optionalMemberHasType(item, "startSeconds",
                [](const rapidjson::Value& member) {
                        return member.IsNumber() && std::isfinite(member.GetDouble()) &&
                            member.GetDouble() >= 0;
                }) ||
            !optionalMemberHasType(item, "end",
                [](const rapidjson::Value& member) { return member.IsInt64(); }) ||
            !optionalMemberHasType(item, "endSeconds",
                [](const rapidjson::Value& member) {
                        return member.IsNumber() && std::isfinite(member.GetDouble()) &&
                            member.GetDouble() >= 0;
                })) {
            fail(error, "Chapter contains an invalid timing property.");
            return std::nullopt;
        }
        chapter.id = optionalMember<std::int64_t>(item, "id",
            [](const rapidjson::Value& member, std::int64_t& output) {
                if (!member.IsInt64()) return false;
                output = member.GetInt64();
                return true;
            });
        chapter.timeBase = stringMember(item, "timeBase");
        chapter.start = optionalMember<std::int64_t>(item, "start",
            [](const rapidjson::Value& member, std::int64_t& output) {
                if (!member.IsInt64()) return false;
                output = member.GetInt64();
                return true;
            });
        chapter.startSeconds = optionalMember<double>(item, "startSeconds",
            [](const rapidjson::Value& member, double& output) {
                if (!member.IsNumber()) return false;
                output = member.GetDouble();
                return std::isfinite(output);
            });
        chapter.end = optionalMember<std::int64_t>(item, "end",
            [](const rapidjson::Value& member, std::int64_t& output) {
                if (!member.IsInt64()) return false;
                output = member.GetInt64();
                return true;
            });
        chapter.endSeconds = optionalMember<double>(item, "endSeconds",
            [](const rapidjson::Value& member, double& output) {
                if (!member.IsNumber()) return false;
                output = member.GetDouble();
                return std::isfinite(output);
            });
        const auto chapterTags = item.FindMember("tags");
        if (chapterTags == item.MemberEnd() || !chapterTags->value.IsObject()) {
            fail(error, "Chapter tags must be an object.");
            return std::nullopt;
        }
        for (auto tag = chapterTags->value.MemberBegin();
            tag != chapterTags->value.MemberEnd(); ++tag) {
            if (!tag->value.IsArray()) {
                fail(error, "Chapter tag values must be arrays.");
                return std::nullopt;
            }
            auto& values = chapter.tags[std::string(tag->name.GetString(), tag->name.GetStringLength())];
            for (const auto& tagValue : tag->value.GetArray()) {
                if (!tagValue.IsString()) {
                    fail(error, "Chapter tag values must be strings.");
                    return std::nullopt;
                }
                values.emplace_back(tagValue.GetString(), tagValue.GetStringLength());
            }
        }
        record.chapters.push_back(std::move(chapter));
    }
    if (!readRequiredString(support->value, "tags", record.tagSupport) ||
        !readRequiredString(support->value, "nativeInventory", record.nativeInventorySupport) ||
        !readRequiredString(support->value, "decode", record.decodeSupport) ||
        !readRequiredString(support->value, "chapters", record.chapterSupport) ||
        !readRequiredString(analysis->value, "status", record.analysisStatus)) {
        fail(error, "Media record support or audioAnalysis status is missing.");
        return std::nullopt;
    }
    if ((record.tagSupport != "supported" && record.tagSupport != "unsupported") ||
        (record.nativeInventorySupport != "complete" && record.nativeInventorySupport != "partial" &&
            record.nativeInventorySupport != "unsupported") ||
        (record.chapterSupport != "supported" && record.chapterSupport != "unsupported") ||
        (record.decodeSupport != "verified" && record.decodeSupport != "failed" &&
            record.decodeSupport != "unsupported" && record.decodeSupport != "not_run") ||
        (record.analysisStatus != "complete" && record.analysisStatus != "partial" &&
            record.analysisStatus != "failed" && record.analysisStatus != "not_run")) {
        fail(error, "Media record contains an unknown support or analysis status.");
        return std::nullopt;
    }
    if (!optionalMemberHasType(analysis->value, "tier",
        [](const rapidjson::Value& member) { return member.IsString(); })) {
        fail(error, "Media record scan tier must be a string.");
        return std::nullopt;
    }
    record.scanTier = stringMember(analysis->value, "tier").value_or("catalog");
    if (record.scanTier != "catalog" && record.scanTier != "integrity" &&
        record.scanTier != "analysis") {
        fail(error, "Media record scan tier is invalid.");
        return std::nullopt;
    }
    const auto measurements = analysis->value.FindMember("measurements");
    if (measurements != analysis->value.MemberEnd()) {
        if (!measurements->value.IsObject()) {
            fail(error, "Audio analysis measurements must be an object.");
            return std::nullopt;
        }
        if (!optionalMemberHasType(analysis->value, "method",
            [](const rapidjson::Value& member) { return member.IsString(); })) {
            fail(error, "Audio analysis method must be a string.");
            return std::nullopt;
        }
        record.signal.method = stringMember(analysis->value, "method");
        for (auto item = measurements->value.MemberBegin();
            item != measurements->value.MemberEnd(); ++item) {
            if (!item->value.IsNumber() || !std::isfinite(item->value.GetDouble())) {
                fail(error, "Audio analysis measurements must be finite numbers.");
                return std::nullopt;
            }
            record.signal.measurements.emplace(
                std::string(item->name.GetString(), item->name.GetStringLength()),
                item->value.GetDouble());
        }
    }
    if (!optionalMemberHasType(analysis->value, "clippedSamples",
            [](const rapidjson::Value& member) { return member.IsUint64(); }) ||
        !optionalMemberHasType(analysis->value, "usedBits",
            [](const rapidjson::Value& member) { return member.IsInt(); }) ||
        !optionalMemberHasType(analysis->value, "usedBitsStatus",
            [](const rapidjson::Value& member) { return member.IsString(); })) {
        fail(error, "Audio analysis contains an invalid property type.");
        return std::nullopt;
    }
    record.signal.clippedSamples = optionalMember<std::uint64_t>(
        analysis->value, "clippedSamples",
        [](const rapidjson::Value& member, std::uint64_t& output) {
            if (!member.IsUint64()) return false;
            output = member.GetUint64();
            return true;
        });
    record.signal.usedBits = optionalMember<int>(analysis->value, "usedBits",
        [](const rapidjson::Value& member, int& output) {
            if (!member.IsInt()) return false;
            output = member.GetInt();
            return true;
        });
    record.signal.usedBitsStatus = stringMember(analysis->value, "usedBitsStatus");
    record.signal.status = record.analysisStatus;
    if (record.chapterSupport == "unsupported" && !record.chapters.empty()) {
        fail(error, "Unsupported chapter metadata cannot contain chapter records.");
        return std::nullopt;
    }
    if (!optionalMemberHasType(value, "error",
        [](const rapidjson::Value& member) { return member.IsString(); })) {
        fail(error, "Media record error must be a string.");
        return std::nullopt;
    }
    record.error = stringMember(value, "error");
    return record;
}
