#!/usr/bin/env python3
"""Score the clock against makedip.py's exact (unjittered) beat grid.

Usage: score_dip.py dip.pulses
"""
import sys
import numpy as np


def bpm_at(t):
    if t < 60.0:
        return 90.0
    if t < 61.0:
        return 90.0 - 4.0 * (t - 60.0)
    if t < 62.0:
        return 86.0
    if t < 63.0:
        return 86.0 + 4.0 * (t - 62.0)
    return 90.0


def centered(x):
    return (x + 0.5) % 1.0 - 0.5


def main(path):
    p = np.loadtxt(path, comments="#")
    t, clock_phase, audible = p[:, 0], p[:, 1], p[:, 5] > 0.5

    beats = [3.0]
    while beats[-1] < t[-1] + 1.0:
        logical = beats[-1] - 3.0
        beats.append(beats[-1] + 60.0 / bpm_at(logical))
    beats = np.asarray(beats)
    at = np.searchsorted(beats, t, side="right") - 1
    at = np.clip(at, 0, len(beats) - 2)
    true_phase = (t - beats[at]) / (beats[at + 1] - beats[at])
    err_beats = centered(clock_phase - true_phase)
    period = 60.0 / np.asarray([bpm_at(x - 3.0) for x in t])
    err_ms = err_beats * period * 1000.0

    changed = 63.0
    evaluate = (t >= changed) & audible
    e = err_ms[evaluate]
    te = t[evaluate]
    peak_i = np.argmax(np.abs(e))

    settled = -1.0
    for now in te:
        hold = (te >= now) & (te < now + 4.0)
        if hold.any() and te[hold][-1] >= now + 3.95 and np.all(np.abs(e[hold]) <= 15.0):
            settled = now - changed
            break

    print(f"picco {e[peak_i]:+.1f} ms a {te[peak_i] - changed:.1f} s")
    print(f"rientro <=15 ms tenuto 4 s: {settled:.1f} s")
    print(f"min/max dopo il cambio: {e.min():+.1f}/{e.max():+.1f} ms")


if __name__ == "__main__":
    main(sys.argv[1])
