/*
  ==============================================================================

    LCDisplay.h
    Created: 18 Aug 2024 12:13:42am
    Author:  Giulio Zausa

  ==============================================================================
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../PluginProcessor.h"

//==============================================================================
/*
*/
class LCDisplay  : public juce::Component
{
public:
    enum struct Color: uint8_t
    {
        Green       = 0U,
        Amber       = 1U,
        Red         = 2U,
        Blue        = 3U,
        WhiteBlack  = 4U,
        WhiteBlue   = 5U,
        BlackWhite  = 6U,
        BlackAmber  = 7U,
        BlackRed    = 8U,
        BlackGreen  = 9U,
        BlackBlue   = 10U,
        BlackVFD    = 11U,
    };

    LCDisplay(VirtualJVProcessor&);
    ~LCDisplay() override;

    void paint (juce::Graphics&) override;
    void resized() override {}
    void mouseDown(const juce::MouseEvent & /* event */) override;
    void setLCDColor(const Color color);

    // Extracted from mouseDown()'s own right-click handling (Alan's request, 2026-09-08) so
    // PanelSkin's embedded live LCD (Panel Compact/Full display mode - see PanelSkin.h) can pop
    // the exact same colour picker on a right-click over the photo's own LCD opening, instead of
    // only being reachable through this always-820x100 standalone component.
    void showColorMenu();

private:
    class RedrawTimer : public juce::Timer
    {
    public:
        RedrawTimer(LCDisplay* parent) : parent(parent) {}

        void timerCallback() override { parent->repaint(); }

    private:
      LCDisplay* parent;
    };

    RedrawTimer redrawTimer;
    Color lcdColor;

    VirtualJVProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LCDisplay)
};
