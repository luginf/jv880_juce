/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <array>
#include <atomic>
#include <map>
#include <vector>
#include <JuceHeader.h>
#include "emulator/mcu.h"
#include "rom.h"
#include "ui/widgets/VirtualKeyboardHost.h"

constexpr int NUM_EXPS = romCount - 6;

// patchInfos[]/patchInfoPerGroup capacity for ROM-sourced patches, and a reserved tail for
// user-saved ones (Alan's request, 2026-09-04: Save As + a single flat "User" bank). Both are
// plain arrays/reserved vectors sized once at compile time rather than fully dynamic
// containers, matching how the ROM side of this same array already works - see
// VirtualJVProcessor::refreshUserPatches() for how that tail gets populated from disk.
constexpr int romPatchCapacity = 192 + 256 * NUM_EXPS;
constexpr int userPatchCapacity = 256;
// patchInfoPerGroup's last group, appended once in the constructor right after the ROM-based
// ones (which occupy indices 0..NUM_EXPS - see the constructor's own loop).
constexpr int userGroupIndex = NUM_EXPS + 1;

//==============================================================================
/**
*/

class VirtualJVEditor;

class VirtualJVProcessor  : public juce::AudioProcessor, public VirtualKeyboardHost
{
public:
    //==============================================================================
    VirtualJVProcessor();
    ~VirtualJVProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    void sendSysexParamChange(uint32_t address, uint8_t value);

    std::vector<std::string> readMultisampleNames(uint8_t romIdx);

    // Discards edits made to the currently-loaded patch since it was picked, restoring it to
    // the snapshot taken when setCurrentProgram() last loaded it from ROM.
    void revertCurrentPatch();

    // True if patchInfos[index] differs from its own pristine ROM/file source - Alan's
    // request, so PatchBrowser can colour modified patches differently in the list. Computed
    // on demand (a bounded memcmp, at most 2684 bytes) rather than tracked with a side flag
    // that every edit call site would need to remember to set: for the currently-loaded patch,
    // compares the live edit buffer against originalPatchSnapshot/originalDrumsSnapshot; for
    // any other patch, compares its entry in editedPatchCache/editedDrumsCache (if any) against
    // patchInfos[index]'s own pristine bytes - both already exist for revertCurrentPatch() and
    // the switch-away-and-back cache respectively, so this adds no new state.
    bool isPatchModified(int index) const;

    //==============================================================================
    // User patch library (Alan's request, 2026-09-04): one flat "User" bank, one file per
    // patch, so a modified patch/rhythm-set can be saved under a new name instead of only
    // living in the current DAW project/session. `file` should be under userPatchesDir() with
    // a ".jvp" extension - PatchBrowser's Save As... button uses a juce::FileChooser in save
    // mode to pick both the name and (implicitly) that. Returns false if the write failed.
    bool saveCurrentPatchAs(const juce::File &file);
    static juce::File userPatchesDir();
    int numUserPatches = 0; // how many of the reserved patchInfos[] tail slots are populated

    //==============================================================================
    // VirtualKeyboardHost - feeds the on-screen VirtualKeyboard through the same
    // MidiMessageCollector path a real MIDI IN port would use, merged into the host's own
    // MIDI in processBlock(). Most of this is session-only (not part of DataToSave/getState
    // Information: that struct is a fixed-size raw memcpy blob loaded back with memcpy(&status,
    // data, sizeof(DataToSave)) regardless of the actual saved size, so appending fields to it
    // would read past the end of an older, smaller saved blob). The PC-keyboard-input toggle and
    // its QWERTY/AZERTY layout are the exception (Alan's request) - persisted to a small XML
    // file of their own under the same JV880 app-data folder as the ROMs, sidestepping that
    // blob entirely - see loadPersistedKeyboardSettings()/savePersistedKeyboardSettings() in the
    // .cpp.
    void injectTestNote(int channel, int note, float velocity, bool on) override;
    int getKeyboardMidiChannel() const override { return keyboardMidiChannel; }
    void setKeyboardMidiChannel(int channel) override { keyboardMidiChannel = juce::jlimit(1, 16, channel); }
    bool getMidiRemap() const override { return keyboardMidiRemap; }
    void setMidiRemap(bool remap) override { keyboardMidiRemap = remap; }
    bool getKeyboardPcInputEnabled() const override { return keyboardPcInput; }
    void setKeyboardPcInputEnabled(bool enabled) override;
    int getKeyboardPcLayout() const override { return keyboardPcLayout; }
    void setKeyboardPcLayout(int layout) override;
    int getKeyboardNumOctaves() const override { return keyboardNumOctaves; }
    void setKeyboardNumOctaves(int numOctaves) override { keyboardNumOctaves = juce::jlimit(1, 4, numOctaves); }
    bool isNoteActive(int note) const override {
        return note >= 0 && note < 128 && noteActiveTable[static_cast<size_t>(note)].load();
    }

