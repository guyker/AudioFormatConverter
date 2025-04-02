#pragma once

#include <string>

#include <array>
#include <vector>
#include <utility>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

namespace CommonUtils
{
    constexpr std::array<char, 4> ProgressCircleChars = { '|', '/', '-', '\\' };

    std::wstring ToLower(const std::wstring& str);

    // Pair type for symbol name and UTF-8 string
    using SymbolPair = std::pair<const char*, const char*>;

    // Compile-time array of symbols
    constexpr std::array<SymbolPair, 24> SYMBOLS = { {
        {"check", "\xE2\x9C\x94"},         // ✔️ Heavy Check Mark (Success)
        {"gear", "\xE2\x9A\x99"},          // ⚙️ Gear (Processing/Settings)
        {"x", "\xE2\x9C\x98"},             // ✘ Heavy Ballot X (Error)
        {"info", "\xE2\x84\xB9"},          // ℹ️ Info Symbol (Information)
        {"warning", "\xE2\x9A\xA0"},       // ⚠️ Warning Sign (Warning)
        {"no_entry", "\xE2\x9B\x94"},      // ⛔ No Entry (Error/Forbidden)
        {"exclamation", "\xE2\x9D\x97"},   // ❗ Heavy Exclamation (Alert)
        {"question", "\xE2\x9D\x93"},      // ❓ Red Question Mark (Query)
        {"check_button", "\xE2\x9C\x85"},  // ✅ Check Button (Success)
        {"hourglass", "\xE2\x8F\xB3"},     // ⏳ Hourglass (Processing)
        {"prohibited", "\xF0\x9F\x9A\xAB"}, // 🚫 Prohibition (Error)
        {"tools", "\xF0\x9F\x9B\xA0"},     // 🛠️ Hammer & Wrench (Tools)
        {"search", "\xF0\x9F\x94\x8D"},    // 🔍 Magnifying Glass (Search)
        {"white_square", "\xE2\xAC\x9C"},  // ⬜ White Square (Neutral)
        {"black_square", "\xE2\xAC\x9B"},  // ⬛ Black Square (Status)
        {"plus", "\xE2\x9E\x95"},          // ➕ Heavy Plus (Add)
        {"minus", "\xE2\x9E\x96"},         // ➖ Heavy Minus (Remove)
        {"red_circle", "\xF0\x9F\x94\xB4"}, // 🔴 Red Circle (Error/Stop)
        {"green_circle", "\xF0\x9F\x9F\xA2"}, // 🟢 Green Circle (Success)
        {"blue_circle", "\xF0\x9F\x94\xB5"}, // 🔵 Blue Circle (Info)
        {"error_cross", "\xE2\x9D\x8C" },   // ❌ Cross Mark Button (Error)
        {"error_triangle", "\xE2\x96\xB2\xE2\x81\x89"}, // ▲! (Error, Warning Triangle)
        {"skull", "\xF0\x9F\x92\x80"},     // 💀 Skull (Fatal Error)
        {"stop_sign", "\xF0\x9F\x9B\x91"}  // 🛑 Stop Sign (Stop/Error)
    } };

    // Constexpr function to get symbol by name
    constexpr const char* getSymbolConstexpr(const char* name) {
        for (const auto& [symName, symValue] : SYMBOLS) {
            if (std::strcmp(name, symName) == 0) {
                return symValue;
            }
        }
        return ""; // Return empty string if not found
    }


    static std::string wstringToUtf8(const std::wstring& wstr) {
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


    // Function to convert UTF-8 string to wstring (cross-platform)
    static std::wstring utf8ToWstring(const std::string& str) {
#ifdef _WIN32
        if (str.empty()) return {};

        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        std::wstring wstr(size_needed - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);

        return wstr;
#else
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.from_bytes(str);
#endif
    }


    // Convert UTF-8 to wstring (cross-platform)
    static std::wstring utf8ToWstring2_XXXXX(const std::string& utf8Str) {
        if (utf8Str.empty()) {
            return std::wstring();
        }

#ifdef _WIN32
        // Windows: UTF-8 to UTF-16
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(),
            static_cast<int>(utf8Str.length()),
            nullptr, 0);
        if (size_needed == 0) {
            //  throw std::runtime_error("MultiByteToWideChar failed to determine size");
        }

        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(),
            static_cast<int>(utf8Str.length()),
            &wstr[0], size_needed);
        return wstr;

#else
        // Linux: UTF-8 to UTF-32 (assuming wstring is UTF-32)
        iconv_t cd = iconv_open("WCHAR_T", "UTF-8");
        if (cd == (iconv_t)-1) {
            throw std::runtime_error("iconv_open failed");
        }

        size_t inBytes = utf8Str.length();
        size_t outBytes = inBytes * sizeof(wchar_t);
        std::wstring wstr(outBytes / sizeof(wchar_t), 0);

        char* inBuf = const_cast<char*>(utf8Str.c_str());
        char* outBuf = reinterpret_cast<char*>(&wstr[0]);
        size_t outBytesLeft = outBytes;

        size_t result = iconv(cd, &inBuf, &inBytes, &outBuf, &outBytesLeft);
        iconv_close(cd);

        if (result == (size_t)-1) {
            throw std::runtime_error("iconv conversion failed");
        }

        wstr.resize((outBytes - outBytesLeft) / sizeof(wchar_t));
        return wstr;
#endif
    }

}


