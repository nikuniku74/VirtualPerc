#!/usr/bin/env python3
"""Quanto i colpi della parte stanno sulla batteria del brano, battito per battito.

    onset_fit.py WAV PULSES [--from S] [--trace] [--target]

--target misura contro la griglia a cui il decoder porta il clock (colonna
phaseErr dei PULSES): se li' i colpi stanno e sul clock no, e' l'inseguimento;
se escono anche li', e' la fase pubblicata.

Senza click non c'e' una griglia vera; c'e' la batteria. Si trovano gli attacchi
di cassa (40-160 Hz) e rullante (160 Hz-4 kHz) nell'audio, ognuno si confronta
con il sedicesimo del clock piu' vicino (PULSES da `VPTrack --pulses`), e lo
scarto locale e' la mediana su 2 s. Lo scarto costante del brano (feel del
batterista, anticipo d'attacco) si toglie: resta quello che si sente come
"esce e poi rientra". Un'uscita e' un tratto oltre 25 ms dallo scarto tipico,
lungo almeno un battito. Misura offline (centrata): l'app non la vede.
"""
import sys, wave
import numpy as np

EXIT_MS, MIN_BEATS, WIN_S = 25.0, 1.0, 2.0


def onsets(x, sr):
    win, hop = 2048, 128
    frames = np.lib.stride_tricks.sliding_window_view(x, win)[::hop] * np.hanning(win)
    mag = np.abs(np.fft.rfft(frames, axis=1))
    f = np.fft.rfftfreq(win, 1 / sr)
    frate = sr / hop
    out = []
    for lo, hi in ((40, 160), (160, 4000)):
        b = np.log1p(mag[:, (f >= lo) & (f < hi)] * 40)
        fl = np.concatenate([[0.0], np.maximum(np.diff(b, axis=0), 0).sum(1)])
        k = int(frate * 0.5)
        thr = np.convolve(fl, np.ones(k) / k, 'same') * 1.6 + 1e-6
        # strong local maxima only, 60 ms apart: kick and snare hits, not hat bleed
        pk = np.nonzero((fl > thr) & (fl >= np.roll(fl, 1)) & (fl > np.roll(fl, -1)))[0]
        keep, last = [], -1e9
        for i in pk:
            if i - last >= 0.06 * frate:
                keep.append(i); last = i
            elif fl[i] > fl[keep[-1]]:
                keep[-1] = i; last = i
        for i in keep:
            if 0 < i < len(fl) - 1:
                a, c0, c = fl[i - 1], fl[i], fl[i + 1]
                d = 0.5 * (a - c) / (a - 2 * c0 + c) if (a - 2 * c0 + c) != 0 else 0.0
                # frame i covers the diff between windows i-1 and i: date at the window centre
                out.append(((i + d) * hop + win / 2) / sr)
    return np.sort(np.array(out))


def wav_onsets(wav):
    cache = wav + '.onsets.npy'   # the audio does not change between runs
    try:
        return np.load(cache)
    except OSError:
        pass
    w = wave.open(wav, 'rb')
    sr, nch = w.getframerate(), w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32) / 32768.0
    on = onsets(x.reshape(-1, nch).mean(1), sr)
    np.save(cache, on)
    return on


def context(P, t0, t1):
    """(regime, trust) over an exit, when the pulses carry them: 0 CERCO, 1 FISSO, 2 VIVO."""
    if P.shape[1] < 9:
        return (-1, float('nan'))
    m = (P[:, 0] >= t0) & (P[:, 0] <= t1)
    if not m.any():
        return (-1, float('nan'))
    return (int(np.bincount(P[m, 7].astype(int)).argmax()), float(P[m, 8].mean()))


def fit(wav, pul, t_from=20.0, target=False):
    P = np.loadtxt(pul, comments='#')
    pt, bph, clk, snd = P[:, 0], P[:, 1], P[:, 4], P[:, 5]
    beat = np.unwrap(bph * 2 * np.pi) / (2 * np.pi)          # continuous beat count
    if target:
        # the grid the decoder is steering the clock to: phaseErr > 0 is the clock ahead
        beat = beat - P[:, 6]
    on = wav_onsets(wav)
    on = on[(on > max(t_from, pt[0])) & (on < pt[-1])]
    on = on[np.interp(on, pt, snd) > 0.5]
    if len(on) < 50:
        return None
    b = np.interp(on, pt, beat) * 4.0                         # in sixteenths
    frac = b - np.round(b)                                    # onset vs nearest clock 16th
    per = 60.0 / np.interp(on, pt, clk)
    off = frac * per / 4.0 * 1000.0                           # ms, + = drum after the grid
    # local offset: median of the hits within +-1 s
    loc = np.array([np.median(off[np.abs(on - t) <= WIN_S / 2]) for t in on])
    typ = float(np.median(loc))
    dev = loc - typ
    ex, i = [], 0
    while i < len(on):
        if abs(dev[i]) > EXIT_MS:
            j = i
            while j + 1 < len(on) and abs(dev[j + 1]) > EXIT_MS * 0.6:
                j += 1
            dur = on[j] - on[i]
            if dur >= MIN_BEATS * np.median(per[i:j + 1]):
                ex.append((on[i], dur, float(dev[i:j + 1][np.argmax(np.abs(dev[i:j + 1]))]))
                          + context(P, on[i], on[j]))
            i = j + 1
        else:
            i += 1
    span = on[-1] - on[0]
    return dict(n=len(on), typ=typ, jit=float(np.median(np.abs(off - loc))),
                p15=float(np.mean(np.abs(dev) > 15) * 100), p25=float(np.mean(np.abs(dev) > 25) * 100),
                exits=ex, per_min=len(ex) / span * 60, span=span,
                exit_s=sum(e[1] for e in ex), t=on, dev=dev)


if __name__ == '__main__':
    a = sys.argv[1:]
    t_from = float(a[a.index('--from') + 1]) if '--from' in a else 20.0
    r = fit(a[0], a[1], t_from, '--target' in a)
    if r is None:
        sys.exit('troppo pochi attacchi')
    print(f"attacchi {r['n']}  scarto tipico {r['typ']:+.1f} ms  dispersione colpo {r['jit']:.1f} ms  "
          f">15ms {r['p15']:.1f}%  >25ms {r['p25']:.1f}%  uscite {len(r['exits'])} "
          f"({r['per_min']:.2f}/min, {r['exit_s']:.1f} s)")
    for t0, dur, peak, reg, trust in r['exits']:
        print(f"  {int(t0 // 60)}:{t0 % 60:05.2f}  per {dur:4.1f} s  picco {peak:+6.1f} ms"
              + ('' if reg < 0 else f"  {('CERCO', 'FISSO', 'VIVO')[reg]:5s} fiducia {trust:.2f}"))
    if '--trace' in a:
        for t, d in zip(r['t'], r['dev']):
            print(f"{t:8.3f} {d:+7.1f}")
