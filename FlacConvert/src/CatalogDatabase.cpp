#include "CatalogDatabase.h"

#include "CommonUtils.h"
#include "MediaRecordJson.h"
#include "MediaTrack.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <format>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>

#include <spdlog/spdlog.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#ifdef _WIN32
#include "PlatformUtils.h"
#endif

namespace {

struct Database
{
    sqlite3* handle{ nullptr };
    ~Database() { if (handle != nullptr) sqlite3_close(handle); }
};

struct Statement
{
    sqlite3_stmt* handle{ nullptr };
    Statement() = default;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
    Statement& operator=(Statement&& other) noexcept
    {
        if (this != &other) {
            if (handle != nullptr) sqlite3_finalize(handle);
            handle = std::exchange(other.handle, nullptr);
        }
        return *this;
    }
    ~Statement() { if (handle != nullptr) sqlite3_finalize(handle); }
};

void checkSqlite(int result, sqlite3* db, std::string_view operation)
{
    if (result != SQLITE_OK) {
        throw std::runtime_error(std::string(operation) + ": " +
            (db == nullptr ? sqlite3_errstr(result) : sqlite3_errmsg(db)));
    }
}

void execute(sqlite3* db, const char* sql)
{
    char* error = nullptr;
    const int result = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    if (result != SQLITE_OK) {
        const std::string message = error == nullptr ? sqlite3_errmsg(db) : error;
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

Statement prepare(sqlite3* db, const char* sql)
{
    Statement statement;
    checkSqlite(sqlite3_prepare_v2(db, sql, -1, &statement.handle, nullptr), db,
        "Preparing catalog database statement");
    return statement;
}

std::string objectJson(const std::string& recordJson, const char* key)
{
    rapidjson::Document document;
    document.Parse(recordJson.c_str());
    if (document.HasParseError() || !document.IsObject() ||
        !document.HasMember(key) || !document[key].IsObject()) {
        throw std::runtime_error(std::string("Catalog record is missing JSON object '") + key + "'.");
    }
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    document[key].Accept(writer);
    return std::string(buffer.GetString(), buffer.GetSize());
}

void bindText(sqlite3* db, sqlite3_stmt* statement, int index, std::string_view value)
{
    if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        throw std::runtime_error("Catalog text value exceeds SQLite's supported length.");
    }
    checkSqlite(sqlite3_bind_text(statement, index, value.empty() ? "" : value.data(),
        static_cast<int>(value.size()), SQLITE_TRANSIENT), db, "Binding catalog text");
}

void bindOptionalText(sqlite3* db, sqlite3_stmt* statement, int index,
    const std::optional<std::string>& value)
{
    if (value) bindText(db, statement, index, *value);
    else checkSqlite(sqlite3_bind_null(statement, index), db, "Binding NULL text");
}

template<typename T>
void bindOptionalInteger(sqlite3* db, sqlite3_stmt* statement, int index,
    const std::optional<T>& value)
{
    if (value) checkSqlite(sqlite3_bind_int64(statement, index,
        static_cast<sqlite3_int64>(*value)), db, "Binding catalog integer");
    else checkSqlite(sqlite3_bind_null(statement, index), db, "Binding NULL integer");
}

void bindOptionalDouble(sqlite3* db, sqlite3_stmt* statement, int index,
    const std::optional<double>& value)
{
    if (value) checkSqlite(sqlite3_bind_double(statement, index, *value), db,
        "Binding catalog number");
    else checkSqlite(sqlite3_bind_null(statement, index), db, "Binding NULL number");
}

void stepDone(sqlite3* db, sqlite3_stmt* statement, std::string_view operation)
{
    const int result = sqlite3_step(statement);
    if (result != SQLITE_DONE) checkSqlite(result, db, operation);
    checkSqlite(sqlite3_reset(statement), db, "Resetting catalog statement");
    checkSqlite(sqlite3_clear_bindings(statement), db, "Clearing catalog statement");
}

std::string pathUtf8(const std::filesystem::path& path)
{
    return CommonUtils::path_to_utf8(path);
}

bool isSameOrChildPath(const std::filesystem::path& candidate, const std::filesystem::path& base)
{
    auto candidatePart = candidate.begin();
    for (auto basePart = base.begin(); basePart != base.end(); ++basePart, ++candidatePart) {
        if (candidatePart == candidate.end()) return false;
#ifdef _WIN32
        const auto& left = candidatePart->native();
        const auto& right = basePart->native();
        if (CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
                right.c_str(), static_cast<int>(right.size()), TRUE) != CSTR_EQUAL) return false;
#else
        if (*candidatePart != *basePart) return false;
#endif
    }
    return true;
}

std::string pathString(const std::filesystem::path& path)
{
    auto value = pathUtf8(path);
    std::ranges::replace(value, '\\', '/');
    return value;
}

std::string utcNow()
{
    const auto now = std::chrono::system_clock::now();
    return std::format("{:%FT%TZ}", now);
}

std::string newScanId()
{
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::string unitFor(std::string_view column)
{
    if (column.ends_with("_bytes") || column == "size_bytes") return "bytes";
    if (column.ends_with("_seconds")) return "seconds";
    if (column == "sample_rate_hz") return "Hz";
    if (column == "bit_rate_kbps") return "kbps";
    if (column == "declared_bits_per_sample") return "bits";
    return {};
}

std::string humanize(std::string_view value)
{
    std::string result(value);
    std::ranges::replace(result, '_', ' ');
    if (!result.empty()) {
        result.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(result.front())));
    }
    return result;
}

std::string columnHint(std::string_view table, std::string_view column)
{
    if (column == "id" || column == "file_id" || column == "album_id" ||
        column == "library_id" || column == "finding_id") {
        return "Internal relational key only; do not treat this value as a stable external file identity.";
    }
    if (column == "relative_path") {
        return "Stable file identity is the pair (library_id, relative_path); paths use '/' separators.";
    }
    if (column == "record_json") {
        return "Canonical schema-v2 record for lossless round-trip; prefer normalized indexed columns for filtering.";
    }
    if (column == "sha256" || column == "pcm_hash" || column == "fingerprint") {
        return "NULL means this measurement was not computed; it does not mean the file has no value.";
    }
    if (column == "value" && table == "tags") {
        return "A value read from embedded metadata; it is a claimed tag, not a measured audio property.";
    }
    if (column == "status" || column == "analysis_status" || column == "decode_support") {
        return "Interpret the status literally; not_run and unsupported are not successful validation.";
    }
    return "Stored catalog value; NULL for optional properties means unknown or not supplied by the reader.";
}

