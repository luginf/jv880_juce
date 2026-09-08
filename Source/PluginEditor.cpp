/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "rom.h"
#include <algorithm>

//==============================================================================
VirtualJVEditor::VirtualJVEditor(VirtualJVProcessor &p)
    : AudioProcessorEditor(&p), processor(p),
      lcd(p),
      panelDisplay(p,
                   p.displayMode == VirtualJVProcessor::DisplayMode::PanelFull
                       ? PanelSkin::Variant::kFull : PanelSkin::Variant::kCompact,
                   &lcd),
      tabs(), patchBrowser(p), performanceTab(p), editCommonTab(p),
      editTone1Tab(p, this, 0U), editTone2Tab(p, this, 1U), editTone3Tab(p, this, 2U), editTone4Tab(p, this, 3U), editRhythmTab(p, this),
      settingsTab(p), interfaceTab(p), virtualKeyboard(p)
{
    addAndMakeVisible(lcd);
    addAndMakeVisible(panelDisplay);
    lcd.setVisible(processor.displayMode == VirtualJVProcessor::DisplayMode::LcdOnly);
    panelDisplay.setVisible(!lcd.isVisible());
    addAndMakeVisible(tabs);
    addAndMakeVisible(keyboardHandle);
    addAndMakeVisible(virtualKeyboard);

    keyboardHandle.setExpanded(!keyboardCollapsed);
    keyboardHandle.onClick = [this]
    {
        keyboardCollapsed = !keyboardCollapsed;
        keyboardHandle.setExpanded(!keyboardCollapsed);
        virtualKeyboard.setVisible(!keyboardCollapsed);
        resized();
    };

    tabs.tabChangedFunction =
        [this](int index)
        {
            processor.status.selectedTab = index;
        };

    // Pin each fixed-layout tab at its natural size inside its own Viewport (see the members'
    // own comment in PluginEditor.h) - `false` for deleteComponentWhenNoLongerNeeded, since
    // these components are owned as plain members here, not by the viewport.
    auto pinInViewport = [](juce::Viewport &vp, juce::Component &content)
    {
        content.setSize(kTabAreaW, kTabAreaH);
        vp.setViewedComponent(&content, false);
        vp.setScrollBarsShown(true, false);
    };
    pinInViewport(editCommonViewport, editCommonTab);
    pinInViewport(editTone1Viewport, editTone1Tab);
    pinInViewport(editTone2Viewport, editTone2Tab);
    pinInViewport(editTone3Viewport, editTone3Tab);
    pinInViewport(editTone4Viewport, editTone4Tab);
    pinInViewport(editRhythmViewport, editRhythmTab);
    pinInViewport(settingsViewport, settingsTab);
    pinInViewport(interfaceViewport, interfaceTab);

    // Height is free to shrink well below the default: the tabs area scrolls (PatchBrowser's
    // ListBoxes natively, the other tabs via the Viewports above) rather than clipping. Width
    // can also grow past 820 (Alan's request, 2026-09-07) - the tabs area stays pinned at its
    // native 820px regardless (pinned per-Viewport, see pinInViewport above), but the keyboard
    // strip is fully responsive (VirtualKeyboard::rebuildKeys() lays out from getLocalBounds())
    // so it stretches to fill the extra width instead of leaving it blank. Minimum width still
    // can't go below 820 without clipping the tabs.
    //
    // The top strip's own width behaviour depends on processor.displayMode (Alan's request,
    // 2026-09-08 - "retirer la limite de resize en largeur"): `lcd` (LcdOnly) still draws its
    // emulated dot-matrix bitmap at a fixed 820x100, unscaled, same as always: but `panelDisplay`
    // (PanelCompact/PanelFull) scales to the window's own current width (see resized()), and
    // PanelFull's own native artwork is 3280px wide - so the old maxWidth=2400 cap (chosen back
    // when nothing in this window could usefully be wider than the 820px tabs anyway) is raised
    // well past that instead of removed outright (setResizeLimits has no "no limit" - some finite
    // bound is required), leaving plenty of headroom beyond even a 1:1 full-panel display.
    setResizable(true, true);
    setResizeLimits(820, 400, 6000, 900 + (int)VirtualKeyboard::kRefH);

    setSize(820, 900 + (int)VirtualKeyboard::kRefH);

    // ROMs not found (Alan's report, 2026-09-08: this used to be a blocking OS alert dialog and
    // an otherwise-empty window - no visible path forward besides guessing the app-data folder
    // and restarting). The main interface now shows regardless, with a reduced tab set (just
    // Settings, which has its own ROM Folder section to point at the right place and retry
    // without restarting) - see showRomSetupOnly()/romsBecameAvailable().
    if (!processor.loaded)
    {
        showRomSetupOnly();
    }
    else
    {
        showToneOrRhythmEditTabs(processor.status.isDrums);
        setSelectedTab(processor.status.selectedTab);
        updateEditTabs();
    }
}

