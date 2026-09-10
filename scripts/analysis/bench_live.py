#!/usr/bin/env python3
"""Il banco su materiale reale: cinque brani di una band dal vivo.

Per ogni brano confronta il tempo che l'app pubblica con una stima indipendente
del tempo vero (`tempocurve.py`, tempogramma a finestra di 12 s) e riporta tre
cose, che sono tre difetti diversi:

  ritardo   di quanti secondi l'app e' indietro rispetto al batterista, dal
            massimo della correlazione incrociata. E' il "punto 1": accorgersi.
  errore    scarto medio in percentuale fra il tempo pubblicato e quello vero.
  strattoni deviazione standard della velocita' ISTANTANEA della griglia
            rispetto al tempo dichiarato: quanto l'orologio strattona.

La stima indipendente NON e' verita' di fase: e' un secondo parere, e serve solo
per il tempo. Vedi scripts/analysis/README.md.

  python3 scripts/analysis/bench_live.py <dir> [--follow low|medium|high]
"""
import numpy as np, subprocess, sys, os

VPTRACK = "./build-host/VPTrack_artefacts/Release/VPTrack"
kMinR = 0.50   # sotto questa correlazione il ritardo non e' misurabile
SONGS = [1, 2, 3, 4, 5]

def score(d, k, follow):
    wav, pul = f"{d}/s{k}.wav", f"{d}/s{k}.bench.pulses"
    cmd = [VPTRACK, "--wav", wav, "--pulses", pul]
    if follow: cmd += ["--follow", follow]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    P = np.loadtxt(pul, comments='#')
    t, bph, bpm, sn = P[:, 0], P[:, 1], P[:, 3], P[:, 5]
    m = (bpm > 50) & (sn > 0.5) & (t > 20)
    t, bph, bpm = t[m], bph[m], bpm[m]
    tr = []
    for line in open(f"{d}/s{k}-truth.txt"):
        f = line.split()
        if len(f) == 3:
            try: a, b, c = float(f[0]), float(f[1]), float(f[2])
            except ValueError: continue
            if c > 0.15: tr.append((a + 6.0, b))       # centro della finestra
    tr = np.array(tr)
    if len(tr) < 5 or len(t) < 100: return None

    g = np.arange(max(t[0], tr[0, 0]) + 2, min(t[-1], tr[-1, 0]) - 2, 1.0)
    if len(g) < 20: return None
    A = np.interp(g, t, bpm); A = A - A.mean()
    best = (0.0, -2.0)
    for lag in np.arange(-4.0, 12.01, 0.25):
        T = np.interp(g - lag, tr[:, 0], tr[:, 1]); T = T - T.mean()
        if T.std() < 1e-9: continue
        r = float(np.corrcoef(A, T)[0, 1])
        if r > best[1]: best = (lag, r)
    ref = np.interp(t, tr[:, 0], tr[:, 1])
    # Ripiegato sull'ottava: quattro dei brani veri misurati finora sono suonati
    # sul livello metrico sbagliato, e un errore del 100% seppellirebbe quello
    # che qui interessa, che e' la *precisione del passo* - se l'app segue la
    # deriva del batterista - non quale ottava ha scelto. L'ottava e' un difetto
    # separato: vedi docs/TODO.md item 36.
    oct_ = np.round(np.log2(bpm / ref))
    err = np.abs((bpm / np.power(2.0, oct_) - ref) / ref * 100).mean()
    wrongoct = (oct_ != 0).mean() * 100
    un = np.unwrap(bph * 2 * np.pi) / (2 * np.pi)
    k2 = max(1, int(0.5 / np.median(np.diff(t))))
    inst = (un[k2:] - un[:-k2]) / (t[k2:] - t[:-k2]) * 60.0
    rel = (inst - bpm[k2:]) / bpm[k2:] * 100
    return best[0], best[1], err, rel.std(), wrongoct

def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "."
    follow = None
    if "--follow" in sys.argv: follow = sys.argv[sys.argv.index("--follow") + 1]
    print(f"{'brano':>6} {'ritardo':>9} {'(r)':>7} {'errore':>8} {'strattoni':>10} {'ottava':>8}")
    lags, errs, jit, rs, oc = [], [], [], [], []
    for k in SONGS:
        r = score(d, k, follow)
        if r is None: print(f"{k:6d}   (non misurabile)"); continue
        lag, corr, err, sd, wo = r
        # Una stima di ritardo con correlazione bassa non e' una stima: il
        # massimo vaga fino al bordo della finestra di ricerca. Misurato: con
        # r=0.17 un brano ha riportato -4.00 s, che e' l'app *in anticipo* sul
        # batterista, e trascinava la media. Sotto kMinR il ritardo si stampa
        # ma non entra nella media.
        errs.append(err); jit.append(sd); rs.append(corr); oc.append(wo)
        flag = "" if corr >= kMinR else "   (r basso: ritardo non attendibile)"
        if corr >= kMinR: lags.append(lag)
        print(f"{k:6d} {lag:+8.2f}s {corr:7.2f} {err:7.2f}% {sd:9.2f}% {wo:7.0f}%{flag}")
    if lags:
        print(f"{'MEDIA':>6} {np.mean(lags):+8.2f}s {np.mean(rs):7.2f} "
              f"{np.mean(errs):7.2f}% {np.mean(jit):9.2f}% {np.mean(oc):7.0f}%"
              f"   (ritardo su {len(lags)}/{len(rs)} brani)")

main()