std::string objectDescription(std::string_view object)
{
    static const std::map<std::string_view, std::string_view> descriptions{
        { "libraries", "One source library represented by this catalog database." },
        { "albums", "A discovered album directory within a library." },
        { "files", "One audio file and its current catalog record." },
        { "tags", "Normalized, searchable tag values; repeated values have separate ordinals." },
        { "raw_tags", "Source-scoped native tag entries retained independently of normalization." },
        { "pictures", "Embedded picture descriptors; picture bytes are not stored in the database." },
        { "chapters", "Chapter boundaries and timing for a media file." },
        { "chapter_tags", "Repeated tag values associated with a chapter." },
        { "measurements", "Measured integrity or signal results; tag claims are not measurements." },
        { "findings", "Validation or heuristic findings, with evidence and confidence." },
        { "v_tracks", "One-row-per-file convenience view with common searchable tags." },
        { "v_findings", "Findings joined to their file or album path." },
        { "v_albums", "Album counts and technical summary for library browsing." }
    };
    const auto found = descriptions.find(object);
    return found == descriptions.end() ? humanize(object) : std::string(found->second);
}

void addSchemaDocs(sqlite3* db)
{
    auto insert = prepare(db,
        "INSERT OR REPLACE INTO schema_docs "
        "(object_name, column_name, description, unit, value_kind, example, ai_hint) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);");

    static constexpr std::array objects{
        "libraries", "albums", "files", "tags", "raw_tags", "pictures", "chapters",
        "chapter_tags", "measurements", "findings", "schema_docs", "enum_docs",
        "catalog_meta", "v_tracks", "v_findings", "v_albums"
    };
    for (const char* object : objects) {
        bindText(db, insert.handle, 1, object);
        bindText(db, insert.handle, 2, "");
        bindText(db, insert.handle, 3, objectDescription(object));
        bindText(db, insert.handle, 4, "");
        bindText(db, insert.handle, 5, "object");
        bindText(db, insert.handle, 6, "");
        bindText(db, insert.handle, 7, "See catalog_meta.ai_guide before interpreting this object.");
        stepDone(db, insert.handle, "Writing schema documentation");

        const std::string sql = std::string("PRAGMA table_info(\"") + object + "\");";
        auto columns = prepare(db, sql.c_str());
        while (true) {
            const int result = sqlite3_step(columns.handle);
            if (result == SQLITE_DONE) break;
            if (result != SQLITE_ROW) {
                checkSqlite(result, db, "Reading catalog schema columns");
            }
            const auto* nameValue = sqlite3_column_text(columns.handle, 1);
            const auto* typeValue = sqlite3_column_text(columns.handle, 2);
            const std::string name = nameValue == nullptr ? "" :
                reinterpret_cast<const char*>(nameValue);
            const std::string type = typeValue == nullptr ? "" :
                reinterpret_cast<const char*>(typeValue);
            if (name.empty()) continue;
            bindText(db, insert.handle, 1, object);
            bindText(db, insert.handle, 2, name);
            bindText(db, insert.handle, 3, humanize(name) + " stored for " + object + ".");
            bindText(db, insert.handle, 4, unitFor(name));
            bindText(db, insert.handle, 5, type);
            bindText(db, insert.handle, 6, type.find("INT") != std::string::npos ? "0" :
                type.find("REAL") != std::string::npos ? "0.0" : "text");
            bindText(db, insert.handle, 7, columnHint(object, name));
            stepDone(db, insert.handle, "Writing column documentation");
        }
    }
}