void VirtualJVEditor::showRomSetupOnly()
{
    const auto bgColor = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    tabs.clearTabs();
    tabs.addTab("Settings", bgColor, &settingsViewport, false);
    settingsTab.updateValues();
    // Not 0/1 (see this field's own comment) - forces romsBecameAvailable()'s call into
    // showToneOrRhythmEditTabs() to do a real rebuild instead of being short-circuited as a
    // no-op "already configured for this mode".
    tabsConfiguredForRhythm = -1;
}

void VirtualJVEditor::romsBecameAvailable()
{
    showToneOrRhythmEditTabs(processor.status.isDrums);
    setSelectedTab(processor.status.selectedTab);
    updateEditTabs();
}

VirtualJVEditor::~VirtualJVEditor()
{
    processor.status.selectedTab = tabs.getCurrentTabIndex();
}

void VirtualJVEditor::updateEditTabs()
{
    editCommonTab.updateValues();
    editTone1Tab.updateValues();
    editTone2Tab.updateValues();
    editTone3Tab.updateValues();
    editTone4Tab.updateValues();
    editRhythmTab.updateValues();
    settingsTab.updateValues();
}

void VirtualJVEditor::updatePerformanceTab()
{
    performanceTab.refreshFromProcessor();
}

void VirtualJVEditor::showToneOrRhythmEditTabs(const bool isRhythm)
{
    // Rebuilding the tabs (clearTabs() + re-addTab() below) is only needed when the tone/rhythm
    // set actually changed - it was previously called unconditionally from
    // VirtualJVProcessor::setCurrentProgram() on every single patch pick, which tore down and
    // rebuilt the TabbedComponent's content each time and stole keyboard focus back from
    // whichever patches ListBox the user had just clicked (Alan's report: arrow keys navigated
    // the bank list instead of moving between patches after clicking one).
    const int wantRhythm = isRhythm ? 1 : 0;
    if (tabsConfiguredForRhythm == wantRhythm)
    {
        editCommonTab.rhythmSetMode(isRhythm);
        return;
    }
    tabsConfiguredForRhythm = wantRhythm;

    const auto bgColor = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    auto selTab = processor.status.selectedTab;

    tabs.clearTabs();

    // The Interface (now labelled "Panel") tab only makes sense when the top-of-window display
    // is LCD-only (Alan's request, 2026-09-08): it exists to give button/DATA-dial access when
    // there's no photo-based panel already on screen. Renamed to "Panel" since that's exactly
    // what it now is together with the always-visible LCD above it in that mode - a photo-based
    // panel interface, just without duplicating the LCD/Volume that mode already shows elsewhere.
    // Always the LAST tab in both branches, so hiding it never shifts any other tab's index.
    const bool showPanelTab = processor.displayMode == VirtualJVProcessor::DisplayMode::LcdOnly;

    if (isRhythm)
    {
        tabs.addTab("Browse", bgColor, &patchBrowser, false);
        tabs.addTab("Performance", bgColor, &performanceTab, false);
        tabs.addTab("Common", bgColor, &editCommonViewport, false);
        tabs.addTab("Rhythm Set", bgColor, &editRhythmViewport, false);
        tabs.addTab("Settings", bgColor, &settingsViewport, false);
        if (showPanelTab)
            tabs.addTab("Panel", bgColor, &interfaceViewport, false);
    }
    else
    {
        tabs.addTab("Browse", bgColor, &patchBrowser, false);
        tabs.addTab("Performance", bgColor, &performanceTab, false);
        tabs.addTab("Common", bgColor, &editCommonViewport, false);
        tabs.addTab("Tone 1", bgColor, &editTone1Viewport, false);
        tabs.addTab("Tone 2", bgColor, &editTone2Viewport, false);
        tabs.addTab("Tone 3", bgColor, &editTone3Viewport, false);
        tabs.addTab("Tone 4", bgColor, &editTone4Viewport, false);
        tabs.addTab("Settings", bgColor, &settingsViewport, false);
        if (showPanelTab)
            tabs.addTab("Panel", bgColor, &interfaceViewport, false);
    }

    // just in case... - index 3 is "Rhythm Set" in the isRhythm branch (Browse=0, Performance=1,
    // Common=2, Rhythm Set=3, Settings=4) - Settings moved to the end (Alan's request,
    // 2026-09-07), which happens to put Rhythm Set back at its original pre-Performance-tab
    // index since Settings no longer sits between Performance and Common.
    if (selTab > 3 && processor.status.isDrums)
    {
        selTab = 3;
        tabs.setCurrentTabIndex(selTab);
    }

    processor.status.selectedTab = selTab;

    editCommonTab.rhythmSetMode(isRhythm);
}

