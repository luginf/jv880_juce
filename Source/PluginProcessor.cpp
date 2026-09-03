/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
// Nuked-SC55-derived cores like this one emulate the real chip's output level bit-for-bit
// rather than going through an adjustable per-voice constant (contrast the D-50 emulator's own
// kOutputMakeupGain, tuned against its own internal 0.37 level), so there's no internal knob to
// raise without risking accuracy. Alan reported the JV-880 sounding noticeably quieter than the
// D-110 emulator by ear; applied post-synthesis, same technique and reasoning as the D-50's own
// makeup gain. 1.5x is a starting estimate (this session has no working audio output to verify
// against a reference by ear) - the Settings tab's Master Volume slider stacks on top of it as
// a pure attenuator, so if this still isn't enough Alan can say so and it can be raised further.
constexpr float kOutputMakeupGain = 1.5f;

juce::File keyboardSettingsFile() {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
      .getChildFile("JV880")
      .getChildFile("keyboard_settings.xml");
}

void loadPersistedKeyboardSettings(bool &pcInput, int &pcLayout) {
  auto file = keyboardSettingsFile();
  if (!file.existsAsFile())
    return;

  if (auto xml = juce::XmlDocument::parse(file)) {
    pcInput = xml->getBoolAttribute("pcInput", pcInput);
    pcLayout = xml->getIntAttribute("pcLayout", pcLayout);
  }
}

void savePersistedKeyboardSettings(bool pcInput, int pcLayout) {
  juce::XmlElement xml("KEYBOARD_SETTINGS");
  xml.setAttribute("pcInput", pcInput);
  xml.setAttribute("pcLayout", pcLayout);

  auto file = keyboardSettingsFile();
  file.getParentDirectory().createDirectory();
  xml.writeTo(file);
}
} // namespace

