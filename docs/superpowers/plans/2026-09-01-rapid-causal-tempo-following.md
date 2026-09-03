# Rapid Causal Tempo Following Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Detect a 5–10% abrupt live-tempo step within two new beats, adopt its BPM immediately, and bring phase below 25 ms during the following beat without look-ahead or regressions.

**Architecture:** Add a bounded `stable → suspected → rapid` transition detector to `BeatDecoder`, fed by causal activation peaks before the old-grid rejection gate. Publish explicit transition metadata to `BeatTracker`; on a newly confirmed transition, let `TempoFollower` adopt the new rate without moving phase and temporarily use the existing 25% monotonic steering ceiling.

**Tech Stack:** C++20, JUCE 8.0.15, CMake, deterministic `VPTests`, `VPAlign`, existing ONNX/BeatNet worker pipeline.

## Global Constraints

- Do not touch recorded-loop banks, hybrid rendering, time stretching, LOOP/PATTERN behaviour, or the loop-debug worktree changes.
- Do not add look-ahead: every decision uses only activation peaks whose frames have already completed.
- Do not allocate, block, log, format strings, run inference, or perform unbounded work on the audio callback.
- Preserve continuous, monotonically advancing phase while percussion is audible.
- Preserve `CASSA: NO` as the default; retain the dedicated-kick path and preference.
- Accept the implementation only when existing assertions and baseline metrics are unchanged or improved.
- Do not create a git commit unless the user explicitly authorizes it.

## File map

- `Source/AI/BeatHypothesis.h`: public transition enums and worker-to-audio-thread scalar payload.
- `Source/AI/BeatDecoder.h/.cpp`: transition evidence, state machine, direct provisional tempo adoption, and reset handling.
- `Source/Tracking/TempoFollower.h/.cpp`: one-shot rate adoption and one-beat rapid phase-steering window.
- `Source/Tracking/PhaseTrust.h`: named rapid-phase constant shared by app and probes.
- `Source/Tracking/BeatTracker.h/.cpp`: consume each transition serial once and expose diagnostics.
- `Source/Core/Types.h`: engine snapshot transition diagnostics.
- `Source/Audio/VirtualPercussionEngine.h/.cpp`: atomically bridge diagnostics to UI snapshots.
- `Source/UI/MainComponent.cpp`: append compact transition information to the existing debug page.
- `Tests/TestAiBeat.cpp`: deterministic decoder, clock, fill, ramp, dropout, octave, kick-default, and pulse-integrity assertions.
- `scripts/probe_align.cpp`: baseline/after metrics for tempo steps and phase recovery; remove the inert `quickStep` A/B seam.
- `docs/AI_BEAT_TRACKING.md`, `docs/CORE_TIMING_AUDIT.md`: measured behaviour and causal limits.

---

## Preflight: preserve the current baseline

- [ ] Build the unchanged timing targets.

Run:

```bash
cmake --build build --target VPTests VPAlign --config Release -j 8
```

Expected: both targets build successfully. If `build` is not configured, configure the existing host preset first rather than changing CMake options.

- [ ] Capture deterministic baseline output outside the repository.

Run:

```bash
./build/VPAlign_artefacts/Release/VPAlign > /tmp/virtualperc-vpalign-before.txt
./build/VPTests_artefacts/Release/VPTests > /tmp/virtualperc-vptests-before.txt
```

Expected: `VPTests` ends with `0 failed`; `/tmp/virtualperc-vpalign-before.txt` contains the tempo-step and ramp-phase sections.

- [ ] Record the current worktree boundary.

Run:

```bash
git status --short
```

Expected: the pre-existing loop, JUCE, UI, engine, test, documentation, and script changes remain present. Later staging or cleanup must not include unrelated files.

---

### Task 1: Publish a bounded tempo-transition state

**Files:**
- Modify: `Source/AI/BeatHypothesis.h`
- Modify: `Source/AI/BeatDecoder.h`
- Modify: `Source/AI/BeatDecoder.cpp`
- Test: `Tests/TestAiBeat.cpp`

