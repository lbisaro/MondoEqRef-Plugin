#include "MainComponent.h"
#include "AudioNormalizer.h"

MainComponent::MainComponent()
{
    // Setup Top Bar UI
    addAndMakeVisible(settingsButton);
    addAndMakeVisible(deviceStatusLabel);
    settingsButton.onClick = [this] { showAudioSettings(); };

    // Setup Custom Navigation
    addAndMakeVisible(navAnalyzeButton);
    addAndMakeVisible(navGuitarDIButton);
    addAndMakeVisible(navStemsButton);
    addAndMakeVisible(navSpectralRefButton);

    navAnalyzeButton.setClickingTogglesState(true);
    navGuitarDIButton.setClickingTogglesState(true);
    navStemsButton.setClickingTogglesState(true);
    navSpectralRefButton.setClickingTogglesState(true);

    navAnalyzeButton.setRadioGroupId(1);
    navGuitarDIButton.setRadioGroupId(1);
    navStemsButton.setRadioGroupId(1);
    navSpectralRefButton.setRadioGroupId(1);

    auto showView = [this](int viewIndex) {
        if (viewIndex != 0 && analyzeView.getIsPlaying()) {
            analyzeView.setIsPlaying(false); // Forzar stop si salimos de la pestaña
        }
        
        analyzeView.setVisible(viewIndex == 0);
        guitarDIView.setVisible(viewIndex == 1);
        stemsView.setVisible(viewIndex == 2);
        spectralRefView.setVisible(viewIndex == 3);
        
        if (viewIndex == 0) analyzeView.grabKeyboardFocus();
        else if (viewIndex == 1) guitarDIView.grabKeyboardFocus();
        else if (viewIndex == 3) spectralRefView.grabKeyboardFocus();
        
        // Update nav button toggles to match
        if (viewIndex == 0) navAnalyzeButton.setToggleState(true, juce::dontSendNotification);
        if (viewIndex == 1) navGuitarDIButton.setToggleState(true, juce::dontSendNotification);
        if (viewIndex == 2) navStemsButton.setToggleState(true, juce::dontSendNotification);
        if (viewIndex == 3) navSpectralRefButton.setToggleState(true, juce::dontSendNotification);
    };

    analyzeView.onRequestTabChange = showView;

    navAnalyzeButton.onClick = [showView] { showView(0); };
    navGuitarDIButton.onClick = [showView] { showView(1); };
    navStemsButton.onClick = [showView] { showView(2); };
    navSpectralRefButton.onClick = [showView] { showView(3); };

    // Add views
    addChildComponent(analyzeView);
    addChildComponent(guitarDIView);
    addChildComponent(stemsView);
    addChildComponent(spectralRefView);

    // Stem Setup
    formatManager.registerBasicFormats();
    formatManager.registerFormat(new juce::MP3AudioFormat(), false);
    stemsView.onStemLoadRequested = [this, showView](const juce::File& f) {
        loadStemFile(f);
        showView(0); // Cambiar a pestaña Analyze
    };
    analyzeView.onPlayStateChanged = [this](bool play) {
        if (play) {
            int mode = analyzeView.getTrackMode();
            if (mode == 2 && diTransportSource != nullptr) {
                diTransportSource->start();
            } else if (mode == 3 && stemTransportSource != nullptr) {
                stemTransportSource->start();
            }
        } else {
            if (diTransportSource != nullptr) diTransportSource->stop();
            if (stemTransportSource != nullptr) stemTransportSource->stop();
        }
    };
    
    analyzeView.onNormalizeRequested = [this](float targetLufs, float measuredLufs) {
        if (analyzeView.getTrackMode() == 3) {
            juce::File currentStem = analyzeView.getCurrentStemFile();
            if (currentStem.existsAsFile()) {
                if (AudioNormalizer::normalizeFile(currentStem, targetLufs, measuredLufs, formatManager)) {
                    // Reload the file if normalization succeeded
                    loadStemFile(currentStem);
                }
            }
        }
    };
    
    // DI Setup
    guitarDIView.onDiLoadRequested = [this, showView](const juce::File& f) {
        const juce::ScopedLock sl(audioLock);
        loadDiFile(f);
        showView(0); // Cambiar a pestaña Analyze
    };
    
    guitarDIView.onRecordToggled = [this](bool startRecord, const juce::String& fileName) {
        if (startRecord) startRecordingDI();
        else             stopRecordingDI(fileName);
    };
    
    guitarDIView.getDiRms = [this]() {
        return getCurrentDiRms();
    };
    
    // Initial state
    navAnalyzeButton.setToggleState(true, juce::sendNotification);

    // Size the main window. 
    setSize (800, 600);

    // Setup Audio
    deviceManager.addChangeListener(this);
    deviceManager.addAudioCallback(&audioSourcePlayer);
    audioSourcePlayer.setSource(this);
    
    startTimerHz(2); // Check device status twice a second
    
    // Defer audio initialization to avoid deadlocking the message thread 
    // when ASIO drivers try to initialize COM objects or windows.
    juce::Component::SafePointer<MainComponent> safeThis (this);
    juce::Timer::callAfterDelay(500, [safeThis]() {
        if (safeThis != nullptr) {

            auto& profile = safeThis->routingManager.lastUsedProfile;
            if (profile.deviceName.isEmpty()) {
                safeThis->showAudioSettings();
                return;
            }

            // Load the native JUCE XML settings if they exist
            std::unique_ptr<juce::XmlElement> savedState;
            auto settingsFile = DeviceRoutingManager::getSettingsDirectory().getChildFile("audio_settings.xml");
            if (settingsFile.existsAsFile()) {
                savedState = juce::XmlDocument::parse(settingsFile);
            }

            // Let's directly connect using ONLY the device name, requesting max channels by default
            juce::String error = safeThis->deviceManager.initialise(256, 256, savedState.get(), false, profile.deviceName, nullptr);
            auto* currentDevice = safeThis->deviceManager.getCurrentAudioDevice();
            
            if (error.isNotEmpty() || currentDevice == nullptr || currentDevice->getName() != profile.deviceName) {
                safeThis->showAudioSettings();
            } else {
                // Apply our exact JSON profile overrides!
                juce::AudioDeviceManager::AudioDeviceSetup setup;
                safeThis->deviceManager.getAudioDeviceSetup(setup);
                
                
                // Restore MIDI settings from JSON Profile
                for (const auto& midiIn : profile.activeMidiInputs)
                    safeThis->deviceManager.setMidiInputDeviceEnabled(midiIn, true);
                    
                if (profile.defaultMidiOutput.isNotEmpty())
                    safeThis->deviceManager.setDefaultMidiOutputDevice(profile.defaultMidiOutput);
            }
        }
    });
}

