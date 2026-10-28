#!/usr/bin/env bash
# A separate, locked device build; never reuses the personal FULL configuration.
set -euo pipefail
[[ $# -eq 1 && "$1" =~ ^[1-9][0-9]*$ ]] || {
  echo "usage: $0 <new App Store Connect build number>" >&2
  exit 2
}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$ROOT" -B "$ROOT/build-ios-store" -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_CONFIGURATION_TYPES=Release \
  -DVP_DEVELOPMENT_TEAM="${VP_DEVELOPMENT_TEAM:-C2APL6E5HJ}" \
  -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="${VP_DEVELOPMENT_TEAM:-C2APL6E5HJ}" \
  -DVP_APPSTORE_RELEASE=ON -DVP_UNLOCKED=OFF -DVP_STOREKIT=ON \
  -DVP_FILE_SHARING=OFF -DVP_PAYWALL_DEBUG=ON \
  -DVP_BUNDLE_ID=com.virtualpercussionist.app -DVP_PRODUCT_NAME="Virtual Percussionist" \
  -DVP_BUILD_NUMBER="$1" -DVP_USE_SIGNALSMITH=OFF -DVP_ENABLE_RECORDED_LOOPS=ON
