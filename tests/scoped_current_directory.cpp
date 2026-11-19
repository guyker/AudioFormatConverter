#include "PlatformUtils.h"
#include "FolderConvert.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
fs::path cleanupPath(const fs::path& path)
{
#ifdef _WIN32
    return PlatformUtils::WindowsLongPath(path);
#else
    return path;
#endif
}

void cleanupFixture(const fs::path& root, const fs::path& output)
{
    std::error_code ec;
    fs::remove_all(cleanupPath(output), ec);
    if (ec) throw std::runtime_error("Could not remove output fixture: " + ec.message());
    auto parent = output.parent_path();
    while (parent != root) {
        fs::remove(cleanupPath(parent), ec);
        if (ec) throw std::runtime_error("Could not remove output parent: " + ec.message());
        parent = parent.parent_path();
    }
    fs::remove(root / "source" / "sentinel.txt", ec);
    if (ec) throw std::runtime_error("Could not remove source fixture: " + ec.message());
    fs::remove(root / "source", ec);
    if (ec) throw std::runtime_error("Could not remove source directory: " + ec.message());
    fs::remove(root, ec);
    if (ec) throw std::runtime_error("Could not remove test fixture: " + ec.message());
}

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void requireCompatibleWorkingDirectory()
{
#ifdef _WIN32
    std::wstring directory(MAX_PATH, L'\0');
    const auto length = GetCurrentDirectoryW(static_cast<DWORD>(directory.size()), directory.data());
    require(length > 0 && length < directory.size(), "Working directory exceeds the legacy limit");
    require(directory.rfind(L"\\\\?\\", 0) != 0,
        "Child processes would inherit an extended-prefix working directory");
#endif
}

void checkMarkerCreatedAfterPlanning(const fs::path& source, const fs::path& output)
{
    FolderConversionPlan plan;
    require(FolderConvert::PrepareConversion(source, output, true, {}, plan) == 0,
        "Could not prepare marker-conflict fixture");
    const auto marker = output / ".conversion-in-progress";
    fs::create_directory(marker);
    std::ostringstream messages;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(messages);
    struct RestoreLogger {
        std::shared_ptr<spdlog::logger> previous{ spdlog::default_logger() };
        ~RestoreLogger() { spdlog::set_default_logger(previous); }
    } restoreLogger;
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("marker-conflict", sink));

    require(FolderConvert::ExecuteConversion(plan) != 0, "Competing conversion was accepted");
    require(messages.str().find("Cannot reserve in-progress marker: it already exists") != std::string::npos,
        "Marker conflict did not explain that the marker already exists");
    require(messages.str().find(std::error_code{}.message()) == std::string::npos,
        "Marker conflict reported a successful OS operation as an error");
    require(fs::is_directory(marker), "Competing run's marker was removed");
    require(!fs::exists(output / "conversion-audit.json"), "Competing run's audit was written");
}

#ifdef _WIN32
int runMappedDriveFixture()
{
    const DWORD drives = GetLogicalDrives();
    const auto fixtureName = "afc-scoped-mapped-drive-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    for (unsigned index = 0; index < 26; ++index) {
        if ((drives & (1u << index)) == 0) continue;
        const wchar_t driveRoot[] = {
            static_cast<wchar_t>(L'A' + index), L':', L'\\', L'\0'
        };
        if (GetDriveTypeW(driveRoot) != DRIVE_REMOTE) continue;

        const fs::path remoteRoot = fs::path(driveRoot) / fixtureName;
        std::error_code ec;
        if (!fs::create_directory(remoteRoot, ec) || ec) continue;
        fs::path source;
        try {
            source = fs::temp_directory_path() / (fixtureName + "-source");
            const fs::path output = remoteRoot / "new" / "nested" / "output";
            require(fs::create_directory(source, ec) && !ec,
                "Could not create the local source fixture");
            const auto extendedOutput = PlatformUtils::WindowsLongPath(output, true);
            {
                PlatformUtils::ScopedCurrentDirectory anchor;
                require(anchor.EnterValidatedDirectory(extendedOutput, source, true),
                    "Could not enter an extended output path on a mapped drive");
                require(anchor.IsAnchored(extendedOutput),
                    "Extended mapped-drive output was not anchored");
                requireCompatibleWorkingDirectory();
                require(anchor.Restore(),
                    "Could not restore the working directory after mapped-drive validation");
            }

            fs::remove_all(PlatformUtils::WindowsLongPath(remoteRoot, true), ec);
            require(!ec, "Could not remove the mapped-drive fixture output");
            fs::remove_all(cleanupPath(source), ec);
            require(!ec, "Could not remove the mapped-drive source fixture");
            return 0;
        }
        catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            std::error_code cleanupError;
            fs::remove_all(PlatformUtils::WindowsLongPath(remoteRoot, true), cleanupError);
            if (!source.empty()) fs::remove_all(cleanupPath(source), cleanupError);
            return 1;
        }
    }

    std::cout << "No writable mapped drive was available; mapped-drive fixture skipped.\n";
    return 0;
}
#endif
}

