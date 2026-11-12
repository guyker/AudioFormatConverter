#include "FlacMetadata.h"
#include "PreservationAudit.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <algorithm>
#include <functional>
#include <vector>
#include <stdexcept>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    try {
        require(argc == 3, "Expected source FLAC and disposable scratch directory");
        const std::filesystem::path source = argv[1];
        const std::filesystem::path scratch = argv[2];
        const auto sidecar = scratch / "archive.flac-metadata";
        bool prepared = false;
        const auto binary = MetadataSidecarFormat::Binary;
        require(FlacMetadata::Save(source, sidecar, binary, "", scratch / "clean.flac", prepared), "Save failed");
        require(FlacMetadata::Verify(source, sidecar, binary), "Valid archive rejected");
        std::ifstream input(sidecar, std::ios::binary);
        std::string original((std::istreambuf_iterator<char>(input)), {});
        input.close();
        const auto write = [&](const std::string& data) {
            std::ofstream output(sidecar, std::ios::binary | std::ios::trunc);
            output.write(data.data(), static_cast<std::streamsize>(data.size()));
            output.close();
            require(static_cast<bool>(output), "Cannot write fixture");
        };
        auto changed = original;
        changed.back() ^= 1;
        write(changed);
        require(!FlacMetadata::Verify(source, sidecar, binary), "Changed archive accepted");
        write(original.substr(0, original.size() - 1));
        require(!FlacMetadata::Verify(source, sidecar, binary), "Truncated archive accepted");
        write(original + "extra");
        require(!FlacMetadata::Verify(source, sidecar, binary), "Extra archive bytes accepted");
        write(original);
        require(FlacMetadata::Verify(source, sidecar, binary), "Restored archive rejected");
        require(!FlacMetadata::Verify(source, sidecar, MetadataSidecarFormat::Json), "Binary read as JSON");

        const auto json = MetadataSidecarFormat::Json;
        const auto jsonSidecar = scratch / "archive.flac-metadata.json";
        require(FlacMetadata::SidecarPath(scratch / "a.flac", json) == scratch / "a.flac.flac-metadata.json",
            "Unexpected JSON sidecar path");
        require(FlacMetadata::Save(source, jsonSidecar, json, "{\"title\":\"x\"}", scratch / "clean.flac", prepared),
            "JSON save failed");
        require(FlacMetadata::Verify(source, jsonSidecar, json), "Valid JSON archive rejected");
        std::ifstream jsonInput(jsonSidecar, std::ios::binary);
        const std::string jsonText((std::istreambuf_iterator<char>(jsonInput)), {});
        jsonInput.close();
        require(jsonText.find("\"title\"") != std::string::npos, "Readable metadata missing");
        const auto writeJson = [&](const std::string& data) {
            std::ofstream output(jsonSidecar, std::ios::binary | std::ios::trunc);
            output.write(data.data(), static_cast<std::streamsize>(data.size()));
            output.close();
            require(static_cast<bool>(output), "Cannot write JSON fixture");
        };
        const auto data = jsonText.find("\"data\"");
        require(data != std::string::npos, "JSON archive data missing");
        auto tampered = jsonText;
        const auto value = tampered.find('"', tampered.find(':', data)) + 1;
        tampered[value] = tampered[value] == 'A' ? 'B' : 'A';
        writeJson(tampered);
        require(!FlacMetadata::Verify(source, jsonSidecar, json), "Changed JSON archive accepted");
        writeJson(jsonText.substr(0, jsonText.size() / 2));
        require(!FlacMetadata::Verify(source, jsonSidecar, json), "Truncated JSON accepted");
        writeJson(jsonText);
        require(FlacMetadata::Verify(source, jsonSidecar, json), "Restored JSON archive rejected");

        require(FlacMetadata::SidecarPath(scratch / "a.flac", MetadataSidecarFormat::None).empty(),
            "None format must have no sidecar path");
        require(FlacMetadata::Save(source, {}, MetadataSidecarFormat::None, "", scratch / "clean.flac", prepared),
            "None-format preparation failed");
        require(PreservationAudit::VerifyCopy(sidecar, sidecar), "Identical copy rejected");
        require(!PreservationAudit::VerifyCopy(source, sidecar), "Changed copy accepted");
        require(PreservationAudit::Compare(source, source, true).ok, "Identical resources rejected");
        require(!PreservationAudit::Compare(source, scratch / "missing.flac", false).ok,
            "Uninspectable output accepted");
        std::ifstream audio(source, std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(audio)), {});
        require(bytes.size() >= 4 && std::string(bytes.begin(), bytes.begin() + 4) == "fLaC",
            "Native regression fixture must have no leading ID3");
        const auto transform = [&](const char* name, int omitted,
            const std::function<void(int, std::vector<unsigned char>&)>& modify) {
            std::vector<unsigned char> output(bytes.begin(), bytes.begin() + 4);
            std::size_t offset = 4, lastHeader = 4;
            bool last = false;
            while (!last) {
                require(offset + 4 <= bytes.size(), "Fixture header truncated");
                last = (bytes[offset] & 128) != 0;
                const int kind = bytes[offset] & 127;
                const std::size_t size = (bytes[offset + 1] << 16) |
                    (bytes[offset + 2] << 8) | bytes[offset + 3];
                require(size <= bytes.size() - offset - 4, "Fixture block truncated");
                std::vector<unsigned char> payload(bytes.begin() + offset + 4,
                    bytes.begin() + offset + 4 + size);
                offset += 4 + size;
                if (kind == omitted) continue;
                modify(kind, payload);
                lastHeader = output.size();
                output.push_back(static_cast<unsigned char>(kind));
                output.push_back(static_cast<unsigned char>(payload.size() >> 16));
                output.push_back(static_cast<unsigned char>(payload.size() >> 8));
                output.push_back(static_cast<unsigned char>(payload.size()));
                output.insert(output.end(), payload.begin(), payload.end());
            }
            output[lastHeader] |= 128;
            output.insert(output.end(), bytes.begin() + offset, bytes.end());
            const auto target = scratch / name;
            std::ofstream file(target, std::ios::binary);
            file.write(reinterpret_cast<const char*>(output.data()), output.size());
            file.close();
            require(static_cast<bool>(file), "Cannot write transformed fixture");
            return target;
        };
        const auto unchanged = [](int, std::vector<unsigned char>&) {};
        const auto lostTags = transform("lost-tags.flac", 4, unchanged);
        require(!PreservationAudit::Compare(source, lostTags, false).ok,
            "Unarchived tag loss accepted");
        require(PreservationAudit::Compare(source, lostTags, true).ok,
            "Archive-backed tag loss rejected");
        const auto lostPictures = transform("lost-pictures.flac", 6, unchanged);
        require(!PreservationAudit::Compare(source, lostPictures, true).ok,
            "Supported picture loss accepted despite archive");
        const auto changedPictures = transform("changed-pictures.flac", -1,
            [](int kind, std::vector<unsigned char>& payload) {
                if (kind == 6) payload.back() ^= 1;
            });
        require(!PreservationAudit::Compare(source, changedPictures, true).ok,
            "Changed picture payload accepted despite archive");
        const auto normalized = transform("normalized.flac", -1,
            [](int kind, std::vector<unsigned char>& payload) {
                if (kind != 6) return;
                const auto be = [&](std::size_t offset) {
                    require(offset + 4 <= payload.size(), "Picture header truncated");
                    return (payload[offset] << 24) | (payload[offset + 1] << 16) |
                        (payload[offset + 2] << 8) | payload[offset + 3];
                };
                const auto description = 8 + static_cast<std::size_t>(be(4));
                const auto width = description + 4 + static_cast<std::size_t>(be(description));
                require(width + 4 <= payload.size(), "Picture dimensions truncated");
                payload[width + 3] ^= 1;
            });
        const auto audit = PreservationAudit::Compare(source, normalized, true);
        require(audit.ok && std::any_of(audit.entries.begin(), audit.entries.end(),
            [](const auto& entry) { return entry.status == PreservationAudit::Status::Normalized; }),
            "Normalized picture descriptors not reported");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
