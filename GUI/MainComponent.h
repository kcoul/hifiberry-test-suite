#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "Devices.h"
#include "Report.h"

namespace AudioApp
{
class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class State
    {
        idle,
        recording,
        analysing
    };

    void timerCallback() override;
    void openButtonClicked();
    void playButtonClicked();
    void stopButtonClicked();
    void setState (State newState);
    void startAnalysis();
    void showMessage (const juce::String& message);

    // Declared before the device manager so it outlives the device that calls into it.
    RoundTrip::Engine engine;
    juce::AudioDeviceManager deviceManager;

    juce::TextButton openButton { "Open..." };
    juce::TextButton playButton { "Play" };
    juce::TextButton stopButton { "Stop" };
    juce::ToggleButton coldStartToggle { "Restart the device on Play (includes start-up in the timing)" };
    juce::Label statusLabel;
    juce::TextEditor results;

    juce::AudioDeviceSelectorComponent selector {
        deviceManager, 2, 2, 2, 2, false, false, true, false};

    std::unique_ptr<juce::FileChooser> chooser;
    std::shared_ptr<const RoundTrip::AudioClip> stimulus;
    State state = State::idle;

    // Last, so a running analysis finishes before anything it posts back to is destroyed.
    juce::ThreadPool analysisPool { juce::ThreadPoolOptions{}.withNumberOfThreads (1) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace AudioApp
