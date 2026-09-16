# Tempo Motion Shadow Tracker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Follow causal accelerando and rallentando on file and mixer feeds without changing stable-tempo, abrupt-step, octave, or monotonic-clock behaviour.

**Architecture:** A fixed-size `TempoMotionTracker` runs beside `BeatDecoder` on the neural worker. It estimates local period velocity and uncertainty from accepted quarter beats, keeps authority exactly zero until dynamic evidence separates from onset scatter, then supplies a bounded bridge target only while the decoder is still in `TempoRegime::fixed`; the existing transition, octave, live-fit, phase, and clock owners remain unchanged.

**Tech Stack:** C++20, CMake/Ninja, JUCE 8.0.15, deterministic `VPTests`, `VPAlign`, standalone C++ probes, and Python 3/Numpy analysis scripts.

## Global Constraints

- The percussion clock never restarts because BPM changes.
- Phase remains continuous while percussion is audible; no backward phase jump, duplicate pulse, or skipped pulse.
- Audio thread: no allocation, lock, I/O, ONNX inference, or unbounded loop.
- One observation never selects BPM or phase.
- Confirmed abrupt transitions, octave/grid rebuilds, TAP, and manual tempo retain priority.
- The bridge changes at most 0.75% per accepted beat and requests a target no farther than 4% from the currently committed BPM.
- Authority zero must leave the existing fixed branch bit-identical.
- File/mixer validation completes before any microphone-specific authority is designed or enabled.
- Tuning decisions use the complete fixed/continuous/step populations and independent offsets, never one song or one ramp.

---

### Task 1: Preserve the diagnostic-only control checkpoint

**Files:**
- Commit unchanged checkpoint content: `.claude/skills/realtime-tempo/SKILL.md`
- Commit unchanged checkpoint content: `Source/AI/BeatDecoder.cpp`
- Commit unchanged checkpoint content: `Source/AI/BeatDecoder.h`
- Commit unchanged checkpoint content: `docs/HANDOFF_TEMPO.md`
- Commit unchanged checkpoint content: `docs/TODO.md`
- Commit unchanged checkpoint content: `scripts/probe_motion_matrix.cpp`
- Exclude: `third_party/JUCE`
- Exclude: `pop-dance-congas-procedural-124.wav`

**Interfaces:**
- Consumes: current working-tree curvature diagnostic and corrected MIXER-path matrix.
- Produces: one reproducible Git checkpoint from which control binaries and numbers can be rebuilt.

- [ ] **Step 1: Verify that curvature has no production authority**

Run:

```bash
rg -n "motionFitEvidence.*enterRegime|enterRegime.*motionFitEvidence" Source/AI/BeatDecoder.cpp
```

Expected: no match.

- [ ] **Step 2: Build and run the current global control**

Run:

```bash
clang++ -std=c++17 -O2 -Wno-unused-function -I Source \
  scripts/probe_motion_matrix.cpp Source/AI/BeatDecoder.cpp \
  Source/AI/TempoEstimator.cpp Source/AI/BeatHmm.cpp \
  Source/Tracking/TempoFollower.cpp -o /tmp/probe_motion_matrix-control
/tmp/probe_motion_matrix-control | tee /tmp/vp-motion-control-offset-0.txt
```

Expected: exit 0, `fisso` has zero curve proofs, and `continuo` has at least one.

- [ ] **Step 3: Run the protected control gates**

Run:

```bash
cmake -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --target VPTests VPAlign
./build-host/VPTests_artefacts/Release/VPTests --tempo-step
./build-host/VPTests_artefacts/Release/VPTests --tempo-slow
./build-host/VPTests_artefacts/Release/VPTests --evidence
./build-host/VPAlign_artefacts/Release/VPAlign --ramps
```

Expected: the three `VPTests` filters pass. `VPAlign --ramps` reports the four known red ramp rows; this is the failing behaviour the tracker must change, not a green prerequisite.

- [ ] **Step 4: Commit only the control checkpoint**

Run:

```bash
git add .claude/skills/realtime-tempo/SKILL.md \
  Source/AI/BeatDecoder.cpp Source/AI/BeatDecoder.h \
  docs/HANDOFF_TEMPO.md docs/TODO.md scripts/probe_motion_matrix.cpp
git diff --cached --check
git commit -m "test: establish global tempo motion baseline"
```

Expected: the six tempo files are committed; JUCE and the untracked WAV remain outside the commit.

---

### Task 2: Make the global benchmark an A/B acceptance gate

**Files:**
- Modify: `scripts/probe_motion_matrix.cpp:82-99,220-338,340-432`
- Create: `scripts/analysis/compare_motion_matrix.py`

**Interfaces:**
- Consumes: deterministic scenario seed, decoder BPM, sounding clock phase, and truth beat index.
- Produces: `--csv` rows with `offset,family,mean,p95,p995,over50,bpm_error,bpm_over4,releases,curve,trace_hash,recovery_violations,authority_frames`.
- Produces: `compare_motion_matrix.py CONTROL.csv CANDIDATE.csv`, exit 0 only when fixed and step hashes are identical, every continuous bank improves mean and p95, and candidate recovery violations are zero.

- [ ] **Step 1: Add a red comparator test before changing decoder behaviour**

Create `scripts/analysis/compare_motion_matrix.py` with a self-test and strict comparison:

```python
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
```

- [ ] **Step 2: Extend each score with an exact behaviour digest**

Add fixed-width FNV-1a helpers and score fields in `probe_motion_matrix.cpp`:

```cpp
#include <cstdint>

struct Score
{
    std::vector<double> phaseMs;
    double tempoErrorSum = 0.0;
    double measured = 0.0;
    int phaseOver50 = 0;
    int tempoOver4 = 0;
    int fixedToLive = 0;
    int curveProofs = 0;
    int recoveryViolations = 0;
    int authorityFrames = 0;
    uint64_t traceHash = 1469598103934665603ULL;
};

void hashWord (uint64_t& hash, uint32_t word) noexcept
{
    for (int byte = 0; byte < 4; ++byte)
    {
        hash ^= static_cast<uint8_t> (word >> (byte * 8));
        hash *= 1099511628211ULL;
    }
}

void hashFloat (uint64_t& hash, float value) noexcept
{
    uint32_t word = 0;
    static_assert (sizeof (word) == sizeof (value));
    std::memcpy (&word, &value, sizeof (word));
    hashWord (hash, word);
}
```

