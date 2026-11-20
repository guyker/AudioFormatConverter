#include "RepairRules.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string_view>

namespace RepairRules
{
namespace
{
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

const std::vector<RuleInfo> kCatalog{
    { "flac.leading-id3", "flac", true,
        "ID3v2 tag before the fLaC marker: removed when every field is already in the Vorbis comments, else reported." },
    { "flac.trailing-tags", "flac", true,
        "ID3v1/APEv2 tags after the FLAC frames: removed when redundant with the Vorbis comments; Lyrics3 is reported." },
    { "flac.multiple-comment-blocks", "flac", true,
        "Extra Vorbis comment blocks: removed when they only repeat the first block, else reported." },
    { "flac.duplicate-comments", "flac", true,
        "Exact repeated KEY=VALUE comments: later copies removed; conflicting or joined CDTOC values are reported." },
    { "flac.empty-comments", "flac", true, "Comments with an empty value are removed." },
    { "flac.invalid-comments", "flac", false,
        "Malformed comment blocks, missing '=', invalid field names or non-UTF-8 values are reported." },
    { "flac.picture-descriptor", "flac", true,
        "PICTURE MIME type, width, height, colour depth and palette size corrected to match the image bytes; "
        "empty or unrecognized images are reported." },
    { "flac.invalid-seektable", "flac", true,
        "Unsorted or out-of-range SEEKTABLE is replaced by padding of the same size." },
    { "flac.missing-md5", "flac", false, "Unset STREAMINFO MD5 signature is reported (filling it requires decoding)." },
    { "mp3.duplicate-id3v2", "mp3", true,
        "Additional leading ID3v2 tags removed when the first tag already holds all their frames, else reported." },
    { "mp3.duplicate-frames", "mp3", true, "Byte-identical repeated ID3v2 frames are removed." },
    { "mp3.empty-frames", "mp3", true, "ID3v2 text frames without text are removed." },
    { "mp3.text-encoding", "mp3", true,
        "ID3v2.3 text frames using UTF-8/UTF-16BE (2.4-only encodings) are rewritten as UTF-16 with BOM." },
    { "mp3.picture-mime", "mp3", true,
        "APIC MIME type corrected to match the image bytes; empty or unrecognized images are reported." },
    { "mp3.redundant-id3v1", "mp3", true,
        "ID3v1 tag removed when the ID3v2 tag already holds its fields, else reported." },
    { "mp3.ape-lyrics3", "mp3", true,
        "APEv2 tag removed when redundant with ID3v2; other APEv2 and Lyrics3 tags are reported." },
    { "mp3.stream-garbage", "mp3", false, "Non-audio bytes before the first MPEG frame are reported." },
};

struct Reader
{
    std::ifstream stream;
    std::uint64_t size{ 0 };

    explicit Reader(const fs::path& path) : stream(path, std::ios::binary)
    {
        if (!stream) throw std::runtime_error("Cannot open file.");
        stream.seekg(0, std::ios::end);
        const auto end = stream.tellg();
        if (end < 0) throw std::runtime_error("Cannot determine file size.");
        size = static_cast<std::uint64_t>(end);
    }

    Bytes Read(std::uint64_t offset, std::uint64_t count)
    {
        if (offset > size || count > size - offset) throw std::runtime_error("Unexpected end of file.");
        Bytes data(static_cast<std::size_t>(count));
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(offset));
        if (count != 0 && !stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(count))) {
            throw std::runtime_error("Cannot read file.");
        }
        return data;
    }

    bool Matches(std::uint64_t offset, std::string_view text)
    {
        if (offset > size || text.size() > size - offset) return false;
        const auto data = Read(offset, text.size());
        return std::equal(data.begin(), data.end(), text.begin(),
            [](std::uint8_t left, char right) { return left == static_cast<std::uint8_t>(right); });
    }
};

std::uint32_t be32(const Bytes& data, std::size_t offset)
{
    return (static_cast<std::uint32_t>(data[offset]) << 24) | (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
        (static_cast<std::uint32_t>(data[offset + 2]) << 8) | data[offset + 3];
}

std::uint32_t le32(const Bytes& data, std::size_t offset)
{
    return static_cast<std::uint32_t>(data[offset]) | (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(data[offset + 2]) << 16) | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::uint32_t synchsafe(const Bytes& data, std::size_t offset)
{
    return (static_cast<std::uint32_t>(data[offset]) << 21) | (static_cast<std::uint32_t>(data[offset + 1]) << 14) |
        (static_cast<std::uint32_t>(data[offset + 2]) << 7) | data[offset + 3];
}

void appendBe32(Bytes& data, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8) data.push_back(static_cast<std::uint8_t>(value >> shift));
}

void appendLe32(Bytes& data, std::uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8) data.push_back(static_cast<std::uint8_t>(value >> shift));
}

void appendSynchsafe(Bytes& data, std::uint32_t value)
{
    if (value >= (1U << 28)) throw std::runtime_error("ID3v2 size exceeds its synchsafe limit.");
    for (int shift = 21; shift >= 0; shift -= 7) data.push_back(static_cast<std::uint8_t>((value >> shift) & 0x7f));
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : static_cast<char>(ch);
    });
    return value;
}

std::string upper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - ('a' - 'A')) : static_cast<char>(ch);
    });
    return value;
}

std::string trim(std::string value)
{
    const auto blank = [](char ch) { return ch == ' ' || ch == '\0' || ch == '\t' || ch == '\r' || ch == '\n'; };
    while (!value.empty() && blank(value.back())) value.pop_back();
    std::size_t start = 0;
    while (start < value.size() && blank(value[start])) ++start;
    return value.substr(start);
}

// Report text must be valid UTF-8 JSON; untrusted tag bytes are reduced to printable ASCII.
std::string printable(std::string_view value)
{
    std::string result;
    for (const unsigned char ch : value.substr(0, 64)) {
        result.push_back(ch >= 0x20 && ch < 0x7f ? static_cast<char>(ch) : '?');
    }
    return result;
}

std::string joined(const std::set<std::string>& values)
{
    std::string result;
    for (const auto& value : values) {
        if (!result.empty()) result += ", ";
        result += value;
    }
    return result;
}

bool validUtf8(const std::uint8_t* data, std::size_t size)
{
    std::size_t index = 0;
    while (index < size) {
        const auto lead = data[index];
        std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xe ? 3 :
            (lead >> 3) == 0x1e ? 4 : 0;
        if (length == 0 || length > size - index) return false;
        char32_t code = length == 1 ? lead : lead & (0x7f >> length);
        for (std::size_t next = 1; next < length; ++next) {
            if ((data[index + next] & 0xc0) != 0x80) return false;
            code = (code << 6) | (data[index + next] & 0x3f);
        }
        if ((length == 2 && code < 0x80) || (length == 3 && code < 0x800) || (length == 4 && code < 0x10000) ||
            code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) {
            return false;
        }
        index += length;
    }
    return true;
}

