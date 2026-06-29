#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../../SpectralRef/MondoSpectralRefProcessor.h"

class SpectralRefView : public juce::Component
{
public:
    SpectralRefView(MondoSpectralRefAudioProcessor& processor);
    ~SpectralRefView() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    MondoSpectralRefAudioProcessor& processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralRefView)
};
