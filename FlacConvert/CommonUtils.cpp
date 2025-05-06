#include "CommonUtils.h"
#include <string>
#include <algorithm> // Include this header for std::transform
#include <cstdint>
#include <charconv>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <filesystem>
#include <spdlog/spdlog.h>


namespace CommonUtils {
    
	// Function to convert a string to uintmax_t
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
		static int last_message_length = 0;
		if (last_message_length > 0) {
			// Clear the last line
			std::string emptyLastLineString = std::string(last_message_length, ' ');
			std::cout << "\r" << emptyLastLineString.c_str() << std::flush;
		}
		last_message_length = static_cast<int>(str.length()) + 2; // +2 for spinner and space

        const char* spinner = "|/-\\";
        static int spinner_index = 0;

        std::cout << "\r" << spinner[spinner_index] << " " << str.c_str() << std::flush;
        spinner_index = (spinner_index + 1) % 4;
    }





    static std::chrono::steady_clock::time_point lastTime{ std::chrono::steady_clock::now() };


    double avarage_duration(long long count, long long total) {
        using Clock = std::chrono::steady_clock;
        static auto lastTime = Clock::now();
        static double sumMs = 0.0;
        static long long calls = 0;

        auto now = Clock::now();
        if (calls > 0) {
            // milliseconds since last call
            double delta = std::chrono::duration<double, std::milli>(now - lastTime).count();
            sumMs += delta;
            double avgMs = sumMs / calls;

            //std::cout
            //    << "\r[" << count << "/" << total << "] "
            //    << "Avg interval: " << avgMs << " ms"
            //    << std::flush;

            ++calls;
            lastTime = now;
            return avgMs;
        }
        ++calls;
        lastTime = now;

        return 0;
    }

    void show_progress_bar(int total, std::string prefix, size_t count, size_t size, std::string name, size_t currentCount, size_t totalCount, bool isCompleted) {
        //const char* spinner = "|/-\\";
        static std::string spinnerDone = std::string(reinterpret_cast<const char*>(u8"\u2714"));
        static std::string spinner = std::string(reinterpret_cast<const char*>(u8"\u280B\u2819\u2839\u2838\u283C\u2834\u2826\u2827\u2807\u280F"));
		static auto spinnerSize = CommonUtils::utf8_char_count( spinner);
        static int spinner_index = 0;

        const int bar_width = 20; // Width of the progress bar (characters)

        static std::chrono::steady_clock::time_point lastTime{ std::chrono::steady_clock::now() };
		
        auto currentTime = std::chrono::steady_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime).count();


        auto normalized_count = std::min<size_t>(count, size);
        int i = bar_width * normalized_count / size;

        int percent = (i * 100) / total;
        int filled = (i * bar_width) / total;

        // Build progress bar
        std::string bar(bar_width, ' ');
        for (int j = 0; j < filled; ++j) {
            bar[j] = '=';
        }

        auto av_duration = avarage_duration(count, size);        
		std::string avarageDuration = (size <= count) ? "" : " (" + CommonUtils::GetDurationinString(static_cast<long long>(av_duration * (size - count))) + ")";
		std::string avarageDurationTotal = (totalCount <= currentCount) ? "" : " (" + CommonUtils::GetDurationinString(static_cast<long long>(av_duration * (totalCount - currentCount))) + ")";

        std::cout << "\r" << "\033[K " << std::flush;              // Move cursor to start of line and clear from cursor to end of line

        // Print bar, percentage, and spinner
        if (isCompleted)
        {
            std::cout << "\r";
            //std::string green_bar = "[\033[32m" + bar + "\033[0m]";
            std::string green_bar = "[" + bar + "]";
            std::string green_name = "\033[32m" + name + "\033[0m";
			std::string spinnerDoneGreen = "\033[32m" + spinnerDone + "\033[0m";
            spdlog::info("{}  Progress: {} {}% {}/{} - {}", spinnerDoneGreen, green_bar, percent, normalized_count, size, green_name);
        }
        else
        {            
			auto spinnerChar = CommonUtils::get_utf8_char_at(spinner, spinner_index);
            std::string progressStr = std::format("\rProgress: [{}] {} {}% {}/{}{}/{} - {}", 
                bar, spinnerChar, percent, normalized_count, size, avarageDuration, avarageDurationTotal, name);
            std::cout << progressStr << std::flush;

            spinner_index = (spinner_index + 1) % spinnerSize;
        }

    }


    void show_progress_bar2(int total, int delay_ms) {
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
