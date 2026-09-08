/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

// Where the ROM folder override (see rom.h's setRomsDirectoryOverride()) is persisted -
// deliberately always at this fixed default app-data location regardless of what the override
// itself points to (same reasoning as keyboardSettingsFile()), so it's always findable even if
// the folder it names has since moved, been deleted, or was never valid to begin with.
juce::File romFolderSettingsFile() {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
      .getChildFile("JV880")
      .getChildFile("rom_folder.txt");
}

juce::File loadPersistedRomFolderOverride() {
  auto file = romFolderSettingsFile();
  if (!file.existsAsFile())
    return {};
  auto path = file.loadFileAsString().trim();
  return path.isEmpty() ? juce::File{} : juce::File(path);
}

void savePersistedRomFolderOverride(const juce::File &dir) {
  auto file = romFolderSettingsFile();
  if (dir == juce::File{}) {
    file.deleteFile();
    return;
  }
  file.getParentDirectory().createDirectory();
  file.replaceWithText(dir.getFullPathName());
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

  {
    auto overrideDir = loadPersistedRomFolderOverride();
    if (overrideDir != juce::File{})
      setRomsDirectoryOverride(overrideDir.getFullPathName().toStdString());
  }

  attemptLoadRoms();
}

// See this method's own comment in PluginProcessor.h.
bool VirtualJVProcessor::attemptLoadRoms() {
  if (loaded)
    return true;

  if (!preloadAll(loadedRoms))
    return false;

  mcu->startSC55(loadedRoms[getRomIndex("jv880_rom1.bin")],
                 loadedRoms[getRomIndex("jv880_rom2.bin")],
                 loadedRoms[getRomIndex("jv880_waverom1.bin")],
                 loadedRoms[getRomIndex("jv880_waverom2.bin")],
                 loadedRoms[getRomIndex("jv880_nvram.bin")]);

  // Headless self-test for the native Performance Temp NVRAM hunt (see CLAUDE.md's "Piste
  // future" section) - drives the emulator's own tick function directly instead of going through
  // a real audio device, so it works even with no audio backend available (e.g. this sandbox).
  // Exits before any window/audio device is created, so it needs no display either. Temporary
  // investigation tool, not part of the plugin's normal behavior.
  if (const char *selfTestButtonEnv = std::getenv("JV880_SELFTEST_BUTTON")) {
    // Comma-separated button ids, pressed one after another (e.g. "10,11" = PATCH_PERFORM then
    // EDIT), each held for holdMs and separated by a postMs settle period.
    const int preMs = std::getenv("JV880_SELFTEST_PRE_MS") ? std::atoi(std::getenv("JV880_SELFTEST_PRE_MS")) : 3000;
    const int holdMs = std::getenv("JV880_SELFTEST_HOLD_MS") ? std::atoi(std::getenv("JV880_SELFTEST_HOLD_MS")) : 150;
    const int postMs = std::getenv("JV880_SELFTEST_POST_MS") ? std::atoi(std::getenv("JV880_SELFTEST_POST_MS")) : 3000;

    const int sampleRate = 44100;
    const unsigned int blockFrames = 512;
    std::vector<float> l(blockFrames), r(blockFrames);

    auto runMs = [&](int ms) {
      int totalFrames = sampleRate * ms / 1000;
      int done = 0;
      while (done < totalFrames) {
        mcu->updateSC55WithSampleRate(l.data(), r.data(), blockFrames, sampleRate);
        done += (int)blockFrames;
      }
    };

    std::fprintf(stderr, "[selftest] booting %d ms...\n", preMs);
    runMs(preMs);

    if (auto *f = std::fopen("/tmp/jv880_nvram_before.bin", "wb")) {
      std::fwrite(mcu->nvram, 1, sizeof(mcu->nvram), f);
      std::fclose(f);
    }

    // Tokens are either a plain button id ("10") or "e0"/"e1" for one MCU_EncoderTrigger pulse
    // in that direction (the data entry dial has no button id of its own).
    juce::StringArray tokens;
    tokens.addTokens(juce::String(selfTestButtonEnv), ",", "");
    for (auto &rawTok : tokens) {
      auto tok = rawTok.trim();
      if (tok.startsWithIgnoreCase("e")) {
        const int dir = tok.substring(1).getIntValue();
        std::fprintf(stderr, "[selftest] encoder pulse dir=%d\n", dir);
        mcu->MCU_EncoderTrigger(dir);
      } else {
        const int buttonId = tok.getIntValue();
        std::fprintf(stderr, "[selftest] pressing button %d for %d ms\n", buttonId, holdMs);
        mcu->lcd.LCD_SendButton((uint8_t)buttonId, 1);
        runMs(holdMs);
        mcu->lcd.LCD_SendButton((uint8_t)buttonId, 0);
      }
      std::fprintf(stderr, "[selftest] settling %d ms\n", postMs);
      runMs(postMs);
    }

    if (auto *f = std::fopen("/tmp/jv880_nvram_after.bin", "wb")) {
      std::fwrite(mcu->nvram, 1, sizeof(mcu->nvram), f);
      std::fclose(f);
    }

    std::fprintf(stderr, "[selftest] done, exiting\n");
    std::exit(0);
  }

  // Headless validation for driving the firmware's *native* Performance mode over SysEx DT1
  // instead of button-press NVRAM diffing - address map transcribed from
  // ~/src/D110/edisyn/edisyn/synth/rolandjv880/RolandJV880Multi.java (Sean Luke's Edisyn, already
  // reverse-engineered against real JV-880/JV-80 hardware). Temporary investigation tool, see
  // CLAUDE.md's "Mode Performance" section.
  if (std::getenv("JV880_SELFTEST_PERF")) {
    const int preMs = std::getenv("JV880_SELFTEST_PRE_MS") ? std::atoi(std::getenv("JV880_SELFTEST_PRE_MS")) : 3000;
    const int settleMs = std::getenv("JV880_SELFTEST_POST_MS") ? std::atoi(std::getenv("JV880_SELFTEST_POST_MS")) : 300;

    const int sampleRate = 44100;
    const unsigned int blockFrames = 512;
    std::vector<float> l(blockFrames), r(blockFrames);

    auto runMs = [&](int ms) {
      int totalFrames = sampleRate * ms / 1000;
      int done = 0;
      while (done < totalFrames) {
        mcu->updateSC55WithSampleRate(l.data(), r.data(), blockFrames, sampleRate);
        done += (int)blockFrames;
      }
    };

    auto sendDT1 = [&](uint8_t AA, uint8_t BB, uint8_t CC, uint8_t DD, const std::vector<uint8_t> &payload) {
      std::vector<uint8_t> msg = { 0xF0, 0x41, 0x10, 0x46, 0x12, AA, BB, CC, DD };
      uint32_t sum = AA + BB + CC + DD;
      for (auto b : payload) { msg.push_back(b); sum += b; }
      uint8_t checksum = (uint8_t)((0x80 - (sum & 0x7F)) & 0x7F);
      msg.push_back(checksum);
      msg.push_back(0xF7);
      mcu->postMidiSC55(msg.data(), (int)msg.size());
    };

    std::fprintf(stderr, "[selftest-perf] booting %d ms...\n", preMs);
    runMs(preMs);

    std::fprintf(stderr, "[selftest-perf] switching to Performance mode (System 00 00 00 00 = 0)\n");
    sendDT1(0, 0, 0, 0, { 0x00 });
    runMs(settleMs);

    std::fprintf(stderr, "[selftest-perf] writing Performance Common (name marker)\n");
    std::vector<uint8_t> common(31, 0);
    const char *marker = "TESTPERF1234";
    for (int i = 0; i < 12; i++) common[(size_t)i] = (uint8_t)marker[i];
    sendDT1(0, 0, 0x10, 0x00, common);
    runMs(settleMs);

    // Verify whether this project's "Internal A"/"Internal B" ROM categories are really the
    // firmware's own read-only Preset A / Preset B patch banks (bank 2 / bank 3 in the
    // bank*64+number patchnumber encoding) - if so, Performance Parts can reference any of those
    // 128 factory patches with zero extra work (no need to also write into the real writable
    // Internal Patch Memory bank). Part 1 -> Preset A #1 (value 128), Part 2 -> Preset B #1
    // (value 192) - both should read back on the LCD as "A.Piano 1" per patchInfos[]'s own
    // Internal A/B index 0 (see the Browse tab's first two columns).
    int part1PatchValue = std::getenv("JV880_SELFTEST_PART1_PATCH") ? std::atoi(std::getenv("JV880_SELFTEST_PART1_PATCH")) : 128;
    int part2PatchValue = std::getenv("JV880_SELFTEST_PART2_PATCH") ? std::atoi(std::getenv("JV880_SELFTEST_PART2_PATCH")) : 192;

    std::fprintf(stderr, "[selftest-perf] writing Part 1 (patch=%d, ch=1, level=127)\n", part1PatchValue);
    std::vector<uint8_t> part1(35, 0);
    part1[21] = 1;                    // receiveswitch on
    part1[22] = 0;                    // receivechannel 1 (0-indexed)
    part1[23] = (part1PatchValue >> 4) & 0x7F; // patchnumber MSB nibble
    part1[24] = part1PatchValue & 0x0F;        // patchnumber LSB nibble
    part1[25] = 127;                  // partlevel
    part1[26] = 64;                   // partpan centre
    part1[29] = 1;                    // reverbswitch
    part1[30] = 1;                    // chorusswitch
    sendDT1(0, 0, 0x18, 0x00, part1);
    runMs(settleMs);

    std::fprintf(stderr, "[selftest-perf] writing Part 2 (patch=%d, ch=2, level=100)\n", part2PatchValue);
    std::vector<uint8_t> part2(35, 0);
    part2[21] = 1;
    part2[22] = 1;                    // receivechannel 2
    part2[23] = (part2PatchValue >> 4) & 0x7F;
    part2[24] = part2PatchValue & 0x0F;
    part2[25] = 100;
    part2[26] = 64;
    part2[29] = 1;
    part2[30] = 1;
    sendDT1(0, 0, 0x19, 0x00, part2);
    runMs(settleMs);

    runMs(2000); // let it settle further

    // Quick audio sanity check: does Part 1 (channel 1) actually make sound with default (all-
    // zero) voice reserve / Common effects settings, or does an unreserved part stay silent?
    {
      uint8_t noteOn[3] = { 0x90, 60, 100 };
      mcu->postMidiSC55(noteOn, 3);
      float peakL = 0, peakR = 0;
      double sumSq = 0;
      int totalFrames = sampleRate * 1; // 1 second
      int done = 0;
      while (done < totalFrames) {
        mcu->updateSC55WithSampleRate(l.data(), r.data(), blockFrames, sampleRate);
        for (unsigned int i = 0; i < blockFrames; i++) {
          peakL = std::max(peakL, std::abs(l[i]));
          peakR = std::max(peakR, std::abs(r[i]));
          sumSq += (double)l[i] * l[i] + (double)r[i] * r[i];
        }
        done += (int)blockFrames;
      }
      uint8_t noteOff[3] = { 0x80, 60, 0 };
      mcu->postMidiSC55(noteOff, 3);
      double rms = std::sqrt(sumSq / (2.0 * totalFrames));
      std::fprintf(stderr, "[selftest-perf] audio check: note on ch1, 1s render: peakL=%.4f peakR=%.4f rms=%.6f\n", peakL, peakR, rms);
    }

    // Verify the bank hypothesis: search for the ROM's own "Internal A" patch #1 name and
    // "Internal B" patch #1 name (12 bytes each, straight from ROM2 - same offsets the
    // constructor's own patchInfos[] loop uses) anywhere in RAM, which would mean the firmware
    // actually loaded that factory patch into Part 1 / Part 2's working Tone Temp mirror.
    const char *romName1 = (const char *)&loadedRoms[getRomIndex("jv880_rom2.bin")][0x010ce0];
    const char *romName2 = (const char *)&loadedRoms[getRomIndex("jv880_rom2.bin")][0x018ce0];
    const char *romNameUser = (const char *)&loadedRoms[getRomIndex("jv880_rom2.bin")][0x008ce0];
    std::fprintf(stderr, "[selftest-perf] Internal A patch#1 name in ROM: \"%.12s\"\n", romName1);
    std::fprintf(stderr, "[selftest-perf] Internal B patch#1 name in ROM: \"%.12s\"\n", romName2);
    std::fprintf(stderr, "[selftest-perf] Internal User patch#1 name in ROM: \"%.12s\"\n", romNameUser);
    auto searchFor12 = [&](const char *label, const char *needle) {
      bool found = false;
      for (size_t i = 0; i + 12 <= sizeof(mcu->sram); i++) {
        if (std::memcmp(&mcu->sram[i], needle, 12) == 0) {
          std::fprintf(stderr, "[selftest-perf] FOUND %s at sram offset 0x%04zx\n", label, i);
          found = true;
        }
      }
      if (!found)
        std::fprintf(stderr, "[selftest-perf] %s NOT found in sram\n", label);
    };
    searchFor12("Internal A patch#1 name", romName1);
    searchFor12("Internal B patch#1 name", romName2);
    searchFor12("Internal User patch#1 name", romNameUser);

    if (auto *f = std::fopen("/tmp/jv880_nvram_perf.bin", "wb")) {
      std::fwrite(mcu->nvram, 1, sizeof(mcu->nvram), f);
      std::fclose(f);
    }
    if (auto *f = std::fopen("/tmp/jv880_sram_perf.bin", "wb")) {
      std::fwrite(mcu->sram, 1, sizeof(mcu->sram), f);
      std::fclose(f);
    }

    auto searchRegion = [&](const char *label, const uint8_t *base, size_t size) {
      bool found = false;
      for (size_t i = 0; i + 12 <= size; i++) {
        if (std::memcmp(base + i, marker, 12) == 0) {
          std::fprintf(stderr, "[selftest-perf] FOUND marker \"%s\" in %s at offset 0x%04zx\n", marker, label, i);
          found = true;
        }
      }
      return found;
    };
    bool foundMarker = false;
    foundMarker |= searchRegion("nvram", mcu->nvram, sizeof(mcu->nvram));
    foundMarker |= searchRegion("sram", mcu->sram, sizeof(mcu->sram));
    foundMarker |= searchRegion("ram", mcu->ram, sizeof(mcu->ram));
    foundMarker |= searchRegion("cardram", mcu->cardram, sizeof(mcu->cardram));
    if (!foundMarker)
      std::fprintf(stderr, "[selftest-perf] marker NOT found in nvram/sram/ram/cardram\n");

    // Look for Part 1's distinctive byte-21..30 run (receiveswitch, receivechannel, patch MSB/
    // LSB nibbles, level, pan, coarse/fine tune, reverbswitch, chorusswitch) to pin the exact
    // in-RAM stride between Common and Part 1, and each part's own size.
    const uint8_t part1Pattern[10] = { 1, 0, 0, 5, 0x7F, 0x40, 0, 0, 1, 1 };
    for (size_t i = 0; i + 10 <= sizeof(mcu->sram); i++) {
      if (std::memcmp(&mcu->sram[i], part1Pattern, 10) == 0)
        std::fprintf(stderr, "[selftest-perf] FOUND part1 byte21..30 pattern in sram at offset 0x%04zx (part base ~0x%04zx)\n", i, i - 21);
    }
    const uint8_t part2Pattern[10] = { 1, 1, (70 >> 4) & 0x7F, 70 & 0x0F, 100, 0x40, 0, 0, 1, 1 };
    for (size_t i = 0; i + 10 <= sizeof(mcu->sram); i++) {
      if (std::memcmp(&mcu->sram[i], part2Pattern, 10) == 0)
        std::fprintf(stderr, "[selftest-perf] FOUND part2 byte21..30 pattern in sram at offset 0x%04zx (part base ~0x%04zx)\n", i, i - 21);
    }

    std::fprintf(stderr, "[selftest-perf] nvram[0x11] (patch/perform mode byte) = %02x\n", mcu->nvram[0x11]);

    // Dump the firmware's own rendered LCD screen as a PNG - the strongest possible check that
    // Performance mode and our injected data actually took, since it's literally what the
    // firmware itself chose to display. No window/audio device needed: LCD_Update() just returns
    // its offscreen pixel buffer, same call LCDisplay::paint() makes from the message thread.
    if (auto *bitmapResult = (uint8_t *)mcu->lcd.LCD_Update()) {
      for (size_t i = 0; i < 1024 * 1024; i++) bitmapResult[i * 4 + 3] = 0xff;
      juce::Image image(juce::Image::PixelFormat::ARGB, 820, 100, false);
      juce::Image::BitmapData pixelMap(image, juce::Image::BitmapData::readWrite);
      for (int y = 0; y < pixelMap.height; y++)
        std::memcpy(pixelMap.getLinePointer(y), bitmapResult + (y * 1024 * 4), (size_t)pixelMap.lineStride);
      juce::PNGImageFormat png;
      juce::File outFile("/tmp/jv880_lcd_perf.png");
      juce::FileOutputStream stream(outFile);
      if (stream.openedOk()) {
        stream.setPosition(0);
        stream.truncate();
        png.writeImageToStream(image, stream);
        std::fprintf(stderr, "[selftest-perf] wrote LCD screenshot to /tmp/jv880_lcd_perf.png\n");
      }
    }

    std::fprintf(stderr, "[selftest-perf] done, exiting\n");
    std::exit(0);
  }

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
  refreshPerformanceBank();

  performanceParts[kNumPerformanceParts - 1].isRhythm = true;

  loaded = true;

  loadPerformanceSessionState();

  // Headless validation for the shipped Performance mode v2 API (sendPatchToPerformancePart(),
  // setPerformanceModeEnabled(), etc.) - as opposed to JV880_SELFTEST_PERF above, which only
  // exercised raw DT1 messages to find the address map in the first place. Same technique: drive
  // the emulator directly, no audio device or window needed, dump the firmware's own rendered
  // LCD screen as a PNG - see CLAUDE.md's "Mode Performance" section.
  if (std::getenv("JV880_SELFTEST_PERF2")) {
    const int sampleRate = 44100;
    const unsigned int blockFrames = 512;
    std::vector<float> l(blockFrames), r(blockFrames);

    auto runMs = [&](int ms) {
      int totalFrames = sampleRate * ms / 1000;
      int done = 0;
      while (done < totalFrames) {
        mcu->updateSC55WithSampleRate(l.data(), r.data(), blockFrames, sampleRate);
        done += (int)blockFrames;
      }
    };

    std::fprintf(stderr, "[selftest-perf2] booting 2000 ms...\n");
    runMs(2000);

    setPerformanceName("MyBand");
    sendPatchToPerformancePart(0, 0);   // Internal A #1 -> Part 1
    setPerformancePartParams(0, 1, 127, 64, true);
    sendPatchToPerformancePart(64, 1);  // Internal B #1 -> Part 2
    setPerformancePartParams(1, 2, 100, 64, true);
    sendPatchToPerformancePart(192, 7); // Rhythm Set Int A -> Part 8 (Rhythm)
    setPerformancePartParams(7, 10, 110, 64, true);

    std::fprintf(stderr, "[selftest-perf2] enabling Performance mode\n");
    setPerformanceModeEnabled(true);
    runMs(1500);

    // Audio check: note on channel 1 (Part 1) and channel 2 (Part 2) simultaneously - both
    // should be audible together through the single engine.
    {
      uint8_t note1On[3] = {0x90, 60, 100};
      uint8_t note2On[3] = {0x91, 64, 100};
      mcu->postMidiSC55(note1On, 3);
      mcu->postMidiSC55(note2On, 3);
      float peakL = 0, peakR = 0;
      int totalFrames = sampleRate;
      int done = 0;
      while (done < totalFrames) {
        mcu->updateSC55WithSampleRate(l.data(), r.data(), blockFrames, sampleRate);
        for (unsigned int i = 0; i < blockFrames; i++) {
          peakL = std::max(peakL, std::abs(l[i]));
          peakR = std::max(peakR, std::abs(r[i]));
        }
        done += (int)blockFrames;
      }
      std::fprintf(stderr, "[selftest-perf2] audio check (Part1 ch1 + Part2 ch2 together): peakL=%.4f peakR=%.4f\n", peakL, peakR);
    }

    if (auto *bitmapResult = (uint8_t *)mcu->lcd.LCD_Update()) {
      for (size_t i = 0; i < 1024 * 1024; i++) bitmapResult[i * 4 + 3] = 0xff;
      juce::Image image(juce::Image::PixelFormat::ARGB, 820, 100, false);
      juce::Image::BitmapData pixelMap(image, juce::Image::BitmapData::readWrite);
      for (int y = 0; y < pixelMap.height; y++)
        std::memcpy(pixelMap.getLinePointer(y), bitmapResult + (y * 1024 * 4), (size_t)pixelMap.lineStride);
      juce::PNGImageFormat png;
      juce::File outFile("/tmp/jv880_lcd_perf2.png");
      juce::FileOutputStream stream(outFile);
      if (stream.openedOk()) {
        stream.setPosition(0);
        stream.truncate();
        png.writeImageToStream(image, stream);
        std::fprintf(stderr, "[selftest-perf2] wrote LCD screenshot to /tmp/jv880_lcd_perf2.png\n");
      }
    }

    std::fprintf(stderr, "[selftest-perf2] disabling Performance mode\n");
    setPerformanceModeEnabled(false);
    std::fprintf(stderr, "[selftest-perf2] nvram[0x11] right after disable = %02x\n", mcu->nvram[0x11]);
    runMs(3000);
    std::fprintf(stderr, "[selftest-perf2] nvram[0x11] after 3s settle = %02x\n", mcu->nvram[0x11]);
    if (auto *bitmapResult = (uint8_t *)mcu->lcd.LCD_Update()) {
      for (size_t i = 0; i < 1024 * 1024; i++) bitmapResult[i * 4 + 3] = 0xff;
      juce::Image image(juce::Image::PixelFormat::ARGB, 820, 100, false);
      juce::Image::BitmapData pixelMap(image, juce::Image::BitmapData::readWrite);
      for (int y = 0; y < pixelMap.height; y++)
        std::memcpy(pixelMap.getLinePointer(y), bitmapResult + (y * 1024 * 4), (size_t)pixelMap.lineStride);
      juce::PNGImageFormat png;
      juce::File outFile("/tmp/jv880_lcd_backtopatch.png");
      juce::FileOutputStream stream(outFile);
      if (stream.openedOk()) {
        stream.setPosition(0);
        stream.truncate();
        png.writeImageToStream(image, stream);
        std::fprintf(stderr, "[selftest-perf2] wrote back-to-Patch LCD screenshot to /tmp/jv880_lcd_backtopatch.png\n");
      }
    }

    std::fprintf(stderr, "[selftest-perf2] done, exiting\n");
    std::exit(0);
  }

  // Headless investigation for unlocking expansion/User patches in Performance Parts (Alan's
  // request, 2026-09-08 - see CLAUDE.md's "Mode Performance v2" section). A Performance Part can
  // only reference the firmware's own Patch Memory by bank/number, never inline bytes - the 195
  // ROM-native patches already live there verbatim (see performancePatchMapping()), but
  // expansion/User patches only exist as raw bytes this project injects directly, so they'd need
  // to actually be WRITTEN into the real writable "Internal" Patch Memory bank first.
  // RolandJV880.java's own prepareBuffer() (the single-Patch editor, not Multi) gives the write
  // address for that bank: AA=1 (Internal), BB=number+0x40, CC=0x20, DD=0 for the Patch Common
  // record, CC+=(t+7) for each of the 4 Tones (t=1..4) - this locates where that lands in `nvram`
  // by writing just a 12-byte name marker (plain ASCII on the wire, no nibble encoding unlike
  // most other Patch fields) and searching for it, rather than assuming the wire layout matches
  // this project's own internal Patch struct (dataStructures.h) byte-for-byte.
  if (std::getenv("JV880_SELFTEST_PATCHMEM")) {
    const int sampleRate = 44100;
    const unsigned int blockFrames = 512;
    std::vector<float> l(blockFrames), r(blockFrames);

    auto runMs = [&](int ms) {
      int totalFrames = sampleRate * ms / 1000;
      int done = 0;
      while (done < totalFrames) {
        mcu->updateSC55WithSampleRate(l.data(), r.data(), blockFrames, sampleRate);
        done += (int)blockFrames;
      }
    };

    auto sendDT1 = [&](uint8_t AA, uint8_t BB, uint8_t CC, uint8_t DD, const std::vector<uint8_t> &payload) {
      std::vector<uint8_t> msg = {0xF0, 0x41, 0x10, 0x46, 0x12, AA, BB, CC, DD};
      uint32_t sum = AA + BB + CC + DD;
      for (auto b : payload) { msg.push_back(b); sum += b; }
      uint8_t checksum = (uint8_t)((0x80 - (sum & 0x7F)) & 0x7F);
      msg.push_back(checksum);
      msg.push_back(0xF7);
      mcu->postMidiSC55(msg.data(), (int)msg.size());
    };

    std::fprintf(stderr, "[selftest-patchmem] booting 2000 ms...\n");
    runMs(2000);

    // Slot 0 gets "PATCHMEMTEST", slot 1 gets "SECONDSLOT01" - two different markers so a
    // found stride between them confirms per-slot spacing, not just the base.
    std::fprintf(stderr, "[selftest-patchmem] writing name markers to Internal Patch Memory slots 0 and 1\n");
    std::vector<uint8_t> name0(12, ' ');
    { const char *n = "PATCHMEMTEST"; for (int i = 0; i < 12; i++) name0[(size_t)i] = (uint8_t)n[i]; }
    sendDT1(1, 0x40, 0x20, 0x00, name0); // AA=1 Internal, BB=0+0x40, CC=0x20 Patch Memory Common
    runMs(300);

    std::vector<uint8_t> name1(12, ' ');
    { const char *n = "SECONDSLOT01"; for (int i = 0; i < 12; i++) name1[(size_t)i] = (uint8_t)n[i]; }
    sendDT1(1, 0x41, 0x20, 0x00, name1); // slot 1
    runMs(2000);

    // Control: RolandJV880.java's prepareBuffer() targets AA=0,BB=8,CC=0x20,DD=0 for
    // "toWorkingMemory" (i.e. Patch Temp itself) - a DIFFERENT address than AA=1,BB=0x40 (Internal
    // Patch Memory slot 0) above. If this also lands at 0x0d70, slot 0 of Internal Patch Memory
    // and Patch Temp are genuinely the same NVRAM cells on this firmware; if it lands somewhere
    // else, the slot0 result above was something else entirely (e.g. the write silently fell
    // through to Temp regardless of AA/BB).
    std::vector<uint8_t> name2(12, ' ');
    { const char *n = "WORKINGMEM01"; for (int i = 0; i < 12; i++) name2[(size_t)i] = (uint8_t)n[i]; }
    sendDT1(0, 0x08, 0x20, 0x00, name2);
    runMs(2000);

    auto searchFor12 = [&](const char *label, const char *needle) {
      for (size_t i = 0; i + 12 <= sizeof(mcu->nvram); i++) {
        if (std::memcmp(&mcu->nvram[i], needle, 12) == 0)
          std::fprintf(stderr, "[selftest-patchmem] FOUND %s in nvram at offset 0x%04zx\n", label, i);
      }
      for (size_t i = 0; i + 12 <= sizeof(mcu->sram); i++) {
        if (std::memcmp(&mcu->sram[i], needle, 12) == 0)
          std::fprintf(stderr, "[selftest-patchmem] FOUND %s in sram at offset 0x%04zx\n", label, i);
      }
    };
    searchFor12("slot0 marker (PATCHMEMTEST)", "PATCHMEMTEST");
    searchFor12("slot1 marker (SECONDSLOT01)", "SECONDSLOT01");
    searchFor12("working-memory marker (WORKINGMEM01)", "WORKINGMEM01");

    // End-to-end check of the real shipped API: assign the first expansion-ROM patch found
    // (patchInfoPerGroup[1], if any expansion is installed), else fall back to a freshly-saved
    // User patch (saveCurrentPatchAs() on whatever's currently loaded - this sandbox has no
    // expansion ROM installed), to Performance Part 3, enable Performance mode, and confirm it
    // actually plays.
    int expansionPatchIndex = -1;
    if (patchInfoPerGroup.size() > 1 && !patchInfoPerGroup[1].empty()) {
      expansionPatchIndex = patchInfoPerGroup[1][0]->iInList;
    } else {
      std::fprintf(stderr, "[selftest-patchmem] no expansion ROM installed, falling back to a User patch\n");
      setCurrentProgram(0); // status.patch is all-zero (silent) until a real patch is loaded once
      auto file = userPatchesDir().getChildFile("SelfTestUser.jvp");
      if (saveCurrentPatchAs(file) && !patchInfoPerGroup[userGroupIndex].empty())
        expansionPatchIndex = patchInfoPerGroup[userGroupIndex].back()->iInList;
    }

    if (expansionPatchIndex >= 0) {
      std::fprintf(stderr, "[selftest-patchmem] assigning patch \"%.*s\" (index %d) to Part 3\n",
                   patchInfos[expansionPatchIndex].nameLength, patchInfos[expansionPatchIndex].name,
                   expansionPatchIndex);
      sendPatchToPerformancePart(expansionPatchIndex, 2);
      setPerformancePartParams(2, 3, 127, 64, true);
      setPerformanceModeEnabled(true);
      runMs(3000);

      uint8_t noteOn[3] = {0x92, 60, 100};
      mcu->postMidiSC55(noteOn, 3);
      float peakL = 0, peakR = 0;
      int totalFrames = sampleRate;
      int done = 0;
      while (done < totalFrames) {
        mcu->updateSC55WithSampleRate(l.data(), r.data(), blockFrames, sampleRate);
        for (unsigned int i = 0; i < blockFrames; i++) {
          peakL = std::max(peakL, std::abs(l[i]));
          peakR = std::max(peakR, std::abs(r[i]));
        }
        done += (int)blockFrames;
      }
      std::fprintf(stderr, "[selftest-patchmem] audio check (expansion patch on Part 3, ch3): peakL=%.4f peakR=%.4f\n", peakL, peakR);

      if (auto *bitmapResult = (uint8_t *)mcu->lcd.LCD_Update()) {
        for (size_t i = 0; i < 1024 * 1024; i++) bitmapResult[i * 4 + 3] = 0xff;
        juce::Image image(juce::Image::PixelFormat::ARGB, 820, 100, false);
        juce::Image::BitmapData pixelMap(image, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < pixelMap.height; y++)
          std::memcpy(pixelMap.getLinePointer(y), bitmapResult + (y * 1024 * 4), (size_t)pixelMap.lineStride);
        juce::PNGImageFormat png;
        juce::File outFile("/tmp/jv880_lcd_expansion.png");
        juce::FileOutputStream stream(outFile);
        if (stream.openedOk()) {
          stream.setPosition(0);
          stream.truncate();
          png.writeImageToStream(image, stream);
          std::fprintf(stderr, "[selftest-patchmem] wrote LCD screenshot to /tmp/jv880_lcd_expansion.png\n");
        }
      }
    } else {
      std::fprintf(stderr, "[selftest-patchmem] no patch available for the end-to-end check, skipping\n");
    }

    std::fprintf(stderr, "[selftest-patchmem] done, exiting\n");
    std::exit(0);
  }

  return true;
}

