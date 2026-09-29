#!/usr/bin/env python3
"""Banco globale sui brani veri: ogni modifica si giudica su tutti, mai su uno.

    bench_songs.py run  TAG mp3... [--jobs 8]   suona ogni brano a 44.1 e 48 kHz
    bench_songs.py show TAG [TAG2]              tabella, o confronto A/B

Ogni brano passa per l'app intera (`VPTrack --player --pulses`, il percorso
BRANO). Per ciascuno, solo mentre la parte suona e dopo i primi 20 s:

  scatti   rms % della velocita' SENTITA del clock (clockBpm, la velocita'
           effettiva della griglia) contro la sua mediana su 8 s. E' il
           "corre / trascina": un clock fermo su un brano fermo da' ~0.
  fuori    % del tempo in cui il clock e' oltre il 3% dal tempo del brano
           stimato da un tempogramma indipendente (finestra 12 s, solo punti
           nitidi, ottava allineata al clock). Secondo parere, non verita'.
  ottava   secondi passati a ~doppio o ~meta' della mediana del brano.
  fase     line_scan.py: secondi verificati, errore medio/peggiore del clock
           in ms contro la griglia ricavata dal brano stesso, slittamenti.

Una modifica passa se migliora l'insieme e non peggiora nessun brano.
docs/TODO.md item 58.
"""
import os, subprocess, sys, json
from concurrent.futures import ThreadPoolExecutor
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import line_scan  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
VPTRACK = os.environ.get('VPTRACK', os.path.join(ROOT, 'build-host/VPTrack_artefacts/Release/VPTrack'))
CACHE = '/tmp/vp-bench'
RATES = (44100, 48000)


def wav_for(mp3, rate):
    name = os.path.splitext(os.path.basename(mp3))[0].replace(' ', '_')
    out = f'{CACHE}/wav/{name}_{rate // 1000}k.wav'
    if not os.path.exists(out):
        os.makedirs(os.path.dirname(out), exist_ok=True)
        subprocess.run(['afconvert', '-f', 'WAVE', '-d', f'LEI16@{rate}', mp3, out], check=True)
    return out


def tempogram(wav):
    """(t_centre, bpm, clarity) every 2 s; cached next to the wav."""
    cache = wav + '.tempo.json'
    if os.path.exists(cache):
        return json.load(open(cache))
    import wave
    w = wave.open(wav, 'rb')
    sr, nch = w.getframerate(), w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32) / 32768.0
    x = x.reshape(-1, nch).mean(1)
    win, hop = 1024, 256
    frate = sr / hop
    nf = (len(x) - win) // hop
    frames = np.lib.stride_tricks.sliding_window_view(x, win)[::hop][:nf] * np.hanning(win)
    mag = np.abs(np.fft.rfft(frames, axis=1))
    flux = np.concatenate([[0.0], np.maximum(np.diff(np.log1p(mag * 40), axis=0), 0).sum(1)])
    k = int(frate * 0.5)
    flux = np.maximum(flux - np.convolve(flux, np.ones(k) / k, 'same'), 0)
    W = 12.0
    lags = np.arange(int(frate * 60 / 200), int(frate * 60 / 50))
    out = []
    for t in np.arange(0, len(x) / sr - W, 2.0):
        seg = flux[int(t * frate):int((t + W) * frate)]
        if len(seg) < lags.max() * 2:
            continue
        seg = seg - seg.mean()
        ac = np.correlate(seg, seg, 'full')[len(seg) - 1:]
        ac = ac / max(ac[0], 1e-9)
        v = ac[lags]
        i = int(np.argmax(v))
        lag = lags[i]
        a, b, c = ac[lag - 1], ac[lag], ac[lag + 1]
        d = (a - c) / (2 * (a - 2 * b + c)) if (a - 2 * b + c) != 0 else 0.0
        out.append((float(t + W / 2), float(60 * frate / (lag + d)), float(v[i] - np.median(v))))
    json.dump(out, open(cache, 'w'))
    return out


