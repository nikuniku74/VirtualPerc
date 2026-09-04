#!/usr/bin/env python3
"""Turn VCSL / VSCO-2-CE one-shots into the app's stroke assets.

Trims pre-roll, cuts before a second hit, high-passes hall rumble, normalises,
fades the tail. Same job as scripts/prep_samples.cpp, without needing VPPrep.
"""
from __future__ import annotations

import math
import struct
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = Path("/tmp/vp-perc-src")
OUT = ROOT / "Assets" / "Percussion"
SR_OUT = 44100

# (src, dest, max_seconds, highpass_hz, keep_pre_ms, shape_tau_sec)
# keep_pre_ms is how much of the rise to keep *before* the strike peak.
# Congas used to keep 8 ms before the first sample above 20% of the file
# peak; on these VCSL takes that peak is the body, ten to twenty milliseconds
# after the hand, so the asset started on the swell and the hit landed after
# the clock's dots. Align to the first-strike peak (max in the first 12 ms)
# and keep 2 ms of pre so the transient is where the old library put it.
# shape_tau_sec > 0 applies an exponential decay after 10 ms so a shaker's
# later rattle does not become a later peak (the attack compensator would
# then hear it tens of milliseconds after a slap written on the same pulse).
JOBS = [
    (SRC / "conga" / "Tumba_HitN_v4_rr1.wav",      OUT / "tumba.wav",            0.55, 40, 2, 0.0),
    (SRC / "conga" / "Tumba_HitN_v3_rr1.wav",      OUT / "tumba_b.wav",           0.55, 40, 2, 0.0),
    (SRC / "conga" / "Tumba_HitN_v2_rr1.wav",      OUT / "tumba_med.wav",         0.50, 40, 2, 0.0),
    (SRC / "conga" / "Conga_HitN_v3_rr2.wav",      OUT / "open.wav",              0.55, 55, 2, 0.0),
    (SRC / "conga" / "Conga_HitN_v2_rr2.wav",      OUT / "open_b.wav",             0.55, 55, 2, 0.0),
    (SRC / "conga" / "Conga_HitN_v1_rr2.wav",      OUT / "open_med.wav",           0.48, 55, 2, 0.0),
    (SRC / "conga" / "Quinto_HitN_v3_rr2.wav",     OUT / "slap.wav",              0.20, 70, 2, 0.0),
    (SRC / "conga" / "Quinto_HitN_v3_rr1.wav",     OUT / "slap_b.wav",             0.20, 70, 2, 0.0),
    (SRC / "conga" / "Quinto_HitN_v1_rr1.wav",     OUT / "slap_med.wav",           0.18, 70, 2, 0.0),
    (SRC / "shaker" / "ShakerHighFaster_Down_rr1.wav", OUT / "shaker_down.wav",    0.16, 80, 3, 0.018),
    (SRC / "shaker" / "ShakerHighFaster_Down_rr2.wav", OUT / "shaker_down_b.wav",  0.16, 80, 3, 0.018),
    (SRC / "shaker" / "ShakerDouble_Down_rr1.wav",     OUT / "shaker_down_med.wav",0.16, 80, 3, 0.018),
    (SRC / "shaker" / "ShakerHighFaster_Up_rr1.wav",   OUT / "shaker_up.wav",      0.14, 80, 3, 0.018),
    (SRC / "shaker" / "ShakerHighFaster_Up_rr2.wav",   OUT / "shaker_up_b.wav",    0.14, 80, 3, 0.018),
    (SRC / "shaker" / "ShakerLowFaster_Up_rr2.wav",    OUT / "shaker_up_med.wav",  0.12, 80, 3, 0.018),
    # CLAP: the VCSL ensemble claps, not the SoloClap velocity ladder - a
    # backbeat clap is a group of hands, and the solo takes are 24-bit (which
    # read_mono does not take) besides. All three are loud takes: the file is
    # normalised anyway, so what the three slots buy is round-robin variety,
    # not a dynamic ladder. 0.35 s keeps the body and the short room tail and
    # drops the rest; 110 Hz is under the clap's thump and over the rumble.
    #
    # rr1/rr3/rr6 and not the louder rr4/rr5, because an ensemble clap's hands
    # do not land together and the takes differ in how long they take to get
    # there: measured to half peak, rr1 1.9 ms, rr3 2.5 ms, rr6 5.7 ms against
    # rr4 4.5 ms (spread 25 ms) and rr5 14.6 ms. One compensation is measured
    # per articulation, so a slow take does not get its own correction - it
    # just lands late, and a backbeat that flams differently every round robin
    # is the one place that is audible as bad timing rather than as a player.
    (SRC / "clap" / "Clap_rr1.wav",                    OUT / "clap.wav",           0.35, 110, 2, 0.0),
    (SRC / "clap" / "Clap_rr3.wav",                    OUT / "clap_b.wav",         0.35, 110, 2, 0.0),
    (SRC / "clap" / "Clap_rr6.wav",                    OUT / "clap_med.wav",       0.32, 110, 2, 0.0),
    # CEMBALO: the tambourine, which is what the name means here (VCSL
    # *Tambourine*, not the cymbal it looks like in a dictionary). Down is the
    # struck hit, up the shake - which is how the instrument is actually
    # played in eighths: hit on the pulse, jingles on the return.
    #
    # The shakes need the shaker treatment, not the drum one. A struck sample
    # is aligned to its peak in the first 12 ms; a shake has no strike, it
    # swells - measured 56 to 130 ms to half peak - so aligned that way the
    # asset would open on the swell and land audibly late on every offbeat.
    # shape_tau > 0 switches the script to the shaker path: onset at 35 % of
    # peak, which lands on the jingles rather than the rise, then an
    # exponential so the later rattle cannot become a second, later peak.
    (SRC / "tamb" / "Tamb2_Hit_v2_rr2_Mid.wav",        OUT / "cembalo_down.wav",     0.26, 200, 2, 0.0),
    (SRC / "tamb" / "Tamb1_Hit_v2_rr1_Mid.wav",        OUT / "cembalo_down_b.wav",   0.26, 200, 2, 0.0),
    (SRC / "tamb" / "Tamb2_Hit_v1_rr1_Mid.wav",        OUT / "cembalo_down_med.wav", 0.24, 200, 2, 0.0),
    (SRC / "tamb" / "Tamb2_Shake_rr3_Mid.wav",         OUT / "cembalo_up.wav",       0.16, 200, 3, 0.020),
    (SRC / "tamb" / "Tamb2_Shake_rr4_Mid.wav",         OUT / "cembalo_up_b.wav",     0.16, 200, 3, 0.020),
    (SRC / "tamb" / "Tamb1_Shake_rr2_Mid.wav",         OUT / "cembalo_up_med.wav",   0.14, 200, 3, 0.020),
]


