#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vp
{

/**
    How long a beat is, from the harmony alone.

    Everything else in this engine times something percussive. The network is
    trained on beat activations, the kick channel is one drum, the comb folds an
    onset envelope. Point all of that at a voice with a guitar behind it and
    there is very little to work with: no kick to time, no snare to count from,
    and an activation curve BeatNet was never trained on.

    `HarmonicChange` already reads the one thing that survives - the harmony
    moving - and the app already uses it, for the **bar**: which of the four
    quarters is the one. This asks it the other question, which nothing in the
    app could answer without percussion:

        how long is a beat?

    **How.** Chords change on bar lines. So every interval between two changes
    is a whole number of bars, and the bar length that makes all of them come
    out whole is the one the band is playing. One change says almost nothing - the
    detector's window is 170 ms, which at 118 BPM is a twelfth of a bar - but
    twenty of them agreeing is a tempo.

    **Phase coherence, not intervals between consecutive changes.** That was
    tried first and measured 1 case in 7. The detector reports far more changes
    than the music has - 74 where the arrangement has 28, because a window that
    half-catches a chord reports it twice and a busy bar reports one that is not
    there - so most consecutive intervals lie between two things that are not
    chord changes. A period fitted to those is a period fitted to noise.

    What survives a high false-positive rate is the phase. Against the true bar
    length every real change has nearly the same phase and a spurious one has
    any phase at all, so summing unit vectors adds the real ones up and averages
    the false ones away. It never has to decide which changes are real, which is
    the whole point: nothing here can.

    Measured with `VPSing`, against the tempo the material was rendered at:

        voce e chitarra   92 BPM   0.43%       coerenza 0.90
                         100       0.74                 1.00
                         118       0.59                 1.00
                         132       0.65                 0.89
                         152       0.51                 1.00
        band che vaga    118       0.58                 0.97
        tempo umano      118       0.72                 0.46
        pad tenuti       118       MAI                  -
        con la batteria  118       MAI                  -

    The last two are where it breaks and they are named rather than left out. A
    sustained accompaniment - strings, an organ, anything that holds through the
    bar line - blurs the change until the detector cannot place it, and the
    scatter goes from 34 ms to 552. With drums in the signal the scatter is just
    as bad, and there it does not matter: that is the case the network and the
    kick channel already have.

    **Cost.** The search is a grid over one number, and it is spread across
    blocks rather than run at once - a full sweep completes in about a third of
    a second, at a few dozen candidates per block, so no single audio callback
    pays for it. Nothing allocates after `prepare`.
*/
class HarmonicTempo
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr > 1.0 ? sr : 48000.0;
        reset();
    }

    void reset() noexcept
    {
        count = 0;
        write = 0;
        absPos = 0;
        scan = 0;
        bestR = 0.0f;
        bestBar = 0.0f;
        runR = 0.0f;
        runBar = 0.0f;
        runBarScore = 0.0f;
        settledBpm = 0.0f;
        settledR = 0.0f;
        originSeconds = 0.0;
        lastChangeSeconds = -1.0;
    }

    /** A change the detector has just reported, at `offset` samples into the
        block that is being processed now. Cheap: a store and a counter. */
    void addChange (int offset, float strength) noexcept
    {
        if (! std::isfinite (strength) || strength <= 0.0f)
            return;
        const double t = static_cast<double> (absPos + offset) / sampleRate;
        if (t <= 0.0)
            return;
        at[write] = t;
        lastChangeSeconds = t;
        w[write] = std::max (0.05f, std::min (1.0f, strength));
        write = (write + 1) % kKeep;
        if (count < kKeep)
            ++count;
    }

    /** One block of time passing, and a slice of the search.
    
        Called once per audio callback. The sweep is amortised: `kPerBlock`
        candidates are scored here and the answer is published when the sweep
        wraps, so the cost per callback is bounded and does not depend on how
        many changes have been collected beyond the ring's own size. */
    void process (int numSamples) noexcept
    {
        absPos += numSamples;
        // A sustained chord provides no new timing evidence. Expire the
        // hypothesis, not the engine clock, after two bars (at most 12 s).
        if (lastChangeSeconds >= 0.0 && ageSeconds() > freshnessSeconds())
        {
            count = write = scan = 0;
            settledBpm = settledR = runR = runBar = 0.0f;
            lastChangeSeconds = -1.0;
        }
        if (count < kLeastChanges)
            return;

        for (int i = 0; i < kPerBlock; ++i)
        {
            const float bar = kShortestBar
                            + static_cast<float> (scan) * kBarStep;
            const float r = coherenceAt (bar);
            // The longest period that explains the run, not the best-scoring
            // one. A period that puts every change on a bar line also puts them
            // on a half bar line and a third of one, so the score peaks at
            // every division of the answer; the true bar is the longest of
            // them. Same choice BeatDecoder::userOctave makes, and for the same
            // reason - a metrical level is not decided by which peak is tallest.
            runR = std::max (runR, r);
            // Choose the longest distinct peak, then its summit. Choosing
            // every longer near-tie picked the skirt (98 instead of 100 BPM);
            // choosing only the first near-tie picked its other skirt (102).
            if (r >= runR * kCloseEnough
                && (runBar <= 0.0f || bar > runBar * 1.4f
                    || (bar < runBar * 1.15f && r > runBarScore)))
            {
                runBar = bar;
                runBarScore = r;
            }

            if (++scan >= kCandidates)
            {
                scan = 0;
                bestR = runR;
                bestBar = runBar;
                runR = 0.0f;
                runBar = 0.0f;
                runBarScore = 0.0f;
                if (bestR >= kCoherentEnough && bestBar > 0.0f)
                {
                    settledBpm = 4.0f * 60.0f / bestBar;
                    // runR may belong to the shorter candidate, whereas
                    // bestBar was extended by the longest-period preference.
                    // Phase confidence must score the period actually used.
                    settledR = coherenceAt (bestBar);
                    double re = 0.0, im = 0.0;
                    for (int k = 0; k < count; ++k)
                    {
                        const double ph = kTwoPi * at[k] / bestBar;
                        re += w[k] * std::cos (ph);
                        im += w[k] * std::sin (ph);
                    }
                    originSeconds = std::atan2 (im, re) * bestBar / kTwoPi;
                }
                else
                {
                    settledBpm = 0.0f;
                    settledR = bestR;
                }
                break;
            }
        }
    }

    /** The tempo the harmony implies, in BPM, or zero when the changes do not
        land on any period well enough to be worth saying. Zero is the important
        half of the contract: a number returned from incoherent changes is worse
        than none, because the caller would take it. */
    float bpm() const noexcept { return settledBpm; }

    /** How well the changes line up on that period, 0..1. */
    float coherence() const noexcept { return settledR; }

    /** How many changes the estimate is standing on. */
    int changes() const noexcept { return count; }

    double ageSeconds() const noexcept
    {
        return lastChangeSeconds < 0 ? 1e9 : absPos / sampleRate - lastChangeSeconds;
    }
    double freshnessSeconds() const noexcept
    {
        return settledBpm > 0 ? std::min (12.0, 480.0 / settledBpm) : 12.0;
    }
    bool phaseValid() const noexcept
    {
        return settledBpm >= 50 && settledR >= 0.80f && count >= kLeastChanges
               && ageSeconds() <= freshnessSeconds();
    }
    /** Absolute quarter position, referred to the beginning of this callback. */
    double beatPosition (int numSamples) const noexcept
    {
        return ((absPos - numSamples) / sampleRate - originSeconds) * settledBpm / 60.0;
    }

