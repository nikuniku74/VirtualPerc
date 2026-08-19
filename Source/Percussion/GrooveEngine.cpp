#include "Percussion/GrooveEngine.h"

#include <algorithm>
#include <iterator>
#include <cmath>

namespace vp
{

namespace
{
    // The marcha, on a sixteenth grid. Steps 0, 4, 8, 12 are the quarters;
    // 2, 6, 10, 14 are the off-eighths; the odd steps are the sixteenths in
    // between, where the muffled strokes live.
    //
    // What makes this a marcha and not a list of conga hits is the last two
    // entries: **open, open** on 4 and the "and" of 4, together, pulling into
    // the next bar. Everything before them is there to leave room for that.
    struct Hit { int step; Stroke stroke; float velocity; };

    // Bar A - the plain statement.
    constexpr Hit kBarA[] = {
        {  0, Stroke::heel,  0.34f },
        {  2, Stroke::toe,   0.30f },
        {  4, Stroke::slap,  0.94f },   // the crack on 2
        {  6, Stroke::toe,   0.28f },
        {  8, Stroke::tumba, 0.82f },   // the bass on 3
        { 10, Stroke::toe,   0.28f },
        { 12, Stroke::open,  0.86f },   // and the pair
        { 14, Stroke::open,  0.92f },
    };

    // Bar B - the answer. Same skeleton, but the bass is doubled and the heel
    // moves, which is the ordinary way a player keeps two bars from being the
    // same bar twice.
    constexpr Hit kBarB[] = {
        {  0, Stroke::heel,  0.32f },
        {  2, Stroke::toe,   0.29f },
        {  4, Stroke::slap,  0.92f },
        {  6, Stroke::heel,  0.26f },
        {  8, Stroke::tumba, 0.80f },
        { 10, Stroke::tumba, 0.52f },
        { 12, Stroke::open,  0.84f },
        { 14, Stroke::open,  0.94f },
    };

    // The fill that closes an eight-bar phrase. It replaces the second half of
    // the bar rather than being added on top of it, so the bar still ends where
    // the next one begins.
    constexpr Hit kFill[] = {
        {  8, Stroke::open,  0.72f },
        { 10, Stroke::slap,  0.80f },
        { 11, Stroke::open,  0.62f },
        { 12, Stroke::slap,  0.88f },
        { 13, Stroke::open,  0.70f },
        { 14, Stroke::open,  0.95f },
        { 15, Stroke::slap,  0.78f },
    };

    constexpr int kFillEveryBars = 8;

    // Accent contour across the bar. Beat one carries the bar; beat three is
    // the secondary. A flat contour is the single clearest tell that a part was
    // sequenced rather than played.
    constexpr float kBarAccent[4] = { 1.00f, 0.86f, 0.93f, 0.88f };

    // A triplet-feel off-eighth sits two thirds of the way through the beat
    // instead of half way, which is a sixth of a beat late.
    constexpr float kFullSwingBeats = 1.0f / 6.0f;

    // Micro-timing. A player is never on the grid, but a percussionist is much
    // closer to it than this range would suggest at full humanize - which is
    // why the default is well under 1.
    constexpr float kFeelBiasBeatsAt120 = 0.004f;   // ~2 ms of natural lateness
    constexpr float kFeelSpreadBeatsAt120 = 0.006f; // ~3 ms either side

    const Hit* barFor (int barIndex, int& count) noexcept
    {
        if (((barIndex % kFillEveryBars) + kFillEveryBars) % kFillEveryBars == kFillEveryBars - 1)
        {
            count = static_cast<int> (std::size (kFill));
            return kFill;
        }
        const bool second = (((barIndex % 2) + 2) % 2) == 1;
        count = static_cast<int> (second ? std::size (kBarB) : std::size (kBarA));
        return second ? kBarB : kBarA;
    }

    bool fillBar (int barIndex) noexcept
    {
        return ((barIndex % kFillEveryBars) + kFillEveryBars) % kFillEveryBars == kFillEveryBars - 1;
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

int GrooveEngine::eventsAt (int barIndex, int step, GrooveEvent* out, int maxOut) noexcept
{
    if (out == nullptr || maxOut <= 0 || step < 0 || step >= kStepsPerBar)
        return 0;

    int n = 0;
    const int beat = step / 4;
    const float accent = kBarAccent[beat & 3];

    if (shakerOn && n < maxOut)
    {
        // Down on the pulse, up on the return. They are different strokes on a
        // real shaker, not the same one twice, and playing them as the same one
        // is what makes a shaker part sound like a click track with noise on it.
        bool play = false;
        Stroke s = Stroke::shakerDown;
        float v = 0.0f;

        if ((step % 4) == 0)
        {
            play = true;
            s = Stroke::shakerDown;
            v = 0.86f;
        }
        else if ((step % 4) == 2 && shakerGrid != Subdivision::quarter)
        {
            play = true;
            s = Stroke::shakerUp;
            v = 0.55f;
        }
        else if ((step % 2) == 1 && shakerGrid == Subdivision::sixteenth)
        {
            play = true;
            s = Stroke::shakerUp;
            v = 0.38f;
        }

        if (play)
        {
            out[n].stroke = s;
            out[n].velocity = humanVelocity (v * accent);
            out[n].delayBeats = humanDelay (step);
            ++n;
        }
    }

    if (congasOn)
    {
        int count = 0;
        const Hit* bar = barFor (barIndex, count);
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
        if (n < maxOut && ! fillBar (barIndex) && (step % 2) == 1)
        {
            const float chance = 0.35f * intensity * (0.4f + 0.6f * humanize);
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
