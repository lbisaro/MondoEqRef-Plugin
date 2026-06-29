#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "MondoSpectralRefProcessor.h"

class MondoSpectralRefEditor : public juce::AudioProcessorEditor, public juce::Timer, public juce::TooltipClient
{
public:
    MondoSpectralRefEditor (MondoSpectralRefAudioProcessor&);
    ~MondoSpectralRefEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    
    // Mouse Events for Vertical Zoom/Pan and Measurement
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    
    juce::String getTooltip() override;

private:
    void timerCallback() override;

    MondoSpectralRefAudioProcessor& audioProcessor;
    juce::SharedResourcePointer<juce::TooltipWindow> tooltipWindow;

    // View state for Vertical Zoom
    float minDecibels = -24.0f;
    float maxDecibels = 24.0f;
    float dragStartY = 0.0f;
    float dragStartMinDB = 0.0f;
    float dragStartMaxDB = 0.0f;

    // Mouse Tracking & Measurement
    juce::Point<int> mousePos;
    bool isMouseOverPlot = false;
    bool isMeasuring = false;
    bool isDragging = false;
    juce::Point<int> measurePointA;
    
    juce::TextButton normalizeButton { "Normalize a 0dB" };
    juce::TextButton zoomHzButton { "Zoom Hz 50-10K" };
    bool isZoomedHz = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MondoSpectralRefEditor)
};
