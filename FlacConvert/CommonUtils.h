#pragma once

#include <string>
#include <iostream>
#include <array>
#include <vector>
#include <utility>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

#include <filesystem>    
#include <chrono>
#include <coroutine>

namespace CommonUtils
{

    // Generator type
    template<typename T>
    struct Generator {
        struct promise_type {
            T current_value;
            std::exception_ptr exception = nullptr;

            Generator get_return_object() {
                return Generator{ std::coroutine_handle<promise_type>::from_promise(*this) };
            }

            std::suspend_always initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }
            void return_void() noexcept {}

            std::suspend_always yield_value(T value) noexcept {
                current_value = std::move(value);
                return {};
            }

            void unhandled_exception() noexcept {
                exception = std::current_exception(); // Store exception
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;
        handle_type handle;

        explicit Generator(handle_type h) : handle(h) {}
        Generator(const Generator&) = delete;
        Generator(Generator&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
        ~Generator() {
            if (handle) handle.destroy();
        }

        // Manual resume and value access
        bool resume() {
            if (!handle || handle.done()) {
                return false;
            }
            handle.resume();
            if (handle.promise().exception) {
                std::rethrow_exception(handle.promise().exception);
            }
            return !handle.done();
        }

        T value() const {
            if (handle.promise().exception) {
                std::rethrow_exception(handle.promise().exception);
            }
            return handle.promise().current_value;
        }

        // Iterator for range-based for loop
        struct Iterator {
            handle_type coro;

            Iterator(handle_type h) : coro(h) {}
            Iterator& operator++() {
                if (!coro.done()) {
                    coro.resume();
                    if (coro.promise().exception) {
                        std::rethrow_exception(coro.promise().exception);
                    }
                }
                return *this;
            }

            bool operator==(std::default_sentinel_t) const {
                return coro.done();
            }

            T& operator*() const {
                if (coro.promise().exception) {
                    std::rethrow_exception(coro.promise().exception);
                }
                return coro.promise().current_value;
            }

            T* operator->() const {
                if (coro.promise().exception) {
                    std::rethrow_exception(coro.promise().exception);
                }
                return &coro.promise().current_value;
            }
        };

        Iterator begin() {
            if (!handle.done()) {
                handle.resume();
                if (handle.promise().exception) {
                    std::rethrow_exception(handle.promise().exception);
                }
            }
            return Iterator{ handle };
        }

        std::default_sentinel_t end() { return {}; }
    };


    std::uintmax_t stringToUintmax(const std::string& str);


    constexpr std::array<char, 4> ProgressCircleChars = { '|', '/', '-', '\\' };


    

    long long GetMilliFromDuration(auto time1, auto time2)
    {
        auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(time2 - time1).count();

        return delta;
    }
    
    std::string GetDurationinString(auto duration)
    {
        if (duration <= 0)
        {
            return "Calculating...";
        }
        else if (duration < 1000)
        {
            //less than 1 second
            return std::to_string(duration) + " mSec";
        }
        else if (duration < 1000 * 60)
        {
            //lss than 1 minute
            return std::to_string(duration / 1000) + " sec";
        }
        else if (duration < 1000 * 60 * 60)
        {
            //less than 1 hour
            auto sec = duration / 1000;
            auto min = duration / 1000 / 60;

            if (min <= 10)
            {
                return std::to_string(min) + " min, " + std::to_string(sec - min * 60) + " sec";
            }
            else
            {
                return std::to_string(min) + " min";
            }
        }
        else
        {
            //more than 1 hour
            auto sec = duration / 1000;
            auto min = duration / 1000 / 60;
            auto hour = duration / 1000 / 60 / 60;
            
            return std::to_string(hour) + " hours, " +
                std::to_string(min - hour * 60) + " min";
            //return std::to_string(hour) + " hours, " +
            //    std::to_string(min - hour * 60) + " min, " +
            //    std::to_string(sec - (hour * 60 * 60) - ((min - hour * 60) * 60)) + " sec";
        }
    }


    std::string GetDurationinString(auto time1, auto time2)
    {
        auto duration = GetMilliFromDuration(time1, time2);
        return GetDurationinString(duration);
    }


	enum class ProgressBarType
	{
        Init,
        Progress,
		Complete
	};

    void show_circular_progress(std::string str = "");
    void show_progress_bar(int total, std::string prefix, size_t count, size_t size,
        std::string name, size_t currentCount = 0, size_t totalCount = 0,
        ProgressBarType progressType = ProgressBarType::Progress);

    inline std::string utf8string_to_string(const std::u8string& u8str) {
        return std::string(u8str.begin(), u8str.end());
    }


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

    static size_t utf8_char_count(const std::string& str) {
        size_t count = 0;
        for (unsigned char c : str) {
            // In UTF-8, a leading byte starts with 0b0xxxxxxx or 0b11xxxxxx
            if ((c & 0b11000000) != 0b10000000) {
                ++count;
            }
        }
        return count;
    }

    // Extract the N-th UTF-8 codepoint from the string
    static std::string get_utf8_char_at(const std::string& utf8_str, size_t index) {
        size_t i = 0, char_count = 0;
        while (i < utf8_str.size()) {
            if (char_count == index) {
                size_t char_len = 1;
                unsigned char c = static_cast<unsigned char>(utf8_str[i]);
                if ((c & 0xF8) == 0xF0) char_len = 4;      // 4-byte char
                else if ((c & 0xF0) == 0xE0) char_len = 3; // 3-byte char
                else if ((c & 0xE0) == 0xC0) char_len = 2; // 2-byte char
                return utf8_str.substr(i, char_len);
            }

            // Advance to next UTF-8 character
            unsigned char c = static_cast<unsigned char>(utf8_str[i]);
            if ((c & 0xF8) == 0xF0) i += 4;
            else if ((c & 0xF0) == 0xE0) i += 3;
            else if ((c & 0xE0) == 0xC0) i += 2;
            else i += 1;

            ++char_count;
        }
        return "";  // index out of bounds
    }

    static std::string wstring_to_utf8(const std::wstring& wstr) {
        if (wstr.empty()) return {};

        int size_needed = WideCharToMultiByte(CP_UTF8, 0,
            wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);

        std::string result(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0,
            wstr.c_str(), (int)wstr.size(), &result[0], size_needed, nullptr, nullptr);

        return result;
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


