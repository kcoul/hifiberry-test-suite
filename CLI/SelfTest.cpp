#include <iostream>

#include "Comparison.h"
#include "Report.h"

namespace
{
using namespace RoundTrip;

constexpr double testRate = 48000.0;
constexpr int testLength = 4 * 48000;
constexpr int testLag = 1234;
constexpr int testTail = 24000;

int failures = 0;

void check (bool condition, const juce::String& description)
{
    std::cout << (condition ? "  PASS  " : "  FAIL  ") << description << std::endl;

    if (! condition)
        ++failures;
}

AudioClip makeNoise()
{
    AudioClip clip;
    clip.file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("hifiberry_selftest.wav");
    clip.sampleRate = testRate;
    clip.bitsPerSample = 16;
    clip.audio.setSize (2, testLength);

    juce::Random random (1234);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < testLength; ++i)
            clip.audio.setSample (ch, i, (float) random.nextInt ({ -32768, 32768 }) / 32768.0f);

    clip.audio.setSample (0, 1000, -1.0f);
    return clip;
}

juce::AudioBuffer<float> loopback (const AudioClip& stimulus, int delay)
{
    juce::AudioBuffer<float> capture (stimulus.getNumChannels(), testLength + testTail);
    capture.clear();

    for (int ch = 0; ch < stimulus.getNumChannels(); ++ch)
        capture.copyFrom (ch, delay, stimulus.audio, ch, 0, testLength);

    return capture;
}

void testClean (const AudioClip& stimulus)
{
    std::cout << "Clean loopback" << std::endl;
    const auto a = analyse (stimulus, loopback (stimulus, testLag), testTail);

    check (a.lag == testLag, "latency is " + juce::String (a.lag) + " samples");
    check (a.isBitExact(), "verdict is " + a.getVerdict());
    check (a.warnings.isEmpty(), "no warnings: " + a.warnings.joinIntoString (" | "));
}

void testGlitch (const AudioClip& stimulus)
{
    std::cout << "Ten corrupted samples on channel 1 at 2 s" << std::endl;
    auto capture = loopback (stimulus, testLag);

    for (int i = 0; i < 10; ++i)
        capture.setSample (1, testLag + 96000 + i, 0.123f);

    const auto a = analyse (stimulus, capture, testTail);

    check (a.getVerdict() == "NOT BIT-EXACT", "verdict is " + a.getVerdict());
    check (a.channels[0].mismatches == 0, "channel 0 is untouched");
    check (a.channels[1].mismatches == 10, "channel 1 has " + juce::String (a.channels[1].mismatches) + " mismatches");
    check (a.channels[1].events.size() == 1 && a.channels[1].events[0].start == 96000,
           "one event, at stimulus sample 96000");
    check (a.slips.empty(), "no slips");
}

void testSlip (const AudioClip& stimulus)
{
    std::cout << "256 samples dropped at 3.5 s" << std::endl;
    constexpr int slipAt = 168000, dropped = 256;

    auto capture = loopback (stimulus, testLag);

    for (int ch = 0; ch < 2; ++ch)
    {
        capture.copyFrom (ch, testLag + slipAt, stimulus.audio, ch, slipAt + dropped, testLength - slipAt - dropped);
        capture.clear (ch, testLag + testLength - dropped, dropped);
    }

    const auto a = analyse (stimulus, capture, testTail);

    check (a.getVerdict() == "SLIPPED", "verdict is " + a.getVerdict());
    check (a.slips.size() == 1 && a.slips[0].lagAfter == testLag - dropped,
           "one slip of -" + juce::String (dropped) + " samples");
    check (a.slips.size() == 1 && a.slips[0].stimulusIndex >= slipAt && a.slips[0].stimulusIndex <= slipAt + dropped,
           "slip located at stimulus sample " + juce::String (a.slips.empty() ? -1 : a.slips[0].stimulusIndex));

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& events = a.channels[(size_t) ch].events;
        check (events.size() == 1 && events[0].start >= slipAt && events[0].start + events[0].length <= slipAt + dropped,
               "channel " + juce::String (ch) + " mismatches are only the dropped samples ("
                   + juce::String (a.channels[(size_t) ch].mismatches) + ")");
    }
}

