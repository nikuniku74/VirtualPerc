# Architecture — Virtual Percussionist

## Product

A live virtual percussionist: the app listens to an acoustic drummer and plays a musically locked percussion part that follows tempo and phase continuously. It is not a loop player with automatic BPM.

Shipping platforms: **iPadOS first**, Android later. macOS is used only as a development/test host (same C++ core + UI), never as a product target.

## Layers

```
iPadOS / Android UI
        │  (no DSP)
        ▼
Application Layer  (transport, settings, AUTO mode)
        │
        ▼
C++ Audio Engine (JUCE devices + real-time callback)
        │
        ├── NeuralBeatTracker (ONNX worker)
        ├── BeatTracker (state + tap)
        ├── TempoFollower (internal clock)
        ├── GrooveEngine (interface now, logic in later MVP)
        ├── PercussionEngine (sample performance)
        ├── HybridPercussionRenderer (which of the two plays)
        │     ├── LoopBank / LoopPlayer / LoopStretcher (recorded loops)
        │     └── PercussionEngine (single strokes)
        ├── TimeStretchEngine (loop kits, prototype)
        └── MIDI Engine (interface reserved, not in MVP 1)
                │
                ▼
        Audio Output
```

Rules:

- UI never contains DSP.
- DSP never touches UI objects.
- MIDI (later) injects the same beat events as audio analysis. BeatTracker and PercussionEngine stay unchanged.
- The percussion clock never restarts because BPM changed.

## Threads

| Thread | Work | Constraints |
|---|---|---|
| Audio | Input mix, FIFO push, clock, voice mix | No alloc, no locks, no I/O, no ONNX |
| UI | Read lock-free snapshot at ~15 Hz | Never call into the engine process path |
| Background | Offline file decode (debug/tests only) | Never on audio thread |
| AI worker | Resample, log-spect, ONNX, beat decoder | Never in the audio callback; publishes `BeatHypothesis` |

The musical clock stays on the audio thread. Neural inference never does.

Communication: per-field `std::atomic` snapshot. Settings use atomics too (UI → audio).

## Modules (MVP 1)

- `NeuralBeatTracker` — SPSC audio FIFO + worker (log-spect + ONNX + `BeatDecoder`)
- `TempoFollower` — adaptive PLL clock (`currentTempo`, `beatPhase`, `barPhase`)
- `BeatTracker` — neural hypothesis + tap tempo + LISTENING → FOLLOWING
- `PercussionEngine` — sample-accurate shaker hits on the clock grid
- `TimeStretchEngine` — optional loop kits (WSOLA / Signalsmith), prototype
- `LoopBank` / `LoopPlayer` / `LoopStretcher` / `HybridPercussionRenderer` —
  the recorded percussionist, behind `VP_ENABLE_RECORDED_LOOPS` (off by
  default). The clock is never the loop's: the loop is pulled onto the grid,
  corrected by rate and never by a jump. See `docs/RECORDED_LOOPS.md`.
- `VirtualPercussionEngine` — public real-time facade

## Input path (ready for multi-mic)

```
Device channels → ChannelAssignment → Instrument bus → NeuralBeatTracker
```

MVP 1: mix assigned live channels (default: all inputs) into one analysis bus. Per-instrument buses (kick/snare/hat/overhead) are already typed and will be wired in MVP 2.

## AUTO mode

The engine is already listening when audio starts. It waits until tempo and phase are stable, then locks, then plays. STOP mutes the shaker; analysis keeps following. If tracking dips, percussion continues at the last trusted tempo while armed.

## Future (architecture only)

MIDI clock, Ableton Link, song sections, pattern editor, user kits, multiple virtual players. Not implemented now. The engine API is event-in / clock-out so those features do not require a rewrite.
