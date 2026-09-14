class AudioToolbox : public juce::AudioIODeviceCallback
{
public:
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                               int numInputChannels,
                               float* const* outputChannelData,
                               int numOutputChannels,
                               int numSamples,
                               const AudioIODeviceCallbackContext& context) override
    {
        // Process audio here
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {}
    void audioDeviceStopped() override {}
};