#include "JivSequencerSongsFile.h"

namespace jivseq {

// Gzip + Base64, the same packing VirtualJVProcessor uses for the firmware's own NVRAM
// blocks - duplicated rather than shared, on purpose: this generic MemoryBlock<->String
// helper is the only thing standing between this file and zero dependency on the rest of
// the plugin, and it's ten lines.
static juce::String packBlock(const juce::MemoryBlock &raw) {
	if (raw.getSize() == 0) return {};
	juce::MemoryOutputStream compressed;
	{
		juce::GZIPCompressorOutputStream gzip(compressed);
		gzip.write(raw.getData(), raw.getSize());
	}
	return juce::Base64::toBase64(compressed.getData(), compressed.getDataSize());
}

static juce::MemoryBlock unpackBlock(const juce::String &text) {
	juce::MemoryBlock out;
	if (text.isEmpty()) return out;
	juce::MemoryOutputStream compressed;
	if (!juce::Base64::convertFromBase64(compressed, text)) return out;
	juce::MemoryInputStream rawIn(compressed.getData(), compressed.getDataSize(), false);
	juce::GZIPDecompressorInputStream gzip(rawIn);
	out = juce::MemoryBlock();
	juce::MemoryOutputStream decompressed(out, false);
	decompressed.writeFromInputStream(gzip, -1);
	return out;
}

void writeSongsXml(const JivSequencerEngine &engine, juce::XmlElement &xml, int numTracks) {
	xml.setAttribute("seqCurrentSlot", engine.getCurrentSongSlot());
	for (int slot = 0; slot < JivSequencerEngine::kNumSongSlots; ++slot) {
		const juce::String slotSuffix = "Slot" + juce::String(slot);
		xml.setAttribute("seqTempo" + slotSuffix, engine.slotTempo(slot));
		xml.setAttribute("seqTimeSigNum" + slotSuffix, engine.slotTimeSigNumerator(slot));
		xml.setAttribute("seqTimeSigDen" + slotSuffix, engine.slotTimeSigDenominator(slot));
		for (int t = 0; t < numTracks; ++t) {
			const juce::String suffix = slotSuffix + juce::String(t);
			xml.setAttribute("seqMute" + suffix, engine.slotTrackMuted(slot, t) ? 1 : 0);
			xml.setAttribute("seqSolo" + suffix, engine.slotTrackSoloed(slot, t) ? 1 : 0);
			xml.setAttribute("seqQuantize" + suffix, static_cast<int>(engine.slotTrackQuantize(slot, t)));
			xml.setAttribute("seqName" + suffix, engine.slotTrackName(slot, t));
			xml.setAttribute("seqTrack" + suffix, packBlock(engine.slotTrackToBytes(slot, t)));
			// The fixed Program Change/Bank/BankLsb/Volume/Pan override - per-slot since
			// 2026-08-21 (see JivSequencerEngine::getTrackProgram()'s own comment).
			xml.setAttribute("seqProgram" + suffix, engine.slotTrackProgram(slot, t));
			xml.setAttribute("seqBank" + suffix, engine.slotTrackBank(slot, t));
			xml.setAttribute("seqBankLsb" + suffix, engine.slotTrackBankLsb(slot, t));
			xml.setAttribute("seqVolume" + suffix, engine.slotTrackVolume(slot, t));
			xml.setAttribute("seqPan" + suffix, engine.slotTrackPan(slot, t));
			// JV-880-specific addition (2026-09-09) - see JivSequencerEngine::TrackPatch's own
			// comment for why both the raw index and its content-identity travel together.
			const auto patch = engine.slotTrackPatch(slot, t);
			xml.setAttribute("seqPatchIndex" + suffix, patch.index);
			xml.setAttribute("seqPatchName" + suffix, patch.name);
			xml.setAttribute("seqPatchExpansionI" + suffix, (int)patch.expansionI);
			xml.setAttribute("seqPatchIsRhythm" + suffix, patch.isRhythm ? 1 : 0);
			xml.setAttribute("seqChannelOverride" + suffix, engine.slotTrackChannelOverride(slot, t));
		}
	}
}

// selectSongSlot() also stops/rewinds/disarms, which is fine here - every caller (project
// load, .midiseq import) is a deliberate, explicit load, never something mid-transport.
void readSongsXml(JivSequencerEngine &engine, const juce::XmlElement &xml, int numTracks) {
	engine.selectSongSlot(juce::jlimit(
		0, JivSequencerEngine::kNumSongSlots - 1, xml.getIntAttribute("seqCurrentSlot", 0)));
	for (int slot = 0; slot < JivSequencerEngine::kNumSongSlots; ++slot) {
		const juce::String slotSuffix = "Slot" + juce::String(slot);
		engine.setSlotTempo(slot, xml.getDoubleAttribute("seqTempo" + slotSuffix, engine.slotTempo(slot)));
		engine.setSlotTimeSignature(
			slot, xml.getIntAttribute("seqTimeSigNum" + slotSuffix, engine.slotTimeSigNumerator(slot)),
			xml.getIntAttribute("seqTimeSigDen" + slotSuffix, engine.slotTimeSigDenominator(slot)));
		for (int t = 0; t < numTracks; ++t) {
			const juce::String suffix = slotSuffix + juce::String(t);
			const auto trackBytes = unpackBlock(xml.getStringAttribute("seqTrack" + suffix));
			engine.slotTrackFromBytes(slot, t, trackBytes.getData(), trackBytes.getSize());
			engine.setSlotTrackMuted(slot, t, xml.getIntAttribute("seqMute" + suffix, 0) != 0);
			engine.setSlotTrackSoloed(slot, t, xml.getIntAttribute("seqSolo" + suffix, 0) != 0);
			engine.setSlotTrackQuantize(
				slot, t, static_cast<QuantizeGrid>(xml.getIntAttribute("seqQuantize" + suffix, 0)));
			engine.setSlotTrackName(slot, t, xml.getStringAttribute("seqName" + suffix));
			engine.setSlotTrackProgram(slot, t, xml.getIntAttribute("seqProgram" + suffix, -1));
			engine.setSlotTrackBank(slot, t, xml.getIntAttribute("seqBank" + suffix, 1));
			engine.setSlotTrackBankLsb(slot, t, xml.getIntAttribute("seqBankLsb" + suffix, 1));
			engine.setSlotTrackVolume(slot, t, xml.getIntAttribute("seqVolume" + suffix, -1));
			engine.setSlotTrackPan(slot, t, xml.getIntAttribute("seqPan" + suffix, -1));
			JivSequencerEngine::TrackPatch patch;
			patch.index = xml.getIntAttribute("seqPatchIndex" + suffix, -1);
			patch.name = xml.getStringAttribute("seqPatchName" + suffix);
			patch.expansionI = (uint8_t)xml.getIntAttribute("seqPatchExpansionI" + suffix, 0xff);
			patch.isRhythm = xml.getIntAttribute("seqPatchIsRhythm" + suffix, 0) != 0;
			engine.setSlotTrackPatch(slot, t, patch);
			engine.setSlotTrackChannelOverride(
				slot, t, xml.getIntAttribute("seqChannelOverride" + suffix, -1));
		}
	}
}

juce::String exportSongsFile(const JivSequencerEngine &engine, const juce::File &file, int numTracks) {
	juce::XmlElement xml("JivSequencerSongs");
	xml.setAttribute("version", 1);
	writeSongsXml(engine, xml, numTracks);
	if (!xml.writeTo(file)) return "Could not write songs file: " + file.getFullPathName();
	return "Saved sequencer songs: " + file.getFileName();
}

juce::String importSongsFile(JivSequencerEngine &engine, const juce::File &file, int numTracks) {
	std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
	if (xml == nullptr || !xml->hasTagName("JivSequencerSongs"))
		return "Not a sequencer songs file: " + file.getFileName();
	readSongsXml(engine, *xml, numTracks);
	return "Loaded sequencer songs: " + file.getFileName();
}

} // namespace jivseq
