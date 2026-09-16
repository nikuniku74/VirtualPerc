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
constexpr int kShapeQuarantineBeats = 12;

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
    shapePoints.fill ({});
    shapeFilled = 0;
    shapeEntryTimeSec = -1.0;
    shapeQuarterIndex = 0.0;
    shapeEntryPeriodSec = 0.0f;
    strictProofSeen = false;
    shapeHingeActive = false;
    shapeQuadraticWins = 0;
    shapeQuarantineBeats = 0;

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
    lastOutput.firstStrictProof = false;
    lastOutput.periodDeltaPerBeat = 0.0f;
    lastOutput.uncertainty = 1.0f;
    lastOutput.veto = reason;
    lastOutput.state = filled > 0 ? TempoMotionShadowState::proving
                                  : TempoMotionShadowState::idle;
    lastOutput.shapeModel = TempoMotionShapeModel::insufficient;
    lastOutput.shapePredictedBpm = 0.0f;
    lastOutput.shapeQuadraticVsHinge = 0.0f;
    lastOutput.shapeEvidenceMargin = 0.0f;
    lastOutput.shapeQuadraticWins = 0;
    lastOutput.shapeQuarantineBeats = 0;
    // A discontinuity or transition leaves the bounded period model available
    // for diagnostics, but there is no measurable interval across the boundary.
    // The next valid beat is an anchor, not a period sample.
    lastBeatTimeSec = -1.0;
    if (modelPeriodSec >= kMinPeriodSec && modelPeriodSec <= kMaxPeriodSec)
        lastOutput.predictedBpm = finitePredictedBpm (60.0f / modelPeriodSec);
    else
        lastOutput.predictedBpm = 0.0f;
}

void TempoMotionTracker::beginFixedTenure (double entryTimeSec,
                                           float committedBpm) noexcept
{
    reset (true, TempoMotionVeto::none);
    if (! std::isfinite (entryTimeSec) || entryTimeSec < 0.0
        || ! std::isfinite (committedBpm)
        || committedBpm < 50.0f || committedBpm > 190.0f)
    {
        lastOutput.state = TempoMotionShadowState::vetoed;
        lastOutput.veto = TempoMotionVeto::badObservation;
        return;
    }

    shapeEntryTimeSec = entryTimeSec;
    shapeEntryPeriodSec = 60.0f / committedBpm;
    shapeQuarterIndex = 0.0;
    shapePoints[0] = { 0.0, 0.0 };
    shapeFilled = 1;
    // The entry beat seeds only the cumulative residual curve. The scalar
    // tracker still needs the next accepted beat as its time anchor, so no
    // interval measured before this fixed tenure can leak into its proof.
    lastBeatTimeSec = -1.0;
    lastOutput.predictedBpm = committedBpm;
}

void TempoMotionTracker::quarantineShape() noexcept
{
    authority = 0.0f;
    proofBeats = 0;
    direction = 0;
    shapeQuadraticWins = 0;
    shapeQuarantineBeats = kShapeQuarantineBeats;
    lastOutput.authority = 0.0f;
    lastOutput.proofClosed = false;
    lastOutput.firstStrictProof = false;
    lastOutput.shapeQuadraticWins = 0;
    lastOutput.shapeQuarantineBeats = shapeQuarantineBeats;
}

