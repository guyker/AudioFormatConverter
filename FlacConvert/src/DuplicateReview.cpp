#include "DuplicateReview.h"

#include "CommonUtils.h"
#include "PlatformUtils.h"
#include "PreservationAudit.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/writer.h>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
fs::path ioPath(const fs::path& path)
{
#ifdef _WIN32
    auto value = PlatformUtils::WindowsLongPath(fs::absolute(path));
    if (value.rfind(L"\\\\?\\", 0) == 0) return fs::path(value);
    if (value.rfind(L"\\\\", 0) == 0) return fs::path(L"\\\\?\\UNC\\" + value.substr(2));
    return fs::path(L"\\\\?\\" + value);
#else
    return fs::absolute(path);
#endif
}

bool isLink(const fs::path& path)
{
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        throw fs::filesystem_error("Cannot inspect duplicate move entry", path,
            std::error_code(static_cast<int>(GetLastError()), std::system_category()));
    }
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return fs::is_symlink(fs::symlink_status(path));
#endif
}

struct Entry
{
    bool directory;
    std::uintmax_t size;
    fs::file_time_type modified;
    bool operator==(const Entry&) const = default;
};

using Inventory = std::map<fs::path, Entry>;

void validateEntryPath(const fs::path& root, const fs::path& relative)
{
    auto path = root;
    if (isLink(path)) throw std::runtime_error("Album path became a link or reparse point.");
    for (const auto& component : relative) {
        path /= component;
        if (isLink(path)) throw std::runtime_error("Album entry became a link or reparse point.");
    }
}

Inventory inventory(const fs::path& source)
{
    if (isLink(source) || !fs::is_directory(source)) {
        throw std::runtime_error("Album must be a real directory, not a link or reparse point.");
    }
    Inventory entries;
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        if (isLink(entry.path())) {
            throw std::runtime_error("Album contains a link or reparse point; move refused.");
        }
        const auto status = entry.symlink_status();
        const bool directory = fs::is_directory(status);
        if (!directory && !fs::is_regular_file(status)) {
            throw std::runtime_error("Album contains an unsupported filesystem entry; move refused.");
        }
        entries.emplace(entry.path().lexically_relative(source),
            Entry{ directory, directory ? 0 : entry.file_size(),
                directory ? fs::file_time_type{} : entry.last_write_time() });
    }
    return entries;
}

void verifyFile(const fs::path& source, const fs::path& target)
{
    if (isLink(source) || isLink(target) ||
        !fs::is_regular_file(source) || !fs::is_regular_file(target) ||
        fs::file_size(source) != fs::file_size(target)) {
        throw std::runtime_error("Duplicate move file verification failed.");
    }
    if (!PreservationAudit::VerifyCopy(source, target)) {
        throw std::runtime_error("Duplicate move file bytes do not match or could not be read.");
    }
}

