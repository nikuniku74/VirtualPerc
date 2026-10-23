# realtime-tempo reference — State machine, entry gates, background audio

Moved verbatim from SKILL.md section 6 on 2026-10-07. Chronological log: newest entries are often at the top of a section, older ones further down; grep for the symbol you are touching. When you change a decision, update the entry that justified it.

## First entry waits while the tempo may be half the pulse (2026-10-22, docs/TODO.md item 147)

`BeatTracker::holdEntryForLevel`, called once an entry is otherwise ready
(after the settled confidence below): the decoder publishes
`BeatHypothesis::halfBeatContrast` - `TempoEstimator::beatPhaseFor (bpm)`'s
contrast, how high the folded activation stands half a beat from its peak,
only with >= 4 s of comb evidence. Across every logged second of the bench and
40 unseen songs, three modes: at the pulse median 0.01 (p90 0.31), at half
the pulse median 0.86 (p25 0.71), at double 0.02 (blind: the 50/100 case).
`HalfLevelCheck` (Core/Types.h) smooths it over 1 s, waits while it is unknown
or > 0.70, doubles AUTO once after 3 s high on one tempo (+-4%), and gives up
after 8 s. Only the first entry after STOP / a new epoch (`! hadPlayed`), only
under AUTO, only when the double is <= `kOctaveTooFast`, same exemptions as
`entryConfExempt`. Why the half side works and the double does not: the
fine-tuned network is near zero half a beat off on most songs, so a tempo at
half shows its missing beats as half-beats; at double nothing is missing.
Where it came from: unseen funk at 118-126 acquired on a two-beat interval
(`tryFastAcquire`'s "below 90 BPM the interval is the only fact" rule
re-anchored an HMM that said 125 with margin 8), entering before the comb had
spoken. Measured: right tempo MIXER 79.6 -> 81.3, BRANO 72.2 -> 74.4, IPAD
73.1 -> 75.7, both halves, no file worse, bench `cmp` and first four strokes
unchanged; EVERYTIME in ~4 s later. Rejected the same day: resetting the
entry waits on a cold epoch (one file), refusing the slow interval when the
HMM names exactly its double with a peak mid-interval (11/15 acquisitions
right, but only 1-2 songs change), the raw comb before entry (too weak).

## Entry waits for a settled confidence (2026-10-21, docs/TODO.md item 129)

`BeatTracker::updateState`, listening: on top of every other entry condition
the smoothed confidence must have stayed >= `kEntryConfSettled` (0.60) for
`kEntryConfHoldSec` (4 s), or everything else must have been ready for
`kEntryConfWaitSec` (6 s; that wait advances only while the rest holds, so on
songs where the decoder changes its mind it can reach 15-21 s). Why: the
confidence before the entry separates good entries from bad ones (AUC 0.80 /
0.83 on its 3 s minimum, BRANO / MIXER), and the part came in on the first
block over 0.22. Measured (`scripts/analysis/entry_start.py BASE TAG`, `HALF=A|B`, `V=1`: fixed 25 s window from
today's entry, a beat bad only while the part sounds): first four strokes
right 26 -> 36 / 33 -> 36; wrong played seconds 505 -> 322 / 347 -> 288; of the
seconds no longer played 85% / 74% were wrong. Both halves of the titles.
Whole songs MIXER 85.7 -> 86.2% right; BRANO 81.7 -> 80.2%, all VIVERE 48k
flipping octave (the split song). Rejected the same day: entering only once the
clock has reached its target (+2 entries, FLAMINGO 1200 MIXER -5 points), a
20 s comb wait (only DEJAVU better, GARDEN 1500 worse), and a hard 6 s cap on
this rule (378 wrong seconds instead of 322). Exempt like the comb gate
(`entryConfExempt`): TAP, a tempo set by hand, follow off, harmony,
START immediately - a tempo somebody gave the app is not waiting on the network.


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

**First-bar decoder acquisition (measured 2026-09-22; test contract corrected
2026-10-23).** Three causal peaks on a line feed, four through a room, can
establish a decoder grid inside one bar. These numbers measure that grid and
its next projected quarter, not production percussion entry: the confidence
and half-level guards added in items 129/147 still apply. Once all guards
are satisfied, an armed, live input goes directly to FOLLOWING without an
extra 160 ms in LOCKING. Background listening keeps LOCKING's empty-room
observation window. `VPTests --state-timing` checks the two facts separately:

| BPM | line valid / entry / bar | room valid / entry / bar |
|---:|---:|---:|
| 76 | 1.88 / 2.64 / 3.43 s | 2.66 / 3.43 / 3.43 s |
| 100 | 1.44 / 2.01 / 2.61 s | 2.04 / 2.61 / 2.61 s |
| 140 | 1.02 / 1.44 / 1.86 s | 1.46 / 1.86 / 1.86 s |
| 168 | 0.86 / 1.20 / 1.55 s | 1.22 / 1.55 / 1.55 s |

All eight decoder paths can offer a quarter by the first 4/4 boundary. The
state fixture now supplies four seconds of confident observations and a known
pulse level while the comb is absent, proves entry stays blocked, then opens
the comb gate and checks FOLLOWING on that callback. The unarmed control
waits for the same confidence evidence, then reaches LOCKING. Separate tests
measure the real four-second confidence hold and six-ready-second fallback
at buffers 64/256/1024, without bypassing either with an exemption. The fallback
decides on the callback after it accumulates six seconds: 6.001333 / 6.005333 /
6.037333 s; the high-confidence hold is 4.000000 / 4.000000 / 4.010667 s.
New-file entry must also earn that hold afresh. No production timing rule was
changed to make these tests pass.
