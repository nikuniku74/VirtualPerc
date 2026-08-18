#include "Stretch/StretchFactor.h"

#include "Core/Types.h"

#include <algorithm>
#include <cmath>

namespace vp
{

void StretchFactor::prepare (float loopNativeBpm, double sr) noexcept
{
    sampleRate = sr > 1.0 ? sr : 48000.0;
    nativeBpm = std::clamp (loopNativeBpm, 40.0f, 220.0f);
    reset();
}

void StretchFactor::reset() noexcept
{
    liveBpm = nativeBpm;
    livePhase = 0.0f;
    liveConf = 0.0f;
    smoothed = 1.0f;
    loopPh = 0.0f;
}

void StretchFactor::setLiveClock (float bpm, float beatPhase, float confidence) noexcept
{
    if (bpm > 40.0f && bpm < 220.0f)
        liveBpm = bpm;
    livePhase = wrap01 (beatPhase);
    liveConf = clamp01 (confidence);
}

float StretchFactor::advance (int numSamples) noexcept
{
    float r = liveBpm / nativeBpm;
    const float err = wrapCentered (livePhase - loopPh);
    r *= 1.0f + phaseGain * err * liveConf;
    r = std::clamp (r, minRatio, maxRatio);

    const float alpha = liveConf > 0.3f ? 0.12f : 0.04f;
    smoothed += alpha * (r - smoothed);
    smoothed = std::clamp (smoothed, minRatio, maxRatio);

    const double inc = (static_cast<double> (liveBpm) / 60.0) / sampleRate;
    loopPh = wrap01 (loopPh + static_cast<float> (inc * static_cast<double> (numSamples)));
    return smoothed;
}

} // namespace vp
