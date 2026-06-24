#include "GuitarDIView.h"
#include "../DeviceRouting.h"

class DiItemComponent : public juce::Component
{
public:
    DiItemComponent(const juce::File& f, std::function<void()> onDelete, std::function<void()> onLoad)
        : file(f), onDeleteRequested(onDelete), onLoadRequested(onLoad)
    {
        nameLabel.setText(file.getFileName(), juce::dontSendNotification);
        nameLabel.setFont(16.0f);
        addAndMakeVisible(nameLabel);

        loadButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen);
        addAndMakeVisible(loadButton);
        loadButton.onClick = [this] { if (onLoadRequested) onLoadRequested(); };

        deleteButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
        addAndMakeVisible(deleteButton);
        deleteButton.onClick = [this] { if (onDeleteRequested) onDeleteRequested(); };
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colours::darkgrey.withAlpha(0.3f));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 5.0f);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(5);
        deleteButton.setBounds(bounds.removeFromLeft(80));
        bounds.removeFromLeft(10);
        loadButton.setBounds(bounds.removeFromLeft(80));
        bounds.removeFromLeft(10);
        nameLabel.setBounds(bounds);
    }

private:
    juce::File file;
    juce::Label nameLabel;
    juce::TextButton deleteButton { "Eliminar" };
    juce::TextButton loadButton { "Cargar" };
    
    std::function<void()> onDeleteRequested;
    std::function<void()> onLoadRequested;
};

GuitarDIView::GuitarDIView()
{
    addAndMakeVisible(loadButton);
    loadButton.onClick = [this] { importDiViaDialog(); };

    addAndMakeVisible(recordButton);
    recordButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red.withAlpha(0.6f));
    recordButton.onClick = [this] { promptForRecording(); };

    addAndMakeVisible(cancelButton);
    cancelButton.setColour(juce::TextButton::buttonColourId, juce::Colours::grey);
    cancelButton.setVisible(false);
    cancelButton.onClick = [this] {
        countdownTicks = 0;
        setRecordingState(false);
        if (onRecordToggled) onRecordToggled(false, "");
        refreshList();
    };

    addAndMakeVisible(statusLabel);
    statusLabel.setText("Libreria de Guitar DIs", juce::dontSendNotification);
    statusLabel.setFont(juce::FontOptions(24.0f, juce::Font::bold));

    addAndMakeVisible(debugLabel);
    debugLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);

    viewport.setViewedComponent(&listContainer, false);
    viewport.setScrollBarsShown(true, false, true, false);
    addAndMakeVisible(viewport);

    setWantsKeyboardFocus(true);
    startTimerHz(30);

    refreshList();
}

GuitarDIView::~GuitarDIView()
{
    stopTimer();
}

void GuitarDIView::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    
    auto bounds = getLocalBounds().reduced(20);
    
    // Meter on the right side
    auto meterArea = bounds.removeFromRight(40);
    bounds.removeFromRight(20); // Spacing
    
    // Draw "DI" label above meter
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    g.drawText("DI", meterArea.removeFromTop(20), juce::Justification::centred, false);
    meterArea.removeFromTop(5);
    
    // Draw VU Meter background
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.fillRect(meterArea);
    
    float db = juce::Decibels::gainToDecibels(currentRms, -80.0f);
    auto vuArea = meterArea;
    float vuDb = db;
    if (vuDb > -79.0f)
    {
        float fillHeight = juce::jmap(vuDb, -60.0f, 0.0f, 0.0f, (float)vuArea.getHeight());
        fillHeight = juce::jlimit(0.0f, (float)vuArea.getHeight(), fillHeight);
        
        auto fillArea = vuArea.withTrimmedTop(vuArea.getHeight() - (int)fillHeight);
        
        // Color igual a LUFS fill
        g.setColour(juce::Colour::fromString("ffffffff").withAlpha(0.12f));
        g.fillRect(fillArea);
    }

    // Draw outline
    g.setColour(juce::Colours::white.withAlpha(0.2f));
    g.drawRect(meterArea);
    
    // Draw current dB text below meter
    juce::String valStr = db > -79.0f ? juce::String(db, 1) : "--";
    g.setColour(juce::Colours::white);
    g.drawText(valStr, meterArea.getX(), meterArea.getBottom() + 5, meterArea.getWidth(), 20, juce::Justification::centred, false);
    
    if (countdownTicks > 0)
    {
        g.setColour(juce::Colours::red);
        g.setFont(48.0f);
        g.drawText(juce::String(countdownTicks / 30 + 1), getLocalBounds(), juce::Justification::centred, false);
    }
    else if (isClosingRecording)
    {
        g.setColour(juce::Colours::yellow);
        g.setFont(32.0f);
        g.drawText("Cerrando grabación...", getLocalBounds(), juce::Justification::centred, false);
    }
    else if (isRecording)
    {
        int totalSeconds = recordingSeconds / 30;
        int mins = totalSeconds / 60;
        int secs = totalSeconds % 60;
        juce::String timeStr = juce::String::formatted("%02d:%02d", mins, secs);
        
        g.setColour(juce::Colours::red.withAlpha(0.2f));
        g.fillAll(); // Red flash background
        
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(64.0f, juce::Font::bold));
        g.drawText(timeStr, getLocalBounds(), juce::Justification::centred, false);
    }
}