//==============================================================================
VirtualJVProcessor::VirtualJVProcessor()
    : AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {

  loadPersistedKeyboardSettings(keyboardPcInput, keyboardPcLayout);

  ownedNames.reserve(52);

  mcu = new MCU();

  if (!preloadAll(loadedRoms))
    return;

  mcu->startSC55(loadedRoms[getRomIndex("jv880_rom1.bin")],
                 loadedRoms[getRomIndex("jv880_rom2.bin")],
                 loadedRoms[getRomIndex("jv880_waverom1.bin")],
                 loadedRoms[getRomIndex("jv880_waverom2.bin")],
                 loadedRoms[getRomIndex("jv880_nvram.bin")]);

  int currentPatchI = 0;

  patchInfoPerGroup.push_back(std::vector<PatchInfo *>());

  // Internal A
  for (int j = 0; j < 64; j++) {
      patchInfos[currentPatchI].name =
          (const char*)&loadedRoms[getRomIndex("jv880_rom2.bin")]
          [0x010ce0 + j * 0x16a];
      patchInfos[currentPatchI].nameLength = 0xc;
      patchInfos[currentPatchI].expansionI = 0xff;
      patchInfos[currentPatchI].patchI = j;
      patchInfos[currentPatchI].present = true;
      patchInfos[currentPatchI].drums = false;
      patchInfos[currentPatchI].iInList = currentPatchI;
      patchInfoPerGroup[0].push_back(&patchInfos[currentPatchI]);
      currentPatchI++;
  }

  // Internal B
  for (int j = 0; j < 64; j++) {
      patchInfos[currentPatchI].name =
          (const char*)&loadedRoms[getRomIndex("jv880_rom2.bin")]
          [0x018ce0 + j * 0x16a];
      patchInfos[currentPatchI].nameLength = 0xc;
      patchInfos[currentPatchI].expansionI = 0xff;
      patchInfos[currentPatchI].patchI = j;
      patchInfos[currentPatchI].present = true;
      patchInfos[currentPatchI].drums = false;
      patchInfos[currentPatchI].iInList = currentPatchI;
      patchInfoPerGroup[0].push_back(&patchInfos[currentPatchI]);
      currentPatchI++;
  }

  // Internal User
  for (int j = 0; j < 64; j++) {
    patchInfos[currentPatchI].name =
        (const char *)&loadedRoms[getRomIndex("jv880_rom2.bin")]
                                 [0x008ce0 + j * 0x16a];
    patchInfos[currentPatchI].nameLength = 0xc;
    patchInfos[currentPatchI].expansionI = 0xff;
    patchInfos[currentPatchI].patchI = j;
    patchInfos[currentPatchI].present = true;
    patchInfos[currentPatchI].drums = false;
    patchInfos[currentPatchI].iInList = currentPatchI;
    patchInfoPerGroup[0].push_back(&patchInfos[currentPatchI]);
    currentPatchI++;
  }

  patchInfos[currentPatchI].name = "Rhythm Set Int A";
  patchInfos[currentPatchI].ptr =
      (char *)&loadedRoms[getRomIndex("jv880_rom2.bin")][0x016760];
  patchInfos[currentPatchI].nameLength = 21;
  patchInfos[currentPatchI].expansionI = 0xff;
  patchInfos[currentPatchI].patchI = 0;
  patchInfos[currentPatchI].present = true;
  patchInfos[currentPatchI].drums = true;
  patchInfos[currentPatchI].iInList = currentPatchI;
  patchInfoPerGroup[0].push_back(&patchInfos[currentPatchI]);
  currentPatchI++;

  patchInfos[currentPatchI].name = "Rhythm Set Int B";
  patchInfos[currentPatchI].ptr =
      (char *)&loadedRoms[getRomIndex("jv880_rom2.bin")][0x01e760];
  patchInfos[currentPatchI].nameLength = 21;
  patchInfos[currentPatchI].expansionI = 0xff;
  patchInfos[currentPatchI].patchI = 0;
  patchInfos[currentPatchI].present = true;
  patchInfos[currentPatchI].drums = true;
  patchInfos[currentPatchI].iInList = currentPatchI;
  patchInfoPerGroup[0].push_back(&patchInfos[currentPatchI]);
  currentPatchI++;

  patchInfos[currentPatchI].name = "Rhythm Set User";
  patchInfos[currentPatchI].ptr =
      (char*)&loadedRoms[getRomIndex("jv880_rom2.bin")][0x00e760];
  patchInfos[currentPatchI].nameLength = 21;
  patchInfos[currentPatchI].expansionI = 0xff;
  patchInfos[currentPatchI].patchI = 0;
  patchInfos[currentPatchI].present = true;
  patchInfos[currentPatchI].drums = true;
  patchInfos[currentPatchI].iInList = currentPatchI;
  patchInfoPerGroup[0].push_back(&patchInfos[currentPatchI]);
  currentPatchI++;

  for (int i = 0; i < NUM_EXPS; i++) {
    const int isRD500 = (i == 0);

    patchInfoPerGroup.push_back(std::vector<PatchInfo *>());

    if ((isRD500 && !romInfos[romCountRequired].loaded) || !romInfos[i + romCountRequired + 1].loaded)
      continue;

    expansionsDescr[i] = loadedRoms[i + 6];

    // get patches
    int nPatches = isRD500 ? 192 : expansionsDescr[i][0x67] | expansionsDescr[i][0x66] << 8;

    for (int j = 0; j < nPatches; j++) {
      size_t patchesOffset =
          expansionsDescr[i][0x8f] | expansionsDescr[i][0x8e] << 8 |
          expansionsDescr[i][0x8d] << 16 | expansionsDescr[i][0x8c] << 24;

      if (isRD500)
      {
        if (j < 64)
          patchesOffset = 0x0ce0;
        else if (j < 128)
          patchesOffset = 0x8370;
        else
          patchesOffset = 0x12b82;
      }

      patchInfos[currentPatchI].name =
          (char *)&expansionsDescr[i][patchesOffset + j * 0x16a];

      if (isRD500)
        patchInfos[currentPatchI].name =
            (char *)&loadedRoms[getRomIndex("rd500_patches.bin")]
                               [patchesOffset + (j % 64) * 0x16a];

      patchInfos[currentPatchI].nameLength = 0xc;
      patchInfos[currentPatchI].expansionI = i;
      patchInfos[currentPatchI].patchI = j;
      patchInfos[currentPatchI].present = true;
      patchInfos[currentPatchI].drums = false;
      patchInfos[currentPatchI].iInList = currentPatchI;
      patchInfoPerGroup[i + 1].push_back(&patchInfos[currentPatchI]);
      currentPatchI++;
    }

    // get drumkits
    int nDrumkits = isRD500 ? 3 : expansionsDescr[i][0x69] | expansionsDescr[i][0x68] << 8;

    for (int j = 0; j < nDrumkits; j++) {
      size_t patchesOffset =
          expansionsDescr[i][0x93] | expansionsDescr[i][0x92] << 8 |
          expansionsDescr[i][0x91] << 16 | expansionsDescr[i][0x90] << 24;

      if (isRD500)
      {
        if (j < 64)
          patchesOffset = 0x6760;
        else if (j < 128)
          patchesOffset = 0xd2a0;
        else
          patchesOffset = 0x18602;
      }

      ownedNames.push_back("Rhythm Set " + std::to_string(j + 1));
      patchInfos[currentPatchI].name = ownedNames.back().c_str();

      patchInfos[currentPatchI].ptr =
          (const char *)&expansionsDescr[i][patchesOffset + j * 0xa7c];

      if (isRD500)
        patchInfos[currentPatchI].ptr =
            (const char *)&loadedRoms[getRomIndex("rd500_patches.bin")]
                                     [patchesOffset];

      patchInfos[currentPatchI].nameLength = (int)ownedNames.back().size();
      patchInfos[currentPatchI].expansionI = i;
      patchInfos[currentPatchI].patchI = j;
      patchInfos[currentPatchI].present = true;
      patchInfos[currentPatchI].drums = true;
      patchInfos[currentPatchI].iInList = currentPatchI;
      patchInfoPerGroup[i + 1].push_back(&patchInfos[currentPatchI]);
      currentPatchI++;
    }

    // total count
    totalPatchesExp += nPatches;
    totalPatchesExp += nDrumkits;
  }

  // The "User" bank - one flat group appended after every ROM-based one (indices 0..NUM_EXPS
  // above), populated from disk by refreshUserPatches().
  patchInfoPerGroup.push_back(std::vector<PatchInfo *>());
  userToneBuffers.reserve(userPatchCapacity);
  userDrumBuffers.reserve(userPatchCapacity);
  userPatchNames.reserve(userPatchCapacity);
  refreshUserPatches();

  loaded = true;
}

