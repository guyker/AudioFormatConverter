#pragma once

enum class AudioTarget { Flac, Mp3 };

struct ConversionOptions
{
    AudioTarget target{ AudioTarget::Flac };
    int mp3Quality{ 0 };
    bool downmixToStereo{ false };
};
