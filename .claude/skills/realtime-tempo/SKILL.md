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

**Bounded line acquisition refinement (2026-09-09).** After level selection,
unpaired direct-feed acquisition averages the two already available intervals
only if they agree within 14%. It does not reject acquisition, add observations
or change the selected octave. Swing cells and room acquisition are unchanged.
On INFINITO's ~30–70 s excerpt, cold-start decoder at +2 s changes 95.43 ->
92.27 BPM (reference ~91); VPLive relative drift 17.5/81.1 -> 9.0/25.1 ms
mean/max. This is one real-network run per variant, not rendered audio phase.
Reduced matrix: mean lock 7.28 -> 7.30 s; 18 excursion runs and six unacquired
half-time cases unchanged, fraction outside 4% 13.33 -> 13.31%. This is a local
initial-accuracy improvement, not a universal faster-lock claim. The earlier
failed experiment below required coherence as a gate; this one only refines
the period when coherence is already present.

Acquisition experiment, 2026-09-09: extending the room's 14% consecutive-interval
check to all unpaired line-feed fast acquisitions was reverted. On the reduced
`probe_matrix --quick` bank it improved chords (7.79 -> 4.94 s) but worsened rock
eighths (8.19 -> 12.40 s), mean acquisition 7.28 -> 7.41 s, excursions 18 -> 20.
Do not equate a stricter interval gate with faster correct acquisition. Letting
only provisional HMM grids without measured intervals accept off-grid peaks was
also tried and removed: the same bank was unchanged. Both are synthetic decoder
measurements; the user's previous INFINITO file was unavailable for this run.

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
from the immediately preceding accepted interval.

**Quarter-by-quarter step detector (2026-09-17, uncommitted).** The interval
path compares consecutive intervals, so its noise is twice the onset scatter;
it stands down once `3 * jitter > 5%` and never claims less than 5%. Measured on
decoder+clock: clean 3-5% steps took 8-13 s, and with 6 ms of onset scatter a
+10% step at 120 opened no candidate at all (~4 s through the ordinary release).
`BeatDecoder::observeGridStep` (direct feed only, after each accepted beat)
extrapolates the eight-beat line fitted up to a pivot and reads the newest 2-4
accepted quarters against it: a step leaves the line by `k * step`, a drummer
drop or late mix by a constant. It confirms at the earliest m in {2,3,4} when:
every quarter is consecutive, beat-strength (0.70 x median) and leans the same
way; the through-pivot step fits within `(m+2) sigma^2`; it beats the best
"displacement starting at any of the m quarters" by `16 sigma^2` (sigma = the
8/24-beat pre-pivot residual, floor 0.5% of a period); 2.5% <= |step| <= 30%;
the two four-beat periods before the pivot agree within 30% of the step (ramp /
tilted-window guard); no beat-strength peak was rejected by the grid since the
pivot (75 -> 140 lands every other new beat 7% late and otherwise delayed the
octave path 10.7 -> 23.6 s); and it does not oppose a proven causal direction.
It publishes through the same `rapid` transition (history = pivot + new
quarters, grid on the fitted line, `live`, refit quarantine), so the clock path
is the measured one. Inert with a manual octave shift (grid gap != 1).

Evidence: `probe_tempo_step` byte-identical; `probe_small_steps` 52->50 FAIL->PASS
(4.92 -> 3.74 s), all other rows identical; `probe_motion_matrix --quick`,
offsets 0/16/32/48: **fixed trace hashes identical on all four**, step mean/p95
36.8/180 -> 36.0/175, 49.3/209 -> 45.1/174, 47.9/196 -> 43.0/171, 34.8/165 ->
32.4/139 ms, continuous mean/p95 better on all four and recovery violations
9/2/10/16 -> 5/0/8/3; `VPAlign --ramps` identical; `VPTests --tempo-step` 13/0
(new jittered 120->132/110 x 6 seeds: 4/12 -> 12/12 confirmed in 0.92-1.40 s;
+44 ms displacement: 0 transitions), `--tempo-slow` 10/0, `--tempo-motion`
same 291/2 and `--octave` same 5/6 verdicts as HEAD. Scratch sweep (decoder+clock, 70-150 BPM, +/-3..15%, 3
seeds): mean time to stable 5.4 -> 1.9 s clean, 12.1 -> 5.6 s at 6 ms, 14.3 ->
11.5 s at 12 ms. Known cost: at the start of very steep ramps (10% in 12 s at
60-80 BPM) it fires 4-12 times per 9 minutes; mean BPM error still improves
(1.73 -> 1.52% at 60) but time outside 2.5% at 100 BPM rises 5.1 -> 6.7%.
Not covered: jittered steps beyond ~15% (the new beats fall off the grid).
No real recording or listening yet.

**Wide non-octave line steps (2026-09-10).** The old transition path stopped at
25% and also required the candidate to remain within a quarter octave. The
octave machinery was supposed to own everything beyond that, but it only knows
how to decide metrical levels: a real 120 -> 160 change is neither a level nor
inside the transition window. It therefore took 23.6 s through the stale-grid
watchdog; 120 -> 150 took 12.8 s and 90 -> 120 took 20.7 s.

On a direct mixer/file feed, coherent candidates may now reach 65% provided
their log-distance is not within 0.15 of a whole octave. They require **three**
causal intervals rather than two, retain the abrupt-edge and jitter checks, and
the stale fold is quarantined until the new eight-beat fit has formed. Measured
with `probe_tempo_step`: 120->150 1.2 s, 120->160 1.1 s, 100->160 1.1 s,
160->100 1.8 s, 120->90 2.0 s, 90->120 2.2 s; all finish within 1 BPM and the
probe asserts a 2.5 s ceiling. The room path is unchanged. Exact/near octaves
(120/60, 140/75, 75/140) remain with the octave/TAP path because the sound does
not say whether the player changed tempo or subdivision. `probe_matrix --quick`
is byte-identical to HEAD (7.37 s mean acquisition, 18 excursions, 13.37% out),
and the slow-tempo and bar gates remain 10/0 each.

The 97-minute `Flamingo Marco 09.07.26.m4a` mixer feed was also sampled at five
song centres with `extract_live.swift`. Only two of the five tempogram curves
were reliable enough to act as a loose BPM reference (mean lag 5.12 s), so this
is evidence of the remaining continuous-drift problem, not ground truth for a
decoder threshold. On the centre of Sally the final chain needed no extra
restart and finished at 102.51 BPM, but `prec.py` still measured 94 ms median
phase-window movement and 258.4 ms worst movement. Do not cite the wide-step
fix as solving gradual breathing: that path remains separate.

**A confirmed direct-feed step must not be re-argued by the old comb
(2026-09-15).** Confirmation already replaced the BPM and rebuilt the short-fit
history from the new peaks, but the four-second activation autocorrelation was
still allowed to pull the live target immediately. On the clean 120->132 probe,
the causal detector confirmed 132.03 after 0.92 s, then the stale 120-BPM comb
pulled the next publication to 129.2 and the sounding clock did not remain
settled until 8.90 s. On 120->108, confirmation came at 1.14 s; after the first
eight refit beats the still-old comb produced a later 108.92-BPM rebound, moving
the stable time to 14.12 s.

For mixer/file (`lineFeed`) only, `updateTempo` now bypasses
`pullTowardsComb` while `transitionRefitBeats` is non-zero. The quarantine is
the eight accepted beats needed by the short fit plus three more accepted beats
for the slower autocorrelation. It is bounded, does not restart or move the
musical clock, and does not change the iPad/room path. Measured end to end by
`probe_small_steps`: 120->132 is at rate in 0.92 s and stable in 1.34 s; 120->108
is at rate in 1.14 s and stable in 1.66 s. The new `VPTests --tempo-step`
regression watches every post-confirmation publication: worst error is 0.041 BPM
and 0.026 BPM respectively, one serial each (11/11 PASS). `probe_tempo_step`'s
120->132 row improves 4.1->0.9 s and every other row is unchanged.

Safety evidence for that isolated step fix, before the continuous-motion work
below: the complete 360-run `probe_matrix` was line-for-line identical to HEAD
(5.22 s mean acquisition, 104 excursions, 30 never-acquired, 9.11% out), as were
all five pulse files from the reproducible `Flamingo` mixer extracts.
`VPAlign` now correctly treats its 100->140 case as a required three-interval
wide transition: it reaches rate in the causal minimum 1.30 s and is at 24.5 ms
worst phase one beat later; all six steps and both ramps pass. This does not
solve near/exact octaves or the two remaining marginal small steps (52->50 at
4.915 s; 120->118 at 26.02 ms against a 25 ms gate).

**A ramp must be scored in phase, not only in BPM (2026-09-15).** A 10 BPM rise
over 30 seconds changes by only 0.33 BPM/s: a follower can remain inside a 2%
tempo gate while accumulating an audible fraction of a beat. `VPAlign --ramps`
therefore drives decoder and sounding MIXER clock against the unjittered beat
grid and now fails on mean, worst, or post-ramp phase debt. Flat 100/130 controls
are part of the same gate.

The held regime used a median of three newest intervals for its fast motion
vote. On a direct feed one interpolated onset can reverse that median and erase
a real ramp repeatedly. The line path now takes the deviation from the smoother
eight-beat fit, still requires the raw newest intervals to support the same
direction, and spends one vote rather than clearing the run on one disagreement.
A tightly placed short fit (`residual < 0.030`) earns release after two net
votes; otherwise it still needs three. The room path keeps the old 2.4% raw-
interval rule.

While that same clean direct-feed evidence says the held tempo is moving by
more than 2%, `BeatTracker` shortens phase averaging from 0.90 to 0.30 seconds
and lets the existing phase-derived rate trim use a 0.20 rather than 0.08 gain.
This hint does not choose a tempo, is disabled for speaker/microphone, harmony,
TAP and manual tempo ownership, and expires 1.5 expected beats after the last
accepted beat so a dropout cannot leave it armed.
Measured over four deterministic seeds, sounding MIXER phase (mean/worst ms):

| ramp | before | after |
|---|---:|---:|
| 100->110 / 30 s | 26.0 / 140.7 | **22.0 / 84.7** |
| 100->110 / 12 s | 40.8 / 153.4 | **40.8 / 127.2** |
| 120->132 / 20 s | 33.3 / 130.4 | **28.5 / 95.5** |
| 128->120 / 20 s | 20.2 / 62.7 | **20.2 / 48.0** |

Flat controls remain 7.1/33.3 and 7.2/22.0 ms. A ten-second drummer dropout
also stays `fixed`: with 44 ms of acoustic lateness its real MIXER row remains
25.3/66.2 ms, so the direct-feed exception does not turn that passage into a
tempo change. The complete material matrix retains 5.22 s mean acquisition,
104 excursions and 30 never-acquired runs; its outside fraction moves only
9.11->9.12%. The five `Flamingo` extracts are no longer byte-identical because
four contain clean motion evidence; the loose tempogram score moves 8.62->8.70%
error and 2.21->2.27% grid jerk, with only one of five lag correlations reliable.
Treat that as a transparent weak negative, not phase ground truth: a manually
marked beat grid and listening pass are still required before calling continuous
live following complete.

**Curvature remains diagnostic (2026-09-16).** The remaining fast-ramp debt is
real: on the worst 100->110 / 12 s seed, truth had reached 104.17 while the
held decoder still published 100.07 and the clock was 69.9 ms late. A raw
quadratic over the short eight-beat window is not the answer; at a fixed 100 BPM
it reports local slopes up to +/-1.7 BPM per beat.

`fitPeriodCurve` therefore fits `t(n) = a + b*n + c*n^2` over sixteen accepted
beats and evaluates the local period at the newest one. The first checkpoint
documented that fit as a production release, but the actual switch contained no
`motionFitEvidence` exit: fresh `VPAlign --ramps` reproduced the preceding
22.0/84.7, 40.8/127.2, 28.5/95.5 and 20.2/48.0 ms MIXER results exactly. The
code and the claim did not match.

The missing test is now `scripts/probe_motion_matrix.cpp`: deterministic flat,
continuous and abrupt trajectories from 55 to 175 BPM with subdivisions,
swing, jitter, missing beats, false peaks and gaps, all scored against their
written beat grids. Its original integration also overstated the clock: it
snapped phase whenever error exceeded 0.04 beat and omitted the sounding
clock's locked tempo trim. It now uses the MIXER path (`setLocked`, tempo trim,
motion hint and `setGridPhase`; no silent/STOP snap).

The original three-curve selector was not globally safe. Across 128 runs per
family (one 64-case bank plus four independent 16-case offsets), it produced
**8 proofs on fixed tempo**, 51 on continuous motion and 11 on steps. That is
roughly one false proof per eighteen minutes of flat material, not a production
gate. Three follow-ups were rejected:

- requiring at least 50% quadratic residual improvement on every proof removes
  the flat false positives but reaches the clean ramps too late;
- requiring it only at the start of a run is still too late;
- a fourth unfiltered proof still fires on a fixed case and leaves one targeted
  ramp over its gate.

The current selector is deliberately diagnostic: each of three proofs must
remove at least half the straight-line squared error. On the full 64-case bank
it yields 0 flat, 11 continuous and 3 step proofs, so the probe's selector gate
passes without disabling the detector. It still does **not** release FISSO or
drive BPM/phase. Do not add another threshold to this binary release. The next
design has to replace the hard fixed/live switch with a bounded continuous
motion authority, and must improve the global phase distribution without
moving the flat or abrupt populations. A manually marked beat grid and listening
remain necessary before any product claim about live phase.

**The hybrid shape-authority bridge was rejected too (2026-09-16).** The plan in
`docs/superpowers/plans/2026-09-16-hybrid-tempo-motion-proof.md` added a strict
proof edge, a residual-shape classifier and a one-shot bridge inside FISSO. On
the four-offset quick bank it left 3/4 continuous rows with no authority at all,
fired on two fixed and three step rows (trace hashes changed) and broke one
recovery. It was removed whole; the classifier and diagnostics stay, and
`compare_motion_matrix.py` now rejects a vacuous zero-authority "pass".

**Same-beat direct-feed release ordering (2026-09-17).** The decoder observes
the accepted beat before deciding whether `FISSO` must become `VIVO`. A retained
quadratic shape could therefore be decisive on the release beat but still see
the old regime and leave bridge authority at zero until the following accepted
beat. Re-evaluate only the bridge-authority predicate immediately after the
existing `fixed -> live` decision; do not observe the beat twice. This changes
no proof threshold, history, grid, serial or clock state and retains all
transition/refit/quarantine/direction vetoes. It is an ordering correction, not
evidence that the global fixed/step gates have passed; those gates remain
required before committing the experiment.

For manual listening diagnostics the debug trace also reports `trim` (the
phase loop's accumulated BPM correction) and `phaseErr` (the most recent
accepted onset error converted from beats to milliseconds at the sounding
clock rate). These are lock-free display copies only. Together with `bpm`,
`target` and `clock` they distinguish late tempo recognition from a clock that
knows the rate but has not yet closed phase debt.

**The beat-date filter was rejected and removed (2026-09-17).** A later
experiment replaced the direct-feed phase and rate with an IMM filter over beat
dates. It improved aggregate synthetic ramp scores, but it did not satisfy the
acceptance contract: all four quick offsets changed the fixed and step trace
hashes, the full continuous p99.5 worsened, one `VPAlign` rallentando remained
red, and the first hand-tapped real recording worsened from 27.5/72.6 ms
mean/p95 to 31.3/76.9 ms. A follow-up selected from that one recording was also
worse against human taps and was reverted first.

The production filter, its clock fields, its special phase constant and its
probe glue are now removed. The four-offset quick comparison once again has
bit-identical fixed and step traces. Continuous rows have zero applied authority
and therefore still fail the anti-vacuity comparator: this is intentional and
means the continuous-motion goal remains open rather than being declared solved
by a rollback. `VPAlign --ramps` is likewise red on all four baseline ramps.

Do not restore or retune this filter from one title, timestamp, seed, offset or
BPM. A future candidate must pass fixed/step identity, every continuous row,
recovery, targeted ramps and independent real accelerando/rallentando grids
before it can be accepted. At this rollback checkpoint the shape classifier and
shadow diagnostics were diagnostic-only; the explicitly untested listening
candidate below is the only later exception.

**Continuing the scalar shadow into `live` is also rejected (2026-09-17).**
Keeping the existing strict proof alive after `fixed -> live`, then using its
bounded BPM prediction only in the matrix clock, gave positive authority on all
four continuous quick populations and left every fixed trace identical. It did
not separate a step: offset 0 changed the step hash and accumulated 44 authority
frames. Continuous mean improved only 0.12-0.23 ms, p95 was unchanged on offsets
0 and 32, and every offset produced recovery violations (2/6/5/15). The whole
experiment was removed without tuning thresholds. A future candidate cannot
infer continuous ownership from `live` plus scalar slope alone.

**Uncommitted iPad listening candidate (2026-09-17, deliberately untested).**
Unlike the rejected scalar continuation, this keeps only the fixed-entry
residual-shape window across the ordinary `fixed -> live` release. It grants
authority only in `live`, with no hinge and no quarantine. After manual iPad
feedback that correction began late, the first quadratic verdict retained into
`live` may start at 35% authority rather than wait for a second shape win. Shape
alone still cannot release `fixed`. The early verdict may act only when it beats
both the generic BIC runner-up and the explicit hinge/step explanation by the
classifier's existing 2-BIC
evidence rule. A second consecutive quadratic win grants full authority. This
is a model-selection rule, not a title, BPM, seed or offset threshold. The
bridge remains limited to 0.75% per accepted beat and 4% from the BPM at first
authority; it only leads an ordinary target that is still behind in the same
direction, never pulls an already-current fit back. It reaches the decoder
only through `commit()`.
Transitions/refit, octave/grid rebuild, input
epoch, discontinuity, stale beats and non-direct input reset or veto it. The
tracker's faster phase/trim hint additionally obeys TAP/manual ownership and is
disabled for speaker/microphone. The first verdict retains 0.30 s phase
averaging; twice-proven shape motion uses 0.15 s. It still never snaps. No
automated test has been run at the
user's request; this is a listening candidate, not accepted production evidence.
An attempted shape-only `fixed -> live` release was removed before handoff: it
would let one classifier verdict replace the existing causal release predicate,
contrary to the fixed/step contract. The earliest retained intervention is the
first strong shape verdict after the ordinary release.
The decoder fixture now records the first eligible live beat and requires bridge
authority on that same beat, so the integration itself cannot add another beat
of latency. `probe_motion_matrix` also mirrors the production phase policy:
0.30 s on the provisional verdict and 0.15 s only at full shape authority.
At full authority the follower also stops repeating the decoder's proof: one
fresh phase interval may update the existing bounded tempo trim immediately,
instead of waiting for three additional same-sign intervals. Provisional shape,
fixed tempo, steps, TAP/manual ownership and speaker/microphone retain the
ordinary three-interval filter. This changes only rate trim; it never snaps or
restarts the clock.
Full shape authority also substitutes full control trust inside the follower.
The ordinary trust score is derived from linear-fit placement and may fall on a
valid curved trajectory; applying its 2.5 s poor-evidence glide after two
quadratic wins would add the old delay back after recognition. The override is
revoked with bridge authority and does not change the decoder target, recovery
serials or any fixed/step path.
The same full proof is passed explicitly to `observeRecoveryBeat` on the direct
path. It may replace the stale linear-fit trust check on the first proven beat,
but never the two fresh, serial-distinct and phase-consistent observations. This
removes an ordering delay of one accepted beat without turning recovery into a
single-observation phase move; room/speaker calls cannot use the override.
The following iPad trace exposed the remaining case: near the end of a loaded
song the decoder target rose from 109.47 to 112.61 BPM while the sounding clock
trailed by roughly 1.3--1.5 BPM; the accepted onset error grew from -44 to
-62 ms and phase trim stayed near +0.04 BPM. Shape authority had already
expired, so the constant-tempo fit's low trust prevented the existing two-beat
fast recovery even though the decoder was stably `live`.

The listening candidate therefore also lets an established direct-feed `live`
regime bypass only that constant-fit trust check. A confirmed abrupt transition
and every accepted beat in its complete refit window are excluded explicitly;
fixed, TAP/manual, octave/grid and speaker/microphone paths are unchanged. The
recovery itself is not weakened: it still needs two fresh serial-distinct phase
observations, matching sign and placement, more than 40 ms of persistent debt,
and it corrects by a bounded monotonic rate lean rather than a phase snap. This
is still uncommitted and has not passed the automated fixed/step identity gates.
The first full iPad pass with that candidate did recover an 86 ms displacement
to 6--8 ms and a later 31 ms displacement to 3 ms, but took roughly four and
two seconds respectively. The missing observations were already accepted by
the decoder but did not clear the follower's second `confidence > 0.40` gate.
In direct `live`, outside the complete abrupt-transition refit quarantine, that
redundant gate is now omitted: decoder acceptance, a fresh analysis timestamp
and the recovery's own two serial-distinct agreeing phases remain mandatory.
Fixed, transition/refit, TAP/manual and speaker/microphone paths retain the old
confidence rule. `phaseRecoveryEvents` is a display-only counter added to the
debug trace so the next listening pass can prove whether the bounded recovery
actually armed; it cannot influence DSP.
That pass recorded ten recoveries and the listener still heard smaller exits.
The remaining discontinuity was in the recovery gesture, not its trigger: the
old quarter-beat minimum could spend a 25--40 ms debt as a 12--20% temporary
rate lean. Direct `live` recovery now distributes the same monotonic correction
over at least one complete beat. A later full trace exposed that this window
was not actually being spent: `setTempoTrust()` cancelled it on the next audio
callback whenever the constant-tempo fit remained below 0.80, even though the
direct-live proof had explicitly been allowed to replace that stale score.
This produced a short sounding-rate spike (for example 112 -> 122 BPM for one
trace publication), then left the remaining debt to the slow ordinary loop.
The direct-live proof now preserves only its already-bounded recovery window
across low trust and clears the override when that window ends. The next trace
confirmed complete recoveries in roughly 0.4--0.6 s. For the band-led direct
path, listening still identified that duration as lag on the sixteenth-note
grid, and the product decision explicitly permits a near-net rate gesture.
The correction therefore starts on the same causal observation and uses a
half-beat minimum; the 20% rail keeps phase monotonic, so pulses may move closer
together but cannot be duplicated or skipped. This is dimensionless and applies
at every supported BPM;
dropout/re-entry keeps the old quarter-beat minimum, and the 20% hard rail
remains a safety ceiling for larger debts. The
next Xcode trace put an end-of-third recovery inside 3 ms within the following
trace second, but listening still identified a small delay before it began.
The remaining delay was the direct-live independence floor: a fresh accepted
serial at an eighth-note distance was discarded by the 0.55-beat minimum, so
the controller waited for the following quarter. Direct `live` now uses 0.45
beat, allowing two coherent eighths to satisfy the same two-observation proof;
fixed, transition/refit, dropout and speaker paths retain 0.55 beat.
The subsequent complete iPad trace showed that making this one-shot earlier was
the wrong gesture for normal band motion. At 158.45 s the target/clock were
106.37/105.27 BPM with +28 ms phase error; when recovery armed, the sounding
clock moved 106.55 -> 103.32 -> 106.34 BPM in 0.21 s. An earlier recovery moved
roughly 105 -> 109.99 BPM. These rate lurches were the audible accelerations and
slowdowns. Stable direct-feed `live` now uses a continuous proportional phase
servo instead: 0.65/0.45/0.35 s response and a symmetric 3.5/5.5/7.5% rail for
low/medium/high follow. Its derivative term is zero, so a fresh 6 Hz
publication cannot become a short rate spike. The direct grid observation uses
the existing 0.30 s motion average instead of stretching to 2.2 s when the
constant-fit trust falls on valid curvature. The one-shot path can no longer
arm from ordinary persistent debt in this mode; it remains available after a
real poor-evidence interval has armed dropout/re-entry recovery. Transition,
fixed, TAP/manual, harmonic, speaker/microphone and refit-quarantine behavior is
unchanged. This candidate is global and dimensionless, still never snaps or
restarts the grid, and is not yet listening-accepted or automated-test-verified
at the user's request.
The first listening pass was better but still drifted. Its full trace showed
that the remaining audible gestures were one-shots armed by only 200 ms of low
fit trust, not residual error from the continuous servo: 108.56 -> 101.71 BPM,
106.12 -> 109.03 BPM and 107.54 -> 120.13 BPM. The first and third even occurred
while the decoder called the tempo `fixed`. On a stable direct file/mixer feed,
normal following now vetoes every one-shot regardless of whether it was armed
by low trust or persistent debt. The same accepted phase continues through the
monotonic ordinary/continuous servo. A one-shot is permitted again only when
the tracker state itself is `recovering`, which distinguishes an actual loss
and re-entry from ordinary confidence movement. Room/speaker recovery and all
transition/refit guards retain their existing behavior. This is still an
uncommitted listening candidate; no automated tests were run at the user's
request.
debug `clock` value now reports the effective steered rate actually advancing
the grid; groove/voice consumers still receive the nominal PLL tempo, so this
diagnostic correction cannot change playback by itself.
The app's lock-free `EngineSnapshot` now carries bridge authority, shape model,
quadratic wins, predicted BPM, BIC evidence, hinge separation and quarantine.
The DEBUG panel and debug log show them. This is diagnostic propagation only:
the UI cannot feed any value back into the decoder or follower. It also confirms
that both `FollowSource::internalPlayer` and mixer/`kitMic` reach BeatNet with
`lineFeed=true`; only speaker follow disables the bridge.
The DEBUG panel labels shape decisions in words (`CURVA`, `GRADINO`, `LINEARE`,
`PICCO ISOLATO`, `IN ATTESA`), labels bridge state (`SPENTO`, `AVVIO`, `PIENO`)
and displays the heard clock BPM beside decoder and target BPM. That is the
manual iPad seam for locating latency without interpreting enum integers.
In a JUCE debug build the same decision chain is also emitted automatically to
the Xcode console about five times per second. Filter on `VP_TEMPO_TRACE`; the
trace is message-thread-only and is compiled out of performance builds.
The first real iPad trace exposed a lifetime bug in the listening candidate: an
earlier hinge kept `shapeHingeActive` latched after its visible twelve-beat
quarantine reached zero, so later decisive `CURVA` frames in `live` remained at
zero wins and the bridge could never start. The latch now clears only after the
hinge has left the sliding window and the entire quarantine has elapsed. The
second iPad trace then showed that overlapping windows describing the same
physical hinge repeatedly restarted the twelve-beat countdown. A hinge episode
now arms that bounded quarantine once; while the hinge survives its latch still
prevents all authority, and after a non-hinge release a genuinely new hinge can
arm a new quarantine. These follow-ups are uncommitted and have not run
automated gates at the user's request.

The next iPad trace exposed a separate abrupt-transition failure during a
rallentando: the bridge remained off, but two coherent off-grid peaks confirmed
an opposite 106.74 -> 110.38 BPM transition in one frame while accepted beats
already carried a two-beat slowing direction. On a direct feed the rapid
transition detector now rejects a candidate that opposes that proven causal
direction. Stable tempo has no direction to oppose and retains the ordinary
step path; a genuine reversal must first replace the earlier direction with
accepted evidence. This is global and contains no song, BPM or offset value.

The following trace exposed an independent shape-direction failure: during a
rallentando both causal fits read downward (`short 108.71`, `long 107.41`) while
a briefly winning quadratic endpoint extrapolated upward to `109.77` and gained
initial bridge authority. Shape authority now additionally requires its BPM
delta to have the same sign as the current short-fit delta. The short fit does
not select the bridge target; it is a causal direction veto that prevents a
statistically winning extrapolation from driving away from the band.

The same trace showed a second inflection cost below the decoder: positive
phase-derived `tempoTrim` accumulated during the preceding accelerando and made
the effective target continue upward after the fresh decoder target had turned
down. On the direct file/mixer path, `TempoFollower` now clears only that trim
integrator when adding it would reverse the sign of the raw target error
relative to the heard clock. This is a direction invariant, not a BPM or song
threshold; it never moves phase, restarts the clock or changes the grid, and
room, TAP and manually owned tempo retain their existing path.

The subsequent iPad trace localized the remaining late correction before the
follower: the direct short fit began falling at about 51.8 s while the committed
tempo stayed fixed until the ordinary release at about 55.1 s; once published,
the clock closed most of the gap in under a second. The DEBUG trace therefore
also publishes the already-existing causal release evidence as `fast`, `raw`
and `votes` (net count/direction). These fields are display-only and are meant
to distinguish missing causal support from a late threshold without changing
the fixed-tempo path during the listening run.

That run also exposed premature re-entry into `fixed`: after a motion episode,
the long window could satisfy the ordinary stability test while the responsive
short fit was already 1-3 BPM away. The decoder then held the older rate until
a late transition/release made the correction audible. The uncommitted direct-
feed listening candidate therefore reuses the existing 0.4% cross-window
agreement rail before `live -> fixed`. Initial acquisition is unchanged, as are
room/speaker input and the already-fixed release path. This is a global state-
certification invariant, not a title, timestamp or BPM exception; it remains
untested except for the requested manual iPad pass.

The next filtered trace located the complementary exit delay. At 95.54 s the
direct path had accumulated three net causal votes: the responsive fit was
2.87% below the held tempo and the newest measured interval was 2.27% below it,
yet `fixed` persisted until 97.21 s because the twenty-four-beat trend had not
also agreed. The listening candidate now lets the existing full three-vote
direct proof release `fixed` without that redundant long-window confirmation.
The earlier two-vote shortcut still requires both a clean fit and the agreeing
long window; room/speaker input is unchanged. This does not lower a deviation
threshold or allow release before three causal observations, and remains
uncommitted and automated-test-free at the user's request.

A fresh run showed why that first edit still waited: at 41.01 s the third vote
was present but the generic four-beat minimum dwell still wrapped every release
path; it reached six votes before the separate small-step path moved the target.
The causal counter also survived `live -> fixed`, so simply bypassing the dwell
could have made an old vote undo a new stability decision. `enterRegime(fixed)`
now clears the fast count and deviations, and the direct path may bypass the
generic dwell only after three fresh same-tenure votes. All other releases keep
the four-beat minimum. This changes no threshold and gives neither old evidence
nor fewer than three accepted causal observations authority.

**Causal realignment still open (2026-09-18).** The listening candidate is in
HEAD. Titles do not choose constants: a mixer excerpt that still lags is
evidence that a *class* of motion is late (FISSO held through a ramp,
already-VIVO with a dirty 8-beat window), and the next candidate has to
move that class on the known-phase bank without changing fisso/gradino
hashes. Smoke on the known-phase bank (offset 0, `--quick`) and
`VPAlign --ramps` shows the listening candidate did not close the remaining
lag, and three follow-ups were reverted rather than shipped:

- Offset 0 HEAD: fisso 22.3/76.9 ms mean/p95, hash-stable, authority 0, 6
  `FISSO->VIVO` releases; continuo 54.5/130.3 ms, 171 authority frames, 5
  recovery violations; gradino 36.0/174.9 ms, **17 authority frames**.
  `compare_motion_matrix.py` therefore still fails step-authority and continuous
  anti-vacuity against any control that requires zero step authority and an
  improved continuous row.
- `VPAlign --ramps` MIXER is still the rollback checkpoint: 22.2/84.7,
  40.3/127.2, 28.6/95.5, 20.3/48.0 ms mean/worst. Flat 100/130 controls stay
  6.9/33.3 and 7.2/22.0. `probe_tempo_step` non-octave rows stay PASS.
- `--trace-ramp 100 110 12` seed 101: ramp starts at t=20 s; committed BPM stays
  100 until the three-vote release at t=26 s (truth already 105). Clock worst is
  **-93.2 ms at that instant** (decoder **-63.1 ms**). Residual-shape
  `auth=0.00` through the lag; first 35% win is t=30 s, after the ramp. The 8-beat
  fit first exceeds 1.2% at t=25 s; one jittered IOI at t=24 s disagrees and
  spends the vote. `kTempoMotionResidual` 0.030 never lets the existing motion
  hint arm (short residual 0.047-0.048).
- Reverted, in order: (1) unpublished FISSO walk from residual-shape wins plus
  sliding the 4% live rail — changed fisso/step hashes, continuo bit-identical;
  (2) live 16-beat curve lead at 20% improvement plus wiring
  `setDirectLivePhaseFollow` into `VPAlign` MIXER — fisso 22.3→24.1 ms, continuo
  54.5→57.2, ramp worsts 84.7→100.5 and 127.2→138.2; (3) FISSO walk toward
  `motionFit` at 20% improvement — 130 BPM flat MIXER 7.2/22.0→9.9/65.3, 12 s
  ramp decoder floor 23.8→35.5. Decoder restored; only `--trace-ramp` now prints
  `auth` and quadratic wins.

- Clock-side follow-ups, also reverted except the direction-guard
  correction below: (4a) hint-rate trim plus 0.30 s grid tau on
  persistent mixer phase debt — VPAlign MIXER collapsed onto LEANA
  (12 s 50.2/146.1) because the direction guard compared decoder BPM
  to the sounding clock and wiped the trim during FISSO; (4b) trim
  boost without tau, 50 ms floor, still no worst-phase movement
  (clock-versus-decoder gap peaks ~30 ms, below that floor; the 93 ms
  peak is decoder-versus-truth); (4c) 18 ms floor with full agreement
  — 12 s mean 40.3→39.3 but 128→120 worst 48.0→66.8. Clock trim cannot
  close decoder lag to the notated grid. Do not retry a sub-50 ms
  clock-decoder floor.

The direction guard now keys off the decoder target turning around
(`decoderDelta = bpm - target`, opposite the trim, `|delta| > 1 BPM`)
instead of clock-versus-decoder. `VPAlign --ramps` MIXER enables it
and stays at the rollback checkpoint (22.2/84.7, 40.3/127.2, 28.0/95.5,
20.3/48.0; flats 6.9/33.3 and 7.2/22.0). Production mixer already
had the guard on; the old test would have been the LEANA collapse.
The known-phase bank probe is left without the guard so offset-0
fisso hashes stay comparable to the existing control CSV.

- Decoder-side unpublished FISSO walk toward the 8-beat short fit,
  gated by 16-beat quadratic improvement >= 0.10, two consecutive
  beats, one causal vote and short residual < 0.050: 30 s MIXER
  22.2/84.7 -> 20.4/78.4 (PASS), flats stay 6.8/33.3 and 6.4/22.0,
  12 s mean 40.3 -> 36.3 but worst 127.2 -> 128.7, 120 -> 132 worst
  95.5 -> 104.4, 128 -> 120 worst 48.0 -> 57.3. Reverted. The 12 s
  peak is the FISSO-release instant; one bounded kFixedMaxStep
  cannot close it, and a looser residual/improvement gate walks on
  the 130 BPM flat (7.2/22.0 -> 6.6/39.3). Do not retry a FISSO
  short-fit walk without a gate that is silent on VPAlign 130 and
  still moves published BPM at t=24 on the 12 s ramp.

- Further walk attempts, also reverted: monotonic short-vs-anchor
  growth plus a 0.18%/beat rate floor walked VPAlign 130 (worst
  22.0->26.2) and delayed 120->132 by shrinking the three causal
  votes (worst 95.5->119.9). Voting those releases against the
  frozen long-fit anchor unstarved VIVO and took MIXER to
  20.8/84.2, 32.4/93.2, 24.6/75.4, 19.8/48.0 with 130 still
  7.0/22.0 — and changed the offset-0 fisso/gradino hashes
  (fisso mean 22.26->22.40). Fourteen matrix false starts had
  *cleaner* residuals (0.022-0.046) and *stronger* quadratics
  (g=0.25-0.74) than the 12 s ramp (res 0.047-0.048, g=0.12-0.21).
  A residual band just above `kMotionCurveResidual` (0.045, 0.050)
  with g in [0.10, 0.50) restores those hashes bit-identically,
  but one `kFixedMaxStep` in that band still left the 12 s
  four-seed mean 40.3->41.1. Do not walk published BPM in FISSO
  unless the offset-0 fisso hash stays `8e3c8d2cdc5854f5`.

**Kept (2026-09-18), strain-line release, growing-clean votes, vote hold.**
Same residual/quadratic band to leave FISSO one vote early
(`strainLineRelease`). After an IOI-backed vote, a strictly growing
clean 8-beat fit (residual <= 0.040, quadratic g>=0.08, same-sign
rate) may add a vote without the newest interval. If that fit is
still on the same side with the same quadratic but is not growing,
the vote is held instead of spent. The two-vote clean path still
requires that interval this beat.

Offset-0 fisso/gradino hashes stay `8e3c8d2cdc5854f5` /
`4746e366a35be8a7`. Continuo mean/p95 54.53/130.29 -> 54.12/128.39
with recovery still 5. `probe_tempo_step` PASS. `VPAlign --ramps`
MIXER is now **all PASS**: flats 6.9/33.3 and 7.2/22.0; 30 s
19.9/78.4; 12 s 30.6/93.2; 120->132 **25.7/81.6**; 128->120
**19.1/48.0**. Without the quadratic term one extra matrix F->V
appeared; without the two-vote IOI check, 128->120 worst 48.0->56.5;
without the hold, 120->132 stayed 28.0/95.5.

Direct-path synthetic ramps are green. Step authority and the
lifetime recovery latch are closed in the next kept note. Real
beat-grids and listening remain open.