    //==============================================================================
    // Plugin-side output trim (Settings tab) - see SettingsTab.h's own comment on why this
    // isn't a real JV-880 parameter and isn't part of DataToSave.
    float getMasterVolume() const { return masterVolume; }
    void setMasterVolume(float v) { masterVolume = juce::jlimit(0.0f, 1.0f, v); }

    struct PatchInfo
    {
        const char* name;
        const char* ptr;
        int nameLength;
        int expansionI; // 0xff: no expansion
        int patchI;
        bool present = false;
        bool drums = false;
        int iInList;
    };

    struct DataToSave
    {
        int8_t masterTune{0};
        bool reverbEnabled{1};
        bool chorusEnabled{1};

        int currentExpansion{0};
        bool isDrums{false};

        uint8_t patch[0x16a] = {0};
        uint8_t drums[0xa7c] = {0};

        int selectedTab{0};
        int selectedRom{-1};
        int selectedPatch{-1};

        uint8_t selectedLCDColor{0};
    };

    DataToSave status;
    MCU *mcu;
    // Zero-initialised so the nullptr checks in setStateInformation()/setCurrentProgram() (an
    // expansion index this build doesn't have that ROM loaded for - a saved DAW project on a
    // different machine, or a user patch saved while a since-removed expansion was active) see
    // a reliable nullptr rather than an indeterminate pointer for the slots the constructor's
    // own loading loop never assigns.
    const uint8_t* expansionsDescr[NUM_EXPS] = {nullptr};
    PatchInfo patchInfos[romPatchCapacity + userPatchCapacity] = {0};
    std::vector<std::vector<PatchInfo*>> patchInfoPerGroup;
    int totalPatchesExp = 0;

    std::array<uint8_t*, romCount> loadedRoms = {0};
    std::vector<std::string> ownedNames;
    bool loaded = false;

    juce::SpinLock mcuLock;

private:
    // VirtualKeyboard support - see the VirtualKeyboardHost overrides above.
    juce::MidiMessageCollector keyboardCollector;
    std::array<std::atomic<bool>, 128> noteActiveTable{};
    int keyboardMidiChannel = 1;
    bool keyboardMidiRemap = false;
    bool keyboardPcInput = false;
    int keyboardPcLayout = 0;
    int keyboardNumOctaves = 2;
    float masterVolume = 1.0f;

    // revertCurrentPatch()'s own pristine copy - see setCurrentProgram()'s and
    // revertCurrentPatch()'s own comments.
    uint8_t originalPatchSnapshot[0x16a] = {0};
    uint8_t originalDrumsSnapshot[0xa7c] = {0};

    // In-session edit cache, keyed by patchInfos[] index (PatchInfo::iInList) - Alan's report,
    // 2026-09-04: picking a different patch and coming back silently discarded whatever had
    // just been edited, reloading the pristine ROM/file version instead. setCurrentProgram()
    // now saves the outgoing patch's live bytes here before loading the new one, and checks
    // here first (falling back to the pristine source) when loading any patch. Deliberately
    // NOT part of DataToSave/persisted to disk - this is "don't lose my place while I'm
    // comparing patches this session", not a substitute for Save As (which is explicit and
    // permanent) - so it starts empty every relaunch.
    int currentPatchIndex = -1;
    std::map<int, std::array<uint8_t, 0x16a>> editedPatchCache;
    std::map<int, std::array<uint8_t, 0xa7c>> editedDrumsCache;

    // Backing storage for the user patches refreshUserPatches() loads from disk - reserved
    // upfront to userPatchCapacity so the vectors never reallocate while patchInfos[] entries
    // hold raw pointers into them (a tone patch's PatchInfo::name points directly at its
    // buffer's first byte, same convention as a ROM tone patch; a rhythm set's PatchInfo::ptr
    // points at its buffer, since Rhythm has no embedded name field to double as one - see
    // dataStructures.h).
    void refreshUserPatches();
    std::vector<std::array<uint8_t, 0x16a>> userToneBuffers;
    std::vector<std::array<uint8_t, 0xa7c>> userDrumBuffers;
    std::vector<std::string> userPatchNames;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualJVProcessor)
};
