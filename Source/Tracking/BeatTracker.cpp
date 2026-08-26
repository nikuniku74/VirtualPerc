#include "Tracking/BeatTracker.h"

#include <algorithm>
#include <cmath>

namespace vp
{

namespace
{
    // Sixteenths. See the note in process().
    constexpr int kClockPulsesPerBeat = 4;

    // How long to hold out for a downbeat before coming in on any quarter.
    // Percussion entering mid-bar is the kind of mistake a listener hears
    // immediately, so this is generous - but it cannot be unbounded, because
    // material the network never finds a downbeat in would then never start.
    constexpr double kBarsBeforeGivingUp = 4.0;

    // Beats the histogram has to hold before it is worth acting on while
    // waiting to come in, and the larger number needed to move a bar that is
    // already playing - where the correction is something the listener hears.
    // Beats and not downbeats: every beat now files an opinion, so this is
    // three bars of evidence and eight bars of it, decayed.
    constexpr float kBeatsToTrustTheBar = 12.0f;
    constexpr float kBeatsToMoveTheBar = 32.0f;

    // Per beat. Over sixty-four of them - sixteen bars - the oldest evidence is
    // worth a third of the newest, which is a phrase or two: long enough to be
    // a vote and short enough to notice a section change. It used to be applied
    // per downbeat *event*, which on material the network was sure about was
    // about once a bar and on material it was unsure about was hardly ever, so
    // the window the vote covered was a function of how confident the network
    // happened to be.
    constexpr float kVoteDecay = 0.982f;

    // How far the winner has to stand clear of the runner-up before the bar is
    // moved at all - and how much further while the part is playing. Shares of
    // the histogram, so four quarters the network cannot separate sit at 0.25
    // each and no margin at all is available: material that carries no bar
    // leaves the count where it is instead of being rotated onto noise.
    //
    // Measured over the thirty rendered tracks, the margin the winner actually
    // achieves separates the two cases cleanly: 0.34 on a line feed, where the
    // network finds the true one in 73% of bars, against 0.014 through an iPad
    // speaker and a room, where it is no better than a coin. See
    // scripts/probe_bar.cpp.
    constexpr float kBarWinMargin = 0.10f;
    constexpr float kBarWinMarginPlaying = 0.20f;

    // And once moved, left alone for four bars at 100 BPM. Anything shorter and
    // two disagreeing votes can trade the bar back and forth inside one phrase.
    constexpr double kBarMoveHoldSeconds = 9.6;

    // And a good deal longer after the listener has moved it by hand.
    constexpr double kBarNudgeHoldSeconds = 30.0;

    // AUTO half/double. The range a percussionist counts in, a little over an
    // octave wide - and that overlap is the hysteresis: a tempo just halved from
    // 168 lands on 84 rather than on 76, so nothing can sit on a boundary and be
    // pushed back and forth across it. See the note on BeatDecoder::userOctave
    // for why the choice cannot be made from the signal in the first place.
    constexpr float kOctaveTooFast = 168.0f;
    constexpr float kOctaveTooSlow = 76.0f;

    // Held before it is taken. Changing the metrical level in the middle of a
    // song is one of the most audible things this app can do, and a reading that
    // has only just arrived is not evidence enough to do it on.
    constexpr double kAutoOctaveHoldSeconds = 2.5;

    // How long the one stays marked after a tap has declared it.
    constexpr double kBarDeclaredFlashSeconds = 0.9;

