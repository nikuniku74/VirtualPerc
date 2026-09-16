#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace vp
{

/**
    Causal beat-date tracker: an interacting-multiple-model (IMM) Kalman filter
    over the decoder's accepted beats. It exists because the decoder's own
    phase is `(now - line-fit anchor) / committed period`: while a band speeds
    up both of those lag, and the published phase falls behind until a hard
    fixed-to-live release catches up.

    State per model, indexed per beat: x = [T, P, V]
      T  date of the newest beat index the filter has reached (seconds)
      P  period from that beat to the next
      V  change of period per beat (negative = accelerando)

    Three models share the state and are mixed by how well each has predicted
    the recent beats - no threshold or regime decides between them:
      constant  the period barely walks: averages like the long fit on a record
      wandering the period walks a little: a band that breathes
      moving    V itself walks: an accelerando or rallentando is followed with
                a constant-acceleration prediction rather than a lagging line

    Pure arithmetic, fixed size, no allocation: safe on any thread.
*/
class BeatKalman
{
public:
    using Vec = std::array<double, 3>;
    using Mat = std::array<std::array<double, 3>, 3>;

    void reset() noexcept { initialised = false; }
    bool started() const noexcept { return initialised; }
    /** Two accepted beats after a (re)start: enough to have measured a period. */
    bool ready() const noexcept { return initialised && updates >= 2; }
    /** The beats stopped fitting the filter in a way only a tempo change
        explains, or a gap longer than it can bridge. The owner re-seeds it
        from a robust grid; it never re-seeds itself from one peak, which after
        a gap may be an offbeat. */
    bool lostTrack() const noexcept { return lost; }
    /** When the refused beats agree on one new period, it and the newest of
        them: a tempo step measured by the filter itself, so the owner can
        re-seed on the new pulse instead of the grid that is still at the old
        one. False when the refusals do not describe one tempo. */
    bool measuredStep (double& beatSec, double& periodSec) const noexcept
    {
        if (! lost || stepPeriod <= 0.0)
            return false;
        beatSec = stepBeat;
        periodSec = stepPeriod;
        return true;
    }

    void start (double beatSec, double periodSec) noexcept
    {
        for (size_t m = 0; m < kModels; ++m)
        {
            x[m] = { beatSec, periodSec, 0.0 };
            const double sp = kInitPeriodSigma * periodSec;
            const double sv = kInitVSigma * periodSec;
            P[m] = {{ { kJitterSec * kJitterSec, 0.0, 0.0 },
                      { 0.0, sp * sp, 0.0 },
                      { 0.0, 0.0, sv * sv } }};
        }
        mu = kInitialProbability;
        initialised = true;
        updates = 0;
        misses = 0;
        lost = false;
        stepPeriod = 0.0;
    }

    /** One beat. False when it was refused as off the predicted grid.
        `mayUpdate` false makes it evidence of a change only: refused, it
        counts towards a measured step; inside the gate it is ignored. */
    bool observe (double beatSec, bool mayUpdate = true) noexcept
    {
        if (! initialised)
            return false;

        const double period = combined (1);
        if (period <= 0.0)
            return false;

        const long steps = std::lround ((beatSec - combined (0)) / period);
        if (steps < 1)
            return false;
        if (steps > kMaxSteps)
        {
            lost = true;
            return false;
        }

        // IMM mixing: each model starts from a blend of all of them, weighted
        // by how likely a switch into it was over `steps` beats.
        const auto pi = transition (steps);
        std::array<double, kModels> cbar {};
        for (size_t j = 0; j < kModels; ++j)
            for (size_t i = 0; i < kModels; ++i)
                cbar[j] += pi[i][j] * mu[i];

        std::array<Vec, kModels> xn {};
        std::array<Mat, kModels> Pn {};
        for (size_t j = 0; j < kModels; ++j)
        {
            Vec xm {};
            Mat pm {};
            for (size_t i = 0; i < kModels; ++i)
            {
                const double w = cbar[j] > 0.0 ? pi[i][j] * mu[i] / cbar[j] : 0.0;
                for (size_t k = 0; k < 3; ++k)
                    xm[k] += w * x[i][k];
            }
            for (size_t i = 0; i < kModels; ++i)
            {
                const double w = cbar[j] > 0.0 ? pi[i][j] * mu[i] / cbar[j] : 0.0;
                for (size_t r = 0; r < 3; ++r)
                    for (size_t c = 0; c < 3; ++c)
                        pm[r][c] += w * (P[i][r][c] + (x[i][r] - xm[r]) * (x[i][c] - xm[c]));
            }
            xn[j] = xm;
            Pn[j] = pm;
            for (long s = 0; s < steps; ++s)
                predict (xn[j], Pn[j], j);
        }

        double innovation = 0.0;
        for (size_t j = 0; j < kModels; ++j)
            innovation += cbar[j] * (beatSec - xn[j][0]);

        // A fixed fraction of the period, not a multiple of the filter's own
        // spread: that spread grows exactly while the motion model is
        // entertaining noise, and a gate that widened with it let sixteenths
        // in to feed the very motion that widened it.
        if (std::fabs (innovation) > kGatePeriods * period)
        {
            // Refused beats are either a stream the grid gate should not have
            // passed - swung offbeats, a *constant* fraction of a beat away -
            // or a changed tempo, which puts each one further off than the
            // last. Only the second means the filter is wrong.
            const double frac = innovation / period - std::round (innovation / period);
            missFrac[static_cast<size_t> (misses % kMissMemory)] = frac;
            missTime[static_cast<size_t> (misses % kMissMemory)] = beatSec;
            ++misses;
            if (misses >= kMissMemory)
            {
                const double a = missFrac[static_cast<size_t> ((misses - 3) % kMissMemory)];
                const double b = missFrac[static_cast<size_t> ((misses - 2) % kMissMemory)];
                const double c = missFrac[static_cast<size_t> ((misses - 1) % kMissMemory)];
                if ((b - a) * (c - b) > 0.0 && std::fabs (c - a) > kMissDriftPeriods)
                {
                    lost = true;
                    measureStep (period);
                }
            }
            return false;
        }
        if (! mayUpdate)
            return false;
        misses = 0;

        const double r2 = kJitterSec * kJitterSec;
        double norm = 0.0;
        for (size_t j = 0; j < kModels; ++j)
        {
            const double S = Pn[j][0][0] + r2;
            const double e = beatSec - xn[j][0];
            // Tempered: onsets are heavier-tailed than a Gaussian, and one
            // late snare must not hand the motion model the song.
            const double St = kLikelihoodTemper * S;
            const double likelihood = std::exp (-0.5 * e * e / St) / std::sqrt (St);

            Vec K {};
            for (size_t k = 0; k < 3; ++k)
                K[k] = Pn[j][k][0] / S;
            Mat p = Pn[j];
            for (size_t r = 0; r < 3; ++r)
            {
                xn[j][r] += K[r] * e;
                for (size_t c = 0; c < 3; ++c)
                    p[r][c] = Pn[j][r][c] - K[r] * Pn[j][0][c];
            }
            x[j] = xn[j];
            P[j] = p;
            mu[j] = std::max (1.0e-300, likelihood * cbar[j]);
            norm += mu[j];
        }

        double sum = 0.0;
        for (double& m : mu)
        {
            m = norm > 0.0 ? std::clamp (m / norm, kMinProbability, 1.0) : 1.0 / kModels;
            sum += m;
        }
        for (double& m : mu)
            m /= sum;

        ++updates;
        return true;
    }

    /** Beat phase 0..1 and local period at `nowSec`, extrapolated along the
        mixed model. False while the filter has nothing to say. */
    bool phaseAt (double nowSec, double& phase, double& periodNow) const noexcept
    {
        if (! ready())
            return false;
        double t = combined (0);
        double p = combined (1);
        const double v = combined (2);
        if (p <= 0.0)
            return false;
        const double floorPeriod = 0.2 * p;
        for (int guard = 0; guard < kMaxSteps && nowSec >= t + p; ++guard)
        {
            t += p;
            p = std::max (floorPeriod, p + v);
        }
        if (nowSec >= t + p || nowSec < t - p)
            return false;
        // The corrected date of the newest beat can sit a few milliseconds
        // after `now` - the onset it was measured from is already behind the
        // frame, the correction is not. That instant is the end of the
        // previous beat, not phase zero.
        phase = nowSec >= t ? (nowSec - t) / p : 1.0 + (nowSec - t) / p;
        phase = std::clamp (phase, 0.0, std::nextafter (1.0, 0.0));
        periodNow = p;
        return true;
    }

    double period() const noexcept { return combined (1); }

private:
    static constexpr size_t kModels = 3;
    static constexpr size_t kMovingModel = 2;
    static constexpr long kMaxSteps = 8;
    static constexpr int kMissMemory = 3;
    static constexpr double kJitterSec = 0.015;
    static constexpr double kInitPeriodSigma = 0.02;
    static constexpr double kInitVSigma = 0.002;
    // Per-beat random walk of the period, as a fraction of it, per model.
    static constexpr std::array<double, kModels> kPeriodWalk { 0.00005, 0.0008, 0.0015 };
    static constexpr double kMovingVWalk = 0.0012;
    static constexpr std::array<double, kModels> kInitialProbability { 0.45, 0.45, 0.10 };
    static constexpr double kStayProbability = 0.99;
    static constexpr double kMinProbability = 1.0e-4;
    static constexpr double kGatePeriods = 0.15;
    static constexpr double kMissDriftPeriods = 0.06;
    static constexpr double kLikelihoodTemper = 2.0;
    // A step measured from refused beats: their folded intervals must agree
    // within this fraction, and describe a change inside this window. Octaves
    // stay with the decoder's level logic.
    static constexpr double kStepAgreement = 0.05;
    static constexpr double kStepSmallest = 0.05;
    static constexpr double kStepLargest = 0.30;

    void measureStep (double oldPeriod) noexcept
    {
        stepPeriod = 0.0;
        std::array<double, kMissMemory - 1> folded {};
        for (int k = 0; k + 1 < kMissMemory; ++k)
        {
            const double older = missTime[static_cast<size_t> ((misses - kMissMemory + k) % kMissMemory)];
            const double newer = missTime[static_cast<size_t> ((misses - kMissMemory + k + 1) % kMissMemory)];
            const double interval = newer - older;
            const double n = std::max (1.0, std::round (interval / oldPeriod));
            folded[static_cast<size_t> (k)] = interval / n;
        }
        const auto [lo, hi] = std::minmax_element (folded.begin(), folded.end());
        const double centre = 0.5 * (*lo + *hi);
        const double change = centre / oldPeriod;
        if (*hi - *lo > kStepAgreement * centre
            || std::fabs (change - 1.0) < kStepSmallest
            || std::fabs (change - 1.0) > kStepLargest)
            return;
        stepPeriod = centre;
        stepBeat = missTime[static_cast<size_t> ((misses - 1) % kMissMemory)];
    }

    static std::array<std::array<double, kModels>, kModels> transition (long steps) noexcept
    {
        const double stay = std::pow (kStayProbability, static_cast<double> (steps));
        const double leave = (1.0 - stay) / static_cast<double> (kModels - 1);
        std::array<std::array<double, kModels>, kModels> pi {};
        for (size_t i = 0; i < kModels; ++i)
            for (size_t j = 0; j < kModels; ++j)
                pi[i][j] = i == j ? stay : leave;
        return pi;
    }

    static void predict (Vec& s, Mat& p, size_t model) noexcept
    {
        // T += P; P += V; V held by the moving model, pinned at zero otherwise.
        const bool moving = model == kMovingModel;
        const double period = std::max (1.0e-3, s[1]);
        s = { s[0] + s[1], s[1] + s[2], moving ? s[2] : 0.0 };
        const Mat f {{ { 1.0, 1.0, 0.0 }, { 0.0, 1.0, 1.0 }, { 0.0, 0.0, moving ? 1.0 : 0.0 } }};
        Mat fp {}, out {};
        for (size_t r = 0; r < 3; ++r)
            for (size_t c = 0; c < 3; ++c)
                for (size_t k = 0; k < 3; ++k)
                    fp[r][c] += f[r][k] * p[k][c];
        for (size_t r = 0; r < 3; ++r)
            for (size_t c = 0; c < 3; ++c)
                for (size_t k = 0; k < 3; ++k)
                    out[r][c] += fp[r][k] * f[c][k];
        const double qp = kPeriodWalk[model] * period;
        const double qv = moving ? kMovingVWalk * period : 0.0;
        out[1][1] += qp * qp;
        out[2][2] += qv * qv;
        p = out;
    }

    double combined (size_t k) const noexcept
    {
        double v = 0.0;
        for (size_t m = 0; m < kModels; ++m)
            v += mu[m] * x[m][k];
        return v;
    }

    std::array<Vec, kModels> x {};
    std::array<Mat, kModels> P {};
    std::array<double, kModels> mu = kInitialProbability;
    std::array<double, kMissMemory> missFrac {};
    std::array<double, kMissMemory> missTime {};
    double stepPeriod = 0.0;
    double stepBeat = 0.0;
    bool initialised = false;
    bool lost = false;
    int updates = 0;
    int misses = 0;
};

} // namespace vp
