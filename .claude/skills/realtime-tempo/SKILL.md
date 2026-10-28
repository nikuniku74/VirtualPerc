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
- **FISSO (`! tempoFollow`) listens to nothing** (item 153): no hypothesis
  reaches the tracker, the clock runs free at the listener's tempo, the part
  plays from START like START SUBITO, trust is always `sure`, DINAMICA and
  the drift guard are off. The network is still fed so SEGUI comes back warm.
  The audio thread never writes `userBpm`; it publishes `appliedUserBpmGen()`.
- **The octave never changes under a sounding part** automatically. It is chosen
  before entry; ÷2/×2 and TAP are the manual way out.
- A sounding phase correction is a **glide** (`TempoFollower::glidePhase`), never
  a snap that skips or repeats a sixteenth - snaps were the audible "crack".
- Bar count: a trusted one is moved only with strong evidence (network alone at
  `kBarNetAloneMargin`, or network + harmony agreeing). `barLocked` (SPOSTA L'1 /
  a tap on the one) stops all automatic rotation.
  The network's downbeat votes forget at `kVoteDecay` 0.95 per beat (half in
  ~3.5 bars, item 106): the app was already at the best fixed one-per-song,
  and the true one moves mid-song; 0.982 kept it wrong for ten bars.
  A half-bar move under a sounding part must also win 8 beats running
  (`kHalfBarPersistBeats`, item 121): half the breaks of a right one.

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
- **Stale pulse across a song change** (item 155): on direct feeds, a clean
  held fit that is starving can be challenged by eight coherent *ungated*
  completed peaks agreeing with the settled comb. For a damaged fit, item156
  requires sixteen and a wide non-octave step (existing0.25log2 boundary);
  small moves keep ordinary motion recovery. Retain their fitted history
  and the comb, emit a tempo transition without a grid re-anchor, and classify
  the new regime from unknown (new tempo is not proof of continuing motion).
  Later small grid/re-entry corrections glide across pauses of the same source;
  explicit replacement/mode change clears that policy. No title/BPM exceptions.
- **Ramps**: followed by the short fit, the direct-feed motion evidence that
  releases FISSO (`motionFit*`, `fastTempo*`) and the IOI lead (`ioiLead`). The
  residual-shape bridge (`TempoMotionTracker`) was removed (item 94: active
  0.15% of the time, real-song metrics unchanged without it).
- **Published phase under a sounding part**: the line through the newest
  `kPhaseLineBeats` (6) accepted beats, FISSO/VIVO only - on the iPad
  microphone too since item 130 (speaker bench 24.2 -> 18.6 ms; there the trim
  is off, so this is what closes a committed tempo a hair slow).
- **Entry**: the part enters only when decoder tempo and comb agree within
  `kEntryCombAgree` (3%, ×2/÷2 allowed), capped at `kEntryCombWaitSec` (12 s);
  exempt with TAP, user tempo, follow off, harmony. And only once the
  confidence has **settled**: >= `kEntryConfSettled` (0.60) for 4 s, or 6 s
  ready for everything else (item 129: first four strokes right 26 -> 36 of
  54 BRANO, 33 -> 36 MIXER; +1.3 s median). And the **first** entry of a song
  waits (8 s at most) while the activation folded onto the tempo stands as
  high half a beat off as on it (`BeatHypothesis::halfBeatContrast` > 0.70,
  `HalfLevelCheck`): that tempo is half the pulse, and AUTO doubles it
  *before* the part plays (item 147: unseen songs read at half - SEX BOMB,
  CELEBRATION, I FEEL GOOD - come in at the pulse; right tempo +1.7 / +2.2 /
  +2.6 MIXER / BRANO / IPAD, no file worse).
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

## 4. Where the precision goes (measured 2026-10-02/08, items 87, 90, 100, 94)

Against the offline teacher (Beat This!) on the real-song bench:

| stage | scatter ms | > 25 ms |
|---|---:|---:|
| drummer vs own previous 8 beats (floor) | ~7 | ~5% |
| raw BeatNet peaks | 7.2 | 6.8% |
| causal line over last 8 peaks | 10.2 | 13.8% |
| clock after phase line + glide (current) | ~11.8 | ~18.7% |

Historical decisions (2026-10-08, 54 files): right tempo/octave 81.7% of
beats on loaded files and 81.1% on the MIXER at -12 dB (1.2.6: 78.3 / 76.7%),
right one 75.0 / 78.8% (item 106; 69.5 / 75.7 before), scatter 12.1 / 11.8 ms, beats > 25 ms 19.3 / 18.4%,
rate surges 0.18 / 0.23 per minute; no drift between the first and the last
quarter of a song (median < 1 ms). **Phase is near the network's ceiling; the open problems
are decisions (octave, the one, entry), not precision.** Any threshold measured
before 2026-10-02 was measured on the old network's activations.

