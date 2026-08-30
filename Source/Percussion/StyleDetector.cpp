#include "Percussion/StyleDetector.h"

#include <algorithm>
#include <cmath>

namespace vp
{

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    // Band edges, placed for what the app actually hears rather than for a
    // full-range mix. A phone or tablet speaker in a room, picked up by the
    // device's own microphone, has essentially nothing below ~180 Hz - the
    // first attempt put the kick band under 120 Hz and was reading an empty
    // band, which is why every four-on-the-floor track scored a different
    // evenness at every tempo.
    //
    // So "low" is the part of a kick that survives the speaker, "mid" is the
    // snare's body (a hi-hat has almost nothing under 3 kHz, so this separates
    // them), and "high" is hats and shakers.
    // Where the three drums actually are.
    //
    // These were 160-500, 500-2500 and above 6000, and the first of those does
    // not contain a kick drum: a kick's fundamental is 40 to 90 Hz, so the band
    // called "kick" held the bass guitar's harmonics, the snare's shell and the
    // low end of a guitar - everything except the thing it was named after. The
    // band called "snare" started at 500, above the shell tone that makes a
    // backbeat sound like a backbeat.
    //
    // Measured on material of known style, with the old bands: the four-on-the
    // -floor test read 0.68 on a dance track and 0.64 on a rock one, and the
    // backbeat test read 0.02 on rock - a feature that is supposed to name rock
    // reading as zero on rock. Both were describing the bass line.
    constexpr float kLowLoHz = 35.0f;     // under a kick's fundamental
    constexpr float kLowHiHz = 110.0f;    // and over it
    constexpr float kMidLoHz = 150.0f;    // the snare's shell tone starts here
    constexpr float kMidHiHz = 600.0f;
    constexpr float kHighHz  = 5000.0f;

    // The bins forget over about this many bars, so a song that changes section
    // is followed rather than averaged away.
    constexpr float kMemoryBars = 8.0f;

    // Nothing is decided before this much has been folded in. Two bars is not
    // enough to tell a backbeat from a fill.
    constexpr float kMinBars = 4.0f;

    // A rival has to stay ahead for this many bars before it takes over. The
    // part changing under the listener is worse than the part being slightly
    // wrong, so this is deliberately slow.
    constexpr int kSwitchBars = 4;

    // All four thresholds sit in the gaps measured across material whose style
    // is known - they are not guesses, and the first set of guesses got one
    // case in nine right.

    // Under this much offbeat hat, relative to the hats on the beat, the record
    // is not putting anything between the beats. Measured: pop 0.13-0.16, and
    // 0.43 at the lowest for anything else.
    constexpr float kSparseHats = 0.28f;

    // And over this much, there is an open hat on every offbeat. Measured:
    // dance 0.90-0.95, rock 0.43-0.45.
    constexpr float kOpenHatEveryOffbeat = 0.70f;

    // Energy on the odd sixteenths relative to the beats. Measured: latin
    // 1.23-1.70 against 0.80-1.26 for the rest, so the line is towards the top
    // of that overlap - a latin record that reads low is played as rock, which
    // is a smaller mistake than a rock record played as a marcha.
    constexpr float kSyncopated = 1.30f;

    // A sixteenth counts as occupied at this fraction of the busiest one.
    constexpr float kOccupied = 0.22f;

    // Below this many occupied sixteenths the track wants to be left alone.
    constexpr int kBusyBins = 7;

    float onePoleCoeff (float hz, double sr) noexcept
    {
        return 1.0f - std::exp (-2.0f * kPi * hz / static_cast<float> (sr));
    }

    float sumOf (const float* bins, std::initializer_list<int> which) noexcept
    {
        float s = 0.0f;
        for (int i : which)
            s += bins[i];
        return s;
    }
}

void StyleDetector::prepare (double sr) noexcept
{
    sampleRate = sr > 1.0 ? sr : 48000.0;
    aLowLo = onePoleCoeff (kLowLoHz, sampleRate);
    aLowHi = onePoleCoeff (kLowHiHz, sampleRate);
    aMidLo = onePoleCoeff (kMidLoHz, sampleRate);
    aMidHi = onePoleCoeff (kMidHiHz, sampleRate);
    aHigh = onePoleCoeff (kHighHz, sampleRate);
    // ~30 ms: long enough to ride a drum's body, short enough that two
    // sixteenths at 200 BPM do not merge.
    release = 1.0f - std::exp (-1.0f / (0.030f * static_cast<float> (sampleRate)));
    reset();
}

