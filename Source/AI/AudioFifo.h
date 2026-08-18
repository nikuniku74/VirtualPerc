#pragma once

#include <atomic>
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

    void push (const float* x, int n) noexcept
    {
        if (buf.empty() || x == nullptr || n <= 0)
            return;

        uint32_t wi = w.load (std::memory_order_relaxed);
        uint32_t ri = r.load (std::memory_order_acquire);
        const uint32_t cap = mask + 1u;
        const uint32_t used = wi - ri;
        const uint32_t need = static_cast<uint32_t> (n);

        if (used + need > cap)
        {
            const uint32_t newRead = wi + need - cap;
            dropped.fetch_add (static_cast<uint64_t> (newRead - ri), std::memory_order_release);
            ri = newRead;
            r.store (ri, std::memory_order_release);
        }

        for (int i = 0; i < n; ++i)
            buf[static_cast<size_t> ((wi + static_cast<uint32_t> (i)) & mask)] = x[i];

        w.store (wi + need, std::memory_order_release);
    }

    int pop (float* dest, int maxN) noexcept
    {
        if (buf.empty() || dest == nullptr || maxN <= 0)
            return 0;

        const uint32_t wi = w.load (std::memory_order_acquire);
        uint32_t ri = r.load (std::memory_order_relaxed);
        const int avail = static_cast<int> (wi - ri);
        const int n = avail < maxN ? avail : maxN;
        for (int i = 0; i < n; ++i)
            dest[i] = buf[static_cast<size_t> ((ri + static_cast<uint32_t> (i)) & mask)];
        r.store (ri + static_cast<uint32_t> (n), std::memory_order_release);
        return n;
    }

    int available() const noexcept
    {
        const uint32_t wi = w.load (std::memory_order_acquire);
        const uint32_t ri = r.load (std::memory_order_relaxed);
        return static_cast<int> (wi - ri);
    }

    /** Samples the producer overwrote before the consumer read them. The
        consumer needs this to keep its own position in step with the producer's
        sample count; without it a single overrun would silently offset every
        timestamp derived downstream. */
    uint64_t droppedSamples() const noexcept
    {
        return dropped.load (std::memory_order_acquire);
    }

private:
    std::vector<float> buf;
    uint32_t mask = 0;
    std::atomic<uint32_t> w { 0 };
    std::atomic<uint32_t> r { 0 };
    std::atomic<uint64_t> dropped { 0 };
};

} // namespace vp
