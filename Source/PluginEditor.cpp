#include "PluginProcessor.h"
#include "PluginEditor.h"

MondoEqRefAudioProcessorEditor::MondoEqRefAudioProcessorEditor (MondoEqRefAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (800, 450);

    fftSizeLabel.setText("FFT Size:", juce::dontSendNotification);
    fftSizeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(fftSizeLabel);

    fftSizeBox.addItem("1024", 1);
    fftSizeBox.addItem("2048", 2);
    fftSizeBox.addItem("4096", 3);
    fftSizeBox.addItem("8192", 4);
    fftSizeBox.setSelectedId(2, juce::dontSendNotification);
    fftSizeBox.onChange = [this]() { updateFftSize(); };
    addAndMakeVisible(fftSizeBox);

    targetRoleLabel.setText("Target:", juce::dontSendNotification);
    targetRoleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(targetRoleLabel);

    targetRoleBox.onChange = [this]() { targetRoleChanged(); };
    addAndMakeVisible(targetRoleBox);

    resetButton.setButtonText("Reset Peak");
    resetButton.onClick = [this] {
        std::fill(representativeCurve.begin(), representativeCurve.end(), 0.0f);
        repaint();
    };
    addAndMakeVisible(resetButton);

    setResizable(true, true);

    updateFftSize();
    loadTargets();

    startTimerHz(60);
}

MondoEqRefAudioProcessorEditor::~MondoEqRefAudioProcessorEditor()
{
}

void MondoEqRefAudioProcessorEditor::loadTargets()
{
    presets.clear();
    targetRoleBox.clear();
    
    juce::File dataFile("C:/Users/lbisa/OneDrive/Documentos/soft/Guitarra/Plugins/MondoEqRef/Data/targets.json");

    targetRoleBox.addItem("None", 1);

    if (dataFile.existsAsFile())
    {
        auto parsedJson = juce::JSON::parse(dataFile);
        if (parsedJson.isObject())
        {
            auto* mainObj = parsedJson.getDynamicObject();
            if (mainObj->hasProperty("presets"))
            {
                auto presetsArray = mainObj->getProperty("presets");
                if (presetsArray.isArray())
                {
                    auto* arr = presetsArray.getArray();
                    int presetId = 2; // Start from 2 since 1 is "None"
                    for (auto& item : *arr)
                    {
                        if (item.isObject())
                        {
                            auto* presetObj = item.getDynamicObject();
                            PresetTarget target;
                            target.name = presetObj->getProperty("name").toString();
                            
                            auto bandsProp = presetObj->getProperty("bands");
                            if (bandsProp.isArray())
                            {
                                auto* bandsArr = bandsProp.getArray();
                                for (auto& bandItem : *bandsArr)
                                {
                                    if (bandItem.isObject())
                                    {
                                        auto* bObj = bandItem.getDynamicObject();
                                        PresetBand band;
                                        band.name = bObj->getProperty("name").toString();
                                        band.minFreq = float(bObj->getProperty("minFreq"));
                                        band.maxFreq = float(bObj->getProperty("maxFreq"));
                                        if (bObj->hasProperty("targetMin")) band.targetMin = (int)bObj->getProperty("targetMin"); else band.targetMin = 0;
                                        if (bObj->hasProperty("targetMax")) band.targetMax = (int)bObj->getProperty("targetMax"); else band.targetMax = 100;
                                        band.color = juce::Colour::fromString(bObj->getProperty("color").toString());
                                        target.bands.push_back(band);
                                    }
                                }
                            }
                            
                            presets.push_back(target);
                            targetRoleBox.addItem(target.name, presetId++);
                        }
                    }
                }
            }
        }
    }
    
    targetRoleBox.setSelectedId(1, juce::dontSendNotification);
    currentPresetIndex = 0; // 0 will mean "None"
}


void MondoEqRefAudioProcessorEditor::targetRoleChanged()
{
    currentPresetIndex = targetRoleBox.getSelectedId() - 2;
    repaint();
}

void MondoEqRefAudioProcessorEditor::updateFftSize()
{
    int selection = fftSizeBox.getSelectedId();
    switch (selection)
    {
        case 1: currentFftOrder = 10; break;
        case 2: currentFftOrder = 11; break;
        case 3: currentFftOrder = 12; break;
        case 4: currentFftOrder = 13; break;
        default: currentFftOrder = 11; break;
    }
    
    currentFftSize = 1 << currentFftOrder;
    
    forwardFFT = std::make_unique<juce::dsp::FFT>(currentFftOrder);
    window = std::make_unique<juce::dsp::WindowingFunction<float>>(currentFftSize, juce::dsp::WindowingFunction<float>::hann);
    
    fifo.assign(currentFftSize, 0.0f);
    fftData.assign(currentFftSize * 2, 0.0f);
    scopeData.assign(currentFftSize / 2, 0.0f);
    representativeCurve.assign(currentFftSize / 2, 0.0f);
    fifoIndex = 0;
}

