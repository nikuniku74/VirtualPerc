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

## Voices

Up to 16 overlapping grains. Sample data lives in precomputed buffers generated at `prepare`.

Three things here are what "the shaker and the congas break up" actually was:

- **Every sample ended on a step.** Each is an exponential decay cut off at a fixed length, and none had decayed far by then — the shaker stopped at 21 % of its peak, the open conga at 11 %, the slap at 8 %. That is a click on *every* hit, not a rare glitch. A 12 ms raised-cosine fade is welded onto each sample at synthesis.
- **A voice taken over by the next hit of the same kind was switched off mid-sample.** It is faded over 4 ms instead.
- **Allocation fell back to slot 0 when every voice was busy**, overwriting whichever voice happened to be first, part-way through, at full amplitude. It now takes a free slot, or the oldest voice — the one furthest into its decay.

## Grid continuity

Two more, on the way in:

- **The retrigger guard used to be 82 % of a pulse derived from the *displayed* BPM**, which reads 120 until the tracker locks. At any real tempo above ~145 that guard was longer than the actual pulse and swallowed every second hit. It is now a short fixed guard (20 ms — shorter than a 32nd at 200 BPM) whose only job is to stop the same grid position firing twice after a phase correction, and the groove tempo comes from `ClockTick::tempoBpm`, the tempo the clock is really running at.
- **`pulseBeatInBar` was wrong for any pulse after a beat boundary inside one block.** `PercussionEngine` picks the conga from that label, so the tumbao played the wrong drum whenever a buffer spanned a beat. It is now derived from the pulse index itself.

`Tests/TestAiBeat.cpp` covers the truncated samples and both of the grid faults, and each of those three tests was checked to fail against the old code. The soft steal and the voice allocation are not directly covered — a test that pins them down would have to assert on the shape of overlapping grains.

## Latency display

Shown value = device input latency + output latency + buffer size, converted to ms. It is **measured from the device**, not a marketing guarantee.

## Offline

`VirtualPercussionEngine::process` is the same function used live and in tests. Tests inject synthetic audio instead of a device.
