---
name: percussion-patterns
description: How the shaker and conga parts are written and played in VirtualPercussionist - the 16-step groove tables, the nine stroke articulations, per-style shaker weighting and accents, the 8-bar phrase, swing/humanize/ghosts, band dynamics thinning, and voice/attack handling. Use when touching Source/Percussion/ or Source/Loops/, adding or editing a groove style, changing what the shaker plays, or when the part sounds mechanical, too busy, or lands in the wrong place.
---

# Percussion patterns: what the player actually plays

Read this before editing anything in `Source/Percussion/`. The tables are
musical decisions with reasons written next to them; the guards below exist
because those decisions were lost once already.

## 1. The grid

- Everything is on a **sixteenth grid**: `GrooveEngine::kStepsPerBar = 16`.
- The clock emits pulses at the tracker's subdivision - 1, 2 or 4 per beat,
  **4 by default** (`BeatTracker::pulsesFor`). `PercussionEngine::render` maps a
  pulse onto the sixteenth grid whatever the resolution is:
  `step = (barBeat * 4 + (idx * 4) / pulsesPerBeat) % 16`. At a coarser
  resolution the odd steps are simply never visited - the table is not rewritten.
- Steps `0, 4, 8, 12` are the quarters. `2, 6, 10, 14` are the off-eighths.
  The odd steps are the "e" and the "a", where the quiet strokes live.
- `GrooveEngine::eventsAt(barIndex, step, out, maxOut)`
  (`Source/Percussion/GrooveEngine.cpp:745`) is the single entry point: it
  returns up to `kMaxEvents = 4` `GrooveEvent{stroke, velocity, delayBeats}`
  for one sixteenth of one bar.
- **`delayBeats` is always >= 0.** The clock hands out grid positions as they
  pass and there is no going back for one, so swing and feel are expressed as
  *lateness*, never earliness.

## 2. The stroke vocabulary

`enum class Stroke` (`GrooveEngine.h`). Congas are not one drum you hit - they
are six or seven different sounds on two drums, and which ones fall where is
most of what makes a pattern read as a marcha rather than as a list of hits.

| stroke | what it is |
|---|---|
| `shakerDown` | the accented stroke, away from the body, on the pulse |
| `shakerUp` | the return stroke, lighter, on the off-eighth |
| `tumba` | low drum, open |
| `open` | high drum, open tone |
| `slap` | high drum, cracked |
| `heel` | palm down, muffled - the quiet half of the marcha |
| `toe` | fingertips, muffled |
| `muff` | muted tone, no ring |
| `slapClosed` | the crack with the hand left on the head - **no ring at all** |
| `tapado` | the low drum stopped: a thud with the pitch taken out |

`slapClosed` and `tapado` are the *stopped* strokes. Before they existed every
loud articulation rang, and a part built only out of ringing strokes sits *over*
a band instead of inside it. Reach for them whenever a figure needs weight
without a note.

## 3. The invariant: no conga on the first quarter's down-stroke

```
if (congasOn && step != 0)   // Source/Percussion/GrooveEngine.cpp:795
```

The band is already on the one - kick, bass, and the downbeat of whatever the
guitarist is playing all land together - and a conga on top of that is not heard
as a separate voice, it just thickens the loudest attack in the bar. A
percussionist standing next to a drummer plays *around* the one: the low tone
lands on its "e" or its "and", the heel-toe pair starts a sixteenth late, and
the one is left to the band.

Every table is written that way **and** the guard enforces it, so a table edited
later cannot quietly put a stroke back on the downbeat. Keep both.

The shaker is deliberately **not** covered by the guard: a shaker on the pulse
*is* the pulse, and it is what the listener follows.

## 4. Styles: nine parts, not variations on one

`enum class GrooveStyle` (`Source/Core/Types.h`), one `StyleSpec` each
(`GrooveEngine.cpp:517`). A player does not bring a marcha to a rock track, so
each style carries its **own conga figure and its own shaker weighting**.

