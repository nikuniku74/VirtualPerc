# Audio Engine

## Callback (real-time)

```
getNextAudioBlock
  copy device input into a scratch buffer (never clear first)
  mix assigned inputs → mono analysis
  NeuralBeatTracker.feed(mono)     // SPSC, even after STOP
  BeatTracker reads BeatHypothesis // seqlock
  TempoFollower.advance(numSamples)
  if armed and state in {FOLLOWING, LOW_CONFIDENCE, RECOVERING}
      PercussionEngine.render(shaker hits on grid crossings)
      // or TimeStretchEngine if a loop kit is loaded
  else
      silence
  apply percussion * master volume
```

No allocations, file I/O, logs, mutexes, or UI in this path. Buffers are reserved in `prepare()`.

## Clock

`beatPhase` ∈ [0, 1) is the position inside the current quarter note.

Each sample: `beatPhase += tempo / 60 / sampleRate`.

Tempo is a smoothed `currentTempo` chasing `targetTempo` from the neural hypothesis (or tap). Phase error from the decoder is applied with a clamped PLL gain. Changing tempo never wraps `beatPhase` to 0.

Bar phase assumes 4/4: `barPhase = (beatIndex % 4 + beatPhase) / 4`.

## What the part plays

`GrooveEngine` decides the strokes; `PercussionEngine` only sounds them. Everything is on a **sixteenth grid** even though the loud strokes sit on eighths, because the quiet half of a marcha — the heel and toe filling the gaps — is what a listener hears as a player rather than as a pattern. The clock therefore always runs at sixteenths; the user's subdivision setting selects how dense the *shaker* is, not the clock.

### The stopped strokes

A conga is played as much with the hand that stays on the head as with the one that leaves it, and until now the bank had only the leaving kind: every loud articulation rang. Two sounds that nothing plays are two sounds the app does not have, so these went into the **figures** and not only into the bank.

- **`slapClosed`** — the crack with the hand left on the head. Brighter at the strike, because the skin is tighter under a held hand, and then nothing.
- **`tapado`** — the low drum stopped: the weight of the bass tone with the note taken out.

Both are derived from the **slap** recording rather than the open one: what makes a closed slap a slap is the crack, and that lives in the slap's take. Their damping is far harder than the muffled three — heel, toe and muff are a tone with the ring taken *off*, these are a stroke with the ring taken *out*, and a conga stopped by the hand that struck it is over in fifty milliseconds.

All nine styles reach for them, and where they go is a decision per style: the marcha's bass on 3 and its answering crack, the rock anchor (a rock mix has a bass guitar on the "and" of 1 already), the dance slap that has to cut without ringing into the next stroke, funk's whole vocabulary, samba's surdo-like 2, reggae's one-drop three, and DUE-UNO's low half — which keeps that style at exactly two sounds while making one of them a stopped one, a thud answered by a tone.

Measured across the eight-bar phrase of every style: **54 stopped strokes against 294 that ring.** The proportion is the point — a part made only of stopped strokes has no sustain in it at all, and the test asserts both that nearly every style uses them and that the ringing ones are still most of what the part is made of. Rendered, the bar is empty 26–41 % of the time depending on the style, which is the space those strokes buy.

### The one belongs to the band

No style puts a conga on the **first quarter's down-stroke**, and `eventsAt` enforces it as well as every table observing it. The band is already there — kick, bass and the downbeat of whatever the guitarist is playing, together — and a conga on top of that is not heard as a percussionist, it is heard as a thicker attack. A player standing next to a drummer plays *around* the one: the low tone lands on its "e" or its "and", the heel-toe pair starts a sixteenth late, and the one is left alone. Per style: marcha and samba put the heel on the "e" of 1; rock, pop and bossa put the low tone on the "and"; dance and funk on the "e", where those parts already live in sixteenths; reggae is one-drop and never had anything there.

The shaker is deliberately not covered by the rule. A shaker on the pulse *is* the pulse, and it is what the listener follows.

### Knowing what kind of record it is

`AUTO` is the app's one attempt at understanding the song rather than its pulse, and it has always shipped **off**: it got three cases in nine against material whose style was known, which is no better than always guessing the same style. The bench that measured that lived outside the tree — the CMake target still takes its source from an environment variable — so the number had not been reproducible since.

That bench is now in the repository, and it is harder than the old one: four arrangements written to the same four descriptions the chooser works from, each heard as **three different records** (108 / 122 / 138 BPM, with the syncopation and the pad varying), so thresholds cannot be fitted to one take wearing twelve hats.

**The bands were in the wrong place.** The band called "kick" was 160–500 Hz, and a kick's fundamental is 40–90 — it held the bass guitar's harmonics, the snare's shell and the low end of a guitar, everything except the thing it was named after. The band called "snare" started at 500, above the shell tone that makes a backbeat sound like a backbeat. They are now 35–110 and 150–600.

**And two of the four features still do not work, which is worth saying plainly.** Scored across the twelve:

| feature | what it separates |
|---|---|
| `offHigh` | pop 0.13–0.16 against 0.43–0.95. A clean gap — the only one. |
| `syncopation` | latin 1.23–1.70 against 0.80–1.26. Mostly a gap. |
| `evenKick` | rock 0.34–0.59, dance 0.45–0.56, latin 0.32–0.61, pop 0.33–0.60. **Nothing.** |
| `alternation` | 0.09–0.22 for all four. **Nothing.** |

