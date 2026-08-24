#pragma once

#include "Core/Types.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

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

    /** Bumped whenever the analysis throws its grid away and builds another:
        a metrical level it no longer believes, a grid it has found to be on
        the offbeat, a level the listener changed. Everything measured about
        the bar describes the grid that has just gone, so this is the one
        moment at which the count is worth nothing and should be dropped rather
        than argued down. It is *not* bumped for a dropout - a hole in the
        audio costs the recent evidence, not the song. */
    uint32_t gridSerial     = 0;

    /** Input-sample position this hypothesis describes, in the device clock the
        audio thread counts in. The pipeline delay is whatever the audio thread
        has fed since; without it the phase target refers to the past. */
    int64_t  analysisSample = 0;

    /** Seconds per beat for the committed tempo, so consumers do not have to
        re-derive it while `bpm` is being updated. */
    float    periodSec   = 0.0f;

    TempoRegime regime   = TempoRegime::unknown;

    /** How strongly the network called the last downbeat, 0..1. The bar is
        decided by a vote across the four beats, and counting one vote per event
        makes that vote a coin toss on material where the network fires on beats
        one and three alike - which is most material, because on most records
        those two carry the same kick. Weighted by this, "fires often but
        weakly" stops outvoting "fires less often and means it". */
    float    downbeatStrength = 0.0f;

    /** What the fold is naming, and whether the buffer is yet long enough for
        that to be a decision rather than the fastest thing in view. Diagnostic:
        when the reported tempo is wrong these two say which half of the
        analysis to look at, and there is no way to see them from outside
        otherwise - the estimator lives on the worker thread. */
    float    combBpm     = 0.0f;
    bool     levelSettled = false;
};

/**
    One hypothesis handed from the analysis worker to the audio thread, without
    either of them ever waiting for the other.

    A sequence counter goes odd while the payload is being written and even
    again when it is whole, so a reader that sees the same even count on both
    sides of its copy knows the copy is one moment rather than two halves of
    different ones. Two details in that are easy to get wrong and were:

    - **The counter has to be ordered against the payload in both directions.**
      A release store only stops earlier writes from moving later; it does not
      stop the payload from moving *earlier*, past the odd marker. On a machine
      that reorders stores - every ARM, which is what this ships on - the reader
      could then copy a half-written payload while the counter still read even,
      and its own check would tell it the copy was sound. Explicit fences on
      both sides are what actually holds the order, so they are what is used.

    - **The payload cannot be a plain struct.** Two threads touching the same
      non-atomic object is a data race whatever the counter says, and a compiler
      is entitled to assume races do not happen. Held as relaxed atomic words it
      is a defined concurrent access, and the sequence counter goes back to
      being what it is meant to be: a validity check on the copy, not the thing
      preventing the race.

    The cost is ten word copies per publish, at fifty publishes a second.
*/
class HypothesisSlot
{
public:
    void publish (const BeatHypothesis& h) noexcept
    {
        Words staging {};
        std::memcpy (staging.raw, &h, sizeof (BeatHypothesis));

        const uint32_t s = seq.load (std::memory_order_relaxed);
        seq.store (s + 1u, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        for (size_t i = 0; i < kWords; ++i)
            words[i].store (staging.raw[i], std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        seq.store (s + 2u, std::memory_order_relaxed);
    }

    bool load (BeatHypothesis& out) const noexcept
    {
        for (int i = 0; i < 8; ++i)
        {
            const uint32_t a = seq.load (std::memory_order_acquire);
            if ((a & 1u) != 0)
                continue;

            Words staging {};
            for (size_t k = 0; k < kWords; ++k)
                staging.raw[k] = words[k].load (std::memory_order_relaxed);

            std::atomic_thread_fence (std::memory_order_acquire);
            if (seq.load (std::memory_order_relaxed) == a)
            {
                std::memcpy (&out, staging.raw, sizeof (BeatHypothesis));
                return a != 0;
            }
        }
        return false;
    }

private:
    static_assert (std::is_trivially_copyable<BeatHypothesis>::value,
                   "the slot copies the hypothesis as raw words");
    static constexpr size_t kWords = (sizeof (BeatHypothesis) + sizeof (uint64_t) - 1)
                                     / sizeof (uint64_t);
    union Words
    {
        uint64_t raw[kWords];
        unsigned char bytes[kWords * sizeof (uint64_t)];
    };

    alignas (64) std::atomic<uint32_t> seq { 0 };
    std::atomic<uint64_t> words[kWords] {};
};

} // namespace vp
