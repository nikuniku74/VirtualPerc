#include "AI/NeuralBeatTracker.h"

#include "AI/BeatModelConfig.h"
#include "AI/ModelLocator.h"
#include "AI/OnnxBeatModel.h"
#include "AI/StubBeatModel.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace vp
{

NeuralBeatTracker::~NeuralBeatTracker()
{
    stop();
}

void NeuralBeatTracker::setModel (std::unique_ptr<IBeatModel> m)
{
    stop();
    model = std::move (m);
}

bool NeuralBeatTracker::start (double deviceSampleRate)
{
    stop();
    deviceSr = deviceSampleRate > 1.0 ? deviceSampleRate : 48000.0;

    fifo.prepare (1 << 19);
    resampler.prepare (deviceSr, kBeatModelSampleRate);
    features.prepare (kBeatModelSampleRate, kBeatModelHop);
    decoder.prepare (kBeatModelSampleRate / static_cast<double> (kBeatModelHop));
    // The metrical level comes from the state-space tracker. See
    // BeatDecoder::setLevelAnchor for what that buys and what it costs.
    decoder.setLevelAnchor (true);
    inputSamplesPerModelSample = deviceSr / kBeatModelSampleRate;
    hopInDeviceSamples = static_cast<int> (std::ceil (kBeatModelHop * inputSamplesPerModelSample));
    fedTotal.store (0, std::memory_order_relaxed);
    completedTotal.store (0, std::memory_order_relaxed);
    gapCount.store (0, std::memory_order_relaxed);
    wakeCount.store (0, std::memory_order_relaxed);
    seenDropped = 0;
    modelRefill = 0;
    inputEpoch.store (0, std::memory_order_relaxed);
    seenInputEpoch = 0;
    minimumAnalysisSample.store (0, std::memory_order_relaxed);

    popBuf.assign (4096, 0.0f);
    resampled.assign (4096, 0.0f);

    stopFlag.store (false, std::memory_order_relaxed);
    armed.store (true, std::memory_order_release);
    // Building the network reads the weights and hands them to the runtime to
    // compile, which takes long enough that Xcode flags it: `start` is reached
    // from prepareToPlay, and on iOS that runs on the thread which draws the
    // UI, so opening the audio device froze the interface for as long as the
    // model took. The worker does it on its own first pass instead - it is the
    // only thread that ever touches the model afterwards - and the loop it
    // then enters already knows how to have no model yet.
    worker = std::thread ([this] { workerLoop(); });
    return true;
}

void NeuralBeatTracker::stop()
{
    armed.store (false, std::memory_order_relaxed);
    stopFlag.store (true, std::memory_order_relaxed);
    if (worker.joinable())
        worker.join();
    // Lifecycle-thread exception to the worker-only slot writer rule. `armed`
    // is already false and join proves no publication can overlap this clear.
    slot.clearWhenWriterStopped();
    fifo.reset();
}

void NeuralBeatTracker::feed (const float* mono, int numSamples) noexcept
{
    if (! armed.load (std::memory_order_relaxed) || mono == nullptr || numSamples <= 0)
        return;
    fifo.push (mono, numSamples);
    fedTotal.fetch_add (numSamples, std::memory_order_relaxed);
}

void NeuralBeatTracker::invalidatePublicationsBeforeNow() noexcept
{
    minimumAnalysisSample.store (samplesFed(), std::memory_order_release);
}

bool NeuralBeatTracker::tryLoad (BeatHypothesis& out) const noexcept
{
    const int64_t before = minimumAnalysisSample.load (std::memory_order_acquire);
    BeatHypothesis staging;
    if (! slot.load (staging))
        return false;

    // A reset may race the bounded slot copy. Validate the floor on both sides
    // so a payload accepted against the old floor cannot escape afterwards.
    const int64_t after = minimumAnalysisSample.load (std::memory_order_acquire);
    if (after != before || staging.analysisSample < after)
        return false;

    out = staging;
    return true;
}

void NeuralBeatTracker::workerLoop()
{
    float frame[LogSpectFeatures::kDim];
    float act[3] {};

    // Built here rather than in `start` so that opening the audio device never
    // blocks the thread drawing the UI. Until this returns the loop below runs
    // with no model and publishes no hypothesis, which is the state it is in
    // anyway until the first hop of audio has arrived. Audio fed meanwhile is
    // waiting in the FIFO, which holds eleven seconds.
    if (! model)
    {
        auto onnx = std::make_unique<OnnxBeatModel>();
        if (loadDefaultBeatModel (*onnx))
            model = std::move (onnx);
        else
            model = std::make_unique<StubBeatModel>();
    }
    if (! model->prepare (LogSpectFeatures::kDim))
    {
        // A network that will not build is not a reason to stop listening: on
        // the stub the tap and the clock still work, and the UI already says
        // which of the two is running.
        model = std::make_unique<StubBeatModel>();
        if (! model->prepare (LogSpectFeatures::kDim))
            model.reset();
    }
    if (model != nullptr)
    {
        model->reset();
        onnxFlag.store (model->usesOnnx(), std::memory_order_relaxed);
    }

    while (! stopFlag.load (std::memory_order_relaxed))
    {
        decoder.setUserOctave (wantedOctave.load (std::memory_order_relaxed));
        decoder.setLineFeed (wantedLineFeed.load (std::memory_order_relaxed));

        // Before the audio from after the event reaches the decoder, not after:
        // the whole point is that nothing measured before it may vouch for what
        // comes next.
        const uint32_t epoch = inputEpoch.load (std::memory_order_relaxed);
        if (epoch != seenInputEpoch)
        {
            seenInputEpoch = epoch;
            decoder.notifyInputRestart();
            // The network's recurrent state as well. A cold start begins with it
            // zeroed and that is the case every measurement in this repository
            // was taken in; carrying twenty seconds of an amplified empty room
            // into the first bar is not the same thing, and is not better.
            if (model != nullptr)
                model->reset();
        }

        const int n = fifo.pop (popBuf.data(), static_cast<int> (popBuf.size()));

        // An overrun means the producer overwrote audio this thread never read,
        // and the FIFO reports it at the moment this thread steps over the hole
        // - which is this pop, carrying the first audio from after it. The
        // timestamps already account for the gap, but the feature extractor,
        // the LSTM and the decoder would otherwise carry straight on across it
        // and read the splice as an onset, so they are re-primed before that
        // audio reaches them rather than after.
        const uint64_t droppedNow = fifo.droppedSamples();
        uint64_t droppedThisPass = 0;
        if (droppedNow != seenDropped)
        {
            droppedThisPass = droppedNow - seenDropped;
            const double lostSec = static_cast<double> (droppedNow - seenDropped) / deviceSr;
            seenDropped = droppedNow;
            resampler.reset();
            features.reset();
            if (model != nullptr)
                model->reset();
            decoder.notifyDiscontinuity (lostSec);
            modelRefill += kBeatModelFrame - kBeatModelHop;
            gapCount.fetch_add (1, std::memory_order_relaxed);
        }

        if (n > 0)
        {
            const int nr = resampler.process (popBuf.data(), n, resampled.data(),
                                              static_cast<int> (resampled.size()));
            features.process (resampled.data(), nr);

            while (features.popFrame (frame))
            {
                if (model == nullptr || ! model->infer (frame, LogSpectFeatures::kDim, act))
                    continue;

                auto h = decoder.observe (act[0], act[1], act[2]);
                h.analysisSample = analysisSampleFor (h.frameIndex);
                slot.publish (h);
            }

            // `available() == 0` only proves that pop() took the input. This
            // release happens after every inference and publication caused by
            // that input, which gives deterministic probes a real completion
            // condition. Dropped samples count as passed here because the
            // discontinuity above has already accounted for and reset across
            // them.
            completedTotal.fetch_add (static_cast<int64_t> (n)
                                      + static_cast<int64_t> (droppedThisPass),
                                      std::memory_order_release);
        }

        // Wait for about as much new audio as it takes to make one more frame.
        //
        // Nothing at all can come out of this chain until a whole hop has
        // arrived - twenty milliseconds of it - so polling every millisecond
        // meant waking nineteen times out of twenty to find nothing to do. On a
        // device running on a battery that is the wakeups, not the arithmetic,
        // that cost something. The wait adds no error: every timestamp here is
        // derived from the frame index, and the delay the audio thread leads by
        // is measured rather than assumed, so it simply reads a little larger.
        const int have = fifo.available();
        const int wantMore = hopInDeviceSamples - have;
        if (wantMore > 0)
        {
            const double sec = static_cast<double> (wantMore) / deviceSr;
            const auto us = static_cast<long long> (std::clamp (sec * 1.0e6, 500.0, 8000.0));
            std::this_thread::sleep_for (std::chrono::microseconds (us));
        }
        wakeCount.fetch_add (1, std::memory_order_relaxed);
    }
}

int64_t NeuralBeatTracker::analysisSampleFor (uint64_t frameIndex) noexcept
{
    // Frames leave LogSpectFeatures on a fixed grid: the first once frameLen
    // samples have arrived, then one per hop. So the position of any frame is
    // arithmetic, no plumbing required.
    //
    // The decoder's clock ticks one hop per frame, while the audio the frame
    // describes is centred half a window back. Anchoring on that centre is what
    // makes the delay the audio thread computes a real acoustic delay.
    const double modelPos = static_cast<double> (frameIndex) * kBeatModelHop
                            + kBeatModelFrame * 0.5 - kBeatModelHop
                            + static_cast<double> (modelRefill);
    const double inputPos = modelPos * inputSamplesPerModelSample;

    // Anything the FIFO overwrote never reached the feature extractor, so the
    // worker is that much further behind the audio thread than its own frame
    // count suggests.
    return static_cast<int64_t> (inputPos) + static_cast<int64_t> (fifo.droppedSamples());
}

} // namespace vp
