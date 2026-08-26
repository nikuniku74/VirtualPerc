#!/usr/bin/env bash
# Turn raw percussion recordings into the app's stroke assets.
#
# Usage: ./scripts/prepare-samples.sh
#
# Expects the VCSL / VSCO-2-CE one-shots already downloaded into
# /tmp/vp-perc-src (see Assets/Percussion/ATTRIBUTION.md).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python3 "$ROOT/scripts/prepare_vcsl_samples.py"
echo "Done. Reconfigure so CMake re-embeds them."
