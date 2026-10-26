---
name: percussion-patterns
description: How the shaker and conga parts are written and played in VirtualPercussionist - the 16-step groove tables, the nine stroke articulations, per-style shaker weighting and accents, the 8-bar phrase, swing/humanize/ghosts, band dynamics thinning, and voice/attack handling. Use when touching Source/Percussion/ or Source/Loops/, adding or editing a groove style, changing what the shaker plays, or when the part sounds mechanical, too busy, or lands in the wrong place.
---

# Percussion patterns: what the player actually plays

Read this before editing anything in `Source/Percussion/`. The tables are
musical decisions with reasons written next to them; the guards below exist
because those decisions were lost once already.

## 1. The grid

- Everything is on a **sixteenth grid**: `GrooveEngine::kStepsPerBar = 16` (twelve of
  them used in 3/4, see section 12b).
- The clock always emits **4 pulses per beat** (`kClockPulsesPerBeat` in
  `BeatTracker.cpp`); the subdivision setting chooses how dense the groove is,
  not the clock. `PercussionEngine::render` maps a pulse onto the sixteenth grid
  whatever the resolution is:
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
| `clap` | the backbeat - hands only, no drum. See section 7. |
| `cembaloDown` | tambourine, struck hit - the shaker's job, another sound |
| `cembaloUp` | tambourine shake - the jingles on the return, mirroring `shakerUp` |
| `triangleOpen` | ringing triangle - not a fifth groove voice; a FEEL knob assignment |
| `triangleClosed` | stopped triangle #1 - battere / first of a muted pair |
| `triangleClosed2` | stopped triangle #2 - off-beat 16th / second of the pair |

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
| `dance` | pop-dance hook: stopped low/open answers on the off-eighths, with one closed slap moving through the phrase. No open low tumba against the four-on-the-floor kick; sparse ghosts (`ghostChance` 0.12) keep it programmed and clean. |
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
   means eighths. This thins shaker/cembalo; congas keep their authored
   sixteenth figure, fills and ghosts on every Misure setting. Their swing
   also uses that native sixteenth grid (TODO158 fixes a wrong dependency on
   Misure). The no-conga-on-step-0 guard still wins. Recorded WAV loop stems
   do not pass through this event filter.
4. **`setShakerNatural` is the one exception, and only for the shaker (and
   cembalo, which is the same part).** Off by default: 1/4 / 1/8 / 1/16 stay
   exact grids. On, it lets through authored `shaker[16]` values the
   subdivision had dropped - sixteenths on an eighths part, off-eighths on a
   quarters part - with chance `0.22` / `0.30` scaled by intensity and
   dynamics (`kNaturalEighthChance` / `kNaturalQuarterChance` in
   `GrooveEngine.cpp`). It uses the table, never invents a busier one, and
   the test asserts it does not become a full finer grid. Congas are
   untouched. NATURALE is its own toggle in STRUMENTI, not a fifth
   subdivision. See docs/TODO.md item 11.

`spec.accent[4]` scales by quarter and belongs to the **style**: marcha and pop
lean on the one (1.00, 0.86, 0.93, 0.88), rock leans on 2 and 4 with the drummer
(0.94, 1.00, 0.92, 1.00), a four-on-the-floor bar is nearly even (dance: 1.00,
0.95, 0.97, 0.95). A single global contour favouring beat one silently cancelled
the rock backbeat, which is why this is per style.

## 7. Cembalo and clap: two more voices

Two more strokes (`Stroke::cembaloDown`/`cembaloUp`, `Stroke::clap`), each with
its own enable (`EngineSettings::cembaloEnabled` / `clapEnabled`) and volume
(`cembaloVolume` / `clapVolume`), both **off by default** - turning either on
is the listener's choice, not a change to how an existing install already
sounds. See `docs/TODO.md` item 10.

