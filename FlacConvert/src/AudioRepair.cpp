#include "AudioRepair.h"

#include "CommonUtils.h"
#include "MediaConvertionTask.h"
#include "PlatformUtils.h"
#include "PreservationAudit.h"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
namespace fs = std::filesystem;

struct MetadataBlock
{
    std::uint8_t flags{ 0 };
    std::uint8_t type{ 0 };
    std::vector<std::uint8_t> data;
};

struct ParsedFlac
{
    std::vector<MetadataBlock> blocks;
    std::uint64_t audioOffset{ 0 };
    std::size_t commentBlock{ 0 };
    std::vector<std::uint8_t> vendor;
    std::vector<std::vector<std::uint8_t>> comments;
};

struct CommentResult
{
    bool repairable{ false };
    bool manualReview{ false };
    std::size_t removed{ 0 };
    std::string detail;
};

std::string utf8(const fs::path& path)
{
    return CommonUtils::path_to_utf8(path);
}

std::string asciiLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) :
            static_cast<char>(ch);
    });
    return value;
}

std::uint32_t readLittle32(const std::vector<std::uint8_t>& data, std::size_t& offset)
{
    if (offset > data.size() || data.size() - offset < 4) {
        throw std::runtime_error("Truncated FLAC Vorbis comment.");
    }
    const auto value = static_cast<std::uint32_t>(data[offset]) |
        (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return value;
}

void appendLittle32(std::vector<std::uint8_t>& data, std::uint32_t value)
{
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        data.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::vector<std::uint8_t> readSized(const std::vector<std::uint8_t>& data, std::size_t& offset)
{
    const auto size = readLittle32(data, offset);
    if (size > data.size() - offset) {
        throw std::runtime_error("Vorbis comment field exceeds its metadata block.");
    }
    std::vector<std::uint8_t> value(data.begin() + static_cast<std::ptrdiff_t>(offset),
        data.begin() + static_cast<std::ptrdiff_t>(offset + size));
    offset += size;
    return value;
}

std::vector<std::uint8_t> serializeComments(const ParsedFlac& flac,
    const std::vector<std::vector<std::uint8_t>>& comments)
{
    std::vector<std::uint8_t> result;
    if (flac.vendor.size() > UINT32_MAX || comments.size() > UINT32_MAX) {
        throw std::runtime_error("Vorbis comment count or vendor string exceeds FLAC limits.");
    }
    appendLittle32(result, static_cast<std::uint32_t>(flac.vendor.size()));
    result.insert(result.end(), flac.vendor.begin(), flac.vendor.end());
    appendLittle32(result, static_cast<std::uint32_t>(comments.size()));
    for (const auto& comment : comments) {
        if (comment.size() > UINT32_MAX) {
            throw std::runtime_error("Vorbis comment exceeds FLAC size limits.");
        }
        appendLittle32(result, static_cast<std::uint32_t>(comment.size()));
        result.insert(result.end(), comment.begin(), comment.end());
    }
    if (result.size() > 0x00ffffffU) {
        throw std::runtime_error("Rewritten Vorbis comment block exceeds the FLAC block size limit.");
    }
    return result;
}

ParsedFlac readFlac(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    std::array<char, 4> marker{};
    if (!input.read(marker.data(), static_cast<std::streamsize>(marker.size())) ||
        std::string(marker.data(), marker.size()) != "fLaC") {
        throw std::runtime_error("Not a native FLAC stream.");
    }

    ParsedFlac result;
    bool last = false;
    while (!last) {
        std::array<std::uint8_t, 4> header{};
        if (!input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()))) {
            throw std::runtime_error("Truncated FLAC metadata header.");
        }
        const auto size = (static_cast<std::uint32_t>(header[1]) << 16) |
            (static_cast<std::uint32_t>(header[2]) << 8) | header[3];
        MetadataBlock block;
        block.flags = header[0];
        block.type = header[0] & 0x7f;
        if (block.type == 127 || (result.blocks.empty() &&
                (block.type != 0 || size != 34)) || (block.type == 0 && !result.blocks.empty())) {
            throw std::runtime_error("Invalid FLAC metadata block sequence.");
        }
        block.data.resize(size);
        if (size != 0 && !input.read(reinterpret_cast<char*>(block.data.data()),
                static_cast<std::streamsize>(size))) {
            throw std::runtime_error("Truncated FLAC metadata block.");
        }
        if (block.type == 4) {
            if (std::any_of(result.blocks.begin(), result.blocks.end(),
                        [](const MetadataBlock& item) { return item.type == 4; })) {
                throw std::runtime_error("Multiple FLAC Vorbis comment blocks are not supported.");
            }
            result.commentBlock = result.blocks.size() + 1;
            std::size_t offset = 0;
            result.vendor = readSized(block.data, offset);
            const auto count = readLittle32(block.data, offset);
            if (count > (block.data.size() - offset) / 4) {
                throw std::runtime_error("Invalid Vorbis comment count.");
            }
            result.comments.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index) {
                result.comments.push_back(readSized(block.data, offset));
            }
            if (offset != block.data.size()) {
                throw std::runtime_error("Unexpected trailing bytes in FLAC Vorbis comments.");
            }
        }
        result.blocks.push_back(std::move(block));
        last = (header[0] & 0x80) != 0;
    }
    const auto offset = input.tellg();
    if (offset < 0) {
        throw std::runtime_error("Cannot locate FLAC audio data.");
    }
    result.audioOffset = static_cast<std::uint64_t>(offset);
    return result;
}

