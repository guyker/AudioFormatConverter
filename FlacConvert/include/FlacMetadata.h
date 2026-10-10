#pragma once

#include <filesystem>

namespace FlacMetadata
{
// Original leading ID3, fLaC marker/metadata blocks and trailing APE/ID3v1; no audio frames.
bool Save(const std::filesystem::path& source, const std::filesystem::path& sidecar,
    const std::filesystem::path& temporaryInput, bool& preparedInput);
}
