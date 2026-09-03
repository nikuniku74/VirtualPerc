# Rapid causal tempo following

## Intent

Make VirtualPerc acquire a tempo quickly, react to an abrupt tempo change within
two new beats, and keep its percussion tightly behind live input without using
look-ahead. The change must improve the measured abrupt-change cases without
regressing established acquisition, phase, metrical-level, fill, dropout, or
real-time behaviour.

The recorded-loop work described in `docs/HANDOFF_LOOP_DEBUG.md` remains in
standby. This design does not change loop banks, hybrid rendering, time
stretching, sample-rate policy, or the LOOP/PATTERN setting.

## Success criteria

For abrupt steps of 5–10% that remain on the same metrical level:

- adopt the new tempo within two beats after the step;
- reach at most 1 BPM tempo error;
- bring phase error below 25 ms within one additional beat;
- emit no duplicated, skipped, or backward-moving clock pulses;
- produce no false tempo change on the existing fill, subdivision, dropout, and
  jitter cases.

Larger same-level changes remain covered by safety and convergence tests, but
do not carry the three-beat phase deadline: closing a large accumulated phase
gap that quickly would require either a phase jump or pulse intervals outside
the existing musical safety limits.

All existing deterministic tests and timing probes must remain at least as good
as their checked-in baselines. Separate end-to-end measurements cover mixer
line input and iPad microphone input. A dedicated kick input is also measured,
but is not required for the target.

These criteria are regression barriers for the measured corpus, not a claim
that every possible piece of audio can be classified without ambiguity by a
causal system.

## Existing architecture retained

The audio callback continues to feed mono analysis audio to
`NeuralBeatTracker`. The neural worker continues to produce one activation
frame every 20 ms and passes causal hypotheses through the existing seqlock.
`BeatTracker` continues to project a hypothesis from its historical analysis
timestamp to the current acoustic time. `TempoFollower` remains the only
playable clock and remains on the audio thread.

No neural inference, allocation, blocking operation, or unbounded work is added
to the audio callback. Latency compensation continues to advance timestamps
using elapsed, already-observed audio and measured output latency; it does not
buffer or inspect future input.

## Abrupt-change evidence

`BeatDecoder` gains a small, bounded abrupt-change detector alongside, but
separate from, the existing `unknown`, `live`, and `fixed` regime classifier.
The regime describes the music over time; the new state describes a short
transition:

1. `stable`: the existing long/short-fit policy owns the tempo.
2. `suspected`: the newest eligible interval exceeds the candidate-change
   threshold defined below, but one interval can still be a fill or timing
   error.
3. `rapid`: a second consecutive interval agrees with the candidate period and
   direction. The candidate now owns tempo temporarily.

The detector observes eligible causal activation peaks before the current-grid
gate. This is necessary because the first beat after a large slowing can be too
far from the old grid to pass that gate. An eligible peak must still pass the
existing activation, refractory, supported-BPM, subdivision, and metrical-level
checks. A peak rejected by the current grid can create or support a candidate,
but cannot move the playable clock by itself.

`stable` records an eligible interval that exceeds the candidate-change
threshold as `suspected`. While suspected, the next eligible peak is tested
against both the old grid and the candidate grid. Two candidate intervals
require three causal peaks: the beat at the change boundary and the next two
beats. This is the earliest robust two-interval confirmation and matches the
two-beat tempo-response target. Suspected state does not alter the playable
clock.

The candidate period is the arithmetic mean of the two intervals. Let `jitter`
be the median absolute relative error of the newest stable intervals:

- relative candidate change must exceed
  `max(1 / committedBpm, 3 * jitter)`;
- each line-input interval must lie within `max(1.0%, 2 * jitter)` of the
  candidate;
- each microphone interval must lie within `max(2.0%, 2 * jitter)` of the
  candidate and both supporting activations must exceed the existing
  information gate;
- the candidate must remain in the same accepted metrical octave and inside the
  supported BPM range.

The different coherence floors acknowledge microphone timing scatter without
making microphone evidence weaker. The common metrical checks prevent either
source from bypassing octave and subdivision protection.

Changes that are no larger than 1 BPM remain with the existing live fit because
they already satisfy the agreed tempo-error bound. Gradual accelerando and
rallentando also remain with the live regime because their consecutive
intervals form a moving trend rather than two intervals around one stable step
candidate.

## Rapid adoption

On confirmation, `BeatDecoder` adopts the coherent recent-period estimate
directly instead of waiting for the eight-beat short fit or moving through the
normal live commit rate. `TempoFollower` adopts that running rate on the first
callback that receives the confirmed hypothesis while preserving its current
phase. The historical long fit and `TempoEstimator` continue collecting
evidence and retain responsibility for precision and metrical level.

