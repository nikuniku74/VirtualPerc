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
