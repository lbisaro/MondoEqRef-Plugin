#include "AudioNormalizer.h"
#include <cmath>

bool AudioNormalizer::importAndResample(const juce::File& source, const juce::File& destination, double targetSampleRate, double maxDurationSeconds)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(source));
    if (reader == nullptr) return false;

    int numChannels = reader->numChannels;
    if (numChannels == 0) return false;

    double ratio = reader->sampleRate / targetSampleRate;
    int64_t maxSamplesSource = (int64_t)(maxDurationSeconds * reader->sampleRate);
    int64_t samplesToReadFromSource = juce::jmin(reader->lengthInSamples, maxSamplesSource);
    int64_t samplesToWrite = (int64_t)(samplesToReadFromSource / ratio);

    destination.deleteFile();

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
        new juce::FileOutputStream(destination),
        targetSampleRate,
        numChannels,
        24, // 24-bit
        {},
        0
    ));

    if (writer == nullptr) return false;

    juce::AudioFormatReaderSource readerSource(reader.release(), true);
    readerSource.setLooping(false);
    
    juce::ResamplingAudioSource resampler(&readerSource, false, numChannels);
    resampler.setResamplingRatio(ratio);
    resampler.prepareToPlay(8192, targetSampleRate);

    bool success = writer->writeFromAudioSource(resampler, (int)samplesToWrite, 8192);
    
    writer.reset();
    return success;
}

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
    
    double targetSampleRate = 48000.0;
    double ratio = sampleRate / targetSampleRate;

    // If difference is very small AND sample rate is already 48k, skip normalization
    if (std::abs(diff) < 0.1f && std::abs(sampleRate - targetSampleRate) < 1.0) return true; 

    float linearGain = std::pow(10.0f, diff / 20.0f);

    // 4. Create new file
    juce::File tempFile = file.getSiblingFile(file.getFileNameWithoutExtension() + "_norm.tmp");
    tempFile.deleteFile();

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
        new juce::FileOutputStream(tempFile),
        targetSampleRate,
        numChannels,
        24, // Bits per sample
        {},
        0
    ));

    if (writer == nullptr) return false;

    // Use ResamplingAudioSource to ensure 48kHz output
    juce::AudioFormatReaderSource readerSource(reader.release(), true);
    readerSource.setLooping(false);
    
    juce::ResamplingAudioSource resampler(&readerSource, false, numChannels);
    resampler.setResamplingRatio(ratio);
    
    int blockSize = 8192;
    resampler.prepareToPlay(blockSize, targetSampleRate);

    // 5. Apply gain and write to temp file
    int64_t samplesToWrite = (int64_t)(lengthInSamples / ratio);
    int64_t position = 0;
    juce::AudioBuffer<float> readBuffer(numChannels, blockSize);
    
    while (position < samplesToWrite)
    {
        int numThisTime = (int)juce::jmin((int64_t)blockSize, samplesToWrite - position);
        
        juce::AudioSourceChannelInfo info(&readBuffer, 0, numThisTime);
        resampler.getNextAudioBlock(info);

        readBuffer.applyGain(0, numThisTime, linearGain);
        writer->writeFromAudioSampleBuffer(readBuffer, 0, numThisTime);

        position += numThisTime;
    }

    // Release writer to free file locks
    writer.reset();

    // 6. Replace old file
    return tempFile.replaceFileIn(file);
}
