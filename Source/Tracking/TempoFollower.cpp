#include "Tracking/TempoFollower.h"

#include <algorithm>
#include <cmath>

namespace vp
{

namespace
{
    /** How little the clock may be asked to believe the tempo it is handed. */
    constexpr float kMinTempoTrust = 0.30f;

    /** And what "not believing it" costs, in seconds: at no trust at all the
        clock averages the target over this long instead of taking it.
        Two and a half seconds is about five beats at 120 BPM - long enough to
        ride out a passage whose beats are badly placed, short enough that a
        real change arriving in the middle of one is still taken inside a
        phrase. At full trust it is not applied at all.

        A floor on the constant rather than a multiplier of it, because the
        pathology lives in the *small* corrections. Measured on a passage
        without a drummer the committed tempo wanders about a BPM and a half
        either way, which is inside the band the clock closes in 45 ms - so
        stretching that by three still takes every wobble whole, and the wobble
        is what comes out as phase. */
    constexpr float kPoorEvidenceTauSec = 2.50f;

    /** How far the clock will lean away from its own grid while the evidence is
        poor, in beats.

        This is the one that matters, and finding out why took a bench. A
        passage without a drummer does not make the analysis noisy so much as
        make it *late*: a pad and a bass note swell into the beat where a stick
        lands on it, the activation crests a couple of frames after the beat,
        and the fit follows that faithfully for as long as the passage lasts.
        Measured with `VPAlign`, the same passage with the lateness taken out
        costs 29.5 ms at worst - the same as an accelerando - and with 44 ms of
        it costs 79.5 ms. It is an offset held for ten seconds, so no averaging
        shorter than the passage can touch it: at full smoothing the clock still
        followed it to within a millisecond or two.

        What it can be given is a limit. The clock stays free to correct the
        small errors that are the analysis being imprecise, and refuses to
        follow a large one until the evidence supporting it is as good as this
        song has been giving. The refusal is bounded and self-clearing - the
        limit lifts the moment the fit tightens again - which is what separates
        it from holding the tempo. docs/STATUS.md records five attempts at
        holding, and each one cost half a bar on an accelerando because a held
        tempo integrates into a phase error with no bound at all; a lean that
        cannot exceed two hundredths of a beat costs at most that.

        Two hundredths of a beat is 10 ms at 120 BPM: under what a listener
        picks out, and well over the 0.012 the loop treats as its own noise
        floor, so an ordinary correction is not touched by it. */
    constexpr float kPoorLeanBeats = 0.020f;

    /** Below this the analysis is not trusted to write a *rate* at all.

        Everything else `tempoTrust` scales - the glide, the lean cap - is still
        wanted when the evidence is at its worst, only slower and smaller, which
        is why that trust bottoms out at kMinTempoTrust instead of at zero. The
        trim is not like that. It is the one term here that turns a phase slope
        into a tempo and *keeps* it, and a passage whose beats are badly placed
        is a phase slope the band never played: the activation slides late as the
        kit goes out and slides back as it returns. Integrating that leaves a
        rate error standing after the passage has ended, which is the one failure
        an integrator has that a proportional loop does not.

        So here the scale runs to zero, and it runs to zero early. Trust is a
        ratio against what this song has been fitting, so it slides rather than
        switches: through a passage without a drummer it touches the floor at its
        worst and spends most of its length somewhere above it, and a term scaled
        from the floor upwards would still be half on throughout. Measured with
        `VPAlign` over eight songs, the trim switched on everywhere costs that
        passage 19.4 -> 24.0 ms and an accelerando 18.9 -> 28.6; scaled from a
        half it costs neither and keeps what it is for. */
    constexpr float kTrustToSetRate = 0.50f;

    /** How many beats have to push the phase the same way before the trim is
        allowed to believe them whole. Three: at 120 BPM that is a second and a
        half, which no ramp finishes inside and no run of noise reaches often. */
    constexpr int kDriftAgreeing = 3;

