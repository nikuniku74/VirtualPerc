# realtime-tempo reference — Phase projection, kick channel, which quarter is the one, cuts/seeks/new input

Moved verbatim from SKILL.md section 3 on 2026-10-07. Chronological log: newest entries are often at the top of a section, older ones further down; grep for the symbol you are touching. When you change a decision, update the entry that justified it.

## 3. Phase: the part that is easy to get wrong

**TODO163 (2026-10-26), measured.** Separate fitted `phasePeriodSec`
projection was rejected: Everytime p90 worsened44/48ms. No production field or
projection change remains. A trust-scaled phase nudge was also rejected
(BRANO p90+2ms); keep the ordinary clock controller.

`tryAlignFrom` treats a clear vote for the current one as an answer on direct
sources, with the existing playing margin once sounding. This stops harmony
from overruling a decided network merely because no rotation is required.
Uncertain network and speaker retain their fallback. The all-source variant
lost correct room downbeats (GARDEN3300) and was removed. Direct-only combined
325:321 exact/4 changed, no old correct downbeats lost; new44min MIXER right
one77.433628→79.056047%. Everytime's correct pulse level is a separate START
lifecycle fix, not proof of a correct automatic downbeat.

**Kept (2026-10-09), half-bar moves need 8 beats of persistence (docs/TODO.md item 121).**
With the part playing, a half-bar rotation needed only the network's votes to
clear the playing margin, and the short memory (0.95) let a passage where the 3
sounds like a 1 win for a few bars: 11 of 32 half-bar moves (MIXER 15 of 38)
broke a right one, which came back in a median 10 s. `kHalfBarPersistBeats` 8:
the move must win 8 beats running (`halfBarStreak`). Breaks 11 -> 6 / 15 -> 5,
corrections 21 -> 20 / 23 -> 21, right one within half a point on both halves
(77.8 -> 77.5 / 82.3 -> 82.4). 16 beats stopped nearly all breaks but cost
corrections and a point of the one. Rejected the same day: a per-beat
"centred" mark (green and network-clock error < 5-15 ms) is right against the
teacher only 77-80% of the time, whatever the threshold.

