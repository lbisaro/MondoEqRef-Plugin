#include "PluginProcessor.h"
#include "PluginEditor.h"

MondoEqRefAudioProcessorEditor::MondoEqRefAudioProcessorEditor (MondoEqRefAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (940, 450);

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

    reloadTargetsButton.setButtonText("Refresh");
    reloadTargetsButton.onClick = [this]() {
        int currentId = targetRoleBox.getSelectedId();
        loadTargets();
        targetRoleBox.setSelectedId(currentId, juce::dontSendNotification);
        targetRoleChanged();
    };
    addAndMakeVisible(reloadTargetsButton);

    resetButton.setButtonText("Reset (R)");
    resetButton.onClick = [this] {
        std::fill(representativeCurve.begin(), representativeCurve.end(), 0.0f);
        std::fill(sumCurve.begin(), sumCurve.end(), 0.0f);
        std::fill(representativeCurveHann.begin(), representativeCurveHann.end(), 0.0f);
        std::fill(sumCurveHann.begin(), sumCurveHann.end(), 0.0f);
        std::fill(maxPeakCurve.begin(), maxPeakCurve.end(), 0.0f);
        std::fill(scopeData.begin(), scopeData.end(), 0.0f);
        std::fill(scopeDataHann.begin(), scopeDataHann.end(), 0.0f);
        validFrameCount = 0;
        audioProcessor.resetLufs();
        repaint();
    };
    addAndMakeVisible(resetButton);

    tiltButton.setToggleState(true, juce::dontSendNotification);
    tiltButton.onClick = [this] {
        isTiltEnabled = tiltButton.getToggleState();
        repaint();
    };
    addAndMakeVisible(tiltButton);

    targetOffsetSlider.setRange(-50.0, 50.0, 0.5);
    targetOffsetSlider.setValue(0.0, juce::dontSendNotification);
    targetOffsetSlider.setDoubleClickReturnValue(true, 0.0);
    targetOffsetSlider.onValueChange = [this] {
        currentTargetOffset = targetOffsetSlider.getValue();
        repaint();
    };
    addAndMakeVisible(targetOffsetSlider);

    setWantsKeyboardFocus(true);
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
    
    juce::File dataFile;
#if JUCE_WINDOWS
    dataFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getParentDirectory().getChildFile("Local").getChildFile("MondoEqRef").getChildFile("targets.json");
#else
    dataFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("MondoEqRef").getChildFile("targets.json");
#endif

    targetRoleBox.addItem("None", 1);
    targetRoleBox.addItem("Stem Refs", 999);

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
                    for (auto& item : *arr)
                    {
                        if (item.isObject())
                        {
                            auto* presetObj = item.getDynamicObject();
                            PresetTarget target;
                            target.name = presetObj->getProperty("name").toString();
                            if (presetObj->hasProperty("lufs")) target.targetLufs = float(presetObj->getProperty("lufs"));
                            
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
                                        band.minFreq = float(bObj->getProperty("min"));
                                        band.maxFreq = float(bObj->getProperty("max"));
                                        if (bObj->hasProperty("tip")) band.tip = bObj->getProperty("tip").toString(); else band.tip = "";
                                        band.color = juce::Colour::fromString(bObj->getProperty("color").toString());
                                        target.bands.push_back(band);
                                    }
                                }
                            }
                            
                            auto pointsProp = presetObj->getProperty("frequency_points");
                            if (pointsProp.isArray())
                            {
                                auto* pointsArr = pointsProp.getArray();
                                for (auto& pItem : *pointsArr)
                                {
                                    if (pItem.isObject())
                                    {
                                        auto* pObj = pItem.getDynamicObject();
                                        PresetPoint pt;
                                        pt.f = float(pObj->getProperty("f"));
                                        pt.target = float(pObj->getProperty("target"));
                                        pt.maxLimit = float(pObj->getProperty("max_limit"));
                                        pt.minLimit = float(pObj->getProperty("min_limit"));
                                        target.points.push_back(pt);
                                    }
                                }
                            }
                            
                            presets.push_back(target);
                        }
                    }
                    
                    std::sort(presets.begin(), presets.end(), [](const PresetTarget& a, const PresetTarget& b) {
                        return a.name.compareIgnoreCase(b.name) < 0;
                    });
                    
                    int presetId = 2; // Start from 2 since 1 is "None"
                    for (const auto& p : presets) {
                        targetRoleBox.addItem(p.name, presetId++);
                    }
                }
            }
        }
    }
    
    targetRoleBox.setSelectedId(1, juce::dontSendNotification);
    currentPresetIndex = -1; // -1 will mean "None"
}

float MondoEqRefAudioProcessorEditor::getCurrentTargetLufs() const
{
    if (currentPresetIndex >= 0 && currentPresetIndex < presets.size()) {
        return presets[currentPresetIndex].targetLufs;
    }
    return -18.0f; // Default if no target
}

void MondoEqRefAudioProcessorEditor::triggerReset()
{
    resetButton.triggerClick();
}


