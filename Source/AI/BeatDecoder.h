#pragma once

#include "AI/BeatHypothesis.h"
#include "AI/BeatHmm.h"
#include "AI/TempoEstimator.h"

namespace vp
{

/**
    Turns per-frame BeatNet activations into a playable {bpm, phase} hypothesis.

    Three tempo sources, because no single one is both fast and precise:

      - TempoEstimator folds the activation curve and settles the metrical
        level. Robust, immune to missed and ghost peaks, but averaged over
        seconds. This is the anchor: nothing else may leave its octave.
      - A least-squares fit through recent beat times gives precision. Over a
        long baseline it resolves tempo far finer than the 20 ms frame grid.
      - The same fit over a short baseline gives responsiveness.

    Which of the last two drives the committed tempo depends on the regime. A
    record cut to a click is a fixed tempo and must stop moving once found; a
    band on stage is not, and must be followed. The two are told apart by
    whether the short-baseline fit keeps agreeing with the long one.
*/
class BeatDecoder
{
public:
    void prepare (double framesPerSecond);
    void reset() noexcept;
    BeatHypothesis observe (float pBeat, float pDownbeat, float pNone) noexcept;

    /** The audio feeding this decoder has a hole in it: the FIFO overran and the
        worker never saw `lostSeconds` of input. Every interval measured across
        that hole would be wrong and the splice itself looks like an onset, so
        the beat history is dropped and the timeline is advanced to stay in step
        with real input time. The committed tempo, the regime and the metrical
        level are deliberately kept: a dropout is a reason to stop trusting the
        recent evidence, not a reason to forget the song. */
    void notifyDiscontinuity (double lostSeconds) noexcept;

    /** The input has changed character - measured on its level, before the
        analysis make-up gain erases the difference. In practice that is the
        room the app has been listening to since it was opened turning into a
        band playing.

        Everything the level sources have measured up to here describes the
        room, so their evidence starts again: the fold's buffer, the state
        space, and the beat history that was built on whatever the network made
        of an empty room. What is deliberately kept is the committed tempo and
        `established`, so the clock does not stop and the part does not drop
        out; both are wrong if the previous lock was to a room, and both are
        corrected within a couple of beats by sources that now have nothing but
        the music in them.

        This is not `notifyDiscontinuity`. There, audio was lost and the level
        evidence is still good; here no audio was lost and the level evidence is
        the thing that has gone stale. */
    void notifyInputRestart() noexcept;

    const BeatHypothesis& current() const noexcept { return hyp; }
    TempoRegime regime() const noexcept { return tempoRegime; }

    /** What the three tempo sources are each saying, and how far the level
        argument between them has got. Read by the probes and by the on-screen
        debug panel; nothing in the audio path depends on it. */
    struct Diagnostics
    {
        float combBpm = 0.0f;
        float combSalience = 0.0f;
        float longFit = 0.0f;
        float shortFit = 0.0f;
        float residual = 1.0f;
        float coverage = 0.0f;
        int   octaveMismatch = 0;
        int   beatsHeld = 0;
        /** How fast the short fit is itself moving, BPM per beat. */
        float shortFitRate = 0.0f;
        bool  levelSettled = false;
        int   userOctave = 0;
    };