After warm-up, hash `h.bpm`, `clock.beatPhase()`, `clock.currentTempo()`, and the truth beat index on every frame. Use the exact float bits; no decimal rounding is allowed in the identity gate.

- [ ] **Step 3: Add causal recovery accounting**

Track each phase excursion that begins after shadow authority first becomes positive:

```cpp
bool shadowProven = false;
size_t excursionTruthBeat = 0;
bool excursionOpen = false;
bool excursionFailed = false;

// Inside the scored-frame block, after motion diagnostics are available:
shadowProven = shadowProven || motionAuthority > 0.0f;
if (shadowProven && phaseMs > 50.0 && ! excursionOpen)
{
    excursionOpen = true;
    excursionFailed = false;
    excursionTruthBeat = truth;
}
if (excursionOpen && phaseMs <= 50.0)
{
    excursionOpen = false;
    excursionFailed = false;
}
if (excursionOpen && ! excursionFailed && truth > excursionTruthBeat + 2)
{
    ++score.recoveryViolations;
    excursionFailed = true;
}
```

Before Task 4 exposes shadow diagnostics, set `motionAuthority` to `0.0f`. The saved control binary therefore has the same CSV schema as the later candidate.

- [ ] **Step 4: Add `--csv` output and preserve human-readable output**

Parse `--csv`, suppress the formatted heading in that mode, and emit one aggregate line per family:

```cpp
std::printf (
    "%u,%s,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%d,%d,%016llx,%d,%d\n",
    offset, label, a.runs, a.meanSum / n, a.p95Sum / n, a.worst,
    a.phaseOver50 / n, a.tempoError / n, a.tempoOver4 / n,
    a.releases, a.curveProofs,
    static_cast<unsigned long long> (a.traceHash),
    a.recoveryViolations, a.authorityFrames);
```

The first CSV line must be:

```text
offset,family,runs,mean,p95,p995,over50,bpm_error,bpm_over4,releases,curve,trace_hash,recovery_violations,authority_frames
```

- [ ] **Step 5: Build the durable control binary and prove the new gate is red**

Run:

```bash
clang++ -std=c++17 -O2 -Wno-unused-function -I Source \
  scripts/probe_motion_matrix.cpp Source/AI/BeatDecoder.cpp \
  Source/AI/TempoEstimator.cpp Source/AI/BeatHmm.cpp \
  Source/Tracking/TempoFollower.cpp -o /tmp/probe_motion_matrix-control
: > /tmp/vp-motion-control.csv
for offset in 0 16 32 48; do
  /tmp/probe_motion_matrix-control --quick --offset "$offset" --csv \
    >> /tmp/vp-motion-control.csv
done
python3 scripts/analysis/compare_motion_matrix.py \
  /tmp/vp-motion-control.csv /tmp/vp-motion-control.csv
```

Expected: comparator exits 1 only because continuous mean and p95 are equal, proving the improvement gate can fail; fixed and step identity checks do not fail.

- [ ] **Step 6: Commit the benchmark**

Run:

```bash
git add scripts/probe_motion_matrix.cpp scripts/analysis/compare_motion_matrix.py
git diff --cached --check
git commit -m "test: gate continuous tempo motion globally"
```

---

### Task 3: Implement the fixed-size shadow estimator test-first

**Files:**
- Create: `Source/AI/TempoMotionTracker.h`
- Create: `Source/AI/TempoMotionTracker.cpp`
- Create: `Tests/TestTempoMotion.cpp`
- Create: `Tests/TestTempoMotion.h`
- Modify: `Tests/TestMain.cpp:1102-1212`
- Modify: `CMakeLists.txt:21-47,302-307`

**Interfaces:**
- Consumes: `TempoMotionObservation`.
- Produces: `TempoMotionOutput TempoMotionTracker::observe(const TempoMotionObservation&) noexcept`.
- Produces: `void TempoMotionTracker::reset(bool fullModelReset, TempoMotionVeto reason) noexcept`.
- Produces: `VPTests --tempo-motion`.

- [ ] **Step 1: Declare the public fixed-size interface**

Create `Source/AI/TempoMotionTracker.h`:

```cpp
#pragma once

#include "Core/Types.h"

#include <array>
#include <cstdint>

namespace vp
{
enum class TempoMotionShadowState : int { idle, proving, active, vetoed };
enum class TempoMotionVeto : int
{
    none,
    transition,
    octaveOrGrid,
    inputEpoch,
    discontinuity,
    staleBeats,
    badObservation,
    notDirect,
    weakFit
};

struct TempoMotionObservation
{
    double beatTimeSec = 0.0;
    float beatStrength = 0.0f;
    int gridQuarterSteps = 1;
    float committedBpm = 0.0f;
    float shortFitBpm = 0.0f;
    float longFitBpm = 0.0f;
    float shortFitResidual = 1.0f;
    float fitCoverage = 0.0f;
    float fitIndexGap = 1.0f;
    float intervalJitter = 1.0f;
    TempoTransitionState transitionState = TempoTransitionState::stable;
    int transitionRefitBeats = 0;
    bool lineFeed = false;
    bool fixedRegime = false;
};

struct TempoMotionOutput
{
    TempoMotionShadowState state = TempoMotionShadowState::idle;
    TempoMotionVeto veto = TempoMotionVeto::none;
    float predictedBpm = 0.0f;
    float periodDeltaPerBeat = 0.0f;
    float uncertainty = 1.0f;
    float authority = 0.0f;
    bool proofClosed = false;
};

class TempoMotionTracker
{
public:
    void reset (bool fullModelReset = true,
                TempoMotionVeto reason = TempoMotionVeto::none) noexcept;
    TempoMotionOutput observe (const TempoMotionObservation&) noexcept;
    TempoMotionOutput output() const noexcept { return lastOutput; }

private:
    static constexpr int kWindow = 12;
    std::array<float, kWindow> periods {};
    int write = 0;
    int filled = 0;
    double lastBeatTimeSec = -1.0;
    float modelPeriodSec = 0.0f;
    float modelVelocity = 0.0f;
    float authority = 0.0f;
    int proofBeats = 0;
    int direction = 0;
    TempoMotionOutput lastOutput {};
};
} // namespace vp
```

