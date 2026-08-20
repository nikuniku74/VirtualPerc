#include "AI/BeatHmm.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vp
{

namespace
{
    constexpr float kNegInf = -1.0e30f;

    // Tempi a tempo change may reach in one beat, either side. Two steps of the
    // state space is a few BPM: a band speeding up does it over bars, not
    // inside one beat, and anything that needs to move faster than this is a
    // different song rather than the same one played quicker.
    constexpr int kReach = 2;

    // Where a listener's sense of pulse sits. Not a tie-breaker bolted on
    // afterwards - it is part of the model, because it is part of hearing: the
    // reason nobody taps 184 to a slow rock tune is not that 184 does not fit,
    // it is that 184 is not a pulse.
    constexpr float kPriorCentreBpm = 118.0f;

    // How wide the beat is, as a fraction of the period, and how the "not a
    // beat" mass is shared out. Both come from one constant, and it has to be a
    // constant rather than the period itself: normalising by the period charges
    // a slow tempo for every frame it spends between its beats, which is a bias
    // towards the fastest state in the space and nothing else. With this, every
    // tempo spends the same *fraction* of its time on the beat, so the model
    // compares them on the evidence instead of on their length. Sixteen is
    // madmom's figure and it is not a delicate choice.
    constexpr float kObsLambda = 16.0f;

    inline float logSumExp (float a, float b) noexcept
    {
        if (a < b) std::swap (a, b);
        if (b <= kNegInf * 0.5f) return a;
        return a + std::log1p (std::exp (b - a));
    }
}

void BeatHmm::prepare (double framesPerSecond)
{
    fps = framesPerSecond > 1.0 ? framesPerSecond : 50.0;

    // Round inwards, so the space really covers [kMinBpm, kMaxBpm] and does not
    // spill a state past either end.
    tauMin = std::max (2, static_cast<int> (std::ceil (fps * 60.0 / kMaxBpm)));
    tauMax = std::max (tauMin + 2, static_cast<int> (std::floor (fps * 60.0 / kMinBpm)));
    numTempi = tauMax - tauMin + 1;

    base.assign (static_cast<size_t> (numTempi), 0);
    tau.assign (static_cast<size_t> (numTempi), 0);
    int n = 0;
    for (int i = 0; i < numTempi; ++i)
    {
        tau[static_cast<size_t> (i)] = tauMin + i;
        base[static_cast<size_t> (i)] = n;
        n += tauMin + i;
    }
    numStates = n;

    alpha.assign (static_cast<size_t> (numStates), 0.0f);
    next.assign (static_cast<size_t> (numStates), 0.0f);
    logPrior.assign (static_cast<size_t> (numTempi), 0.0f);

    // Two beats of the slowest tempo before anything is reported: below that
    // the forward pass has not seen a whole period of most of the state space.
    warmupFrames = tauMax * 2;

    rebuildPrior();
    reset();
}

void BeatHmm::setPriorWidth (float octaves) noexcept
{
    priorWidth = octaves;
    rebuildPrior();
}

void BeatHmm::setPriorCentre (float bpm) noexcept
{
    priorCentre = std::clamp (bpm, 60.0f, 200.0f);
    rebuildPrior();
}

void BeatHmm::rebuildPrior() noexcept
{
    for (int i = 0; i < numTempi; ++i)
    {
        const float bpm = static_cast<float> (60.0 * fps) / static_cast<float> (tau[static_cast<size_t> (i)]);
        const float oct = std::log2 (bpm / priorCentre) / std::max (0.05f, priorWidth);
        logPrior[static_cast<size_t> (i)] = -0.5f * oct * oct;
    }
}

void BeatHmm::reset() noexcept
{
    frames = 0;
    reportedBpm = 0.0f;
    reportedPhase = 0.0f;
    margin = 0.0f;
    beatNow = false;
    if (numStates <= 0)
        return;

    // Flat over phase, prior over tempo: nothing is known about where in the
    // beat we are, and the only thing known about the tempo is what a listener
    // brings to it.
    const float flat = -std::log (static_cast<float> (numStates));
    for (int i = 0; i < numTempi; ++i)
        for (int p = 0; p < tau[static_cast<size_t> (i)]; ++p)
            alpha[static_cast<size_t> (base[static_cast<size_t> (i)] + p)] = flat + logPrior[static_cast<size_t> (i)];
}

void BeatHmm::push (float activation) noexcept
{
    if (numStates <= 0)
        return;

    const float act = std::clamp (activation, 1.0e-4f, 1.0f - 1.0e-4f);
    const float logBeat = std::log (act);
    const float logNot  = std::log (1.0f - act);

    std::fill (next.begin(), next.end(), kNegInf);

    // Advance one position inside each tempo, and let the last position of a
    // beat cross into a neighbouring tempo at a price.
    for (int i = 0; i < numTempi; ++i)
    {
        const int t = tau[static_cast<size_t> (i)];
        const int b = base[static_cast<size_t> (i)];

        for (int p = 0; p + 1 < t; ++p)
        {
            const float a = alpha[static_cast<size_t> (b + p)];
            if (a <= kNegInf * 0.5f)
                continue;
            float& dst = next[static_cast<size_t> (b + p + 1)];
            dst = dst <= kNegInf * 0.5f ? a : logSumExp (dst, a);
        }

        // The end of a beat: the next frame is a beat, in this tempo or a
        // neighbouring one.
        const float a = alpha[static_cast<size_t> (b + t - 1)];
        if (a <= kNegInf * 0.5f)
            continue;

        for (int d = -kReach; d <= kReach; ++d)
        {
            const int j = i + d;
            if (j < 0 || j >= numTempi)
                continue;
            const int t2 = tau[static_cast<size_t> (j)];
            // Cost of the change itself, as a fraction of the period, plus what
            // a listener thinks of the tempo being moved to.
            // Absolute, not squared. Squared makes a one-step change almost
            // free - at a tenth of a percent of the period the square is a
            // thousandth - and a tracker that can change tempo for free
            // wanders across the whole space, which is exactly what it did.
            const float rel = std::fabs (static_cast<float> (t2 - t) / static_cast<float> (t));
            const float cost = -changeLambda * rel;
            const float v = a + cost + logPrior[static_cast<size_t> (j)];
            float& dst = next[static_cast<size_t> (base[static_cast<size_t> (j)])];
            dst = dst <= kNegInf * 0.5f ? v : logSumExp (dst, v);
        }
    }

    // The observation. The beat is not one frame, it is the first sixteenth of
    // the period - a network's activation is a bump, not a spike - and the "no
    // beat here" mass is shared over a *fixed* number of parts rather than over
    // the period. See kObsLambda: normalising by the period is what made the
    // whole space collapse onto its fastest tempo.
    const float spread = logNot - std::log (kObsLambda - 1.0f);
    float best = kNegInf, norm = kNegInf;
    int bestIndex = 0;
    for (int i = 0; i < numTempi; ++i)
    {
        const int t = tau[static_cast<size_t> (i)];
        const int b = base[static_cast<size_t> (i)];
        const int beatWide = beatWidthFrames > 0
                                 ? std::min (t, beatWidthFrames)
                                 : std::max (1, static_cast<int> (static_cast<float> (t) / kObsLambda));
        for (int p = 0; p < t; ++p)
        {
            float& v = next[static_cast<size_t> (b + p)];
            if (v <= kNegInf * 0.5f)
                continue;
            v += (p < beatWide ? logBeat : spread);
            if (v > best) { best = v; bestIndex = b + p; }
            norm = logSumExp (norm, v);
        }
    }

    if (norm <= kNegInf * 0.5f)
    {
        reset();
        return;
    }
    for (auto& v : next)
        if (v > kNegInf * 0.5f)
            v -= norm;

    alpha.swap (next);
    ++frames;

    // Read the answer off the most likely state.
    int wi = 0;
    while (wi + 1 < numTempi && base[static_cast<size_t> (wi + 1)] <= bestIndex)
        ++wi;
    const int wt = tau[static_cast<size_t> (wi)];
    const int wp = bestIndex - base[static_cast<size_t> (wi)];
    reportedBpm = static_cast<float> (60.0 * fps) / static_cast<float> (wt);
    reportedPhase = static_cast<float> (wp) / static_cast<float> (wt);
    beatNow = wp == 0;

    // How much better the winning tempo is than the best rival at a different
    // metrical level. Compared as *tempi*, not as single states: the probability
    // of a tempo is spread over all the phases it could be at, and comparing
    // one phase of one against one phase of the other throws away almost all of
    // it. Summed, the answer arrives in seconds instead of half a minute.
    float mine = kNegInf, rival = kNegInf;
    for (int i = 0; i < numTempi; ++i)
    {
        const int b = base[static_cast<size_t> (i)];
        float m = kNegInf;
        for (int p = 0; p < tau[static_cast<size_t> (i)]; ++p)
            m = logSumExp (m, alpha[static_cast<size_t> (b + p)]);
        if (std::abs (i - wi) <= 2)
            mine = std::max (mine, m);
        else
            rival = std::max (rival, m);
    }
    margin = rival <= kNegInf * 0.5f ? 99.0f : mine - rival;
}

} // namespace vp
