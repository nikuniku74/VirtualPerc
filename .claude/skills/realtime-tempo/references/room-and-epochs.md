# realtime-tempo reference — Empty room, input epochs, discontinuities, speaker-leak canceller

Moved verbatim from SKILL.md section 5 on 2026-10-07. Chronological log: newest entries are often at the top of a section, older ones further down; grep for the symbol you are touching. When you change a decision, update the entry that justified it.

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

**The network's operating level on the MIXER (2026-10-05, docs/TODO.md item
90).** BeatNet's features are log10(1 + magnitude), so the analysis level is an
input. The make-up only boosts, to `kMakeupTargetPeak` 0.20, and a loaded file
passes at gain 1: its envelope is its own level, 0.14-0.55 across the bench
(median 0.27), 0.43 on the band's mixer sends, which is also where the
fine-tuned model's training features sit (`VPActivations --features`, no
make-up). A MIXER send that is not at the top was therefore analysed ~7 dB
under what BRANO and the training see. Same live recordings through the MIXER
path (`bench_fast.py run TAG ARGS="--gain -12"`), against the teacher: scatter
14.5 ms, p90 56 ms, 1.28% of beats on the levare, against 11.9 / 35 / 0.65% at
0 dB. Now, on `FollowSource::kitMic` while the part is audible, the target is
`kMakeupPlayingPeak` 0.40: -12 dB reads 11.3 / 32 / 0.33%, -24 dB 12.3 / 39 /
0.50%, right octave -0.6 and -0.4 points. Not before entry: 0.40 there changes
the octave decision (bench 77.9 -> 74.2% at 0 dB, `VPTests --level` 168 BPM at
-6 dB reads the half). Not on a loaded file: there the octave work is tuned on
the file's own level, and the same rule flipped UNA CANZONE 48k to the double
after entry; BRANO stays bit-identical. A symmetric make-up (attenuating too)
changed nothing on this bank: file envelopes almost never exceed 0.30. The
remaining level dependence is in the raw-level gates (`sourceAudible`,
`heardMusic`, epochs): at -24 dB entry comes later. The parallel fast bench is
not bit-deterministic under load (3-6 files differ run to run, isolated reruns
agree); its aggregate scores repeat to two decimals.

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
