#include "TempoMotionShape.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace vp
{
namespace
{
constexpr int kMinimumSidePoints = 2;
constexpr double kAbsoluteNoiseFloorSec = 0.0001;
constexpr double kEvidenceMarginBic = 2.0;
constexpr double kPivotFloor = 1.0e-12;
constexpr int kAffineParameters = 2;
constexpr int kQuadraticParameters = 3;
constexpr int kHingeParameters = 4;   // intercept, two slopes, selected knot
constexpr int kOutlierParameters = 3; // intercept, slope, selected exclusion

using Points = std::array<TempoMotionShapePoint,
                          TempoMotionShape::kMaximumPoints>;
using Values = std::array<double, TempoMotionShape::kMaximumPoints>;

enum class Basis
{
    affine,
    quadratic,
    hinge
};

struct Fit
{
    std::array<double, 3> coefficients {};
    Values residuals {};
    double rss = 0.0;
    double scale = kAbsoluteNoiseFloorSec;
    double knot = 0.0;
    int residualCount = 0;
    int selection = -1;
    bool valid = false;
};

TempoMotionShapeResult invalidResult() noexcept
{
    TempoMotionShapeResult result;
    result.model = TempoMotionShapeModel::invalid;
    result.finite = false;
    return result;
}

double median (Values values, int count) noexcept
{
    for (int i = 1; i < count; ++i)
    {
        const double value = values[static_cast<size_t> (i)];
        int j = i;
        while (j > 0 && values[static_cast<size_t> (j - 1)] > value)
        {
            values[static_cast<size_t> (j)] = values[static_cast<size_t> (j - 1)];
            --j;
        }
        values[static_cast<size_t> (j)] = value;
    }

    const int middle = count / 2;
    if ((count & 1) != 0)
        return values[static_cast<size_t> (middle)];
    return 0.5 * (values[static_cast<size_t> (middle - 1)]
                  + values[static_cast<size_t> (middle)]);
}

double robustScale (const Values& residuals, int count) noexcept
{
    Values centred {};
    const double centre = median (residuals, count);
    for (int i = 0; i < count; ++i)
        centred[static_cast<size_t> (i)] =
            std::abs (residuals[static_cast<size_t> (i)] - centre);
    return 1.4826 * median (centred, count);
}

bool solveNormalEquations (std::array<std::array<double, 3>, 3> matrix,
                           std::array<double, 3> rhs,
                           int parameters,
                           std::array<double, 3>& solution) noexcept
{
    for (int column = 0; column < parameters; ++column)
    {
        int pivot = column;
        double pivotMagnitude =
            std::abs (matrix[static_cast<size_t> (column)]
                            [static_cast<size_t> (column)]);
        for (int row = column + 1; row < parameters; ++row)
        {
            const double candidate =
                std::abs (matrix[static_cast<size_t> (row)]
                                [static_cast<size_t> (column)]);
            if (candidate > pivotMagnitude)
            {
                pivot = row;
                pivotMagnitude = candidate;
            }
        }

        if (! std::isfinite (pivotMagnitude) || pivotMagnitude < kPivotFloor)
            return false;

        if (pivot != column)
        {
            std::swap (matrix[static_cast<size_t> (pivot)],
                       matrix[static_cast<size_t> (column)]);
            std::swap (rhs[static_cast<size_t> (pivot)],
                       rhs[static_cast<size_t> (column)]);
        }

        const double divisor = matrix[static_cast<size_t> (column)]
                                     [static_cast<size_t> (column)];
        for (int row = column + 1; row < parameters; ++row)
        {
            const double factor = matrix[static_cast<size_t> (row)]
                                        [static_cast<size_t> (column)] / divisor;
            for (int col = column; col < parameters; ++col)
                matrix[static_cast<size_t> (row)][static_cast<size_t> (col)] -=
                    factor * matrix[static_cast<size_t> (column)]
                                   [static_cast<size_t> (col)];
            rhs[static_cast<size_t> (row)] -=
                factor * rhs[static_cast<size_t> (column)];
        }
    }

    for (int row = parameters - 1; row >= 0; --row)
    {
        double value = rhs[static_cast<size_t> (row)];
        for (int col = row + 1; col < parameters; ++col)
            value -= matrix[static_cast<size_t> (row)][static_cast<size_t> (col)]
                   * solution[static_cast<size_t> (col)];
        const double divisor =
            matrix[static_cast<size_t> (row)][static_cast<size_t> (row)];
        if (! std::isfinite (divisor) || std::abs (divisor) < kPivotFloor)
            return false;
        solution[static_cast<size_t> (row)] = value / divisor;
        if (! std::isfinite (solution[static_cast<size_t> (row)]))
            return false;
    }
    return true;
}

std::array<double, 3> basisValues (Basis basis, double u, double knot) noexcept
{
    if (basis == Basis::quadratic)
        return { 1.0, u, u * u };
    if (basis == Basis::hinge)
        return { 1.0, u, std::max (0.0, u - knot) };
    return { 1.0, u, 0.0 };
}

Fit fitModel (const Points& points, const Values& normalized, int count,
              Basis basis, double knot, int excluded) noexcept
{
    Fit fit;
    fit.knot = knot;
    const int parameters = basis == Basis::affine ? 2 : 3;
    std::array<std::array<double, 3>, 3> matrix {};
    std::array<double, 3> rhs {};

    for (int i = 0; i < count; ++i)
    {
        if (i == excluded)
            continue;
        const auto terms = basisValues (basis,
                                        normalized[static_cast<size_t> (i)],
                                        knot);
        const double y = points[static_cast<size_t> (i)].residualSec;
        for (int row = 0; row < parameters; ++row)
        {
            rhs[static_cast<size_t> (row)] +=
                terms[static_cast<size_t> (row)] * y;
            for (int column = 0; column < parameters; ++column)
                matrix[static_cast<size_t> (row)][static_cast<size_t> (column)] +=
                    terms[static_cast<size_t> (row)]
                    * terms[static_cast<size_t> (column)];
        }
    }

    if (! solveNormalEquations (matrix, rhs, parameters, fit.coefficients))
        return fit;

    for (int i = 0; i < count; ++i)
    {
        if (i == excluded)
            continue;
        const auto terms = basisValues (basis,
                                        normalized[static_cast<size_t> (i)],
                                        knot);
        double predicted = 0.0;
        for (int parameter = 0; parameter < parameters; ++parameter)
            predicted += fit.coefficients[static_cast<size_t> (parameter)]
                       * terms[static_cast<size_t> (parameter)];
        const double residual =
            points[static_cast<size_t> (i)].residualSec - predicted;
        if (! std::isfinite (residual))
            return fit;
        fit.residuals[static_cast<size_t> (fit.residualCount++)] = residual;
        fit.rss += residual * residual;
    }

    fit.scale = robustScale (fit.residuals, fit.residualCount);
    fit.valid = std::isfinite (fit.rss) && std::isfinite (fit.scale);
    return fit;
}

double bic (double rss, double sigma, int count, int parameters) noexcept
{
    const double variance = std::max (rss / static_cast<double> (count),
                                      sigma * sigma);
    return static_cast<double> (count) * std::log (variance)
         + static_cast<double> (parameters)
             * std::log (static_cast<double> (count));
}
} // namespace

TempoMotionShapeResult TempoMotionShape::classify (
    const std::array<TempoMotionShapePoint, kMaximumPoints>& points,
    int count) noexcept
{
    if (count < 0 || count > kMaximumPoints)
        return invalidResult();
    if (count < kMinimumPoints)
        return {};

    for (int i = 0; i < count; ++i)
    {
        const auto& point = points[static_cast<size_t> (i)];
        if (! std::isfinite (point.quarterIndex)
            || ! std::isfinite (point.residualSec)
            || (i > 0
                && point.quarterIndex
                    <= points[static_cast<size_t> (i - 1)].quarterIndex))
            return invalidResult();
    }

    const double x0 = points[0].quarterIndex;
    const double span = points[static_cast<size_t> (count - 1)].quarterIndex - x0;
    if (! std::isfinite (span) || span <= 0.0)
        return invalidResult();

    Values normalized {};
    for (int i = 0; i < count; ++i)
    {
        normalized[static_cast<size_t> (i)] =
            (points[static_cast<size_t> (i)].quarterIndex - x0) / span;
        if (! std::isfinite (normalized[static_cast<size_t> (i)]))
            return invalidResult();
    }

    const Fit affine = fitModel (points, normalized, count,
                                 Basis::affine, 0.0, -1);
    const Fit quadratic = fitModel (points, normalized, count,
                                    Basis::quadratic, 0.0, -1);
    if (! affine.valid || ! quadratic.valid)
        return invalidResult();

    Fit bestHinge;
    for (int split = kMinimumSidePoints;
         split <= count - kMinimumSidePoints;
         ++split)
    {
        const double knot = 0.5
            * (normalized[static_cast<size_t> (split - 1)]
               + normalized[static_cast<size_t> (split)]);
        Fit candidate = fitModel (points, normalized, count,
                                  Basis::hinge, knot, -1);
        candidate.selection = split;
        if (! candidate.valid)
            return invalidResult();
        if (! bestHinge.valid || candidate.rss < bestHinge.rss)
            bestHinge = candidate;
    }

    Fit bestOutlier;
    for (int excluded = 0; excluded < count; ++excluded)
    {
        Fit candidate = fitModel (points, normalized, count,
                                  Basis::affine, 0.0, excluded);
        candidate.selection = excluded;
        if (! candidate.valid)
            return invalidResult();
        if (! bestOutlier.valid || candidate.rss < bestOutlier.rss)
            bestOutlier = candidate;
    }

    if (! bestHinge.valid || ! bestOutlier.valid)
        return invalidResult();

    const double sigma = std::max (
        kAbsoluteNoiseFloorSec,
        std::min ({ affine.scale, quadratic.scale,
                    bestHinge.scale, bestOutlier.scale }));
    if (! std::isfinite (sigma))
        return invalidResult();

    TempoMotionShapeResult result;
    result.affineBic = bic (affine.rss, sigma, count, kAffineParameters);
    result.quadraticBic = bic (quadratic.rss, sigma, count,
                               kQuadraticParameters);
    result.hingeBic = bic (bestHinge.rss, sigma, count, kHingeParameters);
    result.outlierBic = bic (bestOutlier.rss + sigma * sigma, sigma, count,
                             kOutlierParameters);
    result.quadraticVsHinge = result.hingeBic - result.quadraticBic;
    result.robustNoiseSec = sigma;
    result.hingeSplit = bestHinge.selection;
    result.excludedPoint = bestOutlier.selection;

    const std::array<double, 4> scores {
        result.affineBic,
        result.quadraticBic,
        result.hingeBic,
        result.outlierBic
    };
    for (double score : scores)
        if (! std::isfinite (score))
            return invalidResult();

    int lowest = 0;
    int second = 1;
    if (scores[static_cast<size_t> (second)]
        < scores[static_cast<size_t> (lowest)])
        std::swap (lowest, second);
    for (int i = 2; i < static_cast<int> (scores.size()); ++i)
    {
        if (scores[static_cast<size_t> (i)]
            < scores[static_cast<size_t> (lowest)])
        {
            second = lowest;
            lowest = i;
        }
        else if (scores[static_cast<size_t> (i)]
                 < scores[static_cast<size_t> (second)])
        {
            second = i;
        }
    }
    result.evidenceMargin = scores[static_cast<size_t> (second)]
                          - scores[static_cast<size_t> (lowest)];

    constexpr std::array<TempoMotionShapeModel, 4> models {
        TempoMotionShapeModel::affine,
        TempoMotionShapeModel::quadratic,
        TempoMotionShapeModel::hinge,
        TempoMotionShapeModel::affineWithOutlier
    };
    result.model = result.evidenceMargin >= kEvidenceMarginBic
                       ? models[static_cast<size_t> (lowest)]
                       : TempoMotionShapeModel::affine;

    const double futureU =
        (points[static_cast<size_t> (count - 1)].quarterIndex + 1.0 - x0) / span;
    if (result.model == TempoMotionShapeModel::quadratic)
    {
        result.nextPeriodDeltaSec =
            (quadratic.coefficients[1]
             + 2.0 * quadratic.coefficients[2] * futureU) / span;
    }
    else if (result.model == TempoMotionShapeModel::hinge)
    {
        result.nextPeriodDeltaSec =
            (bestHinge.coefficients[1]
             + (futureU > bestHinge.knot ? bestHinge.coefficients[2] : 0.0))
            / span;
    }
    else if (result.model == TempoMotionShapeModel::affineWithOutlier)
    {
        result.nextPeriodDeltaSec = bestOutlier.coefficients[1] / span;
    }
    else
    {
        result.nextPeriodDeltaSec = affine.coefficients[1] / span;
    }

    if (! std::isfinite (result.evidenceMargin)
        || ! std::isfinite (result.quadraticVsHinge)
        || ! std::isfinite (result.nextPeriodDeltaSec))
        return invalidResult();
    return result;
}
} // namespace vp
