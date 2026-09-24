#include "Tracking/TempoFollower.h"

#include <algorithm>
#include <cmath>

namespace vp
{

namespace
{
    /** How little the clock may be asked to believe the tempo it is handed. */
    constexpr float kMinTempoTrust = 0.30f;
    constexpr float kRecoveryToleranceSeconds = 0.0075f;
    // After a sounding rest the direct-live rail is still 7.5%. A phase
    // debt of about 0.20 beats saturates it, and the displayed tempo jumps
    // by about 8 BPM for the beats the drums come back on. 3.5% for eight
    // beats closed that debt, and on a song whose own tempo only wanders
    // a couple of BPM (I WANNA DANCE, 12 s windows 122–128) the clock
    // then sat 4.5 BPM off the counted tempo for those beats: 2714
    // blocks past 4 BPM, clock 117.7–134.0 against a published 120.7–130.
    // 1.5% for fourteen beats still closes the 0.20-beat debt
    // (14 * 0.015 = 0.21) before the live rail returns, and the sounding
    // tempo stays inside the song's own wander. A confirmed transition
    // still uses its own window.
    constexpr float kPostGapSteer = 0.015f;
    constexpr int kPostGapBeats = 14;

    // The loop's own noise floor, and why it is capped in *time*.
    //
    // It was a flat 0.012 of a beat everywhere, and a fraction of a beat is a
    // different number of milliseconds at every tempo: 4.3 ms at 168 BPM, 6.0
    // at 120, 9.6 at 75 and **13.8 at 52**. The published target for where the
    // part lands is 8 ms, so below about 90 BPM the floor was wider than the
    // thing it exists to achieve - and it is *subtracted* from the error rather
    // than compared against it, so the correction fades to nothing as the error
    // approaches it. The residue does not close: it converges to the floor and
    // stays there. Reported by the listener in exactly those terms - "sono
    // sempre in ritardo (o sempre in anticipo) e ci impiegano molti secondi a
    // rientrare" - and `probe_recovery --slow-passages` says the same in
    // numbers: all 18 cases at 52 BPM read `confirm=-1.000 stable=-1.000`,
    // never confirmed and never inside 8 ms.
    //
    // What the floor protects against is the *analysis's* phase noise, and that
    // noise is a time, not a fraction of a beat: it comes off a 20 ms frame
    // grid, which is 20 ms at every tempo. So the floor is capped at a time.
    //
    // 6 ms is 0.012 of a beat at exactly 120 BPM, which is where the original
    // constant was tuned, so **at and above 120 BPM this changes nothing at
    // all** - the beat-relative value is already the smaller of the two. It
    // only stops the floor from widening below that.
    constexpr float kPhaseFloorBeats = 0.012f;
    constexpr float kPhaseFloorSeconds = 0.006f;

    /** The phase deadband for this tempo, in beats. */
    inline float phaseFloorFor (float periodSec) noexcept
    {
        return std::min (kPhaseFloorBeats,
                         kPhaseFloorSeconds / std::max (0.05f, periodSec));
    }

    // A light direct-live offset, about 25 ms at 120 BPM. Above this the
    // ordinary 0.30 s average and the 2%/beat slew toward the 7.5% rail stay
    // as they were: a fill must not spend that rail inside one beat.
    constexpr float kDirectLightPhaseBeats = 0.050f;
    constexpr float kDirectLightPhaseTau = 0.15f;
    constexpr float kDirectLightSteer = 0.040f;
    constexpr float kDirectLightSlewPerBeat = 0.080f;
    constexpr float kDirectRailSlewPerBeat = 0.020f;