void appendUtf8(std::string& output, char32_t code)
{
    if (code < 0x80) output.push_back(static_cast<char>(code));
    else if (code < 0x800) {
        output.push_back(static_cast<char>(0xc0 | (code >> 6)));
        output.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
    else if (code < 0x10000) {
        output.push_back(static_cast<char>(0xe0 | (code >> 12)));
        output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
    else {
        output.push_back(static_cast<char>(0xf0 | (code >> 18)));
        output.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
}

std::string latin1(const std::uint8_t* data, std::size_t size)
{
    std::string result;
    for (std::size_t index = 0; index < size; ++index) appendUtf8(result, data[index]);
    return result;
}

std::optional<std::string> utf16(const std::uint8_t* data, std::size_t size, bool bigEndian)
{
    if (size % 2 != 0) return std::nullopt;
    std::size_t index = 0;
    if (size >= 2 && ((data[0] == 0xff && data[1] == 0xfe) || (data[0] == 0xfe && data[1] == 0xff))) {
        bigEndian = data[0] == 0xfe;
        index = 2;
    }
    std::string result;
    while (index < size) {
        char32_t unit = bigEndian ? (data[index] << 8) | data[index + 1] : data[index] | (data[index + 1] << 8);
        index += 2;
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (index >= size) return std::nullopt;
            const char32_t low = bigEndian ? (data[index] << 8) | data[index + 1] : data[index] | (data[index + 1] << 8);
            if (low < 0xdc00 || low > 0xdfff) return std::nullopt;
            index += 2;
            unit = 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00);
        }
        else if (unit >= 0xdc00 && unit <= 0xdfff) {
            return std::nullopt;
        }
        appendUtf8(result, unit);
    }
    return result;
}

Bytes utf16LeWithBom(const std::string& value)
{
    Bytes result{ 0xff, 0xfe };
    const auto* data = reinterpret_cast<const std::uint8_t*>(value.data());
    std::size_t index = 0;
    while (index < value.size()) {
        const auto lead = data[index];
        const std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xe ? 3 : 4;
        char32_t code = length == 1 ? lead : lead & (0x7f >> length);
        for (std::size_t next = 1; next < length; ++next) code = (code << 6) | (data[index + next] & 0x3f);
        index += length;
        const auto push = [&](char32_t unit) {
            result.push_back(static_cast<std::uint8_t>(unit));
            result.push_back(static_cast<std::uint8_t>(unit >> 8));
        };
        if (code >= 0x10000) {
            code -= 0x10000;
            push(0xd800 + (code >> 10));
            push(0xdc00 + (code & 0x3ff));
        }
        else {
            push(code);
        }
    }
    return result;
}

// Splits an ID3v2 encoded string list; returns nothing for unknown encodings or invalid text.
std::optional<std::vector<std::string>> id3Strings(std::uint8_t encoding, const std::uint8_t* data, std::size_t size,
    bool* terminated = nullptr)
{
    std::vector<std::string> values;
    const bool wide = encoding == 1 || encoding == 2;
    if (encoding > 3) return std::nullopt;
    const std::size_t unit = wide ? 2 : 1;
    std::size_t start = 0, index = 0;
    const auto flush = [&](std::size_t end) -> bool {
        if (wide) {
            auto text = utf16(data + start, end - start, encoding == 2);
            if (!text) return false;
            values.push_back(std::move(*text));
        }
        else if (encoding == 3) {
            if (!validUtf8(data + start, end - start)) return false;
            values.emplace_back(reinterpret_cast<const char*>(data + start), end - start);
        }
        else {
            values.push_back(latin1(data + start, end - start));
        }
        return true;
    };
    bool endsWithTerminator = false;
    while (index + unit <= size) {
        const bool zero = data[index] == 0 && (!wide || data[index + 1] == 0);
        if (zero) {
            if (!flush(index)) return std::nullopt;
            start = index + unit;
            endsWithTerminator = start == size;
        }
        index += unit;
    }
    if (start < size) {
        if (wide && (size - start) % 2 != 0) return std::nullopt;
        if (!flush(size)) return std::nullopt;
    }
    if (terminated) *terminated = endsWithTerminator;
    return values;
}

std::string imageMime(const Bytes& data, std::size_t offset)
{
    const auto remaining = data.size() - offset;
    const auto* p = data.data() + offset;
    if (remaining >= 3 && p[0] == 0xff && p[1] == 0xd8 && p[2] == 0xff) return "image/jpeg";
    if (remaining >= 8 && std::equal(p, p + 8, std::array<std::uint8_t, 8>{ 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a }.begin())) {
        return "image/png";
    }
    if (remaining >= 6 && (std::string_view(reinterpret_cast<const char*>(p), 6) == "GIF87a" ||
            std::string_view(reinterpret_cast<const char*>(p), 6) == "GIF89a")) {
        return "image/gif";
    }
    if (remaining >= 12 && std::string_view(reinterpret_cast<const char*>(p), 4) == "RIFF" &&
        std::string_view(reinterpret_cast<const char*>(p + 8), 4) == "WEBP") {
        return "image/webp";
    }
    if (remaining >= 14 && p[0] == 'B' && p[1] == 'M') return "image/bmp";
    return {};
}

struct ImageDescriptor
{
    std::uint32_t width{ 0 };
    std::uint32_t height{ 0 };
    std::uint32_t depth{ 0 };
    std::uint32_t colors{ 0 };
};

// Descriptor values follow libFLAC/metaflac conventions so repaired blocks match what reference tools write.
std::optional<ImageDescriptor> imageDescriptor(const Bytes& data, std::size_t offset, const std::string& mime)
{
    const auto size = data.size() - offset;
    const auto* p = data.data() + offset;
    const auto be16 = [p](std::size_t at) { return static_cast<std::uint32_t>((p[at] << 8) | p[at + 1]); };
    const auto be32At = [&data, offset](std::size_t at) { return be32(data, offset + at); };
    ImageDescriptor result;
    if (mime == "image/png") {
        if (size < 33 || std::string_view(reinterpret_cast<const char*>(p + 12), 4) != "IHDR") return std::nullopt;
        result.width = be32At(16);
        result.height = be32At(20);
        const std::uint32_t bits = p[24];
        switch (p[25]) {
        case 0: result.depth = bits; break;
        case 2: result.depth = bits * 3; break;
        case 3: result.depth = 24; break;
        case 4: result.depth = bits * 2; break;
        case 6: result.depth = bits * 4; break;
        default: return std::nullopt;
        }
        if (p[25] == 3) {
            std::size_t chunk = 8;
            while (true) {
                if (size - chunk < 12) return std::nullopt;
                const auto length = be32At(chunk);
                const std::string_view type(reinterpret_cast<const char*>(p + chunk + 4), 4);
                if (type == "PLTE") {
                    if (length == 0 || length % 3 != 0) return std::nullopt;
                    result.colors = length / 3;
                    break;
                }
                if (type == "IDAT" || type == "IEND" || length > size - chunk - 12) return std::nullopt;
                chunk += 12 + length;
            }
        }
    }
    else if (mime == "image/jpeg") {
        std::size_t at = 2;
        while (true) {
            if (at >= size || p[at] != 0xff) return std::nullopt;
            while (at < size && p[at] == 0xff) ++at;
            if (at >= size) return std::nullopt;
            const auto marker = p[at++];
            if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
            if (marker == 0xd9 || marker == 0xda || size - at < 2) return std::nullopt;
            const auto length = be16(at);
            if (length < 2 || length > size - at) return std::nullopt;
            if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
                if (length < 8) return std::nullopt;
                result.height = be16(at + 3);
                result.width = be16(at + 5);
                result.depth = static_cast<std::uint32_t>(p[at + 2]) * p[at + 7];
                if (result.height == 0) return std::nullopt;
                break;
            }
            at += length;
        }
    }
    else if (mime == "image/gif") {
        if (size < 13) return std::nullopt;
        result.width = static_cast<std::uint32_t>(p[6] | (p[7] << 8));
        result.height = static_cast<std::uint32_t>(p[8] | (p[9] << 8));
        result.depth = (((p[10] & 0x70U) >> 4) + 1) * 3;
        result.colors = (p[10] & 0x80U) ? 1U << ((p[10] & 0x07U) + 1) : 0;
    }
    else {
        return std::nullopt;
    }
    if (result.width == 0 || result.height == 0 || result.depth == 0) return std::nullopt;
    return result;
}

bool mimeMatches(const std::string& declared, const std::string& detected)
{
    const auto value = lower(declared);
    return value == detected || (detected == "image/jpeg" && value == "image/jpg");
}

// ---------- Generic tag values used to decide whether one tag only repeats another ----------

struct TagValue
{
    std::string key;
    std::string value;
    bool truncated{ false };
};

struct TagValues
{
    std::vector<TagValue> values;
    std::set<std::string> unmapped;
    std::vector<Bytes> images;
    bool anyGenre{ false };
};

std::optional<long> leadingNumber(const std::string& value)
{
    std::size_t index = 0;
    long result = 0;
    while (index < value.size() && value[index] >= '0' && value[index] <= '9' && index < 9) {
        result = result * 10 + (value[index] - '0');
        ++index;
    }
    return index == 0 ? std::nullopt : std::optional<long>(result);
}

