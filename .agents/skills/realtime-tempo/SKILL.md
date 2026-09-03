---
name: realtime-tempo
description: How BPM, beat phase and the bar are found and followed in real time in VirtualPercussionist - the BeatNet/ONNX worker, BeatDecoder, BeatTracker and the TempoFollower clock. Use when touching Source/AI/, Source/Tracking/, anything about tempo, lock, phase, latency compensation, octave (half/double time), which quarter is the one, tap tempo, or when a part drifts, doubles, jumps or enters on the wrong beat.
---

# Realtime tempo: how the app knows the BPM

Read this before changing anything in `Source/AI/` or `Source/Tracking/`. The
timing chain is measured, not guessed: every number below is in the code or in
`docs/`, and changing one without re-running the probes in the last section is
how this engine regresses.

## 1. The chain, and which thread each part runs on

```
mic / line in
  │  audio thread  (no alloc, no locks, no I/O, no ONNX)
  ├─ VirtualPercussionEngine   analysis bus, leak subtraction, make-up gain
  │      │
  │      ├──> SPSC FIFO ──> AI worker thread
  │      │                    LogSpectFeatures  22.05 kHz, 2048-pt log filterbank
  │      │                                      + flux -> 272-d, one frame / 20 ms
  │      │                    OnnxBeatModel     causal CRNN/TCN, softmax
  │      │                                      [p_beat, p_downbeat, p_none]
  │      │                    BeatDecoder       -> BeatHypothesis {bpm, phase, conf}
  │      │                    (published lock-free, ~6 Hz)
  │      │
  │      ├─ BeatTracker::process   reads the hypothesis, projects it to *now*,
  │      │                         state machine, bar votes, kick/harmony
  │      └─ TempoFollower::advance PLL clock, emits ClockTick pulses
  │                                (4 pulses per beat = 16ths)
  └─ PercussionEngine::render      strokes scheduled on those pulses
```

Hard rules, in `docs/ARCHITECTURE.md` and enforced by review:

- **ONNX never runs on the audio thread.** Ever.
- **The musical clock never leaves the audio thread.**
- The clock **never restarts** because the BPM changed. A tempo change is a
  rate change, never a re-anchor of the grid.
- UI reads a per-field `std::atomic` snapshot at ~15 Hz; UI never calls into the
  engine's process path.

## 2. Where the BPM number actually comes from

`Source/AI/BeatDecoder.h` - three tempo sources, because no single one is both
fast and precise:

| source | what it gives | what it costs |
|---|---|---|
| `TempoEstimator` - comb over the activation autocorrelation | robust metrical **level** (the octave), immune to missed and ghost peaks | averaged over seconds |
| least-squares fit over a **long** baseline of recent beat times | precision far finer than the 20 ms frame grid | slow to turn |
| the same fit over a **short** baseline | responsiveness | noisy |

Plus `BeatHmm`, a state space, which during acquisition names the octave
*seconds before* the comb can speak (`setLevelAnchor`). The fold and the state
space fail in opposite places: the fold reads eighths as the beat below ~100
BPM, the state space is dragged toward the middle of the range at the extremes.
So the anchor takes **only the octave** from the state space and keeps the
fold's precision.

**Regime** (`TempoRegime`, shown as CERCO / FISSO / VIVO) decides which of the
two fits drives the committed tempo: a record cut to a click is *fixed* and must
stop moving once found; a band on stage is *live* and must be followed. The two
are told apart by whether the short fit keeps agreeing with the long one.

Abrupt 5-10% steps use a separate bounded transition path: two completed causal
intervals must agree, and the first changed interval must differ by at least 3%
from the immediately preceding accepted interval. The edge requirement matters:
without it, a 4 s or 12 s ramp eventually moves far enough from a fixed
committed BPM to look like a step even though adjacent intervals never jumped.
Measured in `VPAlign`, the five protected line steps reach +/-1 BPM in
0.78-1.47 s and 23.3-24.4 ms phase error at the third beat, with one transition
and no pulse-count violations; both ramps stay on the ordinary live-fit path.

Autocorrelation runs on **activations**, never on the waveform. Volume-peak /
SuperFlux tracking is deliberately not used.

### The octave (half / double time) is partly unsolvable

Measured on BeatNet output from a 76 BPM mix with full eighths, the activation
half a beat off the beat stands at 0.73-0.77 of the beat's own, against
0.02-0.18 at 104 and 128 BPM. 152 is a *defensible* reading of what the network
was given. Moving thresholds to break that tie makes the aggregate worse,
because the same asymmetry is what stops an ordinary rock backbeat reading as
half-time. So: AUTO keeps the pulse inside the range a percussionist counts in
(`BeatTracker::updateAutoOctave`, `Source/Tracking/BeatTracker.cpp:573`). The
÷2/×2 controls were removed from the UI (item 15): the auto path is always on,
and ambiguous levels are fixed in the tracker, not by a manual override. Do not
try to "fix" the octave in the decoder.

## 3. Phase: the part that is easy to get wrong

