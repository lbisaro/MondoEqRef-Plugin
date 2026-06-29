#include "MondoSpectralRefEditor.h"

MondoSpectralRefEditor::MondoSpectralRefEditor (MondoSpectralRefAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (800, 600);
    
    addAndMakeVisible(normalizeButton);
    normalizeButton.onClick = [this] { audioProcessor.triggerNormalize = true; };
    
    addAndMakeVisible(zoomHzButton);
    zoomHzButton.onClick = [this] { isZoomedHz = !isZoomedHz; repaint(); };
    
    startTimerHz(30);
}

MondoSpectralRefEditor::~MondoSpectralRefEditor() {}

void MondoSpectralRefEditor::timerCallback()
{
    audioProcessor.calculateTransferFunction();
    
    if (audioProcessor.isNormalized.load()) {
        normalizeButton.setColour(juce::TextButton::buttonColourId, juce::Colours::orange.withAlpha(0.6f));
    } else {
        normalizeButton.removeColour(juce::TextButton::buttonColourId);
    }
    
    if (isZoomedHz) {
        zoomHzButton.setColour(juce::TextButton::buttonColourId, juce::Colours::orange.withAlpha(0.6f));
    } else {
        zoomHzButton.removeColour(juce::TextButton::buttonColourId);
    }
    
    repaint();
}

