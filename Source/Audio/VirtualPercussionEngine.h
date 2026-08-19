#pragma once

#include "Core/Types.h"
#include "Percussion/PercussionEngine.h"
#include "Tracking/BeatTracker.h"
#include "Stretch/StretchFactor.h"
#include "Stretch/TimeStretchEngine.h"

#include <atomic>
#include <vector>

namespace vp
{

class VirtualPercussionEngine
{
public:
    void prepare (double sampleRate, int maxBlock, int numInputChannels) noexcept;
    void reset() noexcept;

    void start() noexcept;
    void stop() noexcept;
    void tap() noexcept;
    void tapAt (double timeSeconds) noexcept;

    EngineSettings& settings() noexcept { return cfg; }
    const EngineSettings& settings() const noexcept { return cfg; }

    void setReportedLatencyMs (float ms) noexcept { latencyMs.store (ms, std::memory_order_relaxed); }

    void process (const float* const* inputs, int numInputs,
                  float* const* outputs, int numOutputs,
                  int numSamples) noexcept;

    EngineSnapshot snapshot() const noexcept;
    int shakerHits() const noexcept { return percussion.hitsFired(); }
    TrackingState state() const noexcept { return tracker.state(); }

    void setClickInjectBpm (float bpm) noexcept { clickBpm.store (bpm, std::memory_order_relaxed); }
    void setClickInjectEnabled (bool on) noexcept { clickEnabled.store (on, std::memory_order_relaxed); }

    bool tryLoadNeuralHypothesis (BeatHypothesis& out) const noexcept;

    void loadPercussionLoop (const float* left, const float* right, int frames, float nativeBpm);
    void clearPercussionLoop();

private:
    void mixInputs (const float* const* inputs, int numInputs, int numSamples) noexcept;
    void maybeInjectClick (int numSamples) noexcept;
    void subtractSpeakerLeak (int numSamples) noexcept;
    void pushOutputToRing (int numSamples) noexcept;
    void applyAnalysisMakeup (int numSamples, float rawPeak) noexcept;

    BeatTracker tracker;
    PercussionEngine percussion;
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
    std::atomic<float> lastLeadMs { 0.0f };
    std::atomic<int>   lastRegime { 0 };

    std::atomic<float> clickBpm { 120.0f };
    std::atomic<bool>  clickEnabled { false };
    double clickPhase = 0.0;
    float leakLp = 0.0f;
    float peakEnv = 0.0f;
    int ringWrite = 0;
    static constexpr int ringSize = 32768;

    static constexpr int tapQSize = 16;
    std::atomic<unsigned int> tapWrite { 0 };
    unsigned int tapRead = 0;
    double tapTimes[tapQSize] {};
};

} // namespace vp
