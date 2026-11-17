
#include "PlatformUtils.h"




#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <conio.h>
#else
#include <codecvt>
#include <locale>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif
#include <cerrno>
#include <cstdio>
#include <mutex>
#include <vector>
#include <spdlog/spdlog.h>
#include <iostream>
#include "CommonUtils.h"

namespace PlatformUtils {

namespace
{
std::recursive_mutex& currentDirectoryMutex()
{
    static std::recursive_mutex mutex;
    return mutex;
}

bool samePathComponent(const std::filesystem::path& left,
    const std::filesystem::path& right)
{
#ifdef _WIN32
    const auto& leftValue = left.native();
    const auto& rightValue = right.native();
    return CompareStringOrdinal(leftValue.c_str(), static_cast<int>(leftValue.size()),
        rightValue.c_str(), static_cast<int>(rightValue.size()), TRUE) == CSTR_EQUAL;
#else
    return left == right;
#endif
}

std::filesystem::path normalizedWindowsPath(const std::filesystem::path& path)
{
#ifdef _WIN32
    const std::wstring value = path.native();
    if (value.rfind(L"\\\\?\\UNC\\", 0) == 0) {
        std::wstring uncPath = L"\\\\";
        uncPath += value.substr(8);
        return std::filesystem::path(uncPath);
    }
    if (value.rfind(L"\\\\?\\", 0) == 0) {
        return std::filesystem::path(value.substr(4));
    }
#endif
    return path;
}

bool isSameOrChildPath(const std::filesystem::path& candidate,
    const std::filesystem::path& base)
{
    const auto normalizedCandidate = normalizedWindowsPath(candidate);
    const auto normalizedBase = normalizedWindowsPath(base);
    auto candidatePart = normalizedCandidate.begin();
    for (auto basePart = normalizedBase.begin(); basePart != normalizedBase.end(); ++basePart, ++candidatePart) {
        if (candidatePart == normalizedCandidate.end() ||
            !samePathComponent(*candidatePart, *basePart)) {
            return false;
        }
    }
    return true;
}

bool samePath(const std::filesystem::path& left, const std::filesystem::path& right)
{
    return isSameOrChildPath(left, right) && isSameOrChildPath(right, left);
}

#ifdef _WIN32
std::wstring extendedWindowsPath(const std::filesystem::path& path)
{
    const std::wstring value = std::filesystem::absolute(path).lexically_normal().native();
    if (value.rfind(L"\\\\?\\", 0) == 0) return value;
    if (value.rfind(L"\\\\", 0) == 0) {
        return std::wstring(L"\\\\?\\UNC\\") + value.substr(2);
    }
    return std::wstring(L"\\\\?\\") + value;
}

HANDLE openLockedDirectory(const std::filesystem::path& path, DWORD& error)
{
    const auto handle = CreateFileW(extendedWindowsPath(path).c_str(), DELETE | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return INVALID_HANDLE_VALUE;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) {
        error = GetLastError();
        CloseHandle(handle);
        return INVALID_HANDLE_VALUE;
    }
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        error = ERROR_DIRECTORY;
        CloseHandle(handle);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}

bool canonicalPathFromHandle(HANDLE handle, std::filesystem::path& path, DWORD& error)
{
    std::wstring value(512, L'\0');
    for (;;) {
        const DWORD length = GetFinalPathNameByHandleW(
            handle, value.data(), static_cast<DWORD>(value.size()), FILE_NAME_NORMALIZED);
        if (length == 0) {
            error = GetLastError();
            return false;
        }
        if (length < value.size()) {
            value.resize(length);
            std::error_code ec;
            path = std::filesystem::weakly_canonical(std::filesystem::path(value), ec);
            if (ec) {
                error = static_cast<DWORD>(ec.value());
                return false;
            }
            return true;
        }
        value.resize(static_cast<std::size_t>(length) + 1);
    }
}
#endif
}

ScopedCurrentDirectory::ScopedCurrentDirectory() : _lock(currentDirectoryMutex())
{
}

ScopedCurrentDirectory::~ScopedCurrentDirectory()
{
    Restore();
}

bool ScopedCurrentDirectory::EnterValidatedDirectory(
    const std::filesystem::path& target,
    const std::filesystem::path& sourceRoot,
    bool createDirectories,
    bool* createdTargetDirectory)
{
    namespace fs = std::filesystem;
    if (createdTargetDirectory != nullptr) *createdTargetDirectory = false;
    if (_active) {
        spdlog::error("A scoped working directory is already active.");
        return false;
    }

    std::error_code ec;
    const fs::path absoluteTarget = fs::absolute(target, ec).lexically_normal();
    if (ec) {
        spdlog::error("Cannot resolve output directory '{}': {}",
            CommonUtils::path_to_utf8(target), ec.message());
        return false;
    }
    const fs::path canonicalTarget = fs::weakly_canonical(absoluteTarget, ec);
    if (ec) {
        spdlog::error("Cannot validate output directory '{}': {}",
            CommonUtils::path_to_utf8(target), ec.message());
        return false;
    }
    const fs::path canonicalSource = fs::weakly_canonical(sourceRoot, ec);
    if (ec) {
        spdlog::error("Cannot validate source directory '{}': {}",
            CommonUtils::path_to_utf8(sourceRoot), ec.message());
        return false;
    }
    if (isSameOrChildPath(canonicalTarget, canonicalSource) ||
        isSameOrChildPath(canonicalSource, canonicalTarget)) {
        spdlog::error("Output directory overlaps the source library: '{}'.",
            CommonUtils::path_to_utf8(target));
        return false;
    }

    fs::path anchor = absoluteTarget;
    while (!anchor.empty()) {
        const bool exists = fs::exists(anchor, ec);
        if (ec) {
            spdlog::error("Cannot inspect output directory '{}': {}",
                CommonUtils::path_to_utf8(anchor), ec.message());
            return false;
        }
        if (exists) {
            if (!fs::is_directory(anchor, ec) || ec) {
                spdlog::error("Output path component is not a directory: '{}'.",
                    CommonUtils::path_to_utf8(anchor));
                return false;
            }
            break;
        }
        const auto parent = anchor.parent_path();
        if (parent == anchor) break;
        anchor = parent;
    }
    if (anchor.empty()) {
        spdlog::error("Cannot find an existing directory above '{}'.",
            CommonUtils::path_to_utf8(target));
        return false;
    }

    const fs::path relativeTarget = absoluteTarget.lexically_relative(anchor);
    if (relativeTarget.empty() && absoluteTarget != anchor) {
        spdlog::error("Cannot anchor output directory '{}'.", CommonUtils::path_to_utf8(target));
        return false;
    }
    std::vector<fs::path> components;
    fs::path prefix;
    std::vector<fs::path> expectedPrefixes;
    for (const auto& component : relativeTarget) {
        if (component == "." || component.empty()) continue;
        if (component == ".." || component.has_root_path()) {
            spdlog::error("Output directory is not below its safe anchor: '{}'.",
                CommonUtils::path_to_utf8(target));
            return false;
        }
        components.push_back(component);
        prefix /= component;
        const auto expected = fs::weakly_canonical(anchor / prefix, ec);
        if (ec) {
            spdlog::error("Cannot validate output path component '{}': {}",
                CommonUtils::path_to_utf8(anchor / prefix), ec.message());
            return false;
        }
        if (isSameOrChildPath(expected, canonicalSource)) {
            spdlog::error("Output path component overlaps the source library: '{}'.",
                CommonUtils::path_to_utf8(anchor / prefix));
            return false;
        }
        expectedPrefixes.push_back(expected);
    }
    if (components.empty()) {
        expectedPrefixes.push_back(canonicalTarget);
    }

#ifdef _WIN32
    if (normalizedWindowsPath(absoluteTarget).native().size() >= MAX_PATH - 12) {
        fs::path shortAnchor = anchor;
        while (!shortAnchor.empty() && normalizedWindowsPath(shortAnchor).native().size() >= MAX_PATH) {
            const auto parent = shortAnchor.parent_path();
            if (parent == shortAnchor) break;
            shortAnchor = parent;
        }
        if (shortAnchor.empty() || normalizedWindowsPath(shortAnchor).native().size() >= MAX_PATH) {
            spdlog::error("Cannot find a short working-directory anchor for '{}'.",
                CommonUtils::path_to_utf8(target));
            return false;
        }
        const auto relativeToShortAnchor = absoluteTarget.lexically_relative(shortAnchor);
        if (relativeToShortAnchor.empty() ||
            *relativeToShortAnchor.begin() == "..") {
            spdlog::error("Cannot anchor long output directory '{}'.",
                CommonUtils::path_to_utf8(target));
            return false;
        }

        _previousPath = fs::current_path(ec);
        if (ec) {
            spdlog::error("Cannot save the current directory: {}", ec.message());
            return false;
        }
        _saved = true;
        _longPathFallback = true;
        _targetPath = absoluteTarget;
        _canonicalTarget = canonicalTarget;

        const auto lockAndValidate = [&](const fs::path& directory) {
            DWORD lockError = ERROR_SUCCESS;
            HANDLE handle = openLockedDirectory(directory, lockError);
            if (handle == INVALID_HANDLE_VALUE) {
                spdlog::error("Cannot lock output directory '{}': {}",
                    CommonUtils::path_to_utf8(directory),
                    std::error_code(static_cast<int>(lockError), std::system_category()).message());
                return false;
            }
            fs::path actual;
            DWORD pathError = ERROR_SUCCESS;
            if (!canonicalPathFromHandle(handle, actual, pathError)) {
                spdlog::error("Cannot resolve locked output directory '{}': {}",
                    CommonUtils::path_to_utf8(directory),
                    std::error_code(static_cast<int>(pathError), std::system_category()).message());
                CloseHandle(handle);
                return false;
            }
            std::error_code canonicalError;
            const auto expected = fs::weakly_canonical(directory, canonicalError);
            if (canonicalError || !samePath(actual, expected) ||
                isSameOrChildPath(actual, canonicalSource)) {
                spdlog::error("Output directory changed or overlaps the source library: '{}'.",
                    CommonUtils::path_to_utf8(directory));
                CloseHandle(handle);
                return false;
            }
            _lockedDirectories.push_back(handle);
            return true;
        };

        std::error_code anchorError;
        const auto canonicalShortAnchor = fs::weakly_canonical(shortAnchor, anchorError);
        const auto canonicalPrevious = anchorError
            ? fs::path{} : fs::weakly_canonical(_previousPath, anchorError);
        const bool currentDirectoryProtectsAnchor = !anchorError &&
            isSameOrChildPath(canonicalPrevious, canonicalShortAnchor);
        if (anchorError || isSameOrChildPath(canonicalShortAnchor, canonicalSource)) {
            Restore();
            return false;
        }
        if (!currentDirectoryProtectsAnchor &&
            !SetCurrentDirectoryW(normalizedWindowsPath(shortAnchor).c_str())) {
            const DWORD changeError = GetLastError();
            spdlog::error("Cannot enter short output anchor '{}': {}",
                CommonUtils::path_to_utf8(shortAnchor),
                std::error_code(static_cast<int>(changeError), std::system_category()).message());
            Restore();
            return false;
        }
        std::error_code currentDirectoryError;
        const auto enteredAnchor = fs::weakly_canonical(fs::current_path(currentDirectoryError),
            currentDirectoryError);
        if (currentDirectoryError || !isSameOrChildPath(enteredAnchor, canonicalShortAnchor) ||
            isSameOrChildPath(enteredAnchor, canonicalSource)) {
            spdlog::error("Short output anchor changed while entering '{}'.",
                CommonUtils::path_to_utf8(shortAnchor));
            Restore();
            return false;
        }
        _active = true;

        fs::path currentPath = shortAnchor;
        bool targetCreated = false;
        for (const auto& component : relativeToShortAnchor) {
            if (component == "." || component.empty()) continue;
            if (component == ".." || component.has_root_path()) {
                spdlog::error("Long output path escaped its short anchor: '{}'.",
                    CommonUtils::path_to_utf8(target));
                Restore();
                return false;
            }
            currentPath /= component;
            const auto apiPath = extendedWindowsPath(currentPath);
            DWORD attributes = GetFileAttributesW(apiPath.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) {
                const DWORD inspectError = GetLastError();
                if (inspectError != ERROR_FILE_NOT_FOUND && inspectError != ERROR_PATH_NOT_FOUND) {
                    spdlog::error("Cannot inspect output path component '{}': {}",
                        CommonUtils::path_to_utf8(currentPath),
                        std::error_code(static_cast<int>(inspectError), std::system_category()).message());
                    Restore();
                    return false;
                }
                if (!createDirectories) {
                    spdlog::error("Output directory does not exist: '{}'.",
                        CommonUtils::path_to_utf8(currentPath));
                    Restore();
                    return false;
                }
                if (CreateDirectoryW(apiPath.c_str(), nullptr)) {
                    targetCreated = samePath(currentPath, absoluteTarget);
                }
                else {
                    const DWORD createError = GetLastError();
                    if (createError != ERROR_ALREADY_EXISTS) {
                        spdlog::error("Cannot create output directory '{}': {}",
                            CommonUtils::path_to_utf8(currentPath),
                            std::error_code(static_cast<int>(createError), std::system_category()).message());
                        Restore();
                        return false;
                    }
                }
                attributes = GetFileAttributesW(apiPath.c_str());
            }
            const DWORD attributesError =
                attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
                !lockAndValidate(currentPath)) {
                if (attributes == INVALID_FILE_ATTRIBUTES) {
                    spdlog::error("Cannot inspect output directory '{}': {}",
                        CommonUtils::path_to_utf8(currentPath),
                        std::error_code(static_cast<int>(attributesError), std::system_category()).message());
                }
                else if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                    spdlog::error("Output path component is not a directory: '{}'.",
                        CommonUtils::path_to_utf8(currentPath));
                }
                Restore();
                return false;
            }
        }
        if (_lockedDirectories.empty() ||
            !samePath(currentPath, absoluteTarget) ||
            !samePath(_canonicalTarget, fs::weakly_canonical(currentPath, ec)) || ec) {
            spdlog::error("Long output directory changed during validation: '{}'.",
                CommonUtils::path_to_utf8(target));
            Restore();
            return false;
        }
        if (createdTargetDirectory != nullptr) *createdTargetDirectory = targetCreated;
        return true;
    }