def read_mono(path: Path) -> tuple[int, list[float]]:
    with wave.open(str(path), "rb") as w:
        n, ch, sr, sw = w.getnframes(), w.getnchannels(), w.getframerate(), w.getsampwidth()
        raw = w.readframes(n)
    if sw != 2:
        raise SystemExit(f"{path}: only 16-bit PCM supported, got {sw*8}-bit")
    samples = struct.unpack("<" + "h" * (n * ch), raw)
    mono = []
    inv = 1.0 / (32768.0 * ch)
    for i in range(n):
        s = 0
        for c in range(ch):
            s += samples[i * ch + c]
        mono.append(s * inv)
    return sr, mono


def highpass(x: list[float], sr: int, f: float) -> list[float]:
    if f <= 0:
        return x
    rc = 1.0 / (2.0 * math.pi * f)
    a = rc / (rc + 1.0 / sr)
    y = [0.0] * len(x)
    prev_x = prev_y = 0.0
    for i, s in enumerate(x):
        y[i] = a * (prev_y + s - prev_x)
        prev_x, prev_y = s, y[i]
    return y


def prepare(src: Path, dst: Path, max_sec: float, hp_hz: float,
            keep_pre_ms: float, shape_tau: float) -> None:
    sr, m = read_mono(src)
    m = highpass(m, sr, hp_hz)
    n = len(m)
    peak = max(abs(v) for v in m) or 1e-9

    # Skip digital silence, then put the *strike* at keep_pre_ms. For a drum
    # the file's global peak can be the body or a later ring, so the window is
    # the first 12 ms - a hand hit, not the resonance that follows it. Shakers
    # keep the old 35%-of-peak rule: their energy is a rattle, not a transient.
    quiet = 0
    thresh_quiet = 0.02 * peak
    for i, v in enumerate(m):
        if abs(v) > thresh_quiet:
            quiet = i
            break

    if shape_tau > 0.0:
        onset = 0
        thresh = 0.35 * peak
        for i, v in enumerate(m):
            if abs(v) > thresh:
                onset = i
                break
        onset = max(0, onset - int(keep_pre_ms * 0.001 * sr))
    else:
        search = min(n, quiet + int(0.012 * sr))
        strike = quiet
        strike_pk = 0.0
        for i in range(quiet, search):
            a = abs(m[i])
            if a > strike_pk:
                strike_pk, strike = a, i
        onset = max(0, strike - int(keep_pre_ms * 0.001 * sr))

    length = min(n - onset, int(max_sec * sr))
    hop = max(1, int(0.005 * sr))
    prev = 1e9
    for i in range(onset + 20 * hop, onset + length - hop, hop):
        pk = max(abs(m[j]) for j in range(i, i + hop))
        if pk > 3.0 * prev and pk > 0.08 * peak:
            length = i - onset - hop
            break
        prev = max(pk, 1e-6)
    length = max(length, int(0.03 * sr))

    ratio = SR_OUT / sr
    out_len = max(16, int(length * ratio))
    out = [0.0] * out_len
    last = onset + length - 1
    for i in range(out_len):
        src_pos = i / ratio
        i0 = int(src_pos)
        f = src_pos - i0
        a = onset + i0
        b = min(a + 1, last)
        out[i] = (1.0 - f) * m[a] + f * m[b]

    op = max(abs(v) for v in out) or 1e-9
    g = 0.95 / op
    out = [v * g for v in out]

    if shape_tau > 0.0:
        hold = int(0.008 * SR_OUT)
        for i in range(hold, out_len):
            out[i] *= math.exp(-(i - hold) / (shape_tau * SR_OUT))
        op = max(abs(v) for v in out) or 1e-9
        g = 0.95 / op
        out = [v * g for v in out]

    fade = min(out_len, int(0.010 * SR_OUT))
    for i in range(fade):
        t = i / float(fade)
        out[out_len - fade + i] *= 0.5 * (1.0 + math.cos(math.pi * t))

    dst.parent.mkdir(parents=True, exist_ok=True)
    pcm = b"".join(struct.pack("<h", max(-32767, min(32767, int(v * 32767.0)))) for v in out)
    with wave.open(str(dst), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR_OUT)
        w.writeframes(pcm)
    print(f"{src.name:32} -> {dst.name:22}  pre {1000.0 * onset / sr:5.1f} ms, {out_len / SR_OUT:5.3f} s")


def main() -> int:
    # A partial download is the normal case, not an error: /tmp gets cleared,
    # and re-cutting one instrument must not mean re-fetching every source and
    # rewriting assets that were fine. Jobs whose source is absent are skipped
    # by name so a silent skip cannot be mistaken for a silent success; only a
    # run with nothing at all to do is a failure.
    runnable = [job for job in JOBS if job[0].is_file()]
    skipped = [job for job in JOBS if not job[0].is_file()]
    for src, dst, *_ in skipped:
        print(f"skip {dst.name:22} (no {src}) - keeping the existing asset")
    if not runnable:
        print("No source recordings found at all.", file=sys.stderr)
        print("Download the VCSL / VSCO-2-CE one-shots into /tmp/vp-perc-src first.",
              file=sys.stderr)
        return 1
    for job in runnable:
        prepare(*job)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
