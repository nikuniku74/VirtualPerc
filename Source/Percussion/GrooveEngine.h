#pragma once

#include "Core/DeterministicRng.h"
#include "Core/Types.h"

#include <cstdint>

namespace vp
{

/** One articulation. Congas are not a drum you hit, they are six or seven
    different sounds made on two drums, and which ones fall where is most of
    what makes a pattern recognisable as a marcha rather than as a sequence of
    conga hits. */
enum class Stroke : int
{
    shakerDown = 0,   // the accented stroke, away from the body, on the pulse
    shakerUp,         // the return stroke, lighter, on the off-eighth
    tumba,            // low drum, open
    open,             // high drum, open tone
    slap,             // high drum, cracked
    heel,             // palm down, muffled - the quiet half of the marcha
    toe,              // fingertips, muffled
    muff,             // muted tone, no ring
    // The stopped strokes. A conga is played as much with the hand that stays
    // on the head as with the one that leaves it, and until now the bank had
    // only the leaving kind: every loud articulation rang. These are the two
    // that do not, and they are what a part sounds like when it has to sit
    // inside a band rather than over one.
    slapClosed,       // the crack with the hand left on the head - no ring at all
    tapado,           // the low drum stopped: a thud with the pitch taken out
    clap,             // the backbeat, hands only - see `GrooveEngine::eventsAt`
    // CEMBALO is the tambourine, not the cymbal the word points at in a
    // dictionary - see Assets/Percussion/ATTRIBUTION.md.
    cembaloDown,      // the struck hit - same job as the shaker, another sound:
    cembaloUp,        // the shake on the return. Same table, own switch.
    // Triangle is not a fifth groove voice. It is a sample family a FEEL
    // knob can be assigned. Each beat is `. . _` : stopped #1 on the
    // battere, stopped #2 on the e, open (short tap plus wet tail) on the
    // &, rest on the a.
    triangleOpen,     // short tap, then ambience - not a held ring; on the &
    triangleClosed,   // stopped #1 - battere only
    triangleClosed2,  // stopped #2 - the e of each beat
    count
};

/** Family of a written stroke. Volume falls back to this when a groove
    event did not name the FEEL slot that produced it. */
inline bool isTriangleStroke (Stroke s) noexcept
{
    return s == Stroke::triangleOpen || s == Stroke::triangleClosed
        || s == Stroke::triangleClosed2;
}

inline KitSound kitSoundForStroke (Stroke s) noexcept
{
    switch (s)
    {
        case Stroke::shakerDown:
        case Stroke::shakerUp:       return KitSound::shaker;
        case Stroke::cembaloDown:
        case Stroke::cembaloUp:      return KitSound::cembalo;
        case Stroke::clap:           return KitSound::clap;
        case Stroke::triangleOpen:
        case Stroke::triangleClosed:
        case Stroke::triangleClosed2: return KitSound::triangle;
        default:                      return KitSound::congas;
    }
}

/** One scheduled stroke. `delayBeats` is always >= 0: the clock hands out grid
    positions and a voice can be scheduled later than one, never earlier, so
    swing and feel are expressed as lateness. */
struct GrooveEvent
{
    Stroke stroke = Stroke::shakerDown;
    float  velocity = 0.8f;
    float  delayBeats = 0.0f;
    /** FEEL slot this hit belongs to (which knob). Groove content comes
        from the assigned `KitSound`, not from this index. `count` means
        infer volume from `stroke`. */
    KitSound part = KitSound::count;
};

/**
    What the percussionist actually plays.

    This used to be a constant array of eight entries inside the render loop,
    repeated identically for the length of the song, and it was not a marcha:
    the signature of the pattern is the *pair* of open tones at the end of the
    bar pulling into the next one, and that pair was not there. Nor was there
    any dynamic shape, any two-bar phrasing, or any difference between one bar
    and the next four minutes later.

    Everything here is on a sixteenth grid even though the loud strokes sit on
    eighths, because the quiet half of a marcha - the heel and toe that fill the
    gaps - is what a listener hears as a player rather than as a pattern.

    One rule holds across every style: **no conga on the first quarter's
    down-stroke**. The band is already there - kick, bass and downbeat together
    - and a conga on top of that is not heard as a percussionist, it is heard as
    a thicker attack. So the low tone lands on the "e" or the "and" of one
    instead, and the one is left to the band. `eventsAt` enforces it as well as
    the tables observing it. The shaker is deliberately not covered: a shaker on
    the pulse *is* the pulse.

    No allocation, no locks: this runs from the audio thread.
*/
class GrooveEngine
{
public:
    static constexpr int kStepsPerBar = 16;
    // Four slots can each write a conga hit plus a ghost on one sixteenth
    // when several knobs are assigned the same family. Eight is four pairs.
    static constexpr int kMaxEvents = 8;

    void prepare (std::uint32_t seed) noexcept;
    void reset() noexcept;

    /** Which part to play. Not a set of variations on one pattern: each style
        has its own conga figure and its own shaker weighting. */
    void setStyle (GrooveStyle s) noexcept;
    GrooveStyle currentStyle() const noexcept { return style; }

    /** Whether this bar takes a fill instead of repeating the pattern. */
    bool isFillBar (int barIndex) const noexcept;

    /** 0 = dead on the grid and mechanically even, 1 = as loose as a player
        who is not trying. Drives velocity spread, micro-timing and ghosts. */
    void setHumanize (float amount) noexcept;

