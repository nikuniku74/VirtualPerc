# Beat Tracking

## Goal

Follow live audio — kit, room/speaker, piano, guitar, band, or tap — with a causal clock: neural activations → BPM → phase → percussion. Recover from fills and dropouts without restarting the part.

## Method

1. **Features** — resample to 22.05 kHz, 2048-point log filterbank + flux (272-d, BeatNet).
2. **Network** — causal CRNN/TCN ONNX → softmax `[p_beat, p_downbeat, p_none]` per 20 ms frame. Runs on the AI worker (CoreML / NNAPI / CPU). Never in the audio callback.
3. **Decoder** — during acquisition the level comes from the state space, which has it seconds before the comb may speak; then a comb over the activation autocorrelation settles the metrical level, least-squares fits over recent beat times give precision and responsiveness, and a fixed-vs-live regime decides which of the two drives the tempo. The same fits carry the **phase**: their intercept, not the last accepted peak, so a beat's own timing error is averaged rather than handed over whole. The activation folded onto the committed period is what catches a grid anchored on the offbeat, which nothing inside the beat history can. Publishes `{bpm, beatPhase, barPhase, confidence, regime}`. See `docs/AI_BEAT_TRACKING.md`.
4. **Clock** — `TempoFollower` PLL on the audio thread, fed phase targets that have been advanced by the measured pipeline latency. Tap snaps phase and, after four taps, sets BPM from the median IOI.

Volume-peak / SuperFlux tracking is not used. The autocorrelation in step 3 runs on **activations**, not on the waveform.

## What a fixed tempo gets that a live one does not

A recorded track was cut to a click, so once its tempo is found it must stop moving; a band on stage must be followed. The decoder tells the two apart itself and holds a fixed tempo against fills, dropouts and missed beats, while releasing it within three beats when the pulse genuinely moves.

## States

```
(app / audio start) LISTENING → LOCKING → FOLLOWING
FOLLOWING → LOW_CONFIDENCE → RECOVERING → FOLLOWING
```

Always listening while audio is running. START arms the shaker and waits for the next bar downbeat. STOP mutes percussion; analysis keeps following.

Listening from launch means that by the time anybody plays, the analysis has been running on an empty room for minutes. The level of the analysis bus, read before the make-up gain flattens it, says when that stops being true, and the evidence starts again there. See `docs/AI_BEAT_TRACKING.md`.

## What we refuse to do

- Restart the loop or clock on BPM change
- Snap BPM from a single onset or volume spike
- Take the beat's *position* from a single onset either — see `docs/CORE_TIMING_AUDIT.md`
- Quantize the drummer; we follow them
- Run ONNX on the audio thread