    /** Two-slope slew. Inside ±kDirectLightSteer the command may arrive in
        half a beat; outside it, the old 2% per beat still applies. A block
        that crosses the boundary spends each slope on its own segment. */
    inline float slewDirectLive (float from, float to, float beats) noexcept
    {
        if (! (beats > 0.0f) || from == to)
            return to;
        const float dir = to > from ? 1.0f : -1.0f;
        float pos = from;
        float left = beats;
        for (int segment = 0; segment < 2 && left > 0.0f && (to - pos) * dir > 0.0f; ++segment)
        {
            const float edge = dir > 0.0f ? kDirectLightSteer : -kDirectLightSteer;
            const bool bothInside = std::fabs (pos) <= kDirectLightSteer
                                    && std::fabs (to) <= kDirectLightSteer;
            const bool leaving = std::fabs (pos) < kDirectLightSteer
                                 && std::fabs (to) > kDirectLightSteer;
            const bool entering = std::fabs (pos) > kDirectLightSteer
                                  && (to - pos) * pos < 0.0f;
            const float rate = (bothInside || leaving) ? kDirectLightSlewPerBeat
                                                       : kDirectRailSlewPerBeat;
            const float limit = bothInside ? to
                              : leaving ? edge
                              : entering ? std::copysign (kDirectLightSteer, pos)
                                         : to;
            const float room = rate * left;
            const float step = limit - pos;
            if (std::fabs (step) <= room)
            {
                left -= std::fabs (step) / rate;
                pos = limit;
            }
            else
            {
                pos += std::copysign (room, dir);
                left = 0.0f;
            }
        }
        return pos;
    }
    constexpr float kRecoverySteerRail = 0.20f;

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
    cancelPhaseRecovery();
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
    directTempoDirectionGuard = false;
    directLivePhaseFollow = false;
    directLiveSteer = 0.0f;
    tempoGlideFast = false;
    smallFlexSamples = 0;
    smallFlexSign = 0;
    beatGapHold = false;
    gapSteerGuardBeats = 0;
    tempoMotionHint = false;
    tempoMotionProven = false;
    tempoTrust = 1.0f;
    poorTrustSamples = 0;
    phaseRecoverySamplesRemaining = 0;
    recoveryEvents = 0;
    transitionSamplesRemaining = 0;
}

void TempoFollower::resetClock() noexcept
{
    cancelPhaseRecovery();
    directLivePhaseFollow = false;
    directLiveSteer = 0.0f;
    tempoGlideFast = false;
    smallFlexSamples = 0;
    smallFlexSign = 0;
    beatGapHold = false;
    gapSteerGuardBeats = 0;
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
    poorTrustSamples = 0;
    phaseRecoverySamplesRemaining = 0;
    transitionSamplesRemaining = 0;
    reanchor = true;
}

void TempoFollower::setTempoTrust (float trust) noexcept
{
    const float next = std::isfinite (trust) ? std::clamp (trust, kMinTempoTrust, 1.0f) : kMinTempoTrust;

    // A fill, a newly enabled percussion voice or a level change can make the
    // fitted beats temporarily poor. While that is true the lean cap is
    // deliberate, but once clean beats return it used to leave the residual
    // offset to the ordinary target filter. The analysis had already answered
    // "the drummer is back" and the clock still behaved as if it had not. Arm
    // a bounded dropout/re-entry catch-up only after at least 200 ms of
    // genuinely poor evidence; one bad six-Hz hypothesis is shorter and cannot
    // trigger it. Normal direct-live movement uses the continuous servo below.
    constexpr float kRecoveredAbove = 0.80f;
    const int poorLongEnough = static_cast<int> (sampleRate * 0.20);
    if (next >= kRecoveredAbove && poorTrustSamples >= poorLongEnough
        && phaseRecoverySamplesRemaining <= 0)
    {
        recoveryArmed = true;
        poorTrustSamples = 0;
    }
    tempoTrust = next;
    if (next < kRecoveredAbove)
    {
        // A recovery is useful only after clean evidence has returned. If the
        // fit becomes unreliable again, stop its temporary steering and let
        // the continuous direct-live servo remain inside its normal rail.
        if (! phaseRecoveryTrustOverride)
            phaseRecoverySamplesRemaining = 0;
        recoveryCandidate = false;
    }
}

void TempoFollower::cancelPhaseRecovery() noexcept
{
    poorTrustSamples = 0;
    phaseRecoverySamplesRemaining = 0;
    phaseRecoveryTrustOverride = false;
    recoveryArmed = recoveryCandidate = recoverySerialSeen = false;
    recoveryError = recoveryCorrection = 0.0f;
    recoveryAgeSamples = 0;
    recoveryCooldownSamples = 0;
    recoveryEvents = 0;
}

void TempoFollower::observeRecoveryBeat (float errorBeats, uint32_t serial,
                                         bool allowMissedBeats,
                                         bool allowUntrustedDirectMotion,
                                         bool allowOneShotRecovery) noexcept
{
    if (recoverySerialSeen && serial == recoverySerial)
        return;
    recoverySerialSeen = true;
    recoverySerial = serial;
    // One-shot recovery requires clean evidence. A constant-tempo fit may lose
    // trust while a real band curves its tempo, but direct-live curvature is
    // now handled continuously and must not borrow this recovery path.
    const float recoveryTrust = tempoTrust;
    if (! locked || recoveryTrust < 0.80f || tempoTransitionActive()
        || ! std::isfinite (errorBeats))
    {
        recoveryCandidate = false;
        return;
    }
    const float error = wrapCentered (errorBeats);
    const float expected = wrapCentered (recoveryError - recoveryCorrection);
    const float period = 60.0f / std::max (40.0f, tempo);
    // The decoder also accepts subdivision peaks. A fresh serial inside the
    // minimum independent-beat window cannot confirm recovery, but must not
    // replace its first observation either: eighths otherwise reset this age
    // every half beat and prevent confirmation forever. Keep the accumulated
    // steering so the next eligible beat is compared on the same reference.
    // On a direct path the decoder's serial already distinguishes accepted
    // observations and propagation is stable. After a real low-evidence gap,
    // two coherent eighths may therefore close the mandatory two-observation
    // proof; every other path retains the quarter-beat independence window.
    const float minimumIndependentBeats = allowUntrustedDirectMotion
                                              ? 0.45f : 0.55f;
    if (recoveryCandidate
        && recoveryAgeSamples <= sampleRate * period * minimumIndependentBeats)
        return;
    // A mixed line feed does not reliably give BeatNet every quarter. The old
    // 1.8-beat ceiling discarded the first of two perfectly coherent phase
    // observations whenever one quarter was missed, so the fast recovery could
    // remain unarmed for an entire sparse passage. On a direct path, propagation
    // is stable enough for persistence across a bar to be stronger evidence,
    // not weaker; a room retains the short window because reflections can move
    // the apparent onset between hits. Two fresh serials and phase agreement are
    // still mandatory, and no single onset can move the clock.
    const float maximumIndependentBeats = allowMissedBeats ? 4.5f : 1.8f;
    const bool agrees = recoveryCandidate
        && recoveryAgeSamples > sampleRate * period * minimumIndependentBeats
        && recoveryAgeSamples < sampleRate * period * maximumIndependentBeats
        && error * expected > 0.0f && std::fabs (error - expected) < 0.025f;
    // Two persistent errors beyond both the phase-noise floor and 20 ms.
    // The expected error subtracts our own steering: correcting the clock
    // must not make two observations of the same displacement disagree.
    // Deliberately NOT given the same tempo-aware treatment as `phaseFloorFor`,
    // and that was measured. Scaling `0.04f` by the tempo-aware floor - which
    // is arithmetically the same number at 120 BPM and above - took
    // `probe_recovery`'s default gate from 0 FAIL to **5**, and the slow
    // extension from 18 to 23. The two floors are not the same knob: this one
    // arms a fast correction and a lower bar arms it on evidence that has not
    // settled. Leave it alone.
    const float persistentFloor = std::max (0.04f, 0.020f / period);
    const bool persistent = std::fabs (error) > persistentFloor
        && std::fabs (expected) > persistentFloor
        && std::fabs (error - expected) < 0.015f;
    // Normal direct-live motion is handled continuously by the phase servo.
    // Letting the same phase debt also arm this bounded one-shot produced the
    // audible chase measured in the iPad trace: 106.55 -> 103.32 -> 106.34 BPM
    // in 0.21 s, and elsewhere 105 -> 109.99. In direct-live mode the one-shot
    // is now reserved for its original job: recovery after evidence was poor
    // long enough to arm `recoveryArmed`. Other paths retain persistent phase
    // recovery exactly as before.
    const bool mayStartRecovery = allowOneShotRecovery
                                  && (recoveryArmed
                                      || (! allowUntrustedDirectMotion
                                          && persistent));
    if (mayStartRecovery && agrees && recoveryCooldownSamples <= 0
        && std::fabs (error) > phaseFloorFor (period)
        && std::fabs (error) < 0.25f)
    {
        // Once two independent beats agree after a genuine dropout/re-entry,
        // spend the confirmed offset without another proof delay. The direct
        // path may close over half a beat; other paths retain the quarter-beat
        // minimum. The 20% rail keeps the grid monotonic, so this can shorten
        // intervals sharply without duplicating or skipping a pulse.
        const float excess = std::max (0.0f, std::fabs (error) - kRecoveryToleranceSeconds / period);
        const float minimumCorrectionBeats = allowUntrustedDirectMotion ? 0.5f
                                                                         : 0.25f;
        const float correctionBeats = std::max (minimumCorrectionBeats,
                                                excess / kRecoverySteerRail);
        phaseRecoverySamplesRemaining = std::max (1, static_cast<int> (std::ceil (sampleRate * period * correctionBeats)));
        phaseRecoveryTrustOverride = false;
        ++recoveryEvents;
        recoveryArmed = false;
        recoveryCooldownSamples = static_cast<int> (sampleRate * period * 2.5);
    }
    recoveryCandidate = true;
    recoveryError = error;
    recoveryCorrection = 0.0f;
    recoveryAgeSamples = 0;
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
        // Phase-derived trim is deliberately persistent, but that persistence
        // belongs only to the motion that created it. At an inflection the
        // fresh decoder target can already ask the clock to slow while a trim
        // accumulated through the preceding accelerando still asks it to speed
        // up (and vice versa). Continuing in the old direction is never a
        // useful correction on a direct feed, so discard only the integrator;
        // the clock, grid and phase remain continuous.
        // Key off the decoder turning around, not clock-versus-decoder.
        // While FISSO holds the published BPM the trim is the only term
        // that can follow the band. Comparing `bpm` to the sounding clock
        // makes that disagreement look like an inflection and wipes the
        // integrator: VPAlign MIXER then collapsed onto LEANA
        // (50.2/146.1 vs 40.3/127.2 on the 12 s ramp, seed-matched).
        // A real inflection is a new target whose sign disagrees with
        // the trim. Steps of more than 3 BPM still clear below.
        const float decoderDelta = bpm - target;
        if (directTempoDirectionGuard
            && decoderDelta * tempoTrim < 0.0f
            && std::fabs (decoderDelta) > 1.00f)
        {
            tempoTrim = 0.0f;
            lastDrift = 0.0f;
            driftSameWay = 0;
        }

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
            {
                cancelPhaseRecovery();
                tempo = bpm;
            }
        }
        target = bpm;
    }
}

