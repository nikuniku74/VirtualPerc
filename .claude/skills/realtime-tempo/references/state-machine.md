# realtime-tempo reference — State machine, entry gates, background audio

Moved verbatim from SKILL.md section 6 on 2026-10-07. Chronological log: newest entries are often at the top of a section, older ones further down; grep for the symbol you are touching. When you change a decision, update the entry that justified it.

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
