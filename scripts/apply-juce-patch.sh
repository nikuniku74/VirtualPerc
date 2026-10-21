#!/usr/bin/env bash
# The iOS audio fixes live in a patch on top of JUCE 8.0.15 (explicit sample
# rates, no HFP, no jassert on a late first callback...), plus the iOS file
# chooser no longer leaving the interface frozen when a large file's access
# claim fails (docs/TODO.md item 136). A fresh checkout has
# stock JUCE without them, and the app then sounds and behaves differently. Run
# this once after `git submodule update --init`; safe to run again.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PATCH="$ROOT/third_party/juce-ios-audio.patch"
cd "$ROOT/third_party/JUCE"
if git apply --reverse --check "$PATCH" 2>/dev/null; then
  echo "JUCE iOS audio patch: already applied"
elif git apply --check "$PATCH"; then
  git apply "$PATCH"
  echo "JUCE iOS audio patch: applied"
else
  echo "JUCE iOS audio patch does not apply: JUCE is not the version it was made on" >&2
  exit 1
fi
