#include "AnalyzeView.h"
#include "../../PluginEditor.h"

AnalyzeView::AnalyzeView(MondoEqRefAudioProcessor &p) : processor(p) {
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
  trackSelector.addItem("Guitar DI", 2);
  trackSelector.addItem("Stem", 3);
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
  playStopButton.onClick = [this] { setIsPlaying(!isPlaying); };

  addAndMakeVisible(saveRefButton);
  saveRefButton.onClick = [this] { saveStemReference(); };

  addAndMakeVisible(normalizeButton);
  if (auto* editor = dynamic_cast<MondoEqRefAudioProcessorEditor*>(processorEditor.get())) {
      normalizeButton.setButtonText(juce::String("Norm (") + juce::String(editor->getCurrentTargetLufs(), 1) + ")");
      editor->onTargetLufsChanged = [this](float lufs) {
          normalizeButton.setButtonText(juce::String("Norm (") + juce::String(lufs, 1) + ")");
      };
  } else {
      normalizeButton.setButtonText("Normalize");
  }

  normalizeButton.onClick = [this] {
    if (onNormalizeRequested) {
      float measuredLufs = processor.getIntegratedLufs();
      if (measuredLufs < -90.0f) {
          juce::AlertWindow::showMessageBoxAsync(
              juce::AlertWindow::WarningIcon,
              "Medición requerida",
              "Por favor reproduce el audio primero para que el medidor registre el LUFS Integrado a utilizar como referencia."
          );
          return;
      }
      
      if (auto* editor = dynamic_cast<MondoEqRefAudioProcessorEditor*>(processorEditor.get())) {
        onNormalizeRequested(editor->getCurrentTargetLufs(), measuredLufs);
      }
    }
  };

  setWantsKeyboardFocus(true);
}

AnalyzeView::~AnalyzeView() { processorEditor = nullptr; }

void AnalyzeView::updatePlayStopButton() {
  if (isPlaying) {
    playStopButton.setButtonText(
        juce::String::fromUTF8("\xe2\x96\xa0")); // Stop icon ■
    playStopButton.setColour(juce::TextButton::buttonColourId,
                             juce::Colours::darkred);
    playStopButton.setColour(juce::TextButton::textColourOffId,
                             juce::Colours::white);
  } else {
    playStopButton.setButtonText(
        juce::String::fromUTF8("\xe2\x96\xb6")); // Play icon ▶
    playStopButton.setColour(juce::TextButton::buttonColourId,
                             juce::Colours::darkgreen);
    playStopButton.setColour(juce::TextButton::textColourOffId,
                             juce::Colours::white);
  }
}

void AnalyzeView::setIsPlaying(bool shouldPlay) {
  if (isPlaying != shouldPlay) {
    isPlaying = shouldPlay;
    updatePlayStopButton();
    if (onPlayStateChanged)
      onPlayStateChanged(isPlaying);
  }
}

int AnalyzeView::getTrackMode() const { return trackSelector.getSelectedId(); }

void AnalyzeView::setTrackMode(int modeId) {
  trackSelector.setSelectedId(modeId, juce::sendNotificationSync);
  normalizeButton.setVisible(modeId == 3); // Only visible for Stems
}

bool AnalyzeView::keyPressed(const juce::KeyPress &key) {
  if (key.isKeyCode(juce::KeyPress::spaceKey)) {
    setIsPlaying(!isPlaying);
    return true;
  }
  return false;
}

void AnalyzeView::setLoadedFileName(const juce::String &name) {
  loadedFileNameLabel.setText(name, juce::dontSendNotification);
  if (auto* editor = dynamic_cast<MondoEqRefAudioProcessorEditor*>(processorEditor.get())) {
    editor->triggerReset();
  }
}

void AnalyzeView::setCurrentStemFile(const juce::File& file) {
  currentStemFile = file;
  if (auto* editor = dynamic_cast<MondoEqRefAudioProcessorEditor*>(processorEditor.get())) {
    editor->setStemDirectory(file.getParentDirectory());
  }
}

void AnalyzeView::paint(juce::Graphics &g) {
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

  auto bounds = getLocalBounds();
  auto topBar = bounds.removeFromTop(40);
  g.setColour(juce::Colours::black.withAlpha(0.2f));
  g.fillRect(topBar);
}

void AnalyzeView::resized() {
  auto bounds = getLocalBounds();
  auto topBar = bounds.removeFromTop(40);

  // Left alignment
  auto trackArea = topBar.removeFromLeft(500).reduced(5);
  trackLabel.setBounds(trackArea.removeFromLeft(50));
  trackSelector.setBounds(trackArea.removeFromLeft(150));
  loadedFileNameLabel.setBounds(trackArea.removeFromLeft(280));

  // Right alignment for Play/Stop, Save Ref, and Normalize
  auto rightArea = topBar.removeFromRight(300).reduced(5);
  playStopButton.setBounds(rightArea.removeFromRight(40));
  rightArea.removeFromRight(5); // spacing
  saveRefButton.setBounds(rightArea.removeFromRight(80));
  rightArea.removeFromRight(5); // spacing
  normalizeButton.setBounds(rightArea.removeFromRight(120));

  if (processorEditor)
    processorEditor->setBounds(bounds);

  debugLabel.setBounds(10, 80, 600, 20);
}

void AnalyzeView::saveStemReference() {
  if (currentStemFile == juce::File()) return; // No stem loaded
  
  if (auto* editor = dynamic_cast<MondoEqRefAudioProcessorEditor*>(processorEditor.get())) {
    auto curve = editor->getRepresentativeCurve();
    if (curve.empty()) return;

    juce::var curveArray;
    for (float v : curve) {
      curveArray.append(v);
    }

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("name", currentStemFile.getFileNameWithoutExtension());
    obj->setProperty("representativeCurve", curveArray);

    juce::var mainVar(obj.get());
    juce::String jsonString = juce::JSON::toString(mainVar);

    juce::File jsonFile = currentStemFile.getSiblingFile(currentStemFile.getFileNameWithoutExtension() + ".json");
    
    // Si el archivo ya existe y quieres combinarlo, tendrías que leerlo.
    // Por ahora, como es 1 archivo por stem, lo sobreescribimos.
    jsonFile.replaceWithText(jsonString);
  }
}
