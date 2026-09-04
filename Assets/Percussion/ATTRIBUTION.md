# Percussion samples

The conga and shaker recordings in this folder come from the **Versilian
Community Sample Library (VCSL)** and **VS Chamber Orchestra 2: Community
Edition**, both released by Versilian Studios LLC under **CC0 1.0** (public
domain). No attribution is required; this file is a courtesy so the source is
not lost.

- VCSL: <https://github.com/sgossner/VCSL>
- VSCO 2 CE: <https://github.com/sgossner/VSCO-2-CE>
- Licence: [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/)

Recorded by Sam Gossner. The conga / tumba / quinto one-shots are the VCSL
*Struck Membranophones / Conga* set; the shaker down- and up-strokes are VCSL
*Shaker, Small*; the claps are VCSL *Claps* (the ensemble takes, not the
`SoloClap` velocity ladder); the cembalo is VCSL *Tambourine 1* and *2*.

## What each file is

| File | Original | What it is |
|---|---|---|
| `tumba.wav` | `Tumba_HitN_v4_rr1` | low drum, open tone — loud take |
| `tumba_b.wav` | `Tumba_HitN_v3_rr1` | low drum, open tone — second take, round-robin |
| `tumba_med.wav` | `Tumba_HitN_v2_rr1` | low drum, medium velocity |
| `open.wav` | `Conga_HitN_v3_rr2` | mid drum, open tone — loud take |
| `open_b.wav` | `Conga_HitN_v2_rr2` | mid drum, open tone — round-robin |
| `open_med.wav` | `Conga_HitN_v1_rr2` | mid drum, medium velocity |
| `slap.wav` | `Quinto_HitN_v3_rr2` | high drum, short open — the groove's slap accent |
| `slap_b.wav` | `Quinto_HitN_v3_rr1` | high drum — round-robin |
| `slap_med.wav` | `Quinto_HitN_v1_rr1` | high drum, medium velocity |
| `shaker_down.wav` | `Mid_ShakerHighFaster_Down_rr1` | shaker, accented down-stroke |
| `shaker_down_b.wav` | `Mid_ShakerHighFaster_Down_rr2` | shaker down — round-robin |
| `shaker_down_med.wav` | `Mid_ShakerDouble_Down_rr1` | shaker down, lighter |
| `shaker_up.wav` | `Mid_ShakerHighFaster_Up_rr1` | shaker, return stroke |
| `shaker_up_b.wav` | `Mid_ShakerHighFaster_Up_rr2` | shaker up — round-robin |
| `shaker_up_med.wav` | `Mid_ShakerLowFaster_Up_rr2` | shaker up, lighter |
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

heel, toe and muff have no recording of their own: `PercussionEngine` derives
them from the open tone by damping it, which is physically what those strokes
are. The quietest dynamic layer of each articulation is derived the same way
when a `_soft` take is not present.
