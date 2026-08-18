#!/usr/bin/env bash
# Fetch ONNX Runtime for the Mac host (osx dylib) and/or iPad
# (official Microsoft.ML.OnnxRuntime NuGet xcframework, static slices + CoreML).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="${ORT_VERSION:-1.20.1}"
WANT_HOST=1
WANT_IOS=0
for arg in "$@"; do
  case "$arg" in
    --ios) WANT_IOS=1; WANT_HOST=0 ;;
    --all) WANT_HOST=1; WANT_IOS=1 ;;
    --host) WANT_HOST=1; WANT_IOS=0 ;;
  esac
done
# Default: host on macOS; iOS is pulled by configure-ios / setup-ai --all
if [[ "${1:-}" == "" ]]; then
  WANT_HOST=1
  WANT_IOS=0
fi

fetch_host() {
  local DEST="$ROOT/third_party/onnxruntime"
  if [[ -f "$DEST/include/onnxruntime_c_api.h" && -e "$DEST/lib/libonnxruntime.dylib" ]]; then
    echo "ONNX Runtime host already at $DEST"
    return 0
  fi
  local ARCH PKG URL TMP SRC
  ARCH="$(uname -m)"
  case "$ARCH" in
    arm64) PKG="onnxruntime-osx-arm64-${VER}.tgz" ;;
    x86_64) PKG="onnxruntime-osx-x86_64-${VER}.tgz" ;;
    *) echo "Unsupported host arch $ARCH"; return 1 ;;
  esac
  URL="https://github.com/microsoft/onnxruntime/releases/download/v${VER}/${PKG}"
  TMP="$(mktemp -d)"
  echo "Fetching $URL"
  curl -L --fail "$URL" -o "$TMP/$PKG"
  tar -xzf "$TMP/$PKG" -C "$TMP"
  SRC="$(find "$TMP" -maxdepth 1 -type d -name 'onnxruntime-*' | head -n 1)"
  mkdir -p "$DEST"
  cp -R "$SRC/include" "$SRC/lib" "$DEST/"
  rm -rf "$TMP"
  echo "Installed host headers+libs in $DEST"
}

fetch_ios() {
  local XCF="$ROOT/third_party/onnxruntime-ios/onnxruntime.xcframework"
  local HDR="$XCF/ios-arm64/onnxruntime.framework/Headers/onnxruntime_c_api.h"
  if [[ -f "$HDR" \
        && -f "$XCF/ios-arm64/onnxruntime.framework/onnxruntime" \
        && -f "$XCF/ios-arm64_x86_64-simulator/onnxruntime.framework/onnxruntime" ]]; then
    echo "ONNX Runtime iOS xcframework already at $XCF"
    return 0
  fi
  local NUPKG URL TMP
  URL="https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${VER}/microsoft.ml.onnxruntime.${VER}.nupkg"
  TMP="$(mktemp -d)"
  echo "Fetching official iOS xcframework from $URL"
  curl -L --fail --retry 3 --max-time 180 -o "$TMP/ort.nupkg" "$URL"
  unzip -p "$TMP/ort.nupkg" runtimes/ios/native/onnxruntime.xcframework.zip > "$TMP/xcf.zip"
  mkdir -p "$ROOT/third_party/onnxruntime-ios"
  unzip -o "$TMP/xcf.zip" -d "$ROOT/third_party/onnxruntime-ios"
  rm -rf "$TMP"
  echo "Installed iOS xcframework in $XCF"
}

if [[ "$WANT_HOST" == "1" ]]; then
  fetch_host
fi
if [[ "$WANT_IOS" == "1" ]]; then
  fetch_ios
fi
