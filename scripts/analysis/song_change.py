#!/usr/bin/env python3
"""Cambio di brano senza STOP (docs/TODO.md item 110): quanto ci mette il clock a
prendere il tempo del brano nuovo quando la band attacca il successivo sulla
stessa mandata e la parte resta accesa.

    song_change.py make                 coppie in ~/vp-bench/chg/wav (+ .truth.txt)
    song_change.py run TAG [VAR=VAL..]  VPTrack su ogni coppia (MIXER, -12 dB)
    song_change.py score TAG [TAG2..]   secondi per seguire il brano B, % giusto su B

Le coppie stanno fuori da ~/vp-bench/wav, che bench_fast.py esegue per intero.
A = 60 s dal secondo 40 del brano A; poi 3 s di silenzio (variante `gap`) o
nessuno (variante `seg`, dissolvenza 0.3 s); B = i primi 90 s del brano B.
Ordine delle coppie fisso (seme 7): ogni brano è una volta A e una volta B.
"""
import os, sys, wave, subprocess
import numpy as np
from concurrent.futures import ThreadPoolExecutor

CACHE = os.path.expanduser('~/vp-bench')
OUT = f'{CACHE}/chg'
VPTRACK = os.path.join(os.path.dirname(__file__), '../../build-host/VPTrack_artefacts/Release/VPTrack')
A_FROM, A_LEN, GAP, B_LEN = 40.0, 60.0, 3.0, 90.0


def songs():
    w = sorted(f for f in os.listdir(f'{CACHE}/wav') if f.endswith('_48k.wav')
               and os.path.exists(f'{CACHE}/wav/{f}.truth.txt') and 'UN_ORA_SOLA' not in f)
    rng = np.random.default_rng(7)
    return [w[i] for i in rng.permutation(len(w))]


def read(path):
    r = wave.open(path, 'rb')
    sr, ch = r.getframerate(), r.getnchannels()
    x = np.frombuffer(r.readframes(r.getnframes()), dtype=np.int16).reshape(-1, ch)
    return sr, x