void MondoEqRefAudioProcessorEditor::targetRoleChanged()
{
    int selectedId = targetRoleBox.getSelectedId();
    if (selectedId == 999) {
        // Show Multi-selection dialog for Stem Refs using native file chooser (Async)
        currentPresetIndex = -1;
        
        fileChooser = std::make_unique<juce::FileChooser>("Select Stem Refs", audioProcessor.stemDirectory, "*.json");
        fileChooser->launchAsync(juce::FileBrowserComponent::canSelectMultipleItems | juce::FileBrowserComponent::openMode, 
            [this](const juce::FileChooser& fc) {
                auto results = fc.getResults();
                if (results.isEmpty()) {
                    isStemRefActive = false;
                    targetRoleBox.setSelectedId(1, juce::dontSendNotification);
                    repaint();
                    return;
                }
                
                std::vector<std::vector<float>> loadedCurves;
                
                for (const auto& f : results) {
                    auto parsed = juce::JSON::parse(f);
                    if (parsed.isObject()) {
                        auto curveProp = parsed.getDynamicObject()->getProperty("representativeCurve");
                        if (curveProp.isArray()) {
                            std::vector<float> curve;
                            for (auto& v : *curveProp.getArray()) {
                                curve.push_back((float)v);
                            }
                            if (!curve.empty()) {
                                loadedCurves.push_back(curve);
                            }
                        }
                    }
                }
                
                if (!loadedCurves.empty()) {
                    size_t numBins = loadedCurves[0].size();
                    stemRefAvgCurve.assign(numBins, 0.0f);
                    stemRefMinCurve.assign(numBins, 1000.0f);
                    stemRefMaxCurve.assign(numBins, -1000.0f);
                    
                    for (size_t i = 0; i < numBins; ++i) {
                        float sum = 0.0f;
                        float minVal = 1000.0f;
                        float maxVal = -1000.0f;
                        for (const auto& c : loadedCurves) {
                            float v = c[i];
                            sum += v;
                            if (v < minVal) minVal = v;
                            if (v > maxVal) maxVal = v;
                        }
                        stemRefAvgCurve[i] = sum / loadedCurves.size();
                        stemRefMinCurve[i] = minVal;
                        stemRefMaxCurve[i] = maxVal;
                    }
                    isStemRefActive = true;
                } else {
                    isStemRefActive = false;
                    targetRoleBox.setSelectedId(1, juce::dontSendNotification);
                }
                repaint();
            });
            
    } else {
        isStemRefActive = false;
        currentPresetIndex = selectedId - 2;
    }
    
    if (onTargetLufsChanged) {
        onTargetLufsChanged(getCurrentTargetLufs());
    }
    
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
    windowHann = std::make_unique<juce::dsp::WindowingFunction<float>>(currentFftSize, juce::dsp::WindowingFunction<float>::hann);
    windowBH = std::make_unique<juce::dsp::WindowingFunction<float>>(currentFftSize, juce::dsp::WindowingFunction<float>::blackmanHarris);
    
    fifo.assign(currentFftSize, 0.0f);
    fftData.assign(currentFftSize * 2, 0.0f);
    scopeData.assign(currentFftSize / 2, 0.0f);
    representativeCurve.assign(currentFftSize / 2, 0.0f);
    sumCurve.assign(currentFftSize / 2, 0.0f);
    maxPeakCurve.assign(currentFftSize / 2, 0.0f);

    scopeDataHann.assign(currentFftSize / 2, 0.0f);
    representativeCurveHann.assign(currentFftSize / 2, 0.0f);
    sumCurveHann.assign(currentFftSize / 2, 0.0f);
    validFrameCount = 0;
    fifoIndex = 0;
}

