/*
  ==============================================================================

    InterfaceTab.h

    Virtual front panel (Alan's request, 2026-09-07/08): every other tab in this UI (Common, Tone,
    Rhythm Set, Performance...) pokes NVRAM directly and bypasses the JV-880 firmware entirely -
    see CLAUDE.md's "Mode Performance" section for why. This tab is the opposite: it drives the
    *real* firmware input paths the physical panel uses (LCD::LCD_SendButton,
    MCU::MCU_EncoderTrigger) - the tool that was needed to reverse-engineer the native 8-part
    Performance Temp NVRAM area, see CLAUDE.md's "Piste future" section, and generally the only way
    to reach any firmware screen that has no NVRAM-shortcut equivalent.

    v2 (2026-09-08, Alan's request - "fais pareil que pour le D110"): the plain juce::TextButton
    grid from v1 is replaced by a photo-based skin, same technique as D110Panel in
    ~/src/D110/d110-vst-emulator ("the front panel IS the reference photograph... every control is
    an invisible hit-region placed at coordinates measured off that photo"). The photo here
    (Source/Resources/jv880_panel_compact.png, embedded as BinaryData::jv880_panel_compact_png) is
    a synthetic/generated mockup Alan supplied, not a real photograph like D110's, but the
    technique is identical: connected-component analysis on the image itself (see this file's own
    git history for the one-off Python script) found every button/knob's exact bounding box, so
    the coordinates below are measured, not eyeballed. Only ONE image is used (the "compact"
    layout - VOLUME/LCD/DATA dial/button grid, no PHONES/PCM CARD/DATA CARD/MIDI MESSAGE/POWER
    section) - D110 additionally ships a full-size non-compact skin, but that one (jv880.png,
    3280px wide) doesn't fit usefully inside this project's fixed 820px tab area even scaled down,
    so it wasn't brought in; ask Alan before spending time on a full/compact toggle if he wants one.

    Unlike D110Panel, buttons are NOT cut out of the photo and animated sinking into a recess -
    that needs a real photograph with a matching recess/shadow to cut into, which this synthetic
    mockup doesn't cleanly have. Pressed feedback here is a translucent highlight rectangle/ring
    drawn over the (still static) photo instead - simpler, and honest about the fact that this
    isn't a real product photo.

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

    void paint(juce::Graphics &) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void mouseUp(const juce::MouseEvent &) override;
    void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &) override;

private:
    // One button cap as it sits in the reference image (Source/Resources/jv880_panel_compact.png,
    // 2012x304 - kRefW/kRefH below), measured by connected-component analysis rather than eyeballed.
    struct PanelButton
    {
        float x, y, w, h; // reference-space rect (the photo's own pixels)
        uint8_t buttonId; // MCU_BUTTON_* (see mcu.h)
        const char *name; // for the pressed-highlight paint only, not displayed as text
    };

    static constexpr int kNumButtons = 12;
    static const PanelButton kButtons[kNumButtons];

    // Reference space = the embedded PNG's own pixels (jv880_panel_compact.png is 2012x304).
    static constexpr float kRefW = 2012.0f;
    static constexpr float kRefH = 304.0f;

    // DATA entry dial and VOLUME/PREVIEW knob - both plain flat circles in this mockup (no printed
    // pointer to preserve alignment of, unlike D110's VOLUME knob), measured the same way as the
    // buttons above (connected-component bounding box of the circular face).
    static constexpr float kDialCx = 1200.5f, kDialCy = 106.0f, kDialR = 47.0f;
    static constexpr float kVolCx = 104.5f, kVolCy = 186.0f, kVolR = 37.5f;

    // Hit regions are padded well past their drawn/visual size (see kHitPadRefX/Y and
    // kHitPadRefR) - the photo's buttons are comfortably large to look at printed at this scale.
    // (~90x26 reference px -> ~36x10 component px at the scale resized() picks) but not to click,
    // so the clickable area is inflated without changing what's actually painted.
    static constexpr float kHitPadRefX = 4.0f, kHitPadRefY = 17.0f;
    static constexpr float kHitPadRefR = 15.0f;

    // Where/how big the photo is drawn, in this component's own coordinates - set once in
    // resized() (fixed-size component, see PluginEditor's pinInViewport), read by paint() and
    // every hit-test below.
    juce::Rectangle<float> imageDrawArea;
    float imageScale = 1.0f;

    juce::Point<float> refToComponent(juce::Point<float> ref) const;
    juce::Point<float> componentToRef(juce::Point<float> comp) const;
    juce::Rectangle<float> refRectToComponent(float x, float y, float w, float h) const;

    // -1 = nothing, 0..kNumButtons-1 = kButtons index, kHitDataDial/kHitVolumeKnob = the two
    // circular controls.
    static constexpr int kHitNone = -1, kHitDataDial = -2, kHitVolumeKnob = -3;
    int hitTest(juce::Point<float> componentPos) const;

    VirtualJVProcessor &processor;
    juce::Image panelImage;

    juce::Label headerLabel;
    juce::ToggleButton dataHoldToggle{"Hold DATA while rotating (manual p.49)"};

    int pressedButtonIndex = kHitNone; // kHitNone/kHitDataDial/kHitVolumeKnob, or a kButtons index
    float dialDragStartY = 0.0f;       // component-space Y at mouseDown, while dragging the dial
    int dialStepsFired = 0;            // whole encoder pulses already sent for the current drag
    float dialAngleDeg = 0.0f;         // purely cosmetic indicator - see the dial's own comment
    static constexpr float kDialPxPerStep = 6.0f; // component px of drag per MCU_EncoderTrigger pulse
    static constexpr float kDialDegPerStep = 14.0f;

    void pressButton(int hit, bool down);
    void fireEncoderStep(int direction); // 0 = down/left (Data -), 1 = up/right (Data +)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InterfaceTab)
};
