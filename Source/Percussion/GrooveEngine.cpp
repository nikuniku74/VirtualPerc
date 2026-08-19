#include "Percussion/GrooveEngine.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace vp
{

namespace
{
    // Everything is on a sixteenth grid. Steps 0, 4, 8, 12 are the quarters;
    // 2, 6, 10, 14 are the off-eighths; the odd steps are the sixteenths in
    // between - the "e" and the "a" - where the quiet strokes live.
    struct Hit { int step; Stroke stroke; float velocity; };

    // ---------------------------------------------------------------- marcha
    //
    // What makes this a marcha and not a list of conga hits is the last two
    // entries: open, open on 4 and the "and" of 4, together, pulling into the
    // next bar. Everything before them is there to leave room for that.
    constexpr Hit kMarchaA[] = {
        {  0, Stroke::heel,  0.34f },
        {  2, Stroke::toe,   0.30f },
        {  4, Stroke::slap,  0.94f },   // the crack on 2
        {  6, Stroke::toe,   0.28f },
        {  8, Stroke::tumba, 0.82f },   // the bass on 3
        { 10, Stroke::toe,   0.28f },
        { 12, Stroke::open,  0.86f },   // and the pair
        { 14, Stroke::open,  0.92f },
    };
    constexpr Hit kMarchaB[] = {
        {  0, Stroke::heel,  0.32f },
        {  2, Stroke::toe,   0.29f },
        {  4, Stroke::slap,  0.92f },
        {  6, Stroke::heel,  0.26f },
        {  8, Stroke::tumba, 0.80f },
        { 10, Stroke::tumba, 0.52f },
        { 12, Stroke::open,  0.84f },
        { 14, Stroke::open,  0.94f },
    };
    constexpr Hit kMarchaFill[] = {
        {  8, Stroke::open,  0.72f },
        { 10, Stroke::slap,  0.80f },
        { 11, Stroke::open,  0.62f },
        { 12, Stroke::slap,  0.88f },
        { 13, Stroke::open,  0.70f },
        { 14, Stroke::open,  0.95f },
        { 15, Stroke::slap,  0.78f },
    };

    // ------------------------------------------------------------------ rock
    //
    // A percussionist in a rock band is not the drummer twice. The snare owns
    // 2 and 4, so the congas stay off them and play the gaps: the low drum
    // anchors the one, and the open tones land on the "and" of 2 and the "and"
    // of 4, the second of which is what pushes into the next bar. Doubling the
    // backbeat is the commonest way to make a rock track sound crowded.
    constexpr Hit kRockA[] = {
        {  0, Stroke::tumba, 0.84f },   // the one
        {  3, Stroke::heel,  0.24f },
        {  6, Stroke::open,  0.74f },   // and of 2
        {  8, Stroke::tumba, 0.60f },
        { 11, Stroke::toe,   0.24f },
        { 14, Stroke::open,  0.90f },   // and of 4 - the push
    };
    constexpr Hit kRockB[] = {
        {  0, Stroke::tumba, 0.82f },
        {  4, Stroke::slap,  0.58f },   // a light answer on the backbeat
        {  6, Stroke::open,  0.72f },
        {  8, Stroke::tumba, 0.58f },
        { 11, Stroke::toe,   0.26f },
        { 13, Stroke::open,  0.52f },
        { 14, Stroke::open,  0.92f },
    };
    constexpr Hit kRockFill[] = {
        { 12, Stroke::open,  0.72f },
        { 13, Stroke::open,  0.80f },
        { 14, Stroke::slap,  0.88f },
        { 15, Stroke::open,  0.94f },
    };

    // ----------------------------------------------------------------- dance
    //
    // Four-on-the-floor leaves every offbeat free, and house percussion fills
    // them. The figure is deliberately busy and lands mostly on the "a" of each
    // beat - the sixteenth immediately before the next kick - which is what
    // gives the part its forward lean.
    constexpr Hit kDanceA[] = {
        {  0, Stroke::tumba, 0.70f },
        {  3, Stroke::open,  0.80f },   // the "a" of 1
        {  6, Stroke::slap,  0.66f },
        {  7, Stroke::open,  0.86f },   // the "a" of 2
        { 10, Stroke::tumba, 0.58f },
        { 11, Stroke::open,  0.78f },
        { 14, Stroke::slap,  0.68f },
        { 15, Stroke::open,  0.90f },   // the "a" of 4
    };
    constexpr Hit kDanceB[] = {
        {  0, Stroke::tumba, 0.68f },
        {  3, Stroke::open,  0.78f },
        {  5, Stroke::toe,   0.30f },
        {  7, Stroke::open,  0.84f },
        {  8, Stroke::slap,  0.60f },
        { 11, Stroke::open,  0.80f },
        { 13, Stroke::toe,   0.30f },
        { 15, Stroke::open,  0.92f },
    };
    constexpr Hit kDanceFill[] = {
        {  8, Stroke::slap,  0.66f },
        {  9, Stroke::open,  0.60f },
        { 10, Stroke::slap,  0.74f },
        { 11, Stroke::open,  0.68f },
        { 12, Stroke::slap,  0.82f },
        { 13, Stroke::open,  0.76f },
        { 14, Stroke::slap,  0.88f },
        { 15, Stroke::open,  0.96f },
    };

    // ------------------------------------------------------------------- pop
    //
    // The job here is to be felt and not noticed. There is a vocal in the
    // middle of the record, so the part is mostly space: the one, a light lift
    // into 3, and the push into the next bar.
    constexpr Hit kPopA[] = {
        {  0, Stroke::tumba, 0.72f },
        {  7, Stroke::toe,   0.30f },
        { 14, Stroke::open,  0.80f },
    };
    constexpr Hit kPopB[] = {
        {  0, Stroke::tumba, 0.70f },
        {  8, Stroke::open,  0.56f },
        { 11, Stroke::toe,   0.28f },
        { 14, Stroke::open,  0.82f },
    };
    constexpr Hit kPopFill[] = {
        { 10, Stroke::toe,   0.40f },
        { 12, Stroke::open,  0.66f },
        { 14, Stroke::open,  0.86f },
        { 15, Stroke::slap,  0.62f },
    };

    // The shaker, as a velocity per sixteenth. Zero is silence. The subdivision
    // setting thins this down; it never adds to it.
    //
    // A shaker is two strokes - down on the pulse, up on the return - so which
    // one sounds follows from the step, not from the table. What the table
    // carries is where the weight goes, and that is different for every style:
    // latin leans on the pulse, rock leans on the backbeat with the drummer,
    // dance leans on the offbeat where the open hat sits, pop stays level and
    // out of the way.
    struct StyleSpec
    {
        const Hit* barA; int nA;
        const Hit* barB; int nB;
        const Hit* fill; int nFill;
        float shaker[GrooveEngine::kStepsPerBar];
        // Where the weight sits across the bar. This belongs to the style and
        // not to the engine: latin and pop lean on the one, but a rock part
        // leans on 2 and 4 with the drummer, and a four-on-the-floor bar is
        // nearly even because the kick is on every beat. A single global
        // contour favouring beat one silently cancelled the backbeat emphasis
        // the rock pattern was asking for.
        float accent[4];
        float ghostChance;      // how much the player fills the gaps
        int   fillEveryBars;
    };

    constexpr StyleSpec kStyles[static_cast<int> (GrooveStyle::count)] = {
        // marcha
        { kMarchaA, static_cast<int> (std::size (kMarchaA)),
          kMarchaB, static_cast<int> (std::size (kMarchaB)),
          kMarchaFill, static_cast<int> (std::size (kMarchaFill)),
          { 0.86f, 0.38f, 0.55f, 0.38f,  0.86f, 0.38f, 0.55f, 0.38f,
            0.86f, 0.38f, 0.55f, 0.38f,  0.86f, 0.38f, 0.55f, 0.38f },
          { 1.00f, 0.86f, 0.93f, 0.88f },
          0.35f, 8 },

        // rock - the weight moves onto 2 and 4, with the drummer
        { kRockA, static_cast<int> (std::size (kRockA)),
          kRockB, static_cast<int> (std::size (kRockB)),
          kRockFill, static_cast<int> (std::size (kRockFill)),
          { 0.80f, 0.30f, 0.62f, 0.30f,  0.92f, 0.30f, 0.62f, 0.30f,
            0.80f, 0.30f, 0.62f, 0.30f,  0.92f, 0.30f, 0.62f, 0.30f },
          { 0.94f, 1.00f, 0.92f, 1.00f },
          0.18f, 8 },

        // dance - sixteenths, leaning on the offbeat like an open hat
        { kDanceA, static_cast<int> (std::size (kDanceA)),
          kDanceB, static_cast<int> (std::size (kDanceB)),
          kDanceFill, static_cast<int> (std::size (kDanceFill)),
          { 0.72f, 0.46f, 0.88f, 0.46f,  0.72f, 0.46f, 0.88f, 0.46f,
            0.72f, 0.46f, 0.88f, 0.46f,  0.72f, 0.46f, 0.88f, 0.46f },
          { 1.00f, 0.95f, 0.97f, 0.95f },
          0.30f, 8 },

        // pop - level, quiet, and mostly space
        { kPopA, static_cast<int> (std::size (kPopA)),
          kPopB, static_cast<int> (std::size (kPopB)),
          kPopFill, static_cast<int> (std::size (kPopFill)),
          { 0.72f, 0.24f, 0.52f, 0.24f,  0.72f, 0.24f, 0.52f, 0.24f,
            0.72f, 0.24f, 0.52f, 0.24f,  0.72f, 0.24f, 0.52f, 0.24f },
          { 1.00f, 0.88f, 0.94f, 0.90f },
          0.10f, 8 },
    };

    // A triplet-feel off-eighth sits two thirds of the way through the beat
    // instead of half way, which is a sixth of a beat late.
    constexpr float kFullSwingBeats = 1.0f / 6.0f;

    // Micro-timing. A player is never on the grid, but a percussionist is much
    // closer to it than this range would suggest at full humanize - which is
    // why the default is well under 1.
    constexpr float kFeelBiasBeatsAt120 = 0.004f;   // ~2 ms of natural lateness
    constexpr float kFeelSpreadBeatsAt120 = 0.006f; // ~3 ms either side

    int wrapBar (int barIndex, int period) noexcept
    {
        if (period <= 0)
            return 0;
        return ((barIndex % period) + period) % period;
    }
}

void GrooveEngine::prepare (std::uint32_t seed) noexcept
{
    rng.reset (seed);
    reset();
}

void GrooveEngine::reset() noexcept
{
}

void GrooveEngine::setStyle (GrooveStyle s) noexcept
{
    const int i = static_cast<int> (s);
    style = (i >= 0 && i < static_cast<int> (GrooveStyle::count)) ? s : GrooveStyle::marcha;
}

void GrooveEngine::setHumanize (float amount) noexcept
{
    humanize = clamp01 (amount);
}

void GrooveEngine::setSwing (float amount) noexcept
{
    swing = clamp01 (amount);
}

void GrooveEngine::setIntensity (float amount) noexcept
{
    intensity = clamp01 (amount);
}

void GrooveEngine::setShakerSubdivision (Subdivision s) noexcept
{
    shakerGrid = s == Subdivision::autoDetect ? Subdivision::eighth : s;
}

float GrooveEngine::humanVelocity (float base) noexcept
{
    const float spread = 0.20f * humanize;
    return std::clamp (base * (1.0f + spread * rng.nextSigned()), 0.05f, 1.0f);
}

float GrooveEngine::humanDelay (int step) noexcept
{
    // Swing first: it is a feel, not an error, so it does not scale with
    // humanize and it lands only on the off-eighths.
    float delay = 0.0f;
    if ((step % 4) == 2)
        delay += swing * kFullSwingBeats;

    // Then the part that is an error. Biased late by half its spread so the
    // result never asks to be scheduled before the grid position - the clock
    // hands out positions as they pass, and there is no going back for one.
    delay += humanize * (kFeelBiasBeatsAt120 + kFeelSpreadBeatsAt120 * 0.5f * rng.nextSigned());
    return std::max (0.0f, delay);
}

bool GrooveEngine::isFillBar (int barIndex) const noexcept
{
    const StyleSpec& spec = kStyles[static_cast<int> (style)];
    return wrapBar (barIndex, spec.fillEveryBars) == spec.fillEveryBars - 1;
}

int GrooveEngine::eventsAt (int barIndex, int step, GrooveEvent* out, int maxOut) noexcept
{
    if (out == nullptr || maxOut <= 0 || step < 0 || step >= kStepsPerBar)
        return 0;

    const StyleSpec& spec = kStyles[static_cast<int> (style)];
    int n = 0;
    const int beat = step / 4;
    const float accent = spec.accent[beat & 3];

    if (shakerOn && n < maxOut)
    {
        // The subdivision setting thins the style's pattern; it never adds to
        // it, so a style that does not want sixteenths does not get them just
        // because the user asked for a busy shaker.
        bool allowed = true;
        if (shakerGrid == Subdivision::quarter)
            allowed = (step % 4) == 0;
        else if (shakerGrid == Subdivision::eighth)
            allowed = (step % 2) == 0;

        const float v = allowed ? spec.shaker[step] : 0.0f;
        if (v > 0.0f)
        {
            // Down on the pulse, up on the return. They are different strokes
            // on a real shaker, not the same one twice, and playing them as the
            // same one is what makes a shaker part sound like a click track
            // with noise on it.
            out[n].stroke = (step % 4) == 0 ? Stroke::shakerDown : Stroke::shakerUp;
            out[n].velocity = humanVelocity (v * accent);
            out[n].delayBeats = humanDelay (step);
            ++n;
        }
    }

    if (congasOn)
    {
        const bool fill = isFillBar (barIndex);
        const Hit* bar = fill ? spec.fill
                              : (wrapBar (barIndex, 2) == 1 ? spec.barB : spec.barA);
        const int count = fill ? spec.nFill
                               : (wrapBar (barIndex, 2) == 1 ? spec.nB : spec.nA);

        for (int i = 0; i < count && n < maxOut; ++i)
        {
            if (bar[i].step != step)
                continue;
            out[n].stroke = bar[i].stroke;
            out[n].velocity = humanVelocity (bar[i].velocity * accent);
            out[n].delayBeats = humanDelay (step);
            ++n;
        }

        // Ghost notes: the barely-there fingertip strokes on the sixteenths a
        // player fills with without thinking about it. They are the difference
        // between a part that breathes and one that is merely correct, and they
        // have to stay quiet enough that you notice them only when they stop.
        // How many there are is part of the style: a pop record wants almost
        // none, a marcha is built out of them.
        if (n < maxOut && ! fill && (step % 2) == 1)
        {
            const float chance = spec.ghostChance * intensity * (0.4f + 0.6f * humanize);
            if (rng.nextFloat() < chance)
            {
                out[n].stroke = rng.nextFloat() < 0.5f ? Stroke::toe : Stroke::heel;
                out[n].velocity = humanVelocity (0.16f * accent);
                out[n].delayBeats = humanDelay (step);
                ++n;
            }
        }
    }

    return n;
}

} // namespace vp
