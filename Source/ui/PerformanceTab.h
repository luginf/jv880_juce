/*
  ==============================================================================

    PerformanceTab.h

    Performance mode UI (Alan's request, 2026-09-07): 4 slots, each fed by a right-click in
    PatchBrowser ("Send to Performance Slot N"), independently adjustable (MIDI channel/level/
    pan/on-off), plus Save As.../a bank list to persist named combinations. See
    VirtualJVProcessor's own "Performance mode" section (PluginProcessor.h) for where the actual
    state and audio engines live - this component only ever reads/writes that state, it owns
    none of it itself (mirrors how EditCommonTab etc. work against processor.status.patch).

  ==============================================================================
*/

#pragma once

#include <array>
#include <memory>
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "widgets/Button.h"
#include "widgets/Slider.h"

//==============================================================================
class PerformanceTab : public juce::Component
{
public:
    explicit PerformanceTab(VirtualJVProcessor &);
    ~PerformanceTab() override;

    void resized() override;

    // Pushed by VirtualJVEditor::updatePerformanceTab() after any processor-side Performance
    // state change that didn't originate from this component (a right-click in PatchBrowser, a
    // saved/loaded performance) - same push-refresh convention as updateEditTabs().
    void refreshFromProcessor();

private:
    VirtualJVProcessor &processor;

    Button enablePerformanceToggle{0, "Enable Performance Mode"};

    juce::Label channelHeader, levelHeader, panHeader;

    struct SlotRow
    {
        juce::Label nameLabel;
        juce::ComboBox channelCombo;
        Slider levelSlider{0, 0, 127, 1, 100};
        Slider panSlider{0, -64, 63, 1, 0, true};
        Button enabledToggle{0, "On"};
        juce::TextButton clearButton{"Clear"};
    };
    std::array<SlotRow, 4> slotRows;

    juce::Label bankHeaderLabel;
    juce::TextButton saveAsButton{"Save As..."};
    juce::TextButton deleteButton{"Delete"};
    std::unique_ptr<juce::FileChooser> saveAsChooser;

    class BankListModel : public juce::ListBoxModel
    {
    public:
        explicit BankListModel(PerformanceTab &ownerIn) : owner(ownerIn) {}

        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int height,
                              bool rowIsSelected) override;
        void selectedRowsChanged(int lastRowSelected) override;

    private:
        PerformanceTab &owner;
    };
    BankListModel bankListModel{*this};
    juce::ListBox bankListBox{"Performances", &bankListModel};

    void pushSlotParams(int slotIndex);
    void updateSlotRowFromState(int slotIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PerformanceTab)
};
