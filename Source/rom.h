#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <array>
#include <string>

struct RomInfo {
  size_t length;
  std::string filename;
  std::string checksum;
  bool needsUnscramble;
  std::string checksumUnscrambled;
  bool loaded;
};

constexpr size_t romCount = 27;
constexpr size_t romCountRequired = 5;
constexpr size_t romCountChk = 26;
extern RomInfo romInfos[romCount];

int getRomIndex(std::string filename);
bool loadRom(int romI, uint8_t *dst, std::array<uint8_t *, romCount> &cache);
bool preloadAll(std::array<uint8_t *, romCount> &cache);

void unscrambleRom(const uint8_t *src, uint8_t *dst, int len);

// ROM folder override (Alan's request, 2026-09-08): by default the ROM dump files are read from
// (and, for the ones that need unscrambling, cached into) the OS's own per-user app-data folder -
// a location that isn't obvious to find/reach on every platform, which made "I copied the ROMs
// but it still doesn't work" hard for a user to self-diagnose or fix (see
// VirtualJVProcessor::getRomsFolder()/setRomsFolderOverride() for the persisted, user-facing side
// of this in Settings). Empty override string = use the default location. Plain std::string
// rather than juce::File so this header stays free of the JUCE dependency (only rom.cpp needs it).
void setRomsDirectoryOverride(const std::string &path);
std::string getRomsDirectoryOverride();
// Resolved absolute path currently in effect (override if set, else the default), for display.
std::string getEffectiveRomsDirectory();
