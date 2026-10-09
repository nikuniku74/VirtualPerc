#pragma once

#include "Core/Types.h"

namespace vp
{

/**
    Which part the music is asking for, decided from the music.

    This is deliberately not genre classification. Genre is a hard, open problem
    and it is not the question: the four parts differ from each other by
    *rhythm*, and rhythm is measurable from what the engine already has. Folding
    the analysis signal onto the bar in three bands answers all of it —

      - a kick of even weight on all four beats is four-on-the-floor, and the
        offbeat hat that comes with it puts energy half way between the beats;
      - a snare on 2 and 4 and not on 1 and 3 is a backbeat;
      - energy on the odd sixteenths, without four-on-the-floor, is the busy
        syncopation of a latin part;
      - none of the above, played moderately, is a pop record.

    Runs on the audio thread: three one-pole filters and an accumulate per
    sample, no allocation after prepare().
*/
class StyleDetector
{
public:
    static constexpr int kBins = 16;   // sixteenths of a bar

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** One block of the analysis signal, with the clock's position in the bar
        at the start of it. `stable` gates learning: there is no point folding
        audio onto a bar the clock has not found yet. */
    void process (const float* mono, int numSamples, float barPhase, bool stable) noexcept;

    /** The style the music is asking for. Holds its previous answer until a
        rival is clearly and repeatedly better. */
    GrooveStyle style() const noexcept { return current; }

    /** 0..1. Below ~0.3 the evidence is thin and a caller may prefer to keep
        whatever it was already playing. */
    float confidence() const noexcept { return score; }

    /** `backbeatSide` and `kickSide` are the signed forms of the alternation,
        -1..1: positive when the snare's attacks sit on the clock's 2 and 4 and
        the kick's on its 1 and 3. Unlike the rest these do depend on which
        beat the clock calls one - that is what they are for (docs/TODO.md
        item 119). */
    struct Features { float evenKick, alternation, offHigh, syncopation, occupancy,
                      backbeatSide, kickSide; };
    Features features() const noexcept { return lastFeatures; }

private:
    void decide() noexcept;

    double sampleRate = 48000.0;

    // Band splitters. One pole each: the ear reads which band an onset is in,
    // not the slope of the filter that found it.
    float lpLowLo = 0.0f, lpLowHi = 0.0f, lpMidLo = 0.0f, lpMidHi = 0.0f, lpHigh = 0.0f;
    float aLowLo = 0.0f, aLowHi = 0.0f, aMidLo = 0.0f, aMidHi = 0.0f, aHigh = 0.0f;

    // Envelope followers, and their previous values, because what identifies a
    // drum is the attack: a bass line sustains, a kick strikes.
    float envKick = 0.0f, envBody = 0.0f, envHigh = 0.0f;
    float release = 0.0f;

    float binKick[kBins] {};
    float binBody[kBins] {};
    float binHigh[kBins] {};

    GrooveStyle current = GrooveStyle::pop;
    GrooveStyle challenger = GrooveStyle::pop;
    int   challengerBars = 0;
    float score = 0.0f;
    float observed = 0.0f;
    float lastBarPhase = 0.0f;
    int   sinceDecide = 0;
    Features lastFeatures {};
};

} // namespace vp