VirtualJVProcessor::~VirtualJVProcessor() {
  mcuLock.enter();
  delete mcu;
  mcuLock.exit();
}

//==============================================================================
const juce::String VirtualJVProcessor::getName() const {
  return JucePlugin_Name;
}

bool VirtualJVProcessor::acceptsMidi() const { return true; }

bool VirtualJVProcessor::producesMidi() const { return false; }

bool VirtualJVProcessor::isMidiEffect() const { return false; }

double VirtualJVProcessor::getTailLengthSeconds() const { return 0.0; }

int VirtualJVProcessor::getNumPrograms() {
  return 65                // internal
         + 65              // bank A
         + 65              // bank B
         + totalPatchesExp // expansions
         + numUserPatches  // user-saved patches (Browse tab's "User" bank)
      ;
}

int VirtualJVProcessor::getCurrentProgram() {
  return 0; // TODO
}

void VirtualJVProcessor::setCurrentProgram(int index) {
  // Not getNumPrograms(): that's a rough count of the CONTIGUOUS ROM range starting at 0
  // (195 + totalPatchesExp), but user patches live in patchInfos[]'s separate reserved tail
  // starting at romPatchCapacity (see PluginProcessor.h) - a much higher, unrelated index that
  // getNumPrograms() was always going to reject. .present is what actually says a slot is
  // populated, regardless of which range it's in (Alan's report, 2026-09-04: clicking a saved
  // User patch didn't reload it - this guard was silently returning before doing anything).
  if (index < 0 || index >= romPatchCapacity + userPatchCapacity || !patchInfos[index].present)
    return;

  if (!loaded)
    return;

  mcuLock.enter();

  // Cache the OUTGOING patch's live edits before overwriting the edit buffer, keyed by its own
  // index, so switching back to it later in this session restores those edits instead of
  // silently reloading the pristine ROM/file version (Alan's report, 2026-09-04: clicking
  // another patch and back lost every unsaved change). Skipped on the very first call
  // (currentPatchIndex still -1, nothing loaded yet to cache).
  if (currentPatchIndex >= 0) {
    if (status.isDrums)
      memcpy(editedDrumsCache[currentPatchIndex].data(), status.drums, 0xa7c);
    else
      memcpy(editedPatchCache[currentPatchIndex].data(), status.patch, 0x16a);
  }

  int expansionI = patchInfos[index].expansionI;

  // The extra bounds/nullptr check is for user-saved patches (Alan's request, 2026-09-04):
  // saveCurrentPatchAs() records which expansion was active so the right waverom_exp comes
  // back on reload (see its own comment), but that expansion might not be loaded on whatever
  // machine/ROM-set the patch is later opened with - fall back to leaving waverom_exp alone
  // (silently wrong/no expansion samples for that patch, rather than reading a garbage or
  // out-of-range expansionsDescr[] pointer).
  if (expansionI != 0xff && expansionI >= 0 && expansionI < NUM_EXPS
      && expansionsDescr[expansionI] != nullptr
      && status.currentExpansion != expansionI) {
    status.currentExpansion = expansionI;
    memcpy(mcu->pcm.waverom_exp, expansionsDescr[expansionI], 0x800000);
    mcu->SC55_Reset();
  }

  if (patchInfos[index].drums) {
    status.isDrums = true;
    mcu->nvram[0x11] = 0;
    // Pristine copy for revertCurrentPatch() - always straight from the (read-only) ROM/file
    // source, regardless of whether an in-session edit (below) is what actually gets loaded.
    memcpy(originalDrumsSnapshot, (uint8_t *)patchInfos[index].ptr, 0xa7c);
    {
      const auto cached = editedDrumsCache.find(index);
      const uint8_t *source = cached != editedDrumsCache.end() ? cached->second.data() : originalDrumsSnapshot;
      memcpy(&mcu->nvram[0x67f0], source, 0xa7c);
    }
    memcpy(status.drums, &mcu->nvram[0x67f0], 0xa7c);
    mcu->SC55_Reset();
  } else {
    status.isDrums = false;
    memcpy(originalPatchSnapshot, (uint8_t *)patchInfos[index].name, 0x16a);
    const auto cached = editedPatchCache.find(index);
    const uint8_t *source = cached != editedPatchCache.end() ? cached->second.data() : originalPatchSnapshot;
    if (mcu->nvram[0x11] != 1) {
      mcu->nvram[0x11] = 1;
      memcpy(&mcu->nvram[0x0d70], source, 0x16a);
      memcpy(status.patch, &mcu->nvram[0x0d70], 0x16a);
      mcu->SC55_Reset();
    } else {
      memcpy(&mcu->nvram[0x0d70], source, 0x16a);
      memcpy(status.patch, &mcu->nvram[0x0d70], 0x16a);
      uint8_t buffer[2] = {0xC0, 0x00};
      mcu->postMidiSC55(buffer, sizeof(buffer));
    }
  }

  currentPatchIndex = index;

  mcuLock.exit();

  if (auto editor = getActiveEditor())
  {
      auto e = dynamic_cast<VirtualJVEditor*>(editor);

      e->showToneOrRhythmEditTabs(status.isDrums);
      e->updateEditTabs();
  }
}

