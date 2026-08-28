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

### Four styles

A player does not bring a marcha to a rock track, so the styles are four different parts rather than variations on one:

| Style | Congas | Shaker | Ghosts |
|---|---|---|---|
| **marcha** | the tumbao: slap on 2, bass on 3, the paired open tones closing the bar | eighths, weight on the pulse | busy |
| **rock** | off the backbeat entirely — the snare owns 2 and 4 — anchoring the one and pushing on the "and" of 4 | eighths, weight on **2 and 4**, with the drummer | sparse |
| **dance** | busy sixteenths landing on the "a" of each beat, the sixteenth before the next kick | sixteenths, weight on the **off-eighth** where the open hat sits | medium |
| **pop** | mostly space: the one, a light lift, the push into the next bar | eighths, level and quiet | almost none |

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

### The sounds are recordings

`Assets/Percussion/*.wav` are conga and shaker recordings from the **OLPC Berklee Sound Library, CC BY 3.0** — see `Assets/Percussion/ATTRIBUTION.md`, and note that the credit has to reach a screen in the shipped app, because attribution is a condition of the licence rather than a courtesy.

They are embedded by CMake the same way `beatnet.onnx` is. If the folder is empty the build says so and the whole percussion bank falls back to synthesis, so the tree always builds and always plays; `recordedStrokeCount()` is asserted in the tests so a missing or unreadable asset fails loudly instead of degrading quietly.

The most important thing done to them is the least interesting: each had **24–43 ms of silence in front of the transient**, which is 24–43 ms of lateness on every stroke against a clock calibrated to ±3 ms. `scripts/prepare-samples.sh` trims that, truncates before any second hit in the file, normalises and fades.

What the library does not give us is a multi-sampled instrument — it is four conga hits and two shaker strokes. So the dynamic layers are *derived* from the recording by taking the top off and shortening the ring (the right-shaped approximation: hitting a drum softer does not merely turn it down), heel/toe/muff are the open tone damped, and round-robin is genuine only for the low drum, which has two takes. Replacing the library with a properly multi-sampled one is a file swap plus a re-run of the prepare script.

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
