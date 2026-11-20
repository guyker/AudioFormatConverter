#include "CommonUtils.h"
#include <string>
#include <algorithm> // Include this header for std::transform
#include <cstdint>
#include <charconv>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <format>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <system_error>
#include "PlatformUtils.h"


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

    std::optional<std::string> fileModificationTimeUtc(const fs::path& path) {
        std::error_code ec;
        const auto modified = fs::last_write_time(path, ec);
        if (ec) {
            return std::nullopt;
        }

        const auto fileNow = fs::file_time_type::clock::now();
        const auto systemNow = std::chrono::system_clock::now();
        // Rounding the epoch offset avoids subsecond sampling jitter while preserving file-clock precision.
        const auto epochOffset = std::chrono::round<std::chrono::seconds>(
            systemNow.time_since_epoch() - fileNow.time_since_epoch());
        const auto systemModified = std::chrono::system_clock::time_point{
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                modified.time_since_epoch() + epochOffset)
        };
        return std::format("{:%FT%TZ}", systemModified);
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

    static int progress_filled(std::size_t total, std::size_t count, int width) {
        if (total == 0 || width <= 0) {
            return 0;
        }
        if (count >= total) {
            return width;
        }
        const auto filled = static_cast<long double>(count) * width / total;
        return static_cast<int>((std::min)(filled, static_cast<long double>(width - 1)));
    }

	std::string get_raw_progress_bar(std::size_t total, std::size_t count, int barWidth) {
		const int bar_width = barWidth; // Width of the progress bar (characters)
		const int filled = progress_filled(total, count, bar_width);
		std::string bar(static_cast<std::size_t>((std::max)(bar_width, 0)), ' ');
		for (int j = 0; j < filled; ++j) {
			bar[j] = '=';
		}
		return bar;
	}

    void show_progress_bar(std::shared_ptr<ProgressBarInfo> progressInfoPtr, ProgressBarType progressType) {
        if (!PlatformUtils::IsStdoutInteractive()) {
            if (progressType == ProgressBarType::Complete) {
                spdlog::debug("Progress completed: {} ({}/{}).",
                    progressInfoPtr->name, progressInfoPtr->count, progressInfoPtr->size);
            }
            return;
        }
        render_progress_bar(std::move(progressInfoPtr), progressType, PlatformUtils::EnableVirtualTerminal());
    }

    // Console output stays 7-bit ASCII so it renders with any console font, code page or log viewer.
    void render_progress_bar(std::shared_ptr<ProgressBarInfo> progressInfoPtr, ProgressBarType progressType,
        bool useTerminalControl) {
        static constexpr std::string_view spinner = "|/-\\";
        static constexpr std::string_view doneMarker = "[done]";
        static std::size_t spinner_index = 0;
        static std::size_t lastLineLength = 0;

        const int bar_width = progressInfoPtr->bar_size; // Width of the progress bar (characters)
        const int sub_bar_width_total = progressInfoPtr->bar_size; // Width of the progress bar (characters)

        static std::chrono::steady_clock::time_point lastTime{ std::chrono::steady_clock::now() };
        auto currentTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime).count();


		auto count = progressInfoPtr->count;
		auto size = progressInfoPtr->size;
        auto av_duration = avarage_duration(count, size);


        auto normalized_count = std::min<size_t>(count, size);
        const int i = progress_filled(size, normalized_count, bar_width);
        const int percent = progress_filled(
            static_cast<std::size_t>((std::max)(bar_width, 0)), static_cast<std::size_t>(i), 100);
        auto bar = get_raw_progress_bar(size, normalized_count, 20);
        std::string avarageDuration = (size <= count) ? "" :
            " (" + CommonUtils::GetDurationinString(static_cast<long long>(av_duration * (size - count))) + ")";
            //std::to_string(count) + "/" + std::to_string(size) + ")";


        bool bSubProgress = progressInfoPtr->subProgressInfoPtr != nullptr;
        if (useTerminalControl) {
            std::cout << "\33[2K\r";  // Clear entire line and move cursor to start
            if (bSubProgress) {
                std::cout << "\33[A\33[2K\r";  // Move up 1 line, clear it, and return to start
            }
        }
        else if (lastLineLength > 0) {
            std::cout << '\r' << std::string(lastLineLength, ' ') << '\r';
        }
        lastLineLength = 0;

		std::string subProgressStr = "";
        if (bSubProgress)
        {
            size_t sub_currentCount = 0;
            size_t sub_totalCount = 0;
            if (progressInfoPtr->subProgressInfoPtr != nullptr)
            {
                sub_currentCount = progressInfoPtr->subProgressInfoPtr->count;
                sub_totalCount = progressInfoPtr->subProgressInfoPtr->size;
            }
            auto sub_normalized_total_count = std::min<size_t>(sub_currentCount, sub_totalCount);

            const int i2 = progress_filled(sub_totalCount, sub_normalized_total_count, sub_bar_width_total);

            const int sub_percent_total = progress_filled(
                static_cast<std::size_t>((std::max)(sub_bar_width_total, 0)), static_cast<std::size_t>(i2), 100);
            auto sub_bar_total = get_raw_progress_bar(sub_totalCount, sub_normalized_total_count, progressInfoPtr->subProgressInfoPtr->bar_size);
            std::string sub_avarageDurationTotal = (sub_totalCount <= sub_currentCount) ? "" :
                " (" + CommonUtils::GetDurationinString(static_cast<long long>(av_duration * (sub_totalCount - sub_currentCount))) + ")";
                //+" - " + std::to_string(sub_currentCount) + "/" + std::to_string(sub_totalCount) + ")";

            subProgressStr = std::format("Current Batch:      [{}] {}% {}/{}{} \"{}\"",
                sub_bar_total, sub_percent_total,
				sub_currentCount, sub_totalCount,
                sub_avarageDurationTotal, progressInfoPtr->name);
        }
	
        // Print bar, percentage, and spinner
        if (progressType == ProgressBarType::Complete)
        {
            // Logged text also reaches session log files, so it carries no terminal control sequences.
            std::cout << std::flush;
            spdlog::info("{} Progress: [{}] {}% {}/{} - {}", doneMarker, bar, percent, normalized_count, size,
                progressInfoPtr->name);
        }
        else
        {
            std::string progressTotalStr = std::format("Overall Progress: {} [{}] {}% {}/{}{}",
                spinner[spinner_index],
                bar, percent,
                progressInfoPtr->count,
                progressInfoPtr->size,
                avarageDuration);

            if (!bSubProgress) {
                std::cout << progressTotalStr;
                lastLineLength = progressTotalStr.size();
            }
            else if (useTerminalControl) {
                std::cout << progressTotalStr << '\n' << subProgressStr;
            }
            else {
                // Without cursor movement both bars share one rewritable line.
                const auto line = progressTotalStr + " | " + subProgressStr;
                std::cout << line;
                lastLineLength = line.size();
            }
            std::cout << std::flush;

            spinner_index = (spinner_index + 1) % spinner.size();
        }
    }

    // Deletes all regular files in the same directory as 'filePath'
