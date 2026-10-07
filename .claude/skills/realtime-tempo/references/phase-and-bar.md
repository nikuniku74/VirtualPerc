# realtime-tempo reference — Phase projection, kick channel, which quarter is the one, cuts/seeks/new input

Moved verbatim from SKILL.md section 3 on 2026-10-07. Chronological log: newest entries are often at the top of a section, older ones further down; grep for the symbol you are touching. When you change a decision, update the entry that justified it.

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
**Except a press in the middle of the beat (0.35-0.65, `kLevarePressBand`):**
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
