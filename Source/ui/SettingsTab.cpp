/*
  ==============================================================================

    SettingsTab.cpp
    Created: 18 Aug 2024 1:05:04pm
    Author:  Giulio Zausa

  ==============================================================================
*/

#include "SettingsTab.h"
#include <JuceHeader.h>

// Only declares StandalonePluginHolder/AudioDeviceSelectorComponent wiring - this project builds
// all of AU/LV2/Standalone/VST3 from one shared "Shared Code" library (see Builds/LinuxMakefile),
// so JucePlugin_Build_Standalone is 1 for that shared compilation regardless of which actual
// wrapper links it (each format's own small wrapper .cpp is what's format-specific). The real
// per-instance check is the runtime one below (StandalonePluginHolder::getInstance() only
// returns non-null inside the actual standalone executable, since that's the only wrapper that
// ever constructs one).
#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

//==============================================================================
SettingsTab::SettingsTab(VirtualJVProcessor &p) : processor(p)
{
  addAndMakeVisible(masterTuneSlider);
  masterTuneSlider.addListener(this);
  masterTuneSlider.setTextValueSuffix(" Hz");
  masterTuneSlider.textFromValueFunction =
      [this](double value)
      {
        double floatValue = (value - 1) / 126;
        return juce::String(floatValue * (452.6 - 427.4) + 427.4, 0, false);
      };
  masterTuneSlider.valueFromTextFunction = 
      [this](const juce::String &text)
      {
        double floatValue = text.getDoubleValue();
        return (floatValue - 427.4) / (452.6 - 427.4) * 126 + 1;
      };

  addAndMakeVisible(masterTuneLabel);
  masterTuneLabel.setText("Master Tune", juce::dontSendNotification);
  masterTuneLabel.attachToComponent(&masterTuneSlider, true);

  addAndMakeVisible(reverbToggle);
  reverbToggle.addListener(this);

  addAndMakeVisible(chorusToggle);
  chorusToggle.addListener(this);

  addAndMakeVisible(masterVolumeSlider);
  masterVolumeSlider.addListener(this);
  masterVolumeSlider.setTextValueSuffix(" %");

  addAndMakeVisible(masterVolumeLabel);
  masterVolumeLabel.setText("Master Volume", juce::dontSendNotification);
  masterVolumeLabel.attachToComponent(&masterVolumeSlider, true);

  const auto buildTime = juce::Time::getCompilationDate();
  const juce::String buildInfo = "Build Date: " + buildTime.formatted("%d %b %Y, %H:%M:%S");

  addAndMakeVisible(buildDateLabel);
  buildDateLabel.setText(buildInfo, juce::dontSendNotification);
  buildDateLabel.setJustificationType(juce::Justification::centredRight);

  addAndMakeVisible(audioSettingsHeaderLabel);
  audioSettingsHeaderLabel.setText("Audio/MIDI Settings", juce::dontSendNotification);
  audioSettingsHeaderLabel.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));

#if JucePlugin_Build_Standalone
  if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
  {
    if (auto *holder = juce::StandalonePluginHolder::getInstance())
    {
      auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
          holder->deviceManager,
          0, holder->getNumInputChannels(),
          0, holder->getNumOutputChannels(),
          true,                    // showMidiInputOptions
          processor.producesMidi(), // showMidiOutputSelector
          true,                    // showChannelsAsStereoPairs
          false);                  // hideAdvancedOptionsWithButton - keep sample rate/buffer size visible directly
      addAndMakeVisible(*selector);
      audioDeviceSelector = std::move(selector);
    }
  }
#endif

  if (audioDeviceSelector == nullptr)
  {
    addAndMakeVisible(audioSettingsUnavailableLabel);
    audioSettingsUnavailableLabel.setText(
        "Audio device and buffer size are controlled by your DAW/host when running as a plugin.",
        juce::dontSendNotification);
  }

  addAndMakeVisible(dspLoadLabel);
  dspLoadLabel.setText("DSP Load: -- %", juce::dontSendNotification);

  startTimerHz(4);
}