// Discards every edit made since the current patch was loaded, restoring the snapshot
// setCurrentProgram() took right after copying it in from its (read-only) ROM source - Alan's
// request. Mirrors setCurrentProgram()'s own "same mode, reload the buffer" path (Program
// Change for tone, a full reset for drums) rather than doing a full SC55_Reset() in both cases,
// so reverting a tone patch doesn't have the audible glitch a full engine reset causes.
void VirtualJVProcessor::revertCurrentPatch() {
  if (!loaded)
    return;

  mcuLock.enter();

  if (status.isDrums) {
    memcpy(&mcu->nvram[0x67f0], originalDrumsSnapshot, 0xa7c);
    memcpy(status.drums, originalDrumsSnapshot, 0xa7c);
    mcu->SC55_Reset();
  } else {
    memcpy(&mcu->nvram[0x0d70], originalPatchSnapshot, 0x16a);
    memcpy(status.patch, originalPatchSnapshot, 0x16a);
    uint8_t buffer[2] = {0xC0, 0x00};
    mcu->postMidiSC55(buffer, sizeof(buffer));
  }

  mcuLock.exit();

  if (auto editor = getActiveEditor())
  {
      if (auto e = dynamic_cast<VirtualJVEditor*>(editor))
        e->updateEditTabs();
  }
}

