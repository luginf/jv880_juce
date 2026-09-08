ROOT=$(cd "$(dirname "$0")/.."; pwd)

"$ROOT/build/bin/JUCE/Projucer" --resave "$ROOT/VirtualJV.jucer"

cd "$ROOT/Builds/LinuxMakefile"
make CONFIG=Release

# Strip the shipped binaries (Alan's request, 2026-09-08 - the unstripped Standalone alone was
# ~104MB in a Debug build; Release doesn't carry -g but still has a full symbol table). Done here
# rather than only as a manual dev habit so both local releases and CI get it automatically.
strip build/jv880 build/jv880.lv2/jv880.so build/jv880.vst3/Contents/x86_64-linux/jv880.so

# jv880.a (JUCE_TARGET_SHARED_CODE) is a static archive of every object file, purely an
# intermediate link input for the targets above - already linked into all of them, never needed
# again afterwards. Alan noticed it ballooning the build/ folder (222MB) alongside the unstripped
# Standalone; removing it here keeps that down without touching `make clean`/incremental rebuilds
# (make just relinks it from the still-cached .o files next time it's needed).
rm -f build/jv880.a