juce::File VirtualJVProcessor::getRomsFolder() {
  return juce::File(getEffectiveRomsDirectory());
}

void VirtualJVProcessor::setRomsFolderOverride(const juce::File &dir) {
  setRomsDirectoryOverride(dir == juce::File{} ? std::string() : dir.getFullPathName().toStdString());
  savePersistedRomFolderOverride(dir);

  // Only meaningful to actually re-point the live engine at while it's still unloaded - see
  // attemptLoadRoms()'s own comment for why switching ROM sets under an already-running engine
  // isn't supported. Once loaded, this has already done its job (persisted + updated rom.cpp's
  // override for next launch); nothing here to unwind.
  if (loaded)
    return;

  for (auto &ptr : loadedRoms) {
    if (ptr != nullptr) {
      free(ptr);
      ptr = nullptr;
    }
  }
  for (auto &info : romInfos)
    info.loaded = false;
}

bool VirtualJVProcessor::retryLoadRoms() {
  if (loaded)
    return true;

  if (!attemptLoadRoms())
    return false;

  if (auto editor = getActiveEditor())
    if (auto e = dynamic_cast<VirtualJVEditor *>(editor))
      e->romsBecameAvailable();

  return true;
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

namespace {
// See PerformancePart's own comment (PluginProcessor.h) for what this covers and why: only the
// 195 ROM-native factory tones/rhythm-sets (patchInfos[] index 0..194) map onto a real Patch
// Memory bank/number the firmware can actually reference from a Performance Part. Confirmed
// empirically (LCD screenshot + RAM search naming the right factory patch - see CLAUDE.md):
// patchInfos[] 0..63 ("Internal A" in this project's own Browse-tab naming) IS the firmware's
// real read-only Preset A bank; 64..127 ("Internal B") is Preset B; 128..191 ("Internal User",
// despite the name - it's ROM-sourced like the other two) is the real writable Internal bank's
// factory-default content; 192/193/194 are the matching Rhythm Set for each of those 3 banks.
bool performancePatchMapping(int patchInfoIndex, uint8_t &bank, uint8_t &number, bool &isRhythm) {
  if (patchInfoIndex >= 0 && patchInfoIndex < 192) {
    const int group = patchInfoIndex / 64; // 0 = Internal A/Preset A, 1 = Internal B/Preset B, 2 = Internal User/real Internal
    bank = group == 0 ? 2 : group == 1 ? 3 : 0;
    number = (uint8_t)(patchInfoIndex % 64);
    isRhythm = false;
    return true;
  }
  if (patchInfoIndex == 192) { bank = 2; number = 0; isRhythm = true; return true; }
  if (patchInfoIndex == 193) { bank = 3; number = 0; isRhythm = true; return true; }
  if (patchInfoIndex == 194) { bank = 0; number = 0; isRhythm = true; return true; }
  return false;
}

constexpr size_t kPerfPartNameBytes = sizeof(VirtualJVProcessor::PerformancePart{}.name);
// present(1) + isRhythm(1) + bank(1) + number(1) + name + midiChannel(1) + level(1) + pan(1) +
// enabled(1) + expansionI(1)
constexpr size_t kPerfPartRecordBytes = 4 + kPerfPartNameBytes + 5;
constexpr size_t kPerfNameBytes = 12; // matches the firmware's own Common name field width

// Shared by the "Save As..." bank format and the session-restore file - both serialize the same
// performance name + 8 PerformanceParts, just under different filenames/directories (see
// performancesDir() vs performanceSessionFile()).
void appendPerformanceParts(
    juce::MemoryBlock &block, const char (&name)[13],
    const VirtualJVProcessor::PerformancePart (&parts)[VirtualJVProcessor::kNumPerformanceParts]) {
  block.append(name, kPerfNameBytes);

  for (auto &part : parts) {
    uint8_t present = part.present ? 1 : 0;
    block.append(&present, 1);
    uint8_t isRhythm = part.isRhythm ? 1 : 0;
    block.append(&isRhythm, 1);
    block.append(&part.bank, 1);
    block.append(&part.number, 1);

    char nameBuf[kPerfPartNameBytes] = {0};
    memcpy(nameBuf, part.name, sizeof(nameBuf));
    block.append(nameBuf, sizeof(nameBuf));

    uint8_t ch = (uint8_t)part.midiChannel;
    block.append(&ch, 1);
    uint8_t lvl = (uint8_t)part.level;
    block.append(&lvl, 1);
    uint8_t pan = (uint8_t)part.pan;
    block.append(&pan, 1);
    uint8_t en = part.enabled ? 1 : 0;
    block.append(&en, 1);
    block.append(&part.expansionI, 1);
  }
}

// Returns false (leaving `name`/`parts` untouched) if `size` doesn't match exactly - a file this
// build didn't write, or written by a since-changed format.
bool readPerformanceParts(
    const uint8_t *bytes, size_t size, char (&name)[13],
    VirtualJVProcessor::PerformancePart (&parts)[VirtualJVProcessor::kNumPerformanceParts]) {
  if (size != kPerfNameBytes + VirtualJVProcessor::kNumPerformanceParts * kPerfPartRecordBytes)
    return false;

  memcpy(name, bytes, kPerfNameBytes);
  name[kPerfNameBytes] = '\0';
  size_t offset = kPerfNameBytes;

  for (int i = 0; i < VirtualJVProcessor::kNumPerformanceParts; i++) {
    auto &part = parts[i];

    uint8_t present = bytes[offset]; offset += 1;
    uint8_t isRhythm = bytes[offset]; offset += 1;
    uint8_t bank = bytes[offset]; offset += 1;
    uint8_t number = bytes[offset]; offset += 1;
    const char *nameBytes = (const char *)(bytes + offset); offset += kPerfPartNameBytes;
    uint8_t ch = bytes[offset]; offset += 1;
    uint8_t lvl = bytes[offset]; offset += 1;
    uint8_t pan = bytes[offset]; offset += 1;
    uint8_t en = bytes[offset]; offset += 1;
    uint8_t expansionI = bytes[offset]; offset += 1;

    part.present = present != 0;
    part.isRhythm = isRhythm != 0;
    part.bank = bank;
    part.number = number;
    memcpy(part.name, nameBytes, sizeof(part.name));
    part.name[sizeof(part.name) - 1] = '\0'; // defensive against a corrupt/foreign file
    part.midiChannel = ch;
    part.level = lvl;
    part.pan = pan;
    part.enabled = en != 0;
    part.expansionI = expansionI;
  }
  return true;
}
} // namespace

bool VirtualJVProcessor::isEligibleForPerformancePart(int patchInfoIndex) const {
  if (patchInfoIndex < 0 || patchInfoIndex >= romPatchCapacity + userPatchCapacity
      || !patchInfos[patchInfoIndex].present)
    return false;

  uint8_t bank, number;
  bool isRhythm;
  if (performancePatchMapping(patchInfoIndex, bank, number, isRhythm))
    return true; // ROM-native - already lives in real Patch Memory verbatim

  // Expansion-ROM/User: eligible for tone Parts only (injectCustomPatchIntoInternalMemory()) -
  // rhythm sets from those banks aren't supported yet, see CLAUDE.md.
  return !patchInfos[patchInfoIndex].drums;
}

// See this function's own comment in PluginProcessor.h. Caller must already hold mcuLock.
void VirtualJVProcessor::injectCustomPatchIntoInternalMemory(int patchInfoIndex, int internalSlot) {
  auto &info = patchInfos[patchInfoIndex];
  if (info.drums)
    return;

  memcpy(&mcu->nvram[0x0d70 + internalSlot * 0x16a], info.name, 0x16a);

  // Only one waverom_exp can be loaded into this single engine at a time - matches setCurrent
  // Program()'s own handling for Patch mode, and the real hardware's own single wave-expansion-
  // slot limit. A Performance mixing Parts from two *different* expansion boards will have
  // whichever was injected last sound right and the other(s) not - same real-hardware
  // constraint, not something this project's single-engine design adds on top.
  if (info.expansionI != 0xff && info.expansionI < NUM_EXPS
      && expansionsDescr[info.expansionI] != nullptr) {
    memcpy(mcu->pcm.waverom_exp, expansionsDescr[info.expansionI], 0x800000);
  }
}

// Pushes the Performance Common record (name + effects + voice reserve) to the single `mcu`
// engine over SysEx DT1, address 00 00 10 00 ("Temporary Performance", common). Caller must
// already hold mcuLock. Effects/voice-reserve fields are left at 0 for now (confirmed by an
// audio render test, see CLAUDE.md, that an unreserved Part still plays - voice reserve only
// affects priority under voice contention) - a future UI pass can expose them.
void VirtualJVProcessor::pushPerformanceCommonToEngine() {
  std::vector<uint8_t> common(31, 0);
  memcpy(common.data(), performanceName, kPerfNameBytes);
  sendSysexBlock(0x10u << 7, common.data(), common.size());
}

// Pushes one Part's 35-byte record to the single `mcu` engine over SysEx DT1, address
// 00 00 1n 00 where n = 8+partIndex (see the partOffsets table, matching Edisyn's own
// RolandJV880MultiRec.partOffsets). Caller must already hold mcuLock.
void VirtualJVProcessor::pushPerformancePartToEngine(int partIndex) {
  if (partIndex < 0 || partIndex >= kNumPerformanceParts)
    return;

  auto &part = performanceParts[partIndex];

  std::vector<uint8_t> data(35, 0);
  // Indices below match RolandJV880Multi.java's own allToneParameters ordering exactly - see
  // PerformancePart's comment in PluginProcessor.h for the full field list.
  data[21] = (part.present && part.enabled) ? 1 : 0; // receiveswitch
  data[22] = (uint8_t)juce::jlimit(0, 15, part.midiChannel - 1); // receivechannel, 0-indexed
  const uint8_t patchValue = part.present ? (uint8_t)(part.bank * 64 + part.number) : 0;
  data[23] = (patchValue >> 4) & 0x7F; // patchnumber MSB nibble
  data[24] = patchValue & 0x0F;        // patchnumber LSB nibble
  data[25] = (uint8_t)juce::jlimit(0, 127, part.level); // partlevel
  data[26] = (uint8_t)juce::jlimit(0, 127, part.pan);   // partpan
  data[27] = 64; // partcoarsetune, centre
  data[28] = 64; // partfinetune, centre
  data[29] = 1;  // reverbswitch - onto the Performance's shared reverb bus
  data[30] = 1;  // chorusswitch - ditto

  static constexpr uint8_t partOffsets[VirtualJVProcessor::kNumPerformanceParts] = {
      0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
  const uint32_t address = (uint32_t)(0x10 + partOffsets[partIndex]) << 7;
  sendSysexBlock(address, data.data(), data.size());
}

void VirtualJVProcessor::sendPatchToPerformancePart(int patchInfoIndex, int partIndex) {
  if (!loaded || partIndex < 0 || partIndex >= kNumPerformanceParts)
    return;
  if (patchInfoIndex < 0 || patchInfoIndex >= romPatchCapacity + userPatchCapacity
      || !patchInfos[patchInfoIndex].present)
    return;

  auto &info = patchInfos[patchInfoIndex];

  // Part 8 is fixed to Rhythm on real hardware, Parts 1-7 fixed to tone - reject a mismatch
  // rather than silently mis-configuring the Part.
  const bool wantsRhythmPart = (partIndex == kNumPerformanceParts - 1);
  if (info.drums != wantsRhythmPart)
    return;

  uint8_t bank, number;
  bool isRhythm;
  const bool isRomNative = performancePatchMapping(patchInfoIndex, bank, number, isRhythm);

  mcuLock.enter();

  if (!isRomNative) {
    if (info.drums) {
      // Rhythm set from an expansion/User bank - not addressable yet, see CLAUDE.md.
      mcuLock.exit();
      return;
    }
    // Expansion-ROM/User tone patch: inject it into this Part's own reserved Internal Patch
    // Memory slot (1..7 - slot 0 is Patch mode's own live buffer, never touched here).
    bank = 0;
    number = (uint8_t)(partIndex + 1);
    injectCustomPatchIntoInternalMemory(patchInfoIndex, partIndex + 1);
  }

  auto &part = performanceParts[partIndex];

  part.present = true;
  part.isRhythm = info.drums;
  part.bank = bank;
  part.number = number;
  part.expansionI = (uint8_t)info.expansionI;

  const int n = std::min(info.nameLength, (int)sizeof(part.name) - 1);
  memcpy(part.name, info.name, (size_t)n);
  part.name[n] = '\0';

  if (performanceModeEnabled)
    pushPerformancePartToEngine(partIndex);
  mcuLock.exit();

  // Non-blocking heads-up (Alan's request, 2026-09-08), not a rejection - see PerformancePart::
  // expansionI's own comment in PluginProcessor.h for why mixing boards is a real single-engine/
  // real-hardware limitation rather than something worth preventing outright.
  bool mixedExpansionWarning = false;
  if (part.expansionI != 0xff) {
    for (int i = 0; i < kNumPerformanceParts; ++i) {
      if (i == partIndex)
        continue;
      auto &other = performanceParts[i];
      if (other.present && other.expansionI != 0xff && other.expansionI != part.expansionI) {
        mixedExpansionWarning = true;
        break;
      }
    }
  }

  savePerformanceSessionState();

  if (auto editor = getActiveEditor())
    if (auto e = dynamic_cast<VirtualJVEditor *>(editor))
    {
      e->updatePerformanceTab();
      if (mixedExpansionWarning)
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, "Mixed expansion boards",
            "This Performance now combines Parts from more than one expansion ROM. Only one "
            "expansion board can be loaded into the engine at a time, so only the most recently "
            "assigned board's Part(s) will sound correct - the others will play whatever "
            "expansion is currently loaded instead.",
            "OK", e);
    }
}

void VirtualJVProcessor::clearPerformancePart(int partIndex) {
  if (partIndex < 0 || partIndex >= kNumPerformanceParts)
    return;

  performanceParts[partIndex] = PerformancePart();
  performanceParts[partIndex].isRhythm = (partIndex == kNumPerformanceParts - 1);

  mcuLock.enter();
  if (performanceModeEnabled)
    pushPerformancePartToEngine(partIndex);
  mcuLock.exit();

  savePerformanceSessionState();

  if (auto editor = getActiveEditor())
    if (auto e = dynamic_cast<VirtualJVEditor *>(editor))
      e->updatePerformanceTab();
}

void VirtualJVProcessor::setPerformancePartParams(int partIndex, int midiChannel, int level,
                                                   int pan, bool enabled) {
  if (partIndex < 0 || partIndex >= kNumPerformanceParts)
    return;

  auto &part = performanceParts[partIndex];
  part.midiChannel = juce::jlimit(1, 16, midiChannel);
  part.level = juce::jlimit(0, 127, level);
  part.pan = juce::jlimit(0, 127, pan);
  part.enabled = enabled;

  mcuLock.enter();
  if (performanceModeEnabled)
    pushPerformancePartToEngine(partIndex);
  mcuLock.exit();

  savePerformanceSessionState();
}

void VirtualJVProcessor::setPerformanceName(const juce::String &name) {
  auto padded = name + "            ";
  for (int i = 0; i < 12; i++) {
    auto c = padded[i];
    performanceName[i] = (char)((c < 32 || c > 127) ? ' ' : (char)c);
  }
  performanceName[12] = '\0';

  mcuLock.enter();
  if (performanceModeEnabled)
    pushPerformanceCommonToEngine();
  mcuLock.exit();

  savePerformanceSessionState();
}

void VirtualJVProcessor::setPerformanceModeEnabled(bool enabled) {
  if (!loaded)
    return;

  mcuLock.enter();

  // System Area offset 0 = the real PATCH/PERFORM front-panel button's own state (0 =
  // Performance, 1 = Patch) - confirmed empirically (LCD screenshot + nvram[0x11] flipping
  // exactly as a simulated physical button press does - see CLAUDE.md). Flipping it over SysEx
  // does exactly what pressing the button does, on the SAME single `mcu` engine Patch mode
  // already uses - no parallel engines, no extra CPU cost versus Patch mode regardless of how
  // many of the 8 Parts are active.
  uint8_t modeByte = enabled ? 0 : 1;
  sendSysexBlock(0, &modeByte, 1);

  if (enabled) {
    pushPerformanceCommonToEngine();
    for (int i = 0; i < kNumPerformanceParts; ++i)
      pushPerformancePartToEngine(i);
  }

  performanceModeEnabled = enabled;

  mcuLock.exit();

  savePerformanceSessionState();

  if (auto editor = getActiveEditor())
    if (auto e = dynamic_cast<VirtualJVEditor *>(editor))
      e->updatePerformanceTab();
}

juce::File VirtualJVProcessor::performancesDir() {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
      .getChildFile("JV880")
      .getChildFile("Performances");
}

bool VirtualJVProcessor::savePerformanceAs(const juce::File &file) {
  juce::MemoryBlock block;
  appendPerformanceParts(block, performanceName, performanceParts);

  file.getParentDirectory().createDirectory();
  if (!file.replaceWithData(block.getData(), block.getSize()))
    return false;

  refreshPerformanceBank();
  return true;
}

// (Re)scans performancesDir() and rebuilds performanceBank - same "full rescan, no index file"
// approach as refreshUserPatches(), called at startup and after every savePerformanceAs().
void VirtualJVProcessor::refreshPerformanceBank() {
  performanceBank.clear();

  auto dir = performancesDir();
  dir.createDirectory();

  auto files = dir.findChildFiles(juce::File::findFiles, false, "*.jvpf");
  files.sort();

  for (auto &file : files) {
    if (file.getSize() != (int64_t)(kPerfNameBytes + kNumPerformanceParts * kPerfPartRecordBytes))
      continue; // not a file this build wrote (wrong size) - skip rather than risk misreading it

    performanceBank.push_back({file.getFileNameWithoutExtension(), file});
  }
}

void VirtualJVProcessor::loadPerformance(int bankIndex) {
  if (bankIndex < 0 || bankIndex >= (int)performanceBank.size())
    return;

  juce::MemoryBlock block;
  if (!performanceBank[bankIndex].file.loadFileAsData(block))
    return;

  if (!readPerformanceParts(static_cast<const uint8_t *>(block.getData()), block.getSize(),
                             performanceName, performanceParts))
    return;

  mcuLock.enter();
  if (performanceModeEnabled) {
    pushPerformanceCommonToEngine();
    for (int i = 0; i < kNumPerformanceParts; ++i)
      pushPerformancePartToEngine(i);
  }
  mcuLock.exit();

  savePerformanceSessionState();

  if (auto editor = getActiveEditor())
    if (auto e = dynamic_cast<VirtualJVEditor *>(editor))
      e->updatePerformanceTab();
}

juce::File VirtualJVProcessor::performanceSessionFile() {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
      .getChildFile("JV880")
      .getChildFile("performance_session.dat");
}

void VirtualJVProcessor::savePerformanceSessionState() {
  juce::MemoryBlock block;
  uint8_t modeByte = performanceModeEnabled ? 1 : 0;
  block.append(&modeByte, 1);
  appendPerformanceParts(block, performanceName, performanceParts);

  auto file = performanceSessionFile();
  file.getParentDirectory().createDirectory();
  file.replaceWithData(block.getData(), block.getSize());
}

// Called once at construction, after refreshPerformanceBank() - restores the 8 in-progress
// parts and whether Performance mode was on, so closing and reopening the plugin/app picks up
// right where it left off (Alan's request, 2026-09-07). Deliberately separate from the Bank
// (performancesDir()) - this file is never listed there.
void VirtualJVProcessor::loadPerformanceSessionState() {
  auto file = performanceSessionFile();
  if (!file.existsAsFile())
    return;

  juce::MemoryBlock block;
  if (!file.loadFileAsData(block) || block.getSize() < 1)
    return;

  const auto *bytes = static_cast<const uint8_t *>(block.getData());
  const bool wantEnabled = bytes[0] != 0;

  if (!readPerformanceParts(bytes + 1, block.getSize() - 1, performanceName, performanceParts))
    return;

  if (wantEnabled)
    setPerformanceModeEnabled(true); // pushes Common + all 8 Parts to the engine from the state just read
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
                                             int samplesPerBlock) {
  keyboardCollector.reset(sampleRate);

  dspLoadMeasurer.reset(sampleRate, samplesPerBlock);
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
  // Spans the whole block (MIDI handling + the actual DSP below) - Patch mode and Performance
  // mode alike now share the same single `mcu` engine (Performance mode v2 - see
  // PerformancePart's own comment in PluginProcessor.h), so this is a flat cost either way, same
  // convention as any other JUCE plugin's CPU meter. Read from SettingsTab via
  // dspLoadMeasurer.getLoadAsPercentage().
  juce::AudioProcessLoadMeasurer::ScopedTimer loadTimer(dspLoadMeasurer, buffer.getNumSamples());

  // The on-screen VirtualKeyboard's notes, queued by injectTestNote() - merged in exactly the
  // same way a real MIDI IN port's messages would be.
  keyboardCollector.removeNextBlockOfMessages(midiMessages, buffer.getNumSamples());

  mcuLock.enter();

  // Tracked once regardless of Patch/Performance mode - same value either way, so there's no
  // point recomputing it once per active performance slot below.
  for (const auto metadata : midiMessages)
  {
    auto message = metadata.getMessage();
    if (message.isNoteOnOrOff())
    {
      int note = message.getNoteNumber();
      if (note >= 0 && note < 128)
        noteActiveTable[static_cast<size_t>(note)].store(message.isNoteOn());
    }
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

  const int numSamples = buffer.getNumSamples();

  for (const auto metadata : midiMessages)
  {
    auto message = metadata.getMessage();

    // Patch mode always answers on a single fixed channel (matching the current Patch/Rhythm
    // Temp's own RxCH), same as before. Performance mode forwards each message's real incoming
    // channel unchanged - the firmware's own 8 Parts (configured over SysEx by
    // pushPerformancePartToEngine()) each answer their own receivechannel, exactly like a real
    // JV-880's Performance Play mode routes a multitimbral MIDI cable itself.
    if (!performanceModeEnabled)
      message.setChannel(status.isDrums ? 10 : 1);

    int samplePos = int(((double)metadata.samplePosition / getSampleRate()) * 64000.0);

    mcu->enqueueMidiSC55(message.getRawData(), message.getRawDataSize(), samplePos);
  }

  float *channelDataL = buffer.getWritePointer(0);
  float *channelDataR = buffer.getWritePointer(1);

  mcu->updateSC55WithSampleRate(channelDataL, channelDataR, numSamples, (int)getSampleRate());

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

// Roland DT1 ("Data Set 1") send: F0 41 10 46 12 <4x7bit address, MSB first> <data...>
// <checksum> F7, checksum = (0x80 - (sum(address bytes + data bytes) & 0x7F)) & 0x7F. Same
// command real hardware/librarians (this project used Sean Luke's Edisyn, see CLAUDE.md, to
// transcribe the JV-880's own address map) use to write directly into the firmware's temporary
// or stored memory over MIDI IN - the firmware's own SysEx receiver resolves the address, so the
// caller never needs to know where a given parameter actually lives in RAM. Caller must already
// hold mcuLock.
void VirtualJVProcessor::sendSysexBlock(uint32_t address, const uint8_t *data, size_t length) {
  uint8_t addrBytes[4] = {
      (uint8_t)((address >> 21) & 127),
      (uint8_t)((address >> 14) & 127),
      (uint8_t)((address >> 7) & 127),
      (uint8_t)((address >> 0) & 127),
  };

  uint32_t checksum = 0;
  for (uint8_t b : addrBytes) checksum += b;
  for (size_t i = 0; i < length; i++) checksum += data[i];
  checksum = (0x80 - (checksum & 0x7f)) & 0x7f;

  std::vector<uint8_t> buf;
  buf.reserve(11 + length);
  buf.push_back(0xf0);
  buf.push_back(0x41);
  buf.push_back(0x10); // unit number
  buf.push_back(0x46); // JV-880 model ID
  buf.push_back(0x12); // command: DT1
  for (uint8_t b : addrBytes) buf.push_back(b);
  for (size_t i = 0; i < length; i++) buf.push_back(data[i]);
  buf.push_back((uint8_t)checksum);
  buf.push_back(0xf7);

  mcu->postMidiSC55(buf.data(), (int)buf.size());
}

void VirtualJVProcessor::sendSysexParamChange(uint32_t address, uint8_t value) {
  mcuLock.enter();
  sendSysexBlock(address, &value, 1);
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
