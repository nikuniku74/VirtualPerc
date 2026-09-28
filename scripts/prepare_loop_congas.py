#!/usr/bin/env python3
"""Cut the conga bank out of one salsa conga loop (numpy only).

    prepare_loop_congas.py loop.wav [out-dir]

Source chosen by the percussionist (2026-09-28): SampleFocus "Salsa Congas Loop
- Dance 3" (120 BPM, 48 kHz, a 4 s two-bar phrase played four times). Its
lowest drum (~205 Hz) is left out on request, and the groove's `tumba` strokes
are played on the mid conga. See Assets/Percussion/ATTRIBUTION.md for the
licence note.

Hits are numbered in the first 4 s phrase by onset (spectral flux, 5 ms hop):
mid conga ~325 Hz open tones 3 4 9 10 15 16 22 23 (23 is the only one with a
clean 375 ms tail), slaps 0 12 18, muted/palm strokes 1 2 5 11 13 14 17, low
drum 6-8 19-21 (unused). Flux marks the analysis frame, ~15 ms before the
strike, so each file starts 3 ms before the first-strike peak found after it
(never more than 2 ms after the steep front), with a 0.5 ms fade-in. Every
event, including ghost notes too quiet to be numbered (one sits 125 ms after
hit 23), ends the previous file: no open tone has more than ~120 ms of clean
ring in this loop. The engine plays up to 300 ms, so an open tone is continued
by its own ring model: three partials and one decay fitted by least squares to
its last clean 60 ms, crossfaded in over 10 ms. Everything is normalised to a
0.95 peak, mono, 48 kHz (PercussionEngine resamples to the device rate).
"""
import sys
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]

# dest stem -> hit index; an open tone gets the hit-23 tail when it is cut short
JOBS = {
    "open": 22, "open_b": 3, "open_med": 16, "open_soft": 23,
    "tumba": 9, "tumba_b": 10, "tumba_med": 15, "tumba_soft": 4,
    # 18 is a light, spiky slap: peak-normalised by the engine it came out
    # 6 dBA under the open tone at medium velocity (the percussionist heard
    # the stopped high conga as far too quiet). Without a _med take the engine
    # derives the medium layer from the full slap instead.
    "slap": 0, "slap_b": 12,
    "slap_closed": 0, "slap_closed_b": 12,
    # 11 is a palm then its body 30 ms later: heard 10 ms after every other
    # stroke, past what the attack compensation can carry. 2 is clean.
    "heel": 2,
    "toe": 13, "toe_b": 1,
    "muff": 17,
    "tapado": 14, "tapado_b": 5,
}
OPEN_TONES = {3, 4, 9, 10, 15, 16, 22, 23}
RING_SEC = 0.30        # PercussionEngine's maxRing
MAX_SEC = {"slap": 0.25, "muted": 0.20}


def load(path):
    w = wave.open(str(path))
    sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
    x = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float64) / 32768.0
    return sr, x.reshape(-1, ch).mean(axis=1)


def onsets(x, sr, k_std=2.5):
    hop, win = int(sr * 0.005), 1024
    frames = (len(x) - win) // hop
    hann = np.hanning(win)
    mag = np.array([np.abs(np.fft.rfft(x[i * hop:i * hop + win] * hann)) for i in range(frames)])
    flux = np.concatenate([[0.0], np.maximum(0, np.diff(np.log1p(mag * 50), axis=0)).sum(axis=1)])
    thr = np.median(flux) + k_std * np.std(flux)
    on = []
    for i in range(2, frames - 2):
        if flux[i] > thr and flux[i] == flux[i - 2:i + 3].max():
            if not on or (i - on[-1]) * hop > 0.045 * sr:
                on.append(i)
    return [o * hop for o in on]


def strike_start(x, sr, onset):
    lo, hi = max(0, onset - int(0.005 * sr)), onset + int(0.035 * sr)
    a = np.abs(x[lo:hi])
    front = lo + int(np.argmax(a > 0.5 * a.max()))
    # The asset convention (VPTests drum-strike): the first-strike peak sits
    # within 4 ms of the start. A hand on this conga takes ~5 ms from contact
    # to that peak, so keep 3 ms of the rise before it.
    first_peak = front + int(np.argmax(np.abs(x[front:front + int(0.010 * sr)])))
    return max(front - int(0.002 * sr), first_peak - int(0.003 * sr))