void addEnumDocs(sqlite3* db)
{
    auto insert = prepare(db,
        "INSERT OR REPLACE INTO enum_docs (domain, value, meaning, ai_hint) VALUES (?, ?, ?, ?);");
    const std::array values{
        std::tuple{"schema_version", "2", "Current catalog database schema.", "Refuse other versions; never auto-delete a database."},
        std::tuple{"scan_tier", "catalog", "Tags and container properties were collected.", "Decode and signal measurements may remain not_run."},
        std::tuple{"scan_tier", "integrity", "Full decode and deterministic integrity checks were requested.", "Only fields with status ok are verified."},
        std::tuple{"scan_tier", "analysis", "Integrity and signal analysis were requested.", "Heuristic findings include confidence and evidence."},
        std::tuple{"decode_status", "not_run", "No full decode was performed.", "This is not a pass."},
        std::tuple{"decode_status", "verified", "Full decode completed successfully.", "See measurement rows for checks and versions."},
        std::tuple{"decode_status", "failed", "Decode or integrity verification failed.", "Inspect findings and error evidence."},
        std::tuple{"support", "supported", "The requested metadata interface is available.", "Does not imply byte-complete native inventory."},
        std::tuple{"support", "partial", "Only part of the native inventory is exposed.", "Do not interpret as lossless preservation."},
        std::tuple{"support", "unsupported", "The reader does not inspect this data.", "Unknown is not equivalent to absent."},
        std::tuple{"analysis_status", "complete", "Requested analysis completed.", "Read measurement methods and units."},
        std::tuple{"analysis_status", "partial", "Some requested measurements are unavailable.", "Consult per-measurement status."},
        std::tuple{"analysis_status", "failed", "Requested analysis encountered an error.", "Consult findings and diagnostics."},
        std::tuple{"analysis_status", "not_run", "Analysis was not requested or performed.", "Never treat this as a clean result."},
        std::tuple{"measurement_status", "ok", "A measurement was obtained.", "Only this status has a trustworthy value."},
        std::tuple{"measurement_status", "absent", "The source contains no such value.", "This differs from not_run."},
        std::tuple{"measurement_status", "unavailable", "The value could not be determined.", "Do not substitute zero."},
        std::tuple{"measurement_status", "failed", "The measurement attempt failed.", "Use the diagnostic evidence."},
        std::tuple{"measurement_status", "unsupported", "The reader or decoder cannot measure this value.", "Unknown is not equivalent to absent."},
        std::tuple{"measurement_status", "not_run", "The measurement was not requested or performed.", "Never treat this as a pass."},
        std::tuple{"finding_severity", "error", "A verified problem affecting interpretation or use.", "Read evidence before acting."},
        std::tuple{"finding_severity", "warning", "A concern or heuristic, not necessarily a defect.", "Respect confidence."},
        std::tuple{"finding_severity", "info", "Contextual information without a failure claim.", "Do not treat as an error."},
        std::tuple{"finding_confidence", "low", "Weak evidence for a finding.", "Treat as a hypothesis."},
        std::tuple{"finding_confidence", "medium", "Moderate evidence for a finding.", "Review supporting evidence."},
        std::tuple{"finding_confidence", "high", "Strong evidence for a finding.", "Still retain the stated evidence and method."},
        std::tuple{"finding_code", "MISSING_TITLE", "No non-empty title tag was found.", "A missing tag is not proof the audio has no title."},
        std::tuple{"finding_code", "MISSING_ARTIST", "No non-empty artist tag was found.", "Review raw_tags for alternate source-specific fields."},
        std::tuple{"finding_code", "MISSING_ALBUM", "No non-empty album tag was found.", "A folder name is not an embedded album claim."},
        std::tuple{"finding_code", "INVALID_TRACK_NUMBER", "Track numbering is not a positive n[/total] value.", "Check the raw tag and source format."},
        std::tuple{"finding_code", "INVALID_DISC_NUMBER", "Disc numbering is not a positive n[/total] value.", "Check the raw tag and source format."},
        std::tuple{"finding_code", "NON_ISO_DATE", "A normalized date is not a valid partial or full ISO date.", "Accepted forms are YYYY, YYYY-MM, or YYYY-MM-DD."},
        std::tuple{"finding_code", "INVALID_MUSICBRAINZ_ID", "A MusicBrainz identifier is not a canonical UUID.", "Treat the embedded ID as an unverified claim."},
        std::tuple{"finding_code", "INVALID_ISRC", "An ISRC does not match the 12-character ISO 3901 structure.", "Treat the embedded ID as an unverified claim."},
        std::tuple{"finding_code", "INVALID_BARCODE", "A barcode does not have a supported EAN/UPC/GTIN length or valid check digit.", "Treat the embedded code as an unverified claim."},
        std::tuple{"finding_code", "INCONSISTENT_ALBUM_TAG_album", "Album tag values differ within one album folder.", "Review per-track raw_tags; no value is chosen as authoritative."},
        std::tuple{"finding_code", "INCONSISTENT_ALBUM_TAG_albumArtists", "Album-artist tag values differ within one album folder.", "Review per-track raw_tags; no value is chosen as authoritative."},
        std::tuple{"finding_code", "INCONSISTENT_ALBUM_TAG_releaseDate", "Release-date tag values differ within one album folder.", "Review per-track raw_tags; no value is chosen as authoritative."},
        std::tuple{"finding_code", "INCONSISTENT_ALBUM_TAG_label", "Label tag values differ within one album folder.", "Review per-track raw_tags; no value is chosen as authoritative."},
        std::tuple{"finding_code", "DUPLICATE_TRACK_NUMBER", "More than one track on a disc has the same normalized track number.", "Inspect the track tags; no file is automatically renamed or discarded."},
        std::tuple{"finding_code", "TRACK_NUMBER_GAP", "Track numbers are missing between the lowest and highest number on a disc.", "A gap may indicate an incomplete album rip; it is not proof of missing audio."},
        std::tuple{"finding_code", "TRACK_TOTAL_MISMATCH", "Unique track numbers on a disc do not match its declared total.", "Compare the folder contents and declared tags; partial albums can be intentional."},
        std::tuple{"finding_code", "INCONSISTENT_TRACK_TOTAL", "Tracks on a disc declare different track totals.", "Review each raw tag; the importer does not choose an authoritative total."},
        std::tuple{"finding_code", "DISC_NUMBER_GAP", "Disc numbers are missing between the lowest and highest number.", "A gap may indicate an incomplete multi-disc set."},
        std::tuple{"finding_code", "DISC_TOTAL_MISMATCH", "Distinct disc numbers do not match the declared disc total.", "Compare the folder contents and disc tags; partial sets can be intentional."},
        std::tuple{"finding_code", "INCONSISTENT_DISC_TOTAL", "Tracks declare different disc totals.", "Review each raw tag; no authoritative total is inferred."},
        std::tuple{"finding_code", "SAMPLE_COUNT_MISMATCH", "Decoded sample count differs from the declared sample count.", "This is deterministic integrity evidence, not a duration estimate."},
        std::tuple{"finding_code", "DUAL_MONO", "The decoded stereo channels are strongly correlated.", "This is a signal heuristic, not proof of duplicate channels."},
        std::tuple{"finding_code", "PHASE_INVERTED_CHANNEL", "The decoded stereo channels are strongly anti-correlated.", "Inspect audio before applying any correction."},
        std::tuple{"finding_code", "ZERO_PADDED_HIRES", "The integer samples have zero-padded low bits relative to the declared depth.", "This does not estimate effective resolution or prove mastering quality."},
        std::tuple{"finding_code", "CLIPPED_SAMPLES", "Decoded samples reached or exceeded full scale.", "Respect codec and decoder behavior; this is a sample-peak check."},
        std::tuple{"finding_code", "LARGE_DIGITAL_SILENCE", "Leading or trailing audio remained below the analyzer threshold.", "The threshold and measured interval are recorded in evidence."}
    };
    for (const auto& [domain, value, meaning, hint] : values) {
        bindText(db, insert.handle, 1, domain);
        bindText(db, insert.handle, 2, value);
        bindText(db, insert.handle, 3, meaning);
        bindText(db, insert.handle, 4, hint);
        stepDone(db, insert.handle, "Writing enum documentation");
    }
}

std::int64_t pragmaInteger(sqlite3* db, const char* sql)
{
    auto statement = prepare(db, sql);
    const int result = sqlite3_step(statement.handle);
    if (result != SQLITE_ROW) checkSqlite(result, db, "Reading catalog database state");
    return sqlite3_column_int64(statement.handle, 0);
}

bool hasObject(sqlite3* db, const char* type, const char* name)
{
    auto statement = prepare(db,
        "SELECT 1 FROM sqlite_schema WHERE type=? AND name=? LIMIT 1;");
    bindText(db, statement.handle, 1, type);
    bindText(db, statement.handle, 2, name);
    const int result = sqlite3_step(statement.handle);
    if (result != SQLITE_ROW && result != SQLITE_DONE) {
        checkSqlite(result, db, "Validating catalog schema objects");
    }
    return result == SQLITE_ROW;
}

