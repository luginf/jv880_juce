/*
  ==============================================================================

    PatchBrowser.cpp
    Created: 18 Aug 2024 1:01:38pm
    Author:  Giulio Zausa

  ==============================================================================
*/

#include "PatchBrowser.h"
#include <JuceHeader.h>

//==============================================================================
PatchBrowser::PatchBrowser(VirtualJVProcessor &p)
    : processor(p), categoriesListModel(), categoriesListBox("Categories", &categoriesListModel)
{
  addAndMakeVisible(revertButton);
  revertButton.onClick = [this] { processor.revertCurrentPatch(); };

  addAndMakeVisible(saveAsButton);
  saveAsButton.onClick = [this]
  {
    if (!processor.loaded)
      return;

    auto dir = VirtualJVProcessor::userPatchesDir();
    dir.createDirectory();

    // Tone patches carry their own name (first 12 bytes of status.patch, same field the
    // Common tab's Patch Name box edits) - offered as the default so re-saving an already
    //-named patch doesn't make the user retype it. Rhythm sets have no such field.
    juce::String defaultName = processor.status.isDrums
        ? "New Rhythm Set"
        : juce::String(reinterpret_cast<const char *>(processor.status.patch), 12).trim();
    if (defaultName.isEmpty())
      defaultName = "New Patch";

    saveAsChooser = std::make_unique<juce::FileChooser>(
        "Save patch as...", dir.getChildFile(defaultName + ".jvp"), "*.jvp");

    saveAsChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser &fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{})
              return;
            // Not every OS/dialog re-appends the filter extension when the whole filename
            // field was retyped rather than edited in place (confirmed on Linux/GTK) - enforce
            // it here so refreshUserPatches()'s "*.jvp" scan always finds what was just saved.
            if (!file.hasFileExtension("jvp"))
              file = file.withFileExtension("jvp");

            processor.saveCurrentPatchAs(file);

            // The save may have landed in the currently-visible User bank (or the user may
            // browse to it later) - refresh unconditionally rather than tracking whether it's
            // the active category right now.
            categoriesListBox.updateContent();
            categoriesListBox.repaint();
            for (int i = 0; i < columns; i++)
            {
              patchesListBoxes[i]->updateContent();
              patchesListBoxes[i]->repaint();
            }
        });
  };

  categoriesListBox.setRowHeight(34);
  addAndMakeVisible(categoriesListBox);

  for (int i = 0; i < columns; i++)
  {
    patchesListModels[i] =
        new PatchesListModel(rowPerColumn * i, rowPerColumn * (i + 1), this,
                             &categoriesListBox, &categoriesListModel);
    patchesListBoxes[i] = new juce::ListBox("Patches", patchesListModels[i]);
    patchesListModels[i]->owner = patchesListBoxes[i];

    patchesListBoxes[i]->setRowHeight(17);
    addAndMakeVisible(*patchesListBoxes[i]);
  }

  if (processor.loaded)
  {
    if (processor.status.selectedRom < 0)
    {
      processor.status.selectedRom = 0;
    }

    categoriesListBox.selectRow(processor.status.selectedRom);

    /* this doesn't seem to work for some reason
    const auto col = processor.status.selectedPatch / rowPerColumn;
    const auto row = processor.status.selectedPatch % rowPerColumn;

    patchesListBoxes[col]->selectRow(row);
    */
  }
}

PatchBrowser::~PatchBrowser()
{
  processor.status.selectedRom = categoriesListBox.getSelectedRow();

  for (int i = 0; i < columns; i++)
  {
    if (patchesListBoxes[i]->getSelectedRow() > -1)
    {
      processor.status.selectedPatch = (i * rowPerColumn) + patchesListBoxes[i]->getSelectedRow();
    }

    delete patchesListModels[i];
    delete patchesListBoxes[i];
  }
}

void PatchBrowser::resized()
{
  const int topBarH = 26;
  saveAsButton.setBounds(getWidth() - 110, 2, 106, topBarH - 4);
  revertButton.setBounds(getWidth() - 270, 2, 156, topBarH - 4);

  const int listsH = getHeight() - topBarH;
  categoriesListBox.setBounds(0, topBarH, 180, listsH);

  for (int i = 0; i < columns; i++)
  {
    patchesListBoxes[i]->setBounds(180 + (getWidth() - 180) / columns * i,
                                   topBarH,
                                   (getWidth() - 180) / columns,
                                   listsH);
  }
}
