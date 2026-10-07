---
name: tempo-bench
description: The procedure for judging any change to tempo, phase, octave, the one or the clock in VirtualPercussionist - fast real-song bench, teacher truth score, regression gates, before/after table. Use before claiming a timing change is better, when asked to "misura", "banco", "A/B", "verifica", or after editing Source/AI, Source/Tracking or Source/Audio.
---

# tempo-bench: how a timing change is judged

A timing change is accepted only with numbers from this procedure. Read
`.claude/skills/realtime-tempo/SKILL.md` first for what the chain does.

## 0. Preconditions (run on the user's Mac, not in a cloud container)

- Real audio lives only on the Mac, in `~/vp-bench/wav/*.wav` (bench songs at
  44.1 and 48 kHz + live excerpts), truth files `*.truth.txt` next to them,
  training data in `~/vp-train`. It used to be `/tmp/vp-bench`, which macOS
  empties on reboot and prunes after 3 days (lost twice on 2026-10-07). To
  rebuild: each `~/Desktop/clicks/NN TITLE.mp3` of the bench through
  `afconvert -f WAVE -d LEI16@44100|48000` into `NN_TITLE_44k|48k.wav`
  (`NonSoulFunky - DanceMesh 2015.mp3` -> `NONSOULFUNKY`); live excerpts with
  `extract_live.swift` from Garden Beach 15.07.26 at 600/1500/2400/3300/4200/5100 s
  and Flamingo 09.07.26 at 1200/2400/4500/5400 s (240 s each), Flamingo 3750 s
  (250 s, `98_FLAMINGO_3750`) and 610 s (280 s, `99_SALLY_LIVE`); then
  `truth.py make --no-clicks` (venv `~/.venvs/vp-teacher`, ~10 min).
- Build in the **same** `build-host` every time for A/B of the working tree.
- To measure an older commit, use a separate `git worktree` with its own build
  dir, and link `third_party/JUCE` and `third_party/onnxruntime` into it: without
  ONNX Runtime the build silently falls back to `StubBeatModel` and every
  number is wrong. Done that way (2026-10-06) a worktree of `c119aad`
  reproduced the item-92 numbers exactly. **Never** `git checkout <sha> --
  Source/` in the working tree: it wipes uncommitted work (it erased an
  uncommitted fix twice on 2026-10-06). Older probes need the offline wait
  raised to 10 s (`scripts/probe_track.cpp`, item 96) to be deterministic.
- `cmake --build build-host --target VPTrack VPTests -j8`.

## 1. Baseline first

Run the bench on the unmodified tree under a tag (e.g. `b0`) unless a baseline
tag for the same commit and model already exists in `~/vp-bench/`.

```bash
python3 scripts/analysis/bench_fast.py run b0          # ~1 min, BRANO path (--player)
python3 scripts/analysis/bench_fast.py run b0m ARGS="--gain -12"   # MIXER path, send 12 dB down
```

Runs are bit-identical (`VP_OFFLINE_PACING=1`, since item 96: audio reaches
the worker at the end of each block, `NeuralBeatTracker::releaseFed`, and the
probe waits up to 10 s), at any `JOBS=N` and alone vs in the bench, so one run
per variant is enough; differences are real, not noise. If two runs of one
build ever differ, fix that first.

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

Known pre-existing failures: `--level` 11/5 (168 BPM at -12 dB and clip, 91
BPM at clip) and `--octave` 7/4 (the 50 BPM cases); 2-4 flaky cases on Linux
hosts. Anything else failing is a regression. `--level` and `--octave` take
~10 min each.

## 5. Decide and record

- Reject a change that helps one title and costs others, or that wins on
  `cmp` but loses on `truth` (or vice versa) without an explanation.
- **Held-out check.** Leave out the songs the change was derived from; keep it
  only if it clearly moves the rest (both 44.1/48 kHz copies of a song go to
  the same side). Ablate one rule at a time with the others on. A rule that
  changes one or two files of 92 has not shown it generalises (items 97-99
  removed this way, item 100). A threshold must sit in a wide gap between the
  cases it separates, not at the edge of a handful.
- Report the aggregate; name songs only as examples of the mechanism.
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