bool valueMatches(const TagValue& candidate, const std::string& target)
{
    const auto left = trim(candidate.value);
    const auto right = trim(target);
    if (candidate.key == "track" || candidate.key == "disc") {
        const auto a = leadingNumber(left);
        const auto b = leadingNumber(right);
        return a && b && *a == *b;
    }
    if (candidate.key == "date" && left.size() == 4) return right.rfind(left, 0) == 0;
    if (candidate.key == "genre") return lower(left) == lower(right);
    if (left == right) return true;
    return candidate.truncated && !left.empty() && right.rfind(left, 0) == 0;
}

std::optional<long> totalNumber(const std::string& value)
{
    const auto slash = value.find('/');
    return slash == std::string::npos ? std::nullopt : leadingNumber(trim(value.substr(slash + 1)));
}

// Returns an empty string when every candidate value already exists in the target, otherwise the first difference.
std::string firstDifference(const TagValues& candidate, const TagValues& target)
{
    if (!candidate.unmapped.empty()) return "fields without an equivalent: " + joined(candidate.unmapped);
    for (const auto& image : candidate.images) {
        if (std::find(target.images.begin(), target.images.end(), image) == target.images.end()) {
            return "embedded picture is not present in the target tag";
        }
    }
    const auto hasKey = [&](const std::string& key) {
        return std::any_of(target.values.begin(), target.values.end(),
            [&](const TagValue& item) { return item.key == key && !trim(item.value).empty(); });
    };
    if (candidate.anyGenre && !hasKey("genre")) return "genre is not present in the target tag";
    for (const auto& value : candidate.values) {
        if (trim(value.value).empty()) continue;
        bool found = std::any_of(target.values.begin(), target.values.end(),
            [&](const TagValue& item) { return item.key == value.key && valueMatches(value, item.value); });
        // "2/12" also carries the total; it must survive as "n/12" or a TRACKTOTAL/DISCTOTAL field.
        const auto total = (value.key == "track" || value.key == "disc") ? totalNumber(value.value) : std::nullopt;
        if (found && total) {
            found = std::any_of(target.values.begin(), target.values.end(), [&](const TagValue& item) {
                return (item.key == value.key && totalNumber(item.value) == total) ||
                    (item.key == value.key + "total" && leadingNumber(trim(item.value)) == total);
            });
        }
        if (!found) return value.key + " differs or is missing";
    }
    return {};
}

std::string canonicalVorbisKey(const std::string& key)
{
    static const std::map<std::string, std::string> keys{
        { "title", "title" }, { "artist", "artist" }, { "album", "album" }, { "albumartist", "albumartist" },
        { "album artist", "albumartist" }, { "tracknumber", "track" }, { "discnumber", "disc" },
        { "tracktotal", "tracktotal" }, { "totaltracks", "tracktotal" }, { "disctotal", "disctotal" },
        { "totaldiscs", "disctotal" },
        { "genre", "genre" }, { "date", "date" }, { "year", "date" }, { "comment", "comment" },
        { "description", "comment" }, { "composer", "composer" } };
    const auto found = keys.find(lower(key));
    return found == keys.end() ? std::string{} : found->second;
}

// ---------- ID3v2 ----------

struct Id3Frame
{
    std::string id;
    std::uint16_t flags{ 0 };
    Bytes data;
};

struct Id3Tag
{
    std::uint8_t major{ 0 };
    bool parsed{ false };
    std::size_t bodySize{ 0 };
    std::vector<Id3Frame> frames;
};

