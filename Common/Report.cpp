#include "Report.h"
#include "Devices.h"

namespace RoundTrip
{
namespace
{
constexpr int maxEventsInSummary = 10;

using Property = std::pair<juce::Identifier, juce::var>;

juce::var makeObject (std::initializer_list<Property> properties)
{
    auto* object = new juce::DynamicObject();

    for (auto& [name, value] : properties)
        object->setProperty (name, value);

    return juce::var (object);
}

juce::String seconds (juce::int64 samples, double sampleRate)
{
    return juce::String ((double) samples / sampleRate, 3) + " s";
}

juce::String decibels (double db)
{
    if (db <= -300.0)
        return "-inf dB";

    return (db >= 0.0 ? "+" : "") + juce::String (db, 3) + " dB";
}

} // namespace

juce::var analysisToJson (const Analysis& a)
{
    juce::Array<juce::var> channels, slips, warnings;

    for (auto& c : a.channels)
    {
        juce::Array<juce::var> events;

        for (auto& e : c.events)
            events.add (makeObject ({ { "start", e.start },
                                      { "startSeconds", (double) e.start / a.sampleRate },
                                      { "length", e.length },
                                      { "maxAbsError", e.maxAbsError } }));

        channels.add (makeObject ({ { "lag", c.lag },
                                    { "correlation", c.correlation },
                                    { "inverted", c.inverted },
                                    { "compared", c.compared },
                                    { "mismatches", c.mismatches },
                                    { "fullScaleClamps", c.fullScaleClamps },
                                    { "maxAbsError", c.maxAbsError },
                                    { "gainDb", c.gainDb },
                                    { "residualDb", c.residualDb },
                                    { "preRollNonZero", c.preRollNonZero },
                                    { "postRollNonZero", c.postRollNonZero },
                                    { "eventsTruncated", c.eventsTruncated },
                                    { "events", events } }));
    }

    for (auto& s : a.slips)
        slips.add (makeObject ({ { "stimulusIndex", s.stimulusIndex },
                                 { "seconds", (double) s.stimulusIndex / a.sampleRate },
                                 { "lagBefore", s.lagBefore },
                                 { "lagAfter", s.lagAfter } }));

    for (auto& w : a.warnings)
        warnings.add (w);

    return makeObject ({ { "verdict", a.getVerdict() },
                         { "bitExact", a.isBitExact() },
                         { "error", a.error },
                         { "compareBits", a.compareBits },
                         { "latencySamples", a.lag },
                         { "latencyMs", a.sampleRate > 0.0 ? (double) a.lag * 1000.0 / a.sampleRate : 0.0 },
                         { "channels", channels },
                         { "slips", slips },
                         { "warnings", warnings } });
}

juce::var buildReport (const AudioClip& stimulus,
                       const juce::File& recording,
                       const Analysis& analysis,
                       const Take* take,
                       const Timing* timing)
{
    auto report = makeObject ({
        { "tool", "HiFiBerryTestSuite" },
        { "formatVersion", 1 },
        { "createdAt", juce::Time::getCurrentTime().toISO8601 (true) },
        { "host", makeObject ({ { "name", juce::SystemStats::getComputerName() },
                                { "os", juce::SystemStats::getOperatingSystemName() } }) },
        { "stimulus", makeObject ({ { "file", stimulus.file.getFullPathName() },
                                    { "sampleRate", stimulus.sampleRate },
                                    { "bitsPerSample", stimulus.bitsPerSample },
                                    { "isFloat", stimulus.isFloat },
                                    { "channels", stimulus.getNumChannels() },
                                    { "samples", stimulus.getNumSamples() } }) },
        { "recording", recording.getFullPathName() },
        { "analysis", analysisToJson (analysis) }
    });

    auto* object = report.getDynamicObject();

    if (take != nullptr)
    {
        const auto& d = take->device;

        object->setProperty ("device", makeObject ({ { "type", d.typeName },
                                                     { "name", d.name },
                                                     { "sampleRate", d.sampleRate },
                                                     { "bufferSize", d.bufferSize },
                                                     { "bitDepth", d.bitDepth },
                                                     { "inputs", d.numInputs },
                                                     { "outputs", d.numOutputs },
                                                     { "reportedInputLatency", d.reportedInputLatency },
                                                     { "reportedOutputLatency", d.reportedOutputLatency } }));
        object->setProperty ("coldStart", take->coldStart);
        object->setProperty ("xruns", take->xruns);
    }

    if (timing != nullptr && timing->valid)
        object->setProperty ("timing", makeObject ({ { "pressToFirstCallbackMs", timing->pressToFirstCallbackMs },
                                                     { "pressToLastRoundTripMs", timing->pressToLastRoundTripMs },
                                                     { "stimulusMs", timing->stimulusMs },
                                                     { "overheadMs", timing->overheadMs } }));

    return report;
}

juce::String formatAnalysis (const Analysis& a)
{
    juce::String text;
    text << "Verdict:   " << a.getVerdict() << juce::newLine;

    if (! a.isValid())
        return text << "Error:     " << a.error << juce::newLine;

    text << "Latency:   " << a.lag << " samples (" << juce::String ((double) a.lag * 1000.0 / a.sampleRate, 3)
         << " ms), compared at " << a.compareBits << "-bit" << juce::newLine;

    for (size_t ch = 0; ch < a.channels.size(); ++ch)
    {
        const auto& c = a.channels[ch];

        text << "Channel " << (int) ch << ": " << c.mismatches << " of " << c.compared << " samples differ, max error "
             << c.maxAbsError << " LSB, gain " << decibels (c.gainDb) << ", residual " << decibels (c.residualDb)
             << juce::newLine;

        for (size_t i = 0; i < c.events.size() && i < (size_t) maxEventsInSummary; ++i)
        {
            const auto& e = c.events[i];
            text << "    at " << seconds (e.start, a.sampleRate) << " (sample " << e.start << "): " << e.length
                 << " samples, max " << e.maxAbsError << " LSB" << juce::newLine;
        }

        if (c.events.size() > (size_t) maxEventsInSummary || c.eventsTruncated)
            text << "    ... " << (c.eventsTruncated ? "more than " : "") << (int) c.events.size()
                 << " events in total" << juce::newLine;
    }

    for (auto& s : a.slips)
        text << "Slip:      near " << seconds (s.stimulusIndex, a.sampleRate) << ", lag " << s.lagBefore << " -> "
             << s.lagAfter << " (" << (s.lagAfter > s.lagBefore ? "+" : "") << (s.lagAfter - s.lagBefore)
             << " samples)" << juce::newLine;

    for (auto& w : a.warnings)
        text << "Warning:   " << w << juce::newLine;

    return text;
}

juce::String formatSummary (const AudioClip& stimulus,
                            const Analysis& analysis,
                            const Take* take,
                            const Timing* timing)
{
    juce::String text;

    text << "Stimulus:  " << stimulus.file.getFileName() << ", " << stimulus.getNumChannels() << " ch, "
         << stimulus.bitsPerSample << "-bit" << (stimulus.isFloat ? " float" : "") << ", "
         << juce::String (stimulus.sampleRate, 0) << " Hz, " << seconds (stimulus.getNumSamples(), stimulus.sampleRate)
         << juce::newLine;

    if (take != nullptr)
    {
        const auto& d = take->device;
        text << "Device:    " << d.typeName << " / " << d.name << ", buffer " << d.bufferSize << ", " << d.bitDepth
             << "-bit, reported latency in " << d.reportedInputLatency << " / out " << d.reportedOutputLatency
             << juce::newLine
             << "XRuns:     " << take->xruns << juce::newLine;
    }

    if (timing != nullptr && timing->valid)
        text << "Timing:    " << (take != nullptr && take->coldStart ? "cold" : "warm") << " start; press -> first callback "
             << juce::String (timing->pressToFirstCallbackMs, 1) << " ms, press -> last round trip "
             << juce::String (timing->pressToLastRoundTripMs, 1) << " ms (stimulus "
             << juce::String (timing->stimulusMs, 1) << " ms + " << juce::String (timing->overheadMs, 1) << " ms)"
             << juce::newLine;

    return text + formatAnalysis (analysis);
}

juce::Result writeJson (const juce::File& file, const juce::var& json)
{
    return file.replaceWithText (juce::JSON::toString (json))
             ? juce::Result::ok()
             : juce::Result::fail ("Failed writing " + file.getFullPathName());
}

int getExitCode (const Analysis& analysis) noexcept
{
    if (! analysis.isValid())
        return 1;

    return analysis.isBitExact() ? 0 : 2;
}

Outcome processTake (const AudioClip& stimulus, const Take& take, juce::int64 maxLag)
{
    Outcome outcome;
    outcome.analysis = analyse (stimulus, take.capture, maxLag);
    outcome.analysis.warnings.addArray (getDeviceWarnings (take.device));

    if (take.xruns > 0)
        outcome.analysis.warnings.add (juce::String (take.xruns) + " xruns reported by the device during the take");

    outcome.timing = measureTiming (take, outcome.analysis);
    outcome.recordingFile = getRecordingFileFor (stimulus.file);
    outcome.reportFile = getReportFileFor (stimulus.file);
    outcome.report = buildReport (stimulus, outcome.recordingFile, outcome.analysis, &take, &outcome.timing);
    outcome.summary = formatSummary (stimulus, outcome.analysis, &take, &outcome.timing);

    outcome.writeResult = writeClip (outcome.recordingFile, take.capture, stimulus.sampleRate,
                                     stimulus.bitsPerSample, stimulus.isFloat);

    if (outcome.writeResult.wasOk())
        outcome.writeResult = writeJson (outcome.reportFile, outcome.report);

    return outcome;
}

} // namespace RoundTrip