The current 56-file song bench after items155/156 (2026-10-23) reads right tempo
78.83% BRANO / 86.55% MIXER (item155:78.80/86.55; its own baseline78.54/86.34);
scatter 13.99 / 12.65 ms versus 13.98 / 12.64. The corpus/aggregation differs
from the historical 54-file figures above: compare matching runs, not dates.
Whole sets, 40 additional files and a new 44-minute recording are included in
`references/verification.md` / TODO155; 301 paired probes, no file loses correct
tempo share and no per-file skipped-sixteenth increase. Item156 adds24fresh pairs
(325total), confirms322identical/3improved, and checks long-set lost-correct
sections as well as aggregates; see verification.md. Hardware listening remains for those earlier timing candidates. TODO158 adds
a 40ms output fade and conga balance/routing fixes:325 paired traces,312
identical,13 tiny numerical differences within the regression gates; no
acquisition speedup claimed. A physical iPad microphone/speaker trial confirms
real ONNX operation and the listener hears timing and more present congas.
TODO159 separates the pre-master own-onset witness from the post-master echo
reference: a constant MASTER position must not change source epochs. Later
MASTER increases rebase that witness to preserve the existing return veto.
See room-and-epochs.md; room refit candidates were rejected for real coverage
loss despite synthetic improvements. No new room-acquisition speedup claimed.

TODO160 keeps three narrower corrections: manual range-edge **quality** may
be valid while its out-of-range period remains rejected; a confirmed acoustic
refit is protected only before the decoder has ever sounded; the echo search
correlates coarse envelope cells rather than absolute carrier samples.
The325 paired bank has280 exact traces, including **all229 MIXER/BRANO**;
45 room traces have tiny differences with no regression-gate failure. All96
room results match the echo-only ablation. No real-song acquisition speedup
is claimed. Harmonic and articulation-calibration candidates were rejected
despite green synthetic tests: those two limits remain open. See TODO160 and
the matching references for before/after measurements. Final default suite:
756/2 of758; the two FAIL names are unchanged. Hoisting the invariant input
cells removes repeat work with325/325 outputs identical to the functional
version and17,408 bit-identical scalar checks. Host timings are not an iPad
thermal/audio validation.

TODO162 (2026-10-26) closes the two original remaining FAIL names: default
full-wake suite764/0 (756/2 before, six new assertions). Audible attacks are
calibrated per layer/RR while a shared nominal control bus keeps epochs/AGC
independent. Soft/medium48-case spread12.625/13.521→0ms; hard0.9 is0.77ms.
The clock lead stays17.854ms; prepared-asset target adds2.729ms heard margin,
with no future input. Harmonic memory resets on explicit new source/reset;
direct-feed acquisition averages the confirming chroma observations while a
trusted count and speaker keep the conservative reference. Synthetic full-mix
one19→42%, sparse100% unchanged. Final325:264 exact,61 numeric differences,
zero regression gates or extra clock events; no old correct tempo/downbeats
lost, one held-out BRANO downbeat gained. Direct common-correct phase unchanged,
maximum microphone delta0.00783ms. Host CPU medians cost0.65–0.95 percentage
points of a core; callback maxima remain below1.452ms of5.333ms. Signed iPad
build/install pass; physical listening/thermal verification is pending because
the user cannot unlock it now. Full velocity1.0 retains a5ms articulation
spread: do not claim the2ms bound at every gain. See TODO162 and references.

TODO163 (2026-10-26): gain-aware clipped-hit attack calibration measures0ms
spread on9 velocities ×3 faders ×16 articulations ×3 RR, including the former
5ms fortissimo residual (53/0). A clear aligned network bar vote blocks harmony
fallback on direct sources only; speaker retains baseline. The combined
pre-START325 bank has321 exact/4 changed, no gates lost, and improves the one
on a new44min recording77.433628→79.056047%. Its default suite is789/0.
Separate fitted phase-period projection, trust-scaled nudge and an all-source
bar veto were measured and rejected. START after PLAY and an explicit source
replacement now preserve the existing pending AUTO pulse-level check.24 paired
warm/player-lifecycle probes lose no formerly correct beat/downbeat; Everytime
BRANO0→99.10% correct tempo, MIXER unchanged and common p90 improves4.31ms.
The physical iPad reads about123BPM and the listener confirms correct speed
and a clean entrance. Two concurrent app variants caused the audio-session
churn; leave only FULL active. Final325/suite/CPU closure is tracked in TODO163
and verification.md; do not substitute earlier variant totals.

TODO164 latest: full797/0,325 paired probes zero regression gates,52 actual
output pairs remove38 red openings to0. Keep nominal rendering and its guard
fade independent of actual early-entry veto. Physical iPad playback has no
new XRUN, user hears cleaner entry but imperfect intro timing. Correct BPM
is not correct phase: tonal intro grid remains displaced~0.2s vs teacher.
A phase-only prototype has no effect and is removed. No model/phase fix is
claimed. See verification.md for costs, rejected variants and device limits.

Source165's additional rest-rate freeze is withdrawn after the bank rejects
127/325 combined witnesses. Without165,166 still changes two whole MIXER sets
and loses formerly correct beats/downbeats despite aggregate gains; a lighter
analysis-only restart also fails. The user approves excluding BOTH from the
release. Ordinary target/glide handling and previous silence policy are
restored, retaining the existing rest phase-steer guard. STOP can still be
needed in some after-silence song changes. See clock/room/verification records;
final stable validation:798/0,325/325 pulse traces identical to164, zero gates
rejected. See verification.md for48 CPU runs and retained host wall-time
outliers; signed iPad build3 installed/launched27Oct; user confirms transparent logo,
animated dots and locked purchase page. No new musical/thermal test is claimed.

Store model policy (TODO168): iOS StoreKit builds except personal FULL require
a real ONNX model. Startup load/prepare failure is explicit, disables automatic
following and START NOW, and leaves TAP/FIXED available. Test/host stubs remain
for verification only. Failure reaches the UI via atomic snapshot fields.
Final default suite798/0; evidence5/0, real-callback access8/0 and paywall26/0.
See verification.md for CPU limits and physical-device status.

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
