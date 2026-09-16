#include "TestTempoMotion.h"
#include "AI/BeatDecoder.h"
#include "AI/TempoMotionTracker.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
constexpr float kDyadicBasePeriodSec = 0.5f;
constexpr float kDyadicPeriodSlopeSec = -1.0f / 256.0f;
constexpr float kDyadicCommittedBpm = 100.0f;

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

bool outputFiniteInactiveZeroPrediction (const vp::TempoMotionOutput& out) noexcept
{
    return std::isfinite (out.predictedBpm)
        && std::isfinite (out.periodDeltaPerBeat)
        && std::isfinite (out.uncertainty)
        && std::isfinite (out.authority)
        && out.authority == 0.0f
        && out.predictedBpm == 0.0f;
}

float dyadicPeriodSec (int intervalIndex) noexcept
{
    return kDyadicBasePeriodSec + kDyadicPeriodSlopeSec * static_cast<float> (intervalIndex);
}

struct DyadicPeriodFeedResult
{
    vp::TempoMotionOutput last {};
    double lastTimeSec = 0.0;
    int intervalsFed = 0;
};

DyadicPeriodFeedResult feedDyadicLinearPeriods (vp::TempoMotionTracker& tracker,
                                                int intervalCount) noexcept
{
    DyadicPeriodFeedResult result {};
    result.lastTimeSec = 0.0;
    result.last = tracker.observe (
        observation (result.lastTimeSec, kDyadicCommittedBpm, kDyadicCommittedBpm));

    for (int n = 1; n < intervalCount; ++n)
    {
        const float periodSec = dyadicPeriodSec (n - 1);
        result.lastTimeSec += static_cast<double> (periodSec);
        result.last = tracker.observe (
            observation (result.lastTimeSec,
                         kDyadicCommittedBpm,
                         60.0f / periodSec));
        result.intervalsFed = n;
    }

    return result;
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
        bool provingShowsDelta = false;
        for (int beat = 0; beat < 32; ++beat)
        {
            const float truth = 100.0f + 0.35f * static_cast<float> (beat);
            t += 60.0 / truth;
            const auto out = ramp.observe (observation (t, 100.0f, truth));
            if (out.authority > prevAuthority + 0.350001f)
                authorityBounded = false;
            if (out.authority > 0.0f && prevAuthority == 0.0f)
                firstAuthorityProofClosed = out.proofClosed;
            if (out.authority == 0.0f
                && out.state == vp::TempoMotionShadowState::proving
                && out.periodDeltaPerBeat != 0.0f)
                provingShowsDelta = true;
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
                    && firstAuthorityProofClosed
                    && provingShowsDelta,
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
        vp::TempoMotionTracker dropout;
        dropout.observe (observation (0.0, 120.0f, 120.0f));
        dropout.observe (observation (0.5, 120.0f, 120.0f));
        dropout.reset (false, vp::TempoMotionVeto::discontinuity);

        const auto reanchored =
            dropout.observe (observation (20.0, 120.0f, 120.0f));
        const auto resumed =
            dropout.observe (observation (20.5, 120.0f, 120.0f));
        expect (reanchored.veto == vp::TempoMotionVeto::none
                    && reanchored.state == vp::TempoMotionShadowState::idle
                    && resumed.veto == vp::TempoMotionVeto::none
                    && std::fabs (resumed.predictedBpm - 120.0f) < 0.5f,
                "a partial discontinuity reset invalidates the time anchor");
    }

    {
        auto recoversAcrossValidVeto =
            [] (vp::TempoTransitionState transitionState,
                int refitBeats,
                bool lineFeed,
                vp::TempoMotionVeto expectedVeto)
        {
            vp::TempoMotionTracker tracker;
            tracker.observe (observation (0.0, 120.0f, 120.0f));
            tracker.observe (observation (0.5, 120.0f, 120.0f));

            auto vetoed = observation (1.0, 120.0f, 120.0f);
            vetoed.transitionState = transitionState;
            vetoed.transitionRefitBeats = refitBeats;
            vetoed.lineFeed = lineFeed;
            const auto boundary = tracker.observe (vetoed);
            const auto resumed =
                tracker.observe (observation (1.5, 120.0f, 120.0f));
            return boundary.veto == expectedVeto
                && resumed.veto == vp::TempoMotionVeto::none
                && std::fabs (resumed.predictedBpm - 120.0f) < 0.5f;
        };

        expect (recoversAcrossValidVeto (vp::TempoTransitionState::suspected,
                                         0,
                                         true,
                                         vp::TempoMotionVeto::transition),
                "a transition-vetoed beat advances the time anchor");
        expect (recoversAcrossValidVeto (vp::TempoTransitionState::stable,
                                         2,
                                         true,
                                         vp::TempoMotionVeto::transition),
                "a refit-vetoed beat advances the time anchor");
        expect (recoversAcrossValidVeto (vp::TempoTransitionState::stable,
                                         0,
                                         false,
                                         vp::TempoMotionVeto::notDirect),
                "a direct-path veto advances the time anchor");
    }

    {
        vp::TempoMotionTracker tracker;
        tracker.observe (observation (0.0, 120.0f, 120.0f));
        tracker.observe (observation (0.5, 120.0f, 120.0f));
        auto bad = observation (
            std::numeric_limits<double>::quiet_NaN (), 120.0f, 120.0f);
        const auto vetoed = tracker.observe (bad);
        const auto resumed =
            tracker.observe (observation (1.0, 120.0f, 120.0f));
        expect (vetoed.veto == vp::TempoMotionVeto::badObservation
                    && vetoed.authority == 0.0f
                    && predictedBpmInRange (vetoed.predictedBpm)
                    && resumed.veto == vp::TempoMotionVeto::none
                    && std::fabs (resumed.predictedBpm - 120.0f) < 0.5f,
                "a non-finite observation is vetoed without poisoning the anchor");
    }

    {
        auto rejectsWithoutMovingAnchor =
            [] (double invalidTime, double resumeTime, int resumeSteps)
        {
            vp::TempoMotionTracker tracker;
            tracker.observe (observation (0.0, 120.0f, 120.0f));
            tracker.observe (observation (0.5, 120.0f, 120.0f));

            const auto rejected =
                tracker.observe (observation (invalidTime, 120.0f, 120.0f));
            const auto resumed =
                tracker.observe (
                    observation (resumeTime, 120.0f, 120.0f, resumeSteps));
            return rejected.veto == vp::TempoMotionVeto::badObservation
                && resumed.veto == vp::TempoMotionVeto::none
                && std::fabs (resumed.predictedBpm - 120.0f) < 0.5f
                && resumed.periodDeltaPerBeat == 0.0f
                && resumed.authority == 0.0f;
        };

        expect (rejectsWithoutMovingAnchor (0.25, 1.0, 1),
                "a non-monotonic stable beat cannot move the time anchor");
        expect (rejectsWithoutMovingAnchor (0.60, 1.0, 1),
                "an implausibly short stable period cannot move the time anchor");
        expect (rejectsWithoutMovingAnchor (2.0, 2.5, 4),
                "an implausibly long stable period cannot move the time anchor");
    }

    {
        vp::TempoMotionTracker tracker;
        tracker.observe (observation (0.0, 120.0f, 120.0f));
        tracker.observe (observation (0.5, 120.0f, 120.0f));
        auto invalidTransition = observation (0.25, 120.0f, 120.0f);
        invalidTransition.transitionState = vp::TempoTransitionState::suspected;
        const auto rejected = tracker.observe (invalidTransition);
        const auto resumed =
            tracker.observe (observation (1.0, 120.0f, 120.0f));
        expect (rejected.veto == vp::TempoMotionVeto::badObservation
                    && resumed.veto == vp::TempoMotionVeto::none
                    && std::fabs (resumed.predictedBpm - 120.0f) < 0.5f
                    && resumed.periodDeltaPerBeat == 0.0f,
                "an invalid transition timestamp cannot move the time anchor");
    }

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
        vp::TempoMotionTracker live;
        const auto fed = feedDyadicLinearPeriods (live, 14);
        expect (live.output().state == vp::TempoMotionShadowState::active
                    && live.output().authority >= 0.999f,
                "dyadic linear period ramp reaches active authority");

        const float nextPeriod = dyadicPeriodSec (fed.intervalsFed);
        const double revokeTime = fed.lastTimeSec + static_cast<double> (nextPeriod);
        auto revoke = observation (revokeTime,
                                   kDyadicCommittedBpm,
                                   60.0f / nextPeriod);
        revoke.fixedRegime = false;
        const auto out = live.observe (revoke);
        expect (out.authority == 0.0f
                    && out.periodDeltaPerBeat == 0.0f
                    && out.state != vp::TempoMotionShadowState::active,
                "losing fixed eligibility revokes authority immediately");
    }

    {
        vp::TempoMotionTracker zeroSe;
        const auto fed = feedDyadicLinearPeriods (zeroSe, 14);
        expect (fed.last.authority > 0.0f
                    && fed.last.uncertainty <= 4.1e-6f,
                "dyadic linear period hits infinite rate confidence");
    }

    {
        vp::TempoMotionTracker proof;
        double t = 0.0;
        proof.observe (observation (t, kDyadicCommittedBpm, kDyadicCommittedBpm));
        int qualifyingIndex = 0;
        float prevAuthority = 0.0f;
        bool proofTimingOk = true;
        for (int n = 1; n < 12; ++n)
        {
            const float periodSec = dyadicPeriodSec (n - 1);
            t += static_cast<double> (periodSec);
            const auto out = proof.observe (
                observation (t, kDyadicCommittedBpm, 60.0f / periodSec));
            if (n >= 5)
            {
                ++qualifyingIndex;
                if (qualifyingIndex < 3)
                    proofTimingOk &= out.authority == 0.0f;
                else if (qualifyingIndex == 3)
                    proofTimingOk &= out.authority > 0.0f && out.authority <= 0.350001f;
                if (out.authority > prevAuthority + 0.350001f)
                    proofTimingOk = false;
            }
            prevAuthority = out.authority;
        }
        expect (proofTimingOk,
                "dyadic linear period proof waits exactly three qualifying beats");
    }

    {
        bool allBadCommittedOk = true;
        for (const float badCommitted : { std::numeric_limits<float>::quiet_NaN (),
                                            std::numeric_limits<float>::infinity (),
                                            49.0f,
                                            191.0f,
                                            240.0f })
        {
            vp::TempoMotionTracker tracker;
            tracker.observe (observation (0.0, 100.0f, 100.0f));
            auto bad = observation (0.5, 100.0f, 100.0f);
            bad.committedBpm = badCommitted;
            const auto out = tracker.observe (bad);
            allBadCommittedOk &= outputFiniteInactiveZeroPrediction (out);
        }
        expect (allBadCommittedOk,
                "bad committed tempo publishes finite inactive diagnostics");
    }

    {
        constexpr double fps = 50.0;
        constexpr int framesPerBeat = 30; // 100 BPM at 50 analysis frames/s.
        vp::BeatDecoder decoder;
        decoder.prepare (fps);
        decoder.setLineFeed (true);

        bool fixedSilent = true;
        bool sawValid = false;
        bool sawAdvancingBeatSerial = false;
        bool sawProvingFixedShadow = false;
        int fixedFrames = 0;
        uint32_t previousBeatSerial = 0;
        for (int frame = 0; frame < static_cast<int> (90.0 * fps); ++frame)
        {
            const float activation = (frame % framesPerBeat) == 0 ? 0.95f : 0.02f;
            const auto hypothesis =
                decoder.observe (activation, 0.02f, 1.0f - activation);
            const auto diagnostics = decoder.diagnostics();
            fixedSilent &= diagnostics.motionShadowAuthority == 0.0f;
            sawValid |= hypothesis.valid;
            sawAdvancingBeatSerial |= hypothesis.beatSerial > previousBeatSerial;
            previousBeatSerial = hypothesis.beatSerial;
            if (hypothesis.regime == vp::TempoRegime::fixed)
            {
                ++fixedFrames;
                sawProvingFixedShadow |=
                    diagnostics.motionShadowState
                        == static_cast<int> (vp::TempoMotionShadowState::proving);
            }
        }
        expect (sawValid && sawAdvancingBeatSerial && previousBeatSerial > 100,
                "fixed decoder fixture publishes valid advancing beats");
        expect (fixedFrames > static_cast<int> (30.0 * fps),
                "fixed decoder fixture spends meaningful time fixed");
        expect (sawProvingFixedShadow,
                "fixed decoder fixture exercises a proving shadow");
        expect (fixedSilent, "fixed decoder never publishes motion authority");

        decoder.setUserOctave (1);
        expect (decoder.diagnostics().motionShadowVeto
                    == static_cast<int> (vp::TempoMotionVeto::octaveOrGrid),
                "explicit octave reset reason survives regime exit");
    }
}
