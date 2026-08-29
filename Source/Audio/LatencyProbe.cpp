#include "Audio/LatencyProbe.h"

#include <algorithm>

namespace vp
{

void LatencyProbe::prepare (double sr)
{
    sampleRate = sr > 1.0 ? sr : 48000.0;
    chirpLen = static_cast<int> (sampleRate * kChirpSeconds);
    captureLen = static_cast<int> (sampleRate * kCaptureSeconds);
    chirp.assign (static_cast<size_t> (chirpLen), 0.0f);
    capture.assign (static_cast<size_t> (captureLen), 0.0f);

    // A linear sweep with a raised-cosine window on it. The window matters:
    // an unwindowed sweep starts and ends on a step, and a step correlates
    // with every other step in the room.
    const double k = (kChirpHiHz - kChirpLoHz) / kChirpSeconds;
    double phase = 0.0;
    for (int i = 0; i < chirpLen; ++i)
    {
        const double t = static_cast<double> (i) / sampleRate;
        const double f = kChirpLoHz + k * t;
        phase += 2.0 * 3.14159265358979 * f / sampleRate;
        const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979
                                               * static_cast<double> (i)
                                               / static_cast<double> (chirpLen - 1));
        chirp[static_cast<size_t> (i)] = static_cast<float> (std::sin (phase) * w);
    }

    pos = 0;
    clarity = 0.0f;
    state.store (idle, std::memory_order_relaxed);
    wantStart.store (false, std::memory_order_relaxed);
}

void LatencyProbe::start() noexcept
{
    wantStart.store (true, std::memory_order_release);
}

void LatencyProbe::cancel() noexcept
{
    wantStart.store (false, std::memory_order_relaxed);
    state.store (idle, std::memory_order_relaxed);
}

void LatencyProbe::process (const float* in, float* outL, float* outR, int numSamples) noexcept
{
    if (chirpLen <= 0 || captureLen <= 0)
        return;

    if (wantStart.exchange (false, std::memory_order_acquire))
    {
        pos = 0;
        std::fill (capture.begin(), capture.end(), 0.0f);
        state.store (running, std::memory_order_relaxed);
    }

    if (state.load (std::memory_order_relaxed) != running)
        return;

    for (int i = 0; i < numSamples && pos < captureLen; ++i, ++pos)
    {
        if (pos < chirpLen)
        {
            const float v = chirp[static_cast<size_t> (pos)] * kChirpGain;
            if (outL != nullptr) outL[i] += v;
            if (outR != nullptr) outR[i] += v;
        }
        capture[static_cast<size_t> (pos)] = in != nullptr ? in[i] : 0.0f;
    }

    if (pos >= captureLen)
        state.store (captured, std::memory_order_release);
}

float LatencyProbe::analyse() noexcept
{
    if (state.load (std::memory_order_acquire) != captured)
        return -1.0f;

    clarity = 0.0f;

    // Correlate the capture against the sweep. The search runs to the end of
    // the capture minus the sweep's own length, so a round trip that did not
    // fit in the window reads as "not found" rather than as a wrong answer.
    const int lastLag = captureLen - chirpLen - 1;
    if (lastLag <= 0)
    {
        state.store (idle, std::memory_order_release);
        return -1.0f;
    }

    // Normalised at each lag by the energy under the window, so a loud passage
    // somewhere in the room cannot out-score the sweep simply by being loud.
    // Without it a kick drum landing during the capture wins every time.
    double bestScore = 0.0;
    int bestLag = -1;
    std::vector<double> score (static_cast<size_t> (lastLag), 0.0);
    for (int lag = 0; lag < lastLag; ++lag)
    {
        double dot = 0.0, energy = 1.0e-12;
        for (int i = 0; i < chirpLen; ++i)
        {
            const double x = capture[static_cast<size_t> (lag + i)];
            dot += x * chirp[static_cast<size_t> (i)];
            energy += x * x;
        }
        const double s = std::fabs (dot) / std::sqrt (energy);
        score[static_cast<size_t> (lag)] = s;
        if (s > bestScore)
        {
            bestScore = s;
            bestLag = lag;
        }
    }

    if (bestLag < 0 || bestScore <= 0.0)
    {
        state.store (idle, std::memory_order_release);
        return -1.0f;
    }

    // The best peak that is not this one, so "clear" means clear of the room
    // and not merely large. A whole sweep's width either side is excluded,
    // because the correlation of a sweep with itself is broad.
    const int guard = chirpLen;
    double rival = 0.0;
    for (int lag = 0; lag < lastLag; ++lag)
        if (std::abs (lag - bestLag) > guard)
            rival = std::max (rival, score[static_cast<size_t> (lag)]);

    clarity = rival > 1.0e-12 ? static_cast<float> (bestScore / rival) : 99.0f;

    state.store (idle, std::memory_order_release);

    const double seconds = static_cast<double> (bestLag) / sampleRate;
    if (clarity < kMinClarity || seconds < kMinPlausibleSec)
        return -1.0f;
    return static_cast<float> (seconds * 1000.0);
}

} // namespace vp
