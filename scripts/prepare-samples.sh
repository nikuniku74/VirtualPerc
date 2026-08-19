#!/usr/bin/env bash
# Turn raw percussion recordings into the app's stroke assets.
#
# Usage: ./scripts/prepare-samples.sh <folder-with-source-recordings>
#
# Expects conga_1..conga_4 and shaker_1/shaker_2 (any format JUCE can read).
# See Assets/Percussion/ATTRIBUTION.md for where the current ones came from.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$ROOT/Assets/Percussion/source}"
OUT="$ROOT/Assets/Percussion"
PREP="$ROOT/build-host/VPPrep_artefacts/Release/VPPrep"

if [[ ! -x "$PREP" ]]; then
  echo "Build the preparation tool first:" >&2
  echo "  VP_PREP_SRC=scripts/prep_samples.cpp cmake -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release" >&2
  echo "  cmake --build build-host --target VPPrep" >&2
  exit 1
fi

cd "$SRC"
# max seconds per stroke: long enough for the ring, short enough not to hold a
# voice open through the next bar.
"$PREP" conga_1.mp3  "$OUT/tumba.wav"        1.10 44100
"$PREP" conga_2.mp3  "$OUT/tumba_b.wav"      1.10 44100
"$PREP" conga_3.mp3  "$OUT/open.wav"         0.70 44100
"$PREP" conga_4.mp3  "$OUT/slap.wav"         0.22 44100
"$PREP" shaker_2.mp3 "$OUT/shaker_down.wav"  0.30 44100
"$PREP" shaker_1.mp3 "$OUT/shaker_up.wav"    0.20 44100
echo "Done. Reconfigure so CMake re-embeds them."
