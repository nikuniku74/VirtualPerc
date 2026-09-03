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
  -DVP_ENABLE_RECORDED_LOOPS=ON
  # Signalsmith's first seek primes two stereo stretchers inside the real-time
  # callback and can overrun an iPad buffer as LOOP enters. The built-in WSOLA
  # has bounded, allocation-free priming and passes the same timing suite.
  -DVP_USE_SIGNALSMITH=OFF
)
if [[ -n "$TEAM" ]]; then
  ARGS+=(-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="$TEAM")
  ARGS+=(-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY="Apple Development")
fi
echo "Configuring build-ios ..."
cmake "${ARGS[@]}"

# What actually ended up in the generated project.
#
# The settings live in build-ios/, not in the tree, so an Xcode window opened
# before a change still shows the old destinations - and this script does
# network fetches before it gets here, any of which can abort it under `set -e`
# and leave the old project in place looking untouched. Printing what the
# project really says turns "I still see the old list" into one line of fact.
PBX="build-ios/VirtualPercussionist.xcodeproj/project.pbxproj"
if [[ -f "$PBX" ]]; then
  echo
  echo "Nel progetto generato:"
  grep -hoE '(TARGETED_DEVICE_FAMILY|SUPPORTED_PLATFORMS) = [^;]*' "$PBX" \
    | sort -u | sed 's/^/  /'
  echo "  (1 = iPhone, 2 = iPad. Se qui non c'e' 1, Xcode non mostrera' iPhone.)"
fi
echo
echo "Xcode project: $ROOT/build-ios/VirtualPercussionist.xcodeproj"
echo "Open it, pick an iPad or an iPhone, enable the microphone, Run."
echo "Destinazioni: iPhone e iPad (TARGETED_DEVICE_FAMILY 1,2)."
echo "Mac si', Apple Vision no: quelle due si decidono in App Store Connect,"
echo "  Pricing and Availability - non c'e' un build setting. Vedi docs/PLATFORM.md."
echo "See docs/STATUS.md for what the bundled model can and cannot lock."