| style | the idea |
|---|---|
| `marcha` | latin tumbao. The signature is the **pair of open tones on 4 and the "and" of 4** pulling into the next bar; everything before them exists to leave room for it. |
| `rock` | lives in the gaps. Kick and snare own the numbered beats, so: nothing on 2 and 4, a stopped low answer after one, an open tone after two, an open push after four. |
| `dance` | answers the four-on-the-floor posts - stopped lows on the first half, ringing opens on the off-eighths, one syncopated slap that moves through the phrase. No extra kick disguised as a conga. |
| `pop` | tasteful, quiet, mostly space. `ghostChance` 0.10. |
| `samba` | weight on 2 and 4, syncopated opens. |
| `funk` | sixteenth ghosts, leaning into the "a". Busiest: `ghostChance` 0.45. |
| `reggae` | one-drop - **the one is empty, the three is the post** (`tapado` at 0.90 on step 8). Shaker skanks on the offbeat like a guitar. |
| `bossa` | clave-shaped, mostly space, pulling on the ands. |
| `twoOne` (DUE-UNO) | not a genre, a shape: two strokes on one quarter, one on the next, all the way round, on two drums and nothing else. `ghostChance` 0.00 - the style is two sounds and a ghost would be a third. **The automatic chooser never picks it**: "keep it simple" is a decision about the gig. |

Each spec holds five bars - `barA`, `barB`, `barC`, `barD`, `fill` - plus
`shaker[16]`, `accent[4]`, `ghostChance`, `fillEveryBars`.

## 5. The phrase

```
bar in phrase:  0   1   2   3   4   5   6   7
plays:          A   B   A   C   D   B   A   fill
```

Two bars of A/B alternating is a loop you hear the seam of after twenty seconds;
eight bars is a sentence - state it, answer it, state it, go somewhere, and a
fill on the way out. `C` is the bar that goes somewhere; `D` stops the second
half being the first half again.

`fillEveryBars = 8` for every style; `isFillBar()` is `wrapBar(barIndex, 8) == 7`.

**The phrase must be aligned to the song, not to whenever the part came in.**
`PercussionEngine::alignPhrase()` is called at a bar line when a section change
is noticed (see `VirtualPercussionEngine`) and sets the count so the *next* bar
is bar one of the sentence. Before that existed, fills landed every eighth bar
counted from an arbitrary start - on average four bars off, which sounds like a
fill in the middle of a verse.

## 6. The shaker

`spec.shaker[16]` is a **velocity per sixteenth**; zero is silence. What the
table carries is *where the weight goes*, and that differs per style: latin
leans on the pulse, rock leans on the backbeat with the drummer, dance leans on
the offbeat where the open hat sits, pop stays level and out of the way, reggae
skanks (0.80-0.92 on the off-eighths against 0.36-0.48 on the pulses).

Three rules:

1. **Down on the pulse, up on the return.**
   `stroke = (step % 4) == 0 ? shakerDown : shakerUp`. They are different
   strokes on a real shaker; playing them as the same one is exactly what makes
   a shaker part sound like a click track with noise on it.
2. **A bar-long figure, not a repeated cell.** Each table states its shape over
   two beats and answers it over the next two. These used to be four identical
   beats written out four times; however musical the numbers were, nothing in
   the bar told you where you were in it, so it read as a machine.
3. **`setSubdivision` thins, never adds.** `quarter` keeps steps 0/4/8/12,
   `eighth` keeps even steps, `sixteenth` keeps all steps, and `autoDetect`
   means eighths. The same predicate gates shaker, written congas, fills and
   conga ghosts. The no-conga-on-step-0 guard still wins. Recorded WAV loop
   stems do not pass through this event filter.

`spec.accent[4]` scales by quarter and belongs to the **style**: marcha and pop
lean on the one (1.00, 0.86, 0.93, 0.88), rock leans on 2 and 4 with the drummer
(0.94, 1.00, 0.92, 1.00), a four-on-the-floor bar is nearly even (dance: 1.00,
0.95, 0.97, 0.95). A single global contour favouring beat one silently cancelled
the rock backbeat, which is why this is per style.

## 7. Feel: swing, humanize, ghosts