**Interfaces:**
- Produces: `TempoTransitionState`, `TempoTransitionReason`.
- Produces in `BeatHypothesis`: `transitionState`, `transitionReason`, `transitionBpm`, `transitionConfidence`, `transitionIntervals`, `transitionSerial`.
- Produces in `BeatDecoder`: `observeTempoTransition(...)`, `clearTempoTransition(...)`, `recentIntervalJitter()`.

- [ ] **Step 1: Add a deterministic step feeder and failing decoder assertions**

Add before `vpRunAiBeatTests` in `Tests/TestAiBeat.cpp`:

```cpp
struct DecoderStepResult
{
    double rapidAtSec = -1.0;
    float bpmAtDeadline = 0.0f;
    int rapidCount = 0;
    float clockBpmAtTwoBeats = 0.0f;
    double clockPhaseMsAtThreeBeats = 1.0e9;
    bool clockMovedBackwards = false;
    int pulseViolations = 0;
};

DecoderStepResult runDecoderStep (float fromBpm, float toBpm, bool lineFeed,
                                  bool addSingleOutlier,
                                  double rampSeconds = 0.0,
                                  double discontinuityAt = -1.0)
{
    constexpr double fps = 50.0;
    constexpr double changeAt = 18.0;
    constexpr double duration = 26.0;

    vp::BeatDecoder dec;
    dec.prepare (fps);
    dec.setLevelAnchor (true);
    dec.setLineFeed (lineFeed);

    std::vector<double> beatFrame;
    double t = 0.0;
    while (t < duration + 1.0)
    {
        beatFrame.push_back (t * fps);
        const float bpm = t < changeAt ? fromBpm : toBpm;
        t += 60.0 / static_cast<double> (bpm);
    }
    if (addSingleOutlier)
        beatFrame.push_back ((changeAt + 0.37) * fps);

    std::sort (beatFrame.begin(), beatFrame.end());
    DecoderStepResult result;
    for (int frame = 0; frame < static_cast<int> (duration * fps); ++frame)
    {
        float activation = 0.03f;
        for (double beat : beatFrame)
        {
            const double d = (static_cast<double> (frame) - beat) / 1.35;
            if (std::fabs (d) < 5.0)
                activation = std::max (activation,
                                       0.94f * static_cast<float> (std::exp (-0.5 * d * d)));
        }

        const vp::BeatHypothesis h = dec.observe (activation, 0.02f, 1.0f - activation);
        const double now = static_cast<double> (frame) / fps;
        if (h.transitionState == vp::TempoTransitionState::rapid)
        {
            if (result.rapidAtSec < 0.0)
                result.rapidAtSec = now;
            ++result.rapidCount;
        }

        const double deadline = changeAt + 2.0 * 60.0 / static_cast<double> (toBpm);
        if (now >= deadline && result.bpmAtDeadline == 0.0f)
            result.bpmAtDeadline = h.bpm;
    }
    return result;
}
```

Add inside `vpRunAiBeatTests`:

```cpp
{
    for (const bool line : { true, false })
    {
        const auto up = runDecoderStep (120.0f, 132.0f, line, false);
        const auto down = runDecoderStep (128.0f, 120.0f, line, false);
        expect (up.rapidAtSec >= 0.0
                    && up.rapidAtSec <= 18.0 + 2.0 * 60.0 / 132.0 + 0.04,
                line ? "line confirms rising tempo step within two beats"
                     : "microphone confirms rising tempo step within two beats");
        expect (down.rapidAtSec >= 0.0
                    && down.rapidAtSec <= 18.0 + 2.0 * 60.0 / 120.0 + 0.04,
                line ? "line confirms falling tempo step within two beats"
                     : "microphone confirms falling tempo step within two beats");
        expect (std::fabs (up.bpmAtDeadline - 132.0f) <= 1.0f
                    && std::fabs (down.bpmAtDeadline - 120.0f) <= 1.0f,
                line ? "line step tempo is within one BPM at deadline"
                     : "microphone step tempo is within one BPM at deadline");
    }

    const auto outlier = runDecoderStep (120.0f, 120.0f, true, true);
    expect (outlier.rapidCount == 0,
            "one off-grid event cannot confirm a tempo transition");
}
```

- [ ] **Step 2: Build and verify the new tests fail for the missing interface**

Run:

```bash
cmake --build build --target VPTests --config Release -j 8
```