The two that fail are the ones meant to name four-on-the-floor and a backbeat, and the reason is the same for both: a bass guitar's fundamental sits at 55–110 Hz and its second harmonic at 150–250, which is exactly where a kick and a snare shell are. Half-wave rectifying the rise removes the sustain and not the articulation — a bass is plucked, and a pluck is an attack. Separating them needs the bass taken out of the drums, which is source separation or the desk's own kick channel; the app has the second when the listener gives it one, and this does not use it yet.

So the decision was rebuilt on the two that work, and the two that do not were taken **out of the vote** rather than left to dilute it: sparse offbeat hats → pop, energy on the odd sixteenths → latin, an open hat on every offbeat → dance, and everything else → rock, which is three positive statements rather than a shrug.

**11 of 12, against 3 of 12 for chance** — up from 4 of 12 with the old tree. `AUTO` still ships off: the thresholds come from synthetic material, and turning it on by default wants real records first.

**Density was tried in the dynamics and taken out.** Level cannot tell a quiet full band from an exposed voice, and the occupancy count looked free. It is not usable there: the fold runs on the analysis bus, and the make-up gain on that bus exists to erase exactly the verse-to-chorus difference the dynamics follow. Measured, the part came down 3.5 dB with level alone and 0.9 dB once density could vote. It stays where it works — naming the style.

### The harmony, and the bar

The first thing in the app that looks at **pitch**. Everything else it listens to is percussive: the network is trained on beat activations, the kick channel is one drum, the level meter does not care what note it is — and that left the strongest cue about where a bar begins on the table.

Harmony changes on bar lines. Not always and not only, but the distribution is not close, and it is the only cue on the list that survives when the drums do not: a voice with a guitar behind it has no kick, no snare and a beat activation curve the network was never trained for — and still changes chord, and still changes it on the bar.

`HarmonicChange` is a chromagram — twelve pitch classes over three octaves by Goertzel on a decimated signal — and the cosine distance between what is sounding now and what has been sounding. Pitch classes rather than a spectrum on purpose: a drum is broadband, so it lands on every bin at once and barely moves the vector's *direction*, where a chord moves it a long way. Three things were found by measuring rather than by design:

- **A chord is a step; a drum is an impulse.** Peak-picking the change function reported 41 chord changes on a *drum stem*. What a drum cannot do is stay moved, so a change now has to hold for three hops.
- **Energy and tonality gates.** A near-silent window normalises to a unit vector made of arithmetic noise whose direction wanders freely — 59 more false changes on the same drum stem. And a broadband hit gives a flat chroma, and flat is not a chord.
- **A per-bin median over five windows.** In the time-frequency plane a hit is a vertical line and a chord a horizontal one.

| | changes | on the downbeat |
|---|---|---|
| full mix | 38 | 39 % (chance is 25 %) |
| **no drums at all** | 20 | **100 %** |
| snare stem alone | 4 | — |

On a full mix it is a lean rather than an answer, and that is the right result: where there are drums the app already has two better sources. Where there are none it had nothing. End to end, with the clock started two quarters out of step and a network that finds every beat and cannot find the bar — which is what a real one does on this material — the count went from **0 % right to 60–100 %**, depending on how soon the gate below opens.

The network is asked first and the harmony only when it has not answered: the harmony may **answer**, never overrule. And it is the fallback, so it is held to more than the network — a fallback that acts on a plurality moves the count on material it knows nothing about. At equal thresholds it rotated the bar five times on a kit track and settled on none.

The margin on its own is **not** the guard, and finding that out cost a red build. Measured on one kit track across five runs the harmony's winning margin came out 0.07, 0.10, 0.36, 0.38 and 0.58: a chroma pointed at drums finds a different arbitrary answer every time, so no threshold on it separates anything, and at 0.58 it rotated the bar three times on a track with no chords in it. What does hold is a property of the **material** rather than of a draw — what share of recent windows carried a chord at all:

| | tonal share |
|---|---|
| kit track | 0.31–0.32 |
| band with no drummer | 0.55–0.56 |

Thin: a factor of a little under two, where the kick channel's own guard separates by four. The line sits below the good case rather than half way, because both figures are maxima over their runs and the gate reads the instantaneous value. With it, the bar on drum-free material goes from 0 % right to 60–100 % depending on the run, and the kit track is left alone.

### The form

The eight-bar sentence — state it, answer it, state it, go somewhere, and a fill on the way out — was counted from wherever the part happened to come in, which is to say from nowhere. A band's fills land on the bar before the section changes; the app's landed every eighth bar counted from an arbitrary start, so on average they missed by four.

A section boundary is the one piece of musical form that reads off a level meter: a verse does not become a chorus quietly. Sampled once a bar — a step measured *inside* a bar is a fill or a stab — with four bars between boundaries, because a band that lifts over two of them is one section change and a sentence restarted twice inside itself is worse than one never restarted.

Measured with the part started three bars out of step with the song: the app's fill bar coincides with the band's **70 % → 90 %** of the time.

**Tried and not shipped: anticipating the fill.** Once the sentence is aligned, the app knows which bar the band fills on *before* it arrives, and a fill is the most misleading bar in a song for a beat tracker. So the clock was made to hold its ground through it. It could not be shown to help and could not be shown to hurt: across runs the phase through those bars measured 9.83, 43.02, 1.68 and 9.30 ms without it against 8.98, 61.38 and 9.17 with — overlapping ranges from a measurement that moves five-fold on the same configuration, and draining the analysis worker every block did not settle it. A change to the timing path that cannot be shown to help does not go in. The measurement is left behind as a loose guard.

