#pragma once

#include <cstdint>

namespace vp
{

class DeterministicRng
{
public:
    explicit DeterministicRng (std::uint32_t seed = 0xC0FFEEu) noexcept
        : state (seed == 0 ? 1u : seed)
    {
    }

    void reset (std::uint32_t seed) noexcept
    {
        state = seed == 0 ? 1u : seed;
    }

    std::uint32_t nextU32() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    float nextFloat() noexcept
    {
        return static_cast<float> (nextU32() >> 8) * (1.0f / 16777216.0f);
    }

    float nextSigned() noexcept
    {
        return nextFloat() * 2.0f - 1.0f;
    }

private:
    std::uint32_t state;
};

} // namespace vp