private:
    float coherenceAt (float bar) const noexcept
    {
        if (bar <= 0.0f)
            return 0.0f;
        double re = 0.0, im = 0.0, sum = 0.0;
        for (int k = 0; k < count; ++k)
        {
            const double ph = kTwoPi * at[k] / static_cast<double> (bar);
            const double wk = static_cast<double> (w[k]);
            re += wk * std::cos (ph);
            im += wk * std::sin (ph);
            sum += wk;
        }
        if (sum <= 0.0)
            return 0.0f;
        return static_cast<float> (std::sqrt (re * re + im * im) / sum);
    }

    static constexpr double kTwoPi = 6.283185307179586;
    /** A bar at four four, from 200 BPM down to 50. */
    static constexpr float kShortestBar = 1.20f;
    static constexpr float kBarStep = 0.002f;
    static constexpr int   kCandidates = 1800;
    /** How much of the sweep one audio callback pays for. A full sweep is then
        about a third of a second, which is far faster than the harmony can
        change its mind and slow enough to disappear into the callback. */
    static constexpr int   kPerBlock = 32;
    /** Changes kept. Two minutes at a chord a bar, which is long enough to
        measure and short enough that a song which really changes tempo is not
        held back by what it was doing at the top. */
    static constexpr int   kKeep = 64;
    /** Below this there is nothing to fit. */
    static constexpr int   kLeastChanges = 8;
    /** Below this the changes are not on a period at all. Measured: the cases
        that work sit at 0.89 and above, the human-timing case at 0.46, and the
        two that cannot work below 0.35. */
    static constexpr float kCoherentEnough = 0.42f;
    static constexpr float kCloseEnough = 0.95f;

    double sampleRate = 48000.0;
    double at[kKeep] {};
    float  w[kKeep] {};
    int    count = 0;
    int    write = 0;
    std::int64_t absPos = 0;

    int   scan = 0;
    float runR = 0.0f, runBar = 0.0f;
    float runBarScore = 0.0f;
    float bestR = 0.0f, bestBar = 0.0f;
    float settledBpm = 0.0f, settledR = 0.0f;
    double originSeconds = 0.0, lastChangeSeconds = -1.0;
};

} // namespace vp
