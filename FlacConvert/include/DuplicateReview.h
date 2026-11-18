#pragma once

#include <filesystem>
#include <iosfwd>
#include <vector>

namespace DuplicateReview
{
    struct Options
    {
        std::filesystem::path outputRoot;
        std::vector<std::filesystem::path> libraryRoots;
    };

    enum class Action { Continue, Stop, Error };

    struct Result
    {
        Action action{ Action::Continue };
        int movedAlbum{ 0 };
        std::filesystem::path destination;
    };

    bool IsSameOrChildPath(const std::filesystem::path& candidate,
        const std::filesystem::path& base);

    Result ReviewPair(const std::filesystem::path& first, const std::filesystem::path& second,
        const std::vector<std::filesystem::path>& libraryRoots,
        const std::filesystem::path& outputRoot, bool openFolders,
        std::istream& input, std::ostream& output);
}
