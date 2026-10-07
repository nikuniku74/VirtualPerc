#!/usr/bin/env python3
"""Live (MIXER) song changes and long STOPs, scored against the teacher's beats.

    stop_song.py change [VAR=VALUE ...]   song A, a pause, song B; STOP as A ends, START around B's attack
    stop_song.py inside [VAR=VALUE ...]   STOP held 4 / 8 s inside a song while the band keeps playing

Prints, per case, when the part first sounds at the right tempo after START and how
many seconds it sounds at a wrong one (outside 4% of the local true tempo or its
octaves) in the next 40 s. Uses the live sends and truths in ~/vp-bench/wav
(docs/TODO.md items 91-92). VAR=VALUE pairs go to VPTrack's environment.
"""
import os, subprocess, sys, wave
from concurrent.futures import ThreadPoolExecutor
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
VP = os.environ.get('VPTRACK', os.path.join(HERE, '../../build-host/VPTrack_artefacts/Release/VPTrack'))
C = os.path.expanduser('~/vp-bench')
PAIRS = [('GARDEN_3300_48k', 'FLAMINGO_1200_48k'), ('FLAMINGO_4500_48k', 'GARDEN_600_48k'),
         ('FLAMINGO_1200_48k', 'GARDEN_5100_48k'), ('99_SALLY_LIVE_48k', '98_FLAMINGO_3750_48k'),
         ('GARDEN_5100_48k', 'FLAMINGO_2400_48k'), ('FLAMINGO_5400_48k', 'GARDEN_4200_48k')]


def local_tempo(name, shift=0.0):
    T = np.loadtxt(f'{C}/wav/{name}.wav.truth.txt', ndmin=2)[:, 0]
    bp = np.convolve(60 / np.diff(T), np.ones(4) / 4, 'same')
    tm = (T[1:] + T[:-1]) / 2
    return lambda t: np.interp(t - shift, tm, bp)


def score(pul, t0, tempo):
    t, clk, snd = np.loadtxt(pul, comments='#', usecols=(0, 4, 5)).T
    lt = tempo(t)
    ok = np.zeros(len(t), bool)
    for k in (0.5, 1, 2):
        ok |= np.abs(clk / (lt * k) - 1) < 0.04
    m = (t >= t0) & (t < t0 + 40) & (snd > 0.5)
    first = t[np.argmax(m & ok)] - t0 if (m & ok).any() else np.nan
    return first, float(np.sum(m & ~ok) * np.median(np.diff(t)))


def vptrack(env, wav, pul, *args):
    subprocess.run([VP, '--wav', wav, *args, '--pulses', pul], env=env, capture_output=True, check=True)


def change(env):
    rows = []
    for a, b in PAIRS:
        wa, wb = (wave.open(f'{C}/wav/{n}.wav') for n in (a, b))
        p = wa.getparams(); sr = p.framerate
        xa = np.frombuffer(wa.readframes(p.nframes), np.int16).reshape(-1, p.nchannels)
        xb = np.frombuffer(wb.readframes(wb.getnframes()), np.int16).reshape(-1, p.nchannels)
        for pause in (2, 4, 8, 12):
            x = np.concatenate([xa[30 * sr:90 * sr], np.zeros((pause * sr, p.nchannels), np.int16), xb[30 * sr:90 * sr]])
            f = f'{C}/stop_song.wav'
            with wave.open(f, 'wb') as o:
                o.setparams(p); o.writeframes(x.tobytes())
            attack, stop = 60 + pause, 60.3
            for d in (-1.0, 0.5, 2.0):
                start = attack + d
                if start - stop < 0.5:
                    continue
                vptrack(env, f, f'{C}/stop_song.pul', '--stop-at', str(stop), '--stop-gap', str(start - stop))
                first, wrong = score(f'{C}/stop_song.pul', start, local_tempo(b, attack - 30))
                rows.append((d, first, wrong))
                print(f'{a[:12]:12s}->{b[:12]:12s} pausa {pause:2d}s START {d:+.1f}s: giusta da {first:5.1f}s, sbagliata {wrong:4.1f}s')
    for d in (-1.0, 0.5, 2.0):
        r = [x for x in rows if x[0] == d]
        print(f'START {d:+.1f}s: ingresso giusto mediano {np.nanmedian([x[1] for x in r]):5.1f}s, '
              f'sbagliata totale {sum(x[2] for x in r):5.1f}s')


def inside(env):
    files = [f[:-4] for f in sorted(os.listdir(f'{C}/wav'))
             if f.endswith('_48k.wav') and os.path.exists(f'{C}/wav/{f}.truth.txt')]

    def one(job):
        name, gap = job
        pul = f'{C}/stop_inside_{name}_{gap}.pul'
        vptrack(env, f'{C}/wav/{name}.wav', pul, '--stop-at', '60', '--stop-gap', str(gap))
        tempo = local_tempo(name)
        t, clk, snd = np.loadtxt(pul, comments='#', usecols=(0, 4, 5)).T
        pre = (t > 45) & (t < 60) & (snd > 0.5)
        following = np.mean(np.abs(clk[pre] / tempo(t[pre]) - 1) < 0.04) if pre.any() else 0.0
        first, wrong = score(pul, 60 + gap, tempo)
        os.remove(pul)
        return gap, following, first, wrong
    with ThreadPoolExecutor(6) as ex:
        res = list(ex.map(one, [(n, g) for n in files for g in (4, 8)]))
    for gap in (4, 8):
        r = [x for x in res if x[0] == gap and x[1] > 0.8]   # songs the part was following before STOP
        print(f'STOP {gap}s: {len(r)} brani, ingresso giusto dopo START mediana {np.nanmedian([x[2] for x in r]):4.1f}s '
              f'p90 {np.nanpercentile([x[2] for x in r], 90):4.1f}s, sbagliata totale {sum(x[3] for x in r):5.1f}s')


if __name__ == '__main__':
    if len(sys.argv) < 2 or sys.argv[1] not in ('change', 'inside'):
        print(__doc__); sys.exit(1)
    env = dict(os.environ, VP_OFFLINE_PACING='1')
    env.update(kv.split('=', 1) for kv in sys.argv[2:] if '=' in kv)
    (change if sys.argv[1] == 'change' else inside)(env)
