#include "Stretch/LoopStretcher.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(VP_USE_SIGNALSMITH) && VP_USE_SIGNALSMITH
#include <signalsmith-stretch/signalsmith-stretch.h>
#endif

namespace vp
{

namespace
{
    /** WSOLA fallback geometry. About 21 ms of window at 48 kHz, a quarter of
        it per synthesis hop, and a search of half a hop either side for the
        offset that lines the next grain up with what is already in the
        accumulator. Without the search this is plain overlap-add, which is what
        `TimeStretchEngine` does and what makes a stretched conga sound like a
        stretched conga. */
    constexpr int kGrain = 1024;
    constexpr int kSynthHop = kGrain / 4;
    constexpr int kSearch = kSynthHop / 2;

    int nextPow2 (int n) noexcept
    {
        int p = 1;
        while (p < n)
            p <<= 1;
        return p;
    }
} // namespace

struct LoopStretcher::Impl
{
#if defined(VP_USE_SIGNALSMITH) && VP_USE_SIGNALSMITH
    // Seeded rather than default-constructed: the default constructor pulls
    // from std::random_device, and two runs of the same test on the same input
    // then differ in the phase-randomisation of near-silent bands. The
    // percussion elsewhere in this app is deterministic for the same reason
    // (see DeterministicRng), and a stretcher that is not makes every
    // measurement below a distribution instead of a number.
    signalsmith::stretch::SignalsmithStretch<float> stretch { 0x5EED1234L };
#endif

    // --- WSOLA fallback -------------------------------------------------
    std::vector<float> ringL, ringR;   // source history, power-of-two masked
    std::vector<float> accL, accR;     // overlap-add accumulator, kGrain long
    std::vector<float> window;
    std::vector<float> pendL, pendR;   // output produced but not yet handed back
    int    ringMask = 0;
    long long writePos = 0;            // frames pushed into the ring, ever
    double readPos = 0.0;              // next analysis grain start, same units
    int    pendCount = 0;
    int    pendRead = 0;
    bool   fallbackPrimed = false;