void StyleDetector::reset() noexcept
{
    lpLowLo = lpLowHi = lpMidLo = lpMidHi = lpHigh = 0.0f;
    envKick = envBody = envHigh = 0.0f;
    std::fill (binKick, binKick + kBins, 0.0f);
    std::fill (binBody, binBody + kBins, 0.0f);
    std::fill (binHigh, binHigh + kBins, 0.0f);
    current = GrooveStyle::pop;
    challenger = GrooveStyle::pop;
    challengerBars = 0;
    score = 0.0f;
    observed = 0.0f;
    lastBarPhase = 0.0f;
    sinceDecide = 0;
}

void StyleDetector::process (const float* mono, int numSamples, float barPhase, bool stable) noexcept
{
    if (mono == nullptr || numSamples <= 0)
        return;

    const float phase = wrap01 (barPhase);

    // A bar boundary: age the bins, and count what has been seen.
    if (phase < lastBarPhase)
    {
        const float decay = std::exp (-1.0f / kMemoryBars);
        for (int i = 0; i < kBins; ++i)
        {
            binKick[i] *= decay;
            binBody[i] *= decay;
            binHigh[i] *= decay;
        }
        if (stable)
            observed = std::min (observed + 1.0f, 64.0f);
        if (++sinceDecide >= 1)
        {
            sinceDecide = 0;
            decide();
        }
    }
    lastBarPhase = phase;

    if (! stable)
        return;

    // One bin for the whole block. At 5 ms a block and 125 ms a sixteenth at
    // 120 BPM there are more than twenty blocks to a bin, so the extra
    // precision of doing it per sample would buy nothing.
    const int bin = std::clamp (static_cast<int> (phase * kBins), 0, kBins - 1);

    float accKick = 0.0f, accBody = 0.0f, accHigh = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float x = mono[i];
        lpLowLo += aLowLo * (x - lpLowLo);
        lpLowHi += aLowHi * (x - lpLowHi);
        lpMidLo += aMidLo * (x - lpMidLo);
        lpMidHi += aMidHi * (x - lpMidHi);
        lpHigh  += aHigh  * (x - lpHigh);

        const float kick = std::fabs (lpLowHi - lpLowLo);   // 35 - 110 Hz
        const float body = std::fabs (lpMidHi - lpMidLo);   // 150 - 600 Hz
        const float high = std::fabs (x - lpHigh);          // above 5 kHz

        // Half-wave rectified rise, not level: a bass line sustains, a kick
        // strikes, and only one of the two is a drum.
        accKick += std::max (0.0f, kick - envKick);
        accBody += std::max (0.0f, body - envBody);
        accHigh += std::max (0.0f, high - envHigh);

        envKick += release * (kick - envKick);
        envBody += release * (body - envBody);
        envHigh += release * (high - envHigh);
    }

    binKick[bin] += accKick;
    binBody[bin] += accBody;
    binHigh[bin] += accHigh;
}

