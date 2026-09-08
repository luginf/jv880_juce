# Virtual JV-880 - Documentation

This is a cycle-accurate emulation of the Roland JV-880's actual CPU running its original
firmware (based on [Nuked-SC55](https://github.com/nukeykt/Nuked-SC55)) - not a re-implementation
from scratch. When something behaves a certain way here, it's usually because that's what the
real hardware's ROM does.

## How the JV-880 works (quick primer)

- **Tone**: the smallest sound-generating unit. A Tone has one waveform (PCM sample) plus its own
  envelope, filter, LFO, etc.
- **Patch**: a full playable sound, made of up to **4 Tones** layered/split/velocity-crossfaded
  together, plus effects (reverb, chorus) and a shared TVF/TVA structure. This is what you'd
  normally call "a preset" or "a sound." 128 factory Patches ship in two ROM banks (**Preset A**
  and **Preset B**, 64 each), plus a rewritable **Internal** bank (64 slots) you can save your own
  Patches into - and optionally more from an SR-JV80 expansion board.
- **Rhythm Set**: like a Patch, but each of the 61 keys plays an independent Tone/sample instead
  of one sound across the keyboard - for drums/percussion.
- **Performance**: a multitimbral setup combining **8 Parts** - 7 Parts, each playing one Patch on
  its own MIDI receive channel, plus a fixed 8th Part playing a Rhythm Set. Reverb and chorus are
  shared across the whole Performance (one effects bus), not per-Part. This is how the JV-880 does
  multi-instrument arrangements (e.g. one MIDI track per Part from a DAW/sequencer).
- **Patch mode vs Performance mode**: the front panel's PATCH/PERFORM button switches the whole
  unit between playing a single Patch (or Rhythm Set) and playing a full 8-part Performance.

## Tabs

- **Browse**: lists every Patch/Rhythm Set available - the two ROM Preset banks, your own saved
  "User" patches, and any loaded SR-JV80 expansion board's patches. Click to load it into the
  currently edited Patch/Rhythm. "Save As..." stores the current Patch into your own User bank
  (`~/.config/JV880/User/*.jvp`). Right-click a patch for "Send to Performance Part N" to assign
  it directly to a Performance Part without leaving Browse.
- **Performance**: builds and plays a full 8-Part Performance. Per Part: assigned Patch/Rhythm
  Set, MIDI receive channel, level, pan, on/off. "All On/Off" toggles every Part at once. Runs on
  the same single emulated engine as Patch mode (no extra CPU cost per Part). Save/load your own
  Performances (`~/.config/JV880/Performances/*.jvpf`), separate from the JV-880's own internal
  Performance memory (not yet importable here - see CLAUDE.md's "Piste ouverte" if curious).
  Assigning Parts from two different expansion boards works but only the most recently assigned
  one actually sounds right (same limit as the real hardware: one expansion slot).
- **Common**: Patch-wide settings shared by all 4 Tones - name, effects (reverb/chorus depth,
  type), structure, and other global Patch parameters.
- **Tone 1-4** (Patch mode) / **Rhythm Set** (Rhythm mode): per-Tone editing - waveform, pitch,
  filter (TVF), amplifier (TVA), LFO, and so on. Which set of tabs you get depends on whether the
  currently loaded program is a Patch or a Rhythm Set.
- **Settings**: Master Tune, Master Volume, Reverb/Chorus on-off, DSP Load meter, ROM folder
  (Browse/Use Default/Reload ROMs - can be changed and reloaded without restarting), and the
  on-screen display mode (see below). In Standalone builds only, this is also where you pick your
  audio/MIDI device and buffer size/sample rate (in a plugin, your DAW/host owns that instead).
- **Panel**: only shown when Display is set to "LCD only" (see below) - a row of clickable
  physical panel buttons plus the DATA dial, for driving the real firmware's own screens directly
  (EDIT/SYSTEM/RHYTHM/UTILITY, cursor, TONE SELECT, etc.), same as pressing them on real hardware.

## On-screen display modes (Settings tab)

- **LCD only** (default): just the small dot-matrix screen, like the original small-form displays
  built for this hardware.
- **Panel Compact** / **Panel Full**: a photo of the JV-880's actual front panel across the top of
  the window, with the real LCD screen live-embedded into it and every button/knob clickable in
  place, instead of the separate "Panel" tab. Full shows the whole rack unit (needs a wide enough
  window); Compact crops to just the controls.

On any panel skin, useful gestures beyond a plain click:
- **Ctrl+click** a button to latch it down (stays pressed until Ctrl-clicked again) - needed for
  real two-button combos the JV-880 expects, like holding TONE SELECT while pressing a TONE
  SWITCH, or holding TONE SELECT's second function (PARAM SHIFT) while pressing -/+.
  Right-clicking TONE SELECT does the same thing as Ctrl+clicking it.
- **Ctrl+click or right-click the DATA dial** to hold it down while turning it (same idea, for the
  "hold DATA and rotate" combos the manual describes); drag up/down or use the mouse wheel to
  actually turn it.
- **Right-click the LCD** (in any display mode) to change its color.
- Small red LEDs mirror PATCH/PERFORM's real state and which of the 4 Tones in the current Patch
  are actually sounding (TONE SWITCH lights = active tone).

## Patch/Rhythm storage

- **Preset A / Preset B**: 64+64 factory ROM patches, read-only.
- **Internal**: 64 rewritable slots. Slot 0 is whatever's currently loaded in Patch mode; slots
  1-63 (and Performance Parts 1-7) can hold patches you assign, including User patches and
  expansion-board patches.
- **User**: patches you've saved yourself via "Save As...", stored on disk and listed in Browse
  alongside the ROM banks.
- **Expansions (SR-JV80)**: if a supported expansion board dump is present in the ROM folder, its
  patches show up in Browse and are assignable like any other patch (Performance Parts included).