### Quanto, non solo quando

Everything else in this engine answers **when**: the tracker finds the pulse, the clock places it, the tables say which stroke falls on which sixteenth. Nothing answered **how much**, and that is most of the difference between a part that is correct and a player who is listening.

`BandDynamics` is the missing input. Two things go into it, and the first carries most of the answer: **level**. It is read after the leak subtraction and before the analysis make-up gain — our own part has been removed, so the dynamics cannot follow themselves, and the make-up, which exists to hold the network's operating point and therefore flattens exactly this, has not been applied yet. The second is **density** — how many of the bar's sixteen sixteenths have anything in them, from the fold `StyleDetector` is already computing. Level alone cannot tell a quiet full band from an exposed voice, and those two want opposite things from a percussionist: the first wants the part turned down, the second wants most of it gone. They are combined by taking the smaller, so a bar that is loud and nearly empty is still played sparsely — measured, a bar with 13 of 16 occupied reads 1.00 and one with 3 reads 0.00 at exactly the same level.

The level is measured against **the loudest this song has been**, not against an absolute line: a quiet band and a loud one differ by more than a verse and a chorus do. Same lesson as the fit residual in `docs/STATUS.md`, learned once and applied here without having to learn it again.

What the part does about it is the half that matters. Below full it plays **quieter**, which any fader could do; and it plays **less**, which none could. The figure thins from the bottom — the heel and toe that fill the gaps go first, then the ghosts, then the answering tones — until what is left is the skeleton: the slap, the low tone, the pair that closes the bar. Measured on the marcha: 8 conga strokes a bar at full, 4 at half, 4 at the floor, with the loudest surviving stroke still at 0.29 (a ghost is written at 0.16, so what is left is being played, not brushed). The step from 8 to 4 is not a fault and smoothing it would be one: the figures are written bimodally on purpose, and thinning finds the seam, which is the line a player drops to.

And in the passage that really does not want it, it **stops** — at a bar line, never mid-figure, and it takes two seconds of quiet to leave and half a second of band to come back. The button reads IN ASCOLTO while it is standing down, because "the part went quiet" is the kind of thing a player wants confirmed rather than wondered about.

End to end, over a song whose verse sits 14 dB under its chorus:

| | verse against chorus |
|---|---|
| part with DINAMICA off | −0.1 dB |
| part with DINAMICA on | **−3.5 dB** |

Three decibels is the *smaller* half of it: half the strokes have gone as well, and an RMS taken over the section counts only the level. A player does not come down by 14 dB when the band does — they come down a few and play less, which is what this is.

`VPRender --arc` sweeps the dynamics over a take so it can be heard in one file. Measured on that render: −32.7 → −24.4 → −33.6 dBFS with the stroke count going 7 → 13 → 6.

**On by default, and switchable.** A fixed part is what some jobs want, and the DINAMICA button is next to the styles rather than in SETUP because it is a musical choice and belongs where it can be reached mid-song.

**Density is the obvious next input and is not in yet.** `StyleDetector` already folds the bar into occupied sixteenths, which would separate "quiet but full" from "sparse and exposed". Level alone was shipped first because it is measurable, robust at any gain, and does not need the bar to have been found.

### At most two strokes to a quarter

No written figure puts more than **two conga strokes in a quarter**, and nothing audible does either with the ghost generator wide open. The fill bar is exempt, because that is what a fill is.

The dance part was the reason: it was thirteen strokes a bar, three and four to a quarter, because it was transcribed from a salsa percussion loop — a record where the congas *are* the record. Under a live band they are not. The space between the strokes is where the rest of the band is, and a part that fills it is a wall however good the figure inside it is. Dance is now six a bar and says the same thing: the low drum answering the one from its "e", nothing at all on 2, the slap as the loudest stroke on the "e" of 2, the opens closing the bar.

### Four styles

A player does not bring a marcha to a rock track, so the styles are four different parts rather than variations on one:

| Style | Congas | Shaker | Ghosts |
|---|---|---|---|
| **marcha** | the tumbao: slap on 2, bass on 3, the paired open tones closing the bar | eighths, weight on the pulse | busy |
| **rock** | off the backbeat entirely — the snare owns 2 and 4 — anchoring the "and" of 1 and pushing on the "and" of 4 | eighths, weight on **2 and 4**, with the drummer | sparse |
| **dance** | a tumbao stated rather than filled in: low drum on the "e" of 1 and on 3, slap on the "e" of 2, opens closing the bar | sixteenths, weight on the **off-eighth** where the open hat sits | medium |
| **pop** | mostly space: a light low tone off the one, a lift, the push into the next bar | eighths, level and quiet | almost none |
| **due-uno** | not a genre, a shape: two strokes on a quarter and one on the next, all the way round, on two drums and nothing else | eighths, weight on the pulse | none, on purpose |

**DUE-UNO** is the one style written to a brief rather than transcribed, and the only one `AUTO` will never choose — "keep it simple" is a decision about the gig, not about the music, so the automatic chooser (which decides between four genres) does not get a vote on it. Every stroke sits off the beat: the low-high pair answers the beat that has just gone, the single tone leans on the one coming. Six strokes a bar out of two drums, and the ghost notes are switched off for it, because a ghost is a heel or a toe and that would be a third sound.

