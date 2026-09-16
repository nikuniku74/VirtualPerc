# Hybrid Tempo Motion Proof Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove continuous tempo motion at the `fixed -> live` boundary without changing any fixed-tempo or abrupt-step trace, then apply only bounded same-beat/full-tenure corrections that pass every synthetic, probe, and real-audio gate.

**Architecture:** Keep the existing scalar `TempoMotionTracker` as the source of the first strict-proof edge, and add a separate pure `TempoMotionShape` classifier over fixed-entry phase residuals. The decoder may use one strict-proof/release coincidence for a single 0.35-authority pre-release commit, while authority during a longer fixed tenure requires two consecutive quadratic shape wins; affine, outlier, hinge, veto, and quarantine results cannot own tempo.

**Tech Stack:** C++20 production/tests, fixed-size `std::array` storage, CMake/Ninja, JUCE 8.0.15, deterministic `VPTests --tempo-motion`, `VPAlign`, standalone C++ probes, and Python 3 matrix comparison/scoring.

## Global Constraints

- The behavioral control is commit
  `b9db9139dd6edc18c6c6cf5beda4a470c52222e6` on branch `integrations`.
  Implementation starts from the later commit containing this plan; only
  documentation may differ from that behavioral control before Task 1.
- Existing Tasks 1–4 are the baseline. The Task 5 scalar-tuning experiment was rejected and contributed no production commit.
- Keep `BeatDecoder::fitPeriodCurve` and its existing fields diagnostic-only; neither hybrid proof path may make that older curvature fit a tempo owner.
- Preserve the pre-existing dirty `third_party/JUCE` submodule and untracked `pop-dance-congas-procedural-124.wav`; never stage either.
- The percussion clock never restarts because BPM changes. Do not snap phase, move a beat backwards, duplicate/skip a pulse, or rebuild the clock.
- All new estimator state is owned by `BeatDecoder` on the worker. No allocation, lock, I/O, ONNX work, or unbounded loop may enter an audio callback.
- TAP/manual tempo, confirmed/suspected/rapid transitions, octave changes, grid rebuilds, input epoch, FIFO discontinuity, non-direct input, fixed-regime exit, invalid observations, and stale beats retain priority and immediately remove authority.
- A single measurement never chooses BPM or phase. The strict edge can act only when the same accepted beat already satisfies the existing `fixed -> live` release predicate.
- The early coincidence correction is one shot, at authority `min(0.35, requested)`. If it changes one abrupt-step trace at any tested seed, remove that path entirely; do not weaken or seed-tune it.
- Full fixed-tenure authority starts at `0.35`, grows by at most `0.35` once per accepted beat, requires two consecutive decisive quadratic wins, and is zero during a 12-accepted-beat hinge/abrupt-edge quarantine.
- Both bridge levels clamp prediction to `bpm * [0.96, 1.04]`, clamp one accepted-beat commit to `bpm * [0.9925, 1.0075]`, and call only `BeatDecoder::commit(candidate, 1.0f)`.
- Bridge code must not write `fixedAnchorBpm`, `gridAnchorSec`, `beatTime`, `beatStrength`, `beatWrite`, `beatFilled`, `longHist`, `longWrite`, `longFilled`, `transitionState`, `transitionReason`, `transitionSerial`, `gridSerial`, `downbeatSerial`, bar state, octave state, input epoch, or any clock object.
- Authority zero must execute the current fixed-anchor body byte-for-byte: compute `step`, check `kFixedDeadband`/`kFixedMaxStep`, then assign the clamped `anchored` value exactly as at `b9db913`.
- Every predicted period/BPM and diagnostic score must be finite. An invalid fit publishes zero authority and a finite in-range committed BPM (or zero only where the existing reset contract requires it).
- The acceptance contract is not tunable: fixed and step trace hashes remain identical to control; fixed/step applied authority is zero; every continuous row has positive applied authority; continuous mean and p95 strictly improve in every row; recovery violations are zero.
- Initial constants are global hypotheses, never song/seed/offset values. Change one classifier constant at a time, rerun the complete focused and four-offset quick gates, and retain it only after full and independent A/B also pass.
- Do not add a test CLI filter. Register focused tests under the existing `VPTests --tempo-motion` dispatch; do not edit CRLF `Tests/TestMain.cpp`.
- Do not run the full `VPTests` suite until Task 5, where snapshot diagnostics and documentation are completed. Tasks 1–4 use focused filters only.

## Initial Globally Testable Hypotheses

- Shape window: `kMinimumPoints=7`, `kMaximumPoints=12`; reevaluate every accepted beat and keep a sliding 12-point window. Reaching 12 never forces a verdict.
- Legal hinge split: `kMinimumSidePoints=2`, so split indices are `2..n-2`.
- Numerical pivot floor: `kPivotFloor=1.0e-12`.
- Robust absolute timing floor: `kAbsoluteNoiseFloorSec=0.0001`.
- BIC evidence margin: `kEvidenceMarginBic=2.0`.
- BIC parameter counts: affine `2`, quadratic `3`, continuous hinge `4` (including selected knot), affine-with-one-excluded-point `3` (including selected exclusion).
- Existing strict proof, unchanged: minimum scalar samples `5`, coverage `0.75`, maximum short residual `0.035`, maximum index-gap error `0.15`, minimum rate z-score `4.0`, minimum window displacement `0.006`.
- Shape proof: `kQuadraticProofBeats=2`; no timeout verdict.
- Hinge/abrupt-edge quarantine: `kHingeQuarantineBeats=12` accepted beats after the trigger.
- Authority: `kInitialAuthority=0.35`, `kAuthorityPerBeat=0.35`, range `[0,1]`.
- Prediction/commit: BPM range `[50,190]`, prediction rail `0.04`, accepted-beat rail `0.0075`.
- Stale publication: preserve the current `1.5`-period deadline.
- Recovery acceptance: phase must return to `<=50 ms` no later than two truth beats after proof.

## File Structure

- Create `Source/AI/TempoMotionShape.h`: fixed-size points, model enum, result, and pure classifier signature.
- Create `Source/AI/TempoMotionShape.cpp`: bounded least-squares fits, robust floor, BIC scoring, and deterministic selection only.
- Create `Tests/TestTempoMotionShape.h` and `Tests/TestTempoMotionShape.cpp`: exhaustive synthetic shape cases; called by the existing tempo-motion test entry.
- Modify `Source/AI/TempoMotionTracker.h/.cpp`: fixed-tenure residual window, strict-proof edge, shape confirmation/quarantine, finite prediction, and authority.
- Modify `Source/AI/BeatDecoder.h/.cpp` and `Source/AI/BeatHypothesis.h`: same-beat release diagnostic, one bounded bridge helper, and worker diagnostics.
- Modify `Tests/TestTempoMotion.cpp`: tracker/decoder tenure, coincidence, rails, reset, and forbidden-state tests.
- Modify `CMakeLists.txt`: compile the new production and test `.cpp` files.
- Modify `scripts/probe_motion_matrix.cpp` and `scripts/analysis/compare_motion_matrix.py`: applied-authority anti-vacuity and post-proof recovery acceptance.
- Modify snapshot/trace/docs files only in Tasks 5–6, after the production gates pass.

---

### Task 1: Pure Fixed-Size Residual Shape Classifier

**Files:**
- Create: `Source/AI/TempoMotionShape.h`
- Create: `Source/AI/TempoMotionShape.cpp`
- Create: `Tests/TestTempoMotionShape.h`
- Create: `Tests/TestTempoMotionShape.cpp`
- Modify: `Tests/TestTempoMotion.cpp:1-8,101-108`
- Modify: `CMakeLists.txt:21-47,302-309`

