#pragma once

#include "AudioClip.h"
#include "Engine.h"

namespace RoundTrip
{
struct MismatchEvent
{
    juce::int64 start = 0;  // stimulus sample index
    juce::int64 length = 0;
    int maxAbsError = 0;    // LSBs at the comparison bit depth
};

struct Slip
{
    juce::int64 stimulusIndex = 0;  // first stimulus sample aligned at lagAfter
    juce::int64 lagBefore = 0;
    juce::int64 lagAfter = 0;
};

struct ChannelResult
{
    juce::int64 lag = -1;
    double correlation = 0.0;
    bool inverted = false;

    juce::int64 compared = 0;
    juce::int64 mismatches = 0;
    juce::int64 fullScaleClamps = 0;
    int maxAbsError = 0;
    double gainDb = 0.0;
    double residualDb = 0.0;
    std::vector<MismatchEvent> events;
    bool eventsTruncated = false;

    juce::int64 preRollNonZero = 0;
    juce::int64 postRollNonZero = 0;
};

struct Analysis
{
    juce::String error;
    int compareBits = 0;
    double sampleRate = 0.0;
    juce::int64 stimulusLength = 0;
    juce::int64 lag = -1;           // alignment at the start of the take
    std::vector<Slip> slips;        // alignment changes, in stimulus order
    std::vector<ChannelResult> channels;
    juce::StringArray warnings;

    bool isValid() const noexcept { return error.isEmpty(); }
    bool isBitExact() const noexcept;
    juce::int64 getFinalLag() const noexcept { return slips.empty() ? lag : slips.back().lagAfter; }
    juce::String getVerdict() const;
};

// Follows the piecewise alignment for stimulus indices visited in increasing order.
class LagCursor
{
public:
    explicit LagCursor (const Analysis& a) noexcept : analysis (a), current (a.lag) {}

    juce::int64 at (juce::int64 stimulusIndex) noexcept
    {
        while (next < analysis.slips.size() && analysis.slips[next].stimulusIndex <= stimulusIndex)
            current = analysis.slips[next++].lagAfter;

        return current;
    }

private:
    const Analysis& analysis;
    juce::int64 current;
    size_t next = 0;
};

struct Timing
{
    bool valid = false;
    double pressToFirstCallbackMs = 0.0;
    double pressToLastRoundTripMs = 0.0;
    double stimulusMs = 0.0;
    double overheadMs = 0.0;
};

Analysis analyse (const AudioClip& stimulus, const juce::AudioBuffer<float>& capture, juce::int64 maxLag);

Timing measureTiming (const Take& take, const Analysis& analysis);

double normalisedCorrelation (const float* x, const float* y, int numSamples) noexcept;

} // namespace RoundTrip
