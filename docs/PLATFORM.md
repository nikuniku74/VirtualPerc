# Platform

## iPadOS (priority 1)

First device: **iPad Air M1**.

- JUCE `juce_add_gui_app` with microphone + background audio
- Core Audio via JUCE `AudioDeviceManager`
- USB class-compliant interfaces appear as Core Audio devices
- Interruption / route change: JUCE device callbacks; engine `prepare()` is re-run; tracking state is not forcibly reset to IDLE unless audio actually stops
- Background: `UIBackgroundModes = audio` so a live set is not muted when the screen locks (subject to iPadOS policy)

Permissions:

- `NSMicrophoneUsageDescription` — listen to the kit

Build:

```bash
./scripts/setup-ai.sh               # ORT iOS xcframework + beatnet.onnx
./scripts/configure-ios.sh          # Xcode project in build-ios/
./scripts/build-simulator.sh        # no signing
```

Device deploy: open `build-ios/*.xcodeproj`, set your Development Team, plug in the iPad, run.

Live status of what is bundled and what still needs a device A/B: `docs/STATUS.md`.

## Android (priority 2, not now)

Same `vp_core`. Audio I/O via Oboe/AAudio. JUCE CMake Android is not a complete app workflow; MVP 6 will wrap the core in a Gradle native module.

## Not shipping

macOS, Windows, Web, WebAudio. A Mac GUI/test binary exists only for development.

## Buffer / sample rate

Support 44.1 kHz and 48 kHz. Buffer size is whatever the device/session gives; the engine is block-size agnostic. Prefer the lowest stable buffer on the USB interface (often 64–128 frames).