void GuitarDIView::resized()
{
    auto bounds = getLocalBounds();
    debugLabel.setBounds(10, bounds.getBottom() - 30, bounds.getWidth() - 20, 20);

    bounds = getLocalBounds().reduced(20);
    bounds.removeFromRight(60); // Reserve space for the meter
    
    auto topArea = bounds.removeFromTop(40);
    statusLabel.setBounds(topArea.removeFromLeft(300));
    
    auto buttonArea = topArea.removeFromRight(400); // wider
    loadButton.setBounds(buttonArea.removeFromLeft(120).reduced(5));
    recordButton.setBounds(buttonArea.removeFromLeft(140).reduced(5));
    cancelButton.setBounds(buttonArea.removeFromLeft(120).reduced(5));

    bounds.removeFromTop(10);
    viewport.setBounds(bounds);
    
    int listWidth = viewport.getMaximumVisibleWidth();
    listContainer.setSize(listWidth, listContainer.getHeight());
    
    for (int i = 0; i < diItems.size(); ++i)
    {
        if (auto* item = diItems[i])
            item->setBounds(0, i * 40, listWidth, 40);
    }
}

bool GuitarDIView::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto path : files)
        if (path.endsWithIgnoreCase(".wav"))
            return true;
    return false;
}

void GuitarDIView::filesDropped(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(x, y);
    juce::File diDir = DeviceRoutingManager::getDiDirectory();
    bool anyCopied = false;

    for (auto path : files)
    {
        juce::File sourceFile(path);
        if (sourceFile.existsAsFile() && sourceFile.hasFileExtension(".wav"))
        {
            juce::File destFile = diDir.getChildFile(sourceFile.getFileName());
            if (sourceFile.copyFileTo(destFile))
                anyCopied = true;
        }
    }

    if (anyCopied)
        refreshList();
}

void GuitarDIView::importDiViaDialog()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Seleccionar archivo de Audio (Guitar DI)",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.mp3"
    );

    auto folderChooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser& chooser)
    {
        juce::File result = chooser.getResult();
        if (result.existsAsFile())
        {
            juce::File diDir = DeviceRoutingManager::getDiDirectory();
            juce::File destFile = diDir.getChildFile(result.getFileName());
            if (result.copyFileTo(destFile))
            {
                refreshList();
            }
        }
    });
}

void GuitarDIView::deleteDi(const juce::File& file)
{
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Eliminar DI")
            .withMessage("¿Estas seguro de eliminar '" + file.getFileName() + "'?")
            .withButton("Eliminar").withButton("Cancelar"),
        [this, file](int result)
        {
            if (result == 1)
            {
                file.deleteFile();
                refreshList();
            }
        }
    );
}

void GuitarDIView::refreshList()
{
    diItems.clear();
    
    juce::File diDir = DeviceRoutingManager::getDiDirectory();
    juce::Array<juce::File> wavFiles = diDir.findChildFiles(juce::File::findFiles, false, "*.wav");

    int itemHeight = 40;
    int yOffset = 0;
    
    int listWidth = juce::jmax(400, viewport.getMaximumVisibleWidth());

    for (auto& file : wavFiles)
    {
        auto* item = new DiItemComponent(
            file,
            [this, file] { deleteDi(file); },
            [this, file] { if (onDiLoadRequested) onDiLoadRequested(file); }
        );

        diItems.add(item);
        listContainer.addAndMakeVisible(item);
        item->setBounds(0, yOffset, listWidth, itemHeight);
        yOffset += itemHeight;
    }

    listContainer.setSize(listWidth, yOffset);
}

void GuitarDIView::promptForRecording()
{
    if (isRecording)
    {
        // STOP -> Ask for filename
        juce::AlertWindow* w = new juce::AlertWindow("Guardar DI", "Escribe un nombre para este archivo DI:", juce::MessageBoxIconType::QuestionIcon);
        w->addTextEditor("name", "DI_Nuevo", "Nombre");
        w->addButton("Guardar", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Descartar", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int result) {
            if (result == 1)
            {
                juce::String fileName = w->getTextEditorContents("name");
                if (!fileName.endsWithIgnoreCase(".wav"))
                    fileName += ".wav";
                    
                setRecordingState(false);
                if (onRecordToggled) onRecordToggled(false, fileName);
            }
            else
            {
                setRecordingState(false);
                if (onRecordToggled) onRecordToggled(false, "");
            }
            refreshList();
        }), true);
    }
    else
    {
        if (countdownTicks > 0) return; // Already preparing
        
        // START directly
        countdownTicks = 3 * 30; // 3 seconds countdown
        recordingSeconds = 0;
        isClosingRecording = false;
        
        cancelButton.setVisible(true);
    }
}

void GuitarDIView::timerCallback()
{
    if (getDiRms)
        currentRms = getDiRms();

    if (isClosingRecording)
    {
        countdownTicks--;
        if (countdownTicks <= 0)
        {
            isClosingRecording = false;
            refreshList();
        }
    }
    else if (countdownTicks > 0)
    {
        countdownTicks--;
        if (countdownTicks <= 0)
        {
            setRecordingState(true);
            recordingSeconds = 0;
            if (onRecordToggled)
                onRecordToggled(true, pendingFileName);
        }
    }
    else if (isRecording)
    {
        recordingSeconds++;
        
        // Auto-stop at 1 minute
        if (recordingSeconds >= 60 * 30) // 30 ticks per second
        {
            promptForRecording(); // This will trigger the stop logic
        }
    }
    
    repaint();
}

bool GuitarDIView::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        promptForRecording();
        return true;
    }
    return false;
}

void GuitarDIView::setRecordingState(bool recording)
{
    isRecording = recording;
    if (isRecording)
    {
        recordButton.setButtonText("Detener Grabacion");
        recordButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
        cancelButton.setVisible(true);
    }
    else
    {
        recordButton.setButtonText("Grabar Nuevo DI");
        recordButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red.withAlpha(0.6f));
        cancelButton.setVisible(false);
    }
    repaint();
}