- [ ] **Step 2: Write deterministic failing tests**

Create `Tests/TestTempoMotion.h`:

```cpp
#pragma once
void vpRunTempoMotionTrackerTests (int& passed, int& failed);
```

Create `Tests/TestTempoMotion.cpp` with these six independent assertions:

```cpp
#include "TestTempoMotion.h"
#include "AI/TempoMotionTracker.h"

#include <cmath>
#include <cstdio>

namespace
{
vp::TempoMotionObservation observation (double t, float committed,
                                        float shortFit, int steps = 1)
{
    vp::TempoMotionObservation o;
    o.beatTimeSec = t;
    o.beatStrength = 0.9f;
    o.gridQuarterSteps = steps;
    o.committedBpm = committed;
    o.shortFitBpm = shortFit;
    o.longFitBpm = committed;
    o.shortFitResidual = 0.008f;
    o.fitCoverage = 1.0f;
    o.fitIndexGap = 1.0f;
    o.intervalJitter = 0.004f;
    o.transitionState = vp::TempoTransitionState::stable;
    o.lineFeed = true;
    o.fixedRegime = true;
    return o;
}
}

void vpRunTempoMotionTrackerTests (int& passed, int& failed)
{
    auto expect = [&] (bool condition, const char* name)
    {
        condition ? ++passed : ++failed;
        std::printf ("  %s  %s\n", condition ? "PASS" : "FAIL", name);
    };

    for (float bpm : { 52.0f, 100.0f, 168.0f })
    {
        vp::TempoMotionTracker tracker;
        double t = 0.0;
        bool silent = true;
        bool finite = true;
        for (int beat = 0; beat < 96; ++beat)
        {
            const int steps = beat > 0 && (beat % 31) == 0 ? 2 : 1;
            const double jitter = ((beat * 17) % 11 - 5) * 0.0015;
            const double isolatedOutlier = (beat % 29) == 17 ? 0.020 : 0.0;
            t += steps * 60.0 / bpm + jitter + isolatedOutlier;
            auto input = observation (t, bpm, bpm, steps);
            if (isolatedOutlier != 0.0)
                input.shortFitResidual = 0.080f;
            const auto out = tracker.observe (input);
            silent &= out.authority == 0.0f;
            finite &= std::isfinite (out.predictedBpm)
                   && std::isfinite (out.uncertainty);
        }
        expect (silent && finite, "fixed jitter never earns motion authority");
    }

    vp::TempoMotionTracker ramp;
    double t = 0.0;
    bool rose = false;
    for (int beat = 0; beat < 32; ++beat)
    {
        const float truth = 100.0f + 0.35f * beat;
        t += 60.0 / truth;
        rose |= ramp.observe (observation (t, 100.0f, truth)).authority > 0.0f;
    }
    expect (rose, "a clean accelerando earns bounded authority");

    vp::TempoMotionTracker falling;
    t = 0.0;
    bool fell = false;
    for (int beat = 0; beat < 32; ++beat)
    {
        const float truth = 112.0f - 0.30f * beat;
        t += 60.0 / truth;
        fell |= falling.observe (observation (t, 112.0f, truth)).authority > 0.0f;
    }
    expect (fell, "a clean rallentando earns bounded authority");

    vp::TempoMotionTracker missed;
    missed.observe (observation (0.0, 100.0f, 100.0f));
    const auto afterMiss =
        missed.observe (observation (1.2, 100.0f, 100.0f, 2));
    expect (std::fabs (afterMiss.predictedBpm - 100.0f) < 0.5f,
            "a two-quarter gap is normalized before fitting");

    auto veto = observation (2.0, 100.0f, 103.0f);
    veto.transitionState = vp::TempoTransitionState::suspected;
    expect (ramp.observe (veto).authority == 0.0f,
            "an abrupt transition vetoes the shadow");

    ramp.reset (false, vp::TempoMotionVeto::discontinuity);
    expect (ramp.output().authority == 0.0f,
            "a dropout clears velocity authority");
    falling.reset (true, vp::TempoMotionVeto::octaveOrGrid);
    expect (falling.output().authority == 0.0f,
            "a grid rebuild clears the complete shadow model");
}
```

- [ ] **Step 3: Register and run the red test**

Add `Tests/TestTempoMotion.cpp` to `VPTests` sources, include `TestTempoMotion.h` in `TestMain.cpp`, and add:

```cpp
if (argc > 1 && std::string (argv[1]) == "--tempo-motion")
{
    vpRunTempoMotionTrackerTests (gPassed, gFailed);
    std::printf ("\n%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
```

Run:

```bash
cmake --build build-host --target VPTests
```

Expected: link failure because `TempoMotionTracker::observe` and `reset` are not implemented.

- [ ] **Step 4: Implement robust period/velocity evidence**

Create `Source/AI/TempoMotionTracker.cpp`. The implementation must:

1. normalize `(beatTimeSec - lastBeatTimeSec) / gridQuarterSteps`;
2. keep twelve periods in fixed storage;
3. winsorize samples around median ±
   `3 * max(MAD, committedPeriod * max(0.0015, intervalJitter))`;
4. fit `period(i) = intercept + slope * i`;
5. compute slope standard error and the full-window displacement;
6. require five samples, direct fixed input, coverage ≥0.75, short residual
   ≤0.035, index gap within 0.15 of one, no transition/refit veto, slope
   z-score ≥4, displacement ≥0.6%, and matching signs for `slope` and
   `(60 / shortFitBpm) - (60 / committedBpm)`;
7. require three same-direction qualifying beats;
8. increase authority by at most 0.35 per beat and clear it on contradiction;
9. predict one beat ahead and clamp predicted BPM to 50–190.

