#pragma once

enum class AudioTarget { Flac, Mp3 };

// Effective sidecar format for one conversion run, resolved from MetadataSidecars settings.
enum class MetadataSidecarFormat { None, Binary, Json };

struct ConversionOptions
{
    AudioTarget target{ AudioTarget::Flac };
    int mp3Quality{ 0 };
    bool downmixToStereo{ false };
    MetadataSidecarFormat sidecarFormat{ MetadataSidecarFormat::Binary };
};
