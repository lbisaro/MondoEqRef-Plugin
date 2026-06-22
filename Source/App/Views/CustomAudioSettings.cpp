#include "CustomAudioSettings.h"

CustomAudioSettingsComponent::CustomAudioSettingsComponent(
    juce::AudioDeviceManager &manager, DeviceRoutingManager &routing)
    : deviceManager(manager), routingManager(routing) {
  nativeSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
      deviceManager, 0, 16, 0, 16, true, true, true, false);
  addAndMakeVisible(nativeSelector.get());

  addAndMakeVisible(processedLabel);
  processedLabel.setText("Processed Input:", juce::dontSendNotification);
  processedLabel.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(processedCombo);

  addAndMakeVisible(diLabel);
  diLabel.setText("Guitar DI Input:", juce::dontSendNotification);
  diLabel.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(diCombo);

  addAndMakeVisible(outputLabel);
  outputLabel.setText("Output:", juce::dontSendNotification);
  outputLabel.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(outputCombo);

  auto saveLambda = [this]() { saveLogicalRouting(); };
  processedCombo.onChange = saveLambda;
  diCombo.onChange = saveLambda;
  outputCombo.onChange = saveLambda;

  deviceManager.addChangeListener(this);
  populateCombos();
}

CustomAudioSettingsComponent::~CustomAudioSettingsComponent() {
  deviceManager.removeChangeListener(this);
}

void CustomAudioSettingsComponent::changeListenerCallback(
    juce::ChangeBroadcaster *source) {
  if (source == &deviceManager) {
    populateCombos();
    saveLogicalRouting();
  }
}

void CustomAudioSettingsComponent::populateCombos() {
  auto *device = deviceManager.getCurrentAudioDevice();
  if (device == nullptr) {
    processedCombo.clear();
    diCombo.clear();
    outputCombo.clear();
    currentDeviceName = "";
    return;
  }

  juce::String newDeviceName = device->getName();
  bool deviceChanged = (newDeviceName != currentDeviceName);
  currentDeviceName = newDeviceName;

  // Preserve selections if not changed
  int pSel = processedCombo.getSelectedId();
  int dSel = diCombo.getSelectedId();
  int oSel = outputCombo.getSelectedId();

  processedCombo.clear(juce::dontSendNotification);
  diCombo.clear(juce::dontSendNotification);
  outputCombo.clear(juce::dontSendNotification);

  auto activeOutputs = device->getActiveOutputChannels();
  auto outputNames = device->getOutputChannelNames();

  auto activeInputs = device->getActiveInputChannels();
  auto inputNames = device->getInputChannelNames();

  for (int i = 0; i < inputNames.size(); ++i) {
    juce::String suffix = activeInputs[i] ? "" : " (Inactivo)";
    processedCombo.addItem(inputNames[i].replace("Input", "Output", true) + suffix, i + 1);
  }

  for (int i = 0; i < outputNames.size(); ++i) {
    juce::String suffix = activeOutputs[i] ? "" : " (Inactivo)";
    diCombo.addItem(outputNames[i].replace("Output", "Input", true) + suffix, i + 1);
    outputCombo.addItem(outputNames[i].replace("Output", "Input", true) + suffix, i + 1);
  }

  // Load from routing manager if device changed, else restore previous
  DeviceAudioProfile profile = routingManager.getProfile(currentDeviceName);

  if (deviceChanged) {
    pSel = profile.processedInputChannel + 1;
    dSel = profile.diInputChannel + 1;
    oSel = profile.outputChannel + 1;
  }

  // Ensure selection is valid (exists in the combo)
  if (processedCombo.indexOfItemId(pSel) >= 0)
    processedCombo.setSelectedId(pSel, juce::dontSendNotification);
  else if (processedCombo.getNumItems() > 0)
    processedCombo.setSelectedId(processedCombo.getItemId(0),
                                 juce::dontSendNotification);

  if (diCombo.indexOfItemId(dSel) >= 0)
    diCombo.setSelectedId(dSel, juce::dontSendNotification);
  else if (diCombo.getNumItems() > 0)
    diCombo.setSelectedId(diCombo.getItemId(0), juce::dontSendNotification);

  if (outputCombo.indexOfItemId(oSel) >= 0)
    outputCombo.setSelectedId(oSel, juce::dontSendNotification);
  else if (outputCombo.getNumItems() > 0)
    outputCombo.setSelectedId(outputCombo.getItemId(0),
                              juce::dontSendNotification);

  saveLogicalRouting(); // Ensure routing manager is in sync with valid channels
}

void CustomAudioSettingsComponent::saveLogicalRouting() {
  if (currentDeviceName.isEmpty())
    return;

  DeviceAudioProfile profile = routingManager.getProfile(currentDeviceName);
  profile.deviceName = currentDeviceName;

  if (auto *device = deviceManager.getCurrentAudioDevice()) {
    profile.sampleRate = device->getCurrentSampleRate();
    profile.bufferSize = device->getCurrentBufferSizeSamples();

    profile.activeInputChannels = "";
    auto activeIns = device->getActiveInputChannels();
    for (int i = 0; i < device->getInputChannelNames().size(); ++i)
        profile.activeInputChannels << (activeIns[i] ? "1" : "0");
        
    profile.activeOutputChannels = "";
    auto activeOuts = device->getActiveOutputChannels();
    for (int i = 0; i < device->getOutputChannelNames().size(); ++i)
        profile.activeOutputChannels << (activeOuts[i] ? "1" : "0");
  }

  // Save MIDI state
  profile.activeMidiInputs.clear();
  for (const auto &dev : juce::MidiInput::getAvailableDevices()) {
    if (deviceManager.isMidiInputDeviceEnabled(dev.identifier))
      profile.activeMidiInputs.add(dev.identifier);
  }

  profile.defaultMidiOutput = deviceManager.getDefaultMidiOutputIdentifier();

  // ComboBox IDs are 1-based, we want 0-based for channel index
  if (processedCombo.getSelectedId() > 0)
    profile.processedInputChannel = processedCombo.getSelectedId() - 1;

  if (diCombo.getSelectedId() > 0)
    profile.diInputChannel = diCombo.getSelectedId() - 1;

  if (outputCombo.getSelectedId() > 0)
    profile.outputChannel = outputCombo.getSelectedId() - 1;

  routingManager.addOrUpdateProfile(profile);
}

void CustomAudioSettingsComponent::paint(juce::Graphics &g) {
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void CustomAudioSettingsComponent::resized() {
  auto bounds = getLocalBounds();

  // Bottom area for custom routing
  auto bottomArea = bounds.removeFromBottom(120);

  if (nativeSelector)
    nativeSelector->setBounds(bounds);

  // Layout custom comboboxes
  bottomArea.reduce(20, 10);

  auto row1 = bottomArea.removeFromTop(24);
  processedLabel.setBounds(row1.removeFromLeft(120));
  processedCombo.setBounds(row1);

  bottomArea.removeFromTop(10); // spacing

  auto row2 = bottomArea.removeFromTop(24);
  diLabel.setBounds(row2.removeFromLeft(120));
  diCombo.setBounds(row2);

  bottomArea.removeFromTop(10); // spacing

  auto row3 = bottomArea.removeFromTop(24);
  outputLabel.setBounds(row3.removeFromLeft(120));
  outputCombo.setBounds(row3);
}
