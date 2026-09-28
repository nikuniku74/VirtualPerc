#!/usr/bin/env python3
"""Automatic version of line_check.py over a whole song.

    line_scan.py pulses [pulses...]

For every pair of steady 16 s stretches A and B (B starting 8-40 s after A
ends) whose straight lines agree - both fit within 12 ms rms, same tempo within
0.3%, and A extended lands on B within 0.06 beat - the song kept one tempo from
A to B, so A's line is the grid in between. The clock is scored against it
there. Prints, per file: seconds verified this way, mean/worst phase error in
them, and whole-beat slips (error crossing +-0.4 beat inside a verified span).
A click-track record verifies most of its length; a live band little of it,
which is the honest answer, not a failure. docs/TODO.md item 51.
"""
import math
import sys

WIN, STEP, RMS_MAX, TEMPO_TOL, LAND_TOL = 16.0, 4.0, 0.012, 0.003, 0.06
GAPS = (8.0, 16.0, 24.0, 40.0)


def beats(path):
    out, prev = [], None
    for line in open(path):
        if line.startswith('#'):
            continue
        t, bp = line.split()[:2]
        t, bp = float(t), float(bp)
        if prev is not None and bp < prev[1] - 0.5:
            p0, p1 = prev[1], bp + 1.0
            out.append(prev[0] + (1.0 - p0) / (p1 - p0) * (t - prev[0]))
        prev = (t, bp)
    return out


def fit(ts):
    n = len(ts)
    if n < 8:
        return None
    mx, my = (n - 1) / 2.0, sum(ts) / n
    sxx = sum((i - mx) ** 2 for i in range(n))
    b = sum((i - mx) * (y - my) for i, y in enumerate(ts)) / sxx
    a = my - b * mx
    rms = math.sqrt(sum((y - (a + b * i)) ** 2 for i, y in enumerate(ts)) / n)
    return a, b, rms


def scan(path):
    ts = beats(path)
    if len(ts) < 20:
        return None
    end = ts[-1]
    covered = {}          # whole second -> worst |error| ms seen there
    slips = set()
    t0 = ts[0]
    while t0 + WIN < end:
        A = [t for t in ts if t0 <= t < t0 + WIN]
        fa = fit(A)
        if fa and fa[2] < RMS_MAX:
            a, per, _ = fa
            for g in GAPS:
                b0 = t0 + WIN + g
                B = [t for t in ts if b0 <= t < b0 + WIN]
                fb = fit(B)
                if not fb or fb[2] >= RMS_MAX or abs(fb[1] - per) / per > TEMPO_TOL:
                    continue
                k = (fb[0] - a) / per
                if abs(k - round(k)) > LAND_TOL:
                    continue
                prev_err = None
                for t in ts:
                    if t0 + WIN <= t < b0 + WIN:
                        k = (t - a) / per
                        e = (k - round(k)) * per
                        s = int(t)
                        covered[s] = max(covered.get(s, 0.0), abs(e) * 1000.0)
                        if prev_err is not None and abs(e - prev_err) > 0.8 * per:
                            slips.add(s)
                        prev_err = e
        t0 += STEP
    if not covered:
        return 0, 0.0, 0.0, 0, 0.0
    errs = list(covered.values())
    worst_t = max(covered, key=covered.get)
    return len(covered), sum(errs) / len(errs), max(errs), len(slips), worst_t


if __name__ == '__main__':
    print(f"{'file':44s} {'verificati':>10s} {'media ms':>9s} {'peggio ms':>10s} {'a t=':>6s} {'slitt.':>6s}")
    for p in sys.argv[1:]:
        r = scan(p)
        name = p.rsplit('/', 1)[-1].replace('.pul', '')
        if r is None:
            print(f"{name:44s} {'-':>10s}")
            continue
        n, mean, worst, nslip, wt = r
        print(f"{name:44s} {n:9d}s {mean:9.1f} {worst:10.1f} {wt:6.0f} {nslip:6d}")