Id3Tag parseId3(const Bytes& bytes)
{
    Id3Tag tag;
    if (bytes.size() < 10) return tag;
    tag.major = bytes[3];
    const std::size_t end = 10 + synchsafe(bytes, 6);
    if ((tag.major != 3 && tag.major != 4) || bytes[5] != 0 || end != bytes.size()) return tag;
    tag.bodySize = end - 10;
    std::size_t offset = 10;
    while (offset + 10 <= end && bytes[offset] != 0) {
        std::string id(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + 4));
        if (!std::all_of(id.begin(), id.end(), [](char ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'); })) {
            return tag;
        }
        if (tag.major == 4 && ((bytes[offset + 4] | bytes[offset + 5] | bytes[offset + 6] | bytes[offset + 7]) & 0x80)) {
            return tag;
        }
        const std::size_t size = tag.major == 4 ? synchsafe(bytes, offset + 4) : be32(bytes, offset + 4);
        if (size > end - offset - 10) return tag;
        const auto flags = static_cast<std::uint16_t>((bytes[offset + 8] << 8) | bytes[offset + 9]);
        tag.frames.push_back({ std::move(id), flags,
            Bytes(bytes.begin() + static_cast<std::ptrdiff_t>(offset + 10),
                bytes.begin() + static_cast<std::ptrdiff_t>(offset + 10 + size)) });
        offset += 10 + size;
    }
    if (std::any_of(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end(),
            [](std::uint8_t value) { return value != 0; })) {
        return tag;
    }
    tag.parsed = true;
    return tag;
}

Bytes serializeId3(const Id3Tag& tag)
{
    Bytes body;
    for (const auto& frame : tag.frames) {
        body.insert(body.end(), frame.id.begin(), frame.id.end());
        if (frame.data.size() > UINT32_MAX) throw std::runtime_error("ID3v2 frame is too large.");
        if (tag.major == 4) appendSynchsafe(body, static_cast<std::uint32_t>(frame.data.size()));
        else appendBe32(body, static_cast<std::uint32_t>(frame.data.size()));
        body.push_back(static_cast<std::uint8_t>(frame.flags >> 8));
        body.push_back(static_cast<std::uint8_t>(frame.flags));
        body.insert(body.end(), frame.data.begin(), frame.data.end());
    }
    // Keep the original tag size when the frames shrink so the audio does not move.
    if (body.size() < tag.bodySize) body.resize(tag.bodySize, 0);
    Bytes result{ 'I', 'D', '3', tag.major, 0, 0 };
    appendSynchsafe(result, static_cast<std::uint32_t>(body.size()));
    result.insert(result.end(), body.begin(), body.end());
    return result;
}

bool plainFrame(const Id3Tag& tag, const Id3Frame& frame)
{
    return (frame.flags & (tag.major == 3 ? 0x00e0 : 0x004f)) == 0;
}

bool textFrame(const Id3Frame& frame)
{
    return frame.id[0] == 'T';
}

struct ApicLayout
{
    std::size_t mimeEnd{ 0 };
    std::optional<std::size_t> image;
};

// Locates the MIME terminator and image bytes of an ID3v2.3/2.4 APIC frame body.
std::optional<ApicLayout> apicLayout(const Bytes& data)
{
    if (data.empty() || data[0] > 3) return std::nullopt;
    const auto end = std::find(data.begin() + 1, data.end(), 0);
    if (end == data.end() || data.end() - end < 2) return std::nullopt;
    ApicLayout layout{ static_cast<std::size_t>(end - data.begin()), std::nullopt };
    const bool wide = data[0] == 1 || data[0] == 2;
    for (auto offset = layout.mimeEnd + 2; offset + (wide ? 1 : 0) < data.size(); offset += wide ? 2 : 1) {
        if (data[offset] == 0 && (!wide || data[offset + 1] == 0)) {
            layout.image = offset + (wide ? 2 : 1);
            break;
        }
    }
    return layout;
}

TagValues id3Values(const Id3Tag& tag)
{
    static const std::map<std::string, std::string> keys{
        { "TIT2", "title" }, { "TPE1", "artist" }, { "TALB", "album" }, { "TPE2", "albumartist" },
        { "TRCK", "track" }, { "TPOS", "disc" }, { "TCON", "genre" }, { "TYER", "date" }, { "TDRC", "date" },
        { "TCOM", "composer" } };
    TagValues result;
    for (const auto& frame : tag.frames) {
        if (frame.id == "APIC") {
            const auto layout = plainFrame(tag, frame) ? apicLayout(frame.data) : std::nullopt;
            if (!layout || !layout->image) result.unmapped.insert(frame.id);
            else result.images.emplace_back(frame.data.begin() + static_cast<std::ptrdiff_t>(*layout->image),
                frame.data.end());
            continue;
        }
        const auto mapped = keys.find(frame.id);
        if (!plainFrame(tag, frame) || frame.data.empty() || (mapped == keys.end() && frame.id != "COMM")) {
            result.unmapped.insert(frame.id);
            continue;
        }
        if (frame.id == "COMM") {
            if (frame.data.size() < 4) {
                result.unmapped.insert(frame.id);
                continue;
            }
            const auto strings = id3Strings(frame.data[0], frame.data.data() + 4, frame.data.size() - 4);
            if (!strings || strings->size() < 2 || !(*strings)[0].empty()) {
                result.unmapped.insert(frame.id);
                continue;
            }
            result.values.push_back({ "comment", (*strings)[1] });
            continue;
        }
        const auto strings = id3Strings(frame.data[0], frame.data.data() + 1, frame.data.size() - 1);
        if (!strings) {
            result.unmapped.insert(frame.id);
            continue;
        }
        for (const auto& value : *strings) result.values.push_back({ mapped->second, value });
    }
    return result;
}

// ---------- Trailing tags (ID3v1, APEv2, Lyrics3v2) ----------

struct Segment
{
    std::string kind;
    Bytes bytes;
};

std::vector<Segment> readLeadingId3(Reader& reader, std::uint64_t& offset)
{
    std::vector<Segment> tags;
    while (tags.size() < 16 && reader.Matches(offset, "ID3")) {
        const auto header = reader.Read(offset, 10);
        if ((header[6] | header[7] | header[8] | header[9]) & 0x80) throw std::runtime_error("Invalid ID3v2 tag size.");
        const std::uint64_t size = 10 + synchsafe(header, 6) + ((header[3] == 4 && (header[5] & 0x10)) ? 10 : 0);
        tags.push_back({ "id3v2", reader.Read(offset, size) });
        offset += size;
    }
    return tags;
}

std::vector<Segment> readTrailing(Reader& reader, std::uint64_t start, std::uint64_t& end)
{
    std::vector<Segment> segments;
    for (int guard = 0; guard < 8; ++guard) {
        const auto available = end - start;
        if (available >= 128 && reader.Matches(end - 128, "TAG")) {
            segments.push_back({ "id3v1", reader.Read(end - 128, 128) });
            end -= 128;
            continue;
        }
        if (available >= 32 && reader.Matches(end - 32, "APETAGEX")) {
            const auto footer = reader.Read(end - 32, 32);
            const std::uint64_t size = le32(footer, 12);
            const std::uint64_t total = size + ((le32(footer, 20) & 0x80000000U) ? 32 : 0);
            // A corrupt footer must not make the scan load most of the file as a "tag".
            if (size < 32 || size > (64U << 20) || total > available) break;
            segments.push_back({ "apev2", reader.Read(end - total, total) });
            end -= total;
            continue;
        }
        if (available >= 15 && reader.Matches(end - 9, "LYRICS200")) {
            const auto digits = reader.Read(end - 15, 6);
            if (!std::all_of(digits.begin(), digits.end(), [](std::uint8_t ch) { return ch >= '0' && ch <= '9'; })) break;
            const std::uint64_t total = std::stoul(std::string(digits.begin(), digits.end())) + 15;
            if (total > available || !reader.Matches(end - total, "LYRICSBEGIN")) break;
            segments.push_back({ "lyrics3", reader.Read(end - total, total) });
            end -= total;
            continue;
        }
        break;
    }
    std::reverse(segments.begin(), segments.end());
    return segments;
}

TagValues id3v1Values(const Bytes& tag)
{
    TagValues result;
    const auto field = [&](const char* key, std::size_t offset, std::size_t size) {
        const auto begin = tag.begin() + static_cast<std::ptrdiff_t>(offset);
        const auto stop = std::find(begin, begin + static_cast<std::ptrdiff_t>(size), 0);
        const bool full = stop == begin + static_cast<std::ptrdiff_t>(size);
        auto value = trim(latin1(&*begin, static_cast<std::size_t>(stop - begin)));
        if (!value.empty()) result.values.push_back({ key, std::move(value), full });
    };
    field("title", 3, 30);
    field("artist", 33, 30);
    field("album", 63, 30);
    field("date", 93, 4);
    if (tag[125] == 0 && tag[126] != 0) {
        field("comment", 97, 28);
        result.values.push_back({ "track", std::to_string(tag[126]) });
    }
    else {
        field("comment", 97, 30);
    }
    result.anyGenre = tag[127] != 0xff;
    return result;
}

std::optional<TagValues> apeValues(const Bytes& tag)
{
    static const std::map<std::string, std::string> keys{
        { "title", "title" }, { "artist", "artist" }, { "album", "album" }, { "year", "date" },
        { "track", "track" }, { "genre", "genre" }, { "comment", "comment" }, { "disc", "disc" },
        { "album artist", "albumartist" }, { "albumartist", "albumartist" }, { "composer", "composer" } };
    const std::size_t footer = tag.size() - 32;
    const std::size_t size = le32(tag, footer + 12);
    const std::size_t count = le32(tag, footer + 16);
    if (size > tag.size()) return std::nullopt;
    std::size_t offset = tag.size() - size;
    TagValues result;
    for (std::size_t item = 0; item < count; ++item) {
        if (footer - offset < 9) return std::nullopt;
        const std::size_t valueSize = le32(tag, offset);
        const auto flags = le32(tag, offset + 4);
        offset += 8;
        const auto keyEnd = std::find(tag.begin() + static_cast<std::ptrdiff_t>(offset),
            tag.begin() + static_cast<std::ptrdiff_t>(footer), 0);
        if (keyEnd == tag.begin() + static_cast<std::ptrdiff_t>(footer)) return std::nullopt;
        const std::string key(tag.begin() + static_cast<std::ptrdiff_t>(offset), keyEnd);
        offset = static_cast<std::size_t>(keyEnd - tag.begin()) + 1;
        if (valueSize > footer - offset) return std::nullopt;
        const auto mapped = keys.find(lower(key));
        if (mapped == keys.end() || ((flags >> 1) & 3) != 0 || !validUtf8(tag.data() + offset, valueSize)) {
            result.unmapped.insert(printable(key));
        }
        else {
            std::string text(reinterpret_cast<const char*>(tag.data() + offset), valueSize);
            std::size_t start = 0;
            for (std::size_t index = 0; index <= text.size(); ++index) {
                if (index == text.size() || text[index] == '\0') {
                    result.values.push_back({ mapped->second, text.substr(start, index - start) });
                    start = index + 1;
                }
            }
        }
        offset += valueSize;
    }
    return result;
}

// ---------- FLAC ----------

struct FlacBlock
{
    std::uint8_t type{ 0 };
    Bytes data;
};

struct CommentBlock
{
    Bytes vendor;
    std::vector<Bytes> entries;
};

std::optional<CommentBlock> parseComments(const Bytes& data)
{
    CommentBlock block;
    std::size_t offset = 0;
    const auto sized = [&](Bytes& value) {
        if (data.size() - offset < 4) return false;
        const std::size_t size = le32(data, offset);
        offset += 4;
        if (size > data.size() - offset) return false;
        value.assign(data.begin() + static_cast<std::ptrdiff_t>(offset),
            data.begin() + static_cast<std::ptrdiff_t>(offset + size));
        offset += size;
        return true;
    };
    if (!sized(block.vendor) || data.size() - offset < 4) return std::nullopt;
    const std::size_t count = le32(data, offset);
    offset += 4;
    if (count > (data.size() - offset) / 4) return std::nullopt;
    block.entries.resize(count);
    for (auto& entry : block.entries) {
        if (!sized(entry)) return std::nullopt;
    }
    if (offset != data.size()) return std::nullopt;
    return block;
}

Bytes serializeComments(const CommentBlock& block)
{
    Bytes result;
    appendLe32(result, static_cast<std::uint32_t>(block.vendor.size()));
    result.insert(result.end(), block.vendor.begin(), block.vendor.end());
    appendLe32(result, static_cast<std::uint32_t>(block.entries.size()));
    for (const auto& entry : block.entries) {
        appendLe32(result, static_cast<std::uint32_t>(entry.size()));
        result.insert(result.end(), entry.begin(), entry.end());
    }
    if (result.size() > 0x00ffffffU) throw std::runtime_error("Rewritten Vorbis comment block is too large.");
    return result;
}

std::optional<std::string> commentKey(const Bytes& entry)
{
    const auto separator = std::find(entry.begin(), entry.end(), static_cast<std::uint8_t>('='));
    if (separator == entry.end()) return std::nullopt;
    return std::string(entry.begin(), separator);
}

Bytes commentValue(const Bytes& entry)
{
    const auto separator = std::find(entry.begin(), entry.end(), static_cast<std::uint8_t>('='));
    return separator == entry.end() ? Bytes{} : Bytes(separator + 1, entry.end());
}

std::string normalizedComment(const Bytes& entry)
{
    const auto key = commentKey(entry);
    const auto value = commentValue(entry);
    return lower(key.value_or("")) + '=' + std::string(value.begin(), value.end());
}

bool validCommentKey(const std::string& key)
{
    return !key.empty() && std::all_of(key.begin(), key.end(),
        [](unsigned char ch) { return ch >= 0x20 && ch <= 0x7d && ch != '='; });
}

struct Picture
{
    std::string mime;
    std::size_t mimeEnd{ 0 };
    std::size_t descriptorOffset{ 0 };
    std::size_t dataOffset{ 0 };
};

std::optional<Picture> parsePicture(const Bytes& data)
{
    if (data.size() < 8) return std::nullopt;
    const std::size_t mimeSize = be32(data, 4);
    if (mimeSize > data.size() - 8) return std::nullopt;
    Picture picture;
    picture.mime.assign(data.begin() + 8, data.begin() + 8 + static_cast<std::ptrdiff_t>(mimeSize));
    picture.mimeEnd = 8 + mimeSize;
    if (data.size() - picture.mimeEnd < 4) return std::nullopt;
    const std::size_t descriptionSize = be32(data, picture.mimeEnd);
    const std::size_t fields = picture.mimeEnd + 4;
    if (descriptionSize > data.size() - fields || data.size() - fields - descriptionSize < 20) return std::nullopt;
    const std::size_t lengthOffset = fields + descriptionSize + 16;
    const std::size_t length = be32(data, lengthOffset);
    picture.descriptorOffset = fields + descriptionSize;
    picture.dataOffset = lengthOffset + 4;
    if (length != data.size() - picture.dataOffset) return std::nullopt;
    return picture;
}

bool validSeekTable(const Bytes& data, std::uint64_t totalSamples)
{
    if (data.size() % 18 != 0) return false;
    bool placeholder = false;
    std::optional<std::uint64_t> previous;
    for (std::size_t offset = 0; offset < data.size(); offset += 18) {
        const std::uint64_t sample = (static_cast<std::uint64_t>(be32(data, offset)) << 32) | be32(data, offset + 4);
        if (sample == UINT64_MAX) {
            placeholder = true;
            continue;
        }
        if (placeholder || (previous && sample <= *previous) || (totalSamples != 0 && sample >= totalSamples)) {
            return false;
        }
        previous = sample;
    }
    return true;
}

class Context
{
public:
    explicit Context(const std::set<std::string>& enabled) : _enabled(enabled) {}

    bool On(const char* rule) const { return _enabled.empty() || _enabled.contains(rule); }

    void Add(const char* rule, bool fix, std::size_t count, std::string detail)
    {
        if (count == 0) return;
        findings.push_back({ rule, fix, count, std::move(detail) });
        changed = changed || fix;
    }

    std::vector<Finding> findings;
    bool changed{ false };

private:
    const std::set<std::string>& _enabled;
};

Analysis analyzeFlac(Reader& reader, const std::set<std::string>& enabled)
{
    Context context(enabled);
    std::uint64_t offset = 0;
    const auto leading = readLeadingId3(reader, offset);
    if (!reader.Matches(offset, "fLaC")) throw std::runtime_error("Not a native FLAC stream.");
    offset += 4;

    std::vector<FlacBlock> blocks;
    for (bool last = false; !last;) {
        const auto header = reader.Read(offset, 4);
        offset += 4;
        const std::uint8_t type = header[0] & 0x7f;
        const std::uint32_t size = (static_cast<std::uint32_t>(header[1]) << 16) | (header[2] << 8) | header[3];
        if (type == 127 || (blocks.empty() && (type != 0 || size != 34)) || (type == 0 && !blocks.empty())) {
            throw std::runtime_error("Invalid FLAC metadata block sequence.");
        }
        blocks.push_back({ type, reader.Read(offset, size) });
        offset += size;
        last = (header[0] & 0x80) != 0;
    }
    const std::uint64_t audioOffset = offset;
    std::uint64_t audioEnd = reader.size;
    const auto trailing = readTrailing(reader, audioOffset, audioEnd);

    std::set<std::size_t> removed, modified;
    std::map<std::size_t, CommentBlock> comments;
    std::vector<std::size_t> commentIndexes;
    std::size_t malformed = 0;
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (blocks[index].type != 4) continue;
        commentIndexes.push_back(index);
        if (auto parsed = parseComments(blocks[index].data)) comments.emplace(index, std::move(*parsed));
        else ++malformed;
    }
    if (context.On("flac.invalid-comments")) {
        context.Add("flac.invalid-comments", false, malformed,
            "Vorbis comment block is malformed; comment rules skipped it and it is unchanged.");
    }

    std::size_t duplicates = 0, empties = 0, invalid = 0;
    std::set<std::string> duplicateKeys, emptyKeys;
    for (auto& [index, block] : comments) {
        if (removed.contains(index)) continue;
        bool cdtocReview = false;
        if (context.On("flac.duplicate-comments")) {
            std::optional<Bytes> cdtoc;
            for (const auto& entry : block.entries) {
                const auto key = commentKey(entry);
                if (!key || lower(*key) != "cdtoc") continue;
                const auto value = commentValue(entry);
                if (std::find(value.begin(), value.end(), static_cast<std::uint8_t>(';')) != value.end()) {
                    cdtocReview = true;
                    context.Add("flac.duplicate-comments", false, 1,
                        "CDTOC contains a semicolon-joined value; its original occurrences cannot be inferred safely.");
                    break;
                }
                if (cdtoc && *cdtoc != value) {
                    cdtocReview = true;
                    context.Add("flac.duplicate-comments", false, 1,
                        "CDTOC comments contain different values; no value was selected or changed.");
                    break;
                }
                cdtoc = value;
            }
        }
        std::set<std::string> seen;
        std::vector<Bytes> kept;
        for (const auto& entry : block.entries) {
            const auto key = commentKey(entry);
            const auto value = commentValue(entry);
            if (!key || !validCommentKey(*key) || !validUtf8(value.data(), value.size())) ++invalid;
            if (key && !key->empty() && value.empty() && context.On("flac.empty-comments")) {
                ++empties;
                emptyKeys.insert(upper(printable(*key)));
                continue;
            }
            if (key && context.On("flac.duplicate-comments") && !(cdtocReview && lower(*key) == "cdtoc") &&
                !seen.insert(normalizedComment(entry)).second) {
                ++duplicates;
                duplicateKeys.insert(upper(printable(*key)));
                continue;
            }
            kept.push_back(entry);
        }
        if (kept.size() != block.entries.size()) {
            block.entries = std::move(kept);
            blocks[index].data = serializeComments(block);
            modified.insert(index);
        }
    }
    context.Add("flac.duplicate-comments", true, duplicates,
        "Removed exact duplicate comment(s), retaining the first occurrence: " + joined(duplicateKeys) + ".");
    context.Add("flac.empty-comments", true, empties, "Removed comment(s) with empty values: " + joined(emptyKeys) + ".");
    if (context.On("flac.invalid-comments")) {
        context.Add("flac.invalid-comments", false, invalid,
            "Comment(s) lack '=', use invalid field-name characters or hold non-UTF-8 text; unchanged.");
    }

    // Compare blocks after cleaning so re-analysing the repaired output reaches the same decision.
    if (context.On("flac.multiple-comment-blocks") && commentIndexes.size() > 1) {
        const auto first = comments.find(commentIndexes.front());
        std::size_t extra = 0;
        if (first != comments.end()) {
            std::set<std::string> known;
            for (const auto& entry : first->second.entries) known.insert(normalizedComment(entry));
            for (std::size_t item = 1; item < commentIndexes.size(); ++item) {
                const auto block = comments.find(commentIndexes[item]);
                if (block != comments.end() && std::all_of(block->second.entries.begin(), block->second.entries.end(),
                        [&](const Bytes& entry) { return known.contains(normalizedComment(entry)); })) {
                    removed.insert(commentIndexes[item]);
                }
                else {
                    ++extra;
                }
            }
        }
        else {
            extra = commentIndexes.size() - 1;
        }
        context.Add("flac.multiple-comment-blocks", true, removed.size(),
            "Removed extra Vorbis comment block(s) whose comments all repeat the first block.");
        context.Add("flac.multiple-comment-blocks", false, extra,
            "Extra Vorbis comment block(s) hold values not in the first block (most readers ignore them); unchanged.");
    }

    std::size_t pictureFixes = 0, badPictures = 0;
    std::set<std::string> pictureChanges;
    for (auto& block : blocks) {
        if (block.type != 6 || !context.On("flac.picture-descriptor")) continue;
        const auto picture = parsePicture(block.data);
        if (!picture) {
            ++badPictures;
            continue;
        }
        if (picture->mime == "-->") continue;
        const auto detected = imageMime(block.data, picture->dataOffset);
        if (detected.empty()) {
            ++badPictures;
            continue;
        }
        bool changed = false;
        if (const auto actual = imageDescriptor(block.data, picture->dataOffset, detected)) {
            static constexpr std::array<const char*, 4> names{ "width", "height", "depth", "colors" };
            const std::array<std::uint32_t, 4> values{ actual->width, actual->height, actual->depth, actual->colors };
            for (std::size_t field = 0; field < values.size(); ++field) {
                const auto at = picture->descriptorOffset + field * 4;
                const auto declared = be32(block.data, at);
                if (declared == values[field]) continue;
                for (int shift = 24, byte = 0; shift >= 0; shift -= 8, ++byte) {
                    block.data[at + static_cast<std::size_t>(byte)] = static_cast<std::uint8_t>(values[field] >> shift);
                }
                pictureChanges.insert(std::string(names[field]) + " " + std::to_string(declared) + " -> " +
                    std::to_string(values[field]));
                changed = true;
            }
        }
        if (!mimeMatches(picture->mime, detected)) {
            Bytes data(block.data.begin(), block.data.begin() + 4);
            appendBe32(data, static_cast<std::uint32_t>(detected.size()));
            data.insert(data.end(), detected.begin(), detected.end());
            data.insert(data.end(), block.data.begin() + static_cast<std::ptrdiff_t>(picture->mimeEnd), block.data.end());
            block.data = std::move(data);
            pictureChanges.insert("MIME '" + printable(picture->mime) + "' -> " + detected);
            changed = true;
        }
        if (changed) ++pictureFixes;
    }
    context.Add("flac.picture-descriptor", true, pictureFixes,
        "Corrected PICTURE descriptor fields to match the image bytes (image data unchanged): " +
            joined(pictureChanges) + ".");
    context.Add("flac.picture-descriptor", false, badPictures,
        "PICTURE block is malformed, empty or not a recognized image format; unchanged.");

    const auto& info = blocks.front().data;
    const std::uint64_t totalSamples = (static_cast<std::uint64_t>(info[13] & 0x0f) << 32) | be32(info, 14);
    std::size_t seekTables = 0;
    for (auto& block : blocks) {
        if (block.type == 3 && context.On("flac.invalid-seektable") && !validSeekTable(block.data, totalSamples)) {
            block.type = 1;
            std::fill(block.data.begin(), block.data.end(), 0);
            ++seekTables;
        }
    }
    context.Add("flac.invalid-seektable", true, seekTables,
        "Replaced an invalid SEEKTABLE with same-size padding; players seek without it.");
    if (context.On("flac.missing-md5") && std::all_of(info.begin() + 18, info.end(), [](std::uint8_t v) { return v == 0; })) {
        context.Add("flac.missing-md5", false, 1,
            "STREAMINFO MD5 signature is unset, so decoded audio cannot be integrity-checked; unchanged.");
    }

    TagValues vorbis;
    if (!commentIndexes.empty()) {
        const auto first = comments.find(commentIndexes.front());
        if (first != comments.end()) {
            for (const auto& entry : first->second.entries) {
                const auto key = commentKey(entry);
                if (!key) continue;
                const auto canonical = canonicalVorbisKey(*key);
                const auto value = commentValue(entry);
                if (!canonical.empty()) vorbis.values.push_back({ canonical, std::string(value.begin(), value.end()) });
            }
        }
    }
    for (const auto& block : blocks) {
        if (block.type != 6) continue;
        if (const auto picture = parsePicture(block.data)) {
            vorbis.images.emplace_back(block.data.begin() + static_cast<std::ptrdiff_t>(picture->dataOffset),
                block.data.end());
        }
    }

    Bytes head;
    if (!leading.empty()) {
        std::string difference;
        if (context.On("flac.leading-id3")) {
            for (const auto& segment : leading) {
                const auto tag = parseId3(segment.bytes);
                difference = !tag.parsed ? "ID3v2 tag uses an unsupported version or layout" :
                    firstDifference(id3Values(tag), vorbis);
                if (!difference.empty()) break;
            }
        }
        if (context.On("flac.leading-id3") && difference.empty()) {
            context.Add("flac.leading-id3", true, leading.size(),
                "Removed ID3v2 data before the fLaC marker; all its fields are already in the Vorbis comments.");
        }
        else {
            if (context.On("flac.leading-id3")) {
                context.Add("flac.leading-id3", false, leading.size(),
                    "ID3v2 data precedes the fLaC marker (non-standard) but is not redundant (" + difference + "); unchanged.");
            }
            for (const auto& segment : leading) head.insert(head.end(), segment.bytes.begin(), segment.bytes.end());
        }
    }

    Bytes tail;
    std::size_t trailingFixed = 0, trailingReview = 0;
    std::set<std::string> trailingKinds, trailingReasons;
    for (const auto& segment : trailing) {
        std::string difference = "Lyrics3 tag holds lyrics that have no checked equivalent";
        if (segment.kind == "id3v1") difference = firstDifference(id3v1Values(segment.bytes), vorbis);
        else if (segment.kind == "apev2") {
            const auto values = apeValues(segment.bytes);
            difference = values ? firstDifference(*values, vorbis) : "APEv2 tag is malformed";
        }
        if (context.On("flac.trailing-tags") && difference.empty()) {
            ++trailingFixed;
            trailingKinds.insert(segment.kind);
            continue;
        }
        if (context.On("flac.trailing-tags")) {
            ++trailingReview;
            trailingReasons.insert(segment.kind + ": " + difference);
        }
        tail.insert(tail.end(), segment.bytes.begin(), segment.bytes.end());
    }
    context.Add("flac.trailing-tags", true, trailingFixed,
        "Removed non-FLAC tag(s) after the audio frames that repeat the Vorbis comments: " + joined(trailingKinds) + ".");
    context.Add("flac.trailing-tags", false, trailingReview,
        "Non-FLAC tag(s) after the audio frames are not redundant (" + joined(trailingReasons) + "); unchanged.");

    Analysis analysis{ std::move(context.findings), std::nullopt };
    if (!context.changed) return analysis;
    head.insert(head.end(), { 'f', 'L', 'a', 'C' });
    std::vector<const FlacBlock*> kept;
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (!removed.contains(index)) kept.push_back(&blocks[index]);
    }
    for (std::size_t index = 0; index < kept.size(); ++index) {
        if (kept[index]->data.size() > 0x00ffffffU) {
            throw std::runtime_error("Rewritten FLAC metadata block would exceed the 16 MiB block limit.");
        }
        const auto size = static_cast<std::uint32_t>(kept[index]->data.size());
        head.push_back(static_cast<std::uint8_t>(kept[index]->type | (index + 1 == kept.size() ? 0x80 : 0)));
        head.push_back(static_cast<std::uint8_t>(size >> 16));
        head.push_back(static_cast<std::uint8_t>(size >> 8));
        head.push_back(static_cast<std::uint8_t>(size));
        head.insert(head.end(), kept[index]->data.begin(), kept[index]->data.end());
    }
    analysis.edit = Edit{ std::move(head), audioOffset, audioEnd - audioOffset, std::move(tail) };
    return analysis;
}

