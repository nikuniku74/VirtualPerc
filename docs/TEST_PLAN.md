# Test Plan

## Automated (host, no device)

`VPTests` covers the PLL clock, TAP, START/STOP, and the neural/TSM units. After `./scripts/setup-ai.sh`, the ONNX click-lock case also runs.

| Case | Expect |
|---|---|
| TempoFollower 120 BPM | Downbeats align with `beatPhase` |
| PLL residual +1 BPM | Clock trims toward the observed phase |
| PLL across phase zero | Quarter counter stays continuous |
| Tap 4× at 0.5 s | Shows 120 BPM; shaker stays muted until START |
| TAP then START | Following; shaker plays |
| Fast taps | Do not start the shaker |
| STOP after TAP+START | Immediately silent |
| Retrigger of a sounding stroke | Some voice is always inside its release ramp; none stolen mid-note |
| Speaker leak on a large block | Subtraction covers the whole block, not the first 2048 samples |
| Groove, styles, swing, humanisation, samples, grid | See `Tests/TestAiBeat.cpp` |
| AudioFifo / BeatDecoder / stretch / WSOLA / log-spect / worker | See `Tests/TestAiBeat.cpp` |
| FIFO overrun | Reported exactly; the decoder keeps the tempo but drops its stale confidence |
| Recorded loops: manifest, WAV reader, bank, player, hybrid | See `Tests/TestLoops.cpp`, and `docs/RECORDED_LOOPS.md` for what each one protects |

Shaker events must keep a monotonically increasing sample index (no loop restart).

### Recorded loops

`./build-host/VPTests_artefacts/Release/VPTests --loops` runs only these - seconds
rather than the minutes the neural suite takes, because it does not drive the
worker in real time.

| Case | Expect |
|---|---|
| Coming in | Silence until the quarter the part enters on; the first stroke *on* it |
| The junction | No step in the waveform anywhere across three passes - no click |
| The callback | Zero heap allocations, entry, junctions and loop changes included |
| 128 / 512 / 4096 buffers | Same strokes, same times to within 5 ms, same level to within 8% |
| Changing recording | One stroke per quarter through the change - none doubled, none dropped |
| Phase | Every stroke within 5 ms of its quarter (measured: 2.35 ms) |
| A 3 BPM drift | Still within 5 ms at the end of it (measured: 3.35 ms) |
| Regime `live` | The part goes back to single strokes, on a quarter, once |
| STOP | Silent on the sample, not on the next quarter |
| Swing | The off-eighth moves by the amount asked for; a swing no take is near is refused |
| Stretcher latency | The same measured lead at every buffer size |

Both backends are run: with the submodules checked out the tests run against
Signalsmith, and `-DVP_USE_SIGNALSMITH=OFF` runs the same suite against the
built-in WSOLA. Both must pass - the fallback is a floor, not an excuse.

## Diagnostics (built on demand, not part of a run)

| Target | Answers |
|---|---|
| `VPAlign` | How long the clock takes to get onto the song, and what the phase the decoder hands it is worth. See [docs/CORE_TIMING_AUDIT.md](CORE_TIMING_AUDIT.md). |
| `VPTiming` | Where the stroke lands to the ear, not where the clock scheduled it |
| `VPProbe` | Whole engine over a rendered arrangement |
| `VPBar` | Which of the four quarters the app calls the one, against material whose beat one is at sample zero. Reports the quarter the part came in on, the quarter the bar sat on afterwards, and - separately - how much the network gave it to work with. `--mixer` for a line feed, no flag for an iPad speaker in a room. |
| `VPCpu` | Callback cost against its budget |

## Offline audio

The engine accepts arbitrary buffers through `process()`. Use this to A/B a real `.onnx` against kit/piano/guitar WAVs.

## Device (iPad Air M1)

1. USB-C interface + kit mic
2. Play time (already listening)
3. Shaker enters after lock, not immediately
4. Accelerate / decelerate — shaker follows
5. Fill — shaker does not jump
6. Unplug/replug interface — audio returns
7. Incoming call / interruption — recovers
8. Background — audio policy as allowed by iPadOS

## Not claimed

A specific latency number. The UI shows the device-reported latency.
