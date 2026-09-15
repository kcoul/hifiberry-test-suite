#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace RoundTrip
{
struct AudioClip
{
    juce::File file;
    juce::AudioBuffer<float> audio;
    double sampleRate = 0.0;
    int bitsPerSample = 0;
    bool isFloat = false;

    int getNumChannels() const noexcept { return audio.getNumChannels(); }
    int getNumSamples() const noexcept  { return audio.getNumSamples(); }

    // JUCE carries samples as float32, which is only lossless for integer formats up to 24 bits.
    int getCompareBits() const noexcept { return (isFloat || bitsPerSample > 24) ? 24 : bitsPerSample; }
};

juce::Result loadClip (const juce::File& file, AudioClip& result);

juce::Result writeClip (const juce::File& file,
                        const juce::AudioBuffer<float>& audio,
                        double sampleRate,
                        int bitsPerSample,
                        bool isFloat);

juce::File getRecordingFileFor (const juce::File& stimulus);
juce::File getReportFileFor (const juce::File& stimulus);

inline int quantise (float sample, int bits) noexcept
{
    const auto scale = (double) (1 << (bits - 1));
    return (int) juce::jlimit (-scale, scale - 1.0, std::round ((double) sample * scale));
}

} // namespace RoundTrip