void createSchema(sqlite3* db)
{
    execute(db, R"sql(
CREATE TABLE libraries (
 id INTEGER PRIMARY KEY, -- Internal key; not stable outside this database.
 root TEXT NOT NULL UNIQUE -- Absolute source-library root path in UTF-8.
);
CREATE TABLE albums (
 id INTEGER PRIMARY KEY, -- Internal key; use library_id and relative_path externally.
 library_id INTEGER NOT NULL REFERENCES libraries(id) ON DELETE CASCADE, -- Owning library.
 relative_path TEXT NOT NULL, -- Album path relative to the library root; '/' separated.
 album_path TEXT NOT NULL, -- Source directory path retained for navigation.
 folder TEXT NOT NULL, -- Album directory leaf name; descriptive only.
 last_seen_scan_id TEXT NOT NULL, -- Import generation used to remove missing files within this album.
 UNIQUE(library_id, relative_path)
);
CREATE TABLE files (
 id INTEGER PRIMARY KEY, -- Internal key; use (library_id, relative_path) as file identity.
 library_id INTEGER NOT NULL REFERENCES libraries(id) ON DELETE CASCADE, -- Owning library.
 album_id INTEGER NOT NULL REFERENCES albums(id) ON DELETE CASCADE, -- Current album grouping.
 relative_path TEXT NOT NULL, -- Stable library-relative file path, '/' separated.
 format TEXT NOT NULL, -- Detected container format; not inferred solely from extension.
 size_bytes INTEGER NOT NULL, -- Filesystem size in bytes at scan time.
 modified_utc TEXT, -- Filesystem modification time in UTC; not a content hash.
 duration_seconds REAL, -- Duration in seconds; absent means unknown.
 bit_rate_kbps INTEGER, -- Bitrate in kilobits per second when available.
 sample_rate_hz INTEGER, -- Sample rate in hertz when available.
 channels INTEGER, -- Audio channel count when available.
 declared_bits_per_sample INTEGER, -- Declared source bit depth; not effective resolution.
 total_samples INTEGER, -- Sample count if exposed by the format.
 flac_md5 TEXT, -- FLAC-declared PCM MD5; not independently verified here.
 tag_vendor TEXT, -- Vorbis-comment vendor string when present.
 support_tags TEXT NOT NULL, -- Tag-reader support state.
 native_inventory_support TEXT NOT NULL, -- Native raw inventory completeness state.
 decode_support TEXT NOT NULL, -- Full decode state; not_run is not a pass.
 chapter_support TEXT NOT NULL, -- Chapter metadata reader support state.
 analysis_status TEXT NOT NULL, -- Signal-analysis state; not_run is not a clean result.
 scan_tier TEXT NOT NULL CHECK(scan_tier IN ('catalog','integrity','analysis')), -- Requested scan tier.
 sha256 TEXT, -- Whole-file SHA-256; NULL means not computed.
 pcm_hash TEXT, -- Decoded-PCM hash; NULL means not computed.
 fingerprint TEXT, -- Optional acoustic fingerprint; NULL means not computed.
 scanned_utc TEXT NOT NULL, -- Time this record was imported, in UTC.
 last_seen_scan_id TEXT NOT NULL, -- Import generation used for per-album stale-file cleanup.
 record_json TEXT NOT NULL, -- Canonical schema-v2 JSON record for lossless round-trip.
 UNIQUE(library_id, relative_path)
);
CREATE TABLE tags (
 file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE, -- Owning audio file.
 field TEXT NOT NULL, -- Normalized searchable tag name.
 value TEXT NOT NULL, -- Embedded tag claim; not a measured property.
 ordinal INTEGER NOT NULL, -- Preserves repeated values within the normalized field.
 PRIMARY KEY(file_id, field, ordinal)
);
CREATE TABLE raw_tags (
 file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE, -- Owning audio file.
 ordinal INTEGER NOT NULL, -- Reader enumeration order, not always on-disk order.
 scope TEXT NOT NULL, -- Native metadata source such as vorbis or id3v2.4.
 key TEXT NOT NULL, -- Native key or frame identifier.
 value TEXT NOT NULL, -- Exact decoded value exposed by the reader.
 frame TEXT, -- ID3 frame identifier when applicable.
 description TEXT, -- User-text/comment description when available.
 language TEXT, -- Language qualifier when available.
 encoding INTEGER, -- Native text-encoding identifier when available.
 PRIMARY KEY(file_id, ordinal)
);
CREATE TABLE pictures (
 file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE, -- Owning audio file.
 ordinal INTEGER NOT NULL, -- Order enumerated by the reader.
 picture_type INTEGER, -- Native picture-type code.
 mime TEXT, -- Declared image MIME type.
 description TEXT, -- Embedded picture description.
 width INTEGER, -- Pixel width when exposed.
 height INTEGER, -- Pixel height when exposed.
 size_bytes INTEGER NOT NULL, -- Embedded image payload size; image bytes are not stored.
 PRIMARY KEY(file_id, ordinal)
);
CREATE TABLE chapters (
 file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE, -- Owning audio file.
 ordinal INTEGER NOT NULL, -- Chapter order in the container.
 chapter_id INTEGER, -- Container chapter id.
 time_base TEXT, -- Rational time base for integer timestamps.
 start_tick INTEGER, -- Start timestamp in time-base units.
 start_seconds REAL, -- Start time in seconds.
 end_tick INTEGER, -- End timestamp in time-base units.
 end_seconds REAL, -- End time in seconds.
 PRIMARY KEY(file_id, ordinal)
);
CREATE TABLE chapter_tags (
 file_id INTEGER NOT NULL, -- Owning audio file.
 chapter_ordinal INTEGER NOT NULL, -- Ordinal of the parent chapter.
 field TEXT NOT NULL, -- Chapter tag name.
 value TEXT NOT NULL, -- Chapter tag claim.
 ordinal INTEGER NOT NULL, -- Repeated-value order.
 PRIMARY KEY(file_id, chapter_ordinal, field, ordinal),
 FOREIGN KEY(file_id, chapter_ordinal) REFERENCES chapters(file_id, ordinal) ON DELETE CASCADE
);
CREATE TABLE measurements (
 file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE, -- Owning audio file.
 kind TEXT NOT NULL, -- Measurement name and method family.
 status TEXT NOT NULL, -- ok, absent, unsupported, not_run, failed, or unavailable.
 json TEXT NOT NULL, -- Measurement payload with units and provenance.
 PRIMARY KEY(file_id, kind)
);
CREATE TABLE findings (
 finding_id INTEGER PRIMARY KEY, -- Internal key; not a stable external identifier.
 file_id INTEGER REFERENCES files(id) ON DELETE CASCADE, -- File finding target, if applicable.
 album_id INTEGER REFERENCES albums(id) ON DELETE CASCADE, -- Album finding target, if applicable.
 code TEXT NOT NULL, -- Stable finding code.
 severity TEXT NOT NULL CHECK(severity IN ('error','warning','info')), -- Finding impact classification.
 confidence TEXT NOT NULL CHECK(confidence IN ('low','medium','high')), -- Evidence confidence, not certainty.
 evidence TEXT NOT NULL, -- Human-readable evidence and measurement context.
 CHECK((file_id IS NOT NULL) != (album_id IS NOT NULL))
);
CREATE TABLE schema_docs (
 object_name TEXT NOT NULL, -- Table/view being documented.
 column_name TEXT NOT NULL, -- Empty string documents the object itself.
 description TEXT NOT NULL, -- Human-readable meaning and units.
 unit TEXT NOT NULL, -- Unit when numeric; empty when not applicable.
 value_kind TEXT NOT NULL, -- SQLite type or object marker.
 example TEXT NOT NULL, -- Representative value or an explanatory placeholder.
 ai_hint TEXT NOT NULL, -- Interpretation guidance for AI and query authors.
 PRIMARY KEY(object_name, column_name)
);
CREATE TABLE enum_docs (
 domain TEXT NOT NULL, -- Enum/status/finding-code domain.
 value TEXT NOT NULL, -- Stable allowed value.
 meaning TEXT NOT NULL, -- Human-readable meaning.
 ai_hint TEXT NOT NULL, -- Safe interpretation guidance.
 PRIMARY KEY(domain, value)
);
CREATE TABLE catalog_meta (
 key TEXT PRIMARY KEY, -- Metadata property name.
 value TEXT NOT NULL -- Schema version, tool versions, timestamps, or AI guide.
);
CREATE INDEX idx_tags_field_value ON tags(field, value); -- Search normalized tags by field/value.
CREATE INDEX idx_files_format ON files(format); -- Filter records by detected format.
CREATE INDEX idx_files_sample_rate ON files(sample_rate_hz); -- Find mixed or uncommon sample rates.
CREATE INDEX idx_findings_code ON findings(code, severity); -- Locate findings by code and impact.
CREATE VIEW v_tracks AS -- One row per file; tags remain fully available in tags.
 SELECT f.id, f.library_id, f.relative_path, f.format, f.size_bytes, f.duration_seconds,
        f.bit_rate_kbps, f.sample_rate_hz, f.channels, f.declared_bits_per_sample,
        (SELECT value FROM tags WHERE file_id=f.id AND field='title' ORDER BY ordinal LIMIT 1) AS title,
        (SELECT value FROM tags WHERE file_id=f.id AND field='album' ORDER BY ordinal LIMIT 1) AS album,
        (SELECT value FROM tags WHERE file_id=f.id AND field='artists' ORDER BY ordinal LIMIT 1) AS artist,
        (SELECT value FROM tags WHERE file_id=f.id AND field='albumArtists' ORDER BY ordinal LIMIT 1) AS album_artist,
        f.decode_support, f.analysis_status
 FROM files f;
CREATE VIEW v_findings AS -- Findings joined to their file or album path.
 SELECT x.finding_id, x.code, x.severity, x.confidence, x.evidence,
        f.relative_path, a.relative_path AS album_relative_path
 FROM findings x
 LEFT JOIN files f ON f.id=x.file_id
 LEFT JOIN albums a ON a.id=x.album_id;
CREATE VIEW v_albums AS -- Counts and technical ranges, not a quality score.
 SELECT a.id, a.library_id, a.relative_path, a.folder, COUNT(f.id) AS file_count,
        AVG(f.duration_seconds) AS average_duration_seconds,
        MIN(f.sample_rate_hz) AS minimum_sample_rate_hz,
        MAX(f.sample_rate_hz) AS maximum_sample_rate_hz
 FROM albums a LEFT JOIN files f ON f.album_id=a.id
 GROUP BY a.id, a.library_id, a.relative_path, a.folder;
)sql");

    addSchemaDocs(db);
    addEnumDocs(db);
    auto meta = prepare(db, "INSERT OR REPLACE INTO catalog_meta(key,value) VALUES (?,?);");
    const std::array<std::pair<std::string, std::string>, 4> metadata{{
        {"schema_version", "2"},
        {"created_utc", utcNow()},
        {"sqlite_version", sqlite3_libversion()},
        {"ai_guide",
            "Start with v_tracks for browsing and v_findings for issues. File identity is (library_id, relative_path). "
            "Tags are embedded claims; measurements are separately method-labelled. NULL means unknown or not computed, "
            "while decode_support/analysis_status='not_run' is not a clean result. Use raw_tags for source-scoped values "
            "and record_json for the complete schema-v2 record. Example: SELECT relative_path FROM v_tracks "
            "WHERE sample_rate_hz BETWEEN 88000 AND 96000; "
            "SELECT relative_path,code,evidence FROM v_findings WHERE severity='warning';"}
    }};
    for (const auto& [key, value] : metadata) {
        bindText(db, meta.handle, 1, key);
        bindText(db, meta.handle, 2, value);
        stepDone(db, meta.handle, "Writing catalog metadata");
    }
}

