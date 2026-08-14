#!/bin/zsh
# Replace the Ableton VST3 with the latest local build.
# Quit Ableton Live first or it will keep the old binary in memory.

set -e
# Prefer Release. The Debug binary asserts and aborts Live.
SRC="/Users/johnnyclem/Desktop/Repos/tulipcc/neon-link/build-plugin-rel/NeonLink_artefacts/Release/VST3/NEON LINK.vst3"
if [[ ! -d "$SRC" ]]; then
  SRC="/Users/johnnyclem/Desktop/Repos/tulipcc/neon-link/build-plugin/NeonLink_artefacts/Release/VST3/NEON LINK.vst3"
fi
DEST="$HOME/Library/Audio/Plug-Ins/VST3/NEON LINK.vst3"

if [[ ! -d "$SRC" ]]; then
  echo "Build missing: $SRC"
  echo "Run: cmake --build build-plugin -j --target NeonLink_VST3"
  exit 1
fi

if pgrep -xq "Live" || pgrep -xq "Ableton Live 12 Suite" || pgrep -xq "Ableton Live 12"; then
  echo "Quit Ableton Live first (Cmd+Q), then run this script again."
  exit 1
fi

if [[ -e "$DEST" && ! -w "$DEST" ]]; then
  echo "The installed plug-in is not writable. Need admin once:"
  sudo rm -rf "$DEST"
fi

rm -rf "$DEST"
cp -R "$SRC" "$DEST"
echo "Installed:"
ls -la "$DEST/Contents/MacOS/"
echo
echo "Reopen Live, rescan plug-ins, add NEON LINK. You should see LIVE OUT NET MIDI AUD SYS."
