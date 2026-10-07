#!/usr/bin/env python3
"""Banco veloce e ripetibile: tutti i brani e i tratti live in ~/vp-bench/wav.

    bench_fast.py run TAG [VAR=VALORE ...]   esegue (VPTrack --player) con le variabili date
    bench_fast.py cmp BASE TAG [TAG2 ...]     confronta, brano per brano

`VP_OFFLINE_PACING=1` fa lavorare il thread di analisi senza le attese da
dispositivo e fa aspettare il probe a ogni blocco: un brano di 4 minuti in ~2 s
invece di ~70, e due esecuzioni identiche bit per bit (docs/TODO.md item 84).
Le WAV si preparano come in bench_songs.py; i tratti live con extract_live.swift.
"""
import os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bench_songs  # noqa: E402
import onset_fit  # noqa: E402
import surge_scan  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
VPTRACK = os.environ.get('VPTRACK', os.path.join(ROOT, 'build-host/VPTrack_artefacts/Release/VPTrack'))
CACHE = os.path.expanduser('~/vp-bench')


def run(tag, env_pairs):
    env = dict(os.environ, VP_OFFLINE_PACING='1')
    # ARGS="..." replaces the default `--player`: e.g. ARGS="--gain -12" is the
    # MIXER path (kitMic, the live route) with the send 12 dB down.
    args = ['--player']
    for kv in env_pairs:
        k, v = kv.split('=', 1)
        if k == 'ARGS':
            args = v.split()
        else:
            env[k] = v
    os.makedirs(f'{CACHE}/{tag}', exist_ok=True)
    wavs = sorted(f for f in os.listdir(f'{CACHE}/wav') if f.endswith('.wav'))

    def one(w):
        subprocess.run([VPTRACK, '--wav', f'{CACHE}/wav/{w}', *args, '--pulses',
                        f'{CACHE}/{tag}/{w[:-4]}.pul'], env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    # JOBS=N: fewer parallel runs (VPTrack is deterministic at any N since item 96).
    with ThreadPoolExecutor(int(os.environ.get('JOBS', 6))) as ex:
        list(ex.map(one, wavs))


def metrics(pul, wav):
    P = np.loadtxt(pul, comments='#', usecols=(0, 1, 3, 4, 5, 6))
    t, ph, bpm, clk, snd, perr = P.T
    m = (snd > 0.5) & (t > 20) & (bpm > 40)
    dt = np.median(np.diff(t))
    off30 = float(np.sum(np.abs(perr[m]) * 60000 / bpm[m] > 30) * dt)
    d = np.diff(ph); d = (d + 0.5) % 1 - 0.5
    nat = clk[1:] / 60 * np.diff(t)
    extra = d - nat
    jumps = int(np.sum((np.abs(extra) > 0.03) & (snd[1:] > 0.5)))
    u = np.cumsum(np.concatenate([[ph[0]], d]))
    skipped = 0
    for i in np.nonzero((extra > 0.03) & (snd[1:] > 0.5))[0]:
        skipped += int(np.floor(u[i + 1] * 4) - np.floor((u[i] + nat[i]) * 4))
    r = onset_fit.fit(wav, pul)
    sg, span = surge_scan.surges(pul)
    ref = bench_songs.REF.get(os.path.basename(pul)[:-4].rsplit('_', 1)[0], 0.0)
    far = float('nan')
    if ref > 0:
        far = float(np.sum((snd > 0.5) & (np.abs(clk / ref - 1) > 0.04)) * dt)
    return dict(p25=r['p25'] if r else np.nan, exits=r['per_min'] if r else np.nan,
                surges=len(sg) / max(1.0, span) * 60, off30=off30, jumps=jumps, skipped=skipped, far4=far)


def cmp(tags):
    base = tags[0]
    names = sorted(f[:-4] for f in os.listdir(f'{CACHE}/{base}') if f.endswith('.pul'))
    rows = {n: {t: metrics(f'{CACHE}/{t}/{n}.pul', f'{CACHE}/wav/{n}.wav') for t in tags} for n in names}
    keys = ('p25', 'exits', 'surges', 'off30', 'jumps', 'skipped', 'far4')
    print(f"{'':10s} {'>25ms%':>7s} {'usc/min':>8s} {'sc/min':>7s} {'>30ms s':>8s} {'ricentri':>9s} {'saltati':>8s} {'>4% s':>6s}  peggiorano")
    for t in tags:
        v = {k: [r[t][k] for r in rows.values()] for k in keys}
        worse = [n for n, r in rows.items() if t != base and
                 (r[t]['p25'] - r[base]['p25'] > 1.5 or r[t]['exits'] - r[base]['exits'] > 1.0)]
        print(f"{t:10s} {np.nanmean(v['p25']):7.2f} {np.nanmean(v['exits']):8.2f} {np.nanmean(v['surges']):7.2f} "
              f"{np.nansum(v['off30']):8.0f} {int(np.sum(v['jumps'])):9d} {int(np.sum(v['skipped'])):8d} "
              f"{np.nansum(v['far4']):6.0f}  {worse}")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    if sys.argv[1] == 'run':
        run(sys.argv[2], sys.argv[3:])
    else:
        cmp(sys.argv[2:])
