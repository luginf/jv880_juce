/*
  ==============================================================================

    PerformanceTab.h

    Performance mode UI v2 (Alan's request, 2026-09-07/08): 8 Parts (7 tone + 1 fixed Rhythm),
    matching the real JV-880's own Performance Play mode 1-for-1 - each right-click in
    PatchBrowser ("Send to Performance Part N") assigns a Part's real Roland Patch Memory bank/
    number, independently adjustable (MIDI channel/level/pan/on-off), plus a Performance Name
    field, Save As.../a bank list to persist named combinations. See VirtualJVProcessor's own
    "Performance mode v2" section (PluginProcessor.h) for where the actual state lives and how it
    reaches the firmware (SysEx DT1 into the single `mcu` engine, no separate engines any more) -
    this component only ever reads/writes that state, it owns none of it itself (mirrors how
    EditCommonTab etc. work against processor.status.patch).

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

    juce::Label nameLabel;
    juce::TextEditor nameEditor;

    juce::Label channelHeader, levelHeader, panHeader;

    struct PartRow
    {
        juce::Label nameLabel;
        juce::ComboBox channelCombo;
        Slider levelSlider{0, 0, 127, 1, 100};
        Slider panSlider{0, 0, 127, 1, 64, true};
        Button enabledToggle{0, "On"};
        juce::TextButton clearButton{"Clear"};
    };
    std::array<PartRow, VirtualJVProcessor::kNumPerformanceParts> partRows;

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

    void pushPartParams(int partIndex);
    void updatePartRowFromState(int partIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PerformanceTab)
};
