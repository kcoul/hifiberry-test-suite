#include "Devices.h"

namespace RoundTrip
{
namespace
{
juce::String resolveName (juce::AudioIODeviceType& type, bool isInput, const juce::String& pattern, juce::String& error)
{
    const auto names = type.getDeviceNames (isInput);

    if (names.contains (pattern))
        return pattern;

    for (auto& name : names)
        if (name.containsIgnoreCase (pattern))
            return name;

    error = juce::String ("No ") + (isInput ? "input" : "output") + " device matching '" + pattern
          + "'. Available: " + names.joinIntoString ("; ");
    return {};
}

// Prefers a hw: device that can both play and record, so the loopback stays on one card.
void preferDirectHardware (juce::AudioIODeviceType& type, juce::AudioDeviceManager::AudioDeviceSetup& setup)
{
    const auto typeName = type.getTypeName();
    const auto outputs = type.getDeviceNames (false);
    const auto inputs = type.getDeviceNames (true);

    for (auto& name : outputs)
    {
        if (isDirectHardware (typeName, name) && inputs.contains (name))
        {
            setup.outputDeviceName = setup.inputDeviceName = name;
            return;
        }
    }

    for (auto& name : outputs)
    {
        if (isDirectHardware (typeName, name))
        {
            setup.outputDeviceName = name;
            break;
        }
    }

    for (auto& name : inputs)
    {
        if (isDirectHardware (typeName, name))
        {
            setup.inputDeviceName = name;
            break;
        }
    }
}
} // namespace

bool isDirectHardware (const juce::String& typeName, const juce::String& deviceName)
{
    return typeName == "ALSA HW"
        || deviceName.contains ("[hw:")                                       // JUCE on QNX: "description [id]"
        || deviceName.containsIgnoreCase ("Direct hardware device");          // alsa-lib's description of hw:
}

juce::String openDevice (juce::AudioDeviceManager& manager, const DeviceRequest& request)
{
    auto error = manager.initialise (request.numChannels, request.numChannels, nullptr, true);

    if (error.isNotEmpty())
        return error;

    if (request.typeName.isNotEmpty() && request.typeName != manager.getCurrentAudioDeviceType())
    {
        juce::StringArray typeNames;

        for (auto* type : manager.getAvailableDeviceTypes())
            typeNames.add (type->getTypeName());

        if (! typeNames.contains (request.typeName))
            return "Unknown device type '" + request.typeName + "'. Available: " + typeNames.joinIntoString ("; ");

        manager.setCurrentAudioDeviceType (request.typeName, true);
    }

    auto* type = manager.getCurrentDeviceTypeObject();

    if (type == nullptr)
        return "No audio device type available";

    auto setup = manager.getAudioDeviceSetup();

    if (request.outputName.isEmpty() || request.inputName.isEmpty())
        preferDirectHardware (*type, setup);

    if (request.outputName.isNotEmpty())
        setup.outputDeviceName = resolveName (*type, false, request.outputName, error);

    if (request.inputName.isNotEmpty())
        setup.inputDeviceName = resolveName (*type, true, request.inputName, error);

    if (error.isNotEmpty())
        return error;

    if (request.sampleRate > 0.0)
        setup.sampleRate = request.sampleRate;

    if (request.bufferSize > 0)
        setup.bufferSize = request.bufferSize;

    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.outputChannels.clear();
    setup.inputChannels.setRange (0, request.numChannels, true);
    setup.outputChannels.setRange (0, request.numChannels, true);

    error = manager.setAudioDeviceSetup (setup, true);

    if (error.isEmpty() && manager.getCurrentAudioDevice() == nullptr)
        error = "No audio device could be opened";

    return error;
}

juce::String describeDevices (juce::AudioDeviceManager& manager)
{
    juce::String text;

    for (auto* type : manager.getAvailableDeviceTypes())
    {
        text << "Type: " << type->getTypeName() << juce::newLine;

        for (const auto isInput : { false, true })
        {
            text << (isInput ? "  Inputs:" : "  Outputs:") << juce::newLine;

            for (auto& name : type->getDeviceNames (isInput))
                text << "    " << name << juce::newLine;
        }
    }

    return text;
}

juce::StringArray getDeviceWarnings (const DeviceInfo& info)
{
    juce::StringArray warnings;

    if (info.typeName.startsWith ("ALSA") && ! isDirectHardware (info.typeName, info.name))
        warnings.add ("Device '" + info.name + "' is not a hw: device, so ALSA plugins or a sound server may "
                      "convert, resample or mix; use a hw: device for bit-exact tests");

    return warnings;
}

} // namespace RoundTrip
