#include "DuplicateComparison.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void contains(const std::string& text, const std::string& value)
{
    if (text.find(value) == std::string::npos) throw std::runtime_error("Missing output: " + value + "\n" + text);
}

MediaAlbum album(const char* folder)
{
    MediaAlbum result;
    result.path = std::filesystem::directory_entry(std::filesystem::path("library") / folder);
    for (int i = 1; i <= 2; ++i) {
        MediaTrack track;
        track.trackPath = "song" + std::to_string(i) + ".flac";
        track.fs_fileSize = 1000;
        track.formatInfo.format.duration = i * 120.0;
        track.formatInfo.format.tags = JsonUtils::Tags{
            { "album", "The Album" }, { "artist", "The Artist" }, { "album_artist", "Various Artists" },
            { "title", "Song " + std::to_string(i) }, { "track", std::to_string(i) }, { "date", "2001" }
        };
        Stream artwork;
        artwork.codec_type = "video";
        artwork.codec_name = "mjpeg";
        artwork.sample_rate = "1";
        track.formatInfo.streams.push_back(artwork);
        Stream audio;
        audio.codec_type = "audio";
        audio.codec_name = "flac";
        audio.channels = 2;
        audio.channel_layout = "stereo";
        audio.sample_rate = "44100";
        audio.bits_per_sample = 16;
        audio.bit_rate = 800000;
        track.formatInfo.streams.push_back(audio);
        MediaRecord record;
        record.sizeBytes = 1000;
        record.durationSeconds = i * 120.0;
        record.flacMd5 = std::string(32, static_cast<char>('0' + i));
        record.tags["ALBUM"] = { "The Album" };
        record.tags["ARTIST"] = { "The Artist" };
        record.tags["TITLE"] = { "Song " + std::to_string(i) };
        record.tags["TRACKNUMBER"] = { std::to_string(i) };
        track.catalogRecord = record;
        result.trackList.push_back(track);
    }
    return result;
}

DuplicateComparison::AlbumSnapshot snapshot(const MediaAlbum& source)
{
    DuplicateComparison::AlbumSnapshot result;
    for (auto track : source.trackList) {
        track.catalogRecord.reset();
        result.tracks.push_back(std::move(track));
    }
    result.currentCount = result.tracks.size();
    return result;
}

DuplicateMatcher::PairEvaluation evaluation(const MediaAlbum& first, const MediaAlbum& second,
    const DuplicateMatcher::Options& options = {})
{
    return DuplicateMatcher::EvaluatePair(DuplicateMatcher::BuildProfile(first, options),
        DuplicateMatcher::BuildProfile(second, options), options);
}

void exactAndNumbering()
{
    auto first = album("copy-a"), second = album("copy-b");
    for (auto* item : { &first, &second }) {
        item->trackList[0].catalogRecord->integrity.status = "ok";
        item->trackList[0].catalogRecord->integrity.pcmSha256 = std::string(64, 'a');
    }
    const auto left = snapshot(first), right = snapshot(second);
    const auto text = DuplicateComparison::DescribePair(2, 3, first, left, second, right,
        evaluation(first, second), {}, true);
    contains(text, "Pair [2] <-> [3]: exact candidate (SCAN-TIME evidence)");
    contains(text, "## CONFIDENCE: EXACT   scale: [EXACT] > strong > probable; partial (coverage-only)");
    require(text.find('\x1b') == std::string::npos, "Colour codes emitted without useColor");
    const auto colored = DuplicateComparison::DescribePair(2, 3, first, left, second, right,
        evaluation(first, second), {}, false, true);
    contains(colored, "\x1b[0;30;42m CONFIDENCE: EXACT \x1b[0m");
    contains(DuplicateComparison::ConfidenceBanner(DuplicateMatcher::Confidence::Strong, true),
        "\x1b[0;30;46m CONFIDENCE: STRONG \x1b[0m");
    contains(DuplicateComparison::ConfidenceBanner(DuplicateMatcher::Confidence::Probable, true),
        "\x1b[0;30;43m CONFIDENCE: PROBABLE \x1b[0m");
    contains(DuplicateComparison::ConfidenceBanner(DuplicateMatcher::Confidence::Partial, true),
        "\x1b[0;30;45m CONFIDENCE: PARTIAL \x1b[0m");
    contains(DuplicateComparison::ConfidenceBanner(DuplicateMatcher::Confidence::Partial, false),
        "scale: exact > strong > probable; [PARTIAL] (coverage-only)");
    contains(text, "Stored FLAC MD5: equal 2, different 0, unavailable 0");
    contains(text, "Recorded decoded-PCM SHA-256: equal 1, different 0");
    contains(text, "NOT byte-identical files");
    contains(text, "Audio [2]: flac (lossless format), 44100 Hz, 16-bit, 2 channel(s)");
    contains(text, "Technical differences: none among known fields; unknown on one/both sides: 0");
    contains(text, "Current tags: 6 equal, 0 different/missing");
    require(text.find("mjpeg") == std::string::npos, "Artwork stream was treated as audio");
    require(text.find("Audio analysis was not run") == std::string::npos, "Noisy generic report was used");
    const auto overview = DuplicateComparison::DescribeAlbum(2, first, left);
    contains(overview, "Album [2]");
    contains(overview, "Album artist: Various Artists");
    contains(overview, "2000 bytes; duration 360.000 s across 2/2");
    contains(overview, "Recorded integrity status: ok=1");
    contains(overview, "Recorded integrity status: not_run=1");
}

