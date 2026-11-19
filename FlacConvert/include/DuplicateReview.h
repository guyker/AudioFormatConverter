#pragma once

#include <filesystem>
#include <iosfwd>
#include <cstddef>
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

    struct GroupResult
    {
        Action action{ Action::Continue };
        std::vector<std::size_t> movedAlbums; // indexes into the reviewed album list
        std::vector<std::filesystem::path> destinations;
    };

    // Reviews a group of two or more duplicate albums. Albums can be moved one at a time, each with an
    // explicit MOVE confirmation; the last remaining album can never be moved.
    GroupResult ReviewGroup(const std::vector<std::filesystem::path>& albums,
        const std::vector<std::filesystem::path>& libraryRoots,
        const std::filesystem::path& outputRoot, bool openFolders,
        std::istream& input, std::ostream& output);
}