int runFixture([[maybe_unused]] bool longPath, [[maybe_unused]] bool extendedPath)
{
    const auto originalDirectory = fs::current_path();
    const auto root = fs::temp_directory_path() /
        ("afc-scoped-current-directory-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto source = root / "source";
#ifdef _WIN32
    const auto output = longPath
        ? root / std::string(90, 'a') / std::string(90, 'b') / std::string(90, 'c')
        : root / "output";
    const fs::path configuredOutput = extendedPath ? cleanupPath(output) : output;
#else
    const auto output = root / "output";
    const auto configuredOutput = output;
#endif

    std::error_code ec;
    try {
        fs::create_directories(source);
        const auto sourceSentinel = source / "sentinel.txt";
        {
            std::ofstream file(sourceSentinel);
            require(static_cast<bool>(file), "Could not create source sentinel");
            file << "source remains unchanged";
        }

        PlatformUtils::ScopedCurrentDirectory anchor;
        require(anchor.EnterValidatedDirectory(configuredOutput, source, true),
            "Could not enter the validated output directory");
        requireCompatibleWorkingDirectory();
        require(anchor.IsAnchored(output), "Output directory was not anchored");
        const auto resolvedRoot = anchor.ResolvePath(".");
#ifdef _WIN32
        require(!longPath || (resolvedRoot.is_absolute() && resolvedRoot.native().size() >= 260),
            "Long-path fallback did not resolve output relative to the locked directory");
#endif
        const auto outputSentinel = anchor.ResolvePath("output.txt");
        {
            std::ofstream file(outputSentinel);
            require(static_cast<bool>(file), "Could not write through the resolved output path");
            file << "output";
        }
        require(fs::is_regular_file(outputSentinel), "Resolved output was not created");

#ifdef _WIN32
        const auto extendedOutput = PlatformUtils::WindowsLongPath(output);
        const auto movedOutput = PlatformUtils::WindowsLongPath(output.parent_path() / "moved-output");
        fs::rename(extendedOutput, movedOutput, ec);
        if (!ec) {
            std::error_code restoreError;
            fs::rename(movedOutput, extendedOutput, restoreError);
            throw std::runtime_error("An anchored output directory could be renamed");
        }
        ec.clear();

        const auto extendedParent = PlatformUtils::WindowsLongPath(output.parent_path());
        const auto movedParent = PlatformUtils::WindowsLongPath(output.parent_path().parent_path() / "moved-parent");
        fs::rename(extendedParent, movedParent, ec);
        if (!ec) {
            std::error_code restoreError;
            fs::rename(movedParent, extendedParent, restoreError);
            throw std::runtime_error("An anchored output parent could be renamed");
        }
        ec.clear();
#endif

        require(anchor.Restore(), "Could not restore the original working directory");
        requireCompatibleWorkingDirectory();
        require(fs::current_path() == originalDirectory, "Working directory was not restored");
        require(fs::is_regular_file(sourceSentinel), "The source sentinel was removed");
        {
            std::ifstream file(sourceSentinel);
            std::string contents;
            std::getline(file, contents);
            require(contents == "source remains unchanged", "The source sentinel was modified");
        }
#ifdef _WIN32
        const auto outputAfterRestore = PlatformUtils::WindowsLongPath(output);
        const auto movedOutputAfterRestore =
            PlatformUtils::WindowsLongPath(output.parent_path() / "moved-output");
        fs::rename(outputAfterRestore, movedOutputAfterRestore, ec);
        require(!ec, "Output directory remained locked after restore");
        fs::rename(movedOutputAfterRestore, outputAfterRestore, ec);
        require(!ec, "Could not restore the output directory after lock verification");
#endif
        if (!longPath && !extendedPath) checkMarkerCreatedAfterPlanning(source, output);
        cleanupFixture(root, output);
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        fs::current_path(originalDirectory, ec);
        try {
            cleanupFixture(root, output);
        }
        catch (const std::exception& cleanupError) {
            std::cerr << cleanupError.what() << '\n';
        }
        return 1;
    }

    return 0;
}

int main()
{
#ifdef _WIN32
    for (const bool longPath : { false, true }) {
        for (const bool extendedPath : { false, true }) {
            if (runFixture(longPath, extendedPath) != 0) return 1;
        }
    }
    return runMappedDriveFixture();
#else
    return runFixture(false, false);
#endif
}
