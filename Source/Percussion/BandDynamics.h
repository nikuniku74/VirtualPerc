#pragma once

#include "Core/Types.h"

#include <algorithm>
#include <cmath>

namespace vp
{

/**
    How much the band is giving, right now, compared with the most it gives.

    Everything else in this engine answers "when": the tracker finds the pulse,
    the clock places it, the groove tables say which stroke falls on which
    sixteenth. Nothing answered "how much", and that is most of the difference
    between a part that is correct and a player who is listening. A
    percussionist under an exposed vocal does not play the same figure quieter -
    they play *less of it*, and in the passage that really does not want them
    they stop. The tables cannot express that because a table is indexed by
    step, not by what the rest of the room is doing.

Two inputs, and the first carries most of the answer: **level**. A verse is
    quieter than a chorus, an exposed vocal is quieter than a band, and a
    breakdown is quieter than both.

    The second is **density**, from the bar `StyleDetector` is already folding
    the music onto: how many of its sixteen sixteenths have anything in them.
    Level alone cannot tell a quiet full band from an exposed voice, and those
    two want opposite things from a percussionist - the first wants the part
    turned down, the second wants most of it gone. Density is what separates
    them, and it costs nothing because the fold is already being computed.

    They are combined by taking the smaller: either reason to play less is
    reason enough, and neither can talk the other out of it.

    Two things make it usable rather than a meter:

    - **It reads the band, not us.** The level it is given is taken after the
      leak subtraction and before the analysis make-up gain: our own part has
      been removed, so the dynamics cannot feed back on themselves, and the
      make-up - which exists to hold the network's operating point and therefore
      erases exactly this - has not been applied yet.
    - **It is relative to this song.** An absolute threshold is not a line: a
      quiet band and a loud one differ by more than a verse and a chorus do.
      `docs/STATUS.md` records the same lesson being learned the hard way about
      the fit residual. So the reference is the loudest this song has been,
      decayed slowly enough that a chorus does not reset it every eight bars.
*/
class BandDynamics
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr > 1.0 ? sr : 48000.0;
        reset();
    }

    void reset() noexcept
    {
        env = 0.0f;
        loudest = 0.0f;
        current = 1.0f;
        quietFor = 0.0f;
        loudFor = 0.0f;
        silent = false;
        heardAnything = false;
        lastBarLevel = 0.0f;
        haveBarLevel = false;
        barsSinceSection = 1000;
        occupancy = 16.0f;
        haveDensity = false;
    }

    /** How many of the bar's sixteen sixteenths have anything in them, and
        whether that number means anything yet.

        Held rather than acted on directly: the fold needs several bars before
        it describes the music rather than whatever the clock was doing while it
        found the bar, and until then density has no vote. */
    void setDensity (float occupiedSixteenths, bool settled) noexcept
    {
        haveDensity = settled;
        if (settled)
            occupancy += (std::clamp (occupiedSixteenths, 0.0f, 16.0f) - occupancy)
                         * kOccupancyRate;
    }

    /** One block. `level` is the band's own peak with the app's part already
        taken out. */
    void observe (float level, int numSamples) noexcept
    {
        if (! std::isfinite (level) || level < 0.0f)
            return;

        const float dt = static_cast<float> (numSamples) / static_cast<float> (sampleRate);

        // A musical envelope, not a peak meter: quick enough to follow a band
        // lifting into a chorus, slow enough not to follow the gap between two
        // snare hits. Half a second of release is about one bar at 120.
        const float rise = 1.0f - std::exp (-dt / kAttackSec);
        const float fall = 1.0f - std::exp (-dt / kReleaseSec);
        env += (level > env ? rise : fall) * (level - env);

        if (env > kHeardAtAll)
            heardAnything = true;

        // The loudest this song has been. Takes a rise at once and gives it up
        // over most of a minute, so a song that genuinely gets quieter
        // re-references itself while a loud chorus does not become the new
        // normal the moment it ends.
        if (env > loudest)
            loudest = env;
        else
            loudest -= (loudest - env) * (1.0f - std::exp (-dt / kReferenceSec));

        const float ref = std::max (kReferenceFloor, loudest);
        const float ratio = std::clamp (env / ref, 1.0e-4f, 1.0f);

        // In decibels, because that is how a player hears "half as much". The
        // range is 18 dB: a verse against a chorus is usually inside it, and a
        // band against an exposed vocal usually is not - which is the point.
        const float db = 20.0f * std::log10 (ratio);
        current = std::clamp (1.0f + db / kRangeDb, 0.0f, 1.0f);

        // And whether the passage wants anything at all.
        //
        // Held on both edges. A percussionist who drops out mid-phrase because
        // one bar went quiet is worse than one who never drops out, and one who
        // comes back the instant a single loud note lands is worse still - so
        // leaving takes longer than a bar and coming back takes a beat or two.
        if (! heardAnything)
        {
            silent = false;
            quietFor = loudFor = 0.0f;
            return;
        }
        if (current < kSilenceBelow)
        {
            quietFor += dt;
            loudFor = 0.0f;
            if (quietFor > kSecondsBeforeStopping)
                silent = true;
        }
        else
        {
            loudFor += dt;
            quietFor = 0.0f;
            if (loudFor > kSecondsBeforeReturning && current > kReturnAbove)
                silent = false;
        }
    }

    /** 0 when this is as quiet as the song gets, 1 when the band is giving
        everything it gives. 1 also before anything has been heard, so an engine
        that has this switched on but has not listened yet behaves exactly as it
        did before. */
    float level() const noexcept
    {
        if (! heardAnything)
            return 1.0f;
        if (! haveDensity)
            return current;
        // A bar with four of its sixteenths occupied is a voice and a chord; a
        // bar with twelve is a band playing. Between them the part comes down
        // with the music. The smaller of the two reasons wins - see the class
        // note - so a loud but empty passage is still played sparsely.
        const float fromDensity = std::clamp ((occupancy - kEmptyBar)
                                                  / (kFullBar - kEmptyBar),
                                              0.0f, 1.0f);
        return std::min (current, fromDensity);
    }

    /** The two halves separately, for the debug panel. */
    float levelOnly() const noexcept { return heardAnything ? current : 1.0f; }
    float density() const noexcept { return haveDensity ? occupancy : -1.0f; }

    /** True when a percussionist would have stopped. The caller decides *when*
        to act on it - stopping in the middle of a figure is not what a player
        does, so the engine waits for the bar. */
    bool wantsSilence() const noexcept { return silent; }

    /** The reference the level is measured against, for the debug panel. */
    float reference() const noexcept { return loudest; }

    /** Whether the band has just changed section.

        A section boundary is the one piece of musical form that can be read off
        a level meter: a verse does not become a chorus quietly. Sampled once a
        bar by the caller - a step measured inside a bar is a fill or a stab -
        and reported once, so the caller can start the phrase again on it.

        `barLevel` is `level()` taken at the last bar line. Held for four bars
        afterwards: a band that lifts over two bars is one section change, not
        two, and an eight-bar sentence restarted twice inside itself is worse
        than one never restarted at all. */
    bool sectionChangedAtBar() noexcept
    {
        const float now = level();
        if (! haveBarLevel)
        {
            haveBarLevel = true;
            lastBarLevel = now;
            return false;
        }
        const float step = std::fabs (now - lastBarLevel);
        lastBarLevel = now;
        if (barsSinceSection < 1000)
            ++barsSinceSection;
        if (step < kSectionStep || barsSinceSection < kBarsBetweenSections)
            return false;
        barsSinceSection = 0;
        return true;
    }

