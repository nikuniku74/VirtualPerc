#pragma once

#include <vector>

namespace vp
{

class LogSpectFeatures
{
public:
    static constexpr int kBands = 136;
    static constexpr int kDim   = kBands * 2;
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;

    LogSpectFeatures();
    ~LogSpectFeatures();

    void prepare (double sampleRate, int hopLength);
    void reset() noexcept;
    void process (const float* mono, int numSamples) noexcept;
    bool popFrame (float* dest272) noexcept;

    int hopLength() const noexcept { return hop; }
    double rate() const noexcept { return sampleRate; }

private:
    void processHop() noexcept;
    void buildFilterbank() noexcept;

    struct Impl;
    Impl* impl = nullptr;

    double sampleRate = 22050.0;
    int hop = 441;
    int frameLen = 1411;
    int writePos = 0;
    int samplesUntilHop = 1411;
    float prevBand[kBands] {};
    bool havePrev = false;
    float pending[kDim] {};
};

class LinearResampler
{
public:
    void prepare (double srcRate, double dstRate);
    void reset() noexcept;
    int  process (const float* in, int nIn, float* out, int maxOut) noexcept;

private:
    std::vector<float> q;
    int qRead = 0;
    int qWrite = 0;
    double step = 1.0;
    double frac = 0.0;
};

} // namespace vp