    /** 0 below that line, 1 where the analysis is fitting as well as this song
        has been fitting all along. */
    inline float rateTrustScale (float trust) noexcept
    {
        return std::clamp ((trust - kTrustToSetRate) / (1.0f - kTrustToSetRate), 0.0f, 1.0f);
    }

    /** Above which the analysis is not leaning, it is somewhere else.

        The cap above must never be able to strand the clock. A lean is small by
        construction - a stroke heard a few tens of milliseconds late, which at
        any tempo this app follows is under a tenth of a beat - while a new
        song, an edit or a grid the decoder has just re-anchored puts the target
        a quarter beat away or more. Those have to be followed at once and
        whatever the evidence looks like, because the evidence looking poor is
        exactly what a song that has just changed produces. So the cap applies
        below this and not above it. */
    constexpr float kLeanIsElsewhere = 0.15f;
}

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
    phaseTarget = 0.0f;
    havePhaseTarget = false;
    farTargetSamples = 0;
    farTargetSign = 0;
    tempoTrim = 0.0f;
    lastDrift = 0.0f;
    driftSameWay = 0;
    phaseCorrectionSinceObservation = 0.0f;
    samplesSinceObservation = 0;
    // Nothing has played, so nothing can be flammed against: a clock starting
    // from here must be free to place its first pulse immediately.
    samplesSincePulse = static_cast<int> (sampleRate * 30.0);
    locked = false;
    reanchor = false;
    havePhaseObservation = false;
    tempoTrimEnabled = false;
    tempoTrust = 1.0f;
    transitionSamplesRemaining = 0;
}

void TempoFollower::resetClock() noexcept
{
    phase = 0.0;
    beatInBar = 0;
    phaseErrEma = 0.0f;
    prevPhaseErr = 0.0f;
    lastObservedPhaseErr = 0.0f;
    phaseTarget = 0.0f;
    havePhaseTarget = false;
    farTargetSamples = 0;
    farTargetSign = 0;
    tempoTrim = 0.0f;
    lastDrift = 0.0f;
    driftSameWay = 0;
    phaseCorrectionSinceObservation = 0.0f;
    samplesSinceObservation = 0;
    samplesSincePulse = static_cast<int> (sampleRate * 30.0);
    havePhaseObservation = false;
    transitionSamplesRemaining = 0;
    reanchor = true;
}

void TempoFollower::setTempoTrust (float trust) noexcept
{
    tempoTrust = std::clamp (trust, kMinTempoTrust, 1.0f);
}

void TempoFollower::setTempoTrimEnabled (bool on) noexcept
{
    if (tempoTrimEnabled && ! on)
    {
        tempoTrim = 0.0f;
        lastDrift = 0.0f;
        driftSameWay = 0;
        phaseCorrectionSinceObservation = 0.0f;
        samplesSinceObservation = 0;
        havePhaseObservation = false;
    }
    tempoTrimEnabled = on;
}

