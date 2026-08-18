#include "Stretch/TimeStretchEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(VP_USE_SIGNALSMITH) && VP_USE_SIGNALSMITH
#include <signalsmith-stretch/signalsmith-stretch.h>
#endif

namespace vp
{

void TimeStretchEngine::prepare (double sr, int maxBlk) noexcept
{
    sampleRate = sr > 1.0 ? sr : 48000.0;
    maxBlock = std::max (64, maxBlk);
    grainSize = 1024;
    synthHop = 256;
    grainL.assign (static_cast<size_t> (grainSize), 0.0f);
    grainR.assign (static_cast<size_t> (grainSize), 0.0f);
    window.assign (static_cast<size_t> (grainSize), 0.0f);
    for (int i = 0; i < grainSize; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (grainSize - 1);
        window[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (6.28318530718f * t);
    }
    reset();
}

void TimeStretchEngine::reset() noexcept
{
    readPos = 0.0;
}

void TimeStretchEngine::loadLoop (const float* left, const float* right, int frames)
{
    loopFrames = std::max (0, frames);
    loopL.assign (static_cast<size_t> (loopFrames), 0.0f);
    loopR.assign (static_cast<size_t> (loopFrames), 0.0f);
    if (left != nullptr && loopFrames > 0)
        std::memcpy (loopL.data(), left, static_cast<size_t> (loopFrames) * sizeof (float));
    if (right != nullptr && loopFrames > 0)
        std::memcpy (loopR.data(), right, static_cast<size_t> (loopFrames) * sizeof (float));
    else if (loopFrames > 0)
        loopR = loopL;
    readPos = 0.0;
}

void TimeStretchEngine::process (float* left, float* right, int numSamples, float ratio) noexcept
{
    if (left == nullptr || numSamples <= 0)
        return;
    if (right == nullptr)
        right = left;

    std::fill (left, left + numSamples, 0.0f);
    if (right != left)
        std::fill (right, right + numSamples, 0.0f);

    if (loopFrames < grainSize)
        return;

    const float r = std::clamp (ratio, 0.5f, 2.0f);

#if defined(VP_USE_SIGNALSMITH) && VP_USE_SIGNALSMITH
    (void) r;
    // When vendored: stretch.setTransposeFactor (1.0f);
    // int inCount = static_cast<int> (numSamples * r);
    // gather wrapped input into scratch, then
    // stretch.process (inPtrs, inCount, outPtrs, numSamples);
    wsola (left, right, numSamples, r);
#else
    wsola (left, right, numSamples, r);
#endif
}

void TimeStretchEngine::wsola (float* left, float* right, int numSamples, float ratio) noexcept
{
    const int N = loopFrames;
    const float analysisHop = static_cast<float> (synthHop) * ratio;
    int written = 0;

    while (written < numSamples)
    {
        const int remain = numSamples - written;
        const int hop = std::min (synthHop, remain);

        for (int i = 0; i < grainSize; ++i)
        {
            double idx = readPos + static_cast<double> (i);
            int i0 = static_cast<int> (idx) % N;
            if (i0 < 0)
                i0 += N;
            int i1 = i0 + 1;
            if (i1 >= N)
                i1 -= N;
            const float t = static_cast<float> (idx - std::floor (idx));
            const float w = window[static_cast<size_t> (i)];
            const float l = loopL[static_cast<size_t> (i0)] + (loopL[static_cast<size_t> (i1)] - loopL[static_cast<size_t> (i0)]) * t;
            const float rS = loopR[static_cast<size_t> (i0)] + (loopR[static_cast<size_t> (i1)] - loopR[static_cast<size_t> (i0)]) * t;
            grainL[static_cast<size_t> (i)] = l * w;
            grainR[static_cast<size_t> (i)] = rS * w;
        }

        const int mixN = std::min (grainSize, remain);
        for (int i = 0; i < mixN; ++i)
        {
            left[written + i] += grainL[static_cast<size_t> (i)];
            right[written + i] += grainR[static_cast<size_t> (i)];
        }

        readPos += static_cast<double> (analysisHop);
        while (readPos >= static_cast<double> (N))
            readPos -= static_cast<double> (N);
        while (readPos < 0.0)
            readPos += static_cast<double> (N);
        written += hop;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        left[i] = std::clamp (left[i], -1.0f, 1.0f);
        right[i] = std::clamp (right[i], -1.0f, 1.0f);
    }
}

} // namespace vp
