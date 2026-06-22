#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class StemsView : public juce::Component,
                  public juce::FileDragAndDropTarget
{
public:
    StemsView();
    ~StemsView() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    
    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    std::function<void(const juce::File&)> onStemLoadRequested;

private:
    void refreshList();
    void importStemViaDialog();
    void deleteStem(const juce::File& file);

    juce::TextButton loadButton { "Importar Stem" };
    juce::Label statusLabel;
    
    juce::Viewport viewport;
    juce::Component listContainer;
    juce::OwnedArray<juce::Component> stemItems;
    
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemsView)
};
