/*
  ==============================================================================

    PerformanceTab.cpp

  ==============================================================================
*/

#include "PerformanceTab.h"

//==============================================================================
PerformanceTab::PerformanceTab(VirtualJVProcessor &p) : processor(p)
{
  addAndMakeVisible(enablePerformanceToggle);
  enablePerformanceToggle.onClick = [this]
  {
    processor.setPerformanceModeEnabled(enablePerformanceToggle.getToggleState());
  };

  nameLabel.setText("Performance Name", juce::dontSendNotification);
  addAndMakeVisible(nameLabel);

  nameEditor.setInputRestrictions(12);
  addAndMakeVisible(nameEditor);
  nameEditor.onFocusLost = [this] { processor.setPerformanceName(nameEditor.getText()); };
  nameEditor.onReturnKey = [this] { processor.setPerformanceName(nameEditor.getText()); };

  channelHeader.setText("Channel", juce::dontSendNotification);
  levelHeader.setText("Level", juce::dontSendNotification);
  panHeader.setText("Pan", juce::dontSendNotification);
  for (auto *header : {&channelHeader, &levelHeader, &panHeader})
  {
    header->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*header);
  }

  for (int i = 0; i < VirtualJVProcessor::kNumPerformanceParts; i++)
  {
    auto &row = partRows[(size_t)i];
    const bool isRhythmPart = (i == VirtualJVProcessor::kNumPerformanceParts - 1);

    addAndMakeVisible(row.nameLabel);
    row.nameLabel.setText("Part " + juce::String(i + 1) + (isRhythmPart ? " (Rhythm): Empty" : ": Empty"),
                          juce::dontSendNotification);

    addAndMakeVisible(row.channelCombo);
    for (int ch = 1; ch <= 16; ch++)
      row.channelCombo.addItem(juce::String(ch), ch);
    row.channelCombo.setSelectedId(1, juce::dontSendNotification);
    row.channelCombo.onChange = [this, i] { pushPartParams(i); };

    addAndMakeVisible(row.levelSlider);
    row.levelSlider.onValueChange = [this, i] { pushPartParams(i); };

    addAndMakeVisible(row.panSlider);
    row.panSlider.onValueChange = [this, i] { pushPartParams(i); };

    addAndMakeVisible(row.enabledToggle);
    row.enabledToggle.setToggleState(true, juce::dontSendNotification);
    row.enabledToggle.onClick = [this, i] { pushPartParams(i); };

    addAndMakeVisible(row.clearButton);
    row.clearButton.onClick = [this, i] { processor.clearPerformancePart(i); };
  }

  bankHeaderLabel.setText("Performance Bank", juce::dontSendNotification);
  addAndMakeVisible(bankHeaderLabel);

  addAndMakeVisible(saveAsButton);
  saveAsButton.onClick = [this]
  {
    auto dir = VirtualJVProcessor::performancesDir();
    dir.createDirectory();

    saveAsChooser = std::make_unique<juce::FileChooser>(
        "Save performance as...", dir.getChildFile("New Performance.jvpf"), "*.jvpf");

    saveAsChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser &fc)
        {
          auto file = fc.getResult();
          if (file == juce::File{})
            return;
          // Not every OS/dialog re-appends the filter extension when the whole filename field
          // was retyped rather than edited in place (same caveat as PatchBrowser's Save As...).
          if (!file.hasFileExtension("jvpf"))
            file = file.withFileExtension("jvpf");

          processor.savePerformanceAs(file);
          bankListBox.updateContent();
          bankListBox.repaint();
        });
  };

  addAndMakeVisible(deleteButton);
  deleteButton.onClick = [this]
  {
    const int row = bankListBox.getSelectedRow();
    if (row < 0 || row >= (int)processor.performanceBank.size())
      return;
    processor.performanceBank[(size_t)row].file.deleteFile();
    processor.refreshPerformanceBank();
    bankListBox.updateContent();
    bankListBox.repaint();
  };

  bankListBox.setRowHeight(20);
  addAndMakeVisible(bankListBox);

  refreshFromProcessor();
}

PerformanceTab::~PerformanceTab() {}

void PerformanceTab::pushPartParams(int partIndex)
{
  auto &row = partRows[(size_t)partIndex];
  processor.setPerformancePartParams(partIndex, row.channelCombo.getSelectedId(),
                                     (int)row.levelSlider.getValue(),
                                     (int)row.panSlider.getValue(),
                                     row.enabledToggle.getToggleState());
}

