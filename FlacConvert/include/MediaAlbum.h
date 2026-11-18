#pragma once

#include <filesystem>
#include <future>
#include <optional>
#include <string>

#include "MediaInformation.h"
#include "MediaTrack.h"

enum class SortOrder { Ascending, Descending };

// Template comparator
template <SortOrder Order>
struct SortByTracks {
    bool operator()(const auto& album1, const auto& album2) const {
        if constexpr (Order == SortOrder::Ascending) {
            return album1.trackList.size() < album2.trackList.size();
        }
        else {
            return album1.trackList.size() > album2.trackList.size();
        }
    }
};

struct MediaAlbum
{
	std::filesystem::directory_entry path;
	std::vector<MediaTrack> trackList;
    std::optional<std::size_t> fragmentIndex;
    std::optional<std::size_t> fragmentCount;
    std::optional<std::string> fragmentSet;
    std::optional<std::string> scanManifestId;
};

using SimilarAlbumIndexPairs = std::vector<std::tuple<std::size_t, std::size_t>>;
using MediaLoadingFuture = std::future<MetadataReadResult>;
using TrackInfoList = std::vector<MediaTrack>;
using DirectoryContentEntryList = std::vector<MediaAlbum>;
using MediaAlbumListPtr = std::shared_ptr<std::vector<MediaAlbum>>;