def run_one(tag, mp3, rate):
    wav = wav_for(mp3, rate)
    pul = f'{CACHE}/{tag}/{os.path.basename(wav)[:-4]}.pul'
    os.makedirs(os.path.dirname(pul), exist_ok=True)
    subprocess.run([VPTRACK, '--wav', wav, '--player', '--pulses', pul],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    tempogram(wav)
    return pul


def metrics(pul):
    wav = f'{CACHE}/wav/{os.path.basename(pul)[:-4]}.wav'
    P = np.loadtxt(pul, comments='#')
    t, clk, snd = P[:, 0], P[:, 4], P[:, 5]
    m = (snd > 0.5) & (t > 20) & (clk > 30)
    t, clk = t[m], clk[m]
    if len(t) < 100:
        return None
    dt = np.median(np.diff(t))
    # rolling median over 8 s (decimated for speed)
    step = max(1, int(0.1 / dt))
    td, cd = t[::step], clk[::step]
    half = int(4.0 / (dt * step))
    med = np.array([np.median(cd[max(0, i - half):i + half + 1]) for i in range(len(cd))])
    dev = cd / med - 1.0
    jerk = float(np.sqrt(np.mean(dev ** 2)) * 100)
    song = float(np.median(cd))
    octave = float(np.sum(np.abs(np.log2(cd / song)) > 0.75) * dt * step)
    tg = [(a, b) for a, b, c in tempogram(wav) if c > 0.15]
    out_frac = float('nan')
    if len(tg) > 5:
        ta, ba = np.array(tg).T
        ref = np.interp(td, ta, ba)
        ref = ref * 2.0 ** np.round(np.log2(cd / ref))
        near = np.abs(td[:, None] - ta[None, :]).min(1) < 6.0
        if near.sum() > 10:
            out_frac = float(np.mean(np.abs(cd[near] / ref[near] - 1) > 0.03) * 100)
    ls = line_scan.scan(pul) or (0, 0.0, 0.0, 0, 0.0)
    return dict(jerk=jerk, out=out_frac, octave=octave, bpm=song,
                ver=ls[0], mean=ls[1], worst=ls[2], slips=ls[3])


def show(tags):
    rows = {}
    for tag in tags:
        d = f'{CACHE}/{tag}'
        for f in sorted(os.listdir(d)):
            if f.endswith('.pul'):
                rows.setdefault(f[:-4], {})[tag] = metrics(f'{d}/{f}')
    keys = ('jerk', 'out', 'octave', 'mean', 'worst', 'slips')
    head = f"{'brano':34s} {'bpm':>6s} {'scatti%':>8s} {'fuori%':>7s} {'ottava s':>8s} {'verif s':>7s} {'fase ms':>8s} {'peggio':>7s} {'slitt':>5s}"
    print(head)
    tot = {tag: {k: [] for k in keys} for tag in tags}
    for name, per in rows.items():
        for tag in tags:
            r = per.get(tag)
            if r is None:
                continue
            for k in keys:
                if not np.isnan(r[k]):
                    tot[tag][k].append(r[k])
            lab = name[:34] if tag == tags[0] else f"  {tag}"[:34]
            print(f"{lab:34s} {r['bpm']:6.1f} {r['jerk']:8.2f} {r['out']:7.1f} {r['octave']:8.1f} "
                  f"{r['ver']:7d} {r['mean']:8.1f} {r['worst']:7.1f} {r['slips']:5d}")
    print()
    for tag in tags:
        s = tot[tag]
        print(f"{'MEDIA ' + tag:34s} {'':6s} {np.mean(s['jerk']):8.2f} {np.mean(s['out']):7.1f} "
              f"{np.sum(s['octave']):8.1f} {'':7s} {np.mean(s['mean']):8.1f} {np.mean(s['worst']):7.1f} "
              f"{int(np.sum(s['slips'])):5d}")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    cmd, tag = sys.argv[1], sys.argv[2]
    if cmd == 'run':
        args = sys.argv[3:]
        jobs = 8
        if '--jobs' in args:
            i = args.index('--jobs')
            jobs = int(args[i + 1])
            del args[i:i + 2]
        work = [(m, r) for m in args for r in RATES]
        with ThreadPoolExecutor(jobs) as ex:
            list(ex.map(lambda a: run_one(tag, *a), work))
        show([tag])
    else:
        show(sys.argv[2:])
