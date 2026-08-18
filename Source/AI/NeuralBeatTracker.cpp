#include "AI/NeuralBeatTracker.h"

#include "AI/BeatModelConfig.h"
#include "AI/ModelLocator.h"
#include "AI/OnnxBeatModel.h"
#include "AI/StubBeatModel.h"

#include <chrono>

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

    if (! model)
    {
        auto onnx = std::make_unique<OnnxBeatModel>();
        if (loadDefaultBeatModel (*onnx))
            model = std::move (onnx);
        else
            model = std::make_unique<StubBeatModel>();
    }

    fifo.prepare (1 << 19);
    resampler.prepare (deviceSr, kBeatModelSampleRate);
    features.prepare (kBeatModelSampleRate, kBeatModelHop);
    decoder.prepare (kBeatModelSampleRate / static_cast<double> (kBeatModelHop));
    inputSamplesPerModelSample = deviceSr / kBeatModelSampleRate;
    fedTotal.store (0, std::memory_order_relaxed);
    if (! model->prepare (LogSpectFeatures::kDim))
        return false;
    model->reset();
    onnxFlag.store (model->usesOnnx(), std::memory_order_relaxed);

    popBuf.assign (4096, 0.0f);
    resampled.assign (4096, 0.0f);

    stopFlag.store (false, std::memory_order_relaxed);
    armed.store (true, std::memory_order_release);
    worker = std::thread ([this] { workerLoop(); });
    return true;
}

void NeuralBeatTracker::stop()
{
    armed.store (false, std::memory_order_relaxed);
    stopFlag.store (true, std::memory_order_relaxed);
    if (worker.joinable())
        worker.join();
    fifo.reset();
}

void NeuralBeatTracker::feed (const float* mono, int numSamples) noexcept
{
    if (! armed.load (std::memory_order_relaxed) || mono == nullptr || numSamples <= 0)
        return;
    fifo.push (mono, numSamples);
    fedTotal.fetch_add (numSamples, std::memory_order_relaxed);
}

void NeuralBeatTracker::workerLoop()
{
    float frame[LogSpectFeatures::kDim];
    float act[3] {};

    while (! stopFlag.load (std::memory_order_relaxed))
    {
        const int n = fifo.pop (popBuf.data(), static_cast<int> (popBuf.size()));
        if (n <= 0)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
            continue;
        }

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
    }
}

int64_t NeuralBeatTracker::analysisSampleFor (uint64_t frameIndex) const noexcept
{
    // Frames leave LogSpectFeatures on a fixed grid: the first once frameLen
    // samples have arrived, then one per hop. So the position of any frame is
    // arithmetic, no plumbing required.
    //
    // The decoder's clock ticks one hop per frame, while the audio the frame
    // describes is centred half a window back. Anchoring on that centre is what
    // makes the delay the audio thread computes a real acoustic delay.
    const double modelPos = static_cast<double> (frameIndex) * kBeatModelHop
                            + kBeatModelFrame * 0.5 - kBeatModelHop;
    const double inputPos = modelPos * inputSamplesPerModelSample;

    // Anything the FIFO overwrote never reached the feature extractor, so the
    // worker is that much further behind the audio thread than its own frame
    // count suggests.
    return static_cast<int64_t> (inputPos) + static_cast<int64_t> (fifo.droppedSamples());
}

} // namespace vp
