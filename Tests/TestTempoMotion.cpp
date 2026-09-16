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