CommentResult removeDuplicateCdToc(ParsedFlac& flac)
{
    CommentResult result;
    if (flac.commentBlock == 0) {
        return result;
    }
    std::map<std::string, std::vector<std::uint8_t>> values;
    std::size_t occurrences = 0;
    for (const auto& comment : flac.comments) {
        const auto separator = std::find(comment.begin(), comment.end(), static_cast<std::uint8_t>('='));
        if (separator == comment.end()) continue;
        const std::string key(comment.begin(), separator);
        if (asciiLower(key) != "cdtoc") continue;
        ++occurrences;
        const std::vector<std::uint8_t> value(separator + 1, comment.end());
        const auto [found, inserted] = values.emplace("cdtoc", value);
        if (!inserted && found->second != value) {
            result.manualReview = true;
            result.detail = "CDTOC comments contain different values; no value was selected or changed.";
            return result;
        }
        if (std::find(value.begin(), value.end(), static_cast<std::uint8_t>(';')) != value.end()) {
            result.manualReview = true;
            result.detail = "CDTOC contains a semicolon-joined value; its original occurrences cannot be inferred safely.";
            return result;
        }
    }
    if (occurrences < 2) {
        return result;
    }

    bool retained = false;
    std::vector<std::vector<std::uint8_t>> kept;
    kept.reserve(flac.comments.size());
    for (const auto& comment : flac.comments) {
        const auto separator = std::find(comment.begin(), comment.end(), static_cast<std::uint8_t>('='));
        const auto key = separator == comment.end() ? std::string{} : std::string(comment.begin(), separator);
        if (separator != comment.end() && asciiLower(key) == "cdtoc") {
            if (retained) {
                ++result.removed;
                continue;
            }
            retained = true;
        }
        kept.push_back(comment);
    }
    if (result.removed != 0) {
        flac.comments = std::move(kept);
        flac.blocks[flac.commentBlock - 1].data = serializeComments(flac, flac.comments);
        result.repairable = true;
        result.detail = "Removed exact duplicate CDTOC comments, retaining the first occurrence.";
    }
    return result;
}

bool copyStream(std::istream& source, std::ostream& destination)
{
    std::array<char, 64 * 1024> buffer{};
    while (source) {
        source.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = source.gcount();
        if (count > 0 && !destination.write(buffer.data(), count)) {
            return false;
        }
    }
    return source.eof() && static_cast<bool>(destination);
}

void writeFlac(const fs::path& sourcePath, const fs::path& targetPath, ParsedFlac flac)
{
    std::ifstream source(sourcePath, std::ios::binary);
    std::ofstream target(targetPath, std::ios::binary | std::ios::trunc);
    if (!source || !target) {
        throw std::runtime_error("Cannot open source or temporary repair output.");
    }
    target.write("fLaC", 4);
    for (const auto& block : flac.blocks) {
        const std::uint32_t size = static_cast<std::uint32_t>(block.data.size());
        const std::array<char, 4> header{
            static_cast<char>(block.flags),
            static_cast<char>(size >> 16),
            static_cast<char>(size >> 8),
            static_cast<char>(size)
        };
        target.write(header.data(), static_cast<std::streamsize>(header.size()));
        if (!block.data.empty()) {
            target.write(reinterpret_cast<const char*>(block.data.data()),
                static_cast<std::streamsize>(block.data.size()));
        }
    }
    source.seekg(static_cast<std::streamoff>(flac.audioOffset));
    if (!source || !copyStream(source, target)) {
        throw std::runtime_error("Cannot write complete repaired FLAC output.");
    }
    target.flush();
    if (!target) {
        throw std::runtime_error("Cannot flush repaired FLAC output.");
    }
}