### Coming in on the one

The percussion should enter on beat one. Measured over eight tracks by finding the first non-zero sample of the percussion channel and comparing it with the generator's own bar:

| | before | after |
|---|---|---|
| entered on a strong beat (1 or 3) | 3/8 | **5/8** |
| entered on beat 1 exactly | 2/8 | 2/8 |
| time to entry | 3.8–8.7 s | **4.2–7.2 s** |

Three faults were found, and they are worth separating because only two of them are the engine's:

- **Bar alignment was switched off during the wait.** `hadDownbeat` was gated on `! waitForQuantize`, so through the entire count-in the bar was never corrected and "the first quarter" meant whichever beat the clock happened to start counting on. That is the one moment the alignment matters most.
- **The give-up timeout was shorter than what it was waiting for.** It was capped at 3 s, and two bars is longer than that at anything under 160 BPM — so at every ordinary tempo the wait expired and the part came in on the next quarter regardless. It is now four bars, floored at 2 s and ceilinged at 10 s.
- **A single downbeat is not evidence.** The network answers "this is a strong beat" reliably and "this is *the* strong beat" much less so: over the same eight tracks it put 7 of 12, 9 of 11, 10 of 19 and 13 of 25 detected downbeats on the true one — a plurality every time, and a coin toss taken one at a time. They are now counted and the bar is rotated to the majority before entry, which is what moved 3/8 to 5/8.

**What is left is not the bar logic.** Splitting the residual error into the phase of the clock and the rotation of the bar:

| | 68 | 84 | 96 | 104 | 120 | 126 | 140 | 152 |
|---|---|---|---|---|---|---|---|---|
| distance from the clock's own beat | −0.46 | −0.32 | −0.11 | −0.07 | +0.05 | +0.02 | +0.01 | +0.07 |

At 96 BPM and above the clock is on the beat and every remaining error is a whole number of beats — the bar is rotated, not the clock. Those are all beat 3 rather than beat 1, which is the ambiguity the network genuinely has on this material: the test tracks put the same kick on beats 1 and 3, so nothing distinguishes them. Real music separates them with chord changes, vocal phrasing and crashes; the synthetic loops used here deliberately do not, so this number is a floor rather than an estimate.

At 68 and 84 BPM the clock itself is locked half a beat out, onto the offbeat. That is the slow-tempo octave and phase weakness documented above, not an entry problem, and it is where the next work on this belongs.

### Which quarter is the one — measured end to end

The measurements above look at the first sample of the percussion channel. `VPBar` (`scripts/probe_bar.cpp`) asks the whole question instead: over thirty rendered tracks whose beat one is at sample zero, which quarter did the part come in on, and which quarter did the bar sit on for the twenty seconds after that. Runs where the clock settled at half or double the song's tempo are reported and excluded — at the wrong metrical level the bar is neither right nor wrong, it walks.

| | line feed, entry | line feed, held | mic in a room, entry | mic in a room, held |
|---|---|---|---|---|
| before | 19/25 | 21/25 | 4/25 | 3/25 |
| the count carried over a phase snap | 21/25 | 20/25 | 8/25 | 6/25 |
| **and the bar as a histogram** | **21/25** | **20/25** | **10/25** | **10/25** |

Two faults, and the first is much the larger:

- **A phase correction moved the grid and left the count behind.** `beatInBar` is advanced in one place, `advance()`, when the phase runs past 1.0. `snapPhase` *jumps* the phase, so a correction that crossed the boundary — which any correction lands on when the song sits near one — moved the grid without the count following: forwards the clock never wraps for that beat and the bar falls one behind the song, backwards it wraps twice and the bar gains one. While the part waits to come in the tracker re-places the grid on any error over four hundredths of a beat, so this happened repeatedly, and the bar the part then entered on was a quarter away from the song's for no reason anything downstream could see. The count now goes over the boundary with the grid — but only while nothing is playing. Sounding, moving the count is a bar moved under the listener, and that is left to the histogram below, on evidence.
- **The bar was decided by the downbeats that cleared a threshold.** Those are the loud evidence and also the rare evidence: on a line feed 39% of beats clear the gate, through a speaker and a room 63%, and on a quiet passage none do — and a vote taken over a handful of samples shows a wide margin on noise alone. Every beat now files its downbeat activation into a histogram over the four quarters, decayed over sixteen bars, and the bar is rotated to the winner when it stands clear of the runner-up.

**What is left is the network, and the app can now tell when that is so.** The histogram's margin separates the two listening paths cleanly: 0.34 on a line feed, where the network puts its downbeat on the true one in 73% of bars, against 0.014 through an iPad speaker and a room, where it is no better than a coin. Where the margin is absent the bar is not rotated at all, which is why the room figure moved from 3/25 — worse than chance, the app confidently following a misleading signal — to 10/25, which is chance. It is not the app finding the one in a room; it is the app no longer being certain about the wrong answer, and leaving TAP to settle it.

Why the room is so much worse is not a mystery and is not the bar logic: the iPad speaker has almost nothing below 250 Hz, so the kick fundamental and the bass root — which is where this material marks its bar — never reach the network. On that path the network's downbeat activation is *strongest on the true three* (0.593 against 0.430 on the true one, winning 38% of bars against 18%). No amount of counting downstream turns that round.