    Diagnostics diagnostics() const noexcept;

private:
    /** The metrical level the *listener* wants, as octaves away from the one the
        analysis picked. Zero unless the user has asked for half or double.

        This exists because one part of the octave problem is not solvable from
        the signal. Measured on BeatNet output from a 76 BPM mix with full
        eighths, the activation half a beat from the beat stands at 0.73-0.77 of
        it, against 0.02-0.18 on the same material at 104 and 128: the eighths
        are as strong as the beats, and 152 is a defensible reading of what the
        network was given. Moving the estimator's thresholds to break the tie
        that way makes the aggregate worse, because the same asymmetry is what
        stops an ordinary rock backbeat being read in half-time. So the tie is
        broken by the person listening, in one tap, and the analysis carries on
        unchanged underneath.

        Applied to the fold's answer rather than to the reported number, so the
        decoder genuinely tracks at the chosen level: the peaks between its
        beats fall off the grid and are rejected as offbeats, exactly as they
        would be if the fold had named that level itself. Everything downstream
        - the fits, the phase, the bar - therefore needs no knowledge of it. */
public:
    /** Whether the metrical level is anchored by the state-space tracker.

        The fold and the state space fail in opposite places, measured on the
        same thirty tracks: the fold reads the eighths as the beat below about
        a hundred, the state space is dragged towards the middle of the range at
        the extremes. Anchoring keeps the fold's precision - it resolves the
        tempo far finer than a state space with whole-frame periods can - and
        takes only the octave from the state space, which is the one thing the
        fold gets wrong. Off, the decoder behaves exactly as it did. */
    void setLevelAnchor (bool on) noexcept { useAnchor = on; }
    /** Probe seam: the two numbers that decide which pulse a listener hears. */
    void setAnchorPrior (float centreBpm, float widthOctaves) noexcept
    { hmm.setPriorCentre (centreBpm); hmm.setPriorWidth (widthOctaves); }

    /** Whether the analysis is on a line feed rather than a microphone in a
        room. The two are not the same signal and the acquisition threshold is
        not the same number; see kAnchorAcquireMarginLine. */
    void setLineFeed (bool on) noexcept { lineFeed = on; }

    void setUserOctave (int octaves) noexcept;
    int  userOctave() const noexcept { return octaveShift; }

private:
    float applyUserOctave (float bpmValue) const noexcept;
    /** Moves a tempo by whole octaves to whichever one the state space is
        naming, and leaves it alone when the state space has nothing to say. */
    float foldToAnchor (float bpmValue) const noexcept;
    void  registerBeat (double beatTimeSec, float strength) noexcept;
    /** A beat time the fits are allowed to use, without a beat *event*.

        On a confirmed transition the two peaks that measured the new period are
        already behind us, and the fits need them or they have to re-form from
        scratch over eight beats of the tempo that has just been left. They must
        not be announced: `beatSerial` is what the audio thread counts strokes
        from, and a stroke played for a beat that sounded a second ago is worse
        than the fit being late. So the history gets them and the counter does
        not. */
    void  storeBeatForFit (double beatTimeSec, float strength) noexcept;
    /** The abrupt-change detector, fed every peak that clears the refractory
        and minimum-spacing checks - including the ones the on-grid gate is
        about to throw away, which on a large step is all of them.

        Returns true on the frame a change is confirmed, which is the one frame
        the caller may admit a peak the current grid rejected. */
    bool  observeTempoTransition (double eventTimeSec, float strength,
                                  bool acceptedByCurrentGrid) noexcept;
    /** Forget the candidate *and* the interval reference it was measured from.
        For the boundaries at which no interval spanning them means anything. */
    void  clearTempoTransition (TempoTransitionReason reason) noexcept;
    /** Forget the candidate only, keeping the newest peak as the reference the
        next interval is measured from. */
    void  dropTransitionCandidate (TempoTransitionReason reason) noexcept;
    /** How unsteady the intervals already accepted are, as a fraction of their
        own mean: the median absolute relative deviation of at most the newest
        eight, taken about their median rather than their mean so that one
        interval spanning a swallowed beat cannot move the reference it is being
        judged against. This is what "a changed interval" has to be measured
        against - a fifth of a percent on a line feed and several percent through
        a microphone are both ordinary, and one threshold cannot serve both. */
    float recentIntervalJitter() const noexcept;
    /** How loud the recently accepted beats are, as the median of at most the
        newest eight. A reflection clears the absolute beat threshold; what it
        does not do is stand beside the beats around it, so this is the level a
        candidate peak is held to through a microphone. */
    float recentBeatStrengthMedian() const noexcept;
    /** Whether a measured period is a tempo this grid could have moved to, as
        opposed to a subdivision, a missed beat or something off the range. */
    bool  transitionCandidateAllowed (float candidatePeriodSec) const noexcept;
    /** Fast causal acquisition from the intervals already heard. The state
        space chooses the metrical level; interpolated peaks provide the finer
        period that its whole-frame states cannot. */
    bool  tryFastAcquire() noexcept;
    /** Phase of the committed grid at `timeSec`, and where the folded
        activation says the beat actually is. See `checkGridPhase`. */
    float gridPhaseNow (float periodSec) const noexcept;
    /** The fold does not go through the on-grid gate, so it is the only thing
        that can notice a grid anchored half a beat out - which is a state that
        defends itself, because from that grid every real beat looks like a
        subdivision and is thrown away. Moves the grid when the two disagree,
        repeatedly and by more than a fifth of a beat. */
    void  checkGridPhase (float periodSec) noexcept;
    void  updateTempo() noexcept;
    float foldToPeriod (float ioiSec, float reference) const noexcept;
    /** Least squares through the newest `maxBeats` beat times. `anchorOut` is
        the other half of the line and the half the phase needs: the time the
        fit predicts for the newest beat in its own window, which is an average
        over the whole fit where a single beat time is one measurement carrying
        that one beat's whole error. */
    bool  fitPeriod (int maxBeats, float& period, float& residual, float& coverage,
                     double& anchorOut) const noexcept;
    bool  recentPeriod (float& period) const noexcept;
    void  commit (float candidateBpm, float rate) noexcept;
    float scoreConfidence() const noexcept;
    void  pushLongFit (float bpmValue) noexcept;
    bool  longFitSpread (float& spread, float& trend) const noexcept;
    void  enterRegime (TempoRegime r) noexcept;
    float pullTowardsComb (float target, bool combReady, float combBpm) const noexcept;

