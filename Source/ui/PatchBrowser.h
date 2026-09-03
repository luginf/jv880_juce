/*
  ==============================================================================

    PatchBrowser.h
    Created: 18 Aug 2024 1:01:38pm
    Author:  Giulio Zausa

  ==============================================================================
*/

#pragma once

#include "../PluginProcessor.h"
#include <JuceHeader.h>
#include "../rom.h"

static const char *groupNames[] = {
    "880 Factory",
    "500 Factory",
    "Exp 01 Pop",
    "Exp 02 Orchestral",
    "Exp 03 Piano",
    "Exp 04 Vintage Synth",
    "Exp 05 World",
    "Exp 06 Dance",
    "Exp 07 Super Sound Set",
    "Exp 08 60s/70s Keyboards",
    "Exp 09 Session",
    "Exp 10 Bass & Drum",
    "Exp 11 Techno",
    "Exp 12 Hip-Hop",
    "Exp 13 Vocal",
    "Exp 14 Asia",
    "Exp 15 Special FX",
    "Exp 16 Orchestral II",
    "Exp 17 Country",
    "Exp 18 Latin",
    "Exp 19 House",
    "Exp Custom",
    "User" // patches saved via Save As... (userGroupIndex in PluginProcessor.h) - not a ROM
           // bank, so the romInfos-loaded checks below all skip this row.
};

const int columns = 6;
const int rowPerColumn = 44;

//==============================================================================
/*
 */
class PatchBrowser : public juce::Component {
public:
  PatchBrowser(VirtualJVProcessor &);
  ~PatchBrowser() override;

  void resized() override;

  VirtualJVProcessor &processor;

  // Discards edits made to the current patch since it was picked - Alan's request. A plain
  // juce::TextButton, not the custom widgets/Button.h (that one's a ToggleButton for on/off
  // switches, not a one-shot action).
  juce::TextButton revertButton { "Revert to Original" };
  // Saves the current patch/rhythm-set to the "User" bank under a new name - Alan's request.
  juce::TextButton saveAsButton { "Save As..." };
  std::unique_ptr<juce::FileChooser> saveAsChooser; // kept alive until launchAsync()'s callback fires

  class CategoriesListModel : public juce::ListBoxModel,
                              public juce::ChangeBroadcaster {
    int getNumRows() override { return userGroupIndex + 1; }

    void paintListBoxItem(int rowNumber, juce::Graphics &g, int width,
                          int height, bool rowIsSelected) override {
      g.fillAll(rowIsSelected ? juce::Colour(0xff42A2C8)
                              : juce::Colour(0xff263238));

      g.setColour(rowIsSelected ? juce::Colours::black : juce::Colours::white);

      if (rowNumber < userGroupIndex && rowNumber > 0)
      {
        g.setOpacity(romInfos[romCountRequired + rowNumber].loaded ? 1.f : 0.25f);
      }
      // else: row 0 (880 Factory, always loaded) and userGroupIndex (User, not ROM-backed at
      // all) both stay at full opacity.

      if (rowNumber <= userGroupIndex)
      {
        g.drawFittedText(groupNames[rowNumber], {5, 0, width, height - 2},
                         juce::Justification::left, 1);
      }

      g.setColour(juce::Colours::white.withAlpha(0.4f));
      g.drawRect(0, height - 1, width, 2);
      g.drawRect(width - 2, 0, width, height);
    }

    void selectedRowsChanged(int /* lastRowSelected */) override {
      sendChangeMessage();
    }
  };

  CategoriesListModel categoriesListModel;
  juce::ListBox categoriesListBox;

  class PatchesListModel : public juce::ListBoxModel,
                           public juce::ChangeListener {
  public:
    PatchesListModel(int startI, int endI, PatchBrowser *parent,
                     juce::ListBox *categoriesListBox,
                     CategoriesListModel *categoriesListModel)
        : startI(startI), endI(endI), categoriesListBox(categoriesListBox),
          categoriesListModel(categoriesListModel), parent(parent) {
      categoriesListModel->addChangeListener(this);
    }

    ~PatchesListModel() override {
      categoriesListModel->removeChangeListener(this);
    }

    int getNumRows() override {
      // The User bank isn't backed by any ROM (userGroupIndex, see PluginProcessor.h) - it's
      // however many patches are actually on disk, with no romInfos[]-loaded gate to check
      // (indexing romInfos[groupI + romCountRequired] for that group would in fact run past
      // the end of the array, since it's one past the last real ROM-backed group).
      if (groupI == userGroupIndex)
        return std::min(endI - startI,
                        (int)parent->processor.patchInfoPerGroup[groupI].size() - startI);

      if (!parent->processor.loaded ||
          (groupI > 0 && !romInfos[std::max(groupI, 0) + romCountRequired].loaded) ||
          (groupI == 1 && !romInfos[std::max(groupI, 0) + romCountRequired - 1].loaded))
      {
        return 0;
      }

      return std::min(
          endI - startI,
          (int)parent->processor.patchInfoPerGroup[groupI].size() -
              startI);
    }

    void paintListBoxItem(int rowNumber, juce::Graphics &g, int width,
                          int height, bool rowIsSelected) override {
      g.fillAll(rowIsSelected ? juce::Colour(0xff42A2C8)
                              : juce::Colour(0xff263238));

      if (!parent->processor.loaded) {
        return;
      }

      auto *info = parent->processor.patchInfoPerGroup[groupI][rowNumber + startI];

      // Edited-but-not-(re)saved patches show in amber instead of white - Alan's request, so
      // they're easy to spot while browsing. Not shown once selected: the row's already black
      // on the selection highlight, and that's a stronger enough cue on its own.
      const bool modified = !rowIsSelected && parent->processor.isPatchModified(info->iInList);
      g.setColour(rowIsSelected ? juce::Colours::black
                  : modified    ? juce::Colour(0xffffb238)
                                : juce::Colours::white);

      juce::String str = juce::String(info->name, info->nameLength);
      g.drawFittedText(str, {5, 0, width, height - 2},
                       juce::Justification::left, 1);

      g.setColour(juce::Colours::white.withAlpha(0.4f));
      g.drawRect(0, height - 1, width, 1);
    }

    void changeListenerCallback(juce::ChangeBroadcaster *source) override {
      if (source == categoriesListModel) {
        groupI = categoriesListBox->getSelectedRow();
        parent->processor.status.selectedRom = groupI;
        owner->updateContent();
        owner->deselectAllRows();
        owner->repaint();
      }
    }

    void selectedRowsChanged(int lastRowSelected) override {
      if (!parent->processor.loaded) {
        return;
      }

      for (size_t i = 0; i < columns; i++) {
        if (lastRowSelected != -1 && parent->patchesListBoxes[i] != owner)
          parent->patchesListBoxes[i]->deselectAllRows();
      }

      int selected = owner->getSelectedRow() + startI;
      if (selected >= 0 &&
          selected < parent->processor.patchInfoPerGroup[groupI].size())
        parent->processor.setCurrentProgram(
            parent->processor.patchInfoPerGroup[groupI][selected]
                ->iInList);
    }

    int groupI = 0;
    int startI;
    int endI;
    juce::ListBox *owner = nullptr;
    juce::ListBox *categoriesListBox;
    CategoriesListModel *categoriesListModel;
    PatchBrowser *parent;
  };

  PatchesListModel *patchesListModels[columns];
  juce::ListBox *patchesListBoxes[columns];

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatchBrowser)
};
