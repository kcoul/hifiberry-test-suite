#pragma once

#include "Analysis.h"

namespace RoundTrip
{
// Two recordings of the same stimulus, e.g. the same hat on a Linux Pi and on a QNX Pi.
struct Comparison
{
    Analysis a, b;
    juce::int64 compared = 0;
    juce::int64 differing = 0;            // channel-samples that differ between A and B once each is aligned
    std::vector<MismatchEvent> differences;
    bool equalIntegrity = false;
    juce::String verdict;
};

Comparison compareRecordings (const AudioClip& stimulus,
                              const AudioClip& recordingA,
                              const AudioClip& recordingB,
                              juce::int64 maxLag);

// reportA/reportB are the parsed _RTL.json sidecars, or void if unavailable.
juce::String formatComparison (const Comparison& comparison,
                               const juce::String& nameA, const juce::var& reportA,
                               const juce::String& nameB, const juce::var& reportB);

juce::var comparisonToJson (const Comparison& comparison);

} // namespace RoundTrip
