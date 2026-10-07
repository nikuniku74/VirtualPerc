#!/usr/bin/env python3
"""Da dove viene ogni scatto del clock (vedi surge_scan.py).

    surge_sources.py TAG [--top N]        (pulses in ~/vp-bench/TAG)

Per ogni scatto, al suo picco: quanta parte viene dal tempo del decoder (rete),
quanta dal trim, quanta dalla piega di fase (clock contro tempo del follower), e
se dentro c'e' una transizione o un recupero. Servono le colonne che
`VPTrack --pulses` scrive dal 2026-09-30 (rete target trim trans recover).
docs/TODO.md item 69.
"""
import sys, glob, os, collections
import numpy as np
THR = 3.0
tag = sys.argv[1]
cnt = collections.Counter(); sec = collections.Counter(); rows = []
for p in sorted(glob.glob(os.path.expanduser(f'~/vp-bench/{tag}/*.pul'))):
    X = np.loadtxt(p, comments='#')
    t, pub, clk, snd, pe, reg = X[:, 0], X[:, 3], X[:, 4], X[:, 5], X[:, 6], X[:, 7]
    rete, tgt, trim, trans, rec = X[:, 9], X[:, 10], X[:, 11], X[:, 12], X[:, 13]
    m = (snd > 0.5) & (t > 20) & (clk > 30)
    idx = np.nonzero(m)[0]
    dt = np.median(np.diff(t)); step = max(1, int(0.1 / dt))
    ii = idx[::step]
    cd = clk[ii]; half = int(4.0 / (dt * step))
    med = np.array([np.median(cd[max(0, i - half):i + half + 1]) for i in range(len(cd))])
    dev = (cd / med - 1) * 100
    i = 0
    while i < len(ii):
        if abs(dev[i]) > THR:
            j = i
            while j + 1 < len(ii) and abs(dev[j + 1]) > THR * 0.5:
                j += 1
            k = i + int(np.argmax(np.abs(dev[i:j + 1]))); g = ii[k]; s = np.sign(dev[k])
            dec = (rete[g] / med[k] - 1) * 100 * s          # decoder tempo, same sign as the surge
            tr = trim[g] / med[k] * 100 * s                  # trim
            st = (clk[g] / pub[g] - 1) * 100 * s             # phase steer on top of the follower tempo
            a, b = ii[i], ii[j]
            ev = []
            if trans[a:b + 1].max() > 0: ev.append('transizione')
            if rec[b] != rec[max(0, a - 50)]: ev.append('recupero')
            parts = {'decoder': dec, 'trim': tr, 'piega di fase': st}
            main = max(parts, key=lambda q: parts[q])
            if ev: main = ev[0]
            dur = t[ii[j]] - t[ii[i]] + 0.1
            cnt[main] += 1; sec[main] += dur
            rows.append((abs(dev[k]), os.path.basename(p)[:-4], t[ii[i]], dur, dev[k], dec * s, tr * s, st * s,
                         pe[g] * 60000 / max(40, pub[g]), int(reg[g]), main))
            i = j + 1
        else:
            i += 1
print(f"== {tag}: {sum(cnt.values())} scatti")
for k in sorted(cnt, key=lambda q: -sec[q]):
    print(f"   {k:14s} {cnt[k]:3d} scatti {sec[k]:5.1f} s")
if '--top' in sys.argv:
    for _, n, t0, dur, pkv, dec, tr, st, perr, rg, main in sorted(rows, reverse=True)[:int(sys.argv[sys.argv.index('--top') + 1])]:
        print(f"   {n:26s} {int(t0 // 60)}:{t0 % 60:04.1f} {dur:3.1f}s clock {pkv:+5.1f}% = decoder {dec:+5.1f} trim {tr:+4.1f} piega {st:+5.1f} | err fase {perr:+4.0f} ms {('CERCO', 'FISSO', 'VIVO')[rg]} -> {main}")
