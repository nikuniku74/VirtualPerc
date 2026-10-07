#!/usr/bin/env python3
"""Fine-tune BeatNet (GTZAN weights) on the teacher's beats, for live mixer-send audio.

    train_beatnet_finetune.py NAME [--steps N] [--kd W] [--lr X] [--kdoff W] [--balance P] [--seed S] [--data DIR]

Data (docs/TODO.md item 87), per recording in DIR (default ~/vp-train/wav):
  X.f32            the app's own 272-d frames (`VPActivations --features`), so the
                   network is trained on exactly what LogSpectFeatures computes
  X.act            BeatNet's original outputs on the same frames (the `--kd` anchor)
  X.wav.truth.txt  the offline teacher's beats and ones (`truth.py make --no-clicks`)
The bench songs and the Flamingo / Garden Beach sets are the exam and never go here.

The last 10% of every recording is held out. Printed on it, for the original
network and the fine-tuned one: beat F-measure (activation peaks over 0.3 within
70 ms) and the share of bars whose strongest downbeat sits on the true one.

Writes ~/vp-train/models/NAME.onnx with BeatNet's streaming signature, so the app
runs it through VP_BEAT_MODEL with nothing else changed.
"""
import glob, os, sys
import numpy as np
import torch
import torch.nn.functional as F

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from export_beatnet_onnx import WEIGHTS_DIR, export_model, make_bda  # noqa: E402

FPS = 50.0
# BeatNet's peak for a beat sits 46.5 ms before the teacher's time on the
# frame-index clock (median of 26 000 beats on this data, every recording
# within 41-52 ms): the frame is dated by the start of its window. Labels
# carry the same shift, so a fine-tuned peak lands where the app's latency
# compensation expects BeatNet's to.
LABEL_SHIFT_SEC = -0.0465
HOLD_OUT = 0.10


def load(data):
    tracks = []
    for f in sorted(glob.glob(os.path.join(data, '*.f32'))):
        base = f[:-4]
        # Read from disk as needed: loading every recording at once (9.6 h at four
        # analysis levels, ~10 GB) filled a 19 GB Mac's swap and froze it.
        X = np.memmap(f, dtype=np.float32, mode='r').reshape(-1, 272)
        A = np.loadtxt(base + '.act', comments='#', usecols=(1, 2)).astype(np.float32)
        T = np.loadtxt(base + '.wav.truth.txt', ndmin=2)
        n = len(X)
        # soft targets: the labelled frame 1.0, its neighbours 0.5
        Y = np.zeros((n, 3), np.float32)
        k = np.round((T[:, 0] + LABEL_SHIFT_SEC) * FPS).astype(int)
        cls = np.where(T[:, 1] > 0.5, 1, 0)
        for off, w in ((-1, 0.5), (1, 0.5), (0, 1.0)):
            kk = k + off
            ok = (kk >= 0) & (kk < n)
            Y[kk[ok], cls[ok]] = np.maximum(Y[kk[ok], cls[ok]], w)
        Y[:, 2] = 1.0 - Y[:, :2].sum(1)
        orig = np.clip(np.stack([A[:, 0], A[:, 1], 1 - A.sum(1)], 1), 1e-4, 1)  # .act prints 4 decimals: no log(0)
        orig /= orig.sum(1, keepdims=True)
        cut = int(n * (1 - HOLD_OUT))
        tracks.append(dict(name=os.path.basename(base), X=X, Y=Y, orig=orig, T=T, k=k, cut=cut))
    return tracks


def run_seq(model, X, dev):
    # In blocks with the state carried, as the app streams it: on MPS one LSTM
    # call over a two-hour set (340 000 frames) returned outputs 0.8 away from
    # the app's, while 42 000 frames still matched to 5e-5.
    out = []
    with torch.no_grad():
        h = torch.zeros(2, 1, 150, device=dev)
        c = h.clone()
        for s in range(0, len(X), 10000):
            lg, h, c = model.sequence(torch.from_numpy(X[s:s + 10000])[None].to(dev), h, c)
            out.append(torch.softmax(lg, -1)[0].cpu().numpy())
    return np.concatenate(out)


