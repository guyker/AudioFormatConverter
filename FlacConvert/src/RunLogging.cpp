#include "RunLogging.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <memory>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif

#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "CommonUtils.h"
#include "PlatformUtils.h"

namespace
{
namespace fs = std::filesystem;
constexpr auto kRetention = std::chrono::hours(24 * 30);
constexpr std::uintmax_t kMaximumLogBytes = 1024ull * 1024 * 1024;
std::atomic<std::uint64_t> runSequence{ 0 };
std::shared_ptr<spdlog::sinks::sink> fileSink;
std::optional<fs::path> runLogPath;
fs::path activeMarkerPath;
std::chrono::steady_clock::time_point runStartedAt;

class ProgressConsoleSink final : public spdlog::sinks::sink
{
public:
    ProgressConsoleSink() : _sink(std::make_shared<spdlog::sinks::stdout_color_sink_mt>())
    {
        _sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%^%l%$] %v");
    }

    void log(const spdlog::details::log_msg& message) override
    {
        std::lock_guard lock(_mutex);
        ClearLine();
        _sink->log(message);
    }
    void flush() override
    {
        std::lock_guard lock(_mutex);
        _sink->flush();
    }
    void set_pattern(const std::string& pattern) override
    {
        std::lock_guard lock(_mutex);
        _sink->set_pattern(pattern);
    }
    void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override
    {
        std::lock_guard lock(_mutex);
        _sink->set_formatter(std::move(formatter));
    }
    void Show(const std::string& message)
    {
        std::lock_guard lock(_mutex);
        if (!PlatformUtils::IsStdoutInteractive()) return;
        const auto padding = _lineLength > message.size() ? _lineLength - message.size() : 0;
        std::cout << '\r' << message << std::string(padding, ' ') << std::flush;
        _lineLength = message.size();
    }
    void Clear()
    {
        std::lock_guard lock(_mutex);
        ClearLine();
    }

private:
    void ClearLine()
    {
        if (_lineLength == 0) return;
        std::cout << '\r' << std::string(_lineLength, ' ') << '\r' << std::flush;
        _lineLength = 0;
    }
    std::shared_ptr<spdlog::sinks::sink> _sink;
    std::mutex _mutex;
    std::size_t _lineLength{ 0 };
};

std::shared_ptr<ProgressConsoleSink> consoleSink;

unsigned long currentProcessId()
{
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(getpid());
#endif
}

bool processIsAlive(unsigned long processId)
{
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) return GetLastError() != ERROR_INVALID_PARAMETER;
    DWORD exitCode = 0;
    const bool alive = !GetExitCodeProcess(process, &exitCode) || exitCode == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
#else
    if (processId == 0) return false;
    if (kill(static_cast<pid_t>(processId), 0) == 0) return true;
    return errno == EPERM;
#endif
}

fs::path defaultLogDirectory()
{
#ifdef _WIN32
    const DWORD localAppDataLength = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (localAppDataLength > 1) {
        std::vector<wchar_t> localAppData(localAppDataLength);
        const DWORD copied = GetEnvironmentVariableW(
            L"LOCALAPPDATA", localAppData.data(), localAppDataLength);
        if (copied > 0 && copied < localAppDataLength) {
            return fs::path(localAppData.data()) / L"AudioFormatConverter" / L"logs";
        }
    }
#else
    const char* stateHome = std::getenv("XDG_STATE_HOME");
    if (stateHome != nullptr && *stateHome != '\0') {
        return fs::path(stateHome) / "audioformatconverter" / "logs";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return fs::path(home) / ".local" / "state" / "audioformatconverter" / "logs";
    }
#endif
    std::error_code ec;
    auto temporary = fs::temp_directory_path(ec);
    if (ec) throw fs::filesystem_error("Cannot locate a log directory", ec);
    return temporary / "AudioFormatConverter" / "logs";
}

std::string runId()
{
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif
    std::ostringstream result;
    result << std::put_time(&localTime, "%Y%m%d_%H%M%S") << '_'
        << std::setw(3) << std::setfill('0') << milliseconds << '_'
        << currentProcessId() << '_'
        << runSequence.fetch_add(1, std::memory_order_relaxed);
    return result.str();
}

spdlog::filename_t toFilename(const fs::path& path)
{
#ifdef _WIN32
    return path.wstring();
#else
    return CommonUtils::path_to_utf8(path);
#endif
}

std::shared_ptr<spdlog::sinks::sink> createConsoleSink()
{
    consoleSink = std::make_shared<ProgressConsoleSink>();
    consoleSink->set_level(spdlog::level::info);
    return consoleSink;
}

