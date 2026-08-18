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
    lastPulsePhase = 0.0;
    tempo = 120.0f;
    target = 120.0f;
    conf = 0.0f;
    beatInBar = 0;
    totalBeats = 0;
    sameSignCount = 0;
    lastPhaseErr = 0.0f;
    phaseErrEma = 0.0f;
    prevPhaseErr = 0.0f;
    lastObservedPhaseErr = 0.0f;
    tempoTrim = 0.0f;
    phaseCorrectionSinceObservation = 0.0f;
    samplesSinceObservation = 0;
    locked = false;
    primed = false;
    havePhaseObservation = false;
    tempoTrimEnabled = false;
    frozenLatencyMs = -1.0f;
}

void TempoFollower::resetClock() noexcept
{
    phase = 0.0;
    lastPulsePhase = 0.0;
    beatInBar = 0;
    sameSignCount = 0;
    lastPhaseErr = 0.0f;
    phaseErrEma = 0.0f;
    prevPhaseErr = 0.0f;
    lastObservedPhaseErr = 0.0f;
    tempoTrim = 0.0f;
    phaseCorrectionSinceObservation = 0.0f;
    samplesSinceObservation = 0;
    havePhaseObservation = false;
    primed = true;
}

void TempoFollower::setLatencyCompensationMs (float ms) noexcept
{
    latencyMs = std::clamp (ms, 0.0f, 90.0f);
    if (frozenLatencyMs < 0.0f && latencyMs > 1.0f)
        frozenLatencyMs = latencyMs;
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
    lastPulsePhase = phase;
    phaseErrEma = 0.0f;
    prevPhaseErr = 0.0f;
    sameSignCount = 0;
    lastPhaseErr = 0.0f;
    lastObservedPhaseErr = 0.0f;
    phaseCorrectionSinceObservation = 0.0f;
    samplesSinceObservation = 0;
    havePhaseObservation = false;
    primed = false;
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

    if (err * lastPhaseErr > 0.0f)
        ++sameSignCount;
    else
        sameSignCount = 1;
    lastPhaseErr = err;
    phaseErrEma = phaseErrEma * 0.80f + err * 0.20f;
}

ClockTick TempoFollower::advance (int numSamples) noexcept
{
    ClockTick tick;

    samplesSinceObservation = std::min (samplesSinceObservation + numSamples,
                                        static_cast<int> (sampleRate * 30.0));
    if (samplesSinceObservation > static_cast<int> (sampleRate * 2.5))
    {
        const float a = 1.0f - std::exp (-static_cast<float> (numSamples)
                                         / static_cast<float> (sampleRate * 5.0));
        tempoTrim += (0.0f - tempoTrim) * a;
    }

    const float effectiveTarget = target + tempoTrim;
    const float err = effectiveTarget - tempo;
    if (locked && std::fabs (err) <= 2.5f)
        tempo = effectiveTarget;
    else if (std::fabs (err) <= 1.2f)
        tempo = effectiveTarget;
    else
    {
        const float tau = locked ? 0.55f : 0.90f;
        const float a = 1.0f - std::exp (-static_cast<float> (numSamples)
                                         / (tau * static_cast<float> (sampleRate)));
        tempo += err * a;
    }

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

    const double prevOut = primed ? -1.0e-9 : prevPhase;
    const double curOut  = (primed ? 0.0 : prevPhase) + beats;
    primed = false;

    const double prevPulse = prevOut * static_cast<double> (pulsesPerBeat);
    const double curPulse  = curOut * static_cast<double> (pulsesPerBeat);
    const int from = static_cast<int> (std::floor (prevPulse));
    const int to = static_cast<int> (std::floor (curPulse));
    const double span = std::max (1.0e-12, curPulse - prevPulse);

    for (int p = from + 1; p <= to && tick.pulsesFired < 8; ++p)
    {
        const double frac = (static_cast<double> (p) - prevPulse) / span;
        int offset = static_cast<int> (std::lround (frac * static_cast<double> (numSamples)));
        if (offset < 0) offset = 0;
        if (offset >= numSamples) offset = numSamples - 1;

        const int idx = ((p % pulsesPerBeat) + pulsesPerBeat) % pulsesPerBeat;
        int barBeat = beatAtStart;
        if (tick.wrappedBeat && idx == 0)
            barBeat = beatInBar;
        else if (tick.wrappedBeat && idx != 0)
            barBeat = beatAtStart;

        tick.pulseIndex[tick.pulsesFired] = idx;
        tick.pulseOffset[tick.pulsesFired] = offset;
        tick.pulseBeatInBar[tick.pulsesFired] = barBeat;
        tick.barPulse[tick.pulsesFired] = barBeat * pulsesPerBeat + idx;
        ++tick.pulsesFired;
    }

    lastPulsePhase = phase;
    return tick;
}

} // namespace vp