std::int64_t getLibraryId(sqlite3* db, const std::string& root)
{
    auto insert = prepare(db,
        "INSERT INTO libraries(root) VALUES (?) ON CONFLICT(root) DO NOTHING;");
    bindText(db, insert.handle, 1, root);
    stepDone(db, insert.handle, "Registering catalog library");
    auto query = prepare(db, "SELECT id FROM libraries WHERE root=?;");
    bindText(db, query.handle, 1, root);
    if (sqlite3_step(query.handle) != SQLITE_ROW) {
        throw std::runtime_error("Could not retrieve catalog library id.");
    }
    return sqlite3_column_int64(query.handle, 0);
}

std::int64_t getAlbumId(sqlite3* db, std::int64_t libraryId,
    const std::string& relativePath, const std::string& absolutePath,
    const std::string& folder, const std::string& scanId)
{
    auto insert = prepare(db, R"sql(
INSERT INTO albums(library_id,relative_path,album_path,folder,last_seen_scan_id)
VALUES(?,?,?,?,?)
ON CONFLICT(library_id,relative_path) DO UPDATE SET
 album_path=excluded.album_path, folder=excluded.folder, last_seen_scan_id=excluded.last_seen_scan_id;
)sql");
    checkSqlite(sqlite3_bind_int64(insert.handle, 1, libraryId), db, "Binding library id");
    bindText(db, insert.handle, 2, relativePath);
    bindText(db, insert.handle, 3, absolutePath);
    bindText(db, insert.handle, 4, folder);
    bindText(db, insert.handle, 5, scanId);
    stepDone(db, insert.handle, "Upserting catalog album");
    auto query = prepare(db, "SELECT id FROM albums WHERE library_id=? AND relative_path=?;");
    checkSqlite(sqlite3_bind_int64(query.handle, 1, libraryId), db, "Binding library id");
    bindText(db, query.handle, 2, relativePath);
    if (sqlite3_step(query.handle) != SQLITE_ROW) {
        throw std::runtime_error("Could not retrieve catalog album id.");
    }
    return sqlite3_column_int64(query.handle, 0);
}

std::int64_t findFileId(sqlite3* db, std::int64_t libraryId, const std::string& path)
{
    auto query = prepare(db,
        "SELECT id FROM files WHERE library_id=? AND relative_path=?;");
    checkSqlite(sqlite3_bind_int64(query.handle, 1, libraryId), db, "Binding library id");
    bindText(db, query.handle, 2, path);
    const int result = sqlite3_step(query.handle);
    if (result == SQLITE_DONE) return 0;
    if (result != SQLITE_ROW) checkSqlite(result, db, "Finding catalog file");
    return sqlite3_column_int64(query.handle, 0);
}

