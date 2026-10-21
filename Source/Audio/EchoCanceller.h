#pragma once

// The iPad microphone hears the app's own part come back out of the speaker,
// ~30 ms late and reshaped by the speaker and the room. Followed, that echo is
// a loop: the clock leans late, the part plays late, the echo arrives later
// still, and the tempo sags (docs/TODO.md item 133: 9 songs through the probe
// room, -0.15 BPM with the part as loud as the song at the mic, -0.52 at twice,
// -1.6 on a song at 86).
//
// The band canceller in VirtualPercussionEngine fits one delay and three gains,
// and a room is not that: it takes 7.6-11.5% of the return out of the probe
// room. This is an adaptive FIR of the speaker-to-mic path itself - a
// partitioned-block frequency-domain NLMS (the MDF structure) - on the analysis
// signal only; what the listener hears is never touched.
//
// It adds no latency. The path starts `bulkDelay` samples after the output
// (found by the engine's delay search), so the reference for every input
// sample of the current callback, and of the internal block after it, is
// already in the output ring: the echo estimate for a block is computed before
// its first sample arrives and subtracted sample by sample, and the filter is
// updated once the block's error is complete. Callbacks of any size work as
// long as the path starts later than one callback plus one internal block.
//
// The song is always playing on top of the echo - permanent double talk - so
// the step is small and the filter learns over seconds; the path of an iPad on
// a stand does not move faster than that.

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

namespace vp
{
class EchoCanceller
{
public:
    static constexpr int kBlock = 256;                 // internal block, samples
    static constexpr int kParts = 8;                   // filter length kParts * kBlock
    static constexpr int kFft   = 2 * kBlock;
    static constexpr int kBins  = kBlock + 1;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        fft = std::make_unique<juce::dsp::FFT> (9);   // 512 = kFft
        work.assign (2 * kFft, 0.0f);
        xBlock.assign (kFft, 0.0f);
        X.assign (static_cast<size_t> (kParts * kBins), {});
        W.assign (static_cast<size_t> (kParts * kBins), {});
        pow.assign (kBins, 0.0f);
        yHat.assign (kBlock, 0.0f);
        err.assign (kBlock, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        std::fill (W.begin(), W.end(), std::complex<float> {});
        std::fill (X.begin(), X.end(), std::complex<float> {});
        std::fill (pow.begin(), pow.end(), 0.0f);
        std::fill (xBlock.begin(), xBlock.end(), 0.0f);
        pos = 0;
        haveEstimate = false;
        bulk = -1;
        constrainNext = 0;
        errEnergy = 0.0;
        inEnergy = 0.0;
        share = 0.0f;
        applying = false;
    }

    /** Learns the path and, once our echo is a real share of the input, takes
        it out of `mic` in place. `ref (k)` is the output sample k samples
        before the first sample of this callback (k >= 1). Returns whether it
        subtracted; it leaves `mic` alone when the path starts too close for
        this callback size, and the band canceller is then all there is. */
    template <typename RefAt>
    bool process (float* mic, int numSamples, int bulkDelay, RefAt&& ref) noexcept
    {
        if (fft == nullptr || bulkDelay < numSamples + kBlock)
            return false;
        // The path moved by more than a fraction of a block: what was learnt
        // belongs to another alignment.
        if (bulk < 0 || std::abs (bulkDelay - bulk) > kBlock / 4)
        {
            reset();
            bulk = bulkDelay;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            if (! haveEstimate)
            {
                // The reference for the block starting at input sample i: output
                // sample (i + j - bulk) relative to this callback, j = 0..kBlock-1,
                // all in the past because bulk >= numSamples + kBlock.
                for (int j = 0; j < kBlock; ++j)
                    xBlock[static_cast<size_t> (kBlock + j)] = ref (bulk - (i + j));
                pushReference();
                estimate();
                haveEstimate = true;
            }
            const float e = mic[i] - yHat[static_cast<size_t> (pos)];
            errEnergy = errEnergy * kShareDecay + static_cast<double> (e) * e;
            inEnergy = inEnergy * kShareDecay + static_cast<double> (mic[i]) * mic[i];
            err[static_cast<size_t> (pos)] = e;
            if (applying)
                mic[i] = e;
            if (++pos == kBlock)
            {
                adapt();
                pos = 0;
                haveEstimate = false;
                std::copy (xBlock.begin() + kBlock, xBlock.end(), xBlock.begin());
            }
        }
        // The share is what subtracting the estimate actually takes off the
        // input, not the estimate's own energy. A real echo, removed, lowers
        // the input by its share; an estimate that is only the noise the
        // music (permanent double talk) shakes into the filter is uncorrelated
        // with the input and *raises* it, so it reads zero. The estimate's
        // energy read as a share of 0.02+ on the iPad with the device's volume
        // at zero - SENTO ME STESSO all the time (reported 2026-10-21).
        share = inEnergy > 1.0e-12
                    ? static_cast<float> (std::clamp (1.0 - errEnergy / inEnergy, 0.0, 1.0))
                    : 0.0f;
        // Learn always, subtract only when there is an echo to take out. With
        // none, the filter still fits the small likeness between our shaker
        // and the band's hi-hat and would take some of the song out with it.
        if (share > kApplyOn)
            applying = true;
        else if (share < kApplyOff)
            applying = false;
        return applying;
    }