bool isActiveLog(const fs::path& path)
{
    auto marker = path;
    marker += ".active";
    std::error_code ec;
    if (!fs::exists(marker, ec)) return static_cast<bool>(ec);

    std::ifstream input(marker);
    unsigned long processId = 0;
    input >> processId;
    if (!input || processId == 0 || processIsAlive(processId)) return true;

    fs::remove(marker, ec);
    return false;
}

void pruneLogs(const fs::path& directory, const fs::path& currentLog)
{
    struct LogFile
    {
        fs::path path;
        fs::file_time_type modified;
        std::uintmax_t size;
    };
    std::vector<LogFile> inactiveLogs;
    std::uintmax_t totalBytes = 0;
    std::error_code ec;
    const auto now = fs::file_time_type::clock::now();
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        const auto status = it->symlink_status(ec);
        const auto filename = CommonUtils::path_to_utf8(it->path().filename());
        if (ec || !fs::is_regular_file(status) || it->path().extension() != ".log" ||
            !filename.starts_with("afc-")) {
            ec.clear();
            continue;
        }
        const auto size = it->file_size(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        totalBytes += size;
        if (it->path() == currentLog || isActiveLog(it->path())) continue;

        const auto modified = it->last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        if (now - modified > kRetention) {
            if (fs::remove(it->path(), ec) && !ec) totalBytes -= size;
            if (ec) spdlog::warn("Could not remove expired session log '{}': {}",
                CommonUtils::path_to_utf8(it->path()), ec.message());
            ec.clear();
            continue;
        }
        inactiveLogs.push_back({ it->path(), modified, size });
    }
    if (ec) {
        spdlog::warn("Could not inspect all session logs for retention cleanup: {}", ec.message());
        return;
    }
    std::ranges::sort(inactiveLogs, {}, &LogFile::modified);
    for (const auto& log : inactiveLogs) {
        if (totalBytes <= kMaximumLogBytes) break;
        if (fs::remove(log.path, ec) && !ec) totalBytes -= log.size;
        if (ec) spdlog::warn("Could not remove session log '{}': {}",
            CommonUtils::path_to_utf8(log.path), ec.message());
        ec.clear();
    }
    if (totalBytes > kMaximumLogBytes) {
        spdlog::warn("Session logs exceed the 1 GiB retention limit; active or inaccessible logs were retained.");
    }
}
}

bool RunLogging::Start(const std::string& operation,
    const std::optional<std::filesystem::path>& logDirectory)
{
    runStartedAt = std::chrono::steady_clock::now();
    std::vector<spdlog::sink_ptr> sinks{ createConsoleSink() };
    fileSink.reset();
    runLogPath.reset();
    activeMarkerPath.clear();

    try {
        const auto directory = logDirectory ? *logDirectory : defaultLogDirectory();
        std::error_code ec;
        fs::create_directories(directory, ec);
        if (ec) {
            throw fs::filesystem_error("Cannot create the session log directory", directory, ec);
        }
        if (!fs::is_directory(directory, ec)) {
            if (ec) throw fs::filesystem_error("Cannot inspect the session log directory", directory, ec);
            throw fs::filesystem_error("Session log path is not a directory", directory,
                std::make_error_code(std::errc::not_a_directory));
        }

        runLogPath = directory / ("afc-" + runId() + ".log");
        activeMarkerPath = *runLogPath;
        activeMarkerPath += ".active";
        {
            std::ofstream marker(activeMarkerPath, std::ios::out | std::ios::trunc);
            if (!marker) throw std::runtime_error("Cannot reserve the active session log marker.");
            marker << currentProcessId() << '\n';
            marker.flush();
            if (!marker) throw std::runtime_error("Cannot write the active session log marker.");
        }

        auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>(toFilename(*runLogPath), false);
        file->set_level(spdlog::level::debug);
        file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%l] %v");
        fileSink = file;
        sinks.push_back(file);
    }
    catch (const std::exception& error) {
        if (!activeMarkerPath.empty()) {
            std::error_code ec;
            fs::remove(activeMarkerPath, ec);
        }
        runLogPath.reset();
        activeMarkerPath.clear();
        auto logger = std::make_shared<spdlog::logger>("afc", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::trace);
        spdlog::set_default_logger(std::move(logger));
        spdlog::error("Session log could not be created; continuing with console logging only: {}",
            error.what());
        return false;
    }

    auto logger = std::make_shared<spdlog::logger>("afc", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(std::move(logger));
    spdlog::info("Request started: {} (run {})", operation, runLogPath->stem().string());
    spdlog::info("Session log: {}", CommonUtils::path_to_utf8(*runLogPath));
    pruneLogs(runLogPath->parent_path(), *runLogPath);
    return true;
}