// ---------- MP3 ----------

std::optional<std::size_t> mpegFrameLength(const std::uint8_t* p)
{
    if (p[0] != 0xff || (p[1] & 0xe0) != 0xe0) return std::nullopt;
    const int version = (p[1] >> 3) & 3;
    const int layer = (p[1] >> 1) & 3;
    const int bitrateIndex = p[2] >> 4;
    const int rateIndex = (p[2] >> 2) & 3;
    if (version == 1 || layer == 0 || bitrateIndex == 0 || bitrateIndex == 15 || rateIndex == 3) return std::nullopt;
    static constexpr int rates[3] = { 44100, 48000, 32000 };
    static constexpr int bitrates[5][15] = {
        { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 },
        { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384 },
        { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 },
        { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256 },
        { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 } };
    const bool mpeg1 = version == 3;
    const int table = mpeg1 ? 3 - layer : (layer == 3 ? 3 : 4);
    const long bitrate = bitrates[table][bitrateIndex] * 1000L;
    const long rate = rates[rateIndex] >> (version == 3 ? 0 : version == 2 ? 1 : 2);
    const int padding = (p[2] >> 1) & 1;
    if (layer == 3) return static_cast<std::size_t>((12 * bitrate / rate + padding) * 4);
    return static_cast<std::size_t>((layer == 1 && !mpeg1 ? 72 : 144) * bitrate / rate + padding);
}

