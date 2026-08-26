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
*Shaker, Small*.

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

Each was trimmed of leading silence, aligned so the strike (not the later
ring) sits a couple of milliseconds in, high-passed to drop hall rumble,
truncated before any second hit, normalised, faded out, and written as mono
16-bit WAV. `scripts/prepare_vcsl_samples.py` does this and can be re-run
against replacements.

heel, toe and muff have no recording of their own: `PercussionEngine` derives
them from the open tone by damping it, which is physically what those strokes
are. The quietest dynamic layer of each articulation is derived the same way
when a `_soft` take is not present.