Expected: compilation fails because `TempoTransitionState` and transition fields do not exist.

- [ ] **Step 3: Add transition payload types**

Add to `Source/AI/BeatHypothesis.h` after `TempoRegime`:

```cpp
enum class TempoTransitionState : int
{
    stable = 0,
    suspected,
    rapid
};

enum class TempoTransitionReason : int
{
    none = 0,
    candidateStarted,
    confirmed,
    incoherent,
    outsideRange,
    metricalConflict,
    expired,
    reset
};
```

Add to `BeatHypothesis`:

```cpp
TempoTransitionState transitionState = TempoTransitionState::stable;
TempoTransitionReason transitionReason = TempoTransitionReason::none;
float transitionBpm = 0.0f;
float transitionConfidence = 0.0f;
int transitionIntervals = 0;
uint32_t transitionSerial = 0;
```

The struct remains trivially copyable, so `HypothesisSlot` automatically expands its fixed atomic-word payload.

- [ ] **Step 4: Add fixed-size decoder state and reset helpers**

Declare in `BeatDecoder.h`:

```cpp
bool observeTempoTransition (double eventTimeSec, float strength,
                             bool acceptedByCurrentGrid) noexcept;
void clearTempoTransition (TempoTransitionReason reason) noexcept;
float recentIntervalJitter() const noexcept;
void storeBeatForFit (double beatTimeSec, float strength) noexcept;
```

Add private state:

```cpp
TempoTransitionState transitionState = TempoTransitionState::stable;
TempoTransitionReason transitionReason = TempoTransitionReason::none;
double transitionFirstSec = -1.0;
double transitionLastSec = -1.0;
float transitionPeriodSec = 0.0f;
float transitionConfidence = 0.0f;
int transitionIntervals = 0;
int transitionRapidBeats = 0;
uint32_t transitionSerial = 0;
```

Use these constants in `BeatDecoder.cpp`:

```cpp
constexpr float kTransitionMinBpmDelta = 1.0f;
constexpr float kTransitionMaxRelativeDelta = 0.25f;
constexpr float kTransitionLineCoherence = 0.010f;
constexpr float kTransitionRoomCoherence = 0.020f;
constexpr int kTransitionRapidLifetimeBeats = 2;
```

`recentIntervalJitter()` must inspect at most the newest eight accepted intervals, use stack arrays only, and return their median absolute relative deviation. If fewer than four intervals exist, return the source floor: `0.005f` for line and `0.010f` for microphone.

- [ ] **Step 5: Observe eligible peaks before the old-grid rejection**

Refactor the peak section of `BeatDecoder::observe` into these explicit decisions:

```cpp
const bool eligiblePeak = localMaximum && refractoryFrames == 0
                          && (lastBeatSec < 0.0
                              || (eventTimeSec - lastBeatSec)
                                     >= 0.4 * static_cast<double> (period));

bool acceptedByCurrentGrid = eligiblePeak;
if (acceptedByCurrentGrid && established && lastBeatSec >= 0.0)
{
    const double beats = (eventTimeSec - lastBeatSec) / static_cast<double> (period);
    if (std::fabs (beats - std::round (beats)) > kOnGridTolerance
        && beats < kGridStaleBeats)
        acceptedByCurrentGrid = false;
}

const bool confirmedTransition =
    eligiblePeak && established
    && observeTempoTransition (eventTimeSec, prevPulse, acceptedByCurrentGrid);
bool peak = acceptedByCurrentGrid || confirmedTransition;
```

`observeTempoTransition` must:

1. Compare the newest interval to committed `period`.
2. Start `suspected` only when relative change exceeds
   `max(1 / bpm, 3 * recentIntervalJitter())` and is at most 25%.
3. On the next eligible peak, compare both candidate intervals with their mean.
4. Require `max(1%, 2*jitter)` coherence on line input and
   `max(2%, 2*jitter)` plus both strengths at least `beatThresh` on microphone.
5. Reject candidates outside the current metrical octave or BPM limits.
6. On confirmation, clear old fit histories, retain the first changed beat via
   `storeBeatForFit`, set `bpm = 60 / candidatePeriod`, set
   `gridAnchorSec = eventTimeSec`, enter `TempoRegime::live`, increment
   `transitionSerial`, and return `true`.