bool sameFileBytes(const fs::path& left, std::uint64_t leftOffset,
    const fs::path& right, std::uint64_t rightOffset)
{
    std::ifstream a(left, std::ios::binary);
    std::ifstream b(right, std::ios::binary);
    if (!a || !b) return false;
    a.seekg(static_cast<std::streamoff>(leftOffset));
    b.seekg(static_cast<std::streamoff>(rightOffset));
    if (!a || !b) return false;
    std::array<char, 64 * 1024> leftBuffer{}, rightBuffer{};
    for (;;) {
        a.read(leftBuffer.data(), static_cast<std::streamsize>(leftBuffer.size()));
        b.read(rightBuffer.data(), static_cast<std::streamsize>(rightBuffer.size()));
        const auto leftCount = a.gcount();
        const auto rightCount = b.gcount();
        if (leftCount != rightCount || !std::equal(leftBuffer.begin(),
                leftBuffer.begin() + leftCount, rightBuffer.begin())) {
            return false;
        }
        if (leftCount == 0) return a.eof() && b.eof();
    }
}

bool verifyRepair(const fs::path& sourcePath, const fs::path& outputPath,
    std::size_t expectedRemoved, std::string& error)
{
    try {
        auto source = readFlac(sourcePath);
        auto output = readFlac(outputPath);
        if (source.commentBlock == 0 || output.commentBlock == 0 ||
            source.blocks.size() != output.blocks.size()) {
            error = "FLAC metadata block layout changed.";
            return false;
        }
        auto expected = source;
        const auto classified = removeDuplicateCdToc(expected);
        if (!classified.repairable || classified.removed != expectedRemoved) {
            error = "Source no longer has the planned duplicate CDTOC comments.";
            return false;
        }
        for (std::size_t index = 0; index < source.blocks.size(); ++index) {
            const auto& original = source.blocks[index];
            const auto& actual = output.blocks[index];
            if (original.flags != actual.flags || original.type != actual.type ||
                (index == source.commentBlock - 1
                    ? expected.blocks[index].data != actual.data
                    : original.data != actual.data)) {
                error = "A non-target metadata block or unexpected comment changed.";
                return false;
            }
        }
        if (!sameFileBytes(sourcePath, source.audioOffset, outputPath, output.audioOffset)) {
            error = "FLAC audio bytes changed.";
            return false;
        }
        return true;
    }
    catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

std::atomic_uint64_t temporarySequence{ 0 };

fs::path temporaryPath(const fs::path& target)
{
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
    const auto processId = _getpid();
#else
    const auto processId = getpid();
#endif
    return target.parent_path() / (".afc-repair-" + std::to_string(processId) + "-" +
        std::to_string(timestamp) + "-" + std::to_string(++temporarySequence) + ".tmp");
}

fs::path ioPath(const fs::path& path, const fs::path& outputRoot)
{
#ifdef _WIN32
    if (path.is_absolute()) return PlatformUtils::WindowsLongPath(path);
    return PlatformUtils::WindowsLongPath(outputRoot / path);
#else
    (void)outputRoot;
    return path;
#endif
}

bool samePathComponent(const fs::path& left, const fs::path& right)
{
#ifdef _WIN32
    const auto& a = left.native();
    const auto& b = right.native();
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
        b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
#else
    return left == right;
#endif
}

bool isSameOrChildPath(const fs::path& candidate, const fs::path& base)
{
    auto candidatePart = candidate.begin();
    for (auto basePart = base.begin(); basePart != base.end(); ++basePart, ++candidatePart) {
        if (candidatePart == candidate.end() || !samePathComponent(*candidatePart, *basePart)) {
            return false;
        }
    }
    return true;
}

bool writeReport(const AudioRepair::Plan& plan, const fs::path& reportPath,
    const std::string& status, const std::string& error)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("schemaVersion"); writer.Int(1);
    writer.Key("source"); writer.String(utf8(plan.input).c_str());
    writer.Key("output"); writer.String(utf8(plan.output).c_str());
    writer.Key("status"); writer.String(status.c_str());
    writer.Key("error"); writer.String(error.c_str());
    writer.Key("inspectedFiles"); writer.Uint64(plan.inspectedFiles);
    writer.Key("repairedFiles");
    writer.Uint64(static_cast<std::uint64_t>(std::count_if(plan.files.begin(), plan.files.end(),
        [](const AudioRepair::PlannedFile& file) { return file.repair && file.status == "installed"; })));
    writer.Key("copiedFiles");
    writer.Uint64(static_cast<std::uint64_t>(std::count_if(plan.files.begin(), plan.files.end(),
        [](const AudioRepair::PlannedFile& file) { return !file.repair && file.status == "installed"; })));
    writer.Key("findings"); writer.StartArray();
    for (const auto& finding : plan.findings) {
        writer.StartObject();
        writer.Key("source"); writer.String(utf8(finding.source.lexically_relative(plan.input)).c_str());
        writer.Key("rule"); writer.String("duplicate-cdtoc");
        writer.Key("status"); writer.String(finding.status.c_str());
        writer.Key("duplicateComments"); writer.Uint64(finding.duplicateComments);
        writer.Key("detail"); writer.String(finding.detail.c_str());
        writer.EndObject();
    }
    writer.EndArray();
    writer.Key("files"); writer.StartArray();
    for (const auto& file : plan.files) {
        writer.StartObject();
        writer.Key("source"); writer.String(utf8(file.source.lexically_relative(plan.input)).c_str());
        writer.Key("output"); writer.String(utf8(file.target.lexically_relative(plan.output)).c_str());
        writer.Key("action"); writer.String(file.repair ? "repair" : "copy");
        writer.Key("status"); writer.String(file.status.c_str());
        if (file.repair) {
            writer.Key("removedDuplicateComments"); writer.Uint64(file.duplicateComments);
            writer.Key("impact"); writer.StartObject();
            writer.Key("audio"); writer.String("Byte-identical; not decoded or re-encoded.");
            writer.Key("metadata");
            writer.String("Only repeated CDTOC comments with identical values were removed; the first occurrence remains.");
            writer.Key("otherMetadata"); writer.String("All other tags and metadata blocks are byte-identical.");
            writer.EndObject();
        }
        else {
            writer.Key("impact");
            writer.String("File copied byte-for-byte; no audio or metadata changes.");
        }
        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();

    const auto temporary = temporaryPath(reportPath);
    std::error_code ec;
    if (fs::exists(ioPath(temporary, plan.output), ec) || ec) return false;
    std::ofstream output(ioPath(temporary, plan.output), std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
    output.flush();
    const bool success = static_cast<bool>(output);
    output.close();
    if (!success || output.fail()) return false;
    std::ifstream verify(ioPath(temporary, plan.output), std::ios::binary);
    const std::string actual{ std::istreambuf_iterator<char>(verify), std::istreambuf_iterator<char>() };
    const bool reportMatches = !verify.bad() &&
        actual == std::string(buffer.GetString(), buffer.GetSize());
    verify.close();
    if (!reportMatches) return false;
    if (!MediaConvertionTask::InstallOutputFile(ioPath(temporary, plan.output),
            ioPath(reportPath, plan.output), plan.overwrite)) {
        return false;
    }
    return true;
}

bool isFlac(const fs::path& path)
{
    auto extension = path.extension().string();
    return asciiLower(extension) == ".flac";
}
}

namespace AudioRepair
{
int Prepare(const fs::path& input, const fs::path& output, bool overwrite, Plan& plan)
{
    plan = {};
    std::error_code ec;
    plan.input = fs::absolute(input, ec).lexically_normal();
    if (ec || !fs::is_directory(plan.input, ec) || ec) {
        spdlog::error("Repair input is not a readable directory: '{}'.", utf8(input));
        return -1;
    }
    plan.output = fs::absolute(output, ec).lexically_normal();
    if (ec || plan.output.empty()) {
        spdlog::error("Cannot resolve repair output directory: {}", ec.message());
        return -1;
    }
    plan.overwrite = overwrite;
    const auto canonicalInput = fs::weakly_canonical(plan.input, ec);
    if (ec) {
        spdlog::error("Cannot resolve repair input: {}", ec.message());
        return -1;
    }
    const auto canonicalOutput = fs::weakly_canonical(plan.output, ec);
    if (ec || isSameOrChildPath(canonicalOutput, canonicalInput) ||
        isSameOrChildPath(canonicalInput, canonicalOutput)) {
        spdlog::error("Repair output overlaps its source library.");
        return -1;
    }

    std::set<fs::path> affected;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(plan.input)) {
            const auto status = entry.symlink_status();
            if (fs::is_symlink(status)) {
                if (entry.is_directory(ec) && !ec) {
                    spdlog::error("Repair input contains a directory link: '{}'.", utf8(entry.path()));
                    return -1;
                }
                continue;
            }
            if (!fs::is_regular_file(status) || !isFlac(entry.path())) continue;
            ++plan.inspectedFiles;
            try {
                auto flac = readFlac(entry.path());
                const auto result = removeDuplicateCdToc(flac);
                if (result.repairable) {
                    auto folder = entry.path().parent_path().lexically_relative(plan.input);
                    if (folder == ".") folder.clear();
                    affected.insert(std::move(folder));
                    plan.findings.push_back({ entry.path(), result.removed, "repairable", result.detail });
                }
                else if (result.manualReview) {
                    plan.findings.push_back({ entry.path(), 0, "manual-review", result.detail });
                }
            }
            catch (const std::exception& exception) {
                plan.findings.push_back({ entry.path(), 0, "unreadable", exception.what() });
            }
        }
    }
    catch (const fs::filesystem_error& exception) {
        spdlog::error("Cannot inspect repair input: {}", exception.what());
        return -1;
    }

    for (auto it = affected.begin(); it != affected.end();) {
        bool covered = false;
        for (auto parent = it->parent_path(); !parent.empty(); parent = parent.parent_path()) {
            if (affected.contains(parent)) {
                covered = true;
                break;
            }
        }
        if (covered) it = affected.erase(it);
        else ++it;
    }
    plan.repairedFolders.assign(affected.begin(), affected.end());

    std::set<fs::path> repairSources;
    for (const auto& finding : plan.findings) {
        if (finding.status == "repairable") repairSources.insert(finding.source);
    }
    std::set<fs::path> directories;
    try {
        for (const auto& folder : plan.repairedFolders) {
            const auto root = folder.empty() ? plan.input : plan.input / folder;
            directories.insert(folder);
            for (const auto& entry : fs::recursive_directory_iterator(root)) {
                const auto status = entry.symlink_status();
                if (fs::is_symlink(status)) {
                    spdlog::error("Affected repair folder contains a link: '{}'.", utf8(entry.path()));
                    return -1;
                }
                const auto relative = entry.path().lexically_relative(plan.input);
                if (fs::is_directory(status)) {
                    directories.insert(relative);
                    continue;
                }
                if (!fs::is_regular_file(status)) {
                    spdlog::error("Affected repair folder contains a non-regular file: '{}'.", utf8(entry.path()));
                    return -1;
                }
                const auto size = entry.file_size(ec);
                const auto modified = entry.last_write_time(ec);
                if (ec) {
                    spdlog::error("Cannot inspect repair source '{}': {}", utf8(entry.path()), ec.message());
                    return -1;
                }
                const auto target = plan.output / relative;
                const auto canonicalTarget = fs::weakly_canonical(target, ec);
                if (ec || isSameOrChildPath(canonicalTarget, canonicalInput)) {
                    spdlog::error("Repair destination overlaps its source: '{}'.", utf8(target));
                    return -1;
                }
                const bool targetExists = fs::exists(target, ec);
                if (ec) {
                    spdlog::error("Cannot inspect repair destination '{}': {}", utf8(target), ec.message());
                    return -1;
                }
                if (targetExists) {
                    if (!overwrite) {
                        plan.conflicts.push_back(target);
                    }
                    else if (!fs::is_regular_file(target, ec) || ec) {
                        spdlog::error("Repair destination is not a regular file: '{}'.", utf8(target));
                        return -1;
                    }
                }
                const bool repair = repairSources.contains(entry.path());
                const auto finding = std::find_if(plan.findings.begin(), plan.findings.end(),
                    [&](const Finding& item) { return item.source == entry.path() && item.status == "repairable"; });
                plan.files.push_back({ entry.path(), target, {}, size, modified, repair,
                    finding == plan.findings.end() ? 0 : finding->duplicateComments, "not-attempted" });
                plan.files.back().temporary = temporaryPath(target);
            }
        }
    }
    catch (const fs::filesystem_error& exception) {
        spdlog::error("Cannot prepare affected repair folders: {}", exception.what());
        return -1;
    }

    for (const auto& directory : directories) {
        const auto target = plan.output / directory;
        if (fs::exists(target, ec) && (!overwrite || !fs::is_directory(target, ec) || ec)) {
            plan.conflicts.push_back(target);
        }
        if (ec) {
            spdlog::error("Cannot inspect repair directory destination: {}", ec.message());
            return -1;
        }
    }
    const auto report = plan.output / "repair-report.json";
    if (fs::exists(report, ec) && !overwrite) plan.conflicts.push_back(report);
    if (ec) {
        spdlog::error("Cannot inspect repair report destination: {}", ec.message());
        return -1;
    }
    const auto marker = plan.output / ".repair-in-progress";
    if (fs::exists(marker, ec) || ec) {
        spdlog::error("Repair output has an existing in-progress marker or cannot be inspected.");
        return -1;
    }
    return 0;
}

