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