    /** 0 = straight eighths, 1 = triplet feel: the off-eighth sits two
        thirds of the way through the beat, and the 16ths move with it. */
    void setSwing (float amount) noexcept;

    /** Drives how busy the part is: ghost notes, and whether a bar takes a
        fill on its way out. */
    void setIntensity (float amount) noexcept;

    /** How much the band is giving, 0..1 - see Percussion/BandDynamics.h. One
        is the part as written and is what everything that does not set this
        gets.

        Below one it does two things, and the second is the one that matters. It
        plays quieter, which any volume control could do; and it plays **less**,
        which none could. A percussionist under an exposed vocal does not play
        the same figure softer - the heel-toe fill-in goes first, then the
        ghosts, then the answering tones, until what is left is the skeleton of
        the figure: the slap, the low tone, the push into the next bar. That is
        what makes six strokes read as a decision rather than as a fader. */
    void setDynamics (float amount) noexcept;

    void setShakerEnabled (bool on) noexcept { shakerOn = on; }
    void setCongasEnabled (bool on) noexcept { congasOn = on; }
    /** Same job as the shaker, a different sound: reuses the shaker's table,
        thinning, accents and feel, and is switched independently. */
    void setCembaloEnabled (bool on) noexcept { cembaloOn = on; }
    /** The backbeat only. Also needs `setBarTrusted (true)` to actually sound -
        see that setter. */
    void setClapEnabled (bool on) noexcept { clapOn = on; }
    void setShakerSound (int s) noexcept { shakerSound = s; }
    void setCongaSound (int s) noexcept { congaSound = s; }
    void setCembaloSound (int s) noexcept { cembaloSound = s; }
    void setClapSound (int s) noexcept { clapSound = s; }
    /** Whether the app's "one" is currently trustworthy enough to put the clap
        on it: the listener has locked the bar, or enough time has passed since
        the last automatic rotation that a wrong guess would have been
        corrected by now. Gates CLAP only - every other voice follows the beat
        the app is already playing to, right or wrong, same as before. See
        docs/TODO.md items 2 and 10: a real per-block confidence score from the
        tracker is the proper fix; this is the best signal available until item
        2 lands. */
    void setBarTrusted (bool trusted) noexcept { barTrustedFlag = trusted; }

    /** How dense the synthesized shaker and cembalo parts are. `autoDetect`
        means eighths. The setting only thins those tables; it never adds or
        moves them, and it does not rewrite congas or triangle. Congas keep
        their authored 16-step figure at the clock BPM. `setShakerNatural`
        is the exception for shaker/cembalo only: see that setter. */
    void setSubdivision (Subdivision s) noexcept;

    /** Occasional extra shaker strokes on the grid the subdivision has
        thinned away: sixteenths on an eighths part, off-eighths on a
        quarters part. Off by default, so 1/4 / 1/8 / 1/16 stay exact.
        Uses the authored `shaker[16]` values the thinning had dropped, never
        invents a busier table, and never becomes a full finer grid. Congas
        are untouched. */
    void setShakerNatural (bool on) noexcept { naturalOn = on; }

    /** Strokes falling on one sixteenth of one bar. `barIndex` counts bars
        since the part started and selects the phrase; `step` is 0..15.
        Returns how many events were written. */
    int eventsAt (int barIndex, int step, GrooveEvent* out, int maxOut) noexcept;

private:
    float humanVelocity (float base) noexcept;
    float humanDelay (int step) noexcept;
    /** Level applied to every stroke that survives, 0.4 at the floor to 1. */
    float dynamicGain() const noexcept;
    /** Whether a stroke written at this velocity is still in the part. */
    bool  v_survives (float writtenVelocity) const noexcept;
    /** Written shaker velocity that should sound on this step, or 0. Shared
        by shaker and cembalo so an ornament is one decision, two timbres. */
    float soundingShaker (int step, bool subdivisionAllows,
                          const float* shakerTable) noexcept;

    /** The loudest written stroke that can be thinned away at the floor. Above
        this nothing is ever dropped, so the skeleton of every figure survives
        at any dynamic: the slaps sit at 0.88-0.96, the low tones and the
        closing opens at 0.74-0.90, and the heel/toe fill-in at 0.20-0.36. */
    static constexpr float kThinCeiling = 0.72f;

    DeterministicRng rng { 0x9E3779B9u };
    GrooveStyle style = GrooveStyle::marcha;
    float humanize = 0.35f;
    float swing = 0.0f;
    float intensity = 0.5f;
    float dynamics = 1.0f;
    bool  shakerOn = true;
    bool  congasOn = true;
    // New voices, off until the listener asks for them - see item 10 in
    // docs/TODO.md. Cembalo has no dependency and could default on; it stays
    // off with clap so turning either on is a deliberate choice, not a change
    // to how the app already sounds on upgrade.
    bool  cembaloOn = false;
    bool  clapOn = false;
    bool  barTrustedFlag = false;
    bool  naturalOn = false;
    Subdivision subdivisionGrid = Subdivision::eighth;
    int   shakerSound = static_cast<int> (KitSound::shaker);
    int   congaSound = static_cast<int> (KitSound::congas);
    int   cembaloSound = static_cast<int> (KitSound::cembalo);
    int   clapSound = static_cast<int> (KitSound::clap);
};

} // namespace vp
