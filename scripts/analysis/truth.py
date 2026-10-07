#!/usr/bin/env python3
"""Verità dei battiti da una rete offline, e quanto l'app ci sta sopra (item 87).

    truth.py make [--no-clicks] [WAV ...] battiti e uno -> WAV.truth.txt, e ~/vp-bench/clicks/WAV da ascoltare
    truth.py score TAG [TAG2 ...] i pulses del banco veloce (bench_fast.py run TAG) contro la verità

Il maestro è Beat This! (CPJKU, ISMIR 2024): non causale, quindi gira solo qui e
mai nell'app. `make` va lanciato con il venv ~/.venvs/vp-teacher; `score` con il
python di sistema, come bench_fast.py. Senza WAV, `make` prende tutto
~/vp-bench/wav; i clic vanno fuori da quella cartella, perché bench_fast.py esegue ogni WAV che ci trova; i clic si scrivono una volta per brano: se c'è la versione a
44.1 kHz, la 48k (stesso audio, stessi battiti) si salta.

Formato: quello della Parte 2 di docs/HANDOFF_LIVE_TRACKING.md, una riga per
battito, secondi dall'inizio del file e `1` sull'uno. È la lettura del maestro
finché l'utente non ha ascoltato i clic: i tratti che segnala sbagliati si tolgono
dal .truth.txt (le righe mancanti non contano, il punteggio lo regge).

`score`, solo con la parte che suona e dopo 20 s (come bench_fast.py), su ogni
battito vero:
  giusto / ottava / sbagl  quota di battiti con il tempo del clock entro il 4% del
                           tempo vero, a ×2 o ÷2, o altrove
  anticipo                 scarto mediano clock-verità in ms (taratura: + = clock in anticipo sul battito vero)
  disp / p90               scarto dal mediano, mediana e 90° percentile (inseguimento)
  >25ms                    quota di battiti oltre 25 ms dal mediano
  uno                      quota di uni veri su cui il clock conta l'uno (entro mezzo battito)
  grigl.disp / >25%        le stesse due misure sulla griglia pubblicata dal decoder (fase -
                           phaseErr): quello che il clock perde inseguendola è la differenza
Fase e uno solo dove l'ottava è giusta: a ×2 i battiti veri cadono comunque sui
battiti del clock e la fase sembrerebbe buona.
"""
import os, sys, wave
import numpy as np

CACHE = os.path.expanduser('~/vp-bench')


def make(wavs, clicks_wanted=True):
    import torch
    from beat_this.inference import Audio2Beats
    dev = 'mps' if torch.backends.mps.is_available() else 'cpu'
    a2b = Audio2Beats(checkpoint_path='final0', device=dev, dbn=False)
    for w in wavs:
        out = w + '.truth.txt'
        if not os.path.exists(out):
            # In 5-minute pieces with 10 s of context either side: a two-hour set
            # loaded and transformed whole took the machine down (2026-10-02).
            import soundfile as sf
            sr = sf.info(w).samplerate
            total, body, pad = sf.info(w).frames, 300 * sr, 10 * sr
            parts = ([], [])
            for start in range(0, total, body):
                a0 = max(0, start - pad)
                x, _ = sf.read(w, start=a0, stop=min(total, start + body + pad), dtype='float32', always_2d=True)
                fr = [y.cpu().numpy() for y in a2b.spect2frames(a2b.signal2spect(x.mean(1), sr))]
                lo = round((start - a0) / sr * 50)
                hi = lo + round((min(total, start + body) - start) / sr * 50)
                for p, y in zip(parts, fr):
                    p.append(y[lo:hi])
            logits, dlogits = (np.concatenate(p) for p in parts)
            beats, downs = a2b.frames2beats(*(torch.from_numpy(x) for x in (logits, dlogits)))
            # The network runs at 50 fps, so its beats sit on a 20 ms grid: ±10 ms
            # of rounding in the truth itself. A parabola through the logit peak
            # and its two neighbours puts each beat between frames.
            k = np.clip(np.round(beats * 50).astype(int), 1, len(logits) - 2)
            a, b, c = logits[k - 1], logits[k], logits[k + 1]
            den = a - 2 * b + c
            frac = np.clip(np.where(den < 0, 0.5 * (a - c) / np.where(den < 0, den, 1), 0), -0.5, 0.5)
            d = set(np.round(downs * 50).astype(int))
            with open(out, 'w') as f:
                for kk, fr in zip(k, frac):
                    f.write(f'{(kk + fr) / 50:.4f} {int(kk in d)}\n')
            print(f'{os.path.basename(w)}: {len(beats)} battiti, {len(downs)} uni')
        ck = f'{CACHE}/clicks/' + os.path.basename(w)
        if clicks_wanted and not os.path.exists(w.replace('_48k.wav', '_44k.wav') if w.endswith('_48k.wav') else '') \
                and not os.path.exists(ck):
            clicks(w, ck, np.loadtxt(out, ndmin=2))


