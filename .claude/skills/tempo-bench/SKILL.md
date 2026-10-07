---
name: tempo-bench
description: The procedure for judging any change to tempo, phase, octave, the one or the clock in VirtualPercussionist - fast real-song bench, teacher truth score, regression gates, before/after table. Use before claiming a timing change is better, when asked to "misura", "banco", "A/B", "verifica", or after editing Source/AI, Source/Tracking or Source/Audio.
---

# tempo-bench: how a timing change is judged

A timing change is accepted only with numbers from this procedure. Read
`.claude/skills/realtime-tempo/SKILL.md` first for what the chain does.

## 0. Preconditions (run on the user's Mac, not in a cloud container)

- Real audio lives only on the Mac: `/tmp/vp-bench/wav/*.wav` (bench songs at
  44.1 and 48 kHz + live excerpts), truth files `*.truth.txt` next to them,
  training data in `~/vp-train`. `/tmp` is wiped on reboot: rebuild the bench
  as in `scripts/analysis/bench_songs.py` / `extract_live.swift`, truth with
  `truth.py make` (venv `~/.venvs/vp-teacher`, works in 5-min chunks).
- Build in the **same** `build-host` every time. An A/B in a `git worktree`
  with a fresh cmake gives false numbers (measured 0.94% vs 5.21%). To measure
  an older commit: `git checkout <sha> -- Source/ Tests/`, rebuild, measure,
  `git checkout HEAD -- Source/ Tests/ scripts/`.
- `cmake --build build-host --target VPTrack VPTests -j8`.

## 1. Baseline first

Run the bench on the unmodified tree under a tag (e.g. `b0`) unless a baseline
tag for the same commit and model already exists in `/tmp/vp-bench/`.

```bash
python3 scripts/analysis/bench_fast.py run b0          # ~1 min, BRANO path (--player)
python3 scripts/analysis/bench_fast.py run b0m ARGS="--gain -12"   # MIXER path, send 12 dB down
```

Runs are bit-identical (`VP_OFFLINE_PACING=1`), so one run per variant is
enough; differences are real, not noise.

## 2. The change, under a switch if possible

Prefer an environment variable read once at `prepare()` so variants run
without rebuilding: `bench_fast.py run t1 MYVAR=1`. Remove the switch before
committing; keep only the chosen value.

## 3. Score

```bash
python3 scripts/analysis/bench_fast.py cmp b0 t1       # vs drum attacks: >25ms%, exits, surges, recentres, skipped 16ths, time >4% off; lists files that get worse
python3 scripts/analysis/truth.py score b0 t1          # vs teacher: right/octave/wrong %, calibration, scatter, p90, >25ms, the one, decoder-grid scatter
```

Report the decision metrics, in this order:

| metric | source | direction |
|---|---|---|
| right level (octave) % | truth | up |
| scatter ms / p90 / > 25 ms % | truth | down |
| the one % | truth | up |
| exits/min, surges/min | cmp | down |
| skipped sixteenths, recentres | cmp | down (audible cracks) |
| files that get worse | cmp + truth per file | **none**, or explained |

Phase and the one are only meaningful where the octave is right; truth.py
already restricts them.

## 4. Regression gates

```bash
B=build-host/VPTests_artefacts/Release/VPTests
for g in --bar --phase-lock --state-timing --tempo-step --tempo-slow --new-input --transport --harmonic-entry; do $B $g | tail -1; done
./scripts/run-tests.sh        # full suite before committing
```

Known pre-existing failures: `--level` 11/5 (same 5 as before); 2-4 flaky cases
on Linux hosts. Anything else failing is a regression.

## 5. Decide and record

- Reject a change that helps one title and costs others, or that wins on
  `cmp` but loses on `truth` (or vice versa) without an explanation.
- Musical changes also need the user's ear on the iPad; say "resta ascolto".
- Write the result in `docs/TODO.md` under the item (✅/🟡/🔴, date, the table,
  what was rejected and why) and, if a rule or a current number changed, in
  `.claude/skills/realtime-tempo/SKILL.md` (§4 numbers) or the matching
  `references/` file. Rejected experiments are recorded too: they stop the next
  session from trying them again.

## Traps, measured

- The published BPM (`s.bpm`, `s.clockBpm`) hides what is heard: judge the
  grid via the phase derivative in `--pulses`, which is what the scripts do.
- First-harmonic resultants (`fold.py`, `phase2.py`) read swung material as
  "out" even when locked. Use `hist.py`.
- Live excerpts start with 3 s of silence, so the bench always sees an input
  start; a song already playing at launch is not covered (item 88) - test it
  with `VPTrack` without silence when touching entry.
- Thresholds measured before 2026-10-02 used the old network's activations.