**Kept (2026-09-18), two consecutive quadratic wins before any bridge
authority.** The 35% first-verdict was the whole of the 17 offset-0
gradino authority frames: one post-step window can briefly beat the
hinge. Requiring `shapeQuadraticWins >= 2` before any lead zeros
those frames and leaves the offset-0 fisso hash `8e3c8d2cdc5854f5`.
Gradino hash moves `4746e366a35be8a7` -> `a6249731026d9f82` because
the old hash included that false 0.30 s tau / 35% rail; mean 35.97
-> 35.96, p95 identical, authority 17 -> 0. Continuo 54.53/130.29
-> 54.03/128.40, 171 -> 27 authority frames (one seed, 129495, one
beat at t=48 with qvh=2.5). `VPAlign --ramps` MIXER stays **all
PASS**: flats 6.9/33.3 and 7.2/22.0; 30 s 19.9/78.4; 12 s **32.7/93.2**
(mean +2.1 vs the strain-keep 30.6, worst unchanged — the first 35%
win on that ramp was after the ramp, at t=30); 120->132 25.9/81.6;
128->120 19.0/48.0. `probe_tempo_step` PASS. Do not restore the 35%
first-verdict: it is the step false-positive.

**Kept (2026-09-18), clock hint on two strained causal votes.** The 12 s
MIXER worst class sits in FISSO with two votes while short residual is
0.054, above `kTempoMotionResidual` 0.030, so the existing PLL hint
never armed and the 93.2 ms peak was decoder-frozen-at-100 plus a
clock still at the held rate. `directTempoMotionHint` now also arms
from two causal votes, residual in (0.045, 0.056) and quadratic g in
[0.10, 0.50) — the same shape band as strain, clock-only, no decoder
residual-ceiling change. Offset-0 matrix hashes stay
`8e3c8d2cdc5854f5` / `6b607d51a504b6a1` / `a6249731026d9f82`.
`VPAlign --ramps` MIXER: flats 6.9/33.3 and 7.2/22.0; 30 s 19.9/78.4;
12 s **32.4/89.4** (was 32.7/93.2); 120->132 25.9/81.6; 128->120
19.0/48.0 unchanged. Decoder columns on the 12 s row were unchanged
before the FISSO walk below. Do not reopen the decoder strain ceiling
from that class (mean 32.7->34.1 when tried).

**Kept (2026-09-18), one `kFixedMaxStep` toward the short fit after
two strained votes, still FISSO.** Same class as the clock hint:
direct feed, `fastDriftBeats >= 2`, residual in
(`kMotionCurveResidual`, `kMotionCurveStrainResidual` 0.056), weak
quadratic, rate and short on the same side of the held number. The
long-fit anchor is skipped that beat so it cannot pull the step back.
This is not an earlier VIVO release and not a residual-ceiling change
for `strainLineRelease` (`kMotionCurveWalkResidual` stays 0.050).
Walking the residual band *without* the two votes moved a 12 s seed
that still had zero votes and made the four-seed mean worse; releasing
on this class did the same (32.7→34.1). With the two-vote gate, offset-0
hashes stay `8e3c8d2cdc5854f5` / `6b607d51a504b6a1` /
`a6249731026d9f82` (fisso/gradino unchanged; the 09:09 control CSV is
stale on gradino). `VPAlign --ramps` MIXER: flats 6.9/33.3 and 7.2/22.0;
30 s 19.9/78.4; 12 s **32.1/82.0** (was 32.4/89.4); 120->132 25.9/81.6;
128->120 19.0/48.0; decoder 18.3/-16.1 (coda was -16.3).
`probe_tempo_step` PASS. Do not enlarge the step: landing on the
lagging short fit would spend the votes. Do not reopen strain release
from this class.

**Rejected (2026-09-18), 6-beat fit as the vote source on a dirty
8-beat line.** The 12 s class first crosses 1.2% on eight beats at
t=25. Using `fitPeriod(6)` only to vote, and only while 8-beat
residual is in (0.045, 0.056), left offset-0 hashes identical but
moved VPAlign 130 MIXER **7.2/22.0 → 7.8/24.4** (decoder 6.5→7.6) and
12 s mean **32.1→32.8** (worst still 82.0). 128→120 improved
19.0/48.0→18.1/47.0. The 130 flat sometimes sits in that residual
band, so a shorter vote window without the two-vote+quadratic gate
is not silent on fixed tempo. Reverted. Do not retry a sub-8-beat
vote on that residual band.

**Rejected (2026-09-18), extending `bringSlowFitCurrent` from 75 to
85 BPM.** Offset-0 continuo p95 is carried by slow unknown/live
runs (seed 216604: 67 BPM, 16ths, kit gap, F->V 0, 5.5% late, 8-beat
window 7 s). Fading the interval-median blend out at 85 instead of
75 left VPAlign 100-130 identical but moved fisso **22.3→27.2** and
changed fisso/gradino hashes. Live-only 85 (unknown kept 75) still
changed the fisso hash: six F->V already exist on that family, and
after those releases the stronger live blend rewrites the trace.
Do not extend the slow-fit fade without a gate silent on those six
fisso releases.

**Rejected (2026-09-18), full interval-median weight in unknown when
spread ≥ kLiveTrend.** The 75 BPM fade leaves a 5% weight at 74 BPM,
so a slow sine that has overshot gets no causal pull from the newest
intervals. Full weight (still ±4%) on `spread >= 0.018` in unknown,
combined with the g≥0.50 curve target, moved fisso/gradino hashes,
dropped continuo authority 27→0, and worsened p95 128.4→129.2
while the mean improved. Settling flats spend unknown beats above
that spread bar. Reverted with the curve-target attempt.

**Rejected (2026-09-18), unknown→VIVO on the same causal votes as
FISSO.** `mayFix` stayed first; `haveWindow` plus 3 votes (or 2 clean
+ interval + window) entered live, with the same catch-up bar as a
FISSO release. VPAlign MIXER and `probe_tempo_step` were identical
(12 s 32.1/82.0, 130 7.2/22.0, 128→120 19.0/48.0). Offset-0 fisso
hash stayed `8e3c8d2cdc5854f5`, but gradino moved
`a6249731026d9f82` → `d8efc65a66765c2e` and continuo got slightly
worse (54.03/128.4 → 54.08/128.6). The votes never fire on the
stuck-unknown class (residual 0.06–0.09, 16ths flip the interval
sign); they do fire on steps still acquiring. Do not hoist FISSO
release into unknown. The discriminator for that p95 class is not
a vote count.

**Rejected (2026-09-18), skip the unknown live-lead on a dirty
8-beat residual.** Same strain ceiling (0.056) the ramps already
use: if short residual is above it, unknown kept the short fit
without extrapolating (short−long) as a rate. VPAlign MIXER
identical, `probe_tempo_step` PASS, but offset-0 fisso/gradino
hashes both moved and continuo mean/p95 got worse (54.03/128.4 →
54.22/128.6). The six existing fisso F→V already spend unknown
beats with residual in that band, so a residual gate on unknown
commit is not silent on flats. Reverted. Do not gate unknown lead
on residual without a predicate that those six never satisfy.

**Rejected (2026-09-18), unknown→VIVO on 50% quadratic
improvement.** `mayFix` first; enter live when the 16-beat curve
removes half the linear error (the same `kMotionCurveImprovement`
that was too late to release FISSO). Gradino hash and VPAlign
MIXER stayed identical; fisso hash moved and fisso mean 22.3→20.2
(the quadratic overfits jitter on some flats still acquiring).
Continuo p95 unchanged: the stuck-unknown class never reaches 50%
improvement (residual 0.06–0.09). Do not enter live from unknown
on curvature while flats can still `mayFix`.

**Rejected (2026-09-18), seed the residual-shape tenure in unknown.**
`shapeMaySeed` once `beatFilled >= kLongFit`, then enter live on the
same two-win quadratic vs hinge `shapeCanLead` uses. Offset-0 hashes
and VPAlign MIXER were bit-identical: gaps and octave/grid resets
clear the tenure before seven points accumulate, so the stuck-unknown
class never reaches two wins (model stays `insufficient`). Entering
live would not have repaired it anyway — unknown already chases at
0.70; the 5% error is the 8-beat target, not the regime. Do not start
a shape tenure from unknown until those resets leave a curve standing.

**Rejected (2026-09-18), clock target = quadratic endpoint on the
strained hint.** Same 2-vote + residual (0.045, 0.056) + weak
quadratic gate already used for trim. VPAlign MIXER 12 s worst
82.0→79.4 (mean 32.1), 128→120 19.0→19.1, flats still PASS; but
offset-0 fisso/gradino hashes both moved (fisso mean 22.26→22.33).
Trim-only was silent on those hashes; changing the clock's target
BPM is not, because the matrix hashes clock tempo and phase. Do
not retarget the PLL from `motionFitBpm` on that gate.

**Rejected (2026-09-18), unknown target = 16-beat quadratic when
g≥0.50.** On the stuck-unknown class the quadratic is the truth at
t=29–31 (g=0.80, 71.8 vs 69.8) and the wrong way after the turn
(g=0.23). Gating on `kMotionCurveImprovement` therefore only takes
the forming-curve window. Wrapped in `beatsInRegime > kLongFit`
that window is already over at 67 BPM with misses; fisso stayed
`8e3c8d2cdc5854f5`, gradino moved, continuo was bit-identical.
Lifted out of `kLongFit` (a step is `|short−long| ≥ 4.5%`) fisso
and gradino both moved, VPAlign 100 MIXER 6.9/33.3→6.6/25.9, 12 s
mean 32.1→32.4, continuo mean 54.03→53.56 with p95 128.4→128.7.
The 24-beat wait is what keeps flats quiet and what misses the
slow forming curve. Do not use `motionFitBpm` as an unknown target
without a gate that those six fisso F→V never satisfy and that
still opens before beat 24 at 60–70 BPM.

**Rejected (2026-09-18), two consecutive clean unknown quadratic
beats as the unknown target, before `kLongFit`.** Existing
constants only: `g ≥ 0.50`, 8-beat residual in
(`kFastLineCleanResidual`, `kMotionCurveResidual`), quadratic
residual below the line, `|mot−short| > 1.2%`, `|short−long| < 1.2%`,
`kShortFit < beatsInRegime ≤ kLongFit`, two beats in a row. Offset-0
`--quick` fisso/gradino hashes stayed `8e3c8d2cdc5854f5` /
`a6249731026d9f82`; `VPAlign --ramps` MIXER identical 32.1/82.0;
`probe_tempo_step` PASS. Continuo p95 128.4→125.1 and BPM error
2.09→2.00, but mean phase 54.03→54.19. The next kit gap then turns
with a clock that is already current. Arming the PLL motion hint on
the same gate made the mean worse still (54.22). A live-style
`kLiveLead` blend toward the quadratic instead of the endpoint
did the same. The gate is globally silent on flats and steps; the
mean-phase cost is the hole after the proof. Reverted. Do not
retarget unknown from the 16-beat quadratic until a follow that
improves mean phase, not only the tail.

**Rejected (2026-09-18), latch the two-win forming-curve proof and
raise the interval-median weight to 1.0 below 75 BPM.** No quadratic
retarget. Offset-0 fisso/gradino hashes stayed identical. Continuo
mean 54.03→54.25 and p95 128.4→129.2, while BPM error fell 2.09→1.93.
On the slow unknown deceleration the newest three intervals are not
a safe "now": after a kit gap they pull rate without pulling phase.
Reverted. Do not raise `bringSlowFitCurrent` from that proof.

**Rejected (2026-09-18), four-beat line as the live/unknown target when
the 8-beat residual is dirty.** `fitPeriod(4)` still indexes onsets on
the committed grid (`guess = 60/bpm`, 0.28-beat gate). On the slow
unknown deceleration that carries continuo p95 the decoder is already
~10% fast, so the newest quarters are snapped onto the stale grid and
the 4-beat slope stays at the held tempo — seed 216604 was
bit-identical 155.9/403.9. Offset-0 fisso/gradino hashes both moved
(fisso mean 22.3→25.5). VPAlign 130 MIXER 7.2/22.0→7.4/20.6. A shorter
window on the same grid cannot name a tempo the grid has already left.
Reverted.

**Rejected (2026-09-18), comb as the live/unknown target when the
8-beat residual is dirty and both fits still agree.** The on-grid gate
(0.18 of the last accepted interval) keeps admitting the stale pulse
once the decoder is ~10% fast, so `fitPeriod` and a reindex on the
median IOI are fitting those peaks, not the band. The comb, which
reads the activation autocorrelation, names the true tempo through
that hole (seed 216604: comb error ~3% vs committed ~11%, salience
above the floor, settled). `pullTowardsComb`'s 35% still weights the
stale line, so committed BPM does not move. Taking the comb as the
target (stale-grid band vs the short fit, |short−long|<4.5%, not
during a rapid/refit, unknown or live past 4 beats) followed the
rate — 74→63 against truth 65→63 — but the clock's 0.90 s unknown/live
hold left phase at 200–450 ms. Offset-0 fisso hash identical;
gradino moved; continuo mean 54.03→54.45 and p95 128.4→130.2;
`VPAlign` 12 s MIXER 32.1→32.5. A current comb is not a phase. Do
not retarget from the fold until the same evidence also steers the
grid, and the gate is silent on every gradino trace.

**Kept (2026-09-18), comb as the *ruler* in unknown, not the target.**
Same discriminator as the rejected retarget (dirty 8-beat residual,
both fits within 4.5%, comb in the stale-grid band vs the short fit,
not during a rapid/refit), but only `TempoRegime::unknown`. Peaks
are judged against the comb period with keep 0.12 (0.18 admits both
pulses when they are ~16% apart) and the long/short/quadratic lines
are indexed on that period. The LS fit still owns tempo and phase.
Offset-0 fisso/gradino hashes stay `8e3c8d2cdc5854f5` /
`a6249731026d9f82`. Continuo mean/p95 **54.03/128.40 → 53.79/127.71**,
recovery 0, authority 27, F→V 13; hash `2a475d11b0366f6f`.
`compare_motion_matrix.py` PASS. `VPAlign --ramps` MIXER identical
6.9/33.3, 7.2/22.0, 19.9/78.4, **32.1/82.0**, 25.9/81.6, 19.0/48.0.
`probe_tempo_step` PASS. Allowing the same ruler in live moved the
gradino hash (mean 35.96→36.03); do not reopen live without a gate
that those traces never satisfy. This is a step on the slow unknown
deceleration, not the closed continuous-motion goal.

**Kept (2026-09-18), comb-fold origin + 3.2% floor + split keep,
unknown, after `kLongFit`.** The 8.7% floor never sees the slow
unknown deceleration (comb−short 4–11%). Lowering it without
steering `lastBeat` mixed the two pulses (mean-only, or 54.4/130
with a tighter keep from the stale origin). Before the peak gate,
`snapStalePulseToCombFold` slides `lastBeat` and `gridAnchorSec`
onto `tempo.beatPhaseFor(comb)` when the shift is at least 0.08
comb-beats and contrast clears `kFoldPhaseContrast`, dumps the
stale beat history, and leaves the short/long readings so this
frame still sees the ruler. Keep then tightens to
`max(0.035, 0.40*split)` when the split is inside 0.12.
Offset-0 fisso/gradino hashes identical. Continuo **53.79/127.71 →
50.93/122.63**, p99.5 462.9→355.1, BPM error 2.09→1.93%, recovery
0, authority 27; hash `5f49eb7680d27cd8`. Seed 216604
153.3/393.7/462.9 → 107.5/312.3/355.1 (bpmErr 5.42→2.89). Live
carriers 192847 and 153252 unchanged. `VPAlign --ramps` MIXER
identical 6.9/33.3, 7.2/22.0, 19.9/78.4, **32.1/82.0**, 25.9/81.6,
19.0/48.0. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_comb_origin.csv`. The remaining continuous hole is
the live clean-rate class, not this unknown stale pulse.

**Kept (2026-09-18), slow live 4-beat/IOI lead.** The 8-beat line
is centred 3.5 beats back; at 60 BPM that is seconds of integrated
phase while residual stays clean (seed 192847: short 3-5% off, IOI
and a 4-beat fit indexed on that IOI sit on the pulse).
`bringSlowFitCurrent` already blends toward the IOI but caps at 4%
and then commits at `kRateLive` 0.30. When live, BPM < 75, both
fits agree (<4.5%), short residual < 0.045, quadratic improvement
≥ 0.50, |IOI−short| > 1.2%, and the comb names the same direction
as the IOI vs the short fit: take the IOI-indexed 4-beat line (or
the IOI if that fit fails) and commit at `kRateAcquiring`. Offset-0
fisso/gradino hashes identical. Continuo **50.93/122.63 →
50.57/122.42**, recovery 0, authority 27; hash `a937dbe9556953a4`.
Seed 192847 113.5/232.2 → 109.7/229.7. 153252 and 216604 unchanged.
`VPAlign --ramps` MIXER identical 32.1/82.0. `probe_tempo_step` PASS. Without the comb-sign
term, slow gradino catch-up at 61-63 BPM fires; do not drop it.
Next A/B control: `/tmp/motion_slow_ioi_clean.csv`. The live class
is smaller, not closed.

**Kept (2026-09-18), slow live Door B: 4-beat closer to IOI after
`kLongFit`, without the quadratic bar.** Door A (g ≥ 0.50) left
the remaining 192847 tail on g=0 frames where the IOI-indexed
4-beat still sat on the pulse. Dropping that g floor, or opening
Door B before `kLongFit`, lights fisso 1009 and gradino
210467/281738. Door B: live, BPM < 75, both fits agree, short
residual < 0.045, |IOI−short| > 1.2%, comb-sign, `bir > kLongFit`,
4-beat residual clean and closer to the IOI than to the 8-beat
line. Offset-0 fisso/gradino hashes identical. Continuo
**50.57/122.42 → 50.03/121.81**, recovery 0, authority 27; hash
`1d164f96706c436c`. Seed 192847 mean 109.7→102.7 (p95 still
229.7). 137414 47.8/108.5→46.6/98.7. 216604 and 153252 unchanged.
`VPAlign --ramps` MIXER identical 6.9/33.3, 7.2/22.0, 19.9/78.4,
**32.1/82.0**, 25.9/81.6, 19.0/48.0. `probe_tempo_step` PASS.
Next A/B control: `/tmp/motion_long_four.csv` (copy
`/tmp/motion_kept_long_four.csv`). The 192847 p95 tail is dirty
residual (0.057–0.073) on the acceleration; Door B does not open
there. Comb-sign still required on Door A: without it, slow
gradino catch-up at 61-63 BPM fires.

**Kept (2026-09-18), Door B without comb-sign.** On a slow
deceleration the fold is the last to turn, so Door A's sign is
false while i4 already sits on the pulse. Comb-sign stays on Door
A; Door B no longer needs it. Silent on the post-Door-B log
(fisso 0, gradino 0, continuo 20 frames). Offset-0 fisso/gradino
hashes identical. Continuo **50.03/121.81 → 49.59/121.35**,
recovery 0, authority 27; hash `a083cd1e01688f3a`. Seed 192847
mean 102.7→100.0 (p95 still 229.7). 224523 49.6/90.6→47.0/83.3.
137414 mean 46.6→44.9. `VPAlign --ramps` MIXER identical
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_door_b_nosign.csv` (copy
`/tmp/motion_kept_door_b_nosign.csv`).

**Kept (2026-09-18), Door B ignores the 8-beat residual.** The
192847 p95 tail is an acceleration where the 8-beat residual is
0.057–0.073 while the IOI-indexed 4-beat residual stays < 0.045
and sits on the pulse. Raising Door A's residual ceiling lights
gradino 210467 at bir 7-8 (g ≥ 0.50, dirty, just after a step).
Door B already trusts the 4-beat; the 8-beat residual is not a
veto. `bir >= kLongFit` (was `>`). Offset-0 fisso/gradino hashes
identical. Continuo **49.59/121.35 → 49.39/120.21**, recovery 0,
authority 27; hash `941f2743f215f20b`. Only seed 192847 moved:
100.0/229.7/248.7 → 96.7/211.5/245.9. `VPAlign --ramps` MIXER
identical **32.1/82.0**. `probe_tempo_step` PASS. Next A/B
control: `/tmp/motion_door_b_dirty.csv` (copy
`/tmp/motion_kept_door_b_dirty.csv`).