TempoMotionOutput TempoMotionTracker::observe (const TempoMotionObservation& o) noexcept
{
    const float previousAuthority = authority;
    lastOutput.firstStrictProof = false;

    auto publishVeto = [&] (TempoMotionVeto reason) noexcept
    {
        authority = 0.0f;
        proofBeats = 0;
        direction = 0;
        lastOutput.veto = reason;
        lastOutput.state = TempoMotionShadowState::vetoed;
        lastOutput.authority = 0.0f;
        lastOutput.proofClosed = false;
        lastOutput.firstStrictProof = false;
        lastOutput.periodDeltaPerBeat = 0.0f;
        lastOutput.uncertainty = 1.0f;
        lastOutput.predictedBpm = finitePredictedBpm (o.committedBpm);
        shapeQuadraticWins = 0;
        lastOutput.shapeQuadraticWins = 0;
        if (reason == TempoMotionVeto::transition)
        {
            shapeQuarantineBeats = kShapeQuarantineBeats;
            lastOutput.shapeQuarantineBeats = shapeQuarantineBeats;
        }
        return lastOutput;
    };

    if (! std::isfinite (o.beatTimeSec) || ! std::isfinite (o.committedBpm)
        || ! std::isfinite (o.shortFitBpm)
        || o.beatTimeSec < 0.0 || o.gridQuarterSteps < 1
        || o.committedBpm < 50.0f || o.committedBpm > 190.0f)
        return publishVeto (TempoMotionVeto::badObservation);

    auto publishAnchoredVeto = [&] (TempoMotionVeto reason) noexcept
    {
        // These are accepted beats whose tempo evidence is temporarily
        // ineligible. They still delimit the next measurable interval. Leaving
        // the anchor behind made the following one-step sample span two beats
        // and read 120 BPM as 60 after a single vetoed beat.
        if (lastBeatTimeSec >= 0.0 && o.beatTimeSec <= lastBeatTimeSec)
            return publishVeto (TempoMotionVeto::badObservation);
        lastBeatTimeSec = o.beatTimeSec;
        return publishVeto (reason);
    };

    if (o.transitionState != TempoTransitionState::stable || o.transitionRefitBeats != 0)
        return publishAnchoredVeto (TempoMotionVeto::transition);

    if (! o.lineFeed)
        return publishAnchoredVeto (TempoMotionVeto::notDirect);

    if (o.fixedRegime)
    {
        if (shapeEntryTimeSec < 0.0 || shapeFilled <= 0
            || ! std::isfinite (shapeEntryPeriodSec)
            || shapeEntryPeriodSec <= 0.0f)
        {
            shapePoints.fill ({});
            shapeEntryTimeSec = o.beatTimeSec;
            shapeEntryPeriodSec = 60.0f / o.committedBpm;
            shapeQuarterIndex = 0.0;
            shapePoints[0] = { 0.0, 0.0 };
            shapeFilled = 1;
            shapeHingeActive = false;
            shapeQuadraticWins = 0;
            shapeQuarantineBeats = 0;
            lastOutput.shapeModel = TempoMotionShapeModel::insufficient;
            lastOutput.shapePredictedBpm = 0.0f;
            lastOutput.shapeQuadraticVsHinge = 0.0f;
            lastOutput.shapeEvidenceMargin = 0.0f;
            lastOutput.shapeQuadraticWins = 0;
            lastOutput.shapeQuarantineBeats = 0;
        }
        else
        {
            if (shapeQuarantineBeats > 0)
                --shapeQuarantineBeats;

            shapeQuarterIndex += static_cast<double> (o.gridQuarterSteps);
            const double residual =
                o.beatTimeSec - shapeEntryTimeSec
                - shapeQuarterIndex * static_cast<double> (shapeEntryPeriodSec);
            if (! std::isfinite (shapeQuarterIndex) || ! std::isfinite (residual))
                return publishVeto (TempoMotionVeto::badObservation);

            if (shapeFilled == TempoMotionShape::kMaximumPoints)
            {
                for (int i = 1; i < TempoMotionShape::kMaximumPoints; ++i)
                    shapePoints[static_cast<size_t> (i - 1)] =
                        shapePoints[static_cast<size_t> (i)];
                --shapeFilled;
            }
            shapePoints[static_cast<size_t> (shapeFilled++)] = {
                shapeQuarterIndex, residual
            };

            if (shapeFilled < TempoMotionShape::kMinimumPoints)
            {
                lastOutput.shapeModel = TempoMotionShapeModel::insufficient;
                lastOutput.shapePredictedBpm = 0.0f;
                lastOutput.shapeQuadraticVsHinge = 0.0f;
                lastOutput.shapeEvidenceMargin = 0.0f;
                lastOutput.shapeQuadraticWins = 0;
                lastOutput.shapeQuarantineBeats = shapeQuarantineBeats;
            }
            else
            {
                const auto shape = TempoMotionShape::classify (shapePoints, shapeFilled);
                lastOutput.shapeModel = shape.model;
                lastOutput.shapeQuadraticVsHinge =
                    static_cast<float> (shape.quadraticVsHinge);
                lastOutput.shapeEvidenceMargin =
                    static_cast<float> (shape.evidenceMargin);

                if (! shape.finite || shape.model == TempoMotionShapeModel::invalid
                    || ! std::isfinite (shape.nextPeriodDeltaSec))
                {
                    authority = 0.0f;
                    shapeQuadraticWins = 0;
                    lastOutput.shapePredictedBpm = 0.0f;
                    lastOutput.shapeQuadraticVsHinge = 0.0f;
                    lastOutput.shapeEvidenceMargin = 0.0f;
                }
                else
                {
                    const float shapePeriod = std::clamp (
                        shapeEntryPeriodSec
                            + static_cast<float> (shape.nextPeriodDeltaSec),
                        kMinPeriodSec, kMaxPeriodSec);
                    lastOutput.shapePredictedBpm =
                        finitePredictedBpm (60.0f / shapePeriod);

                    if (shape.model == TempoMotionShapeModel::hinge)
                    {
                        shapeHingeActive = true;
                        shapeQuadraticWins = 0;
                        shapeQuarantineBeats = kShapeQuarantineBeats;
                        authority = 0.0f;
                        proofBeats = 0;
                        direction = 0;
                    }
                    else if (shape.model == TempoMotionShapeModel::quadratic
                             && ! shapeHingeActive
                             && shapeQuarantineBeats == 0)
                    {
                        shapeQuadraticWins = std::min (shapeQuadraticWins + 1, 2);
                    }
                    else
                    {
                        shapeQuadraticWins = 0;
                    }
                }
                lastOutput.shapeQuadraticWins = shapeQuadraticWins;
                lastOutput.shapeQuarantineBeats = shapeQuarantineBeats;
            }
        }
    }
    else
    {
        shapeQuadraticWins = 0;
        lastOutput.shapeQuadraticWins = 0;
    }

    if (lastBeatTimeSec < 0.0)
    {
        lastBeatTimeSec = o.beatTimeSec;
        lastOutput.veto = TempoMotionVeto::none;
        lastOutput.predictedBpm = finitePredictedBpm (o.committedBpm);
        lastOutput.periodDeltaPerBeat = 0.0f;
        lastOutput.uncertainty = 1.0f;
        lastOutput.authority = 0.0f;
        lastOutput.proofClosed = false;
        lastOutput.firstStrictProof = false;
        lastOutput.state = TempoMotionShadowState::idle;
        return lastOutput;
    }

    const double elapsed = o.beatTimeSec - lastBeatTimeSec;
    if (elapsed <= 0.0 || ! std::isfinite (elapsed))
        return publishVeto (TempoMotionVeto::badObservation);

    const float period = static_cast<float> (elapsed / static_cast<double> (o.gridQuarterSteps));
    if (! std::isfinite (period) || period < kMinPeriodSec || period > kMaxPeriodSec)
        return publishVeto (TempoMotionVeto::badObservation);

    // Only a normalized period the model can accept may move this anchor.
    // Otherwise one bad timestamp corrupts both its own sample and the next.
    lastBeatTimeSec = o.beatTimeSec;
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
    lastOutput.firstStrictProof = qualityOk && ! strictProofSeen;
    strictProofSeen = strictProofSeen || qualityOk;

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
        authority = 0.0f;
        proofBeats = 0;
        direction = 0;
    }

    const float predictedPeriod =
        std::clamp (intercept + slope * static_cast<float> (count),
                    kMinPeriodSec, kMaxPeriodSec);
    lastOutput.predictedBpm = finitePredictedBpm (60.0f / predictedPeriod);
    const bool publishMotionDelta =
        ! shortFitContradictsSlope && (qualityOk || authority > 0.0f);
    lastOutput.periodDeltaPerBeat = publishMotionDelta ? slope : 0.0f;
    lastOutput.uncertainty =
        std::clamp (4.0f / std::max (4.0f, rateZ), 0.0f, 1.0f);
    lastOutput.authority = authority;
    lastOutput.proofClosed = previousAuthority == 0.0f && authority > 0.0f;
    lastOutput.state = authority > 0.0f ? TempoMotionShadowState::active
                                        : TempoMotionShadowState::proving;

    return lastOutput;
}

} // namespace vp
