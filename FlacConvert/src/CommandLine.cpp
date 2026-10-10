#include "CommandLine.h"

#include <CLI/CLI.hpp>

#include <iostream>

namespace
{
constexpr const char* kVersion = "1.0.0";

const std::string kConfigurationHelp =
    "Configuration keys (all optional; CLI paths override these values):\n"
    "  Version (string, default 1.0.0): informational config value; --version reports the executable version.\n"
    "  OutDirectory (path, default: C:\\tmp on Windows, system temporary directory elsewhere): default output root for convert, scan, and db import.\n"
    "  DatabaseFileName (string, default all_albums.db): legacy value; db import uses a library-named database.\n"
    "  FLACSettings.ffmpeg_exe_name (string, default ffmpeg): executable used by convert.\n"
    "  FLACSettings.ffmpeg_convert_24bit_flac (string, default -map 0:a:0 -af aresample=osf=s16:osr=44100:dither_method=triangular -c:a flac -y -v warning -stats): compatibility value only; conversion uses fixed options and determines overwrite behavior separately.\n"
    "  FLACSettings.ffmpeg_get_metadata_tags (string, default -v quiet -print_format json -show_format -show_streams -show_chapters): compatibility value only; metadata reads use fixed options or the library API.\n"
    "  MediaDirectoryList (array): legacy menu libraries; subcommands use positional input paths instead.\n"
    "    isActive (bool, default true), mediaPath (path), mediaName (string, defaults to the path's folder name): legacy menu properties.\n"
    "  LogLevel (integer 0-6, default 1): subcommand logging level (trace, debug, info, warn, err, critical, off).\n"
    "  UseAsyncFFmpegCalls (bool, default true): parallel metadata reads while scanning.\n"
    "  UseFFmpegLibraryAPI (bool, default true): use FFmpeg libraries rather than the ffprobe executable for metadata.\n"
    "  ExtraAudioQualityMetrics (bool, default false): include extended audio-quality analysis for duplicate matches.\n"
    "  MinMatchingTracksForDuplicate (integer, default 1): minimum matching tracks for a duplicate candidate.\n"
    "  SizeMatchPercentageThreshold (integer, default 3): track-size matching tolerance, in percent.\n"
    "  RecursionDirectorySearchDepth (integer, default 10): maximum directory depth for scans.\n"
    "  AlbumsSplitThreshold (integer, default 2000): maximum albums per metadata batch.";

void add_config_option(CLI::App* command, CommandLineOptions& options)
{
    command->add_option("--config", options.configPath,
        "Read settings from this JSON file. If omitted, use the persisted config location, then ./config.json, then built-in defaults.")
        ->check([](const std::string& value) {
            return value.empty() ? "configuration path must not be empty" : std::string{};
        })
        ->each([&options](const std::string&) { options.configPathSpecified = true; });
}

std::string command_footer(const std::string& details, const std::string& examples)
{
    return details + "\n\nConfiguration:\n" + kConfigurationHelp + "\n\nExamples:\n" + examples;
}
}

