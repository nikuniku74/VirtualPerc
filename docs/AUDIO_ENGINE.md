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

`Tests/TestAiBeat.cpp` also checks the *musical* shape — the paired open tones, the slap on 2 and bass on 3, the accented down-stroke, the two-bar phrase, the fill, swing landing only on the off-eighth, and that round-robin really produces different takes — plus a real-time budget (the whole voice mix costs ~0.3% of a 128-sample block) and the bank build time (~17 ms). It covers the truncated samples and both of the grid faults, and each of those three tests was checked to fail against the old code. The soft steal and the voice allocation are not directly covered — a test that pins them down would have to assert on the shape of overlapping grains.

## Latency display

Shown value = device input latency + output latency + buffer size, converted to ms. It is **measured from the device**, not a marketing guarantee.

## Offline

`VirtualPercussionEngine::process` is the same function used live and in tests. Tests inject synthetic audio instead of a device.
