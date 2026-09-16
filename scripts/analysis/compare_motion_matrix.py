#!/usr/bin/env python3
import csv
import io
import sys

FIELDS = {
    "offset", "family", "mean", "p95", "p995", "trace_hash",
    "recovery_violations", "authority_frames"
}

CSV_HEADER = (
    "offset,family,runs,mean,p95,p995,over50,bpm_error,bpm_over4,releases,"
    "curve,trace_hash,recovery_violations,authority_frames"
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


def _fixture_rows(continuo_mean, continuo_p95, continuo_recovery="0"):
    return (
        f"{CSV_HEADER}\n"
        "0,fisso,16,10.0,20.0,30.0,0,0,0,0,0,fissohash,0,0\n"
        "0,gradino,16,11.0,21.0,31.0,0,0,0,0,0,gradhash,0,0\n"
        f"0,continuo,16,{continuo_mean},{continuo_p95},200.0,0,0,0,0,0,"
        f"conthash,0,0\n"
    )


def run_self_test():
    control = load_stream(io.StringIO(_fixture_rows("50.0", "100.0")))

    improved = load_stream(io.StringIO(_fixture_rows("40.0", "90.0")))
    pass_failures = compare(control, improved)
    if pass_failures:
        print("self-test FAIL: valid candidate should pass", file=sys.stderr)
        for failure in pass_failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    equal = load_stream(io.StringIO(_fixture_rows("50.0", "100.0")))
    equal_failures = compare(control, equal)
    expected = {
        "(0, 'continuo'): media non migliorata",
        "(0, 'continuo'): p95 non migliorato",
    }
    if set(equal_failures) != expected:
        print("self-test FAIL: equal continuo should fail mean and p95 only",
              file=sys.stderr)
        print(f"  got: {equal_failures}", file=sys.stderr)
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
