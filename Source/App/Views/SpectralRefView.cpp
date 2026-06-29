#include "SpectralRefView.h"

SpectralRefView::SpectralRefView(MondoSpectralRefAudioProcessor& p) : processor(p)
{
    editor.reset(processor.createEditorIfNeeded());
    if (editor != nullptr)
    {
        addAndMakeVisible(editor.get());
    }
}

SpectralRefView::~SpectralRefView()
{
    editor.reset();
}

void SpectralRefView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);
}

void SpectralRefView::resized()
{
    if (editor != nullptr)
        editor->setBounds(getLocalBounds());
}
