#pragma once

#include "AI/BeatHypothesis.h"
#include "AI/NeuralBeatTracker.h"
#include "AI/IBeatModel.h"
#include "Tracking/TempoFollower.h"

#include <cstdint>
#include <memory>

namespace vp
{

class BeatTracker
{
public:
    /** Test seam: supply the model the neural worker will run, instead of
        letting it load the bundled BeatNet. Must be called before prepare(),
        which is what starts the worker. Nothing in the app calls this; it is
        how the tests get a network whose output they choose, which is the only
        way to test what the tracker does with a *particular* activation - a
        stray downbeat, say - rather than with whatever the real one emits. */
    void setBeatModel (std::unique_ptr<IBeatModel> model);

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setFollowStrength (FollowStrength s) noexcept;
    void setSubdivisionOverride (Subdivision s) noexcept;
    void setSpeakerFollow (bool on) noexcept
    {
        speakerFollow = on;
        neural.setLineFeed (! on);
    }
    /** SEGUI vs FISSO. Default true. Switching back to follow hands the tempo
        to the analysis again; a count-in that is still in progress is not
        cleared by calling this with the value already in force. */
    void setTempoFollow (bool on) noexcept;
    /** Apply a listener-set BPM. `generation` must change for the value to
        take; the same stamp is ignored so a tap can keep moving the tempo
        without the control fighting it every block. */
    void setUserTempo (float bpm, uint32_t generation) noexcept;
    /** Half or double, and whether the app chooses it.

        AUTO keeps the pulse the part is played on inside the range a
        percussionist counts in, which is the one thing that can be said about
        the metrical level without solving it - see the note on
        BeatDecoder::userOctave for why the signal does not settle it. A manual
        choice switches AUTO off and stands. */
    void setTempoOctave (int octaves) noexcept;
    void setTempoOctaveAuto (bool on) noexcept;

    /** Move the bar on by whole beats, because the listener says so. Clears the
        tally and holds the automatic alignment off for a while afterwards: the
        evidence that produced the current bar is exactly the evidence that
        has just been overruled, so leaving it in place would let it undo the
        correction within a phrase. */
    void nudgeBar (int beats) noexcept;

    void setReportedLatencyMs (float ms) noexcept { reportedLatencyMs = ms; }

    /** The analysis input has changed character - an empty room becoming a band
        playing. Counted rather than flagged; see
        BeatDecoder::notifyInputRestart.

        The tracker keeps one bit of its own from this: whether it has *ever*
        seen the input change since the app was opened. Until it has, a tempo it
        has found may be the room's - measured, an empty room reaches FOLLOWING
        at 99 BPM with a confidence of 0.91 - and the percussion is held out. */
    void setInputEpoch (uint32_t epoch) noexcept
    {
        if (seenEpoch && epoch != lastInputEpoch)
            sawInputStart = true;
        lastInputEpoch = epoch;
        seenEpoch = true;
        neural.setInputEpoch (epoch);
    }

    struct Output
    {
        ClockTick     clock;
        TrackingState state = TrackingState::idle;
        Subdivision   subdivision = Subdivision::sixteenth;
        /** Resolution the clock is emitting pulses at, for the groove. */
        int           clockPulsesPerBeat = 4;
        float         bpm = 0.0f;
        float         targetBpm = 0.0f;
        float         confidence = 0.0f;
        float         beatPhase = 0.0f;
        float         barPhase = 0.0f;
        bool          percussionShouldPlay = false;
        bool          tapLocked = false;
        FollowBar     followBar = FollowBar::ready;
        int           beatsElapsed = 0;
        bool          aiOnnx = false;
        bool          hypValid = false;
        float         neuralBpm = 0.0f;
        float         pBeat = 0.0f;
        /** Measured analysis-plus-output delay the clock is running ahead by. */
        float         leadMs = 0.0f;
        TempoRegime   regime = TempoRegime::unknown;
        float         combBpm = 0.0f;
        bool          levelSettled = false;
        float         fitResidual = 1.0f;
        float         fitCoverage = 0.0f;
        /** True for a moment after a tap has declared where beat one is, so the
            UI can show that the gesture landed rather than leaving the player
            guessing whether the bar moved because of them. */
        bool          barDeclared = false;
        /** Times the analysis lost audio because the worker fell behind, and
            how many loops that worker has run. Both are diagnostics, and both
            are worth having: a gap costs the recent evidence, and a worker
            spinning is a battery draining. */
        int64_t       analysisGaps = 0;
        int64_t       analysisWakeups = 0;
        /** Input samples fed but not yet analysed. */
        int           analysisBacklog = 0;
        /** The metrical level actually in force, whether AUTO or the listener
            picked it. Reported rather than assumed, so the screen can say what
            the part is being played at rather than what was asked for. */
        int           tempoOctave = 0;
    };