#endif

    _previousPath = fs::current_path(ec);
    if (ec) {
        spdlog::error("Cannot save the current directory: {}", ec.message());
        return false;
    }
#ifndef _WIN32
    _previousDirectory = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (_previousDirectory == -1) {
        spdlog::error("Cannot retain the current directory: {}",
            std::error_code(errno, std::generic_category()).message());
        _previousPath.clear();
        return false;
    }
#endif
    _saved = true;
    // Legacy launchers (including Chocolatey shims) reject an extended-prefix working directory.
    fs::current_path(normalizedWindowsPath(anchor), ec);
    if (ec) {
        spdlog::error("Cannot enter safe output anchor '{}': {}",
            CommonUtils::path_to_utf8(anchor), ec.message());
        Restore();
        return false;
    }
    const auto canonicalAnchor = fs::weakly_canonical(fs::current_path(ec), ec);
    const auto expectedAnchor = ec ? fs::path{} : fs::weakly_canonical(anchor, ec);
    if (ec || !samePath(canonicalAnchor, expectedAnchor) ||
        isSameOrChildPath(canonicalAnchor, canonicalSource)) {
        spdlog::error("Output anchor changed during validation: '{}'.",
            CommonUtils::path_to_utf8(anchor));
        Restore();
        return false;
    }
    _active = true;

    for (std::size_t i = 0; i < components.size(); ++i) {
        const auto& component = components[i];
        const bool exists = fs::exists(component, ec);
        if (ec) {
            spdlog::error("Cannot inspect output path component '{}': {}",
                CommonUtils::path_to_utf8(component), ec.message());
            Restore();
            return false;
        }
        if (!exists) {
            if (!createDirectories || !fs::create_directory(component, ec) || ec) {
                spdlog::error("Cannot create output directory '{}': {}",
                    CommonUtils::path_to_utf8(component),
                    ec ? ec.message() : "directory creation was not requested");
                Restore();
                return false;
            }
            if (createdTargetDirectory != nullptr && i + 1 == components.size()) {
                *createdTargetDirectory = true;
            }
        }
        else if (!fs::is_directory(component, ec) || ec) {
            spdlog::error("Output path component is not a directory: '{}'.",
                CommonUtils::path_to_utf8(component));
            Restore();
            return false;
        }
        fs::current_path(component, ec);
        if (ec) {
            spdlog::error("Cannot enter output path component '{}': {}",
                CommonUtils::path_to_utf8(component), ec.message());
            Restore();
            return false;
        }
        const auto current = fs::weakly_canonical(fs::current_path(ec), ec);
        if (ec || !samePath(current, expectedPrefixes[i]) ||
            isSameOrChildPath(current, canonicalSource)) {
            spdlog::error("Output directory changed during validation: '{}'.",
                CommonUtils::path_to_utf8(absoluteTarget));
            Restore();
            return false;
        }
    }
    if (components.empty()) {
        const auto current = fs::weakly_canonical(fs::current_path(ec), ec);
        if (ec || !samePath(current, canonicalTarget) || isSameOrChildPath(current, canonicalSource)) {
            spdlog::error("Output directory changed during validation: '{}'.",
                CommonUtils::path_to_utf8(absoluteTarget));
            Restore();
            return false;
        }
    }
    _targetPath = absoluteTarget;
    _canonicalTarget = canonicalTarget;
    return true;
}