void differingPropertiesAndLongTags()
{
    const auto first = album("flac"), second = album("lossy");
    const auto left = snapshot(first);
    auto right = snapshot(second);
    auto& track = *right.tracks[0];
    auto& audio = track.formatInfo.streams[1];
    audio.codec_name = "mp3";
    audio.sample_rate = "48000";
    audio.bits_per_sample = 0;
    audio.sample_fmt = "fltp";
    audio.bit_rate = 320000;
    track.formatInfo.format.tags->at("title") = "Other title";
    const auto longValue = std::string(99, 'x') + "\xc3\xa9\xc3\xa9" + std::string(130, 'y') + "END";
    track.formatInfo.format.tags->emplace("COMMENT", longValue);
    track.formatInfo.format.tags->emplace("CUSTOM-KEY", "keep punctuation");
    track.formatInfo.format.tags->emplace("CUSTOMKEY", "separate tag");
    track.formatInfo.streams[1].tags = JsonUtils::Tags{ { "language", "fra" }, { "note", "\x1b[31m" } };
    const auto text = DuplicateComparison::DescribePair(1, 2, first, left, second, right,
        evaluation(first, second), {}, true);
    contains(text, "mp3 (lossy format), 48000 Hz, unknown");
    contains(text, "sample rate;");
    contains(text, "codec;");
    contains(text, "unknown on one/both sides: 1");
    contains(text, "audio.language");
    contains(text, "format.custom-key");
    contains(text, "format.customkey");
    contains(text, "\\x1b[31m");
    require(text.find('\x1b') == std::string::npos, "Unescaped terminal control character");
    require(text.find("0-bit") == std::string::npos, "MP3 decoder precision misrepresented as source bit depth");
    contains(text, std::string(99, 'x') + "\n      \xc3\xa9\xc3\xa9");
    auto unwrapped = text;
    for (auto position = unwrapped.find("\n      "); position != std::string::npos;
        position = unwrapped.find("\n      ")) unwrapped.erase(position, 7);
    contains(unwrapped, longValue);
    const auto summary = DuplicateComparison::DescribePair(1, 2, first, left, second, right,
        evaluation(first, second), {}, false);
    require(summary.find("Track pair 1") == std::string::npos, "Compact recap repeated all track details");
}

