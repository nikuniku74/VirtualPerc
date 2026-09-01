#!/usr/bin/env python3
"""Build a bank manifest from a folder of WAV loops.

The manifest format is documented in docs/RECORDED_LOOPS.md. This writes it
from a small CSV so the numbers that matter - native tempo, bars, swing, which
stem - are stated once by the person who recorded the take, and everything that
can be read off the file (rate, channels, length) is read off the file rather
than typed twice.

It also refuses, loudly, the two mistakes that are silent damage later:

  - a file that is not 48 kHz, because every marker in the manifest is a sample
    position and a 44.1 kHz file described at 48 puts every quarter 8.8% away
    from where it says it is;
  - a body length that is not the exact length of the stated bars at the stated
    tempo, because a loop that is a fraction short is a click once a bar.

Usage:
    scripts/make_loop_manifest.py Assets/Loops/dance/loops.csv

CSV columns (header required):
    file,style,role,stem,bpm,bars,swing,intensity,take[,first_beat_sample]

Example row:
    dance_grooveA_120_straight_congas_t1.wav,dance,grooveA,congas,120,2,0,0.5,1
"""

import argparse
import csv
import os
import struct
import sys

ROLES = ("grooveA", "grooveB", "fill", "intro")
STEMS = ("congas", "shaker")
STYLES = ("marcha", "rock", "dance", "pop", "samba", "funk", "reggae", "bossa", "twoone")

TOLERANCE_FRAMES = 8   # a handful of samples of slop on the cut, no more


def read_wav_header(path):
    """Sample rate, channels, frame count. Deliberately minimal: the C++ loader
    is the one that has to decode, this only has to describe."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 44 or data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE file")

    rate = channels = bits = None
    frames = None
    pos = 12
    while pos + 8 <= len(data):
        tag = data[pos:pos + 4]
        (size,) = struct.unpack("<I", data[pos + 4:pos + 8])
        body = pos + 8
        if tag == b"fmt " and size >= 16:
            _, channels, rate, _, _, bits = struct.unpack("<HHIIHH", data[body:body + 16])
        elif tag == b"data":
            if bits is None or channels is None:
                raise ValueError("data chunk before fmt chunk")
            frames = size // (channels * (bits // 8))
        pos = body + size + (size & 1)

    if rate is None or frames is None:
        raise ValueError("missing fmt or data chunk")
    return rate, channels, frames


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", help="the CSV describing the takes")
    ap.add_argument("-o", "--out", help="manifest to write (default: bank.vploops beside the CSV)")
    ap.add_argument("--bank", help="bank name (default: the folder name)")
    ap.add_argument("--markers", action="store_true",
                    help="write an explicit beat marker per quarter instead of "
                         "leaving them to be assumed equally spaced")
    args = ap.parse_args()

    folder = os.path.dirname(os.path.abspath(args.csv))
    out_path = args.out or os.path.join(folder, "bank.vploops")
    bank = args.bank or os.path.basename(folder)

    lines = ["# VirtualPerc loop bank - see docs/RECORDED_LOOPS.md",
             "version 1",
             "bank %s" % bank]
    problems = []

    with open(args.csv, newline="") as f:
        for n, row in enumerate(csv.DictReader(f), start=2):
            name = (row.get("file") or "").strip()
            if not name:
                continue
            path = os.path.join(folder, name)
            where = "%s line %d" % (os.path.basename(args.csv), n)

            if not os.path.exists(path):
                problems.append("%s: %s is not there" % (where, name))
                continue
            try:
                rate, channels, frames = read_wav_header(path)
            except ValueError as e:
                problems.append("%s: %s: %s" % (where, name, e))
                continue

            style = (row.get("style") or "dance").strip().lower()
            role = (row.get("role") or "grooveA").strip()
            stem = (row.get("stem") or "congas").strip().lower()
            if style not in STYLES:
                problems.append("%s: unknown style %r" % (where, style))
            if role not in ROLES:
                problems.append("%s: unknown role %r" % (where, role))
            if stem not in STEMS:
                problems.append("%s: unknown stem %r" % (where, stem))

            bpm = float(row["bpm"])
            bars = int(row["bars"])
            swing = float(row.get("swing") or 0.0)
            intensity = float(row.get("intensity") or 0.5)
            take = int(row.get("take") or 1)
            first = int(row.get("first_beat_sample") or 0)

            if rate != 48000:
                problems.append("%s: %s is at %d Hz - the library is 48 kHz"
                                % (where, name, rate))
            if channels not in (1, 2):
                problems.append("%s: %s has %d channels" % (where, name, channels))

            beats = bars * 4
            beat_frames = 60.0 / bpm * rate
            body = beats * beat_frames
            have = frames - first
            if abs(have - body) > TOLERANCE_FRAMES:
                problems.append(
                    "%s: %s is %d frames of body, %d bars at %.4f BPM wants %.0f "
                    "(off by %.0f - the cut is wrong, or the BPM is)"
                    % (where, name, have, bars, bpm, body, have - body))

            loop_id = os.path.splitext(name)[0]
            lines.append("")
            lines.append("[loop]")
            lines.append("id %s" % loop_id)
            lines.append("file %s" % name)
            lines.append("style %s" % style)
            lines.append("role %s" % role)
            lines.append("stem %s" % stem)
            lines.append("bpm %.4f" % bpm)
            lines.append("bars %d" % bars)
            lines.append("meter 4/4")
            lines.append("firstBeatSample %d" % first)
            lines.append("frames %d" % frames)
            lines.append("channels %d" % channels)
            lines.append("sampleRate %d" % rate)
            lines.append("intensity %.3f" % intensity)
            lines.append("swing %.3f" % swing)
            lines.append("take %d" % take)
            if args.markers:
                marks = [str(int(round(first + i * beat_frames))) for i in range(beats)]
                lines.append("beats %s" % " ".join(marks))

    if problems:
        for p in problems:
            sys.stderr.write("error: %s\n" % p)
        sys.stderr.write("\n%d problem(s); nothing written.\n" % len(problems))
        return 1

    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote %s (%d loops)" % (out_path, sum(1 for l in lines if l == "[loop]")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
