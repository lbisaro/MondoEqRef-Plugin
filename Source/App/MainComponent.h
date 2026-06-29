#pragma once

#include "../PluginProcessor.h"
#include "DeviceRouting.h"
#include "Views/AnalyzeView.h"
#include "Views/CustomAudioSettings.h"
#include "Views/GuitarDIView.h"
#include "Views/StemsView.h"
#include "Views/SpectralRefView.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

class MainComponent : public juce::Component,
                      public juce::AudioSource,
                      public juce::ChangeListener,
                      public juce::Timer {
public:
  MainComponent();
  ~MainComponent() override;

  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void
  getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;
  void releaseResources() override;

  void paint(juce::Graphics &g) override;
  void resized() override;

  void changeListenerCallback(juce::ChangeBroadcaster *source) override;
  void timerCallback() override;

private:
  void updateDeviceProfileFromManager();
  void applyProfileToDeviceManager(const DeviceAudioProfile &profile);
  void showAudioSettings();
  void populateChannelSelectors();

  MondoEqRefAudioProcessor processor;

  DeviceRoutingManager routingManager;
  juce::AudioDeviceManager deviceManager;
  juce::AudioSourcePlayer audioSourcePlayer;

  // Stem playback
  juce::AudioFormatManager formatManager; // Stems & DI Playback
  juce::CriticalSection audioLock;
  std::atomic<float> currentDiRms{0.0f};

  std::unique_ptr<juce::AudioFormatReaderSource> stemReaderSource;
  std::unique_ptr<juce::AudioTransportSource> stemTransportSource;

  std::unique_ptr<juce::AudioFormatReaderSource> diReaderSource;
  std::unique_ptr<juce::AudioTransportSource> diTransportSource;

  // DI Recording in RAM
  std::atomic<bool> isRecordingDI { false };
  juce::AudioBuffer<float> diRecordBuffer;
  std::atomic<int> diRecordSampleCount { 0 };
  double currentSampleRate = 44100.0;

  void loadStemFile(const juce::File &file);
  void loadDiFile(const juce::File &file);

  float getCurrentDiRms() const { return currentDiRms.load(); }

  void startRecordingDI();
  void stopRecordingDI(const juce::String& fileName);

  // UI elements for Top Bar
  juce::TextButton settingsButton{"Audio Settings"};
  juce::Label deviceStatusLabel;

  // Custom Navigation Menu
  juce::TextButton navAnalyzeButton{"Analyze"};
  juce::TextButton navGuitarDIButton{"Guitar DI"};
  juce::TextButton navStemsButton{"Stems"};
  juce::TextButton navSpectralRefButton{"Spectral Ref"};

  // Views
  AnalyzeView analyzeView{processor};
  GuitarDIView guitarDIView;
  StemsView stemsView;
  SpectralRefView spectralRefView;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