**Cembalo is the shaker's job with a different sound**, not a new pattern.
`GrooveEngine::eventsAt` (`GrooveEngine.cpp`, right after the shaker block)
reads the *same* `spec.shaker[step]` table, the same `subdivisionAllowsStep`
thinning, the same `accent[4]`, the same swing/humanize/dynamics helpers - the
only difference from the shaker block is which `Stroke` it writes and which
`bool` switches it (`cembaloOn` vs `shakerOn`). Shaker and cembalo can both be
on at once (two timbres on one part), just one, or neither; the
no-conga-on-step-0 invariant (section 3) does **not** apply to it, same as the
shaker - a cembalo on the pulse is meant to be there. **Cembalo here means the
tambourine**, not the cymbal the word points at in a dictionary: it sounds from
the VCSL *Tambourine* takes (`cembalo_down*.wav` / `cembalo_up*.wav`), down the
struck hit and up the shake, which is how the instrument is played in eighths -
hand on the pulse, jingles on the return. Cut to 0.26 s and 0.16 s so a stroke
cannot ring over the next eighth. `PercussionEngine::synthesizeCymbal` is the
fallback for a build without `Assets/Percussion/`, not what you normally hear.
The shakes need the shaker cutting path, not the struck-sample one - see
`Assets/Percussion/ATTRIBUTION.md`, which also records what was tried and
measured before.

**Clap does not follow the shaker's table at all.** It is a fixed pattern -
steps 4 and 12, the backbeat quarters (2 and 4) - written directly in
`eventsAt`, not looked up per style. It also carries a second gate no other
voice has: `clapOn && barTrustedFlag`. `barTrustedFlag`
(`GrooveEngine::setBarTrusted`) is set once per block from
`BeatTracker::Output::barTrusted`: the listener's lock is trusted outright;
during a post-cut re-entry window the clap is muted; otherwise the downbeat
histogram has to name beat zero with a coming-in margin. That trust latches
for the song and is often already on when the part comes in, so a wrong
automatic guess **does** put the clap on the song's 1 and 3: measured against
the teacher, 28% of bars in the first 5 s after entry, 15% in the first 20 s,
4.5% later. Only a count off by one or three quarters does it; half a bar
leaves the clap on 2 and 4. So the engine adds a third gate
(`VirtualPercussionEngine::processBlock`, `kClapSteadyBars = 6`): the one must
also have stood still for 6 bars since the part came in and since the last
rotation by an odd number of quarters (`BeatTracker::Output::barOddRotations`);
a one the listener declared skips the wait. First 20 s 15% -> 5.9%, later
4.6% -> 4.3%; the UI says "clap in attesa dell'1". The snare's 150-600 Hz band
folded on the bar cannot tell which pair is the backbeat (bass and voice live
there too). See `docs/TODO.md` items 2 and 119.

Both voices' output gain is looked up by stroke identity in
`PercussionEngine::render`'s voice-mixing loop, next to `shakerVolume` /
`congaVolume` (see item 9): `shakerDown`/`shakerUp` → `shakerVolume`,
`cembaloDown`/`cembaloUp` → `cembaloVolume`, `clap` → `clapVolume`, anything
else → `congaVolume`.

`GrooveEngine::kMaxEvents` is 6, not 4: a single sixteenth can now carry
shaker + cembalo + clap + a conga table hit + a conga ghost, with one spare.

### FEEL sound assignment

The four knobs are slots (enable, volume, colour). Groove is keyed by
the assigned `KitSound`, not by knob index: congas on any knob play the
conga tables, triangle never inherits a shaker/conga figure. Volume stays
on the knob (`Voice.volumeFrom` / `GrooveEvent.part`). The four ints
`shakerSound` / `congaSound` / `cembaloSound` / `clapSound` are persisted
in the existing PropertiesFile.

The long-press picker is a **unique set**. Walk the four-slot map; any
instrument id already sitting on a knob — including this one — is
omitted. The list is only unused families. There is no selected cell;
tap outside keeps the assignment. `assignKitSound` refuses a duplicate.
Reload uniquifies older prefs (first occupant keeps the family, later
slots take a still-free id). There is no empty/unassigned `KitSound`.

The menu is a **vertical stack on the held knob**, not a bar across the
screen: above the disc if that fits with an 8 px gap, otherwise below.
Cell width is **twice** (longest unused label + 6 px each side). Type is
12 px Avenir Next Bold, white on that instrument's knob-interior pastel.
Height stays a Misure chip (22–36).

Triangle is picker-only, and it does **not** inherit the assigned part's
table, swing or subdivision thinning. `GrooveEngine::eventsAt` writes a
16th ostinato for every slot assigned `KitSound::triangle`, even when
Misure is 1/8 or 1/4: the clock already visits every sixteenth
(`BeatTracker` keeps 4 pulses per beat; `setSubdivision` only thins the
other voices).

