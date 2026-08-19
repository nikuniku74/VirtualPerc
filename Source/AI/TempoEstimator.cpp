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

    // The fold gets its own, far flatter weighting. Which metrical level the
    // music is on is not something that changes bar to bar, while *where* the
    // beats are does - so the two want opposite windows. Weighted like the
    // autocorrelation, the fold at a slow tempo averages barely four beats and
    // its ratio swings between 0.34 and 0.76 from one refresh to the next,
    // which is enough to flip the level back and forth indefinitely.
    constexpr double kFoldTauSeconds = 30.0;

    // Harmonics of a candidate period that count in its favour. Three is enough
    // to be robust without reaching so far back that the autocorrelation runs
    // out of overlap.
    constexpr int kHarmonics = 3;

    // The folded activation, half a period from its own peak, relative to that
    // peak. Both charges read this one number, measured at the candidate and at
    // its double, which is what makes them symmetric:
    //
    //   at the candidate - a rival that tall halfway through means the period
    //   spans two beats, so the candidate is an octave too slow;
    //   at twice the candidate - the beats alternating loud/quiet a level up
    //   means the candidate's own teeth are subdivisions, so it is too fast.
    //
    // Measured on BeatNet activations the ratio is ~0.13 at the true period and
    // ~0.80 at the double, so the bands sit inside that gap. The too-fast band
    // is the more forgiving of the two on purpose: a kick/snare backbeat also
    // alternates loud and quiet a level up, and calling that a subdivision is
    // how a tracker ends up half-time on an ordinary rock beat.
    constexpr float kTooSlowLo = 0.40f;
    constexpr float kTooSlowHi = 0.80f;
    constexpr float kTooFastLo = 0.30f;
    constexpr float kTooFastHi = 0.90f;
    constexpr float kTooSlowPenalty = 0.80f;
    constexpr float kTooFastPenalty = 0.90f;

    // Period grid: 0.05 frames is ~0.3 BPM at 120, finer than the metrical
    // decision needs and far finer than the smear it exists to avoid.
    constexpr float kLagStep = 0.05f;

    // A candidate has to reach this fraction of the best comb before the fold
    // is evaluated for it. Anything below is not a metrical level, it is noise
    // between the levels.
    constexpr float kPeakFloor = 0.30f;

    // Once a level has been reported, changing it takes a sustained, clear win
    // by a rival - not a better score on one refresh. Everything downstream is
    // rebuilt when the level moves (the decoder's grid, its beat history, the
    // clock's phase), so a level that flickers here is a clock that flickers
    // everywhere; and on genuinely ambiguous material the two levels do trade
    // the lead from refresh to refresh, so "whoever is ahead right now" is not
    // a decision, it is a coin toss repeated six times a second.
    //
    // A rival still takes over inside two seconds, which is what a new song or
    // a half-time section needs, and immediately if the held level stops being
    // a peak at all.
    constexpr float kHoldOctaves = 0.10f;
    constexpr float kSwitchMargin = 1.20f;
    constexpr int   kSwitchRefreshes = 10;

    // Until the buffer holds enough periods to fold the level one octave slower
    // than the winner, the slow side has not really been examined - the ratio
    // that would rule the winner out as too fast is being measured over a
    // couple of beats. Hysteresis over a verdict that weak is how a fast first
    // guess becomes permanent, so before then a rival only has to hold its lead
    // briefly rather than for two seconds.
    constexpr int kSwitchRefreshesEarly = 3;

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
    foldWeight.assign (static_cast<size_t> (window), 0.0f);
    acf.assign (static_cast<size_t> (acfLags) + 1u, 0.0f);
    score.assign (static_cast<size_t> (candidates), 0.0f);

    const float tau = static_cast<float> (fps * kWeightTauSeconds);
    const float foldTau = static_cast<float> (fps * kFoldTauSeconds);
    for (int age = 0; age < window; ++age)
    {
        weight[static_cast<size_t> (age)] = std::exp (-static_cast<float> (age) / tau);
        foldWeight[static_cast<size_t> (age)] = std::exp (-static_cast<float> (age) / foldTau);
    }

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
    acfValidTop = 0;
    challengerBpm = 0.0f;
    challengerRefreshes = 0;
    isReady = false;
    bestBpm = 0.0f;
    bestSalience = 0.0f;
    bestClarity = 0.0f;
    bestOffbeat = 1.0f;
}

