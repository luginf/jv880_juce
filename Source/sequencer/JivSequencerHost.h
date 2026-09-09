#pragma once

// The whole surface JivSequencerPanel actually needs from whatever it's embedded in - ported
// verbatim from the D-110 emulator's D110SequencerHost (see ~/src/D110/d110-vst-emulator,
// docs/sequencer.md) together with JivSequencerEngine/JivSequencerPanel, 2026-09-09. That
// project has two concrete implementations (its D-110 plugin and the standalone Nonet Sequencer
// app); this project only ever has one (VirtualJVProcessor), so most of the "optional" methods
// below simply stay at their false/no-op default forever - kept on the interface anyway rather
// than trimmed, since JivSequencerPanel.cpp (ported unmodified) still calls through them and
// they're what already correctly hides the corresponding UI when unsupported.
//
// The one deliberate JV-880-specific design choice here: **no Program Change support**
// (supportsProgramChange() stays false, never overridden). Alan's own reasoning, 2026-09-09:
// since this only ever needs to work against the emulated firmware, not real hardware, there's
// no reason to route instrument selection through numeric Program Change/Bank numbers at all -
// a sequencer track IS a Performance Part (see PluginProcessor.h's PerformancePart), and its
// patch is assigned directly from PatchBrowser's "Send to Sequencer" submenu (the same
// mechanism "Send to Performance Part N" already uses), which persists with the Performance
// itself. Keeping supportsProgramChange() off means the numeric dialog, live-patch-hint,
// resync and capture-live-patch machinery below is simply never reached - real dead code paths
// in JivSequencerPanel.cpp, left in place unmodified rather than surgically removed, since
// ripping them out would mean actually editing that 1884-line ported file instead of just not
// calling into it.

#include <juce_core/juce_core.h>

#include <vector>

namespace jivseq { class JivSequencerEngine; }

class JivSequencerHost {
public:
	virtual ~JivSequencerHost() = default;

	virtual jivseq::JivSequencerEngine &getSequencer() = 0;

	// All 4 song slots at once, as a standalone .midiseq file - see JivSequencerSongsFile.h.
	virtual void exportSequencerSongs(const juce::File &file) = 0;
	virtual void importSequencerSongs(const juce::File &file) = 0;

	// Right-click STOP: all-notes-off, everywhere this host can reach.
	virtual void midiPanic() = 0;

	// Where the panel's own file dialogs (Load/Save .mid, Load/Save .midiseq) should start
	// from and remember afterwards.
	virtual juce::File getLastDialogDir() const = 0;
	virtual void setLastDialogDir(const juce::File &dir) = 0;

	// Optional: whether this host lets a track's MIDI channel be changed from the panel itself
	// (clicking its CH readout). False here (never overridden) - a track's channel comes from
	// its Performance Part's own configured receive channel instead (see the Performance tab),
	// exactly the same "the live config already owns this" reasoning the D-110 plugin used for
	// its own SYSTEM page.
	virtual bool supportsTrackChannelEdit() const { return false; }
	virtual void setTrackChannel(int /*track*/, int /*channel*/) {}

	// Optional: whether the panel offers JivSequencerEngine's extra tracks beyond kNumTracks
	// (see JivSequencerEngine::kMaxTracks). False here (never overridden) - always exactly the
	// 8 JV-880 Performance Parts (7 melodic + Rhythm), never more.
	virtual bool supportsExtraTracks() const { return false; }

	// Optional: a track's own fixed MIDI Program Change/Bank, sent once at the PLAY/REC edge.
	// Never overridden here - see this file's own top comment for why.
	virtual bool supportsProgramChange() const { return false; }
	virtual int getTrackProgram(int /*track*/) const { return -1; }
	virtual void setTrackProgram(int /*track*/, int /*program*/) {}

