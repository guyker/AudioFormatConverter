#include "CommandLine.h"
#include "RepairRules.h"
#include "RunLogging.h"

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
    "  MetadataSidecars.Format (binary|json, default binary): original FLAC metadata archive written next to converted audio as .flac-metadata or readable .flac-metadata.json.\n"
    "  MetadataSidecars.ConvertToFlac / ConvertToMp3 (disabled|optional|required, default disabled): sidecar policy per conversion target; optional runs may turn sidecars off (menu prompt or --no-metadata-sidecar), required runs reject that request. Without a sidecar, metadata the output cannot carry stops the conversion.\n"
    "  LogLevel (integer 0-6, default 1): detailed session-log threshold (trace, debug, info, warn, err, critical, off); console output remains concise.\n"
    "  UseAsyncMetadataReads (bool, default true): parallel TagLib metadata reads while scanning; legacy UseAsyncFFmpegCalls is also accepted.\n"
    "  Incremental (bool, default true): reuse metadata records when path, size and modification time are unchanged.\n"
    "  ScanFlacMetadataArchives (bool, default false): write byte-exact native FLAC metadata archives during scans; scan flags can override it.\n"
    "  CatalogExtensions (array, default [.flac,.mp3]): accepted scan extensions; TagLib supports additional formats when explicitly enabled.\n"
    "  MetadataScanMaxThreads (integer >=0, default 0): per-album metadata worker cap; 0 uses hardware concurrency (fallback 4).\n"
    "  UseFFmpegLibraryAPI (deprecated): ignored; metadata re-probes always use FFmpeg libraries.\n"
    "  ScanTier (catalog|integrity|analysis, default catalog): default metadata scan depth; scan --tier overrides it.\n"
    "  DecodeWorkers (integer >=0, default 0): separate full-decode worker cap; 0 uses at most 4 workers.\n"
    "  ExtraAudioQualityMetrics (bool, default false): deprecated compatibility setting; use ScanTier analysis.\n"
    "  MinMatchingTracksForDuplicate (integer, default 1): minimum matched tracks for a duplicate pair.\n"
    "  SizeMatchPercentageThreshold (integer, default 1): duplicate track-duration tolerance, in percent of the longer track.\n"
    "  DuplicateDurationToleranceSeconds (number, default 2): minimum duplicate duration tolerance; the larger tolerance applies.\n"
    "  DuplicateFuzzyNames (bool, default false): also match similar album names when the artists match.\n"
    "  DuplicateStripEditionText (bool, default false): ignore edition text such as (Remastered) or [Deluxe Edition] in names.\n"
    "  DuplicatePartialMatches (bool, default false): report albums whose track lists only partly match.\n"
    "  DuplicatePartialMinCoverage (integer 1-100, default 80): minimum matched percentage of the larger album for partial matches.\n"
    "  RecursionDirectorySearchDepth (integer, default 10): maximum directory depth for scans.\n"
    "  AlbumsSplitThreshold (integer, default 2000): maximum albums per metadata batch.\n"
    "  JsonBatchTargetBytes (integer, default 0): optional hard JSON byte limit; large albums split.";

