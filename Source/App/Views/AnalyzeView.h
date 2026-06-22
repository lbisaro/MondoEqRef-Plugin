#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../PluginProcessor.h"

class AnalyzeView : public juce::Component
{
public:
    AnalyzeView(MondoEqRefAudioProcessor& p);
    ~AnalyzeView() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    
    std::function<void(int)> onRequestTabChange;
    std::function<void(bool)> onPlayStateChanged;
    
    bool getIsPlaying() const { return isPlaying; }
    void setIsPlaying(bool shouldPlay);
    int getTrackMode() const;
    void setTrackMode(int modeId);
    
    juce::Label debugLabel;
    
    void setLoadedFileName(const juce::String& name);

private:
    void updatePlayStopButton();

    MondoEqRefAudioProcessor& processor;
    std::unique_ptr<juce::AudioProcessorEditor> processorEditor;
    
    // UI Elements for "Track"
    juce::Label trackLabel;
    juce::ComboBox trackSelector;
    juce::Label loadedFileNameLabel;
    juce::TextButton playStopButton;
    
    bool isPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalyzeView)
};