bool VirtualJVProcessor::isPatchModified(int index) const {
  if (index < 0 || index >= romPatchCapacity + userPatchCapacity || !patchInfos[index].present)
    return false;

  const PatchInfo &info = patchInfos[index];

  if (index == currentPatchIndex) {
    return info.drums ? memcmp(status.drums, originalDrumsSnapshot, 0xa7c) != 0
                       : memcmp(status.patch, originalPatchSnapshot, 0x16a) != 0;
  }

  if (info.drums) {
    const auto it = editedDrumsCache.find(index);
    return it != editedDrumsCache.end() && memcmp(it->second.data(), info.ptr, 0xa7c) != 0;
  }
  const auto it = editedPatchCache.find(index);
  return it != editedPatchCache.end() && memcmp(it->second.data(), info.name, 0x16a) != 0;
}

juce::File VirtualJVProcessor::userPatchesDir() {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
      .getChildFile("JV880")
      .getChildFile("UserPatches");
}

bool VirtualJVProcessor::saveCurrentPatchAs(const juce::File &file) {
  if (!loaded)
    return false;

  // 1-byte type marker (0 = tone patch, 1 = rhythm set), 1-byte expansion index (0xff = none/
  // internal-only - Alan's question, 2026-09-04: a patch using an expansion board's waveforms
  // needs that same board's waverom_exp loaded again on reload, or its wave numbers resolve
  // against the wrong (or no) samples - see setCurrentProgram()'s own comment on the matching
  // read-back), then the raw bytes.
  juce::MemoryBlock block;
  uint8_t marker = status.isDrums ? 1 : 0;
  block.append(&marker, 1);
  uint8_t expansionByte = (uint8_t)juce::jlimit(0, 0xff, status.currentExpansion);
  block.append(&expansionByte, 1);

  mcuLock.enter();
  if (status.isDrums) {
    block.append(status.drums, sizeof(status.drums));
  } else {
    uint8_t buf[sizeof(status.patch)];
    memcpy(buf, status.patch, sizeof(buf));
    // Embed the chosen name into the patch's own name field (first 12 bytes - Patch::name in
    // dataStructures.h), so it reads back and displays exactly like a factory patch does.
    // Rhythm sets have no equivalent embedded field (see the Rhythm struct's own comment),
    // hence the file's own name being the only source of truth for those.
    char nameBuf[12] = {0};
    auto nameUtf8 = file.getFileNameWithoutExtension().toRawUTF8();
    memcpy(nameBuf, nameUtf8, std::min(sizeof(nameBuf), strlen(nameUtf8)));
    memcpy(buf, nameBuf, sizeof(nameBuf));
    block.append(buf, sizeof(buf));
  }
  mcuLock.exit();

  file.getParentDirectory().createDirectory();
  if (!file.replaceWithData(block.getData(), block.getSize()))
    return false;

  refreshUserPatches();
  return true;
}