MainComponent::~MainComponent()
{
    stopTimer();
    stopRecordingDI("");

    deviceManager.removeAudioCallback(&audioSourcePlayer);
    audioSourcePlayer.setSource(nullptr);
    deviceManager.removeChangeListener(this);
}

void MainComponent::applyProfileToDeviceManager(const DeviceAudioProfile& profile)
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);
    setup.bufferSize = profile.bufferSize;
    setup.sampleRate = profile.sampleRate;
    
    juce::String error = deviceManager.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty())
    {
        deviceStatusLabel.setText("Device Error: " + error, juce::dontSendNotification);
        deviceStatusLabel.setColour(juce::Label::textColourId, juce::Colours::red);
    }
    else
    {
        deviceStatusLabel.setText("Connected: " + profile.deviceName, juce::dontSendNotification);
        deviceStatusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
        populateChannelSelectors();
    }
}

void MainComponent::populateChannelSelectors()
{
    // Channel selectors moved to AnalyzeView, this can be empty for now or updated later.
}

void MainComponent::showAudioSettings()
{
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle = "Audio Settings";
    o.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    
    auto* customSettings = new CustomAudioSettingsComponent(deviceManager, routingManager);
    o.content.setOwned(customSettings);
    o.content->setSize(500, 750); // Increased height to fit our custom comboboxes
    o.launchAsync();
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &deviceManager)
    {
        updateDeviceProfileFromManager();
    }
}

