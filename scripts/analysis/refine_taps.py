#!/usr/bin/env python3
"""Refine hand-tapped beats against the mix's low-band transients.

This is the "Livello 1" of docs/HANDOFF_LIVE_TRACKING.md: the listener decides
*which* transient is the beat (the tap), the machine finds *when exactly* it
falls (the attack), because a hand tap is 20-40 ms late but systematically so,
and a kick's peak is 20-30 ms after its attack. The decision is yours; only the
instant is the machine's.

For each tap it:

  1. takes a -120 ms .. +60 ms window around it;
  2. band-passes 30..180 Hz (two one-pole low-pass at 180 Hz, one high-pass at
     30 Hz) and takes a 3 ms envelope;
  3. finds the steepest rise in that envelope, then walks back to where the
     energy crossed 20% of its local peak - that is the attack;
  4. writes the attack time in place of the tap.

Usage:
    python3 scripts/analysis/refine_taps.py song.wav taps.txt [refined.txt]

taps.txt: one line per tap, seconds from the start of the wav, an optional
`1` (or anything) in a second column marking the downbeat. Blank lines and
lines starting with `#` are ignored. Output is the same shape, four decimals,
with the `1` marker preserved.

Uses only `wave` and `numpy` - no beat tracker. A tap that finds no transient
in its window is left where it is and reported on stderr.
"""
import sys
import wave
import numpy as np


def bandpass_envelope(x, sr, lo=30.0, hi=180.0, smooth_ms=3.0):
    # Low-pass at `hi`: two one-poles in cascade (a 2nd-order roll-off is enough
    # to separate the kick/conga body from the hats). High-pass at `lo`: one
    # one-pole subtracted, to drop the DC/rumble that would otherwise look like
    # a rise everywhere.
    a_hi = 1.0 - np.exp(-2.0 * np.pi * hi / sr)
    lp1 = lp2 = 0.0
    n = len(x)
    y = np.empty(n, dtype=np.float32)
    for i in range(n):
        lp1 += a_hi * (x[i] - lp1)
        lp2 += a_hi * (lp1 - lp2)
        y[i] = lp2
    a_lo = 1.0 - np.exp(-2.0 * np.pi * lo / sr)
    hp = 0.0
    for i in range(n):
        hp += a_lo * (y[i] - hp)
        y[i] -= hp
    # Envelope: rectified, then a 3 ms moving average.
    e = np.abs(y)
    w = max(1, int(sr * smooth_ms / 1000.0))
    k = np.ones(w, dtype=np.float32) / w
    return np.convolve(e, k, mode="same")


def find_attack(env, sr, lo_i, hi_i):
    seg = env[lo_i:hi_i]
    if seg.size < 4:
        return None
    peak_i = int(np.argmax(seg))
    peak = seg[peak_i]
    if peak < 1e-9:
        return None
    # Walk back from the peak to the 20% point of the *local* peak - the moment
    # the energy actually starts rising, not where it crests.
    threshold = 0.20 * peak
    at = peak_i
    while at > 0 and seg[at] > threshold:
        at -= 1
    # The first sample at or above the threshold is the attack.
    while at < peak_i and seg[at] < threshold:
        at += 1
    return lo_i + at


def main():
    if len(sys.argv) < 3:
        print("usage: refine_taps.py song.wav taps.txt [refined.txt]", file=sys.stderr)
        return 2
    wav, taps, out = sys.argv[1], sys.argv[2], (sys.argv[3] if len(sys.argv) > 3 else None)

    try:
        w = wave.open(wav, "rb")
    except (wave.Error, FileNotFoundError) as e:
        print(f"non leggo '{wav}': {e}", file=sys.stderr)
        print("  serve un WAV. Per un MP3/M4A converti prima, es.:", file=sys.stderr)
        print("  mpg123 -w out.wav in.mp3", file=sys.stderr)
        return 2
    sr = w.getframerate()
    nch = w.getnchannels()
    raw = w.readframes(w.getnframes())
    w.close()
    x = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    x = x.reshape(-1, nch).mean(1)

    env = bandpass_envelope(x, sr)

    try:
        f = open(taps, "r")
    except FileNotFoundError:
        print(f"non trovo '{taps}': serve il file coi tuoi tap.", file=sys.stderr)
        print("  un tap per riga, secondi dall'inizio del brano, '1' sull'uno:", file=sys.stderr)
        print("    15.5 1", file=sys.stderr)
        print("    18.4", file=sys.stderr)
        print("    21.3", file=sys.stderr)
        print("    24.1 1", file=sys.stderr)
        return 2

    entries = []
    with f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            try:
                t = float(parts[0])
            except ValueError:
                continue
            marker = parts[1] if len(parts) > 1 else ""
            entries.append((t, marker))

    total = len(entries)
    moved = 0
    miss = 0
    moved_by = []
    out_lines = []
    for t, marker in entries:
        lo = max(0, int((t - 0.120) * sr))
        hi = min(len(x) - 1, int((t + 0.060) * sr))
        at = find_attack(env, sr, lo, hi)
        if at is None:
            miss += 1
            out_lines.append(f"{t:.4f} {marker}".rstrip())
            continue
        refined = at / sr
        moved += 1
        moved_by.append((refined - t) * 1000.0)
        out_lines.append(f"{refined:.4f} {marker}".rstrip())

    if out is not None:
        with open(out, "w") as f:
            f.write("\n".join(out_lines) + "\n")
    else:
        print("\n".join(out_lines))

    mb = np.asarray(moved_by) if moved_by else np.asarray([0.0])
    print(f"tap: {total} totali, {moved} spostati, {miss} senza transiente", file=sys.stderr)
    print(f"spostamento: media {mb.mean():+.1f} ms  mediano {np.median(mb):+.1f} ms  "
          f"max {np.abs(mb).max():.1f} ms", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