Use these exact numerical guards at the first global run:

```cpp
constexpr int kMinimumSamples = 5;
constexpr int kProofBeats = 3;
constexpr float kMinimumCoverage = 0.75f;
constexpr float kMaximumShortResidual = 0.035f;
constexpr float kMaximumIndexGapError = 0.15f;
constexpr float kMinimumRateZ = 4.0f;
constexpr float kMinimumWindowDisplacement = 0.006f;
constexpr float kAuthorityPerBeat = 0.35f;
constexpr float kPeriodNoiseFloor = 0.0015f;
```

The output assignment is:

```cpp
const float predictedPeriod =
    std::clamp (intercept + slope * static_cast<float> (filled),
                60.0f / 190.0f, 60.0f / 50.0f);
lastOutput.predictedBpm = 60.0f / predictedPeriod;
lastOutput.periodDeltaPerBeat = slope;
lastOutput.uncertainty = std::clamp (4.0f / std::max (4.0f, rateZ), 0.0f, 1.0f);
lastOutput.authority = authority;
lastOutput.proofClosed = previousAuthority == 0.0f && authority > 0.0f;
lastOutput.state = authority > 0.0f ? TempoMotionShadowState::active
                                    : TempoMotionShadowState::proving;
```

Add `Source/AI/TempoMotionTracker.cpp` to `VP_CORE_SOURCES`.

- [ ] **Step 5: Run the focused tests**

Run:

```bash
cmake --build build-host --target VPTests
./build-host/VPTests_artefacts/Release/VPTests --tempo-motion
```

Expected: all tracker assertions pass; fixed cases report exactly zero authority.

- [ ] **Step 6: Commit the isolated component**

Run:

```bash
git add CMakeLists.txt Source/AI/TempoMotionTracker.h \
  Source/AI/TempoMotionTracker.cpp Tests/TestTempoMotion.h \
  Tests/TestTempoMotion.cpp Tests/TestMain.cpp
git diff --cached --check
git commit -m "feat: add causal tempo motion shadow tracker"
```

---

### Task 4: Integrate observations, vetoes, resets, and diagnostics without moving BPM

**Files:**
- Modify: `Source/AI/BeatDecoder.h:1-8,74-107,242-267,430-490`
- Modify: `Source/AI/BeatDecoder.cpp:492-568,570-640,866-887,890-1018,1309-1378,2122-2811,2507-2637,3481-3717`
- Modify: `Source/AI/BeatHypothesis.h:68-92`
- Modify: `scripts/probe_motion_matrix.cpp:220-338`

**Interfaces:**
- Consumes: newest accepted beat, inferred integer quarter gap, current fit quality, regime, and transition state.
- Produces: `BeatDecoder::updateMotionShadow() noexcept`.
- Produces: worker/probe diagnostics only; this task must not read shadow authority in the fixed BPM branch.

- [ ] **Step 1: Add a failing decoder integration assertion**

Extend `TestTempoMotion.cpp` with a line-feed `BeatDecoder` fixture that feeds a fixed 100 BPM activation train for 90 seconds and asserts:

```cpp
const auto diagnostics = decoder.diagnostics();
fixedSilent &= diagnostics.motionShadowAuthority == 0.0f;
```

The exact BPM/clock identity assertion belongs to the matrix hash, where the saved control binary supplies every frame without embedding a rounded golden value. The initial unit-test compile must fail because the new diagnostic field does not exist.

- [ ] **Step 2: Own the tracker and expose worker diagnostics**

Include `AI/TempoMotionTracker.h` in `BeatDecoder.h` and add:

```cpp
void updateMotionShadow() noexcept;
void resetMotionShadow (bool full, TempoMotionVeto reason) noexcept;

TempoMotionTracker motionTracker;
TempoMotionOutput motionShadow {};
uint32_t motionObservedBeatSerial = 0;
```

Add to `BeatDecoder::Diagnostics` and `BeatHypothesis`:

```cpp
float motionShadowBpm = 0.0f;
float motionShadowPeriodDelta = 0.0f;
float motionShadowUncertainty = 1.0f;
float motionShadowAuthority = 0.0f;
int motionShadowState = static_cast<int> (TempoMotionShadowState::idle);
int motionShadowVeto = static_cast<int> (TempoMotionVeto::none);
```

These fields are diagnostic. `BeatTracker` must not consume them as tempo or phase owners.

- [ ] **Step 3: Feed exactly one observation per accepted beat**

Call `updateMotionShadow()` in `updateTempo()` after short/long fit fields and curvature diagnostics are current, but before the regime/publication switches. Guard with `motionObservedBeatSerial == beatSerial`.

Build the observation from the newest two entries in the fixed beat ring:

```cpp
void BeatDecoder::updateMotionShadow() noexcept
{
    if (motionObservedBeatSerial == beatSerial || beatFilled < 1)
        return;
    motionObservedBeatSerial = beatSerial;

    const int newest = (beatWrite - 1 + kBeatHistory) % kBeatHistory;
    int steps = 1;
    if (beatFilled >= 2)
    {
        const int previous = (newest - 1 + kBeatHistory) % kBeatHistory;
        const double elapsed = beatTime[newest] - beatTime[previous];
        const double reference = 60.0 / std::max (kMinBpm, bpm);
        steps = std::clamp (static_cast<int> (std::llround (elapsed / reference)),
                            1, 4);
    }

    TempoMotionObservation o;
    o.beatTimeSec = beatTime[newest];
    o.beatStrength = beatStrength[newest];
    o.gridQuarterSteps = steps;
    o.committedBpm = bpm;
    o.shortFitBpm = shortFitBpm;
    o.longFitBpm = longFitBpm;
    o.shortFitResidual = shortFitResidual;
    o.fitCoverage = lastFitCoverage;
    o.fitIndexGap = lastFitIndexGap;
    o.intervalJitter = recentIntervalJitter();
    o.transitionState = transitionState;
    o.transitionRefitBeats = transitionRefitBeats;
    o.lineFeed = lineFeed;
    o.fixedRegime = tempoRegime == TempoRegime::fixed;
    motionShadow = motionTracker.observe (o);
}
```

