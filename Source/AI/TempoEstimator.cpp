#include "AI/TempoEstimator.h"

#include <algorithm>
#include <cmath>

namespace vp
{

namespace
{
    // Recompute every 8 frames (160 ms at 50 fps). Tempo does not move faster
    // than that, and it keeps the search off the worker's hot path.
    constexpr int kRefreshFrames = 8;

    // Activation from ~4 s ago still counts, but the newest bars dominate, so a
    // live tempo change shows up without waiting for the whole buffer.
    constexpr double kWeightTauSeconds = 4.0;

    // Harmonics of a candidate period that count in its favour. Three is enough
    // to be robust without reaching so far back that the autocorrelation runs
    // out of overlap.
    constexpr int kHarmonics = 3;

    // Charges for the two ways a candidate can sit on the wrong metrical level.
    // Both are ratios of correlations, so they do not care how loud the music
    // is, only how the levels compare.
    constexpr float kTooSlowPenalty = 0.85f;
    constexpr float kTooFastPenalty = 0.85f;

    // Inside its own period a candidate must find nothing that rivals its own
    // beats, and the whole interior has to be searched: testing a fixed set of
    // fractions only moves the error around, because for any set there is a
    // multiple of the true period - one and a half beats, two and a half -
    // whose skipped beats all fall between the fractions tested.
    //
    // The guards keep two things out of that search: the short lags, where the
    // width of a single activation bump correlates with itself, and the lags
    // just below the candidate, which are the same bump seen from the far side.
    constexpr float kInteriorLoFrames = 4.0f;
    constexpr float kInteriorLoFrac = 0.22f;
    constexpr float kInteriorHiFrames = 3.0f;
    constexpr float kInteriorHiFrac = 0.12f;

    // An interior peak is a subdivision while it stays below kSameLevel0 of the
    // beat itself, and a beat the candidate is stepping over once it reaches
    // kSameLevel1. Music nearly always has some offbeat energy, so the low end
    // has to be forgiving.
    constexpr float kSameLevel0 = 0.62f;
    constexpr float kSameLevel1 = 0.98f;

    // A candidate whose own lag correlates worse than twice its lag is sitting
    // on subdivisions. Equal correlation is normal and free; clearly worse is
    // charged in full.
    constexpr float kLevelDown0 = 0.72f;
    constexpr float kLevelDown1 = 1.00f;

    // Period grid: 0.05 frames is ~0.3 BPM at 120, finer than the metrical
    // decision needs and far finer than the smear it exists to avoid.
    constexpr float kLagStep = 0.05f;

    // An autocorrelation lag needs this fraction of the window still paired up
    // before its value means anything.
    constexpr float kMinOverlap = 0.25f;

    // Fold needs a few periods before it means anything.
    constexpr int kMinPeriods = 3;

    // Search wider than we report. A candidate pinned against the edge of the
    // reported range is not a measurement, it is the edge; seeing the real
    // period and halving it into range is an answer.
    constexpr double kSearchMinBpm = 42.0;
    constexpr double kSearchMaxBpm = 280.0;

