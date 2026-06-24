#include "StemsView.h"
#include "../DeviceRouting.h"

// Componente para cada fila de archivo
class StemItemComponent : public juce::Component
{
public:
    StemItemComponent(const juce::File& f, std::function<void()> onDelete, std::function<void()> onLoad)
        : file(f), deleteCallback(onDelete), loadCallback(onLoad)
    {
        nameLabel.setText(file.getFileName(), juce::dontSendNotification);
        nameLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(nameLabel);

        deleteButton.setButtonText("Eliminar");
        deleteButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred.withAlpha(0.6f));
        deleteButton.onClick = [this] { if (deleteCallback) deleteCallback(); };
        addAndMakeVisible(deleteButton);

        loadButton.setButtonText("Cargar");
        loadButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen.withAlpha(0.6f));
        loadButton.onClick = [this] { if (loadCallback) loadCallback(); };
        addAndMakeVisible(loadButton);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(5);
        deleteButton.setBounds(bounds.removeFromLeft(80).reduced(2));
        loadButton.setBounds(bounds.removeFromLeft(80).reduced(2));
        nameLabel.setBounds(bounds);
    }

private:
    juce::File file;
    juce::Label nameLabel;
    juce::TextButton deleteButton;
    juce::TextButton loadButton;
    std::function<void()> deleteCallback;
    std::function<void()> loadCallback;
};

StemsView::StemsView()
{
    loadButton.onClick = [this] { importStemViaDialog(); };
    addAndMakeVisible(loadButton);

    statusLabel.setText("Stems Library", juce::dontSendNotification);
    statusLabel.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    addAndMakeVisible(statusLabel);

    viewport.setViewedComponent(&listContainer, false);
    addAndMakeVisible(viewport);

    refreshList();
}

StemsView::~StemsView()
{
}

void StemsView::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void StemsView::resized()
{
    auto bounds = getLocalBounds().reduced(20);
    
    auto topArea = bounds.removeFromTop(40);
    statusLabel.setBounds(topArea.removeFromLeft(300));
    
    auto buttonArea = topArea.removeFromRight(150);
    loadButton.setBounds(buttonArea.reduced(5));

    bounds.removeFromTop(10);
    viewport.setBounds(bounds);
    
    // Update container width to match viewport width
    int listWidth = viewport.getMaximumVisibleWidth();
    listContainer.setSize(listWidth, listContainer.getHeight());
    
    // Update widths of all list items
    for (int i = 0; i < stemItems.size(); ++i)
    {
        if (auto* item = stemItems[i])
            item->setBounds(0, i * 40, listWidth, 40);
    }
}

bool StemsView::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto file : files)
        if (file.endsWithIgnoreCase(".wav") || file.endsWithIgnoreCase(".mp3"))
            return true;
    return false;
}

void StemsView::filesDropped(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(x, y);
    juce::File stemsDir = DeviceRoutingManager::getStemsDirectory();
    bool anyCopied = false;

    for (auto path : files)
    {
        juce::File sourceFile(path);
        if (sourceFile.existsAsFile() && (sourceFile.hasFileExtension(".wav") || sourceFile.hasFileExtension(".mp3")))
        {
            juce::File destFile = stemsDir.getChildFile(sourceFile.getFileName());
            if (sourceFile.copyFileTo(destFile))
                anyCopied = true;
        }
    }

    if (anyCopied)
        refreshList();
}

void StemsView::importStemViaDialog()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Seleccionar archivo de Audio",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.mp3"
    );

    auto folderChooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser& chooser)
    {
        juce::File result = chooser.getResult();
        if (result.existsAsFile())
        {
            juce::File stemsDir = DeviceRoutingManager::getStemsDirectory();
            juce::File destFile = stemsDir.getChildFile(result.getFileName());
            if (result.copyFileTo(destFile))
            {
                refreshList();
            }
        }
    });
}

void StemsView::deleteStem(const juce::File& file)
{
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Eliminar Stem")
            .withMessage("¿Estás seguro de eliminar '" + file.getFileName() + "'?")
            .withButton("Eliminar").withButton("Cancelar"),
        [this, file](int result)
        {
            if (result == 1) // 1 = Eliminar
            {
                file.deleteFile();
                refreshList();
            }
        }
    );
}

void StemsView::refreshList()
{
    stemItems.clear();
    // listContainer children are automatically removed and deleted by OwnedArray
    
    juce::File stemsDir = DeviceRoutingManager::getStemsDirectory();
    juce::Array<juce::File> audioFiles = stemsDir.findChildFiles(juce::File::findFiles, false, "*.wav;*.mp3");

    int itemHeight = 40;
    int yOffset = 0;
    
    // Use a reasonable default width if not yet resized
    int listWidth = juce::jmax(400, viewport.getMaximumVisibleWidth());

    for (auto& file : audioFiles)
    {
        auto* item = new StemItemComponent(
            file,
            [this, file] { deleteStem(file); },
            [this, file] { if (onStemLoadRequested) onStemLoadRequested(file); }
        );

        stemItems.add(item);
        listContainer.addAndMakeVisible(item);
        item->setBounds(0, yOffset, listWidth, itemHeight);
        yOffset += itemHeight;
    }

    listContainer.setSize(listWidth, yOffset);
}
