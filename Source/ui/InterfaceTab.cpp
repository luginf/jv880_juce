/*
  ==============================================================================

    InterfaceTab.cpp

  ==============================================================================
*/

#include "InterfaceTab.h"
#include <cmath>

//==============================================================================
// Reference-space (2012x304, see kRefW/kRefH) bounding boxes, measured off
// jv880_panel_compact.png by connected-component analysis (grayish rounded-rect blobs on the
// near-black background), not eyeballed. Reading order matches the real panel's own
// left-to-right/top-to-bottom layout, same convention as v1's plain-button grid.
const InterfaceTab::PanelButton InterfaceTab::kButtons[InterfaceTab::kNumButtons] = {
    // Top row, y=104, h=26 (left to right, right of the DATA dial).
    {1366.0f, 104.0f, 90.0f, 26.0f, MCU_BUTTON_PATCH_PERFORM, "Patch/Perform"},
    {1522.0f, 104.0f, 90.0f, 26.0f, MCU_BUTTON_EDIT,          "Edit"},
    {1630.0f, 104.0f, 91.0f, 26.0f, MCU_BUTTON_SYSTEM,        "System"},
    {1740.0f, 104.0f, 90.0f, 26.0f, MCU_BUTTON_RHYTHM,        "Rhythm"},
    {1849.0f, 104.0f, 91.0f, 26.0f, MCU_BUTTON_UTILITY,       "Utility"},
    // Bottom row, y=211, h=26: Cursor </>, Tone Select, then the 4 dual-labelled Tone Switch
    // buttons (Mute/Monitor/Info+Compare/Enter).
    {1103.0f, 211.0f, 90.0f, 26.0f, MCU_BUTTON_CURSOR_L,   "Cursor <"},
    {1210.0f, 211.0f, 89.0f, 26.0f, MCU_BUTTON_CURSOR_R,   "Cursor >"},
    {1366.0f, 211.0f, 90.0f, 26.0f, MCU_BUTTON_TONE_SELECT,"Tone Select"},
    {1521.0f, 211.0f, 91.0f, 26.0f, MCU_BUTTON_MUTE,       "Mute"},
    {1630.0f, 211.0f, 91.0f, 26.0f, MCU_BUTTON_MONITOR,    "Monitor"},
    {1740.0f, 211.0f, 90.0f, 26.0f, MCU_BUTTON_COMPARE,    "Info/Compare"},
    {1849.0f, 211.0f, 91.0f, 26.0f, MCU_BUTTON_ENTER,      "Enter"},
};

//==============================================================================
InterfaceTab::InterfaceTab(VirtualJVProcessor &p) : processor(p)
{
    panelImage = juce::ImageCache::getFromMemory(BinaryData::jv880_panel_compact_png,
                                                  BinaryData::jv880_panel_compact_pngSize);

    headerLabel.setText(
        "Real JV-880 panel - these drive the firmware itself (same input the physical buttons "
        "use), unlike every other tab here which edits patch data directly.",
        juce::dontSendNotification);
    headerLabel.setJustificationType(juce::Justification::topLeft);
    headerLabel.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(headerLabel);

    dataHoldToggle.onClick = [this]
    {
        if (processor.loaded && processor.mcu)
            processor.mcu->lcd.LCD_SendButton(MCU_BUTTON_DATA, dataHoldToggle.getToggleState() ? 1 : 0);
    };
    addAndMakeVisible(dataHoldToggle);

    setWantsKeyboardFocus(false);
}

void InterfaceTab::resized()
{
    auto area = getLocalBounds().reduced(20);

    headerLabel.setBounds(area.removeFromTop(48));
    area.removeFromTop(12);

    // Fit the photo to the available width (this tab's own natural size, kTabAreaW=820 minus the
    // margin above) - its own very wide/short aspect (2012x304, ~6.6:1) means width is always the
    // binding constraint here, never height.
    const float w = (float)area.getWidth();
    imageScale = w / kRefW;
    const float h = kRefH * imageScale;
    imageDrawArea = juce::Rectangle<float>((float)area.getX(), (float)area.getY(), w, h);

    area.removeFromTop((int)h + 20);
    dataHoldToggle.setBounds(area.removeFromTop(24));
}

juce::Point<float> InterfaceTab::refToComponent(juce::Point<float> ref) const
{
    return imageDrawArea.getPosition() + ref * imageScale;
}

juce::Point<float> InterfaceTab::componentToRef(juce::Point<float> comp) const
{
    return (comp - imageDrawArea.getPosition()) / imageScale;
}

juce::Rectangle<float> InterfaceTab::refRectToComponent(float x, float y, float w, float h) const
{
    return juce::Rectangle<float>(x, y, w, h)
        .transformedBy(juce::AffineTransform::scale(imageScale)
                           .translated(imageDrawArea.getX(), imageDrawArea.getY()));
}