std::optional<std::string> existingRecordJson(sqlite3* db, std::int64_t libraryId,
    const std::string& path)
{
    auto query = prepare(db,
        "SELECT record_json FROM files WHERE library_id=? AND relative_path=?;");
    checkSqlite(sqlite3_bind_int64(query.handle, 1, libraryId), db, "Binding library id");
    bindText(db, query.handle, 2, path);
    const int result = sqlite3_step(query.handle);
    if (result == SQLITE_DONE) return std::nullopt;
    if (result != SQLITE_ROW) checkSqlite(result, db, "Reading existing catalog record");
    const auto* text = sqlite3_column_text(query.handle, 0);
    if (text == nullptr) throw std::runtime_error("Existing catalog record is NULL.");
    const int length = sqlite3_column_bytes(query.handle, 0);
    return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(length));
}

void insertRecord(sqlite3* db, std::int64_t libraryId, std::int64_t albumId,
    const std::string& relativePath, const MediaRecord& record,
    const std::string& recordJson, const std::string& scanId, const std::string& scannedUtc)
{
    auto upsert = prepare(db, R"sql(
INSERT INTO files(
 library_id,album_id,relative_path,format,size_bytes,modified_utc,duration_seconds,bit_rate_kbps,
 sample_rate_hz,channels,declared_bits_per_sample,total_samples,flac_md5,tag_vendor,support_tags,
 native_inventory_support,decode_support,chapter_support,analysis_status,scan_tier,sha256,pcm_hash,scanned_utc,
 last_seen_scan_id,record_json)
VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
ON CONFLICT(library_id,relative_path) DO UPDATE SET
 album_id=excluded.album_id,format=excluded.format,size_bytes=excluded.size_bytes,
 modified_utc=excluded.modified_utc,duration_seconds=excluded.duration_seconds,
 bit_rate_kbps=excluded.bit_rate_kbps,sample_rate_hz=excluded.sample_rate_hz,
 channels=excluded.channels,declared_bits_per_sample=excluded.declared_bits_per_sample,
 total_samples=excluded.total_samples,flac_md5=excluded.flac_md5,tag_vendor=excluded.tag_vendor,
 support_tags=excluded.support_tags,native_inventory_support=excluded.native_inventory_support,
 decode_support=excluded.decode_support,chapter_support=excluded.chapter_support,
 analysis_status=excluded.analysis_status,scan_tier=excluded.scan_tier,
 sha256=excluded.sha256,pcm_hash=excluded.pcm_hash,scanned_utc=excluded.scanned_utc,
 last_seen_scan_id=excluded.last_seen_scan_id,record_json=excluded.record_json;
)sql");
    checkSqlite(sqlite3_bind_int64(upsert.handle, 1, libraryId), db, "Binding library id");
    checkSqlite(sqlite3_bind_int64(upsert.handle, 2, albumId), db, "Binding album id");
    bindText(db, upsert.handle, 3, relativePath);
    bindText(db, upsert.handle, 4, record.format);
    checkSqlite(sqlite3_bind_int64(upsert.handle, 5,
        static_cast<sqlite3_int64>(record.sizeBytes)), db, "Binding file size");
    bindOptionalText(db, upsert.handle, 6, record.modifiedUtc);
    bindOptionalDouble(db, upsert.handle, 7, record.durationSeconds);
    bindOptionalInteger(db, upsert.handle, 8, record.bitRateKbps);
    bindOptionalInteger(db, upsert.handle, 9, record.sampleRateHz);
    bindOptionalInteger(db, upsert.handle, 10, record.channels);
    bindOptionalInteger(db, upsert.handle, 11, record.declaredBitsPerSample);
    bindOptionalInteger(db, upsert.handle, 12, record.totalSamples);
    bindOptionalText(db, upsert.handle, 13, record.flacMd5);
    bindOptionalText(db, upsert.handle, 14, record.tagVendor);
    bindText(db, upsert.handle, 15, record.tagSupport);
    bindText(db, upsert.handle, 16, record.nativeInventorySupport);
    bindText(db, upsert.handle, 17, record.decodeSupport);
    bindText(db, upsert.handle, 18, record.chapterSupport);
    bindText(db, upsert.handle, 19, record.analysisStatus);
    bindText(db, upsert.handle, 20, record.scanTier);
    bindOptionalText(db, upsert.handle, 21, record.integrity.fileSha256);
    bindOptionalText(db, upsert.handle, 22, record.integrity.pcmSha256);
    bindText(db, upsert.handle, 23, scannedUtc);
    bindText(db, upsert.handle, 24, scanId);
    bindText(db, upsert.handle, 25, recordJson);
    stepDone(db, upsert.handle, "Upserting catalog file");

    const std::int64_t fileId = findFileId(db, libraryId, relativePath);
    if (fileId == 0) throw std::runtime_error("Could not retrieve catalog file id.");
    const std::array childTables{
        "tags", "raw_tags", "pictures", "chapters", "measurements", "findings"
    };
    for (const char* table : childTables) {
        const std::string sql = std::string("DELETE FROM ") + table + " WHERE file_id=?;";
        auto remove = prepare(db, sql.c_str());
        checkSqlite(sqlite3_bind_int64(remove.handle, 1, fileId), db, "Binding file id");
        stepDone(db, remove.handle, "Replacing catalog child rows");
    }

    auto tag = prepare(db,
        "INSERT INTO tags(file_id,field,value,ordinal) VALUES(?,?,?,?);");
    for (const auto& [field, values] : record.tags) {
        for (std::size_t ordinal = 0; ordinal < values.size(); ++ordinal) {
            checkSqlite(sqlite3_bind_int64(tag.handle, 1, fileId), db, "Binding file id");
            bindText(db, tag.handle, 2, field);
            bindText(db, tag.handle, 3, values[ordinal]);
            checkSqlite(sqlite3_bind_int64(tag.handle, 4, static_cast<sqlite3_int64>(ordinal)),
                db, "Binding tag ordinal");
            stepDone(db, tag.handle, "Inserting normalized tag");
        }
    }

    auto raw = prepare(db, R"sql(
INSERT INTO raw_tags(file_id,ordinal,scope,key,value,frame,description,language,encoding)
VALUES(?,?,?,?,?,?,?,?,?);
)sql");
    for (const auto& item : record.rawTags) {
        checkSqlite(sqlite3_bind_int64(raw.handle, 1, fileId), db, "Binding file id");
        checkSqlite(sqlite3_bind_int64(raw.handle, 2, item.ordinal), db, "Binding raw tag ordinal");
        bindText(db, raw.handle, 3, item.scope);
        bindText(db, raw.handle, 4, item.key);
        bindText(db, raw.handle, 5, item.value);
        bindOptionalText(db, raw.handle, 6, item.frame);
        bindOptionalText(db, raw.handle, 7, item.description);
        bindOptionalText(db, raw.handle, 8, item.language);
        bindOptionalInteger(db, raw.handle, 9, item.encoding);
        stepDone(db, raw.handle, "Inserting raw tag");
    }

    auto picture = prepare(db, R"sql(
INSERT INTO pictures(file_id,ordinal,picture_type,mime,description,width,height,size_bytes)
VALUES(?,?,?,?,?,?,?,?);
)sql");
    for (std::size_t ordinal = 0; ordinal < record.pictures.size(); ++ordinal) {
        const auto& item = record.pictures[ordinal];
        checkSqlite(sqlite3_bind_int64(picture.handle, 1, fileId), db, "Binding file id");
        checkSqlite(sqlite3_bind_int64(picture.handle, 2, static_cast<sqlite3_int64>(ordinal)),
            db, "Binding picture ordinal");
        bindOptionalInteger(db, picture.handle, 3, item.type);
        bindOptionalText(db, picture.handle, 4, item.mime);
        bindOptionalText(db, picture.handle, 5, item.description);
        bindOptionalInteger(db, picture.handle, 6, item.width);
        bindOptionalInteger(db, picture.handle, 7, item.height);
        checkSqlite(sqlite3_bind_int64(picture.handle, 8,
            static_cast<sqlite3_int64>(item.sizeBytes)), db, "Binding picture size");
        stepDone(db, picture.handle, "Inserting picture descriptor");
    }

    auto chapter = prepare(db, R"sql(
INSERT INTO chapters(file_id,ordinal,chapter_id,time_base,start_tick,start_seconds,end_tick,end_seconds)
VALUES(?,?,?,?,?,?,?,?);
)sql");
    auto chapterTag = prepare(db, R"sql(
INSERT INTO chapter_tags(file_id,chapter_ordinal,field,value,ordinal) VALUES(?,?,?,?,?);
)sql");
    for (std::size_t ordinal = 0; ordinal < record.chapters.size(); ++ordinal) {
        const auto& item = record.chapters[ordinal];
        checkSqlite(sqlite3_bind_int64(chapter.handle, 1, fileId), db, "Binding file id");
        checkSqlite(sqlite3_bind_int64(chapter.handle, 2, static_cast<sqlite3_int64>(ordinal)),
            db, "Binding chapter ordinal");
        bindOptionalInteger(db, chapter.handle, 3, item.id);
        bindOptionalText(db, chapter.handle, 4, item.timeBase);
        bindOptionalInteger(db, chapter.handle, 5, item.start);
        bindOptionalDouble(db, chapter.handle, 6, item.startSeconds);
        bindOptionalInteger(db, chapter.handle, 7, item.end);
        bindOptionalDouble(db, chapter.handle, 8, item.endSeconds);
        stepDone(db, chapter.handle, "Inserting chapter");
        for (const auto& [field, values] : item.tags) {
            for (std::size_t tagOrdinal = 0; tagOrdinal < values.size(); ++tagOrdinal) {
                checkSqlite(sqlite3_bind_int64(chapterTag.handle, 1, fileId), db,
                    "Binding file id");
                checkSqlite(sqlite3_bind_int64(chapterTag.handle, 2,
                    static_cast<sqlite3_int64>(ordinal)), db, "Binding chapter ordinal");
                bindText(db, chapterTag.handle, 3, field);
                bindText(db, chapterTag.handle, 4, values[tagOrdinal]);
                checkSqlite(sqlite3_bind_int64(chapterTag.handle, 5,
                    static_cast<sqlite3_int64>(tagOrdinal)), db, "Binding chapter tag ordinal");
                stepDone(db, chapterTag.handle, "Inserting chapter tag");
            }
        }
    }

    auto measurement = prepare(db,
        "INSERT INTO measurements(file_id,kind,status,json) VALUES(?,?,?,?);");
    for (const auto& [kind, status] : std::array{
        std::pair{"integrity", record.integrity.status},
        std::pair{"audioAnalysis", record.analysisStatus}
    }) {
        checkSqlite(sqlite3_bind_int64(measurement.handle, 1, fileId), db, "Binding file id");
        bindText(db, measurement.handle, 2, kind);
        bindText(db, measurement.handle, 3, status);
        bindText(db, measurement.handle, 4, objectJson(recordJson, kind));
        stepDone(db, measurement.handle, "Inserting catalog measurement");
    }

    auto finding = prepare(db,
        "INSERT INTO findings(file_id,album_id,code,severity,confidence,evidence) VALUES(?,NULL,?,?,?,?);");
    for (const auto& item : record.findings) {
        checkSqlite(sqlite3_bind_int64(finding.handle, 1, fileId), db, "Binding file id");
        bindText(db, finding.handle, 2, item.code);
        bindText(db, finding.handle, 3, item.severity);
        bindText(db, finding.handle, 4, item.confidence);
        bindText(db, finding.handle, 5, item.evidence);
        stepDone(db, finding.handle, "Inserting catalog finding");
    }
}

