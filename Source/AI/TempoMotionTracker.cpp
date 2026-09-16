#include "AI/TempoMotionTracker.h"

#include <algorithm>
#include <cmath>
#include <cstring>
namespace vp
{
namespace
{
constexpr int kMinimumSamples = 5;
constexpr int kProofBeats = 3;
constexpr float kMinimumCoverage = 0.75f;
constexpr float kMaximumShortResidual = 0.035f;
constexpr float kMaximumIndexGapError = 0.15f;
constexpr float kMinimumRateZ = 4.0f;
constexpr float kMinimumWindowDisplacement = 0.006f;
constexpr float kAuthorityPerBeat = 0.35f;
constexpr float kPeriodNoiseFloor = 0.0015f;

constexpr float kMinPeriodSec = 60.0f / 190.0f;
constexpr float kMaxPeriodSec = 60.0f / 50.0f;
constexpr float kInfiniteRateZ = 1.0e6f;

void sortInPlace (float* values, int count) noexcept
{
    for (int i = 1; i < count; ++i)
    {
        const float key = values[i];
        int j = i - 1;
        while (j >= 0 && values[j] > key)
        {
            values[j + 1] = values[j];
            --j;
        }
        values[j + 1] = key;
    }
}

float medianOf (float* values, int count) noexcept
{
    if (count <= 0)
        return 0.0f;
    sortInPlace (values, count);
    const int mid = count / 2;
    if ((count & 1) != 0)
        return values[mid];
    return 0.5f * (values[mid - 1] + values[mid]);
}

float madOf (const float* values, int count, float med) noexcept
{
    if (count <= 0)
        return 0.0f;
    float deviations[12];
    for (int i = 0; i < count; ++i)
        deviations[i] = std::fabs (values[i] - med);
    return medianOf (deviations, count);
}

int signOf (float value) noexcept
{
    if (value > 0.0f)
        return 1;
    if (value < 0.0f)
        return -1;
    return 0;
}

float finitePredictedBpm (float bpm) noexcept
{
    if (! std::isfinite (bpm) || bpm < 50.0f || bpm > 190.0f)
        return 0.0f;
    return bpm;
}
} // namespace

void TempoMotionTracker::reset (bool fullModelReset, TempoMotionVeto reason) noexcept
{
    authority = 0.0f;
    proofBeats = 0;
    direction = 0;
    modelVelocity = 0.0f;

    if (fullModelReset)
    {
        periods.fill (0.0f);
        write = 0;
        filled = 0;
        lastBeatTimeSec = -1.0;
        modelPeriodSec = 0.0f;
        lastOutput = {};
        lastOutput.veto = reason;
        lastOutput.state = TempoMotionShadowState::idle;
        return;
    }

    lastOutput.authority = 0.0f;
    lastOutput.proofClosed = false;
    lastOutput.periodDeltaPerBeat = 0.0f;
    lastOutput.uncertainty = 1.0f;
    lastOutput.veto = reason;
    lastOutput.state = filled > 0 ? TempoMotionShadowState::proving
                                  : TempoMotionShadowState::idle;
    if (modelPeriodSec >= kMinPeriodSec && modelPeriodSec <= kMaxPeriodSec)
        lastOutput.predictedBpm = finitePredictedBpm (60.0f / modelPeriodSec);
    else
        lastOutput.predictedBpm = 0.0f;
}

TempoMotionOutput TempoMotionTracker::observe (const TempoMotionObservation& o) noexcept
{
    const float previousAuthority = authority;

    auto publishVeto = [&] (TempoMotionVeto reason) noexcept
    {
        authority = 0.0f;
        proofBeats = 0;
        direction = 0;
        lastOutput.veto = reason;
        lastOutput.state = TempoMotionShadowState::vetoed;
        lastOutput.authority = 0.0f;
        lastOutput.proofClosed = false;
        lastOutput.periodDeltaPerBeat = 0.0f;
        lastOutput.uncertainty = 1.0f;
        lastOutput.predictedBpm = finitePredictedBpm (o.committedBpm);
        return lastOutput;
    };

    if (! std::isfinite (o.beatTimeSec) || ! std::isfinite (o.committedBpm)
        || ! std::isfinite (o.shortFitBpm)
        || o.gridQuarterSteps < 1 || o.committedBpm < 50.0f || o.committedBpm > 190.0f)
        return publishVeto (TempoMotionVeto::badObservation);

    if (o.transitionState != TempoTransitionState::stable || o.transitionRefitBeats != 0)
        return publishVeto (TempoMotionVeto::transition);

    if (! o.lineFeed)
        return publishVeto (TempoMotionVeto::notDirect);

    if (lastBeatTimeSec < 0.0)
    {
        lastBeatTimeSec = o.beatTimeSec;
        lastOutput = {};
        lastOutput.predictedBpm = finitePredictedBpm (o.committedBpm);
        lastOutput.state = TempoMotionShadowState::idle;
        return lastOutput;
    }

    const double elapsed = o.beatTimeSec - lastBeatTimeSec;
    lastBeatTimeSec = o.beatTimeSec;

    if (elapsed <= 0.0 || ! std::isfinite (elapsed))
        return publishVeto (TempoMotionVeto::badObservation);

    const float period = static_cast<float> (elapsed / static_cast<double> (o.gridQuarterSteps));
    if (! std::isfinite (period) || period < kMinPeriodSec || period > kMaxPeriodSec)
        return publishVeto (TempoMotionVeto::badObservation);

    periods[static_cast<size_t> (write)] = period;
    write = (write + 1) % kWindow;
    filled = std::min (filled + 1, kWindow);

    lastOutput.veto = TempoMotionVeto::none;

    const int count = filled;
    float chronological[ kWindow ];
    for (int age = 0; age < count; ++age)
    {
        const int index = (write - 1 - age + kWindow * 2) % kWindow;
        chronological[count - 1 - age] = periods[static_cast<size_t> (index)];
    }

    float scratch[ kWindow ];
    std::memcpy (scratch, chronological, static_cast<size_t> (count) * sizeof (float));
    const float rawMedian = medianOf (scratch, count);
    const float mad = madOf (chronological, count, rawMedian);
    const float committedPeriod = 60.0f / std::max (50.0f, o.committedBpm);
    const float noiseScale =
        committedPeriod * std::max (kPeriodNoiseFloor, o.intervalJitter);
    const float winsorHalfWidth = 3.0f * std::max (mad, noiseScale);

    float winsorized[ kWindow ];
    for (int i = 0; i < count; ++i)
    {
        winsorized[i] = std::clamp (chronological[i],
                                    rawMedian - winsorHalfWidth,
                                    rawMedian + winsorHalfWidth);
    }

    float intercept = rawMedian;
    float slope = 0.0f;
    float rateZ = 0.0f;
    float displacement = 0.0f;

    if (count >= 2)
    {
        float meanX = 0.0f;
        float meanY = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            meanX += static_cast<float> (i);
            meanY += winsorized[i];
        }
        meanX /= static_cast<float> (count);
        meanY /= static_cast<float> (count);

        float sxx = 0.0f;
        float sxy = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            const float dx = static_cast<float> (i) - meanX;
            sxx += dx * dx;
            sxy += dx * (winsorized[i] - meanY);
        }

