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

    v3 (2026-09-08, Alan's request): now just a thin host for PanelSkin (see PanelSkin.h)
    configured with the buttons-only "commands" crop (jv880_commands.png - DATA dial + all 12
    discrete buttons, no LCD/Volume) - this tab's whole photo-based skin (v2, same day) moved into
    PanelSkin once the top-of-window Panel Compact/Full display modes needed the identical hit-
    region logic against a different crop of the same artwork. This tab exists specifically for
    Alan's "LCD only" display-mode choice in Settings: the real LCD is already shown at the very
    top of the window in that mode, so this crop deliberately excludes the photo's own LCD/Volume
    area rather than duplicating it.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PanelSkin.h"
#include "../PluginProcessor.h"

//==============================================================================
class InterfaceTab : public juce::Component
{
public:
    explicit InterfaceTab(VirtualJVProcessor &);

    void resized() override;

private:
    PanelSkin skin;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InterfaceTab)
};
