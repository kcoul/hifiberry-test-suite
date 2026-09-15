#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>

#include "Comparison.h"
#include "Devices.h"
#include "Report.h"

int runSelfTest();

namespace
{
using juce::ConsoleApplication;

int exitCode = 0;

void print (const juce::String& text)
{
    std::cout << text << std::endl;
}

RoundTrip::AudioClip loadOrFail (const juce::File& file)
{
    RoundTrip::AudioClip clip;

    if (const auto result = RoundTrip::loadClip (file, clip); result.failed())
        ConsoleApplication::fail (result.getErrorMessage());

    return clip;
}

// ArgumentList::getValueForOption only accepts "--option=value" for long options; also allow "--option value".
juce::String getOption (const juce::ArgumentList& args, const juce::String& option)
{
    for (int i = 0; i < args.size(); ++i)
    {
        const auto& text = args.arguments.getReference (i).text;

        if (text == option && i + 1 < args.size())
            return args.arguments.getReference (i + 1).text.unquoted();

        if (text.startsWith (option + "="))
            return text.fromFirstOccurrenceOf ("=", false, false).unquoted();
    }

    return {};
}

juce::File getExistingFile (const juce::ArgumentList& args, const juce::String& option)
{
    const auto value = getOption (args, option);

    if (value.isEmpty())
        ConsoleApplication::fail ("Expected a filename after " + option);

    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (value);

    if (! file.existsAsFile())
        ConsoleApplication::fail ("Couldn't find file: " + file.getFullPathName());

    return file;
}

int getIntOption (const juce::ArgumentList& args, const juce::String& option, int fallback)
{
    const auto value = getOption (args, option);
    return value.isNotEmpty() ? value.getIntValue() : fallback;
}

juce::int64 getTailSamples (const juce::ArgumentList& args, double sampleRate)
{
    return (juce::int64) (sampleRate * getIntOption (args, "--tail-ms", RoundTrip::defaultTailMs) / 1000.0);
}

juce::var readSidecar (const juce::File& recording)
{
    const auto json = recording.withFileExtension ("json");
    return json.existsAsFile() ? juce::JSON::parse (json) : juce::var();
}

void writeJsonOption (const juce::ArgumentList& args, const juce::var& json)
{
    const auto path = getOption (args, "--json");

    if (path.isEmpty())
        return;

    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (path);

    if (const auto result = RoundTrip::writeJson (file, json); result.failed())
        ConsoleApplication::fail (result.getErrorMessage());

    print ("Wrote " + file.getFullPathName());
}

void listDevices (const juce::ArgumentList&)
{
    juce::AudioDeviceManager manager;
    print (RoundTrip::describeDevices (manager));
}

void run (const juce::ArgumentList& args)
{
    const auto stimulusFile = getExistingFile (args, "--file");
    const auto stimulus = loadOrFail (stimulusFile);
    const auto tail = getTailSamples (args, stimulus.sampleRate);
    const auto cold = args.containsOption ("--cold");

    RoundTrip::DeviceRequest request;
    request.typeName = getOption (args, "--type");
    request.outputName = getOption (args, "--output");
    request.inputName = getOption (args, "--input");
    request.sampleRate = stimulus.sampleRate;
    request.bufferSize = getIntOption (args, "--buffer", 0);
    request.numChannels = stimulus.getNumChannels();

    // Declared before the manager so it outlives the device that calls into it.
    RoundTrip::Engine engine;
    juce::AudioDeviceManager manager;

    if (const auto error = RoundTrip::openDevice (manager, request); error.isNotEmpty())
        ConsoleApplication::fail ("Couldn't open the audio device: " + error);

    manager.addAudioCallback (&engine);

    if (cold)
        manager.closeAudioDevice();

    if (const auto result = engine.arm (stimulus, (int) tail, cold); result.failed())
        ConsoleApplication::fail (result.getErrorMessage());

    if (cold)
    {
        manager.restartLastAudioDevice();

        if (manager.getCurrentAudioDevice() == nullptr)
            ConsoleApplication::fail ("The audio device failed to restart");
    }

    print ("Playing " + stimulusFile.getFullPathName() + " ...");

    const auto expectedMs = 1000.0 * (double) (stimulus.getNumSamples() + tail) / stimulus.sampleRate;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + expectedMs + 10000.0;

    while (engine.isBusy())
    {
        if (juce::Time::getMillisecondCounterHiRes() > deadline)
        {
            const auto progress = engine.getProgress();
            engine.cancel();
            ConsoleApplication::fail ("Timed out " + juce::String (progress * 100.0, 1)
                                      + "% into the take; did the device stop calling back?");
        }

        juce::Thread::sleep (10);
    }

    if (! engine.isFinished())
        ConsoleApplication::fail ("Take aborted: " + engine.getLastError());

    const auto take = engine.takeResult();
    const auto outcome = RoundTrip::processTake (stimulus, take, tail);

    print (outcome.summary);

    if (outcome.writeResult.failed())
        ConsoleApplication::fail (outcome.writeResult.getErrorMessage());

    print ("Wrote " + outcome.recordingFile.getFullPathName() + " and " + outcome.reportFile.getFileName());
    exitCode = RoundTrip::getExitCode (outcome.analysis);
}

void analyse (const juce::ArgumentList& args)
{
    const auto stimulus = loadOrFail (getExistingFile (args, "--file"));
    const auto recordingFile = getOption (args, "--recording").isNotEmpty() ? getExistingFile (args, "--recording")
                                                                            : RoundTrip::getRecordingFileFor (stimulus.file);
    const auto recording = loadOrFail (recordingFile);

    if (! juce::approximatelyEqual (recording.sampleRate, stimulus.sampleRate))
        ConsoleApplication::fail ("Recording and stimulus sample rates differ");

    const auto analysis = RoundTrip::analyse (stimulus, recording.audio, getTailSamples (args, stimulus.sampleRate));

    print (RoundTrip::formatSummary (stimulus, analysis, nullptr, nullptr));
    writeJsonOption (args, RoundTrip::buildReport (stimulus, recordingFile, analysis, nullptr, nullptr));
    exitCode = RoundTrip::getExitCode (analysis);
}

void compare (const juce::ArgumentList& args)
{
    const auto stimulus = loadOrFail (getExistingFile (args, "--file"));
    const auto fileA = getExistingFile (args, "--recording-a");
    const auto fileB = getExistingFile (args, "--recording-b");
    const auto recordingA = loadOrFail (fileA);
    const auto recordingB = loadOrFail (fileB);

    if (! juce::approximatelyEqual (recordingA.sampleRate, stimulus.sampleRate)
        || ! juce::approximatelyEqual (recordingB.sampleRate, stimulus.sampleRate))
        ConsoleApplication::fail ("Recording and stimulus sample rates differ");

    const auto comparison = RoundTrip::compareRecordings (stimulus, recordingA, recordingB,
                                                          getTailSamples (args, stimulus.sampleRate));

    print (RoundTrip::formatComparison (comparison,
                                        fileA.getFullPathName(), readSidecar (fileA),
                                        fileB.getFullPathName(), readSidecar (fileB)));
    writeJsonOption (args, RoundTrip::comparisonToJson (comparison));

    if (! comparison.a.isValid() || ! comparison.b.isValid())
        exitCode = 1;
    else
        exitCode = comparison.equalIntegrity ? 0 : 2;
}

} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    ConsoleApplication app;

    app.addHelpCommand ("--help|-h",
                        "HiFiBerry round-trip audio test suite\n"
                        "Exit codes: 0 bit-exact / equal, 1 error, 2 not bit-exact / different",
                        true);

    app.addVersionCommand ("--version", juce::String ("HiFiBerry Test Suite CLI ") + JUCE_APPLICATION_VERSION_STRING);

    app.addCommand ({ "run",
                      "run --file <stimulus.wav> [--type <device type>] [--output <name>] [--input <name>] "
                      "[--buffer <samples>] [--tail-ms <ms>] [--cold]",
                      "Plays the stimulus, records the loopback, and writes <stimulus>_RTL.wav and _RTL.json beside it",
                      "Device names may be a case-insensitive substring. --cold closes the device and reopens it at the "
                      "start of the take, so start-up time is included in the timing.",
                      run });

    app.addCommand ({ "analyse|analyze",
                      "analyse --file <stimulus.wav> [--recording <recording.wav>] [--tail-ms <ms>] [--json <out.json>]",
                      "Re-analyses an existing recording (defaults to <stimulus>_RTL.wav)",
                      {},
                      analyse });

    app.addCommand ({ "compare",
                      "compare --file <stimulus.wav> --recording-a <a_RTL.wav> --recording-b <b_RTL.wav> "
                      "[--tail-ms <ms>] [--json <out.json>]",
                      "Checks whether two recordings of the same stimulus (e.g. Linux vs QNX) have equal integrity",
                      {},
                      compare });

    app.addCommand ({ "list-devices", "list-devices", "Lists audio device types and devices", {}, listDevices });

    app.addCommand ({ "selftest", "selftest", "Checks the analyser against simulated loopback faults", {},
                      [] (const juce::ArgumentList&) { exitCode = runSelfTest(); } });

    const auto result = app.findAndRunCommand (juce::ArgumentList (argc, argv), true);
    return result != 0 ? result : exitCode;
}