void staleMissingAndPartial()
{
    auto first = album("first"), second = album("second");
    second.trackList.pop_back();
    second.trackList[0].catalogRecord->flacMd5 = std::string(32, '9');
    auto left = snapshot(first), right = snapshot(second);
    left.tracks[1].reset();
    left.tracks[0]->formatInfo.format.duration = 180.0;
    left.tracks[0]->formatInfo.format.tags->at("artist") = "Current artist";
    left.changedTracks.push_back(0);
    left.addedTracks.emplace_back("new-bonus.flac");
    left.otherEntries = { "cover.jpg", "bonus-disc" };
    left.currentCount = 2;
    first.trackList[0].catalogRecord->integrity.status = "failed";
    first.trackList[0].catalogRecord->integrity.error = "Checksum mismatch";
    first.trackList[0].catalogRecord->findings.push_back({ "MD5_MISMATCH", "error", "high", "Check audio" });
    CatalogPicture picture;
    picture.width = 1200;
    picture.height = 1200;
    picture.mime = "image/jpeg";
    picture.sizeBytes = 12345;
    first.trackList[0].catalogRecord->pictures.push_back(picture);
    DuplicateMatcher::Options options;
    options.partialMatches = true;
    options.partialMinCoverage = 50;
    const auto match = evaluation(first, second, options);
    require(match.duplicate, "Invalid partial fixture");
    const auto text = DuplicateComparison::DescribePair(1, 2, first, left, second, right, match, options, true);
    contains(text, "Coverage: 1/2 scanned tracks in [1], 1/1 in [2]");
    contains(text, "different 1, unavailable 0");
    contains(text, "stored audio hashes differ");
    contains(text, "OUTSIDE tolerance");
    contains(text, "rescan before relying on them");
    contains(text, "unmatched/bonus tracks");
    contains(text, "Unmatched scanned track [1]: song2.flac");
    const auto overview = DuplicateComparison::DescribeAlbum(1, first, left);
    contains(overview, "Track artists: Current artist");
    contains(overview, "1000 bytes; duration 180.000 s across 1/2");
    contains(overview, "1 changed (size/time), 1 missing, 1 added/uncompared");
    contains(overview, "Added/uncompared: new-bonus.flac");
    contains(overview, "Other immediate files/folders NOT compared");
    contains(overview, "cover.jpg");
    contains(overview, "bonus-disc");
    contains(overview, "Recorded artwork: 1200x1200, image/jpeg, 12345 bytes");
    contains(overview, "Checksum mismatch");
    contains(overview, "error MD5_MISMATCH - Check audio");

    left.tracks[0].reset();
    const auto missing = DuplicateComparison::DescribePair(1, 2, first, left, second, right, match, options, true);
    contains(missing, "Track missing in one of the albums. Current comparison unavailable");
    contains(missing, "currently readable pairs 0/1");
    require(missing.find("Comparing scanned tracks:") == std::string::npos, "Missing file compared as current");
}

void unknownAndLegacy()
{
    auto first = album("legacy-a"), second = album("legacy-b");
    for (auto* item : { &first, &second }) {
        for (auto& track : item->trackList) {
            track.catalogRecord.reset();
            track.formatInfo.format.duration.reset();
            track.formatInfo.format.tags = JsonUtils::Tags{ { "album", "The Album" } };
            track.formatInfo.streams[1] = Stream{};
            track.formatInfo.streams[1].codec_type = "audio";
        }
    }
    auto left = snapshot(first), right = snapshot(second);
    for (auto* item : { &left, &right }) {
        for (auto& track : item->tracks) track->formatInfo.format.tags.reset();
    }
    auto match = evaluation(first, second);
    require(match.duplicate, "Invalid unknown-duration fixture");
    const auto text = DuplicateComparison::DescribePair(1, 2, first, left, second, right, match, {}, true);
    contains(text, "unknown on one/both sides: 6");
    contains(text, "tag equality is unknown");
    contains(text, "current paired durations outside tolerance 0, unknown 2");
    contains(text, "Metadata/duration similarity is not proof");
    const auto overview = DuplicateComparison::DescribeAlbum(1, first, left);
    contains(overview, "Legacy tracks without inventory: 2");
    contains(overview, "Scan inventory unavailable");
    left.tracks[0]->formatInfo.streams.push_back(left.tracks[0]->formatInfo.streams[1]);
    contains(DuplicateComparison::DescribeAlbum(1, first, left), "multiple audio streams");
    right.tracks[0]->formatInfo.format.tags = JsonUtils::Tags{ { "invalid-utf8", std::string(120, '\x80') } };
    contains(DuplicateComparison::DescribePair(1, 2, first, left, second, right, match, {}, true), "format.invalid-utf8");
}
}

int main()
{
    try {
        exactAndNumbering();
        differingPropertiesAndLongTags();
        staleMissingAndPartial();
        unknownAndLegacy();
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