**Swing** (`humanDelay`, `GrooveEngine.cpp:711`) is a **warp of the beat**, not a
late off-eighth. At full amount the "&" sits two thirds of the way through the
beat (`kFullSwingBeats = 1/6`); "e" and "a" ride the same stretch, so a
sixteenth part shuffles instead of fighting delayed eighths.

**Humanize** does two things:
- velocity spread: `+/- 0.20 * humanize` around the written value;
- micro-timing: `humanize * (0.004 + 0.006 * 0.5 * signed_random)` beats at 120
  - about 2 ms of natural lateness with ~3 ms either side, biased late by half
  its spread so nothing is ever asked for before the grid position.

Default is 0.35, well under 1: a percussionist is much closer to the grid than
the full range would suggest.

**Ghosts** - the barely-there fingertip strokes on the odd sixteenths, written
at 0.16:

```
chance = ghostChance * intensity * (0.4 + 0.6*humanize) * dynamics * dynamics
```

Odd steps only, never in a fill. They are the difference between a part that
breathes and one that is merely correct, and they must stay quiet enough that
you notice them only when they stop. They are also the **first thing to go** as
the band comes down - note the squared `dynamics`.

## 8. Dynamics: playing *less*, not just quieter

`setDynamics(0..1)` from `Percussion/BandDynamics.h`. The point is the second
half of this: any fader can play quieter; only a player plays **less**.

- `dynamicGain() = 0.32 + 0.68 * dynamics` - about 10 dB, roughly what a player
  gives between a chorus and a verse.
- `v_survives(v)` drops any stroke written below `(1 - dynamics) * kThinCeiling`,
  with `kThinCeiling = 0.72`. So the figure thins **from the bottom**: at full
  dynamics nothing is dropped; half way down the heel/toe fill-in has gone; near
  the floor what is left is the skeleton - the slap (0.88-0.96), the low tones
  and closing opens (0.74-0.90), the push into the next bar. Nothing above 0.72
  is ever dropped, at any dynamic.
- Below the floor the engine **stops**, at a bar line, and comes back when the
  band does (`BandDynamics::wantsSilence`; the UI reads IN ASCOLTO).

Two structural facts about the input, from `BandDynamics.h`, worth knowing
before you try to improve it:

- The level is taken **after leak subtraction and before the analysis make-up
  gain**, so the app reads the band and not itself, and the rider that erases
  verse-vs-chorus has not been applied yet.
- The reference is **the loudest this song has been**, decayed slowly - not an
  absolute threshold. A quiet band and a loud one differ by more than a verse
  and a chorus do.
- **Density was tried as a second input and rejected**: it is computed on the
  analysis bus, which has the make-up gain on it, so it has nothing left to say
  about dynamics. Measured: -3.5 dB against a band that came down 14 with level
  alone, -0.9 dB once density voted. Density is still used for naming the style
  (`StyleDetector`). Do not re-add it as a dynamics input.

## 9. Choosing the style from the music

`Source/Percussion/StyleDetector.h` - deliberately **not** genre classification.
Genre is a hard open problem and it is not the question: the parts differ by
*rhythm*, which is measurable. It folds the analysis signal onto the bar in
three bands (kick / body / high, 16 bins) and reads:

- even kick weight on all four beats + offbeat high energy -> four-on-the-floor;
- high energy alternating on 2 and 4, absent on 1 and 3 -> backbeat;
- energy on the odd sixteenths without four-on-the-floor -> latin syncopation;
- none of the above, moderate -> pop.

Three one-pole filters and an accumulate per sample; no allocation after
`prepare()`. It holds its previous answer until a rival is clearly and
repeatedly better, and `confidence() < ~0.3` means keep playing whatever you
were playing.

## 10. From event to sound

`PercussionEngine` (`Source/Percussion/PercussionEngine.cpp`):

- **Bank**: `kStrokes x 3 dynamic layers x 3 round-robin`. A conga slapped hard
  is brighter and shorter, not the same recording with more gain - and a part
  where every stroke is bit-identical reads as a machine no matter how good the
  timing is. Recordings come from `Assets/Percussion/` (VCSL, CC0); missing
  assets fall back to synthesis, and `recordedStrokeCount()` exists so tests can
  assert that fallback did *not* happen silently.