**Interfaces:**
- Consumes: `std::array<TempoMotionShapePoint, 12>` in chronological order, with strictly increasing cumulative quarter indices and finite residual seconds.
- Produces: `TempoMotionShapeResult TempoMotionShape::classify(const std::array<TempoMotionShapePoint, kMaximumPoints>&, int count) noexcept`.
- Produces: `void vpRunTempoMotionShapeTests(int& passed, int& failed)`.
- Does not consume `BeatDecoder`, transition state, committed BPM, or bridge state.

- [ ] **Step 1: Freeze the control executable and four quick CSVs**

Run before changing any source:

```bash
cd /Users/nicolamarogna/Desktop/NK/VirtualPerc
test -z "$(git diff --name-only \
  b9db9139dd6edc18c6c6cf5beda4a470c52222e6..HEAD -- \
  CMakeLists.txt Source Tests scripts)"
clang++ -std=c++17 -O2 -Wno-unused-function -I Source \
  scripts/probe_motion_matrix.cpp Source/AI/BeatDecoder.cpp \
  Source/AI/TempoMotionTracker.cpp Source/AI/TempoEstimator.cpp \
  Source/AI/BeatHmm.cpp Source/Tracking/TempoFollower.cpp \
  -o /tmp/probe_motion_matrix-hybrid-control
: > /tmp/vp-hybrid-control-quick.csv
for offset in 0 16 32 48; do
  /tmp/probe_motion_matrix-hybrid-control --quick --offset "$offset" --csv \
    >> /tmp/vp-hybrid-control-quick.csv
done
```

Expected: exit 0. Confirm the report’s fixed hashes
`c37d576393d993e3/8655dfbfc3d7ed59/fe2cc82887b3d515/014ffb81e89081c2`
and step hashes
`7ebf23a7c6f05c83/12323b42531ac16e/a11125395c407d13/226e2cd87cb84250`
for offsets `0/16/32/48`.

- [ ] **Step 2: Declare the classifier and write the RED tests**

Create `Source/AI/TempoMotionShape.h`:

```cpp
#pragma once

#include <array>

namespace vp
{
enum class TempoMotionShapeModel : int
{
    invalid,
    insufficient,
    affine,
    quadratic,
    hinge,
    affineWithOutlier
};

struct TempoMotionShapePoint
{
    double quarterIndex = 0.0;
    double residualSec = 0.0;
};

struct TempoMotionShapeResult
{
    TempoMotionShapeModel model = TempoMotionShapeModel::insufficient;
    double affineBic = 0.0;
    double quadraticBic = 0.0;
    double hingeBic = 0.0;
    double outlierBic = 0.0;
    double evidenceMargin = 0.0;
    double quadraticVsHinge = 0.0;
    double nextPeriodDeltaSec = 0.0;
    double robustNoiseSec = 0.0001;
    int hingeSplit = -1;
    int excludedPoint = -1;
    bool finite = true;
};

class TempoMotionShape
{
public:
    static constexpr int kMinimumPoints = 7;
    static constexpr int kMaximumPoints = 12;

    static TempoMotionShapeResult classify (
        const std::array<TempoMotionShapePoint, kMaximumPoints>& points,
        int count) noexcept;
};
} // namespace vp
```

Create `Tests/TestTempoMotionShape.h`:

```cpp
#pragma once
void vpRunTempoMotionShapeTests (int& passed, int& failed);
```

In `Tests/TestTempoMotion.cpp`, include that header and make the first statement of
`vpRunTempoMotionTrackerTests`:

```cpp
vpRunTempoMotionShapeTests (passed, failed);
```

In `Tests/TestTempoMotionShape.cpp`, define one deterministic builder using
`y = phase + slope*x + curvature*x*x + hingeDelta*max(0, x-knot)`, then assert:

```cpp
using Points = std::array<vp::TempoMotionShapePoint,
                          vp::TempoMotionShape::kMaximumPoints>;

Points makeShape (int n, double phase, double slope, double curvature,
                  int split, double hingeDelta, int outlier,
                  double outlierDelta, bool missFirstAfterSplit)
{
    Points points {};
    double x = 0.0;
    const double knot = split >= 0
                            ? static_cast<double> (split)
                                  - (missFirstAfterSplit ? 0.0 : 0.5)
                            : 0.0;
    for (int i = 0; i < n; ++i)
    {
        if (i > 0)
            x += missFirstAfterSplit && i == split ? 2.0 : 1.0;
        const double noise = (i & 1) != 0 ? 0.00008 : -0.00008;
        const double hinge = split >= 0 ? hingeDelta * std::max (0.0, x - knot)
                                        : 0.0;
        points[static_cast<size_t> (i)] = {
            x,
            phase + slope * x + curvature * x * x + hinge + noise
                + (i == outlier ? outlierDelta : 0.0)
        };
    }
    return points;
}

auto makeAffine = [] (int n, double phase, double slope)
{
    return makeShape (n, phase, slope, 0.0, -1, 0.0, -1, 0.0, false);
};
auto makeQuadratic = [] (int n, double curvature)
{
    return makeShape (n, 0.0, 0.0, curvature, -1, 0.0, -1, 0.0, false);
};
auto makeQuadraticWithMissingQuarter = [] (int n, double curvature)
{
    Points points {};
    double x = 0.0;
    for (int i = 0; i < n; ++i)
    {
        if (i > 0)
            x += i == 3 ? 2.0 : 1.0;
        const double noise = (i & 1) != 0 ? 0.00008 : -0.00008;
        points[static_cast<size_t> (i)] = { x, curvature * x * x + noise };
    }
    return points;
};
auto makeAffineWithOutlier = [] (int n, int excluded, double displacement)
{
    return makeShape (n, 0.011, -0.0002, 0.0, -1, 0.0,
                      excluded, displacement, false);
};
auto makeHinge = [] (int n, int split, double slopeDelta, bool missed)
{
    return makeShape (n, 0.0, 0.0001, 0.0, split, slopeDelta,
                      -1, 0.0, missed);
};
auto expectModel = [&] (const Points& points, int count,
                        vp::TempoMotionShapeModel expected, const char* name)
{
    const auto result = vp::TempoMotionShape::classify (points, count);
    expect (result.finite && result.model == expected, name);
};
auto expectHinge = [&] (const Points& points, int count,
                        int split, const char* name)
{
    const auto result = vp::TempoMotionShape::classify (points, count);
    expect (result.finite
                && result.model == vp::TempoMotionShapeModel::hinge
                && result.hingeSplit == split,
            name);
};

for (int n = 7; n <= 12; ++n)
{
    expectModel (makeAffine (n, 0.013, -0.0002), n,
                 vp::TempoMotionShapeModel::affine,
                 "fixed/phase-offset residual is affine");
    expectModel (makeQuadratic (n, -0.00032), n,
                 vp::TempoMotionShapeModel::quadratic,
                 "accelerando residual is quadratic");
    expectModel (makeQuadratic (n, 0.00032), n,
                 vp::TempoMotionShapeModel::quadratic,
                 "rallentando residual is quadratic");

    for (int excluded = 0; excluded < n; ++excluded)
        expectModel (makeAffineWithOutlier (n, excluded, 0.045), n,
                     vp::TempoMotionShapeModel::affineWithOutlier,
                     "one displaced beat is an outlier, not motion");

    for (int split = 2; split <= n - 2; ++split)
    {
        expectHinge (makeHinge (n, split, -0.0045, false), n, split,
                     "faster step residual is a hinge");
        expectHinge (makeHinge (n, split,  0.0045, false), n, split,
                     "slower step residual is a hinge");
        expectHinge (makeHinge (n, split, -0.0045, true), n, split,
                     "faster step with first new beat missing is a hinge");
        expectHinge (makeHinge (n, split,  0.0045, true), n, split,
                     "slower step with first new beat missing is a hinge");
    }

    expectModel (makeQuadraticWithMissingQuarter (n, -0.00032), n,
                 vp::TempoMotionShapeModel::quadratic,
                 "cumulative quarter index preserves accelerando across a missed beat");
    expectModel (makeQuadraticWithMissingQuarter (n, 0.00032), n,
                 vp::TempoMotionShapeModel::quadratic,
                 "cumulative quarter index preserves rallentando across a missed beat");
}
```

