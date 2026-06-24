#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class GuitarDIView : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     public juce::Timer
{
public:
    GuitarDIView();
    ~GuitarDIView() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    
    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    
    // Timer para cuenta regresiva y medidor
    void timerCallback() override;

    // Teclado
    bool keyPressed(const juce::KeyPress& key) override;

    std::function<void(const juce::File&)> onDiLoadRequested;
    std::function<void(bool, const juce::String&)> onRecordToggled; // isRecording, fileName
    std::function<float()> getDiRms;
    
    void setRecordingState(bool isRecording);

private:
    void refreshList();
    void importDiViaDialog();
    void deleteDi(const juce::File& file);
    void promptForRecording();

    juce::TextButton loadButton { "Importar DI" };
    juce::TextButton recordButton { "Grabar Nuevo DI" };
    juce::TextButton cancelButton { "Cancelar" };
    juce::Label statusLabel;

public:
    juce::Label debugLabel;
private:
    
    juce::Viewport viewport;
    juce::Component listContainer;
    juce::OwnedArray<juce::Component> diItems;
    
    std::unique_ptr<juce::FileChooser> fileChooser;
    
    bool isRecording = false;
    int countdownTicks = 0;
    int recordingSeconds = 0;
    bool isClosingRecording = false;
    juce::String pendingFileName;
    
    float currentRms = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GuitarDIView)
};
