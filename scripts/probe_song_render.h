// Shared render for the song probes: a full arrangement at a perfectly fixed
// tempo, then an iPad speaker, a room and an iPad microphone in front of it.
#pragma once

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace vp::probe
{
constexpr double kPi = 3.14159265358979323846;

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void highpass (double sr, double f, double q)
    {
        const double w = 2.0 * kPi * f / sr, c = std::cos (w), s = std::sin (w);
        const double alpha = s / (2.0 * q), a0 = 1.0 + alpha;
        b0 = static_cast<float> ((1.0 + c) * 0.5 / a0);
        b1 = static_cast<float> (-(1.0 + c) / a0);
        b2 = b0;
        a1 = static_cast<float> (-2.0 * c / a0);
        a2 = static_cast<float> ((1.0 - alpha) / a0);
    }

    void lowpass (double sr, double f, double q)
    {
        const double w = 2.0 * kPi * f / sr, c = std::cos (w), s = std::sin (w);
        const double alpha = s / (2.0 * q), a0 = 1.0 + alpha;
        b0 = static_cast<float> ((1.0 - c) * 0.5 / a0);
        b1 = static_cast<float> ((1.0 - c) / a0);
        b2 = b0;
        a1 = static_cast<float> (-2.0 * c / a0);
        a2 = static_cast<float> ((1.0 - alpha) / a0);
    }

    float process (float x) noexcept
    {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// One-pole envelope, used for every percussive voice.
inline float decay (float t, float rate) { return std::exp (-t * rate); }

inline float noiseAt (std::mt19937& rng)
{
    return std::uniform_real_distribution<float> (-1.0f, 1.0f) (rng);
}

struct SongOptions
{
    float bpm = 120.0f;
    bool  syncopated = false;   // bass and kick off the grid
    bool  sustained = false;    // pads and strings holding through the beats
    bool  halfTimeFeel = false; // snare on 3 only
    bool  breakdown = true;     // eight bars with the drums out
    bool  fills = true;
};

// A full mix at a fixed tempo. Beat one is at sample zero, so the true beat
// phase at any sample is exactly known.
inline void renderSong (std::vector<float>& dest, const SongOptions& opt, double sr, unsigned seed)
{
    std::mt19937 rng (seed);
    const double beatSec = 60.0 / static_cast<double> (opt.bpm);
    const double inc = 1.0 / (beatSec * sr);

    // Chord roots for a four-bar loop, in Hz.
    const float roots[4] = { 110.0f, 146.83f, 98.0f, 130.81f };

    double ph = 0.0;
    for (size_t i = 0; i < dest.size(); ++i)
    {
        const double beats = ph;
        const int    beatIdx = static_cast<int> (std::floor (beats));
        const double inBeat = beats - std::floor (beats);
        const int    bar = beatIdx / 4;
        const int    beatInBar = beatIdx % 4;
        const float  tBeat = static_cast<float> (inBeat * beatSec);
        const double sixteenth = inBeat * 4.0 - std::floor (inBeat * 4.0);
        const float  tSix = static_cast<float> (sixteenth * beatSec * 0.25);
        const double eighth = inBeat * 2.0 - std::floor (inBeat * 2.0);
        const float  tEig = static_cast<float> (eighth * beatSec * 0.5);
        const int    sixIdx = static_cast<int> (inBeat * 4.0);

        const bool drumsOut = opt.breakdown && (bar % 16) >= 8 && (bar % 16) < 12;
        const bool fillBar = opt.fills && (bar % 8) == 7;
        const float root = roots[bar % 4];

        float s = 0.0f;

        if (! drumsOut)
        {
            // Kick. On one and three, plus a pushed sixteenth before three when
            // the groove is syncopated - which is where a beat tracker most
            // often finds a beat that is not one.
            const bool kickHere = (beatInBar == 0 || beatInBar == 2);
            if (kickHere && inBeat < 0.30)
            {
                // Beat one a little heavier than beat three. Records nearly
                // always do this and it costs the tracker nothing on tempo;
                // identical kicks on one and three leave the bar with no mark
                // at all, which is what this material used to have.
                const float weight = beatInBar == 0 ? 1.0f : 0.82f;
                const float env = decay (tBeat, 26.0f);
                s += weight * 0.95f * std::sin (2.0f * kPi * (48.0f + 30.0f * env) * tBeat) * env;
            }

            // A cymbal on the one of every fourth bar. The single clearest
            // downbeat cue there is, and common enough on real records that
            // leaving it out was the material being unusual, not hard.
            if (beatInBar == 0 && (bar % 4) == 0 && inBeat < 0.9)
            {
                const float env = decay (tBeat, 3.2f);
                s += 0.30f * noiseAt (rng) * env;
            }
            if (opt.syncopated && beatInBar == 1 && sixIdx == 3 && sixteenth < 0.5)
            {
                const float env = decay (tSix, 30.0f);
                s += 0.55f * std::sin (2.0f * kPi * (48.0f + 26.0f * env) * tSix) * env;
            }

            // Snare, with the shell tone as well as the noise: the noise alone
            // is a click, and a click is what already works.
            const bool snareHere = opt.halfTimeFeel ? (beatInBar == 2)
                                                    : (beatInBar == 1 || beatInBar == 3);
            if (snareHere && inBeat < 0.35)
            {
                const float env = decay (tBeat, 17.0f);
                s += (0.45f * noiseAt (rng) + 0.30f * std::sin (2.0f * kPi * 195.0f * tBeat)
                      + 0.18f * std::sin (2.0f * kPi * 331.0f * tBeat)) * env * 0.85f;
            }

            // Hats on the eighths, alternating strong and weak, with the odd
            // open hat. This is the pattern that makes an activation curve look
            // the same at the beat and at twice the beat.
            if (eighth < 0.30)
            {
                const bool onBeat = inBeat < 0.5;
                const bool open = ((beatIdx * 2 + (onBeat ? 0 : 1)) % 8) == 7;
                const float env = decay (tEig, open ? 9.0f : 45.0f);
                const float amp = (onBeat ? 0.22f : 0.15f) * (open ? 1.4f : 1.0f);
                s += amp * noiseAt (rng) * env;
            }

            if (fillBar && beatInBar == 3 && sixteenth < 0.5)
            {
                const float env = decay (tSix, 22.0f);
                s += (0.5f * noiseAt (rng)
                      + 0.3f * std::sin (2.0f * kPi * (150.0f + 40.0f * sixIdx) * tSix)) * env;
            }
        }

        // Bass. Held notes, so most of its energy has no onset at all - but the
        // root lands on the one and the note in the middle of the bar is weaker
        // and higher, which is how a bass line marks a bar. It used to
        // articulate the same note every two beats, so the strongest thing in
        // the arrangement above the beat had a period of two - the material had
        // no bar in it, and neither the network nor a listener could have found
        // one.
        {
            const double inBar = opt.syncopated ? std::fmod (beats + 0.5, 4.0)
                                                : std::fmod (beats, 4.0);
            const bool secondHalf = inBar >= 2.0;
            const float tb = static_cast<float> ((secondHalf ? inBar - 2.0 : inBar) * beatSec);
            const float env = 0.25f + 0.75f * decay (tb, 3.0f);
            const float note = secondHalf ? root * 0.75f : root * 0.5f;
            s += (secondHalf ? 0.30f : 0.46f) * std::sin (2.0f * kPi * note * tb) * env;
        }

        // Pad. No transient whatsoever; pure smear across the beats.
        if (opt.sustained)
        {
            const float t = static_cast<float> (i) / static_cast<float> (sr);
            s += 0.16f * (std::sin (2.0f * kPi * root * t)
                          + 0.7f * std::sin (2.0f * kPi * root * 1.26f * t)
                          + 0.6f * std::sin (2.0f * kPi * root * 1.5f * t)) * 0.33f;
        }

        // Lead line, deliberately not on the grid: a real record has one.
        {
            const double lead = std::fmod (beats * 1.5 + 0.25, 4.0);
            const float tl = static_cast<float> (lead * beatSec);
            s += 0.20f * std::sin (2.0f * kPi * root * 3.0f * tl) * decay (tl, 3.0f);
        }

        dest[i] = s * 0.32f;
        ph += inc;
    }
}

// iPad speaker, a couple of metres of room, iPad microphone. The band limiting
// is the part that matters: the speaker has almost nothing below 250 Hz, so the
// kick fundamental - the clearest beat cue in the mix - never reaches the
// network at all.
inline void speakerRoomMic (std::vector<float>& buf, double sr, unsigned seed, float level)
{
    std::mt19937 rng (seed ^ 0x5eedu);

    Biquad hp1, hp2, lp;
    hp1.highpass (sr, 260.0, 0.707);
    hp2.highpass (sr, 260.0, 0.707);
    lp.lowpass (sr, 9000.0, 0.707);

    // Sparse early reflections plus a diffuse tail. Onsets arrive more than
    // once and each one is smeared.
    const int taps[] = { 411, 967, 1733, 2591, 3701, 5273, 7639, 11003 };
    const float gains[] = { 0.42f, 0.31f, 0.26f, 0.20f, 0.16f, 0.12f, 0.09f, 0.06f };

    std::vector<float> dry (buf);
    for (size_t i = 0; i < buf.size(); ++i)
    {
        float s = dry[i];
        for (int k = 0; k < 8; ++k)
            if (i >= static_cast<size_t> (taps[k]))
                s += gains[k] * dry[i - static_cast<size_t> (taps[k])];

        s = hp2.process (hp1.process (s));
        s = lp.process (s);
        s += 0.0016f * noiseAt (rng);          // room + preamp floor
        buf[i] = std::clamp (s * level, -1.0f, 1.0f);
    }
}


inline const char* styleName (const SongOptions& o)
{
    if (o.halfTimeFeel) return "half-time";
    if (o.syncopated && o.sustained) return "sync+pad";
    if (o.syncopated) return "syncopated";
    if (o.sustained) return "pad";
    return "straight";
}

} // namespace vp::probe
