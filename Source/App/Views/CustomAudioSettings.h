#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../DeviceRouting.h"

class CustomAudioSettingsComponent : public juce::Component,
                                     public juce::ChangeListener
{
public:
    CustomAudioSettingsComponent(juce::AudioDeviceManager& manager, DeviceRoutingManager& routing);
    ~CustomAudioSettingsComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

private:
    void populateCombos();
    void saveLogicalRouting();

    juce::AudioDeviceManager& deviceManager;
    DeviceRoutingManager& routingManager;

    std::unique_ptr<juce::AudioDeviceSelectorComponent> nativeSelector;

    juce::Label processedLabel;
    juce::ComboBox processedCombo;

    juce::Label diLabel;
    juce::ComboBox diCombo;

    juce::Label outputLabel;
    juce::ComboBox outputCombo;
    
    juce::ToggleButton dualMonoToggle;

    juce::String currentDeviceName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CustomAudioSettingsComponent)
};