void TempoFollower::beginTempoTransition (float bpm) noexcept
{
    if (! std::isfinite (bpm) || bpm <= 40.0f || bpm >= 220.0f)
        return;
    cancelPhaseRecovery();

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
    cancelPhaseRecovery();
    tempo = bpm;
    target = bpm;
    tempoTrim = 0.0f;
    lastDrift = 0.0f;
    driftSameWay = 0;
    phaseCorrectionSinceObservation = 0.0f;
    samplesSinceObservation = 0;
    havePhaseObservation = false;
    poorTrustSamples = 0;
    phaseRecoverySamplesRemaining = 0;
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
    cancelPhaseRecovery();
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
            // A held decoder can otherwise spend most of a short ramp proving
            // that it really moved while this independent phase slope already
            // says the clock is late. Only the tightly fitted direct-feed hint
            // selects the larger gain. Four-seed VPAlign: the 100 -> 110 / 12 s
            // worst phase falls 153.4 -> 127.2 ms; fixed 100/130 controls remain
            // 33.3/22.0 ms. BeatTracker owns the TAP/manual/room guards.
            const float rateGain = tempoMotionHint
                                       ? std::clamp (strength * 0.20f, 0.20f, 0.40f)
                                       : std::clamp (strength * 0.08f, 0.10f, 0.28f);
            // Full residual-shape authority already contains two consecutive
            // multi-beat quadratic decisions and an explicit rejection of a
            // hinge/step. Requiring three additional phase-drift signs here
            // repeats that proof after the decoder has finished it. One fresh
            // phase interval is still mandatory; only its agreement multiplier
            // is made whole. Provisional motion and every ordinary/fixed path
            // retain the three-observation filter.
            const float agreement = tempoMotionProven
                                        ? 1.0f
                                        : static_cast<float> (driftSameWay)
                                              / static_cast<float> (kDriftAgreeing);
            const float controlTrust = tempoMotionProven ? 1.0f : tempoTrust;
            const float trust = rateGain * rateTrustScale (controlTrust) * agreement;
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
        tick.soundingTempoBpm = tempo;
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
        merged.soundingTempoBpm = ordinary.soundingTempoBpm;

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
    // The ordinary trust score comes from a constant-tempo fit. A genuinely
    // curved beat trajectory can lower that score precisely because the band
    // is moving. Once the independent residual-shape path has proved that
    // motion twice, using the linear-fit penalty here would reintroduce up to
    // 2.5 seconds of lag after recognition. This override is local to the
    // follower controls; it neither changes the decoded target nor survives
    // loss of full shape authority.
    const float controlTrust = tempoMotionProven ? 1.0f : tempoTrust;
    constexpr float kFarTarget = 0.06f;
    // A live line feed normally gets the responsive phase loop even when its
    // fit is temporarily rough. A *large* displacement on a rough
    // fit is different: through a fill the accepted tom/snare crests can walk
    // away from the counted quarter, and giving that observation full phase
    // authority makes the clock accelerate to catch a beat the band never
    // moved. A proved tempo curve or an explicit transition/recovery keeps its
    // own authority. Small corrections retain the direct-live response.
    const bool dubiousDirectPhase = directLivePhaseFollow && ! tempoMotionProven
                                    && tempoTrust < kTrustToSetRate
                                    && transitionSamplesRemaining <= 0
                                    && phaseRecoverySamplesRemaining <= 0
                                    && havePhaseTarget
                                    && std::fabs (phaseTarget) > kFarTarget;
    const float phaseControlTrust = (tempoMotionProven
                                     || (directLivePhaseFollow && ! dubiousDirectPhase))
                                        ? 1.0f : tempoTrust;
    recoveryCooldownSamples = std::max (0, recoveryCooldownSamples - numSamples);
    recoveryAgeSamples = std::min (recoveryAgeSamples + numSamples, static_cast<int> (sampleRate * 30.0));
    ClockTick tick;
    const bool rapidTransition = transitionSamplesRemaining > 0;

    samplesSinceObservation = std::min (samplesSinceObservation + numSamples,
                                        static_cast<int> (sampleRate * 30.0));
    samplesSincePulse = std::min (samplesSincePulse + numSamples,
                                  static_cast<int> (sampleRate * 30.0));
    if (controlTrust < 0.55f)
        poorTrustSamples = std::min (poorTrustSamples + numSamples,
                                     static_cast<int> (sampleRate * 30.0));
    else if (controlTrust >= 0.80f && phaseRecoverySamplesRemaining <= 0)
        poorTrustSamples = 0;
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
    // quickly. Once the part is sounding, a wobble under 2 BPM is averaged over
    // about a second and a half. The old 0.22 s constant adopted that wobble
    // faster than a real move, which is the clock accelerating and braking
    // for seconds on an analysis that has not gone anywhere. A move past 2 BPM
    // keeps 0.28 s until it is within half a BPM, so the tail of a real step
    // is not reclassified as wobble and still lands inside a second. A
    // proved curve and a confirmed transition do too. This is not a freeze:
    // the slow path still adopts, and a real accelerando grows past 2 BPM.
    const float absErr = std::fabs (err);
    if (absErr > 2.0f)
        tempoGlideFast = true;
    else if (absErr < 0.5f)
        tempoGlideFast = false;
    float glide = ! locked ? (absErr <= 1.2f ? 0.045f : 0.18f)
                           : ((rapidTransition || tempoMotionProven || tempoGlideFast)
                                  ? 0.28f : 1.60f);
    // A bend the band holds, still under 2 BPM, used to sit on the 1.60 s
    // wobble average for its whole life: that is the lazy re-lock. Direct
    // live only, and only after the error has kept its sign for 0.40 s, so a
    // wobble that reverses never leaves 1.60. The known-phase matrix does
    // not set this follow.
    if (locked && directLivePhaseFollow && ! tempoGlideFast && ! rapidTransition
        && absErr >= 0.5f && absErr <= 2.0f)
    {
        const int sign = err > 0.0f ? 1 : -1;
        if (sign == smallFlexSign)
            smallFlexSamples = std::min (smallFlexSamples + numSamples,
                                         static_cast<int> (sampleRate * 2.0));
        else
        {
            smallFlexSign = sign;
            smallFlexSamples = numSamples;
        }
        if (smallFlexSamples > static_cast<int> (sampleRate * 0.40f))
            glide = 0.45f;
    }
    else
    {
        smallFlexSamples = 0;
        smallFlexSign = 0;
    }

    // Floored while the beats the tempo was fitted through are worse placed
    // than this song's own - which is what a passage with the drummer out looks
    // like from inside the fit, and is measured in Tracking/PhaseTrust.h. At
    // full trust, which is everything else including an accelerando, `poor` is
    // zero and this is the same number it has always been.
    const float poor = (1.0f - std::clamp (controlTrust, kMinTempoTrust, 1.0f))
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
        // A light offset on a stable direct feed was still averaged for 0.30 s
        // and then slewed at 2% per beat, so 20 ms at 120 took 0.70 s to get
        // inside 8 ms. Two publications (0.15 s) are enough when the raw error
        // is itself inside a twentieth of a beat; a larger debt keeps the tau
        // the caller asked for.
        if (directLivePhaseFollow && ! beatGapHold
            && std::fabs (phaseTarget) <= kDirectLightPhaseBeats
            && phaseTargetTau > kDirectLightPhaseTau)
            tau = kDirectLightPhaseTau;
        // Shorten a slow average when the same phase error has held for a
        // quarter of a second. The floor is 0.10 s. An ioi-lead follow is
        // already 0.01 s (`kGridTauIoiLead`), and `std::clamp` aborts on the
        // audio thread when the upper bound is below the lower one — that is
        // the EVERYTIME SIGABRT after the clock had already stepped. A tau
        // that is already at or under the floor has nothing to shorten.
        if (farTargetSamples > static_cast<int> (sampleRate * 0.25)
            && phaseTargetTau > 0.10f)
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
        const float poorLean = (1.0f - std::clamp (phaseControlTrust, kMinTempoTrust, 1.0f))
                               / (1.0f - kMinTempoTrust);
        if (poorLean > 0.0f
            && (std::fabs (phaseTarget) < kLeanIsElsewhere || dubiousDirectPhase))
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
        // A stable direct feed in VIVO follows accepted beats as they arrive.
        // The old path first averaged them for up to 2.2 s, then spent the
        // accumulated debt at a 20% one-shot rail. A percussionist does the
        // opposite: small continuous leans prevent a conspicuous catch-up.
        // Keep this proportional and tightly bounded; removing the derivative
        // prevents a fresh 6 Hz publication becoming a short tempo spike.
        if (directLivePhaseFollow && ! rapidTransition)
        {
            switch (follow)
            {
                case FollowStrength::low:    tau = 0.65f; steerLim = 0.035f; break;
                case FollowStrength::medium: tau = 0.45f; steerLim = 0.055f; break;
                case FollowStrength::high:   tau = 0.35f; steerLim = 0.075f; break;
            }
            steerCeil = steerLim;
            dGain = 0.0f;
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
        const float kPhaseFloor = phaseFloorFor (60.0f / std::max (40.0f, tempo));
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

    // A confirmed tempo transition also bypasses the ordinary phase filter.
    // Carry its memory along, just as for phase recovery below: otherwise the
    // old error resumes steering after the rapid window has already paid it.
    if (rapidTransition && haveRawGridPhaseError && std::isfinite (rawGridPhaseError))
    {
        phaseErrEma = rawGridPhaseError;
        prevPhaseErr = std::copysign (
            std::max (0.0f, std::fabs (rawGridPhaseError)
                                - phaseFloorFor (60.0f / std::max (40.0f, tempo))),
            rawGridPhaseError);
    }

    if (! rapidTransition && phaseRecoverySamplesRemaining > 0
        && haveRawGridPhaseError && numSamples > 0
        && std::isfinite (rawGridPhaseError) && std::isfinite (tempo))
    {
        // Clean beats have returned after a passage that the evidence itself
        // marked unreliable, or a non-direct path confirmed displacement.
        // Land within 8 ms over the bounded recovery window, updating the
        // command from every projected phase. This is a player's fast rientro:
        // temporarily lengthen or shorten the next interval, never restart the
        // clock or replay/skip a grid position.
        // Aim half a millisecond inside the public 8 ms line. The song and
        // clock phases are float grids sampled at different instants; aiming
        // at the inclusive boundary otherwise lands a few ulps outside it.
        const float toleranceBeats = kRecoveryToleranceSeconds * tempo / 60.0f;
        const int commandSamples = std::max (numSamples,
                                             phaseRecoverySamplesRemaining);
        const float remainingBeats = tempo * static_cast<float> (commandSamples)
                                     / (60.0f * static_cast<float> (sampleRate));
        const float excess = std::max (0.0f, std::fabs (rawGridPhaseError)
                                               - toleranceBeats);
        if (remainingBeats > 1.0e-9f)
        {
            const float needed = std::copysign (excess / remainingBeats,
                                                rawGridPhaseError);
            if (std::isfinite (needed))
                steer = std::clamp (needed, -kRecoverySteerRail,
                                    kRecoverySteerRail);
        }
        if (excess <= 0.0f)
            phaseRecoverySamplesRemaining = 0;
        // The fast command follows the confirmed raw error. Bring the ordinary
        // controller along with it: retaining its pre-return average would
        // steer away again as soon as the fast window ends.
        phaseErrEma = rawGridPhaseError;
        prevPhaseErr = std::copysign (
            std::max (0.0f, std::fabs (rawGridPhaseError)
                                - phaseFloorFor (60.0f / std::max (40.0f, tempo))),
            rawGridPhaseError);
    }

    // A rest while the part is already sounding. The direct-live rail is
    // 7.5% at high, which is about 8 BPM near 107: that is the surge
    // through a pause whose pulse has not changed. Hold the counted
    // rate through the rest, then 1.5% for fourteen beats. 3.5% for
    // eight beats closed the same debt and left the clock 4.5 BPM off
    // a song that only wanders a couple of BPM. A confirmed transition
    // still spends its own window.
    if (beatGapHold)
        gapSteerGuardBeats = kPostGapBeats;
    if (beatGapHold && ! rapidTransition)
    {
        steer = 0.0f;
        directLiveSteer = 0.0f;
    }
    else if (gapSteerGuardBeats > 0 && ! rapidTransition)
        steer = std::clamp (steer, -kPostGapSteer, kPostGapSteer);

    // A saturated direct-live rail is 7.5% at high, which at 123 BPM is
    // the whole of a 123→132 reading in one buffer. The phase still has
    // to close, but a percussionist leans over a beat, not inside one
    // callback. The way up to that rail stays at 2% per beat. A light
    // lean (inside 4%) may arrive in half a beat: that is the command a
    // 20 ms offset actually asks for, and holding it to the rail's slew
    // left the rientro waiting on a slope it was never going to climb.
    // A confirmed rapid window keeps its own rail. A rest zeroes the
    // lean in the same buffer: slewing that zero brought the pause surge
    // back. The matrix lane does not set this follow, so the known-phase
    // hashes do not see it.
    if (directLivePhaseFollow && ! rapidTransition && ! beatGapHold
        && tempo > 40.0f && numSamples > 0)
    {
        const float beatsInBlock = tempo / 60.0f
                                 * static_cast<float> (numSamples)
                                 / static_cast<float> (sampleRate);
        // The 7.5% rail still cannot appear inside one beat. Only the light
        // band (±4%, the lean a ~20 ms offset at 120 actually asks for) may
        // arrive in half a beat.
        steer = slewDirectLive (directLiveSteer, steer, beatsInBlock);
    }
    directLiveSteer = steer;

    const float applied = steer * nominalBeats;
    recoveryCorrection += applied;
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
        if (! beatGapHold && gapSteerGuardBeats > 0)
            gapSteerGuardBeats = std::max (0, gapSteerGuardBeats - crossed);
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
    tick.soundingTempoBpm = effTempo;
    transitionSamplesRemaining =
        std::max (0, transitionSamplesRemaining - std::max (0, numSamples));
    phaseRecoverySamplesRemaining =
        std::max (0, phaseRecoverySamplesRemaining - std::max (0, numSamples));
    if (phaseRecoverySamplesRemaining == 0)
        phaseRecoveryTrustOverride = false;
    return tick;
}

} // namespace vp