def evaluate(P, tr):
    """Beat F and per-bar one accuracy on the held-out tail."""
    lo = tr['cut']
    a = P[:, 0] + P[:, 1]
    pk = np.nonzero((a[1:-1] > 0.3) & (a[1:-1] >= a[:-2]) & (a[1:-1] > a[2:]))[0] + 1
    pk = pk[pk >= lo]
    k, T = tr['k'], tr['T']
    true = k[(k >= lo) & (k < len(a))]
    if len(true) == 0 or len(pk) == 0:
        return np.nan, np.nan
    tol = 0.07 * FPS
    i = np.clip(np.searchsorted(pk, true), 1, len(pk) - 1)
    hit = np.minimum(np.abs(pk[i] - true), np.abs(pk[i - 1] - true)) <= tol
    j = np.clip(np.searchsorted(true, pk), 1, len(true) - 1)
    good = np.minimum(np.abs(true[j] - pk), np.abs(true[j - 1] - pk)) <= tol
    rec, prec = hit.mean(), good.mean()
    f = 2 * rec * prec / max(1e-9, rec + prec)
    d = P[:, 1]
    win = lambda kk: d[max(0, kk - 2):kk + 3].max()
    downs = np.nonzero((T[:, 1] > 0.5) & (k >= lo))[0]
    bars = [int(np.argmax([win(k[i0 + m]) for m in range(4)]) == 0)
            for i0 in downs if i0 + 3 < len(k) and k[i0 + 3] < len(d)]
    return f, float(np.mean(bars)) if bars else np.nan


def main():
    args = sys.argv[1:]
    if not args or args[0].startswith('-'):
        print(__doc__)
        return 1
    name = args[0]
    opt = dict(steps=3000, kd=1.0, kdoff=-1.0, lr=1e-4, balance=1.0, seed=0, data=os.path.expanduser('~/vp-train/wav'))
    for a, v in zip(args[1::2], args[2::2]):
        key = a.lstrip('-')
        opt[key] = type(opt[key])(v) if key != 'data' else v
    torch.manual_seed(opt['seed'])
    np.random.seed(opt['seed'])
    dev = 'mps' if torch.backends.mps.is_available() else 'cpu'
    tracks = load(opt['data'])
    model = make_bda()
    model.load_state_dict(torch.load(WEIGHTS_DIR / 'model_1_weights.pt', map_location='cpu',
                                     weights_only=False), strict=False)
    model.to(dev)

    def report(tag):
        model.eval()
        rows = []
        for tr in tracks:
            if len(tr['X']) - tr['cut'] < 500:
                continue
            P = run_seq(model, tr['X'], dev)
            rows.append((len(tr['X']) - tr['cut'], *evaluate(P, tr)))
        r = np.array(rows)
        w = r[:, 0] / r[:, 0].sum()
        print(f'{tag:>10s}  beat F {np.nansum(w * r[:, 1]):.3f}  uno per battuta {np.nansum(w * r[:, 2]):.3f}',
              flush=True)

    report('BeatNet')
    seq, batch = 1000, 16
    weights = np.array([max(0, t['cut'] - seq) for t in tracks], float)
    # --balance 0.5: pick recordings by sqrt(length), so 13 studio songs are not
    # drowned by 8.5 hours of gigs; 1.0 is plain length.
    weights = weights ** opt['balance']
    weights /= weights.sum()
    optim = torch.optim.Adam(model.parameters(), lr=opt['lr'])
    for step in range(1, opt['steps'] + 1):
        model.train()
        xs, ys, os_ = [], [], []
        for ti in np.random.choice(len(tracks), batch, p=weights):
            tr = tracks[ti]
            s = np.random.randint(0, tr['cut'] - seq)
            xs.append(tr['X'][s:s + seq]); ys.append(tr['Y'][s:s + seq]); os_.append(tr['orig'][s:s + seq])
        x = torch.from_numpy(np.stack(xs)).to(dev)
        y = torch.from_numpy(np.stack(ys)).to(dev)
        o = torch.from_numpy(np.stack(os_)).to(dev)
        h = torch.zeros(2, batch, 150, device=dev)
        lg, _, _ = model.sequence(x, h, h.clone())
        logp = F.log_softmax(lg, -1)
        kl = (o * (o.log() - logp)).sum(-1)
        if opt['kdoff'] >= 0:
            # --kdoff W: hold the original outputs (weight --kd) only within three
            # frames of a teacher beat, W elsewhere. The anchor keeps the peak
            # shape the decoder is tuned on; away from the beats it was also
            # keeping BeatNet's off-beat peaks, which on slow songs make it read
            # the eighths (UN ORA SOLA: 1.76 peaks per true beat, played at 153).
            near = F.max_pool1d(y[..., :2].sum(-1, keepdim=True).transpose(1, 2), 7, 1, 3).transpose(1, 2)[..., 0] > 0
            kl = kl * torch.where(near, opt['kd'], opt['kdoff'])
        else:
            kl = kl * opt['kd']
        loss = -(y * logp).sum(-1).mean() + kl.mean()
        optim.zero_grad()
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        optim.step()
        if step % 500 == 0:
            print(f'step {step} loss {loss.item():.4f}', flush=True)
            report(f'{step}')
    out = os.path.expanduser(f'~/vp-train/models/{name}.onnx')
    model.to('cpu')
    from pathlib import Path
    export_model(model, Path(out))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