void testInvertedAndStale (const AudioClip& stimulus)
{
    std::cout << "Channel 1 inverted, stale buffer after the stimulus" << std::endl;
    auto capture = loopback (stimulus, testLag);
    capture.applyGain (1, 0, capture.getNumSamples(), -1.0f);
    capture.copyFrom (0, testLag + testLength + 100, stimulus.audio, 0, 0, 512);

    const auto a = analyse (stimulus, capture, testTail);

    check (a.channels[1].inverted, "channel 1 reported as inverted");
    check (! a.isBitExact(), "not bit-exact");
    check (a.channels[0].postRollNonZero > 0, "stale samples found after the stimulus");
}

void testFileRoundTrip (const AudioClip& stimulus)
{
    std::cout << "16-bit WAV write/read" << std::endl;
    const auto file = stimulus.file.getSiblingFile ("hifiberry_selftest_RTL.wav");

    AudioClip loaded;
    const auto written = writeClip (file, stimulus.audio, testRate, 16, false);
    const auto read = loadClip (file, loaded);
    file.deleteFile();

    check (written.wasOk() && read.wasOk(), "wrote and re-read " + file.getFullPathName());

    auto identical = loaded.getNumSamples() == testLength;

    for (int ch = 0; identical && ch < 2; ++ch)
        for (int i = 0; identical && i < testLength; ++i)
            identical = quantise (loaded.audio.getSample (ch, i), 16) == quantise (stimulus.audio.getSample (ch, i), 16);

    check (identical, "every sample survives, including -32768");
}

void testComparison (const AudioClip& stimulus)
{
    std::cout << "Comparing two recordings" << std::endl;

    AudioClip clean, laterClean, glitched;

    for (auto* clip : { &clean, &laterClean, &glitched })
    {
        clip->sampleRate = testRate;
        clip->bitsPerSample = 16;
    }

    clean.audio = loopback (stimulus, testLag);
    laterClean.audio = loopback (stimulus, testLag + 480);
    glitched.audio = loopback (stimulus, testLag);
    glitched.audio.setSample (0, testLag + 50000, 0.5f);

    const auto same = compareRecordings (stimulus, clean, laterClean, testTail);
    check (same.equalIntegrity, "different latencies, both clean: " + same.verdict);

    const auto different = compareRecordings (stimulus, clean, glitched, testTail);
    check (! different.equalIntegrity && different.differing == 1, "one glitch: " + different.verdict);
}

void testTiming (const AudioClip& stimulus)
{
    std::cout << "Timing from block stamps" << std::endl;
    constexpr int blockSize = 512;

    Take take;
    take.capture = loopback (stimulus, testLag);
    take.device.sampleRate = testRate;
    take.pressTicks = 1000;

    const auto ticksPerSample = (double) juce::Time::getHighResolutionTicksPerSecond() / testRate;
    const auto startupTicks = (juce::int64) (0.005 * (double) juce::Time::getHighResolutionTicksPerSecond());
    take.firstCallbackTicks = take.pressTicks + startupTicks;

    for (juce::int64 i = 0; i < take.capture.getNumSamples(); i += blockSize)
        take.blocks.push_back ({ i, take.firstCallbackTicks + (juce::int64) ((double) i * ticksPerSample) });

    const auto analysis = analyse (stimulus, take.capture, testTail);
    const auto timing = measureTiming (take, analysis);

    const auto lastIndex = testLength - 1 + testLag;
    const auto expectedMs = 5.0 + 1000.0 * (double) (lastIndex - lastIndex % blockSize) / testRate;

    check (timing.valid && std::abs (timing.pressToFirstCallbackMs - 5.0) < 0.01, "press -> first callback is 5 ms");
    check (timing.valid && std::abs (timing.pressToLastRoundTripMs - expectedMs) < 0.01,
           "press -> last round trip is " + juce::String (timing.pressToLastRoundTripMs, 3) + " ms, expected "
               + juce::String (expectedMs, 3));
}

} // namespace

int runSelfTest()
{
    const auto stimulus = makeNoise();

    testClean (stimulus);
    testGlitch (stimulus);
    testSlip (stimulus);
    testInvertedAndStale (stimulus);
    testFileRoundTrip (stimulus);
    testComparison (stimulus);
    testTiming (stimulus);

    std::cout << (failures == 0 ? "All checks passed" : juce::String (failures) + " checks failed") << std::endl;
    return failures == 0 ? 0 : 2;
}