The `missed=true` hinge case must advance the first post-step point by two quarter
indices, so the first new beat is missing. Add invalid tests for `count=6`,
`count=13`, duplicate/decreasing `quarterIndex`, NaN/Inf coordinates, and tests
that every returned score, margin, noise floor, and `nextPeriodDeltaSec` is
finite. Use deterministic alternating timing noise of `±0.00008 s`; do not use
randomness.

Register `Tests/TestTempoMotionShape.cpp` in the `VPTests` source list, but do
not yet add `Source/AI/TempoMotionShape.cpp` to `VP_CORE_SOURCES`.

Run:

```bash
cmake -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --target VPTests
```

Expected RED: link failure for undefined
`vp::TempoMotionShape::classify`. There is no new CLI filter.

- [ ] **Step 3: Implement the bounded fits and exact score**

Create `Source/AI/TempoMotionShape.cpp` and add it immediately after
`Source/AI/TempoMotionTracker.cpp` in `VP_CORE_SOURCES`.

Implement these exact initial hypotheses:

```cpp
constexpr int kMinimumSidePoints = 2;
constexpr double kAbsoluteNoiseFloorSec = 0.0001;
constexpr double kEvidenceMarginBic = 2.0;
constexpr double kPivotFloor = 1.0e-12;
constexpr int kAffineParameters = 2;
constexpr int kQuadraticParameters = 3;
constexpr int kHingeParameters = 4;   // intercept, two slopes, selected knot
constexpr int kOutlierParameters = 3; // intercept, slope, selected exclusion
```

Normalize `u = (x - x0) / (xLast - x0)`. Fit with fixed-size normal equations
and partial-pivot Gaussian elimination:

```text
affine:   y = a + b*u
quadratic:y = a + b*u + c*u*u
hinge:    y = a + b*u + c*max(0, u-knot)
outlier:  y = a + b*u, fitted once for every excluded point
```

For hinge split `s = 2..count-2`, use
`knot = 0.5 * (u[s-1] + u[s])`; both sides therefore contain at least two
observations. Evaluate every legal split and keep the lowest score. For outlier,
exclude each `0..count-1`, fit the other `count-1`, and score the excluded datum
as one noise-floor residual rather than silently reducing `n`.

After fitting all four candidate families, compute a residual MAD for each
family’s best fit and use the smallest finite robust scale. This prevents the
curvature that an affine model cannot explain from being mislabelled as noise:

```text
scale(model) = 1.4826 * median(abs(e[i] - median(e)))
sigma = max(0.0001, minFinite(scale(affine), scale(quadratic),
                             scale(bestHinge), scale(bestOutlier)))
rssOutlier = sum(inlierResidual^2) + sigma^2
BIC(model) = n * log(max(rss(model) / n, sigma^2)) + k(model) * log(n)
```

Use only the `n-1` fitted residuals for `scale(bestOutlier)`; the omitted datum
is charged as `sigma^2` in RSS after the common floor has been selected.

The raw lowest BIC is accepted only when its advantage over the second-lowest
score is at least `2.0`; otherwise return `affine`. This conservative fallback
cannot create authority. Set `quadraticVsHinge = hingeBic - quadraticBic`,
`evidenceMargin = secondLowestBic - lowestBic`, and:

```text
quadratic nextPeriodDeltaSec =
    (b + 2*c*((xLast + 1 - x0)/(xLast-x0))) / (xLast-x0)
affine/hinge/outlier nextPeriodDeltaSec =
    derivative of the winning segment at xLast+1
```

Reject a zero/non-finite span, singular solve, non-finite coefficient, RSS, BIC,
or derivative with `model=invalid`, `finite=false`, and all numeric outputs
reset to finite defaults. All loops are bounded by 12 points, 9 hinge splits,
12 exclusions, and 3 pivots; no heap storage is permitted.

- [ ] **Step 4: Run GREEN and commit only the pure classifier**

Run:

```bash
cmake --build build-host --target VPTests
./build-host/VPTests_artefacts/Release/VPTests --tempo-motion
git diff --check
```

Expected GREEN: all pre-existing tracker tests and all shape tests pass.

Commit scope:

```bash
git add CMakeLists.txt Source/AI/TempoMotionShape.h \
  Source/AI/TempoMotionShape.cpp Tests/TestTempoMotionShape.h \
  Tests/TestTempoMotionShape.cpp Tests/TestTempoMotion.cpp
git diff --cached --check
git commit -m "feat: classify fixed-tenure tempo residual shapes"
```

---

### Task 2: Wire Strict-Proof and Shape Diagnostics Only

**Files:**
- Modify: `Source/AI/TempoMotionTracker.h`
- Modify: `Source/AI/TempoMotionTracker.cpp`
- Modify: `Source/AI/BeatDecoder.h:74-113,250-269,420-470`
- Modify: `Source/AI/BeatDecoder.cpp:858-908,2147-2187,2700-3180,3754-3783`
- Modify: `Source/AI/BeatHypothesis.h:68-103`
- Modify: `Tests/TestTempoMotion.cpp`

**Interfaces:**
- Produces: `void TempoMotionTracker::beginFixedTenure(double entryTimeSec, float committedBpm) noexcept`.
- Produces: `void TempoMotionTracker::quarantineShape() noexcept`.
- Extends `TempoMotionOutput` with strict edge and shape diagnostics.
- Produces diagnostic `BeatDecoder::motionReleaseCoincidence`; this task does not call a bridge or read authority to move BPM.

- [ ] **Step 1: Write decoder/tracker diagnostic tests RED**

Extend `Tests/TestTempoMotion.cpp` under the existing test function:

1. Call `beginFixedTenure(0.0, 100.0f)` and feed cumulative residuals through
`TempoMotionObservation`. Assert the first strict predicate produces exactly one
`firstStrictProof` pulse in the tenure.
2. Feed seven through twelve quadratic points and assert model/quadratic margin
diagnostics update while authority remains unused by `BeatDecoder`.
3. Use the report’s non-vacuous decoder ramp: fixed 100 BPM (`0.6 s` periods)
until `55 s`, then `period = max(0.54, 0.6 - 0.001 * motionBeat)`, omitting
`motionBeat == 3`. Assert a strict-proof pulse and existing fixed-release
predicate coincide on one accepted beat.
4. Use the same pre-roll followed by a `0.6 -> 0.565 s` step, with the first new
beat omitted. Assert the release coincidence count is zero and the shape is
hinge/quarantined, never quadratic-authoritative.
5. Snapshot `h.bpm`, `h.gridSerial`, and `h.transitionSerial`; diagnostics-only
wiring must not change any of them.

Expected RED: compile errors for the new methods/fields.

- [ ] **Step 2: Add exact tracker state and outputs**

Include `AI/TempoMotionShape.h` and extend `TempoMotionOutput`:

```cpp
bool firstStrictProof = false;
TempoMotionShapeModel shapeModel = TempoMotionShapeModel::insufficient;
float shapePredictedBpm = 0.0f;
float shapeQuadraticVsHinge = 0.0f;
float shapeEvidenceMargin = 0.0f;
int shapeQuadraticWins = 0;
int shapeQuarantineBeats = 0;
```

Add fixed storage/state:

```cpp
std::array<TempoMotionShapePoint, TempoMotionShape::kMaximumPoints> shapePoints {};
int shapeFilled = 0;
double shapeEntryTimeSec = -1.0;
double shapeQuarterIndex = 0.0;
float shapeEntryPeriodSec = 0.0f;
bool strictProofSeen = false;
bool shapeHingeActive = false;
int shapeQuadraticWins = 0;
int shapeQuarantineBeats = 0;
```

`beginFixedTenure` must call the existing full reset, validate `entryTimeSec` and
`committedBpm`, store `p0 = 60/committedBpm`, seed
`shapePoints[0] = {0.0, 0.0}`, and set `shapeFilled=1`. Keep
`lastBeatTimeSec=-1.0`: the next beat remains the scalar tracker’s anchor, so no
pre-tenure interval is carried. Shape accumulation happens before that scalar
anchor early return:

```cpp
shapeQuarterIndex += static_cast<double> (o.gridQuarterSteps);
const double residual = o.beatTimeSec - shapeEntryTimeSec
                      - shapeQuarterIndex * shapeEntryPeriodSec;
```

Append chronologically; when full, shift exactly 11 entries left before
appending. Classify at every accepted fixed beat for counts `7..12`.

The existing strict predicate remains exactly the current `qualityOk`:
five scalar samples, direct fixed input, coverage `>=0.75`, short residual
`<=0.035`, index-gap error `<=0.15`, rate z-score `>=4.0`, displacement
`>=0.006`, non-zero matching scalar/short-fit signs. Publish:

```cpp
lastOutput.firstStrictProof = qualityOk && ! strictProofSeen;
strictProofSeen = strictProofSeen || qualityOk;
```

Do not lower those constants and do not let this field alter `authority`.
Convert a finite shape derivative to:

```cpp
const float shapePeriod = std::clamp (
    shapeEntryPeriodSec + static_cast<float> (shape.nextPeriodDeltaSec),
    60.0f / 190.0f, 60.0f / 50.0f);
lastOutput.shapePredictedBpm = 60.0f / shapePeriod;
```

Invalid/non-finite shape output clears shape confirmation diagnostics and
publishes no authority.

For `reset(false, discontinuity)`, `reset(false, transition)`, and stale-beat
expiry, clear `shapeFilled`, `shapeEntryTimeSec`, `shapeQuarterIndex`,
confirmation, and authority while preserving only the committed BPM supplied by
the next observation. If the decoder remains fixed, the next valid observation
seeds `{quarterIndex=0,residual=0}` with its timestamp and current committed
period; no interval spanning the boundary is admitted.

- [ ] **Step 3: Preserve the exact fixed-entry boundary**

In `BeatDecoder::enterRegime`, replace only the fixed-entry reset call:

```cpp
if (r == TempoRegime::fixed)
{
    const int newest = (beatWrite - 1 + kBeatHistory) % kBeatHistory;
    if (beatFilled > 0)
    {
        motionTracker.beginFixedTenure (beatTime[newest], bpm);
        motionShadow = motionTracker.output();
        motionObservedBeatSerial = beatSerial;
    }
    else
    {
        resetMotionShadow (true, TempoMotionVeto::none);
    }
}
else if (previous == TempoRegime::fixed)
{
    resetMotionShadow (false, TempoMotionVeto::none);
}
```

At the point `transitionState` first becomes `suspected`, call
`motionTracker.quarantineShape()` and copy `motionShadow = motionTracker.output()`.
The method sets a 12-beat diagnostic quarantine and clears authority; it does
not change decoder transition state.

- [ ] **Step 4: Factor the release predicate without changing behavior**

At the start of each accepted-beat `updateTempo`, set
`motionReleaseCoincidence=false`. In the current `TempoRegime::fixed` decision,
name the existing predicate without changing a term:

```cpp
const bool releaseFixed =
    beatsInRegime >= kRegimeMinBeats
    && (moving
        || fixedErrorBeats >= kBeatsToLeaveFixed
        || (fastDriftBeats >= (lineFeed && shortFitResidual < kFastLineCleanResidual
                                    ? kFastBeatsToLeaveFixedLine
                                    : kFastBeatsToLeaveFixed)
            && windowAgrees)
        || (fastDriftLargeBeats >= kFastBeatsAlone
            && (lineFeed || windowAgrees))
        || (haveLong && std::fabs (anchorError) > 0.06f));

motionReleaseCoincidence = releaseFixed && motionShadow.firstStrictProof;
if (releaseFixed)
{
    enterRegime (TempoRegime::live);
    fixedErrorBeats = 0;
    leftFixedBeats = kBeatsToLeaveFixed;
}
```

Expose, in both `BeatDecoder::Diagnostics` and `BeatHypothesis`:

```cpp
bool  motionFirstStrictProof = false;
bool  motionReleaseCoincidence = false;
int   motionShapeModel = static_cast<int> (TempoMotionShapeModel::insufficient);
float motionShapeBpm = 0.0f;
float motionShapeQuadraticVsHinge = 0.0f;
float motionShapeEvidenceMargin = 0.0f;
int   motionShapeQuadraticWins = 0;
int   motionShapeQuarantineBeats = 0;
float motionBridgeAuthority = 0.0f;
```

`motionBridgeAuthority` remains exactly zero in this task. Publish all fields
through the existing 1.5-period `fastMotionCurrent` stale gate. When that gate
first expires, call:

```cpp
if (! fastMotionCurrent && motionShadow.veto != TempoMotionVeto::staleBeats)
    resetMotionShadow (false, TempoMotionVeto::staleBeats);
```

This removes internal authority as well as hiding stale diagnostics.

- [ ] **Step 5: Prove diagnostics are behaviorally inert**

Build the candidate and run:

```bash
clang++ -std=c++17 -O2 -Wno-unused-function -I Source \
  scripts/probe_motion_matrix.cpp Source/AI/BeatDecoder.cpp \
  Source/AI/TempoMotionTracker.cpp Source/AI/TempoMotionShape.cpp \
  Source/AI/TempoEstimator.cpp Source/AI/BeatHmm.cpp \
  Source/Tracking/TempoFollower.cpp \
  -o /tmp/probe_motion_matrix-hybrid-candidate
: > /tmp/vp-hybrid-diagnostics-quick.csv
for offset in 0 16 32 48; do
  /tmp/probe_motion_matrix-hybrid-candidate --quick --offset "$offset" --csv \
    >> /tmp/vp-hybrid-diagnostics-quick.csv
done
python3 scripts/analysis/compare_motion_matrix.py \
  /tmp/vp-hybrid-control-quick.csv /tmp/vp-hybrid-diagnostics-quick.csv
```

Expected RED only on continuous improvement (and later anti-vacuity once added).
Every fixed/step hash must match the control hashes and fixed/step
`authority_frames` must be zero. Any hash difference is a wiring bug.

Run focused tests:

```bash
cmake --build build-host --target VPTests
./build-host/VPTests_artefacts/Release/VPTests --tempo-motion
./build-host/VPTests_artefacts/Release/VPTests --tempo-step
./build-host/VPTests_artefacts/Release/VPTests --tempo-slow
./build-host/VPTests_artefacts/Release/VPTests --new-input
```

Expected GREEN: all pass.

- [ ] **Step 6: Commit diagnostics-only integration**

```bash
git add Source/AI/TempoMotionTracker.h Source/AI/TempoMotionTracker.cpp \
  Source/AI/BeatDecoder.h Source/AI/BeatDecoder.cpp \
  Source/AI/BeatHypothesis.h Tests/TestTempoMotion.cpp
git diff --cached --check
git commit -m "feat: diagnose hybrid tempo motion proof"
```

---

### Task 3: Same-Beat Bridge and Full-Tenure Shape Authority

