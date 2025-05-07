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

	std::string get_raw_progress_bar(int total, int count, int barWidth) {
		const int bar_width = barWidth; // Width of the progress bar (characters)
		int filled = (count * bar_width) / total;
		std::string bar(bar_width, ' ');
		for (int j = 0; j < filled; ++j) {
			bar[j] = '=';
		}
		return bar;
	}
//    void show_progress_bar(ProgressBarInfo progressInfo, std::optional<ProgressBarInfo> progressSubInfo, ProgressBarType progressType) {
    //void show_progress_bar(int total, std::string prefix, size_t count, size_t size,
    //    std::string name, size_t currentCount, size_t totalCount,

    void show_progress_bar(std::shared_ptr<ProgressBarInfo> progressInfoPtr, ProgressBarType progressType) {
        //const char* spinner = "|/-\\";
        static std::string spinnerDone = std::string(reinterpret_cast<const char*>(u8"\u2714"));
        static std::string spinner = std::string(reinterpret_cast<const char*>(u8"\u280B\u2819\u2839\u2838\u283C\u2834\u2826\u2827\u2807\u280F"));
        static auto spinnerSize = CommonUtils::utf8_char_count(spinner);
        static int spinner_index = 0;

        const int bar_width = progressInfoPtr->bar_size; // Width of the progress bar (characters)
        const int sub_bar_width_total = progressInfoPtr->bar_size; // Width of the progress bar (characters)

        static std::chrono::steady_clock::time_point lastTime{ std::chrono::steady_clock::now() };
        auto currentTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime).count();


		auto count = progressInfoPtr->count;
		auto size = progressInfoPtr->size;
        auto av_duration = avarage_duration(count, size);


        auto normalized_count = std::min<size_t>(count, size);
        int i = bar_width * normalized_count / size;
        int percent = (i * 100) / bar_width;
        auto bar = get_raw_progress_bar(size, normalized_count, 20);
        std::string avarageDuration = (size <= count) ? "" :
            " (" + CommonUtils::GetDurationinString(static_cast<long long>(av_duration * (size - count))) + " - " + 
            std::to_string(count) + "/" + std::to_string(size) + ")";


        std::cout << "\33[2K\r";  // Clear entire line and move cursor to start
        
        bool bSubProgress = progressInfoPtr->subProgressInfoPtr != nullptr;
		std::string subProgressStr = "";
        if (bSubProgress)
        {
            std::cout << "\33[A\33[2K\r";  // Move up 1 line, clear it, and return to start

            size_t sub_currentCount = 0;
            size_t sub_totalCount = 0;
            if (progressInfoPtr->subProgressInfoPtr != nullptr)
            {
                sub_currentCount = progressInfoPtr->subProgressInfoPtr->count;
                sub_totalCount = progressInfoPtr->subProgressInfoPtr->size;
            }
            auto sub_normalized_total_count = std::min<size_t>(sub_currentCount, sub_totalCount);

            auto i2 = sub_bar_width_total * sub_normalized_total_count / sub_totalCount;

            int sub_percent_total = (i2 * 100) / sub_bar_width_total;
            auto sub_bar_total = get_raw_progress_bar(sub_totalCount, sub_normalized_total_count, 20);
            std::string sub_avarageDurationTotal = (sub_totalCount <= sub_currentCount) ? "" :
                " (" + CommonUtils::GetDurationinString(static_cast<long long>(av_duration * (sub_totalCount - sub_currentCount))) + " - " +
                std::to_string(sub_currentCount) + "/" + std::to_string(sub_totalCount) + ")";

            subProgressStr = std::format("Current Batch:      [{}] {}%{} \"{}\"",
                sub_bar_total, sub_percent_total, sub_avarageDurationTotal, progressInfoPtr->name);
        }
	
        // Print bar, percentage, and spinner
        if (progressType == ProgressBarType::Complete)
        {
         //   std::cout << "\r";
            std::string green_name = "\033[32m" + progressInfoPtr->name + "\033[0m";
            std::string spinnerDoneGreen = "\033[32m" + spinnerDone + "\033[0m";
            spdlog::info("{}  Progress: {} {}% {}/{} - {}", spinnerDoneGreen, bar, percent, normalized_count, size, green_name);
        }
        else
        {
            auto spinnerChar = CommonUtils::get_utf8_char_at(spinner, spinner_index);
            std::string progressTotalStr = std::format("Overall Progress: {} [{}] {}%{}",
                spinnerChar,
                bar, percent, avarageDuration);

            std::cout << progressTotalStr;

			if (bSubProgress)
			{
                std::cout << std::endl << subProgressStr << std::flush;
            }

            spinner_index = (spinner_index + 1) % spinnerSize;
        }
    }
}
