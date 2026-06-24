#include "AudioNormalizer.h"
#include <cmath>

bool AudioNormalizer::normalizeFile(const juce::File& file, float targetLufs, float measuredLufs, juce::AudioFormatManager& formatManager)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr) return false;

    double sampleRate = reader->sampleRate;
    int numChannels = reader->numChannels;
    int64_t lengthInSamples = reader->lengthInSamples;

    if (lengthInSamples <= 0 || sampleRate <= 0.0) return false;

    // 1. Calculate Gain difference
    float diff = targetLufs - measuredLufs;
    
    // If difference is very small, skip normalization
    if (std::abs(diff) < 0.1f) return true; 

    float linearGain = std::pow(10.0f, diff / 20.0f);

    // 4. Create new file
    juce::File tempFile = file.getSiblingFile(file.getFileNameWithoutExtension() + "_norm.tmp");
    tempFile.deleteFile();

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
        new juce::FileOutputStream(tempFile),
        sampleRate,
        numChannels,
        24, // Bits per sample
        {},
        0
    ));

    if (writer == nullptr) return false;

    // 5. Apply gain and write to temp file
    int64_t position = 0;
    int blockSize = 8192;
    juce::AudioBuffer<float> readBuffer(numChannels, blockSize);
    
    while (position < lengthInSamples)
    {
        int numThisTime = (int)juce::jmin((int64_t)blockSize, lengthInSamples - position);
        reader->read(&readBuffer, 0, numThisTime, position, true, true);

        readBuffer.applyGain(0, numThisTime, linearGain);
        writer->writeFromAudioSampleBuffer(readBuffer, 0, numThisTime);

        position += numThisTime;
    }

    // Release writer/reader to free file locks
    writer.reset();
    reader.reset();

    // 6. Replace old file
    return tempFile.replaceFileIn(file);
}
