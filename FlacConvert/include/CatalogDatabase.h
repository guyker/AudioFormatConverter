#pragma once

#include "MediaAlbum.h"

#include <filesystem>
#include <optional>

class CatalogDatabase
{
public:
    static bool Import(const DirectoryContentEntryList& albums,
        const std::filesystem::path& databasePath,
        const std::optional<std::filesystem::path>& libraryRoot = std::nullopt);
};
