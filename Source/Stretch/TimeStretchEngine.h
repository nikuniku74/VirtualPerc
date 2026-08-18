#pragma once

#include <vector>

namespace vp
{

class TimeStretchEngine
{
public:
    void prepare (double sampleRate, int maxBlock) noexcept;
    void reset() noexcept;
    void loadLoop (const float* left, const float* right, int frames);

    bool hasLoop() const noexcept { return loopFrames > 0; }
    int  loopLength() const noexcept { return loopFrames; }

    void process (float* left, float* right, int numSamples, float ratio) noexcept;

private:
    void wsola (float* left, float* right, int numSamples, float ratio) noexcept;

    std::vector<float> loopL;
    std::vector<float> loopR;
    std::vector<float> grainL;
    std::vector<float> grainR;
    std::vector<float> window;
    double sampleRate = 48000.0;
    int maxBlock = 1024;
    int loopFrames = 0;
    double readPos = 0.0;
    int grainSize = 1024;
    int synthHop = 256;
};

} // namespace vp
