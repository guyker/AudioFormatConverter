
#include "PlatformUtils.h"




#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <cstdlib>
#endif

namespace PlatformUtils {

    std::string wstringToUtf8_ver2(const std::wstring& wstr) {
#ifdef _WIN32
        if (wstr.empty()) return {};

        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (size_needed == 0) {
            //  throw std::runtime_error("WideCharToMultiByte failed to determine size");
        }
        std::string utf8Str(size_needed - 1, 0); // -1 to exclude null terminator
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8Str[0], size_needed, nullptr, nullptr);

        return utf8Str;
#else
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.to_bytes(wstr);
#endif
    }

    std::string WideToUTF8(const std::wstring& wstr) {
        if (wstr.empty()) {
            return std::string();
        }

        std::string utf8;
        utf8.reserve(wstr.size() * 2); // Rough estimate for UTF-8 size

        for (wchar_t wc : wstr) {
#if defined(_WIN32)
            // Windows: wchar_t is UTF-16 (2 bytes)
            uint32_t codepoint;
            if (wc >= 0xD800 && wc <= 0xDBFF) {
                // High surrogate; need the next wchar_t
                throw std::runtime_error("Incomplete UTF-16 surrogate pair (not yet fully implemented)");
                // Note: Full surrogate pair handling requires peeking at the next character
            }
            else if (wc >= 0xDC00 && wc <= 0xDFFF) {
                throw std::runtime_error("Unexpected UTF-16 low surrogate");
            }
            else {
                codepoint = static_cast<uint32_t>(wc);
            }
#else
            // Linux/macOS: wchar_t is UTF-32 (4 bytes)
            uint32_t codepoint = static_cast<uint32_t>(wc);
#endif

            // Convert codepoint to UTF-8
            if (codepoint <= 0x7F) {
                utf8 += static_cast<char>(codepoint);
            }
            else if (codepoint <= 0x7FF) {
                utf8 += static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
                utf8 += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
            else if (codepoint <= 0xFFFF) {
                utf8 += static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
                utf8 += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                utf8 += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
            else if (codepoint <= 0x10FFFF) {
                utf8 += static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
                utf8 += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                utf8 += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                utf8 += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
            else {
                throw std::runtime_error("Invalid Unicode codepoint");
            }
        }

        return utf8;
    }

    void OpenDirectoryInExplorer(std::wstring dirName)
    {
        auto pathSeperator = std::filesystem::path::preferred_separator;
        //using namespace std::string_literals;
        //COMMAND

        //std::wstring cmdExecNameW{ L"ffmpeg" };
        //std::wstring convertParamsW{ L"-c:v copy -sample_fmt s16 -ar 44100 -y -v warning -stats"s };
        //std::wstring commandW{ cmdExecNameW + LR"( -i ")"s + _sourcePath.generic_wstring() + LR"(" )"s + convertParamsW + LR"( ")"s + _targetTMPPath.generic_wstring() + LR"(")"s };

        auto dirNameConv1 = dirName.substr(4, dirName.length() - 4);
        std::wstring dirNameConv2{};
        for (auto c : dirNameConv1)
        {
            if (c == '/')
            {
                dirNameConv2 += '\\';
            }
            else
            {
                dirNameConv2 += c;
            }
        }

        std::wstring cmdExecNameW{ L"explorer.exe /e '" };
        std::wstring commandW{ cmdExecNameW + dirNameConv2 + L"'" };


        try {
            //   _wsystem(commandW.c_str());

            ShellExecute(NULL, NULL, dirNameConv2.c_str(), NULL, NULL, SW_SHOWNORMAL);

        }
        catch (const std::exception& ex) {
        }
    }



    void OpenDirectory(const std::filesystem::path& path) {
#ifdef _WIN32
        std::wstring wPath = path.wstring();
        HINSTANCE result = ShellExecuteW(
            nullptr, L"open", wPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL
        );
        if (reinterpret_cast<intptr_t>(result) <= 32) {
            throw std::runtime_error("Failed to open directory on Windows: " + path.string());
        }
#else
        std::string p = path.string();
        // Try xdg-open first
        std::string command = "xdg-open \"" + p + "\"";
        if (std::system(command.c_str()) != 0) {
            // Fallback to common file managers
            command = "nautilus \"" + p + "\" 2>/dev/null || dolphin \"" + p + "\" 2>/dev/null";
            if (std::system(command.c_str()) != 0) {
                throw std::runtime_error("Failed to open directory on Linux: " + p);
            }
        }
#endif
    }

    void waitForKeyPress() {
#ifdef _WIN32
        _getch(); // Windows: Use _getch
#else
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        getchar();
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
    }
}