std::filesystem::path ScopedCurrentDirectory::ResolvePath(const std::filesystem::path& path) const
{
#ifdef _WIN32
    if (_longPathFallback && !path.is_absolute()) {
        return extendedWindowsPath(_targetPath / path);
    }
#endif
    return path;
}

bool ScopedCurrentDirectory::IsAnchored(const std::filesystem::path& target) const
{
    if (!_active) return false;
#ifdef _WIN32
    if (_longPathFallback) {
        std::error_code ec;
        const auto canonical = std::filesystem::weakly_canonical(target, ec);
        return !ec && !_lockedDirectories.empty() &&
            samePath(canonical, _canonicalTarget);
    }
#endif
    std::error_code ec;
    return std::filesystem::equivalent(".", target, ec) && !ec;
}

bool ScopedCurrentDirectory::Restore()
{
    if (!_saved) return true;
    bool restored = false;
    std::error_code error;
#ifdef _WIN32
    restored = SetCurrentDirectoryW(normalizedWindowsPath(_previousPath).c_str()) != 0;
    if (!restored) error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
    restored = fchdir(_previousDirectory) == 0;
    if (!restored) error = std::error_code(errno, std::generic_category());
#endif
    if (!restored) {
        spdlog::error("Cannot restore the original working directory '{}': {}",
            CommonUtils::path_to_utf8(_previousPath), error.message());
        return false;
    }
#ifdef _WIN32
    for (void* lockedDirectory : _lockedDirectories) {
        CloseHandle(static_cast<HANDLE>(lockedDirectory));
    }
    _lockedDirectories.clear();
#endif
#ifndef _WIN32
    close(_previousDirectory);
    _previousDirectory = -1;
#endif
    _saved = false;
    _active = false;
    _longPathFallback = false;
    _previousPath.clear();
    _targetPath.clear();
    _canonicalTarget.clear();
    return restored;
}