void MondoEqRefAudioProcessorEditor::timerCallback()
{
    int procFifoCount = audioProcessor.fifoIndex.load();
    if (procFifoCount > 0)
    {
        for (int i = 0; i < procFifoCount; ++i)
        {
            pushNextSampleIntoFifo(audioProcessor.fifo[(size_t)i]);
        }
        audioProcessor.fifoIndex.store(0);
    }

    if (nextFFTBlockReady)
    {
        drawNextFrameOfSpectrum();
        nextFFTBlockReady = false;
        repaint();
    }
}

void MondoEqRefAudioProcessorEditor::pushNextSampleIntoFifo (float sample) noexcept
{
    if (fifoIndex == currentFftSize)
    {
        if (! nextFFTBlockReady)
        {
            std::fill(fftData.begin(), fftData.end(), 0.0f);
            std::copy(fifo.begin(), fifo.end(), fftData.begin());
            nextFFTBlockReady = true;
        }
        fifoIndex = 0;
    }
    fifo[(size_t)fifoIndex++] = sample;
}

void MondoEqRefAudioProcessorEditor::drawNextFrameOfSpectrum()
{
    window->multiplyWithWindowingTable (fftData.data(), (size_t) currentFftSize);
    forwardFFT->performFrequencyOnlyForwardTransform (fftData.data());

    for (int i = 0; i < currentFftSize / 2; ++i)
    {
        float magnitude = (fftData[i] * 4.0f) / (float)currentFftSize;
        
        // Smoothing in magnitude domain
        scopeData[i] = scopeData[i] * 0.8f + magnitude * 0.2f;

        if (scopeData[i] > representativeCurve[i])
            representativeCurve[i] = scopeData[i];
    }
}

void MondoEqRefAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromString("ff252526")); // Dark background

    auto plotArea = getLocalBounds();
    plotArea.removeFromTop(40); // header
    plotArea.removeFromRight(40); // dB scale
    plotArea.removeFromBottom(20); // Freq scale

    float left = (float)plotArea.getX();
    float bottom = (float)plotArea.getBottom();
    float width = (float)plotArea.getWidth();
    float height = (float)plotArea.getHeight();

    float minFreq = 20.0f;
    float maxFreq = 20000.0f;
    float minLogFreq = std::log10(minFreq);
    float maxLogFreq = std::log10(maxFreq);

    float skewFactor = 1.5f;

    // 1. Draw Target Ranges
    if (currentPresetIndex > 0 && currentPresetIndex <= presets.size())
    {
        const auto& target = presets[currentPresetIndex - 1];
        
        // Calculate dynamic Min and Max between 100Hz and 16kHz
        float globalMinDb = 1000.0f;
        float globalMaxDb = -1000.0f;
        for (int i = 1; i < currentFftSize / 2; ++i)
        {
            float freq = (float)i * ((float)audioProcessor.getSampleRate() / (float)currentFftSize);
            if (freq >= 100.0f && freq <= 16000.0f)
            {
                float rawPeakDb = juce::Decibels::gainToDecibels(representativeCurve[i], -100.0f);
                float tilt = 4.5f * std::log2(freq / 1000.0f);
                float dbLevel = rawPeakDb > -99.9f ? rawPeakDb + tilt : -100.0f;
                if (dbLevel > -99.0f) {
                    if (dbLevel < globalMinDb) globalMinDb = dbLevel;
                    if (dbLevel > globalMaxDb) globalMaxDb = dbLevel;
                }
            }
        }
        if (globalMaxDb <= globalMinDb) {
            globalMinDb = -100.0f;
            globalMaxDb = 0.0f;
        }

        struct LabelPos { float x; float bottomY; float topY; };
        std::vector<LabelPos> drawnLabels;

        for (const auto& band : target.bands)
        {
            float normX1 = (std::log10(juce::jlimit(minFreq, maxFreq, band.minFreq)) - minLogFreq) / (maxLogFreq - minLogFreq);
            float normX2 = (std::log10(juce::jlimit(minFreq, maxFreq, band.maxFreq)) - minLogFreq) / (maxLogFreq - minLogFreq);
            
            float x1 = left + width * std::pow(normX1, skewFactor);
            float x2 = left + width * std::pow(normX2, skewFactor);

            float targetMinDb = juce::jmap((float)band.targetMin, 0.0f, 100.0f, globalMinDb, globalMaxDb);
            float targetMaxDb = juce::jmap((float)band.targetMax, 0.0f, 100.0f, globalMinDb, globalMaxDb);
            
            float yTop = bottom - juce::jmap(targetMaxDb, minDecibels, maxDecibels, 0.0f, 1.0f) * height;
            float yBottom = bottom - juce::jmap(targetMinDb, minDecibels, maxDecibels, 0.0f, 1.0f) * height;

            juce::Rectangle<float> bandRect(x1, yTop, x2 - x1, yBottom - yTop);
            
            // Draw Target Box background
            g.setColour(band.color.withAlpha(0.12f));
            g.fillRect(bandRect);
            
            // Draw Target Box borders
            g.setColour(band.color.withAlpha(0.4f));
            g.drawRect(bandRect, 1.0f);
            
            // Draw vertical guidelines
            g.setColour(band.color.withAlpha(0.15f));
            g.drawVerticalLine(juce::roundToInt(x1), (float)plotArea.getY(), bottom);
            g.drawVerticalLine(juce::roundToInt(x2), (float)plotArea.getY(), bottom);

            g.setColour(band.color.withAlpha(0.8f));
            g.setFont(13.0f);
            
            // Calculate AVG dB for the band based on historical peaks
            float avgDb = -100.0f;
            int binCount = 0;
            float sumDb = 0.0f;
            
            for (int i = 1; i < currentFftSize / 2; ++i)
            {
                float freq = (float)i * ((float)audioProcessor.getSampleRate() / (float)currentFftSize);
                if (freq >= band.minFreq && freq <= band.maxFreq)
                {
                    float rawPeakDb = juce::Decibels::gainToDecibels(representativeCurve[i], -100.0f);
                    float tilt = 4.5f * std::log2(freq / 1000.0f);
                    float dbLevel = rawPeakDb > -99.9f ? rawPeakDb + tilt : -100.0f;
                    sumDb += dbLevel;
                    binCount++;
                }
            }
            if (binCount > 0) avgDb = sumDb / binCount;
            
            int percentage = 0;
            if (avgDb > -99.0f) {
                float mapped = juce::jmap(avgDb, globalMinDb, globalMaxDb, 0.0f, 100.0f);
                percentage = juce::roundToInt(juce::jlimit(0.0f, 100.0f, mapped));
            }

            juce::String avgText = avgDb > -99.0f ? juce::String(percentage) + "%" : "-";

            g.setFont(15.0f);
            int textWidth = juce::jmax(g.getCurrentFont().getStringWidth(band.name), g.getCurrentFont().getStringWidth(avgText));
            int textHeight = 15;
            
            float startY = plotArea.getBottom() - 10.0f;
            bool overlapped;
            do {
                overlapped = false;
                for (const auto& lbl : drawnLabels) {
                    if (std::abs(x1 - lbl.x) < 24.0f) {
                        if (startY > lbl.topY - 10.0f && (startY - textWidth - 10.0f) < lbl.bottomY + 10.0f) {
                            startY = lbl.topY - 10.0f;
                            overlapped = true;
                        }
                    }
                }
            } while(overlapped);

            drawnLabels.push_back({x1, startY, startY - textWidth - 14.0f});

            g.saveState();
            // Move right to avoid overlapping the line, and use dynamic startY
            g.addTransform(juce::AffineTransform::translation(x1 + 14, startY));
            g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi));
            
            // Draw background to improve readability
            g.setColour(juce::Colour::fromString("ff252526").withAlpha(0.9f));
            g.fillRect(-4, -textHeight - 2, textWidth + 14, textHeight * 2 + 4);
            
            // Draw text with conditional feedback color
            if (percentage < band.targetMin) {
                g.setColour(juce::Colours::cyan.withAlpha(0.9f));
            } else if (percentage > band.targetMax) {
                g.setColour(juce::Colours::red.withAlpha(0.9f));
            } else {
                g.setColour(juce::Colours::lightgreen.withAlpha(0.9f));
            }
            g.drawText(band.name, 0, -textHeight, textWidth + 10, textHeight, juce::Justification::centredLeft, false);
            g.drawText(avgText, 0, 0, textWidth + 10, textHeight, juce::Justification::centredLeft, false);
            g.restoreState();
        }
    }

    // 2. Draw Grids
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    
    // Frequency grid
    std::array<float, 9> freqs = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
    for (float f : freqs)
    {
        float normX = (std::log10(f) - minLogFreq) / (maxLogFreq - minLogFreq);
        float x = left + width * std::pow(normX, skewFactor);
        g.drawVerticalLine(juce::roundToInt(x), (float)plotArea.getY(), bottom);
        
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        juce::String text = f >= 1000.0f ? juce::String(f / 1000.0f, 0) + "k" : juce::String(f, 0);
        g.drawText(text, (int)x - 20, (int)bottom + 2, 40, 15, juce::Justification::centredTop, false);
        g.setColour(juce::Colours::white.withAlpha(0.1f));
    }

    // dB grid
    for (float db = -120.0f; db <= 24.0f; db += 10.0f)
    {
        if (db < minDecibels || db > maxDecibels) continue;
        float level = juce::jmap(db, minDecibels, maxDecibels, 0.0f, 1.0f);
        float y = bottom - height * level;
        
        g.drawHorizontalLine(juce::roundToInt(y), left, (float)plotArea.getRight());
        
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        juce::String dbText = db > 0.0f ? "+" + juce::String(db, 0) : juce::String(db, 0);
        g.drawText(dbText, (int)plotArea.getRight() + 5, (int)y - 10, 30, 20, juce::Justification::centredLeft, false);
        g.setColour(juce::Colours::white.withAlpha(0.1f));
    }

    // 3. Draw Curves
    juce::Path spectrumPath;
    juce::Path peakPath;
    bool first = true;

    std::vector<float> pixelLiveMax(juce::roundToInt(width) + 1, -100.0f);
    std::vector<float> pixelPeakMax(juce::roundToInt(width) + 1, -100.0f);

    for (int i = 1; i < currentFftSize / 2; ++i)
    {
        float freq = (float)i * ((float)audioProcessor.getSampleRate() / (float)currentFftSize);
        if (freq < minFreq) continue;
        if (freq > maxFreq) break;

        float normFreq = (std::log10(freq) - minLogFreq) / (maxLogFreq - minLogFreq);
        float xPos = width * std::pow(normFreq, skewFactor);
        int xPixel = juce::jlimit(0, juce::roundToInt(width), juce::roundToInt(xPos));
        
        float tilt = 4.5f * std::log2(freq / 1000.0f); // 4.5dB/oct matches SPAN default

        float rawDb = juce::Decibels::gainToDecibels(scopeData[i], -100.0f);
        float dbLevel = rawDb > -99.9f ? rawDb + tilt : -100.0f;
        
        float rawPeakDb = juce::Decibels::gainToDecibels(representativeCurve[i], -100.0f);
        float peakDbLevel = rawPeakDb > -99.9f ? rawPeakDb + tilt : -100.0f;

        pixelLiveMax[xPixel] = juce::jmax(pixelLiveMax[xPixel], dbLevel);
        pixelPeakMax[xPixel] = juce::jmax(pixelPeakMax[xPixel], peakDbLevel);
    }
    
    // Fill empty pixels with linear interpolation
    auto interpolateGaps = [&](std::vector<float>& arr, int w) {
        int lastValid = -1;
        for (int x = 0; x <= w; ++x) {
            if (arr[x] > -99.9f) {
                if (lastValid != -1 && x - lastValid > 1) {
                    float startVal = arr[lastValid];
                    float endVal = arr[x];
                    for (int j = lastValid + 1; j < x; ++j) {
                        float t = (float)(j - lastValid) / (x - lastValid);
                        arr[j] = startVal + t * (endVal - startVal);
                    }
                }
                lastValid = x;
            }
        }
        if (lastValid != -1 && lastValid < w) {
            for (int j = lastValid + 1; j <= w; ++j) arr[j] = arr[lastValid];
        }
        int firstValid = 0;
        while (firstValid <= w && arr[firstValid] <= -99.9f) firstValid++;
        if (firstValid <= w && firstValid > 0) {
            for (int j = 0; j < firstValid; ++j) arr[j] = arr[firstValid];
        }
    };
    
    interpolateGaps(pixelLiveMax, juce::roundToInt(width));
    interpolateGaps(pixelPeakMax, juce::roundToInt(width));

    // Visual smoothing pass (Moving Average) to round out the straight lines in the low end
    auto smoothArray = [&](std::vector<float>& arr, int w, int radius) {
        std::vector<float> temp = arr;
        for (int x = 0; x <= w; ++x) {
            float sum = 0.0f;
            int count = 0;
            for (int k = -radius; k <= radius; ++k) {
                int idx = juce::jlimit(0, w, x + k);
                sum += temp[idx];
                count++;
            }
            arr[x] = sum / count;
        }
    };

    // Apply a 5-pixel radius visual smoothing
    smoothArray(pixelLiveMax, juce::roundToInt(width), 5);
    smoothArray(pixelPeakMax, juce::roundToInt(width), 5);
    
    std::vector<float> pixelSmoothedMax = pixelPeakMax;
    // Apply a 40-pixel radius visual smoothing to create the 'General Shape' contour
    smoothArray(pixelSmoothedMax, juce::roundToInt(width), 40);

    juce::Path smoothedPath;
    first = true;

    for (int x = 0; x <= juce::roundToInt(width); ++x)
    {
        float dbLevel = pixelLiveMax[x];
        float peakDbLevel = pixelPeakMax[x];
        float smoothedDbLevel = pixelSmoothedMax[x];
        
        float level = juce::jmap(juce::jlimit(minDecibels, maxDecibels, dbLevel), minDecibels, maxDecibels, 0.0f, 1.0f);
        float y = bottom - level * height;
        
        float peakLevel = juce::jmap(juce::jlimit(minDecibels, maxDecibels, peakDbLevel), minDecibels, maxDecibels, 0.0f, 1.0f);
        float peakY = bottom - peakLevel * height;

        float smoothedLevel = juce::jmap(juce::jlimit(minDecibels, maxDecibels, smoothedDbLevel), minDecibels, maxDecibels, 0.0f, 1.0f);
        float smoothedY = bottom - smoothedLevel * height;

        if (first)
        {
            spectrumPath.startNewSubPath(left + x, y);
            peakPath.startNewSubPath(left + x, peakY);
            smoothedPath.startNewSubPath(left + x, smoothedY);
            first = false;
        }
        else
        {
            spectrumPath.lineTo(left + x, y);
            peakPath.lineTo(left + x, peakY);
            smoothedPath.lineTo(left + x, smoothedY);
        }
    }

    // Draw Live Curve
    g.setColour(juce::Colours::cyan.withAlpha(0.8f));
    g.strokePath(spectrumPath, juce::PathStrokeType(1.5f));

    // Draw Normal Peak Hold (without shading)
    g.setColour(juce::Colours::cyan.withAlpha(0.3f));
    g.strokePath(peakPath, juce::PathStrokeType(1.0f));

    // Draw Smoothed Contour Peak Hold
    g.setColour(juce::Colours::yellow.withAlpha(0.8f));
    g.strokePath(smoothedPath, juce::PathStrokeType(2.0f));

    // 4. Mouse Crosshair & Measurement
    if (isMouseOverPlot && plotArea.contains(mousePos))
    {
        float mouseX = (float)mousePos.x;
        float mouseY = (float)mousePos.y;

        float normX = (mouseX - left) / width;
        float inverseNormX = std::pow(normX, 1.0f / skewFactor);
        float freq = std::pow(10.0f, minLogFreq + inverseNormX * (maxLogFreq - minLogFreq));
        
        float normY = (bottom - mouseY) / height;
        float db = juce::jmap(normY, 0.0f, 1.0f, minDecibels, maxDecibels);

        // Draw crosshair
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        float dashes[] = {3.0f, 4.0f};
        g.drawDashedLine(juce::Line<float>(mouseX, (float)plotArea.getY(), mouseX, bottom), dashes, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>(left, mouseY, (float)plotArea.getRight(), mouseY), dashes, 2, 1.0f);

        // Tooltip text
        juce::String textFreq = freq >= 1000.0f ? juce::String(freq / 1000.0f, 2) + " kHz" : juce::String(freq, 1) + " Hz";
        juce::String text = textFreq + " | " + juce::String(db, 1) + " dB";

        // Measurement overlay
        if (isMeasuring)
        {
            float ax = (float)measurePointA.x;
            float ay = (float)measurePointA.y;
            
            g.setColour(juce::Colours::orange);
            g.drawEllipse(ax - 3, ay - 3, 6, 6, 2.0f);
            
            float normYA = (bottom - ay) / height;
            float dbA = juce::jmap(normYA, 0.0f, 1.0f, minDecibels, maxDecibels);
            
            float diffDb = db - dbA;
            text += " (Δ " + juce::String(diffDb > 0 ? "+" : "") + juce::String(diffDb, 1) + " dB)";

            g.drawLine(ax, ay, mouseX, mouseY, 1.5f);
        }

        g.setFont(14.0f);
        int textWidth = g.getCurrentFont().getStringWidth(text) + 16;
        int textX = plotArea.getRight() - textWidth;
        int textY = plotArea.getY() + 4;

        g.setColour(juce::Colour::fromString("ff252526").withAlpha(0.9f));
        g.fillRect(textX, textY, textWidth, 22);

        g.setColour(juce::Colours::white);
        g.drawText(text, textX, textY, textWidth, 22, juce::Justification::centred, false);
    }
}