7. Keep `rapid` for at most two subsequently accepted beats, then return to
   `stable`.

`storeBeatForFit` writes `beatTime` and `beatStrength` without incrementing
`beatSerial`; the current confirmed peak still goes through `registerBeat`, so
the audio thread never receives a retroactive beat event.

- [ ] **Step 6: Clear transition state at every invalidation boundary**

Call `clearTempoTransition(TempoTransitionReason::reset)` from `reset`,
`notifyDiscontinuity`, `notifyInputRestart`, and `setUserOctave`. Also clear it
when `checkGridPhase` increments `gridSerial`.

Publish all six fields at the end of `observe`:

```cpp
hyp.transitionState = transitionState;
hyp.transitionReason = transitionReason;
hyp.transitionBpm = transitionPeriodSec > 0.0f ? 60.0f / transitionPeriodSec : 0.0f;
hyp.transitionConfidence = transitionConfidence;
hyp.transitionIntervals = transitionIntervals;
hyp.transitionSerial = transitionSerial;
```

- [ ] **Step 7: Build and run the full deterministic suite**

Run:

```bash
cmake --build build --target VPTests --config Release -j 8
./build/VPTests_artefacts/Release/VPTests
```

Expected: new rising/falling line and microphone assertions pass; single outlier produces no rapid transition; all previous tests end with `0 failed`.

- [ ] **Step 8: Review checkpoint**

Inspect:

```bash
git diff --check
git diff -- Source/AI/BeatHypothesis.h Source/AI/BeatDecoder.h Source/AI/BeatDecoder.cpp Tests/TestAiBeat.cpp
```

Expected: no whitespace errors, heap allocation, logging, or unrelated changes.

---

### Task 2: Adopt confirmed tempo without moving phase

**Files:**
- Modify: `Source/Tracking/TempoFollower.h`
- Modify: `Source/Tracking/TempoFollower.cpp`
- Modify: `Source/Tracking/PhaseTrust.h`
- Modify: `Source/Tracking/BeatTracker.h`
- Modify: `Source/Tracking/BeatTracker.cpp`
- Test: `Tests/TestAiBeat.cpp`

**Interfaces:**
- Consumes: `BeatHypothesis::transitionState`, `transitionSerial`,
  `transitionBpm`, `transitionConfidence`.
- Produces: `TempoFollower::beginTempoTransition(float bpm) noexcept`.
- Produces: `kGridTauRapid = 0.10f`.

- [ ] **Step 1: Extend the step feeder with a real clock and failing assertions**

In `runDecoderStep`, create and initialise a clock before the frame loop:

```cpp
vp::TempoFollower clock;
clock.prepare (48000.0);
clock.setPulsesPerBeat (4);
clock.forceTempo (fromBpm);
clock.setTargetTempo (fromBpm, 1.0f);
clock.setFollowStrength (vp::FollowStrength::high);
clock.setLocked (true);

uint32_t lastBeatSerial = 0;
uint32_t lastTransitionSerial = 0;
bool haveBeatSerial = false;
double previousClockPosition = 0.0;
```

After each `dec.observe` call, drive the clock:

