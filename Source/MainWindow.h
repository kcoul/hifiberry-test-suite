#pragma once

#include "MainComponent.h"

namespace AudioApp
{
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name);

private:
    void closeButtonPressed() override;
    juce::Colour getBackgroundColour();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};
}

