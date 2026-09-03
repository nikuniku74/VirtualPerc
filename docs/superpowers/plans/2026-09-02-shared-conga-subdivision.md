# Shared Conga Subdivision Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the existing AUTO / 1/4 / 1/8 / 1/16 control thin synthesized congas, ghosts and fills with the same grid used by the shaker.

**Architecture:** `GrooveEngine::eventsAt` remains the only event-generation boundary. One bounded subdivision predicate gates shaker and conga events before any event is emitted; the clock, phrase selection, dynamics, swing and recorded-loop renderer remain unchanged.

**Tech Stack:** C++17, JUCE 8, CMake, `VPTests`, `VPRender`, `VPTiming`

## Global Constraints

- 1/4 keeps steps 0, 4, 8 and 12.
- 1/8 keeps every even step, including offbeat `&` eighths.
- 1/16 keeps all sixteen steps.
- AUTO resolves to 1/8.
- The rule thins events and never adds or moves them.
- Written congas, fill congas and conga ghosts use the same rule.
- No conga may be emitted on step 0.
- The clock remains at four pulses per beat.
- Recorded WAV loop stems, tempo following, swing, dynamics and phrase counters remain unchanged.
- The audio path remains bounded, allocation-free, lock-free and I/O-free.
- Preserve all pre-existing dirty-worktree and approved rapid-tempo changes.
- Do not commit unless the user explicitly requests a commit.

---

### Task 1: Share the subdivision gate across synthesized instruments

**Files:**
- Modify: `Source/Percussion/GrooveEngine.h`
- Modify: `Source/Percussion/GrooveEngine.cpp`
- Modify: `Source/Percussion/PercussionEngine.h`
- Modify: `Source/Audio/VirtualPercussionEngine.cpp`
- Test: `Tests/TestAiBeat.cpp`

**Interfaces:**
- Consumes: `Subdivision` from `Source/Core/Types.h`.
- Produces: `GrooveEngine::setSubdivision(Subdivision) noexcept`.
- Produces: `PercussionEngine::setSubdivision(Subdivision) noexcept`.
- Produces: one internal `stepAllowedForSubdivision(int, Subdivision) noexcept` predicate.

- [ ] **Step 1: Add focused failing subdivision tests**

Add a helper in the groove-test section which collects only non-shaker events:

```cpp
auto congaSteps = [] (vp::GrooveStyle style, int bar, vp::Subdivision subdivision)
{
    vp::GrooveEngine groove;
    groove.prepare (0x51BD1u);
    groove.setStyle (style);
    groove.setHumanize (0.0f);
    groove.setSwing (0.0f);
    groove.setIntensity (0.0f);
    groove.setShakerEnabled (false);
    groove.setSubdivision (subdivision);

    std::vector<int> steps;
    for (int step = 0; step < vp::GrooveEngine::kStepsPerBar; ++step)
    {
        vp::GrooveEvent events[vp::GrooveEngine::kMaxEvents];
        const int count = groove.eventsAt (bar, step, events,
                                           vp::GrooveEngine::kMaxEvents);
        for (int i = 0; i < count; ++i)
            steps.push_back (step);
    }
    return steps;
};
```

Add exact assertions using the authored dance pattern:

```cpp
const auto dance16 = congaSteps (vp::GrooveStyle::dance, 0,
                                 vp::Subdivision::sixteenth);
const auto dance8 = congaSteps (vp::GrooveStyle::dance, 0,
                                vp::Subdivision::eighth);
const auto dance4 = congaSteps (vp::GrooveStyle::dance, 0,
                                vp::Subdivision::quarter);

expect (containsStep (dance16, 3) && containsStep (dance16, 11),
        "sixteenth subdivision preserves authored conga sixteenths");
expect (! containsOddStep (dance8)
            && containsStep (dance8, 2)
            && containsStep (dance8, 6)
            && containsStep (dance8, 14),
        "eighth subdivision keeps conga eighths and removes e/a sixteenths");
expect (allStepsAreQuarterGrid (dance4),
        "quarter subdivision keeps congas only on quarter steps");
```

For dance fill bar 7, assert odd steps 11, 13 and 15 exist at 1/16, are absent
at 1/8, and even steps 8, 10 and 14 remain at 1/8. Also instantiate identical
seeded grooves for AUTO and 1/8 and compare their event shapes step by step.

Add deterministic ghost coverage with intensity and humanize at 1 over at least
64 non-fill funk bars:

```cpp
expect (oddCongaCountAtEighth == 0,
        "eighth subdivision suppresses all odd-step conga ghosts");
expect (oddCongaCountAtSixteenth > 0,
        "sixteenth subdivision retains deterministic conga ghosts");
```

Add a shaker-only comparison proving 1/8 still removes odd shaker steps and
1/16 retains authored odd shaker steps.

- [ ] **Step 2: Build and capture the intended RED**

Run:

```bash
cmake --build build --target VPTests --config Release -j 8
```

Expected: compilation fails because `setSubdivision` does not exist. If a
temporary compatibility setter is used to isolate behavior, the conga/fill/
ghost assertions must fail against the current shaker-only gate.

- [ ] **Step 3: Add one shared bounded predicate**

In the anonymous namespace in `GrooveEngine.cpp`, add:

```cpp
bool stepAllowedForSubdivision (int step, Subdivision subdivision) noexcept
{
    if (subdivision == Subdivision::quarter)
        return (step % 4) == 0;
    if (subdivision == Subdivision::eighth
        || subdivision == Subdivision::autoDetect)
        return (step % 2) == 0;
    return true;
}
```

Rename the stored field and setter in `GrooveEngine`:

```cpp
void setSubdivision (Subdivision s) noexcept;
Subdivision subdivisionGrid = Subdivision::eighth;
```

Implement the setter:

```cpp
void GrooveEngine::setSubdivision (Subdivision s) noexcept
{
    subdivisionGrid =
        s == Subdivision::autoDetect ? Subdivision::eighth : s;
}
```

At the top of `eventsAt`, after validating `step`, compute:

```cpp
const bool subdivisionAllowsStep =
    stepAllowedForSubdivision (step, subdivisionGrid);
```

Use `subdivisionAllowsStep` in the shaker velocity gate. Change the conga
boundary to:

```cpp
if (congasOn && step != 0 && subdivisionAllowsStep)
```

This single boundary covers selected A/B/C/D tables, `spec.fill` and the ghost
block. Do not add a second downstream filter.

- [ ] **Step 4: Rename the forwarding API**

In `PercussionEngine.h`:

```cpp
void setSubdivision (Subdivision s) noexcept
{
    groove.setSubdivision (s);
}
```

In `VirtualPercussionEngine.cpp`, replace:

```cpp
percussion.setShakerSubdivision (tr.subdivision);
```

with:

```cpp
percussion.setSubdivision (tr.subdivision);
```

Search and remove all production references to `setShakerSubdivision` and
`shakerGrid`. Do not alter `BeatTracker::effectiveSubdivision`, the UI setting,
or clock pulses.

- [ ] **Step 5: Make authored-pattern tests explicit**

Existing tests that validate the complete style tables or compare fill density
must call:

```cpp
groove.setSubdivision (vp::Subdivision::sixteenth);
```

before collecting events. Do this only where the test intends to inspect the
authored full-resolution figure. Leave density/control tests at their stated
subdivision, and add assertions that explain each choice. Do not weaken style,
fill or no-conga-on-one expectations.

- [ ] **Step 6: Run focused and complete tests**

Run:

```bash
cmake --build build --target VPTests --config Release -j 8
./build/VPTests_artefacts/Release/VPTests
```

Expected: all new subdivision tests pass; existing groove/style/fill/density,
clock, rapid-tempo and loop tests remain green.

- [ ] **Step 7: Review Task 1**

Run:

```bash
rg "setShakerSubdivision|shakerGrid" Source Tests
git -c core.whitespace=cr-at-eol diff --check -- \
  Source/Percussion/GrooveEngine.h \
  Source/Percussion/GrooveEngine.cpp \
  Source/Percussion/PercussionEngine.h \
  Source/Audio/VirtualPercussionEngine.cpp \
  Tests/TestAiBeat.cpp
```

Expected: no stale shaker-only API names, no new whitespace errors, no clock or
recorded-loop changes, and no weakening of existing assertions.

---

### Task 2: Update the musical contract and run rendering gates

**Files:**
- Modify: `.claude/skills/percussion-patterns/SKILL.md`
- Verify: `docs/superpowers/specs/2026-09-02-shared-conga-subdivision-design.md`
- Verify: all Task 1 files

**Interfaces:**
- Consumes: the shared `setSubdivision` API and test evidence from Task 1.
- Produces: durable documentation that the setting governs shaker and synthesized congas.

- [ ] **Step 1: Update the percussion-patterns source of truth**

Replace shaker-only wording with:

```markdown
**`setSubdivision` thins, never adds.** `quarter` keeps steps 0/4/8/12,
`eighth` keeps even steps, `sixteenth` keeps all steps, and `autoDetect`
means eighths. The same predicate gates shaker, written congas, fills and
conga ghosts. The no-conga-on-step-0 guard still wins.
```

State explicitly that recorded loop stems do not pass through this event filter.
Do not change any unrelated style or timing guidance.

- [ ] **Step 2: Build rendering and timing probes**

Run:

```bash
cmake --build build --target VPRender VPTiming --config Release -j 8
./build/VPTiming_artefacts/Release/VPTiming
```

Expected: timing/attack checks remain within their existing thresholds.

- [ ] **Step 3: Render audible comparison artifacts**

Run:

```bash
./build/VPRender_artefacts/Release/VPRender \
  --style dance --bpm 124 --bars 8 --click --out /tmp/conga-subdivision-dance.wav
./build/VPRender_artefacts/Release/VPRender \
  --style marcha --bpm 110 --bars 8 --click --out /tmp/conga-subdivision-marcha.wav
```

Expected: both files render successfully with no hard voice steals. Record that
human listening remains a handoff item; do not claim an automated test listened.

- [ ] **Step 4: Run the repository regression script and app build**

Run:

```bash
./scripts/run-tests.sh
./scripts/build-simulator.sh
```

Expected: tests pass and the simulator reports `BUILD SUCCEEDED`. Diagnose any
failure before changing musical thresholds.

- [ ] **Step 5: Lint and audit final scope**

Use `ReadLints` on all Task 1 modified C++ files. Then run:

```bash
git diff --stat
git status --short
git -c core.whitespace=cr-at-eol diff --check -- \
  Source/Percussion/GrooveEngine.h \
  Source/Percussion/GrooveEngine.cpp \
  Source/Percussion/PercussionEngine.h \
  Source/Audio/VirtualPercussionEngine.cpp \
  Tests/TestAiBeat.cpp \
  .claude/skills/percussion-patterns/SKILL.md
```

Inspect the isolated feature diff for no recorded-loop, tempo-tracking, clock,
CASSA, style-table, swing, dynamics or phrase-counter changes.

- [ ] **Step 6: Final handoff**

Report exact test/build/probe results, files changed, subdivision behavior at
1/4, 1/8, 1/16 and AUTO, unchanged recorded-loop behavior, and paths to the two
rendered WAVs. Do not commit.