uint8_t VirtualJVEditor::getSelectedRomIdx()
{
    auto idx = patchBrowser.categoriesListBox.getSelectedRow();

    if (idx <= 0)
    {
        return 2; // internal ROM 2 of the 880, contains multisample info table
    }
    else
    {
        return std::min(romCountRequired + idx, romCount - 1); // RD expansion ROM and other SR-JV ROMs henceforth
    }
}

void VirtualJVEditor::resized()
{
    const int handleH = 18;
    const int keyboardH = keyboardCollapsed ? 0 : (int)VirtualKeyboard::kRefH;

    // Top strip height depends on processor.displayMode (see PluginEditor.h's own comment on
    // lcd/panelDisplay): LcdOnly is the fixed 820x100 it's always been; the panel modes scale
    // with the window's own current width instead, spanning it fully (unlike the tabs below,
    // which stay pinned at 820 - see the constructor's setResizeLimits() comment).
    int topAreaH;
    if (processor.displayMode == VirtualJVProcessor::DisplayMode::LcdOnly)
    {
        topAreaH = 100;
        lcd.setBounds(0, 0, 820, 100);
    }
    else
    {
        topAreaH = (int)panelDisplay.heightForWidth((float)getWidth()) + (int)PanelSkin::kControlsRowH;
        panelDisplay.setBounds(0, 0, getWidth(), topAreaH);
    }

    const int tabsH = juce::jmax(0, getHeight() - topAreaH - handleH - keyboardH);

    tabs.setBounds(0, topAreaH, 820, tabsH);
    keyboardHandle.setBounds(0, topAreaH + tabsH, getWidth(), handleH);
    virtualKeyboard.setBounds(0, topAreaH + tabsH + handleH, getWidth(), keyboardH);
}

void VirtualJVEditor::refreshDisplayMode()
{
    const bool lcdOnly = processor.displayMode == VirtualJVProcessor::DisplayMode::LcdOnly;
    lcd.setVisible(lcdOnly);
    panelDisplay.setVisible(!lcdOnly);
    if (!lcdOnly)
        panelDisplay.setVariant(processor.displayMode == VirtualJVProcessor::DisplayMode::PanelFull
                                     ? PanelSkin::Variant::kFull : PanelSkin::Variant::kCompact);

    // Forces the tab rebuild below to actually run (see tabsConfiguredForRhythm's own comment) -
    // the Panel tab's presence depends on displayMode too now, not just isRhythm, so a mode
    // change alone (isRhythm unchanged) would otherwise be short-circuited as a no-op.
    tabsConfiguredForRhythm = -1;
    showToneOrRhythmEditTabs(processor.status.isDrums);
    setSelectedTab(processor.status.selectedTab);

    resized();
}

void VirtualJVEditor::parentHierarchyChanged()
{
    juce::AudioProcessorEditor::parentHierarchyChanged();
    // Plugin builds: no such window exists (the host draws its own). Standalone:
    // StandaloneFilterWindow's ctor hardcodes JUCE's own custom-drawn title bar, with no
    // constructor hook to ask for the native one instead, so it's flipped here, the first time
    // this editor is far enough up the hierarchy to reach that window.
    if (processor.wrapperType != juce::AudioProcessor::wrapperType_Standalone) return;
    if (nativeTitleBarRequested) return;
    nativeTitleBarRequested = true;

    // Deferred to the next message-loop turn rather than done inline here: this callback
    // fires as soon as the editor is added as the window's content component (see
    // ResizableWindow::setContent(), which adds the child BEFORE resizing to fit it), which is
    // before StandaloneFilterWindow's own constructor has resized/positioned the window to its
    // final bounds. Switching the title bar inline recreates the native window peer at
    // whatever tiny bounds the DocumentWindow still had at that point - confirmed on a fresh
    // Linux build, the window got wedged at 128x128. Deferring past the full constructor (which
    // finishes synchronously before this async callback can run) avoids that race.
    juce::Component::SafePointer<VirtualJVEditor> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis == nullptr) return;
        if (auto *dw = dynamic_cast<juce::DocumentWindow *>(safeThis->getTopLevelComponent()))
            if (!dw->isUsingNativeTitleBar()) dw->setUsingNativeTitleBar(true);
    });
}
