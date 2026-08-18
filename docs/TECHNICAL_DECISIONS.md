# Technical Decisions

Decisions not specified in the master prompt. Chosen for musicality, stability, then latency.

## TD-01 — JUCE 8.0.15, CMake, one C++ app

JUCE is the audio/DSP/UI framework for iPadOS. One language, real-time audio devices, iOS session/permissions, USB route changes. SwiftUI would force a JNI/ObjC bridge around the DSP core for no live benefit.

Android will reuse `vp_core` and a native audio callback (Oboe). JUCE CMake does not fully support Android; that port is MVP 6 via Projucer/Gradle around the same core.

## TD-02 — JUCE GUI on iPad, not a separate native UI

Live UI is simple (BPM, state, shaker, follow, start/stop). JUCE Components are enough and keep the UI in-process with the engine. A later native UI can sit on the same `VirtualPercussionEngine` API.

## TD-03 — Sample performance, not loop timestretch (MVP 1)

A shaker that follows accelerando must change **when** hits occur, not stretch a loop. Loops restart or smear under tempo change. Hits on a 16th grid from the PLL clock stay continuous and musical. Time-stretch remains available later for loop kits (JUCE `TimeSlice` / `AudioTransportSource` is not suitable; we would add a dedicated stretcher only if needed).

## TD-04 — Procedural shaker sample

No third-party sample pack. A short band-limited noise grain is synthesized at `prepare()`. Original, licensable, no I/O on the audio thread. WAV/AIFF import is a later kit feature.

## TD-05 — Neural beat tracking + PLL clock, not a volume-peak detector

Beat / downbeat activations come from the causal BeatNet CRNN in ONNX. A deterministic C++ decoder combines beat/downbeat activations, detects causal local maxima, and uses multi-interval tempo consensus before handing BPM/phase to `TempoFollower`. The official Python Monte Carlo particle filter is not embedded. `TempoFollower` remains the hysteresis-limited musical clock and is never restarted on BPM change. SuperFlux / lag-ACF onset tracking remains removed.

## TD-06 — ONNX on a worker thread; clock on the audio thread

The audio callback only mixes input, pushes mono into a lock-free FIFO, reads a seqlock `BeatHypothesis`, and advances the PLL. Log-spect, resample, and `OrtRun` run on the AI worker. Callback duration is still measured and shown in debug.

## TD-07 — Host tests on macOS, product on iPadOS

The spec forbids a macOS product. A console test binary and the same GUI compiled for Mac exist only to run DSP tests and iterate without a device. They are not shipping artifacts.

## TD-08 — No extra DSP libraries (MVP 1)

JUCE `juce_dsp` covers FFT, windows, and filters. Adding KissFFT, SoundTouch, or Rubber Band would add license and iOS complexity without a measured need.

## TD-13 — Neural beat tracking is the analysis path

BeatNet-class ONNX (see `docs/AI_BEAT_TRACKING.md`). Started in `BeatTracker::prepare()`. There is no DSP fallback. Until a `.onnx` is bundled, `StubBeatModel` stands in so the tree still builds.

## TD-14 — ONNX Runtime Mobile (MIT) only, C API

When the iOS xcframework is present, link ONNX Runtime via the C API and enable the CoreML EP (`VP_ORT_COREML`), then CPU. Host tests use the official macOS dylib. Do not call `OrtRun` from `process()`. Without the SDK, `StubBeatModel` keeps the tree building.

## TD-15 — Time stretch for loop kits: Signalsmith (MIT) or built-in WSOLA

Loop kits may time-stretch with pitch locked. Preferred library: Signalsmith Stretch (MIT, `-DVP_USE_SIGNALSMITH=ON`). Default build uses a built-in WSOLA so we do not vendor a third-party header yet. SoundTouch (LGPL) and Rubber Band (GPL) remain forbidden (`docs/LICENSES.md`). Shaker grains (`TD-03`) stay the default live voice.

## TD-09 — JUCE license

JUCE is AGPLv3 or commercial. A live/closed-source App Store app requires a JUCE commercial license before distribution. Splash is disabled because it is unacceptable on a live stage; that implies a commercial JUCE license for shipping. See `docs/LICENSES.md`.

## TD-10 — Shaker grid vs subdivision

The shaker plays 16ths at the followed tempo unless the user thins it to 1/8 or 1/4. AUTO maps to 16ths.

## TD-11 — Minimum iPadOS 16, iPad family, arm64

iPad Air M1 is the first device. arm64 only. Simulator builds use `iphonesimulator`.

## TD-12 — Follow strength maps to PLL / tempo time constants

| | Tempo smoothing | Max phase correction |
|---|---|---|
| LOW | ~4 s | small |
| MEDIUM | ~1.5 s | medium |
| HIGH | ~0.6 s | larger, still clamped |

Never a hard jump from a single onset.
