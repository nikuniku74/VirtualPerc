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
  training data in `~/vp-train`. Held-out material is kept beside it, out of
  `bench_fast.py`: `~/vp-bench/new` (songs never tuned on, e.g. FBI 22.01.10,
  24 songs) and `~/vp-bench/long` (whole sets, 48 and 104 min, for song
  changes); run them with VPTrack in a loop, `truth.py score` finds their truth
  too. The bench is the material the app was tuned on: FBI in MIXER reads
  55.6% right tempo against 86.2% on the bench (docs/TODO.md item 141), so a
  change that only holds on the bench is not shown to hold. The teacher's
  level is the user's: blind A/B on 8 excerpts where teacher and app disagree
  by an octave, the user picked the teacher 8/8 (2026-10-22). It used to be `/tmp/vp-bench`, which macOS
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

For continuous sets, also compare the previously correct beats by section.
Inspect contiguous runs that were correct before and are now wrong (the
existing four-second sustained-lock window is a useful witness). A gain in
whole-set correct share can hide a lost following song: TODO156 rejected a
variant with81gained/35lost for this reason. Inspect phase/bar on the common
correct cohort too; adding correct beats changes the conditional population.

## 4. Regression gates

```bash
B=build-host/VPTests_artefacts/Release/VPTests
for g in --bar --phase-lock --state-timing --tempo-step --tempo-slow --new-input --transport --harmonic-entry; do $B $g | tail -1; done
./scripts/run-tests.sh        # full suite before committing
```

Run the full suite with its **default worker pacing**, without exporting
`VP_OFFLINE_PACING` globally. That flag is for VPTrack and compatible probes:
two direct-worker fixtures do not release held audio, and the worker-wake test
measures the real polling delay. TODO159's supplemental whole-suite paced run
adds four failures that pass normally (including a mismatched-tempo canceller
pair); do not use that total as the production regression result. Latest
verified ledger: TODO168 after withdrawing165/166, default full-wake798/0;
325/325 paired traces byte-identical to164, no regression gates lost.
The164 reference has52 actual-output lifecycle pairs removing38 red openings to0.
Nominal timing alone cannot certify heard startup. See verification.md for
CPU cost,64-frame stress failures and physical iPad evidence. TODO163 closes
the earlier5ms fortissimo residual:9 velocities ×3 gains ×16 strokes ×3 RR,
0ms isolated spread (53/0). Earlier totals/remaining5ms are historical.
The user hears a cleaner but imperfectly timed Canzone entrance; its tonal
intro phase remains displaced despite correct BPM. No phase improvement is
claimed. Latest build3 compiles and signature passes; build3 installed/launched on the iPad27Oct. User confirms the logo without a
black tile, animated dots and locked purchase page; this is visual validation,
not a new musical or thermal test. See verification.md for48 CPU runs and host
wall-time outliers; do not claim all callback maxima pass or iPad thermal safety.
The full suite is slow; buffered silence alone is not a hang.

Scripted bar-reentry fixtures drain already-fed input every eight callbacks
and at section boundaries. Feeding a long section before draining couples its
bar votes to host load. Keep the same two-bar recovery bound; never solve that
test-harness problem by extending the deadline or globally changing worker
pacing. TODO160 retains the initial failed run and12 old/new isolated repeats.

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

- `EngineSnapshot::percussionAudible` and the legacy `.pul` `suona` column
  mean the tracker's permission, before drift guard/stand-down. They do not
  prove the part is actually sounding. For entry/muting bugs, add VPTrack
  `--audibility file`: actual rendered output peak and guard/stand-down from
  t=0. Ordinary truth scoring excludes20s and would miss a3s false intro.
  Keep the old clock baseline, compare actual audio separately, and test PLAY
  followed immediately by START (0.1/0.5/1/2s), not only3/7s. TODO164 reproduced
  Una canzone per te's false red entry despite791 passing checkpoint tests.
- The published BPM (`s.bpm`, `s.clockBpm`) hides what is heard: judge the
  grid via the phase derivative in `--pulses`, which is what the scripts do.
- First-harmonic resultants (`fold.py`, `phase2.py`) read swung material as
  "out" even when locked. Use `hist.py`.
- Live excerpts start with 3 s of silence, so the bench always sees an input
  start; a song already playing at launch is not covered (item 88) - test it
  with `VPTrack` without silence when touching entry.
- Thresholds measured before 2026-10-02 used the old network's activations.
- Removing code that is ever active cannot be bit-identical to HEAD. Check the
  removal bit for bit against a worktree of HEAD with that code forced off,
  then judge off vs on with `cmp`/`truth` as any change (item 94: the motion
  bridge acted in 14-18 of 54 files; every diff started on its first frame).

### Host sleep / DarkWake (TODO162, 2026-10-26)

`pmset -g log` proved repeated Idle Sleep and maintenance DarkWake during the
long runs. This can expire a worker's wall-clock drain deadline with queued
audio still present; it is not evidence of an engine timing regression. An
idle-sleep assertion started inside DarkWake does not itself promote the Mac
to full wake. The macOS test driver now runs `caffeinate -i -u` for the test's
lifetime: wake once, then prevent idle sleep; no deadline or musical recovery
bound is relaxed. Keep the failed logs, the power log and a complete full-wake
repeat. Do not dismiss a failure as sleep without that evidence. On iPad the
audio session, not this developer-only wrapper, owns audio activity.