float TempoEstimator::foldIntoRange (float bpmCandidate) noexcept
{
    // The winning period is the metrical level the music actually has; the
    // reported range is what a percussion clock can use. Octave-fold rather
    // than clamp, so a very fast pulse comes back as its own half.
    float v = bpmCandidate;
    for (int i = 0; i < 8 && v > kMaxBpm; ++i)
        v *= 0.5f;
    for (int i = 0; i < 8 && v < kMinBpm; ++i)
        v *= 2.0f;
    return std::clamp (v, kMinBpm, kMaxBpm);
}

float TempoEstimator::tempoPrior (float bpmCandidate) const noexcept
{
    // A tie-breaker, and nothing more. It used to be the only thing in here
    // that knew anything about metrical level, which made it the thing actually
    // choosing the level - and a Gaussian on 120 BPM chooses the double for
    // everything slow and the half for everything fast. Now that the fold
    // decides, this only has to lean on material that is genuinely ambiguous,
    // so it is centred lower and spread much wider than it was.
    const float octaves = std::log2 (std::max (1.0f, bpmCandidate) / 110.0f) / 2.2f;
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

float TempoEstimator::combStrength (float lag) const noexcept
{
    // How much pulse there is at this period at all, with no opinion about the
    // metrical level: the candidate's own harmonics, averaged over however many
    // of them the autocorrelation still covers, so a slow candidate is not
    // punished for the ones that fall off the end.
    float sum = 0.0f, norm = 0.0f;
    for (int k = 1; k <= kHarmonics; ++k)
    {
        const float at = static_cast<float> (k) * lag;
        // Stop at the last lag the autocorrelation still has enough pairs for.
        // Counting a zeroed harmonic in the average charges a slow candidate
        // for a measurement nobody made, and a slow candidate runs out of
        // harmonics first - which is exactly the octave error to avoid.
        if (at > static_cast<float> (acfValidTop))
            break;
        const float w = 1.0f / static_cast<float> (k);
        sum += w * lagAt (at);
        norm += w;
    }
    if (norm <= 0.0f)
        return 0.0f;

    const float strength = sum / norm;
    return strength > 0.0f ? strength : 0.0f;
}

float TempoEstimator::halfPhaseRatio (float lag, int n) const noexcept
{
    // Fold the activation onto `lag` and report how tall it still is half a
    // period away from its own peak, as a fraction of that peak. The profile
    // floor is subtracted first: what matters is the contrast between the beat
    // and the middle, not the DC the network sits at between beats.
    if (lag < 2.0f || n <= 0)
        return 1.0f;

    float acc[kPhaseBins] {};
    float wsum[kPhaseBins] {};
    const float binsPerFrame = static_cast<float> (kPhaseBins) / lag;

    for (int age = 0; age < n; ++age)
    {
        const int bin = static_cast<int> (static_cast<float> (age) * binsPerFrame) % kPhaseBins;
        const float w = foldWeight[static_cast<size_t> (age)];
        acc[bin] += w * lin[static_cast<size_t> (age)];
        wsum[bin] += w;
    }

    float prof[kPhaseBins];
    float lo = 0.0f, hi = 0.0f;
    int hiIdx = 0;
    bool first = true;
    for (int b = 0; b < kPhaseBins; ++b)
    {
        prof[b] = wsum[b] > 0.0f ? acc[b] / wsum[b] : 0.0f;
        if (first || prof[b] < lo)
            lo = prof[b];
        if (first || prof[b] > hi)
        {
            hi = prof[b];
            hiIdx = b;
        }
        first = false;
    }

    const float on = hi - lo;
    if (on <= 1.0e-6f)
        return 1.0f;
    const float off = prof[(hiIdx + kPhaseBins / 2) % kPhaseBins] - lo;
    return std::clamp (off / on, 0.0f, 1.0f);
}

float TempoEstimator::levelScore (float lag, int n, float& offbeatOut, bool& ruledOutFast) const noexcept
{
    const float strength = combStrength (lag);
    offbeatOut = 1.0f;
    ruledOutFast = false;
    if (strength <= 0.0f)
        return 0.0f;

    const float own = halfPhaseRatio (lag, n);
    offbeatOut = own;
    const float tooSlow = smoothStep (kTooSlowLo, kTooSlowHi, own);

    // Twice the candidate has to stay inside the window the fold can still see
    // several periods of, or the ratio is measuring the buffer, not the music.
    float tooFast = 0.0f;
    if (2.0f * lag * static_cast<float> (kMinPeriods) <= static_cast<float> (n))
    {
        const float up = halfPhaseRatio (2.0f * lag, n);
        tooFast = 1.0f - smoothStep (kTooFastLo, kTooFastHi, up);
        ruledOutFast = true;
    }

    return strength
           * (1.0f - kTooSlowPenalty * tooSlow)
           * (1.0f - kTooFastPenalty * tooFast)
           * tempoPrior (static_cast<float> (60.0 * fps) / lag);
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
        bestOffbeat = 1.0f;
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
    acfValidTop = 0;
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
        if (den >= static_cast<double> (minPairs))
        {
            acf[static_cast<size_t> (lag)] = static_cast<float> (num / (den * var));
            acfValidTop = lag;
        }
        else
        {
            acf[static_cast<size_t> (lag)] = 0.0f;
        }
    }
    if (acfValidTop < minLag)
    {
        isReady = false;
        return;
    }

    const int topIndex = std::min (candidates - 1,
                                   static_cast<int> (std::floor (static_cast<float> (lagTop - minLag) / kLagStep)));
    if (topIndex < 2)
    {
        isReady = false;
        return;
    }

    // Pass one: how much pulse each candidate period carries, with no opinion
    // about the metrical level. The fold that decides the level is far too
    // expensive to run for every one of a thousand candidates, and it does not
    // need to be: only a local maximum of this curve can be a metrical level.
    float strongest = 0.0f;
    for (int i = 0; i <= topIndex; ++i)
    {
        const float lag = static_cast<float> (minLag) + static_cast<float> (i) * kLagStep;
        const float s = combStrength (lag);
        score[static_cast<size_t> (i)] = s;
        strongest = std::max (strongest, s);
    }

    if (strongest <= 1.0e-4f)
    {
        isReady = false;
        bestSalience = 0.0f;
        bestClarity = 0.0f;
        bestOffbeat = 1.0f;
        return;
    }

    // Pass two: keep the strongest local maxima, worst-first insertion into a
    // fixed array so nothing allocates.
    int peakIdx[kMaxPeaks];
    float peakVal[kMaxPeaks];
    int nPeaks = 0;
    const float floorValue = kPeakFloor * strongest;

    for (int i = 1; i < topIndex; ++i)
    {
        const float v = score[static_cast<size_t> (i)];
        if (v < floorValue)
            continue;
        if (! (v > score[static_cast<size_t> (i - 1)] && v >= score[static_cast<size_t> (i + 1)]))
            continue;

        if (nPeaks < kMaxPeaks)
        {
            peakIdx[nPeaks] = i;
            peakVal[nPeaks] = v;
            ++nPeaks;
        }
        else
        {
            int worst = 0;
            for (int k = 1; k < kMaxPeaks; ++k)
                if (peakVal[k] < peakVal[worst])
                    worst = k;
            if (v > peakVal[worst])
            {
                peakIdx[worst] = i;
                peakVal[worst] = v;
            }
        }
    }

    if (nPeaks == 0)
    {
        // A curve with no interior maximum: take the best candidate outright so
        // a plateau still reports something.
        int bestI = 0;
        for (int i = 0; i <= topIndex; ++i)
            if (score[static_cast<size_t> (i)] > score[static_cast<size_t> (bestI)])
                bestI = i;
        peakIdx[0] = bestI;
        peakVal[0] = score[static_cast<size_t> (bestI)];
        nPeaks = 1;
    }

    // Pass three: the level decision, on the folded activation.
    int bestPeak = -1;
    float best = 0.0f;
    float rival = 0.0f;
    float finalScore[kMaxPeaks];
    float finalBpm[kMaxPeaks];
    float finalOffbeat[kMaxPeaks];
    bool  finalRuledOutFast[kMaxPeaks];

    for (int k = 0; k < nPeaks; ++k)
    {
        const float lag = static_cast<float> (minLag) + static_cast<float> (peakIdx[k]) * kLagStep;
        float offbeat = 1.0f;
        bool ruledOutFast = false;
        float s = levelScore (lag, n, offbeat, ruledOutFast);
        const float folded = foldIntoRange (static_cast<float> (60.0 * fps) / lag);

        finalScore[k] = s;
        finalBpm[k] = folded;
        finalOffbeat[k] = offbeat;
        finalRuledOutFast[k] = ruledOutFast;
        if (s > best)
        {
            best = s;
            bestPeak = k;
        }
    }

    if (bestPeak < 0 || best <= 1.0e-4f)
    {
        isReady = false;
        bestSalience = 0.0f;
        bestClarity = 0.0f;
        bestOffbeat = 1.0f;
        return;
    }

    // Hysteresis. Find the level currently being reported among this refresh's
    // peaks; if it is still there, it keeps the seat unless a rival both beats
    // it by a margin and keeps beating it.
    if (isReady && bestBpm > 0.0f)
    {
        int incumbent = -1;
        float closest = kHoldOctaves;
        for (int k = 0; k < nPeaks; ++k)
        {
            if (finalScore[k] <= 0.0f)
                continue;
            const float d = std::fabs (std::log2 (finalBpm[k] / bestBpm));
            if (d < closest)
            {
                closest = d;
                incumbent = k;
            }
        }

        if (incumbent >= 0 && incumbent != bestPeak)
        {
            const bool clearWin = finalScore[bestPeak] > finalScore[incumbent] * kSwitchMargin;
            if (clearWin)
            {
                if (challengerBpm > 0.0f
                    && std::fabs (std::log2 (finalBpm[bestPeak] / challengerBpm)) < kHoldOctaves)
                    ++challengerRefreshes;
                else
                {
                    challengerBpm = finalBpm[bestPeak];
                    challengerRefreshes = 1;
                }
            }
            else
            {
                challengerRefreshes = std::max (0, challengerRefreshes - 1);
            }

            const float incumbentLag = static_cast<float> (minLag)
                                       + static_cast<float> (peakIdx[incumbent]) * kLagStep;
            const bool levelSettled = 4.0f * incumbentLag * static_cast<float> (kMinPeriods)
                                          <= static_cast<float> (n);
            const int need = levelSettled ? kSwitchRefreshes : kSwitchRefreshesEarly;

            if (challengerRefreshes < need)
            {
                bestPeak = incumbent;
                best = finalScore[incumbent];
            }
            else
            {
                challengerBpm = 0.0f;
                challengerRefreshes = 0;
            }
        }
        else
        {
            challengerBpm = 0.0f;
            challengerRefreshes = 0;
        }
    }

    // Read off the winner, whoever hysteresis left holding the seat.
    const float bestOff = finalOffbeat[bestPeak];
    const bool bestRuledOutFast = finalRuledOutFast[bestPeak];

    // Best rival that is a different period, not the same peak a step over.
    // Octaves are left in on purpose: if one is competitive, confidence should
    // say so.
    for (int k = 0; k < nPeaks; ++k)
    {
        if (k == bestPeak)
            continue;
        if (std::fabs (peakIdx[k] - peakIdx[bestPeak]) * kLagStep
              <= 0.08f * (static_cast<float> (minLag) + static_cast<float> (peakIdx[bestPeak]) * kLagStep))
            continue;
        rival = std::max (rival, finalScore[k]);
    }

    // A level whose too-fast charge could not be evaluated is not a level, it
    // is whatever the buffer happened to be long enough to see. Early on the
    // window holds only a few seconds, so the slow half of the range is not
    // searched at all and the fastest candidate present wins by default with
    // nothing slower to be compared against - which is how a 68 BPM song gets
    // established at 136 in the first two seconds and defended ever after.
    // Saying nothing for another second costs nothing; the decoder waits.
    if (! bestRuledOutFast)
    {
        isReady = false;
        return;
    }

    bestBpm = finalBpm[bestPeak];
    // A comb that correlates at 0.5 after the offbeat charge is an unmistakable
    // pulse; scale so that maps to ~1.
    bestSalience = std::clamp (best / 0.5f, 0.0f, 1.0f);
    bestClarity = std::clamp (1.0f - rival / best, 0.0f, 1.0f);
    bestOffbeat = bestOff;
    isReady = true;
}

} // namespace vp
