#!/usr/bin/env python3
"""A/B of two `probe_motion_matrix --csv` runs.

    compare_motion_matrix.py CONTROL.csv CANDIDATE.csv
    compare_motion_matrix.py --self-test

fisso and gradino must keep their trace hash (bit for bit the same clock); the
continuo family must not get worse in mean or p95 phase. Until 2026-10-08 the
continuo row also had to show bridge authority and beat the control: the
residual-shape bridge it was written for was removed (docs/TODO.md item 94).
"""
import csv
import io
import sys

FIELDS = {"offset", "family", "mean", "p95", "p995", "trace_hash"}

CSV_HEADER = (
    "offset,family,runs,mean,p95,p995,over50,bpm_error,bpm_over4,releases,"
    "trace_hash"
)


def load_stream(stream):
    rows = list(csv.DictReader(line for line in stream if not line.startswith("#")))
    rows = [row for row in rows if row.get("offset") != "offset"]
    if not rows or not FIELDS.issubset(rows[0]):
        raise ValueError("colonne mancanti")
    return {(int(r["offset"]), r["family"]): r for r in rows}


def load(path):
    with open(path, newline="") as stream:
        return load_stream(stream)


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
        if family == "continuo":
            if float(after["mean"]) > float(before["mean"]):
                failures.append(f"{key}: media peggiorata")
            if float(after["p95"]) > float(before["p95"]):
                failures.append(f"{key}: p95 peggiorato")
    return failures


def _fixture_rows(continuo_mean, continuo_p95, fisso_hash="fissohash"):
    return (
        f"{CSV_HEADER}\n"
        f"0,fisso,16,10.0,20.0,30.0,0,0,0,0,{fisso_hash}\n"
        "0,gradino,16,11.0,21.0,31.0,0,0,0,0,gradhash\n"
        f"0,continuo,16,{continuo_mean},{continuo_p95},200.0,0,0,0,0,conthash\n"
    )


def run_self_test():
    control = load_stream(io.StringIO(_fixture_rows("50.0", "100.0")))
    cases = [
        ("equal passes", _fixture_rows("50.0", "100.0"), []),
        ("better passes", _fixture_rows("40.0", "90.0"), []),
        ("worse continuo fails", _fixture_rows("51.0", "101.0"),
         ["(0, 'continuo'): media peggiorata", "(0, 'continuo'): p95 peggiorato"]),
        ("moved fisso fails", _fixture_rows("50.0", "100.0", "otherhash"),
         ["(0, 'fisso'): traccia cambiata"]),
    ]
    for name, rows, expected in cases:
        got = compare(control, load_stream(io.StringIO(rows)))
        if got != expected:
            print(f"self-test FAIL: {name}: got {got}", file=sys.stderr)
            return 1
    print("PASS compare_motion_matrix self-test")
    return 0


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return run_self_test()
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
