#include "Comparison.h"
#include "Report.h"

namespace RoundTrip
{
namespace
{
constexpr int eventMergeGap = 32;
constexpr size_t maxDifferences = 1000;
constexpr size_t maxDifferencesInSummary = 10;

juce::String describe (const Analysis& a)
{
    if (! a.isValid())
        return "could not be aligned (" + a.error + ")";

    juce::int64 mismatches = 0;
    size_t events = 0;

    for (auto& c : a.channels)
    {
        mismatches += c.mismatches;
        events += c.events.size();
    }

    return a.getVerdict().toLowerCase() + " (" + juce::String (mismatches) + " differing samples in "
         + juce::String ((int) events) + " events, " + juce::String ((int) a.slips.size()) + " slips)";
}

juce::String describeReport (const juce::var& report)
{
    if (! report.isObject())
        return "no JSON report found";

    const auto device = report["device"];
    const auto timing = report["timing"];

    juce::String text;
    text << report["host"]["name"].toString() << " (" << report["host"]["os"].toString() << ")";

    if (device.isObject())
        text << ", " << device["type"].toString() << " / " << device["name"].toString()
             << ", buffer " << device["bufferSize"].toString() << ", xruns " << report["xruns"].toString();

    if (timing.isObject())
        text << ", press -> last round trip " << juce::String ((double) timing["pressToLastRoundTripMs"], 1) << " ms ("
             << ((bool) report["coldStart"] ? "cold" : "warm") << " start)";

    return text;
}
} // namespace

Comparison compareRecordings (const AudioClip& stimulus,
                              const AudioClip& recordingA,
                              const AudioClip& recordingB,
                              juce::int64 maxLag)
{
    Comparison result;
    result.a = analyse (stimulus, recordingA.audio, maxLag);
    result.b = analyse (stimulus, recordingB.audio, maxLag);

    if (! result.a.isValid() || ! result.b.isValid())
    {
        result.verdict = "ERROR: A " + describe (result.a) + "; B " + describe (result.b);
        return result;
    }

    const auto bits = stimulus.getCompareBits();
    const auto numChannels = stimulus.getNumChannels();
    const auto lengthA = (juce::int64) recordingA.getNumSamples();
    const auto lengthB = (juce::int64) recordingB.getNumSamples();

    juce::int64 lastDifference = -eventMergeGap - 1;
    auto storing = false;
    LagCursor cursorA (result.a), cursorB (result.b);

    for (int n = 0; n < stimulus.getNumSamples(); ++n)
    {
        const auto ia = n + cursorA.at (n);
        const auto ib = n + cursorB.at (n);
        auto maxError = -1;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto va = (ia >= 0 && ia < lengthA) ? quantise (recordingA.audio.getReadPointer (ch)[ia], bits) : 0;
            const auto vb = (ib >= 0 && ib < lengthB) ? quantise (recordingB.audio.getReadPointer (ch)[ib], bits) : 0;

            ++result.compared;

            if (va != vb)
            {
                ++result.differing;
                maxError = juce::jmax (maxError, std::abs (va - vb));
            }
        }

        if (maxError < 0)
            continue;

        if (n - lastDifference > eventMergeGap)
        {
            storing = result.differences.size() < maxDifferences;

            if (storing)
                result.differences.push_back ({ n, 0, 0 });
        }

        if (storing)
        {
            auto& e = result.differences.back();
            e.length = n - e.start + 1;
            e.maxAbsError = juce::jmax (e.maxAbsError, maxError);
        }

        lastDifference = n;
    }

    const auto exactA = result.a.isBitExact();
    const auto exactB = result.b.isBitExact();

    if (exactA && exactB)
        result.verdict = "EQUAL: both recordings are bit-exact";
    else if (result.differing == 0)
        result.verdict = "EQUAL: the recordings are identical to each other, but neither is bit-exact to the stimulus";
    else if (exactA)
        result.verdict = "DIFFERENT: A is bit-exact; B is " + describe (result.b);
    else if (exactB)
        result.verdict = "DIFFERENT: B is bit-exact; A is " + describe (result.a);
    else
        result.verdict = "DIFFERENT: A is " + describe (result.a) + "; B is " + describe (result.b);

    result.equalIntegrity = (exactA && exactB) || result.differing == 0;
    return result;
}

juce::String formatComparison (const Comparison& c,
                               const juce::String& nameA, const juce::var& reportA,
                               const juce::String& nameB, const juce::var& reportB)
{
    juce::String text;

    text << "Verdict:   " << c.verdict << juce::newLine
         << "A:         " << nameA << juce::newLine
         << "           " << describeReport (reportA) << juce::newLine
         << "B:         " << nameB << juce::newLine
         << "           " << describeReport (reportB) << juce::newLine;

    if (c.a.isValid() && c.b.isValid())
    {
        text << "A vs B:    " << c.differing << " of " << c.compared << " channel-samples differ" << juce::newLine;

        for (size_t i = 0; i < c.differences.size() && i < maxDifferencesInSummary; ++i)
        {
            const auto& e = c.differences[i];
            text << "    at " << juce::String ((double) e.start / c.a.sampleRate, 3) << " s (sample " << e.start
                 << "): " << e.length << " samples, max " << e.maxAbsError << " LSB" << juce::newLine;
        }
    }

    text << juce::newLine << "--- A ---" << juce::newLine << formatAnalysis (c.a)
         << juce::newLine << "--- B ---" << juce::newLine << formatAnalysis (c.b);

    return text;
}

juce::var comparisonToJson (const Comparison& c)
{
    juce::Array<juce::var> differences;

    for (auto& e : c.differences)
    {
        auto* event = new juce::DynamicObject();
        event->setProperty ("start", e.start);
        event->setProperty ("length", e.length);
        event->setProperty ("maxAbsError", e.maxAbsError);
        differences.add (juce::var (event));
    }

    auto* object = new juce::DynamicObject();
    object->setProperty ("verdict", c.verdict);
    object->setProperty ("equalIntegrity", c.equalIntegrity);
    object->setProperty ("compared", c.compared);
    object->setProperty ("differing", c.differing);
    object->setProperty ("differences", differences);
    object->setProperty ("a", analysisToJson (c.a));
    object->setProperty ("b", analysisToJson (c.b));
    return juce::var (object);
}

} // namespace RoundTrip
