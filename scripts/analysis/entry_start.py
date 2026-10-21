"""entry_start.py BASE TAG: how the part comes in (docs/TODO.md item 129). Entries of TAG against BASE on the same fixed window
(25 s from BASE's entry); a beat counts as bad only while TAG's part sounds."""
import sys, os, glob
import numpy as np
B = os.path.expanduser('~/vp-bench')
wrap = lambda p: (p + 0.5) % 1 - 0.5
unwrap = lambda p: np.unwrap(p * 2 * np.pi) / (2 * np.pi)
def entry(P):
    on = np.nonzero(P[:, 5] > 0.5)[0]
    return P[on[0], 0] if len(on) else None
def window(P, T, t0, te):
    t, ph, clk, snd = P[:, 0], P[:, 1], P[:, 4], P[:, 5]
    tb = T[:, 0]; tbpm = 60 / np.gradient(tb)
    c = np.interp(tb, t, clk); r = c / tbpm; ok = np.abs(r - 1) < 0.04
    s = np.interp(tb, t, snd) > 0.5
    err = wrap(np.interp(tb, t, unwrap(ph))) * 60000 / c
    steady = ok & (tb > t0 + 25)
    off = np.median(err[steady]) if steady.sum() > 8 else (np.median(err[ok]) if ok.sum() else 0.0)
    dev = np.abs(err - off)
    after = np.nonzero(tb >= te)[0]; f4 = after[:4]
    good4 = len(f4) == 4 and ok[f4].all() and (dev[f4] <= 80).all()
    w = (tb >= t0) & (tb < t0 + 25)
    beat = np.gradient(tb)
    bad = float(((~(ok & (dev <= 40))) & s & w) .astype(float) @ beat)
    played = float((s & w).astype(float) @ beat)
    return good4, bad, played
base, tag = sys.argv[1], sys.argv[2]
G = {}; rows = []
half = os.environ.get('HALF')
allnames = sorted(os.path.basename(x)[:-4] for x in glob.glob(f'{B}/{base}/*.pul'))
titles = sorted(set(n.rsplit('_', 1)[0] for n in allnames))
keepA = set(titles[::2])
for pul in sorted(glob.glob(f'{B}/{base}/*.pul')):
    name = os.path.basename(pul)[:-4]; tr = f'{B}/wav/{name}.wav.truth.txt'
    if not os.path.exists(tr) or not os.path.exists(f'{B}/{tag}/{name}.pul'): continue
    if half and ((name.rsplit('_', 1)[0] in keepA) != (half == 'A')): continue
    T = np.loadtxt(tr, ndmin=2); PA = np.loadtxt(pul, comments='#'); PB = np.loadtxt(f'{B}/{tag}/{name}.pul', comments='#')
    ta, tb_ = entry(PA), entry(PB)
    if ta is None or tb_ is None: continue
    a = window(PA, T, ta, ta); b = window(PB, T, ta, tb_)
    rows.append((name, ta, tb_, a, b))
g4a = sum(r[3][0] for r in rows); g4b = sum(r[4][0] for r in rows)
bada = sum(r[3][1] for r in rows); badb = sum(r[4][1] for r in rows)
pla = sum(r[3][2] for r in rows); plb = sum(r[4][2] for r in rows)
d = np.array([r[2] - r[1] for r in rows])
print(f"{base} -> {tag}: primi 4 buoni {g4a} -> {g4b} di {len(rows)} | sbagliati suonando (25 s) {bada:.0f} -> {badb:.0f} s"
      f" | suonati {pla:.0f} -> {plb:.0f} s | attesa in più: mediana {np.median(d):.1f} media {d.mean():.1f} max {d.max():.1f} s")
tolto = pla - plb; meno = bada - badb
if tolto > 0: print(f"   dei {tolto:.0f} s non più suonati, sbagliati {meno:.0f} ({meno/tolto*100:.0f}%); media sbagliati della base {bada/pla*100:.0f}%")
worse = [(r[0], r[3][1], r[4][1]) for r in rows if r[4][1] > r[3][1] + 2]
print("   peggiorano (>2 s): " + (', '.join(f'{n} {a:.0f}->{b:.0f}' for n, a, b in worse) or 'nessuno'))
if os.environ.get('V'):
    for n, ta, tb_, a, b in rows:
        if tb_ - ta > 6.0:
            print(f"   {n:26s} entra {ta:5.1f} -> {tb_:5.1f}s | base: suonati {a[2]:4.1f}s di cui sbagliati {a[1]:4.1f} | ora: suonati {b[2]:4.1f} sbagliati {b[1]:4.1f} | primi 4 {'ok' if a[0] else 'no'} -> {'ok' if b[0] else 'no'}")
