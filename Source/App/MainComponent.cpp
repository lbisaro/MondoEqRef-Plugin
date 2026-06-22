#include "MainComponent.h"

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

    navAnalyzeButton.setClickingTogglesState(true);
    navGuitarDIButton.setClickingTogglesState(true);
    navStemsButton.setClickingTogglesState(true);

    navAnalyzeButton.setRadioGroupId(1);
    navGuitarDIButton.setRadioGroupId(1);
    navStemsButton.setRadioGroupId(1);

    auto showView = [this](int viewIndex) {
        analyzeView.setVisible(viewIndex == 0);
        guitarDIView.setVisible(viewIndex == 1);
        stemsView.setVisible(viewIndex == 2);
        
        // Update nav button toggles to match
        if (viewIndex == 0) navAnalyzeButton.setToggleState(true, juce::dontSendNotification);
        if (viewIndex == 1) navGuitarDIButton.setToggleState(true, juce::dontSendNotification);
        if (viewIndex == 2) navStemsButton.setToggleState(true, juce::dontSendNotification);
    };

    analyzeView.onRequestTabChange = showView;

    navAnalyzeButton.onClick = [showView] { showView(0); };
    navGuitarDIButton.onClick = [showView] { showView(1); };
    navStemsButton.onClick = [showView] { showView(2); };

    // Add views
    addChildComponent(analyzeView);
    addChildComponent(guitarDIView);
    addChildComponent(stemsView);

    // Stem Setup
    formatManager.registerBasicFormats();
    stemsView.onStemLoadRequested = [this, showView](const juce::File& f) {
        loadStemFile(f);
        showView(0); // Cambiar a pestaña Analyze
    };
    
    analyzeView.onPlayStateChanged = [this](bool play) {
        if (stemTransportSource != nullptr)
        {
            if (play) stemTransportSource->start();
            else      stemTransportSource->stop();
        }
        if (diTransportSource != nullptr)
        {
            if (play) diTransportSource->start();
            else      diTransportSource->stop();
        }
    };
    
    // DI Setup
    guitarDIView.onDiLoadRequested = [this, showView](const juce::File& f) {
        const juce::ScopedLock sl(audioLock);
        loadDiFile(f);
        showView(0); // Cambiar a pestaña Analyze
    };
    
    guitarDIView.onRecordToggled = [this](bool startRecord, const juce::String& fileName) {
        if (startRecord) startRecordingDI(fileName);
        else             stopRecordingDI();
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
    stopRecordingDI();

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
    
    if (activeIns[physicalProcessedCh])
    {
        denseProcessedCh = 0;
        for (int i = 0; i < physicalProcessedCh; ++i)
            if (activeIns[i]) denseProcessedCh++;
    }

    int physicalOutCh = routingManager.lastUsedProfile.outputChannel;
    int denseOutCh = -1;
    auto activeOuts = device->getActiveOutputChannels();
    
    if (activeOuts[physicalOutCh])
    {
        denseOutCh = 0;
        for (int i = 0; i < physicalOutCh; ++i)
            if (activeOuts[i]) denseOutCh++;
    }

    int physicalDiCh = routingManager.lastUsedProfile.diInputChannel;
    int denseDiCh = -1;
    if (activeIns[physicalDiCh])
    {
        denseDiCh = 0;
        for (int i = 0; i < physicalDiCh; ++i)
            if (activeIns[i]) denseDiCh++;
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
        float lufsVal = processor.getIntegratedLufs();

        juce::String text = "Modo: " + juce::String(trackMode)
            + " | Proc[P:" + juce::String(physicalProcessedCh) + " D:" + juce::String(denseProcessedCh) + "]"
            + " | DI[P:" + juce::String(physicalDiCh) + " D:" + juce::String(denseDiCh) + "]"
            + " | Bufs: " + juce::String(bufferToFill.buffer->getNumChannels())
            + " | SR: " + juce::String(currentSampleRate);
            
        juce::MessageManager::callAsync([this, text]() {
            analyzeView.debugLabel.setText(text, juce::dontSendNotification);
            guitarDIView.debugLabel.setText(text, juce::dontSendNotification);
        });
    }

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
        
        if (shouldLog)
            DBG("LiveMode - eqInput RMS: " << eqInput.getRMSLevel(0, 0, eqInput.getNumSamples()));
            
        processor.processBlock(eqInput, midi);
        return; // Mute outputs
    }
    else if (trackMode == 2) // Guitar DI
    {
        juce::ScopedLock sl(audioLock);
        if (diTransportSource != nullptr)
        {
            juce::AudioSourceChannelInfo diInfo(&eqInput, 0, eqInput.getNumSamples());
            diTransportSource->getNextAudioBlock(diInfo);
        }

        processor.processBlock(eqInput, midi);
        return; 
    }
    else if (trackMode == 3) // Stems
    {
        juce::ScopedLock sl(audioLock);
        if (stemTransportSource != nullptr)
        {
            juce::AudioSourceChannelInfo stemInfo(&eqInput, 0, eqInput.getNumSamples());
            stemTransportSource->getNextAudioBlock(stemInfo);
        }

        if (shouldLog)
            DBG("StemsMode - eqInput RMS: " << eqInput.getRMSLevel(0, 0, eqInput.getNumSamples()) 
                << " playing: " << ((stemTransportSource && stemTransportSource->isPlaying()) ? "true" : "false")
                << " denseOutCh: " << denseOutCh);

        processor.processBlock(eqInput, midi);

        // Mix down stem to physical output so user can hear it
        if (denseOutCh >= 0 && denseOutCh < bufferToFill.buffer->getNumChannels())
        {
            bufferToFill.buffer->addFrom(denseOutCh, bufferToFill.startSample, eqInput, 0, 0, eqInput.getNumSamples());
            // If output is stereo, we probably want it on next channel too, but let's stick to standard denseOutCh
            if (denseOutCh + 1 < bufferToFill.buffer->getNumChannels())
                bufferToFill.buffer->addFrom(denseOutCh + 1, bufferToFill.startSample, eqInput, 1, 0, eqInput.getNumSamples());
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
            stemReaderSource.reset(newSource.release());
            stemTransportSource = std::move(newTransport);
        }

        analyzeView.setIsPlaying(false);
        
        // Cambiar a track mode Stem
        analyzeView.setTrackMode(3); // 3 = Stems
        analyzeView.setLoadedFileName(file.getFileName());
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
    
    // Main View
    analyzeView.setBounds(bounds);
    guitarDIView.setBounds(bounds);
    stemsView.setBounds(bounds);
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
            diReaderSource.reset(newSource.release());
            diTransportSource = std::move(newTransport);
        }

        analyzeView.setIsPlaying(false);
        
        analyzeView.setTrackMode(2); // 2 = Guitar DI
        analyzeView.setLoadedFileName(file.getFileName());
    }
}

void MainComponent::startRecordingDI(const juce::String& fileName)
{
    stopRecordingDI(); // Stop any existing recording
    
    currentRecordingFileName = fileName;
    diRecordSampleCount.store(0);
    isRecordingDI.store(true);
}

void MainComponent::stopRecordingDI()
{
    if (isRecordingDI.load())
    {
        isRecordingDI.store(false);
        
        int totalSamples = diRecordSampleCount.load();
        if (totalSamples > 0)
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
            juce::File outFile = diDir.getChildFile(currentRecordingFileName);
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
