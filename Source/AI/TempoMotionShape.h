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
