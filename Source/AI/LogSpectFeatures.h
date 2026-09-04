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

    /** How many of the low bands count as "kick and snare body". The filterbank
        is logarithmic from 30 Hz, but at the bottom the madmom FFT grid is
        coarser than 1/24 octave and neighbouring centres collapse onto the same
        bin, so this is a count of filters rather than a frequency - see
        docs/HANDOFF_OCTAVE_50BPM.md for the sweep that chose it. */
    static constexpr int kLowBands = 24;

    /** Mean of those bands in a frame from `popFrame`. Kick and snare have body
        down there; a hi-hat has almost none, and since the bands are
        log10(mag + 1) an empty low end reads as very nearly zero rather than as
        a small negative number. What the metrical-level test in BeatDecoder is
        built on: see observeMetricalCadence. */
    static float lowBandEnergy (const float* frame) noexcept;

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