    void start() noexcept;
    void stop() noexcept;
    void tap (double timeSeconds) noexcept;

    Output process (const float* mono, int numSamples) noexcept;

    /** Input samples fed to the analysis but not yet analysed, read live rather
        than as of the last `process`. The probes wait on this to make a run
        repeatable: otherwise how far behind the worker happens to be is decided
        by the host's scheduler, and the same build measures differently from
        one run to the next. */
    int analysisBacklog() const noexcept { return neural.backlog(); }

    TrackingState state() const noexcept { return currentState; }
    bool tryLoadHypothesis (BeatHypothesis& out) const noexcept { return neural.tryLoad (out); }

private:
    void updateState (float confidence, bool hadBeat, bool loudEnough, bool periodic) noexcept;
    void alignBarFromVotes (bool comingIn) noexcept;
    void updateAutoOctave (float bpm, bool periodic, int numSamples) noexcept;
    void holdBarDecision() noexcept;
    int  pulsesFor (Subdivision s) const noexcept;

    NeuralBeatTracker neural;
    TempoFollower     follower;

    TrackingState currentState = TrackingState::idle;
    Subdivision userSubdivision = Subdivision::autoDetect;
    Subdivision effectiveSubdivision = Subdivision::sixteenth;

    double sampleRate = 48000.0;
    uint32_t lastBeatSerial = 0;
    uint32_t lastDownbeatSerial = 0;
    uint32_t lastGridSerial = 0;
    bool seenSerials = false;
    float lastLeadMs = 0.0f;
    int samplesSinceBeat = 0;
    int lockHoldSamples = 0;
    int lowHoldSamples = 0;
    int listeningSamples = 0;
    int beatCount = 0;
    float smoothedConf = 0.0f;
    float heldBpm = 0.0f;
    float reportedLatencyMs = 0.0f;
    float inputPeakEnv = 0.0f;
    int quietSamples = 0;
    int gridMuteSamples = 0;
    int ghostLockSamples = 0;
    bool armed = true;
    bool speakerFollow = false;
    bool tempoFollow = true;
    uint32_t userTempoGen = ~0u;
    bool lockedOnce = false;
    bool tapHold = false;
    bool tapAligned = false;
    bool tapEstablished = false;
    /** Whether the input has changed character since the app was opened, and
        the last value the engine reported. See setInputEpoch. */
    bool sawInputStart = false;
    bool seenEpoch = false;
    uint32_t lastInputEpoch = 0;
    bool waitForQuantize = false;
    bool heardMusic = false;
    bool hadPlayed = false;
    bool needsResync = false;
    bool waitForSongBeat = false;
    /** Whether a stroke actually came out last block. Nothing on the grid means
        nothing to disturb by moving it, which is the difference between placing
        the clock and having to lean it into place. */
    bool sounding = false;
    /** The level the listener chose, the level AUTO has settled on, the level it
        is arguing for, and how long it has been arguing. */
    int  userOctave = 0;
    bool octaveAuto = true;
    int  autoOctave = 0;
    int  autoWant = 0;
    int  autoHoldSamples = 0;
    int tapHoldSamples = 0;
    int downbeatHoldSamples = 0;
    float downbeatVotes[4] {};
    int   barDeclaredSamples = 0;

    int quantizeWaitSamples = 0;
    double lastTapSec = -1.0;
    float tapIoi[8] {};
    int tapIoiWrite = 0;
    int tapIoiFilled = 0;
};

} // namespace vp