int InterfaceTab::hitTest(juce::Point<float> componentPos) const
{
    auto ref = componentToRef(componentPos);

    for (int i = 0; i < kNumButtons; i++)
    {
        auto &b = kButtons[i];
        juce::Rectangle<float> padded(b.x - kHitPadRefX, b.y - kHitPadRefY,
                                      b.w + 2.0f * kHitPadRefX, b.h + 2.0f * kHitPadRefY);
        if (padded.contains(ref))
            return i;
    }

    if (ref.getDistanceFrom({kDialCx, kDialCy}) <= kDialR + kHitPadRefR)
        return kHitDataDial;
    if (ref.getDistanceFrom({kVolCx, kVolCy}) <= kVolR + kHitPadRefR)
        return kHitVolumeKnob;

    return kHitNone;
}

void InterfaceTab::pressButton(int hit, bool down)
{
    if (hit >= 0 && hit < kNumButtons)
    {
        if (processor.loaded && processor.mcu)
            processor.mcu->lcd.LCD_SendButton(kButtons[hit].buttonId, down ? 1 : 0);
    }
    else if (hit == kHitVolumeKnob)
    {
        if (processor.loaded && processor.mcu)
            processor.mcu->lcd.LCD_SendButton(MCU_BUTTON_PREVIEW, down ? 1 : 0);
    }
}

void InterfaceTab::fireEncoderStep(int direction)
{
    if (processor.loaded && processor.mcu)
        processor.mcu->MCU_EncoderTrigger(direction);
    dialAngleDeg += (direction != 0) ? kDialDegPerStep : -kDialDegPerStep;
    repaint();
}

void InterfaceTab::mouseDown(const juce::MouseEvent &e)
{
    int hit = hitTest(e.position);
    pressedButtonIndex = hit;

    if (hit == kHitDataDial)
    {
        dialDragStartY = e.position.y;
        dialStepsFired = 0;
        if (dataHoldToggle.getToggleState() && processor.loaded && processor.mcu)
            processor.mcu->lcd.LCD_SendButton(MCU_BUTTON_DATA, 1);
    }
    else
    {
        pressButton(hit, true);
    }
    repaint();
}

void InterfaceTab::mouseDrag(const juce::MouseEvent &e)
{
    if (pressedButtonIndex != kHitDataDial)
        return;

    // Vertical drag, same convention as a plugin's own rotary knobs: up = increment (Data +),
    // down = decrement (Data -). Tracks total travel since mouseDown rather than per-event deltas
    // so a slow drag still eventually registers (each event only converts whole kDialPxPerStep
    // chunks of the total into pulses, same idea as D110Panel's own volume-drag handling).
    const float totalUp = dialDragStartY - e.position.y; // positive = dragged upward
    const int stepsWanted = (int)std::floor(std::abs(totalUp) / kDialPxPerStep) *
                            (totalUp >= 0.0f ? 1 : -1);
    while (dialStepsFired < stepsWanted)
    {
        fireEncoderStep(1);
        dialStepsFired++;
    }
    while (dialStepsFired > stepsWanted)
    {
        fireEncoderStep(0);
        dialStepsFired--;
    }
}

void InterfaceTab::mouseUp(const juce::MouseEvent &)
{
    if (pressedButtonIndex == kHitDataDial)
    {
        if (dataHoldToggle.getToggleState() && processor.loaded && processor.mcu)
            processor.mcu->lcd.LCD_SendButton(MCU_BUTTON_DATA, 0);
    }
    else
    {
        pressButton(pressedButtonIndex, false);
    }
    pressedButtonIndex = kHitNone;
    repaint();
}

void InterfaceTab::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &wheel)
{
    if (hitTest(e.position) != kHitDataDial)
        return;
    fireEncoderStep(wheel.deltaY > 0.0f ? 1 : 0);
}

void InterfaceTab::paint(juce::Graphics &g)
{
    if (panelImage.isValid())
        g.drawImage(panelImage, imageDrawArea, juce::RectanglePlacement::stretchToFit);

    if (pressedButtonIndex >= 0 && pressedButtonIndex < kNumButtons)
    {
        auto &b = kButtons[pressedButtonIndex];
        auto rect = refRectToComponent(b.x, b.y, b.w, b.h);
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.fillRoundedRectangle(rect, 3.0f);
    }
    else if (pressedButtonIndex == kHitDataDial)
    {
        auto c = refToComponent({kDialCx, kDialCy});
        float r = kDialR * imageScale;
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.drawEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f, 2.5f);
    }
    else if (pressedButtonIndex == kHitVolumeKnob)
    {
        auto c = refToComponent({kVolCx, kVolCy});
        float r = kVolR * imageScale;
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.drawEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f, 2.5f);
    }

    // Purely cosmetic rotation indicator on the DATA dial - see this field's own comment in the
    // header for why (the mockup's knob, unlike D110's real photographed VOLUME knob, has no
    // printed pointer to preserve alignment of, so this is a synthetic one that just advances a
    // fixed step per encoder pulse rather than tracking any real firmware value).
    {
        auto c = refToComponent({kDialCx, kDialCy});
        float r = kDialR * imageScale;
        auto angle = juce::degreesToRadians(dialAngleDeg - 90.0f);
        juce::Point<float> tip(c.x + r * 0.8f * std::cos(angle), c.y + r * 0.8f * std::sin(angle));
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.drawLine(c.x, c.y, tip.x, tip.y, 2.0f);
    }
}
