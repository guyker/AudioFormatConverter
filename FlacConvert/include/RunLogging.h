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
    std::optional<std::filesystem::path> CurrentLogPath();
    void Finish(int exitCode);
}
