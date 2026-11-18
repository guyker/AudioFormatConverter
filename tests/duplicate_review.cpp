#include "DuplicateReview.h"
#include "CommonUtils.h"
#include "PlatformUtils.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void write(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << content;
    file.close();
    require(static_cast<bool>(file), "Could not write fixture");
}

std::string read(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    require(static_cast<bool>(file), "Could not read fixture");
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

struct Fixture
{
    fs::path root;
    fs::path library, first, second, output;

    explicit Fixture(bool longPath = false)
    {
        root = fs::temp_directory_path() / ("afc-duplicate-review-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
#ifdef _WIN32
        if (longPath) root = fs::path(L"\\\\?\\" + fs::absolute(root).native());
#endif
        library = root / "library";
        first = library / CommonUtils::path_from_utf8("album-\xc3\xa9");
        second = library / "second";
        output = root / "out";
        if (longPath) output /= fs::path(std::string(100, 'a')) / std::string(100, 'b');
        write(first / "track.flac", std::string(150000, 'x'));
        write(first / "artwork" / "cover.jpg", "picture");
        write(first / "playlist.m3u", "track.flac");
        fs::create_directories(first / "empty");
        write(second / "track.flac", "keep");
    }

    ~Fixture()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
        if (ec) std::cerr << "Fixture cleanup failed: " << ec.message() << '\n';
    }

    DuplicateReview::Result review(const std::string& choices,
        const std::vector<fs::path>& roots = {})
    {
        std::istringstream input(choices);
        std::ostringstream messages;
        return DuplicateReview::ReviewPair(first, second,
            roots.empty() ? std::vector<fs::path>{ library } : roots,
            output, false, input, messages);
    }
};

void promptChoices()
{
    Fixture fixture;
    require(fixture.review("\n").action == DuplicateReview::Action::Continue,
        "Enter did not continue review");
    require(fixture.review("STOP\n").action == DuplicateReview::Action::Stop,
        "STOP did not stop review");
    require(fixture.review("").action == DuplicateReview::Action::Stop,
        "EOF did not stop review safely");
    require(fixture.review("invalid\nSTOP\n").action == DuplicateReview::Action::Stop,
        "Invalid input advanced without a valid choice");
    require(fixture.review("1\n\nSTOP\n").movedAlbum == 0,
        "Cancelled move was executed");
    require(fixture.review("2\n").action == DuplicateReview::Action::Stop,
        "EOF at MOVE confirmation was not safe");
    require(fs::exists(fixture.first / "track.flac") && fs::exists(fixture.second / "track.flac"),
        "Review or cancellation changed an album");
    require(!fs::exists(fixture.output), "Read-only review or cancellation created output");
}

void moveAlbum(int choice, bool longPath = false)
{
    Fixture fixture(longPath);
    const auto source = choice == 1 ? fixture.first : fixture.second;
    const auto keep = choice == 1 ? fixture.second : fixture.first;
    const auto trackBytes = read(source / "track.flac");
    const auto keepBytes = read(keep / "track.flac");
    write(fixture.output / "deleted" / "unrelated.txt", "unrelated");
    const auto result = fixture.review(std::to_string(choice) + "\nMOVE\n\n");
    require(result.action == DuplicateReview::Action::Continue && result.movedAlbum == choice,
        "Confirmed move failed or did not pause afterward");
    require(!fs::exists(source), "Moved source directory still exists");
    require(read(result.destination / "track.flac") == trackBytes, "Moved track changed");
    require(read(keep / "track.flac") == keepBytes, "Kept album changed");
    require(read(fixture.output / "deleted" / "unrelated.txt") == "unrelated",
        "Unrelated deleted output changed");
    require(read(result.destination.parent_path() / "move.json").find("\"completed\"") != std::string::npos,
        "Completed move receipt missing");
    if (choice == 1) {
        require(read(result.destination / "artwork" / "cover.jpg") == "picture", "Artwork was not moved");
        require(read(result.destination / "playlist.m3u") == "track.flac", "Playlist was not moved");
        require(fs::is_directory(result.destination / "empty"), "Empty subdirectory was not moved");
    }
    const auto secondMove = DuplicateReview::ReviewPair;
    std::istringstream input("1\nMOVE\nSTOP\n");
    std::ostringstream messages;
    const auto repeat = secondMove(keep, result.destination, { fixture.library },
        fixture.output, false, input, messages);
    require(repeat.movedAlbum == 1 && repeat.action == DuplicateReview::Action::Stop,
        "Second move or stop-after-move failed");
    require(repeat.destination != result.destination && fs::exists(result.destination),
        "Repeated album name overwrote a prior move");
}

void unsafeMoves()
{
    {
        Fixture fixture;
        const auto result = fixture.review("1\nMOVE\n", { fixture.first, fixture.library });
        require(result.action == DuplicateReview::Action::Error, "Library root could be moved");
        require(fs::exists(fixture.first / "track.flac"), "Refused root move changed source");
    }
    {
        Fixture fixture;
        fixture.second = fixture.first / "nested";
        write(fixture.second / "track.flac", "nested album");
        require(fixture.review("1\nMOVE\n").action == DuplicateReview::Action::Error,
            "Nested kept album could be moved");
        require(fs::exists(fixture.second / "track.flac"), "Nested kept album changed");
    }
    {
        Fixture fixture;
        fixture.output = fixture.library / "out";
        require(fixture.review("1\nMOVE\n").action == DuplicateReview::Action::Error,
            "Deleted output inside source library accepted");
        require(fs::exists(fixture.first / "track.flac"), "Unsafe output changed source");
    }
    {
        Fixture fixture;
        std::error_code ec;
        fs::create_directory_symlink(fixture.second, fixture.first / "linked", ec);
        if (!ec) {
            require(fixture.review("1\nMOVE\n").action == DuplicateReview::Action::Error,
                "Album containing a directory link could be moved");
            require(fs::exists(fixture.first / "track.flac") &&
                read(fixture.second / "track.flac") == "keep", "Link refusal changed source");
        }
#ifndef _WIN32
        else throw std::runtime_error("Could not create symlink fixture");
#endif
    }
    {
        Fixture fixture;
        fs::create_directories(fixture.output);
        std::error_code ec;
        fs::create_directory_symlink(fixture.second, fixture.output / "deleted", ec);
        if (!ec) {
            require(fixture.review("1\nMOVE\n").action == DuplicateReview::Action::Error,
                "Deleted output alias into the kept library accepted");
            require(read(fixture.first / "track.flac") == std::string(150000, 'x') &&
                read(fixture.second / "track.flac") == "keep",
                "Unsafe deleted output alias changed an album");
            require(std::distance(fs::directory_iterator(fixture.second), fs::directory_iterator{}) == 1,
                "Unsafe deleted output alias wrote into the kept album");
        }
#ifndef _WIN32
        else throw std::runtime_error("Could not create output symlink fixture");
#endif
    }
}

void relativePaths()
{
    Fixture fixture;
    const auto previous = fs::current_path();
    fixture.output = "out";
    fs::current_path(fixture.root);
    const auto result = fixture.review("1\nMOVE\n\n", { fs::path("library") });
    const bool restored = fs::equivalent(fs::current_path(), fixture.root);
    fs::current_path(previous);
    require(restored, "Move did not restore its working directory");
    require(result.action == DuplicateReview::Action::Continue && result.movedAlbum == 1,
        "Relative configured paths changed meaning while output was anchored");
    require(read(result.destination / "track.flac") == std::string(150000, 'x'),
        "Relative output did not preserve the album");
}

void movedPathContainment()
{
    Fixture fixture;
    const auto moved = fs::weakly_canonical(fixture.first);
    require(DuplicateReview::IsSameOrChildPath(fs::weakly_canonical(fixture.first), moved),
        "Moved album was not excluded from later pairs");
    require(DuplicateReview::IsSameOrChildPath(fs::weakly_canonical(fixture.first / "artwork"), moved),
        "Moved descendant was not excluded from later pairs");
    require(!DuplicateReview::IsSameOrChildPath(fs::weakly_canonical(fixture.second), moved) &&
        !DuplicateReview::IsSameOrChildPath(fs::weakly_canonical(fixture.library), moved) &&
        !DuplicateReview::IsSameOrChildPath(fs::path(moved.native() + fs::path("-other").native()), moved),
        "Moving an album incorrectly excluded other albums");
}

#ifdef _WIN32
void failedCopyAndCleanup()
{
    for (const DWORD share : { DWORD(0), DWORD(FILE_SHARE_READ | FILE_SHARE_WRITE) }) {
        Fixture fixture;
        const auto source = share == 0 ? fixture.first / "artwork" / "cover.jpg" :
            fixture.first / "track.flac";
        const auto original = read(source);
        const auto handle = CreateFileW(source.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, 0, nullptr);
        require(handle != INVALID_HANDLE_VALUE, "Could not lock source fixture");
        const auto result = fixture.review("1\nMOVE\n");
        CloseHandle(handle);
        require(result.action == DuplicateReview::Action::Error, "Locked source move unexpectedly succeeded");
        require(read(source) == original, "Locked original changed");
        if (share != 0) {
            require(!fs::exists(fixture.first / "artwork" / "cover.jpg"),
                "Fixture did not exercise a partially completed source cleanup");
            require(read(result.destination / "artwork" / "cover.jpg") == "picture",
                "Cleanup failure lost already removed artwork");
            require(read(result.destination / "track.flac") == std::string(150000, 'x'),
                "Cleanup failure did not retain complete verified copy");
            require(read(result.destination.parent_path() / "move.json").find("\"verified\"") != std::string::npos,
                "Cleanup failure lost verified receipt");
        }
        else {
            require(read(fixture.first / "track.flac") == std::string(150000, 'x'),
                "Copy failure removed an original before verification");
        }
    }
}
#endif
}

int main()
{
    try {
        promptChoices();
        moveAlbum(1);
        moveAlbum(2);
        moveAlbum(1, true);
        unsafeMoves();
        relativePaths();
        movedPathContainment();
#ifdef _WIN32
        failedCopyAndCleanup();
#endif
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
