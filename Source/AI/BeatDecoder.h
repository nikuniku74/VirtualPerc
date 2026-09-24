#pragma once

#include "AI/BeatHypothesis.h"
#include "AI/BeatHmm.h"
#include "AI/TempoEstimator.h"
#include "AI/TempoMotionTracker.h"

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
    /** `lowBand` is the mean of the low bands of the same feature frame the
        three probabilities came from - see LogSpectFeatures::lowBandEnergy. It
        is the only thing here that can tell a kick from a hi-hat, which is what
        the metrical level turns on; the probabilities cannot. It defaults to
        zero for the callers that feed activations directly (tests, probes):
        with no band energy the level test simply never fires, which is the
        behaviour those callers had before it existed.

        `highBand` is the mean of the top 16 log-mag bands of that same frame
        (LogSpectFeatures::highBandEnergy, index 120..135). Same contract:
        default 0, and the hats-only eighth fold stays shut when it is 0.
        Probes do not pass it. */
    BeatHypothesis observe (float pBeat, float pDownbeat, float pNone,
                            float lowBand = 0.0f,
                            float highBand = 0.0f) noexcept;

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
        of an empty room. The last BPM value is kept, but `established` is
        cleared so acquisition can choose a new grid. The downstream clock
        continues while new evidence arrives.

        This is not `notifyDiscontinuity`. There, audio was lost and the level
        evidence is still good; here no audio was lost and the level evidence is
        the thing that has gone stale. */
    // preserveComb is only for a continuous arrangement acquiring a rhythm
    // section, never for a new source or an ordinary quiet-to-loud restart.
    void notifyInputRestart (bool preserveComb = false) noexcept;

    const BeatHypothesis& current() const noexcept { return hyp; }
    TempoRegime regime() const noexcept { return tempoRegime; }

    /** What the three tempo sources are each saying, and how far the level
        argument between them has got. Read by the probes and by the on-screen
        debug panel; nothing in the audio path depends on it. */
    struct Diagnostics
    {
        float combBpm = 0.0f;
        float combSalience = 0.0f;
        bool  combReady = false;
        float longFit = 0.0f;
        float shortFit = 0.0f;
        float residual = 1.0f;
        float coverage = 0.0f;
        int   octaveMismatch = 0;
        int   beatsHeld = 0;
        /** How fast the short fit is itself moving, BPM per beat. */
        float shortFitRate = 0.0f;
        /** Diagnostic quadratic fit over the responsive window, evaluated at
            its newest accepted beat. Unlike `shortFit`, which is the average
            slope through the window, this asks what the local slope would be
            if the beat dates really form a smooth accelerando. It never owns
            the target tempo. The global fixed/motion/step population rejected
            using this evidence to release a stale fixed-tempo hold, so these
            fields remain diagnostic only. */
        float motionFit = 0.0f;
        float motionFitRate = 0.0f;
        float motionFitResidual = 1.0f;
        float motionFitImprovement = 0.0f;
        int   motionFitEvidence = 0;
        int   motionFitDirection = 0;
        /** Scalar shadow diagnostics. Shape fields below explain the separate
            bounded authority reported by `motionBridgeAuthority`. */
        float motionShadowBpm = 0.0f;
        float motionShadowPeriodDelta = 0.0f;
        float motionShadowUncertainty = 1.0f;
        float motionShadowAuthority = 0.0f;
        int   motionShadowState = static_cast<int> (TempoMotionShadowState::idle);
        int   motionShadowVeto = static_cast<int> (TempoMotionVeto::none);
        bool  motionFirstStrictProof = false;
        int   motionShapeModel = static_cast<int> (TempoMotionShapeModel::insufficient);
        float motionShapeBpm = 0.0f;
        float motionShapeQuadraticVsHinge = 0.0f;
        float motionShapeEvidenceMargin = 0.0f;
        int   motionShapeQuadraticWins = 0;
        int   motionShapeQuarantineBeats = 0;
        float motionBridgeAuthority = 0.0f;
        bool  levelSettled = false;
        int   userOctave = 0;
        /** Median grid-index step of the fitted beats: 1 on a grid at the pulse,
            2 on one an octave too fast. See `fitPeriod`. */
        float fitIndexGap = 1.0f;
        /** Accepted beats spent in the current regime. Diagnostic; the
            unknown commit waits for `kLongFit` of these before applying
            the live lead. */
        int   beatsInRegime = 0;
        /** Eight- and four-beat lines indexed on the median of the last three
            raw intervals instead of the committed period. Probe-only: they
            never own the target. The committed-grid fit cannot name a tempo
            the grid has already left (0.28-beat index gate); these ask whether
            the same accepted times would, under a current guess. */
        float recentIoiBpm = 0.0f;
        float ioiIndexedFit = 0.0f;
        float ioiIndexedResidual = 1.0f;
        float ioiIndexedFit4 = 0.0f;
        float ioiIndexedResidual4 = 1.0f;
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
    /** Whether a part is sounding right now. The octave snap holds the
        metrical level (`levelHeldWhilePlaying`); the on-grid gate holds the
        committed pulse through a hats-only hole, re-opens for a crest on
        that fold that is not a half-beat off lastBeat, and
        `checkGridPhase` does not slide the origin while sounding. A
        file-feed kick after a hat-class lastBeat may steal that half
        (FEEL hats-then-Q); mute when lowBand is 0. */
    void setSounding (bool on) noexcept { sounding = on; }

    /** The listener pressed "L'1 è QUI". The one is the nearest beat
        already on the grid, not this sample: moving the anchor onto the
        sample publishes phase 0 from mid-beat and the clock spends that
        as a rate bend. Does not move the lattice, the fits, or the tempo.
        Hats after a hole still cannot steal lastBeat. */
    void declarePulseHere() noexcept;

    void setUserOctave (int octaves) noexcept;
    int  userOctave() const noexcept { return octaveShift; }

private:
    float applyUserOctave (float bpmValue) const noexcept;
    /** Moves a tempo by whole octaves to whichever one the state space is
        naming, and leaves it alone when the state space has nothing to say. */
    float foldToAnchor (float bpmValue) const noexcept;
    void  registerBeat (double beatTimeSec, float strength,
                        float lowBand = 0.0f,
                        float highBand = 0.0f) noexcept;
    /** True once a kick-body beat has been accepted and a later crest
        arrives more than one beat after it with the body still gone.
        Hats between kicks (one beat) stay on the ordinary path. */
    bool  kitBodyHolding (double nowSec) const noexcept;
    /** Use repeated downbeat spacing to distinguish a 50 BPM quarter from its
        100 BPM hi-hat eighths: three true downbeats contain two complete
        intervals, each eight accepted fast-grid beats long instead of four.
        Called only on beats whose downbeat activation crosses `downThresh`.

        This is what `VPTests` "a 50 BPM bar makes its 100 BPM hi-hat pulse count
        as eighths" exercises, and it passes there. On the real network it has
        never been measured to fire at all: the fixture gives it a clean 0.90
        downbeat spike on the one and 0.02 everywhere else, where the real
        network at 50 BPM crosses the threshold far too rarely and spreads its
        downbeat mass over beat one *and* beat three. See
        docs/HANDOFF_OCTAVE_50BPM.md before building on it. */
    void  observeDownbeatCadence() noexcept;
    /** Distinguish a 50 BPM quarter from its 100 BPM hi-hat eighths by how much
        body each accepted beat has, over an assumed eight-slot bar. Neither the
        beat curve nor the downbeat curve can make this choice - both were tried
        and measured; docs/HANDOFF_OCTAVE_50BPM.md has the numbers and why. What
        separates the two readings is that at the wrong level every other
        accepted beat is a hi-hat with nothing underneath it. */
    void  observeMetricalCadence (double eventTimeSec, float lowBand) noexcept;
    /** A beat time the fits are allowed to use, without a beat *event*.

        On a confirmed transition the two peaks that measured the new period are
        already behind us, and the fits need them or they have to re-form from
        scratch over eight beats of the tempo that has just been left. They must
        not be announced: `beatSerial` is what the audio thread counts strokes
        from, and a stroke played for a beat that sounded a second ago is worse
        than the fit being late. So the history gets them and the counter does
        not. */
    void  storeBeatForFit (double beatTimeSec, float strength,
                           float lowBand = 0.0f,
                           float highBand = 0.0f) noexcept;
    /** The abrupt-change detector, fed every peak that clears the refractory
        and minimum-spacing checks - including the ones the on-grid gate is
        about to throw away, which on a large step is all of them.

        Returns true on the frame a change is confirmed, which is the one frame
        the caller may admit a peak the current grid rejected. */
    bool  observeTempoTransition (double eventTimeSec, float strength,
                                  bool acceptedByCurrentGrid) noexcept;
    /** Direct-feed step detector on accepted quarters: the newest two to four
        beats against the line fitted through the beats before them. Publishes
        through the same confirmed transition as `observeTempoTransition`. */
    bool  observeGridStep() noexcept;
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
    /** How deeply the accepted beats alternate loud/quiet, 0 when they are all
        the same weight and approaching 1 when every other one is nothing.
        A grid sitting on a filled subdivision - a hi-hat on the eighths under a
        kit playing quarters - alternates; a grid on the pulse does not, and
        that difference is the only thing that separates the two once the
        subdivision is dense enough to make `coverage`, the residual and the
        index gap all look perfect. See `unprovenSlowerOctave`. */
    float recentStrengthAlternation() const noexcept;
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
    void  updateMotionShadow() noexcept;
    void  refreshMotionBridgeAuthority() noexcept;
    void  resetMotionShadow (bool full, TempoMotionVeto reason) noexcept;
    float bridgedMotionTarget (float ordinaryTarget) const noexcept;
    void  updateTempo() noexcept;
    float foldToPeriod (float ioiSec, float reference) const noexcept;
    /** Direct-feed unknown: the committed pulse is the one the on-grid
        gate is still admitting, the comb names a different tempo in the
        stale-grid band, and both line fits still agree they are looking at
        the same (wrong) pulse. Then the comb period is the ruler for peak
        admission and for indexing the line, not a tempo target. After
        `kLongFit` the stale-grid floor drops to ~3.2% and `lastBeat` is
        taken from the comb fold before the keep tightens below the pulse
        split; without that origin a tighter keep rejects the true peak
        too. Live is excluded: that is where a confirmed step rebuilds,
        and the ruler there moved the offset-0 gradino hash. */
    bool  stalePulseCombRuler() const noexcept;
    double pulseIndexGuess() const noexcept;
    /** Peak/fit keep on the comb ruler when the two pulses sit closer
        than `kCombRulerTolerance`. Negative when the ordinary 0.12 keep
        already splits them, or the ruler is off. */
    double stalePulseKeep (double rulerPeriod) const noexcept;
    /** Slide `lastBeat` / `gridAnchorSec` onto the comb fold so the
        tighter keep is measured from the true pulse, not the stale one.
        Dumps beat history; leaves the last short/long readings so this
        frame's peak gate still sees the ruler. */
    void  snapStalePulseToCombFold() noexcept;
    /** Least squares through the newest `maxBeats` beat times. `anchorOut` is
        the other half of the line and the half the phase needs: the time the
        fit predicts for the newest beat in its own window, which is an average
        over the whole fit where a single beat time is one measurement carrying
        that one beat's whole error. */
    /** `indexGapOut`, when asked for, is the median step between the grid
        indices the kept beats landed on: 1 when the committed grid is the pulse
        the beats are actually at, 2 when it is an octave too fast. It is the
        half of the picture `coverage` cannot supply - see the comment in the
        body. */
    bool  fitPeriod (int maxBeats, float& period, float& residual, float& coverage,
                     double& anchorOut, float* indexGapOut = nullptr) const noexcept;
    bool  fitPeriodBefore (int maxBeats, float& period, float& residual, float& coverage,
                           double& anchorOut, float* indexGapOut, int skipNewest,
                           double guessPeriod = 0.0) const noexcept;
    bool  fitPeriodCurve (int maxBeats, float& periodNow, float& bpmPerBeat,
                          float& residual, float& improvement) const noexcept;
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
    /** Low-band of the last accepted peak, same three-frame window as
        cadence. Zero on the click bank. The sounding hat-to-kick half
        steal reads this and must stay mute when it is zero. */
    float  lastAcceptedLowBand = 0.0f;
    /** A beat whose low band cleared the kick-body mute. Zero on the
        click bank and the motion matrix, which never pass lowBand, so
        the drum-pause hold below cannot arm there. */
    bool   kitBodyHeard = false;
    double kitBodyLastSec = -1.0;
    /** Established tempo, and the crest is a hat: high band present,
        low band under the kick mute. The counted grid is held. Probes
        pass highBand 0, so this stays shut there. */
    bool   hatGridHolding = false;
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
    /** The low-band energy of the last two frames, so the value taken at a beat
        can be the largest of the three around it - the same three-frame window
        the downbeat confidence uses, and for the same reason: the peak lands
        between frames as often as on one. */
    float  prevLowBand = 0.0f;
    float  prevPrevLowBand = 0.0f;
    /** Same three-frame window for the top-octave bands. Default 0, so a
        caller that never passes `highBand` cannot open the hats-only fold. */
    float  prevHighBand = 0.0f;
    float  prevPrevHighBand = 0.0f;
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
    /** Low-band energy at the accepted peak, same three-frame window as
        cadence. Zero on the synthetic click bank and every probe that
        omits `observe`'s fourth argument. Kick-vs-hat acquire fold
        reads this and must stay mute when it is zero. */
    float  beatLowBand[kBeatHistory] {};
    /** Top-octave energy at the accepted peak, same three-frame max as
        `beatLowBand`. Zero on the click bank and every probe: `observe`'s
        fifth argument defaults to 0 and `storeBeatForFit` defaults to 0.
        The hats-only eighth fold reads this and stays mute at 0. */
    float  beatHighBand[kBeatHistory] {};
    int    beatWrite = 0;
    int    beatFilled = 0;
    uint32_t beatSerial = 0;
    uint32_t downbeatSerial = 0;
    uint32_t gridSerial = 0;

    /** Direct/file-feed bar-cadence evidence: a decayed downbeat-confidence
        histogram over eight slots spanning an assumed bar, indexed by elapsed
        time since `cadenceAnchorSec` divided by the current period - not by
        counting accepted beats, because a beat the network's own gate missed
        (measured: it happens even on a clean line feed) would then shift every
        bin after it, permanently, for no musical reason. A grid already at the
        right level puts its downbeat on the same two bins, four apart, forever
        - the mod-8 index cannot tell "bar 1" from "bar 3". A grid running one
        octave too fast (hi-hat eighths read as the beat) puts it on only one
        of the eight, because the true bar is eight accepted beats long, not
        four. That asymmetry, built up continuously rather than counted past a
        threshold, is what `observeDownbeatCadence` reads the level from. */
    float    cadenceHist[8] {};
    /** Decayed count of accepted beats behind `cadenceHist`, in the same units
        BeatTracker::tryAlignFrom's evidence gate uses - enough for the shares
        above to mean something before either octave decision is trusted. */
    float    cadenceHistBeats = 0.0f;
    /** Where slot 0 of `cadenceHist` sits in time. Arbitrary - it is whichever
        accepted beat happened to be first after the histogram was last reset -
        but fixed, so the bin a given beat falls into does not depend on how
        many beats before it were missed. */
    double   cadenceAnchorSec = -1.0;
    /** The downbeat-spacing counter `observeDownbeatCadence` keeps: which beat
        the last threshold-crossing downbeat landed on, and how many consecutive
        eight-beat bars have been seen since. Two are required, so one downbeat
        the network missed on an ordinary track cannot move the level. */
    uint32_t cadenceDownbeatBeatSerial = 0;
    int      cadenceOctaveCandidate = 0;
    int      cadenceOctaveVotes = 0;
    int      metricalOctaveHint = 0;
    bool     metricalOctaveHintValid = false;

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
    /** How many consecutive beats the comb has named the slower octave at
        high salience. Proves the slower level on a full mix, where the
        strength alternation that lifts the veto on synthetic kit material is
        washed out. See `unprovenSlowerOctave` in BeatDecoder.cpp. */
    int   combHalfBeats = 0;
    /** The level the comb was naming when the current re-anchor vote started.
        A comb that keeps changing its own mind is not evidence; only a comb
        that holds one answer while disagreeing with us gets to move the grid. */
    float octaveVoteBpm = 0.0f;
    /** Beats the current metrical level has been in use for. A level that has
        worked for a minute is not overturned as cheaply as one adopted five
        seconds ago. */
    int   beatsOnLevel = 0;
    /** A grid at the wrong *rate* is stable the same way a grid on the wrong
        half-beat is (see checkGridPhase): the real beats fall off it and are
        rejected, the ones that survive fit it cleanly, and residual and
        coverage both look healthy. The comb is the one thing outside that loop,
        and in the stuck state it sits a steady few percent away from the
        committed tempo instead of on it - too little for the octave snap, which
        is looking for a metrical level, and forever. See docs/TODO.md item 19. */
    int   staleGridBeats = 0;
    /** The comb level this staleness vote is for, same reason as
        `octaveVoteBpm`: a comb changing its own mind is not evidence. */
    float staleGridBpm = 0.0f;
    int   octaveShift = 0;
    bool  useAnchor = false;
    bool  lineFeed = false;
    bool  sounding = false;
    float anchorBpm = 0.0f;
    /** How clear the state space is about the level right now, 0..1, from its
        own margin over the rival metrical levels. Zero when it is not clear
        enough to be used at all. This is the only thing that can answer "is
        there a pulse, and is its level unambiguous" during the seconds before
        the fold has enough buffer to answer anything. */
    float anchorStrength = 0.0f;
    /** The twenty-four beat window is lying across a tempo event: the two
        fits disagree on the tempo and the short one fits far better. */
    bool  longWindowStraddles = false;
    double straddleSinceSec = -1.0;
    double anchorBlend = 0.0;
    float lastFitResidual = 1.0f;
    float lastFitCoverage = 0.0f;
    /** Median step between grid indices of the fitted beats: 1 on a grid at the
        pulse, 2 on one an octave too fast. */
    float lastFitIndexGap = 1.0f;
    float longFitBpm = 0.0f;
    float shortFitBpm = 0.0f;
    float shortFitResidual = 1.0f;
    /** How fast the short fit is itself moving, in BPM per beat, smoothed.
        The gap between the two fits means one thing on a ramp and another on a
        step, and this is what separates them - see the live branch. */
    float shortFitRate = 0.0f;
    float prevShortFitBpm = 0.0f;
    /** The last responsive-fit and raw recent-interval deviations, as fractions
        of the committed tempo. The first releases a clean direct-feed hold; the
        second proves that its direction is causal rather than a phase offset. */
    float lastFastDeviation = 0.0f;
    float lastIntervalDeviation = 0.0f;
    /** A line-feed-only constant-acceleration fit over sixteen accepted beats.
        Kept as diagnostic evidence: selectors fast enough to release FISSO on
        all clean ramps also fired on globally varied fixed-tempo material. */
    float motionFitBpm = 0.0f;
    float motionFitRate = 0.0f;
    float motionFitResidual = 1.0f;
    float motionFitImprovement = 0.0f;
    int   motionFitEvidence = 0;
    int   motionFitDirection = 0;
    TempoMotionTracker motionTracker;
    TempoMotionOutput motionShadow {};
    uint32_t motionObservedBeatSerial = 0;
    float motionBridgeAuthority = 0.0f;
    float motionBridgeAnchorBpm = 0.0f;
    bool  ioiClockLead = false;
    int   ioiClockLeadBeats = 0;
    /** Set only when the four-term long-fit phase gate opens. Cleared
        when fast motion is no longer current, including a beat train that
        `resetMotionShadow` has already dropped `ioiClockLead` from, and
        on `declarePulseHere` (that command sets lastBeat to now, so fast
        motion would otherwise keep the pre-button period). Does not arm
        `ioiLead` or the 0.01 s tau. */
    bool  longFitPeriodHeld = false;
    // Sounding stale-grid reopen: a leftover lattice after a pause can
    // look like a causal step (100→150) while the comb still names the
    // held tempo. Remember the reopen so that confirmation can refuse a
    // huge post-hole jump. The synthetic bank never sets sounding.
    double postHoleReopenSec = -1.0;
    // Door D (live) and unknown Door B: keep aiming at that 4-beat
    // for kShortFit, including kit-gap !haveShort frames.
    float ioiTargetHoldBpm = 0.0f;
    int   ioiTargetHoldBeats = 0;

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
    /** Consecutive slightly-strained 8-beat fits this FISSO tenure. Used
        to release a direct feed one vote early on a weak linear ramp without
        walking the published BPM. */
    int   fixedWalkRun = 0;
    /** Previous beat's IOI-indexed 4-beat, when it already sat on a
        step the 8-beat had not taken. Zero unless that beat passed. */
    float stepFourHoldBpm = 0.0f;
    float stepFourStraddleHoldBpm = 0.0f;
    /** Previous live beat's IOI-indexed 4-beat, when it had already
        left the 8-beat by more than 8% and the newest interval agreed.
        Zero unless that beat passed. Confirmed on the next beat. */
    float liveFourHoldBpm = 0.0f;
    /** Beats left to keep a declared tempo if a hold was armed. "L'1 è QUI"
        no longer wipes the fits or moves the lattice, so it does not arm
        this. A hold that is armed still restores `bpm` and the fixed
        anchor at the end of `updateTempo`. */
    int   barTempoHoldBeats = 0;
    int   beatsInRegime = 0;
    int   fixedErrorBeats = 0;
    /** Beats of catch-up still owed after leaving FISSO. The number held
        there is stale by definition - that is why the regime was left - so
        the first bar back is spent at the acquiring rate. */
    int   leftFixedBeats = 0;

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
    /** Accepted fit-history beats still owed before another candidate may open
        or, on a direct feed, the comb may pull the measured rate. A confirmation
        empties the fit history down to the two peaks that measured the new
        period. The short fit reforms first; the four-second autocorrelation
        remains stale for a few more beats and otherwise drags the committed BPM
        back. See `observeTempoTransition` and the live branch in `updateTempo`. */
    int    transitionRefitBeats = 0;
    uint32_t transitionSerial = 0;
    /** The newest eligible peak, whatever the grid then did with it. Intervals
        are measured from this rather than from `lastBeatSec` because on a step
        large enough to matter the grid rejects exactly the peaks that carry the
        evidence, and `lastBeatSec` stops moving at the very moment the answer
        is needed. */
    double transitionPrevEventSec = -1.0;
    float  transitionPrevStrength = 0.0f;
    /** Newest eligible peak the grid rejected at beat strength. Vetoes
        `observeGridStep` over the quarters after it. */
    double strongOffGridPeakSec = -1.0;

    BeatHypothesis hyp {};
};

} // namespace vp
