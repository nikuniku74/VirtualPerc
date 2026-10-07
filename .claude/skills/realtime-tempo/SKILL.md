---
name: realtime-tempo
description: How BPM, beat phase and the bar are found and followed in real time in VirtualPercussionist - the BeatNet/ONNX worker, BeatDecoder, BeatTracker and the TempoFollower clock. Use when touching Source/AI/, Source/Tracking/, Source/Audio/, anything about tempo, lock, phase, latency compensation, octave (half/double time), which quarter is the one, tap tempo, or when a part drifts, doubles, jumps, crackles or enters on the wrong beat.
---

# Realtime tempo: how the app knows the BPM

Read this file in full before changing anything in `Source/AI/`,
`Source/Tracking/` or `Source/Audio/`. It is the map and the rules. The detail,
including every measured kept/rejected experiment, lives in `references/` -
open only the file for the part you are touching, and grep it for the symbol
before changing that symbol: most "new" ideas here have already been tried and
measured.

| reference | read it when you touch |
|---|---|
| `references/tempo-sources.md` | `BeatDecoder`, `TempoEstimator`, `BeatHmm`: acquisition, regimes FISSO/VIVO, steps, ramps, holes, octave (3,500 lines - grep, don't read) |
| `references/phase-and-bar.md` | latency projection, kick channel, the one (bar votes, SPOSTA L'1, L'1 è QUI), cuts, seeks, new input |
| `references/clock.md` | `TempoFollower`: rate glide, phase steering, snaps, `glidePhase` |
| `references/room-and-epochs.md` | empty-room lock, `updateAnalysisEpoch`, discontinuities, `subtractSpeakerLeak` |
| `references/state-machine.md` | LISTENING/LOCKING/FOLLOWING, entry gates, background audio |
| `references/verification.md` | every probe and test target, and the per-change measurement log |

## 1. The chain, and which thread each part runs on

```
mic / line in
  │  audio thread  (no alloc, no locks, no I/O, no ONNX)
  ├─ VirtualPercussionEngine   analysis bus, leak subtraction, make-up gain
  │      │
  │      ├──> SPSC FIFO ──> AI worker thread
  │      │                    LogSpectFeatures  22.05 kHz, 2048-pt log filterbank
  │      │                                      + flux -> 272-d, one frame / 20 ms
  │      │                    OnnxBeatModel     causal BeatNet (fine-tuned), softmax
  │      │                                      [p_beat, p_downbeat, p_none]
  │      │                    BeatDecoder       -> BeatHypothesis {bpm, phase, conf}
  │      │                    (published lock-free, ~6 Hz)
  │      │
  │      ├─ BeatTracker::process   projects the hypothesis to *now*, state
  │      │                         machine, entry gate, bar votes, kick/harmony
  │      └─ TempoFollower::advance PLL clock, emits ClockTick pulses
  │                                (4 pulses per beat = 16ths)
  └─ PercussionEngine::render      strokes scheduled on those pulses
```

## 2. Rules that are never traded

- **ONNX never runs on the audio thread. The musical clock never leaves it.**
- No allocation, locks or I/O in `process` / `advance` / `render`.
- UI reads a per-field `std::atomic` snapshot at ~15 Hz; it never calls into the
  process path.
- **The clock never restarts because the BPM changed.** A tempo change is a rate
  change, never a re-anchor of the grid.
- **Never take tempo or beat position from a single onset or volume spike.** Fits
  carry phase through their intercept so one beat's error is averaged.
- Every piece of evidence is projected to "now" before it reaches the clock:
  `(numSamples - sampleOffset)/sampleRate + reportedLatencyMs*0.001`
  (+ the network's response trim on the neural path). A new source is
  projected the same way.
- Time constants are in **seconds**, never a per-block blend (a blend makes the
  constant depend on buffer size - measured).
- **The octave never changes under a sounding part** automatically. It is chosen
  before entry; ÷2/×2 and TAP are the manual way out.
- A sounding phase correction is a **glide** (`TempoFollower::glidePhase`), never
  a snap that skips or repeats a sixteenth - snaps were the audible "crack".
- Bar count: a trusted one is moved only with strong evidence (network alone at
  `kBarNetAloneMargin`, or network + harmony agreeing). `barLocked` (SPOSTA L'1 /
  a tap on the one) stops all automatic rotation.

## 3. How the decoder works, in one page

`BeatDecoder` turns network peaks into a grid. No single source is both fast
and precise, so there are several:

| source | gives | costs |
|---|---|---|
| `TempoEstimator` comb (the "fold") over activation autocorrelation | the metrical **level** (octave); immune to missed/ghost peaks | averaged over seconds |
| least-squares line over a **long** baseline of accepted beats (`kLongFit` = 24) | precision well under the 20 ms frame | slow to turn |
| the same line over a **short** baseline (`kShortFit` = 8; plus IOI-indexed 4-beat fits for steps) | responsiveness | noisy |
| `BeatHmm` state space | the octave seconds before the comb can speak, during acquisition only (`setLevelAnchor`) | dragged to mid-range at extremes |
| `HarmonicChange` | phase/bar on drum-free tonal material | slow: needs 8 chord changes |

- **Regime** (`TempoRegime`, CERCO / FISSO / VIVO): a record cut to a click is
  *fixed* and must stop moving; a band is *live* and must be followed. Told apart
  by whether the short fit keeps agreeing with the long one.
- **Steps**: interval path (two agreeing causal intervals), `observeGridStep`
  (2-4 quarters leaving the 8-beat line by `k*step`), wide non-octave steps on a
  line feed. Under a sounding part on a direct feed every step also needs the
  comb to agree (`stepLacksComb`).
- **Published phase under a sounding part**: the line through the newest
  `kPhaseLineBeats` (6) accepted beats, direct feed, FISSO/VIVO only.
- **Entry**: the part enters only when decoder tempo and comb agree within
  `kEntryCombAgree` (3%, ×2/÷2 allowed), capped at `kEntryCombWaitSec` (12 s);
  exempt with TAP, user tempo, follow off, harmony.
- **Octave**: partly ambiguous, and for straight slow grooves with hat eighths
  (50 vs 100) **acoustically undecidable** - same sound as half-time at 100.
  AUTO keeps the pulse in a percussionist's range (`updateAutoOctave`). A dense
  grid is not a right grid (hats fill the eighths: coverage 1.00 at the wrong
  octave); `recentStrengthAlternation` is the one cue that separates them.
  On the **fine-tuned** network the activation half a beat off is 0.00-0.03 of
  the beat's on most songs (VIVERE 0.55 is the genuinely split one): most wrong
  octaves are now the decoder committing before that evidence arrives.
  Read `references/tempo-sources.md` "The octave..." before touching any of it.
- **Intro lock at the band's entrance**: a cold epoch before the part has ever
  played drops the lock held from the intro, so the part waits for the band's
  own (`BeatTracker::setInputEpoch`, `staleIntroLock`, TODO item 96; held-out
  MIXER right tempo +4.7).

## 4. Where the precision goes (measured 2026-10-02/06, items 87, 90, 100)

Against the offline teacher (Beat This!) on the real-song bench:

| stage | scatter ms | > 25 ms |
|---|---:|---:|
| drummer vs own previous 8 beats (floor) | ~7 | ~5% |
| raw BeatNet peaks | 7.2 | 6.8% |
| causal line over last 8 peaks | 10.2 | 13.8% |
| clock after phase line + glide (current) | ~11.8 | ~18.7% |

Decisions, current model (2026-10-06, 54 files): right tempo/octave 81.6% of
beats on loaded files and 81.0% on the MIXER at -12 dB (1.2.6: 78.3 / 76.7%),
right one 69.5 / 75.7%, scatter 12.1 / 11.8 ms, beats > 25 ms 19.3 / 18.6%,
rate surges 0.18 / 0.24 per minute; no drift between the first and the last
quarter of a song (median < 1 ms). **Phase is near the network's ceiling; the open problems
are decisions (octave, the one, entry), not precision.** Any threshold measured
before 2026-10-02 was measured on the old network's activations.

## 5. Things we refuse to do

- Restart the loop or clock on a BPM change.
- Snap BPM from a single onset or a volume spike.
- Tune a residual, vote count, BPM band, timeout or exception from one
  recording. Flamingo, Sally, INFINITO, dip and rall4 are witnesses of the
  product, not knobs. A candidate that only helps one title is rejected; the
  bench decides whether a rule is global - **on files it was not derived
  from**: leave the songs that motivated a rule out and keep it only if it
  clearly moves that held-out half (items 97-99 failed this and were removed,
  item 100).
- Take the beat's *position* from a single onset (dating beats on the mix's
  nearest attack was measured worse in 5/6 songs, items 70 and 86).
- Quantize the drummer.
- "Fix" the 50/100 octave from the audio (three attempts, all documented).
- Run ONNX on the audio thread; allocate, lock or do I/O in the audio path.

## 6. How to verify - do not skip

Use the `tempo-bench` skill for the procedure. In short: a timing change is
judged on the whole real-song bench against the teacher's truth, plus the
regression gates, and reported as a before/after table. A change with no probe
number attached, or a musical change nobody has listened to, is not reviewable.

```bash
./scripts/run-tests.sh                                   # TAP suite (StubBeatModel without ONNX)
build-host/VPTests_artefacts/Release/VPTests --bar       # also: --phase-lock --state-timing
                                                         # --tempo-step --tempo-slow --new-input --transport
python3 scripts/analysis/bench_fast.py run TAG [VAR=VALUE...]
python3 scripts/analysis/bench_fast.py cmp BASE TAG
python3 scripts/analysis/truth.py score BASE TAG
```

The full target list (`probe_matrix`, `VPAlign`, `VPBar`, `VPTiming`, `VPProbe`,
`VPRoom`, `VPReplay`, `VPLive`, `VPTrack`, `VPActivations`, `VPCpu`...) with what
each one answers is in `references/verification.md`.

## 7. Map: "I want to change X"

| X | file |
|---|---|
| features fed to the network | `Source/AI/LogSpectFeatures.cpp` |
| model I/O, session, providers | `Source/AI/OnnxBeatModel.cpp`, `OnnxSession.cpp` |
| tempo sources, regime, octave anchor, steps, published phase | `Source/AI/BeatDecoder.cpp` (6,000 lines) |
| the comb / metrical level | `Source/AI/TempoEstimator.cpp` |
| the state-space prior | `Source/AI/BeatHmm.cpp` |
| worker, FIFO, publication | `Source/AI/NeuralBeatTracker.cpp` |
| state machine, entry gate, bar votes, kick/harmony fusion, AUTO octave | `Source/Tracking/BeatTracker.cpp` |
| PLL glide, phase steering, snaps, glides | `Source/Tracking/TempoFollower.cpp` |
| kick onset detection | `Source/Tracking/KickOnsetDetector.h` |
| chord-change detection | `Source/Tracking/HarmonicChange.h` |
| evidence quality / trust | `Source/Tracking/PhaseTrust.h` |
| analysis bus, leak subtraction, epochs, make-up gain | `Source/Audio/VirtualPercussionEngine.cpp` |
| the network's weights and training | `Assets/Models/beatnet.onnx`, `scripts/train_beatnet_finetune.py` |

Background: `docs/BEAT_TRACKING.md` (short), `docs/AI_BEAT_TRACKING.md`,
`docs/CORE_TIMING_AUDIT.md` (why the rules exist), `docs/TODO.md` (open items:
read the item you are working on, not the whole file).

## 8. Keeping this skill useful

Keep this file short: rules, the model of how it works, current numbers, map.
New measurements and kept/rejected experiments go in the matching
`references/` file (and the TODO item), not here. If a rule or a number in this
file changes, change it here in the same commit.