void MainComponent::updateDeviceProfileFromManager()
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        deviceManager.addAudioCallback(&audioSourcePlayer);
        audioSourcePlayer.setSource(this);
        
        deviceStatusLabel.setText("Connected: " + device->getName(), juce::dontSendNotification);
        deviceStatusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
    }
    else
    {
        deviceManager.removeAudioCallback(&audioSourcePlayer);
        audioSourcePlayer.setSource(nullptr);
        
        deviceStatusLabel.setText("Device Disconnected", juce::dontSendNotification);
        deviceStatusLabel.setColour(juce::Label::textColourId, juce::Colours::red);
    }
}

void MainComponent::timerCallback()
{
    if (deviceManager.getCurrentAudioDevice() == nullptr)
    {
        analyzeView.debugLabel.setText("Device ERROR: device == nullptr", juce::dontSendNotification);
        guitarDIView.debugLabel.setText("Device ERROR: device == nullptr", juce::dontSendNotification);
    }
    else if (analyzeView.debugLabel.getText().startsWith("Device ERROR"))
    {
        analyzeView.debugLabel.setText("", juce::dontSendNotification);
        guitarDIView.debugLabel.setText("", juce::dontSendNotification);
    }
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    // Fix: Asegurar que el AudioProcessor base conozca el sample rate
    processor.setRateAndBufferSizeDetails(sampleRate, samplesPerBlockExpected);
    processor.prepareToPlay(sampleRate, samplesPerBlockExpected);
    
    if (stemTransportSource != nullptr)
        stemTransportSource->prepareToPlay(samplesPerBlockExpected, sampleRate);
        
    if (diTransportSource != nullptr)
        diTransportSource->prepareToPlay(samplesPerBlockExpected, sampleRate);
        
    currentSampleRate = sampleRate;
    // Asignar memoria para hasta 1 minuto de grabación DI
    int maxSamples = (int)(sampleRate * 60.0);
    diRecordBuffer.setSize(1, maxSamples);
    diRecordBuffer.clear();
    diRecordSampleCount.store(0);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // NO CLEAR HERE! Otherwise we destroy the input channels provided by AudioSourcePlayer.
    
    static int logCounter = 0;
    bool shouldLog = (logCounter++ > 100); // Log roughly every ~0.5-1s
    if (shouldLog) logCounter = 0;

    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr) 
    {
        if (shouldLog)
        {
            juce::MessageManager::callAsync([this]() {
                analyzeView.debugLabel.setText("Device ERROR: device == nullptr", juce::dontSendNotification);
                guitarDIView.debugLabel.setText("Device ERROR: device == nullptr", juce::dontSendNotification);
            });
        }
        bufferToFill.clearActiveBufferRegion();
        return;
    }
    
    int trackMode = analyzeView.getTrackMode();

    int physicalProcessedCh = routingManager.lastUsedProfile.processedInputChannel;
    int denseProcessedCh = -1;
    auto activeIns = device->getActiveInputChannels();
    
    if (physicalProcessedCh >= 0 && activeIns[physicalProcessedCh])
    {
        denseProcessedCh = 0;
        for (int i = 0; i < physicalProcessedCh; ++i)
            if (activeIns[i]) denseProcessedCh++;
    }

    int physicalOutCh = routingManager.lastUsedProfile.outputChannel;
    int denseOutCh = -1;
    auto activeOuts = device->getActiveOutputChannels();
    
    if (physicalOutCh >= 0 && activeOuts[physicalOutCh])
    {
        denseOutCh = 0;
        for (int i = 0; i < physicalOutCh; ++i)
            if (activeOuts[i]) denseOutCh++;
    }

    int physicalDiCh = routingManager.lastUsedProfile.diInputChannel;
    int denseDiCh = -1;
    if (physicalDiCh >= 0 && activeIns[physicalDiCh])
    {
        denseDiCh = 0;
        for (int i = 0; i < physicalDiCh; ++i)
            if (activeIns[i]) denseDiCh++;
    }

    // --- ALWAYS PROCESS DI METERS AND RECORDING ---
    if (denseDiCh >= 0 && denseDiCh < bufferToFill.buffer->getNumChannels())
    {
        // Measure RMS for VU meter
        currentDiRms = bufferToFill.buffer->getRMSLevel(denseDiCh, bufferToFill.startSample, bufferToFill.numSamples);

        if (isRecordingDI.load())
        {
            const float* inputData = bufferToFill.buffer->getReadPointer(denseDiCh, bufferToFill.startSample);
            int samplesToWrite = juce::jmin(bufferToFill.numSamples, (int)(diRecordBuffer.getNumSamples() - diRecordSampleCount.load()));
            
            if (samplesToWrite > 0)
            {
                diRecordBuffer.copyFrom(0, diRecordSampleCount.load(), inputData, samplesToWrite);
                diRecordSampleCount.store(diRecordSampleCount.load() + samplesToWrite);
            }
        }
    }
    else
    {
        currentDiRms = 0.0f;
    }

    // --- ALWAYS PROCESS DI METERS AND RECORDING, EVEN IF NOT PLAYING ---    // -------------------------------------------------------------------

    if (!analyzeView.getIsPlaying()) 
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }

    juce::AudioBuffer<float> eqInput (2, bufferToFill.numSamples);
    eqInput.clear();
    juce::MidiBuffer midi;

    if (shouldLog)
    {
        DBG("trackMode: " << trackMode << " denseProcessedCh: " << denseProcessedCh << " denseDiCh: " << denseDiCh 
            << " bufferToFillChs: " << bufferToFill.buffer->getNumChannels() 
            << " isRecordingDI: " << (isRecordingDI.load() ? "true" : "false"));
    }

    if (trackMode == 1) // Live Audio
    {
        if (denseProcessedCh >= 0 && denseProcessedCh < bufferToFill.buffer->getNumChannels())
        {
            eqInput.copyFrom(0, 0, *bufferToFill.buffer, denseProcessedCh, bufferToFill.startSample, bufferToFill.numSamples);
            eqInput.copyFrom(1, 0, *bufferToFill.buffer, denseProcessedCh, bufferToFill.startSample, bufferToFill.numSamples);
        }
        
        bufferToFill.clearActiveBufferRegion(); // Clear physical input from outputs
        processor.processBlock(eqInput, midi);
        return; // Live audio does not output to avoid feedback, unless we want to monitor? (Assuming mute)
    }
    else if (trackMode == 2) // Guitar DI Reamping
    {
        juce::ScopedLock sl(audioLock);
        
        juce::AudioBuffer<float> dryDiBuffer(1, bufferToFill.numSamples);
        dryDiBuffer.clear();
        
        if (diTransportSource != nullptr)
        {
            juce::AudioSourceChannelInfo diInfo(&dryDiBuffer, 0, dryDiBuffer.getNumSamples());
            diTransportSource->getNextAudioBlock(diInfo);
        }
        
        // Output dry DI to the selected Output Channel (for Reamping)
        if (denseOutCh >= 0 && denseOutCh < bufferToFill.buffer->getNumChannels())
        {
            bufferToFill.buffer->addFrom(denseOutCh, bufferToFill.startSample, dryDiBuffer, 0, 0, dryDiBuffer.getNumSamples());
            // Optionally output to right channel too if user selects a stereo pair for reamping
            if (denseOutCh + 1 < bufferToFill.buffer->getNumChannels())
                bufferToFill.buffer->addFrom(denseOutCh + 1, bufferToFill.startSample, dryDiBuffer, 0, 0, dryDiBuffer.getNumSamples());
        }

        // Now, we must READ the processed return from the Helix to analyze it!
        // The return comes from the Processed Input Channel
        if (denseProcessedCh >= 0 && denseProcessedCh < bufferToFill.buffer->getNumChannels())
        {
            eqInput.copyFrom(0, 0, *bufferToFill.buffer, denseProcessedCh, bufferToFill.startSample, bufferToFill.numSamples);
            eqInput.copyFrom(1, 0, *bufferToFill.buffer, denseProcessedCh, bufferToFill.startSample, bufferToFill.numSamples);
        }
        
        // Clear the physical inputs from the final output buffer so we do not create a feedback loop
        bufferToFill.clearActiveBufferRegion(); 
        
        // Re-apply the dry DI output that we just cleared!
        if (denseOutCh >= 0 && denseOutCh < bufferToFill.buffer->getNumChannels())
        {
            bufferToFill.buffer->addFrom(denseOutCh, bufferToFill.startSample, dryDiBuffer, 0, 0, dryDiBuffer.getNumSamples());
            if (denseOutCh + 1 < bufferToFill.buffer->getNumChannels())
                bufferToFill.buffer->addFrom(denseOutCh + 1, bufferToFill.startSample, dryDiBuffer, 0, 0, dryDiBuffer.getNumSamples());
        }

        // Analyze the RE-AMPED signal
        processor.processBlock(eqInput, midi);
        return; 
    }
    else if (trackMode == 3) // Stems
    {
        bufferToFill.clearActiveBufferRegion();
        juce::ScopedLock sl(audioLock);
        if (stemTransportSource != nullptr)
        {
            juce::AudioSourceChannelInfo stemInfo(&eqInput, 0, eqInput.getNumSamples());
            stemTransportSource->getNextAudioBlock(stemInfo);
        }

        processor.processBlock(eqInput, midi);

        // Enviar el Stem de salida por el mismo canal físico seleccionado para el Processed Input
        int physicalStemOutCh = routingManager.lastUsedProfile.processedInputChannel;
        int stemDenseOutCh = -1;
        if (physicalStemOutCh >= 0 && activeOuts[physicalStemOutCh])
        {
            stemDenseOutCh = 0;
            for (int i = 0; i < physicalStemOutCh; ++i)
                if (activeOuts[i]) stemDenseOutCh++;
        }

        if (stemDenseOutCh >= 0 && stemDenseOutCh < bufferToFill.buffer->getNumChannels())
        {
            bufferToFill.buffer->addFrom(stemDenseOutCh, bufferToFill.startSample, eqInput, 0, 0, eqInput.getNumSamples());
            if (stemDenseOutCh + 1 < bufferToFill.buffer->getNumChannels())
                bufferToFill.buffer->addFrom(stemDenseOutCh + 1, bufferToFill.startSample, eqInput, 1, 0, eqInput.getNumSamples());
        }
        return;
    }

    // Default mute
    bufferToFill.clearActiveBufferRegion();
}

