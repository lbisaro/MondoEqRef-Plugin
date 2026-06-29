#include "MondoSpectralRefProcessor.h"
#include "MondoSpectralRefEditor.h"

MondoSpectralRefAudioProcessor::MondoSpectralRefAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                       #endif
                       ),
       forwardFFT(fftOrder)
#endif
{
    std::fill(sendFifo.begin(), sendFifo.end(), 0.0f);
    std::fill(returnFifo.begin(), returnFifo.end(), 0.0f);
    std::fill(transferFunctionMagnitudes.begin(), transferFunctionMagnitudes.end(), 0.0f);

    for (int i = 0; i < fftSize; ++i) {
        periodicNoise[i] = (random.nextFloat() * 2.0f - 1.0f) * noiseLevel;
    }
}

MondoSpectralRefAudioProcessor::~MondoSpectralRefAudioProcessor()
{
}

const juce::String MondoSpectralRefAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MondoSpectralRefAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool MondoSpectralRefAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool MondoSpectralRefAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double MondoSpectralRefAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int MondoSpectralRefAudioProcessor::getNumPrograms()
{
    return 1;
}

int MondoSpectralRefAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MondoSpectralRefAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String MondoSpectralRefAudioProcessor::getProgramName (int index)
{
    return {};
}

void MondoSpectralRefAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

void MondoSpectralRefAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Re-generar el ruido por si cambió el noiseLevel
    for (int i = 0; i < fftSize; ++i) {
        periodicNoise[i] = (random.nextFloat() * 2.0f - 1.0f) * noiseLevel;
    }
    noiseIndex = 0;
    
    std::fill(baselineMagnitudes.begin(), baselineMagnitudes.end(), 1.0f);
    std::fill(displayMagnitudes.begin(), displayMagnitudes.end(), 0.0f);
    isNormalized = false;
    triggerNormalize = false;
}

void MondoSpectralRefAudioProcessor::releaseResources()
{
}

bool MondoSpectralRefAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

void MondoSpectralRefAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // The Send signal will be generated and output on Channel 0
    // The Return signal will be read from Channel 0 (Input)

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        // Generate Periodic White Noise
        float noise = periodicNoise[noiseIndex];
        noiseIndex = (noiseIndex + 1) % fftSize;
        
        float returnSample = 0.0f;
        if (totalNumInputChannels > 0)
            returnSample = buffer.getReadPointer(0)[i];

        pushNextSampleIntoFifo(noise, returnSample);

        // Output noise to channel 0
        if (totalNumOutputChannels > 0)
            buffer.getWritePointer(0)[i] = noise;
        
        // Output silence to other channels
        for (int ch = 1; ch < totalNumOutputChannels; ++ch)
            buffer.getWritePointer(ch)[i] = 0.0f;
    }
}

void MondoSpectralRefAudioProcessor::pushNextSampleIntoFifo (float sendSample, float returnSample)
{
    if (fifoIndex == fftSize)
    {
        if (!nextFFTBlockReady)
        {
            std::copy(sendFifo.begin(), sendFifo.begin() + fftSize, sendFftData.begin());
            std::copy(returnFifo.begin(), returnFifo.begin() + fftSize, returnFftData.begin());
            nextFFTBlockReady = true;
        }

        fifoIndex = 0;
    }

    sendFifo[(size_t)fifoIndex] = sendSample;
    returnFifo[(size_t)fifoIndex] = returnSample;
    ++fifoIndex;
}

void MondoSpectralRefAudioProcessor::calculateTransferFunction()
{
    if (nextFFTBlockReady)
    {
        // Sin ventana (Rectangular) porque usamos ruido periódico sincronizado al tamaño de la FFT
        forwardFFT.performFrequencyOnlyForwardTransform(sendFftData.data());
        forwardFFT.performFrequencyOnlyForwardTransform(returnFftData.data());

        bool shouldNormalize = triggerNormalize.exchange(false);
        if (shouldNormalize) {
            isNormalized = !isNormalized.load(); // Toggle
        }

        // Transfer function Magnitude = Return Mag / Send Mag
        for (int i = 0; i < fftSize / 2; ++i)
        {
            float sendMag = sendFftData[i];
            float returnMag = returnFftData[i];
            
            float tfMag = 0.0f;
            if (sendMag > 1e-5f)
            {
                tfMag = returnMag / sendMag;
            }
            
            // Logarithmic smoothing
            transferFunctionMagnitudes[i] = 0.8f * transferFunctionMagnitudes[i] + 0.2f * tfMag;
            
            // Capture if toggled ON just now
            if (shouldNormalize && isNormalized.load()) {
                baselineMagnitudes[i] = transferFunctionMagnitudes[i];
            }
            
            // Display calculation
            if (isNormalized.load() && baselineMagnitudes[i] > 1e-5f) {
                displayMagnitudes[i] = transferFunctionMagnitudes[i] / baselineMagnitudes[i];
            } else {
                displayMagnitudes[i] = transferFunctionMagnitudes[i];
            }
        }

        nextFFTBlockReady = false;
    }
}

bool MondoSpectralRefAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* MondoSpectralRefAudioProcessor::createEditor()
{
    return new MondoSpectralRefEditor (*this);
}

void MondoSpectralRefAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
}

void MondoSpectralRefAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
}

#ifdef MONDO_SPECTRAL_REF_PLUGIN
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MondoSpectralRefAudioProcessor();
}
#endif