def extend_ring(y, sr, want):
    """Continue an open tone past its clean end with its own fitted ring."""
    n = len(y)
    seg_n = min(n - int(0.004 * sr), int(0.060 * sr))
    s0 = n - int(0.002 * sr) - seg_n
    seg = y[s0:s0 + seg_n]
    t = np.arange(seg_n) / sr
    blk = int(0.005 * sr)
    env = [np.sqrt((seg[i:i + blk] ** 2).mean() + 1e-12) for i in range(0, seg_n - blk, blk)]
    slope = np.polyfit(np.arange(len(env)) * blk / sr, np.log(env), 1)[0]
    alpha = float(np.clip(-slope, 3.0, 40.0))
    spec = np.abs(np.fft.rfft(seg * np.hanning(seg_n), 1 << 16))
    f = np.fft.rfftfreq(1 << 16, 1 / sr)
    band = np.where((f > 80) & (f < 2000))[0]
    order = band[np.argsort(spec[band])[::-1]]
    freqs = []
    for i in order:
        if all(abs(f[i] - g) > 40 for g in freqs):
            freqs.append(f[i])
        if len(freqs) == 3:
            break
    def basis(tt):
        d = np.exp(-alpha * tt)
        return np.column_stack([c for g in freqs for c in
                                (d * np.cos(2 * np.pi * g * tt), d * np.sin(2 * np.pi * g * tt))])
    coef, *_ = np.linalg.lstsq(basis(t), seg, rcond=None)
    tt = np.arange(want - s0) / sr
    model = basis(tt) @ coef
    out = np.concatenate([y[:s0], model])
    xf = int(0.010 * sr)
    j = n - int(0.002 * sr) - xf
    w = np.linspace(0.0, 1.0, xf)
    out[j:j + xf] = y[j:j + xf] * (1 - w) + model[j - s0:j - s0 + xf] * w
    return out


def fade_out(y, sr, sec):
    n = min(len(y), int(sec * sr))
    y[-n:] *= 0.5 * (1 + np.cos(np.linspace(0, np.pi, n)))
    return y


def main():
    src = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "Assets" / "Percussion"
    sr, x = load(src)
    on = onsets(x, sr)
    phrase = [o for o in on if o < 4.0 * sr + int(0.235 * sr)][:24]
    assert len(phrase) == 24, len(phrase)
    starts = [strike_start(x, sr, o) for o in phrase]
    # every event, ghosts included, bounds the file before it
    events = sorted(strike_start(x, sr, o) for o in onsets(x, sr, 0.8))
    ends = []
    for st in starts:
        later = [e for e in events if e > st + int(0.030 * sr)]
        ends.append((later[0] if later else len(x)) - int(0.001 * sr))

    for stem, k in JOBS.items():
        length = ends[k] - starts[k]
        y = x[starts[k]:ends[k]].copy()
        if k in OPEN_TONES:
            want = int(RING_SEC * sr)
            if length < want:
                y = extend_ring(y, sr, want)
            y = fade_out(y[:want], sr, 0.030)
        else:
            cap = MAX_SEC["slap"] if stem.startswith("slap") else MAX_SEC["muted"]
            y = fade_out(y[:int(cap * sr)], sr, 0.010)
        fi = int(0.0005 * sr)
        y[:fi] *= np.linspace(0.0, 1.0, fi)
        y *= 0.95 / max(1e-9, np.abs(y).max())
        pcm = np.clip(np.round(y * 32767), -32768, 32767).astype("<i2")
        with wave.open(str(out / f"{stem}.wav"), "wb") as f:
            f.setnchannels(1)
            f.setsampwidth(2)
            f.setframerate(sr)
            f.writeframes(pcm.tobytes())
        print(f"{stem:18s} hit {k:2d}  {1000 * len(y) / sr:5.0f} ms"
              + (f"  (pulito {1000 * length / sr:.0f} ms, coda modellata)"
                 if k in OPEN_TONES and length < int(RING_SEC * sr) else ""))


if __name__ == "__main__":
    main()
