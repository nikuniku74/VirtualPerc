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

### Where the beat is, as opposed to how far apart the beats are

The tempo comes from the sources below. The **phase** comes from the same fits —
the intercept of the least-squares line, read at the newest beat of its window —
and not from the last accepted peak, which is one measurement carrying that one
beat's whole error. Measured against 22 ms of onset jitter, taking it from the
last peak put 22 ms rms into the reported phase in steps of up to 0.18 of a
beat; off the fit it is 8 ms and there are no steps.

One thing a fit cannot fix, because it is fitted to the same beats: the gate
that decides whether a peak counts measures against the grid, so a grid that
once anchors on an offbeat is *stable* — every real beat then sits half a beat
off it and is discarded as a subdivision. Measured at 168 BPM with an eighth at
0.45 of the beat, the decoder reported 168.00 BPM, exactly right, half a beat
out, indefinitely. The activation folded onto the committed period is the only
measurement in the chain outside that loop, and folded onto the true period it
is tall on the beat and flat half a period later. `TempoEstimator::beatPhaseFor`
answers it; `BeatDecoder::checkGridPhase` moves the grid when the two disagree
by more than a fifth of a beat for three beats running, and stands down when the
fold is itself flat half a period away.

### Three tempo sources

No single measurement is both fast and precise, so each does one job.

| Source | Window | Job |
|---|---|---|
| `BeatHmm` | from the first frame | the level **during acquisition**, before the fold may speak |
| `TempoEstimator` | ~12 s of activation | which **metrical level** — the octave nothing else may leave |
| Least squares over 24 beat times | ~24 beats | **precision**, far finer than the 20 ms frame grid |
| Least squares over 8 beat times | ~8 beats | **responsiveness** when a player moves |

`TempoEstimator` scores candidate periods on the recency-weighted autocorrelation of the activation. That answers *how much pulse is at this period* — but not, on its own, which metrical level the music is on.

#### Autocorrelation cannot choose the octave

Measured on BeatNet activations from real material, the correlation at one, two, three and four beats:

| | 0.5× | 1× | 2× | 3× | 4× |
|---|---|---|---|---|---|
| 100 BPM, 16th hats | −0.10 | 0.85 | 0.97 | 0.84 | 0.96 |
| 120 BPM, rock | −0.18 | 0.74 | 0.78 | 0.82 | 0.94 |
| 168 BPM, rock | −0.26 | 0.87 | 0.80 | 0.78 | 0.86 |

A beat train correlates with itself just as well at any multiple of the beat. Anything that picks the level from correlation alone is really picking it from whatever tie-breaker sits behind it — and the tie-breaker used to be a Gaussian preference for 120 BPM, which hands the double a 46 % advantage at 60 BPM and the half a comparable one at 168. That is precisely the reported failure: **slow songs read double, fast songs read half, and neither reading holds still.**

#### The activation's amplitude can

Fold the activation onto a candidate period and compare the height half a period from its own peak against that peak, floor subtracted. Folded onto the true beat the curve is tall on the beat and flat half a beat later; folded onto twice the true period the two halves look alike, because the "empty" half is a full beat. Over 52 captures:

| folded at | median half-phase ratio |
|---|---|
| true beat period | **0.13** |
| twice the true period | **0.80** |

Both octave charges read that one number, which is what makes them symmetric — at the candidate it says *too slow*, at twice the candidate it says *too fast*. The tempo prior stays, widened and recentred, as a tie-breaker on genuinely ambiguous material rather than as the thing deciding the level.

Three details matter more than they look:

- **Candidate periods are real-valued.** 138 BPM is 21.74 frames at 50 fps. Scoring only whole-frame periods makes such a tempo slide a full beat across a 12 s window and lose to its own sub-harmonics — which is what used to report 138 BPM as 115.
- **The fold is weighted almost flat** while the autocorrelation stays recency-weighted. Which level the music is on does not change bar to bar; where the beats are does. Weighted alike, the fold at a slow tempo averages barely four beats and its ratio swings between 0.34 and 0.76 between refreshes — enough to flip the level indefinitely.
- **A harmonic the autocorrelation could not measure is not counted against a candidate.** Slow candidates run out of harmonics first, so charging them for the missing ones was itself an octave bias.