    /** How much of what came in was our own echo, 0..1, over the last ~2 s:
        the share of the input's energy that subtracting the estimate removes. */
    float selfShare() const noexcept { return share; }

private:
    // ~2 s at 48 kHz per sample.
    static constexpr double kShareDecay = 1.0 - 1.0 / 96000.0;
    // NLMS step against permanent double talk; see the header.
    static constexpr float kMu = 0.05f;
    // Mean reference power per bin (unnormalised 512-point FFT) below which
    // the output counts as silent and nothing is learnt. Without it the step,
    // divided by a near-zero reference, blew the filter up when the part
    // stopped (NaN 104 s into a song on the probe room).
    static constexpr float kSilentPow = 1.0e-2f;
    // When to subtract, and when the screen says SENTO ME STESSO (0.02 is
    // kSelfShareWarn). 9 songs through the probe return, after the band fit:
    // no return 0.000 (p95 <= 0.001, also with the song itself through the
    // probe room), the part as loud as the song 0.000-0.015 (room 0.000-0.039),
    // twice as loud 0.036-0.083, above 0.02 85-100% of the time. Subtracting
    // with no echo costs: from 0.005 on the old reading it moved 35 of 54
    // --speaker files, the phase a little worse (p90 62 -> 70 ms). Tempo
    // against the teacher, without -> with: equal level -0.15 -> -0.06 BPM,
    // twice -0.52 -> -0.00.
    static constexpr float kApplyOn = 0.02f, kApplyOff = 0.012f;

    void forward (const float* in, std::complex<float>* out) noexcept
    {
        std::copy (in, in + kFft, work.begin());
        std::fill (work.begin() + kFft, work.end(), 0.0f);
        fft->performRealOnlyForwardTransform (work.data(), true);
        for (int k = 0; k < kBins; ++k)
            out[k] = { work[static_cast<size_t> (2 * k)], work[static_cast<size_t> (2 * k + 1)] };
    }

    void inverse (const std::complex<float>* in) noexcept
    {
        for (int k = 0; k < kBins; ++k)
        {
            work[static_cast<size_t> (2 * k)] = in[k].real();
            work[static_cast<size_t> (2 * k + 1)] = in[k].imag();
        }
        std::fill (work.begin() + 2 * kBins, work.end(), 0.0f);
        fft->performRealOnlyInverseTransform (work.data());
    }

    void pushReference() noexcept
    {
        // Frequency-domain delay line: partition p holds the spectrum of the
        // reference window p blocks ago.
        std::move_backward (X.begin(), X.end() - kBins, X.end());
        forward (xBlock.data(), X.data());
        for (int k = 0; k < kBins; ++k)
            pow[static_cast<size_t> (k)] = 0.9f * pow[static_cast<size_t> (k)] + 0.1f * std::norm (X[static_cast<size_t> (k)]);
    }

    void estimate() noexcept
    {
        // Never hand the analysis a non-finite sample: start again instead.
        for (const auto& w : W)
            if (! std::isfinite (w.real()) || ! std::isfinite (w.imag()))
            {
                const int keep = bulk;
                reset();
                bulk = keep;
                break;
            }
        std::vector<std::complex<float>>& Y = scratch;
        std::fill (Y.begin(), Y.end(), std::complex<float> {});
        for (int p = 0; p < kParts; ++p)
            for (int k = 0; k < kBins; ++k)
                Y[static_cast<size_t> (k)] += W[static_cast<size_t> (p * kBins + k)] * X[static_cast<size_t> (p * kBins + k)];
        inverse (Y.data());
        for (int j = 0; j < kBlock; ++j)
            yHat[static_cast<size_t> (j)] = work[static_cast<size_t> (kBlock + j)];
    }

    void adapt() noexcept
    {
        float* buf = errWindow.data();
        std::fill (buf, buf + kBlock, 0.0f);
        std::copy (err.begin(), err.end(), buf + kBlock);
        std::complex<float>* E = errSpec.data();
        forward (buf, E);
        float total = 0.0f;
        for (int k = 0; k < kBins; ++k)
            total += pow[static_cast<size_t> (k)];
        // Nothing of ours went out: nothing to learn the path from, and dividing
        // the error by a near-zero reference power is how the filter blows up
        // when the part stops (seen: NaN 104 s into a song).
        if (total / kBins < kSilentPow)
            return;
        const float floorPow = 1.0e-2f * total / kBins + kSilentPow;
        for (int p = 0; p < kParts; ++p)
            for (int k = 0; k < kBins; ++k)
            {
                const size_t at = static_cast<size_t> (p * kBins + k);
                W[at] += (kMu / (pow[static_cast<size_t> (k)] * kParts + floorPow))
                         * std::conj (X[at]) * E[k];
            }
        // Constrain one partition per block back to a linear (not circular)
        // convolution: its second half in time goes to zero.
        std::complex<float>* Wp = W.data() + constrainNext * kBins;
        inverse (Wp);
        std::fill (work.begin() + kBlock, work.begin() + kFft, 0.0f);
        std::copy (work.begin(), work.begin() + kFft, buf);
        forward (buf, Wp);
        constrainNext = (constrainNext + 1) % kParts;
    }

    double sr = 48000.0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> work, xBlock, pow, yHat, err;
    std::vector<float> errWindow = std::vector<float> (kFft, 0.0f);
    std::vector<std::complex<float>> errSpec = std::vector<std::complex<float>> (kBins);
    std::vector<std::complex<float>> X, W, scratch = std::vector<std::complex<float>> (kBins);
    int pos = 0, bulk = -1, constrainNext = 0;
    bool haveEstimate = false, applying = false;
    double errEnergy = 0.0, inEnergy = 0.0;
    float share = 0.0f;
};
} // namespace vp
