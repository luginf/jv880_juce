/*
  ==============================================================================

    SettingsTab.h
    Created: 18 Aug 2024 1:05:04pm
    Author:  Giulio Zausa

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"

#include "widgets/Button.h"
#include "widgets/Slider.h"

//==============================================================================

class SettingsTab  : public juce::Component,
                     public juce::Slider::Listener,
                     public juce::Button::Listener
{
public:
    SettingsTab(VirtualJVProcessor&);
    ~SettingsTab() override;

    void updateValues();
    void resized() override;
    void sliderValueChanged (juce::Slider*) override;
    void buttonClicked (juce::Button*) override;
    void buttonStateChanged(juce::Button*) override {}

private:
    VirtualJVProcessor& processor;

    enum SettingsWidgets
    {
        MasterTune   = 10U,
        Reverb       = 11U,
        Chorus       = 12U,
        MasterVolume = 13U,
    };

    Slider masterTuneSlider{ MasterTune, 1, 127, 1, 64, true };
    juce::Label masterTuneLabel;
    Button reverbToggle{ Reverb, "Reverb" };
    Button chorusToggle{ Chorus, "Chorus" };
    // Plugin-side output trim, not a real JV-880 hardware/MIDI parameter (the real unit's
    // volume knob is an analog attenuator, nothing SysEx/nvram reaches) - applied as a plain
    // gain in VirtualJVProcessor::processBlock(). Session-only, like the VirtualKeyboard
    // settings: not part of DataToSave (see that struct's own comment on why).
    Slider masterVolumeSlider{ MasterVolume, 0, 100, 1, 100 };
    juce::Label masterVolumeLabel;
    juce::Label buildDateLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsTab)
};
