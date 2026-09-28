# Percussion samples

The shaker, clap and cembalo recordings in this folder come from the **Versilian
Community Sample Library (VCSL)** and **VS Chamber Orchestra 2: Community
Edition**, both released by Versilian Studios LLC under **CC0 1.0** (public
domain). No attribution is required; this file is a courtesy so the source is
not lost.

- VCSL: <https://github.com/sgossner/VCSL>
- VSCO 2 CE: <https://github.com/sgossner/VSCO-2-CE>
- Licence: [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/)

The **congas** (`tumba*`, `open*`, `slap*`, `slap_closed*`, `heel*`, `toe*`,
`muff*`, `tapado*`) are no longer VCSL. Since 2026-09-28 they are cut from the
loop the percussionist chose: SampleFocus **"Salsa Congas Loop - Dance 3"**
(120 BPM, published by Dezz / user1259059406567, "Standard Licensing - Royalty
Free"), by `scripts/prepare_loop_congas.py`. **That is not CC0.** SampleFocus's
standard licence covers using a sample inside music; shipping the sound itself
inside a distributed app may count as redistribution. Fine for the
percussionist's own device; read https://samplefocus.com/license before any
public release. The loop's lowest drum (~205 Hz) is not used; `tumba*` is the
mid conga, at the percussionist's request.

The **shaker** (`shaker_down*`, `shaker_up*`) is, since the same day, cut from
the loop the percussionist chose, `soft-bright-shaker_128bpm.wav` (128 BPM,
two bars), by `scripts/prepare_loop_shaker.py`: the accent is `shaker_down`,
the three different light strokes are `shaker_up`, `_b`, `_med` and
`shaker_down_med`. Same licence caution as the congas: the file came from a
sample site, not a CC0 library; check its terms before a public release.

Claps and cembalo are VCSL, recorded by Sam Gossner: the claps are VCSL
*Claps* (the ensemble takes, not the `SoloClap` velocity ladder); the cembalo
is VCSL *Tambourine 1* and *2*.

## What each file is

| File | Source | What it is |
|---|---|---|
| `open`, `_b`, `_med`, `_soft` | loop hits 22, 3, 16, 23 | mid conga (~325 Hz), open tone |
| `tumba`, `_b`, `_med`, `_soft` | loop hits 9, 10, 15, 4 | the same mid conga, other open takes |
| `slap`, `_b` | loop hits 0, 12 | slap |
| `slap_closed`, `_b` | loop hits 0, 12 | the loop's slaps are already stopped |
| `heel` | loop hit 2 | palm, muted |
| `toe`, `_b` | loop hits 13, 1 | fingertip ghost notes |
| `muff` | loop hit 17 | muted tone |
| `tapado`, `_b` | loop hits 14, 5 | stopped, pitch taken out |

Hit numbers are the first two-bar phrase, see the script. No open tone in the
loop rings clean past ~120 ms (the next sixteenth, or a ghost note, lands on
it); the script continues each one to the engine's 300 ms with its own fitted
ring (three partials, one decay).

| File | Original | What it is |
|---|---|---|
| `clap.wav` | `Clap_rr1` | backbeat clap, ensemble |
| `clap_b.wav` | `Clap_rr3` | clap — round-robin |
| `clap_med.wav` | `Clap_rr6` | clap — third take, tightest of the set |
| `cembalo_down.wav` | `Tamb2_Hit_v2_rr2_Mid` | tambourine, struck hit — the accent on the pulse |
| `cembalo_down_b.wav` | `Tamb1_Hit_v2_rr1_Mid` | cembalo down — round-robin |
| `cembalo_down_med.wav` | `Tamb2_Hit_v1_rr1_Mid` | cembalo down, medium velocity |
| `cembalo_up.wav` | `Tamb2_Shake_rr3_Mid` | tambourine shake — the jingles on the return |
| `cembalo_up_b.wav` | `Tamb2_Shake_rr4_Mid` | cembalo up — round-robin |
| `cembalo_up_med.wav` | `Tamb1_Shake_rr2_Mid` | cembalo up, lighter |

**CEMBALO here means the tambourine**, not the cymbal a dictionary points at.
Down is the struck hit and up is the shake, which is how the instrument is
played in eighths: hand on the pulse, jingles on the return.

Takes that look right on the shelf and were rejected on measurement, so they
do not get picked again by mistake:

- The tambourine **shakes** cannot be cut like a struck sample. They have no
  strike — they swell, measured 56 to 130 ms to half peak — so aligning them
  to a peak in the first 12 ms opens the asset on the rise and lands late on
  every offbeat. They use the shaker path instead (`shape_tau > 0`: onset at
  35 % of peak, then an exponential), which trims 56–95 ms of swell and brings
  them to 1.4–3.0 ms.
- **Finger Cymbals** (`Fing_Cymb.wav`) and the **closed hi-hat** set were both
  tried for the cembalo before the name was understood to mean tambourine. For
  the record: the finger cymbal take is a single strike with no round robin, a
  3.9 s ring that washes into itself at eighths, and a −37 dBFS peak whose
  noise floor sits only 37 dB under it.
- **Claps**: `Clap_rr1/rr3/rr6` reach half peak in 1.9 / 2.5 / 5.7 ms, where
  the louder `rr4` and `rr5` take 4.5 ms (with 25 ms of spread) and 14.6 ms.
  One attack compensation is measured per articulation, so a slow take is not
  corrected for separately — it just lands late, and a backbeat that flams
  differently every round robin reads as bad timing rather than as a player.

Each was trimmed of leading silence, aligned so the strike (not the later
ring) sits a couple of milliseconds in, high-passed to drop hall rumble,
truncated before any second hit, normalised, faded out, and written as mono
16-bit WAV. `scripts/prepare_vcsl_samples.py` does this and can be re-run
against replacements.

heel, toe, muff, slap_closed and tapado now have recordings of their own and
are played as recorded. Without those files `PercussionEngine` still derives
them from the open tone / slap by damping it. The quietest dynamic layer of
each articulation is derived the same way when a `_soft` take is not present.

Triangle is a FEEL assignment, not a VCSL take in this folder yet. Drop these
in and they will be picked up by the existing `*.wav` glob:

| File | What it must be |
|---|---|
| `triangle_open.wav` | short ringing strike (dry tap; wet tail is baked in the engine) |
| `triangle_closed.wav` | stopped #1 - first mute, the beat side of a pair |
| `triangle_closed_b.wav` | stopped #2 - second mute, off the battere |

Optional `_med` / `_soft` takes follow the other stems. `triangle_closed_b`
is its own articulation (`Stroke::triangleClosed2`), not a round-robin of
#1. Until those files exist, `PercussionEngine::synthesizeTriangle` is the
fallback.