### And the listener's answer stands

Two properties follow from that, and they are the reason the automatic answer is safe to have at all.

**The correction only ever rotates.** `alignBarFromVotes` moves the count with `rotateBarIndex` and nothing else — no phase, no tempo, no metrical level. Moving a bar therefore costs the clock nothing, which is what makes it worth doing on a plurality rather than on a certainty. It used to snap the *phase* to move the bar, which threw the whole loop state away for a correction that only ever concerned the count.

The one exception is the count following the grid when the grid is re-placed, and it runs the other way round: the phase moves and the count goes with it, so that a bar stays on the beat of the song it was on. That happens only while nothing is playing — see `TempoFollower::snapPhase`.

**A one placed by hand is not moved again.** The automatic alignment used to be held off for thirty seconds after the listener moved the bar, which is long enough to look like it worked and short enough that the bar was moved back before the song was over. On material where the vote is no better than a coin that is the worst of both. Now moving the one — with **SPOSTA L'1**, or with a **TAP** that declares it — locks the count: the histogram carries on being built, so the answer is there the moment the lock comes off, but nothing rotates anything. Four presses take the one all the way round the bar; the fifth hands the count back to the app.

`bar-lock` in the tests is the property, on the same provocation for both: a network whose downbeat moves by two quarters half way through, which is what a section change looks like from here. Free, the bar follows it. Locked, it does not move at all — checkable without knowing where the one truly is, because a bar that is never rotated counts 0 1 2 3 forever and every rotation is a step that is not +1.

### Choosing the style automatically — measured, and not good enough

`StyleDetector` folds the analysis signal onto the bar in three bands and reads four rotation-invariant features from it: how evenly the low band lands on the four quarters (four-on-the-floor), the depth of the two-beat alternation in the snare band (backbeat), the high band on the offbeat against the beat (open hat), and energy on the odd sixteenths (syncopation).

**Measured against nine pieces of material whose style is known: 3 correct.** Always guessing "rock" would also score 3. So it is **off by default**, and the manual setting is what the app uses.

Two things were established on the way, both by measurement rather than argument:

- The first attempt put the kick band under 120 Hz. A tablet speaker in a room, picked up by the device's own microphone, has essentially nothing down there — it was reading an empty band, which is why identical material scored differently at different tempi.
- The obvious suspect was bar alignment: the tracker finds the beat to ±3 ms but the downbeat much less surely, and a bar rotated by one beat inverts "are 2 and 4 louder than 1 and 3" (a half-time track with the snare on 3 scored the highest backbeat of anything tested). Feeding the detector the **ground-truth bar phase** from the generator scored **3/9 as well**, which rules alignment out: the features themselves do not separate these styles.

What would work is a small classifier over the log-spectrogram the engine already computes for BeatNet, folded onto the bar — a learned model, and so a different piece of work needing training data. The detector and its measurement harness are kept so that work can start from a baseline rather than from nothing.

The accent contour across the bar belongs to the **style**, not to the engine. That is not a refinement — a single global contour favouring beat one silently cancelled the rock pattern's backbeat emphasis (0.80 against 0.79, when the pattern asked for 0.80 against 0.92), and the test caught it.

The user's subdivision setting *thins* a style's shaker pattern and never adds to it, so a style that does not want sixteenths does not get them because the user asked for a busy shaker.

This used to be a constant array of eight entries inside the render loop, repeated identically for the length of the song, and it was not a marcha:

- **The signature was missing.** A marcha closes the bar with the *pair* of open tones on 4 and the "and" of 4, pulling into the next bar. Without them it is a list of conga hits.
- **The shaker had no figure.** It fired on every pulse with the same velocity. A real shaker alternates an accented down-stroke on the pulse with a lighter up-stroke on the return — two different strokes, not one sample twice.
- **Nothing varied.** No two-bar phrasing, no accent contour across the bar, no fills, no ghost notes.
- **`humanization` was pinned to 0.00** in the UI and only jittered shaker velocity; timing was exactly on the grid and there was no swing.

Now: a two-bar phrase (bar B doubles the bass and moves the heel), a fill closing every eight bars, an accent contour across the bar, ghost notes on the sixteenths scaled by intensity, swing that lands only on the off-eighths, and micro-timing. Micro-timing is expressed as *lateness* only — the clock hands out grid positions as they pass and a voice cannot be scheduled before one — so the feel is biased late by half its own spread.

## The clock is not allowed to hear itself

The app plays a part and then listens to the room. Whatever of that part comes
back on the input is a pulse in perfect agreement with the clock, so a tracker
that sees it is being told it is right no matter where it actually is - and a
part played louder is a tracker more certain of a tempo it is getting from
itself.

That is the whole of two separate reports. *"When I raise the shaker volume it
slows down and loses the beat"*: the louder the part, the more of the analysis is
the part. *"When one song ends and another starts, it only picks up the new one
if I press STOP first"*: STOP mutes the part, and muting the part is what lets it
hear the song again.

Three things were wrong. The measurement is `leak-residual` in
`Tests/TestMain.cpp`, which feeds the engine nothing but its own delayed output
and reports the share of it that survives into the analysis signal - so 1.000 is
a tracker following its own shaker and nothing else.

| | before | after |
|---|---|---|
| SPEAKER, 1024-frame block | 0.62 | **0.063** |
| SPEAKER, 4096-frame block | 0.57 | **0.021** |
| MIXER, 1024-frame block | **1.000** | **0.063** |