	// JV-880-specific addition (2026-09-09): the name of whatever patch this track currently
	// recalls (assigned from PatchBrowser's "Send to Sequencer" submenu, not from this
	// interface - see this file's own top comment), for the CC Change dialog to show as a
	// read-only recall label. Empty string = no patch assigned. Default empty/unused here since
	// nothing else implements this host.
	virtual juce::String getTrackPatchName(int /*track*/) const { return {}; }
	virtual bool supportsProgramChangeForTrack(int track) const { return supportsProgramChange(); }
	virtual int getTrackBank(int /*track*/) const { return 1; }
	virtual void setTrackBank(int /*track*/, int /*bank*/) {}
	virtual bool supportsBankLsb() const { return false; }
	virtual int getTrackBankLsb(int /*track*/) const { return 1; }
	virtual void setTrackBankLsb(int /*track*/, int /*bankLsb*/) {}

	// One named patch out of a loaded instrument definition file - Nonet Sequencer-only
	// feature in the source project, never applicable here since supportsProgramChange() is
	// permanently false.
	struct InstrumentPatch {
		juce::String group, name;
		int prog = 0, bank = -1, bankLsb = -1;
	};
	virtual bool supportsInstrumentDefinitions() const { return false; }
	virtual juce::String getInstrumentDefinitionName() const { return {}; }
	virtual juce::String getInstrumentDefinitionPath() const { return {}; }
	virtual bool loadInstrumentDefinition(const juce::File & /*file*/) { return false; }
	virtual std::vector<InstrumentPatch> getInstrumentPatches() const { return {}; }

	// Manual "resend now" escape hatch for supportsProgramChange() hosts - no-op here, same
	// reasoning as the rest of the Program Change surface above.
	virtual void resyncProgramChanges() {}

	// Optional: a track's own fixed Channel Volume/Pan, sent alongside its Program Change.
	// Never overridden here (JV-880 Performance Parts already have their own LEVEL/PAN, set
	// directly on the Performance tab, same "the live config already owns this" reasoning as
	// supportsTrackChannelEdit()).
	virtual bool supportsTrackVolumePan() const { return false; }
	virtual int getTrackVolume(int /*track*/) const { return -1; }
	virtual void setTrackVolume(int /*track*/, int /*volume*/) {}
	virtual int getTrackPan(int /*track*/) const { return -1; }
	virtual void setTrackPan(int /*track*/, int /*pan*/) {}
	virtual bool supportsTrackVolumePanForTrack(int track) const { return supportsTrackVolumePan(); }

	// Optional: read the host's own live sound per part back into the stored per-track
	// Program Change/Bank/Volume/Pan. Never reachable here - supportsProgramChange() is
	// permanently false, so there's nothing for this to capture into.
	virtual bool supportsCaptureLivePatch() const { return false; }
	virtual void captureLivePatchIntoTracks() {}

	// Optional convenience hint for the (never-shown, see above) Program Change dialog's
	// pre-fill.
	virtual int getTrackProgramHint(int /*track*/) const { return -1; }
	virtual int getTrackVolumeHint(int /*track*/) const { return -1; }
	virtual int getTrackPanHint(int /*track*/) const { return -1; }

	// Per-song sound snapshot: the instrument's whole memory, captured/restored per song slot.
	// Not implemented here (2026-09-09 port) - the JV-880's equivalent of "whole memory" would
	// be a much larger NVRAM/patch-bank snapshot than the D-110's, and Alan didn't ask for this
	// specific feature when requesting the port. Stays at its false/no-op default; revisit only
	// if asked.
	virtual bool supportsSoundSnapshots() const { return false; }
	virtual bool hasSoundSnapshot(int /*slot*/) const { return false; }
	virtual void storeSoundSnapshotForSlot(int /*slot*/) {}
	virtual void loadSoundSnapshotForSlot(int /*slot*/) {}

	// JV-880-specific addition (2026-09-10, Alan's report): a sequencer track's patch only
	// actually sounds once the firmware is in Performance mode (see PluginProcessor.h's
	// PerformancePart) - Patch mode ignores it entirely, e.g. after a Browse click left the
	// firmware in Patch mode (see setCurrentProgram()'s own comment on that desync). Called from
	// JivSequencerPanel.cpp wherever the user starts doing something that depends on a track's
	// assigned sound actually being audible - Play, arming/starting a recording - so they never
	// have to remember to flip back to the Performance tab themselves first. No-op default since
	// nothing else implements this host.
	virtual void ensurePerformanceMode() {}
};
