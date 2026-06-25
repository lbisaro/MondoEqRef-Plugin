#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

class AudioNormalizer
{
public:
    static bool normalizeFile(const juce::File& file, float targetLufs, float measuredLufs, juce::AudioFormatManager& formatManager);
    
    // Importa, recorta a maxDurationSeconds (ej. 30.0), y convierte a targetSampleRate (ej. 48000.0) y 24-bit
    static bool importAndResample(const juce::File& source, const juce::File& destination, double targetSampleRate = 48000.0, double maxDurationSeconds = 30.0);
};
