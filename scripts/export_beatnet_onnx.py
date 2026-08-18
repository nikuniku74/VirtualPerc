#!/usr/bin/env python3
"""Export BeatNet BDA (GTZAN / Ballroom / Rock Corpus) to Assets/Models/beatnet.onnx.

Streaming graph: features [1,1,272], h0/c0 [2,1,150] → logits [1,3], hn, cn.
Weights: Heydari et al., CC-BY-4.0 (https://github.com/mjhydri/BeatNet).
"""
from __future__ import annotations

import os
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "Assets" / "Models" / "beatnet.onnx"
WEIGHTS_DIR = ROOT / "third_party" / "beatnet-weights"
WEIGHT_URL = (
    "https://github.com/mjhydri/BeatNet/raw/main/src/BeatNet/models/"
    "model_{n}_weights.pt"
)


def download_weights(model_id: int) -> Path:
    import shutil
    import subprocess

    WEIGHTS_DIR.mkdir(parents=True, exist_ok=True)
    dest = WEIGHTS_DIR / f"model_{model_id}_weights.pt"
    if dest.exists() and dest.stat().st_size > 1000:
        print("weights already at", dest)
        return dest
    url = WEIGHT_URL.format(n=model_id)
    print("GET", url)
    curl = shutil.which("curl")
    if curl:
        subprocess.check_call([curl, "-L", "--fail", "--retry", "3", "-o", str(dest), url])
    else:
        urllib.request.urlretrieve(url, dest)
    return dest


def export(model_id: int) -> int:
    import torch
    import torch.nn as nn
    import torch.nn.functional as F

    class BDA(nn.Module):
        def __init__(self):
            super().__init__()
            self.dim_in = 272
            self.dim_hd = 150
            self.num_layers = 2
            self.conv_out = 150
            self.kernelsize = 10
            self.conv1 = nn.Conv1d(1, 2, self.kernelsize)
            self.linear0 = nn.Linear(2 * int((self.dim_in - self.kernelsize + 1) / 2), self.conv_out)
            self.lstm = nn.LSTM(
                input_size=self.conv_out,
                hidden_size=self.dim_hd,
                num_layers=self.num_layers,
                batch_first=True,
                bidirectional=False,
            )
            self.linear = nn.Linear(self.dim_hd, 3)

        def forward(self, features, h0, c0):
            x = features.reshape(-1, self.dim_in).unsqueeze(1)
            x = F.max_pool1d(F.relu(self.conv1(x)), 2)
            x = torch.flatten(x, 1)
            x = self.linear0(x)
            x = x.reshape(features.shape[0], features.shape[1], self.conv_out)
            x, (hn, cn) = self.lstm(x, (h0, c0))
            logits = self.linear(x)
            return logits[:, -1, :], hn, cn

    path = download_weights(model_id)
    model = BDA()
    state = torch.load(path, map_location="cpu", weights_only=False)
    missing, unexpected = model.load_state_dict(state, strict=False)
    print("loaded", path.name, "missing", missing, "unexpected", unexpected)
    model.eval()

    dummy = torch.zeros(1, 1, 272)
    h0 = torch.zeros(2, 1, 150)
    c0 = torch.zeros(2, 1, 150)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    kw = dict(
        input_names=["features", "h0", "c0"],
        output_names=["logits", "hn", "cn"],
        opset_version=17,
    )
    with torch.no_grad():
        try:
            torch.onnx.export(model, (dummy, h0, c0), str(OUT), dynamo=False, **kw)
        except TypeError:
            torch.onnx.export(model, (dummy, h0, c0), str(OUT), **kw)

    print("wrote", OUT, "bytes", OUT.stat().st_size)
    try:
        import onnx

        onnx.checker.check_model(str(OUT))
        print("onnx checker: ok")
    except Exception as exc:
        print("onnx checker skipped:", exc)
    return 0


def main() -> int:
    try:
        import torch  # noqa: F401
    except ImportError:
        print("Need torch (+ onnx): python3 -m pip install torch onnx", file=sys.stderr)
        return 1
    model_id = int(os.environ.get("VP_BEATNET_MODEL", "1"))
    if model_id not in (1, 2, 3):
        print("VP_BEATNET_MODEL must be 1 (GTZAN), 2 (Ballroom), or 3 (Rock)", file=sys.stderr)
        return 1
    return export(model_id)


if __name__ == "__main__":
    raise SystemExit(main())
