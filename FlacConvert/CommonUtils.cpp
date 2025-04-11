#include "CommonUtils.h"
#include <string>
#include <algorithm> // Include this header for std::transform

namespace CommonUtils {
    std::wstring ToLower(const std::wstring& str) {
        std::wstring lowerStr = str;

        std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::towlower);

        return lowerStr;
    }

    void show_circular_progress(std::string str) {
        const char* spinner = "|/-\\";
        static int spinner_index = 0;

        std::cout << "\r" << str.c_str() << spinner[spinner_index] << std::flush;
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