void MondoSpectralRefEditor::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour::fromString("ff252526")); // Dark background

  auto fullArea = getLocalBounds();
  auto plotArea = fullArea;
  plotArea.removeFromTop(40);    // header
  plotArea.removeFromRight(40);  // dB scale
  plotArea.removeFromBottom(20); // Freq scale
  plotArea.removeFromLeft(40);

  float left = (float)plotArea.getX();
  float bottom = (float)plotArea.getBottom();
  float width = (float)plotArea.getWidth();
  float height = (float)plotArea.getHeight();

  float minFreq = isZoomedHz ? 50.0f : 20.0f;
  float maxFreq = isZoomedHz ? 10000.0f : 20000.0f;
  float minLogFreq = std::log10(minFreq);
  float maxLogFreq = std::log10(maxFreq);

  float skewFactor = 1.5f;

  // Draw Grids
  g.setColour(juce::Colours::white.withAlpha(0.1f));

  // Frequency grid
  std::array<float, 9> freqs = {20.0f,   50.0f,   100.0f,  200.0f,  500.0f,
                                1000.0f, 2000.0f, 5000.0f, 10000.0f};
  for (float f : freqs) {
    if (f < minFreq || f > maxFreq) continue;
    float normX = (std::log10(f) - minLogFreq) / (maxLogFreq - minLogFreq);
    float x = left + width * std::pow(normX, skewFactor);
    g.drawVerticalLine(juce::roundToInt(x), (float)plotArea.getY(), bottom);

    g.setColour(juce::Colours::white.withAlpha(0.5f));
    juce::String text =
        f >= 1000.0f ? juce::String(f / 1000.0f, 0) + "k" : juce::String(f, 0);
    g.drawText(text, (int)x - 20, (int)bottom + 2, 40, 15,
               juce::Justification::centredTop, false);
    g.setColour(juce::Colours::white.withAlpha(0.1f));
  }

  // Custom 300Hz and 2000Hz soft guidelines
  auto drawCrossoverLine = [&](float f) {
    float normX = (std::log10(f) - minLogFreq) / (maxLogFreq - minLogFreq);
    float x = left + width * std::pow(normX, skewFactor);
    g.setColour(juce::Colours::yellow.withAlpha(0.3f));
    const float dashes[] = {4.0f, 4.0f};
    g.drawDashedLine(juce::Line<float>(x, (float)plotArea.getY(), x, bottom),
                     dashes, 2, 1.5f);
  };
  if (300.0f >= minFreq && 300.0f <= maxFreq) drawCrossoverLine(300.0f);
  if (2000.0f >= minFreq && 2000.0f <= maxFreq) drawCrossoverLine(2000.0f);

  // dB grid
  for (float db = -120.0f; db <= 48.0f; db += 6.0f) {
    if (db < minDecibels || db > maxDecibels)
      continue;
    float level = juce::jmap(db, minDecibels, maxDecibels, 0.0f, 1.0f);
    float y = bottom - height * level;

    g.drawHorizontalLine(juce::roundToInt(y), left, (float)plotArea.getRight());

    g.setColour(juce::Colours::white.withAlpha(0.5f));
    juce::String dbText =
        db > 0.0f ? "+" + juce::String(db, 0) : juce::String(db, 0);
    g.drawText(dbText, (int)plotArea.getRight() + 5, (int)y - 10, 30, 20,
               juce::Justification::centredLeft, false);
    g.setColour(juce::Colours::white.withAlpha(0.1f));
  }

  // Map curve
  std::vector<float> pixelLiveMax(juce::roundToInt(width) + 1, -100.0f);

  const auto &mags = audioProcessor.displayMagnitudes;
  int numBins = mags.size();
  float binFreq = (float)audioProcessor.getSampleRate() / (float)(numBins * 2);

  for (int i = 1; i < numBins; ++i) {
    float freq = binFreq * (float)i;
    if (freq < minFreq)
      continue;
    if (freq > maxFreq)
      break;

    float normFreq =
        (std::log10(freq) - minLogFreq) / (maxLogFreq - minLogFreq);
    float xPos = width * std::pow(normFreq, skewFactor);
    int xPixel =
        juce::jlimit(0, juce::roundToInt(width), juce::roundToInt(xPos));

    float mag = mags[i];
    float dbLevel = mag > 1e-5f ? juce::Decibels::gainToDecibels(mag) : -100.0f;

    pixelLiveMax[xPixel] = juce::jmax(pixelLiveMax[xPixel], dbLevel);
  }

  // Fill empty pixels with linear interpolation
  int lastValid = -1;
  for (int x = 0; x <= juce::roundToInt(width); ++x) {
    if (pixelLiveMax[x] > -99.9f) {
      if (lastValid != -1 && x - lastValid > 1) {
        float startVal = pixelLiveMax[lastValid];
        float endVal = pixelLiveMax[x];
        for (int j = lastValid + 1; j < x; ++j) {
          float t = (float)(j - lastValid) / (x - lastValid);
          pixelLiveMax[j] = startVal + t * (endVal - startVal);
        }
      }
      lastValid = x;
    }
  }
  if (lastValid != -1 && lastValid < juce::roundToInt(width)) {
    for (int j = lastValid + 1; j <= juce::roundToInt(width); ++j)
      pixelLiveMax[j] = pixelLiveMax[lastValid];
  }
  int firstValid = 0;
  while (firstValid <= juce::roundToInt(width) &&
         pixelLiveMax[firstValid] <= -99.9f)
    firstValid++;
  if (firstValid <= juce::roundToInt(width) && firstValid > 0) {
    for (int j = 0; j < firstValid; ++j)
      pixelLiveMax[j] = pixelLiveMax[firstValid];
  }

  // Visual smoothing pass (Moving Average) to mimic Fractional Octave Smoothing
  std::vector<float> temp = pixelLiveMax;
  int radius = 5;
  for (int x = 0; x <= juce::roundToInt(width); ++x) {
    float sum = 0.0f;
    int count = 0;
    for (int k = -radius; k <= radius; ++k) {
      int idx = juce::jlimit(0, juce::roundToInt(width), x + k);
      sum += temp[idx];
      count++;
    }
    pixelLiveMax[x] = sum / count;
  }

  // Draw Curve
  juce::Path spectrumPath;
  bool first = true;

  for (int x = 0; x <= juce::roundToInt(width); ++x) {
    float dbLevel = pixelLiveMax[x];
    float level = juce::jmap(juce::jlimit(minDecibels, maxDecibels, dbLevel),
                             minDecibels, maxDecibels, 0.0f, 1.0f);
    float y = bottom - level * height;

    if (first) {
      spectrumPath.startNewSubPath(left + x, y);
      first = false;
    } else {
      spectrumPath.lineTo(left + x, y);
    }
  }

  if (audioProcessor.isNormalized.load()) {
    g.setColour(juce::Colours::orange.withAlpha(0.9f));
  } else {
    g.setColour(juce::Colours::cyan.withAlpha(0.8f));
  }
  g.strokePath(spectrumPath, juce::PathStrokeType(1.5f));

  // Mouse Crosshair & Measurement
  if (isMouseOverPlot && plotArea.contains(mousePos)) {
    float mouseX = (float)mousePos.x;
    float mouseY = (float)mousePos.y;

    float normX = (mouseX - left) / width;
    float inverseNormX = std::pow(normX, 1.0f / skewFactor);
    float freq =
        std::pow(10.0f, minLogFreq + inverseNormX * (maxLogFreq - minLogFreq));

    float normY = (bottom - mouseY) / height;
    float db = juce::jmap(normY, 0.0f, 1.0f, minDecibels, maxDecibels);

    g.setColour(juce::Colours::white.withAlpha(0.3f));
    const float dashes[] = {4.0f, 4.0f};
    g.drawDashedLine(
        juce::Line<float>(mouseX, (float)plotArea.getY(), mouseX, bottom),
        dashes, 2, 1.0f);
    g.drawDashedLine(
        juce::Line<float>(left, mouseY, (float)plotArea.getRight(), mouseY),
        dashes, 2, 1.0f);

    juce::String textFreq = freq >= 1000.0f
                                ? juce::String(freq / 1000.0f, 2) + " kHz"
                                : juce::String(freq, 1) + " Hz";
    juce::String text = textFreq + " | " + juce::String(db, 1) + " dB";

    if (isMeasuring) {
      float ax = (float)measurePointA.x;
      float ay = (float)measurePointA.y;

      g.setColour(juce::Colours::orange);
      g.drawEllipse(ax - 3, ay - 3, 6, 6, 2.0f);

      float normYA = (bottom - ay) / height;
      float dbA = juce::jmap(normYA, 0.0f, 1.0f, minDecibels, maxDecibels);

      float diffDb = db - dbA;
      text += " (" + juce::String(diffDb > 0 ? "+" : "") +
              juce::String(diffDb, 1) + " dB)";

      g.drawLine(ax, ay, mouseX, mouseY, 1.5f);
    }

    g.setFont(14.0f);
    int textWidth = g.getCurrentFont().getStringWidth(text) + 16;
    int textX = plotArea.getRight() - textWidth;
    int textY = plotArea.getY() + 4;

    g.setColour(juce::Colour::fromString("ff252526").withAlpha(0.9f));
    g.fillRect(textX, textY, textWidth, 22);

    g.setColour(juce::Colours::orange);
    g.setFont(12.0f);
    g.drawText(text, textX, textY, textWidth, 22, juce::Justification::centred,
               false);
  }
}

void MondoSpectralRefEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(40);
    int center = header.getWidth() / 2;
    normalizeButton.setBounds(center - 125, 8, 120, 24);
    zoomHzButton.setBounds(center + 5, 8, 120, 24);
}

// --- Mouse Events ---

void MondoSpectralRefEditor::mouseWheelMove(
    const juce::MouseEvent &event, const juce::MouseWheelDetails &wheel) {
  float zoomFactor = wheel.deltaY > 0 ? 1.1f : 0.9f;
  float range = maxDecibels - minDecibels;
  float newRange = range / zoomFactor;
  float diff = range - newRange;

  minDecibels += diff / 2.0f;
  maxDecibels -= diff / 2.0f;

  minDecibels = juce::jmax(minDecibels, -144.0f);
  maxDecibels = juce::jmin(maxDecibels, 48.0f);
  if (maxDecibels - minDecibels < 10.0f)
    maxDecibels = minDecibels + 10.0f;
}

void MondoSpectralRefEditor::mouseDoubleClick(const juce::MouseEvent &event) {
  auto fullArea = getLocalBounds();
  auto plotArea = fullArea;
  plotArea.removeFromTop(40);
  plotArea.removeFromRight(40);
  plotArea.removeFromBottom(20);
  plotArea.removeFromLeft(40);

  if (plotArea.contains(event.getPosition())) {
    minDecibels = -24.0f;
    maxDecibels = 24.0f;
    repaint();
  }
}

void MondoSpectralRefEditor::mouseDown(const juce::MouseEvent &event) {
  auto fullArea = getLocalBounds();
  auto plotArea = fullArea;
  plotArea.removeFromTop(40);
  plotArea.removeFromRight(40);
  plotArea.removeFromBottom(20);
  plotArea.removeFromLeft(40);

  if (!plotArea.contains(event.getPosition()))
    return;

  dragStartY = (float)event.getMouseDownY();
  dragStartMinDB = minDecibels;
  dragStartMaxDB = maxDecibels;
  isDragging = false;
}

void MondoSpectralRefEditor::mouseDrag(const juce::MouseEvent &event) {
  auto fullArea = getLocalBounds();
  auto plotArea = fullArea;
  plotArea.removeFromTop(40);
  plotArea.removeFromRight(40);
  plotArea.removeFromBottom(20);
  plotArea.removeFromLeft(40);

  isDragging = true;

  float dy = (float)event.getDistanceFromDragStartY();
  float height = (float)plotArea.getHeight();

  float range = dragStartMaxDB - dragStartMinDB;
  float offsetDB = (dy / height) * range;

  minDecibels = dragStartMinDB + offsetDB;
  maxDecibels = dragStartMaxDB + offsetDB;

  if (maxDecibels > 48.0f) {
    float diff = maxDecibels - 48.0f;
    maxDecibels -= diff;
    minDecibels -= diff;
  }
  if (minDecibels < -144.0f) {
    float diff = -144.0f - minDecibels;
    minDecibels += diff;
    maxDecibels += diff;
  }
}

void MondoSpectralRefEditor::mouseUp(const juce::MouseEvent &event) {
  auto fullArea = getLocalBounds();
  auto plotArea = fullArea;
  plotArea.removeFromTop(40);
  plotArea.removeFromRight(40);
  plotArea.removeFromBottom(20);
  plotArea.removeFromLeft(40);

  if (!isDragging && plotArea.contains(event.getPosition())) {
    if (!isMeasuring) {
      isMeasuring = true;
      measurePointA = event.getPosition();
    } else {
      isMeasuring = false;
    }
    repaint();
  }
}

void MondoSpectralRefEditor::mouseMove(const juce::MouseEvent &event) {
  auto fullArea = getLocalBounds();
  auto plotArea = fullArea;
  plotArea.removeFromTop(40);
  plotArea.removeFromRight(40);
  plotArea.removeFromBottom(20);
  plotArea.removeFromLeft(40);

  if (plotArea.contains(event.getPosition())) {
    isMouseOverPlot = true;
    mousePos = event.getPosition();
    repaint();
  } else if (isMouseOverPlot) {
    isMouseOverPlot = false;
    repaint();
  }
}

void MondoSpectralRefEditor::mouseExit(const juce::MouseEvent &event) {
  juce::ignoreUnused(event);
  if (isMouseOverPlot) {
    isMouseOverPlot = false;
    repaint();
  }
}

juce::String MondoSpectralRefEditor::getTooltip() { return ""; }