// whose filenames start with the filename of 'filePath'.
// Returns the number of files removed.
    std::size_t deleteFilesWithSamePrefix(const fs::path& filePath) {
        std::size_t removedCount = 0;
        std::error_code ec;

        // Derive prefix from the filename
        const std::string prefix = CommonUtils::path_to_utf8(filePath.filename());
        if (prefix.empty()) {
            std::cerr << "Error: filePath has no filename component: "
                << CommonUtils::path_to_utf8(filePath) << "\n";
            return 0;
        }

        // Get parent directory
        fs::path dir = filePath.parent_path();
        if (dir.empty() || !fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
//            std::cerr << "Error: invalid directory: " << dir << " (" << ec.message() << ")\n";
            return 0;
        }

        // Iterate through the directory
        for (auto const& entry : fs::directory_iterator(dir, ec)) {
            if (ec) {
                std::cerr << "Directory iteration error: " << ec.message() << "\n";
                break;
            }
            // Only consider regular files
            if (!entry.is_regular_file(ec) || ec)
                continue;

            const std::string fname = CommonUtils::path_to_utf8(entry.path().filename());
            // If filename starts with our prefix, delete it
            if (fname.rfind(prefix, 0) == 0) {
                if (fs::remove(entry.path(), ec)) {
                    ++removedCount;
                }
                else {
                    std::cerr << "Failed to remove " << CommonUtils::path_to_utf8(entry.path())
                        << ": " << ec.message() << "\n";
                }
            }
        }

        return removedCount;
    }
}