#ifdef _WIN32
    std::wstring WindowsLongPath(const std::filesystem::path& path) {
        const std::wstring normalized = path.lexically_normal().native();
        if (normalized.rfind(L"\\\\?\\", 0) == 0) {
            return normalized;
        }

        std::wstring value = std::filesystem::absolute(path).lexically_normal().native();
        if (value.size() < MAX_PATH) {
            return value;
        }
        if (value.rfind(L"\\\\", 0) == 0) {
            return std::wstring(L"\\\\?\\UNC\\") + value.substr(2);
        }
        return std::wstring(L"\\\\?\\") + value;
    }
#endif

    std::string wstringToUtf8_ver2(const std::wstring& wstr) {
#ifdef _WIN32
        if (wstr.empty()) return {};

        const int size_needed = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wstr.data(), static_cast<int>(wstr.size()),
            nullptr, 0, nullptr, nullptr);
        if (size_needed == 0) {
            throw std::runtime_error("Failed to convert UTF-16 string to UTF-8.");
        }
        std::string utf8Str(size_needed, 0);
        if (WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, wstr.data(), static_cast<int>(wstr.size()),
                utf8Str.data(), size_needed, nullptr, nullptr) == 0) {
            throw std::runtime_error("Failed to convert UTF-16 string to UTF-8.");
        }

        return utf8Str;