std::optional<std::size_t> firstMpegFrame(const Bytes& data)
{
    for (std::size_t offset = 0; offset + 4 <= data.size(); ++offset) {
        const auto length = mpegFrameLength(data.data() + offset);
        if (!length) continue;
        const auto next = offset + *length;
        if (next == data.size() || (next + 4 <= data.size() && mpegFrameLength(data.data() + next))) return offset;
        if (next > data.size() && offset == 0) return offset;
    }
    return std::nullopt;
}

Analysis analyzeMp3(Reader& reader, const std::set<std::string>& enabled)
{
    Context context(enabled);
    std::uint64_t audioOffset = 0;
    const auto leading = readLeadingId3(reader, audioOffset);
    std::uint64_t audioEnd = reader.size;
    const auto trailing = readTrailing(reader, audioOffset, audioEnd);
    if (audioEnd <= audioOffset) throw std::runtime_error("MP3 file contains no audio data.");

    std::vector<Id3Tag> tags;
    for (const auto& segment : leading) tags.push_back(parseId3(segment.bytes));
    std::vector<bool> keepTag(tags.size(), true);
    bool firstModified = false;

    if (tags.size() > 1 && context.On("mp3.duplicate-id3v2")) {
        std::size_t fixed = 0, review = 0;
        for (std::size_t index = 1; index < tags.size(); ++index) {
            const bool subset = tags[0].parsed && tags[index].parsed &&
                std::all_of(tags[index].frames.begin(), tags[index].frames.end(), [&](const Id3Frame& frame) {
                    return std::any_of(tags[0].frames.begin(), tags[0].frames.end(), [&](const Id3Frame& known) {
                        return known.id == frame.id && known.data == frame.data;
                    });
                });
            if (subset) {
                keepTag[index] = false;
                ++fixed;
            }
            else {
                ++review;
            }
        }
        context.Add("mp3.duplicate-id3v2", true, fixed,
            "Removed additional ID3v2 tag(s) whose frames all repeat the first tag.");
        context.Add("mp3.duplicate-id3v2", false, review,
            "Additional ID3v2 tag(s) hold frames not in the first tag (most players read only the first); unchanged.");
    }

    if (!tags.empty() && tags[0].parsed) {
        auto& tag = tags[0];
        if (context.On("mp3.duplicate-frames")) {
            std::vector<Id3Frame> kept;
            std::set<std::string> ids;
            for (auto& frame : tag.frames) {
                if (std::any_of(kept.begin(), kept.end(), [&](const Id3Frame& known) {
                        return known.id == frame.id && known.flags == frame.flags && known.data == frame.data;
                    })) {
                    ids.insert(frame.id);
                    continue;
                }
                kept.push_back(std::move(frame));
            }
            const auto count = tag.frames.size() - kept.size();
            tag.frames = std::move(kept);
            firstModified = firstModified || count != 0;
            context.Add("mp3.duplicate-frames", true, count,
                "Removed byte-identical repeated ID3v2 frame(s): " + joined(ids) + ".");
        }
        if (context.On("mp3.empty-frames")) {
            std::set<std::string> ids;
            const auto before = tag.frames.size();
            std::erase_if(tag.frames, [&](const Id3Frame& frame) {
                const bool empty = textFrame(frame) && frame.id != "TXXX" && plainFrame(tag, frame) &&
                    std::all_of(frame.data.begin() + (frame.data.empty() ? 0 : 1), frame.data.end(),
                        [](std::uint8_t value) { return value == 0; });
                if (empty) ids.insert(frame.id);
                return empty;
            });
            firstModified = firstModified || before != tag.frames.size();
            context.Add("mp3.empty-frames", true, before - tag.frames.size(),
                "Removed ID3v2 text frame(s) without text: " + joined(ids) + ".");
        }
        if (context.On("mp3.text-encoding")) {
            std::size_t fixed = 0, review = 0;
            std::set<std::string> ids;
            for (auto& frame : tag.frames) {
                if (!textFrame(frame) || !plainFrame(tag, frame) || frame.data.empty()) continue;
                const auto encoding = frame.data[0];
                if (encoding <= 1 || (tag.major == 4 && encoding <= 3)) continue;
                bool terminated = false;
                const auto strings = encoding <= 3 ?
                    id3Strings(encoding, frame.data.data() + 1, frame.data.size() - 1, &terminated) : std::nullopt;
                if (!strings) {
                    ++review;
                    continue;
                }
                Bytes data{ 1 };
                for (std::size_t index = 0; index < strings->size(); ++index) {
                    if (index != 0) data.insert(data.end(), { 0, 0 });
                    const auto encoded = utf16LeWithBom((*strings)[index]);
                    data.insert(data.end(), encoded.begin(), encoded.end());
                }
                if (terminated) data.insert(data.end(), { 0, 0 });
                frame.data = std::move(data);
                ids.insert(frame.id);
                ++fixed;
            }
            firstModified = firstModified || fixed != 0;
            context.Add("mp3.text-encoding", true, fixed,
                "Re-encoded ID3v2.3 text frame(s) that used an ID3v2.4-only encoding as UTF-16 with BOM: " +
                    joined(ids) + ".");
            context.Add("mp3.text-encoding", false, review,
                "ID3v2 text frame(s) use an unknown encoding or invalid text; unchanged.");
        }
        if (context.On("mp3.picture-mime")) {
            std::size_t fixed = 0, review = 0;
            std::set<std::string> changes;
            for (auto& frame : tag.frames) {
                if (frame.id != "APIC" || !plainFrame(tag, frame)) continue;
                const auto& data = frame.data;
                const auto mimeEnd = data.empty() ? data.end() : std::find(data.begin() + 1, data.end(), 0);
                if (mimeEnd == data.end() || data.end() - mimeEnd < 2 || data[0] > 3) {
                    ++review;
                    continue;
                }
                const std::string mime(data.begin() + 1, mimeEnd);
                std::size_t offset = static_cast<std::size_t>(mimeEnd - data.begin()) + 2;
                const bool wide = data[0] == 1 || data[0] == 2;
                bool found = false;
                for (; offset + (wide ? 1 : 0) < data.size(); offset += wide ? 2 : 1) {
                    if (data[offset] == 0 && (!wide || data[offset + 1] == 0)) {
                        offset += wide ? 2 : 1;
                        found = true;
                        break;
                    }
                }
                if (mime == "-->") continue;
                const auto detected = found ? imageMime(data, offset) : std::string{};
                if (detected.empty()) {
                    ++review;
                }
                else if (!mimeMatches(mime, detected)) {
                    Bytes updated{ data[0] };
                    updated.insert(updated.end(), detected.begin(), detected.end());
                    updated.insert(updated.end(), mimeEnd, data.end());
                    frame.data = std::move(updated);
                    changes.insert("'" + printable(mime) + "' -> " + detected);
                    ++fixed;
                }
            }
            firstModified = firstModified || fixed != 0;
            context.Add("mp3.picture-mime", true, fixed,
                "Corrected APIC MIME type to match the image bytes: " + joined(changes) + ".");
            context.Add("mp3.picture-mime", false, review,
                "APIC picture is malformed, empty or not a recognized image format; unchanged.");
        }
    }

    const bool haveId3 = !tags.empty() && tags[0].parsed;
    const auto id3 = haveId3 ? id3Values(tags[0]) : TagValues{};
    Bytes tail;
    std::size_t v1Fixed = 0, v1Review = 0, apeFixed = 0, apeReview = 0;
    std::set<std::string> v1Reasons, apeReasons;
    for (const auto& segment : trailing) {
        const char* rule = segment.kind == "id3v1" ? "mp3.redundant-id3v1" : "mp3.ape-lyrics3";
        if (!context.On(rule) || (segment.kind == "id3v1" && tags.empty())) {
            tail.insert(tail.end(), segment.bytes.begin(), segment.bytes.end());
            continue;
        }
        std::string difference;
        if (!haveId3) difference = "no readable ID3v2 tag to compare with";
        else if (segment.kind == "id3v1") difference = firstDifference(id3v1Values(segment.bytes), id3);
        else if (segment.kind == "apev2") {
            const auto values = apeValues(segment.bytes);
            difference = values ? firstDifference(*values, id3) : "APEv2 tag is malformed";
        }
        else difference = "Lyrics3 tag holds lyrics that have no checked equivalent";
        const bool v1 = segment.kind == "id3v1";
        if (difference.empty()) {
            ++(v1 ? v1Fixed : apeFixed);
            continue;
        }
        ++(v1 ? v1Review : apeReview);
        (v1 ? v1Reasons : apeReasons).insert(segment.kind + ": " + difference);
        tail.insert(tail.end(), segment.bytes.begin(), segment.bytes.end());
    }
    context.Add("mp3.redundant-id3v1", true, v1Fixed,
        "Removed ID3v1 tag; every field is already in the ID3v2 tag.");
    context.Add("mp3.redundant-id3v1", false, v1Review,
        "ID3v1 tag is not redundant (" + joined(v1Reasons) + "); unchanged.");
    context.Add("mp3.ape-lyrics3", true, apeFixed,
        "Removed APEv2 tag; every field is already in the ID3v2 tag.");
    context.Add("mp3.ape-lyrics3", false, apeReview,
        "Non-ID3 tag in MP3 is not redundant (" + joined(apeReasons) + "); unchanged.");

    if (context.On("mp3.stream-garbage")) {
        const auto sample = reader.Read(audioOffset, std::min<std::uint64_t>(audioEnd - audioOffset, 64 * 1024));
        const auto first = firstMpegFrame(sample);
        if (!first) {
            context.Add("mp3.stream-garbage", false, 1, "No MPEG audio frame found in the first 64 KiB of audio data.");
        }
        else if (*first != 0) {
            context.Add("mp3.stream-garbage", false, *first,
                std::to_string(*first) + " non-audio byte(s) precede the first MPEG frame; unchanged.");
        }
    }

    Analysis analysis{ std::move(context.findings), std::nullopt };
    if (!context.changed) return analysis;
    Bytes head;
    for (std::size_t index = 0; index < leading.size(); ++index) {
        if (!keepTag[index]) continue;
        const auto bytes = index == 0 && firstModified ? serializeId3(tags[0]) : leading[index].bytes;
        head.insert(head.end(), bytes.begin(), bytes.end());
    }
    analysis.edit = Edit{ std::move(head), audioOffset, audioEnd - audioOffset, std::move(tail) };
    return analysis;
}

