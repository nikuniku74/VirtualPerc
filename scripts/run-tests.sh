#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
cmake -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --target VPTests --parallel
# Full wake matters too: idle-only assertions do not leave maintenance DarkWake.
# A suspended worker can otherwise expire wall-clock test deadlines.
if [[ "$(uname -s)" == "Darwin" ]]; then
    exec /usr/bin/caffeinate -i -u ./build-host/VPTests_artefacts/Release/VPTests
fi
exec ./build-host/VPTests_artefacts/Release/VPTests
