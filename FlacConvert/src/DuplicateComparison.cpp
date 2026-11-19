#include "DuplicateComparison.h"

#include "CommonUtils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace
{
std::string safeText(std::string_view value)
{
    std::ostringstream text;
    for (const unsigned char character : value) {
        if (character < 0x20 || character == 0x7f) {
            text << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(character);
        }
        else text << static_cast<char>(character);
    }
    return text.str();
}

void printValue(std::ostream& output, const std::string& label, const std::string& value)
{
    const auto text = safeText(value);
    output << label;
    std::size_t offset = 0;
    while (offset < text.size()) {
        auto end = (std::min)(offset + 100, text.size());
        while (end < text.size() && end > offset &&
            (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
        if (end == offset) end = (std::min)(offset + 100, text.size());
        output << text.substr(offset, end - offset);
        offset = end;
        if (offset < text.size()) output << "\n      ";
    }
    output << '\n';
}

std::string tagKey(std::string_view key)
{
    std::string result;
    for (const unsigned char character : key) {
        if (character >= 'A' && character <= 'Z') result += static_cast<char>(character + ('a' - 'A'));
        else if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9')) {
            result += static_cast<char>(character);
        }
    }
    return result;
}

const Stream* audioStream(const MediaTrack& track)
{
    const auto& streams = track.formatInfo.streams;
    const auto found = std::ranges::find_if(streams, [](const Stream& stream) {
        return stream.codec_type == "audio";
    });
    return found == streams.end() ? nullptr : &*found;
}

std::map<std::string, std::string> currentTags(const MediaTrack& track)
{
    std::map<std::string, std::string> result;
    const auto add = [&](const std::optional<JsonUtils::Tags>& tags, const std::string& scope) {
        if (tags) for (const auto& [key, value] : *tags) {
            auto folded = key;
            for (auto& character : folded) {
                if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
            }
            result.emplace(scope + folded, value);
        }
    };
    add(track.formatInfo.format.tags, "format.");
    if (const auto* stream = audioStream(track)) add(stream->tags, "audio.");
    return result;
}

std::string tag(const MediaTrack& track, std::initializer_list<const char*> names)
{
    const auto tags = currentTags(track);
    for (const auto* name : names) {
        for (const auto* scope : { "format.", "audio." }) {
            for (const auto& [key, value] : tags) {
                if (key.starts_with(scope) && tagKey(std::string_view(key).substr(
                        std::string_view(scope).size())) == name && !value.empty()) return value;
            }
        }
    }
    return {};
}

std::string join(const std::set<std::string>& values)
{
    std::string result;
    for (const auto& value : values) {
        if (!result.empty()) result += " | ";
        result += value;
    }
    return result.empty() ? "unknown" : result;
}

std::string seconds(double value)
{
    std::ostringstream text;
    text << std::fixed << std::setprecision(3) << value;
    return text.str();
}

std::optional<double> duration(const MediaTrack& track)
{
    const auto value = track.formatInfo.format.duration;
    return value && std::isfinite(*value) && *value > 0 ? value : std::nullopt;
}

std::string durationText(const MediaTrack& track)
{
    const auto value = duration(track);
    return value ? seconds(*value) + " s" : "unknown";
}

template<typename T>
std::string positive(const std::optional<T>& value, const char* unit = "")
{
    return value && *value > 0 ? std::to_string(*value) + unit : "unknown";
}

std::string codec(const MediaTrack& track)
{
    const auto* stream = audioStream(track);
    if (stream && stream->codec_name && !stream->codec_name->empty()) return *stream->codec_name;
    return track.formatInfo.format.audio_codec_name.value_or("unknown");
}

std::string encoding(const MediaTrack& track)
{
    const auto name = codec(track);
    if (name == "flac" || name == "alac" || name.starts_with("pcm_")) return "lossless format";
    if (name == "mp3" || name == "aac" || name == "opus" || name == "vorbis" ||
        name == "ac3" || name == "eac3" || name == "mp2") return "lossy format";
    return "encoding type unknown";
}

using Properties = std::map<std::string, std::string>;

std::optional<std::int64_t> reportedBitrate(const MediaTrack& track)
{
    const auto* stream = audioStream(track);
    const auto value = stream && stream->bit_rate && *stream->bit_rate > 0 ?
        stream->bit_rate : track.formatInfo.format.bit_rate;
    return value && *value > 0 ? value : std::nullopt;
}

Properties properties(const MediaTrack& track)
{
    const auto* stream = audioStream(track);
    const auto rate = stream && stream->sample_rate && *stream->sample_rate != "0" &&
        !stream->sample_rate->empty() ? *stream->sample_rate + " Hz" : "unknown";
    // Decoder sample formats (for example fltp for MP3) are not source bit depth.
    return {
        { "codec", codec(track) },
        { "sample rate", rate },
        { "bit depth", stream ? positive(stream->bits_per_sample, "-bit") : "unknown" },
        { "channels", stream ? positive(stream->channels) : "unknown" },
        { "channel layout", stream ? stream->channel_layout.value_or("unknown") : "unknown" },
        { "bitrate (reported)", positive(reportedBitrate(track), " bit/s") },
    };
}

std::string audioDescription(const MediaTrack& track, bool includeBitrate = true)
{
    const auto values = properties(track);
    return values.at("codec") + " (" + encoding(track) + "), " + values.at("sample rate") + ", " +
        values.at("bit depth") + ", " + values.at("channels") + " channel(s), " +
        values.at("channel layout") + (includeBitrate ? ", " + values.at("bitrate (reported)") : "");
}

void validateSnapshot(const MediaAlbum& album, const DuplicateComparison::AlbumSnapshot& snapshot)
{
    if (snapshot.tracks.size() != album.trackList.size()) {
        throw std::invalid_argument("Duplicate comparison snapshot does not match the scanned track list.");
    }
}

std::size_t readable(const DuplicateComparison::AlbumSnapshot& snapshot)
{
    return static_cast<std::size_t>(std::ranges::count_if(snapshot.tracks, [](const auto& track) {
        return track.has_value();
    }));
}

void printTagDifferences(std::ostream& output, std::size_t firstNumber, const MediaTrack& first,
    std::size_t secondNumber, const MediaTrack& second)
{
    const auto left = currentTags(first);
    const auto right = currentTags(second);
    std::set<std::string> keys;
    for (const auto& [key, value] : left) keys.insert(key);
    for (const auto& [key, value] : right) keys.insert(key);
    std::size_t same = 0, differences = 0;
    for (const auto& key : keys) {
        const auto a = left.find(key);
        const auto b = right.find(key);
        if (a != left.end() && b != right.end() && a->second == b->second) ++same;
        else ++differences;
    }
    output << "    Current tags: " << same << " equal, " << differences << " different/missing.\n";
    if (keys.empty()) output << "      No readable tags on either side; tag equality is unknown.\n";
    for (const auto& key : keys) {
        const auto a = left.find(key);
        const auto b = right.find(key);
        if (a != left.end() && b != right.end() && a->second == b->second) continue;
        printValue(output, "      ", key);
        printValue(output, "        [" + std::to_string(firstNumber) + "] ",
            a == left.end() ? "<missing>" : a->second);
        printValue(output, "        [" + std::to_string(secondNumber) + "] ",
            b == right.end() ? "<missing>" : b->second);
    }
}

std::string trackLabel(const MediaTrack& track)
{
    const auto title = tag(track, { "title" });
    const auto artist = tag(track, { "artist", "artists" });
    const auto number = tag(track, { "tracknumber", "track" });
    const auto disc = tag(track, { "discnumber", "disc" });
    return CommonUtils::path_to_utf8(track.trackPath) + " | title: " + (title.empty() ? "unknown" : title) +
        " | artist: " + (artist.empty() ? "unknown" : artist) +
        " | disc/track: " + (disc.empty() ? "?" : disc) + "/" + (number.empty() ? "?" : number);
}

std::string storedHashRelation(const MediaTrack& first, const MediaTrack& second, bool decodedPcm)
{
    if (!first.catalogRecord || !second.catalogRecord) return "unavailable";
    const auto& left = decodedPcm ? first.catalogRecord->integrity.pcmSha256 : first.catalogRecord->flacMd5;
    const auto& right = decodedPcm ? second.catalogRecord->integrity.pcmSha256 : second.catalogRecord->flacMd5;
    if (!left || left->empty() || !right || right->empty()) return "unavailable";
    return *left == *right ? "equal" : "different";
}
}

std::string DuplicateComparison::DescribeAlbum(
    std::size_t number, const MediaAlbum& album, const AlbumSnapshot& snapshot)
{
    validateSnapshot(album, snapshot);
    std::ostringstream output;
    printValue(output, "  Album [" + std::to_string(number) + "]: ", CommonUtils::path_to_utf8(album.path.path()));
    output << "    Tracks: " << album.trackList.size() << " scanned; " << snapshot.currentCount
        << " currently present; " << readable(snapshot) << " scanned paths readable.\n";
    const auto field = [&](const char* label, std::initializer_list<const char*> names) {
        std::set<std::string> values;
        std::size_t present = 0;
        for (const auto& track : snapshot.tracks) {
            if (track) {
                const auto value = tag(*track, names);
                if (!value.empty()) { values.insert(value); ++present; }
            }
        }
        printValue(output, std::string("    ") + label + ": ", join(values) +
            " (tagged " + std::to_string(present) + "/" + std::to_string(readable(snapshot)) +
            " readable tracks" + (values.size() > 1 ? "; MULTIPLE VALUES" : "") + ")");
    };
    field("Album", { "album" });
    field("Album artist", { "albumartist", "albumartists" });
    field("Track artists", { "artist", "artists" });
    field("Release date", { "date", "year" });
    field("Original date", { "originaldate", "originalyear" });
    field("Edition/version", { "edition", "version", "albumversion" });
    field("Label", { "label", "publisher", "organization" });
    field("Catalog number", { "catalognumber", "catalog" });
    field("MusicBrainz release ID", { "musicbrainzalbumid", "musicbrainzreleaseid" });
    std::map<std::string, std::size_t> formats;
    double totalDuration = 0;
    std::size_t knownDurations = 0, multipleStreams = 0, knownBitrates = 0;
    std::set<std::int64_t> bitrates;
    std::uintmax_t totalBytes = 0;
    for (const auto& track : snapshot.tracks) {
        if (!track) continue;
        ++formats[audioDescription(*track, false)];
        if (const auto bitrate = reportedBitrate(*track)) {
            bitrates.insert(*bitrate);
            ++knownBitrates;
        }
        totalBytes += track->fs_fileSize;
        if (const auto value = duration(*track)) {
            totalDuration += *value;
            ++knownDurations;
        }
        if (std::ranges::count_if(track->formatInfo.streams, [](const Stream& stream) {
                return stream.codec_type == "audio";
            }) > 1) ++multipleStreams;
    }
    output << "    Current readable scanned tracks: " << totalBytes << " bytes; duration "
        << (knownDurations ? seconds(totalDuration) + " s" : "unknown") << " across "
        << knownDurations << "/" << album.trackList.size()
        << " scanned tracks (not a total for added/missing files).\n";
    for (const auto& [format, count] : formats) {
        printValue(output, "    Audio (" + std::to_string(count) + " track(s)): ", format);
    }
    if (!bitrates.empty()) output << "    Reported bitrate range: " << *bitrates.begin() << "-"
        << *bitrates.rbegin() << " bit/s across " << knownBitrates << "/" << album.trackList.size()
        << " scanned tracks.\n";
    if (multipleStreams) output << "    WARNING: " << multipleStreams
        << " track(s) have multiple audio streams; technical/tag comparison uses the first audio stream.\n";
    std::size_t artworkTracks = 0, catalogTracks = 0, failedIntegrity = 0, findings = 0;
    std::map<std::string, std::size_t> integrityStatuses;
    std::set<std::string> pictures;
    for (const auto& track : album.trackList) {
        if (!track.catalogRecord) continue;
        const auto& record = *track.catalogRecord;
        ++catalogTracks;
        if (!record.pictures.empty()) ++artworkTracks;
        for (const auto& picture : record.pictures) {
            pictures.insert(positive(picture.width) + "x" + positive(picture.height) + ", " +
                picture.mime.value_or("unknown MIME") + ", " + std::to_string(picture.sizeBytes) + " bytes");
        }
        if (record.integrity.decodeErrors || record.integrity.status == "failed" ||
            record.integrity.flacMd5Matches == false) ++failedIntegrity;
        ++integrityStatuses[record.integrity.status];
        findings += record.findings.size();
    }
    output << "    Scan-only inventory: embedded artwork on " << artworkTracks << "/" << catalogTracks
        << " catalog tracks; integrity failed/error flags on " << failedIntegrity
        << " track(s); scan findings " << findings << ". Legacy tracks without inventory: "
        << album.trackList.size() - catalogTracks << ".\n";
    if (!catalogTracks) output << "      Scan inventory unavailable, not evidence of absent artwork or intact audio.\n";
    for (const auto& [status, count] : integrityStatuses) {
        printValue(output, "      Recorded integrity status: ", status + "=" + std::to_string(count));
    }
    for (const auto& picture : pictures) printValue(output, "      Recorded artwork: ", picture);
    for (const auto& track : album.trackList) {
        if (!track.catalogRecord) continue;
        const auto& record = *track.catalogRecord;
        if (record.integrity.error) {
            printValue(output, "      Recorded integrity error: ",
                CommonUtils::path_to_utf8(track.trackPath) + ": " + *record.integrity.error);
        }
        for (const auto& finding : record.findings) {
            printValue(output, "      Recorded scan finding: ", CommonUtils::path_to_utf8(track.trackPath) +
                ": " + finding.severity + " " + finding.code + " - " + finding.evidence);
        }
    }
    output << "    Detected since scan: " << snapshot.changedTracks.size() << " changed (size/time), "
        << album.trackList.size() - readable(snapshot) << " missing, " << snapshot.addedTracks.size()
        << " added/uncompared.\n";
    for (const auto& name : snapshot.addedTracks) {
        printValue(output, "      Added/uncompared: ", CommonUtils::path_to_utf8(name));
    }
    if (!snapshot.otherEntries.empty()) {
        output << "    Other immediate files/folders NOT compared (whole-folder moves include these):\n";
        for (const auto& name : snapshot.otherEntries) {
            printValue(output, "      ", CommonUtils::path_to_utf8(name));
        }
    }
    return output.str();
}

std::string DuplicateComparison::ConfidenceBanner(DuplicateMatcher::Confidence confidence, bool useColor)
{
    using DuplicateMatcher::Confidence;
    const char* meaning = "";
    const char* color = "";
    switch (confidence) {
    case Confidence::Exact:
        meaning = "Every track paired by equal stored FLAC MD5 (scan-time; not byte-identical files).";
        color = "42";
        break;
    case Confidence::Strong:
        meaning = "All tracks paired; MusicBrainz release or album name + artists agree; durations known.";
        color = "46";
        break;
    case Confidence::Probable:
        meaning = "All tracks paired, but identity evidence is weaker or incomplete.";
        color = "43";
        break;
    case Confidence::Partial:
        meaning = "Only some tracks paired (coverage threshold met); likely a different or incomplete edition.";
        color = "45";
        break;
    }
    std::string scale;
    for (const auto level : { Confidence::Exact, Confidence::Strong, Confidence::Probable, Confidence::Partial }) {
        if (!scale.empty()) scale += level == Confidence::Partial ? "; " : " > ";
        std::string name = DuplicateMatcher::ConfidenceName(level);
        if (level == confidence) {
            std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            name = "[" + name + "]";
        }
        scale += name;
        if (level == Confidence::Partial) scale += " (coverage-only)";
    }
    std::string label = DuplicateMatcher::ConfidenceName(confidence);
    std::ranges::transform(label, label.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    label = " CONFIDENCE: " + label + " ";
    const std::string border(76, '#');
    const std::string open = useColor ? std::string("\x1b[0;30;") + color + "m" : "";
    const std::string close = useColor ? "\x1b[0m" : "";
    std::ostringstream output;
    output << "  " << border << '\n'
        << "  ##" << open << label << close << "  scale: " << scale << '\n'
        << "  ##  " << meaning << '\n'
        << "  ##  Scan-time rule-based label, not a probability or proof of identical audio.\n"
        << "  " << border << '\n';
    return output.str();
}

std::string DuplicateComparison::DescribePair(std::size_t firstNumber, std::size_t secondNumber,
    const MediaAlbum& first, const AlbumSnapshot& firstSnapshot,
    const MediaAlbum& second, const AlbumSnapshot& secondSnapshot,
    const DuplicateMatcher::PairEvaluation& evaluation, const DuplicateMatcher::Options& options,
    bool includeTracks, bool useColor)
{
    validateSnapshot(first, firstSnapshot);
    validateSnapshot(second, secondSnapshot);
    std::ostringstream output;
    output << '\n' << ConfidenceBanner(evaluation.confidence, useColor);
    output << "  Pair [" << firstNumber << "] <-> [" << secondNumber << "]: "
        << DuplicateMatcher::ConfidenceName(evaluation.confidence) << " candidate (SCAN-TIME evidence)\n";
    printValue(output, "    [" + std::to_string(firstNumber) + "] ", CommonUtils::path_to_utf8(first.path.path()));
    printValue(output, "    [" + std::to_string(secondNumber) + "] ", CommonUtils::path_to_utf8(second.path.path()));
    for (const auto& evidence : evaluation.evidence) printValue(output, "    Why flagged: ", evidence);
    std::map<std::string, std::size_t> methods;
    std::size_t available = 0, equalMd5 = 0, differentMd5 = 0, equalPcm = 0, differentPcm = 0;
    std::size_t outsideTolerance = 0, unknownDuration = 0;
    for (const auto& pair : evaluation.trackPairs) {
        ++methods[pair.method];
        const auto& a = firstSnapshot.tracks.at(pair.first);
        const auto& b = secondSnapshot.tracks.at(pair.second);
        if (a && b) {
            ++available;
            const auto left = duration(*a), right = duration(*b);
            if (!left || !right) ++unknownDuration;
            else if (std::abs(*left - *right) > DuplicateMatcher::DurationTolerance(*left, *right, options)) {
                ++outsideTolerance;
            }
        }
        const auto md5 = storedHashRelation(first.trackList.at(pair.first), second.trackList.at(pair.second), false);
        const auto pcm = storedHashRelation(first.trackList.at(pair.first), second.trackList.at(pair.second), true);
        if (md5 == "equal") ++equalMd5;
        else if (md5 == "different") ++differentMd5;
        if (pcm == "equal") ++equalPcm;
        else if (pcm == "different") ++differentPcm;
    }
    output << "    Coverage: " << evaluation.trackPairs.size() << "/" << first.trackList.size()
        << " scanned tracks in [" << firstNumber << "], " << evaluation.trackPairs.size() << "/"
        << second.trackList.size() << " in [" << secondNumber << "]; currently readable pairs "
        << available << "/" << evaluation.trackPairs.size() << ".\n";
    output << "    Match methods:";
    for (const auto& [method, count] : methods) output << ' ' << method << '=' << count;
    output << "\n    Stored FLAC MD5: equal " << equalMd5 << ", different " << differentMd5
        << ", unavailable " << evaluation.trackPairs.size() - equalMd5 - differentMd5
        << ". Not decoded or verified in this review.\n";
    if (equalPcm || differentPcm) output << "    Recorded decoded-PCM SHA-256: equal " << equalPcm
        << ", different " << differentPcm << ". Scan-only; not recomputed now.\n";
    if (differentMd5 || differentPcm) output << "    CAUTION: stored audio hashes differ; possible different "
        "mastering, edits or resolution. Do not treat this as identical audio.\n";
    if (evaluation.confidence == DuplicateMatcher::Confidence::Exact) {
        output << "    Exact means all paired stored FLAC MD5s matched, NOT byte-identical files or "
            "a fresh audio verification.\n";
    }
    else output << "    Metadata/duration similarity is not proof of identical recordings.\n";
    if (outsideTolerance || unknownDuration) output << "    CAUTION: current paired durations outside tolerance "
        << outsideTolerance << ", unknown " << unknownDuration << ".\n";
    if (!firstSnapshot.changedTracks.empty() || !secondSnapshot.changedTracks.empty() ||
        readable(firstSnapshot) != first.trackList.size() || readable(secondSnapshot) != second.trackList.size() ||
        !firstSnapshot.addedTracks.empty() || !secondSnapshot.addedTracks.empty()) {
        output << "    WARNING: files changed, disappeared or were added since scanning. Confidence and "
            "pairing still refer to the scan; rescan before relying on them.\n";
    }
    if (!evaluation.unmatchedFirst.empty() || !evaluation.unmatchedSecond.empty()) {
        output << "    CAUTION: unmatched/bonus tracks exist; one album may be a different or incomplete edition.\n";
    }
    if (!includeTracks) return output.str();
    output << "    Matched-track details (current first audio stream; full differing tag values):\n";
    std::size_t position = 0;
    for (const auto& pair : evaluation.trackPairs) {
        ++position;
        const auto& a = firstSnapshot.tracks.at(pair.first);
        const auto& b = secondSnapshot.tracks.at(pair.second);
        output << "\n    Track pair " << position << " | matched by " << pair.method << '\n';
        printValue(output, "      [" + std::to_string(firstNumber) + "] ",
            (a ? "" : "MISSING (scan-time tags): ") + trackLabel(a ? *a : first.trackList.at(pair.first)));
        printValue(output, "      [" + std::to_string(secondNumber) + "] ",
            (b ? "" : "MISSING (scan-time tags): ") + trackLabel(b ? *b : second.trackList.at(pair.second)));
        output << "      Stored hashes (scan-only): FLAC MD5 "
            << storedHashRelation(first.trackList.at(pair.first), second.trackList.at(pair.second), false)
            << "; decoded PCM SHA-256 "
            << storedHashRelation(first.trackList.at(pair.first), second.trackList.at(pair.second), true) << ".\n";
        if (!a || !b) {
            output << "      Track missing in one of the albums. Current comparison unavailable.\n";
            continue;
        }
        output << "      Comparing scanned tracks: '"
            << safeText(CommonUtils::path_to_utf8(first.path.path() / a->trackPath)) << "' and '"
            << safeText(CommonUtils::path_to_utf8(second.path.path() / b->trackPath)) << "'.\n";
        const auto leftDuration = duration(*a), rightDuration = duration(*b);
        output << "      Duration [" << firstNumber << "]/[" << secondNumber << "]: "
            << durationText(*a) << " / " << durationText(*b);
        if (leftDuration && rightDuration) {
            const auto difference = std::abs(*leftDuration - *rightDuration);
            const auto tolerance = DuplicateMatcher::DurationTolerance(*leftDuration, *rightDuration, options);
            output << "; delta " << seconds(difference) << " s; tolerance " << seconds(tolerance)
                << " s (" << (difference <= tolerance ? "within tolerance" : "OUTSIDE tolerance") << ')';
        }
        output << '\n';
        output << "      File bytes [" << firstNumber << "]/[" << secondNumber << "]: "
            << a->fs_fileSize << " / " << b->fs_fileSize << " (size is not an audio-quality score).\n";
        printValue(output, "      Audio [" + std::to_string(firstNumber) + "]: ", audioDescription(*a));
        printValue(output, "      Audio [" + std::to_string(secondNumber) + "]: ", audioDescription(*b));
        const auto leftProperties = properties(*a), rightProperties = properties(*b);
        std::size_t different = 0, unknown = 0;
        output << "      Technical differences:";
        for (const auto& [key, value] : leftProperties) {
            const auto& other = rightProperties.at(key);
            if (value == "unknown" || other == "unknown") { ++unknown; continue; }
            if (value != other) { output << ' ' << key << ';'; ++different; }
        }
        if (!different) output << " none among known fields;";
        output << " unknown on one/both sides: " << unknown << ".\n";
        printTagDifferences(output, firstNumber, *a, secondNumber, *b);
    }
    const auto unmatched = [&](std::size_t number, const MediaAlbum& album, const std::vector<std::size_t>& tracks) {
        for (const auto index : tracks) {
            const auto& track = album.trackList.at(index);
            printValue(output, "    Unmatched scanned track [" + std::to_string(number) + "]: ",
                trackLabel(track) + " | scanned duration: " + durationText(track));
        }
    };
    unmatched(firstNumber, first, evaluation.unmatchedFirst);
    unmatched(secondNumber, second, evaluation.unmatchedSecond);
    return output.str();
}
