#pragma once

#include "Core/Types.h"
#include "Loops/HybridPercussionRenderer.h"
#include "Loops/LoopBank.h"
#include "Percussion/BandDynamics.h"
#include "Percussion/PercussionEngine.h"
#include "Percussion/StyleDetector.h"
#include "Tracking/BeatTracker.h"
#include "Audio/LatencyProbe.h"
#include "Tracking/HarmonicChange.h"
#include "Tracking/KickOnsetDetector.h"
#include "Stretch/StretchFactor.h"
#include "Stretch/TimeStretchEngine.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace vp
{

class VirtualPercussionEngine
{
public:
    /** Test seam - see BeatTracker::setBeatModel. Call before prepare(). */
    void setBeatModel (std::unique_ptr<IBeatModel> model) { tracker.setBeatModel (std::move (model)); }

    void prepare (double sampleRate, int maxBlock, int numInputChannels) noexcept;
    /** True when prepare() has already run for this clock and analysis is still
        alive. A Split View resize re-calls prepareToPlay at the same rate;
        running prepare() again zeros the leak canceller and BeatTracker::reset,
        which is the click. */
    bool isPreparedFor (double sampleRate) const noexcept;
    /** Stop background analysis after the audio device has been closed.
        prepare() starts it again when the device returns. */
    void suspendAnalysis();

    /** Input samples fed to the analysis but not yet analysed, live. Safe from
        any thread; see BeatTracker::analysisBacklog for what it is for. */
    int analysisBacklog() const noexcept { return tracker.analysisBacklog(); }
    int64_t analysisCompletedSamples() const noexcept
    {
        return tracker.analysisCompletedSamples();
    }
    uint32_t hypothesisPublicationSequence() const noexcept
    {
        return tracker.hypothesisPublicationSequence();
    }
    void reset() noexcept;

    void start() noexcept;
    void stop() noexcept;
    void tap() noexcept;
    void tapAt (double timeSeconds) noexcept;

    /** SEGUI (true, default) vs FISSO. Turning follow off freezes the BPM
        currently showing; turning it on lets BeatNet drive again. */
    void setTempoFollow (bool follow) noexcept;
    /** Lock a BPM and switch to FISSO. Tap and later nudges still update it. */
    void setFixedBpm (float bpm) noexcept;

    /** The backing track jumped to a new position. Opens the bar re-entry
        window (item 2) so the one can catch the new downbeats; does *not*
        restart the tempo decoder - the clock kept time. Call from the
        message thread on seek. */
    void notifyTrackSeek() noexcept;

    EngineSettings& settings() noexcept { return cfg; }
    const EngineSettings& settings() const noexcept { return cfg; }

    void setReportedLatencyMs (float ms) noexcept { latencyMs.store (ms, std::memory_order_relaxed); }

    /** Measure this rig's round trip instead of taking the device's word for
        it: a short sweep goes out, whatever comes back is captured, and the two
        are correlated. See Audio/LatencyProbe.h for why the reported figure is
        not the same quantity.

        `startLatencyMeasurement` arms it and returns at once. When
        `latencyMeasurementReady` goes true, call `finishLatencyMeasurement`
        from a thread that is not the audio thread: it does the correlation and
        returns the round trip in milliseconds, or a negative number when the
        sweep did not come back clearly enough to believe. A believed result is
        kept and is what the clock runs on from then on. */
    void  startLatencyMeasurement() noexcept { latencyProbe.start(); }
    bool  latencyMeasurementRunning() const noexcept { return latencyProbe.isRunning(); }
    bool  latencyMeasurementReady() const noexcept { return latencyProbe.ready(); }
    float finishLatencyMeasurement() noexcept;
    void  cancelLatencyMeasurement() noexcept { latencyProbe.cancel(); }
    /** The measured round trip, or 0 when nobody has measured one. */
    float measuredLatency() const noexcept { return measuredLatencyMs.load (std::memory_order_relaxed); }
    /** Restore a figure measured in an earlier session. */
    void  setMeasuredLatency (float ms) noexcept
    {
        measuredLatencyMs.store (ms > 0.0f && ms < 400.0f ? ms : 0.0f, std::memory_order_relaxed);
    }

    /** Accepts any block length. Anything longer than the size prepare() was
        given is split - never truncated: a truncated block leaves the tail of
        the host's output buffer holding whatever was in it, which is a burst of
        noise at full scale, and drops the input it should have analysed. */
    void process (const float* const* inputs, int numInputs,
                  float* const* outputs, int numOutputs,
                  int numSamples) noexcept;

    EngineSnapshot snapshot() const noexcept;
    int shakerHits() const noexcept { return lastHits.load (std::memory_order_relaxed); }
    TrackingState state() const noexcept
    { return static_cast<TrackingState> (lastState.load (std::memory_order_relaxed)); }

    void setClickInjectBpm (float bpm) noexcept { clickBpm.store (bpm, std::memory_order_relaxed); }
    void setClickInjectEnabled (bool on) noexcept { clickEnabled.store (on, std::memory_order_relaxed); }

    /** Test-only cancellation A/B seam. Set before processing begins; production
        leaves this enabled. */
    void setLeakCancellationEnabledForTest (bool on) noexcept
    {
        leakCancellationEnabledForTest = on;
    }

    bool tryLoadNeuralHypothesis (BeatHypothesis& out) const noexcept;

    /** Loop-kit playback (see docs/ARCHITECTURE.md). Nothing calls these yet.
        Both allocate, so they may only be called while the device is closed -
        that is, before prepare() or after releaseResources(). Calling either
        one while the audio callback is running reallocates the buffers the
        stretcher is reading from. If this is ever wired to a control the user
        can touch mid-song, it needs a handoff rather than a direct load. */
    void loadPercussionLoop (const float* left, const float* right, int frames, float nativeBpm);
    void clearPercussionLoop();

    /** The recorded percussionist (docs/RECORDED_LOOPS.md).

        `loadLoopBank` reads a manifest and every file it names into memory and
        hands the result to the hybrid renderer. It allocates and it does file
        I/O, so like `loadPercussionLoop` it may only be called while the device
        is closed - before prepare() or after releaseResources(). It returns
        false and fills `error` when the bank is not usable, and in that case
        nothing changes: a bank is all there or it is not used.

        With `VP_ENABLE_RECORDED_LOOPS` off - the default - loading a bank is
        allowed and changes nothing: the render path is the one it always was.
        The build flag is the switch; `setRecordedLoopsEnabled` is how the app
        turns it off again at runtime once the build has turned it on. */
    bool loadLoopBank (const std::string& manifestPath, std::string& error);
    /** The iPad build embeds the same manifest and WAV bytes in BinaryData.
        This overload keeps JUCE out of the core while avoiding fragile bundle
        paths. Like the file overload, call it only while the device is closed. */
    bool loadLoopBankFromMemory (const std::string& manifestText,
                                 const LoopBank::AudioLoader& loader,
                                 std::string& error);
    void clearLoopBank();
    void setRecordedLoopsEnabled (bool on) noexcept { hybrid.setEnabled (on); }
    bool recordedLoopsEnabled() const noexcept { return hybrid.isEnabled(); }
    /** True when a recording is what is currently being heard. */
    bool recordedLoopPlaying() const noexcept { return hybrid.loopIsPlaying(); }

private:
    /** Bands the leak canceller fits our own output in, split at ~250 Hz and
        ~1.4 kHz.
        Two was not enough, and which voice it was not enough for is the point.
        The split used to be one filter at 1.5 kHz, which puts the shaker
        entirely in the upper band and the congas entirely in the lower one -
        measured on eight bars of marcha, 9.5 dB above the line against 13.6 dB
        below it. A tablet speaker has no low end, so what reaches the
        microphone is the conga with everything under a couple of hundred hertz
        taken out, while the reference still has all of it: one gain across
        0-1.5 kHz cannot say "none of this and all of that", so it fits a
        compromise that under-subtracts the body and over-subtracts the
        fundamental. Measured on the one-wall room fixture, funk at eighths, the
        share of our own return removed was 30.4% with the shaker alone and
        6.0% with the congas alone, and the congas' worst block came out at
        1.84 of the input peak - the canceller adding more than the leak it was
        removing. A third band at the speaker's own roll-off lets the fit put
        that gain near zero where nothing arrives and a real one where the body
        does. See `VPTests --leak`, rows `leak-voice`.
        Three is still a piecewise-constant approximation of a room; the honest
        ceiling is a short adaptive FIR, and the residuals in `leak-room` say
        how far that would have to go. */
    static constexpr int kLeakBands = 3;

    void processBlock (const float* const* inputs, int numInputs,
                       float* const* outputs, int numOutputs,
                       int numSamples) noexcept;
    void mixInputs (const float* const* inputs, int numInputs, int numSamples) noexcept;
    void maybeInjectClick (int numSamples) noexcept;
    void subtractSpeakerLeak (int numSamples, bool speaker) noexcept;
    /** Everything the leak canceller has learned about the return path. A new
        audio-device session is a new path - and `prepare()` clears the reference
        ring the evidence was measured against, so keeping the evidence would
        mean fitting a signal that is no longer there. Called from both
        `prepare()` and `reset()`; it touches nothing that `prepare()` sets up. */
    void resetLeakEstimate() noexcept;
    /** The twelve scalars that say what level this input arrives at: the
        make-up envelope and gain, the epoch watcher's level and own-output
        trackers with their timers, and the epoch count. The same lifecycle
        argument as `resetLeakEstimate()` above, and called from the same two
        places for the same reason - a new session is a new input, possibly a
        line-level feed replaced by a microphone in a room. It touches no
        buffer and no prepared resource. */
    void resetAnalysisLevelState() noexcept;
    /** The public mirrors of three of those scalars - the epoch count, the
        make-up gain and the analysis peak - which the audio callback is the only
        writer of. Cleared at the same two lifecycle boundaries and for the same
        reason as `lastHypValid` beside them: between `prepare()` and the first
        block of the new session there is no live reading, and the last session's
        is not one. */
    void clearAnalysisLevelMirrors() noexcept;
    void updateLeakDelay (int numSamples, bool speaker) noexcept;
    void applyAnalysisHpf (int numSamples) noexcept;
    /** Store the part as it actually leaves the outputs, for the leak canceller
        to subtract and for the level watcher to blame us with. `master` is part
        of what the speaker emits, so it is part of what comes back on the
        microphone. */
    void pushOutputToRing (int numSamples, float master) noexcept;
    void applyAnalysisMakeup (int numSamples, float rawPeak, bool levelJumped) noexcept;
    /** Watches the analysis level *before* the make-up gain and counts the
        moments the input changes character - an empty room becoming a band.
        The counter is what the neural worker is told; see
        BeatDecoder::notifyInputRestart for why it needs telling. Returns true
        on the block the change is called, so the make-up gain can be re-primed
        at the new level instead of gliding to it. */
    bool updateAnalysisEpoch (int numSamples, float rawPeak, bool rhythmArrived) noexcept;
    /** Share of the un-amplified analysis signal that sits below 200 Hz, and
        the step up in it that says a rhythm section just walked in. Audio
        thread, before the make-up gain: a broadband gain leaves the share
        alone, but everything else here reads a level, and a level after the
        make-up is the network's operating point rather than the room's. */
    bool updateRhythmShare (int numSamples) noexcept;
    /** A two-quarter (or longer) hole in the *analysis* level, then music
        again, while already following: open the bar re-entry window. Not an
        epoch - the decoder must not restart. Audio thread. */
    void maybeDetectBarReentry (int numSamples, float rawPeak) noexcept;

    BeatTracker tracker;
    PercussionEngine percussion;
    /** The recorded percussionist and the bank it plays out of. Both exist
        whatever `VP_ENABLE_RECORDED_LOOPS` says - they are compiled and tested
        in every build - but with the flag off nothing in `processBlock` calls
        them and the part is `PercussionEngine`'s exactly as before. */
    HybridPercussionRenderer hybrid;
    std::unique_ptr<LoopBank> loopBank;
    StyleDetector styleDetector;
    StretchFactor stretch;
    TimeStretchEngine stretcher;
    EngineSettings cfg;

    std::vector<float> mono;
    std::vector<float> outL;
    std::vector<float> outR;
    std::vector<float> clickScratch;
    /** Our own output split into bands at the accepted delay, one block long.
        Sized in prepare(): a fixed stack buffer used to cap this at 2048
        samples, which left the tail of a larger block un-subtracted, and the
        step at the splice point is a textbook onset. */
    std::vector<float> leakBand[kLeakBands];
    std::vector<float> outRing;

    double sampleRate = 48000.0;
    int maxBlock = 1024;
    int preparedInputs = 2;
    bool analysisSuspended = false;

    std::atomic<float> latencyMs { 0.0f };
    std::atomic<float> lastBpm { 0.0f };
    std::atomic<float> lastTarget { 0.0f };
    std::atomic<float> lastConf { 0.0f };
    std::atomic<float> lastBeat { 0.0f };
    std::atomic<float> lastBar { 0.0f };
    std::atomic<bool>  lastBarDeclared { false };
    std::atomic<bool>  lastBarLocked { false };
    std::atomic<float> lastPeak { 0.0f };
    std::atomic<float> lastCallbackMs { 0.0f };
    std::atomic<int>   lastState { 0 };
    std::atomic<int>   lastSub { 0 };
    std::atomic<int>   lastBuffer { 0 };
    std::atomic<int>   lastBeats { 0 };
    std::atomic<bool>  lastAudible { false };
    std::atomic<bool>  lastTapLock { false };
    std::atomic<int>   lastFollowBar { 0 };
    std::atomic<int>   lastVoices { 0 };
    std::atomic<double> lastSr { 0.0 };
    std::atomic<bool>  lastAiOnnx { false };
    std::atomic<bool>  lastHypValid { false };
    std::atomic<float> lastNeuralBpm { 0.0f };
    std::atomic<float> lastPBeat { 0.0f };
    std::atomic<float> lastAnalysisPeak { 0.0f };
    std::atomic<float> lastAnalysisGain { 1.0f };
    std::atomic<float> lastLeakRemain { 0.0f };
    /** Input samples that arrived non-finite and were replaced with silence.
        Never zero on a healthy device; worth showing, because a driver that
        does this is a driver whose other numbers are also suspect. */
    std::atomic<int>   badInputSamples { 0 };
    std::atomic<int>   lastGaps { 0 };
    /** The bar rotations the automatic alignment has made, and how much the
        analysis is being believed. Diagnostics; see BeatTracker::Output. */
    std::atomic<int>   lastBarRotations { 0 };
    /** The kick channel, when the listener has assigned one. The detector runs
        on the raw channel before any of the analysis conditioning: the leak
        canceller and the high-pass exist to protect a *microphone* from the
        app's own output, and a desk send of the kick has neither problem. */
    KickOnsetDetector  kickDetector;
    std::vector<float> kickScratch;
    std::atomic<int>   lastKickChannel { -1 };
    std::atomic<float> lastKickLevel { 0.0f };
    std::atomic<float> lastKickQuiet { 0.0f };
    std::atomic<int>   lastKickOnsets { 0 };
    std::atomic<bool>  lastKickTrusted { false };
    std::atomic<bool>  lastDrumsOut { false };
    /** The rig's own round trip, measured rather than reported. See
        Audio/LatencyProbe.h. Zero means nobody has measured it and the
        device's own figure is what the clock is running on. */
    LatencyProbe       latencyProbe;
    /** The input as the device delivered it, before the leak subtraction, the
        rumble high-pass and the make-up gain. The latency probe has to hear its
        own sweep come back, and the leak canceller's whole job is removing the
        app's own output from what the analysis sees - so a probe fed the
        conditioned bus would be listening for something already subtracted. */
    std::vector<float> rawIn;
    std::atomic<float> measuredLatencyMs { 0.0f };
    /** How much the band is giving, and whether the part has stood down. The
        stand-down is taken at a bar line, never in the middle of a figure. */
    /** When the harmony moves. Runs on the analysis bus, which is where the
        band is and where our own part has already been taken out. */
    HarmonicChange     harmony;
    std::atomic<int>   lastHarmonicChanges { 0 };
    std::atomic<bool>  lastBarFromHarmony { false };
    std::atomic<float> lastHarmonyMargin { 0.0f };
    std::atomic<float> lastHarmonicShare { 0.0f };
    BandDynamics       bandDynamics;
    bool               standingDown = false;
    bool               wantStandDown = false;
    std::atomic<float> lastDynamics { 1.0f };
    std::atomic<bool>  lastStandingDown { false };
    /** Section boundaries found, and where the eight-bar sentence has got to. */
    int                sectionCount = 0;
    std::atomic<int>   lastSections { 0 };
    std::atomic<int>   lastPhraseBar { 0 };
    std::atomic<float> lastEvidenceTrust { 1.0f };
    std::atomic<float> lastGridTauSec { 0.0f };
    std::atomic<int>   lastBacklog { 0 };
    std::atomic<float> lastLeadMs { 0.0f };
    std::atomic<int>   lastRegime { 0 };
    std::atomic<int>   lastOctave { 0 };
    std::atomic<float> lastCombBpm { 0.0f };
    std::atomic<bool>  lastLevelSettled { false };
    std::atomic<float> lastFitResidual { 1.0f };
    std::atomic<float> lastFitCoverage { 0.0f };
    std::atomic<int>   lastTempoTransitionState { 0 };
    std::atomic<int>   lastTempoTransitionReason { 0 };
    std::atomic<float> lastTempoTransitionBpm { 0.0f };
    std::atomic<float> lastTempoTransitionConfidence { 0.0f };
    std::atomic<int>   lastTempoTransitionIntervals { 0 };
    int seenBarNudge = 0;

    std::atomic<int>   lastStyle { 0 };
    std::atomic<float> lastStyleConf { 0.0f };
    std::atomic<float> lastStyleEvenKick { 0.0f };
    std::atomic<float> lastStyleBackbeat { 0.0f };
    std::atomic<float> lastStyleOffHigh { 0.0f };
    std::atomic<float> lastStyleSync { 0.0f };
    std::atomic<float> lastStyleOccupancy { 0.0f };
    std::atomic<int>   lastHits { 0 };
    std::atomic<bool>  lastLoopPlaying { false };
    std::atomic<float> lastLoopPhaseMs { 0.0f };
    std::atomic<int>   lastHandovers { 0 };
    std::atomic<float> lastAttackLeadMs { 0.0f };

    std::atomic<float> clickBpm { 120.0f };
    std::atomic<bool>  clickEnabled { false };
    double clickPhase = 0.0;
    /** The two one-poles that split our own output into kLeakBands. State, not
        scratch: they carry across blocks, and they are deliberately *not*
        cleared when the accepted delay moves - see subtractSpeakerLeak. */
    float leakLpLow = 0.0f;
    float leakLp = 0.0f;
    float analysisHp = 0.0f;
    int leakDelaySamples = 0;
    int leakScanCountdown = 0;
    bool leakDelayLocked = false;
    /** How much of our own output the input is carrying back, fitted per band
        and held across blocks. Signed, and clamped only where it is used: see
        subtractSpeakerLeak. */
    float leakGain[kLeakBands] = {};
    /** The normal equations that fit is solved from, accumulated over about
        half a second of causal history instead of over the current block. A
        block of 256 samples is a noisy estimate and a block whose reference is
        silent is no estimate at all; both used to reach the gain anyway,
        through a smoother that could not tell a measurement from an absence of
        one. Our own bands against the input, and the Gram matrix of the bands
        against each other. */
    double leakFitXy[kLeakBands] = {};
    double leakFitGram[kLeakBands][kLeakBands] = {};
    /** The delay all of that evidence was measured at. When the accepted delay
        moves the reference is a different signal and the cross-products stop
        describing anything, so they are dropped. */
    int leakFitDelay = -1;
    bool leakCancellationEnabledForTest = true;
    float peakEnv = 0.0f;
    float makeupGain = 1.0f;
    /** Pre-make-up level, and the level the current run of analysis evidence
        started at. See updateAnalysisEpoch. */
    float levelFast = 0.0f;
    float levelRef = 0.0f;
    /** How loud this input gets when something is happening, remembered for
        about a minute. What makes "quiet" mean quiet rather than merely
        quieter. */
    float levelLoud = 0.0f;
    int   levelStepSamples = 0;
    int   levelPrimeSamples = 0;
    /** Two-pole 200 Hz state, the two smoothed energies it feeds, the plateau
        the share has been sitting in, and how long it has been above it. */
    float lowLp1 = 0.0f, lowLp2 = 0.0f;
    float lowEnergy = 0.0f, fullEnergy = 0.0f;
    float shareBase = 0.0f;
    int   shareStepSamples = 0;
    int   sharePrimeSamples = 0;
    int   shareHighSamples = 0;
    /** Latched: this input has been heard to have a rhythm section in it. */
    bool  rhythmSeen = false;
    std::atomic<float> lastLowShare { 0.0f };
    std::atomic<uint32_t> analysisEpoch { 0 };
    // Audio-thread event kind, retained until the next epoch. Continuous
    // arrangement changes preserve both comb history and network state.
    bool preserveCombOnEpoch = false;
    /** Message-thread seek (and the audio-thread hole detector) set this;
        process() turns it into BeatTracker::notifyBarReentry. */
    std::atomic<bool> barReentryPending { false };
    int   musicGapSamples = 0;
    bool  musicGapArmed = false;
    std::atomic<bool> lastBarTrusted { false };
    std::atomic<bool> lastBarReentry { false };
    /** The same envelope on our *own* output, and how long our own part is
        still answerable for a rise on the input. What we play comes back on the
        microphone, the canceller does not always find it, and a level that rose
        because we started playing is not the room turning into a band. */
    std::atomic<uint32_t> lastRestarts { 0 };
    float ownPeakLast = 0.0f;
    float ownFast = 0.0f;
    float ownRef = 0.0f;
    int   ownStepSamples = 0;
    int ringWrite = 0;
    /** Power of two: the wrap is a mask, not a divide. */
    static constexpr int ringSize = 32768;
    static_assert ((ringSize & (ringSize - 1)) == 0, "ringSize must be a power of two");

    /** Channels the block splitter can carry offset pointers for. Devices have
        one or two; anything past this is silenced rather than left as it was. */
    static constexpr int kMaxSplitChannels = 16;

    static constexpr int tapQSize = 16;
    std::atomic<unsigned int> tapWrite { 0 };
    unsigned int tapRead = 0;
    double tapTimes[tapQSize] {};
};

} // namespace vp