**Kept (2026-10-07), shorter memory for the network's downbeat votes (docs/TODO.md item 106).**
Measured against the offline teacher on the 54-file bench, from the votes the app
actually receives (per-beat dump, removed): the app's one was already at the best
*fixed* alignment per song (72.1 vs 72.7% loaded files, 78.2 vs 74.7% MIXER), while
re-aligning every four bars would reach 88-89%: the true one moves mid-song and
`kVoteDecay` 0.982 (half-life 38 beats) followed it ten bars late. Errors: 1-vs-3
55/78%, 76-82% in runs of 8+ bars, only 10-12% in the first 20 s. Bench split by
song, chosen on half A, checked on half B: 0.97 / 0.95 / 0.93 / 0.90 all better
than 0.982 on both halves, 0.93 best on A, 0.90 drops. Listener chose 0.95 for
fewer audible rotations: held-out one 73.0/82.6 -> 78.2/85.3%, whole bench
69.5/75.7 -> 75.0/78.8%, rotations 0.27/0.21 -> 0.39/0.44 per min (14/19 of them
move a right one, 1/4 before). `kBeatsToMoveTheBar` 11.6 (same share of the
counter's limit), trust thresholds 6.6 (still the eighth beat), harmony keeps
0.982, a declared one starts from 32. Tempo/phase untouched.

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

**Audit fix / test synchronization (2026-10-23, TODO 154).** The raw kick
detector now substitutes zero for NaN/±infinity before its one-pole states;
the analysis bus's later guard cannot protect this separate input. Recovery
is checked against the same samples explicitly replaced with zero, including
onset dates, strengths, envelope and quiet time. The bar-reentry fixture also
drains `analysisCompletedSamples()` through the previous section before
flipping `ShiftBarModel::shifted`: a fixed sleep could relabel queued old
audio with the new section's accents and intermittently fail the seek test.
The wait is bounded and test-only. Eight updated `--bar` runs pass; this
changes no production bar rule or audio-thread wait.

`Tracking/KickOnsetDetector.h` + `BeatTracker::notifyKickOnset`. A channel
carrying only the kick dates the beat to the sample instead of to a 20 ms frame.
It buys **precision, not a new reference** - the absolute calibration stays on
the neural path. Measured: phase error 17 ms rms -> 13 ms. It is gated by
`kickIsTrusted()`, a running share of strikes landing near a clock beat, so
somebody routing the full mix into that input fails it immediately and forever.

### METRO: three or four quarters (2026-10-21, docs/TODO.md item 128)

`EngineSettings::beatsPerBar` -> `BeatTracker::setBeatsPerBar` ->
`TempoFollower::setBeatsPerBar`. The listener sets it; nothing guesses it (a
meter that moved under a sounding part would be the octave problem again).
Every bar `4` in the clock and the tracker is now that number, written so that
4 gives the same integers and floats (`(x % n + n) % n` for `& 3`, `/ n` for
`* 0.25`): `bench_fast` BRANO and MIXER bit-identical to before, 0 of 54 files.
In 3/4 there is **no half bar**: `tryAlignFrom`'s `halfBar` is false, so on a
trusted count every move needs both sources (the network's playing margin or
harmony agreeing), and the pause rule never rotates. A change clears both vote
histograms and the trust (a locked bar stays locked); the count keeps its
quarter or folds into the new bar. Margins and decays are the 4/4 ones - not
measured on real 3/4 material yet (phase 2). The decoder's own `beatsInBar`
still counts four: nothing downstream reads `BeatHypothesis::barPhase`.
`VPTests --meter`: a scripted network calling every third beat, the engine's
one right 100% after 24 s (4/4 control 100%).

### Which quarter is the one

Two separate histograms, deliberately not summed
(`BeatTracker::alignBarFromVotes`, `:617`):

- `downbeatVotes[4]` from the network's `p_downbeat`, decayed;
- `harmonyVotes[4]` from `Tracking/HarmonicChange.h`, one vote per chord change,
  gated on `harmonicShare` (the material has to actually be harmonic).

They are different qualities of evidence: on a drum-free arrangement the harmony
is the *only* evidence, and measured there every single change landed on the
downbeat. Summing them would hide which one answered.

**The iPad mic votes too (2026-10-21, item 134).** Its network vote had been shut
out ("no better than a coin" through a tablet speaker, with the network of the
time), leaving only the harmony, so the one was right 19.5% of the `--speaker`
bench. With the fine-tuned network it is 72.7% with the vote back (title halves
19 -> 73, 29 -> 82); through the probe room (`VPTrack --room`) 18.3 -> 35.9%.
Tempo and phase unchanged to the decimal. The entry stays quantized to the beat
on IPAD (state-machine.md).

The harmony's gate is `harmonicShare` > 0.42 (`HarmonicChange::tonalShare`,
an average over ~15 s). It is divided by the weight gathered since `reset()`:
started at zero, the average read its own start as "no chord" and kept the
gate shut ~40 s on drum-free material whose votes were already unanimous
(item 126). The engine never resets it at an epoch, so after a long room the
share still climbs from near zero at the band's entrance. On today's rendered
song the gate no longer separates anything: kit mix 0.62-0.68, drum-free
stem 0.47 (the 0.32 / 0.55 it was set on were measured on 2026-08-29).

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

