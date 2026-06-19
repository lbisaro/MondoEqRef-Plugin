#pragma once

#include <JuceHeader.h>

class MondoEqRefAudioProcessor : public juce::AudioProcessor
{
public:
    MondoEqRefAudioProcessor();
    ~MondoEqRefAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // FIFO variables
    static constexpr auto fifoSize = 8192 * 2;
    std::atomic<int> fifoIndex { 0 };
    std::array<float, fifoSize> fifo;

    void pushNextSampleIntoFifo (float sample);

    // EBU R 128 LUFS Measurement
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> preFilter;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> rlbFilter;

    std::atomic<double> lufsSumSquares { 0.0 };
    std::atomic<int64_t> lufsSampleCount { 0 };

    void resetLufs() {
        lufsSumSquares = 0.0;
        lufsSampleCount = 0;
    }
    
    float getIntegratedLufs() const {
        int64_t count = lufsSampleCount.load();
        if (count == 0) return -100.0f;
        double meanSquare = lufsSumSquares.load() / (double)count;
        if (meanSquare <= 1e-10) return -100.0f;
        return (float)(10.0 * std::log10(meanSquare) - 0.691);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MondoEqRefAudioProcessor)
};