void MondoEqRefAudioProcessorEditor::resized()
{
    fftSizeLabel.setBounds(10, 5, 80, 20);
    fftSizeBox.setBounds(fftSizeLabel.getRight() + 5, 5, 100, 20);

    targetRoleLabel.setBounds(fftSizeBox.getRight() + 20, 5, 50, 20);
    targetRoleBox.setBounds(targetRoleLabel.getRight() + 5, 5, 200, 20);

    resetButton.setBounds(targetRoleBox.getRight() + 20, 5, 100, 20);
}

void MondoEqRefAudioProcessorEditor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    float zoomFactor = wheel.deltaY > 0 ? 1.1f : 0.9f;
    float range = maxDecibels - minDecibels;
    float newRange = range / zoomFactor;
    float diff = range - newRange;
    
    minDecibels += diff / 2.0f;
    maxDecibels -= diff / 2.0f;

    minDecibels = juce::jmax(minDecibels, -144.0f);
    maxDecibels = juce::jmin(maxDecibels, 24.0f);
    if (maxDecibels - minDecibels < 10.0f)
        maxDecibels = minDecibels + 10.0f;
}

void MondoEqRefAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    auto plotArea = getLocalBounds().withTrimmedTop(40).withTrimmedRight(40).withTrimmedBottom(20);
    if (!plotArea.contains(event.getPosition())) return;

    dragStartY = (float)event.getMouseDownY();
    dragStartMinDB = minDecibels;
    dragStartMaxDB = maxDecibels;
    isDragging = false;
}

void MondoEqRefAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    auto plotArea = getLocalBounds().withTrimmedTop(40).withTrimmedRight(40).withTrimmedBottom(20);

    isDragging = true;
    
    float dy = (float)event.getDistanceFromDragStartY();
    float height = (float)getHeight() - 60.0f; // Adjusted for margins
    
    float range = dragStartMaxDB - dragStartMinDB;
    float offsetDB = (dy / height) * range;

    minDecibels = dragStartMinDB + offsetDB;
    maxDecibels = dragStartMaxDB + offsetDB;

    if (maxDecibels > 48.0f) {
        float diff = maxDecibels - 48.0f;
        maxDecibels -= diff;
        minDecibels -= diff;
    }
    if (minDecibels < -200.0f) {
        float diff = -200.0f - minDecibels;
        minDecibels += diff;
        maxDecibels += diff;
    }
}

void MondoEqRefAudioProcessorEditor::mouseUp(const juce::MouseEvent& event)
{
    auto plotArea = getLocalBounds().withTrimmedTop(40).withTrimmedRight(40).withTrimmedBottom(20);
    
    // If it was just a simple click (not dragged), handle measurement logic
    if (!isDragging && plotArea.contains(event.getPosition()))
    {
        if (!isMeasuring)
        {
            isMeasuring = true;
            measurePointA = event.getPosition();
        }
        else
        {
            // Second click: either confirm or clear
            isMeasuring = false;
        }
        repaint();
    }
}

void MondoEqRefAudioProcessorEditor::mouseMove(const juce::MouseEvent& event)
{
    auto plotArea = getLocalBounds().withTrimmedTop(40).withTrimmedRight(40).withTrimmedBottom(20);

    if (plotArea.contains(event.getPosition())) {
        isMouseOverPlot = true;
        mousePos = event.getPosition();
        repaint(); // Repaint needed for crosshair
    } else if (isMouseOverPlot) {
        isMouseOverPlot = false;
        repaint();
    }
}

void MondoEqRefAudioProcessorEditor::mouseExit(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    if (isMouseOverPlot) {
        isMouseOverPlot = false;
        repaint();
    }
}
