#pragma once

#include "AI/BeatHypothesis.h"
#include "AI/NeuralBeatTracker.h"
#include "AI/IBeatModel.h"
#include "Tracking/PhaseTrust.h"
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
        tally and locks the bar: the evidence that produced the current bar is
        exactly the evidence that has just been overruled, so leaving the
        automatic alignment free to act on it would undo the correction within a
        phrase. */
    void nudgeBar (int beats) noexcept;

    /** Whether the count is the listener's to move and nobody else's.

        Locked, `alignBarFromVotes` does nothing at all: the histogram keeps
        being built, because it costs nothing and is right again the moment the
        lock comes off, but it never rotates anything. Moving the one and a tap
        that declares it both set this; only the listener clears it.

        It does not freeze the count against the *grid*. When the clock
        re-places its phase over a beat boundary the count still goes with it -
        that is what keeps a locked bar on the beat of the song it was locked
        to, instead of drifting a quarter away from it. */
    void setBarLocked (bool on) noexcept { barLocked = on; }
    bool barIsLocked() const noexcept { return barLocked; }


    void setReportedLatencyMs (float ms) noexcept { reportedLatencyMs = ms; }

    /** A kick strike, timed on the audio of a channel that carries only the
        kick drum. `sampleOffset` is relative to the start of the block about to
        be processed and may be negative; `strength` is 0..1.

        Call before `process` for the block the onset was found in. What it buys
        is precision, not a new reference: the absolute calibration of the clock
        - the pipeline delay, the device round trip, the network's own response
        offset - is measured on the neural path and is left exactly as it is.
        This removes the +/-10 ms of frame quantisation on top of it, which is
        the largest single term left in the phase once everything else is right.
        See Tracking/KickOnsetDetector.h. */
    void notifyKickOnset (int sampleOffset, float strength) noexcept;

    /** The harmony has moved. `sampleOffset` is relative to the start of the
        block about to be processed and may be negative; `strength` is 0..1.

        Used for one thing: which quarter is the one. Harmony changes on bar
        lines, and on material with no drums in it that is the only cue there
        is - measured on a drum-free arrangement, every single change landed on
        the downbeat, where the network's own vote through a speaker is no
        better than a coin. On a full mix it is a lean rather than an answer and
        is weighted as one. See Tracking/HarmonicChange.h. */
    void notifyHarmonicChange (int sampleOffset, float strength) noexcept;

    /** How long the kick channel has been silent, and whether one is assigned
        at all. A negative value means none is, and everything the kick path
        does is then switched off. */
    void setKickChannelState (bool assigned, float quietSeconds) noexcept;

    /** Whether the kick onsets are landing where the clock says beats are, and
        are therefore worth believing. False when no channel is assigned, and
        also when one is assigned but is not a kick - somebody routing the app
        the full mix by mistake is the case this catches, and it costs nothing
        to catch it. */
    bool kickIsTrusted() const noexcept { return kickTrusted; }

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
        /** And whether the count is now the listener's. Reported rather than
            assumed, because a tap sets it as well as the on-screen control, and
            the screen has to say so either way. */
        bool          barLocked = false;
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
        /** How well the analysis is fitting compared with how well it has been
            fitting on this song, 1 down to 0.3, and the constant the clock is
            therefore averaging its phase over. Diagnostics: together they say
            whether a passage the listener can hear going soft is one the clock
            has noticed. */
        float         evidenceTrust = 1.0f;
        float         gridTauSec = 0.0f;
        /** The kick channel: strikes counted since the tracker was reset,
            whether they are being believed, and whether the channel says the
            drummer has stopped. */
        int           kickOnsets = 0;
        bool          kickTrusted = false;
        bool          drumsOut = false;
        /** Harmonic changes counted, and whether the bar is currently being
            placed from them rather than from the network. */
        int           harmonicChanges = 0;
        bool          barFromHarmony = false;
        /** How clear the harmony's histogram is: the winner's share of it less
            the runner-up's. Near one is a chord on every bar line; near zero is
            material the chroma has nothing to say about. */
        float         harmonyMargin = 0.0f;
        /** How many times the automatic alignment has rotated the bar since the
            tracker was reset. Diagnostic, and the only way from outside to know
            that a rotation has just happened - which is the moment the count is
            held still, and therefore the moment worth asking what else that
            hold is stopping. */
        int           barRotations = 0;
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
    /** One source's opinion about the bar, acted on or not. Returns true when
        it moved the count, so the caller can stop asking. */
    bool tryAlignFrom (const float* votes, float evidence, bool comingIn,
                       float extraMargin) noexcept;
    void updateAutoOctave (float bpm, bool periodic, int numSamples) noexcept;
    void holdBarDecision() noexcept;
    int  pulsesFor (Subdivision s) const noexcept;

    void updateKickTrust (float phaseErr) noexcept;

    NeuralBeatTracker neural;
    TempoFollower     follower;
    EvidenceTrust     evidence;

    /** The kick channel's state, as the audio thread last reported it. */
    bool  kickAssigned = false;
    float kickQuietSec = 0.0f;
    int   kickOnsetCount = 0;
    bool  kickTrusted = false;
    /** A running share of kick strikes that landed near a beat of the clock.
        A channel that is not a kick fails this immediately and permanently;
        one that is passes it inside a bar. */
    float kickOnGrid = 0.0f;
    float kickSeen = 0.0f;
    /** Pending onsets for the block about to be processed. */
    static constexpr int kMaxPendingKicks = 8;
    int   pendingKicks = 0;
    int   pendingKickOffset[kMaxPendingKicks] {};
    float pendingKickStrength[kMaxPendingKicks] {};

    TrackingState currentState = TrackingState::idle;
    Subdivision userSubdivision = Subdivision::autoDetect;
    Subdivision effectiveSubdivision = Subdivision::sixteenth;

    double sampleRate = 48000.0;
    uint32_t lastBeatSerial = 0;
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
    bool barLocked = false;
    /** The bar as a histogram over its four quarters: how much downbeat the
        network has found on each, decayed, and how many beats have gone into
        it. The count is what says whether the shares mean anything yet, and it
        is kept separately because activation and evidence are different
        quantities - a network that is loud about every beat has said nothing. */
    float downbeatVotes[4] {};
    float voteBeats = 0.0f;
    int   barRotations = 0;

    /** The bar as the harmony votes on it, one vote per chord change, decayed
        the same way the network's is. Kept apart from the network's histogram
        rather than added into it: they are not the same quality of evidence -
        on material with no drums the harmony is the only evidence there is -
        and mixing them would hide which one answered. */
    float harmonyVotes[4] {};
    float harmonyVoteCount = 0.0f;
    int   harmonicChangeCount = 0;
    bool  barFromHarmony = false;
    static constexpr int kMaxPendingHarmony = 4;
    int   pendingHarmony = 0;
    int   pendingHarmonyOffset[kMaxPendingHarmony] {};
    float pendingHarmonyStrength[kMaxPendingHarmony] {};
    int   barDeclaredSamples = 0;

    int quantizeWaitSamples = 0;
    double lastTapSec = -1.0;
    float tapIoi[8] {};
    int tapIoiWrite = 0;
    int tapIoiFilled = 0;
};

} // namespace vp