    // The pipeline delay computed below is a geometric figure - where the
    // analysis frame sits in the input stream - and it comes out about one hop
    // too long. Measured against a notated click through the real network, the
    // clock ran a steady 15-20 ms *ahead* of the song at 78, 100 and 138 BPM:
    // constant in milliseconds rather than in beats, which is what identifies
    // it as a fixed pipeline offset and not a rate error.
    //
    // Trimming the lead by 18 ms takes that residual to -3..+2 ms across the
    // same three tempi. The value is a calibration, not a derivation: it is
    // suspiciously close to one 20 ms hop, but the geometry in
    // NeuralBeatTracker::analysisSampleFor checks out frame by frame, so the
    // rest is most likely the network's own response offset. It is measured on
    // a click, so real material may sit a millisecond or two either side.
    constexpr float kAnalysisLeadTrimSec = 0.018f;
}

void BeatTracker::setBeatModel (std::unique_ptr<IBeatModel> m)
{
    neural.setModel (std::move (m));
}

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
    lastGridSerial = 0;
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
    tapHold = false;
    tapAligned = false;
    tapEstablished = false;
    sawInputStart = false;
    seenEpoch = false;
    lastInputEpoch = 0;
    waitForQuantize = false;
    heardMusic = false;
    hadPlayed = false;
    sounding = false;
    needsResync = false;
    waitForSongBeat = false;
    armed = false;
    tapHoldSamples = 0;
    downbeatHoldSamples = 0;
    std::fill (downbeatVotes, downbeatVotes + 4, 0.0f);
    voteBeats = 0.0f;
    quantizeWaitSamples = 0;
    lastTapSec = -1.0;
    barDeclaredSamples = 0;
    tapIoiWrite = 0;
    tapIoiFilled = 0;
    std::fill (tapIoi, tapIoi + 8, 0.0f);
    userTempoGen = ~0u;
}

void BeatTracker::setFollowStrength (FollowStrength s) noexcept
{
    follower.setFollowStrength (s);
}

void BeatTracker::setTempoFollow (bool on) noexcept
{
    if (on == tempoFollow)
        return;

    // SEGUI after FISSO: the listener asked the analysis to drive again, so a
    // tap that had owned the tempo no longer does. Calling this every block
    // with the same value is a no-op above, so a count-in that is still
    // landing is not cleared.
    if (on)
    {
        tapEstablished = false;
        tapHold = false;
        tapAligned = false;
    }
    tempoFollow = on;
}