```cpp
if (h.valid)
{
    if (h.transitionState == vp::TempoTransitionState::rapid
        && h.transitionSerial != lastTransitionSerial)
    {
        clock.beginTempoTransition (h.transitionBpm);
        lastTransitionSerial = h.transitionSerial;
    }
    clock.setTargetTempo (h.bpm, h.confidence);
    clock.setGridPhase (h.beatPhase,
                        clock.tempoTransitionActive()
                            ? vp::kGridTauRapid
                            : vp::gridPhaseTau (vp::kGridTauHolding, true, 1.0f));
    if (! haveBeatSerial)
    {
        lastBeatSerial = h.beatSerial;
        haveBeatSerial = true;
    }
    else if (h.beatSerial != lastBeatSerial)
    {
        lastBeatSerial = h.beatSerial;
        clock.observeOnsetPhase (
            vp::wrap01 (clock.beatPhase() - h.beatPhase), h.confidence, 1);
    }
}

const vp::ClockTick tick = clock.advance (960);
if (tick.pulsesFired > 2)
    ++result.pulseViolations;
const double clockPosition =
    static_cast<double> (clock.beatsElapsed()) + clock.beatPhase();
if (clockPosition + 1.0e-6 < previousClockPosition)
    result.clockMovedBackwards = true;
previousClockPosition = clockPosition;

const double twoBeatDeadline =
    changeAt + 2.0 * 60.0 / static_cast<double> (toBpm);
const double threeBeatDeadline =
    changeAt + 3.0 * 60.0 / static_cast<double> (toBpm);
if (now >= twoBeatDeadline && result.clockBpmAtTwoBeats == 0.0f)
    result.clockBpmAtTwoBeats = clock.currentTempo();
if (now >= threeBeatDeadline && result.clockPhaseMsAtThreeBeats > 1.0e8)
{
    double lastTrueBeat = 0.0;
    for (double beat : beatFrame)
        if (beat / fps <= now)
            lastTrueBeat = beat / fps;
    const float truePhase = vp::wrap01 (
        static_cast<float> ((now - lastTrueBeat) * toBpm / 60.0));
    result.clockPhaseMsAtThreeBeats =
        std::fabs (vp::wrapCentered (clock.beatPhase() - truePhase))
        * 60.0 / static_cast<double> (toBpm) * 1000.0;
}
```

Add assertions for `120→132`, `128→120`, `76→82`, and `168→156`:

```cpp
expect (std::fabs (result.clockBpmAtTwoBeats - toBpm) <= 1.0f,
        "clock adopts a five-to-ten-percent step within two beats");
expect (result.clockPhaseMsAtThreeBeats <= 25.0,
        "clock phase is below twenty-five milliseconds by the following beat");
expect (! result.clockMovedBackwards,
        "rapid tempo transition never moves clock phase backwards");
expect (result.pulseViolations == 0,
        "rapid tempo transition neither drops nor duplicates a quarter grid");
```

- [ ] **Step 2: Build and verify the new clock interface is missing**

Run:

```bash
cmake --build build --target VPTests --config Release -j 8
./build/VPTests_artefacts/Release/VPTests
```

Expected: compilation fails because `beginTempoTransition`,
`tempoTransitionActive`, and `kGridTauRapid` do not exist yet.

- [ ] **Step 3: Add one-shot transition adoption**

Declare in `TempoFollower.h`:

```cpp
void beginTempoTransition (float bpm) noexcept;
bool tempoTransitionActive() const noexcept { return transitionSamplesRemaining > 0; }
```

Add state:

```cpp
int transitionSamplesRemaining = 0;
```

Implement in `TempoFollower.cpp`:

```cpp
void TempoFollower::beginTempoTransition (float bpm) noexcept
{
    if (! std::isfinite (bpm) || bpm <= 40.0f || bpm >= 220.0f)
        return;

    tempo = bpm;
    target = bpm;
    tempoTrim = 0.0f;
    lastDrift = 0.0f;
    driftSameWay = 0;
    transitionSamplesRemaining =
        std::max (1, static_cast<int> (std::lround (sampleRate * 60.0 / bpm)));
}
```

Reset `transitionSamplesRemaining` in `reset`, `resetClock`, and `forceTempo`.
In `advance`, decrement it by `numSamples` with a zero floor.

- [ ] **Step 4: Open only the confirmed transition phase path**

Add to `PhaseTrust.h`:

```cpp
constexpr float kGridTauRapid = 0.10f;
```

Inside `TempoFollower::advance`, while `transitionSamplesRemaining > 0`:

```cpp
tau = std::min (tau, 60.0f / std::max (40.0f, tempo));
steerCeil = std::max (steerCeil, 0.25f);
```

Do not change normal `glide`, `kGridTauHolding`, follow-strength limits, or
evidence-trust behaviour outside this window.

- [ ] **Step 5: Consume each transition serial once in BeatTracker**

Add to `BeatTracker`:

```cpp
uint32_t lastTransitionSerial = 0;
bool seenTransitionSerial = false;
```

Reset both in `BeatTracker::reset`. In `process`, after loading a valid
hypothesis and before normal `setTargetTempo`:

```cpp
const bool newTempoTransition =
    hyp.transitionState == TempoTransitionState::rapid
    && (! seenTransitionSerial || hyp.transitionSerial != lastTransitionSerial);

if (newTempoTransition && ! tempoOwned)
{
    follower.beginTempoTransition (hyp.transitionBpm);
    lastTransitionSerial = hyp.transitionSerial;
    seenTransitionSerial = true;
}
```

When setting phase:

```cpp
const float phaseTau =
    follower.tempoTransitionActive()
        ? kGridTauRapid
        : gridPhaseTau (kGridTauHolding, holding, evidence.trust());
follower.setGridPhase (songPhase, phaseTau);
```

Manual TAP/FISSO ownership must continue to win; a neural transition cannot
change a user-owned tempo.

- [ ] **Step 6: Run decoder-plus-clock and full-suite tests**

Run:

```bash
cmake --build build --target VPTests --config Release -j 8
./build/VPTests_artefacts/Release/VPTests
```

Expected: all four tempo-step cases meet BPM and phase deadlines, phase remains
monotonic, pulse assertions pass, and the suite ends with `0 failed`.

- [ ] **Step 7: Review checkpoint**

Run:

```bash
git diff --check
```

Inspect the clock diff and confirm that only a new transition serial can bypass
the normal tempo glide.

---

### Task 3: Prove fills, ramps, dropouts, and metrical levels do not regress

**Files:**
- Modify: `Tests/TestAiBeat.cpp`
- Modify: `scripts/probe_align.cpp`
- Modify: `Source/AI/BeatDecoder.h`

**Interfaces:**
- Consumes: transition state and clock API from Tasks 1–2.
- Removes: inert `BeatDecoder::setQuickStep(bool)` and `quickStep`.

- [ ] **Step 1: Add adversarial transition tests**

Generalise the beat-list construction inside `runDecoderStep` with an additional
`double rampSeconds = 0.0` argument:

```cpp
const float bpm = t < changeAt
                      ? fromBpm
                      : (rampSeconds <= 0.0
                             ? toBpm
                             : static_cast<float> (
                                   fromBpm + (toBpm - fromBpm)
                                       * std::min (1.0, (t - changeAt) / rampSeconds)));
```

Add an optional `double discontinuityAt = -1.0` argument and invoke:

```cpp
if (discontinuityAt >= 0.0
    && now >= discontinuityAt
    && now < discontinuityAt + 1.0 / fps)
    dec.notifyDiscontinuity (0.20);
```

Add these complete assertions:

```cpp
expect (runDecoderStep (120.0f, 120.0f, true, true).rapidCount == 0,
        "one fill onset does not trigger rapid tempo");
expect (runDecoderStep (118.0f, 126.0f, true, false, 4.0).rapidCount == 0,
        "four-second accelerando remains on the live-fit path");
expect (runDecoderStep (118.0f, 126.0f, true, false, 12.0).rapidCount == 0,
        "twelve-second accelerando remains on the live-fit path");
expect (runDecoderStep (120.0f, 120.0f, true, true, 0.0, 18.5).rapidCount == 0,
        "analysis discontinuity clears partial tempo-transition evidence");

const auto eighths = runDecoderStep (76.0f, 76.0f, true, false);
expect (std::fabs (eighths.bpmAtDeadline - 76.0f) <= 1.0f
            && eighths.rapidCount == 0,
        "steady slow tempo cannot turn rapid transition into double tempo");
```

In the alternating-eighth test beginning near line 368, add:

```cpp
bool eighthWentRapid = false;
```

Inside its frame loop, after `eighths.observe`, add:

```cpp
eighthWentRapid = eighthWentRapid
                  || h.transitionState == vp::TempoTransitionState::rapid;
if (h.valid && acquiredBpm == 0.0f)
{
    acquiredAt = static_cast<double> (f) / fps;
    acquiredBpm = h.bpm;
}
```

Remove the old `break` so the full four-second train is observed, then add:

```cpp
expect (! eighthWentRapid,
        "alternating eighths never become an abrupt tempo transition");
```

- [ ] **Step 2: Run tests and tighten only transition-specific thresholds**

Run:

```bash
cmake --build build --target VPTests --config Release -j 8
./build/VPTests_artefacts/Release/VPTests
```

Expected: all adversarial cases pass. If a case fails, adjust only candidate
change/coherence/metrical checks; do not globally alter existing octave,
refractory, phase, or confidence constants.

- [ ] **Step 3: Replace VPAlign’s inert A/B with measured before/after metrics**

Remove the `quickStep` parameter from `tempoChange`, remove
`dec.setQuickStep(quickStep)`, and remove the two-pass `q` loop. Extend
`TempoStep`:

```cpp
struct TempoStep
{
    double secondsToOneBpm = -1.0;
    double phaseMsAtThirdBeat = 1.0e9;
    double worstErrPct = 0.0;
    double settledErrPct = 0.0;
    int rapidTransitions = 0;
    int pulseViolations = 0;
};
```

Score the 5–10% line cases `118→124`, `118→128`, `128→120`, `76→82`, and
`168→156`, plus the existing 40% stress step and both ramps. Print one result
per case; do not retain an old/new runtime flag.

- [ ] **Step 4: Remove the dead decoder seam**

Delete from `BeatDecoder.h`:

```cpp
void setQuickStep (bool on) noexcept { quickStep = on; }
bool quickStep = true;
```

Run:

```bash
rg "setQuickStep|quickStep" Source scripts Tests
```

Expected: no matches.

- [ ] **Step 5: Run VPAlign and compare protected metrics**

Run:

```bash
cmake --build build --target VPAlign --config Release -j 8
./build/VPAlign_artefacts/Release/VPAlign > /tmp/virtualperc-vpalign-after.txt
diff -u /tmp/virtualperc-vpalign-before.txt /tmp/virtualperc-vpalign-after.txt
```

Expected: step output format changes and 5–10% step response improves. Existing
steady, jitter, half-beat, hole, ramp, and alignment metrics remain unchanged or
improve. Investigate any worsening before continuing.

- [ ] **Step 6: Review checkpoint**

Run:

```bash
git diff --check
git diff -- Tests/TestAiBeat.cpp scripts/probe_align.cpp Source/AI/BeatDecoder.h
```

Expected: deterministic fixed-seed tests, no inert branch, and no unrelated
cleanup.

---

### Task 4: Expose bounded transition diagnostics

**Files:**
- Modify: `Source/Tracking/BeatTracker.h`
- Modify: `Source/Tracking/BeatTracker.cpp`
- Modify: `Source/Core/Types.h`
- Modify: `Source/Audio/VirtualPercussionEngine.h`
- Modify: `Source/Audio/VirtualPercussionEngine.cpp`
- Modify: `Source/UI/MainComponent.cpp`
- Test: `Tests/TestAiBeat.cpp`

**Interfaces:**
- Consumes: transition payload from `BeatHypothesis`.
- Produces in `BeatTracker::Output` and `EngineSnapshot`: state, reason,
  candidate BPM, confidence, interval count.

- [ ] **Step 1: Add a failing snapshot-shape assertion**

Inside `vpRunAiBeatTests`, assert the default snapshot shape:

```cpp
const vp::EngineSnapshot snap;
expect (static_cast<int> (snap.tempoTransitionState) >= 0,
        "engine snapshot publishes tempo-transition state");
expect (snap.tempoTransitionIntervals >= 0,
        "engine snapshot publishes bounded transition evidence");
```

Build once and expect compilation to fail because the snapshot fields are absent.

- [ ] **Step 2: Add scalar output and snapshot fields**

Add matching defaults to `BeatTracker::Output` and `EngineSnapshot`:

```cpp
TempoTransitionState tempoTransitionState = TempoTransitionState::stable;
TempoTransitionReason tempoTransitionReason = TempoTransitionReason::none;
float tempoTransitionBpm = 0.0f;
float tempoTransitionConfidence = 0.0f;
int tempoTransitionIntervals = 0;
```

Populate `BeatTracker::Output` from the latest valid hypothesis. Add relaxed
atomics in `VirtualPercussionEngine`:

```cpp
std::atomic<int> lastTempoTransitionState { 0 };
std::atomic<int> lastTempoTransitionReason { 0 };
std::atomic<float> lastTempoTransitionBpm { 0.0f };
std::atomic<float> lastTempoTransitionConfidence { 0.0f };
std::atomic<int> lastTempoTransitionIntervals { 0 };
```