std::string extension(const fs::path& path)
{
    return lower(path.extension().string());
}

bool compareRange(std::ifstream& left, std::uint64_t leftOffset, std::ifstream& right, std::uint64_t rightOffset,
    std::uint64_t size)
{
    left.clear();
    right.clear();
    left.seekg(static_cast<std::streamoff>(leftOffset));
    right.seekg(static_cast<std::streamoff>(rightOffset));
    std::array<char, 64 * 1024> a{}, b{};
    while (size != 0) {
        const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(size, a.size()));
        if (!left.read(a.data(), chunk) || !right.read(b.data(), chunk) ||
            !std::equal(a.begin(), a.begin() + chunk, b.begin())) {
            return false;
        }
        size -= static_cast<std::uint64_t>(chunk);
    }
    return true;
}

bool compareBytes(std::ifstream& stream, std::uint64_t offset, const Bytes& expected)
{
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset));
    Bytes actual(expected.size());
    return (expected.empty() || stream.read(reinterpret_cast<char*>(actual.data()),
        static_cast<std::streamsize>(actual.size()))) && actual == expected;
}
}

const std::vector<RuleInfo>& Catalog()
{
    return kCatalog;
}

bool IsKnownRule(const std::string& id)
{
    return std::any_of(kCatalog.begin(), kCatalog.end(), [&](const RuleInfo& rule) { return rule.id == id; });
}

