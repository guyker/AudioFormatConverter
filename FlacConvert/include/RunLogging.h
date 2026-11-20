#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <spdlog/common.h>

namespace RunLogging
{
    bool Start(const std::string& operation,
        const std::optional<std::filesystem::path>& logDirectory = std::nullopt);
    void SetFileLevel(spdlog::level::level_enum level);
    void RecordError(const std::string& message);
    // Transient console-only progress; ordinary log messages end the active line first.
    void ShowProgress(const std::string& message);
    void ClearProgress();
    std::optional<std::filesystem::path> CurrentLogPath();
    void Finish(int exitCode);

    // Phase progress for long operations: logs the start, redraws one console line every second when interactive,
    // logs a snapshot every five seconds when redirected, and logs a final line. Counters are thread-safe.
    // Message: "<label>: <count>[/<total>] <unit> (<percent>% | total unknown)[, <n> <secondaryUnit>], <elapsed>[, ETA]".
    class PhaseProgress
    {
    public:
        PhaseProgress(std::string label, std::optional<std::size_t> total = std::nullopt,
            std::string unit = "files", std::string secondaryUnit = {});
        ~PhaseProgress();
        PhaseProgress(const PhaseProgress&) = delete;
        PhaseProgress& operator=(const PhaseProgress&) = delete;

        void Advance(std::size_t count = 1) { _completed.fetch_add(count, std::memory_order_relaxed); }
        void AdvanceSecondary(std::size_t count = 1) { _secondary.fetch_add(count, std::memory_order_relaxed); }
        void Finish(bool success = true);

    private:
        std::string Message() const;
        void Stop();

        std::string _label;
        std::optional<std::size_t> _total;
        std::string _unit;
        std::string _secondaryUnit;
        std::atomic_size_t _completed{ 0 };
        std::atomic_size_t _secondary{ 0 };
        std::chrono::steady_clock::time_point _started{ std::chrono::steady_clock::now() };
        bool _finished = false;
        std::mutex _mutex;
        std::condition_variable_any _wake;
        std::jthread _worker;
    };
}
