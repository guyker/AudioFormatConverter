#pragma once

#include <cstddef>
#include <set>
#include <string>
#include "ConversionOptions.h"

enum class CommandAction
{
    None,
    Convert,
    Repair,
    Scan,
    Duplicates,
    DatabaseImport
};

struct CommandLineOptions
{
    CommandAction action{ CommandAction::None };
    std::string inputPath;
    std::string outputDirectory;
    std::string configPath;
    std::string logLevel;
    std::string reportPath;
    std::set<std::string> repairRules;
    std::size_t repairJobs{ 4 };
    bool repairDryRun{ false };
    bool repairRemoveId3v1{ false };
    bool outputDirectorySpecified{ false };
    bool configPathSpecified{ false };
    bool overwrite{ false };
    bool noMetadataSidecar{ false };
    bool openDuplicateFolders{ false };
    bool fullRescan{ false };
    bool archiveFlacMetadata{ false };
    bool noArchiveFlacMetadata{ false };
    std::string scanTier{ "catalog" };
    bool scanTierSpecified{ false };
    ConversionOptions conversion;
};

int ParseCommandLine(int argc, const char* const argv[], CommandLineOptions& options);