    static constexpr int kBeatHistory = 32;
    static constexpr int kLongFit  = 24;  // precision for a fixed tempo
    static constexpr int kShortFit = 8;   // responsiveness for a live one
    static constexpr int kRecentIoi = 3;  // detection of a change in progress

    TempoEstimator tempo;
    BeatHmm hmm;

    double fps = 50.0;
    double timeSec = 0.0;
    double lastBeatSec = -1.0;
    /** A time at which a beat of the committed grid falls, taken from the
        fit rather than from the last peak. The phase is read off this. */
    double gridAnchorSec = -1.0;
    /** Beats the fold has been saying the grid is on the wrong part of the
        beat. One is a bad fold; three in a row is a grid on the offbeat. */
    int    foldPhaseBeats = 0;
    double lastDownbeatSec = -1.0;
    float  bpm = 120.0f;
    float  beatThresh = 0.40f;
    float  downThresh = 0.40f;
    float  prevPulse = 0.0f;
    float  prevPrevPulse = 0.0f;
    float  prevDownbeat = 0.0f;
    float  prevPrevDownbeat = 0.0f;
    float  lastDownbeatStrength = 0.0f;
    /** The downbeat activation of the beat last counted, gate or no gate. See
        BeatHypothesis::beatDownbeat. */
    float  lastBeatDownbeat = 0.0f;
    int    refractoryFrames = 0;
    int    beatsInBar = 0;
    uint64_t frame = 0;
    bool   established = false;
    /** True until the long-window sources have confirmed an initial interval
        lock. It may be corrected cheaply; it must never acquire the tenure of
        a grid that has already proved itself. */
    bool   provisional = false;
    /** The current grid came from measured intervals rather than the coarse
        early HMM. Kept after promotion: a later half-tempo comb must not throw
        away a grid that still explains every measured event. */
    bool   intervalAcquired = false;
    float  provisionalStrength = 0.0f;

    double beatTime[kBeatHistory] {};
    float  beatStrength[kBeatHistory] {};
    int    beatWrite = 0;
    int    beatFilled = 0;
    uint32_t beatSerial = 0;
    uint32_t downbeatSerial = 0;
    uint32_t gridSerial = 0;

    // Regime tracking. The question "is this a record or a band" is settled on
    // the *spread* of the long fit over a window of beats, not on a run of
    // consecutive beats agreeing: on a microphone in a room a single noisy beat
    // is routine, and a counter any one of them resets never gets anywhere -
    // which is how a track cut to a click stayed in the live regime for its
    // whole length, chasing an eight-beat fit that was never still.
    static constexpr int kLongHistory = 24;
    static constexpr int kLongHistoryMin = 10;

