#include "Engine.h"

namespace RoundTrip
{
namespace
{
constexpr int smallestExpectedBlock = 16;

void clearOutputs (float* const* outputs, int numOutputs, int start, int numSamples)
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr && numSamples > start)
            std::fill (outputs[ch] + start, outputs[ch] + numSamples, 0.0f);
}
} // namespace

juce::Result Engine::arm (const AudioClip& clip, int tailSamples, bool cold)
{
    const auto info = getDeviceInfo();
    const auto numChannels = clip.getNumChannels();

    if (info.sampleRate <= 0.0)
        return juce::Result::fail ("No audio device has been opened");

    if (! juce::approximatelyEqual (info.sampleRate, clip.sampleRate))
        return juce::Result::fail ("Device runs at " + juce::String (info.sampleRate) + " Hz but the stimulus is "
                                   + juce::String (clip.sampleRate) + " Hz; resampling would defeat the test");

    if (info.numInputs < numChannels || info.numOutputs < numChannels)
        return juce::Result::fail ("Stimulus has " + juce::String (numChannels) + " channels but the device has "
                                   + juce::String (info.numInputs) + " inputs and "
                                   + juce::String (info.numOutputs) + " outputs active");

    const juce::ScopedLock sl (audioLock);

    if (state == running)
        return juce::Result::fail ("A take is already running");

    stimulus.makeCopyOf (clip.audio);
    capture.setSize (numChannels, clip.getNumSamples() + tailSamples);
    capture.clear();
    blocks.assign ((size_t) (capture.getNumSamples() / smallestExpectedBlock + 16), {});
    numBlocks = 0;
    position = 0;
    totalSamples = capture.getNumSamples();
    armedSampleRate = clip.sampleRate;
    coldStart = cold;
    xrunsAtStart = xrunsAtEnd = 0;

    {
        const juce::ScopedLock il (infoLock);
        lastError.clear();
    }

    pressTicks = juce::Time::getHighResolutionTicks();
    state = armed;
    return juce::Result::ok();
}

void Engine::cancel()
{
    const juce::ScopedLock sl (audioLock);
    state = idle;
}

bool Engine::isBusy() const noexcept
{
    const auto s = state.load();
    return s == armed || s == running;
}

bool Engine::isFinished() const noexcept
{
    return state == finished;
}

double Engine::getProgress() const noexcept
{
    const auto total = totalSamples.load();
    return total > 0 ? juce::jlimit (0.0, 1.0, (double) position.load() / (double) total) : 0.0;
}

juce::String Engine::getLastError() const
{
    const juce::ScopedLock il (infoLock);
    return lastError;
}

DeviceInfo Engine::getDeviceInfo() const
{
    const juce::ScopedLock il (infoLock);
    return deviceInfo;
}

Take Engine::takeResult()
{
    const juce::ScopedLock sl (audioLock);
    jassert (state == finished);

    Take take;
    take.capture = std::move (capture);
    take.blockLogOverflowed = numBlocks == blocks.size();
    blocks.resize (numBlocks);
    take.blocks = std::move (blocks);
    take.device = getDeviceInfo();
    take.pressTicks = pressTicks;
    take.firstCallbackTicks = firstCallbackTicks;
    take.xruns = xrunsAtEnd - xrunsAtStart;
    take.coldStart = coldStart;

    stimulus.setSize (0, 0);
    capture = {};
    blocks = {};
    state = idle;
    return take;
}

void Engine::abort (const juce::String& reason)
{
    const juce::ScopedLock sl (audioLock);

    if (state == running || state == armed)
    {
        state = idle;
        const juce::ScopedLock il (infoLock);
        lastError = reason;
    }
}

void Engine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    DeviceInfo info;
    info.typeName = device->getTypeName();
    info.name = device->getName();
    info.sampleRate = device->getCurrentSampleRate();
    info.bufferSize = device->getCurrentBufferSizeSamples();
    info.bitDepth = device->getCurrentBitDepth();
    info.numInputs = device->getActiveInputChannels().countNumberOfSetBits();
    info.numOutputs = device->getActiveOutputChannels().countNumberOfSetBits();
    info.reportedInputLatency = device->getInputLatencyInSamples();
    info.reportedOutputLatency = device->getOutputLatencyInSamples();

    {
        const juce::ScopedLock il (infoLock);
        deviceInfo = info;
    }

    currentDevice = device;

    if (state == running)
        abort ("The audio device restarted during the take");
    else if (state == armed && ! juce::approximatelyEqual (info.sampleRate, armedSampleRate))
        abort ("The audio device reopened at " + juce::String (info.sampleRate) + " Hz while armed");
}

void Engine::audioDeviceStopped()
{
    currentDevice = nullptr;

    if (state == running)
        abort ("The audio device stopped during the take");
}

void Engine::audioDeviceIOCallbackWithContext (const float* const* inputs,
                                               int numInputs,
                                               float* const* outputs,
                                               int numOutputs,
                                               int numSamples,
                                               const juce::AudioIODeviceCallbackContext&)
{
    const auto now = juce::Time::getHighResolutionTicks();
    const juce::ScopedTryLock sl (audioLock);
    const auto current = state.load();

    if (! sl.isLocked() || current == idle || current == finished)
    {
        clearOutputs (outputs, numOutputs, 0, numSamples);
        return;
    }

    if (current == armed)
    {
        firstCallbackTicks = now;

        if (auto* device = currentDevice.load())
            xrunsAtStart = device->getXRunCount();

        state = running;
    }

    const auto pos = position.load();
    const auto total = (juce::int64) capture.getNumSamples();
    const auto numToCapture = (int) juce::jmin ((juce::int64) numSamples, total - pos);

    if (numBlocks < blocks.size())
        blocks[numBlocks++] = { pos, now };

    // Capture before writing outputs: some devices alias the input and output buffers.
    for (int ch = 0; ch < capture.getNumChannels(); ++ch)
        if (ch < numInputs && inputs[ch] != nullptr)
            capture.copyFrom (ch, (int) pos, inputs[ch], numToCapture);

    const auto numToPlay = (int) juce::jlimit ((juce::int64) 0,
                                               (juce::int64) numSamples,
                                               (juce::int64) stimulus.getNumSamples() - pos);

    for (int ch = 0; ch < numOutputs; ++ch)
    {
        if (outputs[ch] == nullptr)
            continue;

        const auto played = ch < stimulus.getNumChannels() ? numToPlay : 0;

        if (played > 0)
            std::copy_n (stimulus.getReadPointer (ch, (int) pos), played, outputs[ch]);

        std::fill (outputs[ch] + played, outputs[ch] + numSamples, 0.0f);
    }

    position = pos + numSamples;

    if (pos + numSamples >= total)
    {
        if (auto* device = currentDevice.load())
            xrunsAtEnd = device->getXRunCount();

        state = finished;
    }
}

} // namespace RoundTrip