void MainComponent::releaseResources()
{
    processor.releaseResources();
    if (stemTransportSource != nullptr)
        stemTransportSource->releaseResources();
    if (diTransportSource != nullptr)
        diTransportSource->releaseResources();
}

void MainComponent::loadStemFile(const juce::File& file)
{
    auto* reader = formatManager.createReaderFor(file);
    if (reader != nullptr)
    {
        std::unique_ptr<juce::AudioFormatReaderSource> newSource(new juce::AudioFormatReaderSource(reader, true));
        auto newTransport = std::make_unique<juce::AudioTransportSource>();
        newTransport->setSource(newSource.get(), 0, nullptr, reader->sampleRate);
        
        // Configurar loop infinito
        newSource->setLooping(true);
        
        auto* device = deviceManager.getCurrentAudioDevice();
        if (device != nullptr)
            newTransport->prepareToPlay(device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());
        
        newTransport->setPosition(0.0);
        
        {
            juce::ScopedLock sl(audioLock);
            if (stemTransportSource != nullptr)
            {
                stemTransportSource->setSource(nullptr);
            }
            stemTransportSource = std::move(newTransport);
            stemReaderSource.reset(newSource.release());
        }

        analyzeView.setIsPlaying(false);
        
        // Cambiar a track mode Stem
        analyzeView.setTrackMode(3); // 3 = Stems
        analyzeView.setLoadedFileName(file.getFileName());
        analyzeView.setCurrentStemFile(file);
    }
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    
    auto topBar = getLocalBounds().removeFromTop(40);
    g.setColour(juce::Colours::black.withAlpha(0.2f));
    g.fillRect(topBar);
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    auto topBar = bounds.removeFromTop(40);
    
    settingsButton.setBounds(topBar.removeFromRight(150).reduced(5));
    // The deviceStatusLabel can be hidden or moved to avoid clutter, 
    // but let's place it right next to settings
    deviceStatusLabel.setBounds(topBar.removeFromRight(200).reduced(5));
    
    // Navigation buttons on the left
    auto navArea = topBar.reduced(5);
    navAnalyzeButton.setBounds(navArea.removeFromLeft(100));
    navArea.removeFromLeft(5); // spacing
    navGuitarDIButton.setBounds(navArea.removeFromLeft(100));
    navArea.removeFromLeft(5); // spacing
    navStemsButton.setBounds(navArea.removeFromLeft(100));
    navArea.removeFromLeft(5); // spacing
    navSpectralRefButton.setBounds(navArea.removeFromLeft(100));
    
    // Main View
    analyzeView.setBounds(bounds);
    guitarDIView.setBounds(bounds);
    stemsView.setBounds(bounds);
    spectralRefView.setBounds(bounds);
}

