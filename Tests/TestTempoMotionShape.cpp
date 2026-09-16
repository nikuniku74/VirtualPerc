#include "TestTempoMotionShape.h"
#include "AI/TempoMotionShape.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
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

Points makeAffine (int n, double phase, double slope)
{
    return makeShape (n, phase, slope, 0.0, -1, 0.0, -1, 0.0, false);
}

Points makeQuadratic (int n, double curvature)
{
    return makeShape (n, 0.0, 0.0, curvature, -1, 0.0, -1, 0.0, false);
}

Points makeQuadraticWithMissingQuarter (int n, double curvature)
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
}

Points makeAffineWithOutlier (int n, int excluded, double displacement)
{
    return makeShape (n, 0.011, -0.0002, 0.0, -1, 0.0,
                      excluded, displacement, false);
}

Points makeHinge (int n, int split, double slopeDelta, bool missed)
{
    return makeShape (n, 0.0, 0.0001, 0.0, split, slopeDelta,
                      -1, 0.0, missed);
}

bool allNumericOutputsFinite (const vp::TempoMotionShapeResult& result) noexcept
{
    return std::isfinite (result.affineBic)
        && std::isfinite (result.quadraticBic)
        && std::isfinite (result.hingeBic)
        && std::isfinite (result.outlierBic)
        && std::isfinite (result.evidenceMargin)
        && std::isfinite (result.quadraticVsHinge)
        && std::isfinite (result.nextPeriodDeltaSec)
        && std::isfinite (result.robustNoiseSec);
}
} // namespace

void vpRunTempoMotionShapeTests (int& passed, int& failed)
{
    auto expect = [&] (bool condition, const char* name)
    {
        condition ? ++passed : ++failed;
        std::printf ("  %s  %s\n", condition ? "PASS" : "FAIL", name);
    };

    auto expectModel = [&] (const Points& points, int count,
                            vp::TempoMotionShapeModel expected, const char* name)
    {
        const auto result = vp::TempoMotionShape::classify (points, count);
        expect (result.finite && allNumericOutputsFinite (result)
                    && result.model == expected,
                name);
    };
    auto expectHinge = [&] (const Points& points, int count,
                            int split, const char* name)
    {
        const auto result = vp::TempoMotionShape::classify (points, count);
        expect (result.finite && allNumericOutputsFinite (result)
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
            expectHinge (makeHinge (n, split, 0.0045, false), n, split,
                         "slower step residual is a hinge");
            expectHinge (makeHinge (n, split, -0.0045, true), n, split,
                         "faster step with first new beat missing is a hinge");
            expectHinge (makeHinge (n, split, 0.0045, true), n, split,
                         "slower step with first new beat missing is a hinge");
        }

        expectModel (makeQuadraticWithMissingQuarter (n, -0.00032), n,
                     vp::TempoMotionShapeModel::quadratic,
                     "cumulative quarter index preserves accelerando across a missed beat");
        expectModel (makeQuadraticWithMissingQuarter (n, 0.00032), n,
                     vp::TempoMotionShapeModel::quadratic,
                     "cumulative quarter index preserves rallentando across a missed beat");
    }

    {
        const auto result = vp::TempoMotionShape::classify (
            makeAffine (6, 0.0, 0.0), 6);
        expect (result.finite && allNumericOutputsFinite (result)
                    && result.model == vp::TempoMotionShapeModel::insufficient,
                "six points are insufficient");
    }
    {
        const auto result = vp::TempoMotionShape::classify (
            makeAffine (12, 0.0, 0.0), 13);
        expect (! result.finite && allNumericOutputsFinite (result)
                    && result.model == vp::TempoMotionShapeModel::invalid,
                "count above fixed capacity is invalid");
    }

    auto expectInvalid = [&] (Points points, const char* name)
    {
        const auto result = vp::TempoMotionShape::classify (points, 7);
        expect (! result.finite && allNumericOutputsFinite (result)
                    && result.model == vp::TempoMotionShapeModel::invalid,
                name);
    };

    auto duplicate = makeAffine (7, 0.0, 0.0);
    duplicate[3].quarterIndex = duplicate[2].quarterIndex;
    expectInvalid (duplicate, "duplicate quarter index is invalid");

    auto decreasing = makeAffine (7, 0.0, 0.0);
    decreasing[3].quarterIndex = decreasing[2].quarterIndex - 1.0;
    expectInvalid (decreasing, "decreasing quarter index is invalid");

    auto nanCoordinate = makeAffine (7, 0.0, 0.0);
    nanCoordinate[2].residualSec = std::numeric_limits<double>::quiet_NaN();
    expectInvalid (nanCoordinate, "NaN coordinate is invalid");

    auto infiniteCoordinate = makeAffine (7, 0.0, 0.0);
    infiniteCoordinate[2].quarterIndex = std::numeric_limits<double>::infinity();
    expectInvalid (infiniteCoordinate, "infinite coordinate is invalid");
}
