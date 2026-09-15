#include "MainComponent.h"

namespace AudioApp
{
MainComponent::MainComponent()
{
    addAndMakeVisible (&openButton);
    openButton.onClick = [this] { openButtonClicked(); };

    addAndMakeVisible (&playButton);
    playButton.onClick = [this] { playButtonClicked(); };
    playButton.setColour (juce::TextButton::buttonColourId, juce::Colours::green);

    addAndMakeVisible (&stopButton);
    stopButton.onClick = [this] { stopButtonClicked(); };
    stopButton.setColour (juce::TextButton::buttonColourId, juce::Colours::red);

    addAndMakeVisible (&coldStartToggle);

    addAndMakeVisible (&statusLabel);
    statusLabel.setText ("Open a WAV stimulus to begin", juce::dontSendNotification);

    addAndMakeVisible (&results);
    results.setMultiLine (true);
    results.setReadOnly (true);
    results.setScrollbarsShown (true);
    results.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));

    addAndMakeVisible (selector);

    const auto error = RoundTrip::openDevice (deviceManager, {});
    deviceManager.addAudioCallback (&engine);

    if (error.isNotEmpty())
        showMessage ("Couldn't open an audio device: " + error);

    setState (State::idle);
    setSize (700, 800);
    startTimer (20);
}

MainComponent::~MainComponent()
{
    stopTimer();
    deviceManager.removeAudioCallback (&engine);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (10);

    const auto row = [&area] (int height)
    {
        auto r = area.removeFromTop (height);
        area.removeFromTop (8);
        return r;
    };

    openButton     .setBounds (row (20));
    playButton     .setBounds (row (20));
    stopButton     .setBounds (row (20));
    coldStartToggle.setBounds (row (20));
    statusLabel    .setBounds (row (20));
    selector       .setBounds (row (230));
    results        .setBounds (area);
}

void MainComponent::timerCallback()
{
    if (state != State::recording)
        return;

    if (engine.isFinished())
    {
        startAnalysis();
    }
    else if (! engine.isBusy())
    {
        showMessage ("Take aborted: " + engine.getLastError());
        setState (State::idle);
    }
    else
    {
        statusLabel.setText ("Recording... " + juce::String (engine.getProgress() * 100.0, 0) + "%",
                             juce::dontSendNotification);
    }
}

void MainComponent::setState (State newState)
{
    state = newState;

    const auto idle = state == State::idle;
    openButton.setEnabled (idle);
    playButton.setEnabled (idle && stimulus != nullptr);
    stopButton.setEnabled (state == State::recording);
    coldStartToggle.setEnabled (idle);
    selector.setEnabled (idle);
}

void MainComponent::showMessage (const juce::String& message)
{
    statusLabel.setText (message, juce::dontSendNotification);
    results.setText (message, false);
}

void MainComponent::openButtonClicked()
{
    chooser = std::make_unique<juce::FileChooser> ("Select a WAV stimulus to play...",
                                                   juce::File{},
                                                   "*.wav");
    auto chooserFlags = juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file == juce::File{})
            return;

        auto clip = std::make_shared<RoundTrip::AudioClip>();

        if (const auto result = RoundTrip::loadClip (file, *clip); result.failed())
        {
            showMessage (result.getErrorMessage());
            return;
        }

        stimulus = clip;
        auto message = file.getFileName() + " loaded";

        auto setup = deviceManager.getAudioDeviceSetup();

        if (! juce::approximatelyEqual (setup.sampleRate, clip->sampleRate))
        {
            setup.sampleRate = clip->sampleRate;

            if (const auto error = deviceManager.setAudioDeviceSetup (setup, true); error.isNotEmpty())
                message << "; couldn't switch the device to " << clip->sampleRate << " Hz: " << error;
            else
                message << "; device switched to " << clip->sampleRate << " Hz";
        }

        showMessage (message);
        setState (State::idle);
    });
}

void MainComponent::playButtonClicked()
{
    if (stimulus == nullptr)
        return;

    const auto cold = coldStartToggle.getToggleState();
    const auto tail = (int) (stimulus->sampleRate * RoundTrip::defaultTailMs / 1000.0);

    if (cold)
        deviceManager.closeAudioDevice();

    const auto result = engine.arm (*stimulus, tail, cold);

    if (cold)
        deviceManager.restartLastAudioDevice();

    if (result.failed())
    {
        showMessage (result.getErrorMessage());
        return;
    }

    results.clear();
    setState (State::recording);
}

void MainComponent::stopButtonClicked()
{
    engine.cancel();
    showMessage ("Stopped");
    setState (State::idle);
}

void MainComponent::startAnalysis()
{
    setState (State::analysing);
    statusLabel.setText ("Analysing...", juce::dontSendNotification);

    auto take = std::make_shared<RoundTrip::Take> (engine.takeResult());
    const auto maxLag = (juce::int64) (take->capture.getNumSamples() - stimulus->getNumSamples());

    analysisPool.addJob ([safeThis = SafePointer<MainComponent> (this), clip = stimulus, take, maxLag]
    {
        auto outcome = std::make_shared<RoundTrip::Outcome> (RoundTrip::processTake (*clip, *take, maxLag));

        juce::MessageManager::callAsync ([safeThis, outcome]
        {
            auto* self = safeThis.getComponent();

            if (self == nullptr)
                return;

            auto text = outcome->summary;

            if (outcome->writeResult.failed())
                text << "Error:     " << outcome->writeResult.getErrorMessage();
            else
                text << "Wrote:     " << outcome->recordingFile.getFullPathName() << juce::newLine
                     << "           " << outcome->reportFile.getFullPathName();

            self->results.setText (text, false);
            self->statusLabel.setText (outcome->analysis.getVerdict(), juce::dontSendNotification);
            self->setState (State::idle);
        });
    });
}

} // namespace AudioApp