void MainComponent::loadDiFile(const juce::File& file)
{
    auto* reader = formatManager.createReaderFor(file);
    if (reader != nullptr)
    {
        std::unique_ptr<juce::AudioFormatReaderSource> newSource(new juce::AudioFormatReaderSource(reader, true));
        auto newTransport = std::make_unique<juce::AudioTransportSource>();
        newTransport->setSource(newSource.get(), 0, nullptr, reader->sampleRate);
        
        newSource->setLooping(true);
        
        auto* device = deviceManager.getCurrentAudioDevice();
        if (device != nullptr)
            newTransport->prepareToPlay(device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());

        newTransport->setPosition(0.0);
        
        {
            juce::ScopedLock sl(audioLock);
            if (diTransportSource != nullptr)
            {
                diTransportSource->setSource(nullptr);
            }
            diTransportSource = std::move(newTransport);
            diReaderSource.reset(newSource.release());
        }

        analyzeView.setIsPlaying(false);
        
        analyzeView.setTrackMode(2); // 2 = Guitar DI
        analyzeView.setLoadedFileName(file.getFileName());
    }
}

void MainComponent::startRecordingDI()
{
    stopRecordingDI(""); // Stop any existing recording without saving
    
    diRecordSampleCount.store(0);
    isRecordingDI.store(true);
}

