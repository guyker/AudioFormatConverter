#include "DuplicateMatcher.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace DuplicateMatcher;

namespace
{
void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

struct TrackSpec
{
    std::string title;
    std::optional<double> duration;
    std::optional<int> number;
    std::string md5;
};

MediaAlbum album(const std::string& folder, const std::string& name, const std::string& artist,
    const std::vector<TrackSpec>& tracks, const std::string& musicBrainz = {})
{
    MediaAlbum result;
    result.path = fs::directory_entry(fs::path("library") / folder);
    for (const auto& spec : tracks) {
        MediaTrack track;
        track.trackPath = spec.title + ".flac";
        MediaRecord record;
        record.sizeBytes = 1000;
        record.durationSeconds = spec.duration;
        if (!spec.md5.empty()) record.flacMd5 = spec.md5;
        if (!name.empty()) record.tags["ALBUM"] = { name };
        if (!artist.empty()) record.tags["ARTIST"] = { artist };
        if (!spec.title.empty()) record.tags["TITLE"] = { spec.title };
        if (spec.number) record.tags["TRACKNUMBER"] = { std::to_string(*spec.number) };
        if (!musicBrainz.empty()) record.tags["MUSICBRAINZ_ALBUMID"] = { musicBrainz };
        track.catalogRecord = std::move(record);
        result.trackList.push_back(std::move(track));
    }
    return result;
}

std::vector<TrackSpec> standardTracks()
{
    return { { "One", 200.0, 1, {} }, { "Two", 180.0, 2, {} }, { "Three", 240.0, 3, {} } };
}

PairEvaluation evaluate(const MediaAlbum& first, const MediaAlbum& second, const Options& options = {})
{
    return EvaluatePair(BuildProfile(first, options), BuildProfile(second, options), options);
}

void normalization()
{
    // NFC "é" and NFD "e" + combining acute fold to the same key, as do case and punctuation.
    require(NormalizeText("Caf\xc3\xa9 Del Mar") == NormalizeText("cafe\xcc\x81  del-mar"),
        "Composed and decomposed accents did not normalize equally");
    require(NormalizeText("\xc5\x81\xc3\xb3" "d\xc5\xba") == NormalizeText("lodz"), "Latin Extended-A not folded");
    require(NormalizeText("Album (Deluxe Edition)", true) == NormalizeText("Album"),
        "Edition text not stripped");
    require(NormalizeText("Album (Deluxe Edition)") != NormalizeText("Album"),
        "Edition text stripped without the option");
}

void nameConsensus()
{
    auto first = album("a", "Best Album", "Artist", standardTracks());
    auto second = album("b", "Best Album", "Artist", standardTracks());
    first.trackList.front().catalogRecord->tags["ALBUM"] = { "Wrong Name" };
    const auto result = evaluate(first, second);
    require(result.duplicate && result.confidence == Confidence::Strong,
        "Most common album name did not win over an inconsistent first track");
}

void reorderedAndRepeatedTitles()
{
    auto first = album("a", "X", "Artist", { { "Intro", 60.0, 1, {} }, { "Intro", 90.0, 2, {} },
        { "Song", 200.0, 3, {} } });
    auto second = album("b", "X", "Artist", { { "Song", 200.5, 3, {} }, { "Intro", 90.4, 2, {} },
        { "Intro", 60.2, 1, {} } });
    const auto result = evaluate(first, second);
    require(result.duplicate && result.trackPairs.size() == 3 && result.unmatchedFirst.empty(),
        "Reordered tracks with repeated titles were not fully paired");
    for (const auto& pair : result.trackPairs) {
        require(first.trackList[pair.first].catalogRecord->tags["TRACKNUMBER"] ==
            second.trackList[pair.second].catalogRecord->tags["TRACKNUMBER"],
            "Tracks were paired across different track numbers");
    }
}

void durationsAndCounts()
{
    auto first = album("a", "X", "Artist", standardTracks());
    auto shifted = standardTracks();
    for (auto& track : shifted) { track.duration = *track.duration + 30.0; track.number.reset(); track.title.clear(); }
    require(!evaluate(first, album("b", "X", "Artist", shifted)).duplicate,
        "Albums with incompatible durations were reported");

    auto unknown = standardTracks();
    for (auto& track : unknown) track.duration.reset();
    const auto missing = evaluate(first, album("b", "X", "Artist", unknown));
    require(missing.duplicate && missing.confidence == Confidence::Probable,
        "Missing durations should give a probable, not strong, match");

    auto fewer = standardTracks();
    fewer.pop_back();
    const auto counts = album("b", "X", "Artist", fewer);
    require(!evaluate(first, counts).duplicate, "Unequal track counts matched without partial matching");
    Options partial;
    partial.partialMatches = true;
    partial.partialMinCoverage = 60;
    const auto partialResult = evaluate(first, counts, partial);
    require(partialResult.duplicate && partialResult.confidence == Confidence::Partial &&
        partialResult.unmatchedFirst.size() == 1, "Partial match not reported with coverage");
    partial.partialMinCoverage = 80;
    require(!evaluate(first, counts, partial).duplicate, "Partial match ignored the coverage threshold");
}

void maximumTrackPairing()
{
    const auto first = album("a", "Matching", "Artist", {
        { "First recording", 100.0, std::nullopt, {} },
        { "Second recording", 103.0, std::nullopt, {} } });
    const auto second = album("b", "Matching", "Artist", {
        { "Other one", 101.0, std::nullopt, {} },
        { "Other two", 98.0, std::nullopt, {} } });
    const auto result = evaluate(first, second);
    require(result.duplicate && result.trackPairs.size() == 2,
        "Greedy duration choice prevented a complete track pairing");
    const auto matchedSecondDuration = [&](std::size_t firstIndex) {
        const auto pair = std::ranges::find(result.trackPairs, firstIndex, &TrackPair::first);
        require(pair != result.trackPairs.end(), "A source track was left unmatched");
        return *second.trackList[pair->second].catalogRecord->durationSeconds;
    };
    require(matchedSecondDuration(0) == 98.0 && matchedSecondDuration(1) == 101.0,
        "Maximum-cardinality duration pairing chose the wrong assignment");
}

void identifiers()
{
    auto md5Tracks = standardTracks();
    for (std::size_t i = 0; i < md5Tracks.size(); ++i) md5Tracks[i].md5 = std::string(31, 'a') + char('0' + i);
    const auto exact = evaluate(album("a", "Name One", "", md5Tracks), album("b", "Other", "Someone", md5Tracks));
    require(exact.duplicate && exact.confidence == Confidence::Exact, "Equal FLAC MD5s not exact");

    const auto mb = evaluate(album("a", "Name One", "Artist", standardTracks(), "id-1"),
        album("b", "Different", "Artist", standardTracks(), "ID-1"));
    require(mb.duplicate && mb.confidence == Confidence::Strong, "Equal MusicBrainz release IDs not strong");

    const auto conflict = evaluate(album("a", "X", "Artist", standardTracks(), "id-1"),
        album("b", "X", "Artist", standardTracks(), "id-2"));
    require(conflict.duplicate && conflict.confidence == Confidence::Probable,
        "Conflicting MusicBrainz IDs were not downgraded");

    require(!evaluate(album("a", "Greatest Hits", "Artist A", standardTracks()),
        album("b", "Greatest Hits", "Artist B", standardTracks())).duplicate,
        "Same name with different artists was reported");
}

void fuzzyAndLegacy()
{
    const auto first = album("a", "The Wall", "Pink Floyd", standardTracks());
    const auto second = album("b", "The Wal", "Pink Floyd", standardTracks());
    require(!evaluate(first, second).duplicate, "Fuzzy match reported without the option");
    Options fuzzy;
    fuzzy.fuzzyNames = true;
    require(FindDuplicates({ first, second }, {}, fuzzy).pairs.size() == 1, "Fuzzy candidate not found");

    MediaAlbum legacy;
    legacy.path = fs::directory_entry(fs::path("library") / "legacy");
    for (const auto& spec : standardTracks()) {
        MediaTrack track;
        track.formatInfo.format.duration = spec.duration;
        JsonUtils::Tags tags;
        tags["album"] = "The Wall";
        tags["artist"] = "Pink Floyd";
        tags["title"] = spec.title;
        tags["track"] = std::to_string(*spec.number) + "/3";
        track.formatInfo.format.tags = tags;
        legacy.trackList.push_back(std::move(track));
    }
    const auto legacyResult = evaluate(first, legacy);
    require(legacyResult.duplicate && legacyResult.confidence == Confidence::Strong,
        "Legacy formatInfo records were not matched");
}

void aliasesAndGroups()
{
    const auto a = album("a", "X", "Artist", standardTracks());
    const auto b = album("b", "X", "Artist", standardTracks());
    const auto c = album("c", "X", "Artist", standardTracks());
    const auto result = FindDuplicates({ a, b, a, c }, { "a", "b", "a", "c" }, {});
    require(result.aliases.size() == 1 && result.aliases[0].excluded == 2 && result.aliases[0].kept == 0,
        "Repeated folder not excluded as alias");
    require(result.groups.size() == 1 && result.groups[0].albums == std::vector<std::size_t>{ 0, 1, 3 } &&
        result.pairs.size() == 3, "Three-way duplicate not grouped once");
    for (const auto& pair : result.pairs) {
        require(pair.first != 2 && pair.second != 2, "Alias was compared");
    }
    bool threw = false;
    try { FindDuplicates({ a, b }, { "a" }, {}); }
    catch (const std::invalid_argument&) { threw = true; }
    require(threw, "Mismatched identity keys accepted");
}

// Every pair accepted by brute force must be reported exactly once by indexed candidate search.
void oracle()
{
    std::vector<MediaAlbum> albums;
    const std::vector<std::string> names{ "Alpha", "Beta", "alpha", "Gamma" };
    const std::vector<std::string> artists{ "A", "B", "" };
    for (std::size_t i = 0; i < 24; ++i) {
        auto tracks = standardTracks();
        if (i % 5 == 0) tracks.pop_back();
        if (i % 7 == 0) for (auto& track : tracks) track.md5 = "md5-" + track.title;
        albums.push_back(album("album" + std::to_string(i), names[i % names.size()],
            artists[i % artists.size()], tracks, i % 6 == 0 ? "mb" : ""));
    }
    for (const bool partial : { false, true }) {
        Options options;
        options.partialMatches = partial;
        options.partialMinCoverage = 60;
        const auto result = FindDuplicates(albums, {}, options);
        std::set<std::pair<std::size_t, std::size_t>> found;
        for (const auto& pair : result.pairs) {
            require(pair.first < pair.second, "Pair not ordered");
            require(found.emplace(pair.first, pair.second).second, "Pair reported twice");
        }
        std::set<std::pair<std::size_t, std::size_t>> expected;
        for (std::size_t i = 0; i < albums.size(); ++i) {
            for (std::size_t j = i + 1; j < albums.size(); ++j) {
                if (evaluate(albums[i], albums[j], options).duplicate) expected.emplace(i, j);
            }
        }
        require(found == expected, "Indexed candidate search differs from brute force");
        std::size_t grouped = 0;
        for (const auto& group : result.groups) grouped += group.pairs.size();
        require(grouped == result.pairs.size(), "Not every pair belongs to exactly one group");
    }
}
}

int main()
{
    try {
        normalization();
        nameConsensus();
        reorderedAndRepeatedTitles();
        durationsAndCounts();
        maximumTrackPairing();
        identifiers();
        fuzzyAndLegacy();
        aliasesAndGroups();
        oracle();
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
