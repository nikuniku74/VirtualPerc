#!/usr/bin/env python3
"""Cut the shaker bank out of one shaker loop (numpy only).

    prepare_loop_shaker.py loop.wav [out-dir]

Source chosen by the percussionist (2026-09-28): "soft-bright-shaker"
(128 BPM, 44.1 kHz stereo, two bars). Every sixteenth is a stroke, cleanly
separated (below -50 dB before the next). On each beat the third sixteenth is
the accent (~-13.5 dB) - one take, repeated identically - and the other three
are light strokes, three different takes (peaks 0.152 / 0.132 / 0.149).

    shaker_down      the accent
    shaker_down_med  light take 2 (a down-stroke played gently)
    shaker_up        light take 1
    shaker_up_b      light take 3
    shaker_up_med    light take 2

Shakes swell (~20 ms of rising beads), so like the rest of the shaker path each
file starts 1 ms before the 1 ms envelope reaches 35% of the stroke's peak,
with a 0.5 ms fade-in; PercussionEngine measures and compensates the attack.
The tail is the recording's own up to the next stroke (no imposed decay: the
old exponential shaping is what made the VCSL shakes sound dry), with a 10 ms
fade. Mono sum, normalised to a 0.95 peak, written at the source rate.
"""
import sys
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
BPM = 128.0
# stem -> sixteenth index in the first beat (0..3)
JOBS = {"shaker_down": 2, "shaker_down_med": 1,
        "shaker_up": 0, "shaker_up_b": 3, "shaker_up_med": 1}


def main():
    src = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "Assets" / "Percussion"
    w = wave.open(str(src))
    sr, ch = w.getframerate(), w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64) / 32768.0
    x = x.reshape(-1, ch).mean(axis=1)
    six = int(round(60.0 / BPM / 4.0 * sr))
    k1 = int(0.001 * sr)
    env = np.sqrt(np.convolve(x * x, np.ones(k1) / k1, mode="same"))

    def start_of(k):
        lo = max(0, k * six - int(0.030 * sr))
        hi = k * six + int(0.060 * sr)
        e = env[lo:hi]
        return lo + int(np.argmax(e >= 0.35 * e.max())) - k1

    for stem, k in JOBS.items():
        s, e = start_of(k), start_of(k + 1) - k1
        y = x[s:e].copy()
        fi = int(0.0005 * sr)
        y[:fi] *= np.linspace(0.0, 1.0, fi)
        fo = int(0.010 * sr)
        y[-fo:] *= 0.5 * (1 + np.cos(np.linspace(0, np.pi, fo)))
        y *= 0.95 / max(1e-9, np.abs(y).max())
        pcm = np.clip(np.round(y * 32767), -32768, 32767).astype("<i2")
        with wave.open(str(out / f"{stem}.wav"), "wb") as f:
            f.setnchannels(1)
            f.setsampwidth(2)
            f.setframerate(sr)
            f.writeframes(pcm.tobytes())
        print(f"{stem:16s} sixteenth {k}  {1000 * len(y) / sr:4.0f} ms  peak at "
              f"{1000 * np.argmax(np.abs(y)) / sr:4.1f} ms")


if __name__ == "__main__":
    main()
