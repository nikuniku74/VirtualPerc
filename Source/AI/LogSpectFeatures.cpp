#include "AI/LogSpectFeatures.h"
#include "AI/BeatModelConfig.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <juce_dsp/juce_dsp.h>

namespace vp
{

struct LogSpectFeatures::Impl
{
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> hann {
        static_cast<size_t> (kBeatModelFrame),
        juce::dsp::WindowingFunction<float>::hann,
        false
    };
    std::vector<float> ring;
    std::vector<float> fftWork;
    std::vector<float> frameQ;
    float weight[kBands][fftSize / 2] {};
    int qRead = 0;
    int qCount = 0;
    static constexpr int kQueue = 512;
};

LogSpectFeatures::LogSpectFeatures() : impl (new Impl) {}

LogSpectFeatures::~LogSpectFeatures()
{
    delete impl;
}

void LogSpectFeatures::prepare (double sr, int hopLength)
{
    sampleRate = sr > 1.0 ? sr : kBeatModelSampleRate;
    hop = hopLength > 16 ? hopLength : kBeatModelHop;
    frameLen = kBeatModelFrame;
    impl->ring.assign (static_cast<size_t> (frameLen), 0.0f);
    impl->fftWork.assign (static_cast<size_t> (fftSize * 2), 0.0f);
    impl->frameQ.assign (static_cast<size_t> (Impl::kQueue * kDim), 0.0f);
    buildFilterbank();
    reset();
}

void LogSpectFeatures::reset() noexcept
{
    if (impl == nullptr)
        return;
    std::fill (impl->ring.begin(), impl->ring.end(), 0.0f);
    std::fill (prevBand, prevBand + kBands, 0.0f);
    impl->qRead = 0;
    impl->qCount = 0;
    writePos = 0;
    samplesUntilHop = frameLen;
    havePrev = false;
}

void LogSpectFeatures::buildFilterbank() noexcept
{
    const int nBins = fftSize / 2;
    const float fMin = kBeatFminHz;
    const float fMax = kBeatFmaxHz;
    const int bpo = kBeatBandsPerOctave;

    for (int b = 0; b < kBands; ++b)
        for (int k = 0; k < nBins; ++k)
            impl->weight[b][k] = 0.0f;

    // BeatNet is trained with madmom's 1411-point STFT and a 24-band/octave
    // logarithmic filterbank. madmom maps the requested centre frequencies
    // to that coarse FFT grid and removes duplicate bins, which leaves
    // exactly 138 corner bins -> 136 filters. JUCE uses a zero-padded 2048
    // FFT here; map the original grid into it so the model sees the same
    // filter centres while retaining JUCE's real-time-safe FFT.
    constexpr int madmomFftBins = (kBeatModelFrame >> 1) * 2; // 1410
    constexpr float fRef = 440.0f;
    const int left = static_cast<int> (std::floor (std::log2 (fMin / fRef) * static_cast<float> (bpo)));
    const int right = static_cast<int> (std::ceil (std::log2 (fMax / fRef) * static_cast<float> (bpo)));

    int cornerBins[kBands + 2] {};
    int numCorners = 0;
    int previous = -1;
    for (int band = left; band < right; ++band)
    {
        const float hz = fRef * std::pow (2.0f, static_cast<float> (band) / static_cast<float> (bpo));
        if (hz < fMin)
            continue;
        if (hz > fMax)
            break;

        int sourceBin = static_cast<int> (std::lround (
            hz * static_cast<float> (madmomFftBins) / static_cast<float> (sampleRate)));
        sourceBin = std::clamp (sourceBin, 1, madmomFftBins / 2 - 1);
        if (sourceBin != previous)
        {
            if (numCorners < kBands + 2)
                cornerBins[numCorners++] = sourceBin;
            previous = sourceBin;
        }
    }

    if (numCorners != kBands + 2)
        return;

    for (int b = 0; b < kBands; ++b)
    {
        const int start = cornerBins[b];
        const int centre = cornerBins[b + 1];
        const int stop = cornerBins[b + 2];
        float sum = 0.0f;
        for (int sourceBin = start; sourceBin < stop; ++sourceBin)
        {
            float w = 0.0f;
            if (sourceBin < centre && centre > start)
                w = static_cast<float> (sourceBin - start) / static_cast<float> (centre - start);
            else if (stop > centre)
                w = static_cast<float> (stop - sourceBin) / static_cast<float> (stop - centre);

            const float juceBin = static_cast<float> (sourceBin * fftSize)
                                  / static_cast<float> (madmomFftBins);
            const int lo = std::clamp (static_cast<int> (std::floor (juceBin)), 0, nBins - 1);
            const int hi = std::min (lo + 1, nBins - 1);
            const float frac = juceBin - static_cast<float> (lo);
            impl->weight[b][lo] += w * (1.0f - frac);
            impl->weight[b][hi] += w * frac;
            sum += w;
        }
        if (sum > 1.0e-8f)
        {
            for (int k = 1; k < nBins; ++k)
                impl->weight[b][k] /= sum;
        }
    }
}

void LogSpectFeatures::process (const float* mono, int numSamples) noexcept
{
    if (impl == nullptr || mono == nullptr || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        impl->ring[static_cast<size_t> (writePos)] = mono[i];
        writePos = (writePos + 1) % frameLen;
        if (--samplesUntilHop <= 0)
        {
            samplesUntilHop = hop;
            processHop();
        }
    }
}

void LogSpectFeatures::processHop() noexcept
{
    auto& work = impl->fftWork;
    std::fill (work.begin(), work.end(), 0.0f);
    int idx = writePos;
    for (int i = 0; i < frameLen; ++i)
    {
        work[static_cast<size_t> (i)] = impl->ring[static_cast<size_t> (idx)];
        idx = (idx + 1) % frameLen;
    }
    impl->hann.multiplyWithWindowingTable (work.data(), static_cast<size_t> (frameLen));
    impl->fft.performFrequencyOnlyForwardTransform (work.data(), true);

    const int nBins = fftSize / 2;
    float bands[kBands];
    for (int b = 0; b < kBands; ++b)
    {
        float acc = 0.0f;
        for (int k = 1; k < nBins; ++k)
            acc += impl->weight[b][k] * work[static_cast<size_t> (k)];
        bands[b] = std::log10 (std::max (acc + 1.0f, 1.0e-6f));
    }

    for (int b = 0; b < kBands; ++b)
    {
        pending[b] = bands[b];
        const float delta = havePrev ? bands[b] - prevBand[b] : 0.0f;
        pending[kBands + b] = std::max (0.0f, delta);
        prevBand[b] = bands[b];
    }
    havePrev = true;
    if (impl->qCount == Impl::kQueue)
    {
        impl->qRead = (impl->qRead + 1) % Impl::kQueue;
        --impl->qCount;
    }
    const int w = (impl->qRead + impl->qCount) % Impl::kQueue;
    std::memcpy (impl->frameQ.data() + static_cast<size_t> (w * kDim),
                 pending, sizeof (float) * static_cast<size_t> (kDim));
    ++impl->qCount;
}

bool LogSpectFeatures::popFrame (float* dest272) noexcept
{
    if (impl == nullptr || dest272 == nullptr || impl->qCount <= 0)
        return false;
    std::memcpy (dest272,
                 impl->frameQ.data() + static_cast<size_t> (impl->qRead * kDim),
                 sizeof (float) * static_cast<size_t> (kDim));
    impl->qRead = (impl->qRead + 1) % Impl::kQueue;
    --impl->qCount;
    return true;
}

void LinearResampler::prepare (double srcRate, double dstRate)
{
    const double src = srcRate > 1.0 ? srcRate : 48000.0;
    const double dst = dstRate > 1.0 ? dstRate : 22050.0;
    step = src / dst;
    q.assign (16384, 0.0f);
    reset();
}

void LinearResampler::reset() noexcept
{
    qRead = 0;
    qWrite = 0;
    frac = 0.0;
}

int LinearResampler::process (const float* in, int nIn, float* out, int maxOut) noexcept
{
    if (in == nullptr || out == nullptr || nIn <= 0 || maxOut <= 0)
        return 0;

    if (qRead > 0)
    {
        const int remain = qWrite - qRead;
        if (remain > 0)
            std::memmove (q.data(), q.data() + qRead, static_cast<size_t> (remain) * sizeof (float));
        qWrite = remain;
        qRead = 0;
    }

    if (qWrite + nIn > static_cast<int> (q.size()))
        nIn = static_cast<int> (q.size()) - qWrite;
    if (nIn > 0)
    {
        std::memcpy (q.data() + qWrite, in, static_cast<size_t> (nIn) * sizeof (float));
        qWrite += nIn;
    }

    int nOut = 0;
    while (nOut < maxOut && (qWrite - qRead) >= 2)
    {
        const float a = q[static_cast<size_t> (qRead)];
        const float b = q[static_cast<size_t> (qRead + 1)];
        const float t = static_cast<float> (std::min (frac, 1.0));
        out[nOut++] = a + (b - a) * t;
        frac += step;
        while (frac >= 1.0 && (qWrite - qRead) >= 1)
        {
            ++qRead;
            frac -= 1.0;
        }
    }
    return nOut;
}

} // namespace vp