void RunLogging::SetFileLevel(spdlog::level::level_enum level)
{
    if (fileSink) fileSink->set_level(level);
}

void RunLogging::RecordError(const std::string& message)
{
    if (fileSink) {
        spdlog::logger logger("afc-file", fileSink);
        logger.error("{}", message);
        logger.flush();
    }
}

void RunLogging::ShowProgress(const std::string& message)
{
    if (consoleSink) consoleSink->Show(message);
}

void RunLogging::ClearProgress()
{
    if (consoleSink) consoleSink->Clear();
}

namespace
{
std::string formatElapsed(long long seconds)
{
    if (seconds < 60) return std::to_string(seconds) + "s";
    if (seconds < 3600) return std::to_string(seconds / 60) + "m " + std::to_string(seconds % 60) + "s";
    return std::to_string(seconds / 3600) + "h " + std::to_string(seconds % 3600 / 60) + "m";
}
}

RunLogging::PhaseProgress::PhaseProgress(std::string label, std::optional<std::size_t> total,
    std::string unit, std::string secondaryUnit)
    : _label(std::move(label)), _total(total), _unit(std::move(unit)), _secondaryUnit(std::move(secondaryUnit))
{
    spdlog::info("{}", Message());
    const bool interactive = PlatformUtils::IsStdoutInteractive();
    _worker = std::jthread([this, interactive](std::stop_token stop) {
        std::unique_lock lock(_mutex);
        unsigned ticks = 0;
        while (!_wake.wait_for(lock, stop, std::chrono::seconds(1), [] { return false; })) {
            if (stop.stop_requested()) break;
            if (interactive) {
                ShowProgress(Message());
            }
            else if (++ticks % 5 == 0) {
                spdlog::info("{}", Message());
            }
        }
    });
}

RunLogging::PhaseProgress::~PhaseProgress()
{
    Stop();
}

void RunLogging::PhaseProgress::Finish(bool success)
{
    Stop();
    if (_finished) return;
    _finished = true;
    spdlog::info("{} [{}].", Message(), success ? "done" : "stopped");
}

void RunLogging::PhaseProgress::Stop()
{
    if (_worker.joinable()) {
        _worker.request_stop();
        _worker.join();
        ClearProgress();
    }
}

std::string RunLogging::PhaseProgress::Message() const
{
    const auto count = _completed.load(std::memory_order_relaxed);
    std::string message = _label + ": " + std::to_string(count);
    if (_total) message += "/" + std::to_string(*_total);
    message += " " + _unit;
    if (_total) {
        const auto percent = *_total == 0 ? 100 :
            static_cast<unsigned>(static_cast<long double>((std::min)(count, *_total)) * 100 / *_total);
        message += " (" + std::to_string(percent) + "%)";
    }
    else message += " (total unknown)";
    if (!_secondaryUnit.empty()) {
        message += ", " + std::to_string(_secondary.load(std::memory_order_relaxed)) + " " + _secondaryUnit;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - _started).count();
    message += ", " + formatElapsed(elapsed);
    if (_total && count > 0 && count < *_total && elapsed >= 5) {
        const auto remaining = static_cast<long long>(
            static_cast<long double>(elapsed) * (*_total - count) / count);
        message += ", ETA " + formatElapsed(remaining);
    }
    return message;
}

std::optional<std::filesystem::path> RunLogging::CurrentLogPath()
{
    return runLogPath;
}

void RunLogging::Finish(int exitCode)
{
    ClearProgress();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - runStartedAt).count();
    if (runLogPath) {
        if (fileSink) fileSink->set_level(spdlog::level::info);
        spdlog::info("Request finished: {} (exit code {}, {} ms).",
            exitCode == 0 ? "completed" : "failed", exitCode, elapsed);
        if (auto logger = spdlog::default_logger()) logger->flush();
        auto console = createConsoleSink();
        auto logger = std::make_shared<spdlog::logger>("afc", console);
        logger->set_level(spdlog::level::trace);
        spdlog::set_default_logger(std::move(logger));
        fileSink.reset();
        std::error_code ec;
        fs::remove(activeMarkerPath, ec);
        if (ec) spdlog::warn("Could not release active session log marker '{}': {}",
            CommonUtils::path_to_utf8(activeMarkerPath), ec.message());
    }
}