int Execute(Plan& plan, std::size_t* repaired, std::size_t* copied)
{
    if (!plan.conflicts.empty()) {
        spdlog::error("Repair plan has {} destination conflicts.", plan.conflicts.size());
        return -1;
    }
    if (repaired) *repaired = 0;
    if (copied) *copied = 0;
    bool owned = false;
    PlatformUtils::ScopedCurrentDirectory outputDirectory;
    if (!outputDirectory.EnterValidatedDirectory(plan.output, plan.input, true, &owned)) {
        return -1;
    }
    (void)owned;
    const auto resolve = [&](const fs::path& path) {
#ifdef _WIN32
        return PlatformUtils::WindowsLongPath(path.is_absolute() ? path : plan.output / path);
#else
        return path.is_absolute() ? path : plan.output / path;
#endif
    };
    std::error_code ec;
    const auto marker = resolve(".repair-in-progress");
    if (!fs::create_directory(marker, ec)) {
        spdlog::error("Cannot reserve repair marker: {}.", ec ? ec.message() : "already exists");
        return -1;
    }

    std::string error;
    fs::path currentAlbum;
    for (auto& file : plan.files) {
        file.status = "processing";
        if (file.repair && file.source.parent_path() != currentAlbum) {
            currentAlbum = file.source.parent_path();
            spdlog::info("Repairing album folder: {}", utf8(currentAlbum));
        }
        if (file.repair) {
            spdlog::info("Checking repair candidate: {}",
                utf8(file.source.lexically_relative(plan.input)));
        }
        const auto relative = file.target.lexically_relative(plan.output);
        const auto temporary = file.temporary.lexically_relative(plan.output);
        if (relative.empty() || temporary.empty() || *relative.begin() == ".." ||
            *temporary.begin() == "..") {
            file.status = "failed";
            error = "repair destination escaped its output directory";
            break;
        }
        const auto sourceSize = fs::file_size(file.source, ec);
        const auto sourceModified = fs::last_write_time(file.source, ec);
        if (ec || sourceSize != file.size || sourceModified != file.modified) {
            file.status = "failed";
            error = "source changed after repair preview: " + utf8(file.source);
            break;
        }
        fs::create_directories(resolve(relative.parent_path()), ec);
        if (ec) {
            file.status = "failed";
            error = "cannot create repair output directory: " + ec.message();
            break;
        }
        if (file.repair) {
            spdlog::info("Staging repaired copy for '{}'.", utf8(file.target.lexically_relative(plan.output)));
        }
        const auto sourcePath = file.source;
        const auto temporaryPathname = resolve(temporary);
        bool verified = false;
        if (fs::exists(temporaryPathname, ec) || ec) {
            file.status = "failed";
            error = "repair temporary already exists or cannot be inspected: " + utf8(temporaryPathname);
            break;
        }
        if (file.repair) {
            try {
                auto flac = readFlac(sourcePath);
                spdlog::info("Read FLAC metadata for '{}'.",
                    utf8(file.source.lexically_relative(plan.input)));
                const auto result = removeDuplicateCdToc(flac);
                if (!result.repairable || result.removed != file.duplicateComments) {
                    file.status = "failed";
                    error = "repair finding changed after preview: " + utf8(sourcePath);
                    break;
                }
                spdlog::info("Prepared CDTOC metadata edit for '{}'.",
                    utf8(file.source.lexically_relative(plan.input)));
                writeFlac(sourcePath, temporaryPathname, std::move(flac));
                spdlog::info("Wrote staged FLAC repair for '{}'.",
                    utf8(file.source.lexically_relative(plan.input)));
                spdlog::info("Verifying audio and metadata preservation for '{}'.",
                    utf8(file.source.lexically_relative(plan.input)));
                std::string verificationError;
                verified = verifyRepair(sourcePath, temporaryPathname,
                    file.duplicateComments, verificationError);
                if (!verified) {
                    file.status = "failed";
                    error = "repair verification failed: " + verificationError;
                }
            }
            catch (const std::exception& exception) {
                file.status = "failed";
                error = "cannot repair '" + utf8(sourcePath) + "': " + exception.what();
            }
        }
        else {
            fs::copy_file(sourcePath, temporaryPathname, fs::copy_options::none, ec);
            if (!ec) {
                verified = PreservationAudit::VerifyCopy(sourcePath, temporaryPathname);
                if (!verified) {
                    file.status = "failed";
                    error = "unchanged file copy verification failed: " + utf8(sourcePath);
                }
            }
            else {
                file.status = "failed";
                error = "cannot copy '" + utf8(sourcePath) + "': " + ec.message();
            }
        }
        if (!verified) {
            fs::remove(temporaryPathname, ec);
            break;
        }
        if (!MediaConvertionTask::InstallOutputFile(temporaryPathname, resolve(relative), plan.overwrite)) {
            file.status = "failed";
            error = "cannot install verified repair output: " + utf8(file.target);
            break;
        }
        if (file.repair) {
            std::string verificationError;
            if (!verifyRepair(sourcePath, resolve(relative), file.duplicateComments, verificationError)) {
                file.status = "failed";
                error = "installed repair verification failed: " + verificationError;
                break;
            }
            file.status = "installed";
            const auto finding = std::find_if(plan.findings.begin(), plan.findings.end(),
                [&](const Finding& item) { return item.source == file.source && item.status == "repairable"; });
            if (finding != plan.findings.end()) finding->status = "repaired";
            if (repaired) ++*repaired;
            spdlog::info("Repaired '{}': duplicate-cdtoc removed {} exact duplicate comment(s); "
                "audio bytes are unchanged (no decode/re-encode); only duplicate CDTOC metadata was removed, "
                "with all other tags and metadata blocks unchanged.",
                utf8(file.source.lexically_relative(plan.input)), file.duplicateComments);
        }
        else {
            file.status = "installed";
            if (copied) ++*copied;
            spdlog::debug("Copied unchanged repair-folder file: {}",
                utf8(file.source.lexically_relative(plan.input)));
        }
    }

    const bool attention = std::any_of(plan.findings.begin(), plan.findings.end(),
        [](const Finding& finding) { return finding.status == "manual-review" || finding.status == "unreadable"; });
    for (const auto& finding : plan.findings) {
        if (finding.status == "manual-review" || finding.status == "unreadable") {
            spdlog::warn("Repair finding [{}] '{}': {}",
                finding.status, utf8(finding.source.lexically_relative(plan.input)), finding.detail);
        }
    }
    const auto report = resolve("repair-report.json");
    const auto reportStatus = !error.empty() ? "failed" :
        attention ? "completed-with-findings" : plan.files.empty() ? "no-repairs-needed" : "completed";
    if (!writeReport(plan, report, reportStatus, error)) {
        error = error.empty() ? "cannot write or verify repair report" : error;
    }
    if (error.empty() && outputDirectory.IsAnchored(plan.output) &&
        fs::remove(marker, ec) && !ec) {
        if (attention) {
            spdlog::warn("Repair completed with findings requiring review; report '{}'.", utf8(report));
            return 1;
        }
        spdlog::info("Repair completed: {} repaired, {} copied; report '{}'.",
            repaired ? *repaired : 0, copied ? *copied : 0, utf8(report));
        return 0;
    }
    if (error.empty()) {
        error = ec ? ec.message() : "repair output directory changed during execution";
    }
    spdlog::error("Repair failed: {}; output and in-progress marker retained at '{}'.",
        error, utf8(plan.output));
    return -1;
}
}
