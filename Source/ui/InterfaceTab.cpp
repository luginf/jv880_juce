/*
  ==============================================================================

    InterfaceTab.cpp

  ==============================================================================
*/

#include "InterfaceTab.h"

//==============================================================================
InterfaceTab::PanelButton::PanelButton(VirtualJVProcessor &p, uint8_t buttonIdIn,
                                        const juce::String &label)
    : juce::TextButton(label), processor(p), buttonId(buttonIdIn)
{
}

void InterfaceTab::PanelButton::mouseDown(const juce::MouseEvent &e)
{
    juce::TextButton::mouseDown(e);
    if (processor.loaded && processor.mcu)
        processor.mcu->lcd.LCD_SendButton(buttonId, 1);
}

void InterfaceTab::PanelButton::mouseUp(const juce::MouseEvent &e)
{
    juce::TextButton::mouseUp(e);
    if (processor.loaded && processor.mcu)
        processor.mcu->lcd.LCD_SendButton(buttonId, 0);
}

//==============================================================================
InterfaceTab::DialButton::DialButton(VirtualJVProcessor &p, int directionIn,
                                      const juce::String &label)
    : juce::TextButton(label)
{
    onClick = [&p, directionIn]()
    {
        if (p.loaded && p.mcu)
            p.mcu->MCU_EncoderTrigger(directionIn);
    };
}

//==============================================================================
InterfaceTab::InterfaceTab(VirtualJVProcessor &p)
    : processor(p),
      dialDown(p, 0, "Data -"),
      dialUp(p, 1, "Data +"),
      patchPerform(p, MCU_BUTTON_PATCH_PERFORM, "Patch/Perform"),
      edit(p, MCU_BUTTON_EDIT, "Edit"),
      system(p, MCU_BUTTON_SYSTEM, "System"),
      rhythm(p, MCU_BUTTON_RHYTHM, "Rhythm"),
      utility(p, MCU_BUTTON_UTILITY, "Utility"),
      cursorLeft(p, MCU_BUTTON_CURSOR_L, "Cursor <"),
      cursorRight(p, MCU_BUTTON_CURSOR_R, "Cursor >"),
      toneSelect(p, MCU_BUTTON_TONE_SELECT, "Tone Select"),
      mute(p, MCU_BUTTON_MUTE, "Mute"),
      monitor(p, MCU_BUTTON_MONITOR, "Monitor"),
      compare(p, MCU_BUTTON_COMPARE, "Info/Compare"),
      enter(p, MCU_BUTTON_ENTER, "Enter"),
      preview(p, MCU_BUTTON_PREVIEW, "Preview (Volume Push)")
{
    headerLabel.setText(
        "Real JV-880 panel buttons - these drive the firmware itself (same input the physical "
        "buttons use), unlike every other tab here which edits patch data directly.",
        juce::dontSendNotification);
    headerLabel.setJustificationType(juce::Justification::topLeft);
    headerLabel.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(headerLabel);

    dialLabel.setText("Data Entry Dial", juce::dontSendNotification);
    dialLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(dialLabel);
    addAndMakeVisible(dialDown);
    addAndMakeVisible(dialUp);

    dataHoldToggle.onClick = [this]() { dataHoldToggled(); };
    addAndMakeVisible(dataHoldToggle);

    for (auto *b : { &patchPerform, &edit, &system, &rhythm, &utility,
                      &cursorLeft, &cursorRight, &toneSelect,
                      &mute, &monitor, &compare, &enter, &preview })
        addAndMakeVisible(b);
}

void InterfaceTab::dataHoldToggled()
{
    if (processor.loaded && processor.mcu)
        processor.mcu->lcd.LCD_SendButton(MCU_BUTTON_DATA, dataHoldToggle.getToggleState() ? 1 : 0);
}

void InterfaceTab::resized()
{
    auto area = getLocalBounds().reduced(20);

    headerLabel.setBounds(area.removeFromTop(48));
    area.removeFromTop(16);

    const int buttonH = 32;
    const int gap = 10;

    // Data entry dial cluster - own row, mirrors its standalone position on the real panel.
    auto dialRow = area.removeFromTop(buttonH);
    dialLabel.setBounds(dialRow.removeFromLeft(140));
    dialDown.setBounds(dialRow.removeFromLeft(60));
    dialRow.removeFromLeft(gap);
    dialUp.setBounds(dialRow.removeFromLeft(60));
    dialRow.removeFromLeft(gap * 2);
    dataHoldToggle.setBounds(dialRow.removeFromLeft(320));

    area.removeFromTop(24);

    const int buttonW = 140;

    auto layoutRow = [&](std::initializer_list<juce::TextButton *> rowButtons)
    {
        auto row = area.removeFromTop(buttonH);
        for (auto *b : rowButtons)
        {
            b->setBounds(row.removeFromLeft(buttonW));
            row.removeFromLeft(gap);
        }
        area.removeFromTop(gap);
    };

    // Top row on the real panel: Patch/Perform, Edit, System, Rhythm, Utility (right of the
    // DATA dial).
    layoutRow({ &patchPerform, &edit, &system, &rhythm, &utility });

    // Bottom row: Cursor </>, Tone Select, then the 4 dual-labelled Tone Switch buttons (Mute/
    // Monitor/Info+Compare/Enter).
    layoutRow({ &cursorLeft, &cursorRight, &toneSelect });
    layoutRow({ &mute, &monitor, &compare, &enter });

    // Preview - physically the VOLUME knob's push function, far away from this cluster on the
    // real panel; kept here on its own row until the skinned version exists.
    layoutRow({ &preview });
}