- [ ] **Step 4: Mirror every evidence boundary**

Call `resetMotionShadow` at the same boundaries that clear fits or invalidate the grid:

```cpp
resetMotionShadow (true, TempoMotionVeto::inputEpoch);       // reset(), notifyInputRestart()
resetMotionShadow (false, TempoMotionVeto::discontinuity);   // notifyDiscontinuity()
resetMotionShadow (true, TempoMotionVeto::octaveOrGrid);     // setUserOctave()
resetMotionShadow (true, TempoMotionVeto::octaveOrGrid);     // checkGridPhase()
resetMotionShadow (true, TempoMotionVeto::octaveOrGrid);     // octave snap, stale-grid rebuild
resetMotionShadow (false, TempoMotionVeto::transition);      // confirmed/small-step rapid transition
```

`resetMotionShadow` must also set `motionObservedBeatSerial = beatSerial` so an old ring entry is not observed again after reset.

- [ ] **Step 5: Publish diagnostics with the existing stale-beat deadline**

Populate `BeatDecoder::diagnostics()` directly from `motionShadow`. In `observe()`, publish shadow fields only while `fastMotionCurrent`; otherwise publish zero authority, idle state, and `staleBeats` veto. This reuses the existing 1.5-period expiry and prevents stale authority during drummer silence.

- [ ] **Step 6: Connect the matrix counters but not the bridge**

In `probe_motion_matrix.cpp`, read the stale-gated hypothesis field rather than
the raw worker diagnostic:

```cpp
const float motionAuthority = h.motionShadowAuthority;
if (motionAuthority > 0.0f)
    ++score.authorityFrames;
```

Compile a candidate binary with `TempoMotionTracker.cpp`, run all four quick offsets, and compare:

```bash
clang++ -std=c++17 -O2 -Wno-unused-function -I Source \
  scripts/probe_motion_matrix.cpp Source/AI/BeatDecoder.cpp \
  Source/AI/TempoMotionTracker.cpp Source/AI/TempoEstimator.cpp \
  Source/AI/BeatHmm.cpp Source/Tracking/TempoFollower.cpp \
  -o /tmp/probe_motion_matrix-candidate
: > /tmp/vp-motion-candidate.csv
for offset in 0 16 32 48; do
  /tmp/probe_motion_matrix-candidate --quick --offset "$offset" --csv \
    >> /tmp/vp-motion-candidate.csv
done
python3 scripts/analysis/compare_motion_matrix.py \
  /tmp/vp-motion-control.csv /tmp/vp-motion-candidate.csv
```

Expected: comparator remains red only on continuous improvement. Fixed and step hashes are identical, and both families have zero authority frames. If either identity gate fails, fix observation/veto logic before Task 5.

- [ ] **Step 7: Run reset and octave gates**

Run:

```bash
cmake --build build-host --target VPTests
./build-host/VPTests_artefacts/Release/VPTests --tempo-motion
./build-host/VPTests_artefacts/Release/VPTests --new-input
./build-host/VPTests_artefacts/Release/VPTests --tempo-step
./build-host/VPTests_artefacts/Release/VPTests --tempo-slow
./build-host/VPTests_artefacts/Release/VPTests --octave focused
```

Expected: all pass.

- [ ] **Step 8: Commit diagnostics-only integration**

Run:

```bash
git add Source/AI/BeatDecoder.h Source/AI/BeatDecoder.cpp \
  Source/AI/BeatHypothesis.h scripts/probe_motion_matrix.cpp \
  Tests/TestTempoMotion.cpp
git diff --cached --check
git commit -m "feat: observe bounded tempo motion in decoder"
```

---

### Task 5: Let proven shadow motion bridge the fixed-tempo delay

**Files:**
- Modify: `Source/AI/BeatDecoder.cpp:3111-3283`
- Modify: `Tests/TestTempoMotion.cpp`
- Modify: `scripts/probe_motion_matrix.cpp`

**Interfaces:**
- Consumes: `motionShadow.predictedBpm` and `motionShadow.authority`.
- Produces: a fixed-regime bridge target passed through `BeatDecoder::commit`, with no anchor/history/grid mutation.

- [ ] **Step 1: Confirm the global acceptance test is red**

Run the comparator from Task 4.

Expected: fixed and step identity pass; every continuous row fails because the diagnostics-only candidate is behaviourally identical to control.

- [ ] **Step 2: Add bridge safety assertions**

Extend the pure/integration test to assert:

```cpp
expect (maximumCommittedChangePerBeat <= 0.0075f * priorCommittedBpm + 1.0e-5f,
        "the fixed bridge respects the 0.75 percent per-beat rail");
expect (maximumRequestedDistance <= 0.04f * priorCommittedBpm + 1.0e-5f,
        "the bridge request stays within four percent");
expect (transitionSerialAfter == transitionSerialBefore,
        "continuous motion does not manufacture an abrupt transition");
expect (gridSerialAfter == gridSerialBefore,
        "continuous motion does not rebuild the grid");
```

Run `VPTests --tempo-motion`; expected: failure because the bridge does not yet move BPM.

- [ ] **Step 3: Insert the bridge after the fixed anchor is computed**

In the fixed branch, preserve the current code unchanged when authority is zero. When it is positive:

```cpp
const float authority = std::clamp (motionShadow.authority, 0.0f, 1.0f);
if (authority > 0.0f
    && std::isfinite (motionShadow.predictedBpm)
    && motionShadow.predictedBpm >= kMinBpm
    && motionShadow.predictedBpm <= kMaxBpm
    && transitionState == TempoTransitionState::stable
    && transitionRefitBeats == 0)
{
    const float boundedPrediction =
        std::clamp (motionShadow.predictedBpm, bpm * 0.96f, bpm * 1.04f);
    const float blended = anchored + (boundedPrediction - anchored) * authority;
    const float boundedStep =
        std::clamp (blended, bpm * (1.0f - 0.0075f), bpm * (1.0f + 0.0075f));
    commit (boundedStep, 1.0f);
}
else
{
    const float step = (anchored - bpm) / std::max (kMinBpm, bpm);
    if (std::fabs (anchored - bpm) > kFixedDeadband
        && std::fabs (step) <= kFixedMaxStep)
        bpm = std::clamp (anchored, kMinBpm, kMaxBpm);
}
```