    // --- measured latency ------------------------------------------------
    // inputLeadFrames (r) = leadA + leadB * r. Measured in prepare() rather
    // than taken from the library's own numbers: the two are not the same
    // quantity (one is an analysis centre, the other is where a stroke is
    // heard), and a constant that is only true for one version of one library
    // is a constant that goes quietly wrong.
    double leadA = 0.0;
    double leadB = 0.0;
};

LoopStretcher::LoopStretcher() : impl (std::make_unique<Impl>()) {}
LoopStretcher::~LoopStretcher() = default;

bool LoopStretcher::isSignalsmith() noexcept
{
#if defined(VP_USE_SIGNALSMITH) && VP_USE_SIGNALSMITH
    return true;
#else
    return false;
#endif
}

int LoopStretcher::inputFramesFor (int outputFrames, double ratio) const noexcept
{
    if (outputFrames <= 0)
        return 0;
    const double r = std::clamp (ratio, 0.25, static_cast<double> (maxRatio));
    const int n = static_cast<int> (std::lround (static_cast<double> (outputFrames) * r));
    return std::max (1, n);
}

double LoopStretcher::inputLeadFrames (double ratio) const noexcept
{
    return impl->leadA + impl->leadB * std::clamp (ratio, 0.25, static_cast<double> (maxRatio));
}

int LoopStretcher::primeFrames() const noexcept
{
#if defined(VP_USE_SIGNALSMITH) && VP_USE_SIGNALSMITH
    return impl->stretch.seekLength();
#else
    return 2 * kGrain;
#endif
}

void LoopStretcher::reset() noexcept
{
    // Nothing to throw away before `prepare` has sized anything, and asking
    // anyway is not harmless: Signalsmith's own reset divides by a block size
    // that is still zero, which is a SIGFPE rather than a no-op. Owners call
    // reset from their own reset, which can run before they have been prepared.
    if (! prepared)
        return;

#if defined(VP_USE_SIGNALSMITH) && VP_USE_SIGNALSMITH
    impl->stretch.reset();
#endif
    std::fill (impl->ringL.begin(), impl->ringL.end(), 0.0f);
    std::fill (impl->ringR.begin(), impl->ringR.end(), 0.0f);
    std::fill (impl->accL.begin(), impl->accL.end(), 0.0f);
    std::fill (impl->accR.begin(), impl->accR.end(), 0.0f);
    impl->writePos = 0;
    impl->readPos = 0.0;
    impl->pendCount = 0;
    impl->pendRead = 0;
    impl->fallbackPrimed = false;
}

void LoopStretcher::prepare (double sr, int maxBlk, float maxR) noexcept
{
    sampleRate = sr > 1.0 ? sr : 48000.0;
    maxBlock = std::max (32, maxBlk);
    maxRatio = std::clamp (maxR, 1.0f, 4.0f);

#if defined(VP_USE_SIGNALSMITH) && VP_USE_SIGNALSMITH
    impl->stretch.presetDefault (2, static_cast<float> (sampleRate));
    impl->stretch.setTransposeFactor (1.0f); // pitch stays where it was recorded
#endif

    const int needed = 4 * kGrain + static_cast<int> (static_cast<float> (maxBlock) * maxRatio) + 8;
    const int ringSize = nextPow2 (needed);
    impl->ringMask = ringSize - 1;
    impl->ringL.assign (static_cast<size_t> (ringSize), 0.0f);
    impl->ringR.assign (static_cast<size_t> (ringSize), 0.0f);
    impl->accL.assign (static_cast<size_t> (kGrain), 0.0f);
    impl->accR.assign (static_cast<size_t> (kGrain), 0.0f);
    impl->pendL.assign (static_cast<size_t> (kGrain + maxBlock), 0.0f);
    impl->pendR.assign (static_cast<size_t> (kGrain + maxBlock), 0.0f);
    impl->window.assign (static_cast<size_t> (kGrain), 0.0f);
    for (int i = 0; i < kGrain; ++i)
    {
        const double t = static_cast<double> (i) / static_cast<double> (kGrain);
        // Hann at a quarter-window hop sums to a constant 2, so the grains are
        // halved here rather than divided out per sample later.
        impl->window[static_cast<size_t> (i)] =
            static_cast<float> (0.5 * (0.5 - 0.5 * std::cos (6.283185307179586 * t)));
    }

    prepared = true;
    reset();

    // And now find out what this backend actually does to timing. See Impl.
    calibrateLead();
    reset();
}

void LoopStretcher::process (const float* inL, const float* inR, int inFrames,
                             float* outL, float* outR, int outFrames) noexcept
{
    if (outL == nullptr || outFrames <= 0)
        return;
    if (outR == nullptr)
        outR = outL;
    if (! prepared || inL == nullptr || inFrames <= 0)
    {
        std::fill (outL, outL + outFrames, 0.0f);
        if (outR != outL)
            std::fill (outR, outR + outFrames, 0.0f);
        return;
    }
    if (inR == nullptr)
        inR = inL;

#if defined(VP_USE_SIGNALSMITH) && VP_USE_SIGNALSMITH
    // Signalsmith wants an indexable [channel][frame]; the loop player holds
    // its two channels apart, so this is the adapter and it owns nothing.
    struct TwoIn
    {
        const float* c[2];
        const float* operator[] (int i) const noexcept { return c[i]; }
    };
    struct TwoOut
    {
        float* c[2];
        float* operator[] (int i) const noexcept { return c[i]; }
    };
    TwoIn in { { inL, inR } };
    TwoOut out { { outL, outR } };
    impl->stretch.process (in, inFrames, out, outFrames);
    return;
#else
    wsola (inL, inR, inFrames, outL, outR, outFrames);
#endif
}

void LoopStretcher::prime (const float* inL, const float* inR, int frames, double ratio) noexcept
{
    if (! prepared || inL == nullptr || frames <= 0)
        return;
    if (inR == nullptr)
        inR = inL;

#if defined(VP_USE_SIGNALSMITH) && VP_USE_SIGNALSMITH
    struct TwoIn
    {
        const float* c[2];
        const float* operator[] (int i) const noexcept { return c[i]; }
    };
    TwoIn in { { inL, inR } };
    impl->stretch.seek (in, frames, ratio > 0.0 ? ratio : 1.0);
#else
    (void) ratio;
    // The fallback's window is 21 ms, so the honest thing is to fill the ring
    // with the run of source that ends where playback is about to start and
    // begin one window behind it. Nothing is emitted from the priming itself.
    std::fill (impl->accL.begin(), impl->accL.end(), 0.0f);
    std::fill (impl->accR.begin(), impl->accR.end(), 0.0f);
    impl->pendCount = 0;
    impl->pendRead = 0;
    impl->writePos = 0;
    for (int i = 0; i < frames; ++i)
    {
        const int w = static_cast<int> (impl->writePos) & impl->ringMask;
        impl->ringL[static_cast<size_t> (w)] = inL[i];
        impl->ringR[static_cast<size_t> (w)] = inR[i];
        ++impl->writePos;
    }
    impl->readPos = static_cast<double> (impl->writePos) - static_cast<double> (kGrain + kSearch);
    if (impl->readPos < 0.0)
        impl->readPos = 0.0;
    impl->fallbackPrimed = true;
#endif
}


void LoopStretcher::wsola (const float* inL, const float* inR, int inFrames,
                           float* outL, float* outR, int outFrames) noexcept
{
    Impl& d = *impl;

    // Whatever the caller handed us joins the history first. `writePos` is an
    // absolute frame count, so the ring never has to be told it wrapped.
    for (int i = 0; i < inFrames; ++i)
    {
        const int w = static_cast<int> (d.writePos) & d.ringMask;
        d.ringL[static_cast<size_t> (w)] = inL[i];
        d.ringR[static_cast<size_t> (w)] = inR[i];
        ++d.writePos;
    }
    if (! d.fallbackPrimed)
    {
        // One window plus the search behind the newest input, and deliberately
        // *not* clamped at zero. The window has to have somewhere to look
        // ahead, and the caller has already put the source read ahead by
        // `inputLeadFrames` to pay for it. Clamped instead, the read spent the
        // whole run pinned against `maxRead` below, and how it settled there
        // depended on how much input arrived per call - which made this
        // backend's latency a function of the device's buffer size: measured at
        // 1018 frames on a 128-frame buffer and 263 on a 4096-frame one, and
        // the part correspondingly early or late. The ring starts zeroed, so a
        // negative position reads silence, which is what a stretcher that has
        // not been given any history should produce.
        d.readPos = static_cast<double> (d.writePos)
                    - static_cast<double> (inFrames + kGrain + kSearch);
        d.fallbackPrimed = true;
    }

    auto tapL = [&d] (double pos) noexcept
    {
        const long long i0 = static_cast<long long> (std::floor (pos));
        const float t = static_cast<float> (pos - static_cast<double> (i0));
        const float a = d.ringL[static_cast<size_t> (static_cast<int> (i0) & d.ringMask)];
        const float b = d.ringL[static_cast<size_t> (static_cast<int> (i0 + 1) & d.ringMask)];
        return a + (b - a) * t;
    };
    auto tapR = [&d] (double pos) noexcept
    {
        const long long i0 = static_cast<long long> (std::floor (pos));
        const float t = static_cast<float> (pos - static_cast<double> (i0));
        const float a = d.ringR[static_cast<size_t> (static_cast<int> (i0) & d.ringMask)];
        const float b = d.ringR[static_cast<size_t> (static_cast<int> (i0 + 1) & d.ringMask)];
        return a + (b - a) * t;
    };

    // The ratio this call is being asked to run at, taken from what it was
    // given rather than from a parameter: the caller has already decided how
    // much source one block of output costs, and deriving it here keeps the two
    // from ever disagreeing.
    const double ratio = static_cast<double> (inFrames) / static_cast<double> (outFrames);

    int written = 0;
    while (written < outFrames)
    {
        if (d.pendCount > 0)
        {
            const int n = std::min (d.pendCount, outFrames - written);
            std::memcpy (outL + written, d.pendL.data() + d.pendRead,
                         static_cast<size_t> (n) * sizeof (float));
            std::memcpy (outR + written, d.pendR.data() + d.pendRead,
                         static_cast<size_t> (n) * sizeof (float));
            d.pendRead += n;
            d.pendCount -= n;
            written += n;
            continue;
        }

        // How far the next grain would nominally start from, and the offset
        // within the search window whose first samples line up best with what
        // the accumulator is already holding. This is the whole difference
        // between WSOLA and plain overlap-add.
        double want = d.readPos;
        const double maxRead = static_cast<double> (d.writePos) - static_cast<double> (kGrain + 2);
        int bestOffset = 0;
        float bestScore = -1.0e30f;
        for (int off = -kSearch; off <= kSearch; off += 8)
        {
            const double start = want + static_cast<double> (off);
            if (start > maxRead)
                continue;
            float score = 0.0f;
            for (int i = 0; i < kSynthHop; i += 4)
            {
                const float a = d.accL[static_cast<size_t> (i)];
                score += a * tapL (start + static_cast<double> (i));
            }
            if (score > bestScore)
            {
                bestScore = score;
                bestOffset = off;
            }
        }
        double start = want + static_cast<double> (bestOffset);
        if (start > maxRead)
            start = maxRead;

        for (int i = 0; i < kGrain; ++i)
        {
            const float w = d.window[static_cast<size_t> (i)];
            d.accL[static_cast<size_t> (i)] += tapL (start + static_cast<double> (i)) * w;
            d.accR[static_cast<size_t> (i)] += tapR (start + static_cast<double> (i)) * w;
        }

        std::memcpy (d.pendL.data(), d.accL.data(), static_cast<size_t> (kSynthHop) * sizeof (float));
        std::memcpy (d.pendR.data(), d.accR.data(), static_cast<size_t> (kSynthHop) * sizeof (float));
        d.pendRead = 0;
        d.pendCount = kSynthHop;

        std::memmove (d.accL.data(), d.accL.data() + kSynthHop,
                      static_cast<size_t> (kGrain - kSynthHop) * sizeof (float));
        std::memmove (d.accR.data(), d.accR.data() + kSynthHop,
                      static_cast<size_t> (kGrain - kSynthHop) * sizeof (float));
        std::fill (d.accL.begin() + (kGrain - kSynthHop), d.accL.end(), 0.0f);
        std::fill (d.accR.begin() + (kGrain - kSynthHop), d.accR.end(), 0.0f);

        // The grid the next grain is measured from advances by the nominal hop,
        // not by wherever the search happened to land. Letting the search move
        // the grid is how a WSOLA walks away from its source over a minute.
        d.readPos += static_cast<double> (kSynthHop) * ratio;
    }
}

void LoopStretcher::calibrateLead() noexcept
{
    Impl& d = *impl;
    d.leadA = 0.0;
    d.leadB = 0.0;

    // A short percussive burst - two hundred cycles of nothing, then a windowed
    // 1 kHz tone a few milliseconds long - pushed through the backend at two
    // ratios. Where it comes back out says how far ahead the source read has to
    // run for a stroke to be *heard* on the beat. Measured, because that is not
    // the same quantity as the analysis latency a library reports, and because
    // a number hard-coded from one version of one library is a number that goes
    // quietly wrong on the next.
    // Long enough that the burst and everything the backend delays it by are
    // both inside the run, whatever block size this object was prepared at. Cut
    // short, the centre-of-energy below is measured on the leading edge of a
    // burst whose tail never came out, and the lead it returns is wrong by
    // about the stretcher's own block - which is exactly the error the phase
    // test then reports at 4096 and not at 128.
    const int kLen = 32768 + 8 * maxBlock;
    const int kBurstAt = 4096;
    constexpr int kBurstLen = 240;

    std::vector<float> src (static_cast<size_t> (kLen), 0.0f);
    for (int i = 0; i < kBurstLen; ++i)
    {
        const double t = static_cast<double> (i) / static_cast<double> (kBurstLen);
        const double env = 0.5 - 0.5 * std::cos (6.283185307179586 * t);
        src[static_cast<size_t> (kBurstAt + i)] =
            static_cast<float> (env * std::sin (6.283185307179586 * 1000.0
                                                * static_cast<double> (i) / sampleRate));
    }

    std::vector<float> outL (static_cast<size_t> (kLen + maxBlock), 0.0f);
    std::vector<float> outR (static_cast<size_t> (kLen + maxBlock), 0.0f);

    auto runAt = [&] (double ratio) -> double
    {
        reset();
        std::fill (outL.begin(), outL.end(), 0.0f);
        std::fill (outR.begin(), outR.end(), 0.0f);

        int inPos = 0;
        int outPos = 0;
        while (true)
        {
            const int need = inputFramesFor (maxBlock, ratio);
            if (inPos + need > kLen || outPos + maxBlock > static_cast<int> (outL.size()))
                break;
            process (src.data() + inPos, src.data() + inPos, need,
                     outL.data() + outPos, outR.data() + outPos, maxBlock);
            inPos += need;
            outPos += maxBlock;
        }

        // Centre of energy rather than the loudest sample: a phase vocoder
        // spreads a transient over its window roughly symmetrically, and the
        // peak of the spread wanders while its centre does not.
        double num = 0.0;
        double den = 0.0;
        for (int i = 0; i < outPos; ++i)
        {
            const double e = static_cast<double> (outL[static_cast<size_t> (i)])
                             * static_cast<double> (outL[static_cast<size_t> (i)]);
            num += e * static_cast<double> (i);
            den += e;
        }
        if (den <= 1.0e-12)
            return -1.0;
        const double centre = num / den;
        // Where the burst would have landed with no latency at all, and how far
        // past that it actually came out - expressed in *input* frames, which is
        // the unit the loop player reads its source in.
        return (centre - static_cast<double> (kBurstAt) / ratio) * ratio;
    };

    const double d1 = runAt (1.0);
    const double d2 = runAt (1.5);
    if (d1 < 0.0 || d2 < 0.0)
        return;

    // lead(r) = a + b*r through the two measurements.
    d.leadB = (d2 - d1) / 0.5;
    d.leadA = d1 - d.leadB;

    // A backend that measured as *negative* lead would have the player reading
    // behind the beat to compensate, which is never right; clamp rather than
    // trust it, and leave the rest to the phase test.
    if (d.leadA + d.leadB < 0.0)
    {
        d.leadA = 0.0;
        d.leadB = 0.0;
    }
}

} // namespace vp
