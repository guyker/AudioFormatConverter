#pragma once

#include <filesystem>
#include <optional>
#include <string>

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
}