**Kept (2026-09-18), Door B above 75 BPM only when the 8-beat is
already this dirty (0.080).** Below 75 BPM Door B trusts the
4-beat with no 8-beat residual veto. At faster tempi that 8-beat
is current enough unless residual ≥ `kMotionCurveFourBeatDirty`:
0.075 lights offset-0 gradino 273819; 0.080 is silent (fisso 0,
gradino 0, continuo 5 frames on two seeds). Door A stays below 75
BPM. Offset-0 fisso/gradino hashes identical. Continuo
**49.39/120.21 → 48.51/117.31**, recovery 0, authority 27; hash
`89464609b5b5cff2`. Seed 153252 51.3/219.8/233.0 → 40.3/159.3/225.0.
145333 mean 73.6→70.5, p95 146.6→160.8 (one frame where i4 ran
away from the comb). `VPAlign --ramps` MIXER identical
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_fast_dirty.csv` (copy
`/tmp/motion_kept_fast_dirty.csv`). The remaining family p95/p995
carrier is still the unknown quiet-grid seed after the comb-fold
snap.

**Kept (2026-09-18), snap re-gates the beat ring onto the comb
fold instead of dumping.** Dumping left a quiet grid for seconds
through a kit gap; shifting every stored time with the origin
kept the stale period (312→402). After the origin slides onto
`beatPhaseFor(comb)`, times that already sit within
`kCombRulerMinKeep` of the new lattice stay, the other pulse is
dropped, `lastBeat` becomes the newest kept time. Offset-0
fisso/gradino hashes identical. Continuo **48.51/117.31 →
46.92/113.16**, recovery 0, authority 27; hash `f16b097470986515`.
Only seed 216604 moved: 107.4/312.3/355.1 → 82.0/245.8/406.1.
`VPAlign --ramps` MIXER identical **32.1/82.0**. `probe_tempo_step`
PASS. Next A/B control: `/tmp/motion_snap_regate.csv` (copy
`/tmp/motion_kept_snap_regate.csv`). The remaining tail on that
seed is still a gap with no on-fold times; rate-from-IOI during
the gap raised mean and p95 (rejected below).

**Kept (2026-09-18), unknown Door B at `kRateLive` when the IOI
has already left the dirty 8-beat by three line-votes.** Live
already had Door B; unknown did not, so a dirty 8-beat with a
clean 4-beat on the pulse stayed on the long+lead (216604 t=52,
sal 0.129, i4 66.2 vs committed 69.6, phase 406 ms). The 4-beat
is the current pulse there; `combReady` is false because
salience is under 0.14, so this sign-check uses `combBpm` (the
fold) without waiting on the floor. Offset-0 fisso/gradino
hashes identical (`8e3c8d2cdc5854f5` / `a6249731026d9f82`).
Continuo **46.92/113.16 → 46.90/113.04**, p995 406.07→405.76,
recovery 0, authority 27; hash `10be2e86ecd8e5f4`. Only seed
216604 moved: 82.0/245.8/406.1 → 81.7/244.0/405.8. `VPAlign
--ramps` MIXER identical **32.1/82.0**. `probe_tempo_step`
PASS. Next A/B control: `/tmp/motion_unk_doorb_live.csv` (copy
`/tmp/motion_kept_unk_doorb_live.csv`). The 406 ms frame is
still a half-beat, not a lag this rate can unwind.

**Kept (2026-09-18), unknown Door B on a clean late 8-beat at
`kUnknownIoiLead` 0.035.** The 406 ms peak is integrated rate
through the deceleration, not a fold-origin error: at t=43.5 the
8-beat is still clean (residual 0.010) and 3.5 beats late, while
the IOI-indexed 4-beat already sits on the pulse. Requiring
walk-residual (0.050) waited until t=52. `kUnknownIoiLead` 0.035
is silent on offset-0 fisso/gradino; 0.033 lights fisso 24766
(click IOI scatter). Offset-0 hashes identical
(`8e3c8d2cdc5854f5` / `a6249731026d9f82`). Continuo
**46.90/113.04 → 46.88/112.49**, p995 405.76→401.39, recovery 0,
authority 27; hash `5e804ba6cce474e2`. Only seed 216604 moved:
81.7/244.0/405.8 → 81.3/235.2/401.4. `VPAlign --ramps` MIXER
identical **32.1/82.0**. `probe_tempo_step` PASS. Next A/B
control: `/tmp/motion_unk_clean_035.csv` (copy
`/tmp/motion_kept_unk_clean_035.csv`). After t=43.5 the phase is
36 ms; the kit gap then pulls the stale comb (~71) at
`kRateAcquiring`, and the first post-gap 8-beat is a *clean*
lattice on that pulse (t=50.2 short=i4=73, residual 0.038,
truth 66). That yank is the ~400 ms peak.

**Kept (2026-09-18), live Door C: a clean 4-beat that is not
closer to the IOI, one beat before Door B.** Door B needs
`i4` closer to the IOI than to the 8-beat and `bir >= kLongFit`
(24). On 192847 t=57.42 `bir=23`, `i4=60.6` sits *between*
short 59.5 and IOI 61.8 (truth 63.5), residual 0.039, fold
unturned, `g=0.046`, so A and B are both closed and phase is
already 198 ms. Taking that 4-beat when `|IOI−short| >
kUnknownIoiLead` (0.035) is silent on offset-0 fisso/gradino
(0.012 and 0.020 light gradino 281738). Offset-0 hashes
identical (`8e3c8d2cdc5854f5` / `a6249731026d9f82`). Continuo
**46.88/112.49 → 46.86/112.43**, p995 401.39 unchanged,
recovery 0, authority 27; hash `abb941f605d4d9d0`. `VPAlign
--ramps` MIXER identical **32.1/82.0** (flats 6.9/33.3 and
7.2/22.0, 30 s 19.9/78.4, 120→132 25.9/81.6, 128→120 19.0/48.0).
`probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_doorc.csv` (copy `/tmp/motion_kept_doorc.csv`).
192847 t=59.32 peak 247→244 ms; 216604's post-gap 73 yank and
401 ms p995 are untouched.

**Kept (2026-09-18), unknown Door B at `kRateAcquiring` when the
8-beat is already strained, stacked with live Door A without
comb-sign after `kLongFit`.** Door B at live on a clean 8-beat
(t=43.5 residual 0.010) must stay at `kRateLive` — acquiring
there raised the family mean. At t=52 the 8-beat residual is
already 0.061 and phase is 401 ms; `kRateAcquiring` on that one
frame is silent on offset-0 fisso/gradino. Alone it moved
continuo mean 46.856→46.860 (FAIL) and p95 112.43→111.97.
Door A without comb-sign after `kLongFit` was mean-only alone
(p95 identical). Together: offset-0 hashes identical
(`8e3c8d2cdc5854f5` / `a6249731026d9f82`). Continuo
**46.86/112.43 → 46.85/111.97**, p995 401.39→400.05, recovery 0,
authority 27; hash `cb0cd495e0cf52a8`. `VPAlign --ramps` MIXER
identical **32.1/82.0**. `probe_tempo_step` PASS. Next A/B
control: `/tmp/motion_stack.csv` (copy
`/tmp/motion_kept_stack.csv`). The post-gap 73 yank and the
~400 ms peak remain.

**Kept (2026-09-18), Door A at `kDoorAIoiLead` 0.010 after two
short windows, Door B still at 1.2%.** 0.012 missed 192847
t=54.56 (ioiDev 0.0107, `g=0.55`, clean, bir=21, fold unturned).
Dropping the shared outer bar to 0.010 without gating Door B
lights fisso 88118 and gradino. Offset-0 hashes identical
(`8e3c8d2cdc5854f5` / `a6249731026d9f82`). Continuo
**46.85/111.97 → 46.75/111.56**, p995 400.05 unchanged, recovery
0, authority 27; hash `41a8424411cbd2d5`. `VPAlign --ramps`
MIXER identical **32.1/82.0**. `probe_tempo_step` PASS. Next A/B
control: `/tmp/motion_doora_010.csv` (copy
`/tmp/motion_kept_doora_010.csv`). 216604's post-gap yank and
~400 ms peak are untouched.

**Kept (2026-09-18), live Door D: 4-beat on a mixed 8/24-beat
window, at `kRateLive`.** Door B requires the two fits to agree,
so it never sees 169090 t=47.58 (short 95.8, long 103.4, i4 91.1
vs truth 91.2). Comb-sign, `|IOI−short| > kUnknownIoiLead`,
4-closer, `bir >= kLongFit` are silent on offset-0 fisso/gradino
(0 frames, all t, all regimes). Residual
`kDoorDFourResidual` 0.015 — 0.045 includes 153252 t=67.36
(r4=0.036) and fattened family p95. Not `slowIoiLeads`:
acquiring-rate yank fattened 169090 p995 210→217. Offset-0
hashes identical (`8e3c8d2cdc5854f5` / `a6249731026d9f82`).
Continuo **46.75/111.56 → 46.71/111.51**, p995 400.05 unchanged,
recovery 0, authority 27; hash `1a86e6856b530715`. `VPAlign
--ramps` MIXER identical **32.1/82.0**. `probe_tempo_step` PASS.
Next A/B control: `/tmp/motion_doord_live.csv` (copy
`/tmp/motion_kept_doord_live.csv`). 216604's post-gap yank and
~400 ms peak are untouched.

**Kept (2026-09-18), clock motion tau on Door B/C/D, not Door A.**
`directTempoMotionHint` was false for live/unknown unless
`bridgeAuthority > 0`, so the PLL stayed on the 0.90 s hold tau
after those doors had already moved the decoder onto a 4-beat.
`BeatHypothesis::ioiLead` is set on live Door B/C, Door D, and
unknown Door B. Door A is excluded: the same live band lights
fisso 1009. Offset-0 B/C/D frames: fisso 0, gradino 0, continuo
46. Do not reuse `motionBridgeAuthority` (that would pull
`bridgedMotionTarget` toward the quadratic). Offset-0 hashes
identical (`8e3c8d2cdc5854f5` / `a6249731026d9f82`). Continuo
**46.71/111.51 → 45.96/110.44**, p995 400.05→401.02, recovery 0,
authority 27; hash `fc4e0a45de4979de`. 192847 99.95/208.9/241.1
→ 93.9/194.6/227.7. 216604's 401 ms peak is the t=52 Door B
frame itself — shorter tau from that frame cannot unwind it.
`VPAlign --ramps` MIXER identical **32.1/82.0**.
`probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_clock_bcd.csv` (copy `/tmp/motion_kept_clock_bcd.csv`).

**Kept (2026-09-18), proven-motion tau (0.15 s) while Door B/C/D
lead.** The KEEP above armed `kGridTauMotion` (0.30 s). Passing
`ioiLead` as the existing proven flag uses `kGridTauProvenMotion`
instead — still no snap, still silent on offset-0 fisso/gradino
(0 B/C/D frames). Offset-0 hashes identical
(`8e3c8d2cdc5854f5` / `a6249731026d9f82`). Continuo
**45.96/110.44 → 45.80/108.30**, p995 401.02→383.15, recovery 0,
authority 27; hash `45151ba57b017436`. `VPAlign --ramps` MIXER
identical **32.1/82.0**. `probe_tempo_step` PASS. Next A/B
control: `/tmp/motion_clock_proven.csv` (copy
`/tmp/motion_kept_clock_proven.csv`).

**Kept (2026-09-18), clock proven tau on live Door A too.** The
previous KEEP excluded Door A because the *log band* (g, residual,
bpm<75, no `bir>=4`) lights fisso 1009 at bir=1, already LIVE
after an F→V. The product block is behind `beatsInRegime >= 4`,
so offset-0 fisso/gradino have **0** Door A frames. Pure-A
continuo frames (12) were still on the 0.90 s hold tau after the
decoder had moved. Offset-0 hashes identical
(`8e3c8d2cdc5854f5` / `a6249731026d9f82`). Continuo
**45.80/108.30 → 45.49/107.76**, p995 383.15 unchanged, recovery
0, authority 27; hash `5f1dab0be1df1039`. `VPAlign --ramps`
MIXER identical **32.1/82.0**. `probe_tempo_step` PASS. Next A/B
control: `/tmp/motion_doora_clock.csv` (copy
`/tmp/motion_kept_doora_clock.csv`).

**Kept (2026-09-18), persist proven tau for `kShortFit` live
beats after a door.** Door D on 169090 is one frame; the peak is
two beats later, after the 8/24 fits agree again and the PLL had
fallen back to 0.90 s. Counting down only while `live` is silent
on offset-0 fisso/gradino (0 door frames) and does not follow
the unknown post-gap 73 yank. Offset-0 hashes identical
(`8e3c8d2cdc5854f5` / `a6249731026d9f82`). Continuo
**45.49/107.76 → 44.22/105.38**, p995 383.15 unchanged, recovery
0, authority 27; hash `290b44562a37f120`. `VPAlign --ramps`
MIXER identical **32.1/82.0**. `probe_tempo_step` PASS. Next A/B
control: `/tmp/motion_live_persist.csv` (copy
`/tmp/motion_kept_live_persist.csv`).

**Kept (2026-09-18), `kGridTauRapid` (0.10 s) while a door
leads, proven 0.15 s still for `bridgeAuthority`.** Same silent
band as persist. Offset-0 hashes identical
(`8e3c8d2cdc5854f5` / `a6249731026d9f82`). Continuo
**44.22/105.38 → 44.16/105.22**, p995 383.15→382.05, recovery 0,
authority 27; hash `df0956c470e98c88`. `VPAlign --ramps` MIXER
identical **32.1/82.0**. Next A/B control:
`/tmp/motion_rapid_ioi.csv` (copy `/tmp/motion_kept_rapid_ioi.csv`).

**Kept (2026-09-18), persist the door tau in unknown too, aborted
on the same-lattice post-gap 8-beat.** Live persist already
covered Door D. Unknown Door B lost the 0.10 s tau on the next
haveShort, and `!haveShort` returned before restoring it, so the
PLL was on 0.90 s through the kit gap. Persisting while
`unknown` is silent on offset-0 fisso/gradino (0 unknown Door B
frames). Aborting when 4-beat and 8-beat agree within 1.2% is
the 73 yank — do not chase it. Offset-0 hashes identical
(`8e3c8d2cdc5854f5` / `a6249731026d9f82`). Continuo
**44.16/105.22 → 44.15/104.91**, p995 382.05→381.77, recovery 0,
authority 27; hash `986f1ccdd8fe0c35`. `VPAlign --ramps` MIXER
identical **32.1/82.0**. Next A/B control:
`/tmp/motion_unk_persist.csv` (copy
`/tmp/motion_kept_unk_persist.csv`).

**Kept (2026-09-18), hold Door D's 4-beat for `kShortFit` live
beats at `kRateLive`.** Door D is one frame; the next beat the
8/24 fits agree and Door B above 75 needs residual 0.080, so
the 4-beat on the pulse is dropped while phase is still
climbing (169090 t=47.58→49.54). Opening that Door B band
lights fisso 88118 and gradino. Holding only after Door D is
silent on offset-0 fisso/gradino (0 Door D frames). Not
`slowIoiLeads`: acquiring overshot 169090. Offset-0 hashes
identical (`8e3c8d2cdc5854f5` / `a6249731026d9f82`). Continuo
**44.15/104.91 → 43.94/104.31**, p995 381.77 unchanged, recovery
0, authority 27; hash `da5514c86f9b0382`. `VPAlign --ramps`
MIXER identical **32.1/82.0**. `probe_tempo_step` PASS.

**Rejected (2026-09-18), hold Door A/B/C's 4-beat the same way
as Door D.** Silent on offset-0 fisso/gradino hashes. Continuo
**43.94/104.31 → 44.44/106.94**. A late Door-A 4-beat held
through the next window is worse than returning to the 8-beat.
Do not persist A/B/C targets.

**Kept (2026-09-18), Door C takes the IOI when the 4-beat has
already left the 8-beat by `kFastDriftToleranceLine`, same
sign.** Door C exists because the 4-beat sits *between* the
late 8-beat and the IOI; targeting the 4-beat leaves the more
current interval on the table. Taking the IOI on every Door C
frame yanks when the 4-beat has not moved (a single displaced
onset). Offset-0 live fisso/gradino: 0 Door C frames. Hashes
identical (`8e3c8d2cdc5854f5` / `a6249731026d9f82`). Continuo
**43.94/104.31 → 43.90/104.26**, p995 381.77 unchanged, recovery
0, authority 27; hash `8b2b134cb8ddd162`. `VPAlign --ramps`
MIXER identical **32.1/82.0**. `probe_tempo_step` PASS.

**Kept (2026-09-18), skip `pullTowardsComb` on live doors and
their ioiLead persist.** The doors exist because the 8-beat is
late; the comb is later (Door C: fold unturned). Pulling 35%
toward it undid the 4-beat/IOI and the Door D hold, the same
way a stale comb undid a confirmed step before the refit
quarantine. Offset-0 fisso/gradino never fire those doors.
Hashes identical (`8e3c8d2cdc5854f5` / `a6249731026d9f82`).
Continuo **43.90/104.26 → 43.08/103.03**, p995 381.77 unchanged,
recovery 0, authority 27, BPM>4% 5.49→5.00; hash
`c78913566bb97268`. `VPAlign --ramps` MIXER identical
**32.1/82.0**. `probe_tempo_step` PASS.

**Kept (2026-09-18), Door D hold walks at 0.45, not 0.30 or
0.70.** Acquiring overshot 169090; live rate left phase climbing
through the hold window. A/B/C stay at acquiring (`slowIoiLeads`).
Offset-0 fisso/gradino: 0 Door D frames. Hashes identical
(`8e3c8d2cdc5854f5` / `a6249731026d9f82`). Continuo
**43.08/103.03 → 42.96/102.66**, p995 381.77 unchanged, recovery
0, authority 27; hash `95b9b2ccd94fb899`. `VPAlign --ramps`
MIXER identical **32.1/82.0**. `probe_tempo_step` PASS.

**Rejected (2026-09-18), unknown Door B takes the IOI when the
4-beat has left the 8-beat (live Door C rule).** Silent on
offset-0 fisso/gradino (0 unknown Door B frames). Continuo mean
42.96→42.92, p95 **102.66→103.13**, p995 381→385. The three
216604 Door B frames have a closer IOI, but swapping them
fattens the family tail. Do not import Door C onto unknown.

**Rejected (2026-09-18), Door B above 75 only while 4-beat and
comb share a lattice (`kStaleGridThreshold`).** Silent on
offset-0 hashes (0 Door B above 75 on fisso/gradino). Continuo
**42.96/102.66 → 43.49/103.27**, BPM>4% 5.00→5.89. Only 145333
moved (mean 62.5→68.0, p95 148→155, max 184→181): closing the
false-peak 4-beat diverges the beat ring and the later windows
are worse. Do not gate the dirty-above-75 door on comb distance.

**Rejected (2026-09-18), Door D hold at 0.50.** Silent on
offset-0 hashes. Continuo mean **42.9623→42.9624** (FAIL), p95
102.662→102.652. 0.45 remains the measured hold rate.

**Kept (2026-09-18), ioiLead clock tau walked 0.10 → 0.08 →
0.06 → 0.04 → 0.02 → 0.01 s.** `kGridTauRapid` stays 0.10: that
constant also times confirmed steps and would move offset-0
gradino. New `kGridTauIoiLead` only. 0.01 is
`TempoFollower::setGridPhase`'s floor (smaller values clamp).
Each step silent on offset-0 hashes (`8e3c8d2cdc5854f5` /
`a6249731026d9f82`), VPAlign MIXER **32.1/82.0**, recovery 0,
authority 27. Continuo **42.96/102.66 → 42.87/102.17**, p995
381.77→378.88; hash `6b02894541050654`. 216604/208685
unchanged; 153252 p95 102.3→97.0 is most of the family move;
192847 158.4→158.3; 145333 148.1→147.1. Next A/B control:
`/tmp/motion_tau01.csv` (copy `/tmp/motion_kept_tau01.csv`).

**Fixed (2026-09-24), the far-target phase shortcut must not clamp a tau that is already under 0.10 s.** After a quarter of a second of the same phase error it used to call `std::clamp(tau, 0.10, phaseTargetTau)`. An ioi-lead follow is 0.01 s, so the upper bound is below the lower one and libc++ aborts on the audio thread (`Bad bounds passed to std::clamp`, `AURemoteIO::IOThread`). A tau already at or under the floor is left alone. Quick bank: no seed moved by more than 0.05 ms. Fisso `8e3c8d2cdc5854f5`, continuo `ca2588bfe0ce70c5`, gradino still 30.308/128.993. The trace hash moved (`f5bfa30e91b39f1a` → `1aeaba66744b5548`) because the path that used to abort now continues.

**Rejected (2026-09-24), refuse an octave snap when the last raw quarter is still within 8% of the held tempo.** 297576 is correct at 106.8, phase 20 ms, then the fold votes a double and the snap publishes 213 for six seconds. Blocking that snap when the quarter has not halved: 297576 53.6/246.1 bpmErr 11.50% → 30.3/220.3 bpmErr 1.24%. Fisso 1009 went the other way, 158.6/396.9 bpmErr 25.78% → 201.8/416.3 bpmErr 47.50%, hash `8e3c8d2cdc5854f5` → `d07b95e02be2a2ec`. Continuo 216604 65.5/147.0 → 131.0/424.6, hash `ca2588bfe0ce70c5` → `4a2aaaae222fbd7f`. The same quarter is how a real octave is still being counted. Reverted. Do not veto the snap on one held interval.

**Kept (2026-09-24), the direct-live phase rail may move only 2% of the tempo per beat.** A saturated 7.5% rail at 123 BPM is the whole of a 123→132 reading in one buffer. The lean still reaches the same ceiling, over about four beats instead of one callback. Rapid windows are not slewed, and a rest still zeroes the lean in the same buffer: slewing that zero brought the pause surge back. The matrix lane does not set `directLivePhaseFollow`: fisso `8e3c8d2cdc5854f5`, continuo `ca2588bfe0ce70c5`, gradino `1aeaba66744b5548` unchanged. `VPAlign --ramps` MIXER stays on the checkpoint (6.8/33.3, 7.4/23.8, 19.9/78.4, 32.1/82.0, 25.8/81.6, 19.0/48.0).

**Kept (2026-09-24), a slightly dirty fixed 4-beat may arm the hold, and only a clean beat confirms.** Residual in [0.03, 0.06) sets `stepFourHoldBpm` when the beat is at least 5% off the held tempo, the 8-beat is still within 3%, and the interval is within 2% and on the same side. It does not publish. The existing door still confirms only under residual 0.03, within 2% of that hold, and more than 5.5% off the 8-beat. 242143 stays fixed at 146 while the truth is already 132: at t=41.96 the 4-beat is 134 (residual 0.031), at t=42.42 it is 132.6 (residual 0.002) and the number is taken. Phase 9.1/45.8/208.6 → 8.7/31.6/208.6. Two beats both in that band must not confirm: that is the flat 64361 pair (residual 0.031 and 0.033) that took 128.6 to 140. 329252's beat at residual 0.039 misses the 2% interval test and does not arm. Fisso hash stays `8e3c8d2cdc5854f5`, continuo `ca2588bfe0ce70c5`. Only 242143 moved. Gradino 30.308/128.993 hash `1aeaba66744b5548` → 30.288/128.101 hash `0c7399beb4632b57`. Recovery 0. `VPAlign --ramps` MIXER unchanged (6.8/33.3, 7.4/23.8, 19.9/78.4, 32.1/82.0, 25.8/81.6, 19.0/48.0) and `--steps` all PASS, worst phase after evidence 24.5 ms.

**Kept (2026-09-24), the dirty-beat arm accepts an interval within 3%, and the confirming beat stays at 2%.** 329252's arming beat (residual 0.039, four 79.9 against a held 85.3) is 2.6% off the interval, so the 2% arm never set. At 3% the next clean beat (residual 0.019, four 78.4) confirms. Phase 25.1/155.4/306.1 → 16.5/93.9/239.9. Only that seed moved. Fisso `8e3c8d2cdc5854f5`, continuo `ca2588bfe0ce70c5`. Gradino 30.288/128.101 hash `0c7399beb4632b57` → 29.751/124.258 hash `2bc74394ed328dfb`. Recovery 0. `VPAlign --ramps` MIXER unchanged and `--steps` all PASS, worst phase after evidence 24.5 ms. Do not widen the confirming beat's 2%.

**Rejected (2026-09-24), slide the grid to the fold's phase on a downward octave snap.** 119794 sits near 400 ms for several seconds after the number has already halved to 67.7. Moving the anchor once, only when the new level is under 75% of the held one and the fold contrast is inside 0.70: 119794 28.3/229.4 → 25.6/228.4, and continuo 216604 65.5/147.0/254.8 → 113.8/416.9/459.9. Continuo 36.551/91.156 hash `ca2588bfe0ce70c5` → 39.558/108.023 hash `f519d892f51253e8`. Fisso hash `8e3c8d2cdc5854f5` → `73e82d4c9212a3e1`. Reverted. Do not spend the fold's half at the snap.

**Rejected (2026-09-18), persist ioiLead 12 beats instead of
8.** Silent on offset-0 hashes. Continuo **42.94/102.59 →
43.00/102.78**. Eight remains the persist window.

**Kept (2026-09-18), skip `pullTowardsComb` on unknown Door B
(ioiClockLead), matching live doors.** Offset-0 fisso/gradino: 0
unknown Door B frames. Hashes identical. Continuo
**42.87/102.17 → 42.86/101.51**, p995 378.88→370.73, BPM>4%
5.00→4.84, recovery 0, authority 27; hash `b5e076ebc2e14727`.
`VPAlign --ramps` MIXER identical **32.1/82.0**.
`probe_tempo_step` PASS. Only 216604 moved: 82.8/201.5/384.6 →
82.0/185.6/375.6. Persist frames still pull (the 73 yank abort
must keep that). Next A/B control: `/tmp/motion_unk_skipcomb.csv`
(copy `/tmp/motion_kept_unk_skipcomb.csv`).

**Rejected (2026-09-21), persist-gap skip comb while
`|comb−bpm|<kCombPullThreshold`.** Silent on offset-0 hashes.
Continuo **42.86/101.51 → 42.87/101.55**. Comb commit during a
persist dropout is how other live holes recover; the unturned
fold and a runaway door do not separate on a 3% band.

**Rejected (2026-09-21), unknown Door A (quadratic, bir>=4,
`kDoorAIoiLead`).** Silent on offset-0 hashes. Continuo
**42.86/101.51 → 48.39/113.97**, p995 371→433. Opening the
quadratic door before `kLongFit` yanks unknown acquisition.
Tightening to `kLongFit`+comb-sign+4-closer was identity.

**Rejected (2026-09-21), live Door A quadratic 0.40 instead of
0.50.** Lights offset-0 gradino (`fb0bf46723408a16`). Continuo
mean 42.86→42.93, p95 101.51→101.92. 0.50 remains the silent
curve bar.

**Rejected (2026-09-21), unknown Door B on a dirty 4-beat
(`r4` in `[kMotionCurveResidual, kMotionCurveFourBeatDirty)`)
that still agrees with the IOI inside the line drift bar,
below 75 BPM after `kLongFit`.** Silent on offset-0 hashes
(the only dirty-4 fisso frame is above 75 BPM). Continuo mean
**42.86→42.80**, p95 **101.51→104.00**, p995 371→356. One
beat earlier on the pulse-aligned dirty 4-beat fattens family
p95 the same way as taking that lattice through the gap.
Clean Door B stays at 0.035.

**Kept (2026-09-22), FISSO publishes the IOI-indexed 4-beat after two beats when that fit has left the 8-beat by more than 5.5%, the 8-beat is still within 3% of the held BPM, the recent interval agrees within 2% and on the same side, and the 4-beat residual is under 0.03.** `fitPeriod(4)` stays on the committed grid and the under-3 BPM refinement never sees these steps. Offset-0 fisso and continuo hashes identical (`8e3c8d2cdc5854f5`, `55ccc5c84d49749d`, continuo 35.48/87.22). Gradino **35.96/174.89 → 33.96/162.11**, p995 548.70 unchanged, recovery 0, authority 0, hash `4dd43630dc0ebc83`. One isolated 4-beat (250062, next beat 186) does not confirm. The runs that drop beats outside the 0.18 keep are not this gate.

**Kept (2026-09-23), VIVO aims the commit at the IOI-indexed 4-beat after two beats when that fit has left the 8-beat by more than 8%, the newest interval agrees within 2% and on the same side, the 4-beat residual is under 0.03, and the two 4-beats agree within 2%.** The rate stays `kRateLive` (0.30); the comb is not pulled in. A 3–4% gap is a ramp overshoot and is not this door. Offset-0 fisso and continuo hashes identical to the current tree (`8e3c8d2cdc5854f5`, `ca2588bfe0ce70c5`, continuo 36.551/91.156). Gradino **33.962/162.108 → 33.537/157.311**, p995 548.70 unchanged, recovery 0, authority 0, hash `f9bf323ff9351137`. Confirms on 234224, 257981 and 297576 only. Walking that aim at Door D's 0.45 instead of `kRateLive` was reverted: fisso hash changed, continuo 36.551/91.156 → 52.481/128.505 with 2 recovery violations and 363 authority frames, gradino 33.537/157.311 → 38.979/159.143. `VPAlign --ramps` MIXER stays on the checkpoint: flats 6.8/33.3 and 7.4/23.8, 30 s 19.9/78.4, 12 s 32.1/82.0, 120→132 25.8/81.6, 128→120 19.0/48.0, all PASS. Seed 257981 mean phase 38.2→22.8 ms; 297576 56.1→59.8 because the tempo arrives before the phase. Do not lower the 8% gap to 6%: offset-0 fisso 88118 confirms.

**Kept (2026-09-23), the FISSO 4-beat hold survives the fixed→live boundary and the release beat confirms it.** The second beat of 329252 is the release itself (t=43.86 i4=78.4 still fixed, gap 5.8%; t=44.62 already live and the gap has fallen to 4.5%, so the 8% live pair never starts). Offset-0 fisso and continuo hashes identical (`8e3c8d2cdc5854f5`, `ca2588bfe0ce70c5`). Gradino **33.537/157.311 → 33.452/155.272**, hash `a22d3c04d2ac06d0`, recovery 0, authority 0. Only seed 329252 moved: phase 36.3/212.5/336.6 → 35.0/179.9/308.9. A confirmed transition that already rewrote the tempo clears the hold. `VPAlign --ramps` MIXER identical to the checkpoint: flats 6.8/33.3 and 7.4/23.8, 30 s 19.9/78.4, 12 s 32.1/82.0, 120→132 25.8/81.6, 128→120 19.0/48.0, all PASS.

**Kept (2026-09-23), the carried second vote takes the tempo.** Walking that vote at `kRateLive` left 329252 at 83.3 on the release beat against a truth of 78.4. The vote is the same second 4-beat the fixed door would have taken, so it now publishes the same way: tempo taken, grid on that 4-beat, one rapid transition. Fisso `8e3c8d2cdc5854f5` and continuo `ca2588bfe0ce70c5` unchanged. Gradino 33.270/153.677 → 32.651/152.146, hash `019cb69fc2aa5963`. Only 329252 moved: 35.0/179.9/308.9 → 25.1/155.4/306.1, bpm error 0.86% → 0.55%. The ordinary live pair still walks at `kRateLive`. `VPAlign --ramps` MIXER is the same checkpoint and `--steps` still passes, worst phase 24.5 ms.

**Rejected (2026-09-23), one-beat phase spend after a clean FISSO leave.** The decoder grid is already on the beat while the clock is still late (218386 t=47.08: grid 19 ms, clock 157 ms; 257981: grid 1 ms, clock 121 ms). Spending that debt on the existing 25% rail, only when the clock was already 0.18 beats off a clean 4-beat, moved gradino 33.537/157.311 → 33.169/149.095 (218386 p95 97.6→58.2, 265900 142.7→99.1, 329252 212.5→164.0) but fisso hash changed and the family got worse: 22.256/76.932 → 22.354/77.187, seed 64361 phase p995 101.7→159.0. Continuo 36.551→36.600, seed 153252 35.0→35.8. Reverted. Requiring the published anchor to already match that 4-beat within 0.06 beats produced the same numbers and was reverted too. Do not reopen this rail on the first live beats.

**Rejected (2026-09-23), one beat of the 25% phase rail on a live 4-beat confirm only.** Fisso and continuo hashes stayed identical. Gradino family 33.452/155.272 → 33.417/155.044, but seed 257981 phase p95 128.4 → 156.2 against `/tmp/motion_curve_live4.csv` (that seed is unchanged by the carried hold). 234224 moved 179.0 → 178.3. 297576 did not move. Reverted. Do not spend that rail on the confirm beat either: the published grid is not yet the beat the clock should land on.

**Rejected (2026-09-23), two beats for the confirmed rapid window.** On 305495 the FISSO 4-beat confirm at t=51.98 already has the grid 10 ms from the beat and the clock 250 ms off; the existing one-beat 25% rail is what the product spends, and the matrix (which does not call `beginTempoTransition`) then takes until ~t=56 to get under 20 ms because the regime stays fixed. A probe that does adopt the transition, compared with the window at one beat (`/tmp/motion_1beat_wired.csv`: fisso hash unchanged, gradino 32.926/152.192, continuo 36.569/91.409) against two beats: spreading the spend made gradino 33.119/153.172 and continuo mean 36.583. Keeping the first-beat denominator at one beat and only continuing into a second beat made gradino 33.014/152.598 and continuo mean 36.570. Fisso hash stayed `8e3c8d2cdc5854f5` both times. On 305495 itself both two-beat clocks do land: 250 ms at t=51.98, then 33 ms by t=53.12, while the one-beat clock is still at 64 ms there. The run-level score barely moves (mean 15.1→14.7, p95 86.5 unchanged) because that spike is above p95. The family cost is one seed, gradino 281738: p95 128.7→136.3. At t=39.90 its tempo is already right (70.4 vs 69.5) and the one-beat clock is on the beat (2.5 ms); the extra beat pulls it to 68 ms, chasing a published phase that is not the beat. Reverted. Do not lengthen that window, and do not keep steering once the clock has already landed. The one-beat rail already closes the quarter; the beats after it are the fixed-regime tau, and a faster tau on that path is the stopped 0.30-vs-0.01 measurement.

**Rejected (2026-09-23), the same hold only after four beats, and only while the long residual is under 0.020 and the short residual is at least 0.030.** Fisso and gradino hashes stayed identical. Continuo 36.551/91.156 → 36.581/91.189, hash `b83baf3b06b80408`, recovery still 0. Reverted. Waiting four beats does not separate the fill from the ramp.

**Rejected (2026-09-23), phase tau 0.30 through every `transitionRefitBeats` window.** Fisso hash identical. Gradino 33.452/155.272 → 33.201/153.207, and the late steps did move: 305495 p95 113.9→100.1, 321333 42.8→32.2, 257981 14.7→8.0, 281738 134.8→133.4. Continuo mean 36.551→36.531 but p95 91.156→91.283, hash `eebb80c251bd7318`, because 169090 p95 167.8→169.9. That seed's phase error is the decoder grid, and a shorter average follows it. Reverted.

**Kept (2026-09-23), the same 0.30 tau only while the regime is still fixed.** Live refit stays on the holding tau. Fisso and continuo hashes identical (`8e3c8d2cdc5854f5`, `ca2588bfe0ce70c5`, 36.551/91.156). Gradino 33.452/155.272 → 33.360/154.375, hash `99f863e93be8d563`. Only two seeds moved, both down: 305495 p95 113.9→100.1 and 242143 46.4→45.8. The product uses it after the one-beat rapid window, because a FISSO confirm does not turn on direct-live. `VPAlign --ramps` MIXER is the checkpoint: flats 6.8/33.3 and 7.4/23.8, 30 s 19.9/78.4, 12 s 32.1/82.0, 120→132 25.8/81.6, 128→120 19.0/48.0, all PASS.

**Kept (2026-09-23), hold the counted rate through a sounding rest.** Once a part is playing, `hyp.beatGap` tells the clock to spend no phase steer in two cases: no beat accepted for 1.5 periods, or the kick body has been gone for more than 1.05 periods while hats and voice still crest (`kitBodyHolding`). The direct-live rail is 7.5% at high: a fifth of a beat of phase debt is spent as 129 BPM against a 120 clock (`/tmp/probe_gap_hold`, open 129.00, held 120.00). A kick train at low-band 0.80 never arms the flag; the same grid continued as hats at 0.02 arms it for 102 frames and the published tempo stays 120.00. A confirmed rapid window still spends. The bank passes lowBand 0, so the body is never heard there, and the quick hashes stay `8e3c8d2cdc5854f5`, `ca2588bfe0ce70c5`, `a22d3c04d2ac06d0`. A crest that still has kick-body energy refreshes the body and is not this rest.

**Kept (2026-09-23), the beats after that rest use 1.5% for fourteen beats.** Zeroing steer only while the rest lasts leaves the debt intact, and the next beats spend it at 7.5%: the same 0.20-beat debt reads 129.00 the moment the hold ends. Eight beats at 3.5% closed 0.28 beats (`/tmp/probe_gap_return`: open 129.00, held 120.00 during the rest and 124.20 once the beats return) but on a song whose own tempo only wanders a couple of BPM the clock then sat on that rail: I WANNA DANCE, 12 s windows 122–128, after 12 s `|clock−bpm|` mean 0.91 p95 4.38 max 6.14, clock 117.70–133.97, 2714 blocks past 4 BPM. The plateaus were −4.50 BPM, which is 3.5% of ~128. 1.5% for fourteen beats still closes a 0.20-beat debt (14 × 0.015 = 0.21) before the live rail returns. Same song after the change: mean 0.74, p95 1.92, max 4.52, clock 118.94–130.88, 69 blocks past 4 BPM. The committed tempo itself still reaches 120.8–130.0; this rail only stops the clock adding another few BPM on top. A confirmed rapid window is not clamped. The guard arms only from `beatGapHold`. Quick bank unchanged: fisso `8e3c8d2cdc5854f5` 22.256/76.932, continuo `ca2588bfe0ce70c5` 36.551/91.156, gradino `bcf2c1d9141d2abb` 32.488/150.903, recovery 0.

**Rejected (2026-09-23), call a rest at 1.20 periods and snap an early crest.** A crest more than 6% of a beat early, with the gap between 1.20 and 2.5 periods, sounding, and the kit body already heard, was treated as the pause: `beatGap`, the long-fit period cleared, the crest snapped onto the grid without `updateTempo`. I WANNA DANCE showed the same ±4.5 BPM clock plateaus with that gate and without it. A beat only 20% late would have armed the eight-beat clamp on an ordinary feel. Reverted. Do not open the rest below 1.5 periods.

**Kept (2026-09-23), the two live 4-beats may sit 4% apart.** The hold was 2%. On 234224 the first clean 4-beat is 137 and the next is 142 (truth 142, 3.3% apart), so the second vote never fired and the clock walked up from the 8-beat. Nothing else in the quick bank has two passing 4-beats between 2% and 4%. Fisso `8e3c8d2cdc5854f5` 22.256/76.932 and continuo `ca2588bfe0ce70c5` 36.551/91.156 unchanged. Gradino 33.360/154.375 → 33.270/153.677, hash `2e763c2ddde7d225`. Only 234224 moved: 71.7/158.2/190.2 → 70.3/147.1/180.0. Rate stays `kRateLive`. The fixed-regime hold is unchanged. `VPAlign --ramps` MIXER is the same checkpoint (flats 6.8/33.3 and 7.4/23.8, 30 s 19.9/78.4, 12 s 32.1/82.0, 120→132 25.8/81.6, 128→120 19.0/48.0) and `--steps` still passes, worst phase after evidence 24.5 ms.

**Kept (2026-09-23), the live pair may be 7.5% off the 8-beat and the interval 2.5% off the 4-beat.** 289657's two beats were 2.1% and then 7.7% outside the old pair, and both 4-beats name the new tempo (88 against a truth of 89). No other quick-bank pair appears. The carried vote keeps the 2% interval test. Fisso and continuo hashes unchanged. Gradino 32.651/152.146 → 32.488/150.903, hash `bcf2c1d9141d2abb`. Only 289657 moved: 42.8/276.3/330.2 → 40.2/256.4/328.0. Rate stays `kRateLive`. `VPAlign --ramps` MIXER is the same checkpoint and `--steps` still passes, worst phase 24.5 ms.

**Rejected (2026-09-23), take the ordinary live pair the way the carried vote does.** The current log has three live confirms and all three name the truth (234224, 289657, 297576); none on fisso or continuo. Publishing tempo, anchor and rapid anyway: fisso `8e3c8d2cdc5854f5` and continuo `ca2588bfe0ce70c5` unchanged, gradino mean 32.488 → 31.262, but 234224's phase went the wrong way, 70.3/147.1/180.0 → 66.9/156.1/202.6. 297576's wrong lattice did improve (p95 246.1 → 228.2, bpmErr 11.50% → 1.21%). The right number taken all at once can still open the phase. The live pair keeps walking at `kRateLive`.

**Rejected (2026-09-23), re-anchor the grid when the newest interval is a hole.** 210467 slows 63→54 and the fit disappears. At t=53.90 the hole reads 20.7 (below `kMinBpm`), the previous quarter reads 54.6 and the comb reads 53.9, while bpm is still 64. Taking the comb and setting the grid on the beat that closed the hole moved only that seed, phase 108.7/493.9/548.7 → 130.3/420.9/516.6. The number is right at once and the phase stays near 400 ms, so the mean goes up. Do not spend the grid on that hole.

**Kept (2026-09-23), take the comb in full when the newest interval is a hole and the quarter before it agrees.** Same frame, no grid move. The two raw quarters never agree, so a two-interval door does not fire. The comb at 1.0, only when it has left the committed tempo by more than 8%, the previous quarter is within 4% of the comb, and the newest interval is a hole (log2 above 0.40, which is how a 20 BPM hole qualifies below `kMinBpm`): only 210467 moved, 108.7/493.9/548.7 → 93.8/339.4/516.6. Fisso `8e3c8d2cdc5854f5` and continuo `ca2588bfe0ce70c5` unchanged. Gradino 31.239/138.647 hash `957235d9b3d026bc` → 30.308/128.993 hash `f5bfa30e91b39f1a`, p995 548.703 → 516.596. Recovery 0. `VPAlign --ramps` MIXER unchanged and `--steps` still PASS, worst phase 24.5 ms. The ungated blind-fit comb at 1.0 is still rejected: it moved continuo.

**Kept (2026-09-23), a straddling fixed 4-beat only when the residual is in [0.06, 0.10).** 289657 stays fixed at 76 while the truth is already 89: at t=50.64 the 4-beat is 84 (residual 0.099) and at t=51.34 it is 86 (residual 0.085), the 8-beat still on 76. Two beats, at least 8% off the held tempo, the 8-beat within 2%, the interval within 5% of the 4-beat, and the second beat further from the held tempo than the first. Opening that from residual 0.03 took flat seed 64361 (truth 128.6) to 140 on two almost-clean beats (residual 0.031 and 0.033) and moved fisso 22.256/76.932 → 22.967/81.952, hash `1dbab2e6e14d4afc`. The floor at 0.06 does not pass there. Only 289657 moved: phase 40.2/256.4/328.0 → 20.2/60.3/310.4. Fisso hash stays `8e3c8d2cdc5854f5`, continuo `ca2588bfe0ce70c5`. Gradino 32.488/150.903 hash `bcf2c1d9141d2abb` → 31.239/138.647 hash `957235d9b3d026bc`. Recovery 0. `VPAlign --ramps` MIXER unchanged (6.8/33.3, 7.4/23.8, 19.9/78.4, 32.1/82.0, 25.8/81.6, 19.0/48.0) and `--steps` still PASS, worst phase 24.5 ms. Do not lower the residual floor back to 0.03.

**Rejected (2026-09-23), do not let `checkGridPhase` shift the anchor when the intervals have already left.** A step lands late on the old grid and three late beats look like a flipped beat; the shift then wipes the history (210467 is blind, ioi 0, from t=48.72). Skipping the shift whenever the recent interval is 5% off the committed tempo: fisso 22.256/76.932 → 24.190/78.968 hash `6237abef47b69208`, continuo 36.551/91.156 → 43.774/107.854 hash `27a80e46bd178413`, gradino unchanged in the p95 and slightly worse. Narrowing it to a clean 4-beat (residual under 0.03, within 2% of the interval, more than 8% off the committed tempo, log2 under 0.45) left continuo and gradino hashes identical and moved only fisso, the wrong way: 22.256/76.932 → 23.735/77.838 hash `45d0b576211b3db5`. The half-beat correction is doing real work on flat tempos. Do not gate it on the intervals.

**Rejected (2026-09-23), take the comb in full when the live 8-beat has gone blind.** On a step the quarters leave the committed grid, `haveShort` fails, and the comb pull is 0.70. At the beat the comb has arrived, 210467 goes 64.0→56.9 against a truth of 54; taking the comb at 1.0 when the gap is over 8% and log2 is under 0.45 (a step, not a 3:2 or a double) would have landed on 53.9. Fisso hash identical. Gradino 33.360/154.375 → 32.446/145.279, hash `882e0032fd2ef254`, almost entirely 210467 p95 493.9→339.4; 273819 p95 170.1→179.0. Continuo 36.551/91.156 → 37.693/92.230, hash `4a09b869c16a036d`. The one continuo frame is 145333 at t=36.78, where the 0.70 pull is already bringing a 196 spike back toward the ramp. Reverted. Do not spend the blind-fit comb at 1.0.

**Rejected (2026-09-23), loosen the grid-step evidence so a noisy step confirms sooner.** On 210467 the truth steps 63→54 at t=46 and the committed number stays near 64 until the comb pull at t=53.88. `observeGridStep` never sees that step: it returns until ten beats of history exist, and the intervals around the change are not consecutive (missed quarters, gaps at t=52–54). The candidates in that tempo band that do reach the test fail `kGridStepEvidence` (the step does not beat a plain offset by 16 σ²) with a relative step of only 3–7%, which is the late stroke the guard was measured on (a +44 ms drop). Do not lower that evidence.

**Rejected (2026-09-23), the matrix clock adopts `beginTempoTransition` the way the product does.** Fisso hash identical. Gradino 33.360/154.375 → 32.912/152.192, hash `dda9879139c03970`: 305495 p95 100.1→86.5, 321333 42.8→33.8, 257981 14.7→7.6, 281738 134.8→128.7. Continuo 36.551/91.156 → 36.569/91.409, hash `d0a46c99bf34529a`, because 224523 mean 26.5→28.1 and p95 76.1→80.3. One ramp pays for the steps. Reverted in the probe. Do not put that adopt on the official matrix lane.

**Rejected (2026-09-23), the same adopt but with the one-beat phase rail suppressed when the new tempo is within 4% of the clock.** The gradino numbers did not move: same hash `dda9879139c03970`, 32.912/152.192. Continuo p95 still rose, 91.156→91.416, hash `06ede18bd828c948`, and 224523 p95 76.1→80.4. The ramp's rapid publication was already more than 4% from the clock, so the gate never saw it. Reverted the follower and the probe.

**Rejected (2026-09-23), snap a crest after a rest of two beats back onto the counted grid.** A crest 70 ms early after two beats still passes the 0.18 keep, and folded into one beat that is about 8% fast. Snapping every such gap: fisso 22.256/76.932 → 28.989/64.701 hash `dd0e1ac7033c5bcd`, continuo 36.551/91.156 → 63.087/181.333, gradino 33.452/155.272 → 54.126/201.011. The missed beats in the bank are real timing. Restricting the snap to `sounding` (the bank never sets it) left the three hashes identical, but a 120 BPM rest with the re-entry 75 ms early produced the same clock span either way: 115.9–126.7 at the crest and 120.1–125.4 over the next eight beats. Reverted. The about-8 BPM a player sees is the size of the direct-live phase-steer rail, 7.5% at high (around 107 BPM that is 8 BPM), not this one interval.

**Rejected (2026-09-23), hold the counted tempo while the long fit is still on it and the short fit has left by 2–8%.** That is the EVERYTIME shape (short walks, long stays, kick still in the beats, so the kit-body hold never arms). Freezing the live target on the published bpm and skipping the comb pull and the motion bridge: fisso 22.256/76.932 → 22.722/76.728 hash `7563352468067861`, continuo recovery 0 → 1 (36.551/91.156 → 36.622/91.225), gradino 33.452/155.272 → 33.123/154.376. Also putting the grid anchor back was worse on fisso (22.786/76.728, hash `6096cf006cfff6ac`) with the same continuo recovery. Reverted both. A short fit a few percent off a still long fit is ordinary jitter on a flat tempo and the start of a ramp. Do not freeze that band. The 8% 4-beat door stays the step path.

**Kept (2026-09-21), two-vote FISSO leave without the 24-beat
window when the quadratic is already at `kMotionCurveImprovement`
(0.50), `|mot−held| > kLeaveFixedError`, and the 4-beat is
still on the 8-beat (`kCurveLatticeAgree` 0.015).** Offset-0
fisso/gradino hashes identical (`8e3c8d2cdc5854f5` /
`a6249731026d9f82`). The previous FISSO *walk* on 4/8
agreement lit gradino because it moved the held number; this
only releases the regime. A step's 4-beat has already left by
2%+ on the offset-0 control log; a continuous ramp's has not.
Continuo **42.86/101.51 → 42.78/101.28**, p995 370.73
unchanged, recovery 0, authority 27, BPM>4% 4.84; hash
`fe5d97305650251b`. `VPAlign --ramps` MIXER 12 s identical
**32.1/82.0** (130 flat 7.2/22.0 → 7.4/23.8, still inside the
9/25 gate). `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_curve_lattice.csv` (copy
`/tmp/motion_kept_curve_lattice.csv`).

**Kept (2026-09-21), same lattice leave on one agreeing
interval and an IOI-indexed 4-beat.** The two-vote committed-grid
fit missed the silent census: the second 1.2% vote arrives after
the 4-beat has left the 8-beat (1.9%), and the held-period
4-beat never sees the moving pulse. Offset-0 hashes identical.
Continuo **42.78/101.28 → 42.62/99.37**, p995 370.73 unchanged,
recovery 0, authority 27, BPM>4% 4.84; hash `fb876bc019fc6ab9`.
`VPAlign --ramps` MIXER 12 s identical **32.1/82.0** (130 flat
still 7.4/23.8). `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_lattice_1vote.csv` (copy
`/tmp/motion_kept_lattice_1vote.csv`).

**Kept (2026-09-21), unknown dirty 4-beat that still agrees
with the IOI (`r4` in `[kMotionCurveResidual,
kMotionCurveFourBeatDirty)`, `|four−IOI|` inside the line
drift bar, below 75 BPM after `kLongFit`): clock tau only,
do not retarget BPM.** Taking that 4-beat as tempo fattened
family p95 (42.86→42.80 / 101.51→104.00). Offset-0 hashes
identical. Continuo **42.62/99.37 → 42.59/99.36**, p995
370.73→370.72, recovery 0, authority 27; hash
`645593c737f855e8`. `VPAlign --ramps` MIXER 12 s identical
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_dirty4_clock.csv` (copy
`/tmp/motion_kept_dirty4_clock.csv`).

**Kept (2026-09-21), live Door B does not take a 4-beat that
is octave-apart from the 8-beat (`|log2(four/short)| <
kOctaveThreshold`).** Offset-0 Door B frames with that split:
0 fisso/gradino. A mixed post-gap 8-beat plus an IOI-indexed
4-beat is not a refinement of the line; the octave snap owns
disagreements this wide. Continuo **42.59/99.36 → 42.38/99.09**,
p995 370.72 unchanged, recovery 0, authority 27, BPM>4%
4.84→4.89; hash `ea677c1bb4134a0b`. `VPAlign --ramps` MIXER
12 s identical **32.1/82.0**. `probe_tempo_step` PASS. Next
A/B control: `/tmp/motion_octave_veto.csv` (copy
`/tmp/motion_kept_octave_veto.csv`).

**Kept (2026-09-21), unknown Door C on a dirty 4-beat that is
not closer to the IOI (`r4` in `[kMotionCurveResidual,
kMotionCurveFourBeatDirty)`, `|IOI−short| > kUnknownIoiLead`,
comb-sign, below 75 BPM after `kLongFit`): take the IOI at
`kRateAcquiring`.** Same geometry as live Door C; the dirty
4-beat has not left the 8-beat so Door B stays closed, but the
IOI and comb already have. The 73 yank is a same-lattice
4-beat with clean r4 and does not fire. Offset-0 hashes
identical. Continuo **42.38/99.09 → 42.26/98.73**, p995
**370.72→339.12**, recovery 0, authority 27; hash
`ccb188856d2bd818`. `VPAlign --ramps` MIXER 12 s identical
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_unk_doorc.csv` (copy
`/tmp/motion_kept_unk_doorc.csv`).

**Rejected (2026-09-21), live recede to the comb when the
8-beat has left it by >5% same-octave, g<0.10, residual
clean, after `kLongFit`.** Silent on offset-0 hashes.
Continuo **42.26/98.73 → 42.98/99.73**. Holding the comb
while the accepted beats have formed a false lattice costs
phase on the hashed clock. Do not recede live BPM onto the
comb from that band.

**Rejected (2026-09-21), live Door A above 75 BPM at g≥0.80,
`r4 < kDoorDFourResidual`, 4-closer, after `kLongFit`.**
Silent on offset-0 hashes (`g≥0.50` still lights gradino
234224 at g=0.52). Continuo mean **42.26→41.51**, p95
**98.73→100.37**. 169090 130→160 and 153252 97→127: taking
that 4-beat as tempo overshoots. g≥0.99 is the same 169090
frames. Do not open Door A above 75.

**Kept (2026-09-21), hold unknown Door B's 4-beat for
`kShortFit` at `kRateDoorHold`, including `!haveShort` kit
gaps, and skip the same-lattice abort while the hold is
live.** Live A/B/C hold fattened family p95; this is the
Door D hold in unknown. Offset-0 fisso/gradino: 0 unknown
Door B frames. Hashes identical. Continuo **42.260/98.729 →
42.257/98.334**, p995 **339.12→337.08**, recovery 0,
authority 27; hash `4341204575f0e1b2`. `VPAlign --ramps`
MIXER 12 s identical **32.1/82.0** (130 flat 7.4/23.8).
`probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_unk_hold.csv` (copy
`/tmp/motion_kept_unk_hold.csv`).

**Rejected (2026-09-21), live Door C above 75 BPM when the
8-beat has left the comb by >5% same-octave and the IOI still
sits on it (`r4` dirty, 4-not-closer, `|IOI−short| >
kUnknownIoiLead`).** Silent on offset-0 hashes (the residual
band without the 5%/3% split lights fisso 88118 and gradino
234224). Continuo **42.257/98.334 → 42.794/99.033**. Taking
that IOI while the accepted beats are still the false lattice
costs phase the same way receding onto the comb did. Do not
open Door C above 75.

**Kept (2026-09-21), unknown same-lattice post-gap 8-beat
(`|four−short|` inside the line drift bar, `|IOI−short| >
kUnknownIoiLead`, comb-sign, `|comb−short| > 3%`, below 75 BPM
after `kLongFit`): commit the comb, do not arm `ioiClockLead`.**
Taking the IOI here fattened family p95 (G1) because it kept
the 73 lattice on the proven tau. The comb is already on that
IOI; abortYank still drops the tau. `|comb−short|>3%` is 0
unknown frames on offset-0 fisso (24766 is 0.14%). Hashes
identical. Continuo **42.257/98.334 → 42.249/98.010**, p995
**337.08→319.37**, recovery 0, authority 27; hash
`db9d03ae88889232`. `VPAlign --ramps` MIXER 12 s identical
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_yank_comb.csv` (copy
`/tmp/motion_kept_yank_comb.csv`).

