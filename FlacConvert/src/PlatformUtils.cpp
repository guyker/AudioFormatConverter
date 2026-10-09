
#include "PlatformUtils.h"




#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <conio.h>
#else
#include <codecvt>
#include <locale>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif
#include <cerrno>
#include <cstdio>
#include <spdlog/spdlog.h>
#include <iostream>
#include "CommonUtils.h"

namespace PlatformUtils {

#ifdef _WIN32
    std::wstring WindowsLongPath(const std::filesystem::path& path) {
        const std::wstring original = path.native();
        if (original.rfind(L"\\\\?\\", 0) == 0) {
            return original;
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