// (Re)scans userPatchesDir() and rebuilds the reserved "User" tail of patchInfos[]/
// patchInfoPerGroup[userGroupIndex] from what's there - called once at startup and again after
// every saveCurrentPatchAs(), so a freshly-saved patch shows up in the browser immediately.
void VirtualJVProcessor::refreshUserPatches() {
  constexpr size_t kPatchBytes = 0x16a;
  constexpr size_t kRhythmBytes = 0xa7c;

  userToneBuffers.clear();
  userDrumBuffers.clear();
  userPatchNames.clear();
  numUserPatches = 0;

  auto &group = patchInfoPerGroup[userGroupIndex];
  group.clear();

  auto dir = userPatchesDir();
  dir.createDirectory();

  auto files = dir.findChildFiles(juce::File::findFiles, false, "*.jvp");
  files.sort();

  for (auto &file : files) {
    if (numUserPatches >= userPatchCapacity)
      break; // the reserved tail of patchInfos[] is a fixed size

    juce::MemoryBlock block;
    if (!file.loadFileAsData(block) || block.getSize() < 2)
      continue;

    const auto *bytes = static_cast<const uint8_t *>(block.getData());
    const bool isDrums = bytes[0] != 0;
    const int expansionByte = bytes[1];
    const size_t expected = isDrums ? kRhythmBytes : kPatchBytes;
    if (block.getSize() != 2 + expected)
      continue; // not a file this build wrote (wrong size) - skip rather than risk misreading it

    PatchInfo &info = patchInfos[romPatchCapacity + numUserPatches];
    // Which expansion's waverom_exp to reload alongside this patch (Alan's question,
    // 2026-09-04) - 0xff means none/internal-only. Bounds-checked here too:
    // setCurrentProgram()'s own check additionally requires that expansion's ROM to actually
    // be loaded on THIS run, which may not hold (the patch could have been saved on a machine
    // with a different ROM set).
    info.expansionI = (expansionByte >= 0 && expansionByte < NUM_EXPS) ? expansionByte : 0xff;
    info.patchI = 0;
    info.present = true;
    info.drums = isDrums;
    info.iInList = romPatchCapacity + numUserPatches;

    if (isDrums) {
      auto &buf = userDrumBuffers.emplace_back();
      memcpy(buf.data(), bytes + 2, kRhythmBytes);
      info.ptr = (const char *)buf.data();
      userPatchNames.push_back(file.getFileNameWithoutExtension().toStdString());
      info.name = userPatchNames.back().c_str();
      info.nameLength = (int)userPatchNames.back().size();
    } else {
      auto &buf = userToneBuffers.emplace_back();
      memcpy(buf.data(), bytes + 2, kPatchBytes);
      info.name = (const char *)buf.data(); // first 12 bytes are the embedded patch name
      info.nameLength = 12;
    }

    group.push_back(&info);
    numUserPatches++;
  }
}

const juce::String VirtualJVProcessor::getProgramName(int index) {
  // See setCurrentProgram()'s own comment on why this isn't bounded by getNumPrograms().
  if (index < 0 || index >= romPatchCapacity + userPatchCapacity || !patchInfos[index].present)
    return {};
  int length = patchInfos[index].nameLength;
  const char *strPtr = (const char *)patchInfos[index].name;
  return juce::String(strPtr, length);
}

