#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class MondoSpectralRefAudioProcessor : public juce::AudioProcessor
{
public:
    MondoSpectralRefAudioProcessor();
    ~MondoSpectralRefAudioProcessor() override;

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

    static constexpr int fftOrder = 12; // 4096 samples
    static constexpr int fftSize = 1 << fftOrder;

    juce::dsp::FFT forwardFFT;

    std::array<float, fftSize> periodicNoise;
    int noiseIndex = 0;

    std::array<float, fftSize * 2> sendFifo;
    std::array<float, fftSize * 2> returnFifo;
    std::array<float, fftSize * 2> sendFftData;
    std::array<float, fftSize * 2> returnFftData;
    
    int fifoIndex = 0;
    bool nextFFTBlockReady = false;

    std::array<float, fftSize / 2> transferFunctionMagnitudes;
    std::array<float, fftSize / 2> baselineMagnitudes;
    std::array<float, fftSize / 2> displayMagnitudes;
    
    std::atomic<bool> isNormalized { false };
    std::atomic<bool> triggerNormalize { false };
    
    float noiseLevel = 0.5f;
    juce::Random random;

    void pushNextSampleIntoFifo (float sendSample, float returnSample);
    void calculateTransferFunction();

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MondoSpectralRefAudioProcessor)
};