def clicks(w, ck, T):
    r = wave.open(w, 'rb')
    sr, nch = r.getframerate(), r.getnchannels()
    x = np.frombuffer(r.readframes(r.getnframes()), dtype=np.int16).astype(np.float32)
    x = x.reshape(-1, nch).mean(1) * 0.6
    n = int(0.03 * sr)
    env = np.exp(-np.arange(n) / (0.006 * sr))
    for t, one in T:
        i = int(t * sr)
        c = np.sin(2 * np.pi * (2000 if one > 0.5 else 1000) * np.arange(n) / sr) * env * 16000
        x[i:i + n] += c[:max(0, min(n, len(x) - i))]
    os.makedirs(os.path.dirname(ck), exist_ok=True)
    o = wave.open(ck, 'wb')
    o.setnchannels(1); o.setsampwidth(2); o.setframerate(sr)
    o.writeframes(np.clip(x, -32768, 32767).astype(np.int16).tobytes())


def score(pul, truth):
    t, ph, bar, clk, snd, perr = np.loadtxt(pul, comments='#', usecols=(0, 1, 2, 4, 5, 6)).T
    T = np.loadtxt(truth, ndmin=2)
    tb, down = T[:, 0], T[:, 1] > 0.5
    tbpm = 60 / np.gradient(tb)
    unwrap = lambda p: np.unwrap(p * 2 * np.pi) / (2 * np.pi)
    wrap = lambda p: (p + 0.5) % 1 - 0.5
    c, s = np.interp(tb, t, clk), np.interp(tb, t, snd)
    m = (s > 0.5) & (tb > 20) & (tb < t[-1])
    r = c / tbpm
    ok = m & (np.abs(r - 1) < 0.04)
    octv = m & ((np.abs(r - 2) < 0.08) | (np.abs(r - 0.5) < 0.02))
    err = wrap(np.interp(tb, t, unwrap(ph))) * 60000 / c
    e = err[ok]
    off = float(np.median(e)) if len(e) else np.nan
    dv = np.abs(e - off)
    g = (wrap(np.interp(tb, t, unwrap(ph) - perr)) * 60000 / c)[ok]   # the decoder's grid (onset_fit --target)
    gv = np.abs(g - np.median(g)) if len(g) else g
    bw = np.abs(wrap(np.interp(tb, t, unwrap(bar))))[ok & down]
    n = max(1, int(m.sum()))
    return dict(n=int(m.sum()), ok=ok.sum() / n * 100, oct=octv.sum() / n * 100,
                wrong=(m.sum() - ok.sum() - octv.sum()) / n * 100, off=off,
                disp=float(np.median(dv)) if len(dv) else np.nan,
                p90=float(np.percentile(dv, 90)) if len(dv) else np.nan,
                p25=float(np.mean(dv > 25) * 100) if len(dv) else np.nan,
                one=float(np.mean(bw < 0.125) * 100) if len(bw) else np.nan,
                gdisp=float(np.median(gv)) if len(gv) else np.nan,
                g25=float(np.mean(gv > 25) * 100) if len(gv) else np.nan)


def report(tags):
    keys = ('ok', 'oct', 'wrong', 'off', 'disp', 'p90', 'p25', 'one', 'gdisp', 'g25')
    head = f"{'':28s} {'giusto%':>8s} {'ottava%':>8s} {'sbagl%':>7s} {'anticipo':>9s} {'disp':>6s} {'p90':>6s} {'>25ms%':>7s} {'uno%':>6s} {'grigl.disp':>10s} {'>25%':>5s}"
    fmt = lambda name, v: (f"{name:28s} {v['ok']:8.1f} {v['oct']:8.1f} {v['wrong']:7.1f} {v['off']:9.1f} "
                           f"{v['disp']:6.1f} {v['p90']:6.1f} {v['p25']:7.1f} {v['one']:6.1f} {v['gdisp']:10.1f} {v['g25']:5.1f}")
    for tag in tags:
        rows = {}
        for f in sorted(os.listdir(f'{CACHE}/{tag}')):
            truth = f'{CACHE}/wav/{f[:-4]}.wav.truth.txt'
            if f.endswith('.pul') and os.path.exists(truth):
                rows[f[:-4]] = score(f'{CACHE}/{tag}/{f}', truth)
        print(f'== {tag} ({len(rows)} file con verità)\n{head}')
        for name, v in rows.items():
            print(fmt(name[:28], v))
        w = np.array([v['n'] for v in rows.values()], float)
        mean = {k: float(np.nansum([v[k] * x for v, x in zip(rows.values(), w)]) / max(1, w.sum())) for k in keys}
        print(fmt('MEDIA (pesata sui battiti)', mean))


if __name__ == '__main__':
    if len(sys.argv) < 2 or sys.argv[1] not in ('make', 'score'):
        print(__doc__)
        sys.exit(1)
    if sys.argv[1] == 'make':
        nc = '--no-clicks' in sys.argv
        args = [a for a in sys.argv[2:] if a != '--no-clicks']
        wavs = args or sorted(f'{CACHE}/wav/{f}' for f in os.listdir(f'{CACHE}/wav') if f.endswith('.wav')
                                      )
        make(wavs, not nc)
    else:
        report(sys.argv[2:])