void validateSchema(sqlite3* db)
{
    const auto version = pragmaInteger(db, "PRAGMA user_version;");
    if (version == 2) {
        static constexpr std::array requiredTables{
            "libraries", "albums", "files", "tags", "raw_tags", "pictures",
            "chapters", "chapter_tags", "measurements", "findings", "schema_docs",
            "enum_docs", "catalog_meta"
        };
        static constexpr std::array requiredViews{ "v_tracks", "v_findings", "v_albums" };
        Database reference;
        checkSqlite(sqlite3_open(":memory:", &reference.handle), reference.handle,
            "Creating schema reference");
        createSchema(reference.handle);
        const auto definition = [](sqlite3* database, const char* type, const char* name) {
            auto statement = prepare(database,
                "SELECT sql FROM sqlite_schema WHERE type=? AND name=?;");
            bindText(database, statement.handle, 1, type);
            bindText(database, statement.handle, 2, name);
            const int result = sqlite3_step(statement.handle);
            if (result == SQLITE_DONE) return std::string{};
            if (result != SQLITE_ROW) checkSqlite(result, database, "Validating schema definition");
            const auto* text = sqlite3_column_text(statement.handle, 0);
            return text ? std::string(reinterpret_cast<const char*>(text)) : std::string{};
        };
        const auto checkObject = [&](const char* type, const char* name) {
            if (!hasObject(db, type, name) ||
                definition(db, type, name) != definition(reference.handle, type, name)) {
                throw std::runtime_error(
                    std::string("The catalog-v2.sqlite file has an incompatible ") + type + ": " + name);
            }
        };
        for (const char* table : requiredTables) checkObject("table", table);
        for (const char* view : requiredViews) checkObject("view", view);
        return;
    }
    if (version != 0) {
        throw std::runtime_error("Refusing a database with an unsupported SQLite user_version.");
    }
    const auto userTables = pragmaInteger(db,
        "SELECT COUNT(*) FROM sqlite_schema WHERE type='table' AND name NOT LIKE 'sqlite_%';");
    if (userTables != 0) {
        throw std::runtime_error(
            "Refusing an existing database without catalog schema v2; it was not modified.");
    }
    execute(db, "BEGIN IMMEDIATE;");
    try {
        createSchema(db);
        execute(db, "PRAGMA user_version=2;");
        execute(db, "COMMIT;");
    }
    catch (...) {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }
}

