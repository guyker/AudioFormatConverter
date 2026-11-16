#pragma once

#include <filesystem>
#include <future>

#include "MediaInformation.h"
#include "MediaTrack.h"

enum class SortOrder { Ascending, Descending };

// Template comparator
template <SortOrder Order>
struct SortByTracks {
    bool operator()(const auto& album1, const auto& album2) const {
        const auto& [albumName1, trackList1] = album1;
        const auto& [albumName2, trackList2] = album2;
        if constexpr (Order == SortOrder::Ascending) {
            return trackList1.size() < trackList2.size();
        }
        else {
            return trackList1.size() > trackList2.size();
        }
    }
};

struct MediaAlbum
{
	std::filesystem::directory_entry path;
	std::vector<MediaTrack> trackList;
};

using SimilarAlbumIndexPairs = std::vector<std::tuple<std::size_t, std::size_t>>;
using MediaLoadingFuture = std::future<MetadataReadResult>;
using TrackInfoList = std::vector<MediaTrack>;
using DirectoryContentEntryList = std::vector<MediaAlbum>;
using MediaAlbumListPtr = std::shared_ptr<std::vector<MediaAlbum>>;



