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

### Where that epoch is decided: `updateAnalysisEpoch`

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
`mic - g * (our own output, delayed)`, in two bands split at ~1.5 kHz: through
the iPad's speaker there is no low end to leak, through a mixer the return
carries the congas too, and one band cannot describe both paths.

Three things about it are load-bearing, and two of them were got wrong once:

- **The delay.** Mixer return is the device round trip (`reportedLatencyMs`,
  floor 8 ms). The iPad mic needs a *search* on top of it - the acoustic hop is
  not in the hardware figure - so `updateLeakDelay` correlates on the magnitude
  envelope first (a drum's raw correlation is a needle a few samples wide and a
  coarse step jumps over it) and refines on the waveform. A candidate is only
  accepted above 0.12 correlation: a room full of music always has a largest
  correlation somewhere in the window, and "largest" is not "ours".
- **The gain is fitted over half a second of causal history, not over the
  block.** The five normal-equation terms of the two-band least squares are
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
  default - 0.62-0.74 at quarters; all 54 rows now under 0.0001.** A per-callback
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

**What the 0.0001 above is and is not.** That bench returns an exact scaled copy
of our own output at a whole number of samples, so a converged two-band fit
removes essentially all of it; it is a statement about the estimator. A room is
not that. Same rig, through the repository's own reflection model
(`vp::probe::speakerRoomMic`), one cause at a time:

| return path | residual |
|---|---|
| exact copy, integer delay | 0.0000 |
| delay 0.373 of a sample off the grid | 0.0957 |
| a -56 dB noise floor | 0.0045 |
| 260 Hz/9 kHz band limiting alone | 0.8050 |
| one wall at 7.3 ms, a second at 14.6 ms | 0.8484 |
| all of it, one wall | 0.8755 (0.9903 with cancellation off) |
| all of it, the full eight-tap tail | 0.9150 (0.9902 off) |

As a share of the return removed, against each fixture's own cancellation-off
control: **90.2%** when the direct component reaches the mic spectrally
unmodified at a fractional delay, **18.9%** once that same direct component is
band-shaped by 260 Hz and 9 kHz and nothing else changes, **11.5%** on the
complete one-wall fixture and **7.6%** with the full eight-tap tail.

A fractional delay it can still cancel. A return whose spectrum has been
*reshaped inside each band* it largely cannot - two gains cannot follow a 260 Hz
high pass through a conga - and neither can it reach a reflection that is not in
the reference at any single delay. The 0.805-0.915 residual is where those two
limits of a two-band, single-delay model put the floor for this fixture: a known
limitation of the model's shape, not a regression, and **not** a claim that the
canceller takes the direct arrival out of a room. Quote 0.0000 about the
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
