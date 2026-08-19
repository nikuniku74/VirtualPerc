# Percussion samples

The conga and shaker recordings in this folder come from the **OLPC Berklee
Sound Library**.

- Source: <http://wiki.laptop.org/go/Sound_samples>
- Obtained via: <https://github.com/Tonejs/audio> (`berklee/`)
- **Licence: [Creative Commons Attribution 3.0](https://creativecommons.org/licenses/by/3.0/)**

CC BY is usable in a commercial product, **but attribution is a condition of
the licence, not a courtesy**. The credit has to appear somewhere a user can
reach it — an About or Credits screen in the shipped app, not only this file.
Suggested wording:

> Percussion samples from the OLPC Berklee Sound Library, licensed under CC BY 3.0.

## What each file is

| File | Original | What it is |
|---|---|---|
| `tumba.wav` | `conga_1.mp3` | low drum, open tone — first take |
| `tumba_b.wav` | `conga_2.mp3` | low drum, open tone — second take, used for round-robin |
| `open.wav` | `conga_3.mp3` | high drum, open tone |
| `slap.wav` | `conga_4.mp3` | high drum, slap |
| `shaker_down.wav` | `shaker_2.mp3` | shaker, accented stroke |
| `shaker_up.wav` | `shaker_1.mp3` | shaker, return stroke |

Each was decoded from MP3, **trimmed of its 24–43 ms of leading silence**
(which would otherwise be 24–43 ms of lateness on every stroke, against a clock
calibrated to ±3 ms), truncated before any second hit in the file, normalised,
faded out, and written as mono 16-bit WAV. `scripts/prepare-samples.sh` does
this and can be re-run against replacements.

## Limits of what this library gives us

It is four conga hits and two shaker strokes — not a multi-sampled instrument.
So:

- **Dynamic layers are derived, not recorded.** `PercussionEngine` builds the
  soft and medium layers from the recording by taking the top off and shortening
  the ring, which is the right-shaped approximation — hitting a drum softer does
  not merely turn it down — but it is still an approximation. Only the hard
  layer is the recording untouched.
- **heel, toe and muff are derived from the open tone** by damping it. That is
  physically what those strokes are, but a recorded heel would sound better.
- **Round-robin is genuine only for the low drum**, which has two takes. The
  other articulations vary by start jitter between round-robin slots.

Replacing these with a properly multi-sampled library is a drop-in: keep the
file names, put the louder take in and re-run the prepare script. Nothing in the
DSP needs to change, and `recordedStrokeCount()` is asserted in the tests so a
missing or unreadable file fails loudly instead of silently falling back to
synthesis.
