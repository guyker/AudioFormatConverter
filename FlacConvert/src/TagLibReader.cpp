#include "TagLibReader.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <system_error>

#include <taglib/fileref.h>
#include <taglib/flac/flacfile.h>
#include <taglib/flac/flacpicture.h>
#include <taglib/mpeg/mpegfile.h>
#include <taglib/mpeg/mpegproperties.h>
#include <taglib/mpeg/id3v2/id3v2tag.h>
#include <taglib/mpeg/id3v2/id3v2header.h>
#include <taglib/mpeg/id3v2/id3v2frame.h>
#include <taglib/mpeg/id3v2/frames/attachedpictureframe.h>
#include <taglib/mpeg/id3v2/frames/commentsframe.h>
#include <taglib/mpeg/id3v2/frames/textidentificationframe.h>
#include <taglib/ogg/xiphcomment.h>
#include <taglib/toolkit/tfilestream.h>
#include <taglib/toolkit/tpropertymap.h>
#include <taglib/mpeg/id3v1/id3v1tag.h>

#include <spdlog/spdlog.h>

#include "CommonUtils.h"
#include "ffmpeg.h"

namespace {

TagLibReadResult failedRead(std::string stage, std::string source, std::string code,
    std::string message, const std::error_code& error = {})
{
    auto diagnosticMessage = message;
    return {
        std::nullopt,
        std::move(message),
        { MediaDiagnostic{
            std::move(stage), std::move(source), std::move(code), DiagnosticSeverity::Error,
            std::move(diagnosticMessage),
            error ? std::optional<int>(error.value()) : std::nullopt,
            error ? std::optional<std::string>(error.category().name()) : std::nullopt
        } }
    };
}

std::string toUtf8(const TagLib::String& value)
{
    return value.to8Bit(true);
}

std::string frameId(const TagLib::ID3v2::Frame& frame)
{
    const auto id = frame.frameID();
    return std::string(id.data(), id.size());
}

std::string extensionFor(const std::filesystem::path& path)
{
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}

std::string tagScope(const std::string& extension)
{
    if (extension == ".flac") return "vorbis";
    if (extension == ".mp3") return "id3";
    if (extension == ".m4a" || extension == ".mp4" || extension == ".aac") return "mp4";
    if (extension == ".ogg" || extension == ".oga" || extension == ".opus") return "xiph";
    if (extension == ".wv" || extension == ".ape") return "ape";
    if (extension == ".wav") return "riff-info";
    if (extension == ".aif" || extension == ".aiff") return "id3";
    return "taglib";
}

std::string formatName(const std::string& extension)
{
    if (extension == ".flac") return "flac";
    if (extension == ".mp3") return "mp3";
    if (extension == ".m4a" || extension == ".mp4") return "mp4";
    if (extension == ".aac") return "aac";
    if (extension == ".ogg" || extension == ".oga") return "ogg";
    if (extension == ".opus") return "opus";
    if (extension == ".wv") return "wavpack";
    if (extension == ".ape") return "ape";
    if (extension == ".wav") return "wav";
    if (extension == ".aif" || extension == ".aiff") return "aiff";
    return extension.empty() ? "unknown" : extension.substr(1);
}

std::string normalizeTagKey(std::string key)
{
    std::ranges::transform(key, key.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (key == "artist") return "artists";
    if (key == "genre") return "genres";
    if (key == "albumartist" || key == "album artist" || key == "album_artist") return "albumArtists";
    if (key == "track" || key == "tracknumber" || key == "track number") return "trackNumber";
    if (key == "disc" || key == "discnumber" || key == "disc number") return "discNumber";
    if (key == "date" || key == "year" || key == "releasedate" || key == "release date") return "releaseDate";
    if (key == "originaldate") return "originalDate";
    if (key == "recordingdate" || key == "recording date") return "recordingDate";
    if (key == "encodedby") return "encodedBy";
    if (key == "catalognumber" || key == "catalog number") return "catalogNumber";
    if (key == "barcode" || key == "ean" || key == "upc" || key == "upc_e" ||
        key == "upc-e") return "barcode";
    if (key == "publisher") return "label";
    if (key == "isrc") return "isrc";
    if (key == "musicbrainz_trackid") return "musicBrainzTrackId";
    if (key == "musicbrainz_albumid") return "musicBrainzAlbumId";
    if (key == "musicbrainz_releaseid") return "musicBrainzReleaseId";
    if (key == "musicbrainz_releasegroupid") return "musicBrainzReleaseGroupId";
    if (key == "musicbrainz_recordingid") return "musicBrainzRecordingId";
    if (key == "musicbrainz_artistid") return "musicBrainzArtistId";
    if (key == "musicbrainz_albumartistid") return "musicBrainzAlbumArtistId";
    if (key == "acoustid_id") return "acoustId";
    return key;
}

void addFinding(MediaRecord& record, std::string code, std::string evidence)
{
    if (std::ranges::any_of(record.findings, [&](const CatalogFinding& finding) {
            return finding.code == code;
        })) {
        return;
    }
    record.findings.push_back({ std::move(code), "warning", "high", std::move(evidence) });
}

bool hasTagValue(const MediaRecord& record, const std::string& key)
{
    const auto tag = record.tags.find(key);
    return tag != record.tags.end() &&
        std::ranges::any_of(tag->second, [](const std::string& value) {
            return std::ranges::any_of(value, [](unsigned char character) {
                return !std::isspace(character);
            });
        });
}

bool isValidNumberTag(std::string_view value)
{
    const auto slash = value.find('/');
    const auto numberText = value.substr(0, slash);
    const auto totalText = slash == std::string_view::npos
        ? std::string_view{} : value.substr(slash + 1);
    int number = 0;
    const auto numberResult = std::from_chars(
        numberText.data(), numberText.data() + numberText.size(), number);
    if (numberResult.ec != std::errc{} || numberResult.ptr != numberText.data() + numberText.size() ||
        number <= 0) {
        return false;
    }
    if (slash == std::string_view::npos) return true;
    if (totalText.empty() || totalText.find('/') != std::string_view::npos) return false;
    int total = 0;
    const auto totalResult = std::from_chars(
        totalText.data(), totalText.data() + totalText.size(), total);
    return totalResult.ec == std::errc{} &&
        totalResult.ptr == totalText.data() + totalText.size() && total >= number;
}

bool isValidIsoDate(std::string_view value)
{
    if (value.size() != 4 && value.size() != 7 && value.size() != 10) return false;
    if (value.size() >= 7 && value[4] != '-') return false;
    if (value.size() == 10 && value[7] != '-') return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (value[i] < '0' || value[i] > '9') return false;
    }
    const auto year = std::stoi(std::string(value.substr(0, 4)));
    if (year == 0) return false;
    if (value.size() == 4) return true;
    const auto month = std::stoi(std::string(value.substr(5, 2)));
    if (month < 1 || month > 12) return false;
    if (value.size() == 7) return true;
    const auto day = std::stoi(std::string(value.substr(8, 2)));
    static constexpr std::array<int, 12> daysInMonth{
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    const bool leapYear = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int maximumDay = month == 2 && leapYear ? 29 : daysInMonth[month - 1];
    return day >= 1 && day <= maximumDay;
}

bool isValidMusicBrainzId(std::string_view value)
{
    if (value.size() != 36) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-') return false;
        }
        else if (!std::isxdigit(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }
    return true;
}

bool isValidIsrc(std::string_view value)
{
    if (value.size() != 12) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto character = static_cast<unsigned char>(value[i]);
        const bool valid = i < 2 ? std::isalpha(character) != 0 :
            (i < 5 ? std::isalnum(character) != 0 : std::isdigit(character) != 0);
        if (!valid) return false;
    }
    return true;
}

bool hasValidBarcodeCheckDigit(std::string_view digits)
{
    int weightedSum = 0;
    int weight = 3;
    for (std::size_t index = digits.size() - 1; index > 0; --index) {
        weightedSum += (digits[index - 1] - '0') * weight;
        weight = weight == 3 ? 1 : 3;
    }
    const int checkDigit = (10 - weightedSum % 10) % 10;
    return digits.back() - '0' == checkDigit;
}

bool isValidUpcE(std::string_view digits)
{
    if (digits.size() != 8 || (digits.front() != '0' && digits.front() != '1')) {
        return false;
    }

    std::string expanded;
    expanded.reserve(12);
    expanded.push_back(digits[0]);
    expanded.push_back(digits[1]);
    expanded.push_back(digits[2]);
    switch (digits[6]) {
    case '0':
    case '1':
    case '2':
        expanded.push_back(digits[6]);
        expanded.append("0000");
        expanded.push_back(digits[3]);
        expanded.push_back(digits[4]);
        expanded.push_back(digits[5]);
        break;
    case '3':
        expanded.push_back(digits[3]);
        expanded.append("00000");
        expanded.push_back(digits[4]);
        expanded.push_back(digits[5]);
        break;
    case '4':
        expanded.push_back(digits[3]);
        expanded.push_back(digits[4]);
        expanded.append("00000");
        expanded.push_back(digits[5]);
        break;
    default:
        expanded.push_back(digits[3]);
        expanded.push_back(digits[4]);
        expanded.push_back(digits[5]);
        expanded.append("0000");
        expanded.push_back(digits[6]);
        break;
    }
    expanded.push_back(digits[7]);
    return hasValidBarcodeCheckDigit(expanded);
}

bool isValidBarcode(std::string_view value)
{
    std::string digits;
    digits.reserve(value.size());
    for (const unsigned char character : value) {
        if (std::isdigit(character)) {
            digits.push_back(static_cast<char>(character));
        }
        else if (character != '-' && !std::isspace(character)) {
            return false;
        }
    }
    if (digits.size() == 8) {
        return hasValidBarcodeCheckDigit(digits) || isValidUpcE(digits);
    }
    if (digits.size() != 12 && digits.size() != 13 && digits.size() != 14) {
        return false;
    }
    return hasValidBarcodeCheckDigit(digits);
}

void validateTagValues(MediaRecord& record)
{
    for (const auto* key : { "title", "artists", "album" }) {
        if (!hasTagValue(record, key)) {
            addFinding(record, std::string("MISSING_") +
                (key == std::string_view("artists") ? "ARTIST" : key == std::string_view("title") ?
                    "TITLE" : "ALBUM"), std::string("No non-empty normalized '") + key + "' tag was found.");
        }
    }

    for (const auto* key : { "trackNumber", "discNumber" }) {
        const auto tag = record.tags.find(key);
        if (tag == record.tags.end()) continue;
        for (const auto& value : tag->second) {
            if (!isValidNumberTag(value)) {
                addFinding(record, key == std::string_view("trackNumber")
                    ? "INVALID_TRACK_NUMBER" : "INVALID_DISC_NUMBER",
                    std::string("The normalized '") + key + "' value is not a valid positive number[/total].");
                break;
            }
        }
    }

    for (const auto* key : { "releaseDate", "originalDate", "recordingDate" }) {
        const auto tag = record.tags.find(key);
        if (tag == record.tags.end()) continue;
        for (const auto& value : tag->second) {
            if (!isValidIsoDate(value)) {
                addFinding(record, "NON_ISO_DATE",
                    std::string("The normalized '") + key + "' value is not YYYY, YYYY-MM, or YYYY-MM-DD.");
                break;
            }
        }
    }

    for (const auto& [key, values] : record.tags) {
        if (!key.starts_with("musicBrainz")) continue;
        for (const auto& value : values) {
            if (!isValidMusicBrainzId(value)) {
                addFinding(record, "INVALID_MUSICBRAINZ_ID",
                    std::string("The normalized '") + key + "' value is not a canonical UUID.");
                break;
            }
        }
    }
    if (const auto isrc = record.tags.find("isrc"); isrc != record.tags.end()) {
        for (const auto& value : isrc->second) {
            if (!isValidIsrc(value)) {
                addFinding(record, "INVALID_ISRC",
                    "The normalized 'isrc' value is not 12 characters in the ISO 3901 structure.");
                break;
            }
        }
    }
    if (const auto barcode = record.tags.find("barcode"); barcode != record.tags.end()) {
        for (const auto& value : barcode->second) {
            if (!isValidBarcode(value)) {
                addFinding(record, "INVALID_BARCODE",
                    "The normalized barcode is not a valid EAN/UPC/GTIN with a check digit.");
                break;
            }
        }
    }
}

void addNormalizedTag(MediaRecord& record, const std::string& key, const std::string& value)
{
    record.tags[normalizeTagKey(key)].push_back(value);
}

void addRawTag(MediaRecord& record, const std::string& scope, const std::string& key,
    const std::string& value, std::optional<std::string> frame = std::nullopt,
    std::optional<std::string> description = std::nullopt,
    std::optional<std::string> language = std::nullopt,
    std::optional<int> encoding = std::nullopt, bool normalize = true)
{
    const auto ordinal = static_cast<std::uint32_t>(record.rawTags.size());
    record.rawTags.push_back({ scope, key, value, ordinal, std::move(frame),
        std::move(description), std::move(language), encoding });
    if (normalize) addNormalizedTag(record, key, value);
}

void readProperties(MediaRecord& record, const TagLib::PropertyMap& properties,
    const std::string& scope, bool retainRaw = true)
{
    for (auto property = properties.begin(); property != properties.end(); ++property) {
        const auto key = toUtf8(property->first);
        for (const auto& value : property->second) {
            const auto utf8Value = toUtf8(value);
            if (retainRaw) addRawTag(record, scope, key, utf8Value);
            else addNormalizedTag(record, key, utf8Value);
        }
    }
}

void readId3v2(MediaRecord& record, const TagLib::ID3v2::Tag* tag)
{
    if (tag == nullptr) return;
    const auto* header = tag->header();
    const unsigned int version = header == nullptr ? 0 : header->majorVersion();
    const std::string scope = "id3v2." + std::to_string(version);
    for (const auto* frame : tag->frameList()) {
        const auto id = frameId(*frame);
        const auto value = toUtf8(frame->toString());
        std::optional<std::string> description;
        std::optional<std::string> language;
        std::optional<int> encoding;
        if (const auto* text = dynamic_cast<const TagLib::ID3v2::TextIdentificationFrame*>(frame)) {
            encoding = static_cast<int>(text->textEncoding());
            if (const auto* userText =
                dynamic_cast<const TagLib::ID3v2::UserTextIdentificationFrame*>(text)) {
                description = toUtf8(userText->description());
            }
        }
        else if (const auto* comment = dynamic_cast<const TagLib::ID3v2::CommentsFrame*>(frame)) {
            description = toUtf8(comment->description());
            const auto code = comment->language();
            language = std::string(code.data(), code.size());
            encoding = static_cast<int>(comment->textEncoding());
        }
        addRawTag(record, scope, id, value, id, std::move(description),
            std::move(language), encoding, false);

        if (const auto* picture =
            dynamic_cast<const TagLib::ID3v2::AttachedPictureFrame*>(frame)) {
            const auto pictureBytes = picture->picture();
            record.pictures.push_back({
                static_cast<int>(picture->type()),
                toUtf8(picture->mimeType()),
                toUtf8(picture->description()),
                std::nullopt,
                std::nullopt,
                static_cast<std::uint64_t>(pictureBytes.size())
            });
        }
    }
}

void readFlac(MediaRecord& record, TagLib::FileRef& file)
{
    auto* flac = dynamic_cast<TagLib::FLAC::File*>(file.file());
    if (flac == nullptr) return;
    if (flac->hasXiphComment()) {
        auto* comments = flac->xiphComment();
        if (comments != nullptr) {
            record.tagVendor = toUtf8(comments->vendorID());
            for (const auto& [key, values] : comments->fieldListMap()) {
                const auto utf8Key = toUtf8(key);
                for (const auto& value : values) {
                    addRawTag(record, "vorbis", utf8Key, toUtf8(value), std::nullopt,
                        std::nullopt, std::nullopt, std::nullopt, false);
                }
            }
        }
    }
    auto* properties = flac->audioProperties();
    if (properties != nullptr) {
        if (properties->sampleFrames() > 0) {
            record.totalSamples = properties->sampleFrames();
        }
        record.declaredBitsPerSample = properties->bitsPerSample();
        const auto signature = properties->signature();
        if (!signature.isEmpty() &&
            std::any_of(signature.begin(), signature.end(), [](char byte) { return byte != 0; })) {
            static constexpr char hex[] = "0123456789abcdef";
            std::string digest;
            digest.reserve(signature.size() * 2);
            for (const auto byte : signature) {
                const auto value = static_cast<unsigned char>(byte);
                digest.push_back(hex[value >> 4]);
                digest.push_back(hex[value & 0x0f]);
            }
            record.flacMd5 = std::move(digest);
        }
    }
    if (flac->hasID3v2Tag()) readId3v2(record, flac->ID3v2Tag());
    if (flac->hasID3v1Tag()) {
        const auto* tag = flac->ID3v1Tag();
        if (tag != nullptr) {
            const auto add = [&](const char* key, const TagLib::String& value) {
                if (!value.isEmpty()) {
                    addRawTag(record, "id3v1", key, toUtf8(value), std::nullopt,
                        std::nullopt, std::nullopt, std::nullopt, false);
                }
            };
            add("title", tag->title());
            add("artist", tag->artist());
            add("album", tag->album());
            if (tag->year() != 0) add("year", TagLib::String(std::to_string(tag->year())));
            add("comment", tag->comment());
            add("genre", tag->genre());
            if (tag->track() != 0) {
                addRawTag(record, "id3v1", "track", std::to_string(tag->track()), std::nullopt,
                    std::nullopt, std::nullopt, std::nullopt, false);
            }
        }
    }
    for (const auto* picture : flac->pictureList()) {
        record.pictures.push_back({
            static_cast<int>(picture->type()),
            toUtf8(picture->mimeType()),
            toUtf8(picture->description()),
            picture->width(),
            picture->height(),
            static_cast<std::uint64_t>(picture->data().size())
        });
    }
}

void readMp3(MediaRecord& record, TagLib::FileRef& file)
{
    auto* mpeg = dynamic_cast<TagLib::MPEG::File*>(file.file());
    if (mpeg == nullptr) return;
    if (mpeg->hasID3v2Tag()) {
        readId3v2(record, mpeg->ID3v2Tag());
    }
    if (mpeg->hasID3v1Tag()) {
        const auto* tag = mpeg->ID3v1Tag();
        if (tag != nullptr) {
            const auto add = [&](const char* key, const TagLib::String& value) {
                if (!value.isEmpty()) {
                    addRawTag(record, "id3v1", key, toUtf8(value), std::nullopt,
                        std::nullopt, std::nullopt, std::nullopt, false);
                }
            };
            add("title", tag->title());
            add("artist", tag->artist());
            add("album", tag->album());
            if (tag->year() != 0) add("year", TagLib::String(std::to_string(tag->year())));
            add("comment", tag->comment());
            add("genre", tag->genre());
            if (tag->track() != 0) {
                addRawTag(record, "id3v1", "track", std::to_string(tag->track()), std::nullopt,
                    std::nullopt, std::nullopt, std::nullopt, false);
            }
        }
    }
    const auto* properties = mpeg->audioProperties();
    if (properties != nullptr) {
        record.bitRateKbps = properties->bitrate();
    }
}

std::string fileNameUtf8(const std::filesystem::path& filePath)
{
    return CommonUtils::path_to_utf8(filePath);
}

std::optional<double> parseSeconds(const std::optional<std::string>& value)
{
    if (!value) return std::nullopt;
    double seconds = 0;
    const auto result = std::from_chars(value->data(), value->data() + value->size(), seconds);
    if (result.ec != std::errc{} || result.ptr != value->data() + value->size() ||
        !std::isfinite(seconds) || seconds < 0) {
        return std::nullopt;
    }
    return seconds;
}

}

