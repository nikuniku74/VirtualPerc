#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
chmod +x scripts/fetch_onnxruntime.sh scripts/train_export_beat_model.py scripts/export_beatnet_onnx.py
./scripts/fetch_onnxruntime.sh --all

PY=python3
if [[ -x "$ROOT/.venv-ai/bin/python" ]]; then
  PY="$ROOT/.venv-ai/bin/python"
elif [[ ! -d "$ROOT/.venv-ai" ]]; then
  python3 -m venv "$ROOT/.venv-ai"
  PY="$ROOT/.venv-ai/bin/python"
  "$PY" -m pip install -q numpy torch onnx onnxscript
fi

"$PY" -c "import numpy, torch, onnx" 2>/dev/null || "$PY" -m pip install -q numpy torch onnx onnxscript

need_model=0
if [[ ! -f "$ROOT/Assets/Models/beatnet.onnx" ]]; then
  need_model=1
else
  sz=$(wc -c < "$ROOT/Assets/Models/beatnet.onnx" | tr -d ' ')
  if [[ "${sz:-0}" -lt 500000 ]]; then
    need_model=1
  fi
fi
if [[ "$need_model" == "1" ]]; then
  "$PY" scripts/export_beatnet_onnx.py
else
  echo "Keeping existing Assets/Models/beatnet.onnx"
fi
echo "AI assets ready (host ORT + iOS xcframework + BeatNet beatnet.onnx)."
echo "Host tests: ./scripts/run-tests.sh"
echo "iPad Xcode: ./scripts/configure-ios.sh"
