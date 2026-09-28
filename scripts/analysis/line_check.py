#!/usr/bin/env python3
"""Known grid from the song itself: the sounding clock against the straight
line fitted to a steady stretch *before* an excursion.

    line_check.py pulses pre_lo pre_hi post_lo post_hi [step]

`pulses` is `VPTrack --pulses`. If the line fitted over [pre_lo, pre_hi),
extended, lands on the beats fitted over [post_lo, post_hi) (fraction near 0,
tempi equal), the song kept its tempo through whatever happened in between and
that line is the grid there too. Then the per-window error is the clock's real
phase error in ms (+ = late). A whole-beat slip shows as the error crossing
+-half a period. EVERYTIME 44.1 kHz, 110-136 / 166-186: 123.04 / 122.88 BPM,
115 beats, -23 ms. docs/TODO.md item 49.
"""
import sys, math

def beats(path):
    out, prev = [], None
    for l in open(path):
        if l.startswith('#'):
            continue
        t, bp, bar, bpm, clk, snd = l.split()
        t, bp = float(t), float(bp)
        if prev is not None and bp < prev[1] - 0.5:
            # interpolate the wrap: phase went prev -> bp+1
            p0, p1 = prev[1], bp + 1.0
            out.append(prev[0] + (1.0 - p0) / (p1 - p0) * (t - prev[0]))
        prev = (t, bp)
    return out

def fit(ts, lo, hi):
    pts = [t for t in ts if lo <= t < hi]
    n = len(pts)
    xs = list(range(n))
    mx, my = sum(xs) / n, sum(pts) / n
    b = sum((x - mx) * (y - my) for x, y in zip(xs, pts)) / sum((x - mx) ** 2 for x in xs)
    a = my - b * mx
    res = math.sqrt(sum((y - (a + b * x)) ** 2 for x, y in zip(xs, pts)) / n)
    return pts[0], b, res   # origin time, period, rms residual

if __name__ == '__main__':
    path = sys.argv[1]
    pre = (float(sys.argv[2]), float(sys.argv[3]))
    post = (float(sys.argv[4]), float(sys.argv[5]))
    ts = beats(path)
    o1, p1, r1 = fit(ts, *pre)
    o2, p2, r2 = fit(ts, *post)
    print(f"prima {pre}: {60/p1:.3f} BPM  rms {r1*1000:.1f} ms")
    print(f"dopo  {post}: {60/p2:.3f} BPM  rms {r2*1000:.1f} ms")
    # does the pre line, extended, land on the post beats?
    k = (o2 - o1) / p1
    print(f"linea prima estesa fino a dopo: {k:.3f} battiti (frazione {k - round(k):+.3f} = {(k-round(k))*p1*1000:+.1f} ms)")
    per = p1
    print("errore del clock contro la linea 'prima' (ms, + = in ritardo):")
    for lo in range(int(pre[0]), int(post[1]), int(sys.argv[6]) if len(sys.argv) > 6 else 2):
        es = []
        for t in ts:
            if lo <= t < lo + 2:
                k = (t - o1) / per
                es.append((k - round(k)) * per * 1000)
        if es:
            print(f"  {lo:4d}-{lo+2:<4d} media {sum(es)/len(es):+7.1f}  max {max(es, key=abs):+7.1f}")
