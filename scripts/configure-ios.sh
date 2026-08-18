#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
chmod +x scripts/fetch_onnxruntime.sh scripts/train_export_beat_model.py scripts/export_beatnet_onnx.py

./scripts/fetch_onnxruntime.sh --ios

if [[ ! -f Assets/Models/beatnet.onnx ]] || [[ $(wc -c < Assets/Models/beatnet.onnx | tr -d ' ') -lt 500000 ]]; then
  echo "Exporting BeatNet ONNX (needs torch/onnx)."
  if [[ ! -x .venv-ai/bin/python ]]; then
    python3 -m venv .venv-ai
    .venv-ai/bin/pip install -q numpy torch onnx
  fi
  .venv-ai/bin/python scripts/export_beatnet_onnx.py
fi

TEAM="${VP_DEVELOPMENT_TEAM:-28H5MJ7244}"
ARGS=(
  -B build-ios
  -G Xcode
  -DCMAKE_SYSTEM_NAME=iOS
  -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0
  -DCMAKE_OSX_ARCHITECTURES=arm64
)
if [[ -n "$TEAM" ]]; then
  ARGS+=(-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="$TEAM")
  ARGS+=(-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY="Apple Development")
fi
cmake "${ARGS[@]}"
echo
echo "Xcode project: $ROOT/build-ios/VirtualPercussionist.xcodeproj"
echo "Open it, select the iPad Air M1, enable the microphone, Run."
echo "See docs/STATUS.md for what the bundled model can and cannot lock."