**But the part now enters on the next quarter (~2 s), before the coming-in
count exists (item 61, 2026-09-29).** From then on only the playing path
applies, and once `barTrustEstablished` (eight beats of votes agreeing with the
current count) a trusted count may move only by half a bar. Measured on the
22-run song bench against the quarter the network *and* the harmony name at
the end of the song: applying the coming-in rule right after entry is right
4 times and wrong 6 (early votes are poor), so "decide sooner" is not the fix.
What was wrong is that a count trusted on those early votes could never be
moved by a quarter: in 5 of 22 runs both sources ended a quarter away. A
quarter move on a trusted count is now allowed when the other source names the
same quarter, each at its own ordinary margin (network: playing margin over
>= 32 beats; harmony: plain margin over >= 8 changes on tonal material).
Seconds played off the one 1370 -> 914 on the 15 runs with a reference, no run
worse, no extra rotation on the other 7; `VPBar` identical. The converse (a
clear harmony vetoing the network's half-bar move) was rejected: LET ME LOVE
YOU 44.1k went 70 -> 152 s off the one.

`barLocked` (SPOSTA L'1, or a tap that declares the one) stops all automatic
rotation. It does **not** freeze the count against the grid: a `snapPhase` with
`keepBarInStep` still carries it, which is what keeps a locked bar on the beat
of the song it was locked to. **"L'1 è QUI"** (`BeatTracker::declareBarHere`)
names the nearest beat already on the clock as beat zero and locks the bar.
It does not write the phase. A press in the second half of a beat names the
beat about to land; a press in the first half names the beat that has started.
**Except a press in the middle of the beat (0.30-0.75, `kLevarePressFrom/To`; lopsided because a press only arrives late, widened from 0.35-0.65 on 2026-10-21, item 132):**
the listener hears the part on the levare and is pressing on the one, so the
half moves (2026-10-05, docs/TODO.md item 90). The clock glides half a beat
over one beat (`glidePhase`, half or 1.5x speed, nothing skipped or doubled;
`tapHold` covers the glide), and `BeatDecoder::declarePulseHere (true)` moves
`gridAnchorSec`/`lastBeatSec` half a period back, drops the levare beats
(`dropBeatHistory`) and pins the tempo for `kShortFit` beats
(`barTempoHoldBeats`). `gridSerial` does not move: the tracker would read a
half-beat rebuilt grid as a new pulse and stop the part to rejoin it. While
sounding nothing automatic may move the half (`checkGridPhase` is off, the
keep defends `lastBeat`), so before this a levare lock could not be corrected
at all: neither this button (count only) nor TAP (the decoder pulled the clock
back within a second). Measured with `VPTrack --declare-at T` pressing on the
teacher's beat: FLAMINGO 4500 35 -> 0 levare beats, THE REASON 14 -> 0,
EVERYTIME (mixer -12 dB) 36 -> 0; a wrong mid-beat press on a right grid is
undone by pressing again on the one. Re-enabling `checkGridPhase` while
sounding was measured again and is still worse (disp 11.8 -> 13.4 ms).
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
to place the one on the other quarter. A seek (`notifyTrackSeek`) is a
new input since 2026-09-28 (see below). Verify with `VPTests --bar`.

**Loading another file is a new input, not a cut, and so is a seek.** A
seek used to keep the tempo and only move the one; in a long file with
several songs that held the old tempo 9-13 s after a jump into another song
(docs/TODO.md item 55), so `notifyTrackSeek` now calls `notifyInputRestart`.
Softer variants (keep comb and model, with or without the part sounding)
were measured and were worse. A *different file* is a different source, and the decoder
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


### Continuity after an independent new pulse (2026-10-23, TODO155)

The eight-completed-peak song/tempo recovery in BeatDecoder announces a
seven-interval tempo transition, not gridSerial. BeatTracker latches
`glideRebuiltGrid` when it adopts that proof. Both the small rebuilt-grid nudge
and the ordinary return-after-recovering path then use `glidePhase(-move,1)`
while sounding (same0.20beat cap, spent over one beat). Fixing only the first
path left a0.171beat forward jump on a later musical re-entry and skipped a
sixteenth. Apply the same continuity policy to both, for every source earning
the proof; never by song/title/BPM.

A level epoch may be a quiet passage/pause, not a new source. Keep this policy
through those epochs; reset it on explicit replacement (`dropQueued`), reset(),
or speaker/direct mode change. The timing probe verifies all three source
identity cases; the new-file lock fixture also checks a stale policy is cleared.
Large silent re-joins/manual placements and legacy paths before an independent
proof retain their previous behavior. Making all legacy small snaps glide was
measured to change an unrelated title's correct-tempo share (-0.60pp), so that
wider change was rejected. This is a causal source policy, not a song exception.

Across the14 changed paired runs: phase jumps40→14, skipped sixteenths13→4;
none adds a skipped sixteenth per file. The other287 paired traces are identical,
including96 IPAD room simulations. Full metrics and the conditional/cohort
caveat are in verification.md; true device/listening remains.


Item156 (2026-10-23) adds a sixteen-peak proof for wide non-octave changes
against a damaged old fit. Its metadata is fifteen intervals; the existing
`transitionIntervals >=7` policy covers both proof lengths. Phase still comes
from the latest eight-point fitted line and uses the same continuous clock.
No new phase controller or re-entry rule was introduced.

### Missing downbeat publications retained (2026-10-25, TODO157)

A publication contains only the latest beat's downbeat confidence. When its
serial advances by N, decay the network histogram and observed-evidence count
by 0.95^N, then add one observed vote only. Break the eight-consecutive-beat
half-bar proof when N>1. Never reconstruct or duplicate missing opinions.
N=1 retains the original arithmetic; manual bar lock and all margins remain.

Isolated causal A/B: 325 pairs,323 byte-identical; the two remaining traces
change no decision metric, common-correct phase/bar, audible coverage, jumps,
surges or skipped sixteenths. Combined with the missing-fit proof reset:
325/325 byte-identical. New test covers elapsed-beat decay, one observed vote,
streak interruption and a manual-lock/unlocked control. No improved one score
is claimed on this corpus; this corrects the missing-publication edge case.


### TODO160 — harmonic detector experiments rejected (2026-10-25)

All three global candidates are discarded; production HarmonicChange is
restored byte-for-byte to the pre-TODO160 source. Per-frequency median before
folding (rather than the original folded-chroma median) gave6/0 in the focused
suite: full-mix one19%→34%, drum-free detector100%→90%, actual drum-free engine
count still83%. Nevertheless the325-run combined bank found14 rejected
witnesses; the matching no-harmony ablation isolates additional bar losses
from this detector. Max pooling octaves instead of summing then gave2/4 in the
focused suite (32% full-mix,75% drum-free, engine bar0%); rejected immediately.

A smaller candidate required a full-window span before confirming a change:
three overlapping hops span only two intervals, not three independent
observations. Five hops (`kWindow/kHop+1`) gave58% full-mix one,100% drum-free
and83% engine count, snare-only changes4→2, six focused checks green. However
the65-case regression/held-out cohort still exposes bar losses on real tracks;
this candidate is rejected too. A green synthetic detector is not evidence
that the application's one improved. Original full-mix expectation (>33%)
remains an explicit pre-existing red; its19% result is not hidden by changing
the gate, disabling the test or claiming the entire22-red objective solved.
Artefacts: harmony-first.log, harmony-max.log, harmony-window.log,
all2-assessment.json, core-assessment.json, window-assessment.json under
`build-host/vp-complete-20261025/`. The all2 production-profile suite was758/0
but its real-song regressions still vetoed it. Tests alone are insufficient.


### TODO161 — further harmonic candidates rejected (2026-10-25)

Artifacts in build-host/vp-downbeat-attacks-20261025. Refractory counted on all
hops (instead of usable observations) damages sparse one100→25%; candidate
chroma coherence against the same enter gate gives50% sparse; subtracting
common chroma floor after the original tonality gate gives18% full-mix one;
root-summed energy across octaves15%. All rejected on the causal standalone
probe. The original gates and the production detector are restored.

Median of unit directions per hop, while retaining raw-median tonality/energy
classification, gives44% full-mix one (14/6/1/11, baseline7/13/5/12),100% sparse,
4 snare events and6/0 integration checks, actual sparse engine one83%. Still
rejected:21 real-song causal probes expose four bar regressions, including
MIXER80.8511→74.4681% and50.8621→50% on the common-correct cohort; the other17
traces are byte-identical, all tempo/jump/surge/skip counts unchanged. A
scale-independent median does not by itself prove a more accurate musical
bar. No whole325 claim is made for a candidate already disproven locally.


### TODO162 — source isolation and acquisition reference (2026-10-26)

HarmonicChange now resets in the common explicit analysis lifecycle:
prepare/reset/new file/source. Two new tests warm tonal memory, then prove
both reset and queued input restart clear it. Automatic epochs and ordinary
BPM changes retain it. The older statement that no epoch ever clears it must
be read with this explicit-source exception.

During acquisition on direct feeds, the reference of a confirmed chord is the
mean of its three confirming observations. Once lastBarTrusted is true, use
the previous last-window reference; speaker keeps that conservative profile.
No event threshold, dating, vote margin or musical deadline changes. Synthetic
full mix7/13/5/12→10/6/3/5 (19→42% one), sparse100% and actual sparse engine83%
unchanged; snare-only4→3. Focused suite6/0. A mean used continuously loses two
old correct downbeats on a whole set and is rejected. Clearing harmonic votes
on every new grid or resetting the detector at automatic cold epochs also
regresses real material; neither is kept.

With the independent attack control bus and final echo projection, all325
paired probes pass. Direct phase is unchanged on the common-correct cohort;
bar-by-bar comparison proves zero old correct downbeats lost and one newly
correct at24.0824s on held-out BRANO (97.541→98.361% on that file). This is a
small real gain, not evidence that every song's one or acquisition became
perfect. See TODO162 for the separate attack/harmony/projection ablations,
CPU cost, full-wake tests and the pending iPad listening.
