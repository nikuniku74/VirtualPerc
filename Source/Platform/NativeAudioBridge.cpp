#include "Platform/NativeAudioBridge.h"

#include <algorithm>
#include <cstring>

namespace vp
{

void NativeAudioBridge::prepare (int maxFr, int maxInChannels, int maxOutChannels)
{
    maxFrames = std::max (64, maxFr);
    const int nIn = std::max (1, maxInChannels);
    const int nOut = std::max (1, maxOutChannels);

    inPlanar.resize (static_cast<size_t> (nIn));
    outPlanar.resize (static_cast<size_t> (nOut));
    for (auto& c : inPlanar)
        c.assign (static_cast<size_t> (maxFrames), 0.0f);
    for (auto& c : outPlanar)
        c.assign (static_cast<size_t> (maxFrames), 0.0f);

    inPtrs.resize (static_cast<size_t> (nIn));
    outPtrs.resize (static_cast<size_t> (nOut));
}

void NativeAudioBridge::processInterleaved (VirtualPercussionEngine& engine,
                                            const float* inInterleaved, int inChannels,
                                            float* outInterleaved, int outChannels,
                                            int numFrames) noexcept
{
    if (numFrames <= 0 || maxFrames <= 0)
        return;
    if (numFrames > maxFrames)
        numFrames = maxFrames;

    const int nIn = std::min (inChannels, static_cast<int> (inPlanar.size()));
    const int nOut = std::min (outChannels, static_cast<int> (outPlanar.size()));

    for (int c = 0; c < nIn; ++c)
    {
        auto& dest = inPlanar[static_cast<size_t> (c)];
        if (inInterleaved == nullptr || inChannels <= 0)
        {
            std::fill (dest.begin(), dest.begin() + numFrames, 0.0f);
        }
        else
        {
            for (int i = 0; i < numFrames; ++i)
                dest[static_cast<size_t> (i)] = inInterleaved[i * inChannels + c];
        }
        inPtrs[static_cast<size_t> (c)] = dest.data();
    }

    for (int c = 0; c < nOut; ++c)
    {
        std::fill (outPlanar[static_cast<size_t> (c)].begin(),
                   outPlanar[static_cast<size_t> (c)].begin() + numFrames, 0.0f);
        outPtrs[static_cast<size_t> (c)] = outPlanar[static_cast<size_t> (c)].data();
    }

    engine.process (nIn > 0 ? inPtrs.data() : nullptr, nIn,
                    nOut > 0 ? outPtrs.data() : nullptr, nOut,
                    numFrames);

    if (outInterleaved != nullptr && nOut > 0)
    {
        for (int i = 0; i < numFrames; ++i)
            for (int c = 0; c < outChannels; ++c)
                outInterleaved[i * outChannels + c] =
                    outPlanar[static_cast<size_t> (std::min (c, nOut - 1))][static_cast<size_t> (i)];
    }
}

} // namespace vp