The hypothesis describes audio that arrived *in the past*. Everything is
projected forward to now before it is handed to the clock. The projection term
is the same everywhere in `BeatTracker.cpp` (see `:1157` for the kick path,
`:1185` for harmony, and the neural path just above them):

```
leadSec = (numSamples - sampleOffset) / sampleRate      // block-relative age
        + reportedLatencyMs * 0.001f                    // measured device round trip
        + (neural path only) the network's own response trim
```

Then, and only then:

- `TempoFollower::setGridPhase(phase, tau)` - the ordinary case. **Seconds, not
  a per-block blend**: `advance()` is called once per audio callback, so a fixed
  blend made the time constant a function of buffer size - measured, the grid
  rate wobbled 4.2 BPM rms on a 64-frame buffer against 2.2 on 1024. `tau` must
  also stay longer than the decoder's ~6 Hz refresh, or the clock chases the
  decoder's uncertainty as if it were the band moving.
- `TempoFollower::snapPhase(phase, keepBarInStep)` - a re-anchor. Use only when
  the grid is genuinely somewhere else. `keepBarInStep` decides what happens to
  the *count* when the move crosses a beat boundary: **true while silent** (the
  bar the part will enter on has to be the song's bar), **false while sounding**
  (moving the count under a listener is "one, two, one" and is not worth a few
  milliseconds - `BeatTracker.cpp:1407` passes `! sounding` for exactly this).

**Never take the beat's position from a single onset.** See
`docs/CORE_TIMING_AUDIT.md`. The fits carry the phase through their *intercept*,
so one beat's timing error is averaged rather than handed over whole.

### The one exception: the kick channel

`Tracking/KickOnsetDetector.h` + `BeatTracker::notifyKickOnset`. A channel
carrying only the kick dates the beat to the sample instead of to a 20 ms frame.
It buys **precision, not a new reference** - the absolute calibration stays on
the neural path. Measured: phase error 17 ms rms -> 13 ms. It is gated by
`kickIsTrusted()`, a running share of strikes landing near a clock beat, so
somebody routing the full mix into that input fails it immediately and forever.

### Which quarter is the one

Two separate histograms, deliberately not summed
(`BeatTracker::alignBarFromVotes`, `:617`):

- `downbeatVotes[4]` from the network's `p_downbeat`, decayed;
- `harmonyVotes[4]` from `Tracking/HarmonicChange.h`, one vote per chord change,
  gated on `harmonicShare` (the material has to actually be harmonic).

They are different qualities of evidence: on a drum-free arrangement the harmony
is the *only* evidence, and measured there every single change landed on the
downbeat - where the network through an iPad speaker is no better than a coin.
Summing them would hide which one answered.

`barLocked` (SPOSTA L'1, or a tap that declares the one) stops all automatic
rotation. It does **not** freeze the count against the grid: a `snapPhase` with
`keepBarInStep` still carries it, which is what keeps a locked bar on the beat
of the song it was locked to.

## 4. The clock (`TempoFollower`)

A PLL on the audio thread. Two knobs behave differently and both matter:

**Rate glide** (`TempoFollower.cpp:423`). Acquisition and playing are different
jobs:

```
locked    : tau = 0.22 s if |err| <= 2.5 BPM, else 0.28 s
not locked: tau = 0.045 s if |err| <= 1.2 BPM, else 0.18 s
```

Sounding, the decoder's ~6 Hz refresh must not be heard as six tiny
accelerations a second. A real tempo move still crosses the wider branch and
closes in well under a second. Tempo is clamped 40..220 BPM and *settles*
(snaps) within 0.02 BPM so `currentTempo()` reads as a round number.

**Phase steering** by `FollowStrength` (`TempoFollower.cpp:524`, inside the
`locked` branch). The grid is never jumped: the rate is *bent* until the error
is closed, so the grid stays monotonic and no stroke is ever played twice or
skipped. It is what a player does - nobody moves their hand, they lean until
they are back with the band.

| | tau | steerLim | steerCeil | dGain |
|---|---|---|---|---|
| low | 1.60 | 0.018 | 0.10 | 0.3 |
| medium (default) | 0.90 | 0.035 | 0.18 | 0.8 |
| high | 0.70 | 0.050 | 0.25 | 1.2 |

The rate needed is *derived* from tau, not tuned per tempo - the same phase
error is a longer time at a slower tempo. History worth knowing: HIGH used to
use tau = 0.22 s (gain 2.3 at 120 BPM); against a decoder whose phase wobbles by
0.03 of a beat the grid sat pinned at the limit in both directions, +/-6 BPM at
120, 3.7 BPM rms - audibly running away and catching up.

**`setTempoTrust(t)`** stretches the glide in proportion when the beats the
tempo was fitted through are worse placed than this song's own
(`Tracking/PhaseTrust.h`). It is bounded (`kMinTempoTrust = 0.30`,
`kPoorEvidenceTauSec = 2.50`) and **is not a freeze**: `docs/STATUS.md` records
five attempts at holding the tempo instead, and all five cost half a bar on an
accelerando. A band speeding up with the drummer playing never drops below trust
1, so it is never slowed here at all.

**Lateness only.** `GrooveEvent::delayBeats` is always >= 0. The clock hands out
grid positions as they pass and there is no going back for one, so feel and
swing are expressed as lateness (see the percussion-patterns skill).

## 5. Two things that are about the room, not the tempo

- **The app finds a tempo in an empty room** - measured, 99 BPM at confidence
  0.91 with nobody in front of the mic. So `setInputEpoch` /
  `BeatDecoder::notifyInputRestart` throw the level-based evidence away when the
  input changes character, and the part is held out (`FollowBar::waitStart`,
  "ATTENDO CHE ATTACCHI") until the analysis has *ever* seen the input start.
  The committed tempo and `established` are deliberately kept so the clock does
  not stop. A track already playing when the app opened never "starts": one TAP
  releases it, and there is no timeout on purpose.
- **`notifyDiscontinuity(lostSeconds)`** is the other case: audio was lost, so
  the beat history is dropped and the timeline advanced - but the committed
  tempo, the regime and the metrical level are kept. A dropout is a reason to
  stop trusting recent evidence, not to forget the song.

## 6. State machine

```
LISTENING -> LOCKING -> FOLLOWING
FOLLOWING -> LOW_CONFIDENCE -> RECOVERING -> FOLLOWING
```

Analysis runs from launch, always. START arms; entry is quantized (MIXER on the
next reliable downbeat, IPAD on the next reliable beat, because a tablet speaker
does not carry enough bass for trustworthy downbeat votes). STOP mutes and keeps
following.

## 7. What we refuse to do

- Restart the loop or clock on a BPM change.
- Snap BPM from a single onset or a volume spike.
- Take the beat's *position* from a single onset.
- Quantize the drummer.
- Run ONNX on the audio thread.
- Allocate, lock, or do I/O anywhere in `process`/`advance`/`render`.

## 8. How to verify a change - do not skip this

```bash
./scripts/run-tests.sh                       # TAP suite, works without AI assets
cmake -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --target <target>
```

| target | source | question it answers |
|---|---|---|
| `VPTests` | `Tests/` | the TAP suite; `StubBeatModel` when no ONNX assets |
| `VPProbe` | `probe_song.cpp` | end-to-end on a full arrangement through a speaker into a room: lock time, drift, phase |
| `VPAlign` | `probe_align.cpp` | how long the clock takes to get onto the song, and how good the phase it is aiming at is |
| `VPBar` | `probe_bar.cpp` | does it know which quarter is the one |
| `VPTiming` | `probe_timing.cpp` | where the stroke actually **lands** in the rendered audio, not where the clock says |
| `VPRoom` | `probe_room.cpp` | empty room vs band |
| `VPActivations` | `probe_activations.cpp` | dumps the raw BeatNet curve; design against the real signal |
| `VPReplay` | `probe_replay.cpp` | replays a dumped activation file through `BeatDecoder` alone - a second per sweep instead of two minutes |
| `VPLive` | `probe_live.cpp` | the app against real band recordings with a real answer |
| `VPCpu`, `VPOps`, `VPSing`, `VPDecoderProbe` | | load, ops, sung input, decoder unit diagnostics |

Tuning loop for decoder work: dump once with `VPActivations`, then iterate with
`VPReplay`. The activations do not change when the decoder does.

`scripts/probe_tempo.cpp` has no CMake target of its own; build any probe source
ad hoc with `VP_STYLE_SRC=scripts/probe_tempo.cpp VP_PROBE_DIR=scripts` and the
`VPStyle` target (`CMakeLists.txt:690`).

Probes wait on `BeatTracker::analysisBacklog()` to make a run repeatable -
otherwise the host scheduler decides how far behind the worker is and the same
build measures differently run to run.

## 9. Map: "I want to change X"

| X | file |
|---|---|
| features fed to the network | `Source/AI/LogSpectFeatures.cpp` |
| model I/O, session, providers | `Source/AI/OnnxBeatModel.cpp`, `OnnxSession.cpp` |
| tempo sources, regime, octave anchor | `Source/AI/BeatDecoder.cpp` |
| the comb / metrical level | `Source/AI/TempoEstimator.cpp` |
| the state-space prior | `Source/AI/BeatHmm.cpp` |
| worker, FIFO, publication | `Source/AI/NeuralBeatTracker.cpp` |
| state machine, bar votes, kick/harmony fusion | `Source/Tracking/BeatTracker.cpp` |
| PLL glide, phase steering, snaps | `Source/Tracking/TempoFollower.cpp` |
| kick onset detection | `Source/Tracking/KickOnsetDetector.h` |
| chord-change detection | `Source/Tracking/HarmonicChange.h` |
| evidence quality / trust | `Source/Tracking/PhaseTrust.h` |
| analysis bus, leak subtraction, epochs | `Source/Audio/VirtualPercussionEngine.cpp` |

Background reading, in this order: `docs/BEAT_TRACKING.md` (short),
`docs/AI_BEAT_TRACKING.md` (the detail), `docs/CORE_TIMING_AUDIT.md` (why the
rules above exist), `docs/STATUS.md` (what has been measured, including the
failed attempts).
