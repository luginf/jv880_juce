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
            for (int i = 0; i < maxColumnsPool; i++)
            {
              patchesListBoxes[i]->updateContent();
              patchesListBoxes[i]->repaint();
            }
        });
  };

  categoriesListBox.setRowHeight(34);
  addAndMakeVisible(categoriesListBox);
  categoriesListModel.addChangeListener(this);

  for (int i = 0; i < maxColumnsPool; i++)
  {
    // Real startI/endI are set by the first reflowPatchColumns() call (triggered by the initial
    // resized() pass before anything is shown) - these are just placeholders.
    patchesListModels[i] = new PatchesListModel(0, 0, this, &categoriesListBox, &categoriesListModel);
    patchesListBoxes[i] = new juce::ListBox("Patches", patchesListModels[i]);
    patchesListModels[i]->owner = patchesListBoxes[i];

    patchesListBoxes[i]->setRowHeight(rowHeight);
    addChildComponent(*patchesListBoxes[i]); // visibility (in/active) decided by reflowPatchColumns()
  }

  if (processor.loaded)
  {
    if (processor.status.selectedRom < 0)
    {
      processor.status.selectedRom = 0;
    }

    categoriesListBox.selectRow(processor.status.selectedRom);

    /* this doesn't seem to work for some reason
    const auto col = processor.status.selectedPatch / currentRowsPerColumn;
    const auto row = processor.status.selectedPatch % currentRowsPerColumn;

    patchesListBoxes[col]->selectRow(row);
    */
  }
}

PatchBrowser::~PatchBrowser()
{
  processor.status.selectedRom = categoriesListBox.getSelectedRow();
  categoriesListModel.removeChangeListener(this);

  for (int i = 0; i < maxColumnsPool; i++)
  {
    if (patchesListBoxes[i]->getSelectedRow() > -1)
    {
      processor.status.selectedPatch =
          (i * currentRowsPerColumn) + patchesListBoxes[i]->getSelectedRow();
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

  reflowPatchColumns();
}

// Spreads the current category's patches across as many columns as fit the available height
// without scrolling, rather than a fixed 44 rows/column that only ever scrolled in place when
// the window was short (Alan's request, 2026-09-07). Called on resize and on category change
// (different categories hold different patch counts, so the same height can need a different
// column count either way).
void PatchBrowser::reflowPatchColumns()
{
  const int topBarH = 26;
  const int listsH = getHeight() - topBarH;
  const int availableWidth = getWidth() - 180;
  if (listsH <= 0 || availableWidth <= 0)
    return;

  const int groupI = categoriesListBox.getSelectedRow();
  const int totalItems =
      (processor.loaded && groupI >= 0 && groupI < (int)processor.patchInfoPerGroup.size())
          ? (int)processor.patchInfoPerGroup[groupI].size()
          : 0;

  const int rowsPerColumnByHeight = juce::jmax(1, listsH / rowHeight);
  const int neededColumnsByHeight =
      juce::jmax(1, (totalItems + rowsPerColumnByHeight - 1) / rowsPerColumnByHeight);
  const int maxColumnsByWidth = juce::jmin(maxColumnsPool, juce::jmax(1, availableWidth / minColumnWidth));

  int rowsPerColumn;
  int newActiveColumns;
  if (neededColumnsByHeight <= maxColumnsByWidth)
  {
    // Everything fits without scrolling - use exactly as many columns as that needs, not more.
    newActiveColumns = neededColumnsByHeight;
    rowsPerColumn = rowsPerColumnByHeight;
  }
  else
  {
    // Not enough width to show it all at once even using every column available - grow each
    // column past what's visibly on-screen instead (falling back to the shared/synced scroll)
    // so every item stays reachable, rather than silently capping the list at
    // maxColumnsByWidth * rowsPerColumnByHeight and hiding the rest with no way to reach it.
    newActiveColumns = maxColumnsByWidth;
    rowsPerColumn = juce::jmax(rowsPerColumnByHeight,
                               (totalItems + maxColumnsByWidth - 1) / maxColumnsByWidth);
  }

  // The grid dimensions actually changed - any existing row selection now maps to a different
  // flat patch index than before (same local row, different startI), so drop it rather than
  // leave a highlight pointing at the wrong patch. The currently loaded patch itself (LCD/edit
  // tabs) is unaffected - this only clears the Browse list's own highlight.
  const bool dimensionsChanged =
      newActiveColumns != activeColumns || rowsPerColumn != currentRowsPerColumn;
  if (dimensionsChanged)
    for (int i = 0; i < maxColumnsPool; i++)
      patchesListBoxes[i]->deselectAllRows();

  activeColumns = newActiveColumns;
  currentRowsPerColumn = rowsPerColumn;

  const int columnWidth = availableWidth / activeColumns;

  for (int i = 0; i < maxColumnsPool; i++)
  {
    if (i >= activeColumns)
    {
      patchesListBoxes[i]->setVisible(false);
      continue;
    }

    const int newStartI = rowsPerColumn * i;
    const int newEndI = rowsPerColumn * (i + 1);
    if (patchesListModels[i]->startI != newStartI || patchesListModels[i]->endI != newEndI)
    {
      patchesListModels[i]->startI = newStartI;
      patchesListModels[i]->endI = newEndI;
      patchesListBoxes[i]->updateContent();
    }

    patchesListBoxes[i]->setVisible(true);
    patchesListBoxes[i]->setBounds(180 + columnWidth * i, topBarH, columnWidth, listsH);

    // Only the last active column keeps a (shared, see listWasScrolled()) scrollbar - the
    // trailing `true` keeps mouse-wheel scrolling working on the others despite their scrollbar
    // being hidden (Viewport::useMouseWheelMoveIfNeeded() otherwise gates on its visibility).
    patchesListBoxes[i]->getViewport()->setScrollBarsShown(i == activeColumns - 1, false, true, false);
  }
}