**Files:**
- Modify: `Source/AI/TempoMotionTracker.h`
- Modify: `Source/AI/TempoMotionTracker.cpp`
- Modify: `Source/AI/BeatDecoder.h:245-270,440-470`
- Modify: `Source/AI/BeatDecoder.cpp:3026-3354`
- Modify: `Tests/TestTempoMotion.cpp`

**Interfaces:**
- Produces: `bool BeatDecoder::applyMotionBridge(float anchoredBpm, float requestedAuthority) noexcept`.
- Consumes: `motionReleaseCoincidence`, `motionShadow.predictedBpm`,
  `motionShadow.shapePredictedBpm`, and shape authority.
- Produces: `motionBridgeAuthority`, the actual authority used on the current accepted beat.

- [ ] **Step 1: Write bridge/authority tests RED**

Add assertions under `VPTests --tempo-motion`:

- On the non-vacuous ramp, coincidence occurs once, bridge authority is `0.35`,
  BPM moves in the prediction direction on that same accepted beat, then regime
  becomes `live`.
- On the omitted-first-new-beat step, bridge authority is always zero,
  `transitionSerial` and `gridSerial` follow the control fixture, and no early
  coincidence commit occurs.
- One decisive quadratic win has authority zero; the second consecutive win
  publishes `0.35`; later accepted beats may publish only
  `0.70`, then `1.00`.
- A decisive hinge immediately clears authority and opens a 12-beat quarantine.
  The trigger beat plus exactly the next 12 accepted beats cannot publish
  authority. After the hinge leaves the 12-point window, the model is affine,
  not quadratic.
- Affine, affine-with-outlier, contradictory direction, invalid period,
  transition/refit, non-direct, fixed exit, discontinuity, input epoch, and
  octave/grid reset all publish zero authority.
- At entry periods for `52`, `100`, and `168 BPM`, every strict and shape
  prediction remains finite and inside `50..190 BPM`.
- For every authority-bearing accepted beat:

```cpp
expect (std::fabs (newBpm - oldBpm) <= 0.0075f * oldBpm + 1.0e-5f,
        "hybrid bridge respects the 0.75 percent accepted-beat rail");
expect (boundedPrediction >= oldBpm * 0.96f - 1.0e-5f
        && boundedPrediction <= oldBpm * 1.04f + 1.0e-5f,
        "hybrid bridge respects the four percent prediction rail");
expect (transitionSerialAfter == transitionSerialBefore,
        "hybrid motion does not manufacture a transition");
expect (gridSerialAfter == gridSerialBefore,
        "hybrid motion does not rebuild the grid");
```

Expected RED: authority never comes from shape and the decoder does not move on
the coincidence beat.

- [ ] **Step 2: Replace scalar authority with shape authority**

Keep scalar fit/prediction only for `firstStrictProof`. Delete the scalar
`kProofBeats=3` authority increment and remove `proofBeats` as an authority
owner; a scalar contradiction still clears all shape authority immediately.
Use:

```cpp
constexpr int kQuadraticProofBeats = 2;
constexpr int kHingeQuarantineBeats = 12;
constexpr float kInitialAuthority = 0.35f;
constexpr float kAuthorityPerBeat = 0.35f;
```

A shape is decisive only when the classifier is finite and returns
`quadratic`; `TempoMotionShape::classify` has already enforced its single
`kEvidenceMarginBic=2.0` threshold. Two consecutive accepted-beat wins are
required:

```cpp
if (decisiveHinge)
{
    if (! shapeHingeActive)
        shapeQuarantineBeats = kHingeQuarantineBeats;
    shapeHingeActive = true;
    shapeQuadraticWins = 0;
    authority = 0.0f;
}
else
{
    shapeHingeActive = false;
    const bool blockedThisBeat = shapeQuarantineBeats > 0;
    if (blockedThisBeat)
        --shapeQuarantineBeats;

    if (! blockedThisBeat && decisiveQuadratic)
    {
        shapeQuadraticWins =
            std::min (shapeQuadraticWins + 1, kQuadraticProofBeats);
        if (shapeQuadraticWins >= kQuadraticProofBeats)
            authority = authority == 0.0f
                            ? kInitialAuthority
                            : std::min (1.0f, authority + kAuthorityPerBeat);
    }
    else
    {
        shapeQuadraticWins = 0;
        authority = 0.0f;
    }
}
```

`quarantineShape()` sets the counter to 12, clears authority/confirmation, and
does not re-arm on every repeated sample from the same hinge. Reset/veto paths
clear authority immediately. A full input/grid reset also clears the residual
window; a discontinuity keeps committed BPM but invalidates the time/reference
needed by the residual window, so a fresh fixed tenure must form.

When shape authority is positive, set `predictedBpm` to the finite clamped
`shapePredictedBpm`; otherwise retain the finite strict scalar prediction for
the possible same-beat edge.

- [ ] **Step 3: Implement one bridge helper with no forbidden writes**

Add to `BeatDecoder`:

```cpp
bool BeatDecoder::applyMotionBridge (float anchoredBpm,
                                     float requestedAuthority) noexcept
{
    const float authority = std::clamp (requestedAuthority, 0.0f, 1.0f);
    if (tempoRegime != TempoRegime::fixed
        || authority <= 0.0f
        || transitionState != TempoTransitionState::stable
        || transitionRefitBeats != 0
        || ! std::isfinite (motionShadow.predictedBpm)
        || motionShadow.predictedBpm < kMinBpm
        || motionShadow.predictedBpm > kMaxBpm
        || ! std::isfinite (anchoredBpm))
        return false;

    const float prior = bpm;
    const float prediction = std::clamp (
        motionShadow.predictedBpm, prior * 0.96f, prior * 1.04f);
    const float blended = anchoredBpm + (prediction - anchoredBpm) * authority;
    const float bounded = std::clamp (
        blended, prior * (1.0f - 0.0075f), prior * (1.0f + 0.0075f));
    commit (bounded, 1.0f);
    motionBridgeAuthority = authority;
    return true;
}
```

At the start of each accepted-beat update, set
`motionBridgeAuthority=0.0f`.

- [ ] **Step 4: Apply the one-shot bridge before fixed exits**

Immediately before `enterRegime(TempoRegime::live)` in the named
`releaseFixed` block:

```cpp
if (motionReleaseCoincidence)
    applyMotionBridge (bpm, 0.35f);
enterRegime (TempoRegime::live);
```

This call occurs while the regime is still fixed. `enterRegime(live)` then
revokes shadow state, and the existing live target takes over. No second early
call is possible because `firstStrictProof` is a tenure edge.

- [ ] **Step 5: Apply full shape authority inside the fixed-anchor branch**

After `anchored` is computed and exactly where the current `step`/deadband body
lives:

```cpp
if (motionShadow.authority > 0.0f)
{
    applyMotionBridge (anchored, motionShadow.authority);
}
else
{
    const float step = (anchored - bpm) / std::max (kMinBpm, bpm);
    if (std::fabs (anchored - bpm) > kFixedDeadband
        && std::fabs (step) <= kFixedMaxStep)
        bpm = std::clamp (anchored, kMinBpm, kMaxBpm);
}
```

The `else` is the exact authority-zero control body. Do not merge it into a
common blended path.

- [ ] **Step 6: Run focused GREEN and forbidden-write review**

Run:

```bash
cmake --build build-host --target VPTests
./build-host/VPTests_artefacts/Release/VPTests --tempo-motion
./build-host/VPTests_artefacts/Release/VPTests --tempo-step
./build-host/VPTests_artefacts/Release/VPTests --tempo-slow
./build-host/VPTests_artefacts/Release/VPTests --evidence
./build-host/VPTests_artefacts/Release/VPTests --new-input
./build-host/VPTests_artefacts/Release/VPTests --bar
git diff --check
```

