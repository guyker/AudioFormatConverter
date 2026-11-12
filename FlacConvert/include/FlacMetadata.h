#pragma once

#include <filesystem>
#include <string>

#include "ConversionOptions.h"

namespace FlacMetadata
{
// True when this conversion archives original metadata for the source (MP3 target or native .flac source).
bool AppliesTo(const std::filesystem::path& source, const ConversionOptions& options);
// <audio>.flac-metadata (Binary) or <audio>.flac-metadata.json (Json); empty for None.
std::filesystem::path SidecarPath(std::filesystem::path audio, MetadataSidecarFormat format);

// Archive: original leading ID3, fLaC marker/metadata blocks and trailing APE/ID3v1; no audio frames.
// Binary writes the archive bytes; Json writes readableMetadataJson (a JSON object or empty) plus the
// archive bytes in Base64; None writes no sidecar. Every format prepares temporaryInput when appended
// tags must be removed before decoding.
bool Save(const std::filesystem::path& source, const std::filesystem::path& sidecar,
    MetadataSidecarFormat format, const std::string& readableMetadataJson,
    const std::filesystem::path& temporaryInput, bool& preparedInput);
bool Verify(const std::filesystem::path& source, const std::filesystem::path& sidecar,
    MetadataSidecarFormat format);
}
