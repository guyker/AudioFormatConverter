#include "CommandLine.h"
#include "RunLogging.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::string readFile(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not read session log.");
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}
}

int main()
{
    auto testName = fs::path(u8"afc-log-\u97f3-");
    testName += std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto testDirectory = fs::temp_directory_path() / testName;
    try {
        require(fs::create_directory(testDirectory), "Could not create session-log test directory.");

        const auto oldLog = testDirectory / "afc-expired.log";
        const auto unrelatedLog = testDirectory / "unrelated.log";
        const auto activeLog = testDirectory / "afc-active.log";
        for (const auto& file : { oldLog, unrelatedLog, activeLog }) {
            std::ofstream(file) << "retain only if not expired or active";
            fs::last_write_time(file, fs::file_time_type::clock::now() - std::chrono::hours(24 * 31));
        }
        auto marker = activeLog;
        marker += ".active";
#ifdef _WIN32
        std::ofstream(marker) << _getpid();
#else
        std::ofstream(marker) << getpid();
#endif
        require(RunLogging::Start("logging-test", testDirectory),
            "Could not start the session logger.");
        require(!fs::exists(oldLog), "Expired owned log was not pruned.");
        require(fs::exists(activeLog) && fs::exists(marker),
            "An active log was pruned.");
        require(fs::exists(unrelatedLog), "Retention pruned an unrelated log.");
        const auto infoLog = RunLogging::CurrentLogPath();
        require(infoLog.has_value(), "Session log path was not returned.");
        RunLogging::SetFileLevel(spdlog::level::info);
        spdlog::debug("debug detail excluded");
        spdlog::info("session info retained");
        RunLogging::ShowProgress("transient progress 1/100");
        RunLogging::ShowProgress("transient progress 100/100");
        spdlog::warn("session warning retained");
        RunLogging::ShowProgress("transient progress after warning");
        RunLogging::ClearProgress();
        RunLogging::Finish(0);
        auto activeMarker = *infoLog;
        activeMarker += ".active";
        require(!fs::exists(activeMarker),
            "Completed session retained its active marker.");
        spdlog::shutdown();

        const auto infoContent = readFile(*infoLog);
        require(infoContent.find("Request started: logging-test") != std::string::npos,
            "Session start was not recorded.");
        require(infoContent.find("session info retained") != std::string::npos &&
            infoContent.find("session warning retained") != std::string::npos,
            "Configured session details or warning were not recorded.");
        require(infoContent.find("debug detail excluded") == std::string::npos,
            "The detailed file-log threshold was not applied.");
        require(infoContent.find("transient progress") == std::string::npos &&
            infoContent.find('\x1b') == std::string::npos,
            "Transient progress or terminal control characters leaked into the session log.");

        require(RunLogging::Start("trace-test", testDirectory),
            "Could not start a second request logger.");
        const auto traceLog = RunLogging::CurrentLogPath();
        require(traceLog.has_value() && *traceLog != *infoLog,
            "Each request must receive a unique log file.");
        RunLogging::SetFileLevel(spdlog::level::trace);
        spdlog::debug("trace request detail");
        RunLogging::Finish(0);
        spdlog::shutdown();
        require(readFile(*traceLog).find("trace request detail") != std::string::npos,
            "The trace threshold did not retain detailed diagnostics.");

        require(RunLogging::Start("off-test", testDirectory),
            "Could not start a request with logging disabled.");
        const auto offLog = RunLogging::CurrentLogPath();
        RunLogging::SetFileLevel(spdlog::level::off);
        spdlog::info("disabled detail");
        RunLogging::Finish(1);
        spdlog::shutdown();
        require(offLog.has_value() && readFile(*offLog).find("Request finished: failed") != std::string::npos,
            "A failed request did not retain its finish status when detail logging was disabled.");
        require(readFile(*offLog).find("disabled detail") == std::string::npos,
            "An off threshold did not suppress ordinary details.");

        const char* arguments[] = {
            "FlacConvert", "scan", "library", "--log-level", "trace"
        };
        CommandLineOptions options;
        require(ParseCommandLine(5, arguments, options) == 1 && options.logLevel == "trace",
            "The CLI did not accept the detailed log-level override.");

        require(!RunLogging::Start("unwritable", unrelatedLog),
            "A file was accepted as the session log directory.");
        require(!RunLogging::CurrentLogPath(), "A failed log start reported an available path.");
        RunLogging::Finish(1);
        spdlog::shutdown();
        fs::remove_all(testDirectory);
        return 0;
    }
    catch (const std::exception& error) {
        spdlog::shutdown();
        std::error_code cleanupError;
        fs::remove_all(testDirectory, cleanupError);
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