Expected: all filters pass. Inspect the `applyMotionBridge` diff and verify its
only state writes are through `commit` (`bpm`) and
`motionBridgeAuthority` (diagnostic).

- [ ] **Step 7: Leave the globally unproven candidate uncommitted**

Do not commit yet. Task 4 is the acceptance boundary for production authority.
If Task 4 rejects either path, remove the rejected code/tests, rerun focused
GREEN, and keep no partial bridge.

---

### Task 4: Global Quick, Full, Independent, and Targeted Acceptance

**Files:**
- Modify: `scripts/analysis/compare_motion_matrix.py`
- Modify: `scripts/probe_motion_matrix.cpp`
- Modify if globally justified: `Source/AI/TempoMotionShape.cpp`
- Modify if globally justified: `Source/AI/TempoMotionTracker.cpp`
- Test: `Tests/TestTempoMotion.cpp`

**Interfaces:**
- Comparator requires positive applied bridge authority in every continuous row.
- Recovery starts only after actual bridge authority is observed.
- CSV schema remains
  `offset,family,runs,mean,p95,p995,over50,bpm_error,bpm_over4,releases,curve,trace_hash,recovery_violations,authority_frames`.

- [ ] **Step 1: Add comparator anti-vacuity RED/GREEN**

First change `_fixture_rows` to accept continuous authority, make the valid
fixture use `7`, and add a self-test case that expects a zero-authority
continuous candidate to return
`"(0, 'continuo'): autorita' non positiva"`.

Run:

```bash
python3 scripts/analysis/compare_motion_matrix.py --self-test
```

Expected RED:

```text
self-test FAIL: zero-authority continuo should fail
  got: []
```

Then add:

```python
if family == "continuo":
    if not float(after["mean"]) < float(before["mean"]):
        failures.append(f"{key}: media non migliorata")
    if not float(after["p95"]) < float(before["p95"]):
        failures.append(f"{key}: p95 non migliorato")
    if int(after["authority_frames"]) <= 0:
        failures.append(f"{key}: autorita' non positiva")
    if int(after["recovery_violations"]) != 0:
        failures.append(f"{key}: rientro oltre due beat")
```

Run:

```bash
python3 scripts/analysis/compare_motion_matrix.py --self-test
```

Expected GREEN: `PASS compare_motion_matrix self-test`.

- [ ] **Step 2: Score authority and recovery after proof**

In `probe_motion_matrix.cpp`, use the stale-gated actual field:

```cpp
const float motionAuthority = h.motionBridgeAuthority;
if (motionAuthority > 0.0f)
    ++score.authorityFrames;
shadowProven = shadowProven || motionAuthority > 0.0f;
```

Keep recovery disabled before `shadowProven`. Once proof has caused positive
applied authority, every excursion that opens above `50 ms` records its truth
beat; increment `recoveryViolations` once if it remains open after two further
truth beats. Reset the open excursion only after phase returns to `<=50 ms`.

- [ ] **Step 3: Run the mandatory four-offset quick gate**

Rebuild the candidate, regenerate CSV, then compare:

```bash
clang++ -std=c++17 -O2 -Wno-unused-function -I Source \
  scripts/probe_motion_matrix.cpp Source/AI/BeatDecoder.cpp \
  Source/AI/TempoMotionTracker.cpp Source/AI/TempoMotionShape.cpp \
  Source/AI/TempoEstimator.cpp Source/AI/BeatHmm.cpp \
  Source/Tracking/TempoFollower.cpp \
  -o /tmp/probe_motion_matrix-hybrid-candidate
: > /tmp/vp-hybrid-candidate-quick.csv
for offset in 0 16 32 48; do
  /tmp/probe_motion_matrix-hybrid-candidate --quick --offset "$offset" --csv \
    >> /tmp/vp-hybrid-candidate-quick.csv
done
python3 scripts/analysis/compare_motion_matrix.py \
  /tmp/vp-hybrid-control-quick.csv /tmp/vp-hybrid-candidate-quick.csv
```

Expected: PASS. Every fixed and step hash is bit-identical, both have zero
authority, every continuous row has positive authority, mean and p95 are
strictly lower, and recovery violations are zero.

If any step hash changes and the changed beat used
`motionReleaseCoincidence`, remove the same-beat call entirely and rerun; do not
change its strict threshold, fixture seed, or offset. If shape authority changes
a step trace, fix hinge/outlier/quarantine classification rather than relaxing
identity.

- [ ] **Step 4: Apply the one-variable tuning/rejection protocol if needed**

The immutable values are parameter counts `2/3/4/3`; strict-proof constants
`kMinimumSamples=5`, `kMinimumCoverage=0.75`,
`kMaximumShortResidual=0.035`, `kMaximumIndexGapError=0.15`,
`kMinimumRateZ=4.0`, and `kMinimumWindowDisplacement=0.006`; two quadratic
wins; 12 quarantine beats; authority `0.35`; rails `0.75%/4%`; BPM range
`50..190`; and recovery `2 beats`.

The only initially tunable classifier hypotheses are
`kAbsoluteNoiseFloorSec=0.0001` and `kEvidenceMarginBic=2.0`. Change one, run
shape tests, all focused tempo filters, and all four quick offsets. Record
control/candidate rows for every trial. Revert a trial immediately if one fixed
or step hash/authority changes, one continuous row has zero authority, one
continuous mean/p95 does not improve, or one recovery violation appears. A
quick pass is provisional until full and independent gates pass.

- [ ] **Step 5: Run the full 192-case bank**

```bash
/tmp/probe_motion_matrix-hybrid-control --csv \
  > /tmp/vp-hybrid-control-full.csv
/tmp/probe_motion_matrix-hybrid-candidate --csv \
  > /tmp/vp-hybrid-candidate-full.csv
python3 scripts/analysis/compare_motion_matrix.py \
  /tmp/vp-hybrid-control-full.csv /tmp/vp-hybrid-candidate-full.csv
```

Expected: PASS for 64 fixed, 64 continuous, and 64 step cases.

- [ ] **Step 6: Run independent, previously unused offsets**

```bash
printf '%s\n' \
  'offset,family,runs,mean,p95,p995,over50,bpm_error,bpm_over4,releases,curve,trace_hash,recovery_violations,authority_frames' \
  > /tmp/vp-hybrid-control-independent.csv
printf '%s\n' \
  'offset,family,runs,mean,p95,p995,over50,bpm_error,bpm_over4,releases,curve,trace_hash,recovery_violations,authority_frames' \
  > /tmp/vp-hybrid-candidate-independent.csv
for offset in 64 80 96 112; do
  /tmp/probe_motion_matrix-hybrid-control --quick --offset "$offset" --csv \
    >> /tmp/vp-hybrid-control-independent.csv
  /tmp/probe_motion_matrix-hybrid-candidate --quick --offset "$offset" --csv \
    >> /tmp/vp-hybrid-candidate-independent.csv
done
python3 scripts/analysis/compare_motion_matrix.py \
  /tmp/vp-hybrid-control-independent.csv \
  /tmp/vp-hybrid-candidate-independent.csv
```

Expected: PASS with the same safety and improvement contract. Failure rejects
the provisional constant/path; do not add an offset exception.

- [ ] **Step 7: Run targeted decoder/clock gates, not full VPTests**

```bash
cmake --build build-host --target VPTests VPAlign
./build-host/VPTests_artefacts/Release/VPTests --tempo-motion
./build-host/VPTests_artefacts/Release/VPTests --tempo-step
./build-host/VPTests_artefacts/Release/VPTests --tempo-slow
./build-host/VPTests_artefacts/Release/VPTests --evidence
./build-host/VPTests_artefacts/Release/VPTests --new-input
./build-host/VPTests_artefacts/Release/VPTests --bar
set -o pipefail
./build-host/VPAlign_artefacts/Release/VPAlign --ramps \
  | tee /tmp/vp-hybrid-align-ramps.txt
! rg -n "FAIL" /tmp/vp-hybrid-align-ramps.txt
./build-host/VPAlign_artefacts/Release/VPAlign \
  | tee /tmp/vp-hybrid-align-full.txt
! rg -n "FAIL" /tmp/vp-hybrid-align-full.txt
```