bool isSafeRelative(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute() || path.has_root_name()) return false;
    for (const auto& component : path) {
        if (component == "..") return false;
    }
    return true;
}

}

bool CatalogDatabase::Import(const DirectoryContentEntryList& albums,
    const std::filesystem::path& databasePath,
    const std::optional<std::filesystem::path>& requestedLibraryRoot)
{
    if (albums.empty()) {
        spdlog::error("Cannot import an empty catalog into SQLite.");
        return false;
    }
    try {
        std::optional<std::filesystem::path> recordLibraryRoot;
        for (const auto& [directory, tracks] : albums) {
            (void)directory;
            for (const auto& track : tracks) {
                if (!track.catalogRecord || !track.libraryRoot) {
                    throw std::runtime_error(
                        "Database import requires schema-v2 catalog JSON with Library Root; rescan the library first.");
                }
                const auto trackRoot = std::filesystem::absolute(*track.libraryRoot).lexically_normal();
                if (!recordLibraryRoot) {
                    recordLibraryRoot = trackRoot;
                }
                else if (*recordLibraryRoot != trackRoot) {
                    throw std::runtime_error("JSON batches contain more than one source library root.");
                }
            }
        }
        if (!recordLibraryRoot) {
            throw std::runtime_error("Could not determine catalog library root from JSON.");
        }
        const auto libraryRoot = *recordLibraryRoot;
        if (requestedLibraryRoot &&
            std::filesystem::absolute(*requestedLibraryRoot).lexically_normal() != libraryRoot) {
            throw std::runtime_error("Configured library root does not match the root recorded in JSON.");
        }
        std::error_code ec;
        const auto canonicalLibraryRoot = std::filesystem::weakly_canonical(libraryRoot, ec);
        if (ec) throw std::runtime_error("Cannot resolve source library root: " + ec.message());
        const auto canonicalDatabase = std::filesystem::weakly_canonical(databasePath, ec);
        if (ec) throw std::runtime_error("Cannot resolve database destination: " + ec.message());
        if (isSameOrChildPath(canonicalDatabase, canonicalLibraryRoot)) {
            throw std::runtime_error("Database destination must not be inside the source library.");
        }
        if (!databasePath.parent_path().empty()) {
            std::filesystem::create_directories(databasePath.parent_path(), ec);
            if (ec) throw std::runtime_error("Cannot create database directory: " + ec.message());
        }
        const std::string libraryRootUtf8 = pathUtf8(libraryRoot);
        Database database;
#ifdef _WIN32
        const auto databasePathUtf8 = pathUtf8(databasePath);
        const int openResult = sqlite3_open_v2(databasePathUtf8.c_str(), &database.handle,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, "win32-longpath");
#else
        const auto databasePathUtf8 = pathUtf8(databasePath);
        const int openResult = sqlite3_open(databasePathUtf8.c_str(), &database.handle);
#endif
        checkSqlite(openResult, database.handle, "Opening catalog database");
        execute(database.handle, "PRAGMA foreign_keys=ON;");
        validateSchema(database.handle);
        execute(database.handle, "PRAGMA journal_mode=WAL;");

        execute(database.handle, "BEGIN IMMEDIATE;");
        try {
            const auto scanId = newScanId();
            const auto scannedUtc = utcNow();
            const auto libraryId = getLibraryId(database.handle, libraryRootUtf8);
            std::vector<std::int64_t> seenAlbums;
            for (const auto& [directory, tracks] : albums) {
                const auto albumPath = directory.path().lexically_normal();
                const auto albumRelative = albumPath.lexically_relative(libraryRoot);
                if (!isSafeRelative(albumRelative)) {
                    throw std::runtime_error("An album is outside the configured library root.");
                }
                const auto albumRelativeUtf8 = pathString(albumRelative);
                const auto albumPathUtf8 = pathUtf8(albumPath);
                const auto folderUtf8 = pathUtf8(albumPath.filename());
                const auto albumId = getAlbumId(database.handle, libraryId, albumRelativeUtf8,
                    albumPathUtf8, folderUtf8, scanId);
                seenAlbums.push_back(albumId);

                for (const auto& track : tracks) {
                    const auto& record = *track.catalogRecord;
                    auto fileRelative = albumRelative / CommonUtils::path_from_utf8(record.relativePath);
                    fileRelative = fileRelative.lexically_normal();
                    if (!isSafeRelative(fileRelative)) {
                        throw std::runtime_error("A catalog file path is not a safe library-relative path.");
                    }
                    const auto fileRelativeUtf8 = pathString(fileRelative);
                    const auto recordJson = SerializeMediaRecord(record);
                    if (existingRecordJson(database.handle, libraryId, fileRelativeUtf8) == recordJson) {
                        auto markSeen = prepare(database.handle,
                            "UPDATE files SET album_id=?,last_seen_scan_id=? "
                            "WHERE library_id=? AND relative_path=?;");
                        checkSqlite(sqlite3_bind_int64(markSeen.handle, 1, albumId),
                            database.handle, "Binding album id");
                        bindText(database.handle, markSeen.handle, 2, scanId);
                        checkSqlite(sqlite3_bind_int64(markSeen.handle, 3, libraryId),
                            database.handle, "Binding library id");
                        bindText(database.handle, markSeen.handle, 4, fileRelativeUtf8);
                        stepDone(database.handle, markSeen.handle, "Marking unchanged catalog file");
                        continue;
                    }
                    insertRecord(database.handle, libraryId, albumId, fileRelativeUtf8,
                        record, recordJson, scanId, scannedUtc);
                }

                auto removeMissing = prepare(database.handle,
                    "DELETE FROM files WHERE album_id=? AND last_seen_scan_id<>?;");
                checkSqlite(sqlite3_bind_int64(removeMissing.handle, 1, albumId),
                    database.handle, "Binding album id");
                bindText(database.handle, removeMissing.handle, 2, scanId);
                stepDone(database.handle, removeMissing.handle, "Removing missing album files");
            }
            execute(database.handle, "COMMIT;");
            spdlog::info("Catalog database updated: {} album(s), schema v2.",
                seenAlbums.size());
        }
        catch (...) {
            sqlite3_exec(database.handle, "ROLLBACK;", nullptr, nullptr, nullptr);
            throw;
        }
        return true;
    }
    catch (const std::exception& error) {
        spdlog::error("Catalog database import failed for '{}': {}",
            pathUtf8(databasePath), error.what());
        return false;
    }
}
