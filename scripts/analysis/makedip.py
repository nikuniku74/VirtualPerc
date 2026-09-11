#!/usr/bin/env python3
"""La fixture dell'item 40: un calo di tempo di due secondi, poi la ripresa.

Forma diversa dal rallentando di `rall4.wav`, e costa piu' del doppio: 88 ms di
picco e 18.1 s per rientrare, contro i 7.0 s del rallentando. Cassa e rullo sui
quarti alternati, charleston sugli ottavi, 120 s con 3 s di silenzio davanti.

  python3 scripts/analysis/makedip.py dip.wav
  ./build-host/VPTrack_artefacts/Release/VPTrack --wav dip.wav --pulses dip.pulses
"""
import numpy as np, wave, sys

SR = 44100
rng = np.random.RandomState(11)

def hat(d=0.05):
    L = int(SR * d); t = np.arange(L) / SR
    x = rng.uniform(-1, 1, L)
    a = 1 - np.exp(-2 * np.pi * 6000 / SR); lp = 0.0; o = np.empty(L)
    for i, v in enumerate(x):
        lp += a * (v - lp); o[i] = v - lp
    return o * np.exp(-t * 90)

def kick(d=0.18):
    t = np.arange(int(SR * d)) / SR
    return np.sin(2 * np.pi * (55 * np.exp(-t * 12) + 40) * t) * np.exp(-t * 14)

def snare(d=0.14):
    L = int(SR * d); t = np.arange(L) / SR
    return (0.7 * rng.uniform(-1, 1, L) + 0.3 * np.sin(2 * np.pi * 190 * t)) * np.exp(-t * 24)

def bpm_at(t):
    """90, giu' a 86 in un secondo, tenuto un secondo, su a 90 in un secondo."""
    if t < 60: return 90.0
    if t < 61: return 90.0 - 4.0 * (t - 60)
    if t < 62: return 86.0
    if t < 63: return 86.0 + 4.0 * (t - 62)
    return 90.0

def main(out, secs=120.0):
    x = np.zeros(int(SR * (secs + 3)), np.float32)
    t, b = 3.0, 0
    while t < secs:
        beat = 60.0 / bpm_at(t - 3.0); at = int(t * SR)
        h = hat() * 0.30; x[at:at + len(h)] += h
        e = int(SR * (t + beat / 2)); h2 = hat() * 0.22; x[e:e + len(h2)] += h2
        if b % 4 in (0, 2): k = kick() * 0.85;  x[at:at + len(k)] += k
        if b % 4 in (1, 3): s = snare() * 0.55; x[at:at + len(s)] += s
        t += beat; b += 1
    x = np.clip(x * 0.8, -1, 1)
    w = wave.open(out, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((x * 32767).astype('<i2').tobytes()); w.close()
    print(out)

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "dip.wav")
