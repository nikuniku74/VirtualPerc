# realtime-tempo reference — The clock (TempoFollower): rate glide, phase steering, snaps, glides

Moved verbatim from SKILL.md section 4 on 2026-10-07. Chronological log: newest entries are often at the top of a section, older ones further down; grep for the symbol you are touching. When you change a decision, update the entry that justified it.

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
do too. On a direct live feed, a bend that stays under 2 BPM but keeps its
sign for 0.40 s leaves the 1.60 s average for a 0.45 s glide. A wobble that
reverses never does. The known-phase matrix does not set that follow.
Standalone clock, 120 BPM, a held +1.5 BPM: the ordinary path is inside
0.3 BPM at 2.58 s; direct live at 1.60 s. A held −1.2 BPM is 2.22 s against
1.50 s. Tempo is clamped 40..220 BPM and *settles*
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

**Re-placement is a glide (2026-10-02).** A sounding clock more than 15 ms off the
published grid on one side for 0.5 s is moved by `TempoFollower::glidePhase`: the
move is spent over a quarter beat as a rate bend (never below half speed), so no
sixteenth is skipped or repeated. The old snap was the "crack" a listener heard
(35 in 281 s on 1000 GIORNI). Song bench vs the offline truth: clock 14.8 -> 11.8 ms,
beats outside 25 ms 26.3 -> 18.7%, surges 0.58 -> 0.33 /min, 42 files better, none
worse. Entry waits for the comb to agree with the decoder (`entryTempoAgrees`, cap
12 s). Still open: in 25 of 54 files the first four strokes after entry are more
than 80 ms off, mostly a grid that entered on the off-beat.
