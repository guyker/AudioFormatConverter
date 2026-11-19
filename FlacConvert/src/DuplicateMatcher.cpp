#include "DuplicateMatcher.h"

#include "CommonUtils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace fs = std::filesystem;

namespace
{
using TagValues = std::map<std::string, std::vector<std::string>>;

// Base letters for U+0100..U+017F (Latin Extended-A), in code point order.
constexpr char kLatinExtendedA[] =
    "aaaaaa" "cccccccc" "dddd" "eeeeeeeeee" "gggggggg" "hhhh" "iiiiiiiiii" "ii" "jj" "kkk"
    "llllllllll" "nnnnnnnnn" "oooooo" "oo" "rrrrrr" "ssssssss" "tttttt" "uuuuuuuuuuuu" "ww" "yyy"
    "zzzzzz" "s";
static_assert(sizeof(kLatinExtendedA) - 1 == 0x80, "Latin Extended-A fold table must cover 128 code points");

void appendUtf8(std::string& output, char32_t codePoint)
{
    if (codePoint < 0x80) {
        output += static_cast<char>(codePoint);
    }
    else if (codePoint < 0x800) {
        output += static_cast<char>(0xC0 | (codePoint >> 6));
        output += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
    else if (codePoint < 0x10000) {
        output += static_cast<char>(0xE0 | (codePoint >> 12));
        output += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        output += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
    else {
        output += static_cast<char>(0xF0 | (codePoint >> 18));
        output += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
        output += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        output += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
}

// Decodes one UTF-8 sequence; invalid input yields U+FFFD and advances one byte.
char32_t decodeUtf8(std::string_view text, std::size_t& position)
{
    const auto lead = static_cast<unsigned char>(text[position]);
    std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 :
        (lead >> 3) == 0x1E ? 4 : 0;
    if (length == 0 || position + length > text.size()) {
        ++position;
        return 0xFFFD;
    }
    char32_t codePoint = length == 1 ? lead : lead & (0x7F >> length);
    for (std::size_t i = 1; i < length; ++i) {
        const auto next = static_cast<unsigned char>(text[position + i]);
        if ((next & 0xC0) != 0x80) {
            ++position;
            return 0xFFFD;
        }
        codePoint = (codePoint << 6) | (next & 0x3F);
    }
    position += length;
    return codePoint;
}

const char* foldLatin1(char32_t codePoint)
{
    switch (codePoint) {
    case 0xC6: case 0xE6: return "ae";
    case 0xDE: case 0xFE: return "th";
    case 0xDF: return "ss";
    case 0xD0: case 0xF0: return "d";
    case 0xD1: case 0xF1: return "n";
    case 0xC7: case 0xE7: return "c";
    case 0xD8: case 0xF8: return "o";
    case 0xDD: case 0xFD: case 0xFF: return "y";
    default: break;
    }
    const auto lower = codePoint >= 0xE0 ? codePoint - 0x20 : codePoint;
    if (lower >= 0xC0 && lower <= 0xC5) return "a";
    if (lower >= 0xC8 && lower <= 0xCB) return "e";
    if (lower >= 0xCC && lower <= 0xCF) return "i";
    if (lower >= 0xD2 && lower <= 0xD6) return "o";
    if (lower >= 0xD9 && lower <= 0xDC) return "u";
    return nullptr;
}

bool isSeparator(char32_t codePoint)
{
    return (codePoint < 0x80 && !std::isalnum(static_cast<unsigned char>(codePoint))) ||
        (codePoint >= 0xA0 && codePoint <= 0xBF) || codePoint == 0xD7 || codePoint == 0xF7 ||
        (codePoint >= 0x2000 && codePoint <= 0x206F) || (codePoint >= 0x3000 && codePoint <= 0x303F) ||
        (codePoint >= 0xFF01 && codePoint <= 0xFF0F) || codePoint == 0xFFFD;
}

std::string foldText(std::string_view text)
{
    std::string output;
    output.reserve(text.size());
    bool pendingSpace = false;
    const auto emit = [&](std::string_view value) {
        if (pendingSpace && !output.empty()) output += ' ';
        pendingSpace = false;
        output += value;
    };
    for (std::size_t position = 0; position < text.size();) {
        auto codePoint = decodeUtf8(text, position);
        if (codePoint >= 0x300 && codePoint <= 0x36F) continue; // combining marks (NFD accents)
        if (codePoint == '&') {
            pendingSpace = true;
            emit("and");
            pendingSpace = true;
            continue;
        }
        if (isSeparator(codePoint)) {
            pendingSpace = true;
            continue;
        }
        if (codePoint < 0x80) {
            const char value = static_cast<char>(std::tolower(static_cast<unsigned char>(codePoint)));
            emit(std::string_view(&value, 1));
            continue;
        }
        if (codePoint >= 0xC0 && codePoint <= 0xFF) {
            if (const auto* folded = foldLatin1(codePoint)) {
                emit(folded);
                continue;
            }
        }
        if (codePoint >= 0x100 && codePoint <= 0x17F) {
            if (codePoint == 0x152 || codePoint == 0x153) emit("oe");
            else if (codePoint == 0x132 || codePoint == 0x133) emit("ij");
            else emit(std::string_view(&kLatinExtendedA[codePoint - 0x100], 1));
            continue;
        }
        if (codePoint >= 0x391 && codePoint <= 0x3A9 && codePoint != 0x3A2) codePoint += 0x20; // Greek
        else if (codePoint == 0x3C2) codePoint = 0x3C3; // final sigma
        else if (codePoint >= 0x410 && codePoint <= 0x42F) codePoint += 0x20; // Cyrillic
        else if (codePoint >= 0x400 && codePoint <= 0x40F) codePoint += 0x50;
        std::string encoded;
        appendUtf8(encoded, codePoint);
        emit(encoded);
    }
    return output;
}

std::vector<std::string> words(const std::string& folded)
{
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start < folded.size()) {
        const auto end = folded.find(' ', start);
        result.push_back(folded.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

bool isEditionText(std::string_view text)
{
    static const std::set<std::string> keywords{ "deluxe", "edition", "editions", "expanded",
        "anniversary", "bonus", "version", "versions", "reissue", "special", "collector", "collectors",
        "limited" };
    bool edition = false;
    for (const auto& word : words(foldText(text))) {
        if (word == "cd" || word == "disc" || word == "disk") return false;
        edition = edition || keywords.contains(word) || word.rfind("remaster", 0) == 0;
    }
    return edition;
}

std::string stripEditions(std::string_view text)
{
    std::string result;
    std::size_t position = 0;
    while (position < text.size()) {
        const auto open = text.find_first_of("([", position);
        if (open == std::string_view::npos) {
            result += text.substr(position);
            break;
        }
        const char closeCharacter = text[open] == '(' ? ')' : ']';
        const auto close = text.find(closeCharacter, open + 1);
        if (close == std::string_view::npos) {
            result += text.substr(position);
            break;
        }
        result += text.substr(position, open - position);
        if (!isEditionText(text.substr(open + 1, close - open - 1))) {
            result += text.substr(open, close - open + 1);
        }
        position = close + 1;
    }
    const auto dash = result.rfind(" - ");
    if (dash != std::string::npos && isEditionText(std::string_view(result).substr(dash + 3))) {
        result.erase(dash);
    }
    return result;
}

std::string canonicalKey(std::string_view key)
{
    std::string result;
    for (const unsigned char character : key) {
        if (std::isalnum(character)) result += static_cast<char>(std::tolower(character));
    }
    return result;
}

// Tag view keyed by lowercase alphanumeric names, covering catalog records and legacy projections.
TagValues trackTags(const MediaTrack& track)
{
    TagValues tags;
    if (track.catalogRecord) {
        for (const auto& [key, values] : track.catalogRecord->tags) {
            auto& destination = tags[canonicalKey(key)];
            destination.insert(destination.end(), values.begin(), values.end());
        }
        return tags;
    }
    if (!track.formatInfo.format.tags) return tags;
    for (const auto& [key, value] : *track.formatInfo.format.tags) {
        auto& destination = tags[canonicalKey(key)];
        std::size_t start = 0;
        while (start <= value.size()) {
            const auto end = value.find(';', start);
            destination.push_back(value.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    return tags;
}

const std::vector<std::string>* findTag(const TagValues& tags, std::initializer_list<const char*> keys)
{
    for (const auto* key : keys) {
        const auto found = tags.find(key);
        if (found != tags.end() && std::ranges::any_of(found->second, [](const std::string& value) {
                return !foldText(value).empty();
            })) {
            return &found->second;
        }
    }
    return nullptr;
}

std::string firstValue(const TagValues& tags, std::initializer_list<const char*> keys)
{
    const auto* values = findTag(tags, keys);
    if (!values) return {};
    for (const auto& value : *values) {
        if (!foldText(value).empty()) return value;
    }
    return {};
}

std::optional<int> leadingInteger(const std::string& value)
{
    std::size_t position = 0;
    while (position < value.size() && std::isspace(static_cast<unsigned char>(value[position]))) ++position;
    int result = 0;
    std::size_t digits = 0;
    while (position < value.size() && std::isdigit(static_cast<unsigned char>(value[position])) && digits < 6) {
        result = result * 10 + (value[position] - '0');
        ++position;
        ++digits;
    }
    return digits == 0 ? std::nullopt : std::optional<int>(result);
}

std::string folderName(const MediaAlbum& album)
{
    auto path = album.path.path();
    if (path.filename().empty()) path = path.parent_path();
    std::string name = CommonUtils::path_to_utf8(path.filename());
    // "1999 - Title" folder naming: the year is not part of the album identity.
    if (name.size() > 7 && std::isdigit(static_cast<unsigned char>(name[0])) &&
        std::isdigit(static_cast<unsigned char>(name[1])) && std::isdigit(static_cast<unsigned char>(name[2])) &&
        std::isdigit(static_cast<unsigned char>(name[3]))) {
        auto position = name.find_first_not_of(' ', 4);
        if (position != std::string::npos && (name[position] == '-' || name[position] == '.')) {
            position = name.find_first_not_of(' ', position + 1);
            if (position != std::string::npos) name.erase(0, position);
        }
    }
    return name;
}

std::vector<std::string> sortedUnique(std::vector<std::string> values)
{
    std::erase_if(values, [](const std::string& value) { return value.empty(); });
    std::ranges::sort(values);
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

bool knownDuration(const DuplicateMatcher::TrackProfile& track)
{
    return track.durationSeconds.has_value();
}

std::size_t levenshtein(const std::string& first, const std::string& second)
{
    std::vector<std::size_t> previous(second.size() + 1), current(second.size() + 1);
    std::iota(previous.begin(), previous.end(), std::size_t{ 0 });
    for (std::size_t i = 1; i <= first.size(); ++i) {
        current[0] = i;
        for (std::size_t j = 1; j <= second.size(); ++j) {
            current[j] = (std::min)({ previous[j] + 1, current[j - 1] + 1,
                previous[j - 1] + (first[i - 1] == second[j - 1] ? 0 : 1) });
        }
        std::swap(previous, current);
    }
    return previous[second.size()];
}

std::uint64_t albumBytes(const MediaAlbum& album)
{
    std::uint64_t total = 0;
    for (const auto& track : album.trackList) {
        total += track.catalogRecord ? track.catalogRecord->sizeBytes : track.fs_fileSize;
    }
    return total;
}

struct UnionFind
{
    std::vector<std::size_t> parent;
    explicit UnionFind(std::size_t size) : parent(size) { std::iota(parent.begin(), parent.end(), std::size_t{ 0 }); }
    std::size_t Find(std::size_t value)
    {
        while (parent[value] != value) value = parent[value] = parent[parent[value]];
        return value;
    }
    void Unite(std::size_t first, std::size_t second)
    {
        first = Find(first);
        second = Find(second);
        if (first != second) parent[(std::max)(first, second)] = (std::min)(first, second);
    }
};
}

const char* DuplicateMatcher::ConfidenceName(Confidence confidence)
{
    switch (confidence) {
    case Confidence::Exact: return "exact";
    case Confidence::Strong: return "strong";
    case Confidence::Probable: return "probable";
    case Confidence::Partial: return "partial";
    }
    throw std::logic_error("Invalid duplicate confidence");
}

std::string DuplicateMatcher::NormalizeText(std::string_view text, bool stripEditionText)
{
    return stripEditionText ? foldText(stripEditions(text)) : foldText(text);
}

double DuplicateMatcher::DurationTolerance(double first, double second, const Options& options)
{
    return (std::max)(options.durationToleranceSeconds,
        options.durationTolerancePercent / 100.0 * (std::max)(first, second));
}

DuplicateMatcher::AlbumProfile DuplicateMatcher::BuildProfile(const MediaAlbum& album, const Options& options)
{
    AlbumProfile profile;
    std::map<std::string, std::pair<std::size_t, std::size_t>> albumNames; // name -> {count, first track}
    std::vector<std::string> albumArtists, trackArtists;
    std::set<std::string> musicBrainzIds;
    for (std::size_t index = 0; index < album.trackList.size(); ++index) {
        const auto& track = album.trackList[index];
        const auto tags = trackTags(track);
        if (const auto name = NormalizeText(firstValue(tags, { "album" }), options.stripEditionText); !name.empty()) {
            auto [entry, inserted] = albumNames.try_emplace(name, 0, index);
            ++entry->second.first;
        }
        for (const auto& [keys, destination] : {
                std::pair{ std::initializer_list<const char*>{ "albumartists", "albumartist" }, &albumArtists },
                std::pair{ std::initializer_list<const char*>{ "artists", "artist" }, &trackArtists } }) {
            if (const auto* values = findTag(tags, keys)) {
                for (const auto& value : *values) destination->push_back(NormalizeText(value));
            }
        }
        if (const auto id = canonicalKey(firstValue(tags, { "musicbrainzalbumid", "musicbrainzreleaseid" }));
            !id.empty()) {
            musicBrainzIds.insert(id);
        }

        TrackProfile trackProfile;
        trackProfile.index = index;
        const auto duration = track.catalogRecord ? track.catalogRecord->durationSeconds :
            track.formatInfo.format.duration;
        if (duration && std::isfinite(*duration) && *duration > 0) trackProfile.durationSeconds = duration;
        trackProfile.track = leadingInteger(firstValue(tags, { "tracknumber", "track" }));
        trackProfile.disc = leadingInteger(firstValue(tags, { "discnumber", "disc" }));
        trackProfile.title = NormalizeText(firstValue(tags, { "title" }));
        if (track.catalogRecord && track.catalogRecord->flacMd5) trackProfile.flacMd5 = *track.catalogRecord->flacMd5;
        profile.tracks.push_back(std::move(trackProfile));
    }
    const auto best = std::ranges::max_element(albumNames, [](const auto& left, const auto& right) {
        return left.second.first != right.second.first ? left.second.first < right.second.first :
            left.second.second > right.second.second;
    });
    if (best != albumNames.end()) {
        profile.name = best->first;
    }
    else {
        profile.name = NormalizeText(folderName(album), options.stripEditionText);
        profile.nameFromFolder = true;
    }
    profile.albumArtists = sortedUnique(std::move(albumArtists));
    profile.trackArtists = sortedUnique(std::move(trackArtists));
    if (musicBrainzIds.size() == 1) profile.musicBrainzId = *musicBrainzIds.begin();
    return profile;
}

DuplicateMatcher::ArtistMatch DuplicateMatcher::CompareArtists(const AlbumProfile& first, const AlbumProfile& second)
{
    if ((first.albumArtists.empty() && first.trackArtists.empty()) ||
        (second.albumArtists.empty() && second.trackArtists.empty())) {
        return ArtistMatch::Unknown;
    }
    for (const auto* left : { &first.albumArtists, &first.trackArtists }) {
        for (const auto* right : { &second.albumArtists, &second.trackArtists }) {
            if (!left->empty() && *left == *right) return ArtistMatch::Match;
        }
    }
    return ArtistMatch::Mismatch;
}

bool DuplicateMatcher::FuzzyNameMatch(const std::string& first, const std::string& second)
{
    if (first.empty() || second.empty()) return false;
    if (first == second) return true;
    const auto longest = (std::max)(first.size(), second.size());
    if ((std::min)(first.size(), second.size()) < 4) return false;
    return 1.0 - static_cast<double>(levenshtein(first, second)) / static_cast<double>(longest) >= 0.85;
}

DuplicateMatcher::PairEvaluation DuplicateMatcher::EvaluatePair(
    const AlbumProfile& first, const AlbumProfile& second, const Options& options)
{
    PairEvaluation result;
    const auto& tracksA = first.tracks;
    const auto& tracksB = second.tracks;
    if (tracksA.empty() || tracksB.empty()) return result;

    std::vector<bool> usedA(tracksA.size(), false), usedB(tracksB.size(), false);
    const auto compatible = [&](const TrackProfile& left, const TrackProfile& right) {
        if (!knownDuration(left) || !knownDuration(right)) return true;
        return std::abs(*left.durationSeconds - *right.durationSeconds) <=
            DurationTolerance(*left.durationSeconds, *right.durationSeconds, options);
    };
    const auto addPair = [&](std::size_t i, std::size_t j, const char* method) {
        usedA[i] = usedB[j] = true;
        result.trackPairs.push_back({ tracksA[i].index, tracksB[j].index, method });
    };

    for (std::size_t i = 0; i < tracksA.size(); ++i) {
        if (tracksA[i].flacMd5.empty()) continue;
        for (std::size_t j = 0; j < tracksB.size(); ++j) {
            if (!usedB[j] && tracksB[j].flacMd5 == tracksA[i].flacMd5) {
                addPair(i, j, "flac-md5");
                break;
            }
        }
    }
    const auto pairByUniqueKey = [&](auto keyOf, const char* method) {
        std::map<decltype(keyOf(tracksA[0])), std::vector<std::size_t>> left, right;
        for (std::size_t i = 0; i < tracksA.size(); ++i) {
            if (!usedA[i]) if (auto key = keyOf(tracksA[i])) left[*key].push_back(i);
        }
        for (std::size_t j = 0; j < tracksB.size(); ++j) {
            if (!usedB[j]) if (auto key = keyOf(tracksB[j])) right[*key].push_back(j);
        }
        for (const auto& [key, indexes] : left) {
            const auto other = right.find(key);
            if (indexes.size() == 1 && other != right.end() && other->second.size() == 1 &&
                compatible(tracksA[indexes.front()], tracksB[other->second.front()])) {
                addPair(indexes.front(), other->second.front(), method);
            }
        }
    };
    pairByUniqueKey([](const TrackProfile& track) {
        return track.track ? std::optional<std::pair<int, int>>({ track.disc.value_or(1), *track.track }) :
            std::nullopt;
    }, "track-number");
    pairByUniqueKey([](const TrackProfile& track) {
        return track.title.empty() ? std::nullopt : std::optional<std::string>(track.title);
    }, "title");

    struct Candidate { double difference; std::size_t i, j; };
    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < tracksA.size(); ++i) {
        if (usedA[i] || !knownDuration(tracksA[i])) continue;
        for (std::size_t j = 0; j < tracksB.size(); ++j) {
            if (usedB[j] || !knownDuration(tracksB[j])) continue;
            const auto difference = std::abs(*tracksA[i].durationSeconds - *tracksB[j].durationSeconds);
            if (difference <= DurationTolerance(*tracksA[i].durationSeconds, *tracksB[j].durationSeconds, options)) {
                candidates.push_back({ difference, i, j });
            }
        }
    }
    std::ranges::stable_sort(candidates, {}, &Candidate::difference);
    for (const auto& candidate : candidates) {
        if (!usedA[candidate.i] && !usedB[candidate.j]) addPair(candidate.i, candidate.j, "duration");
    }
    for (std::size_t i = 0, j = 0; i < tracksA.size() && j < tracksB.size();) {
        if (usedA[i] || knownDuration(tracksA[i])) { ++i; continue; }
        if (usedB[j] || knownDuration(tracksB[j])) { ++j; continue; }
        addPair(i++, j++, "unknown-duration");
    }

    std::ranges::sort(result.trackPairs, {}, &TrackPair::first);
    for (std::size_t i = 0; i < tracksA.size(); ++i) if (!usedA[i]) result.unmatchedFirst.push_back(tracksA[i].index);
    for (std::size_t j = 0; j < tracksB.size(); ++j) if (!usedB[j]) result.unmatchedSecond.push_back(tracksB[j].index);

    const auto matched = result.trackPairs.size();
    if (matched == 0 || matched < options.minMatchingTracks) return result;

    std::size_t md5Pairs = 0;
    bool durationsUnknown = false;
    for (const auto& pair : result.trackPairs) {
        if (std::string_view(pair.method) == "flac-md5") {
            ++md5Pairs;
            continue;
        }
        const auto& left = *std::ranges::find(tracksA, pair.first, &TrackProfile::index);
        const auto& right = *std::ranges::find(tracksB, pair.second, &TrackProfile::index);
        durationsUnknown = durationsUnknown || !knownDuration(left) || !knownDuration(right);
    }
    const bool allMatched = tracksA.size() == tracksB.size() && result.unmatchedFirst.empty() &&
        result.unmatchedSecond.empty();
    const bool nameMatch = !first.name.empty() && first.name == second.name;
    const bool musicBrainzMatch = !first.musicBrainzId.empty() && first.musicBrainzId == second.musicBrainzId;
    const bool musicBrainzConflict = !first.musicBrainzId.empty() && !second.musicBrainzId.empty() &&
        !musicBrainzMatch;
    const auto artists = CompareArtists(first, second);
    const bool fuzzy = options.fuzzyNames && !nameMatch && artists == ArtistMatch::Match &&
        FuzzyNameMatch(first.name, second.name);

    if (!(nameMatch || musicBrainzMatch || md5Pairs != 0 || fuzzy)) return result;

    auto& evidence = result.evidence;
    if (nameMatch) evidence.push_back(first.nameFromFolder || second.nameFromFolder ?
        "same album name (folder name fallback)" : "same album name");
    if (fuzzy) evidence.push_back("similar album name");
    if (musicBrainzMatch) evidence.push_back("same MusicBrainz release ID");
    if (musicBrainzConflict) evidence.push_back("conflicting MusicBrainz release IDs");
    evidence.push_back(artists == ArtistMatch::Match ? "artists match" :
        artists == ArtistMatch::Unknown ? "artist unknown on at least one side" : "artists differ");
    evidence.push_back(std::to_string(matched) + "/" + std::to_string((std::max)(tracksA.size(), tracksB.size())) +
        " tracks matched");
    if (md5Pairs) evidence.push_back(std::to_string(md5Pairs) + " tracks with equal stored FLAC MD5");
    if (durationsUnknown) evidence.push_back("some track durations unknown");
    if (tracksA.size() != tracksB.size()) {
        evidence.push_back("track counts differ: " + std::to_string(tracksA.size()) + " vs " +
            std::to_string(tracksB.size()));
    }

    if (allMatched && md5Pairs == matched) {
        result.duplicate = true;
        result.confidence = Confidence::Exact;
        return result;
    }
    if (artists == ArtistMatch::Mismatch && !musicBrainzMatch && md5Pairs == 0) return result;
    if (allMatched) {
        result.duplicate = true;
        result.confidence = (musicBrainzMatch || (nameMatch && artists == ArtistMatch::Match)) &&
            !durationsUnknown && !musicBrainzConflict ? Confidence::Strong : Confidence::Probable;
        return result;
    }
    if (options.partialMatches) {
        const auto coverage = 100.0 * static_cast<double>(matched) /
            static_cast<double>((std::max)(tracksA.size(), tracksB.size()));
        if (coverage >= options.partialMinCoverage) {
            result.duplicate = true;
            result.confidence = Confidence::Partial;
        }
    }
    return result;
}

DuplicateMatcher::Result DuplicateMatcher::FindDuplicates(const std::vector<MediaAlbum>& albums,
    const std::vector<std::string>& identityKeys, const Options& options)
{
    if (!identityKeys.empty() && identityKeys.size() != albums.size()) {
        throw std::invalid_argument("Duplicate identity keys must match the album list.");
    }
    Result result;
    auto& stats = result.statistics;
    stats.loaded = albums.size();

    std::vector<bool> active(albums.size(), true);
    std::unordered_map<std::string, std::size_t> firstByKey;
    for (std::size_t index = 0; index < albums.size(); ++index) {
        if (!identityKeys.empty()) {
            const auto [found, inserted] = firstByKey.try_emplace(identityKeys[index], index);
            if (!inserted) {
                const auto& kept = albums[found->second];
                result.aliases.push_back({ index, found->second,
                    kept.trackList.size() != albums[index].trackList.size() ||
                        albumBytes(kept) != albumBytes(albums[index]) });
                active[index] = false;
                ++stats.aliasesExcluded;
                continue;
            }
        }
        if (albums[index].trackList.empty()) {
            active[index] = false;
            ++stats.emptyExcluded;
        }
    }

    std::vector<AlbumProfile> profiles(albums.size());
    std::unordered_map<std::string, std::vector<std::size_t>> byMd5, byMusicBrainz, byName, byArtist;
    for (std::size_t index = 0; index < albums.size(); ++index) {
        if (!active[index]) continue;
        ++stats.considered;
        profiles[index] = BuildProfile(albums[index], options);
        const auto& profile = profiles[index];
        std::set<std::string> md5s;
        for (const auto& track : profile.tracks) if (!track.flacMd5.empty()) md5s.insert(track.flacMd5);
        for (const auto& md5 : md5s) byMd5[md5].push_back(index);
        if (!profile.musicBrainzId.empty()) byMusicBrainz[profile.musicBrainzId].push_back(index);
        if (!profile.name.empty()) byName[profile.name].push_back(index);
        if (options.fuzzyNames) {
            std::set<std::string> artistKeys;
            for (const auto* artists : { &profile.albumArtists, &profile.trackArtists }) {
                if (artists->empty()) continue;
                std::string key;
                for (const auto& artist : *artists) key += std::to_string(artist.size()) + ":" + artist;
                artistKeys.insert(std::move(key));
            }
            for (const auto& key : artistKeys) byArtist[key].push_back(index);
        }
    }

    std::set<std::pair<std::size_t, std::size_t>> candidates;
    const auto addBucket = [&](const std::vector<std::size_t>& bucket) {
        for (std::size_t i = 0; i < bucket.size(); ++i) {
            for (std::size_t j = i + 1; j < bucket.size(); ++j) {
                if (bucket[i] != bucket[j]) {
                    candidates.emplace((std::min)(bucket[i], bucket[j]), (std::max)(bucket[i], bucket[j]));
                }
            }
        }
    };
    for (const auto& [md5, bucket] : byMd5) {
        if (bucket.size() > options.maxMd5BucketAlbums) {
            ++stats.skippedMd5Buckets;
            continue;
        }
        addBucket(bucket);
    }
    for (const auto* index : { &byMusicBrainz, &byName, &byArtist }) {
        for (const auto& [key, bucket] : *index) addBucket(bucket);
    }
    stats.candidatePairs = candidates.size();

    for (const auto& [first, second] : candidates) {
        auto evaluation = EvaluatePair(profiles[first], profiles[second], options);
        if (!evaluation.duplicate) continue;
        switch (evaluation.confidence) {
        case Confidence::Exact: ++stats.exact; break;
        case Confidence::Strong: ++stats.strong; break;
        case Confidence::Probable: ++stats.probable; break;
        case Confidence::Partial: ++stats.partial; break;
        }
        result.pairs.push_back({ first, second, std::move(evaluation) });
    }
    stats.reportedPairs = result.pairs.size();

    UnionFind sets(albums.size());
    for (const auto& pair : result.pairs) sets.Unite(pair.first, pair.second);
    std::map<std::size_t, std::size_t> groupByRoot;
    for (std::size_t pairIndex = 0; pairIndex < result.pairs.size(); ++pairIndex) {
        const auto root = sets.Find(result.pairs[pairIndex].first);
        const auto [found, inserted] = groupByRoot.try_emplace(root, result.groups.size());
        if (inserted) result.groups.emplace_back();
        auto& group = result.groups[found->second];
        group.pairs.push_back(pairIndex);
        group.albums.push_back(result.pairs[pairIndex].first);
        group.albums.push_back(result.pairs[pairIndex].second);
    }
    for (auto& group : result.groups) {
        std::ranges::sort(group.albums);
        group.albums.erase(std::unique(group.albums.begin(), group.albums.end()), group.albums.end());
    }
    std::ranges::sort(result.groups, {}, [](const DuplicateGroup& group) { return group.albums.front(); });
    stats.groups = result.groups.size();
    return result;
}
