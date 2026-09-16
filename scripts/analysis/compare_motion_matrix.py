#!/usr/bin/env python3
import csv
import io
import sys

FIELDS = {
    "offset", "family", "mean", "p95", "p995", "trace_hash",
    "recovery_violations", "authority_frames"
}


def load(path):
    with open(path, newline="") as stream:
        rows = list(csv.DictReader(line for line in stream if not line.startswith("#")))
    rows = [row for row in rows if row.get("offset") != "offset"]
    if not rows or not FIELDS.issubset(rows[0]):
        raise ValueError(f"{path}: colonne mancanti")
    return {(int(r["offset"]), r["family"]): r for r in rows}


def compare(control, candidate):
    failures = []
    if control.keys() != candidate.keys():
        return ["le banche A/B non coincidono"]
    for key, before in control.items():
        after = candidate[key]
        family = key[1]
        if family in ("fisso", "gradino"):
            if before["trace_hash"] != after["trace_hash"]:
                failures.append(f"{key}: traccia cambiata")
            if int(after["authority_frames"]) != 0:
                failures.append(f"{key}: autorita' non nulla")
        if family == "continuo":
            if not float(after["mean"]) < float(before["mean"]):
                failures.append(f"{key}: media non migliorata")
            if not float(after["p95"]) < float(before["p95"]):
                failures.append(f"{key}: p95 non migliorato")
            if int(after["recovery_violations"]) != 0:
                failures.append(f"{key}: rientro oltre due beat")
    return failures


def main():
    if len(sys.argv) != 3:
        print("usage: compare_motion_matrix.py CONTROL.csv CANDIDATE.csv",
              file=sys.stderr)
        return 2
    failures = compare(load(sys.argv[1]), load(sys.argv[2]))
    for failure in failures:
        print(f"FAIL {failure}")
    if not failures:
        print("PASS motion matrix A/B")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