Expected: every command exits 0 and every printed VPAlign row says PASS,
including all four ramp rows.

Build standalone probes with the new source:

```bash
clang++ -std=c++17 -O2 -I Source scripts/probe_tempo_step.cpp \
  Source/AI/BeatDecoder.cpp Source/AI/TempoMotionTracker.cpp \
  Source/AI/TempoMotionShape.cpp Source/AI/TempoEstimator.cpp \
  Source/AI/BeatHmm.cpp -o /tmp/probe_tempo_step-hybrid
/tmp/probe_tempo_step-hybrid

c++ -std=c++17 -O2 -ISource scripts/probe_recovery.cpp \
  Source/Tracking/TempoFollower.cpp -o /tmp/vp-recovery-hybrid
/tmp/vp-recovery-hybrid
```

Expected: step probe exits 0; recovery reports `84/84`.

- [ ] **Step 8: Commit the accepted production and gate scopes**

```bash
git add Source/AI/TempoMotionTracker.h Source/AI/TempoMotionTracker.cpp \
  Source/AI/TempoMotionShape.cpp \
  Source/AI/BeatDecoder.h Source/AI/BeatDecoder.cpp \
  Tests/TestTempoMotion.cpp Tests/TestTempoMotionShape.cpp
git diff --cached --check
git commit -m "feat: bridge hybrid tempo motion proof"

git add scripts/analysis/compare_motion_matrix.py \
  scripts/probe_motion_matrix.cpp
git diff --cached --check
git commit -m "test: gate hybrid tempo motion globally"
```

If classifier constants changed for a globally passing candidate, include only
their source/test files in the Task 3 feature commit, with the exact four-offset,
full, and independent evidence recorded for Task 5 docs.

---

### Task 5: Publish Snapshot Diagnostics and Record Automated Evidence

**Files:**
- Modify: `Source/Tracking/BeatTracker.h:245-303`
- Modify: `Source/Tracking/BeatTracker.cpp:1794-1811`
- Modify: `Source/Core/Types.h:313-333`
- Modify: `Source/Audio/VirtualPercussionEngine.h:286-379`
- Modify: `Source/Audio/VirtualPercussionEngine.cpp:1860-1905,1940-2020`
- Modify: `scripts/probe_track.cpp:32-69,104-173`
- Modify: `Tests/TestTempoMotion.cpp`
- Modify: `.claude/skills/realtime-tempo/SKILL.md`
- Modify: `docs/HANDOFF_TEMPO.md`
- Modify: `docs/TODO.md`

**Interfaces:**
- Propagates shadow BPM/delta/uncertainty/state/veto, strict edge, shape winner,
  margins, confirmation, quarantine, release coincidence, and applied authority.
- Uses relaxed per-field atomics; no UI object or clock reads these fields.

- [ ] **Step 1: Add a RED copy/clear test**

Add `BeatTracker::Output::setTempoMotionDiagnostics(const BeatHypothesis*)`.
Before implementing it, test with a populated hypothesis that all fields copy,
then call with `nullptr` and assert safe defaults: BPM/delta/authority/margins
zero, uncertainty one, idle/none model state, false edges, and zero counters.

Expected RED: missing output fields/method.

- [ ] **Step 2: Add exact output/snapshot fields**

Add matching fields to `BeatTracker::Output` and `EngineSnapshot`:

```cpp
float motionShadowBpm = 0.0f;
float motionShadowPeriodDelta = 0.0f;
float motionShadowUncertainty = 1.0f;
float motionShadowAuthority = 0.0f;
float motionBridgeAuthority = 0.0f;
int motionShadowState = 0;
int motionShadowVeto = 0;
bool motionFirstStrictProof = false;
bool motionReleaseCoincidence = false;
int motionShapeModel = 0;
float motionShapeBpm = 0.0f;
float motionShapeQuadraticVsHinge = 0.0f;
float motionShapeEvidenceMargin = 0.0f;
int motionShapeQuadraticWins = 0;
int motionShapeQuarantineBeats = 0;
```

Implement the copy/clear boundary in `BeatTracker::Output`:

```cpp
void setTempoMotionDiagnostics (const BeatHypothesis* latest) noexcept
{
    if (latest != nullptr && latest->valid)
    {
        motionShadowBpm = latest->motionShadowBpm;
        motionShadowPeriodDelta = latest->motionShadowPeriodDelta;
        motionShadowUncertainty = latest->motionShadowUncertainty;
        motionShadowAuthority = latest->motionShadowAuthority;
        motionBridgeAuthority = latest->motionBridgeAuthority;
        motionShadowState = latest->motionShadowState;
        motionShadowVeto = latest->motionShadowVeto;
        motionFirstStrictProof = latest->motionFirstStrictProof;
        motionReleaseCoincidence = latest->motionReleaseCoincidence;
        motionShapeModel = latest->motionShapeModel;
        motionShapeBpm = latest->motionShapeBpm;
        motionShapeQuadraticVsHinge = latest->motionShapeQuadraticVsHinge;
        motionShapeEvidenceMargin = latest->motionShapeEvidenceMargin;
        motionShapeQuadraticWins = latest->motionShapeQuadraticWins;
        motionShapeQuarantineBeats = latest->motionShapeQuarantineBeats;
        return;
    }

    motionShadowBpm = 0.0f;
    motionShadowPeriodDelta = 0.0f;
    motionShadowUncertainty = 1.0f;
    motionShadowAuthority = 0.0f;
    motionBridgeAuthority = 0.0f;
    motionShadowState = static_cast<int> (TempoMotionShadowState::idle);
    motionShadowVeto = static_cast<int> (TempoMotionVeto::none);
    motionFirstStrictProof = false;
    motionReleaseCoincidence = false;
    motionShapeModel = static_cast<int> (TempoMotionShapeModel::insufficient);
    motionShapeBpm = 0.0f;
    motionShapeQuadraticVsHinge = 0.0f;
    motionShapeEvidenceMargin = 0.0f;
    motionShapeQuadraticWins = 0;
    motionShapeQuarantineBeats = 0;
}
```

Call it beside `setTempoTransitionDiagnostics` with
`out.setTempoMotionDiagnostics(haveHyp ? &hyp : nullptr)`.
Add one matching `std::atomic<float>`,
`std::atomic<int>`, or `std::atomic<bool>` beside the existing fit/transition
diagnostics in `VirtualPercussionEngine.h`; store from `BeatTracker::Output` in
`processBlock` and load into `snapshot()` with `memory_order_relaxed`.

- [ ] **Step 3: Extend VPTrack diagnostics without changing pulse format**

Append labelled trace columns for all motion fields. Keep the pulse file header
and six pulse columns exactly:

```text
# t beatPhase barPhase bpm clockBpm suona
```

- [ ] **Step 4: Run the full automated suite for the first time**

```bash
./scripts/run-tests.sh
cmake --build build-host --target VPAlign VPTrack
./build-host/VPAlign_artefacts/Release/VPAlign \
  | tee /tmp/vp-hybrid-align-after-snapshot.txt
! rg -n "FAIL" /tmp/vp-hybrid-align-after-snapshot.txt
python3 scripts/analysis/compare_motion_matrix.py --self-test
git diff --check
```

Expected: full TAP suite passes, all VPAlign rows pass, and no compiler/lint
diagnostics are introduced.

