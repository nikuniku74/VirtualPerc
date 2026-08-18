#!/usr/bin/env python3
"""Train a tiny causal beat TCN on synthetic clicks and export Assets/Models/beatnet.onnx.

Feature recipe matches Source/AI/LogSpectFeatures.cpp:
  sr=22050, n_fft=2048, hop=441, 136 log-spaced bands 30–17000 Hz + first difference.
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "Assets" / "Models" / "beatnet.onnx"

SR = 22050
N_FFT = 2048
HOP = 441
BANDS = 136
FMIN = 30.0
FMAX = 17000.0
WIN_T = 64
DIM = BANDS * 2


def hann(n: int):
    import numpy as np
    return 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(n) / max(n - 1, 1))


def filterbank(sr: float):
    import numpy as np
    n_bins = N_FFT // 2
    log_min, log_max = math.log(FMIN), math.log(min(FMAX, sr * 0.5 - 1.0))
    w = np.zeros((BANDS, n_bins), dtype=np.float32)
    bin_hz = sr / N_FFT
    for b in range(BANDS):
        c0 = log_min + (log_max - log_min) * max(0, b - 1) / BANDS
        c1 = log_min + (log_max - log_min) * b / BANDS
        c2 = log_min + (log_max - log_min) * (b + 1) / BANDS
        f0, f1, f2 = math.exp(c0), math.exp(c1), math.exp(c2)
        for k in range(1, n_bins):
            hz = k * bin_hz
            if f0 <= hz <= f1 and f1 > f0:
                w[b, k] = (hz - f0) / (f1 - f0)
            elif f1 < hz <= f2 and f2 > f1:
                w[b, k] = (f2 - hz) / (f2 - f1)
    return w


def log_spect(mono, sr: float, bank):
    import numpy as np
    win = hann(N_FFT).astype(np.float32)
    n = len(mono)
    frames = []
    prev = None
    pos = 0
    while pos + N_FFT <= n:
        chunk = np.array(mono[pos : pos + N_FFT], dtype=np.float32) * win
        spec = np.fft.rfft(chunk, n=N_FFT)
        mag = np.abs(spec[: N_FFT // 2]).astype(np.float32)
        bands = np.log(np.maximum(bank @ (mag * mag), 1e-8))
        diff = np.zeros_like(bands) if prev is None else bands - prev
        prev = bands
        frames.append(np.concatenate([bands, diff]))
        pos += HOP
    if not frames:
        return np.zeros((1, DIM), dtype=np.float32)
    return np.stack(frames, axis=0)


def synth_engine_click(sr: float, seconds: float, bpm: float, rng, hats: bool, noise: float):
    """Match VirtualPercussionEngine::maybeInjectClick (kick + optional 8th hats)."""
    import numpy as np
    n = int(sr * seconds)
    x = rng.normal(0, noise, n).astype(np.float32) if noise > 0 else np.zeros(n, dtype=np.float32)
    inc = (bpm / 60.0) / sr
    ph = 0.0
    beats = []
    last_beat = -1
    for i in range(n):
        beat = ph - math.floor(ph)
        eighth = beat * 2.0 - math.floor(beat * 2.0)
        bi = int(math.floor(ph))
        if bi != last_beat:
            beats.append(i / sr)
            last_beat = bi
        if beat < 0.045:
            t = beat / (bpm / 60.0)
            x[i] += math.sin(2 * math.pi * 55 * t) * math.exp(-t * 28)
        if hats and eighth < 0.020:
            t = eighth
            x[i] += float(rng.uniform(-0.5, 0.5)) * math.exp(-t * 90) * 0.35
        ph += inc
    return x, beats


def synth_click(sr: float, seconds: float, bpm: float, rng):
    import numpy as np
    n = int(sr * seconds)
    x = rng.normal(0, 0.01, n).astype(np.float32)
    period = 60.0 / bpm
    t = 0.0
    beats = []
    while t < seconds:
        i0 = int(t * sr)
        beats.append(t)
        for i in range(i0, min(n, i0 + int(0.012 * sr))):
            dt = (i - i0) / sr
            x[i] += 0.9 * math.sin(2 * math.pi * 55 * dt) * math.exp(-dt * 28)
        t += period
    return x, beats


def windows_and_labels(feat, beats, sr: float):
    import numpy as np
    hop_s = HOP / sr
    labels = []
    for i in range(len(feat)):
        t = (i + 0.5) * hop_s
        lab = 2  # none
        for bi, bt in enumerate(beats):
            if abs(t - bt) <= hop_s * 0.6:
                lab = 1 if bi % 4 == 0 else 0
                break
        labels.append(lab)
    labels = np.array(labels, dtype=np.int64)
    xs, ys = [], []
    for i in range(WIN_T, len(feat)):
        xs.append(feat[i - WIN_T : i])
        ys.append(labels[i - 1])
    return np.stack(xs), np.array(ys)


def train_torch(xs, ys, out_path: Path) -> None:
    import torch
    import torch.nn as nn

    class TinyBeat(nn.Module):
        def __init__(self):
            super().__init__()
            self.pad = nn.ConstantPad1d((4, 0), 0.0)
            self.c1 = nn.Conv1d(DIM, 48, 5)
            self.c2 = nn.Conv1d(48, 48, 5)
            self.c3 = nn.Conv1d(48, 3, 1)

        def forward(self, x):
            x = x.transpose(1, 2)
            x = torch.relu(self.c1(self.pad(x)))
            x = torch.relu(self.c2(self.pad(x)))
            x = self.c3(x)
            return x[:, :, -1]

    device = torch.device("cpu")
    model = TinyBeat().to(device)
    opt = torch.optim.Adam(model.parameters(), lr=1e-3)
    w = torch.tensor([8.0, 12.0, 1.0])
    loss_fn = nn.CrossEntropyLoss(weight=w)
    xt = torch.from_numpy(xs)
    yt = torch.from_numpy(ys)
    model.train()
    batch = 64
    for epoch in range(24):
        perm = torch.randperm(xt.size(0))
        total = 0.0
        n = 0
        for i in range(0, xt.size(0), batch):
            idx = perm[i : i + batch]
            pred = model(xt[idx])
            loss = loss_fn(pred, yt[idx])
            opt.zero_grad()
            loss.backward()
            opt.step()
            total += float(loss.detach()) * idx.numel()
            n += idx.numel()
        print(f"epoch {epoch + 1}  loss={total / max(n, 1):.4f}")

    model.eval()
    dummy = torch.zeros(1, WIN_T, DIM)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    export_kw = dict(
        input_names=["features"],
        output_names=["logits"],
        opset_version=17,
    )
    try:
        torch.onnx.export(model, dummy, str(out_path), dynamo=False, **export_kw)
    except TypeError:
        torch.onnx.export(model, dummy, str(out_path), **export_kw)
    print("wrote", out_path)


def main() -> int:
    try:
        import numpy as np
        import torch  # noqa: F401
    except ImportError:
        print("Need numpy, torch, and onnx: python3 -m pip install numpy torch onnx", file=sys.stderr)
        return 1

    rng = np.random.default_rng(0)
    bank = filterbank(SR)
    xs_all, ys_all = [], []
    for bpm in (80, 96, 100, 110, 120, 128, 140, 160):
        for _ in range(2):
            audio, beats = synth_click(SR, 5.0, float(bpm), rng)
            feat = log_spect(audio, SR, bank)
            x, y = windows_and_labels(feat, beats, SR)
            xs_all.append(x)
            ys_all.append(y)
        for hats, noise in ((False, 0.0), (False, 0.01), (True, 0.008)):
            audio, beats = synth_engine_click(SR, 5.0, float(bpm), rng, hats, noise)
            feat = log_spect(audio, SR, bank)
            x, y = windows_and_labels(feat, beats, SR)
            xs_all.append(x)
            ys_all.append(y)
    xs = np.concatenate(xs_all, axis=0)
    ys = np.concatenate(ys_all, axis=0)
    print(f"train windows={len(ys)}  beat_frac={(ys != 2).mean():.3f}")
    train_torch(xs, ys, OUT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