#### Hysteresis

Everything downstream is rebuilt when the level moves — the decoder's grid, its beat history, the clock's phase — and on ambiguous material the two levels trade the lead from refresh to refresh, so "whoever is ahead right now" is a coin toss repeated six times a second. A rival takes the level only by beating the incumbent by a margin and *keeping* it for ~2 s, or immediately if the incumbent stops being a peak at all. Before the buffer is long enough to fold the level an octave slower, that verdict is too weak to defend, so the requirement is brief instead. Measured over the captures, this took reported-level flips from **1590 to 15**.

All the end-to-end figures here come from a probe that drives the engine faster than real time while the neural worker runs on its own thread, so scheduling moves a few cases between runs — two runs of the same build gave 25/120 and 26/120 wrong octaves. Treat them as magnitudes, not exact counts.

The estimator also refuses to report a level whose *too-fast* charge could not be evaluated yet. Early on the buffer holds a few seconds, the slow half of the range is not searched at all, and the fastest candidate present wins by default with nothing slower to lose to — which is how a 68 BPM song used to be established at 136 in the first two seconds and defended ever after.

#### Acquisition: the state space speaks first

`TempoEstimator` reports nothing until its buffer holds five periods of the octave *below* its winner — ten beats of the tempo being played. Measured end to end that requirement **was** the time to lock, to a third of a second:

| BPM | ten beats | measured t_lock |
|---|---|---|
| 76 | 7.89 | 8.3 |
| 104 | 5.77 | 6.2 |
| 128 | 4.69 | 5.1 |
| 140 | 4.29 | 4.6 |

`BeatHmm` accumulates from the first frame and, on real activations, names the right level with a margin at 1.2–1.7 s — on 10 of 12 captures, the two failures being the same 76-read-as-152 the fold gets wrong too. So when the fold cannot speak yet and the state space is clear, the decoder acquires from the state space instead of waiting.

What it takes from it is the **level and a starting grid, not the number**: the state space's periods are whole frames, so it reads about 2 % sharp (120.0 for 118, 142.9 for 140), which is fine to lock to and not fine to play on. The least-squares fit owns the tempo from the fourth beat. The margin required to *acquire* is higher than the one required to fold the comb into the state space's octave (`kAnchorAcquireMargin` 4 against `kAnchorMargin` 2): folding is undone at the next refresh, acquiring is what the grid, the fits and the bar are then built on.

Its readiness is two periods of the **winning** tempo, not two of the slowest tempo in the space: the old rule cost 2.4 s whatever was playing.

Measured on the decoder alone at 140 BPM: first valid tempo at **0.88 s** with the state space against **4.30 s** with the fold alone.

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
- **An octave flipping under a working lock.** Re-anchoring on the comb is a leaky vote over the last few bars, not a run of consecutive beats: 4 beats of net disagreement while acquiring, 6 when live, 8 once fixed, plus 4 more while the current grid still lands tightly on the beats being detected. It is *not* gated on the comb's clarity, and a healthy grid does not veto it. Clarity is low exactly when the current grid is the rival, and the double of the true tempo lands on every detected peak too — so both of those, used as conditions, were the octave error protecting the octave error.

### Analysis level is part of the model's input

BeatNet's features are `log10(magnitude + 1)`. The `+ 1` knee means the level the analysis signal arrives at is not something the normalisation removes — madmom feeds the network integer-scaled audio, orders of magnitude above float `[-1, 1]`, and too quiet leaves the whole filterbank on the linear part of that knee. `VirtualPercussionEngine::applyAnalysisMakeup` therefore has two jobs, and the second one was missing.

Measured end to end — 30 songs, 60–176 BPM, four styles, counting how often the tracker settles on the wrong metrical level:

| target peak | 0.04 | 0.06 | 0.09 | 0.12 | 0.16 | **0.20** | 0.28 | 0.40 | 0.60 |
|---|---|---|---|---|---|---|---|---|---|
| wrong octave | 7 | 13 | 13 | 8 | 4 | **2** | 2 | 4 | 7 |

