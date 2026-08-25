# Virtual Percussionist

Live virtual percussionist for **iPadOS**, from the same build also on **iPhone** and on an Apple silicon **Mac** ("Designed for iPad"). The app listens to an acoustic drummer and plays a shaker that follows tempo and phase — accelerando, rallentando, no loop restart.

Not on Apple Vision, deliberately: the analysis is tuned around a close kit mic and an iPad's own speaker into its own room, and a headset's array is neither. See [docs/PLATFORM.md](docs/PLATFORM.md).

Not a DAW. Not a BPM meter. Not a web app.

## MVP 1

Microphone → neural beat / tempo / phase → AUTO lock → adaptive shaker.

## Build (Mac host tests)

JUCE is a submodule, so a plain `git clone` leaves `third_party/JUCE` empty and
CMake fails at configure time. Once per checkout:

```bash
git submodule update --init --filter=blob:none third_party/JUCE
```

```bash
./scripts/setup-ai.sh   # ONNX Runtime + Assets/Models/beatnet.onnx
./scripts/run-tests.sh
```

Without AI assets, CMake still builds and TAP tests pass (`StubBeatModel`). Neural lock needs the setup step.

```bash
cmake -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --target VPTests
./build-host/VPTests_artefacts/Release/VPTests
```

### Linux host

`VPTests` is a console target, but JUCE builds `juceaide` before anything else
and that one links the GUI modules, so the X11 headers have to be there even
though nothing on this host opens a window:

```bash
sudo apt-get install -y libx11-dev libxrandr-dev libxinerama-dev \
    libxcursor-dev libxcomposite-dev libfreetype6-dev libfontconfig1-dev \
    libasound2-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target VPTests -j"$(nproc)"
./build/VPTests_artefacts/Release/VPTests
```

`gl`, `libcurl` and `webkit2gtk` are reported missing at configure time and can
stay missing: no target the tests need links them.

## iPad (iPad Air M1), iPhone, Mac

```bash
./scripts/setup-ai.sh          # ORT host+iOS + beatnet.onnx (skip se già presenti)
./scripts/configure-ios.sh
open build-ios/VirtualPercussionist.xcodeproj
```

In Xcode: Development Team, pick an iPad or an iPhone, microfono, Run. Stato attuale e limiti del modello: [docs/STATUS.md](docs/STATUS.md).

Everything in `docs/` was measured on an iPad. A phone runs the same engine and the layout is built for it, but no number here has been taken on one.

Simulator (no device signing):

```bash
./scripts/build-simulator.sh
```

## Live use

1. USB-C audio interface + kit mic into the iPad, or play a track from the iPad speakers (Spotify, etc.)
2. Mode **SPEAKER** (default on iPad) follows the sound in the room and ignores the shaker leaking back into the mic. **KIT MIC** is for a close kit microphone.
3. Play time — the app is already analysing, but the shaker stays muted until START
4. When the state shows **FOLLOWING**, press START; the shaker enters on the next downbeat
5. Speed up / slow down — it should follow without a restart
6. STOP mutes the shaker; it keeps listening. START arms it again and waits for the next downbeat — it does not start at the instant you tap the button.

The app is listening to the room long before anybody plays, and it will find a tempo in an empty room — measured, 99 BPM at a confidence of 0.91 with nobody in front of the microphone. So START arms the shaker but holds it silent until the analysis has heard the input actually *start*, and the state reads **ATTENDO CHE ATTACCHI** while it does. Press START early and the part comes in with the band, not before it.

One case it cannot tell apart: a track that was already playing when the app was opened never *starts*, so it waits. One **TAP** releases it (so does setting the tempo by hand with FISSO). There is no timeout on purpose: long enough to be a guard is long enough to be a nuisance, and short enough to tolerate brings back a shaker playing to an empty stage.

**SETUP** (top right) is everything you set once and never touch mid-song: the **clock** (AUTO, or 44.1 / 48 / 88.2 / 96 kHz), the buffer, MIXER vs IPAD, the theme, **CLICK TEST** and the debug panel — plus a read-out of the rate, buffer, latency and route the hardware actually gave.

Leave the clock on **AUTO** with a USB interface. AUTO means the interface holds the clock and the app opens at whatever it is already running at, so plugging into an X-Air at 48 kHz costs nothing: no click, and a track already playing through the same route keeps playing.

**CLICK TEST** (debug) injects an internal 120 BPM kit so you can verify the engine without drums.

## Docs

- [App status (install / AI / device)](docs/STATUS.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Neural beat tracking (ONNX)](docs/AI_BEAT_TRACKING.md)
- [Technical decisions](docs/TECHNICAL_DECISIONS.md)
- [Roadmap](docs/ROADMAP.md)
- [Percussioni intelligenti — gap analysis e piano](docs/SMART_PERCUSSION.md)
- [Audio engine](docs/AUDIO_ENGINE.md)
- [Beat tracking](docs/BEAT_TRACKING.md)
- [Audit del core — tempo, aggancio, salti](docs/CORE_TIMING_AUDIT.md)
- [Platform](docs/PLATFORM.md)
- [Test plan](docs/TEST_PLAN.md)
- [Licenses](docs/LICENSES.md)

JUCE is AGPLv3 or commercial. A closed-source App Store build needs a **JUCE commercial license**.

The percussion recordings in `Assets/Percussion/` are from the OLPC Berklee Sound Library under **CC BY 3.0**. That is fine commercially, but the attribution is a licence condition: it has to appear on a Credits screen in the shipped app. See [Assets/Percussion/ATTRIBUTION.md](Assets/Percussion/ATTRIBUTION.md).