- [ ] **Step 5: Update the timing source of truth with exact evidence**

In `.claude/skills/realtime-tempo/SKILL.md`, `docs/HANDOFF_TEMPO.md`, and
`docs/TODO.md`, record:

- why scalar tuning was rejected (continuous `0/0/0/0` at original constants;
  permissive trials changed step hashes/authority);
- the fixed-entry residual definition and four model parameter counts;
- strict-edge one-shot and two-win quadratic authority ownership;
- all initial constants and rails;
- exact quick/full/independent CSV rows, hashes, authority, and recovery counts;
- exact VPAlign rows, filtered/full test totals, and standalone probe totals;
- that real beat-grid/listening remains open until Task 6 and microphone
  authority remains deferred.

Copy measured output verbatim; do not round or infer a pass.

- [ ] **Step 6: Commit diagnostics and automated evidence**

```bash
git add Source/Tracking/BeatTracker.h Source/Tracking/BeatTracker.cpp \
  Source/Core/Types.h Source/Audio/VirtualPercussionEngine.h \
  Source/Audio/VirtualPercussionEngine.cpp scripts/probe_track.cpp \
  Tests/TestTempoMotion.cpp .claude/skills/realtime-tempo/SKILL.md \
  docs/HANDOFF_TEMPO.md docs/TODO.md
git diff --cached --check
git commit -m "docs: record hybrid tempo motion evidence"
```

---

### Task 6: Validate Real Accelerando and Rallentando

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
- Scores `VPTrack --pulses` against refined quarter-beat timestamps.
- Produces mean/p95/p99.5 phase error and longest excursion over 50 ms.
- Produces stereo review WAV: source left, rendered percussion right.

- [ ] **Step 1: Require the real source**

```bash
test -f "/Users/nicolamarogna/Desktop/Flamingo Marco 09.07.26.m4a" \
  && echo "mixer recording present"
```

Expected: `mixer recording present`. If absent, stop; a tempogram is not phase
truth.

- [ ] **Step 2: Add scorer self-test before real scoring**

Implement for each sounding pulse between adjacent truth beats:

```python
true_phase = (pulse_time - beat_times[index]) / (
    beat_times[index + 1] - beat_times[index])
phase_cycles = ((clock_phase - true_phase + 0.5) % 1.0) - 0.5
phase_ms = abs(phase_cycles) * (
    beat_times[index + 1] - beat_times[index]) * 1000.0
```

Ignore rows before/after the grid and `suona == 0`. Print mean, p95, p99.5,
longest `>50 ms` excursion in seconds and truth beats. Exit 1 for p95 `>50 ms`
or an excursion longer than two truth beats. `--self-test` uses a 100 BPM grid
offset by 12 ms.

```bash
python3 scripts/analysis/score_beat_grid.py --self-test
```

Expected: PASS and 12.0 ms mean/p95.

- [ ] **Step 3: Implement the bounded review render**

For `--review-wav PATH`, create a two-channel 24-bit JUCE WAV writer at input
sample rate and write each processed block:

```cpp
juce::AudioBuffer<float> review (2, take);
review.copyFrom (0, 0, mono.data() + pos, take);
for (int i = 0; i < take; ++i)
    review.setSample (1, i, 0.5f * (oL[static_cast<size_t> (i)]
                                   + oR[static_cast<size_t> (i)]));
writer->writeFromAudioSampleBuffer (review, 0, take);
```

This allocation is in the offline probe, not the engine/audio callback.

- [ ] **Step 4: Select independent rising/falling windows**

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

Run:

```bash
python3 scripts/analysis/select_motion_windows.py /tmp/vp-live-motion \
  > /tmp/vp-live-motion/selection.env
source /tmp/vp-live-motion/selection.env
```

`select_motion_windows.py` must examine reliable pairs 20–50 seconds apart,
fold the later BPM to the nearest octave, require at least 1.5% total change,
print shell-quoted `ACCEL_SOURCE/START/CHANGE` and
`RALL_SOURCE/START/CHANGE`, and fail unless both directions exist.

Extract exact 45-second working files:

```bash
mkdir -p /tmp/vp-live-motion/accel /tmp/vp-live-motion/rall
swift scripts/analysis/extract_live.swift \
  "$ACCEL_SOURCE" /tmp/vp-live-motion/accel "$ACCEL_START" 45
swift scripts/analysis/extract_live.swift \
  "$RALL_SOURCE" /tmp/vp-live-motion/rall "$RALL_START" 45
```

The tempogram selects direction only. Listen and confirm audible quarter beats.

- [ ] **Step 5: Tap and refine truth grids**

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

Tap every quarter for 20–30 bars and mark downbeats `1`. Add source path and
exact selected interval comments to each grid.

- [ ] **Step 6: Score and listen**

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

Expected for both: p95 `<=50 ms`; no post-proof `>50 ms` excursion exceeds two
truth beats. On headphones, centered then soloed, require no rushing, dragging,
speed lurch, duplicate/skipped stroke, or phase snap. Record PASS or exact
failing time. A failure returns to a new synthetic reproduction before tuning.

- [ ] **Step 7: Commit real-audio evidence**

```bash
git add scripts/analysis/score_beat_grid.py \
  scripts/analysis/select_motion_windows.py scripts/probe_track.cpp \
  docs/tempo-grids/flamingo-accelerando.txt \
  docs/tempo-grids/flamingo-rallentando.txt \
  .claude/skills/realtime-tempo/SKILL.md \
  docs/HANDOFF_TEMPO.md docs/TODO.md
git diff --cached --check
git commit -m "test: validate hybrid motion on real beat grids"
```

Record exact scores, longest excursions, review filenames, and listening
verdicts.

---

### Task 7: Audit Direct-Path Completion

**Files:**
- Read: `docs/superpowers/specs/2026-09-16-tempo-motion-shadow-tracker-design.md`
- Read: `docs/superpowers/plans/2026-09-16-hybrid-tempo-motion-proof.md`
- Read: `.claude/skills/realtime-tempo/SKILL.md`
- Read: `docs/HANDOFF_TEMPO.md`
- Read: current Git diff and all recorded test outputs

**Interfaces:**
- Produces a requirement-by-requirement file/mixer completion decision.
- Does not enable microphone authority.

- [ ] **Step 1: Rebuild and rerun every authoritative artifact**

From fresh binaries, rerun quick/full/independent A/B, comparator self-test,
full `VPTests`, all focused tempo filters, full/ramps `VPAlign`,
`probe_tempo_step`, 84-case `probe_recovery`, and both real beat-grid scores.

Expected: every command exits 0; every VPAlign row prints PASS; fixed/step
hashes match; continuous mean/p95 improve per row with positive authority and
zero recovery violations; real measurement/listening passes.

- [ ] **Step 2: Inspect ownership and continuity**

Confirm by code inspection:

- `TempoMotionShape` and `TempoMotionTracker` use only fixed storage/bounded loops.
- Tracker state is worker-owned by `BeatDecoder`; snapshots are atomic diagnostics.
- `TempoFollower` has no new tempo/phase owner and clock restart/snap behavior is unchanged.
- Authority-zero fixed code is the original body.
- Early/full bridge code calls only `commit` and writes its diagnostic authority.
- No forbidden anchor/history/grid/transition/octave/bar state is written.
- Every veto/reset/stale boundary zeros authority; hinge/edge quarantine is 12 accepted beats.
- Every published period/BPM is finite and in range.

- [ ] **Step 3: Decide completion without a code commit**

Mark file/mixer direct path complete only with all automated numbers and both
human listening PASS verdicts. Otherwise leave the goal open with the exact
failed gate. Do not commit an audit-only change. Microphone/room evidence begins
a separate brainstorming/specification cycle and must not inherit line-feed
authority by default.
