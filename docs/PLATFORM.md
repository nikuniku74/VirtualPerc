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

44.1, 48, 88.2 and 96 kHz, offered on the settings page as **CLOCK**, and **AUTO** by default. Buffer size likewise: 64 to 512, AUTO by default. The engine is block-size agnostic.

AUTO is not "pick something". It means **the interface holds the clock and the app opens at whatever it is already running at**. On a rig where an X-Air is the master at 48 kHz that is the whole answer: nothing is written to the hardware, so nothing re-clocks and nothing else playing through the route is disturbed. A fixed value asks the interface for that rate instead; if the interface does not have it, iOS answers with the one it does have, the device reports it and the engine prepares at it.

## Opening the device without a click

Three things at startup used to reconfigure the hardware, and on an external interface each reconfiguration is an audible click and a chance for whatever else is playing to be dropped:

- **JUCE finding the sample rates.** Left to itself, `juce_Audio_ios.cpp` discovers what the route supports by *setting* it: `setPreferredSampleRate` at 4 kHz, then 192 kHz, then every kilohertz in between, around 190 times back to back, on every device open. That sweep is the crack, and it is what pulls the hardware rate out from under another app. The rates are now declared instead, through `JUCE_IOS_AUDIO_EXPLICIT_SAMPLERATES` in `CMakeLists.txt`, and the sweep never runs.
- **Session after device.** The category, mode, rate and buffer were applied *after* JUCE had opened the device, so the device was opened at whatever came out and then the session moved the hardware under it. `vp::prepareAudioSession` now runs first, and every write to the session is conditional: a rate or a buffer already correct is not written again.
- **Open, then reopen.** `setAudioChannels` opened the device and `setAudioDeviceSetup` immediately closed and reopened it at 48 kHz with a 256-frame buffer, whatever the interface had been on. The session is now settled first, so the open lands on the right clock and `applyAudioSetup` has nothing left to change: it only ever writes the channel fields for the one case that has to move them, the microphone granted after the device was opened without one. Any other write there makes the setup compare unequal and reopens the device for nothing.

The category is `AVAudioSessionCategoryPlayAndRecord` with `MixWithOthers`, `DefaultToSpeaker`, `AllowBluetoothA2DP` and `AllowAirPlay`. `MixWithOthers` is what lets the track being played along to keep playing; HFP Bluetooth is deliberately absent, because that route is 8-16 kHz and makes everything mixed through it sound slow and crushed.

Mode is `AVAudioSessionModeMeasurement` - no AGC, no noise suppression, no echo canceller between the room and the tracker. The settings page can put it back to `Default` (**INGRESSO / ELAB.**) for a route that misbehaves without iOS's processing.