Store them beside `lastFitResidual` and load them in `snapshot()`.

- [ ] **Step 3: Add one compact debug line**

Append beside the existing `prove/tau/fit` line in `MainComponent.cpp`:

```cpp
lines.add ("cambio " + juce::String (static_cast<int> (snap.tempoTransitionState))
           + "  motivo " + juce::String (static_cast<int> (snap.tempoTransitionReason))
           + "  bpm " + juce::String (snap.tempoTransitionBpm, 1)
           + "  conf " + juce::String (snap.tempoTransitionConfidence, 2)
           + "  int " + juce::String (snap.tempoTransitionIntervals));
```

Keep formatting on the message thread; only scalar stores occur in the callback.

- [ ] **Step 4: Verify CASSA remains default-off and functional**

Add or retain:

```cpp
vp::EngineSettings defaults;
expect (defaults.kickChannel.load() == -1,
        "dedicated kick input remains disabled by default");
```

Run the existing kick-chain assertions without changing their expected timing.

- [ ] **Step 5: Build, test, and inspect diagnostics**

Run:

```bash
cmake --build build --target VPTests --config Release -j 8
./build/VPTests_artefacts/Release/VPTests
```

Expected: propagation and CASSA-default assertions pass; full suite ends with
`0 failed`.

Use `ReadLints` on the modified source files and fix only newly introduced
diagnostics.

---

### Task 5: Document measurements and perform final regression gate

**Files:**
- Modify: `docs/AI_BEAT_TRACKING.md`
- Modify: `docs/CORE_TIMING_AUDIT.md`
- Verify only: all implementation files above

**Interfaces:**
- Consumes: final deterministic and probe results.
- Produces: durable measured limits and rationale for future maintainers.

- [ ] **Step 1: Write measured documentation**

Add a section to `docs/AI_BEAT_TRACKING.md` describing:

- transition uses two causal intervals and no future frames;
- the first interval only creates suspicion and never changes the clock;
- confirmation directly adopts BPM while keeping phase continuous;
- 5–10% steps meet the two-beat BPM and following-beat phase targets;
- larger changes remain limited by the 25% monotonic steering ceiling;
- CASSA remains optional/default-off and refines phase only.

Add a dated result block to `docs/CORE_TIMING_AUDIT.md` containing the actual
before/after numbers from `/tmp/virtualperc-vpalign-before.txt` and
`/tmp/virtualperc-vpalign-after.txt`. Do not claim improvement for any metric
that was not measured.

- [ ] **Step 2: Run formatting and source diagnostics**

Run:

```bash
git diff --check
```

Use `ReadLints` for all modified `.h` and `.cpp` files.

Expected: no whitespace errors and no newly introduced diagnostics.

- [ ] **Step 3: Run the complete host regression suite**

Run:

```bash
cmake --build build --target VPTests VPAlign --config Release -j 8
./build/VPTests_artefacts/Release/VPTests
./build/VPAlign_artefacts/Release/VPAlign
```

Expected: `VPTests` ends with `0 failed`; every protected VPAlign metric is
unchanged or improved; all 5–10% step targets pass.

- [ ] **Step 4: Verify the Apple build**

Run:

```bash
./scripts/build-simulator.sh
```

Expected: `BUILD SUCCEEDED`. The recorded-loop backend and WSOLA/Signalsmith
configuration remain exactly as they were before this task.

- [ ] **Step 5: Audit scope and real-time safety**

Run:

```bash
git diff --stat
git diff --check
git status --short
```

Inspect the final diff for:

- no changes under `Assets/Loops/dance` or `Source/Loops`;
- no new allocation, mutex, I/O, or formatting in audio-thread code;
- no modification to `kickChannel` default or CASSA routing;
- no retained A/B flag or `quickStep` symbol;
- no accidental overwrite of pre-existing worktree changes.

- [ ] **Step 6: Final handoff**

Report measured before/after tempo and phase results, tests/builds run, any
remaining larger-step limitations, and the exact modified files. Do not commit
unless the user separately asks for a commit.