void MondoEqRefAudioProcessorEditor::timerCallback()
{
    if (audioProcessor.triggerReset.exchange(false)) {
        audioProcessor.resetLufs();
        std::fill(scopeData.begin(), scopeData.end(), 0.0f);
        std::fill(scopeDataHann.begin(), scopeDataHann.end(), 0.0f);
        std::fill(maxPeakCurve.begin(), maxPeakCurve.end(), -100.0f); // or 0.0f depending on type, it's linear so 0.0f
        std::fill(maxPeakCurve.begin(), maxPeakCurve.end(), 0.0f);
        std::fill(sumCurve.begin(), sumCurve.end(), 0.0f);
        std::fill(sumCurveHann.begin(), sumCurveHann.end(), 0.0f);
        validFrameCount = 0;
    }

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
    std::vector<float> fftDataHann = fftData;
    std::vector<float> fftDataBH = fftData;

    windowHann->multiplyWithWindowingTable (fftDataHann.data(), (size_t) currentFftSize);
    forwardFFT->performFrequencyOnlyForwardTransform (fftDataHann.data());

    windowBH->multiplyWithWindowingTable (fftDataBH.data(), (size_t) currentFftSize);
    forwardFFT->performFrequencyOnlyForwardTransform (fftDataBH.data());

    float maxMag = 0.0f;
    for (int i = 0; i < currentFftSize / 2; ++i)
    {
        float magnitudeBH = (fftDataBH[i] * 4.0f) / (float)currentFftSize;
        float magnitudeHann = (fftDataHann[i] * 4.0f) / (float)currentFftSize;
        
        if (magnitudeBH > maxMag) maxMag = magnitudeBH;
        
        // Smoothing in magnitude domain for display
        scopeData[i] = scopeData[i] * 0.8f + magnitudeBH * 0.2f;
        scopeDataHann[i] = scopeDataHann[i] * 0.8f + magnitudeHann * 0.2f;
    }
    
    // Noise Gate: only include frame in average if signal is detected (> -80dB approx)
    if (maxMag > 0.0001f)
    {
        validFrameCount++;
        for (int i = 0; i < currentFftSize / 2; ++i)
        {
            sumCurve[i] += scopeData[i];
            representativeCurve[i] = sumCurve[i] / (float)validFrameCount;
            maxPeakCurve[i] = juce::jmax(maxPeakCurve[i], scopeData[i]);

            sumCurveHann[i] += scopeDataHann[i];
            representativeCurveHann[i] = sumCurveHann[i] / (float)validFrameCount;
        }
    }
    
    // Almacenamos el maxMag en una variable de la clase o lo imprimimos de otra forma
    // para que la GUI lo muestre. Para no tocar .h, vamos a confiar en que llegue al log si lo forzamos.
    juce::Logger::writeToLog("FFT Drawn! MaxMag: " + juce::String(maxMag));
}

void MondoEqRefAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromString("ff252526")); // Dark background

    auto fullArea = getLocalBounds();
    auto meterArea = fullArea.removeFromRight(100).withTrimmedTop(40).withTrimmedBottom(20);
    auto leftArea = fullArea.removeFromLeft(40).withTrimmedTop(40).withTrimmedBottom(20);
    
    auto plotArea = fullArea;
    plotArea.removeFromTop(40); // header
    plotArea.removeFromRight(40); // dB scale
    plotArea.removeFromBottom(20); // Freq scale
    auto crestFactorArea = plotArea.removeFromTop(30); // CF metrics

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
    if (currentPresetIndex >= 0 && currentPresetIndex < presets.size())
    {
        const auto& target = presets[currentPresetIndex];
        
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

        // --- Draw Frequency Points Target Curves (Spline) ---
        if (!target.points.empty())
        {
            auto catmullRom = [](float t, float p0, float p1, float p2, float p3) {
                return 0.5f * (
                    (2.0f * p1) +
                    (-p0 + p2) * t +
                    (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t +
                    (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t * t * t
                );
            };

            auto generateSplinePoints = [&](const std::vector<juce::Point<float>>& pts) {
                std::vector<juce::Point<float>> result;
                if (pts.empty()) return result;
                if (pts.size() < 3) return pts; // Not enough points for spline

                result.push_back(pts.front());
                for (size_t i = 0; i < pts.size() - 1; ++i) {
                    auto p0 = pts[i == 0 ? 0 : i - 1];
                    auto p1 = pts[i];
                    auto p2 = pts[i + 1];
                    auto p3 = pts[i + 2 >= pts.size() ? pts.size() - 1 : i + 2];
                    
                    int numSteps = 20;
                    for (int step = 1; step <= numSteps; ++step) {
                        float t = (float)step / (float)numSteps;
                        float x = catmullRom(t, p0.x, p1.x, p2.x, p3.x);
                        float y = catmullRom(t, p0.y, p1.y, p2.y, p3.y);
                        result.push_back({x, y});
                    }
                }
                return result;
            };

            std::vector<juce::Point<float>> targetPts;
            for (const auto& pt : target.points) {
                if (pt.f < minFreq || pt.f > maxFreq) continue;
                float normX = (std::log10(pt.f) - minLogFreq) / (maxLogFreq - minLogFreq);
                float x = left + width * std::pow(normX, skewFactor);

                float tilt = isTiltEnabled ? 4.5f * std::log2(pt.f / 1000.0f) : 0.0f;
                float tiltedTarget = pt.target + tilt + currentTargetOffset;

                float targetY = bottom - juce::jmap(tiltedTarget, minDecibels, maxDecibels, 0.0f, 1.0f) * height;
                targetPts.push_back({x, targetY});
            }

            auto smoothTarget = generateSplinePoints(targetPts);

            if (!smoothTarget.empty()) {
                // 1. Draw Target Line
                juce::Path targetPath;
                targetPath.startNewSubPath(smoothTarget.front());
                for (size_t i = 1; i < smoothTarget.size(); ++i) {
                    targetPath.lineTo(smoothTarget[i]);
                }
                g.setColour(juce::Colours::lightgreen.withAlpha(0.6f));
                g.strokePath(targetPath, juce::PathStrokeType(2.0f));
                
                // 4. Draw Individual Points
                g.setColour(juce::Colours::white.withAlpha(0.8f));
                for (const auto& pt : target.points)
                {
                    float tiltOffset = isTiltEnabled ? 4.5f * std::log2(pt.f / 1000.0f) : 0.0f;
                    float tiltedTarget = pt.target + tiltOffset + currentTargetOffset;
                
                    float normFreq = (std::log10(pt.f) - minLogFreq) / (maxLogFreq - minLogFreq);
                    float px = left + width * std::pow(normFreq, skewFactor);
                    float py = bottom - juce::jmap(juce::jlimit(minDecibels, maxDecibels, tiltedTarget), minDecibels, maxDecibels, 0.0f, 1.0f) * height;
                    
                    g.fillEllipse(px - 3.0f, py - 3.0f, 6.0f, 6.0f);
                }
            }
        }


        activeBandTooltips.clear();
        std::vector<juce::Rectangle<float>> drawnBands;

        for (const auto& band : target.bands)
        {
            float normX1 = (std::log10(juce::jlimit(minFreq, maxFreq, band.minFreq)) - minLogFreq) / (maxLogFreq - minLogFreq);
            float normX2 = (std::log10(juce::jlimit(minFreq, maxFreq, band.maxFreq)) - minLogFreq) / (maxLogFreq - minLogFreq);
            
            float x1 = left + width * std::pow(normX1, skewFactor);
            float x2 = left + width * std::pow(normX2, skewFactor);

            float bandWidth = x2 - x1;
            float bandHeight = 20.0f;
            
            float stackY = plotArea.getY() + 5.0f;
            bool overlapping = true;
            while (overlapping) {
                overlapping = false;
                juce::Rectangle<float> testRect(x1, stackY, bandWidth, bandHeight);
                for (const auto& r : drawnBands) {
                    if (testRect.intersects(r)) {
                        overlapping = true;
                        stackY += bandHeight + 4.0f;
                        break;
                    }
                }
            }
            
            juce::Rectangle<float> bandRect(x1, stackY, bandWidth, bandHeight);
            drawnBands.push_back(bandRect);
            
            if (band.tip.isNotEmpty()) {
                activeBandTooltips.push_back({bandRect, band.tip});
            }
            
            g.setColour(band.color.withAlpha(0.6f));
            g.fillRect(bandRect);
            
            g.setColour(band.color);
            g.drawRect(bandRect, 1.0f);
            
            g.setColour(band.color.withAlpha(0.15f));
            g.drawVerticalLine(juce::roundToInt(x1), stackY + bandHeight, bottom);
            g.drawVerticalLine(juce::roundToInt(x2), stackY + bandHeight, bottom);

            g.setColour(juce::Colours::white);
            g.setFont(12.0f);
            g.drawText(band.name, bandRect.toNearestInt(), juce::Justification::centred, true);
        }
    }
    
    // --- Draw Stem Refs ---
    if (isStemRefActive && !stemRefAvgCurve.empty()) {
        std::vector<float> pixelMin(juce::roundToInt(width) + 1, -100.0f);
        std::vector<float> pixelMax(juce::roundToInt(width) + 1, -100.0f);
        std::vector<float> pixelAvg(juce::roundToInt(width) + 1, -100.0f);
        
        float binFreq = (float)audioProcessor.getSampleRate() / (float)(stemRefAvgCurve.size() * 2);

        for (size_t i = 1; i < stemRefAvgCurve.size(); ++i) {
            float freq = binFreq * (float)i;
            if (freq < minFreq) continue;
            if (freq > maxFreq) break;

            float normFreq = (std::log10(freq) - minLogFreq) / (maxLogFreq - minLogFreq);
            float xPos = width * std::pow(normFreq, skewFactor);
            int xPixel = juce::jlimit(0, juce::roundToInt(width), juce::roundToInt(xPos));
            
            float tilt = isTiltEnabled ? 4.5f * std::log2(freq / 1000.0f) : 0.0f;

            auto mapDb = [&](float raw) {
                float db = juce::Decibels::gainToDecibels(raw, -100.0f);
                return db > -99.9f ? db + tilt : -100.0f;
            };

            pixelMin[xPixel] = juce::jmax(pixelMin[xPixel], mapDb(stemRefMinCurve[i]));
            pixelMax[xPixel] = juce::jmax(pixelMax[xPixel], mapDb(stemRefMaxCurve[i]));
            pixelAvg[xPixel] = juce::jmax(pixelAvg[xPixel], mapDb(stemRefAvgCurve[i]));
        }
        
        auto interpolateGapsRef = [&](std::vector<float>& arr, int w) {
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
        
        interpolateGapsRef(pixelMin, juce::roundToInt(width));
        interpolateGapsRef(pixelMax, juce::roundToInt(width));
        interpolateGapsRef(pixelAvg, juce::roundToInt(width));
        
        auto smoothArrayRef = [&](std::vector<float>& arr, int w, int radius) {
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
        
        smoothArrayRef(pixelMin, juce::roundToInt(width), 40);
        smoothArrayRef(pixelMax, juce::roundToInt(width), 40);
        smoothArrayRef(pixelAvg, juce::roundToInt(width), 40);
        
        juce::Path minPath, maxPath, avgPath, shadedPath;
        bool firstRef = true;
        
        for (int x = 0; x <= juce::roundToInt(width); ++x) {
            auto getY = [&](float db) {
                float level = juce::jmap(juce::jlimit(minDecibels, maxDecibels, db), minDecibels, maxDecibels, 0.0f, 1.0f);
                return bottom - level * height;
            };
            
            float yMin = getY(pixelMin[x]);
            float yMax = getY(pixelMax[x]);
            float yAvg = getY(pixelAvg[x]);
            
            if (firstRef) {
                minPath.startNewSubPath(left + x, yMin);
                maxPath.startNewSubPath(left + x, yMax);
                avgPath.startNewSubPath(left + x, yAvg);
                firstRef = false;
            } else {
                minPath.lineTo(left + x, yMin);
                maxPath.lineTo(left + x, yMax);
                avgPath.lineTo(left + x, yAvg);
            }
        }
        
        // Build shaded path
        firstRef = true;
        for (int x = 0; x <= juce::roundToInt(width); ++x) {
            float yMax = bottom - juce::jmap(juce::jlimit(minDecibels, maxDecibels, pixelMax[x]), minDecibels, maxDecibels, 0.0f, 1.0f) * height;
            if (firstRef) { shadedPath.startNewSubPath(left + x, yMax); firstRef = false; }
            else shadedPath.lineTo(left + x, yMax);
        }
        for (int x = juce::roundToInt(width); x >= 0; --x) {
            float yMin = bottom - juce::jmap(juce::jlimit(minDecibels, maxDecibels, pixelMin[x]), minDecibels, maxDecibels, 0.0f, 1.0f) * height;
            shadedPath.lineTo(left + x, yMin);
        }
        shadedPath.closeSubPath();
        
        g.setColour(juce::Colours::lightgreen.withAlpha(0.1f));
        g.fillPath(shadedPath);
        
        g.setColour(juce::Colours::lightgreen.withAlpha(0.6f));
        g.strokePath(avgPath, juce::PathStrokeType(2.0f));
        
        g.setColour(juce::Colours::lightgreen.withAlpha(0.2f));
        g.strokePath(maxPath, juce::PathStrokeType(1.0f));
        g.strokePath(minPath, juce::PathStrokeType(1.0f));
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
        g.setColour(juce::Colours::white.withAlpha(0.1f));
    }

    // Custom 300Hz and 2000Hz soft guidelines
    auto drawCrossoverLine = [&](float f) {
        float normX = (std::log10(f) - minLogFreq) / (maxLogFreq - minLogFreq);
        float x = left + width * std::pow(normX, skewFactor);
        g.setColour(juce::Colours::yellow.withAlpha(0.3f));
        const float dashes[] = { 4.0f, 4.0f };
        g.drawDashedLine(juce::Line<float>(x, (float)plotArea.getY(), x, bottom), dashes, 2, 1.5f);
    };
    drawCrossoverLine(300.0f);
    drawCrossoverLine(2000.0f);

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
    juce::Path peakPath; // Average path
    juce::Path hannPath;
    juce::Path maxPeakPath; // True peak path
    bool first = true;

    std::vector<float> pixelLiveMax(juce::roundToInt(width) + 1, -100.0f);
    std::vector<float> pixelPeakMax(juce::roundToInt(width) + 1, -100.0f); // Average
    std::vector<float> pixelLiveHann(juce::roundToInt(width) + 1, -100.0f);
    std::vector<float> pixelTruePeak(juce::roundToInt(width) + 1, -100.0f);

    float binFreq = (float)audioProcessor.getSampleRate() / (float)currentFftSize;

    for (int i = 1; i < currentFftSize / 2; ++i)
    {
        float freq = binFreq * (float)i;
        if (freq < minFreq) continue;
        if (freq > maxFreq) break;

        float normFreq = (std::log10(freq) - minLogFreq) / (maxLogFreq - minLogFreq);
        float xPos = width * std::pow(normFreq, skewFactor);
        int xPixel = juce::jlimit(0, juce::roundToInt(width), juce::roundToInt(xPos));
        
        float tilt = isTiltEnabled ? 4.5f * std::log2(freq / 1000.0f) : 0.0f;

        float rawDb = juce::Decibels::gainToDecibels(scopeData[i], -100.0f);
        float dbLevel = rawDb > -99.9f ? rawDb + tilt : -100.0f;
        
        float rawPeakDb = juce::Decibels::gainToDecibels(representativeCurve[i], -100.0f);
        float peakDbLevel = rawPeakDb > -99.9f ? rawPeakDb + tilt : -100.0f;

        float rawHannDb = juce::Decibels::gainToDecibels(scopeDataHann[i], -100.0f);
        float hannDbLevel = rawHannDb > -99.9f ? rawHannDb + tilt : -100.0f;

        float rawTruePeakDb = juce::Decibels::gainToDecibels(maxPeakCurve[i], -100.0f);
        float truePeakDbLevel = rawTruePeakDb > -99.9f ? rawTruePeakDb + tilt : -100.0f;

        pixelLiveMax[xPixel] = juce::jmax(pixelLiveMax[xPixel], dbLevel);
        pixelPeakMax[xPixel] = juce::jmax(pixelPeakMax[xPixel], peakDbLevel);
        pixelLiveHann[xPixel] = juce::jmax(pixelLiveHann[xPixel], hannDbLevel);
        pixelTruePeak[xPixel] = juce::jmax(pixelTruePeak[xPixel], truePeakDbLevel);
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
    interpolateGaps(pixelLiveHann, juce::roundToInt(width));
    interpolateGaps(pixelTruePeak, juce::roundToInt(width));

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
    smoothArray(pixelLiveHann, juce::roundToInt(width), 5);
    smoothArray(pixelTruePeak, juce::roundToInt(width), 5);
    
    std::vector<float> pixelSmoothedMax = pixelPeakMax;
    // Apply a 40-pixel radius visual smoothing to create the 'General Shape' contour
    smoothArray(pixelSmoothedMax, juce::roundToInt(width), 40);

    std::vector<float> pixelSmoothedTruePeak = pixelTruePeak;
    smoothArray(pixelSmoothedTruePeak, juce::roundToInt(width), 40);

    juce::Path smoothedPath;
    first = true;

    for (int x = 0; x <= juce::roundToInt(width); ++x)
    {
        float dbLevel = pixelLiveMax[x];
        float peakDbLevel = pixelPeakMax[x];
        float smoothedDbLevel = pixelSmoothedMax[x];
        float hannDbLevel = pixelLiveHann[x];
        float truePeakDbLevel = pixelSmoothedTruePeak[x];
        
        float level = juce::jmap(juce::jlimit(minDecibels, maxDecibels, dbLevel), minDecibels, maxDecibels, 0.0f, 1.0f);
        float y = bottom - level * height;
        
        float peakLevel = juce::jmap(juce::jlimit(minDecibels, maxDecibels, peakDbLevel), minDecibels, maxDecibels, 0.0f, 1.0f);
        float peakY = bottom - peakLevel * height;

        float smoothedLevel = juce::jmap(juce::jlimit(minDecibels, maxDecibels, smoothedDbLevel), minDecibels, maxDecibels, 0.0f, 1.0f);
        float smoothedY = bottom - smoothedLevel * height;

        float hannLevel = juce::jmap(juce::jlimit(minDecibels, maxDecibels, hannDbLevel), minDecibels, maxDecibels, 0.0f, 1.0f);
        float hannY = bottom - hannLevel * height;

        float truePeakLevel = juce::jmap(juce::jlimit(minDecibels, maxDecibels, truePeakDbLevel), minDecibels, maxDecibels, 0.0f, 1.0f);
        float truePeakY = bottom - truePeakLevel * height;

        if (first)
        {
            spectrumPath.startNewSubPath(left + x, y);
            peakPath.startNewSubPath(left + x, peakY);
            smoothedPath.startNewSubPath(left + x, smoothedY);
            hannPath.startNewSubPath(left + x, hannY);
            maxPeakPath.startNewSubPath(left + x, truePeakY);
            first = false;
        }
        else
        {
            spectrumPath.lineTo(left + x, y);
            peakPath.lineTo(left + x, peakY);
            smoothedPath.lineTo(left + x, smoothedY);
            hannPath.lineTo(left + x, hannY);
            maxPeakPath.lineTo(left + x, truePeakY);
        }
    }

    // Draw Live Curve (BH)
    g.setColour(juce::Colours::cyan.withAlpha(0.8f));
    g.strokePath(spectrumPath, juce::PathStrokeType(1.5f));

    // Draw Hann Curve
    g.setColour(juce::Colours::green.withAlpha(0.8f));
    g.strokePath(hannPath, juce::PathStrokeType(1.5f));

    // Draw Average (Peak) Hold (without shading)
    g.setColour(juce::Colours::cyan.withAlpha(0.3f));
    g.strokePath(peakPath, juce::PathStrokeType(1.0f));

    // Draw Smoothed Contour Average
    g.setColour(juce::Colours::yellow.withAlpha(0.8f));
    g.strokePath(smoothedPath, juce::PathStrokeType(2.0f));

    // Draw True Max Peak
    g.setColour(juce::Colours::yellow.withAlpha(0.3f));
    g.strokePath(maxPeakPath, juce::PathStrokeType(1.5f));

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
        
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        const float dashes[] = { 4.0f, 4.0f };
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
        g.setFont(12.0f);
        g.drawText(text, textX, textY, textWidth, 22, juce::Justification::centred, false);
    }

    // 5. Draw Crest Factor Metrics at the top
    float newLowCF = 0.0f;
    float newMidCF = 0.0f;
    float newHiCF = 0.0f;
    
    if (validFrameCount > 0 && !representativeCurve.empty() && !maxPeakCurve.empty())
    {
        auto calcCrestFactor = [&](float minF, float maxF) -> float {
            float sampleRate = (float)audioProcessor.getSampleRate();
            if (sampleRate <= 0.0f) return 0.0f;
            
            int numBins = (int)representativeCurve.size();
            float binWidth = sampleRate / (float)currentFftSize;
            
            int startBin = (int)(minF / binWidth);
            int endBin = (int)(maxF / binWidth);
            
            startBin = juce::jlimit(0, numBins - 1, startBin);
            endBin = juce::jlimit(0, numBins - 1, endBin);
            
            if (startBin >= endBin) return 0.0f;
            
            float totalGap = 0.0f;
            int count = 0;
            for (int i = startBin; i <= endBin; ++i) {
                float avgLin = representativeCurve[i];
                float maxLin = maxPeakCurve[i];
                if (avgLin > 1e-10f && maxLin > 1e-10f) {
                    float peakDb = juce::Decibels::gainToDecibels(maxLin, -100.0f);
                    float avgDb = juce::Decibels::gainToDecibels(avgLin, -100.0f);
                    if (peakDb > avgDb) {
                        totalGap += (peakDb - avgDb);
                        count++;
                    }
                }
            }
            return count > 0 ? (totalGap / (float)count) : 0.0f;
        };
        
        newLowCF = calcCrestFactor(80.0f, 300.0f);
        newMidCF = calcCrestFactor(300.0f, 2000.0f);
        newHiCF = calcCrestFactor(2000.0f, 5000.0f);
    }
    
    if (validFrameCount > 0)
    {
        
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRect(crestFactorArea);
        
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        
        float colWidth = crestFactorArea.getWidth() / 3.0f;
        
        auto drawCrestBar = [&](juce::Rectangle<int> area, const juce::String& label, float cf, float minTarget, float maxTarget) {
            g.setColour(juce::Colours::white.withAlpha(0.7f));
            g.drawText(label + ": " + juce::String(cf, 1) + " dB", area.removeFromTop(15), juce::Justification::centred, false);
            
            auto barArea = area.reduced(10, 4);
            g.setColour(juce::Colours::darkgrey);
            g.fillRect(barArea);
            
            // Map 0-25dB to 0-100% width
            float fillWidth = juce::jmap(cf, 0.0f, 25.0f, 0.0f, (float)barArea.getWidth());
            fillWidth = juce::jlimit(0.0f, (float)barArea.getWidth(), fillWidth);
            
            juce::Colour fillColour = juce::Colours::yellow;
            if (cf >= minTarget && cf <= maxTarget) {
                fillColour = juce::Colours::lightgreen;
            }
            
            g.setColour(fillColour.withAlpha(0.8f));
            g.fillRect(barArea.withWidth((int)fillWidth));
        };
        
        juce::Rectangle<int> r1(crestFactorArea.getX(), crestFactorArea.getY(), (int)colWidth, crestFactorArea.getHeight());
        juce::Rectangle<int> r2((int)(crestFactorArea.getX() + colWidth), crestFactorArea.getY(), (int)colWidth, crestFactorArea.getHeight());
        juce::Rectangle<int> r3((int)(crestFactorArea.getX() + colWidth * 2), crestFactorArea.getY(), (int)colWidth, crestFactorArea.getHeight());
        
        drawCrestBar(r1, "LOW DYNAMICS", newLowCF, 10.0f, 14.0f);
        drawCrestBar(r2, "MID DYNAMICS", newMidCF, 10.0f, 15.0f);
        drawCrestBar(r3, "HI DYNAMICS", newHiCF, 12.0f, 18.0f);
    }
    
    // 6. Draw LUFS Meters
    // (Removed background fill to leave it transparent)
    
    // Draw "LUFS" title above the meters
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(14.0f, juce::Font::bold));
    g.drawText("LUFS", meterArea.getX(), meterArea.getY() - 10, meterArea.getWidth(), 20, juce::Justification::centred, false);

    float intLufs = audioProcessor.getIntegratedLufs();
    float stLufs = audioProcessor.getShortTermLufs();
    
    // Meter drawing helper
    auto drawMeter = [&](juce::Rectangle<int> area, float lufsValue, juce::String label, bool isIntegrated) {
        // Draw background
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillRect(area);
        
        // Calculate filled height (scale: minDecibels to maxDecibels)
        float mappedLufs = juce::jlimit(minDecibels, maxDecibels, lufsValue);
        float fillRatio = (mappedLufs - minDecibels) / (maxDecibels - minDecibels);
        int fillHeight = juce::roundToInt(fillRatio * area.getHeight());
        
        juce::Rectangle<int> fillRect = area.withTrimmedTop(area.getHeight() - fillHeight);
        
        // Use a neutral/transparent color similar to the frequency bands
        g.setColour(juce::Colour::fromString("ffffffff").withAlpha(0.12f));
        g.fillRect(fillRect);
        
        // Draw target line for Integrated LUFS
        if (isIntegrated && currentPresetIndex >= 0 && currentPresetIndex < presets.size()) {
            float targetLufs = presets[currentPresetIndex].targetLufs;
            float mappedTarget = juce::jlimit(minDecibels, maxDecibels, targetLufs);
            float targetRatio = (mappedTarget - minDecibels) / (maxDecibels - minDecibels);
            int targetY = area.getBottom() - juce::roundToInt(targetRatio * area.getHeight());
            
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.drawHorizontalLine(targetY, (float)area.getX(), (float)area.getRight());
            
            // Draw the target value text
            g.setFont(juce::FontOptions(11.0f));
            g.drawText(juce::String(targetLufs, 1), area.getX() - 30, targetY - 6, 28, 12, juce::Justification::centredRight, false);
        }

        // Draw outline
        g.setColour(juce::Colours::white.withAlpha(0.2f));
        g.drawRect(area);

        // Draw label (I or ST)
        g.setColour(juce::Colours::white);
        g.setFont(12.0f);
        g.drawText(label, area.getX(), area.getY() - 15, area.getWidth(), 15, juce::Justification::centred, false);

        // Draw value
        juce::String valStr = lufsValue > -99.0f ? juce::String(lufsValue, 1) : "--";
        g.drawText(valStr, area.getX() - 5, area.getBottom() + 2, area.getWidth() + 10, 15, juce::Justification::centred, false);
    };

    int meterWidth = 30;
    int spacing = 15;
    int totalMetersWidth = meterWidth * 2 + spacing;
    int startX = meterArea.getX() + (meterArea.getWidth() - totalMetersWidth) / 2;
    
    // Shift meters down slightly to make room for the "LUFS" label
    juce::Rectangle<int> intMeterArea(startX, meterArea.getY() + 25, meterWidth, meterArea.getHeight() - 45);
    juce::Rectangle<int> stMeterArea(intMeterArea.getRight() + spacing, meterArea.getY() + 25, meterWidth, meterArea.getHeight() - 45);
    
    drawMeter(intMeterArea, intLufs, "I", true);
    drawMeter(stMeterArea, stLufs, "ST", false);
}

void MondoEqRefAudioProcessorEditor::resized()
{
    fftSizeLabel.setBounds(10, 5, 80, 20);
    fftSizeBox.setBounds(fftSizeLabel.getRight() + 5, 5, 100, 20);

    targetRoleLabel.setBounds(fftSizeBox.getRight() + 20, 5, 50, 20);
    targetRoleBox.setBounds(targetRoleLabel.getRight() + 5, 5, 200, 20);
    reloadTargetsButton.setBounds(targetRoleBox.getRight() + 5, 5, 60, 20);
    resetButton.setBounds(reloadTargetsButton.getRight() + 20, 5, 100, 20);
    tiltButton.setBounds(resetButton.getRight() + 20, 5, 120, 20);
    
    // Position targetOffsetSlider vertically on the left
    auto fullArea = getLocalBounds();
    auto leftArea = fullArea.removeFromLeft(40).withTrimmedTop(40).withTrimmedBottom(20);
    targetOffsetSlider.setBounds(leftArea);
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

void MondoEqRefAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent& event)
{
    auto plotArea = getLocalBounds().withTrimmedLeft(40).withTrimmedRight(100).withTrimmedTop(40).withTrimmedRight(40).withTrimmedBottom(20);
    if (plotArea.contains(event.getPosition()))
    {
        minDecibels = -100.0f;
        maxDecibels = 0.0f;
        repaint();
    }
}

void MondoEqRefAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    auto plotArea = getLocalBounds().withTrimmedLeft(40).withTrimmedRight(100).withTrimmedTop(40).withTrimmedRight(40).withTrimmedBottom(20);
    if (!plotArea.contains(event.getPosition())) return;

    dragStartY = (float)event.getMouseDownY();
    dragStartMinDB = minDecibels;
    dragStartMaxDB = maxDecibels;
    isDragging = false;
}

void MondoEqRefAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    auto plotArea = getLocalBounds().withTrimmedLeft(40).withTrimmedRight(100).withTrimmedTop(40).withTrimmedRight(40).withTrimmedBottom(20);

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
    auto plotArea = getLocalBounds().withTrimmedLeft(40).withTrimmedRight(100).withTrimmedTop(40).withTrimmedRight(40).withTrimmedBottom(20);
    
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
    auto plotArea = getLocalBounds().withTrimmedLeft(40).withTrimmedRight(100).withTrimmedTop(40).withTrimmedRight(40).withTrimmedBottom(20);

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

bool MondoEqRefAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == 'R' || key.getKeyCode() == 'r')
    {
        resetButton.triggerClick();
        return true;
    }
    return juce::AudioProcessorEditor::keyPressed(key);
}

juce::String MondoEqRefAudioProcessorEditor::getTooltip()
{
    // Note: getTooltip doesn't have the mouse event, but juce::Desktop can provide the mouse position,
    // or we can use a stored mouse position. However, getTooltip is queried periodically.
    // TooltipClient provides getTooltip() but usually TooltipWindow handles checking which Component is under mouse.
    // If the whole Editor is the TooltipClient, we check the relative mouse pos.
    auto pos = getMouseXYRelative();
    for (const auto& tooltip : activeBandTooltips) {
        if (tooltip.rect.contains(pos.toFloat())) {
            return tooltip.text;
        }
    }
    return "";
}
