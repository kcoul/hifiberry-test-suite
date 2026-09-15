#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "Engine.h"

namespace RoundTrip
{
struct DeviceRequest
{
    juce::String typeName;
    juce::String outputName;  // exact name, or a case-insensitive substring of one
    juce::String inputName;
    double sampleRate = 0.0;
    int bufferSize = 0;
    int numChannels = 2;
};

// Returns an error message, or an empty string on success. Devices not named in the
// request default to a hw: device where one exists.
juce::String openDevice (juce::AudioDeviceManager& manager, const DeviceRequest& request);

juce::String describeDevices (juce::AudioDeviceManager& manager);

juce::StringArray getDeviceWarnings (const DeviceInfo& info);

bool isDirectHardware (const juce::String& typeName, const juce::String& deviceName);

} // namespace RoundTrip
