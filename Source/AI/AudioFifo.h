#pragma once

#include <algorithm>
#include <atomic>
#include <new>
#include <cstdint>
#include <cstring>
#include <vector>

namespace vp
{

class AudioFifo
{
public:
    void prepare (int capacityPow2)
    {
        int n = 1;
        while (n < capacityPow2)
            n <<= 1;
        buf.assign (static_cast<size_t> (n), 0.0f);
        mask = static_cast<uint32_t> (n - 1);
        w.store (0, std::memory_order_relaxed);
        r.store (0, std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        w.store (0, std::memory_order_relaxed);
        r.store (0, std::memory_order_relaxed);
        dropped.store (0, std::memory_order_relaxed);
    }

    /** Audio thread. Writes and advances the write cursor, and touches nothing
        else: when the consumer has fallen behind, this simply runs past it. The
        read cursor belongs to the consumer alone - moving it from here as well
        is a race with a visible consequence, because the consumer can then
        store a position the producer has already passed and hand the same
        samples out twice. */
    void push (const float* x, int n) noexcept
    {
        if (buf.empty() || x == nullptr || n <= 0)
            return;

        const uint32_t wi = w.load (std::memory_order_relaxed);
        const uint32_t cap = mask + 1u;
        const uint32_t need = static_cast<uint32_t> (n);

        // More than a bufferful at once: only the tail can survive, so write
        // just that and advance the cursor by the whole block. The consumer
        // reads the loss off the cursor, so the count stays exact.
        int from = 0;
        if (need > cap)
            from = n - static_cast<int> (cap);

        for (int i = from; i < n; ++i)
            at ((wi + static_cast<uint32_t> (i)) & mask).store (x[i], std::memory_order_relaxed);

        w.store (wi + need, std::memory_order_release);
    }

    /** Worker thread. Returns a forward-only walk through what the producer
        wrote: whatever it overran is skipped and added to droppedSamples()
        here, where it is noticed, rather than at the moment it was overwritten.

        The copy is validated afterwards against the write cursor. A producer
        that laps the reader *during* the copy has made it a mixture of two
        different moments, and a mixture is worse than a gap - the gap is
        reported and the caller re-primes, while the mixture reads as an onset
        that never happened. */
    int pop (float* dest, int maxN) noexcept
    {
        if (buf.empty() || dest == nullptr || maxN <= 0)
            return 0;

        const uint32_t cap = mask + 1u;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t wi = w.load (std::memory_order_acquire);
            uint32_t ri = r.load (std::memory_order_relaxed);

            // Overrun. Resync a margin behind the writer rather than to the
            // very edge of what survives: landing on the edge means the producer
            // only has to write one more block to be overwriting the samples
            // being copied out, and a copy taken from two different moments
            // reads as an onset that never happened.
            //
            // The margin is sized against this read, not against the buffer.
            // To spoil the copy the producer would have to write a whole read's
            // worth of samples in the time it takes to memcpy that many - it
            // cannot come close - while a margin taken as a fraction of the
            // buffer would throw away seconds of audio on every overrun, and
            // seconds of audio thrown away is a worse tracker than the tearing
            // it was avoiding.
            if (wi - ri > cap)
            {
                const uint32_t margin = std::min (cap / 2u, static_cast<uint32_t> (maxN));
                const uint32_t skipTo = wi - (cap - margin);
                dropped.fetch_add (static_cast<uint64_t> (skipTo - ri), std::memory_order_relaxed);
                ri = skipTo;
            }

            const uint32_t avail = wi - ri;
            const uint32_t want = static_cast<uint32_t> (maxN);
            const int n = static_cast<int> (avail < want ? avail : want);
            if (n <= 0)
            {
                r.store (ri, std::memory_order_release);
                return 0;
            }

            for (int i = 0; i < n; ++i)
                dest[i] = at ((ri + static_cast<uint32_t> (i)) & mask).load (std::memory_order_relaxed);

            std::atomic_thread_fence (std::memory_order_acquire);
            if (w.load (std::memory_order_relaxed) - ri <= cap)
            {
                r.store (ri + static_cast<uint32_t> (n), std::memory_order_release);
                return n;
            }
            r.store (ri, std::memory_order_release);
        }
        return 0;
    }

    int available() const noexcept
    {
        const uint32_t wi = w.load (std::memory_order_acquire);
        const uint32_t ri = r.load (std::memory_order_relaxed);
        return static_cast<int> (wi - ri);
    }

    /** The producer's cursor before it writes the next block. A source-change
        event captures this on the audio thread, so the worker can discard only
        the previous source even if the new file has already queued audio. */
    uint32_t writePosition() const noexcept
    {
        return w.load (std::memory_order_acquire);
    }

    /** Worker thread only. Discard through a captured producer cursor, leaving
        later samples in the FIFO. If the worker has already passed the cursor,
        there is nothing old left to discard. Cursor subtraction is modular;
        the live FIFO span is far below half the 32-bit range. */
    void discardBefore (uint32_t cursor) noexcept
    {
        const uint32_t ri = r.load (std::memory_order_relaxed);
        const uint32_t distance = cursor - ri;
        if (distance == 0 || distance >= 0x80000000u)
            return;

        const uint32_t wi = w.load (std::memory_order_acquire);
        const uint32_t count = std::min (distance, wi - ri);
        if (count == 0)
            return;
        dropped.fetch_add (static_cast<uint64_t> (count), std::memory_order_relaxed);
        r.store (ri + count, std::memory_order_release);
    }

    /** Samples skipped by an overrun or a source change. The consumer needs
        this to keep its position in step with the producer's sample count;
        without it a gap would offset every timestamp derived downstream.
        Frames processed plus samples dropped is always exactly what was
        pushed. */
    uint64_t droppedSamples() const noexcept
    {
        return dropped.load (std::memory_order_acquire);
    }

private:
    /** The cells themselves.

        A ring that overwrites is a ring where the producer can, in the worst
        case, be writing a cell the consumer is reading - that is what the
        validation after each copy is for. Reached through atomic_ref, that
        worst case is a defined concurrent access rather than a data race the
        compiler is entitled to assume away, and the validation goes back to
        being what it is meant to be: a check on whether the copy is one moment,
        not the thing keeping the program well defined. Relaxed is all that is
        wanted here - the ordering that matters is on the cursors. */
    std::atomic_ref<float> at (uint32_t index) const noexcept
    {
        return std::atomic_ref<float> (const_cast<float&> (buf[index]));
    }

    std::vector<float> buf;
    uint32_t mask = 0;
    std::atomic<uint32_t> w { 0 };
    std::atomic<uint32_t> r { 0 };
    std::atomic<uint64_t> dropped { 0 };
};

} // namespace vp