The old target of 0.12 sat on the near side of the optimum, and its failures were the half-tempo readings above 150 BPM and the double-tempo readings below 72.

The gain that gets there also has to *stay* there. It used to take the input peak instantly and release over half a second, so every drum hit dropped the gain and the next half second crept back up — moving the network's operating point on every beat. The envelope is slow in both directions now, primed from the first audio rather than crawling up to it, and the gain is ramped across the block so no edge is put into the analysis signal.

A user `inputGain` (0–2, default 1) scales the mixed analysis bus *before* leak subtraction and makeup. It does not touch the output. In SPEAKER/iPad-mic mode an ~80 Hz HPF on the same bus takes rumble off the mic; it is not applied to a mixer aux, where it would thin the kick.

### Own-part leak

The analysis bus always subtracts a delayed copy of our shaker/congas (`subtractSpeakerLeak`). The delay is searched around the device latency; in SPEAKER/iPad-mic mode the search also covers the extra acoustic hop (roughly 8–80 ms) so the canceller still finds the part when the room path is longer than the hardware figure. Makeup is applied after the subtraction.

Acquire timing (`LISTENING` 0.70 s + two beats, `LOCKING` ~0.16 s) is unchanged: shortening it locked a 78 BPM click a half-beat off. The decoder still uses the comb head-start for tempo; the clock still waits for a valid hypothesis before snapping phase.

### The room the app was listening to is not evidence

Every measurement above starts the music at sample zero, and that is the one case a device is never in: the app has been listening since it was opened, so by the time anybody plays, the analysis has been running for minutes on an empty room — and the guards inside `TempoEstimator` counted *frames*. Measured on the real network, forty seconds of room noise is enough for it to name a tempo with a salience of 0.29 and call the level **settled** a tenth of a second after the first beat, having examined none of it. `BeatHmm` is poisoned the same way: at the downbeat it already sits at 107 or 136 BPM with a margin of 2.7–8.0, and having a change penalty it then defends that against the music. End to end, a 128 BPM track behind twenty seconds of room locked at 150 BPM in the *fixed* regime and took 27 s to find 128, against 5.7 s from a cold start.

The make-up gain is what hides this from everything downstream — it exists to hold the analysis at one level — so the moment has to be found before it, on the level of the analysis bus itself. `VirtualPercussionEngine::updateAnalysisEpoch` counts a restart when that level rises by 18 dB **out of a state that was properly quiet** (24 dB below the loudest this input reaches, remembered for a minute). Both halves are needed: a rise on its own cannot tell a band starting from a chorus arriving or a breakdown ending. It is upwards only — a level that falls is a song ending or a quiet verse — and a rise our own part caused is excluded, because what we play comes back on the microphone and the canceller does not always find it.

The counter reaches the worker as `NeuralBeatTracker::setInputEpoch`, and `BeatDecoder::notifyInputRestart` starts the evidence again: the fold's counter, the state space, the beat history, the regime and `established`. The committed tempo is kept only as a number to move from. At the same moment the make-up gain is re-primed at the new level instead of gliding to it over its 0.8 s attack, and the network's recurrent state is reset — a cold start begins with it zeroed, and that is the condition every figure here was measured in.

What this does **not** do is stop the app locking to the room in the first place: it reaches FOLLOWING at 99 BPM, confidence 0.91, on a microphone hearing nobody. `VPRoom` measures the rules that could stop it and why none of them ships — the short version is that the quietest band the host tests require to lock is *quieter* than the room that fools the tracker (peak 0.0023 against 0.0060), so no threshold on level can work; the activation's floor separates the two in MIXER and fails in IPAD, where the room path closes the gaps between beats; and the network's own 0.40 gate separates them by 0.9 % against 1.5 %, with the room's share arriving in a burst in exactly the seconds the decision is made.

`VPProbe --pre <sec>` renders an empty room before the song and reports `rst`, the number of restarts. One per song is a set; more than one inside a song is the watcher being fooled, and the host tests assert both. `VPProbe --sync` waits for the analysis to finish each block before feeding the next: without it the same unchanged binary gave mean spans of 8.7, 9.2 and 12.1 BPM over three runs, a noise floor wider than most of the differences worth measuring.

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