bool IsSupportedFile(const fs::path& path)
{
    const auto value = extension(path);
    return value == ".flac" || value == ".mp3";
}

namespace {
Analysis analyzeAs(const fs::path& path, const fs::path& formatPath, const std::set<std::string>& enabled)
{
    Reader reader(path);
    return extension(formatPath) == ".mp3" ? analyzeMp3(reader, enabled) : analyzeFlac(reader, enabled);
}
}

Analysis Analyze(const fs::path& path, const std::set<std::string>& enabled)
{
    return analyzeAs(path, path, enabled);
}

void Write(const fs::path& source, const fs::path& target, const Edit& edit)
{
    std::ifstream input(source, std::ios::binary);
    std::ofstream output(target, std::ios::binary | std::ios::trunc);
    if (!input || !output) throw std::runtime_error("Cannot open source or temporary repair output.");
    output.write(reinterpret_cast<const char*>(edit.head.data()), static_cast<std::streamsize>(edit.head.size()));
    input.seekg(static_cast<std::streamoff>(edit.audioOffset));
    std::array<char, 64 * 1024> buffer{};
    for (std::uint64_t remaining = edit.audioSize; remaining != 0;) {
        const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));
        if (!input.read(buffer.data(), chunk) || !output.write(buffer.data(), chunk)) {
            throw std::runtime_error("Cannot copy audio bytes into the repaired output.");
        }
        remaining -= static_cast<std::uint64_t>(chunk);
    }
    output.write(reinterpret_cast<const char*>(edit.tail.data()), static_cast<std::streamsize>(edit.tail.size()));
    output.flush();
    if (!output) throw std::runtime_error("Cannot write complete repaired output.");
}

bool Verify(const fs::path& source, const fs::path& output, const Edit& edit,
    const std::set<std::string>& enabled, std::string& error)
{
    try {
        std::error_code ec;
        const auto size = fs::file_size(output, ec);
        if (ec || size != edit.head.size() + edit.audioSize + edit.tail.size()) {
            error = "repaired file size differs from the planned edit";
            return false;
        }
        std::ifstream original(source, std::ios::binary);
        std::ifstream repaired(output, std::ios::binary);
        if (!original || !repaired) {
            error = "cannot reopen source or repaired output";
            return false;
        }
        if (!compareBytes(repaired, 0, edit.head) ||
            !compareBytes(repaired, edit.head.size() + edit.audioSize, edit.tail)) {
            error = "repaired metadata differs from the planned edit";
            return false;
        }
        if (!compareRange(original, edit.audioOffset, repaired, edit.head.size(), edit.audioSize)) {
            error = "audio bytes changed";
            return false;
        }
        repaired.close();
        // Temporary outputs do not carry the source extension; the source decides the format.
        const auto check = analyzeAs(output, source, enabled);
        const auto remaining = std::find_if(check.findings.begin(), check.findings.end(),
            [](const Finding& finding) { return finding.fix; });
        if (remaining != check.findings.end()) {
            error = "repaired output still needs " + remaining->rule;
            return false;
        }
        return true;
    }
    catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
}
