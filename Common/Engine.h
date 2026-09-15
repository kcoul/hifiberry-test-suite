#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "AudioClip.h"

namespace RoundTrip
{
// Recording continues this long after the stimulus ends; it also bounds the latency search.
constexpr int defaultTailMs = 2000;

struct DeviceInfo
{
    juce::String typeName;
    juce::String name;
    double sampleRate = 0.0;
    int bufferSize = 0;
    int bitDepth = 0;
    int numInputs = 0;
    int numOutputs = 0;
    int reportedInputLatency = 0;
    int reportedOutputLatency = 0;
};

struct BlockStamp
{
    juce::int64 captureIndex = 0;
    juce::int64 ticks = 0;
};

struct Take
{
    juce::AudioBuffer<float> capture;
    std::vector<BlockStamp> blocks;
    bool blockLogOverflowed = false;
    DeviceInfo device;
    juce::int64 pressTicks = 0;
    juce::int64 firstCallbackTicks = 0;
    int xruns = 0;
    bool coldStart = false;
};

// Plays a stimulus and records the inputs sample-synchronously: output sample n and
// capture sample n are always handled in the same device callback.
class Engine final : public juce::AudioIODeviceCallback
{
public:
    Engine() = default;

    // For a cold start, close the device before arming and restart it afterwards.
    juce::Result arm (const AudioClip& stimulus, int tailSamples, bool coldStart);
    void cancel();

    bool isBusy() const noexcept;
    bool isFinished() const noexcept;
    double getProgress() const noexcept;
    juce::String getLastError() const;

    Take takeResult();
    DeviceInfo getDeviceInfo() const;

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    enum State { idle, armed, running, finished };

    void abort (const juce::String& reason);

    std::atomic<int> state { idle };
    std::atomic<juce::int64> position { 0 }, totalSamples { 0 };
    std::atomic<juce::AudioIODevice*> currentDevice { nullptr };

    // The audio thread only ever try-locks this, so it never blocks.
    juce::CriticalSection audioLock;
    juce::AudioBuffer<float> stimulus, capture;
    std::vector<BlockStamp> blocks;
    size_t numBlocks = 0;
    juce::int64 pressTicks = 0, firstCallbackTicks = 0;
    double armedSampleRate = 0.0;
    int xrunsAtStart = 0, xrunsAtEnd = 0;
    bool coldStart = false;

    mutable juce::CriticalSection infoLock;
    DeviceInfo deviceInfo;
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};

} // namespace RoundTrip
