# Platform

## Where the app is allowed to run

One iOS build, and Xcode's four destinations answered deliberately rather than
left to their defaults - because the defaults say yes to all of them.

| Destination | | Decided where |
|---|---|---|
| iPhone | **yes** | `TARGETED_DEVICE_FAMILY 1` in `CMakeLists.txt` |
| iPad | **yes** | `TARGETED_DEVICE_FAMILY 2`, and the device everything here was measured on |
| Mac (Designed for iPad) | **yes** | App Store Connect |
| Apple Vision | **no** | App Store Connect |

Two of those four are not build settings, and that is the part worth knowing
before hunting for one. Apple's help pages are explicit: an iPhone/iPad app is
offered on **Apple Vision Pro** and on **Macs with Apple silicon** by default,
and the developer opts out in App Store Connect under *Pricing and Availability*
— [Vision](https://developer.apple.com/help/app-store-connect/manage-your-apps-availability/manage-availability-of-iphone-and-ipad-apps-on-apple-vision-pro),
[Mac](https://developer.apple.com/help/app-store-connect/manage-your-apps-availability/manage-availability-of-iphone-and-ipad-apps-on-macs-with-apple-silicon/).
There is no Xcode setting that turns Vision off; one that looks like it does is
being ignored.

So the checklist is two items in two places:

- **Xcode / CMake** — `TARGETED_DEVICE_FAMILY "1,2"`. This is what puts iPhone
  in the list, and it lands in the Info.plist as `UIDeviceFamily`.
- **App Store Connect** — Pricing and Availability: leave *iPhone and iPad Apps
  on Apple Silicon Mac* selected, deselect *iPhone and iPad Apps on Apple Vision
  Pro*.

`SUPPORTED_PLATFORMS` is set to `iphoneos iphonesimulator` as well, but only to
keep Xcode's own destination list tidy: with the visionOS SDK installed Xcode
adds "Apple Vision (Designed for iPad)" to any iOS target by itself. That is
about what the IDE offers, not about what a buyer can install.

Vision is off on purpose. The whole analysis is tuned around two microphone
paths - a close kit mic, and an iPad's own speaker into its own room - and a
headset's array is neither. Shipping it there would mean shipping a listener
that has never been near the thing it has to listen to, and "installable" is not
a claim the buyer can tell apart from "works".

This is a **destination** list, not a port. A Mac still builds the app natively
too (no `CMAKE_SYSTEM_NAME=iOS`), which is what the host tests and the probes
run in.

**After changing any of this, regenerate the project**: the settings live in
`build-ios/VirtualPercussionist.xcodeproj`, which is generated, so an Xcode
window opened before the change still shows the old destinations.

```bash
rm -rf build-ios && ./scripts/configure-ios.sh
```

### If Supported Destinations still shows the old list

Ask the generated project rather than the Xcode window, because the two can
disagree for a whole minute after a `git pull`:

```bash
grep -hoE '(TARGETED_DEVICE_FAMILY|SUPPORTED_PLATFORMS) = [^;]*' \
  build-ios/VirtualPercussionist.xcodeproj/project.pbxproj | sort -u
```

`TARGETED_DEVICE_FAMILY = "1,2"` is iPhone and iPad. A bare `2` means the
project predates the change and was never regenerated - `configure-ios.sh`
prints the same two lines at the end for exactly this reason.

And check the script actually reached its own last step. It fetches ONNX Runtime
and may build a Python environment before it ever calls `cmake`, it runs under
`set -euo pipefail`, and a failure in any of that aborts it - leaving the
previous `build-ios` in place, looking untouched because it is.

Xcode also keeps the project in memory. Regenerating the file underneath an open
window does not reliably refresh that panel, so quit Xcode rather than closing
the tab. **The iPhone row is the canary**: the file says `"1,2"`, so if the panel
still shows iPad alone it is the window that is stale, not the project.

### Finding out what a destination really is

`SUPPORTED_PLATFORMS` does *not* remove "Apple Vision (Designed for iPad)" from
the list - measured, on a project where the setting had landed and the row was
still there. Rather than guess at another setting name, ask Xcode to write it:

```bash
cp build-ios/VirtualPercussionist.xcodeproj/project.pbxproj /tmp/pbx.before
# In Xcode: General -> Supported Destinations -> select the row -> "-", then save.
diff /tmp/pbx.before build-ios/VirtualPercussionist.xcodeproj/project.pbxproj
```

The diff is the setting, exactly as Xcode spells it. It belongs in
`CMakeLists.txt` afterwards, because an edit made in the Xcode UI is written to
a **generated** file and the next `configure-ios.sh` throws it away.

Worth knowing before doing any of that: this panel decides what Xcode offers
*you*, not what a buyer can install. That is the App Store Connect setting
above, and it is the one that matters for keeping the app off Apple Vision.

## iPadOS (priority 1)

First device: **iPad Air M1**.

On a phone the same layout has to survive a screen a third of the size and a
notch. Two things make it: every full-screen page starts from the system's own
safe-area insets rather than a margin that happened to look right on an iPad
(`MainComponent::safePadded`), and the stage rows scale together when there is
less room than they naturally want instead of running off the bottom. Measured
against the real geometries, the scale is 1.00 on an iPad in both orientations
and 0.71-0.94 on a phone.

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

Windows, Web, WebAudio, and visionOS. macOS gets the iPad build under "Designed
for iPad" on Apple silicon; a native Mac GUI/test binary also exists, and that
one is for development.

## Buffer / sample rate

44.1, 48, 88.2 and 96 kHz, offered on the settings page as **CLOCK**, and **AUTO** by default. Buffer size likewise: 64 to 512, AUTO by default. The engine is block-size agnostic.

AUTO is not "pick something". It means **the interface holds the clock and the app opens at whatever it is already running at**. On a rig where an X-Air is the master at 48 kHz that is the whole answer: nothing is written to the hardware, so nothing re-clocks and nothing else playing through the route is disturbed. A fixed value asks the interface for that rate instead; if the interface does not have it, iOS answers with the one it does have, the device reports it and the engine prepares at it.

## Opening the device without a click

Three things at startup used to reconfigure the hardware, and on an external interface each reconfiguration is an audible click and a chance for whatever else is playing to be dropped:

- **JUCE finding the sample rates.** Left to itself, `juce_Audio_ios.cpp` discovers what the route supports by *setting* it: `setPreferredSampleRate` at 4 kHz, then 192 kHz, then every kilohertz in between, around 190 times back to back, on every device open. That sweep is the crack, and it is what pulls the hardware rate out from under another app. The rates are now declared instead, through `JUCE_IOS_AUDIO_EXPLICIT_SAMPLERATES` in `CMakeLists.txt`, and the sweep never runs.
- **Session after device.** The category, mode, rate and buffer were applied *after* JUCE had opened the device, so the device was opened at whatever came out and then the session moved the hardware under it. `vp::prepareAudioSession` now runs first, and every write to the session is conditional: a rate or a buffer already correct is not written again.
- **Open, then reopen.** `setAudioChannels` opened the device and `setAudioDeviceSetup` immediately closed and reopened it at 48 kHz with a 256-frame buffer, whatever the interface had been on. The session is now settled first, so the open lands on the right clock and `applyAudioSetup` has nothing left to change: it only ever writes the channel fields for the one case that has to move them, the microphone granted after the device was opened without one. Any other write there makes the setup compare unequal and reopens the device for nothing.

## When the audio stops and does not come back

Reported on an iPad plugged into an X-Air over USB, with both clocks on the same
rate: the sound plays for a moment and then stops dead, and the only way to get
it back is to change the clock on the settings page - which reopens the device -
after which it lasts a few more seconds and stops again.

That the fix is *reopening the device* is the whole diagnosis. When iOS restarts
its media server, everything audio the process owns becomes invalid: the session,
the audio unit, all of it. Apple's answer is to build new ones. JUCE hears the
notification and answers it in `handleStatusChange`:

```
isRunning = enabled;
setAudioSessionActive (enabled);
AudioOutputUnitStart (audioUnit);      // the unit from before the reset
```

which starts a handle to something that no longer exists. The app goes silent and
stays silent until something makes it construct a new unit - and changing the
clock is the only thing in the app that did. A class-compliant USB interface is
one of the more reliable ways to provoke the reset in the first place, which is
why it shows up with the cable in and not without it.

Two answers, because one of them can be missed:

- `vp::setMediaServicesResetHandler` observes
  `AVAudioSessionMediaServicesWereResetNotification` directly and rebuilds:
  session first (a reset leaves it with none of the category, mode, rate or
  buffer that were set on it), then `closeAudioDevice()` and
  `restartLastAudioDevice()` - close, not reopen, because
  `setAudioDeviceSetup` keeps the device object and its dead unit.
- A watchdog on the message thread counts audio callbacks. A device that is
  supposed to be running and has not called back for a second is rebuilt the
  same way, whatever stopped it. There is a two-second floor between rebuilds:
  a rig that genuinely cannot hold a device open should not be rebuilt fifteen
  times a second.

The settings page counts them under **STATO / riavvii**. It should read 0. A
number that climbs on its own is a rig losing its audio device repeatedly, and
the rebuild is papering over it rather than fixing it.

The category is `AVAudioSessionCategoryPlayAndRecord` with `MixWithOthers`, `DefaultToSpeaker`, `AllowBluetoothA2DP` and `AllowAirPlay`. `MixWithOthers` is what lets the track being played along to keep playing; HFP Bluetooth is deliberately absent, because that route is 8-16 kHz and makes everything mixed through it sound slow and crushed.

Mode is `AVAudioSessionModeMeasurement` - no AGC, no noise suppression, no echo canceller between the room and the tracker. The settings page can put it back to `Default` (**INGRESSO / ELAB.**) for a route that misbehaves without iOS's processing.
