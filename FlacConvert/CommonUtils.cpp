#include "CommonUtils.h"
#include <string>
#include <algorithm> // Include this header for std::transform
#include <cstdint>
#include <charconv>
#include <stdexcept>
#include <iostream>

namespace CommonUtils {
    std::uintmax_t stringToUintmax(const std::string& str) {
        if (str.empty()) {
            throw std::invalid_argument("Empty string");
        }

        // Ensure string contains only valid characters (digits, no leading/trailing spaces)
        const char* start = str.data();
        const char* end = start + str.size();

        std::uintmax_t result;
        auto [ptr, ec] = std::from_chars(start, end, result);

        if (ec == std::errc::invalid_argument) {
            throw std::invalid_argument("Invalid number format");
        }
        if (ec == std::errc::result_out_of_range) {
            throw std::out_of_range("Number exceeds uintmax_t range");
        }
        if (ptr != end) {
            throw std::invalid_argument("Trailing characters after number");
        }

        return result;
    }


    std::wstring ToLower(const std::wstring& str) {
        std::wstring lowerStr = str;

        std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::towlower);

        return lowerStr;
    }

    void show_circular_progress(std::string str) {
        const char* spinner = "|/-\\";
        static int spinner_index = 0;

        std::cout << "\r" << spinner[spinner_index] << " " << str.c_str() << std::flush;
        spinner_index = (spinner_index + 1) % 4;
    }

    void show_progress_bar(int total, int delay_ms) {
        const char* spinner = "|/-\\";
        int spinner_index = 0;
        const int bar_width = 20; // Width of the progress bar (characters)

        for (int i = 0; i <= total; ++i) {
            // Calculate progress
            int percent = (i * 100) / total;
            int filled = (i * bar_width) / total;

            // Build progress bar
            std::string bar(bar_width, ' ');
            for (int j = 0; j < filled; ++j) {
                bar[j] = '=';
            }

            // Print bar, percentage, and spinner
            std::cout << "\rProgress: [" << bar << "] " << percent << "% " << spinner[spinner_index] << std::flush;

            // Update spinner
            spinner_index = (spinner_index + 1) % 4;

            // Simulate work
         //   std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }

        // Clear the line with final message
        std::cout << "\rDone!                    " << std::endl;
    }




}
