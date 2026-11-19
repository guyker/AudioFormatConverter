#pragma once

#include "DuplicateMatcher.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace DuplicateComparison
{
    struct AlbumSnapshot
    {
        std::vector<std::optional<MediaTrack>> tracks; // indexed like the scanned track list
        std::size_t currentCount{ 0 };
        std::vector<std::filesystem::path> addedTracks;
        std::vector<std::filesystem::path> otherEntries;
        std::vector<std::size_t> changedTracks;
    };

    // Presentation only: snapshots are provided by the caller; no reads, matching or move decisions.
    std::string DescribeAlbum(std::size_t number, const MediaAlbum& album, const AlbumSnapshot& snapshot);
    // Plain-text confidence banner; ANSI colour is added only when useColor is true.
    std::string ConfidenceBanner(DuplicateMatcher::Confidence confidence, bool useColor);
    std::string DescribePair(std::size_t firstNumber, std::size_t secondNumber,
        const MediaAlbum& first, const AlbumSnapshot& firstSnapshot,
        const MediaAlbum& second, const AlbumSnapshot& secondSnapshot,
        const DuplicateMatcher::PairEvaluation& evaluation, const DuplicateMatcher::Options& options,
        bool includeTracks, bool useColor = false);
}