void VirtualJVProcessor::changeProgramName(int /* index */,
                                                 const juce::String& /* newName */) { }

//==============================================================================
void VirtualJVProcessor::prepareToPlay(double sampleRate,
                                             int /* samplesPerBlock */) {
  keyboardCollector.reset(sampleRate);
}

void VirtualJVProcessor::releaseResources() {
  // When playback stops, you can use this as an opportunity to free up any
  // spare memory, etc.
}

bool VirtualJVProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    return false;

  return true;
}

void VirtualJVProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midiMessages)
{
  // The on-screen VirtualKeyboard's notes, queued by injectTestNote() - merged in exactly the
  // same way a real MIDI IN port's messages would be.
  keyboardCollector.removeNextBlockOfMessages(midiMessages, buffer.getNumSamples());

  mcuLock.enter();

  for (const auto metadata : midiMessages)
  {
    auto message = metadata.getMessage();

    message.setChannel(status.isDrums ? 10 : 1);

    if (message.isNoteOnOrOff())
    {
      int note = message.getNoteNumber();
      if (note >= 0 && note < 128)
        noteActiveTable[static_cast<size_t>(note)].store(message.isNoteOn());
    }

    int samplePos = int(((double)metadata.samplePosition / getSampleRate()) * 64000.0);

    mcu->enqueueMidiSC55(message.getRawData(), message.getRawDataSize(), samplePos);
  }

  juce::ScopedNoDenormals noDenormals;

  auto totalNumInputChannels = getTotalNumInputChannels();
  auto totalNumOutputChannels = getTotalNumOutputChannels();

  for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
  {
    buffer.clear(i, 0, buffer.getNumSamples());
  }

  if (!loaded)
  {
    mcuLock.exit();
    return;
  }

  float *channelDataL = buffer.getWritePointer(0);
  float *channelDataR = buffer.getWritePointer(1);

  mcu->updateSC55WithSampleRate(channelDataL, channelDataR,
                                buffer.getNumSamples(), (int)getSampleRate());

  mcuLock.exit();

  buffer.applyGain(kOutputMakeupGain * masterVolume);
}

//==============================================================================
bool VirtualJVProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor *VirtualJVProcessor::createEditor() {
  return new VirtualJVEditor(*this);
}

//==============================================================================
void VirtualJVProcessor::getStateInformation(juce::MemoryBlock &destData)
{
  mcuLock.enter();
  status.masterTune = mcu->nvram[0x00];
  status.reverbEnabled = ((mcu->nvram[0x02] >> 0) & 1) == 1;
  status.chorusEnabled = ((mcu->nvram[0x02] >> 1) & 1) == 1;
  mcuLock.exit();

  destData.ensureSize(sizeof(DataToSave));
  destData.replaceAll(&status, sizeof(DataToSave));
}

void VirtualJVProcessor::setStateInformation(const void *data, int /* sizeInBytes */)
{
  memcpy(&status, data, sizeof(DataToSave));

  mcuLock.enter();

  mcu->nvram[0x0d] |= 1 << 5; // LastSet
  mcu->nvram[0x00] = status.masterTune;
  mcu->nvram[0x02] = status.reverbEnabled | status.chorusEnabled << 1;

  if (expansionsDescr[status.currentExpansion] == nullptr) {
    mcuLock.exit();
    return;
  }

  memcpy(mcu->pcm.waverom_exp, expansionsDescr[status.currentExpansion],
         0x800000);
  mcu->nvram[0x11] = status.isDrums ? 0 : 1;
  memcpy(&mcu->nvram[0x67f0], status.drums, 0xa7c);
  memcpy(&mcu->nvram[0x0d70], status.patch, 0x16a);
  mcu->SC55_Reset();
  mcuLock.exit();

  if (auto editor = getActiveEditor())
  {
      auto e = dynamic_cast<VirtualJVEditor*>(editor);

      e->setLCDColor((LCDisplay::Color)status.selectedLCDColor);
      e->showToneOrRhythmEditTabs(status.isDrums);
      e->setSelectedTab(status.selectedTab);
      e->updateEditTabs();

      if (status.selectedRom > -1)
      {
          e->setSelectedROM(status.selectedRom);
      }
  }
}

