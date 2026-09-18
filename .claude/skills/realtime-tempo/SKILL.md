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
the click mixes (INFINITO, dip, Flamingo, Sally) is still required.
Microfono iPad still waits. Sally click mix:
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
of the song it was locked to. Unlocking is a tap on the lit control: that
hands the count back without rotating. The old five-tap unlock (all the way
round the bar, then one more) read as a button stuck on. See docs/TODO.md
item 13.

**A two-quarter cut is not a new song.** The epoch watcher needs ~4 s of quiet
before it will restart the decoder, so a mute of two quarters never fired, and
must not: the clock kept time, only the *count* is now on the three. A separate
gap detector (`VirtualPercussionEngine::maybeDetectBarReentry`) looks at the
block peak against the recent loud level - a mute clears it in one callback, a
fill never does - and opens a four-bar coming-in window
(`BeatTracker::notifyBarReentry`). Same window on seek (`notifyTrackSeek`),
without bumping `analysisEpoch`. One rotation per return, two bars of evidence
instead of 32, `rotateBarIndex` only. The clap reads `barTrusted` from the
tracker (histogram names beat zero, or the listener's lock), not a time-since-
rotation proxy. Verify with `VPTests --bar`.

**Loading another file is a new input, not a cut.** A seek keeps the tempo and
only moves the one; a *different file* is a different source, and the decoder
holds a lock that is right for the song that is gone. `loadInternalTrack` calls
`VirtualPercussionEngine::notifyInputRestart()`, which forces a fresh
`analysisEpoch` with `preserveCombOnEpoch = false` - the same restart the input
change uses - while never restarting the clock. Drop the epoch with
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
