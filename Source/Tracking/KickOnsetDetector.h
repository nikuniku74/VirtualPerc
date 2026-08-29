#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vp
{

/**
    Where the kick drum struck, to the sample, on a channel that carries only
    the kick drum.

    This exists because of what a live rig actually hands the app. On any
    digital console the kick has its own channel, and that channel is a
    different signal from the mix in every way that matters here: one
    instrument, one attack per event, no bass note sustaining through it, and -
    the part nothing else in the chain can see - **silent for exactly as long as
    the drummer is not playing**.

    Three things follow, and they are the three the rest of the tracker is worst
    at:

    - **Phase, to the sample.** The neural path reports on a 20 ms frame grid,
      so its beat times carry +/-10 ms of quantisation before any other error is
      counted. A kick strike is the sharpest event on a stage and there is
      nothing else on the channel to confuse it with, so it can be timed on the
      audio itself.
    - **The metrical level.** A kick pattern states the beat. The fold has to
      infer it from an activation curve where the eighths can be as tall as the
      beats - see BeatDecoder::userOctave for why that is not solvable in
      general - and a channel with one event per kick simply does not have that
      ambiguity in it.
    - **The drummer stopping.** docs/STATUS.md spends two sections trying to
      tell "the kit went out" from "the band is speeding up" using the fit, and
      concludes it cannot be done early enough from there. On this channel it is
      not an inference at all: the channel goes quiet.

    Deliberately not a general onset detector. It is a band-limited envelope
    with a rise test and a refractory period, which is all a kick channel needs
    and is cheap enough to run on the audio thread: four one-pole filters and a
    handful of comparisons per sample, no allocation, no history buffer.
*/
class KickOnsetDetector
{
public:
    struct Onset
    {
        /** Samples from the start of the block this was reported in, and it
            may be **negative**.

            A strike is timed at the foot of its rise, but it cannot be
            *confirmed* there - at that instant it is indistinguishable from the
            channel getting louder - so confirmation takes a few milliseconds
            and by then the block carrying the rise may have gone. Reporting the
            confirmation time instead was worth 70 ms of systematic lateness,
            measured, which is three times the frame grid this exists to beat.
            So the timestamp is the rise and the offset is signed. */
        int  offset = 0;
        /** How hard, 0..1, from the peak of the envelope's rise. */
        float strength = 0.0f;
    };

    static constexpr int kMaxOnsets = 8;

    void prepare (double sr) noexcept
    {
        sampleRate = sr > 1.0 ? sr : 48000.0;
        // The band the kick's body lives in. Above 180 Hz is snare shell and
        // guitar; below 30 Hz is stage rumble and the desk's own offset.
        aLo = onePole (30.0f);
        aHi = onePole (180.0f);
        // Fast enough to sit on the strike, slow enough not to follow the
        // waveform's own cycles - a 50 Hz kick is 20 ms a cycle, so an
        // envelope that tracked it would rise and fall four times per hit.
        aAttack = onePole (140.0f);
        aRelease = 1.0f - std::exp (-1.0f / static_cast<float> (sampleRate * 0.120));
        reset();
    }

    void reset() noexcept
    {
        lpLo = lpHi = env = slow = 0.0f;
        refractory = 0;
        quietSamples = 0;
        peakEnv = 0.0f;
        rising = false;
        riseAt = 0;
        riseFrom = 0.0f;
        lastLevel = 0.0f;
        absPos = 0;
    }

    /** One block of the kick channel. Returns how many onsets were written to
        `out`, each with its offset inside this block.

        Runs on the audio thread. */
    int process (const float* x, int numSamples, Onset* out, int maxOut) noexcept
    {
        int n = 0;
        if (x == nullptr || out == nullptr || maxOut <= 0)
            return 0;

        const std::int64_t blockStart = absPos;
        const int maxRise = static_cast<int> (sampleRate * kConfirmSec);

        for (int i = 0; i < numSamples; ++i, ++absPos)
        {
            // Band-pass by difference of two one-poles, then rectify.
            lpLo += aLo * (x[i] - lpLo);
            lpHi += aHi * (x[i] - lpHi);
            const float band = std::fabs (lpHi - lpLo);

            // Two envelopes: one that can keep up with a strike and one that
            // cannot. A strike is where the fast one leaves the slow one
            // behind, which is a test on the *shape* of the rise rather than on
            // a level - so it works at whatever gain the desk is sending.
            env += (band > env ? aAttack : aRelease) * (band - env);
            slow += aRelease * 0.25f * (band - slow);

            if (refractory > 0)
                --refractory;

            const float floorNow = std::max (kAbsoluteFloor, slow * kOverSlow);
            if (! rising)
            {
                if (refractory == 0 && env > floorNow && env > lastLevel * kRiseRatio)
                {
                    rising = true;
                    riseAt = absPos;
                    riseFrom = lastLevel;
                    peakEnv = env;
                }
            }
            else
            {
                peakEnv = std::max (peakEnv, env);
                // Timed at the foot of the rise, never at the peak: the peak of
                // a kick's envelope is ten to twenty milliseconds into the body,
                // and what a listener hears as the beat - and what the grid has
                // to be aligned to - is where it began.
                //
                // Confirmed either when the envelope stops climbing or when the
                // confirmation window runs out, whichever is sooner. The window
                // is what bounds the report's own latency: a kick's envelope
                // takes 70 ms to fall back through three quarters of its peak,
                // and waiting for that put every strike two blocks late.
                const bool fallen = env < peakEnv * kFallenTo;
                const bool timedOut = absPos - riseAt >= maxRise;
                if (fallen || timedOut)
                {
                    if (n < maxOut && peakEnv > riseFrom * kRiseRatio)
                    {
                        out[n].offset = static_cast<int> (riseAt - blockStart);
                        out[n].strength = std::clamp (peakEnv / kFullScale, 0.0f, 1.0f);
                        ++n;
                    }
                    rising = false;
                    refractory = static_cast<int> (sampleRate * kRefractorySec);
                    peakEnv = 0.0f;
                }
            }

            lastLevel += aRelease * (env - lastLevel);

            if (env > kAbsoluteFloor)
                quietSamples = 0;
            else if (quietSamples < kQuietCap)
                ++quietSamples;
        }
        return n;
    }

    /** How long the channel has been silent, in seconds. On a kick channel this
        is not a diagnostic: it is the answer to "has the drummer stopped",
        which nothing else in the chain can establish - see the class note. */
    float quietSeconds() const noexcept
    {
        return static_cast<float> (quietSamples) / static_cast<float> (sampleRate);
    }

    /** The envelope as it stands, for the debug panel. */
    float level() const noexcept { return env; }

private:
    float onePole (float hz) const noexcept
    {
        return 1.0f - std::exp (-2.0f * 3.14159265358979f * hz
                                / static_cast<float> (sampleRate));
    }

    /** Below this the channel counts as silent whatever the ratios say. A desk
        sends its noise floor even with nothing plugged in. */
    static constexpr float kAbsoluteFloor = 0.0015f;
    /** How far above its own recent level the envelope has to jump. */
    static constexpr float kRiseRatio = 2.2f;
    /** And above the slow average, so a channel fading up is not a strike. */
    static constexpr float kOverSlow = 1.8f;
    /** The rise is over once the envelope has fallen this far from its peak. */
    static constexpr float kFallenTo = 0.72f;
    /** And in any case it is confirmed within this long of starting, so the
        report is never more than one small block behind the strike. */
    static constexpr double kConfirmSec = 0.012;
    /** Two kicks closer than this are one kick with a flam on it. A
        thirty-second at 200 BPM is 37 ms, and no kick drum plays those. */
    static constexpr double kRefractorySec = 0.055;
    /** What counts as a full-strength strike, for the reported 0..1. */
    static constexpr float kFullScale = 0.30f;
    static constexpr int kQuietCap = 1 << 24;

    double sampleRate = 48000.0;
    float aLo = 0.0f, aHi = 0.0f, aAttack = 0.0f, aRelease = 0.0f;
    float lpLo = 0.0f, lpHi = 0.0f, env = 0.0f, slow = 0.0f, lastLevel = 0.0f;
    float peakEnv = 0.0f, riseFrom = 0.0f;
    int   refractory = 0;
    /** Absolute sample positions, because a rise and its confirmation can fall
        in different blocks. */
    std::int64_t absPos = 0;
    std::int64_t riseAt = 0;
    int   quietSamples = 0;
    bool  rising = false;
};

} // namespace vp