**Kept (2026-09-21), unknown Door B at rate 1.0 when the 8-beat
residual is already past strain.** Clean 8-beat Door B stays
`kRateLive` (acquiring there overshot t=43.5). Strain is one
216604 frame (t=52, i4=66.2 vs T=64.5): 0.70 left 1.7 BPM on
the table; 1.0 takes the 4-beat this frame. Offset-0
fisso/gradino hashes identical. Continuo **42.249/98.010 →
42.231/97.784**, p995 **319.37→318.43**, recovery 0, authority
27; hash `a4b488d7c03e10c2`. `VPAlign --ramps` MIXER 12 s
identical **32.1/82.0**. `probe_tempo_step` PASS. Next A/B
control: `/tmp/motion_doorb_take.csv` (copy
`/tmp/motion_kept_doorb_take.csv`).

**Kept (2026-09-21), unknown comb ruler/snap without the 0.14
salience floor.** Door B already names this comb at sal 0.129
(216604 t=52); the floor left that frame on the 69.9 lattice.
`tempo.ready()` and `levelSettled()` stay. Snap-geom below the
floor is 1 offset-0 continuo frame, 0 fisso/gradino. Hashes
identical. Continuo **42.231/97.784 → 42.213/97.593**, p995
**318.43→316.56**, recovery 0, authority 27; BPM>4% 4.83→5.37
(continuo only). Hash `683da3813129fd5b`. `VPAlign --ramps`
MIXER 12 s identical **32.1/82.0**. `probe_tempo_step` PASS.
Next A/B control: `/tmp/motion_snap_nosa.csv` (copy
`/tmp/motion_kept_snap_nosa.csv`).

**Kept (2026-09-21), unknown comb snap at walk residual 0.050
instead of strain 0.056.** 216604 t=51.10 is 0.051 (Door C
IOI, comb 67.3 vs short 71.4). Strain waited one more beat
and left 322 ms on the 69.9 lattice; snapping here re-gates
onto the comb a beat earlier. Offset-0 fisso/gradino: 0 extra
frames (clean-lattice ruler with no residual floor fattened
p995). Hashes identical. Continuo **42.213/97.593 →
42.011/96.610**, p995 **316.56→278.14**, recovery 0, authority
27; BPM>4% 5.37→4.84. Hash `d75cd5bc736337e2`. `VPAlign
--ramps` MIXER 12 s identical **32.1/82.0**. `probe_tempo_step`
PASS. Next A/B control: `/tmp/motion_snap_walk.csv` (copy
`/tmp/motion_kept_snap_walk.csv`).

**Kept (2026-09-21), live Door A below 75 BPM at rate 1.0 after
two short windows (`bir >= kShortFit*2+4`).** Acquiring (0.70)
left the 4-beat on the table; unknown Door C at 1.0 fattened
p95. This band is 0 offset-0 fisso/gradino (210467 is bir 13).
Hashes identical. Continuo **42.011/96.610 → 41.868/96.582**,
p995 unchanged 278.14, recovery 0, authority 27. Hash
`e5258be93c6e6dd3`. `VPAlign --ramps` MIXER 12 s identical
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_doora_take.csv` (copy
`/tmp/motion_kept_doora_take.csv`).

**Kept (2026-09-21), unknown `!haveShort` comb at rate 1.0 when
the comb has receded below the committed number.** Leftover
comb is faster than the line (216604 t=47, 71 vs T 67);
skipping it or slowing acquiring fattened p95 because t=48.56
recovers 143→11 on that pull. After Door C the comb is already
*slower* than the clock (t=51.56, 66.5 vs 67.1); 0.70 left
1.6 BPM on the table. `lineFeed`, unknown, `bpm<75`,
`bir>=kLongFit`, `comb<bpm`, `>0.5%`: 0 offset-0 fisso/gradino
(48523 is 77 BPM; 119794 is 0.0%). Hashes identical. Continuo
**41.868/96.582 → 41.692/95.838**, p995 unchanged 278.14,
recovery 0, authority 27. Hash `706dd88d352d78c9`.
`VPAlign --ramps` MIXER 12 s identical **32.1/82.0**.
`probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_gap_recede.csv` (copy
`/tmp/motion_kept_gap_recede.csv`).

**Kept (2026-09-21), Door D 4-beat residual up to
`kMotionCurveResidual` when the 8-beat is already this dirty
(`sres >= kMotionCurveFourBeatDirty`) and the 4-beat sits on
the IOI (`|four−IOI| < 1.2%`).** Opening Door D at r4<0.045
without those guards took 153252 t=65.06 (i4=153 vs T=132) and
fattened p95. t=67.36 is r4=0.036, sres=0.081, i4=132.7 vs
T=131.3: 0 offset-0 fisso/gradino. Hashes identical. Continuo
**41.692/95.838 → 41.689/95.086**, p995 unchanged 278.14,
recovery 0, authority 27. Hash `897a6a33cd8ddfbb`.
`VPAlign --ramps` MIXER 12 s identical **32.1/82.0**.
`probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_doord_dirty8.csv` (copy
`/tmp/motion_kept_doord_dirty8.csv`).

**Kept (2026-09-21), FISSO leave below 75 BPM when the IOI has
left the held number by `kUnknownIoiLead` (0.035), quadratic
50%, `bir >= 12`, one agreeing interval.** Lattice leave waits
for `|short−held|>2%` and 4-on-8; at 192847 t=29.92 the IOI is
already 7% out and the 4-beat has left the 8-beat. Off-lattice
1-vote lights gradino; this band is 0 offset-0 fisso/gradino.
Hashes identical. Continuo **41.689/95.086 → 41.562/94.812**,
p995 unchanged 278.14, recovery 0, authority 27. Hash
`42c92feb74c55772`. `VPAlign --ramps` MIXER 12 s identical
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_fisso_ioi.csv` (copy
`/tmp/motion_kept_fisso_ioi.csv`).

**Kept (2026-09-21), unknown `!haveShort` leftover-faster comb
at `kRateLive` (0.30) instead of acquiring.** Recede 1.0 stays
when `comb<bpm`. Skipping the leftover commit entirely fattened
p995 (t=48.56 143→11 ms is that pull). 0.70 climbed 70.1→71.2
while T fell through 67. The 0.5% floor is silent on offset-0
fisso 119794 (0.0%). Hashes identical. Continuo
**41.562/94.812 → 41.551/94.793**, p995 **278.14→268.82**,
recovery 0, authority 27. Hash `7420a17859fc6e5b`.
216604 max 275.0→267.9. `VPAlign --ramps` MIXER 12 s identical
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_leftover_live.csv` (copy
`/tmp/motion_kept_leftover_live.csv`).

**Kept (2026-09-21), unknown `!haveShort` leftover-faster comb
at `kRateLeftoverComb` (0.15).** 0.30 is KEEP vs acquiring;
skip-to-zero fattened p995. Same 0.5% floor, bpm<75, kLongFit.
Hashes identical. Continuo **41.551/94.793 → 41.538/94.784**,
p995 **268.82→261.50**, recovery 0, authority 27. Hash
`25f0bf73f3ae4877`. `VPAlign --ramps` MIXER 12 s identical
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_leftover_015.csv` (copy
`/tmp/motion_kept_leftover_015.csv`).

**Kept (2026-09-21), unknown `!haveShort` leftover-faster comb
at `kRateLeftoverComb` 0.10.** Same band as 0.15. Hashes
identical. Continuo **41.538/94.784 → 41.533/94.780**, p995
**261.50→258.62**, recovery 0, authority 27. Hash
`2daaa77c190a346d`. `VPAlign --ramps` MIXER 12 s identical
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_leftover_010.csv` (copy
`/tmp/motion_kept_leftover_010.csv`).

**Kept (2026-09-21), unknown `!haveShort` leftover-faster comb
at `kRateLeftoverComb` 0.05.** Same band. Skip-to-zero still
REJECT. Hashes identical. Continuo **41.533/94.780 →
41.527/94.773**, p995 **258.62→254.81**, recovery 0, authority
27. Hash `031779d97ab1d21f`. `VPAlign --ramps` MIXER 12 s
identical **32.1/82.0**. `probe_tempo_step` PASS. Next A/B
control: `/tmp/motion_leftover_005.csv` (copy
`/tmp/motion_kept_leftover_005.csv`).

**Rejected (2026-09-21), live comb *gate* (no snap) at 5–8.7%
(`log2` 0.070–`kStaleGridThreshold`), `sres>kFastLineCleanResidual`
0.030, `bpm>75`, `kLongFit`, fits still agree.** Previous-beat
census on leftover-0.05 was 0 offset-0 fisso/gradino (wider >5%
gate and live snap both moved `a624`; 273819 t=42.88 is sres
0.023). Hashes identical. Continuo **41.527/94.773 →
42.248/99.952**, p995 unchanged 254.81, recovery 0. Gating the
174 lattice (145333 t=57.84) without a new origin leaves the
clock on the first false peak. Live yank-to-comb and live snap
already REJECT. Do not reopen the comb ruler on live.

**Rejected (2026-09-21), live false-lattice comb at
`kRateLeftoverComb` 0.05 (no tau, no gate) in the same 5–8.7%
band.** Hashes identical. Continuo **41.527/94.773 →
42.280/96.519**. The PLL still follows the 174 times; walking
BPM toward the comb without a new origin is the same geometry as
live yank, only slower. Do not retarget live BPM onto the comb
while accepted beats are that lattice.

**Kept (2026-09-21), pull `gridAnchorSec` toward the IOI-indexed
4-beat origin while `ioiClockLead`, at most `kCombRulerTolerance`
(0.12) of that period, only while the 4-beat is closer to the IOI
than to the 8-beat.** Live already publishes the 8-beat origin.
Door D can name a 4-beat on the pulse (169090 t=47.58 i4≈T) while
the 0.01 s tau follows that late origin, so phase climbed 130→190
ms as BPM caught. Switching the origin outright was grid jerk
(`anchorBlend`). Offset-0 doors are 0 fisso/gradino, so the pull
is inert there. Hashes identical. Continuo **41.527/94.773 →
39.686/94.047**, p995 **254.81→236.94**, recovery 0, authority
27. Hash `d70649ee12079208`. `VPAlign --ramps` MIXER 12 s
identical **32.1/82.0** (flats 6.8/33.3 and 7.4/23.8).
`probe_tempo_step` PASS. Next A/B control then:
`/tmp/motion_phase4.csv` (copy `/tmp/motion_kept_phase4.csv`).

**Kept (2026-09-21), same origin pull on live Door C geometry
(`fourBetween`: 4-beat between short and IOI, left the 8-beat by
`kFastDriftToleranceLine`).** fourCloser skipped 192847 t=57.42
(i4=60.6 sits between short 59.5 and IOI 61.8; r4=0.039).
Unknown is not `fourBetween`: pulling 216604 t=51.10 (Door C,
i4=70 vs IOI 67) had already fattened that seed's p95 146→176
under the fourCloser KEEP. Raising the live cap 0.12→0.18
(`kOnGridTolerance`) was identity on family p95 (hash moved,
mean −0.00004 ms). Restricting the KEEP to live-only would give
back 216604's mean and fail compare vs phase4. Hashes identical.
Continuo **39.686/94.047 → 39.526/93.936**, p995 unchanged
236.94, recovery 0, authority 27. Hash `3d8e2cab94a8e42b`.
`VPAlign --ramps` MIXER 12 s identical **32.1/82.0**.
`probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_doorc_origin.csv` (copy `/tmp/motion_kept_doorc_origin.csv`).

**Rejected (2026-09-21), live origin-pull cap 0.12→0.18
(`kOnGridTolerance`) vs Door C origin KEEP.** Hashes
identical. Continuo mean 39.526→39.526 (−0.00004 ms), p95
**93.935533→93.935541**. The 192847 p95 frames are rate lag
(origins already agree); extra cap does not touch them. Reverted.

**Kept (2026-09-21), clock-only `ioiClockLead` above 75 BPM:
quadratic ≥0.80, 4-beat on the IOI (`kFastDriftToleranceLine`),
`r4<kFastLineCleanResidual`, fits agree, `bir>=kLongFit`, no BPM
retarget.** Door A above 75 overshoots (177009 t=53.04). 0.50
quadratic lights gradino 234224 t=47.76. 0.80 is 0 offset-0
fisso/gradino (42 continuo frames; 169090 t=43.16 before Door D).
Hashes identical. Continuo **39.526/93.936 → 38.316/93.856**,
p995 unchanged 236.94, recovery 0, authority 27. Hash
`9ff61148a61f14ef`. `VPAlign --ramps` MIXER 12 s identical
**32.1/82.0**; 120→132 MIXER 25.9→25.8. `probe_tempo_step` PASS.

**Kept (2026-09-21), same clock-only gate at `bir>=kShortFit*2`
(16) instead of `kLongFit`.** Still 0 offset-0 fisso/gradino
(46 vs 42 continuo frames on the Door C origin log). A 12 s ramp
never reaches 24 live beats inside the scored window. Hashes
identical. Continuo **38.316/93.856 → 38.073/93.838**, p995
unchanged, recovery 0, authority 27. Hash `14909f5a42b3f7f5`.
`VPAlign --ramps` MIXER 12 s still **32.1/82.0** (those seeds
do not hit 0.80 quadratic + 4-on-IOI). `probe_tempo_step` PASS.

**Kept (2026-09-21), same clock-only gate at `bir>=kShortFit`
(8).** Still 0 offset-0 fisso/gradino (44 vs 42 continuo; extra
113657 t=34.38 and 208685 t=41.16). `bir>=4` lights fisso
64361. Hashes identical. Continuo **38.073/93.838 →
37.791/93.832**, p995 unchanged, recovery 0, authority 27. Hash
`a346ef4d20e36958`. `VPAlign --ramps` MIXER 12 s still
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_clockonly_bir8.csv` (copy
`/tmp/motion_kept_clockonly_bir8.csv`).

**Rejected (2026-09-21), Door D clock-only at `|IOI−short|>1.2%`
(not 3.5%), comb-sign, fits disagree, `r4<0.045`, `bir>=16`,
bpm≥75, no BPM retarget.** 0 offset-0 fisso/gradino. Continuo
mean 37.791→37.713, p95 **93.832→94.021**. 0.01 s tau on
145333 t=67.20 (i4=162 after the 174 lattice) fattened family
p95. Reverted.

**Rejected (2026-09-21), Door A four-lead as BPM when the IOI
is quiet (`|i4−short|>0.010` and `|IOI−short|≤0.010`), bpm<75,
quadratic ≥0.50, `r4<0.045`, fourCloser.** Aimed at 192847
t=68.44 (i4=65.3 vs IOI 66.0, T 64.2). Hits **fisso 1009
t=63.22** (i4=73.2 vs IOI 69.6 vs short 69.4, held 67.3,
regime FISSO). Hash-unsafe. Do not implement.

**Kept (2026-09-21), live clock-only four-lead when the IOI is
quiet.** Same geometry as the Door A four-lead reject, but
live-only (1009 is FISSO), no BPM retarget, quadratic ≥0.50,
`r4<kMotionCurveResidual`, `|i4−short|>kDoorAIoiLead`,
fourCloser, `bir>=kShortFit`. 0 offset-0 fisso/gradino (5
continuo hits: 192847 t=68.44 and t=85.52, 137414, 224523).
Hashes identical. Continuo **37.791/93.832 → 37.733/93.535**,
p995 unchanged 236.94, recovery 0, authority 27. Hash
`523e9187717ef6c8`. 192847's decelerando frame t=70.32 dropped
145→109 ms (seed max moved to the FISSO-leave at t=29.92).
`VPAlign --ramps` MIXER 12 s still **32.1/82.0**.
`probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_kept_fourlead_clock.csv`.

**Rejected (2026-09-21), live Door B above 75 as BPM when the
4-beat is clean on the IOI, quadratic ≥0.80, `r4<0.030`,
`bir>=kLongFit`, `|IOI−short|>1.2%`.** 0 offset-0 fisso/gradino
(37 continuo; 177009 t=53.04 is g=0.63). Continuo mean
37.733→37.433, p95 **93.535→94.762**. 169090 t=43.16 took
i4=94.6 (T 93.3) at 0.70 and the mixed window no longer
disagreed at Door D t=47.58; that seed's max 159→217. Live
Door B at 1.0 already REJECT. Reverted.

