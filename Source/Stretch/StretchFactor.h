#pragma once

namespace vp
{

class StretchFactor
{
public:
    void prepare (float loopNativeBpm, double sampleRate) noexcept;
    void reset() noexcept;

    void setLiveClock (float bpm, float beatPhase, float confidence) noexcept;
    float advance (int numSamples) noexcept;

    float ratio() const noexcept { return smoothed; }
    float loopPhase() const noexcept { return loopPh; }
    float loopBpm() const noexcept { return nativeBpm; }

    static constexpr float minRatio = 0.5f;
    static constexpr float maxRatio = 2.0f;
    static constexpr float phaseGain = 0.08f;

private:
    double sampleRate = 48000.0;
    float nativeBpm = 120.0f;
    float liveBpm = 120.0f;
    float livePhase = 0.0f;
    float liveConf = 0.0f;
    float smoothed = 1.0f;
    float loopPh = 0.0f;
};

} // namespace vp