void BeatTracker::setUserTempo (float bpm, uint32_t generation) noexcept
{
    if (generation == userTempoGen)
        return;
    userTempoGen = generation;
    if (! std::isfinite (bpm) || bpm < 50.0f || bpm > 200.0f)
        return;

    heldBpm = bpm;
    follower.forceTempo (bpm);
    follower.setTargetTempo (bpm, 0.95f);
    currentState = TrackingState::following;
    lockedOnce = true;
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

    // A gap this long ends the group: the next tap starts a new count, and it
    // is the one that gets to say where the bar is.
    const bool groupIdle = lastTapSec < 0.0 || dt > 2.0;
    if (groupIdle)
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

    const bool startsGroup = groupIdle || tapIoiFilled == 0;
    lastTapSec = timeSeconds;

    // The tap is the one.
    //
    // A single tap, on its own, does not say anything about the tempo - three
    // intervals are needed for that - but it says exactly where the bar starts,
    // and that is the thing the analysis cannot work out for itself. So the
    // first tap of a group declares the downbeat whenever there is already a
    // tempo to hang it on, and the count-in below keeps the same meaning: tap
    // one-two-three-four and the one was the first of them.
    if (startsGroup && ! tapEstablished && heldBpm > 50.0f
        && (currentState == TrackingState::following
            || currentState == TrackingState::lowConfidence
            || currentState == TrackingState::recovering))
    {
        follower.snapBeat (0, 0.0f);
        holdBarDecision();
        barDeclaredSamples = static_cast<int> (sampleRate * kBarDeclaredFlashSeconds);
        // Deliberately not tapHold/tapEstablished: those hand the tempo to the
        // tapper, and this tap was not about the tempo.
    }

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
    float candidate = tapped;

    if (fourthTap)
    {
        follower.forceTempo (candidate);
        follower.setTargetTempo (candidate, 0.95f);
        heldBpm = candidate;
        // Four taps counted in: this one is beat four, so the next is the one.
        follower.snapBeat (3);
        holdBarDecision();
        barDeclaredSamples = static_cast<int> (sampleRate * kBarDeclaredFlashSeconds);
        if (armed && ! hadPlayed)
        {
            waitForQuantize = true;
            waitForSongBeat = false;
        }
    }
    else
    {
        // Keep tapping: the tempo is what the last few intervals say.
        //
        // Used to accept a follow-up tap only inside 8% of the count-in, so
        // accelerating (120 → 140 is 17%) did nothing until a 2 s pause reset
        // the group. A percussionist who is still tapping is conducting; the
        // new rate is the one that counts. Mean of the last three intervals
        // so one rushed tap does not yank the grid, but two or three at the
        // new speed take it.
        float recent = 0.0f;
        {
            const int n = std::min (3, tapIoiFilled);
            float sum = 0.0f;
            for (int k = 0; k < n; ++k)
            {
                const int idx = (tapIoiWrite - 1 - k + 8) % 8;
                sum += tapIoi[idx];
            }
            const float ioi = sum / static_cast<float> (n);
            if (ioi > 1.0e-4f)
                recent = 60.0f / ioi;
        }
        if (recent < 50.0f || recent > 200.0f)
            return;

        candidate = recent;
        const float rel = heldBpm > 50.0f
                              ? std::fabs (candidate - heldBpm) / heldBpm
                              : 1.0f;
        if (rel > 0.06f)
        {
            heldBpm = candidate;
            follower.forceTempo (candidate);
        }
        else
        {
            heldBpm += (candidate - heldBpm) * 0.55f;
            follower.setTargetTempo (heldBpm, 0.95f);
        }
        follower.snapPhase (0.0f);
    }

    tapEstablished = true;
    // Somebody tapped, so somebody is playing. That does not stop being true
    // when they switch back to SEGUI and the tap stops owning the tempo.
    sawInputStart = true;
    currentState = TrackingState::following;
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
            if (tapEstablished || ! tempoFollow)
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

void BeatTracker::holdBarDecision() noexcept
{
    // The evidence that produced the current bar is exactly the evidence that
    // has just been overruled, so it is cleared and the automatic alignment is
    // held off. Without this the vote puts the bar back within a phrase and the
    // correction looks like it did nothing.
    std::fill (downbeatVotes, downbeatVotes + 4, 0.0f);
    downbeatVotes[0] = kBeatsToMoveTheBar;
    voteBeats = kBeatsToMoveTheBar;
    downbeatHoldSamples = static_cast<int> (sampleRate * kBarNudgeHoldSeconds);
}

void BeatTracker::setTempoOctave (int octaves) noexcept
{
    userOctave = octaves < -1 ? -1 : (octaves > 1 ? 1 : octaves);
    if (! octaveAuto)
        neural.setUserOctave (userOctave);
}

void BeatTracker::setTempoOctaveAuto (bool on) noexcept
{
    if (on == octaveAuto)
        return;

    octaveAuto = on;
    // Switching AUTO on starts from the level the listener had chosen rather
    // than from the analysis's own reading: what they picked is the best
    // evidence there is about the pulse they want, and jumping levels the
    // instant the button is released would change the part under their hands.
    autoOctave = userOctave;
    autoWant = userOctave;
    autoHoldSamples = 0;
    neural.setUserOctave (octaveAuto ? autoOctave : userOctave);
}

void BeatTracker::updateAutoOctave (float bpm, bool periodic, int numSamples) noexcept
{
    if (! periodic || bpm < 40.0f)
    {
        autoHoldSamples = 0;
        return;
    }

    // The reading already carries whatever shift is in force, so a level too
    // fast is answered by going one below the shift now applied, not one below
    // zero.
    int want = autoOctave;
    if (bpm > kOctaveTooFast && want > -1)
        --want;
    else if (bpm < kOctaveTooSlow && want < 1)
        ++want;

    if (want != autoWant)
    {
        autoWant = want;
        autoHoldSamples = 0;
        return;
    }
    if (want == autoOctave)
    {
        autoHoldSamples = 0;
        return;
    }

    autoHoldSamples += numSamples;
    if (autoHoldSamples > static_cast<int> (sampleRate * kAutoOctaveHoldSeconds))
    {
        autoOctave = want;
        autoHoldSamples = 0;
        neural.setUserOctave (autoOctave);
    }
}

void BeatTracker::nudgeBar (int beats) noexcept
{
    follower.rotateBarIndex (beats);
    holdBarDecision();
}

void BeatTracker::alignBarFromVotes (bool comingIn) noexcept
{
    float score[4] {};
    float totalVotes = 0.0f;
    for (int i = 0; i < 4; ++i)
        totalVotes += downbeatVotes[i];

    for (int i = 0; i < 4; ++i)
        score[i] = totalVotes > 1.0e-6f ? downbeatVotes[i] / totalVotes : 0.25f;

    int best = 0;
    float bestVotes = 0.0f, runnerUp = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        if (score[i] > bestVotes)
        {
            runnerUp = bestVotes;
            bestVotes = score[i];
            best = i;
        }
        else if (score[i] > runnerUp)
        {
            runnerUp = score[i];
        }
    }

    // How much evidence there is, counted in beats rather than in activation:
    // a network that is loud about everything must not look better supported
    // than one that is quiet about everything, and the shares above already
    // carry how loud it was.
    if (best == 0 || voteBeats < (comingIn ? kBeatsToTrustTheBar : kBeatsToMoveTheBar))
        return;

    // Waiting to come in, a plurality is enough: nothing is playing yet, so a
    // rotation costs nothing and the alternative is entering on the wrong beat.
    // Once the part is playing the bar is audible, and moving it is only worth
    // doing on evidence that is not close - so the leader must also be clear of
    // whoever is second.
    // Scores are normalised now, so a clear win is a margin over the runner-up
    // rather than a share of a total.
    if (bestVotes < runnerUp + kBarWinMargin)
        return;
    if (! comingIn)
    {
        // Stricter once the part is playing, because a bar the listener can
        // hear is being moved under them. It no longer has to be *far*
        // stricter: the histogram is built from every beat rather than from
        // the handful the network was confident enough to call, so the same
        // margin now stands on four times the evidence and stops being a
        // reading of the noise. A bar that is consistently wrong can be
        // corrected with one tap; one that keeps moving cannot.
        if (bestVotes < runnerUp + kBarWinMarginPlaying
            || downbeatHoldSamples > 0)
            return;
        downbeatHoldSamples = static_cast<int> (sampleRate * kBarMoveHoldSeconds);
    }

    follower.rotateBarIndex (-best);

    // Rotating renumbers the beats, so the tally rotates with them rather than
    // being thrown away: the evidence is still good, it is just about different
    // indices now. Clearing it instead meant every correction started the
    // argument again from nothing.
    float rotated[4];
    for (int i = 0; i < 4; ++i)
        rotated[i] = downbeatVotes[(i + best) & 3];
    for (int i = 0; i < 4; ++i)
        downbeatVotes[i] = rotated[i];
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
    if (haveHyp)
    {
        if (! seenSerials)
        {
            lastBeatSerial = hyp.beatSerial;
            lastGridSerial = hyp.gridSerial;
            seenSerials = true;
        }
        newBeats = static_cast<int> (hyp.beatSerial - lastBeatSerial);
        lastBeatSerial = hyp.beatSerial;
    }
    const bool hadBeat = newBeats > 0;

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
        // Clamp last: the trim can take a short startup pipeline below zero, and
        // a negative lead would drag the phase target backwards.
        const float leadSec = std::clamp (static_cast<float> (pipelineSec) - kAnalysisLeadTrimSec
                                              + reportedLatencyMs * 0.001f,
                                          0.0f, 0.60f);
        leadBeats = leadSec / beatSeconds;
        lastLeadMs = leadSec * 1000.0f;
    }
    // Where the song's pulse is *now*, rather than where it was when the worker
    // last looked at it.
    const float songPhase = wrap01 (hyp.beatPhase + leadBeats);

    // TAP is the same button in both modes and means the same thing in both:
    // the listener knows the tempo better than the analysis does. It used to be
    // honoured only while following the iPad's own speaker - the case the tap
    // flow was designed around - so on a mixer feed four taps set the tempo and
    // the network took it straight back. Measured on a 100 BPM track tapped at
    // 132: held 100% of the time in IPAD, 0% in MIXER.
    const bool tapOwnsTempo = tapEstablished && heldBpm > 50.0f;
    const bool userOwnsTempo = ! tempoFollow && heldBpm > 50.0f;
    const bool tempoOwned = tapOwnsTempo || userOwnsTempo;
    const bool holding = tapHold || tempoOwned
                      || currentState == TrackingState::following
                      || currentState == TrackingState::lowConfidence
                      || currentState == TrackingState::recovering;

    // The clock's lead is applied where it is measured - in `songPhase` above,
    // from the pipeline delay plus the device round trip. The follower used to
    // be handed a latency figure of its own as well, which it stored and never
    // used: two names for one correction, one of them doing nothing.
    follower.setLocked (holding && currentState != TrackingState::listening);

    // Trim exists to close a standing rate error the tempo source cannot see.
    // Under TAP there is no source at all. On a fixed tempo the decoder has
    // stopped moving, so any residual drift is ours to correct. While the tempo
    // is genuinely live the decoder is already chasing it and a second
    // controller would only fight the first.
    const bool trimTempo = tapOwnsTempo
                           || (periodic && hyp.regime == TempoRegime::fixed
                               && ! tapHold && tempoFollow);
    follower.setTempoTrimEnabled (trimTempo);

    if (tempoOwned)
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

    // The clock always runs on sixteenths, whatever the part is playing. The
    // loud strokes of a marcha sit on eighths, but the quiet half of it - the
    // heel and toe filling the gaps - is on sixteenths, and that half is what a
    // listener hears as a player rather than as a pattern. What the user's
    // subdivision setting selects is how dense the *shaker* is, which is a
    // question for GrooveEngine, not for the clock.
    effectiveSubdivision = userSubdivision == Subdivision::autoDetect
                               ? Subdivision::eighth
                               : userSubdivision;
    follower.setPulsesPerBeat (kClockPulsesPerBeat);

    if (downbeatHoldSamples > 0)
        downbeatHoldSamples -= numSamples;
    // The bar has to be alignable *while* waiting to come in - that is the one
    // moment it matters most. It used to be excluded here, so through the whole
    // wait the bar was never corrected and "the first quarter" meant whichever
    // beat the clock happened to start counting on. Measured over eight tracks,
    // the percussion came in on the one twice, and the misses were off by very
    // nearly a whole beat: the clock was on the beat and the bar was rotated.
    //
    // Every beat files an opinion about whether it was the one, and the bar is
    // the quarter those opinions add up on - a histogram over the bar, not a
    // tally of the events that happened to clear a threshold. The gated events
    // are the loud half of the evidence and also the rare half: through an iPad
    // speaker only 63% of beats clear the gate at all and on quieter material
    // far fewer, and a vote taken over a handful of samples shows a wide margin
    // on noise alone. Over every beat, the same margin is a measurement: 0.34
    // where the network really has found the one, 0.014 where it has not.
    if (hadBeat && ! tapHold)
    {
        // Which quarter of the bar this beat fell on, in the clock's own count.
        //
        // Not the clock's phase *now*: what the analysis has just reported
        // happened a measured while ago - up to 0.6 s of pipeline and device
        // delay - and `hyp.beatPhase + leadBeats` is exactly how far the song
        // has moved since. Taking that off the clock's position gives the
        // position the clock was at when the beat actually sounded, and the
        // beat belongs to the quarter nearest it. The old test read the phase
        // now and called anything past the half "the next quarter", which files
        // a beat one quarter late whenever the lead runs over half a beat.
        //
        // Unwrapped on purpose, which `songPhase` a few lines up is not: at
        // 100 BPM the delay ceiling is a whole beat, and folded into one beat a
        // lead of 1.2 reads as 0.2 and loses the beat it crossed.
        const float posNow = static_cast<float> (follower.beatInBarIndex())
                             + follower.beatPhase();
        const float sinceBeat = hyp.beatPhase + leadBeats;
        const int nearest = static_cast<int> (std::lround (posNow - sinceBeat));
        const int at = ((nearest % 4) + 4) & 3;

        // Weighted by how strongly the network called this beat the one, which
        // for most beats is "not at all" and is worth counting as such. On
        // material where beats one and three carry the same kick - which is
        // most material - the network fires on both, and what separates them is
        // a difference in confidence too small to survive a threshold and quite
        // large enough to survive a phrase of adding up.
        //
        // The whole tally decays as it goes. Without that, the first bars of a
        // track keep their say forever and a section change can never be heard;
        // with it the vote is always about the recent past.
        for (float& v : downbeatVotes)
            v *= kVoteDecay;
        voteBeats = voteBeats * kVoteDecay + 1.0f;
        downbeatVotes[at] += std::clamp (hyp.beatDownbeat, 0.0f, 1.0f);

        // While playing, the bar is corrected from the whole histogram - never
        // from the single beat that just arrived.
        //
        // It used to snap on one downbeat, which is what a listener hears as
        // "one, two, one": the network puts a plurality of its downbeats on the
        // true one and scatters the rest, so every stray one restarted the bar
        // in the middle of it. Worse, it snapped the *phase* to do it, which
        // threw away the clock's whole loop state - its phase error, its
        // measured trim - for a correction that only ever concerned the count.
        // Rotating the index moves no phase and drops no pulse.
        if (! waitForQuantize)
            alignBarFromVotes (false);
    }

    if (hadBeat)
    {
        samplesSinceBeat = 0;
        quietSamples = 0;
        beatCount += newBeats;
        if (! tapHold && tempoFollow && hyp.confidence > 0.25f)
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

    if (needsResync && armed && waitForQuantize && ! tapEstablished
        && tempoFollow && periodic)
    {
        follower.setTargetTempo (nnBpm, std::max (nnConf, 0.75f));
        heldBpm = nnBpm;
        currentState = TrackingState::following;
        lockedOnce = true;
        waitForSongBeat = true;
        needsResync = false;
    }

    if (octaveAuto && tempoFollow)
        updateAutoOctave (nnBpm, periodic, numSamples);

    // The analysis has thrown its grid away and built another one.
    //
    // This used to be inferred here, from the network's BPM disagreeing with
    // `heldBpm` by 8% for a second - and `heldBpm` is the follower's own tempo
    // five lines below, which is already chasing the network with a time
    // constant of half a second. Measured over steps from 100 BPM, nothing up
    // to and including a doubling ever accumulated the second it needed: the
    // most any of them managed was 1.12 s at 100 -> 60, and the octaves scored
    // zero because the clock takes those in one block. The state it set was
    // therefore never entered, and with it neither was the phase snap for a new
    // song nor the clearing of the bar.
    //
    // Anything the clock can measure for itself, the clock is also busy
    // closing, so it will always be outrun. The decoder is the one that knows:
    // it says so directly when it drops a grid, and that is the only moment at
    // which the bar count is worth nothing rather than merely stale.
    if (haveHyp && seenSerials && hyp.gridSerial != lastGridSerial)
    {
        lastGridSerial = hyp.gridSerial;
        // Not over a bar the listener placed by hand, and not over one that was
        // moved a moment ago: those are the two cases where somebody already
        // answered this question.
        if (! tapEstablished && tempoFollow && downbeatHoldSamples <= 0)
        {
            std::fill (downbeatVotes, downbeatVotes + 4, 0.0f);
            voteBeats = 0.0f;
        }
    }

    if (gridMuteSamples > 0)
        gridMuteSamples -= numSamples;
    else if (! tapHold && tempoFollow && periodic && loudEnough && nnConf > 0.28f
             && currentState != TrackingState::listening
             && currentState != TrackingState::idle
             && ! tapEstablished)
    {
        // With nothing on the grid, put the grid where the song is.
        //
        // The loop corrects phase by bending the rate, because moving the grid
        // under a part that is already playing doubles a stroke or drops one.
        // That reasoning does not apply when no stroke is being played: there
        // the correction is free and exact, and leaning into it instead costs
        // the seconds measured in docs/CORE_TIMING_AUDIT.md - which is exactly
        // the wait between arming the shaker and it being in the right place.
        // This is also the whole of the time the part spends waiting to come
        // in, which used to be excluded here altogether: through the entire
        // wait the phase was never corrected at all.
        const float gridErr = wrapCentered (follower.beatPhase() - songPhase);
        if (! sounding && std::fabs (gridErr) > 0.04f)
        {
            // Nothing is playing, so the count goes over the boundary with the
            // grid: the bar the part will come in on has to be the song's bar,
            // and this is where that is decided.
            follower.snapPhase (songPhase, true);
        }
        else
        {
            // Seconds, not a per-block blend. While holding, long enough to
            // average several hypotheses - the analysis refreshes about six
            // times a second and each one carries its own phase error - so the
            // clock follows the band and not the decoder. While still acquiring
            // it is worth being wrong quickly.
            follower.setGridPhase (songPhase, holding ? 0.90f : 0.25f);
        }
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

    if (! tempoOwned)
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
        }
    }
    else
    {
        ghostLockSamples = 0;
    }

    if (currentState == TrackingState::following && prevState != TrackingState::following)
    {
        if (tapAligned)
            tapAligned = false;
        else if (! tapHold && nnBpm > 50.0f && gridMuteSamples <= 0 && haveHyp)
        {
            // Silent, the grid is simply placed. Sounding, a snap is a stroke:
            // `snapPhase` re-anchors and puts a pulse on the new phase, which
            // for a correction of a hundredth of a beat is a flam bought for
            // nothing. Above the size the steering loop would take seconds
            // over, it is worth the stroke - which is the same threshold the
            // re-tune path beside this one already used, and this one did not.
            if (! sounding
                || std::fabs (wrapCentered (follower.beatPhase() - songPhase)) > 0.12f)
                follower.snapPhase (songPhase, ! sounding);
        }
    }

    if (tempoOwned)
        currentState = TrackingState::following;

    if (currentState == TrackingState::following)
        lockedOnce = true;

    out.state = currentState;
    out.subdivision = effectiveSubdivision;
    out.clockPulsesPerBeat = kClockPulsesPerBeat;
    const bool showBpm = heldBpm > 40.0f
                         && (lockedOnce || tapEstablished || periodic || userOwnsTempo);
    out.bpm = showBpm ? (tempoOwned ? follower.currentTempo() : heldBpm) : 0.0f;
    out.targetBpm = follower.targetTempo() + follower.tempoTrimBpm();
    out.confidence = smoothedConf;
    out.tempoOctave = octaveAuto ? autoOctave : userOctave;
    out.beatPhase = follower.beatPhase();
    out.barPhase = follower.barPhase();
    barDeclaredSamples = std::max (0, barDeclaredSamples - numSamples);
    out.barDeclared = barDeclaredSamples > 0;
    out.analysisGaps = neural.discontinuities();
    out.analysisWakeups = neural.wakeups();
    out.analysisBacklog = neural.backlog();
    out.beatsElapsed = follower.beatsElapsed();

    // Whether what the clock is following is known to be somebody playing.
    //
    // The app listens from the moment it is opened, so before anyone plays it
    // is analysing a room - and it locks to one: measured through the engine,
    // FOLLOWING at 99 BPM with a confidence of 0.91 in front of a microphone
    // that hears nobody, held that still for half a minute. Nothing in the
    // signal separates that from a band: the quietest band the host tests
    // insist must lock is *quieter* than the room that fools the tracker, so no
    // level can decide it, and the activation's floor - the one statistic that
    // does separate them on a line feed - closes up in a room, where the
    // reflections fill the gaps between beats. See scripts/probe_room.cpp.
    //
    // What can be known is whether this input has ever *changed* since the app
    // was opened, which is what an empty room turning into a band looks like
    // and is the one thing a room alone never does. Until that has happened the
    // percussion is armed and silent rather than playing to nobody.
    //
    // It has a blind spot and it is deliberate: an app opened onto a track
    // already playing never sees a change, and waits. The listener releases it
    // the same way they would tell it anything else - a tap, or a tempo set by
    // hand - and the state on screen says which of the two it is waiting for.
    // The alternative is a timeout, and a timeout long enough to be a guard is
    // long enough to be a nuisance, while one short enough to be tolerable
    // brings back the shaker playing to an empty stage.
    const bool inputIsLive = sawInputStart || tapEstablished || ! tempoFollow;
    const bool canPlay = armed
                      && inputIsLive
                      && (currentState == TrackingState::following
                          || currentState == TrackingState::lowConfidence
                          || currentState == TrackingState::recovering);
    if (waitForQuantize && canPlay)
    {
        quantizeWaitSamples += numSamples;
        const float bpmForWait = std::max (50.0f, heldBpm > 40.0f ? heldBpm : 120.0f);
        const double barSec = 4.0 * 60.0 / static_cast<double> (bpmForWait);

        // The fallback, for when no downbeat is ever found. It has to be long
        // enough to actually contain the bars it is waiting for: the old ceiling
        // of three seconds is shorter than two bars at anything under 160 BPM,
        // so at every ordinary tempo the wait expired before a downbeat could
        // arrive and the part came in on whatever quarter was next.
        const int timeout = std::clamp (static_cast<int> (sampleRate * barSec * kBarsBeforeGivingUp),
                                        static_cast<int> (sampleRate * 2.0),
                                        static_cast<int> (sampleRate * 10.0));
        const bool timedOut = quantizeWaitSamples > timeout;

        // Before looking for the one, put the bar where the evidence says it
        // is. Rotating the count is free - it moves no phase and drops no
        // pulse - so it can be done as often as the vote changes its mind,
        // right up until the moment of entry.
        alignBarFromVotes (true);

        bool onOne = false;
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
    sounding = out.percussionShouldPlay;
    if (out.percussionShouldPlay)
        hadPlayed = true;
    out.tapLocked = tapHold;
    out.aiOnnx = neural.usingOnnx();
    out.hypValid = haveHyp && hyp.valid;
    out.neuralBpm = nnBpm;
    out.pBeat = haveHyp ? hyp.pBeat : 0.0f;
    out.leadMs = lastLeadMs;
    out.regime = haveHyp ? hyp.regime : TempoRegime::unknown;
    out.combBpm = haveHyp ? hyp.combBpm : 0.0f;
    out.levelSettled = haveHyp && hyp.levelSettled;
    out.fitResidual = haveHyp ? hyp.fitResidual : 1.0f;
    out.fitCoverage = haveHyp ? hyp.fitCoverage : 0.0f;
    if (! armed)
        out.followBar = FollowBar::paused;
    else if (! inputIsLive && heldBpm > 40.0f
             && (currentState == TrackingState::following
                 || currentState == TrackingState::lowConfidence
                 || currentState == TrackingState::recovering))
        out.followBar = FollowBar::waitStart;
    else if (waitForQuantize && canPlay)
        out.followBar = FollowBar::waitBeat;
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