void PerformanceTab::updatePartRowFromState(int partIndex)
{
  auto &part = processor.performanceParts[(size_t)partIndex];
  auto &row = partRows[(size_t)partIndex];
  const bool isRhythmPart = (partIndex == VirtualJVProcessor::kNumPerformanceParts - 1);

  juce::String label = "Part " + juce::String(partIndex + 1) + (isRhythmPart ? " (Rhythm): " : ": ")
                      + (part.present ? juce::String(part.name) : "Empty");
  row.nameLabel.setText(label, juce::dontSendNotification);
  row.channelCombo.setSelectedId(part.midiChannel, juce::dontSendNotification);
  row.levelSlider.setValue(part.level, juce::dontSendNotification);
  row.panSlider.setValue(part.pan, juce::dontSendNotification);
  row.enabledToggle.setToggleState(part.enabled, juce::dontSendNotification);
}

void PerformanceTab::refreshFromProcessor()
{
  enablePerformanceToggle.setToggleState(processor.performanceModeEnabled, juce::dontSendNotification);

  if (!nameEditor.hasKeyboardFocus(false))
    nameEditor.setText(juce::String(processor.performanceName).trimEnd(), juce::dontSendNotification);

  for (int i = 0; i < VirtualJVProcessor::kNumPerformanceParts; i++)
    updatePartRowFromState(i);

  bankListBox.updateContent();
  bankListBox.repaint();
}

void PerformanceTab::resized()
{
  const int margin = 10;
  int y = margin;

  enablePerformanceToggle.setBounds(margin, y, 260, 24);
  nameLabel.setBounds(margin + 270, y, 130, 24);
  nameEditor.setBounds(margin + 270 + 130 + 6, y, 160, 24);
  y += 24 + margin;

  const int nameW = 220, comboW = 80, sliderW = 120, toggleW = 50, clearW = 70;
  const int colGap = 10;

  int headerX = margin + nameW + colGap;
  channelHeader.setBounds(headerX, y, comboW, 18);
  headerX += comboW + colGap;
  levelHeader.setBounds(headerX, y, sliderW, 18);
  headerX += sliderW + colGap;
  panHeader.setBounds(headerX, y, sliderW, 18);
  y += 18 + 4;

  const int rowH = 26;
  for (auto &row : partRows)
  {
    int x = margin;
    row.nameLabel.setBounds(x, y, nameW, rowH);
    x += nameW + colGap;
    row.channelCombo.setBounds(x, y + 2, comboW, rowH - 4);
    x += comboW + colGap;
    row.levelSlider.setBounds(x, y + 2, sliderW, rowH - 4);
    x += sliderW + colGap;
    row.panSlider.setBounds(x, y + 2, sliderW, rowH - 4);
    x += sliderW + colGap;
    row.enabledToggle.setBounds(x, y + 2, toggleW, rowH - 4);
    x += toggleW + colGap;
    row.clearButton.setBounds(x, y + 2, clearW, rowH - 4);
    y += rowH + 4;
  }

  y += margin;
  bankHeaderLabel.setBounds(margin, y, 200, 22);
  saveAsButton.setBounds(getWidth() - margin - 220, y, 106, 22);
  deleteButton.setBounds(getWidth() - margin - 106, y, 106, 22);
  y += 22 + 4;

  bankListBox.setBounds(margin, y, getWidth() - margin * 2,
                        juce::jmax(0, getHeight() - y - margin));
}

//==============================================================================
int PerformanceTab::BankListModel::getNumRows()
{
  return (int)owner.processor.performanceBank.size();
}

void PerformanceTab::BankListModel::paintListBoxItem(int rowNumber, juce::Graphics &g, int width,
                                                      int height, bool rowIsSelected)
{
  g.fillAll(rowIsSelected ? juce::Colour(0xff42A2C8) : juce::Colour(0xff263238));
  g.setColour(rowIsSelected ? juce::Colours::black : juce::Colours::white);

  if (rowNumber >= 0 && rowNumber < (int)owner.processor.performanceBank.size())
    g.drawFittedText(owner.processor.performanceBank[(size_t)rowNumber].name, {5, 0, width, height - 2},
                     juce::Justification::left, 1);
}

void PerformanceTab::BankListModel::selectedRowsChanged(int lastRowSelected)
{
  if (lastRowSelected < 0)
    return;
  owner.processor.loadPerformance(lastRowSelected);
}
