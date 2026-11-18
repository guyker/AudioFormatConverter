#pragma once

#include "MediaRecord.h"

#include <filesystem>
#include <optional>
#include <string>

struct TagLibReadResult
{
    std::optional<MediaRecord> record;
    std::optional<std::string> error;
    std::vector<MediaDiagnostic> diagnostics;
};

class TagLibReader
{
public:
    static TagLibReadResult Read(const std::filesystem::path& filePath,
        const std::string& relativePath);
};