TagLibReadResult TagLibReader::Read(const std::filesystem::path& filePath,
    const std::string& relativePath)
{
    const auto extension = extensionFor(filePath);
    MediaRecord record;
    record.relativePath = relativePath;
    record.format = formatName(extension);
    record.tagSupport = "supported";
    record.nativeInventorySupport = extension == ".flac" || extension == ".mp3" ?
        "partial" : "unsupported";
    record.decodeSupport = "not_run";
    record.chapterSupport = "unsupported";

    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(filePath, ec);
    if (ec) {
        return failedRead("metadata", "filesystem", DiagnosticCode::FileSizeFailed,
            "Cannot read file size: " + ec.message(), ec);
    }
    record.sizeBytes = fileSize;
    record.modifiedUtc = CommonUtils::fileModificationTimeUtc(filePath);

#ifdef _WIN32
    const TagLib::FileName name(filePath.c_str());
#else
    const auto nameUtf8 = fileNameUtf8(filePath);
    const TagLib::FileName name(nameUtf8.c_str());
#endif
    {
        TagLib::FileStream stream(name, true);
        if (!stream.isOpen()) {
            return failedRead("tags", "taglib", DiagnosticCode::TaglibOpenFailed,
                "TagLib could not open the audio file read-only.");
        }
        TagLib::FileRef file(&stream, true, TagLib::AudioProperties::Fast);
        if (file.isNull()) {
            return failedRead("tags", "taglib", DiagnosticCode::TaglibReadFailed,
                "TagLib could not identify or read the audio file.");
        }
        readProperties(record, file.properties(), tagScope(extension),
            extension != ".flac" && extension != ".mp3");
        if (const auto* properties = file.audioProperties()) {
            const auto durationMs = properties->lengthInMilliseconds();
            if (durationMs >= 0) record.durationSeconds = durationMs / 1000.0;
            if (properties->bitrate() > 0) record.bitRateKbps = properties->bitrate();
            if (properties->sampleRate() > 0) record.sampleRateHz = properties->sampleRate();
            if (properties->channels() > 0) record.channels = properties->channels();
        }
        if (extension == ".flac") readFlac(record, file);
        if (extension == ".mp3") readMp3(record, file);
    }
    if (extension == ".flac" && !record.totalSamples &&
        record.durationSeconds && *record.durationSeconds == 0) {
        record.durationSeconds.reset();
    }
    {
        std::string probeError;
        FFprobeOutput probe;
        std::vector<FFmpeg::FFmpegLogItem> ffmpegLogs;
        {
            FFmpeg::ScopedLogCapture logCapture(filePath, "chapters");
            probe = FFmpeg::GetFFprobeMetadataAPI(filePath, false, &probeError);
            ffmpegLogs = logCapture.TakeLogs();
        }
        for (const auto& log : ffmpegLogs) {
            record.diagnostics.push_back({
                log.stage, "ffmpeg", DiagnosticCode::FfmpegLog, ParseDiagnosticSeverity(log.level).value(),
                log.component + ": " + log.message
            });
        }
        if (!probeError.empty()) {
            const auto message = "FFmpeg container metadata read failed: " + probeError;
            record.diagnostics.push_back({
                "chapters", "ffmpeg", DiagnosticCode::FfmpegProbeFailed,
                extension == ".flac" || extension == ".mp3"
                    ? DiagnosticSeverity::Error : DiagnosticSeverity::Warning, message
            });
            if (extension == ".flac" || extension == ".mp3") {
                return { std::nullopt, message, std::move(record.diagnostics) };
            }
            validateTagValues(record);
            return { std::move(record), std::nullopt, {} };
        }
        if (probe.format.tags) {
            for (const auto& [key, value] : *probe.format.tags) {
                addRawTag(record, "ffmpeg.format", key, value,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt, false);
            }
        }
        for (const auto& stream : probe.streams) {
            if (!stream.tags || !stream.index) continue;
            const auto scope = "ffmpeg.stream." + std::to_string(*stream.index);
            for (const auto& [key, value] : *stream.tags) {
                addRawTag(record, scope, key, value,
                    std::nullopt, std::nullopt, std::nullopt, std::nullopt, false);
            }
        }
        record.metadataInventoryVersion = 1;
        if ((extension == ".flac" || extension == ".mp3") &&
            probe.format.format_name && !probe.format.format_name->empty()) {
            record.format = *probe.format.format_name;
        }
        if (!record.durationSeconds && probe.format.duration && *probe.format.duration > 0) {
            record.durationSeconds = *probe.format.duration;
        }
        record.chapterSupport = "supported";
        record.chapters.reserve(probe.chapters.size());
        for (const auto& source : probe.chapters) {
            CatalogChapter chapter;
            chapter.id = source.id;
            chapter.timeBase = source.time_base;
            chapter.start = source.start;
            chapter.startSeconds = parseSeconds(source.start_time);
            chapter.end = source.end;
            chapter.endSeconds = parseSeconds(source.end_time);
            if ((source.start_time && !chapter.startSeconds) ||
                (source.end_time && !chapter.endSeconds)) {
                const std::string message = "FFmpeg returned an invalid chapter time.";
                record.diagnostics.push_back({
                    "chapters", "ffmpeg", DiagnosticCode::InvalidChapterTime, DiagnosticSeverity::Error, message
                });
                return { std::nullopt, message, std::move(record.diagnostics) };
            }
            if ((chapter.start && chapter.end && *chapter.start > *chapter.end) ||
                (chapter.startSeconds && chapter.endSeconds &&
                    *chapter.startSeconds > *chapter.endSeconds)) {
                const std::string message = "FFmpeg returned a chapter with an inverted time range.";
                record.diagnostics.push_back({
                    "chapters", "ffmpeg", DiagnosticCode::InvertedChapterTime, DiagnosticSeverity::Error, message
                });
                return { std::nullopt, message, std::move(record.diagnostics) };
            }
            if (source.tags) {
                for (const auto& [key, value] : *source.tags) {
                    chapter.tags[key].push_back(value);
                }
            }
            record.chapters.push_back(std::move(chapter));
        }
    }
    if (record.totalSamples && record.sampleRateHz && *record.sampleRateHz > 0) {
        record.durationSeconds = static_cast<double>(*record.totalSamples) / *record.sampleRateHz;
    }
    validateTagValues(record);
    return { std::move(record), std::nullopt, {} };
}
