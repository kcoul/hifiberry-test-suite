#include <JuceHeader.h>

#include "AudioToolbox.h"

int main (int argc, char* argv[])
{
    if (argc != 3) {
        printf("Not enough arguments");
    }

    juce::ConsoleApplication app;
    juce::ScopedJuceInitialiser_GUI juceInit; 

    juce::AudioDeviceManager deviceManager;
    deviceManager.initialise(2, 2, nullptr, true); 

    AudioToolbox toolbox;
    deviceManager.addAudioCallback(&toolbox);


}