#pragma once

#include "AI/AudioFifo.h"
#include "AI/BeatHypothesis.h"
#include "AI/BeatDecoder.h"
#include "AI/IBeatModel.h"
#include "AI/LogSpectFeatures.h"

#include <atomic>
#include <cstdint>
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

    /** The metrical level the listener asked for, in octaves. Set from the
        audio thread, read by the worker: the decoder is not ours to touch from
        here, so the value is handed over rather than the call. */
    void setUserOctave (int octaves) noexcept
    {
        wantedOctave.store (octaves, std::memory_order_relaxed);
    }

    /** MIXER rather than the iPad's own microphone. Handed over rather than
        called, like the octave above: the decoder belongs to the worker. */
    void setLineFeed (bool on) noexcept
    {
        wantedLineFeed.store (on, std::memory_order_relaxed);
    }

    /** Whether a part is currently sounding. Same hand-over as the two above.

        `BeatTracker::updateAutoOctave` already refuses to move the metrical
        level under a playing part, and says at length why. It could only ever
        enforce that on its own shift: the decoder can halve the tempo it
        reports, and then the shift stays at zero while the grid moves anyway.
        The decoder needs the same fact to apply the same rule. */
    void setSounding (bool on) noexcept
    {
        wantedSounding.store (on, std::memory_order_relaxed);
    }

    /** The listener pressed "L'1 è QUI". Counter, not a flag: the decoder
        belongs to the worker, and a press that comes and goes between two
        of its passes must not be missed. */
    void declarePulseHere() noexcept
    {
        wantedDeclarePulse.fetch_add (1, std::memory_order_relaxed);
    }
    /** The analysis input has changed character - see
        BeatDecoder::notifyInputRestart. Handed over as a counter rather than a
        flag, from the audio thread, so an event that comes and goes between two
        of the worker's passes cannot be missed.

        `dropQueued` is a new file, not a quiet-to-loud entrance and not a
        seek. The FIFO still holds the previous file; analysing it after the
        epoch re-certifies that file's tempo. The request sticks on this epoch
        until the epoch changes, so a later block cannot clear it before the
        worker has seen it. */
    void setInputEpoch (uint32_t epoch, bool preserveComb = false,
                        bool dropQueued = false) noexcept
    {
        // Publish event and kind together: the worker must not pair one epoch
        // with the following epoch's kind. Bit 0 is continuous music, for
        // which recurrent state and comb history both survive. Bit 1 drops
        // audio queued from the previous source.
        const uint64_t prev = inputEpoch.load (std::memory_order_relaxed);
        uint64_t word = (static_cast<uint64_t> (epoch) << 2)
                        | (preserveComb ? 1ull : 0ull);
        // A file change sets the drop bit, then the new file's own onset can
        // publish a later epoch before the worker has read the first. That
        // later epoch is a rhythm entrance and would keep the previous song's
        // comb. Stick the drop, and do not preserve, until the worker clears
        // the bit. STOP appeared to fix this because disarming clears sounding.
        if (dropQueued || (prev & 2ull) != 0ull)
            word = (word | 2ull) & ~1ull;
        inputEpoch.store (word, std::memory_order_relaxed);
    }

    /** Reject publications describing audio older than the input position at
        this call. This is reader-side freshness only: it does not touch the
        FIFO, model, feature extractor, decoder, or worker timeline. */
    void invalidatePublicationsBeforeNow() noexcept;
    bool tryLoad (BeatHypothesis& out) const noexcept;
    uint32_t publicationSequence() const noexcept { return slot.completedSequence(); }

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

    /** Times the worker has gone round its loop. Compared against the number of
        analysis frames it produced, this says whether it is doing work or
        spinning: on a battery the wakeups cost more than the arithmetic. */
    int64_t wakeups() const noexcept { return wakeCount.load (std::memory_order_relaxed); }

    /** Input samples fed but not yet analysed. Zero means the worker is caught
        up with the audio thread. The probes wait on this to make a run
        repeatable: otherwise how far behind the worker happens to be is decided
        by the host's scheduler, and the same build measures differently from
        one run to the next. */
    int backlog() const noexcept { return fifo.available(); }

    /** Device-input samples for which the worker has completed feature
        extraction, inference and publication. Tests use this completion
        condition instead of mistaking an empty FIFO for finished work. */
    int64_t completedSamples() const noexcept
    {
        return completedTotal.load (std::memory_order_acquire);
    }

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
    std::atomic<int64_t> completedTotal { 0 };
    std::atomic<int64_t> gapCount { 0 };
    std::atomic<int64_t> wakeCount { 0 };
    std::atomic<int> wantedOctave { 0 };
    std::atomic<bool> wantedLineFeed { false };
    std::atomic<bool> wantedSounding { false };
    std::atomic<uint32_t> wantedDeclarePulse { 0 };
    std::atomic<uint64_t> inputEpoch { 0 };
    static_assert (std::atomic<uint64_t>::is_always_lock_free);
    std::atomic<int64_t> minimumAnalysisSample { 0 };
    uint64_t seenInputEpoch = 0;
    uint32_t seenDeclarePulse = 0;
    uint64_t seenDropped = 0;
    /** Model samples of extra priming the feature extractor has needed across
        all discontinuities. After a reset it buffers a whole frame before
        emitting again, not one hop, so without this every later frame would be
        timestamped early by that difference. */
    int64_t modelRefill = 0;
    double deviceSr = 48000.0;
    double inputSamplesPerModelSample = 1.0;
    /** Device samples that make one analysis hop: the unit of useful work. */
    int    hopInDeviceSamples = 960;
};

} // namespace vp
