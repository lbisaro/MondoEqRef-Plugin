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
