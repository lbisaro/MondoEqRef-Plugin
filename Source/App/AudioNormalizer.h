#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

class AudioNormalizer
{
public:
    static bool normalizeFile(const juce::File& file, float targetLufs, float measuredLufs, juce::AudioFormatManager& formatManager);
};