        if (sxx > 1.0e-12f)
        {
            slope = sxy / sxx;
            intercept = meanY - slope * meanX;

            float sse = 0.0f;
            for (int i = 0; i < count; ++i)
            {
                const float predicted = intercept + slope * static_cast<float> (i);
                const float residual = winsorized[i] - predicted;
                sse += residual * residual;
            }

            const float mse = sse / static_cast<float> (std::max (1, count - 2));
            const float slopeSe = std::sqrt (std::max (0.0f, mse / sxx));
            if (std::fabs (slope) <= 1.0e-12f)
                rateZ = 0.0f;
            else if (slopeSe <= 1.0e-12f)
                rateZ = kInfiniteRateZ;
            else
                rateZ = std::fabs (slope) / slopeSe;

            const float startPeriod = intercept;
            const float endPeriod = intercept + slope * static_cast<float> (count - 1);
            const float denom = std::max (1.0e-6f, std::fabs (startPeriod));
            displacement = std::fabs (endPeriod - startPeriod) / denom;
        }
    }

    modelPeriodSec = intercept + slope * static_cast<float> (count - 1);
    modelVelocity = slope;

    const float committedPeriodSec = 60.0f / o.committedBpm;
    const float shortPeriodSec = 60.0f / std::max (50.0f, o.shortFitBpm);
    const float fitPeriodDelta = shortPeriodSec - committedPeriodSec;
    const int fitSign = signOf (fitPeriodDelta);
    const int slopeSign = signOf (slope);
    const bool shortFitContradictsSlope =
        fitSign != 0 && slopeSign != 0 && fitSign != slopeSign;

    const bool indexGapOk =
        std::fabs (o.fitIndexGap - 1.0f) <= kMaximumIndexGapError;
    const bool directFixed = o.lineFeed && o.fixedRegime;
    const bool qualityOk = count >= kMinimumSamples
                        && directFixed
                        && o.fitCoverage >= kMinimumCoverage
                        && o.shortFitResidual <= kMaximumShortResidual
                        && indexGapOk
                        && rateZ >= kMinimumRateZ
                        && displacement >= kMinimumWindowDisplacement
                        && slopeSign != 0
                        && slopeSign == fitSign;

    if (shortFitContradictsSlope)
    {
        authority = 0.0f;
        proofBeats = 0;
        direction = 0;
    }
    else if (qualityOk)
    {
        if (direction == 0 || direction == slopeSign)
        {
            direction = slopeSign;
            proofBeats = std::min (proofBeats + 1, kProofBeats);
        }
        else
        {
            direction = slopeSign;
            proofBeats = 1;
            authority = 0.0f;
        }

        if (proofBeats >= kProofBeats)
            authority = std::min (1.0f, authority + kAuthorityPerBeat);
    }
    else
    {
        if (proofBeats > 0 || authority > 0.0f)
        {
            if (slopeSign != 0 && direction != 0 && slopeSign != direction)
                authority = 0.0f;
        }
        proofBeats = 0;
        direction = 0;
    }

    const float predictedPeriod =
        std::clamp (intercept + slope * static_cast<float> (count),
                    kMinPeriodSec, kMaxPeriodSec);
    lastOutput.predictedBpm = finitePredictedBpm (60.0f / predictedPeriod);
    lastOutput.periodDeltaPerBeat =
        (authority > 0.0f && ! shortFitContradictsSlope) ? slope : 0.0f;
    lastOutput.uncertainty =
        std::clamp (4.0f / std::max (4.0f, rateZ), 0.0f, 1.0f);
    lastOutput.authority = authority;
    lastOutput.proofClosed = previousAuthority == 0.0f && authority > 0.0f;
    lastOutput.state = authority > 0.0f ? TempoMotionShadowState::active
                                        : TempoMotionShadowState::proving;

    return lastOutput;
}

} // namespace vp
