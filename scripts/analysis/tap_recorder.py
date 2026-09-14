#!/usr/bin/env python3
"""Tap the beat while the song plays, and write a tap.txt the refiner reads.

No DAW, no stopwatch. It plays the WAV with macOS `afplay` and you tap:

    INVIO          un quarto qualsiasi
    1 + INVIO      l'UNO (il downbeat della battuta)
    q + INVIO      fine (salva e chiude)

The times are the wall-clock instants you pressed Enter, relative to the moment
the song started - which is exactly what `refine_taps.py` expects, and why a
20-40 ms hand delay is fine: the refiner snaps each tap to the real transient.

Usage:
    python3 scripts/analysis/tap_recorder.py song.wav [tap.txt]

Play a short piece you know, tap every quarter, mark the one. 20-30 bars of one
song is enough for a phase truth.
"""
import subprocess
import sys
import time


def main():
    if len(sys.argv) < 2:
        print("usage: tap_recorder.py song.wav [tap.txt]", file=sys.stderr)
        return 2
    wav = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "tap.txt"

    print("Premi INVIO per far partire il brano.", flush=True)
    input()
    proc = subprocess.Popen(["afplay", wav])
    t0 = time.time()
    print("Batti INVIO su ogni quarto, '1'+INVIO sull'uno, 'q'+INVIO per finire.",
          flush=True)

    taps = []
    while True:
        try:
            key = input()
        except EOFError:
            break
        t = time.time() - t0
        k = key.strip().lower()
        if k == "q":
            break
        marker = "1" if k == "1" else ""
        taps.append((t, marker))
        n = sum(1 for _, m in taps if m)
        print(f"{t:7.2f}s   (uno n.{n})", flush=True)

    proc.terminate()
    with open(out, "w") as f:
        for t, marker in taps:
            f.write(f"{t:.4f} {marker}\n".rstrip() + "\n")
    print(f"\nscritto {out}: {len(taps)} tap, "
          f"{sum(1 for _, m in taps if m)} uno.", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