    float smoothStep (float edge0, float edge1, float x) noexcept
    {
        const float t = std::clamp ((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
}

void TempoEstimator::prepare (double framesPerSecond)
{
    fps = framesPerSecond > 1.0 ? framesPerSecond : 50.0;

    minLag = std::max (2, static_cast<int> (std::floor (fps * 60.0 / kSearchMaxBpm)));
    maxLag = std::max (minLag + 2, static_cast<int> (std::ceil (fps * 60.0 / kSearchMinBpm)));
    window = std::max (maxLag * kMinPeriods * 2, static_cast<int> (std::ceil (fps * 12.0)));
    acfLags = std::min (window - 1, maxLag * kHarmonics + 2);
    candidates = static_cast<int> (std::floor (static_cast<float> (maxLag - minLag) / kLagStep)) + 1;

    ring.assign (static_cast<size_t> (window), 0.0f);
    lin.assign (static_cast<size_t> (window), 0.0f);
    weight.assign (static_cast<size_t> (window), 0.0f);
    acf.assign (static_cast<size_t> (acfLags) + 1u, 0.0f);
    score.assign (static_cast<size_t> (candidates), 0.0f);

    const float tau = static_cast<float> (fps * kWeightTauSeconds);
    for (int age = 0; age < window; ++age)
        weight[static_cast<size_t> (age)] = std::exp (-static_cast<float> (age) / tau);

    reset();
}

void TempoEstimator::reset() noexcept
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (acf.begin(), acf.end(), 0.0f);
    std::fill (score.begin(), score.end(), 0.0f);
    write = 0;
    filled = 0;
    sinceRefresh = 0;
    isReady = false;
    bestBpm = 0.0f;
    bestSalience = 0.0f;
    bestClarity = 0.0f;
}

float TempoEstimator::tempoPrior (float bpmCandidate) const noexcept
{
    // Only decides genuinely ambiguous material, such as a perfectly even pulse
    // that is as defensible at half time as at face value. Wide enough that a
    // real 60 or a real 190 still wins on evidence.
    const float octaves = std::log2 (std::max (1.0f, bpmCandidate) / 120.0f) / 1.15f;
    return std::exp (-0.5f * octaves * octaves);
}

float TempoEstimator::lagAt (float lag) const noexcept
{
    // Quadratic read of the autocorrelation at a real-valued lag, centred on
    // the nearest frame. A straight line between neighbours pulls the top of
    // every peak towards the lower frame, which at 190 BPM - fifteen frames to
    // the beat - is a whole percent of tempo.
    if (lag < 1.0f || lag > static_cast<float> (acfLags))
        return 0.0f;

    const int c = std::clamp (static_cast<int> (std::lround (lag)), 1, acfLags - 1);
    const float d = lag - static_cast<float> (c);
    const float y0 = acf[static_cast<size_t> (c - 1)];
    const float y1 = acf[static_cast<size_t> (c)];
    const float y2 = acf[static_cast<size_t> (c + 1)];
    return y1 + 0.5f * d * (y2 - y0) + 0.5f * d * d * (y2 - 2.0f * y1 + y0);
}

float TempoEstimator::combScore (float lag) const noexcept
{
    // Strength is the candidate's own harmonics, averaged over however many of
    // them the autocorrelation still covers, so a slow candidate is not
    // punished for the ones that fall off the end.
    float sum = 0.0f, norm = 0.0f;
    for (int k = 1; k <= kHarmonics; ++k)
    {
        const float at = static_cast<float> (k) * lag;
        if (at > static_cast<float> (acfLags))
            break;
        const float w = 1.0f / static_cast<float> (k);
        sum += w * lagAt (at);
        norm += w;
    }
    if (norm <= 0.0f)
        return 0.0f;

    const float strength = sum / norm;
    if (strength <= 0.0f)
        return 0.0f;

    // Choosing the metrical level is two questions, and asking only one of them
    // trades an octave error for its mirror image. Something inside the period
    // as strong as the teeth means this period spans more than one beat. Twice
    // this period correlating better than this one means the teeth are
    // subdivisions.
    const float own = lagAt (lag);
    if (own <= 0.0f)
        return 0.0f;

    const int lo = static_cast<int> (std::ceil (std::max (kInteriorLoFrames, kInteriorLoFrac * lag)));
    const int hi = static_cast<int> (std::floor (lag - std::max (kInteriorHiFrames, kInteriorHiFrac * lag)));
    float interior = 0.0f;
    for (int at = lo; at <= hi; ++at)
        interior = std::max (interior, acf[static_cast<size_t> (at)]);
    const float tooSlow = smoothStep (kSameLevel0, kSameLevel1, interior / own);

    const float dbl = lagAt (2.0f * lag);
    const float tooFast = dbl > 0.0f
                            ? 1.0f - smoothStep (kLevelDown0, kLevelDown1, own / dbl)
                            : 0.0f;

    return strength * (1.0f - kTooSlowPenalty * tooSlow) * (1.0f - kTooFastPenalty * tooFast);
}

void TempoEstimator::push (float activation) noexcept
{
    if (ring.empty())
        return;

    ring[static_cast<size_t> (write)] = activation > 0.0f ? activation : 0.0f;
    write = (write + 1) % window;
    if (filled < window)
        ++filled;

    if (++sinceRefresh < kRefreshFrames)
        return;
    sinceRefresh = 0;
    refresh();
}

void TempoEstimator::refresh() noexcept
{
    const int n = filled;

    // Three periods of the fastest tempo we accept is the floor for saying
    // anything at all; slower tempi simply become available a little later.
    if (n < minLag * kMinPeriods || n < static_cast<int> (fps * 1.2))
    {
        isReady = false;
        return;
    }

    for (int age = 0; age < n; ++age)
    {
        int idx = write - 1 - age;
        if (idx < 0)
            idx += window;
        lin[static_cast<size_t> (age)] = ring[static_cast<size_t> (idx)];
    }

    double weightAcc = 0.0;
    double meanAcc = 0.0;
    for (int age = 0; age < n; ++age)
    {
        const double w = static_cast<double> (weight[static_cast<size_t> (age)]);
        weightAcc += w;
        meanAcc += w * static_cast<double> (lin[static_cast<size_t> (age)]);
    }
    if (weightAcc <= 0.0)
    {
        isReady = false;
        return;
    }
    const float mean = static_cast<float> (meanAcc / weightAcc);

    double varAcc = 0.0;
    for (int age = 0; age < n; ++age)
    {
        const float d = lin[static_cast<size_t> (age)] - mean;
        lin[static_cast<size_t> (age)] = d;
        varAcc += static_cast<double> (weight[static_cast<size_t> (age)]) * static_cast<double> (d) * d;
    }
    const double var = varAcc / weightAcc;
    if (var < 1.0e-10)
    {
        // A flat activation curve carries no pulse. Say so, instead of
        // reporting whichever period correlates best with nothing.
        isReady = false;
        bestSalience = 0.0f;
        bestClarity = 0.0f;
        return;
    }

    const int lagTop = std::min (maxLag, n / kMinPeriods);
    if (lagTop < minLag)
    {
        isReady = false;
        return;
    }

    // Recency-weighted autocorrelation, normalised per lag by the weight that
    // actually contributed, so a long lag is not penalised for having fewer
    // pairs left to average.
    const int top = std::min (acfLags, n - 1);
    const float minPairs = kMinOverlap * static_cast<float> (weightAcc);
    for (int lag = 0; lag <= acfLags; ++lag)
    {
        if (lag > top)
        {
            acf[static_cast<size_t> (lag)] = 0.0f;
            continue;
        }
        double num = 0.0, den = 0.0;
        for (int age = 0; age + lag < n; ++age)
        {
            const double w = static_cast<double> (weight[static_cast<size_t> (age)]);
            num += w * static_cast<double> (lin[static_cast<size_t> (age)])
                     * static_cast<double> (lin[static_cast<size_t> (age + lag)]);
            den += w;
        }
        acf[static_cast<size_t> (lag)] = den >= static_cast<double> (minPairs)
                                            ? static_cast<float> (num / (den * var))
                                            : 0.0f;
    }

    int bestIndex = -1;
    float best = 0.0f;
    const int topIndex = std::min (candidates - 1,
                                   static_cast<int> (std::floor (static_cast<float> (lagTop - minLag) / kLagStep)));

    for (int i = 0; i <= topIndex; ++i)
    {
        const float lag = static_cast<float> (minLag) + static_cast<float> (i) * kLagStep;
        float s = combScore (lag);
        s *= tempoPrior (static_cast<float> (60.0 * fps) / lag);
        score[static_cast<size_t> (i)] = s > 0.0f ? s : 0.0f;
        if (s > best)
        {
            best = s;
            bestIndex = i;
        }
    }

    if (bestIndex < 0 || best <= 1.0e-4f)
    {
        isReady = false;
        bestSalience = 0.0f;
        bestClarity = 0.0f;
        return;
    }

    const float bestLag = static_cast<float> (minLag) + static_cast<float> (bestIndex) * kLagStep;

    // Best rival that is a different period, not the same peak a step over.
    // Octaves are left in on purpose: if one is competitive, confidence should
    // say so.
    float rival = 0.0f;
    for (int i = 0; i <= topIndex; ++i)
    {
        const float lag = static_cast<float> (minLag) + static_cast<float> (i) * kLagStep;
        if (std::fabs (lag - bestLag) <= 0.08f * bestLag)
            continue;
        rival = std::max (rival, score[static_cast<size_t> (i)]);
    }

    // The winning period is the metrical level the music actually has; the
    // reported range is what a percussion clock can use. Octave-fold rather than
    // clamp, so a very fast pulse comes back as its own half.
    float winner = static_cast<float> (60.0 * fps) / bestLag;
    while (winner > kMaxBpm)
        winner *= 0.5f;
    while (winner < kMinBpm)
        winner *= 2.0f;
    bestBpm = std::clamp (winner, kMinBpm, kMaxBpm);
    // A comb that correlates at 0.5 after the offbeat charge is an unmistakable
    // pulse; scale so that maps to ~1.
    bestSalience = std::clamp (best / 0.5f, 0.0f, 1.0f);
    bestClarity = std::clamp (1.0f - rival / best, 0.0f, 1.0f);
    isReady = true;
}

} // namespace vp
