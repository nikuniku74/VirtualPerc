#pragma once

#include "AI/BeatHypothesis.h"
#include "AI/NeuralBeatTracker.h"
#include "AI/IBeatModel.h"
#include "Tracking/TempoFollower.h"

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
    void setSpeakerFollow (bool on) noexcept { speakerFollow = on; }
    void setReportedLatencyMs (float ms) noexcept { reportedLatencyMs = ms; }

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
    };

    void start() noexcept;
    void stop() noexcept;
    void tap (double timeSeconds) noexcept;

    Output process (const float* mono, int numSamples) noexcept;

    TrackingState state() const noexcept { return currentState; }
    bool tryLoadHypothesis (BeatHypothesis& out) const noexcept { return neural.tryLoad (out); }

private:
    void updateState (float confidence, bool hadBeat, bool loudEnough, bool periodic) noexcept;
    void alignBarFromVotes (bool comingIn) noexcept;
    int  pulsesFor (Subdivision s) const noexcept;

    NeuralBeatTracker neural;
    TempoFollower     follower;

    TrackingState currentState = TrackingState::idle;
    Subdivision userSubdivision = Subdivision::autoDetect;
    Subdivision effectiveSubdivision = Subdivision::sixteenth;

    double sampleRate = 48000.0;
    uint32_t lastBeatSerial = 0;
    uint32_t lastDownbeatSerial = 0;
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
    bool lockedOnce = false;
    bool retuning = false;
    bool tapHold = false;
    bool tapAligned = false;
    bool tapEstablished = false;
    bool waitForQuantize = false;
    bool heardMusic = false;
    bool hadPlayed = false;
    bool needsResync = false;
    bool waitForSongBeat = false;
    int tapHoldSamples = 0;
    int lostSyncSamples = 0;
    int downbeatHoldSamples = 0;
    int downbeatVotes[4] {};
    int quantizeWaitSamples = 0;
    double lastTapSec = -1.0;
    float tapIoi[8] {};
    int tapIoiWrite = 0;
    int tapIoiFilled = 0;
};

} // namespace vp
