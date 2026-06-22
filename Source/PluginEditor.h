#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class MondoEqRefAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer, public juce::TooltipClient
{
public:
    MondoEqRefAudioProcessorEditor (MondoEqRefAudioProcessor&);
    ~MondoEqRefAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    
    // Mouse Events for Vertical Zoom/Pan
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    
    bool keyPressed(const juce::KeyPress& key) override;
    juce::String getTooltip() override;

private:
    juce::SharedResourcePointer<juce::TooltipWindow> tooltipWindow;
    
    void timerCallback() override;
    void updateFftSize();
    void pushNextSampleIntoFifo(float sample) noexcept;
    void drawNextFrameOfSpectrum();
    void drawFrame(juce::Graphics& g);
    void loadTargets();
    void targetRoleChanged();

    struct PresetPoint { float f; float target; float maxLimit; float minLimit; };
    struct PresetBand { float minFreq; float maxFreq; juce::String name; juce::String tip; juce::Colour color; };
    struct PresetTarget { juce::String name; float targetLufs = -18.0f; std::vector<PresetBand> bands; std::vector<PresetPoint> points; };
    
    std::vector<PresetTarget> presets;
    int currentPresetIndex = 0;

    MondoEqRefAudioProcessor& audioProcessor;

    juce::ComboBox fftSizeBox;
    juce::Label fftSizeLabel;
    
    juce::ComboBox targetRoleBox;
    juce::Label targetRoleLabel;

    juce::TextButton resetButton;
    juce::ToggleButton tiltButton { "Tilt +4.5dB/Oct" };
    juce::Slider targetOffsetSlider { juce::Slider::LinearVertical, juce::Slider::TextBoxBelow };
    
    bool isTiltEnabled = true;
    float currentTargetOffset = 0.0f;

    int currentFftOrder = 11; // 2^11 = 2048
    int currentFftSize = 2048;
    std::unique_ptr<juce::dsp::FFT> forwardFFT;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

    std::vector<float> fifo;
    std::vector<float> fftData;
    int fifoIndex = 0;
    bool nextFFTBlockReady = false;
    std::vector<float> scopeData;
    std::vector<float> representativeCurve; // Peak Hold

    struct ActiveTooltip { juce::Rectangle<float> rect; juce::String text; };
    std::vector<ActiveTooltip> activeBandTooltips;

    // View state for Vertical Zoom
    float minDecibels = -100.0f;
    float maxDecibels = 0.0f;
    float dragStartY = 0.0f;
    float dragStartMinDB = 0.0f;
    float dragStartMaxDB = 0.0f;

    // Mouse Tracking & Measurement
    juce::Point<int> mousePos;
    bool isMouseOverPlot = false;
    bool isMeasuring = false;
    bool isDragging = false;
    juce::Point<int> measurePointA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MondoEqRefAudioProcessorEditor)
};
