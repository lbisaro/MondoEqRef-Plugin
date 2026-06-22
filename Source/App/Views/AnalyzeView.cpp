#include "AnalyzeView.h"

AnalyzeView::AnalyzeView(MondoEqRefAudioProcessor& p) : processor(p)
{
    processorEditor.reset(processor.createEditorIfNeeded());
    if (processorEditor)
        addAndMakeVisible(processorEditor.get());

    addAndMakeVisible(trackLabel);
    trackLabel.setText("Track:", juce::dontSendNotification);
    trackLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(debugLabel);
    debugLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);

    addAndMakeVisible(trackSelector);
    trackSelector.addItem("Live Audio", 1);
    trackSelector.addItem("Guitar DI...", 2);
    trackSelector.addItem("Stem...", 3);
    trackSelector.setSelectedId(1, juce::dontSendNotification);
    
    addAndMakeVisible(loadedFileNameLabel);
    loadedFileNameLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
    loadedFileNameLabel.setFont(juce::FontOptions(14.0f, juce::Font::italic));
    
    trackSelector.onChange = [this] {
        int id = trackSelector.getSelectedId();
        if (id == 2 && onRequestTabChange) {
            onRequestTabChange(1); // Go to Guitar DI tab
        } else if (id == 3 && onRequestTabChange) {
            onRequestTabChange(2); // Go to Stems tab
        }
    };

    addAndMakeVisible(playStopButton);
    updatePlayStopButton();
    playStopButton.onClick = [this] {
        setIsPlaying(!isPlaying);
    };
}

AnalyzeView::~AnalyzeView()
{
    processorEditor = nullptr;
}

void AnalyzeView::updatePlayStopButton()
{
    if (isPlaying) {
        playStopButton.setButtonText(juce::String::fromUTF8("\xe2\x96\xa0")); // Stop icon ■
        playStopButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
        playStopButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    } else {
        playStopButton.setButtonText(juce::String::fromUTF8("\xe2\x96\xb6")); // Play icon ▶
        playStopButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
        playStopButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    }
}

void AnalyzeView::setIsPlaying(bool shouldPlay)
{
    if (isPlaying != shouldPlay)
    {
        isPlaying = shouldPlay;
        updatePlayStopButton();
        if (onPlayStateChanged)
            onPlayStateChanged(isPlaying);
    }
}

int AnalyzeView::getTrackMode() const
{
    return trackSelector.getSelectedId();
}

void AnalyzeView::setTrackMode(int modeId)
{
    trackSelector.setSelectedId(modeId, juce::sendNotificationSync);
}

void AnalyzeView::setLoadedFileName(const juce::String& name)
{
    loadedFileNameLabel.setText(name, juce::dontSendNotification);
}

void AnalyzeView::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    
    auto bounds = getLocalBounds();
    auto topBar = bounds.removeFromTop(40);
    g.setColour(juce::Colours::black.withAlpha(0.2f));
    g.fillRect(topBar);
}

void AnalyzeView::resized()
{
    auto bounds = getLocalBounds();
    auto topBar = bounds.removeFromTop(40);

    // Left alignment
    auto trackArea = topBar.removeFromLeft(500).reduced(5);
    trackLabel.setBounds(trackArea.removeFromLeft(50));
    trackSelector.setBounds(trackArea.removeFromLeft(150));
    loadedFileNameLabel.setBounds(trackArea.removeFromLeft(280));

    // Right alignment for Play/Stop
    auto rightArea = topBar.removeFromRight(100).reduced(5);
    playStopButton.setBounds(rightArea.removeFromRight(40));

    if (processorEditor)
        processorEditor->setBounds(bounds);
        
    debugLabel.setBounds(10, 80, 600, 20);
}