- **Attack compensation.** A shaker is not a click: measured on the bundled
  library its energy needs 10-13 ms to get where a slap gets in 2. So attacks
  are measured per sample (`measureBankAttacks`), the tracker runs the clock
  ahead by `attackLeadSamples()` (the slowest in the bank), and every stroke is
  then **held back** by `bankAttackLead - s.attack` so they all land together.
  Never hard-code these numbers: they are a property of the recordings.
- **Voices**: `kVoices = 16`. A stolen voice is faded out over a few
  milliseconds, never switched off - cutting a sounding grain is a step, and a
  step is a click. `hardSteals()` counts thefts from still-sounding voices and
  the tests assert it is zero; non-zero means the pool is too small for the grid
  being played.
- **Style changes commit on the next quarter**, so one beat can never contain
  two different parts.
- Real-time: no allocation, no locks, no I/O in `render()`.

Two `render()` traps that have each cost a bug:

- **The bar count runs on every pulse the clock emits**, including the ones
  nothing is played on, and *above* the audible check. It used to sit below it,
  so the count stopped dead while the part was muted or waiting to come in.
  Measured with the part muted for two bars, the phrase came back two bars
  behind the song and stayed there - the fill landed mid-phrase, which a
  listener hears as the percussion having lost the form.
- **Timing comes from `tick.tempoBpm`, not from a cached or displayed BPM.** The
  retrigger guard used to be 82% of a pulse derived from the displayed BPM,
  which is 120 until the tracker locks - so above ~145 BPM the guard was longer
  than the real pulse and swallowed every second stroke. A `reanchored` tick
  discards pending (not-yet-sounded) voices; tails already begun are left alone.

## 11. Editing checklist

Adding or changing a style:

1. Add the enum in `Source/Core/Types.h` (+ `toString`).
2. Write `barA/B/C/D` and `fill` as `Hit{step, stroke, velocity}` arrays. Keep
   step 0 free of congas. Velocity is the design: >= 0.72 survives every
   dynamic, below that it thins.
3. Write `shaker[16]` as a bar-long figure - state and answer, not four
   identical beats.
4. Set `accent[4]` from where *this* music puts its weight.
5. Set `ghostChance` (0.00 twoOne ... 0.45 funk) and `fillEveryBars`.
6. Add the entry to `kStyles[]` in the same order as the enum.
7. `StyleDetector` only chooses between the rhythm classes it can measure - a
   new style is not automatically choosable, and `twoOne` is never chosen at all.

Then **listen to it**:

```bash
cmake -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --target VPRender
./build-host/VPRender_artefacts/Release/VPRender \
    --style dance --bpm 124 --bars 8 --click --out dance.wav
```

`VPRender` (`scripts/render_groove.cpp`) drives the real `PercussionEngine` from
a real `TempoFollower` at a fixed tempo - same bank, same tables, no analysis,
no network. `--click` puts a quiet woodblock on each quarter, loud on the one,
so a claim like "the congas never play the first quarter's down-stroke" can be
checked by ear instead of by reading tables. Some of what this engine does is a
musical decision, and the only review a musical decision has is somebody
hearing it.

And **measure it**: `VPTiming` (`scripts/probe_timing.cpp`) measures where the
stroke actually lands in the rendered audio - per-articulation attack, and the
rendered onset against the notated beat. A stroke can be 12 ms late with a
perfect clock.

Always also run `./scripts/run-tests.sh`.

## 12. Related

- `Source/Loops/` + `docs/RECORDED_LOOPS.md` - the recorded-loop percussionist,
  behind `VP_ENABLE_RECORDED_LOOPS` (off by default). The clock is never the
  loop's: the loop is pulled onto the grid, corrected by rate, never by a jump.
  Loop stems are not thinned by `setSubdivision`; only synthesized
  `GrooveEngine` events are.
- `docs/SMART_PERCUSSION.md` - gap analysis and the plan this came from.
- `docs/AUDIO_ENGINE.md` - the signal path around all of this.
- For anything about *when* a stroke happens rather than *what* it is, use the
  `realtime-tempo` skill.
