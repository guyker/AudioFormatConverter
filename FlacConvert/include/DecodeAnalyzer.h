#pragma once

#include "MediaRecord.h"

#include <filesystem>

class DecodeAnalyzer
{
public:
    static void Analyze(const std::filesystem::path& filePath, MediaRecord& record,
        bool includeSignalAnalysis);
};
