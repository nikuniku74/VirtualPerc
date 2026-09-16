#include "TestTempoMotion.h"
#include "AI/TempoMotionTracker.h"

#include <cmath>
#include <cstdio>
#include <limits>

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

bool predictedBpmInRange (float bpm) noexcept
{
    return std::isfinite (bpm) && bpm >= 50.0f && bpm <= 190.0f;
}

vp::TempoMotionOutput feedAccelerando (vp::TempoMotionTracker& tracker,
                                        int beats,
                                        float committed = 100.0f,
                                        float bpmPerBeat = 0.35f) noexcept
{
    double t = 0.0;
    vp::TempoMotionOutput last {};
    for (int beat = 0; beat < beats; ++beat)
    {
        const float truth = committed + bpmPerBeat * static_cast<float> (beat);
        t += 60.0 / truth;
        last = tracker.observe (observation (t, committed, truth));
    }
    return last;
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
            const float shortFit =
                bpm + static_cast<float> (((beat * 5) % 9) - 4) * 0.12f;
            auto input = observation (t, bpm, shortFit, steps);
            if (isolatedOutlier != 0.0)
                input.shortFitResidual = 0.080f;
            const auto out = tracker.observe (input);
            silent &= out.authority == 0.0f;
            finite &= predictedBpmInRange (out.predictedBpm)
                   && std::isfinite (out.uncertainty);
        }
        expect (silent && finite, "fixed jitter never earns motion authority");
    }

    {
        vp::TempoMotionTracker ramp;
        double t = 0.0;
        float prevAuthority = 0.0f;
        bool authorityBounded = true;
        bool slopeNegative = false;
        bool predictsFaster = false;
        bool firstAuthorityProofClosed = true;
        for (int beat = 0; beat < 32; ++beat)
        {
            const float truth = 100.0f + 0.35f * static_cast<float> (beat);
            t += 60.0 / truth;
            const auto out = ramp.observe (observation (t, 100.0f, truth));
            if (out.authority > prevAuthority + 0.350001f)
                authorityBounded = false;
            if (out.authority > 0.0f && prevAuthority == 0.0f)
                firstAuthorityProofClosed = out.proofClosed;
            prevAuthority = out.authority;
            if (out.authority > 0.0f)
            {
                slopeNegative |= out.periodDeltaPerBeat < 0.0f;
                predictsFaster |= out.predictedBpm > 100.5f;
            }
        }
        const auto finalOut = ramp.output();
        expect (finalOut.authority > 0.0f
                    && slopeNegative
                    && predictsFaster
                    && authorityBounded
                    && firstAuthorityProofClosed,
                "a clean accelerando earns bounded authority");
    }

    {
        vp::TempoMotionTracker falling;
        double t = 0.0;
        float prevAuthority = 0.0f;
        bool authorityBounded = true;
        bool slopePositive = false;
        bool predictsSlower = false;
        for (int beat = 0; beat < 32; ++beat)
        {
            const float truth = 112.0f - 0.30f * static_cast<float> (beat);
            t += 60.0 / truth;
            const auto out =
                falling.observe (observation (t, 112.0f, truth));
            if (out.authority > prevAuthority + 0.350001f)
                authorityBounded = false;
            prevAuthority = out.authority;
            if (out.authority > 0.0f)
            {
                slopePositive |= out.periodDeltaPerBeat > 0.0f;
                predictsSlower |= out.predictedBpm < 111.0f;
            }
        }
        expect (falling.output().authority > 0.0f
                    && slopePositive
                    && predictsSlower
                    && authorityBounded,
                "a clean rallentando earns bounded authority");
    }

    vp::TempoMotionTracker missed;
    missed.observe (observation (0.0, 100.0f, 100.0f));
    const auto afterMiss =
        missed.observe (observation (1.2, 100.0f, 100.0f, 2));
    expect (std::fabs (afterMiss.predictedBpm - 100.0f) < 0.5f,
            "a two-quarter gap is normalized before fitting");

    {
        vp::TempoMotionTracker ramp;
        const auto active = feedAccelerando (ramp, 28);
        expect (active.authority > 0.0f, "accelerando activates before veto checks");

        auto veto = observation (30.0, 100.0f, 103.0f);
        veto.transitionState = vp::TempoTransitionState::suspected;
        const auto vetoOut = ramp.observe (veto);
        expect (vetoOut.authority == 0.0f
                    && vetoOut.state != vp::TempoMotionShadowState::active,
                "an abrupt transition vetoes the shadow");

        vp::TempoMotionTracker dropout;
        const auto live = feedAccelerando (dropout, 28);
        expect (live.authority > 0.0f, "dropout case starts from active shadow");
        dropout.reset (false, vp::TempoMotionVeto::discontinuity);
        const auto partial = dropout.output();
        expect (partial.authority == 0.0f
                    && partial.periodDeltaPerBeat == 0.0f
                    && partial.state != vp::TempoMotionShadowState::active
                    && predictedBpmInRange (partial.predictedBpm),
                "a dropout clears velocity authority");
    }

    {
        vp::TempoMotionTracker falling;
        feedAccelerando (falling, 28, 112.0f, -0.30f);
        expect (falling.output().authority > 0.0f,
                "rallentando activates before full reset check");
        falling.reset (true, vp::TempoMotionVeto::octaveOrGrid);
        const auto cleared = falling.output();
        expect (cleared.authority == 0.0f
                    && cleared.periodDeltaPerBeat == 0.0f
                    && cleared.predictedBpm == 0.0f
                    && cleared.state == vp::TempoMotionShadowState::idle,
                "a grid rebuild clears the complete shadow model");
    }

    {
        vp::TempoMotionTracker contradiction;
        const auto active = feedAccelerando (contradiction, 30);
        expect (active.authority > 0.0f,
                "contradiction case starts from active shadow");

        double t = 0.0;
        for (int beat = 0; beat < 30; ++beat)
            t += 60.0 / (100.0f + 0.35f * static_cast<float> (beat));
        t += 60.0f / 110.0f;
        auto oppose = observation (t, 100.0f, 92.0f);
        oppose.shortFitResidual = 0.008f;
        const auto out = contradiction.observe (oppose);
        expect (out.authority == 0.0f
                    && out.state != vp::TempoMotionShadowState::active,
                "contradicting short-fit clears proof and authority");
    }

    {
        vp::TempoMotionTracker linear;
        double t = 0.0;
        const float startBpm = 100.0f;
        const float endBpm = 111.0f;
        const int beats = 12;
        bool highConfidence = false;
        for (int beat = 0; beat < beats; ++beat)
        {
            const float truth =
                startBpm + (endBpm - startBpm) * static_cast<float> (beat)
                          / static_cast<float> (beats - 1);
            t += 60.0 / truth;
            const auto out = linear.observe (observation (t, startBpm, truth));
            if (beat == beats - 1)
                highConfidence = out.uncertainty < 0.05f && out.authority > 0.0f;
        }
        expect (highConfidence,
                "a perfect-linear ramp yields high rate confidence");
    }

    {
        vp::TempoMotionTracker tracker;
        feedAccelerando (tracker, 10);
        vp::TempoMotionObservation bad {};
        bad.beatTimeSec = std::numeric_limits<double>::quiet_NaN();
        bad.committedBpm = 100.0f;
        bad.shortFitBpm = 105.0f;
        bad.gridQuarterSteps = 1;
        bad.lineFeed = true;
        bad.fixedRegime = true;
        const auto out = tracker.observe (bad);
        expect (predictedBpmInRange (out.predictedBpm)
                    && out.authority == 0.0f,
                "bad observations publish finite inactive diagnostics");
    }
}
