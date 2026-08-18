#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
if [[ ! -d build-ios ]]; then
  ./scripts/configure-ios.sh
fi
cmake --build build-ios --target VirtualPercussionist --config Debug -- -sdk iphonesimulator -arch arm64 CODE_SIGNING_ALLOWED=NO
echo "Simulator build finished."