private:
    static constexpr float kAttackSec = 0.08f;
    static constexpr float kReleaseSec = 0.50f;
    /** How fast the song's own loudest fades.
    
        It has to be slower than a section, or the reference re-tunes itself
        *inside* the verse it is supposed to be measuring and the part walks
        back up while the band is still down. Measured with forty seconds and a
        sixteen-second verse, the part came down 3.2 dB against a band that came
        down 14; at ninety it comes down properly. Long enough to sit out a
        section, short enough that a song which is genuinely quieter than the
        last one adapts inside a minute or two. */
    static constexpr float kReferenceSec = 90.0f;
    static constexpr float kReferenceFloor = 0.004f;
    /** Below the reference by this much reads as no dynamics at all. */
    static constexpr float kRangeDb = 18.0f;
    /** Where a percussionist would stop, where they would come back, and how
        long each takes. Leaving is slower than a bar; returning is a beat. */
    static constexpr float kSilenceBelow = 0.18f;
    static constexpr float kReturnAbove = 0.30f;
    static constexpr float kSecondsBeforeStopping = 2.2f;
    static constexpr float kSecondsBeforeReturning = 0.5f;
    /** Under this the input is not a quiet passage, it is nothing at all, and
        nothing at all must not read as a decision to stop playing. */
    static constexpr float kHeardAtAll = 0.010f;
    /** How much the band's level has to move across a bar line to be a section
        rather than a phrase. A fifth of the range is about four decibels, which
        is more than a band does inside a section and less than one does going
        into a chorus. */
    static constexpr float kSectionStep = 0.20f;
    /** And how far apart two of them have to be. Four bars: a band that lifts
        over two of them is one section change, not two. */
    static constexpr int kBarsBetweenSections = 4;
    /** Sixteenths occupied in a bar that is as empty as a part gets played
        under, and in one that is a band going. */
    static constexpr float kEmptyBar = 4.0f;
    static constexpr float kFullBar = 11.0f;
    /** Per bar. Four bars to move most of the way, which is a phrase. */
    static constexpr float kOccupancyRate = 0.30f;

    double sampleRate = 48000.0;
    float env = 0.0f;
    float loudest = 0.0f;
    float current = 1.0f;
    float quietFor = 0.0f;
    float loudFor = 0.0f;
    bool  silent = false;
    bool  heardAnything = false;
    float lastBarLevel = 0.0f;
    bool  haveBarLevel = false;
    int   barsSinceSection = 1000;
    float occupancy = 16.0f;
    bool  haveDensity = false;
};

} // namespace vp