void MainComponent::stopRecordingDI(const juce::String& fileName)
{
    if (isRecordingDI.load())
    {
        isRecordingDI.store(false);
        
        int totalSamples = diRecordSampleCount.load();
        if (totalSamples > 0 && fileName.isNotEmpty())
        {
            // Apply 30ms fade-in and fade-out
            int fadeSamples = (int)(currentSampleRate * 0.03);
            if (fadeSamples > totalSamples / 2) fadeSamples = totalSamples / 2;
            
            float* data = diRecordBuffer.getWritePointer(0);
            
            // Fade In
            for (int i = 0; i < fadeSamples; ++i)
                data[i] *= (float)i / (float)fadeSamples;
                
            // Fade Out
            for (int i = 0; i < fadeSamples; ++i)
                data[totalSamples - 1 - i] *= (float)i / (float)fadeSamples;
                
            juce::File diDir = DeviceRoutingManager::getDiDirectory();
            juce::File outFile = diDir.getChildFile(fileName);
            outFile.deleteFile();
            
            juce::WavAudioFormat wavFormat;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wavFormat.createWriterFor(new juce::FileOutputStream(outFile),
                                          currentSampleRate,
                                          1, // mono recording
                                          24, // 24-bit
                                          {}, 0)
            );
            
            if (writer != nullptr)
            {
                const float* channelData = diRecordBuffer.getReadPointer(0);
                writer->writeFromAudioSampleBuffer(juce::AudioBuffer<float>(const_cast<float**>(&channelData), 1, totalSamples), 0, totalSamples);
            }
        }
    }
}
