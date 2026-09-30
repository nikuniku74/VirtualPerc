#!/usr/bin/env python3
"""Scatti di velocita' del clock sentito: quello che si sente come "accelera o
frena troppo rispetto al brano e poi fatica a rientrare".

    SURGE=--top surge_scan.py TAG [TAG2]     (pulses in /tmp/vp-bench/TAG)

Uno scatto e' un tratto in cui clockBpm sta oltre THR% dalla sua mediana su 8 s
(mentre la parte suona, dopo 20 s). Per ognuno: picco, quanto si e' mosso il BPM
pubblicato, l'errore di fase clock-decoder, regime e fiducia. docs/TODO.md item 66.
"""
import sys, glob, os, collections
import numpy as np
THR = 3.0
def surges(pul):
    X = np.loadtxt(pul, comments='#')
    t, bpm, clk, snd = X[:, 0], X[:, 3], X[:, 4], X[:, 5]
    m = (snd > 0.5) & (t > 20) & (clk > 30)
    t, bpm, clk = t[m], bpm[m], clk[m]
    reg = X[m, 7] if X.shape[1] > 7 else np.zeros(len(t))
    tr = X[m, 8] if X.shape[1] > 8 else np.ones(len(t))
    pe = X[m, 6] if X.shape[1] > 6 else np.zeros(len(t))
    dt = np.median(np.diff(t)); step = max(1, int(0.1 / dt))
    td, cd, bd, rd, trd, ped = t[::step], clk[::step], bpm[::step], reg[::step], tr[::step], pe[::step]
    half = int(4.0 / (dt * step))
    med = np.array([np.median(cd[max(0, i - half):i + half + 1]) for i in range(len(cd))])
    dev = (cd / med - 1) * 100
    out, i = [], 0
    while i < len(td):
        if abs(dev[i]) > THR:
            j = i
            while j + 1 < len(td) and abs(dev[j + 1]) > THR * 0.5:
                j += 1
            k = i + int(np.argmax(np.abs(dev[i:j + 1])))
            out.append(dict(t=td[i], dur=td[j] - td[i] + 0.1, peak=dev[k], reg=int(rd[k]), trust=trd[k],
                            perr=ped[k] * 60000 / max(40, bd[k]), pub=(bd[k] / med[k] - 1) * 100))
            i = j + 1
        else:
            i += 1
    return out, td[-1] - td[0]
if __name__ == '__main__':
    for tag in sys.argv[1:]:
        n = 0; secs = 0; span = 0; byreg = collections.Counter(); rows = []
        for p in sorted(glob.glob(f'/tmp/vp-bench/{tag}/*.pul')):
            s, sp = surges(p); span += sp
            for e in s:
                n += 1; secs += e['dur']; byreg[('CERCO', 'FISSO', 'VIVO')[e['reg']]] += 1
                rows.append((abs(e['peak']), os.path.basename(p)[:-4], e))
        print(f"== {tag}: {n} scatti oltre {THR:.0f}% ({n / span * 60:.2f}/min), {secs:.0f} s; per regime {dict(byreg)}")
        if '--top' in os.environ.get('SURGE', ''):
            for _, name, e in sorted(rows, key=lambda r: -r[0])[:25]:
                print(f"   {name:26s} {int(e['t'] // 60)}:{e['t'] % 60:04.1f} per {e['dur']:3.1f} s  clock {e['peak']:+5.1f}%  bpm pubblicato {e['pub']:+5.1f}%  errore fase {e['perr']:+4.0f} ms  {('CERCO', 'FISSO', 'VIVO')[e['reg']]} fid {e['trust']:.2f}")
