/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

#include "ui/widgets/CollapseHandle.h"
#include "ui/widgets/LCDisplay.h"
#include "ui/widgets/TabBar.h"
#include "ui/widgets/VirtualKeyboard.h"
#include "ui/PatchBrowser.h"
#include "ui/PerformanceTab.h"
#include "ui/InterfaceTab.h"
#include "ui/EditCommonTab.h"
#include "ui/EditToneTab.h"
#include "ui/EditRhythmTab.h"
#include "ui/SettingsTab.h"

//==============================================================================
/**
*/
class VirtualJVEditor  : public juce::AudioProcessorEditor
{
public:
    VirtualJVEditor (VirtualJVProcessor&);
    ~VirtualJVEditor() override;

    //==============================================================================
    void resized() override;
    void parentHierarchyChanged() override;

    uint8_t getSelectedRomIdx();
    void updateEditTabs();
    void updatePerformanceTab();
    void showToneOrRhythmEditTabs(const bool isRhythm);

    void setSelectedTab(const int index) { tabs.setCurrentTabIndex(index); }
    void setSelectedROM(const int index) { patchBrowser.categoriesListBox.selectRow(index); }
    void setLCDColor(const LCDisplay::Color color) { lcd.setLCDColor(color); }

private:
    // The Common/Tone/Rhythm/Settings tabs lay themselves out with fixed pixel row positions
    // (see e.g. EditCommonTab::resized()), not proportionally to the space they're given - so
    // rather than teach every one of those layouts to reflow, each is held at this fixed
    // "natural" size (matching what the tabs area has always been) inside its own Viewport, and
    // window resizing just clips/scrolls around it instead. Browse (PatchBrowser) needs none of
    // this: its juce::ListBoxes already scroll internally at whatever size they're given.
    static constexpr int kTabAreaW = 820;
    static constexpr int kTabAreaH = 800;

    VirtualJVProcessor& processor;

    LCDisplay lcd;
    TabBar tabs;
    PatchBrowser patchBrowser;
    PerformanceTab performanceTab;
    EditCommonTab editCommonTab;
    EditToneTab editTone1Tab;
    EditToneTab editTone2Tab;
    EditToneTab editTone3Tab;
    EditToneTab editTone4Tab;
    EditRhythmTab editRhythmTab;
    SettingsTab settingsTab;
    InterfaceTab interfaceTab;
    CollapseHandle keyboardHandle;
    VirtualKeyboard virtualKeyboard;
    bool keyboardCollapsed = false;

    juce::Viewport editCommonViewport, editTone1Viewport, editTone2Viewport, editTone3Viewport,
                   editTone4Viewport, editRhythmViewport, settingsViewport, interfaceViewport;

    // showToneOrRhythmEditTabs() tears down and rebuilds the whole TabbedComponent (clearTabs()
    // + re-addTab() for every tab) - fine when the tone/rhythm mode actually changes, but
    // setCurrentProgram() used to call it on EVERY patch pick regardless. That stole keyboard
    // focus back from whichever patches ListBox the user had just clicked, so arrow keys ended
    // up navigating the bank list instead of the patch that was just loaded (Alan's report).
    // -1 (neither 0 nor 1) so the very first call, from the constructor, always goes through.
    int tabsConfiguredForRhythm = -1;

    bool nativeTitleBarRequested = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualJVEditor)
};