void VirtualJVProcessor::injectTestNote(int channel, int note, float velocity, bool on) {
  auto message = on ? juce::MidiMessage::noteOn(channel, note, velocity)
                     : juce::MidiMessage::noteOff(channel, note, velocity);
  message.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
  keyboardCollector.addMessageToQueue(message);
}

void VirtualJVProcessor::setKeyboardPcInputEnabled(bool enabled) {
  keyboardPcInput = enabled;
  savePersistedKeyboardSettings(keyboardPcInput, keyboardPcLayout);
}

void VirtualJVProcessor::setKeyboardPcLayout(int layout) {
  keyboardPcLayout = juce::jlimit(0, 1, layout);
  savePersistedKeyboardSettings(keyboardPcInput, keyboardPcLayout);
}

void VirtualJVProcessor::sendSysexParamChange(uint32_t address,
                                                    uint8_t value) {
  uint8_t data[5];
  data[0] = (address >> 21) & 127; // address MSB
  data[1] = (address >> 14) & 127; // address
  data[2] = (address >> 7) & 127;  // address
  data[3] = (address >> 0) & 127;  // address LSB
  data[4] = value;                 // data

  uint32_t checksum = 0;

  for (size_t i = 0; i < 5; i++) {
    checksum += data[i];

    if (checksum >= 128) {
      checksum -= 128;
    }
  }

  uint8_t buf[12];
  buf[0] = 0xf0;
  buf[1] = 0x41;
  buf[2] = 0x10; // unit number
  buf[3] = 0x46;
  buf[4] = 0x12; // command

  checksum = 128 - checksum;

  for (size_t i = 0; i < 5; i++) {
    buf[i + 5] = data[i];
  }

  buf[10] = checksum;
  buf[11] = 0xf7;

  mcuLock.enter();
  mcu->postMidiSC55(buf, 12);
  mcuLock.exit();
}

#define BITSWAP16(x) (((x & 0xFF00) >> 8) | ((x & 0x00FF) << 8))
#define BITSWAP32(x) (((x & 0xFF000000) >> 24) | ((x & 0x00FF0000) >> 8) | ((x & 0x0000FF00) << 8) | ((x & 0x000000FF) << 24))

std::vector<std::string> VirtualJVProcessor::readMultisampleNames(uint8_t romIdx)
{
    std::vector<std::string> names;

    if (!romInfos[romIdx].loaded)
    {
        return names;
    }

    auto& msNamePtr = loadedRoms[romIdx];
    const int msOffset = 0x3c;
    uint16_t msCount;
    uint32_t msTableAddr;

    memcpy(&msCount, &msNamePtr[0x62], 2);
    memcpy(&msTableAddr, &msNamePtr[0x84], 4);

    // ROMs are written in big endian format...
    msCount = BITSWAP16(msCount);
    msTableAddr = BITSWAP32(msTableAddr);

    // 880's factory multisamples start from a different place in ROM
    if (romIdx == 2)
    {
        msCount = 129;
        msTableAddr = 4;
    }

    std::string name;

    name.reserve(12u);

    for (int i = 0; i < msCount; i++)
    {
        name.clear();

        for (int c = 0; c < 12; c++)
        {
            name.push_back((char)msNamePtr[msTableAddr + (msOffset * i) + c]);
        }

        names.emplace_back(name);
    }

    return names;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new VirtualJVProcessor();
}
