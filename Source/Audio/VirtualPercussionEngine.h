#pragma once

#include "Core/Types.h"
#include "Percussion/PercussionEngine.h"
#include "Percussion/StyleDetector.h"
#include "Tracking/BeatTracker.h"
#include "Stretch/StretchFactor.h"
#include "Stretch/TimeStretchEngine.h"

#include <atomic>
#include <memory>
#include <vector>

namespace vp
{

class VirtualPercussionEngine
{
public:
    /** Test seam - see BeatTracker::setBeatModel. Call before prepare(). */
    void setBeatModel (std::unique_ptr<IBeatModel> model) { tracker.setBeatModel (std::move (model)); }

    void prepare (double sampleRate, int maxBlock, int numInputChannels) noexcept;
    void reset() noexcept;

    void start() noexcept;
    void stop() noexcept;
    void tap() noexcept;
    void tapAt (double timeSeconds) noexcept;

    EngineSettings& settings() noexcept { return cfg; }
    const EngineSettings& settings() const noexcept { return cfg; }

    void setReportedLatencyMs (float ms) noexcept { latencyMs.store (ms, std::memory_order_relaxed); }

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

    bool tryLoadNeuralHypothesis (BeatHypothesis& out) const noexcept;

    /** Loop-kit playback (see docs/ARCHITECTURE.md). Nothing calls these yet.
        Both allocate, so they may only be called while the device is closed -
        that is, before prepare() or after releaseResources(). Calling either
        one while the audio callback is running reallocates the buffers the
        stretcher is reading from. If this is ever wired to a control the user
        can touch mid-song, it needs a handoff rather than a direct load. */
    void loadPercussionLoop (const float* left, const float* right, int frames, float nativeBpm);
    void clearPercussionLoop();

private:
    void processBlock (const float* const* inputs, int numInputs,
                       float* const* outputs, int numOutputs,
                       int numSamples) noexcept;
    void mixInputs (const float* const* inputs, int numInputs, int numSamples) noexcept;
    void maybeInjectClick (int numSamples) noexcept;
    void subtractSpeakerLeak (int numSamples) noexcept;
    void pushOutputToRing (int numSamples) noexcept;
    void applyAnalysisMakeup (int numSamples, float rawPeak) noexcept;

    BeatTracker tracker;
    PercussionEngine percussion;
    StyleDetector styleDetector;
    StretchFactor stretch;
    TimeStretchEngine stretcher;
    EngineSettings cfg;

    std::vector<float> mono;
    std::vector<float> outL;
    std::vector<float> outR;
    std::vector<float> clickScratch;
    std::vector<float> leakScratch;
    std::vector<float> outRing;

    double sampleRate = 48000.0;
    int maxBlock = 1024;
    int preparedInputs = 2;

    std::atomic<float> latencyMs { 0.0f };
    std::atomic<float> lastBpm { 0.0f };
    std::atomic<float> lastTarget { 0.0f };
    std::atomic<float> lastConf { 0.0f };
    std::atomic<float> lastBeat { 0.0f };
    std::atomic<float> lastBar { 0.0f };
    std::atomic<bool>  lastBarDeclared { false };
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
    /** Input samples that arrived non-finite and were replaced with silence.
        Never zero on a healthy device; worth showing, because a driver that
        does this is a driver whose other numbers are also suspect. */
    std::atomic<int>   badInputSamples { 0 };
    std::atomic<int>   lastGaps { 0 };
    std::atomic<int>   lastBacklog { 0 };
    std::atomic<float> lastLeadMs { 0.0f };
    std::atomic<int>   lastRegime { 0 };
    std::atomic<float> lastCombBpm { 0.0f };
    std::atomic<bool>  lastLevelSettled { false };
    int seenBarNudge = 0;

    std::atomic<int>   lastStyle { 0 };
    std::atomic<float> lastStyleConf { 0.0f };
    std::atomic<float> lastStyleEvenKick { 0.0f };
    std::atomic<float> lastStyleBackbeat { 0.0f };
    std::atomic<float> lastStyleOffHigh { 0.0f };
    std::atomic<float> lastStyleSync { 0.0f };
    std::atomic<float> lastStyleOccupancy { 0.0f };
    std::atomic<int>   lastHits { 0 };
    std::atomic<float> lastAttackLeadMs { 0.0f };

    std::atomic<float> clickBpm { 120.0f };
    std::atomic<bool>  clickEnabled { false };
    double clickPhase = 0.0;
    float leakLp = 0.0f;
    float peakEnv = 0.0f;
    float makeupGain = 1.0f;
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
