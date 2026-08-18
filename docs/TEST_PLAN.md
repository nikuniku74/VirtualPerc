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
| AudioFifo / BeatDecoder / stretch / WSOLA / log-spect / worker | See `Tests/TestAiBeat.cpp` |

Shaker events must keep a monotonically increasing sample index (no loop restart).

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