void add_config_option(CLI::App* command, CommandLineOptions& options)
{
    command->add_option("--log-level", options.logLevel,
        "Detailed session-log level: info, debug, or trace. Console output stays concise.")
        ->check(CLI::IsMember({ "info", "debug", "trace" }));
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
        "  repair <input-dir>    Validate FLAC/MP3 metadata and repair safe problems without touching audio.\n"
        "  scan <library-dir>    Create split JSON metadata files for a library.\n"
        "  duplicates <json-dir> Find duplicate albums from JSON files.\n"
        "  db import <json-dir>  Import JSON metadata into SQLite.\n\n"
        "Run a command with --help for its inputs, outputs, applicable settings, and examples.\n\n"
        "Configuration keys:\n" + kConfigurationHelp);

    auto* convert = app.add_subcommand("convert", "Convert audio to FLAC or convert native FLAC to MP3 without changing sources.");
    std::string targetFormat{ "flac" };
    auto* target = convert->add_option("--to", targetFormat, "Target: flac (default) or mp3.")
        ->check(CLI::IsMember({ "flac", "mp3" }));
    auto* quality = convert->add_option("--mp3-quality", options.conversion.mp3Quality,
        "LAME VBR quality: 0 (highest, default) through 9 (lowest).")->check(CLI::Range(0, 9));
    auto* downmix = convert->add_flag("--downmix-to-stereo", options.conversion.downmixToStereo,
        "Allow multichannel FLAC to be downmixed to stereo for MP3; logs the change.");
    quality->needs(target);
    downmix->needs(target);
    convert->add_option("input-dir", options.inputPath,
        "Directory tree to convert (FLAC, APE, DSF, DFF, DSD, WV, WAV, M2TS, and M4A).")
        ->required()
        ->check([](const std::string& value) {
            return value.empty() ? "input directory must not be empty" : std::string{};
        });
    convert->add_option("--output-dir", options.outputDirectory,
        "Base output root: <root>/<FLAC|MP3>/<input-folder-name>, preserving relative paths. Defaults to config OutDirectory or the platform default (C:\\tmp on Windows).")
        ->check([](const std::string& value) {
            return value.empty() ? "output directory must not be empty" : std::string{};
        })
        ->each([&options](const std::string&) { options.outputDirectorySpecified = true; });
    convert->add_flag("--overwrite", options.overwrite,
        "Replace existing destination files only after each conversion succeeds.");
    convert->add_flag("--no-metadata-sidecar", options.noMetadataSidecar,
        "Do not write original-metadata sidecars; allowed only when MetadataSidecars policy for the target is optional.");
    add_config_option(convert, options);
    convert->footer(command_footer(
        "FLAC output: supported inputs become 16-bit/44.1 kHz FLAC with triangular dithering and mapped tags/pictures. Every other regular file in a written folder, including unsupported audio, is copied unchanged, as are folders without audio. A folder subtree containing audio but nothing to convert is skipped and listed in conversion-audit.json; a library without conversions writes only that report. Directory links and unsupported filesystem entries fail instead of being silently omitted. Native FLAC inputs also retain original metadata in .flac.flac-metadata (binary) or .flac.flac-metadata.json sidecars, per MetadataSidecars; source technical fields do not describe the converted audio. MP3 output: only native FLAC inputs; libmp3lame VBR quality 0 by default, ID3v2.4 tags/artwork plus an original FLAC metadata sidecar (per MetadataSidecars) and unchanged non-audio companion files. MP3 retains 44.1/48 kHz; other accepted rates become 48 kHz. Multichannel MP3 requires --downmix-to-stereo. Converted audio requires verified source depth >=16 and rate >=44100 Hz. Paths are preserved under a separate output directory; sources are never replaced. Existing destinations fail unless --overwrite is supplied. Both targets stop at the first error and keep completed outputs.",
        "  FlacConvert convert \"D:\\Music\" --output-dir \"E:\\Converted\"\n"
        "  FlacConvert convert \"D:\\Music\" --config \"D:\\settings\\config.json\""));
    convert->callback([&options, &targetFormat, quality, downmix] {
        options.conversion.target = targetFormat == "mp3" ? AudioTarget::Mp3 : AudioTarget::Flac;
        if (options.conversion.target != AudioTarget::Mp3 && (quality->count() || downmix->count())) {
            throw CLI::ValidationError("MP3 options require --to mp3");
        }
        options.action = CommandAction::Convert;
    });

    auto* repair = app.add_subcommand("repair",
        "Validate FLAC and MP3 metadata and apply safe metadata-only repairs; audio bytes are never changed.");
    repair->add_option("input-dir", options.inputPath, "Audio library directory to inspect.")
        ->required()
        ->check([](const std::string& value) {
            return value.empty() ? "input directory must not be empty" : std::string{};
        });
    repair->add_option("--output-dir", options.outputDirectory,
        "Base output root. Repaired folders are written under <root>/FIXED/<library-name>; defaults to OutDirectory.")
        ->check([](const std::string& value) {
            return value.empty() ? "output directory must not be empty" : std::string{};
        })
        ->each([&options](const std::string&) { options.outputDirectorySpecified = true; });
    repair->add_flag("--overwrite", options.overwrite,
        "Replace existing repair destinations only after each output is verified.");
    std::string ruleHelp = "Comma-separated rule ids to run (default: all). Rules:";
    for (const auto& rule : RepairRules::Catalog()) {
        ruleHelp += "\n    " + rule.id + (rule.canFix ? " [fix]: " : " [report]: ") + rule.description;
    }
    repair->add_option("--rules", options.repairRules, ruleHelp)
        ->delimiter(',')
        ->check([](const std::string& value) {
            return RepairRules::IsKnownRule(value) ? std::string{} : "unknown repair rule: " + value;
        });
    add_config_option(repair, options);
    repair->footer(command_footer(
        "Inspects native FLAC and MP3 files with metadata-only rules (see --rules). Fix rules rebuild only metadata: tags, comments, picture descriptors or padding; the audio data range is copied byte-for-byte and verified, never decoded or re-encoded. Tags are removed only when every field is already present in the primary tag (Vorbis comments for FLAC, the first ID3v2 tag for MP3); anything else is reported for manual review and left unchanged. Report-only rules never change files. Only folders containing a repair are copied, with their other files unchanged. Sources are never modified. The output includes repair-report.json; findings needing review exit with code 1. Existing destinations fail unless --overwrite is supplied.",
        "  FlacConvert repair \"D:\\Music\"\n"
        "  FlacConvert repair \"D:\\Music\" --output-dir \"E:\\Repaired\"\n"
        "  FlacConvert repair \"D:\\Music\" --rules flac.duplicate-comments,mp3.redundant-id3v1"));
    repair->callback([&options] { options.action = CommandAction::Repair; });

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
    scan->add_flag("--full-rescan", options.fullRescan,
        "Ignore cached JSON metadata and reread every audio file.");
    auto* archiveMetadata = scan->add_flag("--archive-flac-metadata", options.archiveFlacMetadata,
        "Write verified byte-exact native FLAC metadata archives for this scan.");
    auto* noArchiveMetadata = scan->add_flag("--no-archive-flac-metadata", options.noArchiveFlacMetadata,
        "Do not write FLAC metadata archives for this scan.");
    archiveMetadata->excludes(noArchiveMetadata);
    scan->add_option("--tier", options.scanTier,
        "Override configured scan tier: catalog, integrity, or analysis.")
        ->check(CLI::IsMember({ "catalog", "integrity", "analysis" }))
        ->each([&options](const std::string&) { options.scanTierSpecified = true; });
    add_config_option(scan, options);
    scan->footer(command_footer(
        "Inputs: one audio library directory. Output: <output-dir>/<library-name>/media_out_<library-name>_<part>.json and scan-manifest.json. Unchanged files reuse prior metadata when Incremental is enabled; --full-rescan forces rereads. --tier selects catalog, integrity, or analysis processing. Native FLAC metadata archives are optional and disabled by default; the archive flags override ScanFlacMetadataArchives. A completed scan replaces generated JSON and retires previous manifest-owned archives only after the new set is ready.",
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
    duplicates->add_option("--report", options.reportPath,
        "Write a JSON report of duplicate groups to this new file (refuses to overwrite).");
    add_config_option(duplicates, options);
    duplicates->footer(command_footer(
        "Inputs: a directory containing generated album JSON files. Duplicate groups are printed without prompting. Use --open to launch matching folders in the operating system's file manager and --report to save a JSON report.",
        "  FlacConvert duplicates \"C:\\tmp\\Music\"\n"
        "  FlacConvert duplicates \"C:\\tmp\\Music\" --open\n"
        "  FlacConvert duplicates \"C:\\tmp\\Music\" --report \"C:\\tmp\\duplicates.json\""));
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
        "Inputs: schema-v2 catalog JSON files. Output: <output-dir>/<library-name>/catalog-v2.sqlite. Import is transactional; old-format databases are left untouched.",
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
        if (result != 0) RunLogging::RecordError(std::string("Command line: ") + error.what());
        return result == 0 ? 0 : 2;
    }

    if (options.action == CommandAction::None)
    {
        std::cout << app.help();
        return 0;
    }

    return 1;
}
