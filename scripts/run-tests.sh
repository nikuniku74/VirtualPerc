#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
cmake -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --target VPTests --parallel
./build-host/VPTests_artefacts/Release/VPTests
