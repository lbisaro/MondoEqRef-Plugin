#pragma once

#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

class MondoEqRefAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer,
                                       public juce::TooltipClient {
public:
  MondoEqRefAudioProcessorEditor(MondoEqRefAudioProcessor &);
  ~MondoEqRefAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

  std::vector<float> getRepresentativeCurve() const {
    return representativeCurve;
  }
  float getCurrentTargetLufs() const;
  std::function<void(float)> onTargetLufsChanged;
  void setStemDirectory(const juce::File &dir) {
    audioProcessor.stemDirectory = dir;
  }
  void triggerReset();

  // Mouse Events for Vertical Zoom/Pan
  void mouseWheelMove(const juce::MouseEvent &event,
                      const juce::MouseWheelDetails &wheel) override;
  void mouseDoubleClick(const juce::MouseEvent &event) override;
  void mouseDrag(const juce::MouseEvent &event) override;
  void mouseDown(const juce::MouseEvent &event) override;
  void mouseUp(const juce::MouseEvent &event) override;
  void mouseMove(const juce::MouseEvent &event) override;
  void mouseExit(const juce::MouseEvent &event) override;

  bool keyPressed(const juce::KeyPress &key) override;
  juce::String getTooltip() override;

private:
  juce::SharedResourcePointer<juce::TooltipWindow> tooltipWindow;

  void timerCallback() override;
  void updateFftSize();
  void pushNextSampleIntoFifo(float sample) noexcept;
  void drawNextFrameOfSpectrum();
  void drawFrame(juce::Graphics &g);
  void loadTargets();
  void loadSettings();
  void targetRoleChanged();

  struct DynamicsBand {
    float hz_min = 0.0f;
    float hz_max = 0.0f;
    float db_min = 0.0f;
    float db_max = 0.0f;
  };

  struct AppSettings {
    float autoResetThreshold = -100.0f;
    bool autoResetEnabled = true;
    int autoResetWaitMs = 2000;
    DynamicsBand bandLow{80.0f, 300.0f, 10.0f, 14.0f};
    DynamicsBand bandMid{300.0f, 2000.0f, 10.0f, 15.0f};
    DynamicsBand bandHigh{2000.0f, 5000.0f, 12.0f, 18.0f};
  };
  AppSettings appSettings;

  struct PresetPoint {
    float f;
    float target;
    float maxLimit;
    float minLimit;
  };
  struct PresetBand {
    float minFreq;
    float maxFreq;
    juce::String name;
    juce::String tip;
    juce::Colour color;
  };
  struct PresetTarget {
    juce::String name;
    float targetLufs = -18.0f;
    std::vector<PresetBand> bands;
    std::vector<PresetPoint> points;
  };

  std::vector<PresetTarget> presets;
  int currentPresetIndex = 0;

  MondoEqRefAudioProcessor &audioProcessor;

  juce::ComboBox fftSizeBox;
  juce::Label fftSizeLabel;

  juce::ComboBox targetRoleBox;
  juce::Label targetRoleLabel;
  juce::TextButton reloadTargetsButton;

  juce::TextButton resetButton;
  juce::ToggleButton autoResetButton{"Auto"};
  juce::ToggleButton tiltButton{"Tilt dB"};
  juce::ToggleButton smoothButton{"Smooth"};
  juce::Slider targetOffsetSlider{juce::Slider::LinearVertical,
                                  juce::Slider::TextBoxBelow};

  uint32_t lastSignalTime = 0;

  bool isTiltEnabled = true;
  bool isSmoothingEnabled = true;
  float currentTargetOffset = 0.0f;

  int currentFftOrder = 11; // 2^11 = 2048
  int currentFftSize = 2048;
  std::unique_ptr<juce::dsp::FFT> forwardFFT;
  std::unique_ptr<juce::dsp::WindowingFunction<float>> windowHann;
  std::unique_ptr<juce::dsp::WindowingFunction<float>> windowBH;

  std::vector<float> fifo;
  std::vector<float> fftData;
  int fifoIndex = 0;
  bool nextFFTBlockReady = false;
  std::vector<float> scopeData;           // For BH
  std::vector<float> representativeCurve; // For BH (Average)
  std::vector<float> sumCurve;            // For BH
  std::vector<float> maxPeakCurve;        // For BH (True Peak Hold)

  std::vector<float> scopeDataHann;
  std::vector<float> representativeCurveHann;
  std::vector<float> sumCurveHann;
  int validFrameCount = 0;
  bool isStemRefActive = false;
  std::vector<float> stemRefAvgCurve;
  std::vector<float> stemRefMinCurve;
  std::vector<float> stemRefMaxCurve;
  std::unique_ptr<juce::FileChooser> fileChooser;

  struct ActiveTooltip {
    juce::Rectangle<float> rect;
    juce::String text;
  };
  std::vector<ActiveTooltip> activeBandTooltips;

  // View state for Vertical Zoom
  float minDecibels = -100.0f;
  float maxDecibels = 0.0f;
  float dragStartY = 0.0f;
  float dragStartMinDB = 0.0f;
  float dragStartMaxDB = 0.0f;

  // Mouse Tracking & Measurement
  juce::Point<int> mousePos;
  bool isMouseOverPlot = false;
  bool isMeasuring = false;
  bool isDragging = false;
  juce::Point<int> measurePointA;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MondoEqRefAudioProcessorEditor)
};
