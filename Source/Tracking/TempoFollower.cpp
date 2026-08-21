#include "Tracking/TempoFollower.h"

#include <algorithm>
#include <cmath>

namespace vp
{

void TempoFollower::prepare (double sr) noexcept
{
    sampleRate = sr > 1.0 ? sr : 48000.0;
    reset();
}

void TempoFollower::reset() noexcept
{
    phase = 0.0;
    tempo = 120.0f;
    target = 120.0f;
    conf = 0.0f;
    beatInBar = 0;
    totalBeats = 0;
    phaseErrEma = 0.0f;
    prevPhaseErr = 0.0f;
    lastObservedPhaseErr = 0.0f;
    tempoTrim = 0.0f;
    phaseCorrectionSinceObservation = 0.0f;
    samplesSinceObservation = 0;
    // Nothing has played, so nothing can be flammed against: a clock starting
    // from here must be free to place its first pulse immediately.
    samplesSincePulse = static_cast<int> (sampleRate * 30.0);
    locked = false;
    reanchor = false;
    havePhaseObservation = false;
    tempoTrimEnabled = false;
}

void TempoFollower::resetClock() noexcept
{
    phase = 0.0;
    beatInBar = 0;
    phaseErrEma = 0.0f;
    prevPhaseErr = 0.0f;
    lastObservedPhaseErr = 0.0f;
    tempoTrim = 0.0f;
    phaseCorrectionSinceObservation = 0.0f;
    samplesSinceObservation = 0;
    samplesSincePulse = static_cast<int> (sampleRate * 30.0);
    havePhaseObservation = false;
    reanchor = true;
}

void TempoFollower::setTempoTrimEnabled (bool on) noexcept
{
    if (tempoTrimEnabled && ! on)
    {
        tempoTrim = 0.0f;
        phaseCorrectionSinceObservation = 0.0f;
        samplesSinceObservation = 0;
        havePhaseObservation = false;
    }
    tempoTrimEnabled = on;
}

void TempoFollower::setTargetTempo (float bpm, float confidence) noexcept
{
    if (bpm > 40.0f && bpm < 220.0f)
    {
        if (std::fabs (bpm - target) > 3.0f)
            tempoTrim = 0.0f;

        // Half or double is not a change of tempo, it is the same pulse counted
        // at another metrical level: every stroke the part is already playing
        // stays exactly where it is and only their spacing changes. Gliding into
        // it walks the grid through half a second of tempos the music is not at
        // - measured at 76 -> 152, the clock spent ~500 ms between the two - and
        // that is heard as the percussion running away and catching up. The
        // level is taken at once instead, leaving the phase alone, so the grid
        // stays continuous through it.
        if (tempo > 40.0f)
        {
            const float ratio = bpm / tempo;
            if (std::fabs (ratio - 2.0f) < 0.08f || std::fabs (ratio - 0.5f) < 0.02f)
                tempo = bpm;
        }
        target = bpm;
    }
    conf = clamp01 (confidence);
}

void TempoFollower::forceTempo (float bpm) noexcept
{
    if (bpm <= 40.0f || bpm >= 220.0f)
        return;
    tempo = bpm;
    target = bpm;
    tempoTrim = 0.0f;
    phaseCorrectionSinceObservation = 0.0f;
    samplesSinceObservation = 0;
    havePhaseObservation = false;
}

void TempoFollower::setGridPhase (float targetPhase, float amount) noexcept
{
    const float tgt = wrap01 (targetPhase);
    amount = clamp01 (amount);
    const float err = wrapCentered (static_cast<float> (phase) - tgt);
    phaseErrEma = phaseErrEma * (1.0f - amount) + err * amount;
}

void TempoFollower::snapPhase (float targetPhase) noexcept
{
    phase = static_cast<double> (wrap01 (targetPhase));
    phaseErrEma = 0.0f;
    prevPhaseErr = 0.0f;
    lastObservedPhaseErr = 0.0f;
    phaseCorrectionSinceObservation = 0.0f;
    samplesSinceObservation = 0;
    havePhaseObservation = false;
    // Re-anchor rather than resume. A snap moves the grid under a part that is
    // already playing, and the pulse sitting at the new phase is the whole point
    // of the gesture - it is the "here is the one" the listener just tapped.
    // Suppressing it left the stroke on the one missing and stretched the gap
    // around the correction to as much as 1.9x the sixteenth being played, which
    // is heard as the shaker stumbling. `advance` decides whether the pulse
    // would flam against the one before it; that is the only reason to drop it.
    reanchor = true;
}

void TempoFollower::snapDownbeat (float targetPhase) noexcept
{
    snapPhase (targetPhase);
    beatInBar = 0;
}

void TempoFollower::snapBeat (int beatIndex, float targetPhase) noexcept
{
    snapPhase (targetPhase);
    beatInBar = ((beatIndex % 4) + 4) % 4;
}

void TempoFollower::rotateBarIndex (int delta) noexcept
{
    beatInBar = ((beatInBar + delta) % 4 + 4) % 4;
}

void TempoFollower::observeOnsetPhase (float beatPhaseOfOnset, float strength, int gridPulses) noexcept
{
    if (strength < 0.12f)
        return;

    const float grid = static_cast<float> (gridPulses < 1 ? 4 : gridPulses);
    const float scaled = beatPhaseOfOnset * grid;
    const float nearest = std::round (scaled);
    float err = wrapCentered ((scaled - nearest) / grid);

    if (std::fabs (err) > 0.22f)
        return;

    const float beatSeconds = 60.0f / std::max (40.0f, tempo);
    const float elapsed = static_cast<float> (samplesSinceObservation)
                          / static_cast<float> (sampleRate);

    // Reject double triggers inside the same quarter. For genuine beat
    // observations, phase drift per second is the clock's BPM error.
    if (havePhaseObservation && elapsed < beatSeconds * 0.55f)
        return;

    if (tempoTrimEnabled && havePhaseObservation && elapsed < beatSeconds * 8.0f)
    {
        const float drift = wrapCentered (err - lastObservedPhaseErr
                                          + phaseCorrectionSinceObservation);
        if (std::fabs (drift) < 0.18f && elapsed > 1.0e-3f)
        {
            const float measuredErrorBpm = drift * 60.0f / elapsed;
            const float wantedTrim = std::clamp (-measuredErrorBpm, -3.5f, 3.5f);
            const float trust = std::clamp (strength * 0.08f, 0.10f, 0.28f);
            tempoTrim += (wantedTrim - tempoTrim) * trust;
        }
    }

    lastObservedPhaseErr = err;
    phaseCorrectionSinceObservation = 0.0f;
    samplesSinceObservation = 0;
    havePhaseObservation = true;

    phaseErrEma = phaseErrEma * 0.80f + err * 0.20f;
}

ClockTick TempoFollower::advance (int numSamples) noexcept
{
    ClockTick tick;

    samplesSinceObservation = std::min (samplesSinceObservation + numSamples,
                                        static_cast<int> (sampleRate * 30.0));
    samplesSincePulse = std::min (samplesSincePulse + numSamples,
                                  static_cast<int> (sampleRate * 30.0));
    if (samplesSinceObservation > static_cast<int> (sampleRate * 2.5))
    {
        const float a = 1.0f - std::exp (-static_cast<float> (numSamples)
                                         / static_cast<float> (sampleRate * 5.0));
        tempoTrim += (0.0f - tempoTrim) * a;
    }

    const float effectiveTarget = target + tempoTrim;
    const float err = effectiveTarget - tempo;

    // A small correction used to be applied whole, on the block it arrived. That
    // is a step in the rate of the grid, and the decoder refreshes six times a
    // second, so every wobble inside the window went straight through to the
    // part and the tempo was never actually still. It is still fast - a couple
    // of BPM closes in about a fifth of a second, which no listener hears as a
    // change of speed - but it is a glide now rather than a jump.
    const float tau = std::fabs (err) <= (locked ? 2.5f : 1.2f)
                          ? 0.045f
                          : (locked ? 0.55f : 0.90f);
    const float a = 1.0f - std::exp (-static_cast<float> (numSamples)
                                     / (tau * static_cast<float> (sampleRate)));
    tempo += err * a;
    // Settle rather than approach forever, so `currentTempo` still reads as the
    // round number the rest of the engine compares against.
    if (std::fabs (effectiveTarget - tempo) < 0.02f)
        tempo = effectiveTarget;

    if (tempo < 40.0f) tempo = 40.0f;
    if (tempo > 220.0f) tempo = 220.0f;

    const float dErr = wrapCentered (phaseErrEma - prevPhaseErr);
    prevPhaseErr = phaseErrEma;

    // Standing phase error is delay, not a slow song. Only the *change* in
    // error means the clock is running at the wrong speed.
    float steer = 0.0f;
    float phaseNudge = 0.0f;
    if (locked)
    {
        float steerGain = 1.4f;
        float nudgeGain = tempoTrimEnabled ? 0.018f : 0.045f;
        float steerLim = 0.006f;
        float nudgeLim = 0.006f;
        switch (follow)
        {
            case FollowStrength::low:
                steerGain = 0.4f;
                nudgeGain = 0.025f;
                steerLim = 0.003f;
                nudgeLim = 0.004f;
                break;
            case FollowStrength::high:
                steerGain = 2.2f;
                nudgeGain = 0.07f;
                steerLim = 0.012f;
                nudgeLim = 0.010f;
                break;
            case FollowStrength::medium:
                break;
        }
        steer = std::clamp (dErr * steerGain, -steerLim, steerLim);
        phaseNudge = std::clamp (phaseErrEma * nudgeGain, -nudgeLim, nudgeLim);
    }
    else
    {
        steer = std::clamp (phaseErrEma * 0.08f, -0.030f, 0.030f);
    }

    if (phaseNudge != 0.0f)
    {
        phase -= static_cast<double> (phaseNudge);
        phaseCorrectionSinceObservation += phaseNudge;
        if (phase < 0.0)
        {
            phase += 1.0;
            beatInBar = (beatInBar + 3) & 3;
        }
        if (phase >= 1.0)
        {
            phase -= 1.0;
            beatInBar = (beatInBar + 1) & 3;
        }
        phaseErrEma -= phaseNudge;
    }

    const float effTempo = tempo * (1.0f - steer);

    const int beatAtStart = beatInBar;
    const double beats = (static_cast<double> (effTempo) / 60.0)
                         * (static_cast<double> (numSamples) / sampleRate);
    const double prevPhase = phase;
    phase += beats;

    if (phase >= 1.0)
    {
        const int crossed = static_cast<int> (phase);
        phase -= static_cast<double> (crossed);
        tick.wrappedBeat = true;
        totalBeats += crossed;
        beatInBar = (beatInBar + crossed) & 3;
        if (beatInBar == 0)
            tick.wrappedBar = true;
    }

    const double ppb = static_cast<double> (pulsesPerBeat);
    double prevPulse = prevPhase * ppb;

    // The span of grid positions this block covers is half-open, so a pulse
    // landing exactly on where the block starts belongs to the block before it.
    // That is right while the clock is free-running and wrong immediately after
    // a snap, where the phase was *placed* on a pulse and nothing has played it
    // yet. Reaching back by a hair puts that pulse inside this block.
    if (reanchor)
    {
        // Just enough to bring a pulse sitting exactly on the new phase inside
        // the span, and no more: a snap onto a grid position lands on it
        // exactly, while a snap to some phase between two of them - a re-lock
        // onto the song rather than a declared downbeat - must not conjure a
        // pulse that was never due.
        constexpr double kAnchorTol = 1.0e-9;

        // Only if it would still be heard as a stroke. Under half a pulse - and
        // never under 20 ms - it fuses with the one just played into a single
        // thick attack, so the grid re-anchors silently and the next pulse
        // carries the correction instead.
        const double pulseSeconds = 60.0 / static_cast<double> (std::max (40.0f, tempo)) / ppb;
        const double refractory = std::max (sampleRate * 0.020,
                                            sampleRate * pulseSeconds * 0.5);
        if (static_cast<double> (samplesSincePulse) >= refractory)
            prevPulse -= kAnchorTol;
    }
    reanchor = false;

    const double curPulse = (prevPhase + beats) * ppb;
    const int from = static_cast<int> (std::floor (prevPulse));
    const int to = static_cast<int> (std::floor (curPulse));
    const double span = std::max (1.0e-12, curPulse - prevPulse);

    for (int p = from + 1; p <= to && tick.pulsesFired < 8; ++p)
    {
        const double frac = (static_cast<double> (p) - prevPulse) / span;
        int offset = static_cast<int> (std::lround (frac * static_cast<double> (numSamples)));
        if (offset < 0) offset = 0;
        if (offset >= numSamples) offset = numSamples - 1;

        // Which beat of the bar this pulse belongs to, read off the pulse index
        // itself. The old form asked only whether the block had crossed a beat
        // at all, so a block containing the downbeat *and* the offbeat after it
        // - one buffer at a fast tempo, or any large buffer - labelled that
        // offbeat with the previous beat. PercussionEngine picks the conga from
        // that label, so the tumbao played the wrong drum on the way past.
        const int idx = ((p % pulsesPerBeat) + pulsesPerBeat) % pulsesPerBeat;
        const int beatOffset = static_cast<int> (std::floor (static_cast<double> (p)
                                                             / static_cast<double> (pulsesPerBeat)));
        const int barBeat = (beatAtStart + beatOffset) & 3;

        tick.pulseIndex[tick.pulsesFired] = idx;
        tick.pulseOffset[tick.pulsesFired] = offset;
        tick.pulseBeatInBar[tick.pulsesFired] = barBeat;
        tick.barPulse[tick.pulsesFired] = barBeat * pulsesPerBeat + idx;
        ++tick.pulsesFired;
        // Counted from where the pulse actually sits in the block, not from its
        // end, so the next re-anchor measures a real interval.
        samplesSincePulse = -offset;
    }

    tick.tempoBpm = tempo;
    return tick;
}

} // namespace vp
