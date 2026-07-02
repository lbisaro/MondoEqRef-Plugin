#include "DeviceRouting.h"

juce::var DeviceAudioProfile::toVar() const
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("deviceName", deviceName);
    obj->setProperty("sampleRate", sampleRate);
    obj->setProperty("bufferSize", bufferSize);
    obj->setProperty("processedInputChannel", processedInputChannel);
    obj->setProperty("diInputChannel", diInputChannel);
    obj->setProperty("outputChannel", outputChannel);
    obj->setProperty("isDualMonoSend", isDualMonoSend);
    
    obj->setProperty("activeInputChannels", activeInputChannels);
    obj->setProperty("activeOutputChannels", activeOutputChannels);
    
    juce::Array<juce::var> midiIns;
    for (const auto& m : activeMidiInputs) midiIns.add(m);
    obj->setProperty("activeMidiInputs", juce::var(midiIns));
    
    obj->setProperty("defaultMidiOutput", defaultMidiOutput);

    return juce::var(obj.get());
}

void DeviceAudioProfile::fromVar(const juce::var& v)
{
    if (auto* obj = v.getDynamicObject())
    {
        deviceName = obj->getProperty("deviceName").toString();
        sampleRate = obj->getProperty("sampleRate");
        bufferSize = obj->getProperty("bufferSize");
        processedInputChannel = obj->getProperty("processedInputChannel");
        diInputChannel = obj->getProperty("diInputChannel");
        outputChannel = obj->getProperty("outputChannel");
        if (obj->hasProperty("isDualMonoSend"))
            isDualMonoSend = obj->getProperty("isDualMonoSend");
        
        if (obj->hasProperty("activeInputChannels"))
            activeInputChannels = obj->getProperty("activeInputChannels").toString();
        if (obj->hasProperty("activeOutputChannels"))
            activeOutputChannels = obj->getProperty("activeOutputChannels").toString();
            
        if (obj->hasProperty("activeMidiInputs"))
        {
            if (auto* arr = obj->getProperty("activeMidiInputs").getArray())
            {
                activeMidiInputs.clear();
                for (auto& item : *arr) activeMidiInputs.add(item.toString());
            }
        }
        
        if (obj->hasProperty("defaultMidiOutput"))
            defaultMidiOutput = obj->getProperty("defaultMidiOutput").toString();
    }
}

DeviceRoutingManager::DeviceRoutingManager()
{
    getSettingsDirectory().createDirectory(); // Create if not exists
    loadProfiles();
}

juce::File DeviceRoutingManager::getSettingsDirectory()
{
    // C:\Users\{usuario}\AppData\Local\MondoEqRef
    // In JUCE userApplicationDataDirectory usually points to Roaming on Windows.
    // So we need to specifically aim for Local if we want Local.
    // But juce::File::userApplicationDataDirectory gives AppData/Roaming on Windows.
    // Let's use userApplicationDataDirectory.getParentDirectory().getChildFile("Local").getChildFile("MondoEqRef")
    // as a cross-platform-ish way that works on Windows to hit AppData\Local.
#if JUCE_WINDOWS
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getParentDirectory()
        .getChildFile("Local")
        .getChildFile("MondoEqRef");
#else
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("MondoEqRef");
#endif
}

juce::File DeviceRoutingManager::getProfilesFile()
{
    return getSettingsDirectory().getChildFile("device_profiles.json");
}

juce::File DeviceRoutingManager::getStemsDirectory()
{
    juce::File stemsDir = getSettingsDirectory().getChildFile("stems");
    stemsDir.createDirectory();
    return stemsDir;
}

juce::File DeviceRoutingManager::getDiDirectory()
{
    juce::File diDir = getSettingsDirectory().getChildFile("di_tracks");
    diDir.createDirectory();
    return diDir;
}

void DeviceRoutingManager::loadProfiles()
{
    profiles.clear();
    auto file = getProfilesFile();
    if (file.existsAsFile())
    {
        juce::var parsed = juce::JSON::parse(file);
        if (auto* obj = parsed.getDynamicObject())
        {
            if (auto* arr = obj->getProperty("profiles").getArray())
            {
                for (auto& v : *arr)
                {
                    DeviceAudioProfile p;
                    p.fromVar(v);
                    profiles.push_back(p);
                }
            }
            if (obj->hasProperty("lastUsedDevice"))
            {
                juce::String lastDevice = obj->getProperty("lastUsedDevice").toString();
                lastUsedProfile = getProfile(lastDevice);
            }
        }
    }
}

void DeviceRoutingManager::saveProfiles()
{
    juce::DynamicObject::Ptr rootObj = new juce::DynamicObject();
    juce::Array<juce::var> arr;
    for (const auto& p : profiles)
    {
        arr.add(p.toVar());
    }
    rootObj->setProperty("profiles", juce::var(arr));
    rootObj->setProperty("lastUsedDevice", lastUsedProfile.deviceName);

    juce::String jsonStr = juce::JSON::toString(juce::var(rootObj.get()));
    getProfilesFile().replaceWithText(jsonStr);
}

void DeviceRoutingManager::addOrUpdateProfile(const DeviceAudioProfile& profile)
{
    for (auto& p : profiles)
    {
        if (p.deviceName == profile.deviceName)
        {
            p = profile;
            lastUsedProfile = profile;
            saveProfiles();
            return;
        }
    }
    profiles.push_back(profile);
    lastUsedProfile = profile;
    saveProfiles();
}

DeviceAudioProfile DeviceRoutingManager::getProfile(const juce::String& deviceName) const
{
    for (const auto& p : profiles)
    {
        if (p.deviceName == deviceName)
            return p;
    }
    DeviceAudioProfile p;
    p.deviceName = deviceName;
    return p;
}