    TempoRegime tempoRegime = TempoRegime::unknown;
    int   fastDriftBeats = 0;
    int   fastDriftLargeBeats = 0;
    int   fastDriftSign = 0;
    int   octaveMismatchBeats = 0;
    /** The level the comb was naming when the current re-anchor vote started.
        A comb that keeps changing its own mind is not evidence; only a comb
        that holds one answer while disagreeing with us gets to move the grid. */
    float octaveVoteBpm = 0.0f;
    /** Beats the current metrical level has been in use for. A level that has
        worked for a minute is not overturned as cheaply as one adopted five
        seconds ago. */
    int   beatsOnLevel = 0;
    int   octaveShift = 0;
    bool  useAnchor = false;
    bool  lineFeed = false;
    float anchorBpm = 0.0f;
    /** How clear the state space is about the level right now, 0..1, from its
        own margin over the rival metrical levels. Zero when it is not clear
        enough to be used at all. This is the only thing that can answer "is
        there a pulse, and is its level unambiguous" during the seconds before
        the fold has enough buffer to answer anything. */
    float anchorStrength = 0.0f;
    float lastFitResidual = 1.0f;
    float lastFitCoverage = 0.0f;
    float longFitBpm = 0.0f;
    float shortFitBpm = 0.0f;
    /** How fast the short fit is itself moving, in BPM per beat, smoothed.
        The gap between the two fits means one thing on a ramp and another on a
        step, and this is what separates them - see the live branch. */
    float shortFitRate = 0.0f;
    float prevShortFitBpm = 0.0f;
    /** The last recent-interval deviation measured, as a fraction of the
        committed tempo. How big a step is decides whether the beat history is
        worth keeping across it - see the exit from the fixed regime. */
    float lastFastDeviation = 0.0f;

    float longHist[kLongHistory] {};
    int   longWrite = 0;
    int   longFilled = 0;

    /** Where a fixed tempo is converging to: the running mean of the long fit
        since the tempo was called fixed. A mean over dozens of beats resolves
        the tempo far finer than any single fit, which is what "find it once and
        then refine it" actually requires - the committed tempo chases nothing,
        it settles. */
    float fixedAnchorBpm = 0.0f;
    int   fixedSamples = 0;
    int   beatsInRegime = 0;
    int   fixedErrorBeats = 0;

    // The abrupt-change detector. Everything here is a scalar with a fixed
    // lifetime: the whole point of it is to answer inside two beats, and it
    // runs beside code that must not allocate or wait.
    TempoTransitionState  transitionState  = TempoTransitionState::stable;
    TempoTransitionReason transitionReason = TempoTransitionReason::none;
    /** The two ends of the candidate: the peak the first changed interval
        started from, and the newest peak inside it. */
    double transitionFirstSec = -1.0;
    double transitionLastSec = -1.0;
    float  transitionFirstStrength = 0.0f;
    float  transitionLastStrength = 0.0f;
    float  transitionPeriodSec = 0.0f;
    float  transitionConfidence = 0.0f;
    int    transitionIntervals = 0;
    int    transitionRapidBeats = 0;
    /** Decoder-frame deadline for the rapid publication. Accepted beats can
        close it sooner, but silence and rejected peaks must not leave it armed.
        Derived at confirmation from the reported, user-octaved period. */
    double transitionRapidDeadlineSec = -1.0;
    /** Accepted fit-history beats still owed before another candidate may open.
        A confirmation empties the fit history down to the two peaks that
        measured the new period, and until a short fit has re-formed over it the
        stale fold is the strongest thing naming a tempo - which is enough to
        drag the committed BPM back and have the detector confirm the same
        change twice. See `observeTempoTransition`. */
    int    transitionRefitBeats = 0;
    uint32_t transitionSerial = 0;
    /** The newest eligible peak, whatever the grid then did with it. Intervals
        are measured from this rather than from `lastBeatSec` because on a step
        large enough to matter the grid rejects exactly the peaks that carry the
        evidence, and `lastBeatSec` stops moving at the very moment the answer
        is needed. */
    double transitionPrevEventSec = -1.0;
    float  transitionPrevStrength = 0.0f;

    BeatHypothesis hyp {};
};

} // namespace vp
