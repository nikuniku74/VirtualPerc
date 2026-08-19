#pragma once

#include "AI/BeatHypothesis.h"
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
    void setUserOctave (int octaves) noexcept;
    int  userOctave() const noexcept { return octaveShift; }

private:
    float applyUserOctave (float bpmValue) const noexcept;
    void  registerBeat (double beatTimeSec) noexcept;
    void  updateTempo() noexcept;
    float foldToPeriod (float ioiSec, float reference) const noexcept;
    bool  fitPeriod (int maxBeats, float& period, float& residual, float& coverage) const noexcept;
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

    double fps = 50.0;
    double timeSec = 0.0;
    double lastBeatSec = -1.0;
    double lastDownbeatSec = -1.0;
    float  bpm = 120.0f;
    float  beatThresh = 0.40f;
    float  downThresh = 0.40f;
    float  prevPulse = 0.0f;
    float  prevPrevPulse = 0.0f;
    float  prevDownbeat = 0.0f;
    int    refractoryFrames = 0;
    int    beatsInBar = 0;
    uint64_t frame = 0;
    bool   established = false;

    double beatTime[kBeatHistory] {};
    int    beatWrite = 0;
    int    beatFilled = 0;
    uint32_t beatSerial = 0;
    uint32_t downbeatSerial = 0;

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
    float lastFitResidual = 1.0f;
    float lastFitCoverage = 0.0f;
    float longFitBpm = 0.0f;
    float shortFitBpm = 0.0f;

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

    BeatHypothesis hyp {};
};

} // namespace vp
