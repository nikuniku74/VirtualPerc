#pragma once

#include "Core/Types.h"

#include <atomic>
#include <cstdint>

namespace vp
{

enum class TempoRegime : int
{
    /** Not enough evidence yet. */
    unknown = 0,
    /** Tempo has held still long enough to be a click track or a sequencer.
        Freeze it; only phase needs correcting. */
    fixed,
    /** Tempo is genuinely moving, as with a band playing. Follow it. */
    live
};

struct BeatHypothesis
{
    float    bpm         = 0.0f;
    float    beatPhase   = 0.0f;
    float    barPhase    = 0.0f;
    float    confidence  = 0.0f;
    float    pBeat       = 0.0f;
    float    pDownbeat   = 0.0f;
    uint64_t frameIndex  = 0;
    bool     valid       = false;
    bool     peak        = false;
    bool     downbeat    = false;

    /** Monotonic beat counters. The audio thread reads this slot at its own
        block rate, which is neither aligned to nor necessarily faster than the
        50 fps analysis rate, so a plain `peak` flag is read twice for one beat
        on small buffers and missed entirely on large ones. Consumers must
        compare serials against the last value they saw. */
    uint32_t beatSerial     = 0;
    uint32_t downbeatSerial = 0;

    /** Input-sample position this hypothesis describes, in the device clock the
        audio thread counts in. The pipeline delay is whatever the audio thread
        has fed since; without it the phase target refers to the past. */
    int64_t  analysisSample = 0;

    /** Seconds per beat for the committed tempo, so consumers do not have to
        re-derive it while `bpm` is being updated. */
    float    periodSec   = 0.0f;

    TempoRegime regime   = TempoRegime::unknown;

    /** What the fold is naming, and whether the buffer is yet long enough for
        that to be a decision rather than the fastest thing in view. Diagnostic:
        when the reported tempo is wrong these two say which half of the
        analysis to look at, and there is no way to see them from outside
        otherwise - the estimator lives on the worker thread. */
    float    combBpm     = 0.0f;
    bool     levelSettled = false;
};

class HypothesisSlot
{
public:
    void publish (const BeatHypothesis& h) noexcept
    {
        const uint32_t s = seq.load (std::memory_order_relaxed);
        seq.store (s + 1u, std::memory_order_release);
        value = h;
        seq.store (s + 2u, std::memory_order_release);
    }

    bool load (BeatHypothesis& out) const noexcept
    {
        for (int i = 0; i < 8; ++i)
        {
            const uint32_t a = seq.load (std::memory_order_acquire);
            if ((a & 1u) != 0)
                continue;
            const BeatHypothesis copy = value;
            const uint32_t b = seq.load (std::memory_order_acquire);
            if (a == b)
            {
                out = copy;
                return a != 0;
            }
        }
        return false;
    }

private:
    alignas (64) std::atomic<uint32_t> seq { 0 };
    BeatHypothesis value {};
};

} // namespace vp
