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

/** What kind of record the arrangement is.

    The app's automatic style chooser decides between four parts by folding the
    music onto the bar in three bands, and it has never had material of *known*
    style to be scored against inside this repository - the bench that measured
    it lived outside the tree and is gone. These four are written to the same
    four descriptions the chooser works from, so a run that gets them wrong is
    the chooser being wrong and not the material being ambiguous. */
enum class Genre
{
    /** Kick on one and three, snare on two and four, eighth hats. */
    rock = 0,
    /** Four on the floor and an open hat on every offbeat. */
    dance,
    /** Syncopated, busy on the sixteenths, no backbeat to speak of. */
    latin,
    /** Sparse and level: a kick, a light snare, and air. */
    pop
};

struct SongOptions
{
    Genre genre = Genre::rock;
    float bpm = 120.0f;
    bool  syncopated = false;   // bass and kick off the grid
    bool  sustained = false;    // pads and strings holding through the beats
    bool  halfTimeFeel = false; // snare on 3 only
    bool  breakdown = true;     // eight bars with the drums out
    bool  fills = true;

    /** Peak-to-peak tempo wander over the take, in BPM, as a smooth random
        walk. Zero is a sequencer. A band that has rehearsed sits around two to
        four; one that has not, more. Everything in this repository was measured
        at zero until this existed, which is a poor way to judge a tracker that
        is going to be pointed at a live recording. */
    float driftBpm = 0.0f;

    /** Human timing scatter per beat, in milliseconds, one standard deviation.
        A drummer is not a click: even a very good one lands within about eight
        to twelve milliseconds of their own average, and the beat the band plays
        is the average of several people doing that. */
    float jitterMs = 0.0f;

    /** How far the first half of every sixteen bars sits below the second, in
        decibels. Zero is a take with no arrangement in it at all, which is what
        everything in this repository was measured on until the part started
        listening to how much the band was giving. Twelve to sixteen is an
        ordinary verse against an ordinary chorus. */
    float quietSectionDb = 0.0f;
};

/** The arrangement split the way a desk splits it.

    A live rig does not hand the app a mix and nothing else: on any digital
    console the kick is on its own channel, and that channel is a different
    signal from the mix in every way that matters to a beat tracker - one
    instrument, one attack per event, no bass note sustaining through it, and
    silent for exactly as long as the drummer is not playing.

    Filled only when `renderSong` is given somewhere to put it. `mix` is what
    `dest` gets and is the sum of all of them. */
struct SongStems
{
    std::vector<float> kick;    // the kick drum alone
    std::vector<float> snare;   // snare and fills
    std::vector<float> hats;    // hats and cymbals
    std::vector<float> music;   // bass, pad, lead - everything that is not a drum

    void prepare (size_t n)
    {
        kick.assign (n, 0.0f);
        snare.assign (n, 0.0f);
        hats.assign (n, 0.0f);
        music.assign (n, 0.0f);
    }
};

// A full mix at a fixed tempo. Beat one is at sample zero, so the true beat
// phase at any sample is exactly known.
/** Renders the arrangement, and optionally reports where the beats truly fell.

    `truePhase`, when given, receives the exact position in beats at every
    sample - which is the only way to score a tracker against material that does
    not hold still, because "the tempo" is then a curve and not a number.

    `stems`, when given, receives the same arrangement split per instrument
    group. The sum of the four is exactly `dest`. */
