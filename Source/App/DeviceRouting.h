#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>

struct DeviceAudioProfile
{
    juce::String deviceName;
    double sampleRate = 44100.0;
    int bufferSize = 512;
    int processedInputChannel = 0;
    int diInputChannel = 1;
    int outputChannel = 0;
    
    juce::String activeInputChannels;
    juce::String activeOutputChannels;
    juce::StringArray activeMidiInputs;
    juce::String defaultMidiOutput;
    
    juce::var toVar() const;
    void fromVar(const juce::var& v);
};

class DeviceRoutingManager
{
public:
    DeviceRoutingManager();
    ~DeviceRoutingManager() = default;

    static juce::File getSettingsDirectory();
    static juce::File getStemsDirectory();
    static juce::File getDiDirectory();
    static juce::File getProfilesFile();

    void loadProfiles();
    void saveProfiles();

    void addOrUpdateProfile(const DeviceAudioProfile& profile);
    DeviceAudioProfile getProfile(const juce::String& deviceName) const;
    
    DeviceAudioProfile lastUsedProfile;

private:
    std::vector<DeviceAudioProfile> profiles;
};