Do not write `fixedAnchorBpm`, `gridAnchorSec`, beat rings, `transitionState`, or serials in the bridge.

- [ ] **Step 4: Run the quick global A/B**

Rebuild `/tmp/probe_motion_matrix-candidate`, regenerate offsets 0/16/32/48, and run the comparator.

Expected:

- fixed and step hashes remain identical;
- fixed and step authority frames remain zero;
- continuous mean and p95 improve in every offset;
- no continuous recovery excursion stays beyond 50 ms for more than two truth beats after proof.

If the initial estimator constants miss this gate, do not weaken fixed/step identity or the two-beat recovery rule. Change one estimator constant at a time, rerun all four offsets, and retain a change only when every continuous row improves.

- [ ] **Step 5: Run the full 64-case family and independent offsets**

Run:

```bash
/tmp/probe_motion_matrix-control --csv > /tmp/vp-motion-control-full.csv
/tmp/probe_motion_matrix-candidate --csv > /tmp/vp-motion-candidate-full.csv
python3 scripts/analysis/compare_motion_matrix.py \
  /tmp/vp-motion-control-full.csv /tmp/vp-motion-candidate-full.csv
```

Expected: PASS. Repeat the quick offset comparison after the full bank so both the broad bank and independent banks remain recorded.

- [ ] **Step 6: Run `VPAlign` ramp and complete tempo gates**

Run:

```bash
cmake --build build-host --target VPAlign VPTests
./build-host/VPAlign_artefacts/Release/VPAlign --ramps
./build-host/VPAlign_artefacts/Release/VPAlign
./build-host/VPTests_artefacts/Release/VPTests --tempo-motion
./build-host/VPTests_artefacts/Release/VPTests --tempo-step
./build-host/VPTests_artefacts/Release/VPTests --tempo-slow
./build-host/VPTests_artefacts/Release/VPTests --evidence
./build-host/VPTests_artefacts/Release/VPTests --new-input
./build-host/VPTests_artefacts/Release/VPTests --bar
```

Expected: all commands exit 0, including all four formerly red ramp rows.

- [ ] **Step 7: Run standalone clock and transition probes**

Run:

```bash
clang++ -std=c++17 -O2 -I Source scripts/probe_tempo_step.cpp \
  Source/AI/BeatDecoder.cpp Source/AI/TempoMotionTracker.cpp \
  Source/AI/TempoEstimator.cpp Source/AI/BeatHmm.cpp \
  -o /tmp/probe_tempo_step
/tmp/probe_tempo_step

c++ -std=c++17 -O2 -ISource scripts/probe_recovery.cpp \
  Source/Tracking/TempoFollower.cpp -o /tmp/vp-recovery
/tmp/vp-recovery
```

Expected: `probe_tempo_step` exits 0 and the default recovery bank reports 84/84.

- [ ] **Step 8: Commit the production bridge**

Run:

```bash
git add Source/AI/BeatDecoder.cpp Tests/TestTempoMotion.cpp \
  scripts/probe_motion_matrix.cpp
git diff --cached --check
git commit -m "feat: bridge proven continuous tempo motion"
```

---

### Task 6: Publish bounded diagnostics and record automated evidence

**Files:**
- Modify: `Source/Tracking/BeatTracker.h:245-303`
- Modify: `Source/Tracking/BeatTracker.cpp:1788-1811`
- Modify: `Source/Core/Types.h:313-333`
- Modify: `Source/Audio/VirtualPercussionEngine.h:286-340`
- Modify: `Source/Audio/VirtualPercussionEngine.cpp:1880-1905,2000-2020`
- Modify: `scripts/probe_track.cpp:102-175`
- Modify: `.claude/skills/realtime-tempo/SKILL.md`
- Modify: `docs/HANDOFF_TEMPO.md`
- Modify: `docs/TODO.md`

**Interfaces:**
- Consumes: diagnostic-only shadow fields from `BeatHypothesis`.
- Produces: matching `BeatTracker::Output` and `EngineSnapshot` scalars through relaxed atomics.
- Produces: real-audio trace columns for shadow BPM, velocity, uncertainty, authority, state, and veto.

- [ ] **Step 1: Add snapshot propagation**

Add the six shadow fields to `BeatTracker::Output` and `EngineSnapshot`, then copy them from a valid hypothesis:

```cpp
out.motionShadowBpm = haveHyp ? hyp.motionShadowBpm : 0.0f;
out.motionShadowPeriodDelta = haveHyp ? hyp.motionShadowPeriodDelta : 0.0f;
out.motionShadowUncertainty = haveHyp ? hyp.motionShadowUncertainty : 1.0f;
out.motionShadowAuthority = haveHyp ? hyp.motionShadowAuthority : 0.0f;
out.motionShadowState = haveHyp ? hyp.motionShadowState : 0;
out.motionShadowVeto = haveHyp ? hyp.motionShadowVeto : 0;
```

Add one `std::atomic<float>` or `std::atomic<int>` per field beside the existing fit diagnostics in `VirtualPercussionEngine`; store from `BeatTracker::Output` in `processBlock` and load into `EngineSnapshot::snapshot()`. No UI object reads worker state directly.

- [ ] **Step 2: Extend the real-audio trace**

Append six labelled columns to `VPTrack --trace` and print the latest valid hypothesis fields. Keep the pulse file's existing six columns unchanged so analysis scripts remain compatible.

- [ ] **Step 3: Run the full deterministic suite and lints**

Run:

```bash
./scripts/run-tests.sh
cmake --build build-host --target VPAlign
./build-host/VPAlign_artefacts/Release/VPAlign
git diff --check
```

Expected: full TAP suite and `VPAlign` pass with no new compiler or linter diagnostics.

- [ ] **Step 4: Update the single source of truth with exact measurements**

In the realtime skill, HANDOFF, and TODO:

- replace “next design” with the implemented tracker and its ownership;
- record the exact control/candidate global rows for the full bank and each offset;
- record all six `VPAlign --ramps` rows;
- record filtered/full test totals and standalone probe totals;
- state explicitly that fixed and step trace hashes are identical;
- leave the real-audio and listening items open until Task 7;
- keep the microphone path deferred.

Copy command output exactly; do not report rounded or inferred wins.

- [ ] **Step 5: Commit diagnostics and automated evidence**

Run:

```bash
git add Source/Tracking/BeatTracker.h Source/Tracking/BeatTracker.cpp \
  Source/Core/Types.h Source/Audio/VirtualPercussionEngine.h \
  Source/Audio/VirtualPercussionEngine.cpp scripts/probe_track.cpp \
  .claude/skills/realtime-tempo/SKILL.md docs/HANDOFF_TEMPO.md docs/TODO.md
git diff --cached --check
git commit -m "docs: record bounded tempo motion evidence"
```

---

### Task 7: Validate phase and listening on real accelerando and rallentando

**Files:**
- Create: `scripts/analysis/score_beat_grid.py`
- Create: `scripts/analysis/select_motion_windows.py`
- Modify: `scripts/probe_track.cpp`
- Create after tapping: `docs/tempo-grids/flamingo-accelerando.txt`
- Create after tapping: `docs/tempo-grids/flamingo-rallentando.txt`
- Modify: `.claude/skills/realtime-tempo/SKILL.md`
- Modify: `docs/HANDOFF_TEMPO.md`
- Modify: `docs/TODO.md`

**Interfaces:**
- Consumes: refined quarter-beat timestamps and `VPTrack --pulses`.
- Produces: mean/p95/p99.5 phase error and count/duration of excursions beyond 50 ms.
- Produces: `VPTrack --review-wav PATH`, stereo with source in the left channel and rendered percussion in the right channel.

- [ ] **Step 1: Verify or restore the source mixer recording**

Run:

```bash
test -f "/Users/nicolamarogna/Desktop/Flamingo Marco 09.07.26.m4a" \
  && echo "mixer recording present"
```

Expected: `mixer recording present`. If absent, stop this task and ask the user to restore or identify the recording; tempogram summaries cannot substitute for phase truth.

- [ ] **Step 2: Add an exact beat-grid scorer with a synthetic self-test**

Create `score_beat_grid.py`. For every pulse timestamp between adjacent refined beats:

```python
true_phase = (pulse_time - beat_times[index]) / (
    beat_times[index + 1] - beat_times[index])
phase_cycles = ((clock_phase - true_phase + 0.5) % 1.0) - 0.5
phase_ms = abs(phase_cycles) * (
    beat_times[index + 1] - beat_times[index]) * 1000.0
```

Ignore samples before the first refined beat, after the last, and rows with `suona == 0`. Print mean, p95, p99.5, longest contiguous excursion above 50 ms in seconds and truth beats, and exit 1 when p95 exceeds 50 ms or an excursion exceeds two truth beats.

Add `--self-test`: a 100 BPM synthetic grid with clock phases offset by 12 ms must report 12 ms mean/p95 and exit 0.

Run:

```bash
python3 scripts/analysis/score_beat_grid.py --self-test
```

Expected: PASS with 12.0 ms.

- [ ] **Step 3: Add a stereo listening render to `VPTrack`**

Parse `--review-wav PATH`. Create a JUCE WAV writer at the input sample rate, 2 channels, 24 bits. For every processed block write:

```cpp
juce::AudioBuffer<float> review (2, take);
review.copyFrom (0, 0, mono.data() + pos, take);
for (int i = 0; i < take; ++i)
    review.setSample (1, i, 0.5f * (oL[static_cast<size_t> (i)]
                                   + oR[static_cast<size_t> (i)]));
writer->writeFromAudioSampleBuffer (review, 0, take);
```

Left is the original mixer feed; right is the rendered percussion. This makes rushing, dragging, and recovery directly reviewable without changing engine behaviour.

- [ ] **Step 4: Extract and identify one rising and one falling passage**

Run:

```bash
mkdir -p /tmp/vp-live-motion
swift scripts/analysis/extract_live.swift \
  "/Users/nicolamarogna/Desktop/Flamingo Marco 09.07.26.m4a" \
  /tmp/vp-live-motion
for wav in /tmp/vp-live-motion/s*.wav; do
  python3 scripts/analysis/tempocurve.py "$wav" 50 190 \
    > "${wav%.wav}-truth.txt"
done
```

Create `scripts/analysis/select_motion_windows.py`:

```python
#!/usr/bin/env python3
import glob
import math
import os
import shlex
import sys


def points(path):
    result = []
    with open(path) as stream:
        for line in stream:
            fields = line.split()
            if len(fields) != 3:
                continue
            try:
                t, bpm, clarity = map(float, fields)
            except ValueError:
                continue
            if clarity > 0.15 and bpm > 0.0:
                result.append((t, bpm))
    return result


def candidates(directory):
    result = []
    for truth in sorted(glob.glob(os.path.join(directory, "s*-truth.txt"))):
        source = truth.removesuffix("-truth.txt") + ".wav"
        rows = points(truth)
        for i, (t0, bpm0) in enumerate(rows):
            for t1, bpm1 in rows[i + 1:]:
                elapsed = t1 - t0
                if elapsed < 20.0:
                    continue
                if elapsed > 50.0:
                    break
                folded = bpm1 * 2.0 ** round(math.log2(bpm0 / bpm1))
                change = (folded - bpm0) / bpm0
                if abs(change) >= 0.015:
                    result.append((change / elapsed, source,
                                   max(0.0, t0 - 5.0), change))
    return result


def main():
    if len(sys.argv) != 2:
        print("usage: select_motion_windows.py EXTRACT_DIR", file=sys.stderr)
        return 2
    rows = candidates(sys.argv[1])
    rising = [row for row in rows if row[0] > 0.0]
    falling = [row for row in rows if row[0] < 0.0]
    if not rising or not falling:
        print("serve almeno una finestra crescente e una calante affidabile",
              file=sys.stderr)
        return 1
    accel = max(rising)
    rall = min(falling)
    print(f"ACCEL_SOURCE={shlex.quote(accel[1])}")
    print(f"ACCEL_START={accel[2]:.1f}")
    print(f"ACCEL_CHANGE={accel[3]:+.6f}")
    print(f"RALL_SOURCE={shlex.quote(rall[1])}")
    print(f"RALL_START={rall[2]:.1f}")
    print(f"RALL_CHANGE={rall[3]:+.6f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

It considers reliable pairs 20–50 seconds apart, folds the second BPM to the
nearest octave, requires at least 1.5% total change, and fails unless both
directions exist. Run it and create exact 45-second working extracts:

```bash
python3 scripts/analysis/select_motion_windows.py /tmp/vp-live-motion \
  > /tmp/vp-live-motion/selection.env