void writeReceipt(const fs::path& directory, const fs::path& source,
    const fs::path& destination, const char* state)
{
    std::ofstream file(directory / "move.json", std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("Cannot write duplicate move receipt.");
    rapidjson::OStreamWrapper stream(file);
    rapidjson::Writer<rapidjson::OStreamWrapper> writer(stream);
    const auto sourceText = CommonUtils::path_to_utf8(source);
    const auto destinationText = CommonUtils::path_to_utf8(destination);
    writer.StartObject();
    writer.Key("source"); writer.String(sourceText.data(), static_cast<rapidjson::SizeType>(sourceText.size()));
    writer.Key("destination"); writer.String(destinationText.data(), static_cast<rapidjson::SizeType>(destinationText.size()));
    writer.Key("state"); writer.String(state);
    writer.EndObject();
    file.flush();
    file.close();
    if (!file) throw std::runtime_error("Cannot finish duplicate move receipt.");
}

void validateMove(const fs::path& source, const fs::path& keep,
    const std::vector<fs::path>& roots, const fs::path& outputRoot)
{
    const auto sourceKey = fs::canonical(source);
    const auto keepKey = fs::canonical(keep);
    const auto outputKey = fs::weakly_canonical(outputRoot);
    const auto deletedKey = fs::weakly_canonical(outputRoot / "deleted");
    if (DuplicateReview::IsSameOrChildPath(sourceKey, keepKey) ||
        DuplicateReview::IsSameOrChildPath(keepKey, sourceKey)) {
        throw std::runtime_error("Cannot move identical, aliased or nested duplicate album folders.");
    }
    bool insideLibrary = false;
    for (const auto& root : roots) {
        const auto rootKey = fs::weakly_canonical(root);
        if (DuplicateReview::IsSameOrChildPath(rootKey, sourceKey)) {
            throw std::runtime_error("Cannot move a library root or a folder containing a library root.");
        }
        insideLibrary = insideLibrary || DuplicateReview::IsSameOrChildPath(sourceKey, rootKey);
        if (DuplicateReview::IsSameOrChildPath(outputKey, rootKey) ||
            DuplicateReview::IsSameOrChildPath(rootKey, outputKey) ||
            DuplicateReview::IsSameOrChildPath(deletedKey, rootKey) ||
            DuplicateReview::IsSameOrChildPath(rootKey, deletedKey)) {
            throw std::runtime_error("Deleted output must not overlap any source library.");
        }
    }
    if (!insideLibrary) {
        throw std::runtime_error("Album is outside the known source libraries; move refused.");
    }
    if (isLink(source)) throw std::runtime_error("Cannot move an album link or reparse point.");
}

void moveAlbum(const fs::path& source, const fs::path& keep,
    const std::vector<fs::path>& roots, const fs::path& outputRoot, const fs::path& destination)
{
    validateMove(source, keep, roots, outputRoot);
    const auto originalKey = fs::canonical(source);
    const auto checkSource = [&] {
        if (isLink(source) || fs::canonical(source) != originalKey) {
            throw std::runtime_error("Album directory changed during source cleanup.");
        }
    };
    const auto before = inventory(source);
    PlatformUtils::ScopedCurrentDirectory anchor;
    if (!anchor.EnterValidatedDirectory(outputRoot / "deleted", source, true)) {
        throw std::runtime_error("Cannot anchor deleted output safely.");
    }
    const auto receiptDirectory = destination.parent_path();
    if (!fs::create_directory(receiptDirectory)) {
        throw std::runtime_error("Duplicate move destination already exists; nothing was overwritten.");
    }
    writeReceipt(receiptDirectory, source, destination, "copying");
    if (!fs::create_directory(destination)) {
        throw std::runtime_error("Duplicate move album destination already exists.");
    }
    for (const auto& [relative, entry] : before) {
        validateEntryPath(source, relative);
        validateEntryPath(destination, relative.parent_path());
        if (entry.directory) {
            if (!fs::create_directory(destination / relative)) {
                throw std::runtime_error("Duplicate move directory collision.");
            }
        }
        else {
            if (!fs::copy_file(source / relative, destination / relative, fs::copy_options::none)) {
                throw std::runtime_error("Duplicate move file collision.");
            }
            fs::last_write_time(destination / relative, entry.modified);
        }
    }
    const auto copied = inventory(destination);
    if (copied.size() != before.size()) {
        throw std::runtime_error("Duplicate move inventory verification failed.");
    }
    for (const auto& [relative, entry] : before) {
        const auto found = copied.find(relative);
        if (found == copied.end() || entry.directory != found->second.directory ||
            entry.size != found->second.size) {
            throw std::runtime_error("Duplicate move inventory verification failed.");
        }
        validateEntryPath(source, relative);
        validateEntryPath(destination, relative);
        if (!entry.directory) verifyFile(source / relative, destination / relative);
    }
    validateMove(source, keep, roots, outputRoot);
    if (fs::canonical(source) != originalKey)
        throw std::runtime_error("Album path changed while copying; originals were not removed.");
    if (inventory(source) != before)
        throw std::runtime_error("Album contents changed while copying; originals were not removed.");
    if (!anchor.IsAnchored(outputRoot / "deleted"))
        throw std::runtime_error("Deleted output anchor changed while copying; originals were not removed.");
    writeReceipt(receiptDirectory, source, destination, "verified");
    spdlog::info("Verified complete album copy at '{}'; removing originals from '{}'.",
        CommonUtils::path_to_utf8(destination), CommonUtils::path_to_utf8(source));
    for (const auto& [relative, entry] : before) {
        if (entry.directory) continue;
        checkSource();
        validateEntryPath(source, relative);
        validateEntryPath(destination, relative);
        verifyFile(source / relative, destination / relative);
        if (!fs::remove(source / relative)) {
            throw std::runtime_error("Cannot remove verified original file.");
        }
    }
    for (auto it = before.rbegin(); it != before.rend(); ++it) {
        if (it->second.directory) {
            checkSource();
            validateEntryPath(source, it->first);
            if (!fs::remove(source / it->first)) {
                throw std::runtime_error("Cannot remove emptied original directory.");
            }
        }
    }
    checkSource();
    if (!fs::remove(source)) throw std::runtime_error("Cannot remove emptied original album directory.");
    writeReceipt(receiptDirectory, source, destination, "completed");
}

std::string readResponse(std::istream& input)
{
    std::string response;
    if (!std::getline(input, response)) {
        if (input.eof()) return "STOP";
        throw std::runtime_error("Cannot read duplicate review choice.");
    }
    const auto first = response.find_first_not_of(" \t\r");
    const auto last = response.find_last_not_of(" \t\r");
    return first == std::string::npos ? std::string{} : response.substr(first, last - first + 1);
}

fs::path newDestination(const fs::path& root, const fs::path& source)
{
    static std::atomic_uint64_t sequence{ 0 };
    const auto receiptDirectory = root / "deleted" /
        ("album-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(sequence.fetch_add(1)));
    return receiptDirectory / source.filename();
}

std::string confirmMove(const fs::path& source, const fs::path& destination,
    std::istream& input, std::ostream& output)
{
    output << "\nMove entire folder (including artwork and subfolders):\n  From: "
        << CommonUtils::path_to_utf8(source) << "\n  To: "
        << CommonUtils::path_to_utf8(destination)
        << "\nThe complete copy is verified before removing originals. A cleanup failure can leave\n"
           "partial originals; the verified copy is retained. Catalog JSON/databases are not updated.\n"
           "Type MOVE to confirm, or press Enter to cancel: " << std::flush;
    return readResponse(input);
}
}

bool DuplicateReview::IsSameOrChildPath(const fs::path& candidate, const fs::path& base)
{
    auto left = candidate.begin();
    for (auto right = base.begin(); right != base.end(); ++right, ++left) {
        if (left == candidate.end()) return false;
#ifdef _WIN32
        if (CompareStringOrdinal(left->c_str(), static_cast<int>(left->native().size()),
                right->c_str(), static_cast<int>(right->native().size()), TRUE) != CSTR_EQUAL) return false;
#else
        if (*left != *right) return false;
#endif
    }
    return true;
}

DuplicateReview::Result DuplicateReview::ReviewPair(const fs::path& first, const fs::path& second,
    const std::vector<fs::path>& libraryRoots, const fs::path& outputRoot, bool openFolders,
    std::istream& input, std::ostream& output)
{
    Result result;
    try {
        const auto album1 = ioPath(first), album2 = ioPath(second), root = ioPath(outputRoot);
        std::vector<fs::path> roots;
        roots.reserve(libraryRoots.size());
        for (const auto& libraryRoot : libraryRoots) roots.push_back(ioPath(libraryRoot));
        if (openFolders) {
            PlatformUtils::OpenDirectory(album1);
            PlatformUtils::OpenDirectory(album2);
        }
        for (;;) {
            output << "\nDuplicate review:\n  1: " << CommonUtils::path_to_utf8(album1)
                << "\n  2: " << CommonUtils::path_to_utf8(album2)
                << "\nPress Enter to continue, STOP to finish";
            if (result.movedAlbum == 0) output << ", or 1/2 to move that entire album to deleted";
            output << ": " << std::flush;
            const auto response = readResponse(input);
            if (response.empty() || response == "CONTINUE") return result;
            if (response == "STOP") {
                result.action = Action::Stop;
                return result;
            }
            if (result.movedAlbum != 0 || (response != "1" && response != "2")) {
                output << "Invalid choice; no additional action was taken.\n";
                continue;
            }
            const int selected = response == "1" ? 1 : 2;
            const auto& source = selected == 1 ? album1 : album2;
            const auto& keep = selected == 1 ? album2 : album1;
            validateMove(source, keep, roots, root);
            result.destination = newDestination(root, source);
            const auto confirmation = confirmMove(source, result.destination, input, output);
            if (confirmation == "STOP") {
                result.action = Action::Stop;
                return result;
            }
            if (confirmation != "MOVE") {
                result.destination.clear();
                output << "Move cancelled; originals unchanged.\n";
                continue;
            }
            moveAlbum(source, keep, roots, root, result.destination);
            result.movedAlbum = selected;
            output << "Album moved to " << CommonUtils::path_to_utf8(result.destination)
                << "\nRun option 2 after review to refresh the catalog.\n";
        }
    }
    catch (const std::exception& error) {
        spdlog::error("Duplicate review failed: {}. Any move copy is retained at '{}'; inspect move.json "
            "before retrying. Source cleanup may be incomplete only after verification.",
            error.what(), CommonUtils::path_to_utf8(result.destination));
        result.action = Action::Error;
        return result;
    }
}

DuplicateReview::GroupResult DuplicateReview::ReviewGroup(const std::vector<fs::path>& albums,
    const std::vector<std::pair<std::size_t, std::size_t>>& directMatches,
    const std::vector<fs::path>& libraryRoots, const fs::path& outputRoot, bool openFolders,
    std::istream& input, std::ostream& output)
{
    GroupResult result;
    fs::path pendingDestination;
    try {
        std::vector<fs::path> paths;
        paths.reserve(albums.size());
        for (const auto& album : albums) paths.push_back(ioPath(album));
        for (const auto& [first, second] : directMatches) {
            if (first >= paths.size() || second >= paths.size() || first == second) {
                throw std::invalid_argument("Duplicate review contains an invalid direct-match pair.");
            }
        }
        const auto root = ioPath(outputRoot);
        std::vector<fs::path> roots;
        roots.reserve(libraryRoots.size());
        for (const auto& libraryRoot : libraryRoots) roots.push_back(ioPath(libraryRoot));
        if (openFolders) {
            for (const auto& path : paths) PlatformUtils::OpenDirectory(path);
        }
        std::vector<bool> moved(paths.size(), false);
        const auto remaining = [&] {
            return static_cast<std::size_t>(std::ranges::count(moved, false));
        };
        for (;;) {
            output << "\nDuplicate review:\n";
            output << "Direct matches:";
            if (directMatches.empty()) output << " none";
            for (const auto& [first, second] : directMatches) {
                output << ' ' << first + 1 << '-' << second + 1;
            }
            output << '\n';
            for (std::size_t index = 0; index < paths.size(); ++index) {
                output << "  " << index + 1 << ": " << CommonUtils::path_to_utf8(paths[index])
                    << (moved[index] ? "  [moved]" : "") << '\n';
            }
            const bool canMove = remaining() > 1;
            output << "Press Enter to continue, STOP to finish";
            if (canMove) output << ", or an album number to move that entire album to deleted";
            output << ": " << std::flush;
            const auto response = readResponse(input);
            if (response.empty() || response == "CONTINUE") return result;
            if (response == "STOP") {
                result.action = Action::Stop;
                return result;
            }
            std::size_t selected = 0;
            const bool numeric = !response.empty() && response.size() < 6 &&
                std::ranges::all_of(response, [](unsigned char c) { return std::isdigit(c) != 0; });
            if (numeric) selected = static_cast<std::size_t>(std::stoul(response));
            if (!canMove || selected == 0 || selected > paths.size() || moved[selected - 1]) {
                output << "Invalid choice; no additional action was taken.\n";
                continue;
            }
            const auto sourceIndex = selected - 1;
            if (remaining() > 2) {
                std::vector<bool> reachable(paths.size(), false);
                std::size_t firstRemaining = paths.size();
                for (std::size_t index = 0; index < paths.size(); ++index) {
                    if (index != sourceIndex && !moved[index]) {
                        firstRemaining = index;
                        break;
                    }
                }
                std::vector<std::size_t> pending{ firstRemaining };
                reachable[firstRemaining] = true;
                while (!pending.empty()) {
                    const auto current = pending.back();
                    pending.pop_back();
                    for (const auto& [first, second] : directMatches) {
                        const auto next = first == current ? second : second == current ? first : paths.size();
                        if (next < paths.size() && next != sourceIndex && !moved[next] && !reachable[next]) {
                            reachable[next] = true;
                            pending.push_back(next);
                        }
                    }
                }
                const auto connectedCount = static_cast<std::size_t>(std::ranges::count(reachable, true));
                if (connectedCount != remaining() - 1) {
                    output << "Cannot move this album: the remaining albums would split into groups without "
                        "direct matches.\n";
                    continue;
                }
            }
            const auto& source = paths[sourceIndex];
            std::optional<std::size_t> keepIndex;
            for (std::size_t index = 0; index < paths.size(); ++index) {
                if (index == sourceIndex || moved[index]) continue;
                validateMove(source, paths[index], roots, root);
                if (!keepIndex) keepIndex = index;
            }
            pendingDestination = newDestination(root, source);
            const auto confirmation = confirmMove(source, pendingDestination, input, output);
            if (confirmation == "STOP") {
                result.action = Action::Stop;
                return result;
            }
            if (confirmation != "MOVE") {
                pendingDestination.clear();
                output << "Move cancelled; originals unchanged.\n";
                continue;
            }
            moveAlbum(source, paths[*keepIndex], roots, root, pendingDestination);
            moved[sourceIndex] = true;
            result.movedAlbums.push_back(sourceIndex);
            result.destinations.push_back(pendingDestination);
            output << "Album moved to " << CommonUtils::path_to_utf8(pendingDestination)
                << "\nRun option 2 after review to refresh the catalog.\n";
            pendingDestination.clear();
        }
    }
    catch (const std::exception& error) {
        spdlog::error("Duplicate review failed: {}. Any move copy is retained at '{}'; inspect move.json "
            "before retrying. Source cleanup may be incomplete only after verification.",
            error.what(), CommonUtils::path_to_utf8(pendingDestination));
        result.action = Action::Error;
        return result;
    }
}