**Kept (2026-09-21), skip unknown leftover-faster origin pull.**
`fourCloser` origin pull while `comb>bpm` by 0.5% (216604
t=46.14 i4=69.3 vs comb 71.5, T 68.4). Live-only origin
restrict failed the family mean; this only skips the leftover
unknown frames. 0 offset-0 fisso/gradino (`ioiClockLead` is
already 0 there). Hashes identical. Continuo **37.733/93.535 →
37.676/92.422**, p995 236.94→254.81 (216604 gap max 240→257
at t=51.56; that seed's p95 180→171, t=46.14 167→125).
Recovery 0, authority 27. Hash `244c639d3d7ce673`.
`VPAlign --ramps` MIXER 12 s still **32.1/82.0**.
`probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_kept_unk_skip_leftover_origin.csv`.

**Rejected (2026-09-21), Door B above 75 BPM take at `kRateLive`
0.30 (no `slowIoiLeads`) on the same 4-on-IOI g80 lattice.**
Hashes identical. Continuo **37.676/92.422 → 37.746/93.601**.
0.70 already destroyed 169090's Door D; 0.30 still fattens
mean and p95. Do not retarget BPM on that lattice. Reverted.

**Kept (2026-09-21), Door C IOI target plus `kLiveLead` of the
4-beat→IOI gap.** Same 4% cap as the 8-beat live lead. Door C
at 1.0 REJECT; this only moves the target (192847 t=57.42
i4=60.6 IOI=61.8 T=63.5). 0 offset-0 Door C frames. Hashes
identical. Continuo **37.676/92.422 → 37.655/92.357**, p995
unchanged 254.81, recovery 0, authority 27. Hash
`f482abaac114da87`. `VPAlign --ramps` MIXER 12 s still
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_kept_doorc_lead.csv`.

**Kept (2026-09-21), FISSO `slowIoiFixedRelease` at quadratic
≥0.45 and `|IOI−held|>0.025` (was 0.50 / 0.035).** 0.028 with
`|short−held|>1.2%` and no extra quadratic was identity on p95
when 192847's tail was the hole. After four-lead clock that
leave frame is the seed max. 0 offset-0 fisso/gradino, one
continuo (192847 t=29.04). Hashes identical. Continuo
**37.655/92.357 → 37.546/91.811**, p995 unchanged 254.81,
recovery 0, authority 27. Hash `37c3b1a2cbbc1887`.
`VPAlign --ramps` MIXER 12 s still **32.1/82.0**.
`probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_kept_fisso_g45.csv`.

**Rejected (2026-09-21), FISSO leave at g≥0.40 and `|IOI−held|>0.020`.**
0 offset-0 fisso/gradino on the log (192847 t=28.06), but
`intervalAgreesNow` does not fire: identity vs g 0.45 / 0.025
(hash `37c3b1a2cbbc1887`). Reverted.

**Rejected (2026-09-21), Door C IOI lead at 1.0×(IOI−4-beat)
instead of `kLiveLead`.** Hashes identical. Continuo
**37.546/91.811 → 37.758/92.818**. Overshoots the hole catch.
`kLiveLead` stays. Reverted.

**Kept (2026-09-21), Door C `fourBetween` origin pull toward
`lastBeat` (IOI) not the 4-beat origin.** 4-beat origin was a
no-op on 192847 t=57.42; lastBeat is the current interval.
Cap still `kCombRulerTolerance`. 0 offset-0 fisso/gradino.
Hashes identical. Continuo **37.546/91.811 → 37.470/91.653**,
p995 unchanged 254.81, recovery 0, authority 27. Hash
`af1d9139933c6c17`. `VPAlign --ramps` MIXER 12 s still
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_kept_doorc_lastbeat.csv`.

**Rejected (2026-09-21), fourBetween lastBeat cap 0.12→0.18.**
Hashes identical. Identity on family p95 (the KEEP shift is
already inside 0.12). Reverted.

**Rejected (2026-09-21), clock-only Door A in quadratic
`[0.40, 0.50)`, fourCloser, `|ioiDev|>kFastDriftToleranceLine`,
`r4<kMotionCurveResidual`, `bir>=kShortFit`, BPM&lt;75.** 0
offset-0 fisso/gradino (`|ioiDev|>0.010` lights gradino 210467
t=86.50; 1.2% drops it). Continuo **37.470/91.653 →
37.518/91.977**. 0.01 s tau on a 0.40 quadratic jerks the
clock. Door A BPM at 0.40 already REJECT. Reverted.

**Kept (2026-09-21), fourCloser origin pull toward `lastBeat`
after a hole longer than `kGridStaleBeats` (2.5 periods).** The
4-beat origin after 192847's 4 s miss is the folded hole, not
the first onset. fourBetween already uses lastBeat; this is the
same geometry on fourCloser. Cap still `kCombRulerTolerance`.
0 offset-0 fisso/gradino (`ioiClockLead` never set there).
Hashes identical. Continuo **37.470/91.653 → 37.470/91.621**,
p995 unchanged 254.81, recovery 0, authority 27. Hash
`b8ca632526b2226c`. `VPAlign --ramps` MIXER 12 s still
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_kept_gap_origin.csv`.

**Rejected (2026-09-21), unknown Door C lastBeat origin pull
(dirty `r4` in `[kMotionCurveResidual, kMotionCurveFourBeatDirty)`,
`!fourCloser`).** 0 offset-0 fisso/gradino (2 continuo, 216604
t=51.10). Hashes identical. Continuo mean **37.470→37.656**,
p95 91.621→91.577, p995 254.8→246.2. Helped the tail, fattened
the mean. Pulling `a4` on that frame already REJECT. Reverted.

**Kept (2026-09-21), fourCloser origin pull toward `lastBeat`
always, period = IOI.** closerAfterGap KEEP is the hole case;
Door D's 4-beat intercept after a mixed window is still the
late grid (169090 t=47.58). Same cap. 0 offset-0 fisso/gradino.
Hashes identical. Continuo **37.470/91.621 → 37.425/91.187**,
p995 unchanged 254.81, recovery 0, authority 27. Hash
`14672404b4a68188`. `VPAlign --ramps` MIXER 12 s still
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_kept_fourcloser_lastbeat.csv`.

**Kept (2026-09-21), persist ioiLead lastBeat origin (live, when
fourCloser/fourBetween miss).** Same cap. Unknown Door C lastBeat
already REJECT. 0 offset-0 fisso/gradino (`ioiClockLead` never
set there). Hashes identical. Continuo **37.425/91.187 →
37.259/90.921**, p995 unchanged 254.81, recovery 0, authority
27. Hash `4ffd0e25daf5b15a`. `VPAlign --ramps` MIXER 12 s still
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_kept_persist_lastbeat.csv`.

**Rejected (2026-09-21), on-grid origin = `gridAnchor`, drop the
`kGridStaleBeats` waive.** Aimed at hats-only (off-beat hats after
a hole flipping lastBeat). 5–20% gradino peaks are inside keep of
lastBeat and outside keep of a lagged fit intercept: fisso hash
`8e3c8d2cdc5854f5` → `76c7d4edfd6bb9cb`, gradino `a624` →
`220b`, continuo **37.259/90.921 → 43.371/122.348**. Reverted.

**Rejected (2026-09-21), refuse half-beat (`frac >= 0.40`) after
the hole, no `sounding` gate.** The `--quick` bank has
subdivision-2/4, so offset-0 fisso/gradino hashes moved
(`8e3c` → `9375`, `a624` → `6790`). Continuo mean/p95
36.919/90.603 (down) is not enough. Reverted.

**Kept (2026-09-21), while sounding, do not waive the on-grid
keep after `kGridStaleBeats`.** Hats-only is loud, so there is no
mute; BeatNet treats hat eighths as beats; the first crest after
2.5 quiet quarters was being taken as lastBeat; true quarters
then sat 0.5 off it; `checkGridPhase` cannot unflip because fold
contrast is gone (`kFoldPhaseContrast` 0.70, CORE_TIMING_AUDIT
item 2). lastBeat stays the origin. Confirmed transitions still
enter through `eligiblePeak`. The synthetic bank never sets
`sounding`, so this is identity there: hashes `8e3c8d2cdc5854f5`
/ `a6249731026d9f82` / continuo `4ffd0e25daf5b15a`, 37.259/90.921,
recovery 0, authority 27. Dedicated fixture (100 BPM quarters,
2 s hole, then off-beat hats only): `sounding=0` phase **0.500**
at t=30 (flipped), `sounding=1` phase **1.000** / 26 accepted
beats (pulse held, hats refused). `VPAlign --ramps` MIXER 12 s
still **32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_kept_hats_sounding.csv` (copy of persist). 1-vs-3
clap after irregular snare (reggaeton) is still open: this only
stops hats being counted as beats under a playing part.

**Rejected (2026-09-21), live persist lastBeat origin when the
4-beat is dirty (`r4` in `[kMotionCurveResidual,
kMotionCurveFourBeatDirty)`).** Aimed at 169090 t=46.92 (r4=0.051,
phase 109→153 into Door D). Unknown Door C lastBeat on this band
already REJECT. 0 offset-0 fisso/gradino (`ioiClockLead` never
set there). Hashes identical. Continuo **37.259/90.921 →
37.282/91.215**. Pulling lastBeat on a mixed-window 4-beat
follows the late lattice. Reverted.

**Kept (2026-09-21), live fourBetween lastBeat origin before
`ioiClockLead` can arm (`bpm<75`, `bir` in `[1, kShortFit)`).**
192847 t=29.92 is the FISSO-leave max (i4=64.5 between short 63.2
and IOI 66.2, bir=1, phase 134 ms). Clock-only ioiLead at bir<4
REJECT; this only pulls origin, same `kCombRulerTolerance` cap.
Silent census (t≥0, fold already in i4): 0 offset-0 fisso/gradino,
1 continuo frame. Hashes identical. Continuo **37.259/90.921 →
37.233/90.882**, p995 unchanged 254.81, recovery 0, authority 27.
Hash `af3dbd034174d682`. 192847 44.7/105.7/130.9 → 44.3/105.0/130.7
(leave max; the hole p95 stays). 145333 66.2/165.8 → 63.5/151.1.
`VPAlign --ramps` MIXER 12 s still **32.1/82.0**.
`probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_kept_fisso_leave_origin.csv`.

**Kept (2026-09-21), sounding stale-grid re-open.** The hats
KEEP (do not waive after `kGridStaleBeats` while sounding) is
also a freeze: `updateTempo` / `checkGridPhase` / the stale-grid
watchdog run only on an accepted peak, so once every crest fails
lastBeat keep, lastBeat never moves and conf overdue-ramps to 0.
Mixer/file activations, sounding held after lock: BPM stayed
~123 (comb too; not an octave dump), max gap **29.3 s**, conf 0
for ~24 s, 240 accepted beats, still overdue at end. Same dump
with sounding off re-anchored (max gap 1.96 s, 320 accepted).
Not `waitForQuantize` / `rhythmSeen` / mute / epoch: the part
is already sounding, so the waive stays off.

Re-open after lastBeat is already stale (`beats >= kGridStaleBeats`)
only for a crest that sits on the committed-tempo fold
(`contrast <= kFoldPhaseContrast`) **and** is not a half-beat
off lastBeat (`offLast < 0.40`). On-fold alone follows
hats-only (the fold's peak is whichever pulse is loud): fixture
A accepted 2 hats, last frac 0.467. The 0.40 bar keeps the
committed origin; the fold supplies a live period after lastBeat
has drifted. While sounding, `checkGridPhase` does not slide the
origin: the fold buffer is 12 s, so three resume quarters after
a hats hole look flipped and would put lastBeat on the hats,
after which the keep freezes (fixture B: phase 0.514 at t=30,
then no pulse). Same hold the octave snap already uses.

The synthetic bank never sets `sounding`: identity vs
`/tmp/motion_kept_fisso_leave_origin.csv`, hashes
`8e3c8d2cdc5854f5` / `a6249731026d9f82` / continuo
`af3dbd034174d682`, **37.233/90.882**, p995 254.81, recovery 0,
authority 27. Fixture A 100 BPM, 2 s hole, off-beat hats only:
`sounding=0` last frac **0.467** (flipped), `sounding=1` last
frac **0.033** / afterHole hats **0** / phase 0.000 (pulse
held). Fixture B, hats then quarters resume: `sounding=1`
resume **20** accepted, phase **1.000**, conf 1.00, Q/H 0/0.
Mixer/file dump with sounding: max gap **29.3 → 2.94 s**,
overdue 41.6 → 10.5 s, low-conf 31.9 → 2.1 s, accepted 240 →
300 (sounding-off is 320 / 1.96 s). `VPAlign --ramps` MIXER
12 s still **32.1/82.0**. `probe_tempo_step` PASS. Next A/B
control: `/tmp/motion_kept_hats_reopen.csv` (copy of
fisso_leave_origin). 1-vs-3 clap is still open: holding
`checkGridPhase` while sounding will not unflip a half that
was already wrong when the part came in. The listener's
"L'1 è QUI" is the command that may (2026-09-22 KEEP): it
snaps the clock and lastBeat onto that quarter even while
sounding; automatic hats still cannot.

**Kept (2026-09-21), sounding post-hole leftover yank.** After
a 2.5-beat hole while sounding, leftover lattice on the
committed fold (fixture D: 100 BPM, 4 s hole, 150 IOIs
coinciding every 1.2 s) was a sudden rate yank: the interval
detector confirmed 100→150 at t=25.22 while comb still named
100, then eight fixed-regime votes snapped onto combRaw 150
at t=36.02 (unknown, history wiped). 100 vs 150 is not an
octave argument (log2 0.585), so sounding did not hold the
level. Stamp `postHoleReopenSec` on a sounding on-grid accept
after `kGridStaleBeats` (the leftover's first crest is
on-fold, so hats-reopen never fired) and on hats-reopen
accept. Refuse a confirmed transition whose
`|log2(mean/held)| > kStaleGridThreshold` after that stamp,
unless IOI+4 already sit on the candidate. Same refuse at the
octave snap onto combRaw. `hugeGapComb` (`!haveShort`,
`|comb-held|` past stale unless IOI+4) moved offset-0
gradino (`a6249731026d9f82` → `90904f52f574b8aa`) and
continuo **37.233/90.882 → 38.343/92.242** — reverted;
leftover 0.05 KEEP stands. Jump class: leftover 3:2 lattice
after a gap, then comb/octave-class snap — not BLUE SKY
quarter-slip, not hats freeze.

The synthetic bank never sets `sounding`: identity vs
`/tmp/motion_kept_hats_reopen.csv`, hashes
`8e3c8d2cdc5854f5` / `a6249731026d9f82` / continuo
`af3dbd034174d682`, **37.233/90.882**, p995 254.81, recovery
0, authority 27. Fixture D `sounding=1` stays **100** through
t=40 (comb 75 / fold 150, short/i4/ioi 100); `sounding=0`
still takes 150. Hats A/B HOLD (A snd=1 last frac **0.033** /
afterHole hats **0** / phase 0.000; B snd=1 resume **20** /
phase **1.000** / Q/H 0/0). `VPAlign --ramps` MIXER 12 s still
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_kept_postgap.csv` (copy of hats_reopen).

**Kept (2026-09-21), sounding lastBeat walk through a roll.**
Complex/syncopated snare rolls are dense extra onsets, not a
missing 2-and-4. Census: lastBeat keep 0.18 lets a crest at
~0.87 of a beat ratchet lastBeat off the fold (32nd/sync
fixtures: frac 0.03→0.47 half-flip) and the interval detector
then confirms a live ~10% yank (100→110). Not a freeze
(resume still accepted) and not leftover-comb after a hole.
Hats are 0.5 away and already fail keep. Requiring on-fold on
every sounding accept rejected hats-B resume quarters (fold
buffer still sees the hats: resume 20→16) — reverted. Refuse
a lastBeat-keep pass while sounding and `beats < kGridStaleBeats`
when `|log2(beats/round(beats))| > kStaleGridThreshold`
(0.87 is log2 0.20; a true quarter is 1.0). Hats reopen and
sounding no-waive unchanged.

The synthetic bank never sets `sounding`: identity vs
`/tmp/motion_kept_postgap.csv`, hashes
`8e3c8d2cdc5854f5` / `a6249731026d9f82` / continuo
`af3dbd034174d682`, **37.233/90.882**. Fixture sync/16th-off
`sounding=1`: lastBeat stays frac **0.033**, BPM **100**,
resume **20**. 32nd residual park frac **0.10** (still inside
keep, BPM 100.1, no 110 yank), resume recovers 0.033. FEEL
hats-only start `sounding=0` still acquires. Hats A/B HOLD
(A snd=1 last frac **0.033** / afterHole hats **0** / phase
0.000; B snd=1 resume **20** / phase **1.000** / Q/H 0/0).
Post-gap D snd=1 stays **100**. `VPAlign --ramps` MIXER 12 s
still **32.1/82.0**. `probe_tempo_step` PASS. Next A/B
control: `/tmp/motion_kept_rolls.csv` (copy of postgap).

**Kept (2026-09-21), file-feed kick-vs-hat acquire fold.**
When a track is loaded the user waits several seconds for the
correct BPM. Census: sounding KEEP does not delay first
accepts (`snd=0`/`snd=1` match). Regular quarters lock at the
3rd peak (1.1–2.4 s). 76 BPM kick+hats published **152 at
1.20 s and never recovered** (HMM names 158 at margin 5.5 and
agrees with the eighth; comb `levelSettled` is 7.9 s at 76).
Three-peak `|b−ends|` 0.18→0.12 never fires (50 fps contrast
0.077 vs 0.097). Pulse-amplitude even/odd at 0.07, holding the
3rd peak, and revisiting `tryFastAcquire` every provisional
frame all moved offset-0 hashes or wrecked continuo
(43.56/117.3, authority 0) — do not retry.

Store per-beat `beatLowBand` (same three-frame window as
cadence). On a direct feed, four equal short intervals whose
even/odd *low-band* means differ by >0.35 of their mid fold
the eighth once (`pairedSubdivision`, stillOnEighth so it
does not repeat). Mute when the four peaks' max low-band is
<0.05: the synthetic click bank and every probe that omits
`observe`'s fourth argument pass 0. Not
`kCadenceCorrectionEnabled` (still off); 50-vs-100 is not
this band (`rawBpm` is not >145). Product file/mixer already
passes `LogSpectFeatures::lowBandEnergy`.

Identity vs `/tmp/motion_kept_rolls.csv`: hashes
`8e3c8d2cdc5854f5` / `a6249731026d9f82` / continuo
`af3dbd034174d682`, **37.233/90.882**, recovery 0, authority
27. Fixture 76 kick+hats with kick low-band 0.85 vs hat 0.04:
first valid still 1.20@152, correct **75.9 at 1.60 s** on-fold.
168 even quarters unchanged 1.10@168. 168 kick+hats phase
**0.868→0.071** at 0.74 s. Dembow still never-locks at 100/168
(irregular IOI, even spacing fails). Hats A/B HOLD. Rolls HOLD
(sync lastBeat 0.033, BPM 100, resume 20). Post-gap D snd=1
stays 100. `VPAlign --ramps` MIXER 12 s still **32.1/82.0**.
`probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_kept_lowband.csv` (copy of rolls). Stop stacking
acquisition even/odd variants.

**Kept (2026-09-21), sounding hat-to-kick half steal.**
FEEL hats-then-Q with `snd=1` still froze after the low-band
acquire KEEP (last=15.92 frac 0.467, Q/H 0/27, conf 0). Product
sounding is false until following, so file-start is the `snd=0`
path which already unfreezes (last 27.62 frac 0.033). The hole
is percussion-in from t=0: lastBeat parked on hats, first
quarter 0.5 later — not a hole, so hats reopen never runs, and
on-fold would follow the hats. `checkGridPhase` is held while
sounding.

When sounding, `lastAcceptedLowBand < kLowBandMute` (0.05) and
the current three-frame low-band is at least mute, and
`offLast` is a half (`0.5 ± kOnGridTolerance`), accept the kick
and slide `gridAnchorSec` onto it. Skip the rolls integer-fold
refuse (0.5 trips that `log2` bar). Mute when low-band is 0
(click bank, hats/rolls HOLD probes). Hats A lastBeat is
kick-class on a file feed, so hats after a hole do not steal.

Identity vs `/tmp/motion_kept_lowband.csv`: hashes
`8e3c8d2cdc5854f5` / `a6249731026d9f82` / continuo
`af3dbd034174d682`, **37.233/90.882**, recovery 0, authority
27. `compare_motion_matrix` FAIL continuo not improved is the
identity KEEP for this fixture HOLD. F1 `snd=1` hat 0.04 / kick
0.85: last **27.62** frac **0.033**, Q/H **20/27**, conf 1.00,
lock. F1-mute still FREEZE. F2 `snd=1` last frac **0.033** Q/H
**39/2** (was hat-steal). Phase at t=24 is still 0.500 (same as
`snd=0`; lastBeat unfreeze is the freeze). Hats A/B HOLD. Rolls
HOLD. Post-gap D snd=1 stays 100. 76 kick+hats still
**1.60@75.9**. 168 kick+hats still 0.74 phase 0.071. Dembow
still never-locks at 100/168 (irregular IOI; no silent
non-acquisition path — even spacing fails; do not retry
even/odd). `VPAlign --ramps` MIXER 12 s still **32.1/82.0**.
`probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_kept_feel.csv` (copy of lowband). Remaining hunt
on 192847 / 145333 / 216604 / Door D hold is closed. Origin-lag
sibling, product-path tau, and 8-beat intercept (below)
are also closed.

**Stopped (2026-09-21), 192847 / 145333 / 216604 / 169090
Door D.** Census on the feel curve log (`--quick` offset-0).
Live Door D fire is 0 fisso/gradino (3 continuo: 153252 t=67.36
r4=0.036, 161171 t=66.60, 169090 t=47.58). 169090 origin error
through the hold is **0.24 beat** (153–160 ms; cap 0.12 =
77 ms). `kLiveLead` on that 4-beat is **+0.13 BPM** (i4 already
on the IOI vs T). Hold 0.50 already identity-FAIL on the mean.
Cap 0.18 already identity on family p95. Comb-fold origin snap
on Door D is the leftover dump-ring (145333 live snap
37.233→37.606). lastBeat/a4 are the mixed 8-beat; pulling them
cannot name the pulse.

192847 t=54.56 first crest after 4.0 beats of clustered
`missChance`: ioi=59.1 i4=59.0 T=61.2 comb=58.1, quadratic
rate −0.19. Post-hole first-crest lights **11 fisso / 36
gradino**. Skip-comb and a `kGridStaleBeats` window light
1009 and 210467. Door C at 1.0 REJECT. Nothing in the decoder
measures the missing quarters.

145333 leftover 174 vs comb 163, fits *agree*. Live
yank/gate/snap all raise family mean and p95: dumping that
ring still leaves the PLL on the remaining 174 times.

216604 leftover+gap is the programmed kit gap (`seed&3=0`)
walking a 73 lattice vs comb ~67 (3:2-class leftover).
leftover 0.05 KEEP; skip-to-zero FAIL; hugeGapComb FAIL;
dirty-r4 lastBeat origin mean up.

Do not invent quarters, skip-comb on live, reopen the comb
ruler on live, or stack leftover below 0.05. Branch closed.

**Stopped (2026-09-21), 169090 origin-lag (lastBeat-relative
pull is a no-op; sibling lights 1009 / 210467).** Feel curve
log, `--quick` offset-0. Door D t=47.58 r4=0.007 (clean),
fourCloser **hits** every hold frame through t=52.12, fourOnIoi,
i4 on pulse, phase **0.24 beat** and stays there. fourCloser
lastBeat origin is already KEEP: lastBeat and `gridAnchor`
already agree inside the 0.12 cap, both late vs truth, so a
larger lastBeat pull cannot accumulate (cap 0.18 was identity
on family p95). persist dirty-r4 (t=46.92 r4=0.051) REJECT.
Fold-at-i4 origin is the Door D comb-fold class (dump-ring).

Sibling `live && !fourCloser && !fourBetween && i4≈T &&
r4<0.015 && bir>=8`: **10 fisso** (1009, 32685, 48523, 64361,
88118) and **97 gradino** (210467 first at t=62.08). Hash-unsafe.
The one 169090 sibling-shaped frame (t=57.24, i4=short=98.8 vs
T 99.8) is persist lastBeat already KEEP.

VPAlign 12 s decoder floor 18.3 ms analog — FISSO,
`sres∈(0.045,0.056]`, fourOnIoi: **18 fisso** including 1009
and **11 gradino**. Same gate as strained clock-target REJECT
(hashes moved). No silent KEEP.

Do not pull origin on fourCloser-miss while i4 is on the
pulse, and do not slide FISSO strain origin. Origin-lag
branch closed.

**Stopped (2026-09-21), product-path tau / 169090 0.24 beat
is decoder-vs-truth, not PLL.** `BeatTracker` already arms
`setDirectLivePhaseFollow` for every stable direct live
hypothesis (`!speakerFollow`, `regime==live`,
`transitionState==stable`, `refitBeats==0`, SEGUI, periodic
= valid BPM≥50). Loaded track and mixer share that path
(`setSpeakerFollow(false)` → decoder `lineFeed`). Flag on
→ `kGridTauMotion` 0.30 s, bypassing `gridPhaseTau` and
**winning over** `ioiLead` 0.01. Do not no-op-edit that
flag. Do not wire it into VPAlign/matrix (22.3→24.1).

Not 0.30 for the *whole* motion: unknown/FISSO while
FOLLOWING stay on 0.90 unless `ioiLead` / clean 2% residual
/ strained two-vote hint (already KEEP). LOCKING uses
`kGridTauAcquire` 0.25. Confirmed steps use Rapid 0.10.
iPad speaker never gets the flag. User ramps that sit in
FISSO until the three-vote leave are decoder regime, not a
missing 0.30 on live.

169090's 0.24 beat (158 ms at 91 BPM) is the published
`beatPhase` (lastBeat and `gridAnchor` both late). Matrix
already uses `ioiLead` 0.01 on those Door D frames; product
live uses 0.30. Faster PLL follows that late origin more
closely (`PhaseTrust` line-feed 0.90→shorter 23.2→23.8 ms;
clock-decoder floor cannot close decoder-vs-truth). Causal
geometry: hop **20 ms** (`kBeatModelHop` 441 @ 22.05 kHz),
window **64 ms**, product lead trim **17 ms**, file round
trip **0**. 158 ms is ~8 hops = mixed 8-beat intercept lag,
not one hop of BeatNet delay. Cannot fix without lookahead
or BeatDecoder origin (exhausted). Tracking KEEP not taken.

**Stopped (2026-09-21), 8-beat intercept / recency-weight.**
`fitPeriodBefore` already reads the intercept at the **newest**
beat of the window, not the centre. Live `gridAnchorSec` *is*
that 8-beat `shortAnchor`. `fitPeriodCurve` is centred on the
newest time, so the quadratic intercept at now **is** lastBeat.
Origin pull already aligns `gridAnchor` to lastBeat inside
0.12. Shortening 8→5 was measured as twice the settled wobble;
eight stays.

169090 t=47.58: clock/published phase **152.9 ms = 0.240 beat**,
i4 **91.124 vs T 91.221**, short **95.775**, g **0.345**
(clock-only needs 0.80 — not this frame). A centre-extrapolation
of the 8-beat slope would be ~0.175 beat; observed 0.240 is the
newest accepted times, not leftover centre lag. t=48.24 and
t=49.54 have g≥0.80 and phase still 0.24: clock-only tau does
not change the intercept buffer.

Census, feel curve log, offset-0: live `|rate|>0.05` recency
lights **fisso 1009 ×39**. Dirty-8 + i4-on-pulse + r4<0.015
lights **1009 ×6** and **31 gradino**. `|short−T|>3%` and
i4-on-pulse lights **1009 ×5**. Clock-only g≥0.80 `bir>=8` is
0 fisso but **7 gradino** (234224, 257981). No silent
recency/short-intercept gate. Do not weaken fisso. 8-beat
branch closed.

**Kept (2026-09-21), live Door D hold keep 0.22 and
refractory 0.18.** Newest accepted times vs the generating
grid, not intercept centre and not hop/trim. 169090 Door D
t=47.60: lastBeat **47.585 vs truth 47.714** (−130 ms /
−0.20 beat); i4 **91.12 vs T 91.22** (on pulse). Nearest
accepted event is 47.565, **+19.5 ms** after that early
crest (one hop). True quarter at 47.714 is present
(dt −10.9 ms) but 0.20 off the early lastBeat, so keep
0.18 never admits it. Refractory 0.40 of the committed
period plus `updateTempo` arming hold *after* that peak
took minRefr=0.40 also blocks it from becoming
`eligiblePeak`. First missed generating quarter is
truth 45.088 (no event within 40 ms); later quarters are
there. Published 0.240 beat at 91 BPM is **158 ms** of
early-lattice offset, not one 20 ms hop, not the 17 ms
product trim, not file round-trip 0, not 8-beat centre
lag (~0.175). Peak-of-activation vs attack on on-pulse
frames t=42–44 is +15–20 ms after the jittered event.

Keep-only 0.22 (no refr) was identity vs feel CSV.
Global keep 0.18→0.22 lights fisso 1009. Unknown Door B
hold is the leftover gap — do not widen there. Live Door
D hold only: `kDoorDHoldKeep` 0.22 (still below a
sixteenth 0.25), `refrFrac` 0.18 while hold is armed, and
clamp `refractoryFrames` to that floor after
`updateTempo`. Offset-0 fisso/gradino: 0 Door D frames.
foldToAnchor previous-row snap/gate is leftover dump-ring
— not this KEEP. Do not invent a silent latency trim;
accepted crests on this seed are the causal observation.

Door D fire itself still takes the early crest (already
eligible on that lattice). True quarters become eligible
during the hold; lastBeat is on-pulse again by t=51.52
(+9 ms vs truth 50.981).

A/B vs `/tmp/motion_kept_feel.csv` `--quick` 16: hashes
`8e3c8d2cdc5854f5` / `a6249731026d9f82` identical,
continuo **37.233/90.882 → 36.636/89.498**, hash
`0b194dcb7e411de7`, recovery 0, authority 27. Hats A/B,
rolls, lowband, post-pause D, FEEL HOLD (sounding=false on
the bank; Door D hold 0 on those probes). `VPAlign --ramps`
MIXER 12 s still **32.1/82.0**. `probe_tempo_step` PASS.
Next A/B control: `/tmp/motion_kept_doord_keep.csv`.

**Rejected (2026-09-21), Door D hold refr 0.15.** Aimed at
169090's true quarter +118 ms sitting on 0.18×period
(115 ms at 94 BPM). Identity vs
`/tmp/motion_kept_doord_keep.csv`: hashes `8e3c` / `a624`
identical, continuo **36.636/89.498** equal, recovery 0,
authority 27. 0.18 already admits +118 ms; t=47.72 is still
153.8 ms (0.22-keep boundary crest, not a further refr).
Do not lower Door D refr again.

**Stopped (2026-09-21), 192847 / 145333 / 216604 / 169090
early crest, after Door D keep 0.22.** New-control curve log
(`--quick` offset-0 vs 36.636/89.498 `0b194dcb7e411de7`).
Live Door D fire is still 0 fisso/gradino (same 3 continuo:
153252 t=67.36 ph 200.8 r4=0.036, 161171 t=66.60 ph 61.0,
169090 t=47.58 ph 152.9). 169090 fire lastBeat still
−130 ms vs truth; recover 31.7 ms at t=52.28. refr 0.15
identity. Global keep 0.18→0.22 lights 1009. Origin /
comb-fold / hold 0.50 / cap 0.18 closed.

192847 hole 50.50→54.56, first crest ioi 59.1 i4 59.0 T 61.2
comb 58.1 rate −0.19, ph 104.9→109.8. skip-comb
`|ioiDev|>1%` + comb apart >0.5% lights fisso 1009 / 48523 /
64361 / 88118. Cannot invent quarters. Door C at 1.0 REJECT.

145333 leftover 174 vs comb 163 at t=58.88, fits agree, ph
130.9→168.7. Geometry unchanged by Door D keep. Dump-ring
still costs family phase on the old control; leftover-faster
census now also hits gradino 257981 ×4. Do not reopen the
comb ruler on live.

216604 leftover+gap is still unknown through the programmed
kit gap (`seed&3=0`), 73 lattice vs comb ~67, worst 256 ms
at t=51.56. skip-to-zero / hugeGapComb FAIL.

No silent Door-D-adjacent KEEP left that recovers the fire
crest without lighting 1009. Control stays
`/tmp/motion_kept_doord_keep.csv`. Branch closed.

**Stopped (2026-09-21), next-worst continuo after Door D keep
0.22.** `--quick` 16 verbose vs 36.636/89.498. Per-seed p95
is not only 192847/145333/216604: 153252 **113.5**, 161171
**96.2**, 208685 **92.3**, 200766 **88.4** (closed four:
145333 151.1, 216604 147.0, 169090 121.9, 192847 105.0).
Those remaining tails are closed geometries, not a new
silent discriminant.

153252 t=66.42 leftover **139.8 vs comb 131.3**, fits agree,
sres 0.082, then Door D t=67.36 ph **200.8** (i4 132.7 vs T
131.3). Dump-ring / Door D origin / r4 0.045 already REJECT.
161171 Door D t=66.60 recovers to 3.7 ms; t=69.16→70.70
`!haveShort` hole ph **116** — invent quarters. 208685 FISSO
leave through a 1.36 s gap (t=28.98→30.34) ph **131.7**.
200766 above-75 FISSO 2-vote leave at t=29.76 (IOI already
on T); earlier IOI leave 0.028 / g 0.40 already identity or
p95-up. 129495 spike 216 ms t=65.48 is a hole (p95 only
71.4, does not move family p95). Door A g 0.40 clock-only
already REJECT. Do not invent a fourth leftover dump.
Control stays `/tmp/motion_kept_doord_keep.csv`.

**Kept (2026-09-21), live Door D hold re-arms haveShort from
the accepted crest.** 161171 Door D t=66.60 recovers; t=69.16
`!haveShort` (ioi/i4/long gone) and coasts to ph 116 at
t=70.70 because `gridAnchor` does not move without a fit.
The hold already commits BPM. Re-arm: IOI, else a clean
4-beat on that IOI, else the held 4-beat; `gridAnchor` =
lastBeat. Live Door D hold only. `bir>=8` live `!haveShort`
lights fisso 1009 / 88118 and gradino 210467. Unknown
`!haveShort` leftover comb stays. foldToAnchor unused
(comb 116.3 vs held 115.4 at t=69.16 is not a snap).

A/B vs `/tmp/motion_kept_doord_keep.csv` `--quick` 16:
hashes `8e3c8d2cdc5854f5` / `a6249731026d9f82` identical,
continuo **36.636/89.498 → 36.556/89.417**, hash
`32fd5bf0131d5acb`, recovery 0, authority 27. 161171 p95
stays **96.2** (the 70.70 hole is past the remaining hold
window). 153252 **36.2/113.5 → 34.9/112.2**. 169090 p95
unchanged 121.9. Hats A/B, rolls, lowband, post-pause D,
FEEL HOLD (Door D hold 0 on those probes).
`VPAlign --ramps` MIXER 12 s still **32.1/82.0**.
`probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_kept_haveshort_rearm.csv`.

**Stopped (2026-09-21), next-worst after haveShort re-arm.**
`--quick` 16 vs `/tmp/motion_kept_haveshort_rearm.csv`
36.556/89.417 `32fd5bf0131d5acb`. Per-seed p95 is still
closed geometries, not a new silent discriminant.

145333 **151.1** leftover 174 vs comb 163. 216604 **147.0**
unknown kit-gap leftover. 169090 **121.9** Door D fire crest
(t=47.72 ph 153.8; origin / refr 0.15 / hold 0.50 / global
keep 0.22 closed). 153252 **112.2** leftover 139.8 vs comb
131.3 then Door D t=67.36 ph 200.8 (dump-ring / r4 0.045
REJECT). 192847 **105.0** hole invent. 161171 **96.2** still
`!haveShort` at t=69.16→70.70 ph **116**, ioi/i4/short gone
(hold does not cover that hole; a second re-arm without hold
is invent quarters). 208685 **92.3** FISSO leave in a gap
(g≥0.45 refuse REJECT). 200766 **88.4** 2-vote leave already
fires at t=29.76 (IOI on T); post-leave bir=1 so clock-only
`bir>=8` misses, `bir>=4` lights fisso 64361; Door A g 0.40
REJECT; do not stack another FISSO-leave threshold.
137414 **86.7** / 224523 **76.2** are live lag below 75
(g 0.40 / 0.00 at the p95 frames; clock-only 0.80 above-75
does not fire). 129495 spike 216 ms t=65.48 is a hole (p95
only 71.4). Do not dump leftover. Do not a second haveShort
variant. Control stays `/tmp/motion_kept_haveshort_rearm.csv`.

**Kept (2026-09-21), below-75 live i4-on-pulse origin+rate
when g<0.80.** 137414 t=32.20: IOI leads (not four-lead
quiet), g=0.40 so Door A curveOk misses, combSign false
(comb tied to the late 8-beat). 224523 t=38–40: i4 on the
IOI pulse, g=0.00–0.11; t=41.20 g=0 and i4≈short so this
gate does not fire there. Not Door A 0.40 (no 0.70 take).
Live-rate toward i4 + `ioiClockLead` origin. Guards:
`bir` in `[kShortFit, kLongFit)`, r4 clean, fourCloser,
`|i4−ioi|<1.2%`, i4 and IOI lead short by >1%, `|ioiDev|<kUnknownIoiLead`,
`|comb−short|<0.5%`. Census t≥0 fold=combRaw≈held: 0 fisso
(1009) / 0 gradino (210467); 4 continuo frames.

A/B vs `/tmp/motion_kept_haveshort_rearm.csv` `--quick` 16:
hashes `8e3c8d2cdc5854f5` / `a6249731026d9f82` identical,
continuo **36.556/89.417 → 36.305/87.626**, hash
`08474b08999e4bfa`, recovery 0, authority 27. 137414
**31.2/86.7 → 29.9/76.7**. 224523 **29.7/76.2 → 27.0/57.6**.
Closed leftover/hole/FISSO-leave seeds unchanged. Hats A/B,
rolls, lowband, post-pause D, FEEL HOLD (this gate 0 on
those probes). `VPAlign --ramps` MIXER 12 s still
**32.1/82.0**. `probe_tempo_step` PASS. Next A/B control:
`/tmp/motion_kept_below75_i4pulse.csv`.

**Rejected (2026-09-21), above-75 live i4-on-pulse origin+rate
when g is in [0.50, 0.80).** Aimed at 113657 t=66.14 (g=0.64,
i4≈T, clock-only 0.80 misses). Bare g≥0.50 lights gradino
234224 t=47.76 (IOI slower than short). Extra guards (IOI and
i4 lead short >1%, `|ioiDev|<kUnknownIoiLead`, fourCloser,
r4<0.030, `|log2(comb/held)|<kOctaveThreshold`) are 0 offset-0
fisso/gradino (14 continuo; 3 of 169090 were comb 127 vs held
96). Matrix `--quick` 16: hashes `8e3c` / `a624` identical,
continuo **36.305/87.626 → 35.754/87.528**, hash
`2d4786749b3be68c`, recovery 0, authority 27. 113657
74.4→72.9, 200766 88.4→87.3; 161171 p95 **96.2→97.3**.
`VPAlign --ramps` MIXER 12 s **32.1→30.1** / 82.0; 120→132
MIXER mean **25.8→26.8**. Ramp curvature sits in that g
band. Reverted. Do not lower clock-only 0.80 above 75, and
do not a second above-75 g-threshold.

**Stopped (2026-09-21), next-worst after below-75 i4-on-pulse.**
`--quick` 16 vs `/tmp/motion_kept_below75_i4pulse.csv`
36.305/87.626 `08474b08999e4bfa`. Remaining tails are closed
geometries. 145333 **151.1** / 216604 **147.0** / 153252
**112.2** leftover. 169090 **121.9** Door D fire crest.
192847 **105.0** / 161171 **96.2** / 129495 hole invent.
208685 **92.3** FISSO leave in a gap. 200766 **88.4** 2-vote
leave already fires (bir=1; `bir>=4` clock-only lights fisso
64361). 137414 **76.7** leftover quiet-IOI g=0.09 at t=31.20
is a second below-75 g-threshold. 113657 **74.4** is the
above-75 g[0.50,0.80) REJECT (VPAlign). Do not dump leftover.
Control stays `/tmp/motion_kept_below75_i4pulse.csv`.

**Kept (2026-09-22), 75<=bpm<90 live i4-on-pulse origin+rate
when g is in [0.50, 0.80).** Same extra guards as the
unbounded above-75 REJECT, plus `bpm<90`. Dump hops on
`--quick` 16: 0 fisso/gradino, 4 continuo (113657
t=66.14/66.80, 200766 t=85.30, 208685 t=74.68). 0 dump hops
on `VPAlign --ramps` (100/130 flats, 100→110 12 s/30 s,
120→132 20 s, 128→120 20 s). Not a second unbounded
above-75 g-threshold and not clock-only 0.80.

A/B vs `/tmp/motion_kept_below75_i4pulse.csv` `--quick` 16:
hashes `8e3c8d2cdc5854f5` / `a6249731026d9f82` identical,
continuo **36.305/87.626 → 36.096/87.459**, hash
`2de8c00b48185ac3`, recovery 0, authority 27. 113657
**30.3/74.4 → 26.9/72.9**. 200766 **88.4→87.3**. 161171
p95 stays **96.2** (bpm~119). 208685 p95 stays **92.3**
(the t=74.68 fire is not the FISSO-leave hole).
`VPAlign --ramps` MIXER 12 s still **32.1/82.0**; 120→132
MIXER mean still **25.8**. `probe_tempo_step` PASS. Next
A/B control: `/tmp/motion_kept_bpm90_i4pulse.csv`.

**Stopped (2026-09-22), next-worst after 75<=bpm<90
i4-on-pulse.** `--quick` 16 vs
`/tmp/motion_kept_bpm90_i4pulse.csv` 36.096/87.459
`2de8c00b48185ac3`. Remaining tails are closed geometries,
not a new silent VPAlign-safe discriminant.

145333 **151.1** / 216604 **147.0** / 153252 **112.2**
leftover. 169090 **121.9** Door D fire crest (origin / refr
0.15 / hold 0.50 / global keep 0.22 closed). 192847
**105.0** / 161171 **96.2** / 129495 hole invent. 208685
**92.3** FISSO leave in a gap. 200766 **87.3** 2-vote leave
already fires (t=29.76); 75–90 i4-pulse already took
t=85.30. 137414 **76.7** leftover quiet-IOI g=0.09 at
t=31.20 is a second below-75 g-threshold. 113657 **72.9**
is the 75–90 KEEP. 184928 **66.0** is a FISSO leave after
a 2.93-beat hole (t=38.14) then 2-vote at t=47.48 — do
not stack another FISSO-leave. 177009 **58.3** t=66.24
(g=0.38, bpm 118.5, i4 122.8 vs T 121.6) is Door A above
75 / unbounded g[0.50,0.80) (177009 t=53.04 i4=124 vs
T=119 overshoots; 120→132 shares bpm≥90). 121576 **52.4**
sits at ~102 BPM with dirty r4 on the p95 frames (100→110
12 s). 224523 / 105738 already below the family p95.
Do not dump leftover. Do not lower clock-only 0.80 above
75. Control stays `/tmp/motion_kept_bpm90_i4pulse.csv`.

**Kept (2026-09-22), "L'1 è QUI" snaps the quarter while
sounding.** Listening: the comb sat on the AND (`1 X 2 X 3 X
4 X`) and the button did not bring it back. That was not the
hats hold swallowing the press. `barButton` → `barDeclare++`
→ `BeatTracker::declareBarHere` only did
`rotateBarIndex(-beatInBarIndex())`: if the clock was on the
levare, relabelling that pulse as beat zero still played on
the AND. TAP's first tap already means NOW is the one
(`snapBeat(0, 0)`). `checkGridPhase` and the on-grid keep
still refuse automatic hat crests (`offLast` ~0.5, no 2.5
waive while sounding, reopen only on-fold AND `offLast<0.40`).

KEEP: `declareBarHere` calls `snapBeat(0, 0)` (clock does not
restart), `holdBarDecision`, a 0.70 s `tapHold` so
`setGridPhase` cannot pull the clock back onto a flipped
decoder hyp in the same block, and `NeuralBeatTracker`
hands `BeatDecoder::declarePulseHere` to the worker
(lastBeat/gridAnchor = analysis now, history wiped, no
`gridSerial` bump). The wipe must not re-estimate the
tempo: `barTempoHoldBeats = kShortFit` restores `bpm` and
`fixedAnchorBpm` at the end of each of the next eight
`updateTempo` calls. Hats KEEP is unchanged. The clock's
`tempoTrim` is left alone; clearing it would itself move
the sounding rate.

The matrix never sends `barDeclare` and never sets
`sounding`: identity vs `/tmp/motion_kept_bpm90_i4pulse.csv`
(fisso `8e3c8d2cdc5854f5`, continuo 36.096/87.459
`2de8c00b48185ac3`, gradino `a6249731026d9f82`, recovery 0,
authority 27). Fixture: `declareBarHere` sounding=1 phase
**0.500 → 0.000** beatInBar 0; decoder `declarePulseHere`
frac **0.467 → 0.000**, then old-quarter hats accepted **0**;
hats hole HOLD afterHoleHats **0** / phase **0.967**.
`VPAlign --ramps` MIXER 12 s still **32.1/82.0**; 120→132
MIXER mean still **25.8**. Control stays
`/tmp/motion_kept_bpm90_i4pulse.csv`.

**Kept (2026-09-23), "L'1 è QUI" names the nearest beat and does not move the phase.** The 2026-09-22 snap (`snapBeat(0, 0)` plus `declarePulseHere` setting the anchor to the sample) put phase on 0 from the middle of a beat. Half a beat of that shortens or stretches the beat under a part that is already playing, discards strokes that had not sounded yet, and is heard as the tempo jumping. The press now only rotates the count: phase ≤ 0.5 names the beat that has started, phase > 0.5 names the beat about to land (`beatInBar` 3, so the next wrap is the one). The decoder publishes that same count and leaves the lattice, the fits and the tempo where they are. `tapHold` still blocks `setGridPhase` for 0.70 s. TAP's first tap still snaps. The matrix never sends `barDeclare`.

**Kept (2026-09-22), hats-only eighths fold to the quarter.**
Hats-only at the start publishes the eighth (76 → 152):
BeatNet peaks every hat and the HMM near 118 prefers the
fast scale. A flat hat has no low-band, so
`evenOddKickHatSubdivision` stays mute. On a direct feed,
four accepted peaks that are regular eighths (`rawBpm > 145`,
quarter = raw/2 still ≥ `kMinBpm`, intervals inside the 0.12
line tolerance, not a swung pair), every peak hat-class
(low-band < `kLowBandMute` 0.05), and high-band present
(three-frame max above `kHighBandPresent` 0.15): fold once
(`bestPeriod = raw * 2`), `intervalSelfSufficient`,
`pairedSubdivision` so it is not folded again, anchor on the
newest peak. That phase may be the levare. "L'1 è QUI" is
the correction; do not invent an accent.

This fold is only equal short IOIs in the eighth-ambiguous
band with hat spectrum and no kick. A genuine >145 BPM
hat-on-quarters song is the same measurement and is halved
too. A 168 kick track is protected by low-band and still
acquires on the interval.

Mute: `observe`'s fifth argument `highBand` defaults to 0,
same contract as lowBand, and `storeBeatForFit` defaults it
to 0. The click bank and every probe omit it, so the gate
never fires. `LogSpectFeatures::highBandEnergy` is the mean
of bands 120..135 (top 16 of the 30 Hz–17 kHz, 24/oct bank,
where a hi-hat has energy and a kick does not). Product
`NeuralBeatTracker` passes it. Probes do not.

Identity vs `/tmp/motion_kept_bpm90_i4pulse.csv` `--quick`
16: hashes `8e3c8d2cdc5854f5` / `a6249731026d9f82` / continuo
`2de8c00b48185ac3`, **36.096/87.459**, recovery 0, authority
27. Fixture, four equal 152 intervals, lowBand 0.01: highBand
0.40 first **1.20@152.2** then **75.9**; highBand 0 stays
**152.0**. 168 kick low-band 0.85 high-band 0.40 still
**1.10@168.0**, same as highBand 0. `VPAlign --ramps` MIXER
12 s still **32.1/82.0**; 120→132 MIXER mean still **25.8**.
Control stays `/tmp/motion_kept_bpm90_i4pulse.csv`.

**Stopped (2026-09-22), 120 BPM heard phase-lock is not a
decoder KEEP.** `VPTests --phase-lock` (click track, 38 s,
ONNX drained): 78 **+4.8/+4.8 PASS**, 100 **+2.5/+0.8 PASS**,
120 **+9.1/+9.9 FAIL** (8 ms bound), 138 **−2.3/−3.8 PASS**,
156 **−4.9/−2.7 PASS**. Attack lead **15.6 ms** (bank,
`PercussionEngine::attackLeadMs`); pipeline lead 66.6 ms.
At 120 the clock sits 25 ms early; subtracting 15.6 leaves
9.1 ms. The same 9.1/9.9 line is in earlier ONNX logs with
this 15.6 ms bank. With the old 6.7 ms shaker lead the same
bench was **+3.8/+3.8 PASS**.

Not `declarePulseHere` (no `barDeclare`), not the hats-only
fold (click high-band is 0, gate mute), not new-file hold
(same click, no source change). Do not close it by pulling
the whole kit earlier. `--quick` 16 vs
`/tmp/motion_kept_bpm90_i4pulse.csv` is identity: fisso
`8e3c8d2cdc5854f5`, continuo **36.096/87.459**
`2de8c00b48185ac3`, gradino `a6249731026d9f82`. Remaining
continuo tails are still leftover / hole-invent / FISSO-leave
/ Door D / 120→132-shared. Control stays
`/tmp/motion_kept_bpm90_i4pulse.csv`.

**Kept (2026-09-22), long-fit phase only while `ioiLead` on an
interval-acquired grid.** `gridPhaseNow` uses `60/longFitBpm`
when `fastMotionCurrent && ioiClockLead && intervalAcquired
&& longFitBpm >= kMinBpm`; otherwise `60/committedBpm`. No
anchor move, no snap, no new constant. The ungated walk
(long-fit period on every `ioiLead` frame) was
36.096/87.459 → 35.631/88.098: 216604 and 169090 were the
whole p95 rise, 145333 and 113657 the drop, 224523 the mean
drop. Those two hurt seeds are comb/HMM grids
(`intervalAcquired` clear on every ioiLead frame: 216604
0/668, 169090 0/1705). The helped seeds have it on every
ioiLead frame (145333 146/146, 113657 1459/1459, 224523
2147/2147). `--quick` 16 vs
`/tmp/motion_kept_bpm90_i4pulse.csv`: fisso
`8e3c8d2cdc5854f5`, gradino `a6249731026d9f82`, continuo
**35.571/87.276** `1fb68ff092c4ef17`, recovery 0, authority
27, gradino authority 0. 216604 p95 147.0 and 169090 p95
121.9 unchanged; 145333 151.1→147.9, 113657 72.9→70.2.
`VPAlign --ramps` MIXER 12 s **32.1/82.0**, 120→132 mean
**25.8**. Control is `/tmp/motion_gated_period.csv`.

**Kept (2026-09-22), hold the long-fit phase period after
`ioiClockLead` clears.** `longFitPeriodHeld` becomes true only
when the existing four-term gate is true, and false when
`fastMotionCurrent` is false (also when the beat train is
invalidated, so a same-call re-beat cannot keep a dropped
episode). While it is true and `intervalAcquired && longFitBpm
>= kMinBpm`, `gridPhaseNow` uses `60/longFitBpm` even though
`resetMotionShadow` has cleared `ioiClockLead`. `hyp.ioiLead`
and `kGridTauIoiLead` (0.01 s) stay on `ioiClockLead`. Frames
that never opened the four-term gate stay on `60/committedBpm`.
No new threshold. The failed widen (every fast-motion frame
with a valid long fit, ignoring `ioiLead`) was 35.375/87.738,
fisso `e3b7ff6552b4a7f1`, gradino `7149b2636235fad5`, and
31067 extra continuo frames. This hold covered **1957** of
those continuo frames (fisso 0, gradino 0). `--quick` 16 vs
`/tmp/motion_gated_period.csv`: fisso `8e3c8d2cdc5854f5`,
gradino `a6249731026d9f82`, continuo **35.482/87.221**
`55ccc5c84d49749d`, recovery 0, authority 27, step authority
0. `VPAlign --ramps` MIXER 12 s **32.1/82.0**, 120→132 mean
**25.8**. Control is `/tmp/motion_episode.csv`.

**Kept (2026-09-22), the long-fit phase hold clears on
"L'1 è QUI".** `longFitPeriodHeld` otherwise drops only when
`fastMotionCurrent` is false. The button used to set
`lastBeatSec` to analysis now, so the next frames stayed inside
that window and `gridPhaseNow` kept walking `60/longFitBpm` from
the pre-button origin. That path cleared the flag beside the fit
wipe. As of 2026-09-23 the button does not move the lattice or
wipe the fits, so it does not take this clear either; the flag
still drops only when fast motion ends. `hyp.ioiLead` and the
0.01 s tau stay on `ioiClockLead`. The matrix never
calls `declarePulseHere`. Do not clear this hold on ordinary
motion.

**Kept (2026-09-22), drum-pause hold once a kick body has
been heard.** A hat, a voice or a bass note that crests early
is the same signal as a faster tempo, and the clock closes
phase by bending its rate, so the part rushes as soon as the
kit drops. `kitBodyHeard` arms only on an accepted beat whose
low band clears `kLowBandMute`. The next crest more than 1.05
beats later, still under that mute, does not call
`updateTempo` and does not move `gridAnchorSec`: it is stored
on the held grid so the pause is not a hole, and
`longFitPeriodHeld` is cleared so a short long-fit period
cannot keep the phase running fast. One beat of hats between
kicks stays on the ordinary path. A hats-only song never
arms, because the body was never heard. The click bank and
the motion matrix pass `lowBand` 0, so this does not move
their hashes. Do not hold from the fit residual: five of
those cost half a bar on an accelerando, and a band speeding
up with the drummer playing still has the body.

**Kept (2026-09-23), an established tempo does not recompute from the hat alone.** The kick-body hold never arms on a hats-only passage: no crest clears the low-band mute, so every hat stays on the ordinary path and the eighths walk the counted tempo off the quarter. Once the tempo is established and no longer provisional, a direct-feed crest under `kLowBandMute` with high band above `kHighBandPresent` is stored on the counted grid and does not call `updateTempo`. `longFitPeriodHeld` drops for that passage so a short period cannot keep the phase fast. A kick still updates. While provisional, the eighth-fold still sees the raw intervals. High band defaults to 0, so the click bank and the matrix do not take it. Quick bank unchanged: fisso `8e3c8d2cdc5854f5`, continuo `ca2588bfe0ce70c5`, gradino `bcf2c1d9141d2abb`, recovery 0. A hats-only accelerando is not followed until a non-hat crest returns; that is the hold.

**Kept (2026-09-22), post-pivot curvature veto in
`observeGridStep`.** The detector already required the eight
beats before a candidate pivot to be straight, but that does
not distinguish a genuine step from a clean ramp which starts
at the pivot. In `VPAlign`, 118 -> 126 over 4 s was therefore
published as one rapid transition on the third new interval;
the clock was 53 ms behind at that publication. Compare the
first and last accepted intervals after the pivot as well: a
step makes them stationary, while a ramp keeps bending them.
Reject clean line evidence above a 0.26 share of the candidate
step; retain the existing 0.30 share once measured scatter is
above its floor, so onset noise does not slow a real change. The
four ramp seeds now publish 0 rapid
transitions, while all six protected tempo-step fixtures still
publish exactly one transition on time, the jittered 120 ->
132/110 bank remains 12/12, and the displacement control stays
0/6. The focused step gate also runs 4 s and 12 s ramps through
the decoder's exact 20 ms peak path; both remain at 0 rapid.
`VPAlign` passes all tempo-change and ramp-phase rows.
The quick motion matrix is fisso **22.256/76.932** hash
`8e3c8d2cdc5854f5`, continuo **36.551/91.156** hash
`ca2588bfe0ce70c5`, gradino **33.962/162.108** hash
`4dd43630dc0ebc83`, recovery violations 0 and bridge-authority
frames 0/27/0. Its `curve` selector now counts rising episodes
of production `motionBridgeAuthority`: the retired
`motionFitEvidence` diagnostic legitimately stays zero, while a
strict shape proof may be quarantined on a step before authority.
Use `VPAlign --trace-change FROM TO RAMP [SEED]` to inspect the
causal beat path; `--trace-ramp` prints the transition fields
on the continuous MIXER path too.

**Phase floor (2026-09-22). Stop. Already sub-hop.**
The continuo mean sits on the fisso floor. `--quick` 16
continuo, post-warmup, control
`/tmp/motion_kept_bpm90_i4pulse.csv` (36.096/87.459, hash
`2de8c00b48185ac3`):

| bucket | frac | published abs/signed ms | clock abs ms |
|---|---|---|---|
| A \|log2(pub/true)\|<0.08 | 99.3% | 24.57 / +1.78 | 35.58 |
| B period disagrees | 0.7% | 118.97 / +48.81 | 109.48 |

Fisso published-phase absolute error is ~22 ms; signed
error is ~+2 ms (scatter, not lag). Clock absolute on that
floor is ~36 ms. Slope projection is 0.27 ms. Bucket B is
too small to move the mean (~0.5 ms if healed). Door, rate,
tau, leftover, and origin retunes are closed. The 8-beat
intercept is already at the newest beat. Do not reopen
them, and do not reopen slope projection, to chase this
floor.

The beat time is not the 20 ms hop index. `observe` emits
the local maximum of `max(pBeat, pDownbeat)` and adds a
causal 3-point parabolic shift in [-0.5, +0.5] hop before
`registerBeat` (`eventTimeSec += shift * hopSec`, hop 20 ms).
The published phase is `gridPhaseNow` on the least-squares
intercept of those times (`fitPeriod` reads the anchor at
the newest beat). `setGridPhase` slews that phase in
continuous time. Nothing snaps it back onto the hop.

Measured on fisso seed 1009, same probe activations, no
behavior change. Fractional hop of the activation beat
time (the parabolic shift), n=390: mean |shift| 0.232 hop,
5.9% inside 0.025 hop of the frame, bins filled across
[-0.5, +0.5) (37, 35, 41, 46, 41, 34, 51, 47, 40, 18). A
hop index would be a spike at 0. The ~22 ms published /
~36 ms clock floor is peak jitter.

**Rejected (2026-09-21), refuse FISSO→live leave when the
accept gap is ≥1.5 held-beats and g≥0.45.** Aimed at 208685
t=28.98→30.34 (1.97 held-beats, g=0.53, ioi present,
foldToAnchor comb 86.9 vs held 86.8 — not a snap).
`kGridStaleBeats` (2.5) misses that leave. Gap≥1.5 alone
lights fisso 1009/64361/88118 and gradino 250062 (g 0.01–
0.11). Quadratic ~0 / IOI-gone miss 208685. The g≥0.45
conjunct is 0 offset-0 fisso/gradino (`8e3c` / `a624`
identical). Continuo **36.636/89.498 → 36.921/92.018**,
hash `6e558619f0b8d88b`, recovery 0, authority 27. 208685
p95 **92.3→132.6** (staying FISSO through the miss climbs
phase). 200766's 1.02-beat 2-vote leave was not this gate.
Reverted. Do not stack another FISSO-leave threshold.

**Rejected (2026-09-21), live leftover-faster comb ruler/snap.**
145333 t=58.88: short 174 vs comb 163, IOI already on the comb
(T 160). Gating that lattice without a new origin left the
clock on the first false peak (live comb gate REJECT). Opening
the ruler on live for a *slower* short moved offset-0 gradino.
This only re-indexed when `short>comb` by >5%, IOI on comb
within `kUnknownIoiLead`, `sres>0.030`, fits agree, `bpm>=75`,
`bir>=kLongFit`, `apart<0.18`. 0 offset-0 fisso/gradino on the
feel curve log (2–6 continuo frames, all 145333). Hashes
`8e3c` / `a624` identical. Continuo **37.233/90.882 →
37.606/91.174**, hash `ad5419c764e72006`, recovery 0, authority
27. Dumping the leftover ring still costs family phase. Do not
reopen the comb ruler on live, including leftover-faster.

**Rejected (2026-09-21), skip `pullTowardsComb` on live when
comb is slower, quadratic rate is negative, and IOI > short.**
Aimed at 192847 t=54.56–55.48 (unturned comb 58 while T 61–62).
Even `|ioiDev|>1%` and comb apart >0.5% lights fisso 1009 t=84.60
and gradino 210467. A post-gap window of `kGridStaleBeats`
lights gradino 210467/281738. Receding onto the comb during
that hole is already the documented Door C lag; do not invent
the missing quarters. Unknown Door C lastBeat origin on 216604
t=51.10 already REJECT (mean up). skip-to-zero and hugeGapComb
already FAIL.

**Listen candidate (2026-09-21).** Leftover 0.05 plus 4-beat
origin pull (fourCloser and live fourBetween toward lastBeat;
leftover-faster unknown origin skipped) plus clock-only ioiLead
above 75 at quadratic 0.80, `bir>=8`, plus live four-lead
clock-only when the IOI is quiet, plus Door C IOI+`kLiveLead`,
plus FISSO IOI leave at g 0.45 / 2.5%. User listens on the
product file/mixer path. That path already sets
`directLivePhaseFollow` on stable direct live (`kGridTauMotion`
0.30 s), so VPAlign MIXER 12 s at 0.90 s tau overstates live
file/mixer. Wiring 0.30 into `VPAlign` MIXER moved the 100 BPM
flat 22.3→24.1; do not rebase gradino hashes to hide that.

The remaining matrix tails are not another rate. 192847's 4 s
hole (50.50→54.56, p95 154 at Door C t=57.42) is an accelerando
through clustered `missChance` of the strong quarters, not a
programmed kit gap (`seed&3=3`). At t=50.50 phase is 2 ms and
truth is turning up; comb is 58.3 (unturned) and `motionFitRate`
is still negative. Receding onto the comb, or coasting the last
rate, would slow the clock while the line speeds up. The first
IOI after the hole is the *average* of four old-tempo periods
(59.1 vs T 61.2), which is why Door A take 1.0 is two BPM late.
Live Door C at 1.0 already REJECT. Do not invent the missing
quarters and do not accept the remaining 0.23 swing offbeats as
beats.

Door A above 75 with comb-sign and `g>=0.60` is 0 offset-0
fisso/gradino on the leftover-0.05 curve log (g=0.50 lights
gradino 234224 t=47.76). The silent hits are 177009 t=53.04
(i4=124 vs T=119) and 161171 t=79.70 (short already on T, IOI
low). Same overshoot as the g=0.99 reject. Do not open Door A
above 75.

**Rejected (2026-09-21), FISSO-leave catch-up at rate 1.0 below
75 BPM, quadratic 50%, `|IOI−held|>0.045`.** 0.020 moved fisso
1009 (held 67.3 vs IOI 69.6). 0.045 is silent on offset-0
hashes (192847 t=29.92). Continuo mean 41.527→41.469, p95
**94.773→94.785**. Compare requires both strictly down.
Reverted.

**Rejected (2026-09-21), unknown Door B clean 4-beat (`r4<0.015`)
at `kRateDoorHold` 0.45.** Silent on offset-0 hashes (216604
t=43.54). Continuo mean 41.538→41.521, p95 **94.784→95.204**,
p995 261→234. Live 0.30 stays when the 8-beat is clean. Strain
1.0 stays. Reverted.

**Rejected (2026-09-21), FISSO leave at |IOI−held|>0.028 plus
`|short−held|>1.2%`, no extra quadratic.** Silent on offset-0
hashes (192847 t=29.04, one beat earlier than 0.035). Continuo
mean 41.562→41.452, p95 **94.812→94.814**. Compare requires
both strictly down. Reverted.

**Rejected (2026-09-21), live Door A above 75 at quadratic 0.99,
r4<0.015, |IOI−short|>0.020, fourCloser, `kLongFit`.** Silent
on offset-0 hashes (169090 t=43.80). Continuo **41.562/94.812
→ 41.944/96.493**. ioiLead tau at 0.01 on a 4-beat still
above truth fattened the family. Reverted.

**Rejected (2026-09-21), live `!haveShort` recede comb at 1.0
above 75 BPM, `|comb−bpm|>3%`, same octave.** Curve-log t≥24
was 0 gradino; C++ `foldToAnchor` folded gradino 273819's 69
comb onto 138 and moved the hash (`a624` → `533314`). Continuo
mean **41.689→42.808**. Unknown recede stays below 75.

**Rejected (2026-09-21), unknown dirty-4 as BPM when the
8-beat is clean (`sres<0.015`).** Silent on offset-0 hashes
(216604 t=42.68). Continuo **41.689/95.086 → 43.835/103.011**
with hold, **43.798/102.917** one frame. Clock-only stays.

**Rejected (2026-09-21), lattice leave at 1.2% short+motion.**
Identity vs gap-recede (quadratic still closed). Reverted.

**Rejected (2026-09-21), live Door C IOI at rate 1.0.** Silent
on offset-0 hashes (192847 t=57.42). Continuo **41.868/96.582
→ 41.878/96.792**. Same overshoot as unknown Door C at 1.0.

**Rejected (2026-09-21), Door A take at `bir>=16`.** Silent on
offset-0 hashes. Mean 41.868→41.861, p95 identical
96.581763. Compare requires both strictly down.

**Rejected (2026-09-21), live yank-to-comb (commit comb, no
tau, no sal floor).** Silent on offset-0 hashes (145333
t=58.88). Continuo **41.868/96.582 → 42.411/97.128**. PLL
follows the 174 lattice; unknown yank-without-tau does not
transfer to live.

**Rejected (2026-09-21), live Door B take 1.0 with Door D
residual and unknown IOI lead.** Silent on offset-0 hashes
(192847 t=58.38). Continuo **41.868/96.582 → 42.092/96.966**.

**Rejected (2026-09-21), unknown Door C at `kRateLive`.**
Silent on offset-0 hashes. Continuo **41.868/96.582 →
41.884/97.070**, p995 **278→280**. Acquiring stays.

**Rejected (2026-09-21), unknown comb snap at residual 0.035.**
Silent on offset-0 hashes. Continuo **42.011/96.610 →
42.515/98.630**, p995 **278→222**. t=50.18 is the 73 yank:
dumping that lattice quiets the grid (dump-ring) even as the
tail drops. Walk residual 0.050 is the KEEP.

**Rejected (2026-09-21), unknown Door C at rate 1.0.** Silent
on offset-0 hashes. Continuo mean **42.231→42.034**, p995
**318→313**, p95 **97.784→98.046**. Taking the IOI fully at
t=51.10 (67.6 vs T=65.0) fattened the family percentile even
as the tail dropped. Strain Door B at 1.0 is the KEEP; Door C
stays `kRateAcquiring`.

**Rejected (2026-09-21), live comb *gate* (no snap) when the
8-beat has left the comb by >5% same-octave with no quadratic.**
The accepted-beat log is 0 fisso/gradino, but the gate uses
the previous peak's `g` on the *next* onset and moved offset-0
gradino (`a6249731026d9f82` → `a687033bc3761433`). Continuo
**42.231/97.784 → 42.832/102.173**. Snap on that band was
already the dump-ring. Do not open the comb ruler on live.

**Rejected (2026-09-21), live comb ruler when the 8-beat has
left the comb by >5% same-octave with no quadratic.** Silent
on the curve log (0 fisso/gradino rows) but snap uses the
previous peak's `g`, so offset-0 gradino hash moved
(`a6249731026d9f82` → `571ff96683c1038b`). Continuo mean
**42.249→43.110** even though p95 dipped. Dumping the 174
lattice is the dump-ring failure: nKept is small and the grid
goes quiet. Do not open the comb ruler on live.

**Rejected (2026-09-21), arm `ioiClockLead` on the unknown
yank-to-comb.** Silent on offset-0 hashes. Continuo mean
dipped 42.249→42.240, p95 **98.010→98.194**, p995
**319→326**. G1 took the IOI with tau; this kept the comb and
still locked the 73 peak. Do not arm tau on that comb commit.

**Rejected (2026-09-21), live rate 0.15 when the 8-beat has
left the comb by >5% same-octave with no quadratic.** Silent
on offset-0 hashes. Continuo **42.249/98.010 → 42.840/98.941**.
Receding onto the comb fattened p95 because the PLL followed
the 174 times; slowing the walk toward those times is the
same geometry. Do not slow the live rate on that band.

**Rejected (2026-09-21), Door A above 75 with g≥0.99, r4<0.015,
4-closer, Door D hold at 0.45, no comb-sign.** Comb-sign plus
`combReady` is identity: the one comb-sign frame is sal 0.087,
below the floor. Dropping comb-sign is silent on offset-0
hashes. Continuo **42.249/98.010 → 42.517/99.806**. Acquiring
yanked past that 4-beat; holding it still overshoots. Do not
open Door A above 75.

**Rejected (2026-09-21), skip the unknown `!haveShort` comb
commit when `|comb−bpm|>1.2%` after `kLongFit` below 75 BPM.**
Silent on offset-0 hashes (fisso 119794 is 0.0%). Continuo
**42.249/98.010 → 45.108/107.524**, p995 **319→458**. The
leftover comb is how other unknown gaps recover phase
(216604 t=48.56 143→11 ms). Do not skip it.

**Rejected (2026-09-21), keep the Door B/D 4-beat hold across
`staleBeats` (1.5 periods), skip refresh onto a same-lattice
8-beat, and do not arm tau on `!haveShort` hold.** Silent on
offset-0 hashes. Continuo **42.249/98.010 → 43.139/99.254**,
BPM>4% 4.85→5.44. Holding the pre-gap 4-beat through false
peaks costs the family; the comb commit on the 73 yank is the
KEEP. Do not keep a held 4-beat past the motion-stale
timeout.

**Rejected (2026-09-21), keep motion authority 4 periods after
the last beat on direct live/unknown.** Silent on offset-0
hashes. Continuo mean **42.249→41.593**, p95 **98.010→98.514**,
recovery **0→7**, authority 27→1203. A kit gap and a drummer
stop do not separate on a longer stale window; the 1.5-period
cut is how dropouts drop the faster clock loop.

**Rejected (2026-09-21), unknown Door B hold `kLongFit` instead
of `kShortFit`, and skip refresh onto a same-lattice 4-beat.**
Silent on offset-0 hashes. Identity vs `4341204575f0e1b2`.
The 73 yank is a haveShort commit after the hold window; a
longer counter does not move the hashed clock. Do not invent
a gap tempo.

**Rejected (2026-09-21), unknown `!haveShort` commit of the
16-beat quadratic (g≥0.50) instead of the comb, and persist
that endpoint through the kit gap.** Silent on offset-0 hashes.
Identity vs curve-on-lattice (`fe5d97305650251b`). Those log
rows are published hypotheses; replacing the fold there does
not move the hashed clock. Do not invent a gap tempo, and do
not treat a no-8-beat publish as a second measurement.

**Rejected (2026-09-18), Door C target = 2×IOI − 4-beat.**
Silent on offset-0 fisso/gradino hashes. Continuo
**43.08/103.03 → 43.09/103.21**. The extra step past a still-late
IOI is not free: family p95 moves the wrong way. Do not
extrapolate Door C past the IOI.

**Rejected (2026-09-18), skip comb pull only while comb≈8-beat
(<3%).** Silent on offset-0 hashes. Continuo mean
**43.08 → 43.27** even though p95 dipped 103.03→102.95. The
unconditional skip on the door persist is the KEEP; restoring
the pull on a far comb undoes more than the runaway 4-beat.

**Rejected (2026-09-18), tighten live no-8-beat keep to
`kCombFoldOrigin` when the comb agrees with the committed
tempo.** Lights offset-0 fisso (`c09db6c53b628ed8`) and
gradino (`5e0a70e4f72f3f1c`). Continuo mean **43.90 → 44.57**
even though p95 dipped. The 0.18 keep is how a dropout
recovers; false-peak yanks and true-beat recovery do not
separate on comb agreement.

**Rejected (2026-09-18), FISSO `kFixedMaxStep` walk on a clean
quadratic with 4-beat still on the 8-beat.** Silent on
offset-0 fisso. Lights gradino (`4b125a5194bfe90c`). Continuo
mean **43.90 → 44.20**, p95 104.26→102.96, recovery 1,
authority 27→64. A clean 4/8 agreement is not a step veto.

**Rejected (2026-09-18), do not steer clock phase from decoder
onsets while ioiLead persist has no 8-beat.** Silent on
offset-0 fisso/gradino hashes. Continuo **43.90/104.26 →
44.10/106.40**. Persist-gap decoder phase is how other live
holes recover; it does not separate false-peak yanks from
true-beat catch-up.

**Rejected (2026-09-18), Door D at `kRateAcquiring` with
4-beat residual 0.045.** Same silent band as KEEP Door D, but
taking 153252 t=67.36 (r4=0.036, phase already 200 ms) and
yanking 169090 at 0.70. Offset-0 hashes identical. Continuo
**46.75/111.56 → 46.76/112.73**, p995 400.05 unchanged. Do not
open a mixed-window 4-beat at the acquiring rate, and do not
take a 4-beat residual that wide through an overshoot.

**Rejected (2026-09-18), dump the unknown beat ring to the newest
two times on the G1 frame.** Same silent band as G1 (offset-0
fisso/gradino hashes identical; 119794's fold is still on the
8-beat). There is no 2.5-period hole to split: false peaks fill
the arrangement gap, so a wall-clock compact is a no-op. Dropping
the stale 8-beat lattice instead of taking the IOI still skips
the 73 yank. Continuo **46.71/111.51 → 48.53/117.76**, p995
400→372. Max without p95, worse than taking the IOI. Do not drop
the first post-gap 8-beat.

**Rejected (2026-09-18), unknown IOI on the dirty post-gap
frame (216604 t=51.1).** 4-beat residual 0.054, IOI and fold
both left the 8-beat by >3.5%. Silent on offset-0 fisso/gradino.
`kRateLive`: mean 46.85→46.78, p95 **111.97→112.30**, p995
400→405. `kRateAcquiring`: mean 46.85→46.75, p95
**111.97→112.17**, p995 400→384. Same p95 miss as skipping the
t=50.2 yank. Do not take the median IOI on a dirty 4-beat that
is still the stale lattice.

**Rejected (2026-09-18), unknown IOI or hold on the one post-gap
clean same-lattice frame.** `|IOI−short|` and `|fold−short|` both
`> kUnknownIoiLead`, residual clean, `|i4−short| < 1.2%`. Silent
on offset-0 fisso/gradino (24766's fold is still on the 8-beat).
Only 216604 t=50.2. Taking the IOI at `kRateLive`: mean
46.86→46.81, p95 **112.43→112.51**, p995 401→385. Holding BPM:
mean 46.86→46.82, p95 **112.43→112.57**, p995 401→389. The yank
to 73 fattens the max; skipping it fattens that seed's p95.
Do not retarget the first post-gap 8-beat without a current
4-beat on the pulse.

**Rejected (2026-09-18), Door B window: freeze gap BPM and/or
refuse an 8-beat that recedes from the comb.** Armed for 12
periods of the 4-beat (covers t=50.2 and t=51.1, expires before
later gaps). Not a stored 4-beat. Offset-0 fisso/gradino hashes
identical. Freeze through `!haveShort` (skip the comb pull) plus
recede-hold: continuo **46.88/112.49 → 50.50/122.56**, p995
401→457. Recede-hold on haveShort only: **47.15/114.44**, p995
401→398. Holding 70 while truth is 66 through the gap adds
phase; refusing later 8-beats after Door B hurts other seeds'
p95. Do not arm a post-Door-B window.

**Rejected (2026-09-18), unknown `kRateLive` after `kLongFit` as
the default commit rate.** Slows the t=50.2 yank, but offset-0
fisso/gradino hashes both move (`8e3c8d2cdc5854f5` /
`a6249731026d9f82` → `2bf414d240fbd665` / `86bdd7a7987d4d2d`).
Continuo **46.88/112.49 → 49.46/122.31**. Fisso mean/p95
numerically better (22.3/76.9 → 20.3/68.7) is still a FAIL
against the identity hashes. Unknown stays on `kRateAcquiring`
except Door B.

**Rejected (2026-09-18), unknown IOI on a clean 8-beat that
equals the 4-beat (same lattice).** Subset of the IOI-when-fold-
left-it reject: residual < 0.045 and `|i4−short| < 1.2%`, then
`target = IOI` at `kRateLive`. Offset-0 **fisso hash moved**
(`8e3c8d2cdc5854f5` → `7b3deaf73848714c`); gradino held.
Continuo mean 46.88→46.83, p95 **112.49→112.58**, p995 401→385
— same p95 miss as the unrestricted IOI path. Do not take the
median IOI when the 4-beat is the stale 8-beat.

**Rejected (2026-09-18), live Door A without comb-sign after
`kLongFit`, alone.** Silent on offset-0 fisso/gradino (12
continuo frames: 137414, 192847 t=70.3/t=86.5, 224523). Continuo
mean 46.880→46.875, **p95 identical** 112.4939. Those frames sit
below the seed p95. Stacked with dirty unknown Door B at
acquiring it is KEEP above.

**Rejected (2026-09-18), comb ruler when IOI and fold have left
a clean 8-beat.** Same two frames as the IOI-rate reject
(216604 t=50.2/51.1). Silent on offset-0 fisso/gradino. The
fold is still ~5 BPM late; re-gating onto it **46.88/112.49 →
48.11/117.19**, p995 401→453. Do not use the comb as a peak
ruler while it is the late pulse with a clean residual.

**Rejected (2026-09-18), hold unknown BPM when the 8-beat recedes
from the comb.** Same 0.035 IOI + comb-sign, but `target = bpm`
instead of the IOI, then `pullTowardsComb`. Offset-0 **fisso
hash moved** (`8e3c8d2cdc5854f5` → `87ddc99bfd1bb214`); the
log's short-vs-bpm test is not the product's short+lead target.
Continuo p95 112.49→112.63. Do not add an unknown recede hold.

**Rejected (2026-09-18), latch unknown Door B through haveShort
beats.** After t=43.5 the phase is 36 ms, then the kit gap pulls
the stale comb at `kRateAcquiring` and the first post-gap 8-beat
reads 73. Holding the 4-beat for eight fitted beats (and
skipping comb pull in the gap) is silent on offset-0
fisso/gradino. Continuo mean 46.88→51.34, p95 112.49→116.50;
seed 216604 **81.3/235.2/401.4 → 152.7/299.3/381.1**. The t=43
4-beat is itself soon late. Do not keep a 4-beat lead onto later
fitted beats.

**Rejected (2026-09-18), hold the Door B 4-beat only on unknown
no-fit peaks, by beat count or by one short-fit window of time.**
Same silent band. Beat-count: mean 46.88→49.68, p95→117.55,
p995 401→366. Time window (8 periods): mean 46.88→48.57, p95
still 117.55, p995 366. The gap max shortens; family mean and
p95 do not. Do not retarget unknown-gap tempo from a previous
4-beat.

**Rejected (2026-09-18), unknown IOI at `kRateLive` when the fold
has also left the 8-beat by 0.035.** After the gap the 4-beat
equals the 8-beat (same stale lattice); fold and IOI agree
against it. Silent on offset-0 fisso/gradino (24766's fold is
still on the 8-beat). Continuo mean 46.88→46.83, p95
112.49→112.58, p995 401→384: seed 216604 81.3/235.2/401.4 →
80.5/236.5/384.1. Mean and max without p95. Do not take the
median IOI after the gap without a current 4-beat on the pulse.

**Rejected (2026-09-18), unknown Door B at `kRateAcquiring`.**
Same 3.6% IOI/short gate, substituting the 4-beat into the
unknown 0.70 commit. Offset-0 fisso/gradino hashes identical.
Continuo mean 46.92→46.94, p95 113.16→112.64: seed 216604
82.0/245.8/406.1 → 82.3/237.6/404.5. Opening at 1.2% also
fires t=58.9 (already on the pulse) and raises the family mean
46.92→47.64. Do not yank unknown at the acquiring rate, and do
not open this door on a 1.2% IOI disagreement.

**Rejected (2026-09-18), comb-fold snap without the salience
floor.** `stalePulseCombRuler` already matches t=52 except
sal 0.129 < 0.14. Dropping the floor on the snap only is
silent on offset-0 fisso/gradino and fires that one frame.
Continuo **46.92/113.16 → 47.19/113.27**. The 8-bin fold at
salience 0.13 is not a usable origin. Do not snap unknown-gap
phase from the fold below `kSalienceFloor`.

**Rejected (2026-09-18), unknown no-fit IOI lead when the median
is more extreme than the comb vs the committed BPM.** Silent on
offset-0 fisso/gradino (hashes identical) and on the control log
three frames of one seed, all closer to truth than the comb.
Product continuo **48.51/117.31 → 49.82/121.14**, p995 355→437.
Rate without a grid still lengthens that seed's tail. Do not
retarget unknown-gap tempo from the 3-interval median.

**Rejected (2026-09-18), snap that keeps beat history.** Same
comb-fold origin, but every stored `beatTime[]` is shifted with
`lastBeat`/`gridAnchorSec` instead of dumping the ring. Offset-0
fisso/gradino hashes identical. Continuo **50.57/122.42 →
56.82/128.04**; seed 216604 **107.5/312.3 → 207.4/402.3**. Mixed
stale+true times after the origin move are worse than a quiet
grid plus the fold. Leave the dump.

**Rejected (2026-09-18), unknown no-fit origin from the comb fold
when the 3-interval median is still alive.** After the snap dump
the 8-beat line is gone for seconds, but 4–7 peaks still produce
an IOI. Sliding `gridAnchorSec` onto `beatPhaseFor(comb)` on that
gate is silent on offset-0 fisso/gradino (hashes identical) and
fires only on seed 216604. Continuo mean **50.57→50.76**, p95
122.42→120.54: that seed's p95 312.3→282.3, mean 107.4→110.4,
max 355.1→361.4. The fold is still the late comb; yanking the
origin onto an 8-bin late phase trades the tail for mean and a
longer max. Do not steer unknown-gap phase from the fold while
the comb itself is the stale rate.

**Rejected (2026-09-18), unknown ruler floor 0.045 log2 after
`kLongFit` beats, index-only or with the peak gate.** The 8.7%
stale-grid floor never fires on the slow unknown deceleration
(comb−short sits at 4–11%). Lowering it to ~3.2% once the long
window has formed is silent on offset-0 fisso/gradino (hashes
identical) and improves continuo mean 53.79→52.91, but p95
127.71→128.27. Indexing alone produces the same hash as also
changing the peak gate. Do not keep a mean-only gain against the
p95 clause.

**Rejected (2026-09-18), unknown ruler floor 0.045 plus keep
tighter than the pulse split.** The p95 hit above was the 0.12
keep admitting both pulses at 4-8%. Tightening keep to
`max(0.035, 0.40*split)` only after `kLongFit` (and only while
the split itself is inside 0.12) still left offset-0 fisso/gradino
visually unchanged, but continuo **53.79/127.71 → 54.4/130.0**.
From a stale `lastBeat` the tighter comb lattice rejects the next
true peak as well as the stale one, so the grid goes quiet and
re-anchors worse. Do not couple a lower floor to a tighter keep
until the gate origin is the comb fold, not the last stale peak.

**Rejected (2026-09-18), unknown short+lead veto when the line
recedes from the comb.** Same silent band as the lowered ruler
floor (dirty agreed 8-beat, bir > 24, comb−short 3.2–18.9%), but
the commit kept the current BPM and let `pullTowardsComb` act
instead of reindexing peaks. Offset-0 fisso/gradino hashes
identical. Continuo mean 53.79→53.04; the only moved seed had
mean 153.3→141.3 and p95 **393.7→404.9**, so family p95
127.71→128.42. Rate without grid/phase still lengthens the tail.
Do not retarget unknown from this band.

**Rejected (2026-09-18), live ruler gated on comb motion
(2.5–8% vs a 4 s delayed fold, same direction as comb−short).**
The high-pass was meant to tell a gliding pulse from a step jump
(~10-16% in one publication) and from a flat metrical fight (comb
unmoved). Off the control log it looked silent on fisso/gradino
and fired only on the slow live deceleration. On the product
decoder it moved offset-0 gradino (mean 35.96→36.31, hash
`a6249731026d9f82` → `059d7e48e2625b21`); continuo 53.79/127.71 →
53.34/126.76 was not keepable. A delayed comb interpolating
through a step still occupies the 2.5–8% band. Do not reopen live
with a comb-velocity window.

**Rejected (2026-09-18), FISSO walk toward `motionFitBpm` instead
of the short fit.** Same two-vote strain gate and `kFixedMaxStep`
cap. Offset-0 hashes bit-identical (the 1.5% cap already bound on
the 12 s class). VPAlign 12 s unchanged 32.1/82.0; 130 MIXER *dopo*
6.5→7.0. The cap is the walk; a further destination does not buy
phase and can still tick a flat that sits in the strain band.

**Rejected (2026-09-18), wider strain residual + two-vote strain
release.** The 12 s MIXER worst class sits in FISSO with two
causal votes while short residual is 0.051-0.054, just above
`kMotionCurveWalkResidual` 0.050, so strain never counts and the
two-vote clean door (`< 0.030`) stays shut. Raising the strain
ceiling to 0.056 and releasing on `walk>=1 && votes>=2` left
offset-0 fisso/gradino/continuo hashes bit-identical (the matrix
never entered that band) but moved `VPAlign` 12 s MIXER mean
**32.7→34.1** (worst still 93.2). Reverted. Do not reopen the
strain residual ceiling from the 12 s worst class; flats and
dropouts already overlap 0.046-0.052 and the quadratic does not
save the four-seed mean.

The remaining two recovery counts on seed 129495 were a dropout at
t=63–65 (short residual 1.0, phase 216 ms) and a 55 ms wander at
t=83–85, fifteen and thirty-five seconds after the one-beat proof.
The probe now expires `shadowProven` twelve truth beats after the
last authority frame (the shape quarantine), so a dropped claim
does not put the rest of the run under a 50 ms SLA. During the
proof itself phase was 21–38 ms. Offset-0 continuo recovery 5 -> 0
under that window. `compare_motion_matrix.py` vs the HEAD control
CSV then fails only on the new gradino hash; every other clause
passes. A lifetime-latch follow after proof was tried and reverted:
it did not close those later holes and nudged continuo mean
54.03 -> 54.07.

`Assets/Models/beatnet.onnx` is present (~1.6 MB). Direct-path real
audio smoke (2026-09-18, ONNX, not iPad):

- INFINITO mixer excerpt `mpg123 -k 1148 -n 1531` (~30-70 s) through
  rebuilt `VPLive --mix --bpm 91`: BeatNet ONNX, mean BPM 91.15
  (0.16%), relative drift **7.1 / 33.8 ms** mean/worst (documented
  acquisition-refinement pass was 9.0 / 25.1; mean better, worst
  worse, one run, no annotated grid). Loaded-file `VPTrack --player`
  on the same wav: lock held 3 s at **2.26 s**, **98.4%** of time
  inside 2% of 91 BPM. Click mix:
  `/tmp/vp-real/infinito-clickmix.wav`.
- `makedip.py` exact grid through `VPTrack --player --pulses`: after
  the 90→86→90 dip, peak **+105.7 ms** at +3.4 s, **return ≤15 ms
  held 4 s in 5.6 s** (item 40 was +88 ms / **18.1 s** on kitMic
  without this causal release). Decoder now leaves FISSO (88.78 VIVO
  at t=70). Click mix: `/tmp/vp-real/dip-clickmix.wav`.
- `rall4.wav` loaded file: published 90.00 FISSO until ~t=68, then
  VIVO 88.86→85.34→84.57 following the comb 86.08. Not a certified
  phase grid (generator not in tree).
- Flamingo brano 1 mixer send, standard `extract_live.swift` centre
  (3 s silence + 140 s from t=75 of
  `Flamingo Marco 09.07.26.m4a`) through `VPTrack --player --pulses`.
  One analysis epoch at 3.3 s (silence into the song), then no further
  restart. Comb briefly reads the double (163) until t=14, then the
  folded 81. Published 80.0–86.5. Causal release: unknown until t=26,
  VIVO at t=28 on the 82→84 climb, FISSO again at t=72 around 81.5,
  VIVO again at t=84 on the 81→85 accelerando. Independent tempogram
  (65–95 BPM): 82.8 / 83.3 / 80.7 / 81.4 / 84.4 / 85.3 / 81.6 along
  the same minutes; published lag is 0.2–1.4 BPM on the quiet drift,
  **2.9 BPM late** at t=80 (start of the accel, still FISSO) and
  **3.9 BPM high** at t=100 (dirty residual 0.17 while catching the
  ritardando). Beat-phase after t=16 never jumps more than 0.008
  beats (6 ms) in a block — no sounding snap. One bar-trust jump at
  t=43.68 (`bar` 0.52→0.77, beat phase continuous) when the downbeat
  vote locks. Click mix: `/tmp/vp-real/flamingo-clickmix.wav`.

No annotated beat-grid on the band recordings. Human listening of
the click mixes (INFINITO, dip, Flamingo, Sally) is the user's
pass, not part of this decoder loop. iPad microphone is out of
this objective. Sally click mix:
`/tmp/vp-real/sally-clickmix.wav`.

Grid-vs-music (`hist.py` / `prec.py` on `VPTrack --player` pulses),
the measurement this repo uses when there is no click track:

- INFINITO 8-38 s: struttura **5.73** (agganciato is >2), inter-window
  phase slip **0.0 ms**, grid jerks 0.55% std / 1.4% worst. Every 8 s
  window 0:00-0:32 is agganciato. The VPLive `--bpm 91` 7.1/33.8 ms
  figure is spread against a rigid metronome, not against the band;
  the same take's clock vs 91 after t=8 from pulses is **4.1 / 13.2 ms**.
- dip 20-55 s (before the dip): struttura 5.88. After the dip (63-90 s):
  struttura **6.80**, still agganciato, 8.0% worst rate jerk while
  catching 90→86→90.
- Flamingo brano 1, 8 s `hist.py` windows 0:00–2:16: every window
  **agganciato**. The only acquisition slip is 0:08–0:16 (−288 ms,
  octave fold). Body 16–80 s (slow drift): struttura **3.96**,
  window slip worst **30.4 ms** (one 24-bin), jerks 1.23% / 4.6%.
  Whole 16–140 s: struttura **3.59**, median slip 30.4 ms, worst
  **152.1 ms**, jerks 2.54% / 8.1%. The 80–140 s accel+ritardando
  is the remaining hole: struttura 2.76 (still >2), worst window
  slip 152 ms, jerks 3.43% / 8.1%, four `<<< SLITTA` windows while
  the decoder is 3–4 BPM off the tempogram. Historical HIGH on a
  longer take of the same song was struttura 3.61 / worst 364 ms
  (`docs/TODO.md` item 35); this smoke is not that take and not a
  claim that the 364 ms hole is closed.
- Flamingo Sally (centre t=680, same 3 s + 140 s extract) through
  `VPTrack --player`: cold start locks the double (198–211 sounding
  for 17.7 s) then folds to ~105 by t=36. Comb already reads 103 at
  t=16. **HEAD without the uncommitted causal-release diff is
  line-for-line the same through t=48** (198.57 / 206.33 VIVO /
  208.22 vs 209.35 / 183→105); this is the pre-existing octave
  hole, not a regression of the FISSO-release work. After the fold
  (52–140 s): struttura **3.02**, median window slip 69.5 ms, worst
  **185.5 ms**, jerks 4.01% / 15.3%. One sounding beat-phase step
  of 0.19 beats (−109 ms) at t=45.16. Historical HIGH on Sally was
  struttura 3.04 / worst 263 ms. Do not retune octave or live lag
  from this one centre.

Offset-16 `--quick` smoke (not the offset-0 control): fisso/gradino
authority 0, continuo authority also 0 (the two-win shape lead is
silent on that draw). Continuo mean 50.8 ms, recovery 0. Do not
restore the 35% first-verdict to fill that anti-vacuity column.

**Both of those "must"s were looser than they read, and the cost was heard.** A
listener reported percussion that occasionally slowed or sped up on a live
recording and took a long time to come back. Measured with
`probe_steady_tempo --verbose` at *constant* tempo: eight excursions between 110
and 160 BPM, 4.0-5.5% for 1.8-3.0 s, at 80, 88, 155, 171, 243, 270, 282 and 295
seconds - not acquisition, which is what docs/TODO.md item 18 had assumed. At each
one the comb read the true tempo while the transition published something 4-5%
away and dropped both fits. Two causes:

- the candidate threshold was `max(1 BPM, 3 * jitter)`, about 4.2% on ordinary
  material, **below** `kTransitionSmallestStep` (5%) which the same file calls
  the smallest step worth claiming;
- the coherence test compares `deviation`, which is *half* the relative
  difference, against a tolerance of `2 * jitter` - so two intervals could
  disagree by **four times** the measured scatter and be called one tempo.
  Measured at the false confirmations: pairs 4.1% apart, as far from each other
  as the step they claimed.

Now `needed` never goes under `kTransitionSmallestStep` and the tolerance is
`1 * jitter`. Eight excursions became three, with 110/120/132 clean over ten
300 s runs each, and nothing else moved: `probe_tempo_step` is identical row for
row, and `VPAlign`'s five protected steps still reach +/-1 BPM in 0.78-1.47 s at
23.3-24.4 ms. The three left are at 144 and 160, where the pair really does agree
inside the jitter and passes on the absolute floor; the discriminator that would
catch them is the comb, which was right every time - but at candidate-start the
comb still reads the old tempo on a genuine step too, so using it means confirming
and then retracting, which is the shape of the watchdog already tried and reverted
in 8528d4e. Do not retry that blind.

**Historical rate-only measurement, not a phase-recovery guarantee.**
The old `fillRecovery` in `probe_steer` times the return of instantaneous rate
inside 1%, not the return of beat phase; it cannot exclude a standing offset.
Use `probe_recovery.cpp` for phase recovery and HANDOFF_TEMPO.md for the scope
of the latest integrated measurements.

**The clock is not where this lives, and that was measured before the decoder
was touched.** `scripts/probe_steer.cpp` holds a wrong phase for two seconds and
times the grid's return: 0.06-0.28 s at every FollowStrength, HIGH fastest. It
did find that HIGH - which is the shipped default in `Types.h`, though the table
below calls medium the default - makes excursions two to three times larger than
LOW for no rms benefit on that bench (6.7% against 2.6% worst at 144 BPM, 0.55
against 0.06 seconds per minute outside 2%). That bench has no genuine tempo
change in it, which is the only thing HIGH exists for, so the default was left
alone. See docs/TODO.md item 21. The edge requirement matters:
without it, a 4 s or 12 s ramp eventually moves far enough from a fixed
committed BPM to look like a step even though adjacent intervals never jumped.
After confirmation, `rapid` is published through at most two accepted beats and
also has a decoder-frame deadline of two periods at the confirmed, user-octaved
tempo. The elapsed deadline is independent of peak eligibility, so silence,
off-grid peaks and dropout-like activation cannot leave transition diagnostics
or reset quarantine armed indefinitely. The separate eight-accepted-beat refit
lockout remains in force after `rapid` expires.
Measured in `VPAlign`, the five protected line steps reach +/-1 BPM in
0.78-1.47 s and 23.3-24.4 ms phase error at the third beat, with one transition
and no pulse-count violations; both ramps stay on the ordinary live-fit path.

Autocorrelation runs on **activations**, never on the waveform. Volume-peak /
SuperFlux tracking is deliberately not used.

### The octave escape hatch was blindfolded, and it is not any more

Before reading the "partly unsolvable" section below, know which part of it was
a genuine ambiguity and which part was a bug. A listener reported slow songs -
60 and 80 BPM - going out and not holding, while faster ones were fine.
Reproduced on material with **one impulse per beat and silence between**, where
doubling means putting half the grid on nothing, so it is not the ambiguity at
all: 60 BPM read double in 10 runs of 10 and never came back; 70 BPM in 9 of 10.

The fold was right the whole time - 61.3 BPM at salience 1.00 - and
`octaveMismatch` sat at zero. Three faults, nested:

- **`combDisagrees` tested `combBpm`, which is `foldToAnchor (tempo.bpm())`** -
  the fold's reading already folded onto the level under suspicion. The test for
  a wrong octave was run on a number the octave had just been taken out of:
  `log2(122.2 / 122.6) = 0.004` against a 0.25 threshold, every frame, forever.
  Same trap `checkGridPhase` documents for phase, and solved there by putting the
  fold outside the gate; for the rate the fold *was* the gate.
- **`unprovenSlowerOctave` used `gridHealthy` as evidence for the fast grid**,
  three lines under a comment explaining that a doubled grid always looks healthy
  because every beat lands on every other tick. `coverage` is one-sided: it sees
  a grid too *slow* (which must discard beats) and cannot see one too fast (which
  discards none). The missing half was free in the same loop - the grid indices
  the kept beats land on, 0/2/4/6 instead of 0/1/2/3. `fitPeriod` now reports
  their median gap; measured 2.0 exactly at 60 BPM with coverage 1.00.
- **and the snap itself was a no-op**: `bpm = clamp (combBpm ...)` re-committed
  the folded value, so it threw the beat history away and adopted the same
  doubled tempo, without moving `anchorBpm` either - so the next beats folded
  straight back.

Now the disagreement, the vote and the snap all read the unfolded `combRawBpm`,
the snap moves the anchor with it, and the veto asks whether the grid is dense
before defending it. Measured, `probe_steady_tempo` at 300 s x 10 seeds: 70 BPM
9/10 -> 3/10; 120, 132 and 140 from one to three runs each to **0/10**; **170
from 4/10 with 50% error and 26.5 s out to 1/10 at 4.4% and 0.1%**. The five
protected steps in `VPAlign` are unchanged to the hundredth (0.78-1.47 s,
23.3-24.4 ms) and the unprotected 100->140 improves from 26.70 to 15.14.

**Swung material is now understood during acquisition, rather than repaired
several bars later.** Before the fix, a strong beat plus a quieter swung
off-eighth locked within 2% in 1.9 s when straight at 81 BPM, but took **13.1 s
at swing 0.65 and 17.7 s at full swing**. The decoder read either half of the
long-short pair as an independent period and acquired the 1.5x level (81 ->
about 123).

`tryFastAcquire` now counts a repeated long-short cell as one quarter. On a
direct feed, strong-weak-strong closes the first cell in three peaks; through a
room, a fourth peak corroborates the second cell. Before a grid exists the peak
picker uses the fastest legal pulse for its refractory window, rather than the
120-BPM default, so the short return of full swing is not discarded. A bare HMM
answer is held until the same three/four peaks exist; this prevents a quick but
context-free wrong answer from escaping just before the cell can be measured.

Focused deterministic measurement at ratios 0.60 and 2/3, 52/81/96/120/168 BPM:
the direct path publishes the right quarter after **1.04-1.40 beats**, the room
path after **1.37-2.07 beats**, and every case is still on the same level after
ten seconds. Straight material across 52-160 BPM acquires in about three beats.
The entry cost is below one bar, comfortably inside the two-bar product
requirement; the old 13-18 s correction path is no longer entered.

**The remaining 60-BPM feedback bug is fixed.** Two separate acquisition orders
were undoing the right answer. First, the HMM can become ready after two periods
of its *104-BPM winner*, before two 52-BPM beats have elapsed; once the real
interval arrived, `tryFastAcquire` was never revisited because the provisional
HMM result had already set `established`. The provisional result is now refined
as soon as the second interval exists. Second, a confident HMM could rewrite
`anchorBpm` at another metrical level on every later frame, including the frame
after the comb had corrected it. A repeated comb octave vote now re-centres the
HMM tempo marginal with `BeatHmm::anchorMetricalLevel`, preserving each tempo's
conditional phase distribution, and an HMM level that disagrees with the
committed decoder level cannot overwrite the anchor on its own.

**A dense grid is not a right grid.** `gridIsDense` (the fit's median index gap)
catches a doubled grid when there is *silence* between the beats, and misses the
commonest case there is: a hi-hat on the eighths fills those ticks, so the
doubled grid reads coverage 1.00, residual 0.03 and gap 1.0, and every test the
`unprovenSlowerOctave` veto owns says it is fine. Measured on kit-shaped material
at 81 BPM - quarters at 0.8-1.0, eighths at 0.45 - the fold sat at 82 with
salience 1.00 for a full minute while the committed tempo held 164 and
`octaveMismatch` never left zero.

What the two grids do not share is the *weight* of their beats: on the pulse they
are all beats, an octave up every other one is a hat.
`recentStrengthAlternation()` measures that - medians by parity over the last
twelve accepted beats, **0.1-0.2 on a grid at the pulse against ~0.5 on one built
on a subdivision** - and stands the veto down when it passes 0.35 *and* the fold
names something within 20% of half the committed tempo. It only lifts the veto;
the snap still needs salience and `snapBeats` of votes. On the material bank:
rock eighths went from **8.30% of the run off the tempo to 0.65%**, its ratio at
81 BPM from 1.44 to 1.00, the bank's mean from 9.70% to 9.01%, and no row got
worse. `probe_tempo_step` and the click-track bench are identical to HEAD row for
row - the alternation cannot fire on a click, where every beat weighs the same.

**And measure on a bank of materials, never on one song.** `probe_matrix` runs
twelve shapes real records have - quarters only, eighths, sixteenths, backbeat,
half-time, swung eighths and sixteenths, a loose band at 25 ms of scatter, a mix
that swallows 18% of its beats, bars the arrangement drops out of, and chords
with no drums at all - across 60-165 BPM.

It exists because a narrower bench (tempo x swing only) reported a **1.78 s** mean
lock where the wide one reports **5.30 s**, and because the rows it added are
where the failures are: rock eighths at 81 BPM spends **8.3%** of its run off the
tempo, a swallowing mix takes 9.05 s to lock and 49.4 s at worst, arrangement
dropouts 9.61 s. `--ratio` separates the two kinds of wrong. Nearly every cell
reads 1.00 - the metrical *level* is right almost everywhere - with two
exceptions: rock eighths at 81 reads **1.44**, half the runs on the octave above,
which is the same slow-tempo-plus-filled-subdivision root as the swing case
below; and half-time reads 2.02 slow / 0.50 fast, which is the documented
ambiguity **plus** a fixture a decoder-only probe cannot judge, since the
convention keeping the pulse in a percussionist's range lives in
`BeatTracker::updateAutoOctave`, which this probe never runs.

The ranked work it points at, all of it *acquisition* rather than recovery:
rock eighths at slow tempo first (the strongest onsets are on the true beat -
`beatStrength` and `recentBeatStrengthMedian()` already exist and are used for
transitions but not for choosing the level), then a mix that swallows beats,
then dropouts and loose bands.

Focused deterministic measurement (`probe_steady_tempo BPM seconds seeds`, one
impulse per beat, 3 BPM live drift, 10 ms jitter): first correct 52-BPM lock
**~12.5 s -> 3.48 s (3.0 beats)** with the context gate above. Over 120 s x 5
seeds, 52 BPM spent **0.4%** outside 4%
(one 2.2 s excursion, worst 4.6%) and 60 BPM **1.5%**; 120 and 160 BPM were
0/3 runs with an excursion. The slow live/unknown target also blends the
three-interval median below 75 BPM because an eight-beat fit is over nine seconds
long at 52; its authority is capped at +/-4% and fades to zero by 75 BPM, so the
faster-tempo stability tuning is unchanged.

The quick regression is `VPTests --tempo-slow`: 52 locks at 3.48 s, the first
beat after a six-second musical gap returns at 26.56 s with 0.000 beat phase
error, straight 60/120/160 also lock in about three beats, and the ten swung
tempo/ratio cases pass on both line and room paths. It is decoder-only and is
the iteration gate for these paths; do not run the full suite for every change.

This does not repeal the ambiguity below: a real 52-BPM arrangement with strong
eighths can be acoustically identical to 104 BPM half-time. The focused real-
network slow-kit test still names 100 for the 50-BPM fixture; the ÷2 control is
the deterministic answer for that material.

**AUTO must not manufacture an answer from missing downbeats.** The old
threshold-crossing cadence path called two consecutive eight-beat gaps proof
that the grid was doubled. A correct 100-BPM track on which BeatNet simply
missed every other downbeat has exactly the same gaps, so that path could force
100 to 50 even while every quarter was clean. It is now gated off with the
already-disabled cadence experiment; the regression feeds 22 beats at 100 with
downbeats only every eight and requires no hint and a final 100 BPM.

The manual buttons are relative to the **effective displayed level**, including
AUTO's choice. Their stored value is absolute relative to the raw analysis and
may be stale while AUTO is active. Sending `+1` directly while AUTO was at `-1`
therefore jumped two octaves (50 -> 200), and `-1` from `+1` did the reverse.
`stepTempoOctave` now turns both transitions through zero: 50 -> 100 -> 200 and
200 -> 100 -> 50. Pressing the already-active manual button still returns to
AUTO.

### The octave (half / double time) is partly unsolvable

Measured on BeatNet output from a 76 BPM mix with full eighths, the activation
half a beat off the beat stands at 0.73-0.77 of the beat's own, against
0.02-0.18 at 104 and 128 BPM. 152 is a *defensible* reading of what the network
was given. Moving thresholds to break that tie makes the aggregate worse,
because the same asymmetry is what stops an ordinary rock backbeat reading as
half-time. So: AUTO keeps the pulse inside the range a percussionist counts in
(`BeatTracker::updateAutoOctave`, `Source/Tracking/BeatTracker.cpp:573`).

**But never while the part is sounding.** The level is chosen before the part
comes in, at a stand-down or after STOP, and held for as long as it plays.
Halving under a percussionist mid-performance is not a correction to them - it
is the grid they are playing against moving, and the part's density and the bar
move with it. Measured on a band drifting through the upper bound: a take at
168 BPM reached 168.20 at 23 s and the level halved to 84 at 26 s, never
returning (returning needs the reading to fall under `kOctaveTooSlow`). A new
input still earns its own level - `setInputEpoch` clears it - and ÷2/×2 stays
available while playing, which is the manual way out of a held wrong level.

### For one class of material it is not "partly" unsolvable - it is undecidable

The slow case is the sharp one, and it has been measured to the end: a straight
groove at 50 BPM (kick 1 and 3, snare 2 and 4, hi-hat eighths) reads as 100 and
will not be talked out of it. Three separate attempts to correct it from the
analysis are documented in `docs/HANDOFF_OCTAVE_50BPM.md`, with the numbers:

1. **Counting downbeats over a threshold** - never fires. At a slow tempo the
   network crosses the downbeat threshold too rarely for a count to close.
2. **The continuous downbeat curve**, phase-locked into an eight-slot histogram
   - fires on the wrong evidence. The network puts comparable downbeat mass on
   the true beat one *and* the true beat three (the same 1-vs-3 ambiguity the
   bar alignment fights), and at the wrong octave those two land exactly four
   slots apart: the same signature as a correct reading.
3. **Low-band energy per beat** (`LogSpectFeatures::lowBandEnergy`, 24 bands) -
   this one *works* on the target case, and is the interesting failure. Depth
   of the alternation between the two interleaved slot classes, at the level the
   grid has settled on: **0.43 when the reading is wrong against 0.85 when it is
   right**, a clean gap, and with the line at 0.55 the 50 BPM groove reads 50
   while 76/100/118/132/140 and the syncopated and pad styles are untouched.

Number 3 was still reverted, and the reason is the thing to carry away. It
halves half-time material - snare on three, nothing on two and four - from a
correct 100 to a wandering 60. That is not a threshold to retune:

| slot | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| straight at 50, read as 100 | kick | hat | snare | hat | kick | hat | snare | hat |
| half-time at 100, read as 100 | kick | hat | snare | hat | kick | hat | snare | hat |

Same spacing, same low end under the same slots. **They are the same sound.**
One reading is wrong and the other is right, and nothing measurable in the audio
distinguishes them, because there is nothing there to distinguish - which of the
two levels a player calls "the tempo" is a convention, not an acoustic fact. The
app implements a convention already: the reportable range (`kMinBpm = 50`, so
50 is the floor and this case sits exactly on it) and the octave bounds in
`updateAutoOctave`.

So: **do not try to "fix" the octave in the decoder.** The test that measured
all of this is still in `BeatDecoder::observeMetricalCadence`, switched off by
`kCadenceCorrectionEnabled = false`, with the numbers in the comment above the
constant - it is there to be re-measured in one command, not to be turned on.
Deciding this case needs something from outside the audio: TAP, or a manual
÷2/×2. Those controls were removed from the UI in item 15 as redundant with the
auto path; this measurement says they were not redundant for this class of
material, and the question is reopened there.

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
  the *count* when the move crosses a beat boundary. The tracker passes **true
  both while silent and while sounding**: a sounding snap is capped at 0.20
  beat, and leaving the count behind after a boundary crossing silently moved
  the one by a quarter. The re-anchor is reserved for a displaced grid; an
  ordinary correction stays in the monotonic phase servo.

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

On the acoustic speaker path, `alignBarFromVotes` is still called but skips the
network histogram and may answer only from the gated harmony histogram. Do not
gate the call itself on `barFromHarmony`: that flag is the result of a successful
alignment, so doing so makes the fallback impossible to enter. The focused
end-to-end harmony regression in `VPTests` explicitly selects
`FollowSource::speaker`; cut/seek regressions remain in `VPTests --bar`.

**How long the one takes to arrive is arithmetic.** Votes accumulate and decay
per *beat* (`kVoteDecay = 0.982`), so `voteBeats` saturates at 55.6. Against
`kBeatsToMoveTheBar = 32` that is **47 accepted beats** before the bar can be
rotated while playing - deliberately conservative because that correction is
audible. Coming in and after a cut use a threshold of 7: with decay it is
crossed by the eighth accepted beat, so a clear winner places the one within two
bars at every tempo. The confidence margin is unchanged; ambiguous evidence
still does not rotate the bar merely because the deadline arrived.

`barLocked` (SPOSTA L'1, or a tap that declares the one) stops all automatic
rotation. It does **not** freeze the count against the grid: a `snapPhase` with
`keepBarInStep` still carries it, which is what keeps a locked bar on the beat
of the song it was locked to. **"L'1 è QUI"** (`BeatTracker::declareBarHere`)
names the nearest beat already on the clock as beat zero and locks the bar.
It does not write the phase. A press in the second half of a beat names the
beat about to land; a press in the first half names the beat that has started.
TAP's first tap is still an instant `snapBeat`, because nothing is being
asked to keep a stroke that is already in the air. Unlocking is a tap on the lit control: that
hands the count back without rotating. The old five-tap unlock (all the way
round the bar, then one more) read as a button stuck on. See docs/TODO.md
item 13.

**A two-quarter cut is not a new song.** The epoch watcher needs ~4 s of quiet
before it will restart the decoder, so a mute of two quarters never fired, and
must not: the clock kept time. A seek can move the one by any quarter, and
still uses the cheap coming-in window. A pause is different. The clock kept
counting, so a bar that was already trusted is not renumbered by whichever
quarter wins the next eight beats: that is how a correct one becomes the
three, or the battere and the levare trade places. `maybeDetectBarReentry`
still opens the window, but the rotation is accepted only when it is half a
bar and the winner clears the playing margin (0.20). While the part is
already playing on a trusted one, the network may not move the bar by one
quarter; a half-bar correction still needs the long count and that margin.
The harmony fallback is not held to that rule. The button remains the way
to place the one on the other quarter. A seek
(`notifyTrackSeek`) is unchanged. Verify with `VPTests --bar`.

**Loading another file is a new input, not a cut.** A seek keeps the tempo and
only moves the one; a *different file* is a different source, and the decoder
holds a lock that is right for the song that is gone. `loadInternalTrack` calls
`VirtualPercussionEngine::notifyInputRestart()`, which forces a fresh
`analysisEpoch` with `preserveCombOnEpoch = false` - the same restart the input
change uses - while never restarting the clock. The worker also discards audio
still queued from the previous file (`dropQueued` on that epoch only). Leaving
the queue in place re-certifies the old tempo after the restart, and the
on-grid keep then defends it for as long as the part is sounding — which is
why STOP, by clearing `sounding`, appeared to be what let the new song in.
The part waits out the old grid (`waitForQuantize` / `needsResync`) and adopts
the new tempo once that grid is valid. It does not snap the clock: an in-song
tempo change never takes this path. Drop the epoch with
`notifyTrackSeek`/`notifyBarReentry` and a 60 BPM file loaded under a 120 one
keeps 60 until STOP. Verify with `VPTests --new-input` (measured: 60.0 -> 120.0,
one restart; with the consumption disabled, 60.0 -> 60.2, no restart).

## 4. The clock (`TempoFollower`)

**Confirmed recovery follow-up (2026-09-09).** The correction budget now uses
max(0.25 beat, excess / 0.20), not a half-beat minimum. Confirmation, trust,
octave protection and the 20% rail are unchanged. `probe_recovery` passes 84/84;
its subdivision gate now requires correction within 0.35 beat plus callbacks.
At 256 frames the 0.075-beat displaced passage at 120 returns within 8 ms in
0.624 s (previously 0.747); subdivision recovery totals at 52/100/168 are
1.445/0.768/0.448 s including confirmation. Negative controls stay identical.
The rapid tempo branch also carries the ordinary phase/derivative memory along
before handback. The small-step probe now mirrors BeatTracker's rapid payload
and phase tau, but is still a synthetic activation/clock probe, not audio.
Slow residual and recognition-delay failures remain open; these measurements
do not establish performance on a real mixer or microphone.

A PLL on the audio thread. Two knobs behave differently and both matter:

**Rate glide** (`TempoFollower::advanceSegment`). Acquisition and playing are
different jobs:

```
not locked: tau = 0.045 s if |err| <= 1.2 BPM, else 0.18 s
locked,    |err| <= 2 BPM and no larger move still closing: tau = 1.60 s
locked,    |err| > 2 BPM, the tail of that move until |err| < 0.5 BPM,
           a proved curve, or a confirmed transition: tau = 0.28 s
```

Sounding, a wobble under 2 BPM must not be heard as the clock accelerating
and braking. The old 0.22 s branch adopted that wobble faster than a real
move. The slow path still adopts — it is not a freeze. A move past 2 BPM
keeps the 0.28 s glide until it is within half a BPM, so the tail of a real
step is not reclassified as wobble. A proved curve and a confirmed transition
do too. Tempo is clamped 40..220 BPM and *settles*
(snaps) within 0.02 BPM so `currentTempo()` reads as a round number.

**Phase steering** by `FollowStrength` (`TempoFollower.cpp:524`, inside the
`locked` branch). The grid is never jumped: the rate is *bent* until the error
is closed, so the grid stays monotonic and no stroke is ever played twice or
skipped. It is what a player does - nobody moves their hand, they lean until
they are back with the band.

**Recovery safety update (2026-09-08).** The trust edge only arms recovery;
**Return after a displaced passage (2026-09-08).** A running clock fed a wrong
phase for two seconds exposed a second problem which starting from a snapped
offset did not: the fast controller closed the raw error, but handed control
back to the ordinary filter's old average. At 120 BPM, a 0.125-beat passage
took 3.605 s after the clean reference returned to stay within 8 ms. During a
confirmed recovery the ordinary error and derivative memory now follow the
raw error, with the actual steering still subtracted once. The same case takes
0.752 s. The accepted displacement range is now <0.25 beat (previously <0.15),
with the same two-beat coherence, trust and cooldown guards. Recovery duration
is max(0.5 beat, error outside 7.5 ms / 0.20), bounded below 1.25 beats by that
range: a half beat at the 20% rail cannot close more than 0.1 beat. The rail
itself is unchanged. A 0.20-beat passage at 120 returns stably in 0.880 s,
including confirmation. At 168, the three tested shifts return in 0.608–0.699 s.
`probe_recovery` default: 84 PASS, including buffers 64/256/1024 and 18 negative
controls (noise, outlier, ramp, duplicate-like bursts, a single 160 ms phase
error). The explicit `--slow-passages` extension has 18 FAIL at 52 BPM, also
present before the fix at 256 frames: unconfirmed small residuals can fall
below the recovery floor and remain near the ordinary 0.012-beat deadband
(13.85 ms at 52). Do not call that slow case fixed or confuse these scripted
clock results with recognition delay from live audio. No new live-audio test
was run for this change.

**Subdivision starvation fix (2026-09-08).** Distinct serials closer than
0.55 beat cannot confirm recovery and now leave the first candidate and its
accumulated steering intact. Previously every accepted eighth replaced that
candidate, so its age never reached the confirmation window. The focused
`probe_recovery` fails all 18 subdivision cases before this fix and passes
afterwards (52/100/168 BPM, both signs, 64/256/1024 buffers). At 256 frames,
error below 8 ms is reached 1.733/0.901/0.533 s from the first clean observation,
including confirmation; correction alone takes 0.576/0.299/0.176 s. Stability
continues beyond two beats and pulse intervals stay bounded. Five negative
controls include fresh-serial bursts shorter than half a beat and an isolated
outlier amid eighths: none activates, all match the ordinary clock. These are
scripted clock observations, not a measured neural recognition/re-entry delay.

Persistent high-trust errors can also confirm: two distinct beats beyond both
0.04 beat and 20 ms, agreeing within 0.015 beat after subtracting our steering.
A 2.5-beat cooldown prevents repeated acceleration. The standalone recovery
gate covers both directions, jitter, isolated outlier and a gradual ramp;
the latter controls never arm and match the ordinary clock.
two fresh accepted beat serials must agree after subtracting the correction
already applied. Repeated publications cannot confirm. Poor trust, explicit
reference changes and confirmed tempo transitions cancel the fast command.
`scripts/probe_recovery.cpp` is the standalone iteration gate: six signed cases
at 52/100/168 pass, reaching 8 ms in 0.571/0.299/0.176 s **after confirmation**
and remaining there for two beats. Historical numbers below used perfect phase
and scripted trust, not end-to-end audio. The old `probe_steer --reentry` is
retired because it supplied no independent beat evidence.

**The return of clean evidence is an edge, not another holding frame.** While a
fill, a level change or a different percussion voice makes the fitted beats
poor, `kPoorLeanBeats` deliberately limits how far the clock may follow them.
Before the edge was handled, the clean fit returning merely restored the normal
0.9 s filter, leaving the small residual displacement to be paid slowly. The
clock now remembers a poor interval only after 200 ms (longer than one bad 6 Hz
hypothesis); when trust rises through 0.80 it spends the residue during the next
half beat, through the same monotonic rate bend and with a 20% rail. It targets
7.5 ms so float phase grids land inside the public 8 ms line. `probe_steer
--reentry`, starting 0.075 beat away, measures **0.579 / 0.312 / 0.253 / 0.179
s** at 52 / 96 / 120 / 168 BPM, all no later than half a beat. Pulse spacing is
0.99-1.14x nominal: no duplicate or skipped stroke. The clean 60 s x 8-seed
clock bench is unchanged because no poor-to-clean edge exists there.

| | tau | steerLim | steerCeil | dGain |
|---|---|---|---|---|
| low | 1.60 | 0.018 | 0.10 | 0.3 |
| medium | 0.90 | 0.035 | 0.18 | 0.8 |
| high (default) | 0.70 | 0.050 | 0.25 | 1.2 |

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

**Short-versus-long residual release (2026-09-11).** A brief bend can make the
24-beat straight-line residual look poor for roughly its whole 16-second window
after the drummer is already coherent again. Do not replace the long fit with
the eight-beat fit: that was measured on real material and made phase worse.
`EvidenceTrust::observe` instead accepts the recent residual as a narrow
release signal only when it is good against the song baseline *and* 25–50%
smaller than the long residual. When both fits are poor, drummerless protection
is unchanged. Exact `makedip.py`/`score_dip.py` A/B: stable return within 15 ms
12.1→9.5 s, peak unchanged. `VPAlign` keeps the 44 ms drummerless worst at
39.2 ms and improves its mean 24.1→22.5 ms. On the last 5:07 of Flamingo the
10-second-window median phase movement is 39→26 ms and grid-rate rms jerk
2.15→2.02%, with one analysis restart in both runs. Run `VPTests --evidence`
for the two discriminant cases; `shortFitBpm`, `longFitBpm` and the recent
residual are diagnostic snapshot fields.

**The phase anchor's straddle gate (2026-09-14).** `BeatDecoder::updateTempo`
decides whether the 24-beat window is "lying across a tempo event" and, in the
`fixed` regime only, eases `gridAnchorSec` from the long fit's intercept toward
the short fit's (`longWindowStraddles` / `anchorBlend`). Its whole job is the
dip: the long window stays stale for its full length after a transient has
settled. It must **not** fire on a ramp, where the short fit is drifting away
from the tempo the decoder has already committed to and handing the anchor to
it is what puts the wobble back (measured 146 → 237 ms on `120→132 in 20 s`
until this was fixed). The discriminator is `shortAgreesCommitted`
(`kStraddleAgreeRatio = 0.008`): the short fit must be back *at the committed
tempo*, not merely better than the long fit. A residual-cleanliness gate was
tried first and is a no-op on the clean synthetic ramp - the ramp's fits are
too clean for a residual to separate them. Verify with `VPAlign` (ramp rows
must read 145.9/165.9 ms, not 236.8/193.8) and `score_dip.py` (return must stay
~9.5 s).

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

### Where that epoch is decided: `updateAnalysisEpoch`

**Arrangement continuity experiment (2026-09-09).** A low-share entrance
can occur after the fold already sees the band. Carrying only that fold across
the restart while resetting the network worsened BLUE SKY's first three-second
lock to 56.971610 s. Keeping both the fold and the recurrent model state, while
clearing the decoder's old grid, reached 41.285805 s (baseline 53.46 s), with
73.8% within 2% over the whole recording (baseline 73.6%). The epoch remains
39.7 s. This is not an always-stable lock: the reading rises near 90 during
47-55 s, and playback is already enabled while the clock is converging at 40 s.
Ordinary quiet-to-loud source epochs still clear the fold and model. The worker
receives the epoch and continuity bit in one atomic word so the reason cannot
race its counter. `probe_input_continuity` checks retained fold evidence and a
subsequent hard reset at four tempos; it does not validate the real network.

The make-up gain exists to hold the analysis at the one level BeatNet was
validated at, and downstream of it an empty room and a band look alike - by
design. `updateAnalysisEpoch` is the last place the difference still exists, so
the moment is found there, on the analysis peak *before* the make-up is applied,
and handed to `setInputEpoch`. It calls the epoch on two conditions together:
upwards only, and out of a level that was **properly quiet** (`wasQuiet`), not
merely quieter - a rise on its own cannot tell a band starting from a chorus
arriving, and choruses are frequent.

It also has one exception, and the exception has an exact scope. Our own part
comes back on the microphone and the canceller does not always find it, so when
the part comes in the analysis level can step up on its own account. That is us.
`ownStepSamples` is set from the previous block's output level and, while it is
running, **vetoes** the rise: it clears any step in progress and returns "no
epoch". That is all it may do.

The INPUT trim is similarly not a source change. The leak residual is linear in
that trim, so source audibility, band dynamics, bar re-entry and
`updateAnalysisEpoch` read `postPeak / inputTrim`; only the analysis make-up and
the UI analysis meter read the trimmed peak. Before this separation, raising
INPUT could manufacture the same level step as a new band and reset the decoder.
Focused `VPOps --input-gain` at 120 BPM measured **+0.07 ms** worst operation
delta through the iPad-room path, **0.00 ms** on the direct path and **zero extra
epochs**. `--voice-toggle` measured +1.80 ms worst through the room, 0.00 ms
direct, also with zero extra epochs.

**A share is only evidence above the audible floor.** `updateRhythmShare` decides
both `rhythmArrived` (the step that opens the epoch) and the standing
`rhythmSeen` that `setSourceAudible` needs before START will join a track already
playing. It measures a *ratio* of low-band to full energy on the pre-make-up bus,
and a ratio taken below the audible level is a ratio of room noise - which is
almost all low. Measured through the full engine on a muted input at a 24x
make-up, `lowShare` read 0.34-0.46 and `rhythmSeen` latched on silence - the one
false entry that could let the part play to an empty room. The vote is now
withheld below the same `sourcePeak > (speaker ? 0.004 : 0.040)` that
`setSourceAudible` uses; the filters keep running so the plateau is warm when a
band arrives. Verify with `VPTests --rhythm` (quiet low tone: share 0.650, not
voted; same tone at band level: 0.639, believed).

The veto is an early `return`, so for the blocks it covers `levelLoud`, the
`wasQuiet` test and the *downward* decay of `levelRef` are skipped rather than
run - the block is not seen at all. That is the whole cost of it, and it is
measured: a legitimate band start that lands inside the blame window is called
at +1.56 s with the part audible against +0.557 s with it muted, one second of
lateness, and nothing on a twenty-second pre-roll, which settles at 10.41 s
either way.

It may not redefine where the level is. It used to also do
`levelRef = max(levelRef, levelFast)` - and `levelRef` is only ever pushed *up*
there, decaying afterwards over four seconds - so once the part had been audible
the bar `wasQuiet` has to clear stood at the *band's* level, and the one
legitimate epoch of a session, the band starting, never fired again. Measured at
138 BPM on an input carrying no leak at all, master fader up against master
fader down: the beat landed 2.96 ms apart, the analysis chain differed on 7678
of 9750 blocks although the input was identical, a genuine quiet-to-band step
went entirely unnoticed, and after twenty seconds of empty room the app took
4.35 s longer to settle because the decoder was never told to drop the room's
evidence. With the ratchet removed: **0.02 ms** at 138 and no more than 0.11 ms
at any of 78/100/120/156, **zero** differing blocks of 9750, the step called at
+1.56 s, and the twenty-second pre-roll settling in 10.41 s either way. The veto
itself is unchanged and still measured: our part returning at 0.6 over a steady
band calls zero epochs with the canceller on or off, and released by FISSO over
a quiet room it calls none before the band and one +1.61 s after it - the same
moment, to the millisecond, at which the same room and the same band are found
with the fader down.

**What the veto is worth, in decibels.** One number decides whether our own
part reads as a band starting: how far our return sits above the room *after*
cancellation. `wasQuiet` wants the reference 24.08 dB (`kQuietFraction`) under
the loudest thing in the last minute, so a residual that clears that over the
room floor is indistinguishable from a band arriving - there is nothing left in
the signal to tell them apart, at any threshold. The canceller is what keeps it
under. Measured over the eighteen rows of RED-D3 - our output returning at 0.6,
0.8 and 1.0, over room floors at 0.03 and 6 and 12 dB below it, on both paths
the canceller can find (the iPad speaker's acoustic hop, which it searches for,
and the mixer round trip, which it is told) - the residual sits **6.2 to
21.9 dB below the bar** and **no row calls an epoch before the band**.

Out of that envelope, when the return is one the canceller cannot find - it
arrives 150 ms late, or the app is in mixer mode where the acoustic hop is not
searched for at all - the residual runs 1.8 to 16.3 dB *over* the bar, and then
what decides is whether our return was already in the analysis when the watcher
primed `levelRef` in its first half second (`kLevelPrimeSec`). If it was, the
reference is primed on it and nothing is ever called; if it was not, one epoch
is called during the stretch when the input carries nothing but the room and our
own part. No configuration measured calls one while a band is playing, which is
the property that matters: the grid is never thrown away under a band. The rows
and their numbers are in `VPTests --makeup sweep`.

**The same bar decides whether a band starting over the part is heard**, because
the reference it has to clear is the room *plus* whatever of us the canceller
left there. Four of RED-D3's eighteen rows hear it, 1.6 to 2.0 s in, and they
are the mixer-return rows with the least residual; the rest never call the
epoch. How far into a playing part a band start can still be noticed is
therefore set by the canceller, not by this watcher, and a guard that fired on
the difference would be firing on the canceller's error. Do not read that sweep
as the gate on the removed ratchet: rebuilt with the ratchet restored, all
eighteen rows behave identically, including the same four epochs, because the
part there plays from the first block and the blame has lapsed long before the
band. The bench that does discriminate it is `--makeup c` - with the ratchet
back it reads `restart audible=-1.000s, muted=8.557s` and fails three
assertions.
`.superpowers/sdd/makeup-phase-fix-report.md`; benches under `VPTests --makeup`.

The twelve scalars this and the make-up gain keep - `peakEnv`, `makeupGain`,
`levelFast`, `levelRef`, `levelLoud`, `levelStepSamples`, `levelPrimeSamples`,
`analysisEpoch`, `ownPeakLast`, `ownFast`, `ownRef`, `ownStepSamples` - describe
one input on one device, so `resetAnalysisLevelState()` clears them from both
`reset()` **and** `prepare()`, next to `resetLeakEstimate()` and for the same
reason. `prepare()` used to clear none of them: after a line-level session, a
re-`prepare()` onto a source 40 dB down analysed it at a gain of 1.0 where a new
engine reached 23.7, and reported the old session's epoch count on its first
block.

The three public mirrors of that state - `analysisRestarts`, `analysisGain`,
`analysisPeak` in the snapshot - are cleared at the same boundary, beside
`lastHypValid` and the rest of the diagnostics. They are what the UI and the
tests read, so leaving them behind meant a reader saw the closed session's epoch
count and gain (measured: 1 restart at a gain of 4.99 and a peak of 0.176) until
the first block of the new one arrived.

### The app hears itself: `subtractSpeakerLeak`

The mic hears the shaker the app is playing, so without this the tracker is
partly following us and the loop is closed. What goes into the analysis is
`mic - g * (our own output, delayed)`, in **three** bands split at ~250 Hz and
~1.4 kHz: through the iPad's speaker there is no low end to leak, through a
mixer the return carries the congas too, and one band cannot describe both
paths.

Why three and not two, because the second split is the one a listener reported.
The shaker and the congas sit on opposite sides of the 1.5 kHz line - measured
on eight bars of marcha at eighths, 9.5 dB above it against 13.6 dB below - so
a two-band fit gave the congas one number for everything from DC to 1.5 kHz,
which is the range a small speaker reshapes hardest. Measured on the one-wall
room fixture, the share of our own return removed was **30.4% with the shaker
alone against 6.0% with the congas alone**, and the congas' worst block reached
**1.84** of the input peak: the subtraction putting *more* onto the analysis
than the leak it was removing, on the app's own grid, which is the one signal
guaranteed to confirm whatever the tracker already believes. With a third band
at the speaker's own roll-off: 33.8% and 13.4%, worst block 1.03. The rows are
`leak-voice` in `VPTests --leak`; `roomLeakRun` takes a `Voices` argument
because a mean over both halves of the part cannot answer a report about one of
them.

The solve is Cholesky with a ridge **relative to the trace** (`kLeakRidge`,
1e-2), not an absolute floor. A middle band taken as the difference of two
one-poles carries far less energy than the two either side of it, so it is the
least determined of the three and the one a feed carrying *no* leak can push
around: unridged, the worst audible block on the no-leak speaker bench went to
1.111 against a 1.10 bound. The ridge costs a proportional bias - the
fifty-four exact-copy rows go from 0.0000 to 0.0179, one order of magnitude
inside their 0.10 bound instead of three - and buys 1.092 there plus the
128-frame row falling from 11.40% of mean (over its own 10% bound) to 5.13%.
0.0179 of a digitally exact return is nothing the tracker can hear; our own
subtraction landing on a band that never leaked is.

Three things about it are load-bearing, and two of them were got wrong once:

- **The delay.** Mixer return is the device round trip (`reportedLatencyMs`,
  floor 8 ms). The iPad mic needs a *search* on top of it - the acoustic hop is
  not in the hardware figure - so `updateLeakDelay` correlates on the magnitude
  envelope first (a drum's raw correlation is a needle a few samples wide and a
  coarse step jumps over it) and refines on the waveform. A candidate is only
  accepted above 0.12 correlation: a room full of music always has a largest
  correlation somewhere in the window, and "largest" is not "ours".
  **And 0.12 is the floor for finding a path, not for leaving one.** The search
  re-runs from scratch every fourth block and decides on that block's
  correlation alone, where the gain fit next door accumulates half a second
  first. On a sparse part that asymmetry bites: between two strokes the true
  delay's own score collapses into noise while the part's own periodicity leaves
  coincidental envelope peaks at other lags. Measured on the fractional-delay
  room fixture, it left a correct locked 8417 for 4811 on one quiet block
  scoring 0.1971 against the incumbent's 0.0739 - both meaningless - then
  wandered 1.2 s through 9613, 7429, 5633, 8746, 7421, dropping the accumulators
  at every hop because `delay != leakFitDelay` fires on each. Giving up a held
  delay now needs `kDelaySwitchMargin` (0.15) over the incumbent: **0.2794 ->
  0.0888** mean on that fixture, worst block 0.99 -> 0.13, and the sparsest voice
  gains most - congas 13.4% -> 25.2% of their return removed.
- **The gain is fitted over half a second of causal history, not over the
  block.** The normal-equation terms of the three-band least squares are
  accumulated with `alpha = exp(-numSamples / (0.5 s * sampleRate))` and the
  coefficients solved from the accumulation. The version before it solved the
  block and smoothed the *answer* towards it at 0.12 per callback, which is not
  the same thing: a block in which the *reference* - our own output - is silent
  has no answer to give, took the degenerate branch, and returned a hard zero.
  An absence of evidence, which the smoother then mixed in as though it were a
  measurement of zero gain. (The input can be as loud as you like in such a
  block; what makes it degenerate is that there is nothing of ours in it.) So the
  estimate decayed between strokes and the *sparser* the part, the less of it
  was cancelled. Measured across nine styles x three subdivisions x mixer and
  speaker (54 rows, `VPTests --leak`), the share of our own part still in the
  analysis: **0.07-0.18 at sixteenths, 0.29-0.46 at eighths - the shipped
  default - 0.62-0.74 at quarters; all 54 rows then under 0.0001, and under
  0.018 since the three-band fit's ridge.** A per-callback
  constant is also a different length of time on every buffer size, the same trap
  the phase constants above were fixed for: 0.4792 at 256 frames against 0.2379
  at 4096 before, 0.0000 on 256 / 1024 / 4096 after.
- **The accumulators are dropped when the accepted delay moves**, and only then.
  Every cross-product in them was measured against the reference at one
  alignment; at a new delay they describe something that no longer exists. The
  gains themselves are kept, because the old estimate is still the best guess
  until new evidence replaces it - and for the same reason a window with no
  evidence in it at all leaves them alone rather than writing a zero over them.
  `leakLp`, the band splitter's own filter state, is *not* cleared with them:
  measured, doing so puts a transient into the split on the first block of the
  new alignment and makes the no-leak damage worse, 1.2049 to 1.3004 on the worst
  block and 0.36% to 0.40% rms.
- **A new device session starts over.** `resetLeakEstimate()` drops the five
  terms, the delay key, both gains, the delay and both filter states, and it is
  called from `prepare()` as well as `reset()` - `prepare()` zeroes the reference
  ring, so evidence measured against the old ring describes a signal that is no
  longer there, and if the new session reports the same latency nothing else
  would ever notice. Measured on a restart from a 0.6 return into a 0.15 one: the
  analysis differed from a new engine's by 0.156 of peak, thirty blocks in.
  `VPTests --leak` now compares the two traces sample for sample.

The estimate stays **signed** until the moment it is used. The fit between two
unrelated signals is not zero, it is zero plus a few per cent of noise, and
clamping at zero before the noise has cancelled keeps only the positive half and
averages it into a standing positive gain - the app then subtracts a few per
cent of its own part from a feed that never carried any, which costs the tracker
real onsets. Measured on a no-leak feed with the part playing the band's own
rhythm, at 128, 256 and 1024 frames: the analysis peak moved by 33% rms (mixer)
and 23% (speaker) before, with single blocks raised 8.9x and 39.8x. After: 0.00
to 0.03% on the mixer path and 0.30 to 0.59% through the speaker.

Read the residue on the speaker path carefully, because the obvious metric lies
about it. Ungated, the worst single block there is **1.2049x** at 128 frames -
and that block's own peak is 0.0074 against a run mean of 0.045 and loud blocks
of 0.25, with an absolute change of 0.0015. It is a decay tail between the
band's onsets, where the denominator is thousandths and any change at all reads
as a large ratio. So `VPTests --leak` asserts the two figures that can carry a
meaning instead: the largest change in any block as a share of the run's own mean
block peak (**4.21%** worst, bound 10%, about 0.9 dB - an *absolute* bound on the
perturbation of every counted block), and the worst ratio among blocks carrying at
least half the mean level (**1.0374** worst, bound 1.10). Between them - with the
rms figure - the loud and measured blocks are held tightly. Be clear about what is
left: a quiet tail is bounded in absolute terms only, not relative to its own
level, so a change that is small against the run and large against that tail is
inside all three bounds. The raw figure is still printed. The cause is not
conditioning and not the window: our part is playing the band's rhythm, so the
least-squares fit converges
to a small non-zero gain because there genuinely is a correlation to find. A
relative determinant guard changes none of these numbers (measured, to four
places) and a longer window would only average the same correlation.

On the same no-leak input the 138 BPM full chain moved 1.8 ms run to run before
and 0.00 ms after - see `.superpowers/sdd/sparse-leak-fix-report.md`.

**What that near-zero is and is not.** That bench returns an exact scaled copy
of our own output at a whole number of samples, so a converged fit
removes essentially all of it; it is a statement about the estimator. A room is
not that. Same rig, through the repository's own reflection model
(`vp::probe::speakerRoomMic`), one cause at a time:

The one-cause-at-a-time column below was taken on the two-band canceller and is
kept because the *shape* of it is the finding - where the residual jumps is
where the model's limit is. The `measured today` column is what `VPTests --leak`
prints on the current three-band fit; only the three fixtures the bench still
runs have one.

| return path | residual (two-band, historical) | measured today |
|---|---|---|
| exact copy, integer delay | 0.0000 | 0.0179 (the ridge - see below) |
| delay 0.373 of a sample off the grid | 0.0957 | 0.0888 (0.9800 off) |
| a -56 dB noise floor | 0.0045 | |
| 260 Hz/9 kHz band limiting alone | 0.8050 | |
| one wall at 7.3 ms, a second at 14.6 ms | 0.8484 | |
| all of it, one wall | 0.8755 (0.9903 off) | 0.8339 (0.9920 off) |
| all of it, the full eight-tap tail | 0.9150 (0.9902 off) | 0.9323 (0.9936 off) |

As a share of the return removed, against each fixture's own cancellation-off
control, on the current fit: **91%** when the direct component reaches the mic
spectrally unmodified at a fractional delay, **16%** on the complete one-wall
fixture and **6%** with the full eight-tap tail, against historical two-band
figures of 90.2%, 11.5% and 7.6%.

A fractional delay it can still cancel. A return whose spectrum has been
*reshaped inside each band* it largely cannot - a handful of gains cannot follow
a 260 Hz high pass through a conga, which is why the band at that roll-off was
added and why three is still not many - and neither can it reach a reflection
that is not in the reference at any single delay. The 0.83-0.93 residual is
where those two limits of a piecewise-constant, single-delay model put the floor
for this fixture: a known
limitation of the model's shape, not a regression, and **not** a claim that the
canceller takes the direct arrival out of a room. Quote the near-zero about the
estimator on an exact copy, never about a room.

The part being audible used to move the phase about 3 ms further out on a feed
carrying no leak at all. That was never the canceller: it was
`updateAnalysisEpoch` ratcheting its level reference on our own output, and it
is fixed - see "Where that epoch is decided" above. Do not confuse the two
paths, and if a phase difference between part-on and part-off appears again,
`VPTests --makeup` says which of them it is: RED-B asserts that the input and
the leak residual match block for block *before* it asserts anything about the
analysis gain.

**Do not compare the phase of two independent network runs and call the
difference coupling.** Two runs of the real model over the same audio do not
commit the same tempo to the last decimal, and a tempo a fraction out walks the
phase across the measurement window. Measured over fifteen runs a variant at
100 BPM: fourteen committed 99.987 BPM and read −1.28 ms with the phase error
falling through the window (+7.36 ms down to +3.97 ms, second by second), and
one committed 100.004 and read +3.19 ms with it climbing (+9.45 up to
+10.15 ms). That run's analysis chain was identical to its partner's on every
block and its make-up gain identical to four decimals; what differed was that
the worker published 2592 hypotheses instead of 2590. Which of the two a run
lands on is the host's scheduler, so the fader-up/fader-down gate is asserted
two ways: `--makeup a` runs a scripted model on the analysis frame grid *and*
holds every block boundary until the worker has gone quiet, and the real network
is asserted on absolute error against the pulse plus a delta over
**tempo-matched** pairs only.

**Fixing the model is only half of determinism.** A scripted model fixes what
the worker publishes; it does not fix which block the publication lands on, and
one block of difference is a difference in the clock. Loading the host during a
verification campaign produced exactly that: one of four scripted runs at
156 BPM came out 5.45 ms from the other three, with the analysis chain identical
on all 4500 blocks and the same 1194 publications. The bench therefore waits, at
each block, for `analysisBacklog()` to reach zero *and* for
`hypothesisPublicationSequence()` to stop moving (`MakeupOpts::syncWorker`).
With that wait it is bit-exact - every delta and every spread 0.0000 ms across
the five tempos, phase error identical to three decimals - and the 0.20 ms bound
exists only so that a host slow enough to time the wait out reports a number.
If you write a timing gate over this engine, synchronise the handoff or expect
to be measuring the scheduler.

At 156 BPM even the tempo-matched network-to-network delta is not asserted: two
runs of one configuration were worth 0.83 ms there with the chain identical on
all 9750 blocks and both runs locked to the same tempo, because the beats are
2.6 times closer together than the analysis hop and the network's settling is
what is left. Coupling at that tempo is the scripted gate, which is bit-exact
there, and the block-exact chain comparison. The network bench is left
asynchronous on purpose: it is the one that has to answer whether the heard beat
sits inside 8 ms with the worker running as it does on stage, and synchronising
it would make that number about a lock-step engine nobody ships. Absolute heard phase is still bounded at 8 ms: extending the
click calibration from 78/100/138 to include 120/156 exposed the old 20 ms trim
as an out-of-range calibration (−8.98 ms at 156 with the part muted), not a
derived hop. The 17 ms minimax trim centres the measured five-tempo envelope;
see `.superpowers/sdd/phase-156-root-cause.md`. If you need a tighter coupling
claim under the real model, make the model deterministic - do not take more
runs.

## 6. State machine

**Harmonic source integration (2026-09-08).** Direct mode can now acquire and
enter from a fresh harmonic phase, not just set a target BPM while the state
machine waits forever for neural beats. `HarmonicTempo` is prepared at the
device rate, reset with the session/input epoch, and expires after two bars
(maximum 12 s). Phase is the circular mean of chord dates at the selected bar
period; readiness requires eight changes and coherence >=0.80. Its predicted
quarters never count as independent observations for fast recovery or neural
downbeat votes. A valid neural tempo takes priority; the acoustic fallback stays
disabled. `VPTests --harmonic-entry` passes at 44.1/48k, with six rendered
attacks within 2.59/0.23 ms on **scripted chord dates**. At one chord per bar,
entry costs 18.79 s: this does not prove a two-bar acquisition without drums.

**Actual detector audio gate (2026-09-08, incomplete).** `VPTests
--harmonic-audio` now renders music-only SongStems (100 BPM, 48k, 256 frames,
36 s, seed 42) through HarmonicChange -> BeatTracker -> PercussionEngine.
No scripted chord dates, neural worker, bus conditioning or room. Production
now suppresses detector publications during the same four-second reference
warm-up that VPSing historically performed only in its probe; startup events no
longer seed the tempo. Source selection no longer duplicates `phaseValid()`
with the slow tonal-share meter: eight fresh changes and coherence >=0.80 are
the source guard, while tonal share still guards bar rotation and neural tempo
still wins immediately. Without pad this yields 99.917 BPM, entry at 23.371 s,
and eight rendered attacks within 18.17 ms (clock pulses within 22.27 ms). It
still FAILS the 4.8 s target. With pad, 34 changes never produce valid phase
(final coherence 0.355), so it correctly emits nothing but still FAILS the
recognisable-pulse product case. Negative controls pass: drums alone produce
seven changes but no phase/BPM; a loud held chord is tonal (share 0.814) but
produces zero changes and no BPM. Absent phase/clock/attack measurements print
-1, not zero error. Keep this gate separate from the passing scripted test.
Two-bar acquisition needs a non-percussive pulse source; chord changes alone
cannot provide eight observations in two bars when harmony moves once per bar.

**Do not implement that source as another onset picker.** The next focused run
fed the same music-only stems through the real BeatNet worker, with every hop
drained: 100 BPM without pad already passes (entry 2.347 s, reading 101.940),
but 52 reads 103.927 with fit 0.003/coverage 0.833 and 168 reads 91.585 with
fit 0.093/coverage 0.909. Those are coherent octave interpretations, not low
confidence. With pad, 52/100/168 read 147.282/60.685/83.565 and all fail.
A trial with only two harmonic changes was reverted: pad transiently read 200
and drums alone reached coherence 1.0. The safe eight-change harmonic reference
eventually reads 168 as 169.492 at 16.427 s, but never validates 52 in the
extended fixture. It cannot meet two bars or safely change level under a part
already playing. The next evidence must be the user's real recording and its
activation dump; if it agrees, the missing component is a model trained for
tonal/no-drum beat and downbeat, not a threshold adjustment. Preserve TAP and
manual octave controls for genuinely ambiguous 52/104 and 84/168 material.

`VPTests --state-timing` checks the four-second low-confidence hold at buffers
64/256/1024: 4.001333/4.005333/4.010667 s. The counter adds actual samples;
the confidence filter also uses elapsed time (old 256/48k response preserved).

```
LISTENING -> LOCKING -> FOLLOWING
FOLLOWING -> LOW_CONFIDENCE -> RECOVERING -> FOLLOWING
```

Analysis runs from launch, always. START arms; entry is quantized (MIXER on the
next reliable downbeat, IPAD on the next reliable beat, because a tablet speaker
does not carry enough bass for trustworthy downbeat votes). STOP mutes and keeps
following.

That rule describes an open foreground audio session. On iOS the target carries
the background-audio entitlement so an armed performance can continue with the
screen locked or another app in front. If the app is backgrounded while STOPped
and its internal track is not playing, `MainComponent::handleAppSuspended`
closes the device and explicitly stops `NeuralBeatTracker`; closing the device
alone is insufficient because `releaseResources()` does not own the worker
(the real-time worker bench measures 110 loop passes per second while active).
Returning to the foreground reopens the device, whose `prepareToPlay()` starts a
fresh analysis session. While an armed performance continues in the background,
the 15 Hz timer remains only for the device watchdog and skips UI repainting.

**First-bar entry (measured 2026-09-22).** Once START is armed and the input is
known to be live (`sawInputStart` or `heardMusic`), a valid periodic decoder grid
goes directly from LISTENING to FOLLOWING. Do not add a second time-based
LOCKING proof there: `BeatDecoder` has already required three causal peaks on a
line feed or four through a room, and the extra 160 ms consumes the margin before
the next-quarter entry. Background listening still uses LOCKING, including its
empty-room rejection window. `VPTests --state-timing` measures the whole causal
deadline from the first beat through the next possible quarter:

| BPM | line valid / entry / bar | room valid / entry / bar |
|---:|---:|---:|
| 76 | 1.88 / 2.64 / 3.43 s | 2.66 / 3.43 / 3.43 s |
| 100 | 1.44 / 2.01 / 2.61 s | 2.04 / 2.61 / 2.61 s |
| 140 | 1.02 / 1.44 / 1.86 s | 1.46 / 1.86 / 1.86 s |
| 168 | 0.86 / 1.20 / 1.55 s | 1.22 / 1.55 / 1.55 s |

All eight paths enter no later than the first 4/4 boundary; the unarmed
background control remains in LOCKING. This changes only the state gate. It does
not reduce the decoder evidence, snap the clock, or restart it.

## 7. What we refuse to do

- Restart the loop or clock on a BPM change.
- Snap BPM from a single onset or a volume spike.
- Tune a residual, vote count, BPM band, timeout or exception from one
  recording. Flamingo, Sally, INFINITO, dip and rall4 are witnesses of
  the product, not knobs. A candidate that only helps one title is
  rejected; the known-phase matrix, `VPAlign --ramps` and the tempo
  gates decide whether the rule is global.
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
| `probe_matrix` | `scripts/probe_matrix.cpp` | twelve kinds of material x five tempos: time to lock, and time spent off the tempo. The bank to A/B a decoder change against - **not** one song. No CMake target; build line in its header. `--ratio` prints reported/true, so an octave error is told apart from a wobble |
| `VPTests` | `Tests/` | the TAP suite; `StubBeatModel` when no ONNX assets |
| `VPTests --bar` | `Tests/TestAiBeat.cpp` | two-quarter cut / seek re-entry of the one (item 2) |
| `VPTests --phase-lock` | `Tests/TestAiBeat.cpp` | click-track heard phase at 78/100/120/138/156 BPM vs 8 ms after subtracting `attackLeadMs` |
| `VPTests --state-timing` | `Tests/TestAiBeat.cpp` | armed live input exposes an already-valid grid immediately and can enter by the first 4/4 boundary; background listening retains LOCKING; state holds are invariant across buffer sizes |
| `VPTests --tempo-step` | `Tests/TestAiBeat.cpp` | wide non-octave line steps confirm once in three intervals and hold against the stale fold |
| `VPTests --swing` | `Tests/TestMain.cpp` | the swing warp's geometry alone: straight where written, swung on 0/⅓/⅔/⅚, never early (item 7) |
| `VPTests --leak` | `Tests/TestMain.cpp` | the canceller alone in twenty seconds: 54 style x subdivision x path rows, the no-leak feed at three buffer sizes, the output A/B, the restart, three rooms |
| `VPTests --makeup` | `Tests/TestAiBeat.cpp` | the other half of the same subject: does our own output move our own analysis. Six benches - `a` phase and the analysis chain with the fader up against down at five tempos, `b` the chain block for block, `c` a real band start with the part playing, `d` our own return not being called one plus the eighteen-row veto margin, `e`/`f` what `prepare()` clears, inside and outside. Name one to run one; naming something that is not a bench fails non-zero rather than passing nothing. `dist`, `sweep` and `epoch` are probes, assert nothing and run only when named |
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

For a file that starts after silence, the epoch kind is part of the input.
`VPLive` uses `setInputEpoch(1)` with `preserveComb=false`: the worker resets
BeatNet's recurrent state and the decoder's comb evidence, while retaining
the resampler and feature history. `VPActivations --silence-prelude` now emits
that cold restart by default, and `VPReplay` reads the restart marker.
`--preserve-model-on-restart` selects an arrangement entrance instead. A dump
made with the opposite epoch kind can give a different metrical level despite
the same WAV; compare the `ACT` frames from `VPLive --trace-activations` before
attributing that difference to decoder or clock code.
The tracker must retain its AUTO octave on a preserved arrangement epoch too:
resetting that shift while preserving the model and comb changes the sounding
part's pulse density without a new musical source. A cold source epoch still
clears the shift.

For a loaded recording, use `VPTrack --player`, not only `VPLive`: the latter
feeds the tracker directly and omits the engine's make-up gain and input
epochs. On a 189 s real recording the full-engine probe had one analysis epoch
at 8.7 s, reached 123 BPM after about 10 s, and `barTrusted` was false for 72
of 179 one-second samples after 10 s despite continuous percussion playback. The
clap reads that flag and went silent mid-song and near the end. Bar trust is
now latched after a reliable one is established, while a new input epoch,
STOP, or explicit bar re-entry clears it; weak downbeat votes alone do not
revoke an otherwise unchanged count. With the latch, all 154/154 one-second
samples after the first trusted bar stayed trusted, versus 107/154 before;
the initial 25 samples still wait for reliable bar evidence. The BPM trace
was byte-for-byte unchanged. The `--bar`
cut/seek/lock smoke gate still passes.
`VPTests --octave focused` currently reports 4 pass / 6 fail on the slow-kit
audio phase checks. The exact same 4/6 result and phase numbers reproduce on
an isolated `HEAD` build without these changes, so this is a pre-existing
phase limitation, not a bar-trust regression; do not treat it as a green gate.

**Bridge/fill diagnosis (2026-09-23; no engine change retained).** A listener
reports a false rise from about 123 to 127 BPM on a late drum figure and a
slow return. The full loaded-file path (`VPTrack --player`, original recording
converted to WAV) reproduces it at approximately t=141–160 s, without a new
analysis epoch. Activation replay isolates the first FISSO→VIVO release at
t=142.72 s (2 s prelude in that dump): `curveOnLatticeRelease=1`, two fast
votes, 8-beat residual 0.037, 24-beat residual 0.048, long-fit trend 0.007
against spread 0.007. `moving=0`; the comb still names 123. If the curve door
is withheld, a third vote plus weak `windowAgrees` releases one beat later.
During the figure `EvidenceTrust` is 0.30 but product direct-live mode gives
the phase servo full trust anyway; the clock briefly runs faster than the
published BPM (about 137 vs 127 in the full trace). On return, trim reaches
about −1.3 BPM, phase error reverses sign, and it takes several seconds for
both to settle. The BPM display alone misses the audible excursion.

Do not repair this with a threshold from that song. Requiring an 8-beat residual
under 0.030 on the curve door delayed the false release by one beat, then the
three-vote path still released; the current known-grid quick bank and
`VPAlign --ramps` were unchanged, so there was no reason to retain it.
Also gating the three-vote and long-window paths on that residual held the
example but worsened the known-grid quick bank: fisso
22.256/76.932→22.830/77.455 ms, continuo 36.551/91.156→36.926/92.104,
gradino 33.962/162.108→34.866/173.629;
`VPAlign --ramps` MIXER failed 4 rows. A 0.55 comb-salience alternative
passed `VPAlign --ramps` but still worsened continuo to 36.704/91.519 and
gradino to 34.490/165.654. Removing the product direct-live phase-trust
override made the optional product-direct quick bank worse on continuo
34.211/89.944→36.349/96.762 and gradino 33.502/160.108→33.924/163.707.
All three candidates were reverted. Requiring `moving` on every dirty
three-vote release and on the dirty curve door, with no lattice test, was
also reverted: offset-0 fisso 22.256/76.932→22.733/77.239, continuo
35.482/87.221→36.722/91.718, gradino 33.962/162.108→34.888/167.619.
The delayed leaves already had the 4-beat off the 8-beat (218386, 265900,
289657, 329252, continuo 129495). Restricting the hold to the fill shape
(4-beat still within 1.5% of the 8-beat, 4-beat residual under 0.045,
8-beat residual at least 0.030, `moving` false) changed only fisso seed
88118, a flat 165. That leave at t=18.18 ran 164.7→168.6 while the comb
stayed at 165.7; holding it raised that seed's mean phase by 2.4 ms and
the later leave published 170.6. Continuo and gradino traces were
identical, and the candidate was reverted. The fill's geometry is not
unique: the same 4-on-8 dirty unmoved lattice is a flat-track correction
the phase score wants to take. Aiming the live target at the long fit
whenever `moving` is false and the 8-beat residual is at least 0.030
raised continuo to 40.86/106.09 and gradino to 39.29/179.74. Restricting
that to a clean long line (`lastFitResidual` under 0.015) left fisso
identical but continuo 36.551/91.156→36.643/90.773 and gradino
33.962/162.108→33.995/162.797. Both reverted. On the current loaded-file
path the same figure is already in VIVO (half-tempo lattice about 61):
published tempo 61.4→64.4 and the clock to 67.5 while the long fit stays
near 61.6. A per-beat log of that climb shows `moving` already true and
the IOI-indexed 4-beat on the short fit, not on the long one
(t=148.8: short 62.9, long 61.6, i4 63.0, r4 0.026, moving 1;
t=151.5: short 63.4, i4 63.8, moving 1). The two beats where `moving`
falls false (t=152.5–153.5) still have the 4-beat on the fast side
(i4 64.7/64.3). A hold that waits for `moving` false, or for the 4-beat
to stay on the long fit, does not see this climb. It is the same
geometry as a ramp the control bank wants followed. Aiming the live
commit at a long fit that has stayed inside 0.8% for 4 s while the
short fit has left it by 1.5–8% was counted on
`/tmp/motion_curve_live4.csv` and not shipped: with the short residual
at least 0.030, the long fit is closer to the truth on 22 frames and
the short fit on 64. Continuo is 1 against 55 (the short fit is the
ramp). An 8 s window is 12 against 31, and continuo is still 1 against
23. The same count with the gap tightened to 2–6% makes continuo 0
against 36. A fill whose long line sits still is not separable from
the start of a ramp the bank has to follow. Aiming at a high-salience
comb that has stayed inside 1.2% for 3 s while the short fit is more
than 3% away is the same trap from the other side: on
`/tmp/motion_curve_live4.csv` it would help fisso and continuo, and
it would pull finished steps back (257981 t=43–45, short 116 and
truth 117, comb still 131). The activation dump of the same recording
(`act_everytime_full.txt`) has low-band energy at beat peaks of about
0.81 through 139–154 s, the same as 60–80 s and 120–135 s, and no
peak in that window is under `kLowBandMute` (0.05). The body-hold
never sees this climb: the kick is still in the beats. Requiring the
fast-drift vote's interval to sit within 2% of a 4-beat whose residual
is under 0.03 held the flat 165 glitch (88118 t=83.20, interval +17%,
8-beat only +1.5%, 4-beat residual 0.068) and was reverted: the 16-case
bank moved fisso 22.256/76.932→22.583/76.248, continuo
36.551/91.156→36.713/91.724, gradino 33.537/157.311→34.226/163.254.
A real exit such as 218386 already has that clean 4-beat, but 313414
releases on three dirty 4-beats and the family follows them. The safe next step
is an independent
rhythmic cue/beat-grid and a diverse fill/bridge control bank, not weaker
release or slower global phase following. `VPTrack --trace` now exposes clock,
target, trim, phase error, trust and recovery count; the known-grid matrix has
an optional `--product-direct` A/B lane. Its default hashes remain the control.

**EVERYTIME early abrupt jump (2026-09-24).** The listener marked the playhead
around 42% of the 189.2 s waveform, before the pause. The old 44.1 kHz replay
was steady near 123 BPM there, but resampling the same WAV to 48 kHz reproduced
the iPad-like event: at t=71.52 s the direct-feed interval detector confirmed
123.02 -> 130.26 BPM in one publication, and the heard clock reached 136.72
BPM at 71.9 s. The activation comb stayed at 123.20 with salience 1.00.
The three candidate strengths were 0.827/0.801/0.498 against a recent median
of 0.948; the third peak was a weak fill/tom-like onset. `lineFeed` exempted
this path from the existing 0.70 x median beat-strength gate, despite the
same rule already protecting room input. Applying that gate to both paths
eliminates the false transition: on the 48 kHz full-engine replay, t=70–90 s
published BPM spans 123.01–123.25 and the clock peaks at 123.66, versus
123.12–131.62 and 136.72 before. No song BPM or timestamp enters production.
The 16-case quick known-grid matrix is byte-identical with and without the
change in both the default and `--product-direct` lanes; `VPAlign --steps` and
`--ramps` pass, and `VPTests --tempo-step` is 14/0. The TAP suite also exercises
a quiet pair on direct feed as well as room. A partial full TAP run reached 13
failures whose assertion texts all occur in the earlier
`/tmp/vp-everytime/vptests.log`; it was stopped during the long ONNX phase-lock
section after the focused gates passed, so it is not a full-suite pass. This
guards an abrupt weak-peak
confirmation; the later EVERYTIME live-fit/phase excursion at ~141–160 s is a
separate unresolved failure described above.

**EVERYTIME loaded-file intro, later listener report (2026-09-24; read-only
comparison).** A screenshot from the listener's updated iPad build, using
CARICA/PLAY, shows 165.4 BPM near the start of the guitar and hi-hat intro,
with `CERCO` and `livello provvisorio`. In this pre-lock state the main BPM
comes directly from the decoder's neural hypothesis, not the follower glide.
The listener says the estimate recovers slowly after the kit enters. The
existing full-engine desktop replays of the same MP3 converted
to WAV do **not** reproduce that onset: both 44.1 and 48 kHz publish about 119–120
BPM at 1.6 s. The 48 kHz replay then drifts to about 114–117 while the comb
already reads about 123–124 at 5–8 s; a rhythm entrance resets analysis near
8.9 s and the published BPM reaches about 122.3 by 10.7 s. Thus the trace
supports slow recovery of a provisional intro estimate, but does not reproduce
the device's 165.4-BPM onset. Do not tune a 165 threshold against this replay.
CARICA/PLAY's message-thread load had started transport before queuing the
new-input epoch; the order is now reversed so playback cannot start before
the restart request. Also, `buildTrackWaveform` used to read the same
`AudioFormatReader` as `AudioTransportSource` *after* `setSource` had started
JUCE's read-ahead thread; building the waveform before `setSource` now keeps
those reads sequential. The transport's own `prepareToPlay` starts the
buffering client during `setSource`, so transport not yet playing did not make
the old order safe. These ordering fixes are not a measured solution to the
device-only 165.4 reading. No probe or test was run in this turn at the
listener's request. A future device trace needs source, gain, octave mode,
neural/comb BPM and analysis epoch from the first seconds.

**Late fill and bar-count guard (2026-09-24; implementation only, not
verified at the listener's request).** The existing full-engine 44.1 kHz
trace at t=141–153 s shows the long fit initially near 123 while the live
target rises through 125–130, fit trust is frequently 0.30, and the
clock-minus-analysis phase error reaches -0.201 beat. The clock reaches
about 130 BPM at t=152.4 while the published number is 128.08. This is a
phase-loop contribution on top of a false-looking live fit, not proof that
the recording changed tempo. `TempoFollower` now withholds the direct-live
phase-trust override only when the raw displacement exceeds 0.06 beat,
fit trust is below 0.50, and neither proved motion nor a confirmed
transition/recovery owns the correction. The existing poor-evidence lean
limit of 0.020 beat then applies even when the raw displacement exceeds
`kLeanIsElsewhere`; small direct-feed corrections keep their former path.
This is narrower than the previously rejected blanket removal of direct-live
phase trust. It prevents weak fill evidence from spending a large phase debt
as audible acceleration; it does not certify the decoder's BPM estimate, so
the late false live-fit climb may still require independent rhythmic evidence.
Do not claim a measured improvement until a replay is permitted.

A second route to an audible 1→2 slip existed independently of BPM: after a
trusted bar survived a pause, `notifyBarReentry` cleared the trust latch,
allowing a quarter rotation after its short half-bar-only window expired.
While sounding, `tryAlignFrom` also exempted harmony from the trusted-bar
quarter guard. Preserve the latch across a pause (clear it on seek), and
apply the quarter guard to both automatic sources. A successful automatic
placement now establishes the count even when harmony was the source; otherwise
the next chord change could move its newly placed one again. An explicit bar-button
press can still place any quarter; a supported half-bar correction remains
automatic. These edits have not been tested or listened to in this turn.

`scripts/probe_tempo.cpp` has no CMake target of its own; build any probe source
ad hoc with `VP_STYLE_SRC=scripts/probe_tempo.cpp VP_PROBE_DIR=scripts` and the
`VPStyle` target (`CMakeLists.txt:690`).

**Independent band-cue check (2026-09-23; no engine change).** The file-feed
activation dump already carries low-/high-band magnitudes at 50 fps. A causal
6 s positive-flux Fourier check over 112–140 BPM does not give an independent,
unambiguous pulse through the bridge: the low band names 122.5 BPM at 140 s,
129 at 144 s, 126 at 148 s, and 134.5 at 152 s; its normalized peak support
is only 0.07–0.09 through 140–148 s. The high band names 117, 129.5, and
117 BPM at 140, 144, and 148 s. These bands can explain *loss of evidence* but
cannot safely supply an alternate beat grid or veto a real ramp. A smooth
direct-live phase-steer rail scaled by trust (0.65 at the trust floor, 1.0 at
full trust) was also rejected: the product-direct quick known-phase bank moved
continuo 34.211/89.944 → 35.004/93.229 ms and gradino
33.502/160.108 → 33.933/164.659. The engine was restored. Do not use this
recording to set a spectral-support threshold; obtain diverse fixed/moving
audio with externally known grids before admitting a new band-cue authority.

**Three-file direct-feed control (2026-09-23; observation only).** `VPTrack
--player --trace --step 1` on the complete BLUE SKY, FEEL and SPLENDIDA
GIORNATA files supplied by the listener was deterministic on a repeat of
FEEL's first 60 s. BLUE SKY's ambiguous intro published about 52–63 BPM until
the arrangement epoch at ~40 s, then about 87 BPM over the main body (the
documented approximate tempo). FEEL published 130–134 BPM from ~9–40 s and
did not reach the documented ~104 BPM until ~46–48 s; throughout 14–40 s the
activation comb was already about 102–103 BPM. A causal 6 s Fourier check of
the raw BeatNet beat activation and high-band positive flux supports ~103–104
BPM at 20–40 s (e.g. at 20 s: normalized 104-BPM support 0.56 and 0.35,
respectively, versus high-band support 0.02 at 130). Thus the initial wrong
non-octave accepted lattice is distinct from EVERYTIME's late fill, where the
band cues become ambiguous. SPLENDIDA stays near 108 BPM across the file;
previous human taps named ~107.8 BPM over 10.8–75.4 s, but the tap timestamps
are not presently available for a fresh phase score. The high-band cue is not
globally authoritative by itself: at BLUE SKY 20 s its best six-second rate
is ~128 BPM, and at SPLENDIDA 30 s it is ~86 BPM, despite the respective
~87/~108 music tempi. Do not turn this into a song-name or band threshold.
If testing a new initial-lattice correction, require independent agreement,
check the other files and a true tempo-change grid, and keep the engine
unchanged until the global gates pass.

`VPReplay --anchor --line --sound-at 400 --trace` on FEEL's activation dump is
diagnostic only, not the product path: it initially fits ~150–154 BPM rather
than the full engine's ~130–134, while the comb reads ~102–103 with salience
often 1.00. Its non-octave mismatch counter rises to 5 and repeatedly returns
to 0 before a snap, with a first correction at ~47 s. The sounding post-hole
guard at `BeatDecoder.cpp` near `refusePostHoleComb` is a plausible cause of
that repeated refusal; the trace does not expose its stamp, so do not call it
proved for the product. Removing or merely time-limiting the guard is not a
safe fix: the documented 100→150 leftover-lattice fixture requires a persistent
veto. A candidate must distinguish a false initial lattice from a genuine
post-hole leftover and pass both fixtures before changing production.

**FEEL listener report after the three-file probe (2026-09-23).** The app showed
~50 BPM and the listener confirmed that the `÷2` button was lit. That is a
*manual octave* carried in preferences (`tempoOctave=-1`,
`tempoOctaveAuto=false`), not evidence that the direct-feed decoder independently
chose 50: its probe had reached ~103–104. `MainComponent::loadInternalTrack`
now returns to AUTO when a *different* file is chosen, preserving manual choice
when the same file is reloaded. The UI previously appended `(auto)` even to a
manual octave; that label now follows the actual flag. Do not "fix" the decoder
to force this file upward on account of the listener's 50-BPM reading. The
separate early wrong-grid (~130–134) issue remains open. Tapping an active
`÷2` during playback turns AUTO on but retains that level until safe to change
(the tracker avoids a mid-part octave snap); `×2` once requests the next level.

Probes wait on `BeatTracker::analysisBacklog()` to make a run repeatable -
otherwise the host scheduler decides how far behind the worker is and the same
build measures differently run to run.

**Kept (2026-09-24), faster rientro of a light direct-live offset.** A stable direct feed at high follow closed a constant 20 ms offset at 120 BPM in 0.70 s, and the first tenth of a second of that was still under 0.3 BPM of lean. The 0.30 s phase average and the 2%/beat slew toward the 7.5% rail were both applied to a command that only wants about 4%. While the raw error is inside 0.05 beat and the part is not in a rest, the average is now 0.15 s. The slew inside ±4% may arrive in half a beat; past that edge the old 2%/beat slope toward the rail is unchanged, and a rest still zeroes the lean in the same buffer. Standalone clock, high follow, 256 samples: 20 ms at 120 is inside 8 ms in 0.44 s with a 2.60 BPM peak lean (was 0.70 s / 2.28 BPM); 30 ms is 0.76 s / 3.83 BPM (was 1.01 s / 3.64); a 50 ms debt, which starts outside the light band, is 1.02 s / 6.24 BPM (was 1.42 s / 5.56). The same 20 ms on the hold path stays at 1.59 s. A ±0.015-beat 6 Hz wobble stays inside the phase floor (peak lean 0). `probe_recovery` is 0 failures and now gates the 20 ms direct case at ≤ 0.55 s and ≤ 3.5 BPM, the hold case at ≥ 1.2 s, and the 50 ms lean at ≤ 8 BPM. The known-phase matrix does not set this follow.

## 9. Map: "I want to change X"

| X | file |
|---|---|
| features fed to the network | `Source/AI/LogSpectFeatures.cpp` |
| model I/O, session, providers | `Source/AI/OnnxBeatModel.cpp`, `OnnxSession.cpp` |
| tempo sources, regime, octave anchor | `Source/AI/BeatDecoder.cpp` |
| phase/rate the direct-feed clock follows | `Source/AI/BeatDecoder.cpp`, `Source/Tracking/BeatTracker.cpp` |
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
