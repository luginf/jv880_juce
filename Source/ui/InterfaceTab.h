/*
  ==============================================================================

    InterfaceTab.h

    Virtual front panel (Alan's request, 2026-09-07): every other tab in this UI (Common, Tone,
    Rhythm Set, Performance...) pokes NVRAM directly and bypasses the JV-880 firmware entirely -
    see CLAUDE.md's "Mode Performance" section for why. This tab is the opposite: it drives the
    *real* firmware input paths the physical panel uses, which existed in the emulator core
    (LCD::LCD_SendButton, MCU::MCU_EncoderTrigger) but were never actually wired to anything in
    this JUCE UI until now. That makes the real PATCH/PERFORM button (and every other firmware
    screen with no NVRAM-shortcut equivalent) reachable, and is the tool needed to reverse-engineer
    the native 8-part Performance Temp NVRAM area - see CLAUDE.md's "Piste future" section.

    Button layout below mirrors the physical panel's left-to-right/top-to-bottom reading order
    (per the JV-880 front panel photo and its Owner's Manual) rather than the MCU_BUTTON_* enum
    order - a pixel-accurate skin of the real panel is future work, this just gets the reading
    order right in the meantime.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"

//==============================================================================
class InterfaceTab : public juce::Component
{
public:
    explicit InterfaceTab(VirtualJVProcessor &);

    void resized() override;

private:
    // Momentary panel button: holds the real firmware button matrix bit
    // (MCU::lcd.LCD_SendButton) down for exactly as long as the mouse button is, instead of
    // toggling - that's what the firmware's own scan loop (MCU_ReadP1) expects from a physical
    // button.
    class PanelButton : public juce::TextButton
    {
    public:
        PanelButton(VirtualJVProcessor &p, uint8_t buttonIdIn, const juce::String &label);

        void mouseDown(const juce::MouseEvent &e) override;
        void mouseUp(const juce::MouseEvent &e) override;

    private:
        VirtualJVProcessor &processor;
        uint8_t buttonId;
    };

    // JV-880 has a rotary data entry dial here, not a slider or a pair of buttons - but a click
    // is the closest a mouse can get to "one detent", so each click fires a single
    // MCU_EncoderTrigger pulse in the given direction.
    class DialButton : public juce::TextButton
    {
    public:
        DialButton(VirtualJVProcessor &p, int directionIn, const juce::String &label);
    };

    VirtualJVProcessor &processor;

    juce::Label headerLabel;

    // Data entry dial cluster: the dial itself is also a pushbutton (MCU_BUTTON_DATA) - the
    // Owner's Manual (p.49) documents holding it down while rotating the dial as its own distinct
    // gesture, so it's a sticky toggle here rather than a momentary button (which couldn't stay
    // held across separate dial clicks).
    juce::Label dialLabel;
    juce::ToggleButton dataHoldToggle{"Hold DATA while rotating (manual p.49)"};
    DialButton dialDown, dialUp;

    PanelButton patchPerform, edit, system, rhythm, utility,
                cursorLeft, cursorRight, toneSelect,
                mute, monitor, compare, enter,
                preview;

    void dataHoldToggled();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InterfaceTab)
};