def make():
    os.makedirs(f'{OUT}/wav', exist_ok=True)
    s = songs()
    for i, a in enumerate(s):
        b = s[(i + 1) % len(s)]
        sr, xa = read(f'{CACHE}/wav/{a}')
        _, xb = read(f'{CACHE}/wav/{b}')
        ta = np.loadtxt(f'{CACHE}/wav/{a}.truth.txt', ndmin=2)
        tb = np.loadtxt(f'{CACHE}/wav/{b}.truth.txt', ndmin=2)
        if len(xa) < (A_FROM + A_LEN) * sr:
            continue
        segA = xa[int(A_FROM * sr):int((A_FROM + A_LEN) * sr)].astype(np.float32)
        segB = xb[:int(B_LEN * sr)].astype(np.float32)
        for kind in ('gap', 'seg'):
            fade = int(0.3 * sr)
            A = segA.copy()
            A[-fade:] *= np.linspace(1, 0, fade)[:, None]
            gap = GAP if kind == 'gap' else 0.0
            mid = np.zeros((int(gap * sr), A.shape[1]), np.float32)
            y = np.concatenate([A, mid, segB])
            name = f'{i:02d}_{kind}_{a[:-8]}__{b[:-8]}'
            o = wave.open(f'{OUT}/wav/{name}.wav', 'wb')
            o.setnchannels(y.shape[1]); o.setsampwidth(2); o.setframerate(sr)
            o.writeframes(np.clip(y, -32768, 32767).astype(np.int16).tobytes())
            tc = A_LEN + gap
            ra = ta[(ta[:, 0] >= A_FROM) & (ta[:, 0] < A_FROM + A_LEN)].copy(); ra[:, 0] -= A_FROM
            rb = tb[tb[:, 0] < B_LEN].copy(); rb[:, 0] += tc
            np.savetxt(f'{OUT}/wav/{name}.wav.truth.txt', np.vstack([ra, rb]), fmt='%.4f %d')
            with open(f'{OUT}/wav/{name}.change', 'w') as f:
                f.write(f'{tc:.3f}\n')
    print(len(os.listdir(f'{OUT}/wav')) // 3, 'coppie')


def run(tag, pairs):
    env = dict(os.environ, VP_OFFLINE_PACING='1')
    args = ['--gain', '-12']
    for p in pairs:
        k, v = p.split('=', 1)
        if k == 'ARGS':
            args = v.split()
        else:
            env[k] = v
    os.makedirs(f'{OUT}/{tag}', exist_ok=True)
    wavs = sorted(f for f in os.listdir(f'{OUT}/wav') if f.endswith('.wav'))

    def one(w):
        # {tc} in ARGS is where B starts, {ta} where A ends: ARGS="--gain -12 --stop-at {ta} --stop-gap 3.5"
        # is the same change with the listener pressing STOP and START.
        tc = open(f'{OUT}/wav/{w[:-4]}.change').read().strip()
        a = [x.replace('{tc}', tc).replace('{ta}', f'{A_LEN:.3f}') for x in args]
        subprocess.run([VPTRACK, '--wav', f'{OUT}/wav/{w}', *a, '--pulses',
                        f'{OUT}/{tag}/{w[:-4]}.pul'], env=env, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    with ThreadPoolExecutor(int(os.environ.get('JOBS', 6))) as ex:
        list(ex.map(one, wavs))


def follow(pul, name):
    t, clk, snd = np.loadtxt(pul, comments='#', usecols=(0, 4, 5)).T
    T = np.loadtxt(f'{OUT}/wav/{name}.wav.truth.txt', ndmin=2)[:, 0]
    tc = float(open(f'{OUT}/wav/{name}.change').read())
    tb = T[T > tc + 1]
    vb = 60 / np.median(np.diff(tb[tb < tc + 40])) if len(tb) > 8 else np.nan
    m = t > tc
    right = (snd > 0.5) & np.any([np.abs(clk * k / vb - 1) < 0.04 for k in (1, 2, 0.5)], axis=0)
    # first time after the change from which the clock is right for 4 s straight
    dt = np.median(np.diff(t)); need = int(4.0 / dt)
    idx = np.nonzero(m)[0]
    when = np.nan
    run_ = 0
    for i in idx:
        run_ = run_ + 1 if right[i] else 0
        if run_ >= need:
            when = t[i - need + 1] - tc
            break
    tail = m & (t > tc + 20) & (snd > 0.5)
    pct = float(np.mean(right[tail]) * 100) if tail.any() else np.nan
    return when, pct, vb


def score(tags):
    names = sorted(f[:-4] for f in os.listdir(f'{OUT}/{tags[0]}') if f.endswith('.pul'))
    print(f"{'':48s}" + ''.join(f'{t:>18s}' for t in tags))
    agg = {t: [] for t in tags}
    for n in names:
        row = f'{n[:48]:48s}'
        for t in tags:
            w, p, vb = follow(f'{OUT}/{t}/{n}.pul', n)
            agg[t].append((w, p))
            row += f"  {('mai' if np.isnan(w) else f'{w:5.1f}s'):>6s} {p:5.1f}%   "
        print(row)
    for kind in ('gap', 'seg', ''):
        sel = [i for i, n in enumerate(names) if kind in n]
        row = f"{'MEDIA ' + (kind or 'tutte'):48s}"
        for t in tags:
            w = np.array([agg[t][i][0] for i in sel]); p = np.array([agg[t][i][1] for i in sel])
            capped = np.where(np.isnan(w), 90.0, w)
            row += f"  {np.median(capped):5.1f}s {np.nanmean(p):5.1f}% m{int(np.isnan(w).sum())}"
        print(row)
    print('(secondi = mediana del tempo per seguire B, «mai» contato 90 s; % = giusto su B dopo 20 s; m = mai)')


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'make': make()
    elif cmd == 'run': run(sys.argv[2], sys.argv[3:])
    elif cmd == 'score': score(sys.argv[2:])
    else: print(__doc__)