inline void renderSong (std::vector<float>& dest, const SongOptions& opt, double sr,
                        unsigned seed, std::vector<double>* truePhase = nullptr,
                        SongStems* stems = nullptr)
{
    std::mt19937 rng (seed);
    const double nominalBeatSec = 60.0 / static_cast<double> (opt.bpm);

    // The tempo curve. A band drifts: it does not step, and it does not
    // oscillate at an audible rate either. Two slow sines at incommensurate
    // periods give a wander with no period a tracker could learn.
    std::uniform_real_distribution<double> ph0 (0.0, 6.2831853);
    const double d1 = ph0 (rng), d2 = ph0 (rng);
    const double driftHalf = 0.5 * static_cast<double> (opt.driftBpm);
    auto bpmAt = [&] (double sec)
    {
        if (driftHalf <= 0.0)
            return static_cast<double> (opt.bpm);
        const double w = 0.62 * std::sin (sec * 2.0 * kPi / 23.0 + d1)
                       + 0.38 * std::sin (sec * 2.0 * kPi / 37.0 + d2);
        return static_cast<double> (opt.bpm) + driftHalf * w;
    };

    // Human scatter, one value per beat, held so that every event inside a beat
    // moves with it - a drummer who is early is early for the whole beat, not
    // for each stroke independently.
    std::normal_distribution<double> jit (0.0, static_cast<double> (opt.jitterMs) * 0.001);
    std::vector<double> beatShift (4096, 0.0);
    if (opt.jitterMs > 0.0f)
        for (auto& v : beatShift)
            v = jit (rng);

    if (truePhase != nullptr)
        truePhase->assign (dest.size(), 0.0);
    if (stems != nullptr)
        stems->prepare (dest.size());

    // Chord roots for a four-bar loop, in Hz.
    const float roots[4] = { 110.0f, 146.83f, 98.0f, 130.81f };

    double ph = 0.0;
    for (size_t i = 0; i < dest.size(); ++i)
    {
        const double sec = static_cast<double> (i) / sr;
        const double curBpm = bpmAt (sec);
        const double beatSec = 60.0 / curBpm;
        const double inc = 1.0 / (beatSec * sr);
        if (truePhase != nullptr)
            (*truePhase)[i] = ph;

        // The scatter shifts where the events land, not the underlying count,
        // so the notated grid stays the thing a tracker is scored against.
        const double shifted = ph + (opt.jitterMs > 0.0f
            ? beatShift[static_cast<size_t> (static_cast<int> (std::floor (ph)) & 4095)] / beatSec
            : 0.0);
        const double beats = shifted;
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
        // The arrangement's own dynamics. Eased over an eighth of a bar at the
        // seam rather than stepped: a band coming out of a verse takes about
        // that long, and a step would be an edit.
        float section = 1.0f;
        if (opt.quietSectionDb > 0.0f)
        {
            const double inSixteen = std::fmod (beats * 0.25, 16.0);
            const float quiet = std::pow (10.0f, -opt.quietSectionDb / 20.0f);
            const double ease = 0.125;
            const double d = inSixteen < 8.0 ? (8.0 - inSixteen) : (16.0 - inSixteen);
            const float toward = inSixteen < 8.0 ? quiet : 1.0f;
            const float from = inSixteen < 8.0 ? 1.0f : quiet;
            section = d < ease ? from + (toward - from) * static_cast<float> (d / ease)
                               : toward;
            section = inSixteen < 8.0 ? (d < ease ? quiet + (1.0f - quiet)
                                                       * static_cast<float> (1.0 - d / ease)
                                                 : quiet)
                                      : (d < ease ? 1.0f + (quiet - 1.0f)
                                                       * static_cast<float> (1.0 - d / ease)
                                                  : 1.0f);
        }
        const bool fillBar = opt.fills && (bar % 8) == 7;
        const float root = roots[bar % 4];

        // One accumulator per instrument group, so the same arithmetic can be
        // handed out as a desk would hand it out. `s` is still their sum and
        // still what `dest` gets, so the mix is bit-identical to before.
        float sKick = 0.0f, sSnare = 0.0f, sHats = 0.0f, sMusic = 0.0f;

        if (! drumsOut)
        {
            // Kick. On one and three, plus a pushed sixteenth before three when
            // the groove is syncopated - which is where a beat tracker most
            // often finds a beat that is not one. Four on the floor for dance,
            // which is the one thing that names that genre from the low band
            // alone.
            const bool kickHere = opt.genre == Genre::dance
                                      ? true
                                      : (beatInBar == 0 || beatInBar == 2);
            if (kickHere && inBeat < 0.30)
            {
                // Beat one a little heavier than beat three. Records nearly
                // always do this and it costs the tracker nothing on tempo;
                // identical kicks on one and three leave the bar with no mark
                // at all, which is what this material used to have.
                const float weight = beatInBar == 0 ? 1.0f : 0.82f;
                const float env = decay (tBeat, 26.0f);
                sKick += weight * 0.95f * std::sin (2.0f * kPi * (48.0f + 30.0f * env) * tBeat) * env;
            }

            // A cymbal on the one of every fourth bar. The single clearest
            // downbeat cue there is, and common enough on real records that
            // leaving it out was the material being unusual, not hard.
            if (beatInBar == 0 && (bar % 4) == 0 && inBeat < 0.9)
            {
                const float env = decay (tBeat, 3.2f);
                sHats += 0.30f * noiseAt (rng) * env;
            }
            if (opt.syncopated && beatInBar == 1 && sixIdx == 3 && sixteenth < 0.5)
            {
                const float env = decay (tSix, 30.0f);
                sKick += 0.55f * std::sin (2.0f * kPi * (48.0f + 26.0f * env) * tSix) * env;
            }

            // Snare, with the shell tone as well as the noise: the noise alone
            // is a click, and a click is what already works.
            // Latin has no backbeat: that is most of what separates it from the
            // other three, and a snare on two and four would put one there.
            const bool snareHere = opt.genre == Genre::latin
                                       ? false
                                       : (opt.halfTimeFeel ? (beatInBar == 2)
                                                           : (beatInBar == 1 || beatInBar == 3));
            if (snareHere && inBeat < 0.35)
            {
                // Pop is felt rather than hit: the same backbeat, half as loud.
                const float weight = opt.genre == Genre::pop ? 0.45f : 0.85f;
                const float env = decay (tBeat, 17.0f);
                sSnare += (0.45f * noiseAt (rng) + 0.30f * std::sin (2.0f * kPi * 195.0f * tBeat)
                           + 0.18f * std::sin (2.0f * kPi * 331.0f * tBeat)) * env * weight;
            }

            // And what fills the bar. Latin puts something on nearly every
            // sixteenth - that busyness is the feature the chooser reads it by
            // - while pop puts almost nothing anywhere.
            if (opt.genre == Genre::latin && sixteenth < 0.5)
            {
                const bool loud = (sixIdx & 1) == 1;   // the "e" and the "a"
                const float env = decay (tSix, 40.0f);
                sSnare += (loud ? 0.26f : 0.12f)
                          * (0.6f * noiseAt (rng)
                             + 0.4f * std::sin (2.0f * kPi * 420.0f * tSix)) * env;
            }

            // Hats on the eighths, alternating strong and weak, with the odd
            // open hat. This is the pattern that makes an activation curve look
            // the same at the beat and at twice the beat.
            // Hats. Dance puts an open one on *every* offbeat, which is the
            // other half of what names it; pop plays them on the beat only.
            const bool hatHere = opt.genre == Genre::pop ? (inBeat < 0.30)
                                                        : (eighth < 0.30);
            if (hatHere)
            {
                const bool onBeat = inBeat < 0.5;
                const bool open = opt.genre == Genre::dance
                                      ? ! onBeat
                                      : ((beatIdx * 2 + (onBeat ? 0 : 1)) % 8) == 7;
                const float env = decay (tEig, open ? 9.0f : 45.0f);
                const float amp = (onBeat ? 0.22f : 0.15f) * (open ? 1.4f : 1.0f);
                sHats += amp * noiseAt (rng) * env;
            }

            if (fillBar && beatInBar == 3 && sixteenth < 0.5)
            {
                const float env = decay (tSix, 22.0f);
                sSnare += (0.5f * noiseAt (rng)
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
            sMusic += (secondHalf ? 0.30f : 0.46f) * std::sin (2.0f * kPi * note * tb) * env;
        }

        // Pad. No transient whatsoever; pure smear across the beats.
        if (opt.sustained)
        {
            const float t = static_cast<float> (i) / static_cast<float> (sr);
            sMusic += 0.16f * (std::sin (2.0f * kPi * root * t)
                               + 0.7f * std::sin (2.0f * kPi * root * 1.26f * t)
                               + 0.6f * std::sin (2.0f * kPi * root * 1.5f * t)) * 0.33f;
        }

        // Lead line, deliberately not on the grid: a real record has one.
        {
            const double lead = std::fmod (beats * 1.5 + 0.25, 4.0);
            const float tl = static_cast<float> (lead * beatSec);
            sMusic += 0.20f * std::sin (2.0f * kPi * root * 3.0f * tl) * decay (tl, 3.0f);
        }

        constexpr float kMixGain = 0.32f;
        sKick *= section; sSnare *= section; sHats *= section; sMusic *= section;
        dest[i] = (sKick + sSnare + sHats + sMusic) * kMixGain;
        if (stems != nullptr)
        {
            stems->kick[i]  = sKick  * kMixGain;
            stems->snare[i] = sSnare * kMixGain;
            stems->hats[i]  = sHats  * kMixGain;
            stems->music[i] = sMusic * kMixGain;
        }
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


inline const char* genreName (Genre g)
{
    switch (g)
    {
        case Genre::rock:  return "ROCK";
        case Genre::dance: return "DANCE";
        case Genre::latin: return "LATIN";
        case Genre::pop:   return "POP";
    }
    return "?";
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