#else
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.to_bytes(wstr);
#endif
    }

    std::string WideToUTF8(const std::wstring& wstr) {
        return wstringToUtf8_ver2(wstr);
    }

    void OpenDirectoryInExplorer(std::wstring dirName)
    {
        OpenDirectory(std::filesystem::path(dirName));
    }



    void OpenDirectory(const std::filesystem::path& path) {
#ifdef _WIN32
        std::wstring wPath = path.wstring();
        HINSTANCE result = ShellExecuteW(
            nullptr, L"open", wPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL
        );
        if (reinterpret_cast<intptr_t>(result) <= 32) {
            throw std::runtime_error("Failed to open directory on Windows: " + CommonUtils::path_to_utf8(path));
        }
#else
        const auto utf8Path = path.u8string();
        const std::string nativePath(reinterpret_cast<const char*>(utf8Path.data()), utf8Path.size());
#ifdef __APPLE__
        constexpr const char* opener = "open";
#else
        constexpr const char* opener = "xdg-open";
#endif
        const pid_t child = fork();
        if (child == -1) {
            throw std::runtime_error("Failed to launch directory opener for: " + nativePath);
        }
        if (child == 0) {
            execlp(opener, opener, nativePath.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        int status = 0;
        while (waitpid(child, &status, 0) == -1) {
            if (errno != EINTR) {
                throw std::runtime_error("Failed while waiting for directory opener: " + nativePath);
            }
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            throw std::runtime_error("Failed to open directory: " + nativePath);
        }
#endif
    }


    int waitForKeyPress(std::string message) {
        int ret = 0;

        if (!message.empty())
        {
            std::cout << message << std::flush;
        }

#ifdef _WIN32
        ret = _getch(); // Windows: Use _getch for non-blocking key press
#else
        struct termios oldt, newt;
        // Get current terminal settings
        if (tcgetattr(STDIN_FILENO, &oldt) != 0) {
            fprintf(stderr, "Error: Failed to get terminal attributes\n");
            return -1;
        }
        newt = oldt;
        // Disable canonical mode and echo
        newt.c_lflag &= ~(ICANON | ECHO);
        if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) != 0) {
            fprintf(stderr, "Error: Failed to set terminal attributes\n");
            return -1;
        }
        getchar(); // Wait for a single key press
        // Restore original terminal settings
        if (tcsetattr(STDIN_FILENO, TCSANOW, &oldt) != 0) {
            fprintf(stderr, "Error: Failed to restore terminal attributes\n");
        }
#endif
        if (!message.empty())
        {
            std::cout << "\33[2K\r";  // Clear entire line and move cursor to start
        }
        return ret;
    }
}