The fast estimate is provisional:

- the next consistent accepted beat promotes it back to the normal live/fixed
  flow;
- a contradictory beat abandons it immediately and restores normal ownership;
- a decoder discontinuity, input epoch restart, user octave change, or
  metrical-grid rebuild clears all transition evidence.

The transition has a hard two-beat lifetime. No stale rapid state or temporary
rate limit survives its exit.

## Clock and phase behaviour

The hypothesis exposes the transition state, candidate tempo, and bounded
confidence to `BeatTracker`. `BeatTracker` uses them only while the rapid state
is active, instead of treating a large phase residual as generally trustworthy.

`TempoFollower` temporarily opens its phase rate-steering limit for a confirmed
transition, only as far as needed to reach the phase target within the next beat
and never beyond the existing monotonic-grid safety bound. Normal limits return
when the transition exits. It must preserve these invariants:

- phase is continuous while percussion is audible;
- the clock always advances;
- correction happens through temporary speed, not a backward phase jump;
- no pulse is emitted twice or skipped;
- when percussion is silent, the existing free phase placement remains
  available.

The normal 0.25–0.90 s phase filtering is not shortened globally. Existing
measurements show that a global reduction follows decoder noise more closely
and slightly worsens phase error. Faster phase response is enabled only by
coherent transition evidence and returns to the established filter as soon as
the transition ends.

## CASSA setting

The `CASSA` setting remains available and remains `CASSA: NO` by default. It is
not merely cosmetic: when assigned to a mixer channel, its raw
`KickOnsetDetector` timestamps improve phase resolution beyond the neural
20 ms frame grid and can identify drummer silence. Trusted kick timestamps keep
using the existing `TempoFollower::observeOnsetPhase` path during a rapid
transition; they refine phase but do not override or confirm the decoder's
tempo candidate.

This work does not automatically claim extra input channels when CASSA is off.
It preserves preference compatibility and the existing trust gate that rejects
a wrongly routed channel.

## Focused cleanup

Cleanup is limited to code encountered in the timing path and proven unused.
In particular, the current `BeatDecoder::setQuickStep` seam and `quickStep`
member select no behaviour and have no application caller; they will be removed
with any obsolete probe call after confirmation by repository-wide search.
No unrelated refactoring is part of this change.

Temporary A/B branches used while tuning must not remain as shipped feature
flags. Baseline measurements are recorded before implementation, and the final
code keeps only the winning path.

## Diagnostics

Existing debug snapshots gain bounded transition diagnostics sufficient to
explain a device result:

- transition state;
- candidate BPM;
- number and coherence of supporting intervals;
- reason for confirmation or rejection;
- elapsed beats in rapid mode.

Diagnostics use fixed-size scalar state and existing snapshot publication. They
do not log, format strings, or allocate on the audio callback.

## Verification

### Deterministic decoder tests

Add rising and falling tempo steps across slow, medium, and fast BPM ranges.
Cover small, medium, and large changes; changes near half/double-time rivals;
alternating subdivisions; one-off outliers; fills; missing beats; and gradual
ramps. Assert confirmation timing, reported BPM, metrical level, and false
transition count.

### Decoder-plus-clock tests

Extend `VPAlign` or its underlying test seam to drive `BeatDecoder` and
`TempoFollower` together. Assert:

- tempo error no greater than 1 BPM by the second changed beat for 5–10% steps;
- phase error below 25 ms by the following beat for 5–10% steps;
- monotonic phase;
- no duplicate or skipped pulse intervals;
- unchanged or improved steady-tempo RMS and worst-case phase;
- unchanged accelerando/rallentando performance.

### End-to-end replay

Run the existing neural replay probes on representative mixer and microphone
captures. Compare baseline and candidate for initial lock time, abrupt-change
response, mean and worst phase error, wrong-octave count, grid rebuilds,
analysis backlog, FIFO discontinuities, and false tempo transitions.

Run dedicated-kick cases separately to verify that CASSA improves or preserves
timing while the default no-kick path still meets the target.

### Real-time and full-suite checks

Run `VPTests` and all relevant timing probes. Verify the iOS/simulator build and
ensure the audio callback introduces no allocation, lock, worker inference, or
unbounded loop. The change is accepted only if every existing assertion passes
and every established baseline metric is unchanged or improved.

If the abrupt-step target improves but any protected metric regresses, the
candidate is rejected or narrowed until the regression is removed.