source /tmp/vp-live-motion/selection.env
mkdir -p /tmp/vp-live-motion/accel /tmp/vp-live-motion/rall
swift scripts/analysis/extract_live.swift \
  "$ACCEL_SOURCE" /tmp/vp-live-motion/accel "$ACCEL_START" 45
swift scripts/analysis/extract_live.swift \
  "$RALL_SOURCE" /tmp/vp-live-motion/rall "$RALL_START" 45
```

The listener confirms that both generated `s1.wav` files contain audible
quarter beats. The tempogram selects direction only; it does not provide phase
truth.

- [ ] **Step 5: Tap and refine both truth grids**

Tap the two exact working extracts:

```bash
python3 scripts/analysis/tap_recorder.py \
  /tmp/vp-live-motion/accel/s1.wav /tmp/accel-taps.txt
python3 scripts/analysis/refine_taps.py \
  /tmp/vp-live-motion/accel/s1.wav /tmp/accel-taps.txt \
  docs/tempo-grids/flamingo-accelerando.txt
python3 scripts/analysis/tap_recorder.py \
  /tmp/vp-live-motion/rall/s1.wav /tmp/rall-taps.txt
python3 scripts/analysis/refine_taps.py \
  /tmp/vp-live-motion/rall/s1.wav /tmp/rall-taps.txt \
  docs/tempo-grids/flamingo-rallentando.txt
```

Tap every quarter for 20–30 bars and mark `1` on downbeats. Add comments to
each grid naming the selected source extract and exact interval printed in
`selection.env`.

- [ ] **Step 6: Run the complete engine and score both passages**

Run the complete engine on both passages:

```bash
cmake --build build-host --target VPTrack
./build-host/VPTrack_artefacts/Release/VPTrack \
  --wav /tmp/vp-live-motion/accel/s1.wav \
  --pulses /tmp/accel.pulses \
  --review-wav /tmp/accel-review.wav
python3 scripts/analysis/score_beat_grid.py \
  docs/tempo-grids/flamingo-accelerando.txt /tmp/accel.pulses
./build-host/VPTrack_artefacts/Release/VPTrack \
  --wav /tmp/vp-live-motion/rall/s1.wav \
  --pulses /tmp/rall.pulses \
  --review-wav /tmp/rall-review.wav
python3 scripts/analysis/score_beat_grid.py \
  docs/tempo-grids/flamingo-rallentando.txt /tmp/rall.pulses
```

Expected for both accelerando and rallentando: p95 ≤50 ms and no excursion above 50 ms lasting more than two truth beats after acquisition.

- [ ] **Step 7: Perform the human listening gate**

Listen to each `/tmp/*-review.wav` on headphones with left/right centred, then solo left and right. Pass only if the percussion neither audibly rushes nor drags through the motion and, after a displaced beat, returns without a speed lurch, duplicate stroke, skipped stroke, or phase snap.

The listener records `PASS` or the exact failing time range. A failing range returns to Task 5 and must be reproduced in the synthetic population before any code is retuned.

- [ ] **Step 8: Commit real-audio evidence**

After both measurement and listening pass:

```bash
git add scripts/analysis/score_beat_grid.py \
  scripts/analysis/select_motion_windows.py scripts/probe_track.cpp \
  docs/tempo-grids/flamingo-accelerando.txt \
  docs/tempo-grids/flamingo-rallentando.txt \
  .claude/skills/realtime-tempo/SKILL.md docs/HANDOFF_TEMPO.md docs/TODO.md
git diff --cached --check
git commit -m "test: validate live tempo motion against beat grids"
```

Record exact p95, worst excursion, review filenames, and the listener verdict in HANDOFF and the realtime skill.

---

### Task 8: Audit direct-path completion before starting microphone work

**Files:**
- Read: `docs/superpowers/specs/2026-09-16-tempo-motion-shadow-tracker-design.md`
- Read: `docs/HANDOFF_TEMPO.md`
- Read: `.claude/skills/realtime-tempo/SKILL.md`
- Read: current Git diff and test outputs

**Interfaces:**
- Consumes: every automated and real-audio artifact from Tasks 1–7.
- Produces: requirement-by-requirement completion decision for file/mixer.
- Produces: a separate microphone-specific design cycle only after the direct path is proven.

- [ ] **Step 1: Re-run all authoritative direct-path commands**

Run the full matrix A/B, `VPAlign`, full `VPTests`, filtered tempo gates, `probe_tempo_step`, `probe_recovery`, and both real beat-grid scores from fresh binaries.

Expected: every command exits 0; fixed and step hashes match the control; every continuous bank improves mean and p95; both real passages pass.

- [ ] **Step 2: Inspect clock and realtime invariants**

Confirm by code inspection and probe output:

- tracker state is owned by `BeatDecoder` on the worker;
- `TempoFollower` has no new tempo owner;
- no new allocation, lock, I/O, or unbounded loop exists on audio callback paths;
- bridge code does not touch grid/history/transition serials;
- pulse violation counts remain zero.

- [ ] **Step 3: Decide the direct-path phase**

Mark file/mixer complete only when every item above has direct evidence and the human listening verdict is PASS. Do not mark the persistent goal complete at this point: start a new brainstorming/specification cycle for microphone/room evidence, using the direct implementation as a candidate rather than enabling its line-feed thresholds on iPad.