void StyleDetector::decide() noexcept
{
    if (observed < kMinBars)
    {
        score = 0.0f;
        return;
    }

    // Everything below has to be invariant to *which* beat the clock calls
    // one. The tracker finds the beat reliably and the bar much less so, and
    // measuring "are 2 and 4 louder than 1 and 3" against a bar that is rotated
    // by a beat gives the exact opposite answer - a half-time track with the
    // snare on 3 scored the highest backbeat of anything tested, which is how
    // this was found.
    //
    // So the beat energies are summed over the whole quarter, and the backbeat
    // is read as the *depth of the two-beat alternation* rather than as which
    // pair is louder. A rotation by one beat flips the sign of that difference
    // and leaves its magnitude alone.
    float quarterKick[4] {}, quarterBody[4] {};
    for (int b = 0; b < 4; ++b)
        for (int k = 0; k < 4; ++k)
        {
            quarterKick[b] += binKick[b * 4 + k];
            quarterBody[b] += binBody[b * 4 + k];
        }

    const float kickLo = std::min (std::min (quarterKick[0], quarterKick[1]),
                                   std::min (quarterKick[2], quarterKick[3]));
    const float kickHi = std::max (std::max (quarterKick[0], quarterKick[1]),
                                   std::max (quarterKick[2], quarterKick[3]));
    const float evenKick = kickHi > 1.0e-9f ? kickLo / kickHi : 0.0f;

    const float bodyTotal = quarterBody[0] + quarterBody[1] + quarterBody[2] + quarterBody[3];
    const float alternation = bodyTotal > 1.0e-9f
        ? std::fabs ((quarterBody[0] + quarterBody[2]) - (quarterBody[1] + quarterBody[3])) / bodyTotal
        : 0.0f;

    // The offbeat is half a beat from the beat, so a rotation by whole beats
    // leaves this alone.
    const float highOn = sumOf (binHigh, { 0, 4, 8, 12 });
    const float highOff = sumOf (binHigh, { 2, 6, 10, 14 });
    const float offHigh = highOn > 1.0e-9f ? highOff / highOn : 0.0f;

    // The low band is left out of this one: a bass note sustains through the
    // sixteenths between the beats and would read as syncopation that nobody
    // played.
    const float odd = sumOf (binBody, { 1, 3, 5, 7, 9, 11, 13, 15 })
                    + sumOf (binHigh, { 1, 3, 5, 7, 9, 11, 13, 15 });
    const float even = sumOf (binBody, { 0, 4, 8, 12 }) + sumOf (binHigh, { 0, 4, 8, 12 });
    const float syncopation = even > 1.0e-9f ? odd / even : 0.0f;

    // How much of the bar has anything happening in it. A ballad has a kick, a
    // snare and some air; a latin bar has something on nearly every sixteenth.
    // Scale-free and rotation-free, and the cleanest way to spot a part that
    // wants to be left alone.
    float loudest = 0.0f;
    for (int i = 0; i < kBins; ++i)
        loudest = std::max (loudest, binKick[i] + binBody[i] + binHigh[i]);
    int occupancy = 0;
    if (loudest > 1.0e-9f)
        for (int i = 0; i < kBins; ++i)
            if ((binKick[i] + binBody[i] + binHigh[i]) > kOccupied * loudest)
                ++occupancy;

    lastFeatures = { evenKick, alternation, offHigh, syncopation,
                     static_cast<float> (occupancy) };

    // Ordered by how unambiguous each test is, most first - and rebuilt around
    // the two measurements that survived being scored.
    //
    // The bench in VPTests puts twelve records of known style through this, at
    // three tempi each and with the syncopation and the pad varying, and the
    // four features do not do equally well:
    //
    //   offHigh       pop 0.13-0.16 against 0.43-0.95 for everything else.
    //                 A clean gap, and the only one there is.
    //   syncopation   latin 1.23-1.70 against 0.80-1.26. Mostly a gap.
    //   evenKick      rock 0.34-0.59, dance 0.45-0.56, latin 0.32-0.61,
    //                 pop 0.33-0.60. **Complete overlap.**
    //   alternation   0.09-0.22 for all four. **Complete overlap.**
    //
    // The last two are the ones that were supposed to name four-on-the-floor
    // and a backbeat, and they name nothing. The reason is the same for both
    // and it is not a threshold: a bass guitar's fundamental sits at 55-110 Hz
    // and its second harmonic at 150-250, which is exactly where a kick and a
    // snare's shell are, so the two bands hold a bass line as much as they hold
    // drums. Half-wave rectifying the rise takes out the sustain and not the
    // articulation - a bass is plucked, and a pluck is an attack.
    //
    // Separating them needs the bass taken out of the drums, which is either
    // source separation or the desk's own kick channel - the app has the second
    // when the listener gives it one (see Tracking/KickOnsetDetector.h) and this
    // does not use it yet. Until then the honest thing is to decide on the two
    // features that work and to leave the two that do not out of the decision
    // rather than let them vote.
    GrooveStyle want = GrooveStyle::rock;
    float margin = 0.0f;

    if (offHigh < kSparseHats)
    {
        // Almost nothing between the beats: a record that wants to be left
        // alone.
        want = GrooveStyle::pop;
        margin = (kSparseHats - offHigh) / kSparseHats;
    }
    else if (syncopation > kSyncopated)
    {
        // Energy on the odd sixteenths, and a lot of it.
        want = GrooveStyle::marcha;
        margin = (syncopation - kSyncopated) / kSyncopated;
    }
    else if (offHigh > kOpenHatEveryOffbeat)
    {
        // An open hat on every offbeat is what names four-on-the-floor here,
        // since the kick band cannot.
        want = GrooveStyle::dance;
        margin = (offHigh - kOpenHatEveryOffbeat) / 0.20f;
    }
    else
    {
        // Everything else. Not a guess of last resort: a record with ordinary
        // eighth hats, no latin sixteenths and no open hat on the offbeat is a
        // rock record, and that is three positive statements.
        want = GrooveStyle::rock;
        margin = (kOpenHatEveryOffbeat - offHigh) / kOpenHatEveryOffbeat;
    }

    score = std::clamp (margin, 0.0f, 1.0f) * std::clamp (observed / 8.0f, 0.0f, 1.0f);

    // Hysteresis. A part that changes under the listener is worse than a part
    // that is slightly wrong, so a rival has to keep the lead for several bars.
    if (want == current)
    {
        challengerBars = 0;
        challenger = current;
        return;
    }
    if (want == challenger)
        ++challengerBars;
    else
    {
        challenger = want;
        challengerBars = 1;
    }
    if (challengerBars >= kSwitchBars)
    {
        current = challenger;
        challengerBars = 0;
    }
}

} // namespace vp