- **The subtraction only ran in SPEAKER mode.** The leak was modelled as the
  iPad's own speaker into its own microphone. But a mixer hands the app its
  output straight back on the return - the same signal, a shorter path, none of
  the room in front of it - and on that rig nothing was subtracted at all. That
  is the 1.000.
- **It only cancelled the top end.** The reference was high-passed at 1.5 kHz,
  which is the right model for a tablet speaker with no low end to leak. On a
  mixer return the congas are in there too, and they went through untouched. Two
  bands are fitted now, solved together rather than one each - a one-pole split
  does not make them orthogonal, and fitted independently each claims part of
  what the other explains. Either path is covered without having to be told
  which one it is: through a speaker the low gain simply fits near zero.
- **It could invent a leak that was not there.** The fitted gain was clamped at
  zero before it was smoothed. Over one block the fit between two unrelated
  signals is not zero, it is zero plus a few per cent of noise, and keeping only
  the positive half of that averages to a standing positive gain - so a few per
  cent of the app's own part was subtracted from the analysis even on a clean
  feed that carried none, which costs the tracker real onsets - it was seen to
  put it badly out on a clean feed with the part turned up. The signed fit is
  smoothed first and clamped only where it is applied, and the subtraction is
  gated on our output explaining at least a few per cent of the input's energy:
  a real leak does that by definition, an accidental resemblance between our
  shaker and the band's hi-hat does not.

Driving the whole engine with a song and a return loop agrees on direction and
magnitude - on one take at 120 BPM with the return at half level, the tracked
tempo went from 0.12 BPM out with the part silent to about 2 BPM out with it up,
and walked over the minute, while the same take in SPEAKER mode, where the
subtraction did run, stayed at 0.13. That is one arrangement and one seed; the
numbers in the table are the ones this rests on.

## Aligning without sounding like it

The clock corrects its phase by *rate*: it runs fractionally fast or slow until
it is back with the band, rather than moving the grid (see **Grid continuity**).
How hard it may lean, and how quickly, is the difference between a percussionist
leaning into the beat and one who has lost it.

Every measurement in this repository until now read `tick.tempoBpm`, which is
the tempo *before* that lean. The pulses come out at `tempo * (1 - steer)`, so
the entire correction was invisible to every test.

Measured against a decoder whose phase carries three hundredths of a beat of its
own error - which is what the real one carries, refreshed about six times a
second - the loop sat at its steering limit **in both directions, permanently**:
±6 BPM at 120, 3.7 BPM rms, on a band that never moved. Two causes:

- **The gain reached that limit on an error of 0.022 of a beat**, which is
  smaller than the uncertainty of the thing it was measuring. It closes the
  error over about half a second now instead of a fifth, which leaves the limit
  for errors that are really there.
- **The error estimate was smoothed per block, not per second.** `setGridPhase`
  was called once per audio callback with a fixed blend, so the loop's whole
  time constant was whatever the buffer happened to be - 4.2 BPM rms of wobble
  on 64 frames against 2.2 on 1024, same music - and at every buffer size it was
  shorter than the interval at which the analysis refreshes, so nothing was
  averaged at all. It is a time constant in seconds now, long enough to span
  several hypotheses.

Below the analysis's own residual uncertainty the loop does nothing: an error
smaller than that is not an error, and a loop that keeps pulling on it is playing
the decoder's noise back as a tempo.

| | rms wobble at 120 BPM | worst | 64 vs 1024 frames |
|---|---|---|---|
| before | 3.7 BPM | ±6.0 | 4.2 / 2.2 |
| after | **0.15 BPM** | 1.2 | **0.14 / 0.19** |

`phase-steer` in `Tests/TestMain.cpp` holds both.

## Half and double, chosen

The metrical level is the one thing about the tempo the signal does not settle -
the measurements are under `BeatDecoder::userOctave` - so it used to be left to
the listener and two buttons. It is now decided by a rule, with those buttons as
the override.

The rule is not about the music, it is about the part: **keep the pulse the
percussionist counts in between 76 and 168 BPM**. That band is a little over an
octave wide and the overlap is the hysteresis - a tempo just halved from 168
lands on 84, not on 76, so nothing sits on a boundary it can be pushed back and
forth across. A level change is held for two and a half seconds before it is
taken, because changing it mid-song is one of the most audible things the app
can do.

What this is not is a solution to the octave problem. Where the band should sit
is a judgement about how a part should feel, not a measurement, and it has not
been validated against listeners. It is bounded, it is reversible in one tap,
and the tempo line says when it is the app's decision rather than yours.

## Voices

Up to 16 overlapping grains. Sample data lives in precomputed buffers generated at `prepare`.

Three things here are what "the shaker and the congas break up" actually was:

### The kick channel

The single most useful thing a digital desk can hand the app, and the one input it has that carries **one instrument**. `SETUP → INGRESSO → CASSA` names the input the kick send arrives on; `NO` is the default and the whole path is switched off until an input is named.

Three things follow from a channel with one drum on it, and they are three of the things the rest of the chain is worst at:

- **Phase, to the sample.** The neural path reports beats on a 20 ms frame grid, so its times carry ±10 ms of quantisation before anything else goes wrong. Measured against material whose kick times are known exactly, `KickOnsetDetector` finds 32 strikes out of 32 with a mean error of **0.56 ms** and a worst of 0.62.
- **The drummer stopping.** Not an inference any more: the channel is silent for exactly as long as the kit is out — measured, 8.3 s of silence through a four-bar breakdown against 0.00 s with the kit in. `EvidenceTrust` takes that directly instead of trying to read it off the fit, which is what two sections of `docs/STATUS.md` failed to do in time.
- **The metrical level.** A kick pattern states the beat; an activation curve where the eighths are as tall as the beats does not.

End to end, driving the whole engine over a drifting band with human scatter, against a network that is being handed the *true* beats — so this is what the channel adds on top of the best a network could do:

| | phase rms | worst |
|---|---|---|
| mix alone | 17.0–19.4 ms | 27.7–51.4 ms |
| mix + kick channel | **11.8–15.3 ms** | 27.4–49.4 ms |

The rms improves every run, by 15–40 %. The **worst** excursion does not reliably improve and the test deliberately does not claim it does: it is dominated by single events — the re-lock coming out of a breakdown, a fill — and one event is not something a stiller phase loop prevents.

It carries its own guard against being pointed at the wrong thing. Nothing stops somebody routing the app the full mix on the channel they called the kick, and a detector aimed at a full mix fires on everything. So the tracker keeps a decayed share of how many strikes landed near a beat of its own clock, and stops believing the channel when that share falls — a channel that is not a kick fails it immediately, one that is passes it inside a bar. The `CASSA` button only lights when the strikes are actually landing.

The detector runs on the **raw** channel, before the leak subtraction, the rumble high-pass and the make-up gain. Those exist to protect a microphone hearing the app's own output in a room; a desk send of the kick has neither problem, and putting a canceller in front of the one clean transient on the stage would give away exactly what makes it worth having.

### How fast a tempo change is taken

The thing a live percussionist is judged on, and until now it had never been measured. `VPAlign`'s tempo bench drives the decoder and the clock together against a song whose tempo genuinely moves, and reports the time from the change until the clock is inside 2 % and stays.

| change | seconds |
|---|---|
| 118 → 124, step | 4.6 |
| 118 → 128, step | 5.3 |
| 100 → 140, step | 16.1 |
| 128 → 120, step | 5.3 |
| **118 → 126 over 4 s (a band picking up)** | **0.02** |
| **118 → 126 over 12 s (a band drifting)** | **0.02** |

The last two rows are the answer to the question people actually ask. A band that *drifts or ramps* — which is what a band does — is followed with no measurable lag at all: the clock never leaves 2 % of the true tempo for the whole change. What costs about five seconds is a **step**, and a step is not a band getting faster, it is a different song.

Two percent, not one: at one percent the column was measuring the decoder's own noise rather than its speed. On this material the eight-beat fit wobbles ±1.5 BPM, which at 128 is 1.2 %, so a clock sitting exactly on the answer fails a 1 % test at random.

**Three ways to make the step faster were tried and none shipped**, and the bench has the numbers for all of them:

| | 8 % step | 40 % step | settled error |
|---|---|---|---|
| leave the fixed regime after 3 beats instead of 5 | 5.46 → 5.30 | 15.33 → **16.11** | — |
| drop the beat history across the step | helps the 40 % | — | ×8 worse on small steps |
| a five-beat responsive fit instead of eight | 5.30 → 4.97 | 16.11 → 15.09 | roughly **doubles** |

What they have in common is that the cost is not where it looks. A fit over eight beats cannot describe a new tempo until most of those eight beats are at it, and at 120 BPM that is four seconds — the regime and the smoothing are rounding on top. Buying a quarter of a second for twice the wobble afterwards is the wrong way round for an app judged on staying in time.

### The round trip, measured

What the clock has to run ahead by is the time between the app deciding to play a stroke and that stroke being heard next to the band. That used to come from two places, and neither is a measurement of *this* rig: the latency the operating system reports for the device, and a 20 ms constant in `BeatTracker` calibrated once against a notated click. Both are reasonable and both are guesses — on a stage the path is an iPad, a USB interface, a desk, its own buffering and whatever the desk does to the return.

`SETUP → PROVE → LATENZA` plays a 30 ms sweep, captures what comes back and correlates the two. It takes three quarters of a second. The result is kept per install and is what the clock runs on from then on; pressing the button again clears it.

| true round trip | measured |
|---|---|
| 6 ms | 6.00 |
| 12 ms | 12.00 |
| 24 ms | 24.00 |
| 48 ms | 48.00 |
| 96 ms | 96.00 |

Sample-exact across the range a rig uses, and it still lands within 2 ms with a band playing over the top — which is the case that matters, because nobody gets a silent room at soundcheck. When the sweep does not come back clearly it **refuses**: the button reads NIENTE RITORNO rather than a number, because the commonest cause on a stage is the send not being routed back, and a figure invented there would be worse than none.

The correlation is normalised by the energy under the window at each lag, or a kick drum landing during the capture out-scores the sweep by being loud. The peak also has to stand clear of the best rival elsewhere in the capture before it is believed.

### Listening to it

Some of this is a musical decision, and the only review a musical decision can have is somebody hearing it. `VPRender` writes a few bars of any style to a WAV, driving the real `PercussionEngine` from a real `TempoFollower` at a fixed tempo — no analysis, no microphone, no network:

```bash
cmake --build build --target VPRender
./build/VPRender_artefacts/Release/VPRender --style dance --bpm 124 --bars 8 --click --out dance.wav
```

`--click` lays a quiet woodblock on each quarter, loud on the one, so "the congas never play the first quarter's down-stroke" can be checked by ear rather than by reading the tables. `--no-shaker` / `--no-congas` isolate one instrument; `--swing`, `--humanize`, `--intensity` and `--mix` are the same controls the app has.

### The sounds are recordings

`Assets/Percussion/*.wav` are conga, tumba, quinto and shaker recordings from the **Versilian Community Sample Library (CC0)** — see `Assets/Percussion/ATTRIBUTION.md`.

They are embedded by CMake the same way `beatnet.onnx` is. If the folder is empty the build says so and the whole percussion bank falls back to synthesis, so the tree always builds and always plays; `recordedStrokeCount()` is asserted in the tests so a missing or unreadable asset fails loudly instead of degrading quietly.

They are tuned up by a **minor seventh** (`kDrumTune`, 2^(10/12)) — one number, driving the synthesised bank and the playback rate of the recordings together so the two halves cannot end up tuned against each other. The VCSL takes are the large drums of the library and they are recorded slack: at concert pitch the part reads as a floor tom under a band, and the tumbao's low tone disappears into the bass guitar instead of answering it. A perfect fourth was the first correction and was not enough; a minor seventh is five semitones above that. Measured on the bundled takes, by the strongest partial — which is what a listener hears as the pitch of the drum:

| | recorded | played, old fourth | played, now |
|---|---|---|---|
| tumba | 138 Hz | 184 Hz | **246 Hz** |
| open tone | 163 Hz | 218 Hz | **290 Hz** |
| slap | 216 Hz | 288 Hz | **385 Hz** |

A conga and a quinto, not two toms. The nominal frequencies in `specFor` are a different thing and are not these numbers: they describe the **synthesis fallback**, which only sounds when `Assets/Percussion` is missing.

`scripts/prepare_vcsl_samples.py` trims pre-roll, high-passes hall rumble, truncates before any second hit, normalises and fades. Loud and medium takes are separate recordings; the soft layer is still derived by taking the top off. heel/toe/muff are the open tone damped. Round-robin is a second take on every articulation, not only the low drum.

- **Every stroke was one sample.** A conga slapped hard is brighter and shorter than one slapped softly, not the same recording with more gain on it. Each articulation is now synthesised at three dynamic layers — force makes the strike noisier, the attack faster and the head bend further into pitch — and each layer three times over with its own noise seed, so two consecutive strokes are different takes rather than the same take twice. The layer sets the timbre and the remainder of the velocity sets the level, so a crescendo moves smoothly instead of stepping.
- **Every sample ended on a step.** Each is an exponential decay cut off at a fixed length, and none had decayed far by then — the shaker stopped at 21 % of its peak, the open conga at 11 %, the slap at 8 %. That is a click on *every* hit, not a rare glitch. A 12 ms raised-cosine fade is welded onto each sample at synthesis.
- **A voice taken over by the next hit of the same kind was switched off mid-sample.** It is faded over 4 ms instead.
- **Allocation fell back to slot 0 when every voice was busy**, overwriting whichever voice happened to be first, part-way through, at full amplitude. It now takes a free slot, or the oldest voice — the one furthest into its decay.

## Grid continuity

Two more, on the way in:

- **The retrigger guard used to be 82 % of a pulse derived from the *displayed* BPM**, which reads 120 until the tracker locks. At any real tempo above ~145 that guard was longer than the actual pulse and swallowed every second hit. It is now a short fixed guard (20 ms — shorter than a 32nd at 200 BPM) whose only job is to stop the same grid position firing twice after a phase correction, and the groove tempo comes from `ClockTick::tempoBpm`, the tempo the clock is really running at.
- **`pulseBeatInBar` was wrong for any pulse after a beat boundary inside one block.** `PercussionEngine` picks the conga from that label, so the tumbao played the wrong drum whenever a buffer spanned a beat. It is now derived from the pulse index itself.

`Tests/TestAiBeat.cpp` also checks the *musical* shape — the paired open tones, the slap on 2 and bass on 3, the accented down-stroke, the two-bar phrase, the fill, swing landing only on the off-eighth, and that round-robin really produces different takes — plus a real-time budget (the whole voice mix costs ~0.3% of a 128-sample block) and the bank build time (~17 ms). It covers the truncated samples and both of the grid faults, and each of those three tests was checked to fail against the old code. The soft steal and the voice allocation are covered too, in `Tests/TestMain.cpp`. Asserting on the shape of overlapping grains turns out not to be necessary, and counting voices is not enough on its own — the shaker alternates a down and an up stroke, and those two overlap perfectly legitimately. What separates a release from a cut is whether any voice is ever *inside* its ramp, so `releasingVoices()` is the observable, and `hardSteals()` counts the times a stroke had to take a slot from a voice that was still sounding. Over 30 s of sixteenths at 200 BPM some voice is releasing in 432 blocks and nothing is ever hard-stolen; switching the ramp back to a hard cut takes the first number to zero.

## Latency display

Shown value = device input latency + output latency + buffer size, converted to ms. It is **measured from the device**, not a marketing guarantee.

## Offline

`VirtualPercussionEngine::process` is the same function used live and in tests. Tests inject synthetic audio instead of a device.
