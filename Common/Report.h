#pragma once

#include "Analysis.h"

namespace RoundTrip
{
struct Outcome
{
    Analysis analysis;
    Timing timing;
    juce::File recordingFile;
    juce::File reportFile;
    juce::var report;
    juce::String summary;
    juce::Result writeResult = juce::Result::ok();
};

// Analyses a finished take, then writes <stimulus>_RTL.wav and <stimulus>_RTL.json beside the stimulus.
Outcome processTake (const AudioClip& stimulus, const Take& take, juce::int64 maxLag);

// take and timing may be null for offline analysis of an existing recording.
juce::var buildReport (const AudioClip& stimulus,
                       const juce::File& recording,
                       const Analysis& analysis,
                       const Take* take,
                       const Timing* timing);

juce::String formatSummary (const AudioClip& stimulus,
                            const Analysis& analysis,
                            const Take* take,
                            const Timing* timing);

juce::String formatAnalysis (const Analysis& analysis);

juce::var analysisToJson (const Analysis& analysis);

juce::Result writeJson (const juce::File& file, const juce::var& json);

int getExitCode (const Analysis& analysis) noexcept;

} // namespace RoundTrip
