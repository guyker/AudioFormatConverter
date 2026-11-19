#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "MediaAlbum.h"

// Duplicate-album detection over reassembled catalog albums. Pure in-memory logic: no filesystem
// access, no settings lookup and no logging, so callers and tests control every input.
namespace DuplicateMatcher
{
    struct Options
    {
        double durationTolerancePercent{ 1.0 };
        double durationToleranceSeconds{ 2.0 };
        std::size_t minMatchingTracks{ 1 };
        bool fuzzyNames{ false };
        bool stripEditionText{ false };
        bool partialMatches{ false };
        double partialMinCoverage{ 80.0 };
        // Buckets of shared FLAC MD5s larger than this (for example a common silent track) are not
        // expanded into candidate pairs; name and MusicBrainz evidence still apply.
        std::size_t maxMd5BucketAlbums{ 64 };
    };

    enum class Confidence { Exact, Strong, Probable, Partial };
    const char* ConfidenceName(Confidence confidence);

    enum class ArtistMatch { Match, Unknown, Mismatch };

    struct TrackProfile
    {
        std::size_t index{ 0 };
        std::optional<double> durationSeconds;
        std::optional<int> disc;
        std::optional<int> track;
        std::string title;
        std::string flacMd5;
    };

    struct AlbumProfile
    {
        std::string name;
        bool nameFromFolder{ false };
        std::vector<std::string> albumArtists;
        std::vector<std::string> trackArtists;
        std::string musicBrainzId;
        std::vector<TrackProfile> tracks;
    };

    struct TrackPair
    {
        std::size_t first{ 0 };
        std::size_t second{ 0 };
        const char* method{ "" };
    };

    struct PairEvaluation
    {
        bool duplicate{ false };
        Confidence confidence{ Confidence::Probable };
        std::vector<std::string> evidence;
        std::vector<TrackPair> trackPairs;
        std::vector<std::size_t> unmatchedFirst;
        std::vector<std::size_t> unmatchedSecond;
    };

    struct DuplicatePair
    {
        std::size_t first{ 0 };
        std::size_t second{ 0 };
        PairEvaluation evaluation;
    };

    struct DuplicateGroup
    {
        std::vector<std::size_t> albums;
        std::vector<std::size_t> pairs;
    };

    struct Alias
    {
        std::size_t excluded{ 0 };
        std::size_t kept{ 0 };
        bool contentDiffers{ false };
    };

    struct Statistics
    {
        std::size_t loaded{ 0 };
        std::size_t aliasesExcluded{ 0 };
        std::size_t emptyExcluded{ 0 };
        std::size_t considered{ 0 };
        std::size_t candidatePairs{ 0 };
        std::size_t reportedPairs{ 0 };
        std::size_t exact{ 0 };
        std::size_t strong{ 0 };
        std::size_t probable{ 0 };
        std::size_t partial{ 0 };
        std::size_t groups{ 0 };
        std::size_t skippedMd5Buckets{ 0 };
    };

    struct Result
    {
        std::vector<DuplicatePair> pairs;
        std::vector<DuplicateGroup> groups;
        std::vector<Alias> aliases;
        Statistics statistics;
    };

    std::string NormalizeText(std::string_view text, bool stripEditionText = false);
    AlbumProfile BuildProfile(const MediaAlbum& album, const Options& options);
    double DurationTolerance(double first, double second, const Options& options);
    ArtistMatch CompareArtists(const AlbumProfile& first, const AlbumProfile& second);
    bool FuzzyNameMatch(const std::string& first, const std::string& second);

    // Complete decision for one pair; FindDuplicates reports exactly the candidate pairs it accepts.
    PairEvaluation EvaluatePair(const AlbumProfile& first, const AlbumProfile& second, const Options& options);

    // identityKeys: one entry per album identifying the physical folder (for example a canonical path);
    // albums sharing a key are aliases and only the first is compared. Empty disables alias handling.
    Result FindDuplicates(const std::vector<MediaAlbum>& albums,
        const std::vector<std::string>& identityKeys, const Options& options);
}