SettingsTab::~SettingsTab() {}

void SettingsTab::timerCallback()
{
  // dspLoadMeasurer is written from the audio thread but is internally atomic/lock-free to
  // read - see juce::AudioProcessLoadMeasurer's own header.
  const double loadPercent = processor.dspLoadMeasurer.getLoadAsPercentage();
  dspLoadLabel.setText("DSP Load: " + juce::String(loadPercent, 1) + " %",
                       juce::dontSendNotification);
}

void SettingsTab::updateValues()
{
  masterTuneSlider.setValue(((int8_t *)processor.mcu->nvram)[0x00] + 64, juce::dontSendNotification);
  reverbToggle.setToggleState((processor.mcu->nvram[0x02] >> 0) & 1, juce::dontSendNotification);
  chorusToggle.setToggleState((processor.mcu->nvram[0x02] >> 1) & 1, juce::dontSendNotification);
  masterVolumeSlider.setValue(processor.getMasterVolume() * 100.0, juce::dontSendNotification);
}

void SettingsTab::resized()
{
  const auto top = 10;
  const auto sliderLeft1 = 100;
  const auto width = getWidth() / 3 - sliderLeft1 - 10;
  const auto sliderLeft2 = sliderLeft1 + getWidth() / 3 + 2;
  const auto sliderLeft3 = sliderLeft2 + getWidth() / 3;
  const auto height = 24;
  const auto row2Top = top + height + 20;

  auto sliderLeft = 120;

  reverbToggle      .setBounds(sliderLeft1 - 90, top, width, height);
  chorusToggle      .setBounds(sliderLeft2 - 90, top, width, height);
  masterTuneSlider  .setBounds(sliderLeft3, top, width, height);
  masterVolumeSlider.setBounds(sliderLeft3, row2Top, width, height);
  dspLoadLabel      .setBounds(sliderLeft1 - 90, row2Top, width, height);

  // Below the rest (Alan's request, 2026-09-07) - this tab has plenty of unused vertical space
  // between row2Top and buildDateLabel's own fixed position already.
  const auto audioSectionTop = row2Top + height + 30;
  audioSettingsHeaderLabel.setBounds(10, audioSectionTop, 400, 24);

  const auto audioContentTop = audioSectionTop + 30;
  const auto audioContentBottom = 725; // leaves buildDateLabel's own row (735) clear
  if (audioDeviceSelector != nullptr)
    audioDeviceSelector->setBounds(10, audioContentTop, getWidth() - 20,
                                   audioContentBottom - audioContentTop);
  else
    audioSettingsUnavailableLabel.setBounds(10, audioContentTop, getWidth() - 20, 24);

  buildDateLabel    .setBounds(10, 735, 800, 20);
}

void SettingsTab::sliderValueChanged(juce::Slider *slider)
{
  uint32_t id = 0xFFFFFFF;

  if (auto i = dynamic_cast<Slider*>(slider))
  {
    id = i->getID();
  }

  if (id == MasterTune)
  {
    processor.sendSysexParamChange(0x01, (uint8_t)masterTuneSlider.getValue());
  }
  else if (id == MasterVolume)
  {
    processor.setMasterVolume((float)masterVolumeSlider.getValue() / 100.0f);
  }
}

void SettingsTab::buttonClicked(juce::Button *button)
{
  uint32_t id = 0xFFFFFFF;

  if (auto i = dynamic_cast<Button*>(button))
  {
    id = i->getID();
  }

  switch (id)
  {
  case Reverb:
    processor.sendSysexParamChange(0x04, reverbToggle.getToggleState());
    break;
  case Chorus:
    processor.sendSysexParamChange(0x05, chorusToggle.getToggleState());
    break;
  }
}