void TempoFollower::setTargetTempo (float bpm, float confidence) noexcept
{
    conf = clamp01 (confidence);

    // A confirmed transition and the ordinary fit are published together. The
    // fit still describes the tempo being left on the first confirmed frame;
    // accepting it here would undo beginTempoTransition in the very next call.
    // The guard lasts one adopted beat, after which the ordinary glide is
    // exactly the path it was before.
    if (tempoTransitionActive())
        return;

    if (bpm > 40.0f && bpm < 220.0f)
    {
        // A step of more than a few BPM is a different tempo, not a drift, so
        // the trim's whole state goes with it - the correction it had built and
        // the run of agreeing beats that earned it.
        if (std::fabs (bpm - target) > 3.0f)
        {
            tempoTrim = 0.0f;
            lastDrift = 0.0f;
            driftSameWay = 0;
        }

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
}

void TempoFollower::beginTempoTransition (float bpm) noexcept
{
    if (! std::isfinite (bpm) || bpm <= 40.0f || bpm >= 220.0f)
        return;

    tempo = bpm;
    target = bpm;
    tempoTrim = 0.0f;
    lastDrift = 0.0f;
    driftSameWay = 0;
    transitionSamplesRemaining =
        std::max (1, static_cast<int> (std::lround (sampleRate * 60.0 / bpm)));
}

void TempoFollower::forceTempo (float bpm) noexcept
{
    if (bpm <= 40.0f || bpm >= 220.0f)
        return;
    tempo = bpm;
    target = bpm;
    tempoTrim = 0.0f;
    lastDrift = 0.0f;
    driftSameWay = 0;
    phaseCorrectionSinceObservation = 0.0f;
    samplesSinceObservation = 0;
    havePhaseObservation = false;
    transitionSamplesRemaining = 0;
}

void TempoFollower::setGridPhase (float targetPhase, float tauSeconds) noexcept
{
    phaseTarget = wrapCentered (static_cast<float> (phase) - wrap01 (targetPhase));
    phaseTargetTau = tauSeconds > 0.01f ? tauSeconds : 0.01f;
    havePhaseTarget = true;
}

void TempoFollower::snapPhase (float targetPhase, bool keepBarInStep) noexcept
{
    // Carry the count over the boundary the snap crosses.
    //
    // `beatInBar` is advanced in one place only - `advance`, when the phase
    // runs past 1.0 - so a snap that *jumps* the phase past 1.0, or back over
    // 0.0, moves the grid without the count following it. Forwards, the clock
    // never wraps for that beat and the bar falls one behind the song;
    // backwards, it wraps twice and the bar gains one. Either way the count is
    // silently rotated by a quarter, and nothing downstream can tell that from
    // the song genuinely being counted from somewhere else.
    //
    // It is not a rare case. While the part is waiting to come in the tracker
    // re-places the grid on any error over four hundredths of a beat, so a song
    // sitting near the boundary snaps across it repeatedly, and every crossing
    // rotates the bar again. Measured over thirty rendered tracks, closing this
    // took the entry onto the true one from 4 in 25 to 8 in 25 on a microphone
    // in a room, and from 19 in 25 to 21 in 25 on a line feed - see
    // scripts/probe_bar.cpp.
    //
    // A correction is the shortest way round, so that is what decides whether a
    // boundary was crossed at all.
    const double from = phase;
    const double to = static_cast<double> (wrap01 (targetPhase));
    if (keepBarInStep)
    {
        const double moved = from + static_cast<double> (wrapCentered (
                                 static_cast<float> (to - from)));
        if (moved >= 1.0)
        {
            beatInBar = (beatInBar + 1) & 3;
            ++totalBeats;
        }
        else if (moved < 0.0)
        {
            beatInBar = (beatInBar + 3) & 3;
            --totalBeats;
        }
    }

    phase = to;
    phaseErrEma = 0.0f;
    prevPhaseErr = 0.0f;
    lastObservedPhaseErr = 0.0f;
    phaseTarget = 0.0f;
    havePhaseTarget = false;
    farTargetSamples = 0;
    farTargetSign = 0;
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
            // What is left over *after* the trim already in force, because the
            // clock was running at `target + tempoTrim` over the interval this
            // was measured across. So it is a correction to add, not the whole
            // answer to ease towards: pulling the trim towards it instead
            // charges the trim for its own work, and the two meet halfway.
            // Measured against a song 1 BPM away from an anchored tempo, that
            // settled at a trim of exactly 0.500 and a clock of 80.500 - and
            // stayed there for as long as the run went on, whatever the tempo
            // and whatever the strength. Half of every standing rate error was
            // permanently left for the phase loop to carry as a standing lean.
            //
            // Scaled twice: by how hard the beat was, which is how sure this
            // one observation is, and by how well the analysis is fitting this
            // song at all, which is what stops a passage without a drummer
            // writing its own lateness into the tempo. See kTrustToSetRate -
            // that second factor is why the trim can now be left on while the
            // band is actually moving, which is the only time it is any use.
            //
            // And by whether the drift is going anywhere. A band that is
            // changing speed pushes the phase the same way beat after beat; the
            // analysis's own jitter does not, and this measurement cannot tell
            // them apart from one beat alone. A frame of jitter is 20 ms, so the
            // difference between two consecutive beats carries ~28 ms of noise
            // over an interval of half a second - a slope of nearly 7 BPM the
            // band never played, and the trim integrates whatever it is handed.
            //
            // Agreement in *sign* is what separates them, and it costs nothing
            // on a real ramp: two or three beats all pushing the same way and
            // the gain is whole, which at 120 BPM is a second and a half. It is
            // the same discriminator Tracking/HarmonicChange.h uses to tell a
            // chord from a snare - a step holds, an impulse does not - and it is
            // here for the same reason. Measured on the gentle accelerando,
            // where there is barely any lag to correct and so nothing but noise
            // to integrate, this is what takes the cost back off.
            if (drift * lastDrift > 0.0f)
                driftSameWay = std::min (driftSameWay + 1, kDriftAgreeing);
            else
                driftSameWay = 0;
            lastDrift = drift;

            const float measuredErrorBpm = drift * 60.0f / elapsed;
            const float trust = std::clamp (strength * 0.08f, 0.10f, 0.28f)
                                * rateTrustScale (tempoTrust)
                                * (static_cast<float> (driftSameWay)
                                   / static_cast<float> (kDriftAgreeing));
            tempoTrim = std::clamp (tempoTrim - measuredErrorBpm * trust, -3.5f, 3.5f);
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
    if (numSamples <= 0)
    {
        ClockTick tick;
        tick.tempoBpm = tempo;
        return tick;
    }

    // A transition boundary is a real rate boundary inside this callback.
    // Process it as two bounded segments so pulse times, controller state and
    // every other observable are identical to two explicit audio callbacks.
    // `advanceSegment` never calls back here, so this split is depth one.
    if (transitionSamplesRemaining > 0
        && numSamples > transitionSamplesRemaining)
    {
        const int rapidSamples = transitionSamplesRemaining;
        ClockTick merged = advanceSegment (rapidSamples);
        const ClockTick ordinary =
            advanceSegment (numSamples - rapidSamples);

        merged.reanchored = merged.reanchored || ordinary.reanchored;
        merged.wrappedBeat = merged.wrappedBeat || ordinary.wrappedBeat;
        merged.wrappedBar = merged.wrappedBar || ordinary.wrappedBar;
        merged.tempoBpm = ordinary.tempoBpm;

        // Both segment ticks are chronological. If their combined pulse count
        // exceeds ClockTick's fixed capacity, retain the first eight exactly as
        // the former single-block loop did.
        for (int i = 0; i < ordinary.pulsesFired && merged.pulsesFired < 8; ++i)
        {
            const int out = merged.pulsesFired++;
            merged.pulseIndex[out] = ordinary.pulseIndex[i];
            merged.pulseOffset[out] =
                rapidSamples + ordinary.pulseOffset[i];
            merged.pulseBeatInBar[out] = ordinary.pulseBeatInBar[i];
            merged.barPulse[out] = ordinary.barPulse[i];
            merged.pulsePhaseError[out] = ordinary.pulsePhaseError[i];
        }
        return merged;
    }

    return advanceSegment (numSamples);
}

ClockTick TempoFollower::advanceSegment (int numSamples) noexcept
{
    ClockTick tick;
    const bool rapidTransition = transitionSamplesRemaining > 0;

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

    // Acquisition and playing are deliberately different jobs. Before the
    // first stroke there is nothing to disturb, so take a credible new rate
    // quickly. Once the part is sounding, the decoder's roughly 6 Hz refresh
    // must not be heard as six tiny accelerations a second: a player holds the
    // pulse and lets phase correction deal with the small timing differences.
    // A real, sustained tempo move still crosses the wider branch and closes in
    // well under a second; only the sub-2.5 BPM wobble is given more inertia.
    const float glide = locked
                            ? (std::fabs (err) <= 2.5f ? 0.22f : 0.28f)
                            : (std::fabs (err) <= 1.2f ? 0.045f : 0.18f);

    // Floored while the beats the tempo was fitted through are worse placed
    // than this song's own - which is what a passage with the drummer out looks
    // like from inside the fit, and is measured in Tracking/PhaseTrust.h. At
    // full trust, which is everything else including an accelerando, `poor` is
    // zero and this is the same number it has always been.
    const float poor = (1.0f - std::clamp (tempoTrust, kMinTempoTrust, 1.0f))
                       / (1.0f - kMinTempoTrust);
    const float tau = std::max (glide, poor * kPoorEvidenceTauSec);
    const float a = 1.0f - std::exp (-static_cast<float> (numSamples)
                                     / (tau * static_cast<float> (sampleRate)));
    tempo += err * a;
    // Settle rather than approach forever, so `currentTempo` still reads as the
    // round number the rest of the engine compares against.
    if (std::fabs (effectiveTarget - tempo) < 0.02f)
        tempo = effectiveTarget;

    if (tempo < 40.0f) tempo = 40.0f;
    if (tempo > 220.0f) tempo = 220.0f;

    // `setGridPhase` records the current centered clock-minus-song error. Keep
    // this raw observation beside the filtered path: a confirmed step has only
    // one beat to spend the offset accumulated during causal confirmation, and
    // filtering it first hides most of the correction that is actually owed.
    const bool haveRawGridPhaseError = havePhaseTarget;
    const float rawGridPhaseError = phaseTarget;

    // The error the analysis reported this block, smoothed over real time.
    // Cleared after use: a callback that brought no observation must not
    // re-inject the last one, which is what a per-block blend did thirty times
    // over between one hypothesis and the next.
    if (havePhaseTarget)
    {
        // How far the analysis is from what the loop currently believes. Its own
        // noise is hundredths of a beat; a quarter of one is a different grid -
        // a new song, an edit, the decoder re-anchoring - and averaging that in
        // over nine tenths of a second is what actually decides how long a
        // re-lock takes, whatever the steering is allowed to do about it.
        //
        // One hypothesis cannot say which of the two it is, so it has to keep
        // saying it: a quarter of a second is two refreshes of the analysis, and
        // noise does not hold a sign across them. After that the target is
        // adopted about as fast as it is far, and the slow average goes back to
        // being the slow average as soon as the gap closes.
        constexpr float kFarTarget = 0.06f;
        const float gap = wrapCentered (phaseTarget - phaseErrEma);
        const bool sameWay = farTargetSign == 0
                             || gap * static_cast<float> (farTargetSign) > 0.0f;
        if (std::fabs (gap) > kFarTarget && sameWay)
        {
            if (farTargetSign == 0)
                farTargetSign = gap > 0.0f ? 1 : -1;
            farTargetSamples = std::min (farTargetSamples + numSamples,
                                         static_cast<int> (sampleRate));
        }
        else
        {
            farTargetSamples = 0;
            farTargetSign = 0;
        }

        float tau = phaseTargetTau;
        if (farTargetSamples > static_cast<int> (sampleRate * 0.25))
            tau = std::clamp (phaseTargetTau * kFarTarget / std::fabs (gap),
                              0.10f, phaseTargetTau);

        const float a = 1.0f - std::exp (-static_cast<float> (numSamples)
                                         / (tau * static_cast<float> (sampleRate)));
        phaseErrEma += (phaseTarget - phaseErrEma) * a;
        havePhaseTarget = false;

        // And however long it is averaged over, how far it may go. See
        // kPoorLeanBeats: a passage whose beats are badly placed moves the
        // analysis's phase as an offset held for the length of the passage, and
        // an offset is the one thing a low-pass cannot take out.
        const float poorLean = (1.0f - std::clamp (tempoTrust, kMinTempoTrust, 1.0f))
                               / (1.0f - kMinTempoTrust);
        if (poorLean > 0.0f && std::fabs (phaseTarget) < kLeanIsElsewhere)
        {
            const float lim = kPoorLeanBeats
                              + (1.0f - poorLean) * (kLeanIsElsewhere - kPoorLeanBeats);
            phaseErrEma = std::clamp (phaseErrEma, -lim, lim);
        }
    }

    // Phase is corrected by *rate*, never by moving the grid.
    //
    // A standing error used to be worked off by subtracting it straight from
    // `phase`. That moves the grid out from under a part which is already
    // playing on it, and the pulses for the block are then read off the moved
    // grid: a position the clock had just passed could be passed a second time,
    // or stepped over without being played. Measured over two minutes against a
    // decoder whose phase wobbles by 3% of a beat, roughly one pulse in a
    // hundred came out either doubled on top of its neighbour or missing
    // altogether - a shaker that now and then plays two strokes on top of each
    // other, or drops one, and then sounds like it has lost the beat.
    //
    // Running fractionally fast or slow for a moment closes the same error and
    // the grid stays monotonic, so no stroke is ever played twice or skipped.
    // It is what a player does: nobody moves their hand, they lean until they
    // are back with the band.
    const float nominalBeats = tempo / 60.0f
                               * static_cast<float> (numSamples)
                               / static_cast<float> (sampleRate);
    float steer = 0.0f;
    if (locked)
    {
        // Seconds to close a standing phase error, and the most the rate may be
        // bent to do it. The pull has to be gentler when the listener has asked
        // the clock to hold its ground.
        float tau = 0.90f;
        float steerLim = 0.035f;
        float steerCeil = 0.18f;
        float dGain = 0.8f;
        switch (follow)
        {
            case FollowStrength::low:
                tau = 1.60f;
                steerLim = 0.018f;
                steerCeil = 0.10f;
                dGain = 0.3f;
                break;
            case FollowStrength::high:
                tau = 0.70f;
                steerLim = 0.050f;
                steerCeil = 0.25f;
                dGain = 1.2f;
                break;
            case FollowStrength::medium:
                break;
        }
        // Rate needed to close `phaseErrEma` beats in `tau` seconds, as a
        // fraction of the tempo. Derived rather than tuned per tempo: the same
        // phase error is a longer time at a slower tempo, so a fixed gain would
        // pull twice as hard at 60 BPM as at 120.
        //
        // `tau` used to be 0.22 s at HIGH, which is a gain of 2.3 at 120 BPM:
        // the limit below was reached by an error of 0.022 of a beat, and the
        // analysis's own phase is not that certain. Measured against a decoder
        // whose phase wobbles by 0.03 of a beat, the grid sat at the limit in
        // both directions permanently - +/-6 BPM at 120, 3.7 BPM rms - which is
        // the percussion audibly running away and catching up on a band that
        // never moved. Closing an error over about half a second instead leaves
        // the limit for errors that are really there.
        const float kp = 60.0f / std::max (40.0f, tempo) / std::max (0.05f, tau);

        // Below the uncertainty of the thing being measured there is nothing to
        // correct, and a loop that keeps pulling on it is only playing back the
        // analysis's noise as a tempo. Subtracted rather than gated, so a real
        // error still crosses it smoothly instead of switching the loop on.
        constexpr float kPhaseFloor = 0.012f;
        const float e = phaseErrEma > kPhaseFloor ? phaseErrEma - kPhaseFloor
                      : (phaseErrEma < -kPhaseFloor ? phaseErrEma + kPhaseFloor : 0.0f);
        // The derivative must see the same dead-banded error as the
        // proportional term. Differentiating phaseErrEma directly bypassed the
        // floor above and put every decoder wobble straight back into the clock
        // as a short tempo change.
        const float dErr = wrapCentered (e - prevPhaseErr);
        prevPhaseErr = e;

        // The limit exists to stop the loop living at its rail on the analysis's
        // own phase noise, and that noise is hundredths of a beat. A quarter of
        // a beat is not noise - it is a different grid, a new song, a re-lock -
        // and holding one ceiling for both is what made every one of those take
        // seconds: at 3.5% of the tempo an error simply costs `error / 0.035`
        // beats to close, measured at 2.8 s for a quarter beat and 4.9 s for
        // half of one, whatever else was true.
        //
        // So the ceiling opens with the error, above the point where noise could
        // have produced it, and closes again as the error does. Bending the rate
        // by a quarter for about a beat is what a player does when they find
        // themselves off the beat; sitting a quarter beat out for five seconds
        // is not. And it stays a rate: `1 - steer` never approaches zero, so the
        // grid is still monotonic and no stroke can be doubled or dropped.
        constexpr float kOpenAbove = 0.06f;
        constexpr float kOpenAt = 0.30f;
        const float open = std::max (0.0f, std::fabs (e) - kOpenAbove)
                           * (steerCeil - steerLim) / (kOpenAt - kOpenAbove);
        const float lim = std::min (steerCeil, steerLim + open);

        steer = std::clamp (e * kp + dErr * dGain, -lim, lim);
    }
    else
    {
        prevPhaseErr = phaseErrEma;
        steer = std::clamp (phaseErrEma * 0.08f, -0.030f, 0.030f);
    }

    if (rapidTransition && haveRawGridPhaseError && numSamples > 0
        && std::isfinite (rawGridPhaseError) && std::isfinite (tempo))
    {
        // Spend only the error outside the accepted 25 ms band, evenly over
        // the transition time still available. This replaces the ordinary
        // filtered command only for the decoder-confirmed one-beat window.
        //
        // A segment never straddles the transition boundary: `advance` splits
        // such callbacks before entering here.
        constexpr float kRapidToleranceSeconds = 0.025f;
        // Leave a sub-millisecond numerical margin inside the public 25 ms
        // acceptance band. The decoder phase and the audio clock are float
        // grids sampled at different rates; targeting the inclusive boundary
        // itself measured up to 0.16 ms outside it after conversion.
        constexpr float kRapidToleranceGuardSeconds = 0.00025f;
        constexpr float kRapidSteerRail = 0.25f;
        // The observation and command both arrive at callback boundaries. When
        // another rapid callback remains, reserve this callback from the
        // denominator so the accepted band is reached by the next boundary,
        // rather than one callback after the musical deadline. A callback that
        // reaches or crosses expiry uses the exact remaining span: reserving it
        // would create a zero denominator and an unnecessary rail command.
        const int commandSamplesRemaining =
            transitionSamplesRemaining > numSamples
                ? transitionSamplesRemaining - numSamples
                : transitionSamplesRemaining;
        const float remainingSeconds =
            static_cast<float> (commandSamplesRemaining)
            / static_cast<float> (sampleRate);
        const float toleranceBeats =
            (kRapidToleranceSeconds - kRapidToleranceGuardSeconds) * tempo / 60.0f;
        const float remainingBeats = tempo * remainingSeconds / 60.0f;
        if (std::isfinite (remainingBeats) && remainingBeats > 1.0e-9f
            && std::isfinite (toleranceBeats))
        {
            const float excess =
                std::max (0.0f, std::fabs (rawGridPhaseError) - toleranceBeats);
            const float needed = std::copysign (excess / remainingBeats,
                                                rawGridPhaseError);
            if (std::isfinite (needed))
                steer = std::clamp (needed, -kRapidSteerRail, kRapidSteerRail);
        }
    }

    // Phase this block will *not* advance because of the steer. The trim
    // controller subtracts it from the drift it measures, so that the loop's own
    // correction is not read back as the song having moved; and the error
    // estimate is worked down by it, so the loop stops pulling once it has
    // leaned far enough rather than overshooting on an estimate that is only
    // refreshed once a beat.
    const float applied = steer * nominalBeats;
    phaseCorrectionSinceObservation += applied;
    phaseErrEma -= applied;

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
    tick.reanchored = reanchor;
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
    transitionSamplesRemaining =
        std::max (0, transitionSamplesRemaining - std::max (0, numSamples));
    return tick;
}

} // namespace vp
