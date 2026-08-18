# Neural Beat Tracking (ONNX) + Time Stretch

Causal, on-device beat tracking in the style of BeatNet (CRNN + online decoder). This is the only analysis path. `TempoFollower` remains the musical clock.

## Why a neural net, and why not on the audio thread

BeatNet-class models hear beat / downbeat / non-beat from a log-spectrogram, not from volume peaks. That is what we want for guitar, piano, and mixed band.

ONNX Runtime (and LSTM/TCN forward) **must not** run inside `process()`. Allocations, CoreML/NNAPI, and unbounded kernels violate the audio-thread contract already documented in `docs/AUDIO_ENGINE.md`. The clock (`TempoFollower`) stays on the audio thread. The network runs on a dedicated worker.

```
CoreAudio / Oboe / JUCE callback
        │  planar float buffers
        ▼
VirtualPercussionEngine::process     (audio thread, no alloc)
        │  mix → mono
        ├─► NeuralBeatTracker.feed (SPSC)
        └─► TempoFollower (clock)
                    │
                    ▼
            Neural worker thread
              resample 22.05 kHz
              LogSpectFeatures (272-d, BeatNet)
              OnnxBeatModel (CRNN/TCN .onnx)
              BeatDecoder (causal peaks → BPM + phase)
                    │
                    ▼
            SeqLock BeatHypothesis
                    │
        audio thread: tryLoad()
        TempoFollower.setTargetTempo / observe phase
        StretchFactor.ratio = liveBpm / loopBpm
        TimeStretchEngine (Signalsmith or WSOLA)
```

## Model (BeatNet BDA, bundled)

Causal CRNN from Heydari et al. (ISMIR 2021), GTZAN weights (`model_1`). Export: `scripts/export_beatnet_onnx.py`.

| | Value |
|---|---|
| Sample rate | 22050 Hz |
| Window | 1411 (~64 ms); JUCE FFT 2048 mappata sulla griglia madmom 1411 |
| Hop | 441 (20 ms, 50 fps) |
| Features | 136 filtri unici da 24 bande/ottava (30–17000 Hz, clipped a Nyquist) + diff positiva `current - previous` = **272** |
| Output | logits `[p_beat, p_downbeat, p_none]` per frame |
| Streaming | LSTM hidden/cell `[2, 1, 150]` (`h0`/`c0` → `hn`/`cn`) |

Place the file at `Assets/Models/beatnet.onnx`. CMake embeds it as `VpBeatModel` / `beatnet_onnx`.

## Execution providers (mobile)

| Platform | EP | Notes |
|---|---|---|
| iPadOS | CoreML, then CPU | `OrtSessionOptionsAppendExecutionProvider_CoreML` |
| Android | NNAPI, then CPU | `OrtSessionOptionsAppendExecutionProvider_Nnapi` |
| Host tests | CPU | No CoreML/NNAPI |

Use **ONNX Runtime Mobile** (MIT). Link the C API (`onnxruntime_c_api.h`), not the C++ `Ort::Session` wrapper, so the same code builds as C++20 in `vp_core`.

CMake auto-enables ONNX when the **platform** SDK is present: host `third_party/onnxruntime`, iPad `third_party/onnxruntime-ios` (`./scripts/setup-ai.sh` or `fetch_onnxruntime.sh --ios`). Without that SDK, `StubBeatModel` keeps host tests green. TAP still works without a real model. iOS builds enable the CoreML EP (`VP_ORT_COREML`); CPU remains the fallback.

## Pulse tracking → stretch

The network outputs **activations**, not a playable clock. `BeatDecoder` uses `max(pBeat, pDownbeat)`, causal local maxima and the official 0.40 information gate, then combines three tempo sources into `{bpm, beatPhase, barPhase, confidence, regime}`. The audio thread copies that into `TempoFollower` (same PLL as DSP). Stretch is derived from the clock, never the other way around.

### Three tempo sources

No single measurement is both fast and precise, so each does one job.

| Source | Window | Job |
|---|---|---|
| `TempoEstimator` | ~12 s of activation | which **metrical level** — the octave nothing else may leave |
| Least squares over 24 beat times | ~24 beats | **precision**, far finer than the 20 ms frame grid |
| Least squares over 8 beat times | ~8 beats | **responsiveness** when a player moves |

`TempoEstimator` scores candidate periods on the recency-weighted autocorrelation of the activation. A candidate collects its own harmonics and is charged for anything strong **inside** its period, which is what brackets the octave from both sides: the true period finds nothing between its beats, while a candidate at twice the true period finds a full pulse there. Two details matter more than they look:

- **Candidate periods are real-valued.** 138 BPM is 21.74 frames at 50 fps. Scoring only whole-frame periods makes such a tempo slide a full beat across a 12 s window and lose to its own sub-harmonics — which is what used to report 138 BPM as 115.
- **The whole interior is searched, not a fixed set of fractions.** For any fixed set there is a multiple of the true period — one and a half beats, two and a half — whose skipped beats all fall between the fractions tested.

### Fixed vs live tempo

A record cut to a click does not change tempo; a band on stage does. `BeatDecoder` decides which it is from whether the short fit keeps agreeing with the long one, and reports it as `TempoRegime`.