Every 4/4 bar is four identical beats, each `. . _` (two stopped
sixteenths then the long one). The 16-step row (`delayBeats` 0):

```
step  0 1 2 3   4 5 6 7   8 9 10 11   12 13 14 15
      1 2 O .   1 2 O .   1 2 O  .    1  2  O  .
```

`1` = `triangleClosed` (stopped #1) — **battere only** `0, 4, 8, 12`.
`2` = `triangleClosed2` (stopped #2) on the "e". `O` = `triangleOpen`
(short tap + wet tail, dry 65 ms / wet ~195 ms / RT60 220 ms) on the
`"&"` (step 2 of each quarter). `.` = rest on the "a" so the tail can
speak. Do **not** put open on the battere and do **not** layer O+1
there. Closed #1 ~90 ms dry; closed #2 is the second mute
(`triangle_closed_b` when present). A later mute still chokes the ring
(`chokeTriangles`).

Required recordings, when they land: `Assets/Percussion/triangle_open.wav`,
`triangle_closed.wav` (stopped #1) and `triangle_closed_b.wav` (stopped #2).
Playback is an octave up (`kTriangleTune = 2`). Until then
`synthesizeTriangle` is the fallback (modes 5.3-12.5 kHz, beater at sample
0). UI chip/knob fill is turchese `0xff2ee8d0`.

### Switching a knob mid-song (2026-10-23, docs/TODO.md item 150)

`PercussionEngine` keeps each knob as asked (`knobWanted`, set from
`EngineSettings` every block) and as playing (`knobOn`, what `GrooveEngine`
is told). **On** waits for the next bar line while the part sounds - a player
joins on the one - and is immediate when nothing sounds (before START, waiting
to come in, before the first pulse). **Off** is immediate; a knob carrying
shaker or cembalo first **shakes out**: 24 32nds on the grid (two per pulse,
three quarters), 1.4 dB down each (`kOutroStrokes`, `kOutroStartVelocity`
0.70, `kOutroDecay` 0.85; 16 at 2 dB was heard and asked a little longer), shaker alternating down/up, cembalo all `cembaloUp`. Below `pick`'s
0.08 gain floor `trigger(..., level)` keeps fading. Congas and clap stop dead.
Hear it: `VPRender --no-congas --click --cembalo-on-at 5.5 --shaker-off-at 17.5`.
Checked by `VPTests --percussion` (knob-join, shake-out). Numbers chosen by
reasoning; the listener's ear decides.

## 8. Feel: swing, humanize, ghosts

**Swing** (`humanDelay`) is a **warp**, not a late off-eighth - and what it warps
is *the grid being played*. The span is the **beat** on an eighths part and the
**eighth** on a sixteenths part (`swungWithinSpan`); at full amount the span's
halfway point moves to two thirds of it (`kFullSwingBeats = 1/6`) and everything
either side rides the same stretch.

So on eighths the "&" goes to 2/3 - a shuffle - while on sixteenths **the "&"
does not move at all** and only the sixteenth inside each eighth goes late:
0, 1/3, 1/2, 5/6.

That distinction is measured, not assumed. Against an Afrobeats shaker reference
at 106 BPM (24 strokes per position, cluster spread 1-8 ms) the mean error was
20.8 ms straight, **26.6 ms warping the beat**, 7.6 ms warping the eighth: the
eighths there sit at 0.516 and 0.484 of the beat and hold still, and only the
sixteenth between them moves, to 57.4% and 65.7% of its eighth. Warping the beat
fitted that music *worse than no swing at all*, because the one stroke it moves
is the one the music holds still. If a swing change ever needs re-checking,
`VPTests --swing` asserts all four combinations and, separately, that the
off-eighth does not move on a sixteenths part.

**In the UI it is a switch, not a knob** - SWING in STRUMENTI, off by default,
next to NATURALE (`MainComponent::applySwing`, docs/TODO.md item 7). A
percussionist does not play 37% of a shuffle, so there is one value and it is
`kSwingOnAmount = 0.65`, **not** the full triplet: the reference lands its
sixteenth at 61.6% of the eighth and 0.65 renders 63.0%, inside the reference's
own eight-point spread, where 1.0 renders 68.9% - above all of it. The value was
chosen by ear from three renders, and it is not a fit: the two halves of the
reference's own bar disagree by more than the number being chased. The engine still takes a 0..1 amount
because the warp is written in terms of it, and `setSwing` still commits on the
next quarter like the style does - one beat can never contain two timing maps.

The geometry is asserted (`swing-grid` in `VPTests`): a sixteenth part
lands on **0, 1/3, 1/2, 5/6** at full swing. An eighth part warps the
whole quarter. Congas always use their authored sixteenth grid, independent
of Misure; shaker/cembalo use the selected grid. `delayBeats` stays nonnegative.

The recorded bank has no swung takes: past `LoopBank::swingTolerance` (0.18) it
refuses, so SWING on always falls back to the stroke engine. That is a handover,
not a silence, and SETUP says which path is sounding.

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

## 9. Dynamics: playing *less*, not just quieter

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
- The drift guard is the other silence (`docs/TODO.md` item 118): the tempo
  light red for 3 s without a break stops new hits and fades the complete
  percussion bus (tails included) in 40 ms; it comes back with a 40 ms fade on
  the first bar line after 1 s out of red. The faded signal is also the echo
  reference. Clock and analysis keep running. Output only, like the stand-down;
  off in FISSO and with PAUSA SE FUORI off. The UI reads FUORI TEMPO - IN PAUSA.

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

## 10. Choosing the style from the music

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

## 11. From event to sound

`PercussionEngine` (`Source/Percussion/PercussionEngine.cpp`):

- **Bank**: `kStrokes x 3 dynamic layers x 3 round-robin`. A conga slapped hard
  is brighter and shorter, not the same recording with more gain - and a part
  where every stroke is bit-identical reads as a machine no matter how good the
  timing is. Recordings come from `Assets/Percussion/` (VCSL, CC0); missing
  assets fall back to synthesis, and `recordedStrokeCount()` exists so tests can
  assert that fallback did *not* happen silently.
- **The quinto plays stopped, the congas open (2026-09-14).**
  `PercussionEngine::trigger` passes every stroke through `stoppedConga`, which
  maps only `slap`->`slapClosed` - the highest drum is the African crack with no
  ring - and leaves `tumba`/`open` as their classic pop open tone. The tables
  still name `slap`; that is the *intent* of the figure, and the mapping is what
  keeps the quinto crisp. Change the mapping in one place, never the tables. The
  bank is read a perfect fourth above the samples' natural pitch
  (`kDrumTune = 2^(5/12)`), so tumba/open/slap are 185/220/288 Hz.
- **The procedural conga bank is a real fallback instrument, not a sine-wave
  placeholder (2026-09-14).** `synthesizeDrum` matches the recorded set's
  139/165/216 Hz natural pitches before tuning, integrates the falling pitch
  into continuous phase, and excites three inharmonic membrane modes plus a
  separate hand transient. Open, stopped and slap articulations weight those
  components differently. All synthesis happens in `prepare()`; `render()`
  still only reads and sums the completed samples. `VPRender --procedural`
  forces this bank for listening even when recordings are embedded.
- **A source take is not automatically three takes.** The bundled library has
  one medium recording for shaker and conga, and the derived heel/toe/muff and
  stopped strokes also begin from one source. `layerFromRecording` gives each
  pre-built round-robin slot a small deterministic colour and sustain
  fingerprint, modelling the hand landing at a slightly different place. It
  does not skip the transient or change playback rate: tuning and contact time
  stay fixed. The work happens in `prepare()`, never in `render()`. The focused
  regression is `VPTests --percussion`; on the current bank the relative
  difference between consecutive medium hits is 0.000470 for shaker and
  0.000171 for open conga (both were exactly zero before).
- **Attack compensation (TODO162).** The clock keeps the nominal articulation
  reference (`bankAttackLead`, bounded25ms); it does not move when per-take
  calibration changes. The prepared samples carry an audible attack measured
  through the existing trim/limiter at the centre of their dynamic layer.
  Voices wait `max(0, bankHeardTarget - s.attack)`. The target covers the slowest
  prepared layer/take, bounded by the existing25ms lead plus6ms common hold.
  On this bank: lead17.854ms, target26.583ms,2.729ms extra heard margin. All this
  reads percussion assets in prepare(), never future input music. A separate
  nominal control bus keeps epochs/AGC independent of audible calibration;
  the echo reference remains the actual emitted waveform after MASTER.
  **Triangle votes for neither the lead nor the audible target.** The first synthesised open
  peaked 15-25 ms in (`kMaxAttackLeadSec = 0.025`), which raised
  `attackLeadMs()` from the shaker's 10-13 ms toward the ceiling and put
  the whole kit slightly *in anticipo* of the band. Triangle still gets a
  measured `.attack` and the same hold against the kit lead, with a sharp
  beater at sample 0 so the audible open sits on the quarter. Do not fix
  early drums by moving the clock.
  The click bench still sits early after that lead is subtracted (+9.1 ms at
  120 BPM). `kHeardEarlyHoldSec` (12 ms) delays every stroke by that
  remainder plus the few milliseconds the ear still heard early after 8 ms.
  It is not a tempo change and it does not change how far a slap waits for
  a shaker. The clock is not moved.
  **Now 6 ms (2026-10-05, docs/TODO.md item 93).** The listener heard every
  stroke slightly late. 12 ms was set on 120 BPM alone and the clock has since
  moved later: the click residual is now -2.6 ms mean over 78-156 BPM (was
  +1.8). On the rendered part (`VPTrack --player --quarters --out`), against
  each song's drums dated by `onset_fit.onsets` corrected by its -16.7 ms
  bias on a sharp attack (synthetic bench), the shaker's 80% point sat
  +5...+9 ms late on the band's live sends; with 6 ms it is about +1. The
  `--percussion` triangle windows are read from the stroke's own start, so a
  hold change no longer moves what that test listens to.
- **Voices**: `kVoices = 16`. A stolen voice is faded out over a few
  milliseconds, never switched off - cutting a sounding grain is a step, and a
  step is a click. `hardSteals()` counts thefts from still-sounding voices and
  the tests assert it is zero; non-zero means the pool is too small for the grid
  being played. Triangle open and closed share that steal path so a damped hit
  actually stops the ring.
- **Style changes commit on the next quarter**, so one beat can never contain
  two different parts.
- A style change does not own or restart the clock. `VPOps --style-change`
  measures it against an untouched control pass: at 118 BPM the direct-file
  path is sample-equivalent at the phase readout (`-0.00 ms`) and the acoustic
  iPad path differs by 1.93 ms, below the probe's roughly 10 ms resolution.
  If a new part *sounds* displaced while those figures hold, first compare its
  authored accents and empty steps; a different rhythm is not a moved grid.
  `--file`, `--mixer` and the default speaker path select the three inputs.
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

### The loop congas (2026-09-28)

The conga bank is no longer VCSL. The percussionist picked SampleFocus
"Salsa Congas Loop - Dance 3" (licence note in Assets/Percussion/ATTRIBUTION.md:
not CC0, fine on his own device). `scripts/prepare_loop_congas.py` cuts it:

- Only the mid conga (~325 Hz) and its slaps/muted strokes; the loop's lowest
  drum is left out on request and `tumba*` is the mid conga too.
- Every event, ghosts included, ends the file before it; no open tone rings
  clean past ~120 ms in the loop, so each is continued to the engine's 300 ms
  cap by its own fitted ring (three partials, one decay, 10 ms crossfade).
- Files start 3 ms before the first-strike peak with a 0.5 ms fade-in: the
  drum-strike check wants the peak within 4 ms; the open tone's hand takes
  ~5 ms to reach it.
- `heel`, `toe`, `muff`, `slap_closed`, `tapado` now have real takes.
  `buildBank` loads a stroke's own stem first and `layerFromRecording(...,
  ownTake)` then skips the artificial `extraDecay`; without the files it still
  derives them from open/slap as before.
- Loop hit 11 (palm, then body 30 ms later) is not used: heard 10 ms after
  every other stroke, past what attack compensation carries. Loop hit 18 (a
  light, spiky slap) is not the `_med` slap: peak-normalised it sat 6 dBA
  under the open tone at medium velocity; the derived medium layer is 1.3 dB.
- `kDrumTune` is 1.0 (natural pitch). The synthesis fallback keeps its own
  `kSynthDrumTune` (2^(5/12)).
- Historical (2026-09-28): `kCongaLevel` 1.778 (+5 dB) on the congas' knob: A-weighted over the loudest
  150 ms at velocity 0.9 the congas were ~-24 dBA against -21 cembalo and
  -18.5 clap; now ~-19. A soft clip (tanh above 0.80) at the end of
  `render` rounds peaks instead of clipping them; it also covers stacked
  shaker+conga+clap, which could already exceed full scale.
- `kShakerLevel` 1.259 (+2.0 dB), `kCembaloLevel` 1.334 (+2.5 dB), `kClapLevel`
  1.679 (+4.5 dB) follow the *sound* (`kitSoundForStroke`), not the knob
  (2026-10-05). Remeasured K-weighted, loudest 50 ms of each stroke, each voice
  alone at a full knob, dance/pop/rock/samba at 120: shaker 0, cembalo -0.5,
  congas +2.0, synthesized clap -2.5 dB; the clap also sits on the snare's 2
  and 4 and had to stay at the top, and the percussionist asked for all four
  alike. Now all four within 0.1 dB on average (+-1.4 dB between styles, the
  patterns differ). All four together reach the soft clip on 0.39% of samples.
  `VPRender --raw` skips the peak normalisation so voices can be compared.
- Historical measurement: attack spread identical to that HEAD (25.9-28.6 ms;
  attribution to clap was not established), drum-strike 2.0-3.0 ms,
  `VPTests --percussion` 17/0, `--leak` MIXER rows and seam unchanged. The iPad
  speaker path is worse (no-leak rows, and the canceller-on/off seam through
  the room): the new congas pass the modelled speaker's 260 Hz high-pass where
  the old tuned ones mostly did not. Accepted by the percussionist: mixer and
  loaded files only for now.
- The shaker is cut from `soft-bright-shaker_128bpm.wav` by
  `scripts/prepare_loop_shaker.py`: its accent is `shaker_down`, its three
  distinct light strokes are `shaker_up`, `_b`, `_med` and `shaker_down_med`.
  Files start at 35% of the stroke's envelope peak minus 1 ms (the shaker
  convention) and keep their own tail - no imposed exponential. Take 3 as
  `shaker_up` came out 2.5 ms late in the attack-align check (its broad swell
  crosses 80% differently once the soft clip rounds it), take 1 is used.
- Default figure is DANCE (`EngineSettings::grooveStyle`), with a one-time
  preference migration (`grooveDanceDefault`) so an old saved MARCHA does not
  keep it.

### Conga balance and routing (2026-10-25, TODO158)

`kCongaLevel` is now **3.274**, +6 dB from 1.641, at the listener's request.
It follows `KitSound::congas`, like the other sound trims; the slot contributes
only its fader. Previously reassigning sounds applied the conga trim to the
wrong family. Three exact waveform tests cover conga, shaker and triangle
on all four equal-level slots. Six styles, eight raw bars each: congas
**+5.21 dB RMS** after existing soft limiting, other default voices **0.00 dB**.
This is an electrical level measurement, not a perceptual balance judgement.
At full volume up to 0.227% of conga samples reach |x|>.999; the existing
0.80 soft clip stays in place. The listener confirmed “A tempo e congas più presenti” on the physical iPad
microphone/speaker trial, with music from the Mac at 20–30 cm. This covers
that listening setup, not every PA or a new loudness calibration.

The TODO158 attack check measured **3.04 ms** against the unchanged2 ms bound,
between shakerUp and triangleClosed, not clap. TODO160 strengthens this check
to all three round-robin takes and measures the40 ms attack window from each
voice's own start (the same window as production calibration), excluding its
variable hold. The80% fraction and2 ms acceptance bound stay unchanged. This
is still an explicit red; do not claim the broader RR spread is a newly
introduced production regression.

### TODO160 — calibration experiments rejected (2026-10-25)

Per-layer/per-take calibration through the existing trim and limiter at the
centre of each velocity band, with `max(0, lead + commonHold - attack)`, reached
1.15 ms across all48 stroke/RR pairs at velocity0.9. Its17.854 ms clock lead,
gains and patterns were unchanged. The earlier variant raising the clock
lead to24.229 ms was already rejected on the325-run bank. The stable-lead
variant also fails the real-song gate: the changed own-output witness shifts
an input epoch by one callback; re-priming make-up from that block then alters
the network's input. Isolated no-calibration controls restore exact primary
MIXER/BRANO traces. Consequently **all production calibration changes were
removed**, not retained because the isolated attack test was green.

A global continuous-epoch/safe-headroom make-up-retention candidate was also
rejected: it only partly recovers the lost MIXER correct share and does not
remove the BRANO common-correct phase regression. Fix this analysis/output
coupling before changing attack timing again. No per-song offset or special
BPM rule is an acceptable workaround. The stronger triangle test injects an
artificially late beater and directly proves that it cannot move the kit lead.
The existing independent raw drum-strike check remains.

Two later isolated TODO160 candidates also preserve both make-up envelope and
gain across a safe-headroom continuous epoch, or only a rhythm-arrival epoch.
They pass26/0 attack checks and restore the BRANO phase gate but still lower
the MIXER regression witness97.1483→96.7681% /96.5779% correct respectively.
Both were rejected before any whole-bank claim; main sources never included
them. Artefacts:continuous*/rhythm-continuity* in vp-complete-20261025. Gain
retention alone does not remove output-to-epoch decision coupling.

`vp-complete-20261025/listen/` contains **rejected-candidate** audition files.
The listener replied “Non posso ascoltare adesso”; no new physical-iPad trial
or approval was obtained. Production sounds remain the previously verified
TODO158 bank and conga gain.

### TODO161 — fast-input-envelope priming rejected (2026-10-25)

Another isolated candidate combined the stable-lead layer/RR calibration with
continuous-source epoch priming from max(current block peak, levelFast times
inputTrim). Percussion checks26/0, but the held-out MIXER witness loses correct
tempo97.1483→0%; BRANO keeps95.2471% tempo but common-correct p90 increases
28.266→30.269 ms and bar100→96.7742%. Microphone passes. Reject immediately,
restore all Source/Tests byte-for-byte to the initial optimized TODO160
snapshot. Neither a fast input envelope nor a single callback peak is a
substitute for preserving the network's validated operating level. No new
listening approval, full-suite success or whole-bank result is claimed for
this rejected candidate. Artifacts: vp-downbeat-attacks-20261025.

A still smaller raw-only per-take/layer calibration (no trim/limiter in the
measurement, clock lead unchanged, common hold available for late takes)
reduces48-case spread17.98→2.79 ms but repeats the MIXER97.1483→96.5779%
regression and BRANO common-correct phase regression. Also rejected; the
post-limiter measurement is not required to trigger the output/epoch coupling.

### TODO162 — separate control timing from heard attacks (verified; listening pending)

A per-articulation nominal control bus now uses the same shared voice renderer
and sample bank as the calibrated audible bus, with independent voice/reverb
state. Actual output, after MASTER, remains the echo waveform. Guard fading
scales both buses. Control is pre-MASTER and unaffected by per-take timing.
HybridPercussionRenderer must carry and blend this bus too: the current CMake
build enables VP_ENABLE_RECORDED_LOOPS, although older comments claimed off.
The control contains actual recorded-loop share plus nominal single-stroke
share. No second copy of the sample bank; all allocation is in prepare().

Stable17.854ms lead,6ms common hold and48-case1.15ms attack spread. Frozen
controlbus2 bank325:275 exact,2 MIXER and48 microphone numeric differences,
zero rejected gates, zero lost/gained correct beats or extra jumps/surges/skips.
Maximum common-correct phase delta0.01684ms MIXER /0.11828ms microphone.
Focused percussion27/0 includes exact nominal-control equality with changed
heard audio; loops59/0 includes actual loop share and zero callback allocation.
The later full-wake TODO162 closure extends soft/medium headroom:48-case
spread12.625→0ms at velocity1/6,13.521→0ms at0.5,17.979→0.77ms at0.9;
percussion29/0, with original2ms/80%/40ms bounds. Six additional edge probes
include0.08..0.67, maximum1.92ms. At full velocity1.0 the stricter diagnostic
still reads5ms (before17.979): do not claim universal2ms calibration at every
gain. Representative layer centres/gain1 are the calibration domain.

Final projection bank325:264 exact,61 numeric differences (4 MIXER,3 BRANO,
54 microphone), zero rejected gates, zero correct tempo beats lost, zero
previously correct downbeats lost and one gained. Common-correct phase delta
0ms direct /max0.00783ms microphone. The causal prediction projection also
fixes two no-return/128-frame microphone canceller failures (51/0 focused).
Host CPU cost is recorded explicitly in TODO162; no allocation in2000 hybrid
callbacks. Signed iPad build and clean install pass; the user cannot unlock
it now, so the physical sound/thermal check and musical listening remain.

### TODO163 — gain-specific attacks, measured (2026-10-26)

Tests were explicitly authorized. The53 focused checks pass; the9-velocity,
3-fader,16-articulation,3-RR matrix measures0ms spread, including velocity1.0
(former5ms). The attack-only325 bank has323 exact/2 changed, zero gates lost.
Layer-centre calibration alone cannot cover fortissimo: the limiter changes
the relative80% attack point with gain. The new trigger path uses the cached
raw attack if the hit's40ms asset window stays below the limiter knee;
otherwise it measures that same immutable percussion asset through soften at
the current velocity/slot/level gain. Two bounded passes, no scratch vector,
allocation, lock, input analysis or I/O. This is kit-asset work, not a read
ahead of the input music. Later fader moves and stacked voices sharing the
limiter are not predicted: the isolation matrix is not proof of mixed-output
alignment. Humanized stereo and musical listening remain in the final checks.
The audible target also covers each non-triangle raw attack within the same
25ms+6ms cap; nominal control timing and clock lead remain separate. There is
no per-title/BPM correction and no change to gains or patterns.

Checks retain the unchanged80%/40ms/2ms bounds. The nominal-control fixture
explicitly forces rawAttack/controlAttack and bypasses the clipped branch in
the reference engine. Measure CPU, including callback peaks at clipped-hit
starts, before accepting this exact gain-dependent computation.

## 12. Editing checklist

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

## 12b. METRO 3/4 (2026-10-21, docs/TODO.md item 128)

`EngineSettings::beatsPerBar` (4 or 3, the METRO pill next to SWING) and
`grooveStyle3`. A 3/4 bar is **twelve sixteenths** (quarters 0/4/8, off-eighths
2/6/10); `PercussionEngine::render` already maps `barBeat * 4 + ...`, so with
the clock counting three nothing past step 11 is visited, and `eventsAt`
refuses `step >= 4 * beatsPerBar` anyway.

- The 3/4 parts are **their own table**, `kStyles3[GrooveStyle3]` (VALZER,
  BALLAD, SEMPLICE), chosen by `specFor`. They are not `GrooveStyle` values on
  purpose: every 4/4 style, its tests, the leak bench rows and the automatic
  chooser stay exactly as they were. In 3/4 the style list shows only these
  three and AUTO is not offered (the chooser measures 4/4 rhythms).
- **No clap in 3/4** - the percussionist's call: there is no backbeat pair.
  The clap block needs `beatsPerBar == 4`; the picker does not offer CLAP and a
  knob already on CLAP reads as off.
- Same rules as 4/4: no conga on step 0 (guard), skeleton at >= 0.72, the
  A B A C D B A fill sentence, a bar-long shaker figure, `accent[3]` used.
- VALZER = um-pa-pa: open tones on 2 and 3 (the second lighter, cracked in
  bar B), heel/toe in the gaps. BALLAD = mostly space: tumba after the one,
  a lift on 3. SEMPLICE = pulse only, two drums, no ghosts.
- Triangle, swing, NATURALE and dynamics need nothing: they work per quarter.
- `setMeter` commits with style and swing on the next quarter.
- Listen: `VPRender --style valzer|ballad|semplice --bpm 150 --click`.
  Checked by `VPTests --meter`. **Written by reasoning, not yet heard.**

## 13. Related

- `Source/Loops/` + `docs/RECORDED_LOOPS.md` - the recorded-loop percussionist,
  behind `VP_ENABLE_RECORDED_LOOPS` (ON in the current CMake build; without a bank the single-stroke fallback runs). The clock is never the
  loop's: the loop is pulled onto the grid, corrected by rate, never by a jump.
  Loop stems are not thinned by `setSubdivision`; only synthesized
  `GrooveEngine` events are.
- `docs/SMART_PERCUSSION.md` - gap analysis and the plan this came from.
- `docs/AUDIO_ENGINE.md` - the signal path around all of this.
- For anything about *when* a stroke happens rather than *what* it is, use the
  `realtime-tempo` skill.


Latest TODO164 full default suite797/0;325 paired probes lose no musical
gates. TODO163 gain-specific calibration closes the old fortissimo5ms residual
(53/0, isolated attack spread0ms). Actual output guard now prevents already-red
first notes; nominal control rendering/fade remain independent.52 audible
pairs remove38 red openings to0. Physical listener confirms cleaner entrance,
but Canzone intro phase remains imperfect; do not call it fully aligned.
