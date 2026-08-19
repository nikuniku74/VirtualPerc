#pragma once

#include "AI/AudioFifo.h"
#include "AI/BeatHypothesis.h"
#include "AI/BeatDecoder.h"
#include "AI/IBeatModel.h"
#include "AI/LogSpectFeatures.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace vp
{

class NeuralBeatTracker
{
public:
    NeuralBeatTracker() = default;
    ~NeuralBeatTracker();

    NeuralBeatTracker (const NeuralBeatTracker&) = delete;
    NeuralBeatTracker& operator= (const NeuralBeatTracker&) = delete;

    void setModel (std::unique_ptr<IBeatModel> model);
    bool start (double deviceSampleRate);
    void stop();

    void feed (const float* mono, int numSamples) noexcept;
    bool tryLoad (BeatHypothesis& out) const noexcept { return slot.load (out); }

    bool running() const noexcept { return armed.load (std::memory_order_relaxed); }
    bool usingOnnx() const noexcept { return onnxFlag.load (std::memory_order_relaxed); }

    /** Total samples handed to feed(). Subtracting a hypothesis's
        analysisSample from this gives the pipeline delay in samples, measured
        rather than assumed, so it covers the analysis window, the FIFO backlog
        and however late the worker thread happened to be scheduled. */
    int64_t samplesFed() const noexcept { return fedTotal.load (std::memory_order_relaxed); }

    /** How many times the worker found the FIFO had overrun and re-primed the
        analysis chain. Non-zero means the worker fell far enough behind to lose
        audio; the tests assert on it so a silent overrun cannot pass for a
        clean run. */
    int64_t discontinuities() const noexcept { return gapCount.load (std::memory_order_relaxed); }

private:
    void workerLoop();
    int64_t analysisSampleFor (uint64_t frameIndex) noexcept;

    AudioFifo fifo;
    LinearResampler resampler;
    LogSpectFeatures features;
    BeatDecoder decoder;
    HypothesisSlot slot;
    std::unique_ptr<IBeatModel> model;

    std::vector<float> popBuf;
    std::vector<float> resampled;
    std::thread worker;
    std::atomic<bool> stopFlag { false };
    std::atomic<bool> armed { false };
    std::atomic<bool> onnxFlag { false };
    std::atomic<int64_t> fedTotal { 0 };
    std::atomic<int64_t> gapCount { 0 };
    uint64_t seenDropped = 0;
    /** Model samples of extra priming the feature extractor has needed across
        all discontinuities. After a reset it buffers a whole frame before
        emitting again, not one hop, so without this every later frame would be
        timestamped early by that difference. */
    int64_t modelRefill = 0;
    double deviceSr = 48000.0;
    double inputSamplesPerModelSample = 1.0;
};

} // namespace vp
