#include "PluginProcessor.h"
#include "PluginEditor.h"

MondoEqRefAudioProcessor::MondoEqRefAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    std::fill(fifo.begin(), fifo.end(), 0.0f);
}

MondoEqRefAudioProcessor::~MondoEqRefAudioProcessor()
{
}

const juce::String MondoEqRefAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MondoEqRefAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool MondoEqRefAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool MondoEqRefAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double MondoEqRefAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int MondoEqRefAudioProcessor::getNumPrograms()
{
    return 1;
}

int MondoEqRefAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MondoEqRefAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String MondoEqRefAudioProcessor::getProgramName (int index)
{
    return {};
}

void MondoEqRefAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

void MondoEqRefAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = getTotalNumInputChannels();

    preFilter.state = juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate, 1681.97f, 0.7071f, juce::Decibels::decibelsToGain(4.0f));
    preFilter.prepare(spec);

    rlbFilter.state = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 38.1355f, 0.5f);
    rlbFilter.prepare(spec);
    
    resetLufs();
}

void MondoEqRefAudioProcessor::releaseResources()
{
}

bool MondoEqRefAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}

void MondoEqRefAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (totalNumInputChannels > 0)
    {
        auto* channelData = buffer.getReadPointer (0);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            pushNextSampleIntoFifo(channelData[i]);
        }
        
        // LUFS Calculation (EBU R 128)
        juce::AudioBuffer<float> lufsBuffer;
        lufsBuffer.makeCopyOf(buffer);
        juce::dsp::AudioBlock<float> block(lufsBuffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        
        preFilter.process(context);
        rlbFilter.process(context);
        
        double blockSumSquares = 0.0;
        int numChannels = lufsBuffer.getNumChannels();
        int numSamples = lufsBuffer.getNumSamples();
        
        for (int ch = 0; ch < numChannels; ++ch) {
            auto* readPtr = lufsBuffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i) {
                blockSumSquares += readPtr[i] * readPtr[i];
            }
        }
        
        // Accumulate in thread-safe manner
        double currentSum = lufsSumSquares.load(std::memory_order_relaxed);
        while (!lufsSumSquares.compare_exchange_weak(currentSum, currentSum + blockSumSquares, std::memory_order_release, std::memory_order_relaxed));
        
        int64_t currentCount = lufsSampleCount.load(std::memory_order_relaxed);
        while (!lufsSampleCount.compare_exchange_weak(currentCount, currentCount + numSamples, std::memory_order_release, std::memory_order_relaxed));
    }
}

void MondoEqRefAudioProcessor::pushNextSampleIntoFifo (float sample)
{
    auto idx = fifoIndex.load();
    fifo[(size_t)idx] = sample;
    
    // Solo permitimos que el Editor resetee el index, o lo reseteamos si llegamos al borde (aunque no deberia pasar si fifo es el doble del tamaño maximo)
    if (idx < fifoSize - 1)
        fifoIndex.store(idx + 1);
}

bool MondoEqRefAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* MondoEqRefAudioProcessor::createEditor()
{
    return new MondoEqRefAudioProcessorEditor (*this);
}

void MondoEqRefAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
}

void MondoEqRefAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MondoEqRefAudioProcessor();
}
