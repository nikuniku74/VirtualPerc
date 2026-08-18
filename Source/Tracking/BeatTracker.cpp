#include "Tracking/BeatTracker.h"

#include <algorithm>
#include <cmath>

namespace vp
{

void BeatTracker::prepare (double sr) noexcept
{
    sampleRate = sr > 1.0 ? sr : 48000.0;
    follower.prepare (sampleRate);
    neural.start (sampleRate);
    reset();
}

void BeatTracker::reset() noexcept
{
    follower.reset();
    currentState = TrackingState::listening;
    effectiveSubdivision = Subdivision::sixteenth;
    lastBeatSerial = 0;
    lastDownbeatSerial = 0;
    seenSerials = false;
    lastLeadMs = 0.0f;
    samplesSinceBeat = 0;
    lockHoldSamples = 0;
    lowHoldSamples = 0;
    listeningSamples = 0;
    beatCount = 0;
    smoothedConf = 0.0f;
    heldBpm = 0.0f;
    inputPeakEnv = 0.0f;
    quietSamples = 0;
    gridMuteSamples = 0;
    ghostLockSamples = 0;
    lockedOnce = false;
    retuning = false;
    tapHold = false;
    tapAligned = false;
    tapEstablished = false;
    waitForQuantize = false;
    heardMusic = false;
    hadPlayed = false;
    needsResync = false;
    waitForSongBeat = false;
    armed = false;
    tapHoldSamples = 0;
    lostSyncSamples = 0;
    downbeatHoldSamples = 0;
    quantizeWaitSamples = 0;
    lastTapSec = -1.0;
    tapIoiWrite = 0;
    tapIoiFilled = 0;
    std::fill (tapIoi, tapIoi + 8, 0.0f);
}

void BeatTracker::setFollowStrength (FollowStrength s) noexcept
{
    follower.setFollowStrength (s);
}

void BeatTracker::setSubdivisionOverride (Subdivision s) noexcept
{
    userSubdivision = s;
}

void BeatTracker::start() noexcept
{
    armed = true;
    waitForQuantize = true;
    quantizeWaitSamples = 0;
    if (currentState == TrackingState::idle || currentState == TrackingState::stopped)
        currentState = TrackingState::listening;
    if (heldBpm > 50.0f)
    {
        currentState = TrackingState::following;
        follower.setTargetTempo (heldBpm, 0.90f);
        if (needsResync)
            waitForSongBeat = true;
    }
}

void BeatTracker::stop() noexcept
{
    armed = false;
    waitForQuantize = false;
    tapHold = false;
    tapAligned = false;
    tapEstablished = false;
    retuning = false;
    if (hadPlayed)
        needsResync = true;
    hadPlayed = false;
    waitForSongBeat = false;
}

void BeatTracker::tap (double timeSeconds) noexcept
{
    const double dt = lastTapSec >= 0.0 ? timeSeconds - lastTapSec : 1.0e9;
    if (lastTapSec >= 0.0 && dt < 0.240)
    {
        lastTapSec = timeSeconds;
        return;
    }

    if (lastTapSec >= 0.0 && dt > 2.0)
    {
        tapIoiWrite = 0;
        tapIoiFilled = 0;
    }

    if (dt >= 0.090 && dt <= 2.00)
    {
        tapIoi[tapIoiWrite] = static_cast<float> (dt);
        tapIoiWrite = (tapIoiWrite + 1) % 8;
        tapIoiFilled = std::min (8, tapIoiFilled + 1);
    }

    lastTapSec = timeSeconds;

    if (tapIoiFilled < 3)
        return;

    float tapped = 0.0f;
    {
        float sorted[8];
        std::copy (tapIoi, tapIoi + tapIoiFilled, sorted);
        const int mid = tapIoiFilled / 2;
        std::nth_element (sorted, sorted + mid, sorted + tapIoiFilled);
        const float median = sorted[mid];
        float sum = 0.0f;
        int nKeep = 0;
        for (int i = 0; i < tapIoiFilled; ++i)
        {
            if (std::fabs (sorted[i] - median) <= median * 0.18f)
            {
                sum += sorted[i];
                ++nKeep;
            }
        }
        const float ioi = nKeep > 0 ? sum / static_cast<float> (nKeep) : median;
        if (ioi > 1.0e-4f)
            tapped = 60.0f / ioi;
    }

    if (tapped < 50.0f || tapped > 200.0f)
        return;

    const bool fourthTap = tapIoiFilled == 3;
    bool acceptTempo = fourthTap;
    float candidate = tapped;

    if (! fourthTap)
    {
        const float instant = static_cast<float> (60.0 / dt);
        if (instant >= 50.0f && instant <= 200.0f)
        {
            const float rel = heldBpm > 50.0f
                                  ? std::fabs (instant - heldBpm) / heldBpm
                                  : 0.0f;
            if (rel <= 0.08f)
            {
                candidate = instant;
                acceptTempo = true;
            }
        }
    }

    if (fourthTap)
    {
        follower.forceTempo (candidate);
        follower.setTargetTempo (candidate, 0.95f);
        heldBpm = candidate;
        follower.snapBeat (3);
        if (armed && ! hadPlayed)
        {
            waitForQuantize = true;
            waitForSongBeat = false;
        }
    }
    else if (acceptTempo)
    {
        const float rel = heldBpm > 50.0f
                              ? std::fabs (candidate - heldBpm) / heldBpm
                              : 0.0f;
        const float blend = rel > 0.08f ? 0.65f : 0.45f;
        heldBpm += (candidate - heldBpm) * blend;
        follower.setTargetTempo (heldBpm, 0.95f);
        follower.snapPhase (0.0f);
    }

    tapEstablished = true;
    currentState = TrackingState::following;
    retuning = false;
    gridMuteSamples = static_cast<int> (sampleRate * 0.35);
    tapHold = true;
    tapHoldSamples = 0;
    tapAligned = true;
    if (armed && (! fourthTap || hadPlayed))
        waitForQuantize = false;
}

int BeatTracker::pulsesFor (Subdivision s) const noexcept
{
    switch (s)
    {
        case Subdivision::quarter:    return 1;
        case Subdivision::eighth:     return 2;
        case Subdivision::sixteenth:  return 4;
        case Subdivision::autoDetect: return 4;
    }
    return 4;
}

void BeatTracker::updateState (float confidence, bool hadBeat, bool loudEnough, bool periodic) noexcept
{
    const int sr = static_cast<int> (sampleRate);

    switch (currentState)
    {
        case TrackingState::idle:
            currentState = TrackingState::listening;
            listeningSamples = 0;
            break;

        case TrackingState::listening:
            if (periodic && (loudEnough || inputPeakEnv > 0.0008f) && confidence > 0.22f
                && listeningSamples > static_cast<int> (sampleRate * 0.70)
                && heldBpm > 50.0f && beatCount >= 2
                && samplesSinceBeat < static_cast<int> (sampleRate * 1.6))
            {
                currentState = TrackingState::locking;
                lockHoldSamples = 0;
            }
            break;

        case TrackingState::locking:
            if (! loudEnough && ! armed)
            {
                currentState = TrackingState::listening;
                lockHoldSamples = 0;
            }
            else if (periodic && confidence > 0.28f
                     && lockHoldSamples > static_cast<int> (sampleRate * (lockedOnce ? 0.10f : 0.16f)))
            {
                currentState = TrackingState::following;
            }
            else if (confidence < 0.18f && lockHoldSamples > static_cast<int> (sampleRate * 1.2))
            {
                currentState = TrackingState::listening;
            }
            break;

        case TrackingState::following:
            if (tapEstablished)
            {
                lowHoldSamples = 0;
                break;
            }
            if (! tapHold && confidence < 0.22f)
            {
                lowHoldSamples += sr / 50;
                if (lowHoldSamples > sr * 4)
                    currentState = TrackingState::lowConfidence;
            }
            else
            {
                lowHoldSamples = 0;
            }
            break;

        case TrackingState::lowConfidence:
            if (confidence > 0.40f)
            {
                currentState = TrackingState::following;
                lowHoldSamples = 0;
            }
            else if (samplesSinceBeat > sr * 8)
            {
                currentState = TrackingState::recovering;
            }
            break;

        case TrackingState::recovering:
            if (confidence > 0.50f && hadBeat)
                currentState = TrackingState::following;
            break;

        case TrackingState::stopped:
            currentState = TrackingState::listening;
            break;
    }
}

BeatTracker::Output BeatTracker::process (const float* mono, int numSamples) noexcept
{
    Output out;

    if (currentState == TrackingState::idle || currentState == TrackingState::stopped)
        currentState = TrackingState::listening;

    if (mono != nullptr && numSamples > 0)
        neural.feed (mono, numSamples);

    float blockPeak = 0.0f;
    if (mono != nullptr)
    {
        for (int i = 0; i < numSamples; ++i)
            blockPeak = std::max (blockPeak, std::abs (mono[i]));
    }

    const float tau = static_cast<float> (sampleRate * 0.55);
    const float release = 1.0f - std::exp (-static_cast<float> (numSamples) / std::max (1.0f, tau));
    if (blockPeak > inputPeakEnv)
        inputPeakEnv = blockPeak;
    else
        inputPeakEnv += (blockPeak - inputPeakEnv) * release;
    const bool loudEnough = inputPeakEnv > (speakerFollow ? 0.0010f : 0.0020f);
    if (inputPeakEnv > (speakerFollow ? 0.004f : 0.020f))
        heardMusic = true;

    BeatHypothesis hyp;
    const bool haveHyp = neural.tryLoad (hyp);
    const float nnBpm = (haveHyp && hyp.valid) ? hyp.bpm : 0.0f;
    const float nnConf = haveHyp ? hyp.confidence : 0.0f;
    const bool periodic = haveHyp && hyp.valid && nnBpm > 50.0f;

    // The worker publishes one hypothesis per 20 ms analysis frame into a
    // single slot, while this runs once per audio block. Reading `peak` would
    // count one beat twice on a small buffer and miss it entirely on a large
    // one, so beats arrive as monotonic serials instead.
    int newBeats = 0;
    int newDownbeats = 0;
    if (haveHyp)
    {
        if (! seenSerials)
        {
            lastBeatSerial = hyp.beatSerial;
            lastDownbeatSerial = hyp.downbeatSerial;
            seenSerials = true;
        }
        newBeats = static_cast<int> (hyp.beatSerial - lastBeatSerial);
        newDownbeats = static_cast<int> (hyp.downbeatSerial - lastDownbeatSerial);
        lastBeatSerial = hyp.beatSerial;
        lastDownbeatSerial = hyp.downbeatSerial;
    }
    const bool hadBeat = newBeats > 0;
    const bool hadDownbeat = newDownbeats > 0;

    // Everything the analysis reports describes audio that arrived a while ago:
    // a 64 ms window, the FIFO backlog, and whenever the worker last ran. The
    // gap between what has been fed and what the hypothesis covers measures all
    // of it at once. Add the device round trip so what is played now is heard
    // in time rather than merely computed in time.
    const float beatSeconds = hyp.periodSec > 0.01f
                                  ? hyp.periodSec
                                  : 60.0f / std::max (50.0f, heldBpm > 50.0f ? heldBpm : 120.0f);
    float leadBeats = 0.0f;
    if (haveHyp && hyp.analysisSample > 0)
    {
        const double pipelineSec = static_cast<double> (neural.samplesFed() - hyp.analysisSample)
                                   / sampleRate;
        const float leadSec = std::clamp (static_cast<float> (pipelineSec), 0.0f, 0.60f)
                              + reportedLatencyMs * 0.001f;
        leadBeats = leadSec / beatSeconds;
        lastLeadMs = leadSec * 1000.0f;
    }
    // Where the song's pulse is *now*, rather than where it was when the worker
    // last looked at it.
    const float songPhase = wrap01 (hyp.beatPhase + leadBeats);

    const bool tapOwnsTempo = tapEstablished && speakerFollow && heldBpm > 50.0f;
    const bool holding = tapHold || tapOwnsTempo || (! retuning
                      && (currentState == TrackingState::following
                          || currentState == TrackingState::lowConfidence
                          || currentState == TrackingState::recovering));

    follower.setLatencyCompensationMs (reportedLatencyMs + 20.0f);
    follower.setLocked (holding && currentState != TrackingState::listening);

    // Trim exists to close a standing rate error the tempo source cannot see.
    // Under TAP there is no source at all. On a fixed tempo the decoder has
    // stopped moving, so any residual drift is ours to correct. While the tempo
    // is genuinely live the decoder is already chasing it and a second
    // controller would only fight the first.
    const bool trimTempo = tapOwnsTempo
                           || (periodic && hyp.regime == TempoRegime::fixed && ! tapHold);
    follower.setTempoTrimEnabled (trimTempo);

    if (tapOwnsTempo)
        follower.setTargetTempo (heldBpm, 0.95f);
    else if (nnBpm > 50.0f)
    {
        // The decoder now guards its own metrical level and decides for itself
        // whether the tempo is fixed or moving, so there is nothing left for a
        // second gate here to protect against. The percentage window this used
        // to apply is what made a real tempo change take twenty seconds to
        // arrive, because the clock refused every estimate that differed from
        // the one it was already wrong about.
        follower.setTargetTempo (nnBpm, nnConf);
    }

    effectiveSubdivision = userSubdivision == Subdivision::autoDetect
                               ? Subdivision::eighth
                               : userSubdivision;
    follower.setPulsesPerBeat (pulsesFor (effectiveSubdivision));

    if (downbeatHoldSamples > 0)
        downbeatHoldSamples -= numSamples;
    if (hadDownbeat && ! tapHold && ! waitForQuantize && downbeatHoldSamples <= 0
        && follower.beatPhase() < 0.22f
        && follower.beatInBarIndex() != 0)
    {
        follower.snapDownbeat (follower.beatPhase());
        downbeatHoldSamples = static_cast<int> (sampleRate * 0.85);
    }

    if (hadBeat)
    {
        samplesSinceBeat = 0;
        quietSamples = 0;
        beatCount += newBeats;
        if (! tapHold && hyp.confidence > 0.25f)
        {
            // observeOnsetPhase wants the clock's own phase at the instant the
            // song's beat happened, which is what makes its error term mean
            // "the clock is early/late". Feeding it the analysis phase instead
            // reported a near-zero error on every beat, so the loop was being
            // told it was already in sync no matter where it actually was.
            follower.observeOnsetPhase (wrap01 (follower.beatPhase() - songPhase),
                                        hyp.confidence, 1);
        }
    }
    else
    {
        samplesSinceBeat += numSamples;
        quietSamples += numSamples;
    }

    if (needsResync && armed && waitForQuantize && ! tapEstablished && periodic)
    {
        follower.setTargetTempo (nnBpm, std::max (nnConf, 0.75f));
        heldBpm = nnBpm;
        currentState = TrackingState::following;
        lockedOnce = true;
        waitForSongBeat = true;
        needsResync = false;
    }

    const bool musicOn = quietSamples < static_cast<int> (sampleRate * 0.35);
    if (lockedOnce && ! tapHold && ! tapOwnsTempo && musicOn && heldBpm > 50.0f && nnBpm > 50.0f)
    {
        const float tempoRel = std::fabs (nnBpm - heldBpm) / heldBpm;
        if (tempoRel > 0.08f)
            lostSyncSamples += numSamples;
        else
            lostSyncSamples = std::max (0, lostSyncSamples - numSamples * 2);

        if (lostSyncSamples > static_cast<int> (sampleRate * 1.15f))
            retuning = true;
    }
    else if (! retuning)
    {
        lostSyncSamples = 0;
    }

    if (gridMuteSamples > 0)
        gridMuteSamples -= numSamples;
    else if (! tapHold && ! waitForQuantize && periodic && loudEnough && nnConf > 0.28f
             && currentState != TrackingState::listening
             && currentState != TrackingState::idle
             && ! tapEstablished)
    {
        follower.setGridPhase (songPhase, holding ? 0.06f : 0.14f);
    }

    if (tapHold)
    {
        tapHoldSamples += numSamples;
        if (tapHoldSamples > static_cast<int> (sampleRate * 0.70))
            tapHold = false;
    }

    out.clock = follower.advance (numSamples);

    smoothedConf = smoothedConf * 0.85f + nnConf * 0.15f;
    if (hadBeat && nnConf > 0.35f)
        smoothedConf = std::min (1.0f, smoothedConf + 0.025f);

    if (currentState == TrackingState::listening)
        listeningSamples += numSamples;
    if (currentState == TrackingState::locking)
        lockHoldSamples += numSamples;

    if (! tapOwnsTempo)
    {
        if (lockedOnce && follower.currentTempo() > 50.0f)
            heldBpm = follower.currentTempo();
        else if (tapEstablished && follower.currentTempo() > 50.0f)
            heldBpm = follower.currentTempo();
        else if (nnBpm > 50.0f)
            heldBpm = nnBpm;
        else if (quietSamples > static_cast<int> (sampleRate * 2.5f))
            heldBpm = 0.0f;
    }

    const TrackingState prevState = currentState;
    updateState (smoothedConf, hadBeat, loudEnough, periodic);

    if (speakerFollow && ! armed && ! lockedOnce && ! tapEstablished && ! heardMusic && ! loudEnough
        && currentState == TrackingState::locking)
    {
        ghostLockSamples += numSamples;
        if (ghostLockSamples > static_cast<int> (sampleRate * 0.85))
        {
            currentState = TrackingState::listening;
            lockedOnce = false;
            heldBpm = 0.0f;
            listeningSamples = 0;
            lockHoldSamples = 0;
            ghostLockSamples = 0;
            retuning = false;
        }
    }
    else
    {
        ghostLockSamples = 0;
    }

    if (retuning && nnBpm > 50.0f && currentState == TrackingState::following)
    {
        follower.setTargetTempo (nnBpm, std::max (nnConf, 0.70f));
        heldBpm = nnBpm;
        if (std::fabs (follower.currentTempo() - nnBpm) < 4.0f)
        {
            retuning = false;
            lostSyncSamples = 0;
        }
    }

    if (currentState == TrackingState::following && prevState != TrackingState::following)
    {
        if (tapAligned)
            tapAligned = false;
        else if (! tapHold && nnBpm > 50.0f && gridMuteSamples <= 0 && haveHyp)
            follower.snapPhase (songPhase);
    }

    if (tapOwnsTempo)
        currentState = TrackingState::following;

    if (currentState == TrackingState::following)
        lockedOnce = true;

    out.state = currentState;
    out.subdivision = effectiveSubdivision;
    const bool showBpm = heldBpm > 40.0f
                         && (lockedOnce || tapEstablished || periodic);
    out.bpm = showBpm ? (tapOwnsTempo ? follower.currentTempo() : heldBpm) : 0.0f;
    out.targetBpm = follower.targetTempo() + follower.tempoTrimBpm();
    out.confidence = smoothedConf;
    out.beatPhase = follower.beatPhase();
    out.barPhase = follower.barPhase();
    out.beatsElapsed = follower.beatsElapsed();

    const bool canPlay = armed
                      && (currentState == TrackingState::following
                          || currentState == TrackingState::lowConfidence
                          || currentState == TrackingState::recovering
                          || retuning);
    if (waitForQuantize && canPlay)
    {
        quantizeWaitSamples += numSamples;
        const float bpmForWait = std::max (50.0f, heldBpm > 40.0f ? heldBpm : 120.0f);
        const int twoBars = static_cast<int> (sampleRate * (8.0 * 60.0 / static_cast<double> (bpmForWait)));
        const int timeout = std::min (static_cast<int> (sampleRate * 3.0),
                                      std::max (static_cast<int> (sampleRate * 1.6), twoBars));
        const bool timedOut = quantizeWaitSamples > timeout;

        bool onOne = out.clock.wrappedBar;
        for (int i = 0; i < out.clock.pulsesFired; ++i)
        {
            if (out.clock.pulseIndex[i] != 0)
                continue;
            if (out.clock.pulseBeatInBar[i] == 0 || timedOut)
            {
                onOne = true;
                break;
            }
        }
        if (onOne)
        {
            waitForQuantize = false;
            waitForSongBeat = false;
            quantizeWaitSamples = 0;
        }
    }

    out.percussionShouldPlay = canPlay && ! waitForQuantize;
    if (out.percussionShouldPlay)
        hadPlayed = true;
    out.tapLocked = tapHold;
    out.aiOnnx = neural.usingOnnx();
    out.hypValid = haveHyp && hyp.valid;
    out.neuralBpm = nnBpm;
    out.pBeat = haveHyp ? hyp.pBeat : 0.0f;
    out.leadMs = lastLeadMs;
    out.regime = haveHyp ? hyp.regime : TempoRegime::unknown;
    if (! armed)
        out.followBar = FollowBar::paused;
    else if (waitForQuantize && canPlay)
        out.followBar = FollowBar::waitBeat;
    else if (retuning)
        out.followBar = FollowBar::recalin;
    else if (tapHold)
        out.followBar = FollowBar::tapAlign;
    else
    {
        switch (currentState)
        {
            case TrackingState::listening:     out.followBar = FollowBar::listening; break;
            case TrackingState::locking:       out.followBar = FollowBar::calibrating; break;
            case TrackingState::following:
                out.followBar = smoothedConf > 0.78f ? FollowBar::following
                                                     : FollowBar::followingListen;
                break;
            case TrackingState::lowConfidence: out.followBar = FollowBar::weakFollow; break;
            case TrackingState::recovering:    out.followBar = FollowBar::recalin; break;
            case TrackingState::stopped:       out.followBar = FollowBar::paused; break;
            case TrackingState::idle:          out.followBar = FollowBar::ready; break;
        }
    }
    return out;
}

} // namespace vp