| Regime | Committed tempo | Why |
|---|---|---|
| `fixed` | refined towards the long fit, capped at 1.5 % per beat | a Spotify track must stop moving once found |
| `live` | short fit, extrapolated forward by the short/long gap | every fit lags a player mid-accelerando |
| `unknown` | acquiring | adopt the comb outright rather than easing from 120 |

`fixed` is deliberately stubborn: it is released by three recent intervals agreeing on a direction, which arrives well before the eight-beat fit notices, and it is what keeps a drum fill from dragging the tempo. It cannot be entered while `TempoEstimator` still names a different level, so a bad first guess cannot make itself permanent.

### Two things that read as "it is not following the beat"

Both were real and both are guarded now:

- **Subdivisions taken as beats.** A hi-hat puts an activation peak halfway between every pair of beats. Once a tempo is established a peak must land within 0.18 of a beat on the grid to count, which is tighter than the sixteenths and triplets it has to turn away. Without that the phase reference moves half a beat back and forth and the clock never settles. If nothing lands on the grid for 2.5 beats the grid itself is wrong — a new song, an edit — and the next peak re-anchors it.
- **An octave flipping under a working lock.** Re-anchoring on the comb needs 4 beats of confident disagreement while acquiring, 8 when live, and 16 once fixed — and is refused outright while the current grid still lands tightly on the beats being detected, because the double of the true tempo always does too.

### Latency

`beatPhase` from the worker describes audio that has already been played. `NeuralBeatTracker` timestamps each hypothesis with the input sample its frame was centred on (including anything `AudioFifo` dropped), `BeatTracker` turns the gap to `samplesFed()` plus the reported output latency into a lead in beats, and every phase target handed to `TempoFollower` refers to the current acoustic moment. Measured end to end on a click track: ~68 ms of lead, leaving a residual offset of 14–19 ms — a constant, not a rate error.

The Python BeatNet package follows the network with a 1500-particle Monte Carlo filter. That complete filter is not embedded here; the C++ mobile decoder is deterministic and lighter. `AI ONNX` means the official neural network is loaded, not that the Python particle filter is present.

```
ratio = clamp(liveBpm / loopNativeBpm, 0.5, 2.0)
ratio *= 1 + kPhase * wrapCentered(liveBeatPhase - loopPhase)
ratio = one-pole(ratio)          // no clicks on BPM jitter
```

Changing BPM never resets the loop playhead (`TD-03` spirit: no restart).

## Time-scale (TSM)

| Library | License | App Store | Choice |
|---|---|---|---|
| **Signalsmith Stretch** | MIT | yes | **preferred** (`-DVP_USE_SIGNALSMITH=ON`) |
| Built-in WSOLA | ours | yes | default so the tree compiles without vendoring |
| SoundTouch | LGPL | problematic | **do not add** |
| Rubber Band | GPL | no | **do not add** |

Signalsmith: `process(inputPtrs, inCount, outputPtrs, outCount)` with `inCount = outCount * ratio` and transpose factor `1` (pitch locked). Vendor the single header under `third_party/signalsmith-stretch/` when enabling the flag.

Shaker **grains** (`PercussionEngine`) remain the default live instrument. TSM is for **loop kits** only.

## Native I/O

iPadOS already reaches Core Audio through JUCE `AudioAppComponent::getNextAudioBlock` → `VirtualPercussionEngine::process` (planar). Android (MVP 6) uses the same `process()` from an Oboe `onAudioReady`. `NativeAudioBridge` deinterleaves device buffers into that API. Do not put ONNX, files, or stretch setup inside the platform callback.

## Action plan (implementation order)

1. Feature extractor + decoder + stretch factor + WSOLA — host tests, no ONNX.
2. `OnnxSession` C API + CoreML/NNAPI options behind `VP_USE_ONNX`.
3. Export BeatNet BDA (GTZAN weights) to `Assets/Models/beatnet.onnx` via `scripts/export_beatnet_onnx.py`.
4. Host: `./scripts/setup-ai.sh` then rebuild — fetches ONNX Runtime and embeds the model.
5. Device A/B on Spotify / kit / piano (SPEAKER, CLICK TEST off).
5. Optional: vendor Signalsmith; load a loop kit.
6. Android: Oboe callback → `NativeAudioBridge` (MVP 6).

## Files

| Path | Role |
|---|---|
| `Source/AI/BeatHypothesis.h` | SeqLock result |
| `Source/AI/AudioFifo.h` | SPSC float ring (drop-oldest) |
| `Source/AI/IBeatModel.h` | `infer()` interface |
| `Source/AI/LogSpectFeatures.*` | 272-d BeatNet features |
| `Source/AI/OnnxSession.*` | ONNX Runtime C API |
| `Source/AI/OnnxBeatModel.*` | activations from `.onnx` |
| `Source/AI/StubBeatModel.*` | tests / builds without ORT |
| `Source/AI/TempoEstimator.*` | activations → metrical level (comb over the autocorrelation) |
| `Source/AI/BeatDecoder.*` | activations → BPM/phase/regime |
| `Source/AI/NeuralBeatTracker.*` | audio FIFO + worker |
| `Source/Stretch/StretchFactor.*` | ratio from clock vs loop |
| `Source/Stretch/TimeStretchEngine.*` | WSOLA / Signalsmith |
| `Source/Platform/NativeAudioBridge.*` | interleaved → `process()` |
| `Tests/TestAiBeat.cpp` | host tests (no model file) |
