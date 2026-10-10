#pragma once

#include <string>
#include "ConversionOptions.h"

enum class CommandAction
{
    None,
    Convert,
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
    bool outputDirectorySpecified{ false };
    bool configPathSpecified{ false };
    bool overwrite{ false };
    bool openDuplicateFolders{ false };
    ConversionOptions conversion;
};

int ParseCommandLine(int argc, const char* const argv[], CommandLineOptions& options);
