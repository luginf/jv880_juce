#pragma once

// (De)serializes the sequencer's 4 song slots (tempo, time signature, all 8 tracks each)
// to/from an XmlElement, and the standalone .midiseq file format built on top of it - an
// XML wrapper around a gzip+base64-packed standard MIDI file per track (see
// JivSequencerEngine::serializeTrack), not a raw memory/NVRAM-style snapshot. Ported from
// the D-110 emulator's D110SequencerSongsFile 2026-09-09 (see JivSequencerEngine.h's own
// header comment - already firmware-agnostic in the source project, so this was a
// mechanical rename, not a rewrite) - shared between VirtualJVProcessor::getStateInformation/
// setStateInformation (a sub-block of the plugin's own project save) and
// VirtualJVProcessor::exportSequencerSongs/importSequencerSongs (the whole document).

#include <juce_core/juce_core.h>

#include "JivSequencerEngine.h"

namespace jivseq {

// numTracks defaults to JivSequencerEngine::kNumTracks (8, this project's own fixed count -
// see its own comment) so every call site is unaffected unless it deliberately wants
// kMaxTracks instead (never needed here: supportsExtraTracks() is always false, see
// JivSequencerHost.h).
void writeSongsXml(const JivSequencerEngine &engine, juce::XmlElement &xml,
                    int numTracks = JivSequencerEngine::kNumTracks);
void readSongsXml(JivSequencerEngine &engine, const juce::XmlElement &xml,
                   int numTracks = JivSequencerEngine::kNumTracks);

// Returns a short status string suitable for showing the user either way (success or
// failure), the same wording VirtualJVProcessor::lastImportMessage already used.
juce::String exportSongsFile(const JivSequencerEngine &engine, const juce::File &file,
                              int numTracks = JivSequencerEngine::kNumTracks);
juce::String importSongsFile(JivSequencerEngine &engine, const juce::File &file,
                              int numTracks = JivSequencerEngine::kNumTracks);

} // namespace jivseq