int ParseCommandLine(int argc, const char* const argv[], CommandLineOptions& options)
{
    CLI::App app(
        "Catalog audio libraries, compare duplicate albums, import metadata, and convert audio.");
    app.set_help_flag("-h,--help", "Show this help and exit.");
    app.set_version_flag("--version", kVersion);
    app.require_subcommand(0, 1);
    app.footer(
        "Configuration lookup: explicit --config, persisted config directory, ./config.json, then built-in defaults.\n"
        "Missing implicit config uses built-in defaults (C:\\tmp on Windows, system temporary directory elsewhere); an explicitly requested invalid config is an error.\n\n"
        "Commands:\n"
        "  convert <input-dir>   Convert supported audio into a separate output tree.\n"
        "  scan <library-dir>    Create split JSON metadata files for a library.\n"
        "  duplicates <json-dir> Find duplicate albums from JSON files.\n"
        "  db import <json-dir>  Import JSON metadata into SQLite.\n\n"
        "Run a command with --help for its inputs, outputs, applicable settings, and examples.\n\n"
        "Configuration keys:\n" + kConfigurationHelp);

    auto* convert = app.add_subcommand("convert", "Convert supported audio files to FLAC without changing sources.");
    convert->add_option("input-dir", options.inputPath,
        "Directory tree to convert (FLAC, APE, DSF, DFF, DSD, WV, WAV, M2TS, and M4A).")
        ->required()
        ->check([](const std::string& value) {
            return value.empty() ? "input directory must not be empty" : std::string{};
        });
    convert->add_option("--output-dir", options.outputDirectory,
        "Destination root; preserves paths relative to input-dir. Defaults to config OutDirectory or the platform default (C:\\tmp on Windows).")
        ->check([](const std::string& value) {
            return value.empty() ? "output directory must not be empty" : std::string{};
        })
        ->each([&options](const std::string&) { options.outputDirectorySpecified = true; });
    convert->add_flag("--overwrite", options.overwrite,
        "Replace existing destination files only after each conversion succeeds.");
    add_config_option(convert, options);
    convert->footer(command_footer(
        "Inputs: a directory containing supported audio files. Sources below 16-bit or 44.1 kHz, or whose properties cannot be verified, are rejected and fail the command. Accepted audio is converted to 16-bit, 44.1 kHz FLAC with triangular dithering. Conversion writes .flac files under output-dir, preserving relative directories and filenames. Source files are never replaced. Existing destination files are skipped with an error unless --overwrite is supplied. The output directory is created when needed.",
        "  FlacConvert convert \"D:\\Music\" --output-dir \"E:\\Converted\"\n"
        "  FlacConvert convert \"D:\\Music\" --config \"D:\\settings\\config.json\""));
    convert->callback([&options] { options.action = CommandAction::Convert; });

    auto* scan = app.add_subcommand("scan", "Scan a library and write split JSON metadata files.");
    scan->add_option("library-dir", options.inputPath, "Audio library directory to scan.")
        ->required()
        ->check([](const std::string& value) {
            return value.empty() ? "library directory must not be empty" : std::string{};
        });
    scan->add_option("--output-dir", options.outputDirectory,
        "Destination root. Defaults to config OutDirectory or the platform default (C:\\tmp on Windows).")
        ->check([](const std::string& value) {
            return value.empty() ? "output directory must not be empty" : std::string{};
        })
        ->each([&options](const std::string&) { options.outputDirectorySpecified = true; });
    add_config_option(scan, options);
    scan->footer(command_footer(
        "Inputs: one audio library directory. Output: <output-dir>/<library-name>/media_out_<library-name>_<part>.json. A completed scan replaces the previous generated JSON set only after the new set is ready.",
        "  FlacConvert scan \"D:\\Music\"\n"
        "  FlacConvert scan \"D:\\Music\" --output-dir \"E:\\Catalog\""));
    scan->callback([&options] { options.action = CommandAction::Scan; });

    auto* duplicates = app.add_subcommand("duplicates", "Find duplicate albums using JSON metadata.");
    duplicates->add_option("json-dir", options.inputPath, "Directory containing generated album JSON files.")
        ->required()
        ->check([](const std::string& value) {
            return value.empty() ? "JSON directory must not be empty" : std::string{};
        });
    duplicates->add_flag("--open", options.openDuplicateFolders,
        "Open matching album folders in the operating system's file manager; by default only report matches.");
    add_config_option(duplicates, options);
    duplicates->footer(command_footer(
        "Inputs: a directory containing generated album JSON files. Duplicate reports are printed without prompting. Use --open to launch matching folders in the operating system's file manager.",
        "  FlacConvert duplicates \"C:\\tmp\\Music\"\n"
        "  FlacConvert duplicates \"C:\\tmp\\Music\" --open"));
    duplicates->callback([&options] { options.action = CommandAction::Duplicates; });

    auto* database = app.add_subcommand("db", "Database operations.");
    database->require_subcommand(1, 1);
    auto* databaseImport = database->add_subcommand("import", "Import generated JSON metadata into SQLite.");
    databaseImport->add_option("json-dir", options.inputPath, "Directory containing generated album JSON files.")
        ->required()
        ->check([](const std::string& value) {
            return value.empty() ? "JSON directory must not be empty" : std::string{};
        });
    databaseImport->add_option("--output-dir", options.outputDirectory,
        "Destination root for the library-named database. Defaults to config OutDirectory or the platform default (C:\\tmp on Windows).")
        ->check([](const std::string& value) {
            return value.empty() ? "output directory must not be empty" : std::string{};
        })
        ->each([&options](const std::string&) { options.outputDirectorySpecified = true; });
    add_config_option(databaseImport, options);
    databaseImport->footer(command_footer(
        "Inputs: a directory containing generated album JSON files. Output: <output-dir>/<library-name>/media_out_<library-name>.db. Import is transactional; existing databases receive the imported rows.",
        "  FlacConvert db import \"C:\\tmp\\Music\"\n"
        "  FlacConvert db import \"C:\\tmp\\Music\" --output-dir \"E:\\Catalog\""));
    databaseImport->callback([&options] { options.action = CommandAction::DatabaseImport; });

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& error)
    {
        const int result = app.exit(error, std::cout, std::cerr);
        return result == 0 ? 0 : 2;
    }

    if (options.action == CommandAction::None)
    {
        std::cout << app.help();
        return 0;
    }

    return 1;
}
