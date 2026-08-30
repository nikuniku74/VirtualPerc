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
    count
};

/** One scheduled stroke. `delayBeats` is always >= 0: the clock hands out grid
    positions and a voice can be scheduled later than one, never earlier, so
    swing and feel are expressed as lateness. */
struct GrooveEvent
{
    Stroke stroke = Stroke::shakerDown;
    float  velocity = 0.8f;
    float  delayBeats = 0.0f;
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
    static constexpr int kMaxEvents = 4;

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

    /** How dense the shaker is. `autoDetect` means eighths, which is what a
        shaker mostly plays. */
    void setShakerSubdivision (Subdivision s) noexcept;

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
    Subdivision shakerGrid = Subdivision::eighth;
};

} // namespace vp
