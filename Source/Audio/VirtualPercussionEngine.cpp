#include "Audio/VirtualPercussionEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace vp
{

namespace
{
    // Where the analysis signal is held for the network, and it is not a free
    // parameter. BeatNet's features are log10(magnitude + 1): the +1 knee means
    // the level is part of the model's input rather than something the
    // normalisation removes, and madmom feeds the network integer-scaled audio,
    // several orders of magnitude above float [-1, 1]. Too quiet and the whole
    // filterbank sits on the linear part of that knee, where the network was
    // never trained.
    //
    // Measured end to end - 30 songs, 60 to 176 BPM, four styles, counting how
    // often the tracker settles on the wrong metrical level:
    //
    //     target peak   0.04  0.06  0.09  0.12  0.16  0.20  0.28  0.40  0.60
    //     wrong octave     7    13    13     8     4     2     2     4     7
    //
    // 0.12 sat on the near side of the optimum, and its failures were the
    // half-tempo readings above 150 BPM and the double-tempo readings below 72.
    constexpr float kMakeupTargetPeak = 0.20f;

    // Below this there is nothing to normalise, only noise to amplify. Room and
    // iPad-speaker-to-mic material commonly sits around 0.001-0.008, well above
    // it, which is the case this stage exists for.
    constexpr float kMakeupFloor = 0.0004f;
    constexpr float kMakeupMaxGain = 24.0f;

    // Seconds. Slow in both directions on purpose: this sets the network's
    // operating point, so it must not follow the music's dynamics. The gain
    // then follows the envelope quickly - the slowness belongs in one place,
    // and stacking a second multi-second smoother on top only means the first
    // seconds of a song are analysed at the wrong level, which is when the
    // metrical level is being decided.
    constexpr double kMakeupAttackSec = 0.8;
    constexpr double kMakeupReleaseSec = 4.0;
    constexpr double kMakeupGlideSec = 0.25;

    // Watching the analysis level for the moment something starts playing.
    //
    // Fast up so the start is caught inside a third of a second, slow down so
    // the gaps between hits do not read as the music stopping.
    constexpr double kLevelFastAttackSec = 0.05;
    constexpr double kLevelFastReleaseSec = 1.5;

    // How much louder than the level it has been sitting at counts as
    // something starting, and for how long. Between an empty room and a band
    // there are thirty to forty decibels; between a verse and a chorus, three
    // to eight, and a drums-out passage with the bass and the pads still in it
    // is nearer ten. Eighteen decibels sits above all of those and well below
    // the one this exists to catch, and a third of a second is longer than any
    // single hit.
    constexpr float  kLevelStepUp = 8.0f;
    constexpr double kLevelStepHoldSec = 0.30;

    // And how far below the loudest this input gets we must have been sitting
    // for the rise to be something *starting* rather than something getting
    // louder. Without it a breakdown coming back in reads as a new song, and
    // measured over thirty tracks that costs more than the whole fix gains:
    // every drums-out passage throws away a working grid.
    //
    // Twenty-four decibels. Between an empty room and a band there are thirty
    // to forty; the deepest breakdown that still has a band in it is nearer
    // twenty. How long that memory lasts has to outlive the gap between two
    // songs, which is where the silence this exists to notice actually is.
    constexpr float  kQuietFraction = 0.0625f;
    constexpr double kLoudMemorySec = 60.0;

    // Before it can say the level has changed, the watcher has to know where
    // the level is. One block is five milliseconds and can land anywhere inside
    // a kick, so a reference taken from it is a fraction of the real level and
    // the rest of that first note reads as something starting.
    constexpr double kLevelPrimeSec = 0.5;

    // How long our own part stays answerable for a rise on the input after it
    // starts. The leak comes back late - the device round trip plus, in a room,
    // the flight - and the canceller needs a moment to find it.
    constexpr double kOwnStepBlameSec = 0.75;
}

void VirtualPercussionEngine::prepare (double sr, int maxBlk, int numInputChannels) noexcept
{
    sampleRate = sr > 1.0 ? sr : 48000.0;
    maxBlock = std::max (512, maxBlk);
    preparedInputs = std::max (1, numInputChannels);

    mono.assign (static_cast<size_t> (maxBlock), 0.0f);
    outL.assign (static_cast<size_t> (maxBlock), 0.0f);
    outR.assign (static_cast<size_t> (maxBlock), 0.0f);
    clickScratch.assign (static_cast<size_t> (maxBlock), 0.0f);
    leakScratch.assign (static_cast<size_t> (maxBlock), 0.0f);
    leakScratchLo.assign (static_cast<size_t> (maxBlock), 0.0f);
    outRing.assign (static_cast<size_t> (ringSize), 0.0f);
    ringWrite = 0;

    tracker.prepare (sampleRate);
    percussion.prepare (sampleRate);
    styleDetector.prepare (sampleRate);
    percussion.setSeed (0x51A4E1u);
    stretcher.prepare (sampleRate, maxBlock);
    stretch.prepare (120.0f, sampleRate);
    clickPhase = 0.0;
    lastSr.store (sampleRate, std::memory_order_relaxed);
}

void VirtualPercussionEngine::reset() noexcept
{
    tracker.reset();
    percussion.reset();
    styleDetector.reset();
    stretcher.reset();
    stretch.reset();
    clickPhase = 0.0;
    std::fill (outRing.begin(), outRing.end(), 0.0f);
    ringWrite = 0;
    leakLp = 0.0f;
    analysisHp = 0.0f;
    leakGainLo = 0.0f;
    leakGainHi = 0.0f;
    leakDelaySamples = 0;
    leakScanCountdown = 0;
    peakEnv = 0.0f;
    makeupGain = 1.0f;
    levelFast = 0.0f;
    levelRef = 0.0f;
    levelLoud = 0.0f;
    levelStepSamples = 0;
    levelPrimeSamples = 0;
    analysisEpoch = 0;
    ownPeakLast = 0.0f;
    ownFast = 0.0f;
    ownRef = 0.0f;
    ownStepSamples = 0;
    tapWrite.store (0, std::memory_order_relaxed);
    tapRead = 0;
}

bool VirtualPercussionEngine::tryLoadNeuralHypothesis (BeatHypothesis& out) const noexcept
{
    return tracker.tryLoadHypothesis (out);
}

void VirtualPercussionEngine::loadPercussionLoop (const float* left, const float* right,
                                                  int frames, float nativeBpm)
{
    stretcher.prepare (sampleRate, maxBlock);
    stretcher.loadLoop (left, right, frames);
    stretch.prepare (nativeBpm, sampleRate);
}

void VirtualPercussionEngine::clearPercussionLoop()
{
    stretcher.loadLoop (nullptr, nullptr, 0);
    stretch.prepare (120.0f, sampleRate);
}

void VirtualPercussionEngine::start() noexcept
{
    tracker.start();
    percussion.clearVoices();
}

void VirtualPercussionEngine::stop() noexcept
{
    tracker.stop();
    percussion.silence();
}

void VirtualPercussionEngine::tapAt (double timeSeconds) noexcept
{
    const unsigned int w = tapWrite.load (std::memory_order_relaxed);
    tapTimes[w % static_cast<unsigned int> (tapQSize)] = timeSeconds;
    tapWrite.store (w + 1u, std::memory_order_release);
}

void VirtualPercussionEngine::tap() noexcept
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    tapAt (std::chrono::duration<double> (now).count());
}

void VirtualPercussionEngine::setTempoFollow (bool follow) noexcept
{
    const bool was = cfg.tempoFollow.exchange (follow, std::memory_order_relaxed);
    if (was && ! follow)
    {
        float bpm = lastBpm.load (std::memory_order_relaxed);
        if (bpm < 50.0f)
            bpm = cfg.userBpm.load (std::memory_order_relaxed);
        if (bpm < 50.0f)
            bpm = 120.0f;
        bpm = std::clamp (bpm, 50.0f, 200.0f);
        cfg.userBpm.store (bpm, std::memory_order_relaxed);
        cfg.userBpmGen.fetch_add (1u, std::memory_order_relaxed);
    }
}

void VirtualPercussionEngine::setFixedBpm (float bpm) noexcept
{
    if (! std::isfinite (bpm))
        return;
    bpm = std::clamp (bpm, 50.0f, 200.0f);
    cfg.tempoFollow.store (false, std::memory_order_relaxed);
    cfg.userBpm.store (bpm, std::memory_order_relaxed);
    cfg.userBpmGen.fetch_add (1u, std::memory_order_relaxed);
}

void VirtualPercussionEngine::mixInputs (const float* const* inputs, int numInputs, int numSamples) noexcept
{
    std::fill (mono.begin(), mono.begin() + numSamples, 0.0f);
    if (inputs == nullptr || numInputs <= 0)
        return;

    const int assigned = cfg.analysisChannel.load (std::memory_order_relaxed);
    int used = 0;
    if (assigned >= 0 && assigned < numInputs && inputs[assigned] != nullptr)
    {
        std::memcpy (mono.data(), inputs[assigned], static_cast<size_t> (numSamples) * sizeof (float));
        used = 1;
    }
    else
    {
        for (int c = 0; c < numInputs; ++c)
        {
            if (inputs[c] == nullptr)
                continue;
            ++used;
            for (int i = 0; i < numSamples; ++i)
                mono[static_cast<size_t> (i)] += inputs[c][i];
        }
        if (used > 1)
        {
            const float g = 1.0f / static_cast<float> (used);
            for (int i = 0; i < numSamples; ++i)
                mono[static_cast<size_t> (i)] *= g;
        }
    }

    const float trim = std::clamp (cfg.inputGain.load (std::memory_order_relaxed), 0.0f, 4.0f);
    if (std::fabs (trim - 1.0f) > 1.0e-6f)
    {
        for (int i = 0; i < numSamples; ++i)
            mono[static_cast<size_t> (i)] *= trim;
    }
}

void VirtualPercussionEngine::maybeInjectClick (int numSamples) noexcept
{
    std::fill (clickScratch.begin(), clickScratch.begin() + numSamples, 0.0f);
    if (! clickEnabled.load (std::memory_order_relaxed))
        return;

    const float bpm = std::max (40.0f, clickBpm.load (std::memory_order_relaxed));
    const double inc = (static_cast<double> (bpm) / 60.0) / sampleRate;

    for (int n = 0; n < numSamples; ++n)
    {
        const double ph = clickPhase;
        const double beat = ph - std::floor (ph);
        const int beatIndex = static_cast<int> (std::floor (ph));
        const double eighth = beat * 2.0 - std::floor (beat * 2.0);

        float kick = 0.0f;
        float snare = 0.0f;
        float hat = 0.0f;
        if (beat < 0.050)
        {
            const float t = static_cast<float> (beat) / static_cast<float> (bpm / 60.0);
            kick = std::sin (2.0f * 3.14159265f * 55.0f * t) * std::exp (-t * 24.0f);
            if ((beatIndex & 1) != 0)
            {
                const auto bits = static_cast<unsigned int> (n) * 1664525u + 1013904223u;
                const float noise = static_cast<float> ((bits >> 16) & 0x7fffu) / 32768.0f - 0.5f;
                snare = (0.55f * noise + 0.35f * std::sin (2.0f * 3.14159265f * 180.0f * t))
                        * std::exp (-t * 16.0f);
            }
        }
        if (eighth < 0.020)
        {
            const float t = static_cast<float> (eighth);
            const auto bits = static_cast<unsigned int> (n) * 1103515245u + 12345u;
            const float noise = static_cast<float> ((bits >> 16) & 0x7fffu) / 32768.0f - 0.5f;
            hat = noise * std::exp (-t * 90.0f) * 0.40f;
        }

        const float click = kick * 0.90f + snare + hat;
        clickScratch[static_cast<size_t> (n)] = click;
        mono[static_cast<size_t> (n)] += click;
        clickPhase += inc;
        if (clickPhase >= 4096.0)
            clickPhase -= 4096.0;
    }
}

void VirtualPercussionEngine::updateLeakDelay (int numSamples, bool speaker) noexcept
{
    if (outRing.empty() || numSamples <= 0)
        return;

    const float latMs = std::max (8.0f, latencyMs.load (std::memory_order_relaxed));
    int center = static_cast<int> (latMs * 0.001 * sampleRate);
    center = std::clamp (center, 64, ringSize - numSamples - 1);

    // The reported figure is the device round trip. Through a mixer that is
    // the leak delay. Through the iPad's own speaker the acoustic path sits
    // on top of it - typically another 10-40 ms - so a canceller pinned to
    // the device number misses the congas and the tracker follows itself.
    const int pad = static_cast<int> ((speaker ? 0.080 : 0.040) * sampleRate);
    const int lo = std::max (64, speaker ? static_cast<int> (0.008 * sampleRate)
                                         : center - pad);
    const int hi = std::min (ringSize - numSamples - 1,
                             speaker ? std::max (center + pad,
                                                 static_cast<int> (0.080 * sampleRate))
                                     : center + pad);
    if (hi < lo)
        return;

    if (leakDelaySamples < lo || leakDelaySamples > hi)
        leakDelaySamples = std::clamp (center, lo, hi);

    auto scoreAt = [this, numSamples] (int d) noexcept -> double
    {
        double xy = 0.0, yy = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const int ri = (ringWrite - d + i + ringSize) & (ringSize - 1);
            const float y = outRing[static_cast<size_t> (ri)];
            xy += static_cast<double> (mono[static_cast<size_t> (i)]) * y;
            yy += static_cast<double> (y) * y;
        }
        return yy > 1.0e-12 ? xy / std::sqrt (yy) : -1.0e9;
    };

    int best = leakDelaySamples;
    double bestScore = scoreAt (best);

    if (--leakScanCountdown <= 0)
    {
        leakScanCountdown = speaker ? 4 : 8;
        const int step = std::max (16, numSamples / 8);
        for (int d = lo; d <= hi; d += step)
        {
            const double s = scoreAt (d);
            if (s > bestScore)
            {
                bestScore = s;
                best = d;
            }
        }
        const int refineLo = std::max (lo, best - step);
        const int refineHi = std::min (hi, best + step);
        for (int d = refineLo; d <= refineHi; ++d)
        {
            const double s = scoreAt (d);
            if (s > bestScore)
            {
                bestScore = s;
                best = d;
            }
        }
    }
    else
    {
        const int step = speaker ? 8 : 16;
        for (int d : { leakDelaySamples - step, leakDelaySamples + step })
        {
            if (d < lo || d > hi)
                continue;
            const double s = scoreAt (d);
            if (s > bestScore)
            {
                bestScore = s;
                best = d;
            }
        }
    }

    leakDelaySamples = best;
}

void VirtualPercussionEngine::applyAnalysisHpf (int numSamples) noexcept
{
    // Analysis only. The output path is untouched. ~80 Hz takes rumble and
    // handling noise off the iPad mic without eating a kick's body.
    const float coef = 1.0f - std::exp (-2.0f * 3.14159265f * 80.0f
                                        / static_cast<float> (std::max (1.0, sampleRate)));
    for (int i = 0; i < numSamples; ++i)
    {
        const float x = mono[static_cast<size_t> (i)];
        analysisHp += coef * (x - analysisHp);
        mono[static_cast<size_t> (i)] = x - analysisHp;
    }
}

void VirtualPercussionEngine::subtractSpeakerLeak (int numSamples, bool speaker) noexcept
{
    if (outRing.empty() || numSamples <= 0)
        return;

    // Mixer return is the device round trip; searching around it can lock
    // onto a musical coincidence with the click and nibble the song. The
    // iPad mic needs the search: the acoustic hop sits on top of the
    // hardware figure, and a canceller glued to latencyMs misses the congas.
    int delay;
    if (speaker)
    {
        updateLeakDelay (numSamples, true);
        delay = leakDelaySamples > 0
                    ? leakDelaySamples
                    : static_cast<int> (std::max (8.0f, latencyMs.load (std::memory_order_relaxed))
                                        * 0.001 * sampleRate);
    }
    else
    {
        delay = static_cast<int> (std::max (8.0f, latencyMs.load (std::memory_order_relaxed))
                                  * 0.001 * sampleRate);
    }
    delay = std::clamp (delay, 64, ringSize - numSamples - 1);

    // The scratch is sized in prepare(). A fixed stack buffer used to cap this
    // at 2048 samples, which left the tail of a larger block un-subtracted: the
    // step it created at the splice point is a textbook onset, and it landed
    // in the analysis signal once per callback.
    const int n = std::min (numSamples, static_cast<int> (leakScratch.size()));
    if (n <= 0)
        return;

    // Split our own output in two around 1.5 kHz and fit a gain to each.
    //
    // One band was not enough. The reference used to be the top end alone -
    // which is the right model for the iPad's own speaker, because that speaker
    // has no low end to leak - so on a mixer, where the return carries the whole
    // part, the shaker was cancelled and the congas went into the analysis
    // untouched. Fitting both bands covers either path without having to be
    // told which one it is: through a speaker the low gain simply fits near
    // zero.
    double xyLo = 0.0, xyHi = 0.0, loLo = 0.0, hiHi = 0.0, loHi = 0.0, xx = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const int ri = (ringWrite - delay + i + ringSize) & (ringSize - 1);
        const float y = outRing[static_cast<size_t> (ri)];
        leakLp += 0.18f * (y - leakLp);
        const float lo = leakLp;
        const float hi = y - leakLp;
        leakScratchLo[static_cast<size_t> (i)] = lo;
        leakScratch[static_cast<size_t> (i)] = hi;
        const float x = mono[static_cast<size_t> (i)];
        xyLo += static_cast<double> (x) * lo;
        xyHi += static_cast<double> (x) * hi;
        loLo += static_cast<double> (lo) * lo;
        hiHi += static_cast<double> (hi) * hi;
        loHi += static_cast<double> (lo) * hi;
        xx += static_cast<double> (x) * x;
    }

    // Least squares over the two together, not one each: a one-pole split does
    // not make them orthogonal, and fitting them independently has each one
    // claiming part of what the other explains.
    float gLo = 0.0f, gHi = 0.0f;
    const double det = loLo * hiHi - loHi * loHi;
    if (std::fabs (det) > 1.0e-12 && loLo > 1.0e-9 && hiHi > 1.0e-9)
    {
        gLo = static_cast<float> ((xyLo * hiHi - xyHi * loHi) / det);
        gHi = static_cast<float> ((xyHi * loLo - xyLo * loHi) / det);
    }
    else
    {
        if (loLo > 1.0e-9) gLo = static_cast<float> (xyLo / loLo);
        if (hiHi > 1.0e-9) gHi = static_cast<float> (xyHi / hiHi);
    }

    // Smoothed *before* it is clamped, which is the whole difference between
    // cancelling a leak and inventing one. Over a block of 256 samples the fit
    // between two unrelated signals is not zero, it is zero plus a few per cent
    // of noise; clamping that at zero first keeps only the positive half and
    // averages it to a standing positive gain, so the analysis had a few per
    // cent of the app's own part subtracted from it even on a feed that carried
    // none - which costs the tracker real onsets, and was seen to put it badly
    // out on a clean line feed with the part turned up. Smoothing the signed fit
    // lets the noise cancel, and a path that does not change from one callback
    // to the next is estimated far better over half a second than over a block.
    constexpr float kGainSmooth = 0.12f;
    gLo = std::clamp (gLo, -2.0f, 2.0f);
    gHi = std::clamp (gHi, -2.0f, 2.0f);
    leakGainLo += (gLo - leakGainLo) * kGainSmooth;
    leakGainHi += (gHi - leakGainHi) * kGainSmooth;

    const float maxG = speaker ? 0.98f : 0.95f;
    const float useLo = std::clamp (leakGainLo, 0.0f, maxG);
    const float useHi = std::clamp (leakGainHi, 0.0f, maxG);

    // And only when our own output actually explains a share of what came in.
    // A leak is a large part of the input by definition; an accidental
    // resemblance between our shaker and the band's hi-hat is not. Below a few
    // per cent of the input's energy there is nothing here worth subtracting,
    // and subtracting it anyway costs the tracker real onsets.
    //
    // On the iPad mic the room music often dominates, so the share of *our*
    // part can sit under that floor even while the shaker is clearly audible.
    // Correlation against the delayed reference still names the leak, which
    // is enough to subtract without eating the song.
    const double explained = useLo * xyLo + useHi * xyHi;
    const double yy = loLo + hiHi;
    const double corr = (xx > 1.0e-12 && yy > 1.0e-12)
                            ? (useLo * xyLo + useHi * xyHi) / std::sqrt (xx * yy)
                            : 0.0;
    const float minShare = speaker ? 0.008f : 0.02f;
    if (xx < 1.0e-9)
        return;
    if (explained / xx < static_cast<double> (minShare) && ! (speaker && corr > 0.12))
        return;

    for (int i = 0; i < n; ++i)
        mono[static_cast<size_t> (i)] -= useLo * leakScratchLo[static_cast<size_t> (i)]
                                       + useHi * leakScratch[static_cast<size_t> (i)];
}

void VirtualPercussionEngine::applyAnalysisMakeup (int numSamples, float rawPeak,
                                                   bool levelJumped) noexcept
{
    // BeatNet's features are log10(magnitude + 1), which is not scale
    // invariant: the +1 knee means the level the analysis signal arrives at is
    // part of the model's input, not a detail the normalisation removes.
    // So this stage has two jobs, and the second one used to be missing: put the
    // signal at the level the network was validated at, and then hold it there.
    //
    // It used to take the peak instantly and release over half a second, so
    // every drum hit dropped the gain and the next half second crept back up -
    // moving the network's operating point on every beat, which is exactly the
    // input a beat tracker should never have. The envelope is slow in both
    // directions now, and the gain itself is smoothed again on top and ramped
    // across the block, so the analysis level is effectively constant over the
    // seconds the tempo estimator looks at.
    const float attack = 1.0f - std::exp (-static_cast<float> (numSamples)
                                          / std::max (1.0f, static_cast<float> (sampleRate * kMakeupAttackSec)));
    const float release = 1.0f - std::exp (-static_cast<float> (numSamples)
                                           / std::max (1.0f, static_cast<float> (sampleRate * kMakeupReleaseSec)));
    if (levelJumped || (peakEnv < kMakeupFloor && rawPeak >= kMakeupFloor))
        peakEnv = rawPeak;   // start at the level, do not crawl up to it
    else
        peakEnv += (rawPeak - peakEnv) * (rawPeak > peakEnv ? attack : release);

    float wanted = 1.0f;
    if (peakEnv >= kMakeupFloor)
        wanted = std::clamp (kMakeupTargetPeak / std::max (peakEnv, 1.0e-5f), 1.0f, kMakeupMaxGain);

    const float smooth = 1.0f - std::exp (-static_cast<float> (numSamples)
                                          / std::max (1.0f, static_cast<float> (sampleRate * kMakeupGlideSec)));
    const float from = makeupGain;
    // A level that has genuinely changed is not something to glide towards. The
    // envelope's attack is deliberately slow - eight tenths of a second, so that
    // no drum hit moves the network's operating point - and after an empty room
    // that is eight tenths of a second of music arriving at the network twenty
    // times too hot, which is exactly the window the level has to be right in.
    if (levelJumped)
        makeupGain = wanted;
    else
        makeupGain += (wanted - makeupGain) * smooth;
    if (from <= 1.0001f && makeupGain <= 1.0001f)
        return;

    // Ramp within the block: a gain that steps between callbacks puts an edge
    // into the analysis signal, and the network hears edges.
    const float step = (makeupGain - from) / static_cast<float> (numSamples);
    for (int i = 0; i < numSamples; ++i)
        mono[static_cast<size_t> (i)] *= from + step * static_cast<float> (i);
}

bool VirtualPercussionEngine::updateAnalysisEpoch (int numSamples, float rawPeak) noexcept
{
    // The make-up gain exists to hold the analysis at the one level the network
    // was validated at, which means that downstream of it an empty room and a
    // band playing arrive looking alike - by design, and measured: room noise
    // forty decibels down still reaches BeatNet amplified to the same peak, and
    // the network answers it with activations tall enough that the tempo
    // estimator names a level and calls it settled. This is the last place the
    // difference between the two still exists, so the moment has to be found
    // here and handed over.
    const float attack = 1.0f - std::exp (-static_cast<float> (numSamples)
                                          / std::max (1.0f, static_cast<float> (sampleRate * kLevelFastAttackSec)));
    const float release = 1.0f - std::exp (-static_cast<float> (numSamples)
                                           / std::max (1.0f, static_cast<float> (sampleRate * kLevelFastReleaseSec)));

    if (levelRef <= 0.0f)
    {
        // First audio. The envelope starts *at* the level rather than crawling
        // up to it, the same reason the make-up gain is primed rather than
        // released into.
        levelFast = rawPeak;
        levelRef = std::max (rawPeak, kMakeupFloor);
        levelLoud = levelRef;
        levelPrimeSamples = static_cast<int> (sampleRate * kLevelPrimeSec);
        return false;
    }

    levelFast += (rawPeak - levelFast) * (rawPeak > levelFast ? attack : release);

    if (levelPrimeSamples > 0)
    {
        // Still learning where the level is. Follow it up, decide nothing.
        levelPrimeSamples -= numSamples;
        levelRef = std::max (levelRef, levelFast);
        levelLoud = std::max (levelLoud, levelFast);
        return false;
    }

    // Our own part, on the same envelope. It reaches the microphone a little
    // after we play it and the canceller does not always find it - in mixer
    // mode the search does not cover an acoustic hop at all - so when the part
    // comes in, the level on the analysis bus can step up by more than this
    // looks for. That is us, not the room filling with a band.
    ownFast += (ownPeakLast - ownFast) * (ownPeakLast > ownFast ? attack : release);
    if (ownFast > std::max (ownRef, 1.0e-5f) * kLevelStepUp)
        ownStepSamples = static_cast<int> (sampleRate * kOwnStepBlameSec);
    else
        ownStepSamples = std::max (0, ownStepSamples - numSamples);
    ownRef = std::max (1.0e-6f, ownRef + (ownFast - ownRef) * release);

    if (ownStepSamples > 0)
    {
        // Take the new plateau as the level we are now sitting at, so that when
        // the blame expires the part's own contribution is not still standing
        // there looking like something that just started.
        levelRef = std::max (levelRef, levelFast);
        levelStepSamples = 0;
        return false;
    }

    const float loudDecay = 1.0f - std::exp (-static_cast<float> (numSamples)
                                             / std::max (1.0f, static_cast<float> (sampleRate * kLoudMemorySec)));
    levelLoud = std::max (levelFast, levelLoud + (levelFast - levelLoud) * loudDecay);

    // Two conditions, and both are needed.
    //
    // Upwards only, because a level that falls is a song ending, a break, a
    // quiet verse, and none of those is a reason to throw away what has been
    // measured: the evidence collected while it is quiet is the room's, and the
    // next rise discards it anyway.
    //
    // And out of a level that was properly quiet, not merely quieter. A rise on
    // its own cannot tell a band starting from a chorus arriving or a breakdown
    // ending, and the second and third are frequent and expensive.
    const bool wasQuiet = levelRef < levelLoud * kQuietFraction;
    if (wasQuiet && levelFast > levelRef * kLevelStepUp)
        levelStepSamples += numSamples;
    else
        levelStepSamples = 0;

    if (levelStepSamples > static_cast<int> (sampleRate * kLevelStepHoldSec))
    {
        levelRef = std::max (levelFast, kMakeupFloor);
        levelStepSamples = 0;
        ++analysisEpoch;
        return true;
    }

    if (levelFast < levelRef)
    {
        // The reference follows the level down, so it stays "where this input
        // has been sitting" rather than the loudest thing ever heard. A running
        // maximum would leave the bar too high for the next start to clear.
        levelRef = std::max (kMakeupFloor, levelRef + (levelFast - levelRef) * release);
    }
    return false;
}

void VirtualPercussionEngine::pushOutputToRing (int numSamples) noexcept
{
    // What we played this block, for the level watcher next block: it runs
    // before the part is rendered, so the newest own-level it can have is the
    // previous one - which is the right one anyway, because that is the part
    // that has had time to come back round.
    float own = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        own = std::max (own, std::abs (0.5f * (outL[static_cast<size_t> (i)]
                                               + outR[static_cast<size_t> (i)])));
    ownPeakLast = own;

    if (outRing.empty())
        return;
    for (int i = 0; i < numSamples; ++i)
    {
        const float y = 0.5f * (outL[static_cast<size_t> (i)] + outR[static_cast<size_t> (i)]);
        outRing[static_cast<size_t> (ringWrite)] = y;
        ringWrite = (ringWrite + 1) & (ringSize - 1);
    }
}

void VirtualPercussionEngine::process (const float* const* inputs, int numInputs,
                                       float* const* outputs, int numOutputs,
                                       int numSamples) noexcept
{
    // Denormals. Every filter in here has a tail that decays towards zero - the
    // leak canceller, the level envelope, the reverb - and a float that falls
    // into the denormal range costs a hundred times what a normal one does on
    // some cores. Silence after a loud passage is exactly when that happens,
    // and a callback that overruns its budget is a dropout.
    const juce::ScopedNoDenormals noDenormals;

    if (numSamples <= 0)
        return;

    // A host is entitled to hand over a longer block than it announced - a
    // screen lock, a route change, an AirPlay hop. Split it rather than
    // truncating: the tail of a truncated block is left holding whatever the
    // host had in the buffer.
    int offset = 0;
    while (offset < numSamples)
    {
        const int chunk = std::min (maxBlock, numSamples - offset);
        const float* inPtrs[kMaxSplitChannels];
        float* outPtrs[kMaxSplitChannels];
        const float* const* in = inputs;
        float* const* out = outputs;

        if (offset > 0 || numSamples > maxBlock)
        {
            const int nIn = std::min (numInputs, kMaxSplitChannels);
            const int nOut = std::min (numOutputs, kMaxSplitChannels);
            for (int c = 0; c < nIn; ++c)
                inPtrs[c] = (inputs != nullptr && inputs[c] != nullptr) ? inputs[c] + offset : nullptr;
            for (int c = 0; c < nOut; ++c)
                outPtrs[c] = (outputs != nullptr && outputs[c] != nullptr) ? outputs[c] + offset : nullptr;
            in = inputs != nullptr ? inPtrs : nullptr;
            out = outputs != nullptr ? outPtrs : nullptr;
            processBlock (in, nIn, out, nOut, chunk);

            // More channels than the split path carries: silence the rest
            // rather than leave the host's buffer as it was.
            if (outputs != nullptr)
                for (int c = nOut; c < numOutputs; ++c)
                    if (outputs[c] != nullptr)
                        std::fill (outputs[c] + offset, outputs[c] + offset + chunk, 0.0f);
        }
        else
        {
            processBlock (in, numInputs, out, numOutputs, chunk);
        }
        offset += chunk;
    }
}

void VirtualPercussionEngine::processBlock (const float* const* inputs, int numInputs,
                                            float* const* outputs, int numOutputs,
                                            int numSamples) noexcept
{
    const auto t0 = std::chrono::steady_clock::now();

    lastBuffer.store (numSamples, std::memory_order_relaxed);

    tracker.setFollowStrength (static_cast<FollowStrength> (cfg.followStrength.load (std::memory_order_relaxed)));
    tracker.setSubdivisionOverride (static_cast<Subdivision> (cfg.subdivision.load (std::memory_order_relaxed)));
    tracker.setTempoOctaveAuto (cfg.tempoOctaveAuto.load (std::memory_order_relaxed));
    tracker.setTempoOctave (cfg.tempoOctave.load (std::memory_order_relaxed));
    {
        const bool follow = cfg.tempoFollow.load (std::memory_order_relaxed);
        tracker.setTempoFollow (follow);
        if (! follow)
            tracker.setUserTempo (cfg.userBpm.load (std::memory_order_relaxed),
                                  cfg.userBpmGen.load (std::memory_order_relaxed));
    }
    {
        const int nudge = cfg.barNudge.load (std::memory_order_relaxed);
        if (nudge != seenBarNudge)
        {
            tracker.nudgeBar (nudge - seenBarNudge);
            seenBarNudge = nudge;
        }
    }
    const bool speaker = cfg.followSource.load (std::memory_order_relaxed)
                         == static_cast<int> (FollowSource::speaker);
    tracker.setSpeakerFollow (speaker);
    // What the clock has to run ahead of the music by, so that what is *heard*
    // lands on the pulse: the device round trip, plus the slowest attack in the
    // percussion bank. The second term is not a device property but it is the
    // same kind of delay - time between the decision and the sound - and this
    // is the one place that knows both.
    tracker.setReportedLatencyMs (latencyMs.load (std::memory_order_relaxed)
                                  + percussion.attackLeadMs());
    percussion.setHumanization (cfg.humanization.load (std::memory_order_relaxed));
    percussion.setVolume (cfg.percussionVolume.load (std::memory_order_relaxed));
    percussion.setReverbAmount (cfg.reverbAmount.load (std::memory_order_relaxed));
    // The two instruments switch independently. `setEnabled` is the master
    // gate, so it may only come off once both are off - otherwise turning the
    // shaker off would take the congas with it.
    const bool shakerOn = cfg.shakerEnabled.load (std::memory_order_relaxed);
    const bool congasOn = cfg.congasEnabled.load (std::memory_order_relaxed);
    percussion.setShakerEnabled (shakerOn);
    percussion.setCongasEnabled (congasOn);
    percussion.setEnabled (shakerOn || congasOn);
    percussion.setSwing (cfg.swing.load (std::memory_order_relaxed));
    percussion.setIntensity (cfg.intensity.load (std::memory_order_relaxed));
    // The manual setting is the override; on auto the music decides.

    mixInputs (inputs, numInputs, numSamples);

    // Whatever the microphone hands over, it stops being able to hurt anything
    // here. One infinity reaching the level envelope poisons it for the rest of
    // the session - the envelope has a four second release, and inf minus inf
    // is a NaN it never leaves - and with it the gain the network is fed. Note
    // that a peak taken with std::max hides this rather than catching it:
    // max(x, NaN) is x, so the bad sample passes straight through into the
    // analysis while every meter still reads normal.
    float peak = 0.0f;
    int bad = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        float x = mono[static_cast<size_t> (i)];
        if (! std::isfinite (x))
        {
            x = 0.0f;
            mono[static_cast<size_t> (i)] = 0.0f;
            ++bad;
        }
        const float a = std::abs (x);
        if (a > peak)
            peak = a;
    }
    if (bad > 0)
        badInputSamples.fetch_add (bad, std::memory_order_relaxed);
    // Both modes. It used to run only under SPEAKER, on the reasoning that the
    // leak is the iPad's own speaker into its own microphone - but a mixer
    // hands the app its output back on the return, which is the same signal
    // with a shorter path and none of the room in front of it. Measured on a
    // 120 BPM song with the app's output returning at half level: the tempo
    // error went from 0.12 BPM with the part silent to 2.10 BPM with it up, and
    // walked 1.2 BPM over the take, because what the tracker was following was
    // its own shaker. It is adaptive and self-gating - the gain it fits is near
    // zero when there is nothing of ours on the input - so running it on a feed
    // that has no leak costs nothing.
    subtractSpeakerLeak (numSamples, speaker);
    maybeInjectClick (numSamples);
    // Mic rumble only. A mixer aux and the click tests carry kick body around
    // 50-60 Hz; an 80 Hz HPF on those feeds thins the very pulse BeatNet
    // was trained on.
    if (speaker)
        applyAnalysisHpf (numSamples);

    // The level the make-up gain works from is the level of the signal it is
    // about to be applied to, which is not the level that arrived. `peak` above
    // is the input including whatever of our own part came back on it, and that
    // part has just been taken out - so driving the gain from it holds the
    // analysis below where the network expects it by exactly the amount the
    // subtraction removed. Measured on a returned feed with the part at full
    // volume, the analysis sat at 0.045 against 0.076 with the part silent: the
    // same band, quieter, for no reason the network can know about.
    float postPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        postPeak = std::max (postPeak, std::abs (mono[static_cast<size_t> (i)]));
    lastLeakRemain.store (postPeak, std::memory_order_relaxed);
    const bool levelJumped = updateAnalysisEpoch (numSamples, postPeak);
    applyAnalysisMakeup (numSamples, postPeak, levelJumped);
    float analysisPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        analysisPeak = std::max (analysisPeak, std::abs (mono[static_cast<size_t> (i)]));

    const unsigned int tw = tapWrite.load (std::memory_order_acquire);
    // More taps than the queue holds arrived since the last callback: the oldest
    // are gone, and reading them anyway means reading slots that now hold new
    // taps, out of order. Skip to what is still there.
    if (tw - tapRead > static_cast<unsigned int> (tapQSize))
        tapRead = tw - static_cast<unsigned int> (tapQSize);
    while (tapRead != tw)
    {
        tracker.tap (tapTimes[tapRead % static_cast<unsigned int> (tapQSize)]);
        ++tapRead;
    }

    tracker.setInputEpoch (analysisEpoch);
    const auto tr = tracker.process (mono.data(), numSamples);
    if (! cfg.tempoFollow.load (std::memory_order_relaxed) && tr.bpm > 50.0f)
        cfg.userBpm.store (tr.bpm, std::memory_order_relaxed);
    // The clock's own tempo, not the BPM on the display. `tr.bpm` is blank
    // until the tracker has locked and reads 0 before then, so the percussion
    // was being told 120 while the clock ran at whatever it had actually found.
    percussion.setGroove (tr.clock.tempoBpm > 40.0f ? tr.clock.tempoBpm
                                                    : (tr.bpm > 40.0f ? tr.bpm : 120.0f),
                          tr.clockPulsesPerBeat);
    percussion.setShakerSubdivision (tr.subdivision);

    // Fold the analysis signal onto the bar to see which part the music is
    // asking for. Only while the clock is actually on the song: folding audio
    // onto a bar the tracker has not found yet just smears every bin.
    const bool autoStyle = cfg.grooveAuto.load (std::memory_order_relaxed);
    if (autoStyle)
    {
        const bool clockStable = tr.state == TrackingState::following
                                 && tr.clock.tempoBpm > 40.0f;
        styleDetector.process (mono.data(), numSamples, tr.barPhase, clockStable);
    }
    const GrooveStyle chosen = autoStyle
                                   ? styleDetector.style()
                                   : static_cast<GrooveStyle> (cfg.grooveStyle.load (std::memory_order_relaxed));
    percussion.setGrooveStyle (chosen);

    if (stretcher.hasLoop())
    {
        stretch.setLiveClock (tr.bpm, tr.beatPhase, tr.confidence);
        const float ratio = stretch.advance (numSamples);
        stretcher.process (outL.data(), outR.data(), numSamples, ratio);
        if (! tr.percussionShouldPlay)
        {
            std::fill (outL.begin(), outL.begin() + numSamples, 0.0f);
            std::fill (outR.begin(), outR.begin() + numSamples, 0.0f);
        }
    }
    else
    {
        percussion.render (outL.data(), outR.data(), numSamples, tr.clock, tr.percussionShouldPlay);
    }

    const bool monitorClick = clickEnabled.load (std::memory_order_relaxed);
    const float master = cfg.masterVolume.load (std::memory_order_relaxed);
    if (outputs != nullptr)
    {
        for (int c = 0; c < numOutputs; ++c)
        {
            if (outputs[c] == nullptr)
                continue;
            const float* src = (c & 1) != 0 ? outR.data() : outL.data();
            for (int i = 0; i < numSamples; ++i)
            {
                float s = src[i];
                if (monitorClick)
                    s += clickScratch[static_cast<size_t> (i)] * 0.45f;
                outputs[c][i] = s * master;
            }
        }
    }

    pushOutputToRing (numSamples);

    lastBpm.store (tr.bpm, std::memory_order_relaxed);
    lastTarget.store (tr.targetBpm, std::memory_order_relaxed);
    lastConf.store (tr.confidence, std::memory_order_relaxed);
    lastBeat.store (tr.beatPhase, std::memory_order_relaxed);
    lastBar.store (tr.barPhase, std::memory_order_relaxed);
    lastBarDeclared.store (tr.barDeclared, std::memory_order_relaxed);
    lastGaps.store (static_cast<int> (tr.analysisGaps), std::memory_order_relaxed);
    lastRestarts.store (analysisEpoch, std::memory_order_relaxed);
    lastBacklog.store (tr.analysisBacklog, std::memory_order_relaxed);
    lastPeak.store (peak, std::memory_order_relaxed);
    lastAnalysisPeak.store (analysisPeak, std::memory_order_relaxed);
    lastAnalysisGain.store (makeupGain, std::memory_order_relaxed);
    lastState.store (static_cast<int> (tr.state), std::memory_order_relaxed);
    lastSub.store (static_cast<int> (tr.subdivision), std::memory_order_relaxed);
    lastBeats.store (tr.beatsElapsed, std::memory_order_relaxed);
    lastAudible.store (tr.percussionShouldPlay, std::memory_order_relaxed);
    lastTapLock.store (tr.tapLocked, std::memory_order_relaxed);
    lastFollowBar.store (static_cast<int> (tr.followBar), std::memory_order_relaxed);
    lastVoices.store (percussion.activeVoices(), std::memory_order_relaxed);
    lastAiOnnx.store (tr.aiOnnx, std::memory_order_relaxed);
    lastHypValid.store (tr.hypValid, std::memory_order_relaxed);
    lastNeuralBpm.store (tr.neuralBpm, std::memory_order_relaxed);
    lastPBeat.store (tr.pBeat, std::memory_order_relaxed);
    lastLeadMs.store (tr.leadMs, std::memory_order_relaxed);
    lastRegime.store (static_cast<int> (tr.regime), std::memory_order_relaxed);
    lastOctave.store (tr.tempoOctave, std::memory_order_relaxed);
    lastCombBpm.store (tr.combBpm, std::memory_order_relaxed);
    lastLevelSettled.store (tr.levelSettled, std::memory_order_relaxed);
    lastStyle.store (static_cast<int> (chosen), std::memory_order_relaxed);
    lastStyleConf.store (styleDetector.confidence(), std::memory_order_relaxed);
    // Everything the UI shows is published from here. Reading it off the
    // objects instead - as the style features and the hit count were - is the
    // message thread reading five floats while this thread writes them, which
    // is a race whose visible form is a meter showing a number that was never
    // true at any single moment.
    {
        const auto f = styleDetector.features();
        lastStyleEvenKick.store (f.evenKick, std::memory_order_relaxed);
        lastStyleBackbeat.store (f.alternation, std::memory_order_relaxed);
        lastStyleOffHigh.store (f.offHigh, std::memory_order_relaxed);
        lastStyleSync.store (f.syncopation, std::memory_order_relaxed);
        lastStyleOccupancy.store (f.occupancy, std::memory_order_relaxed);
    }
    lastHits.store (percussion.hitsFired(), std::memory_order_relaxed);
    lastAttackLeadMs.store (percussion.attackLeadMs(), std::memory_order_relaxed);

    const auto t1 = std::chrono::steady_clock::now();
    const float ms = std::chrono::duration<float, std::milli> (t1 - t0).count();
    lastCallbackMs.store (ms, std::memory_order_relaxed);
}

EngineSnapshot VirtualPercussionEngine::snapshot() const noexcept
{
    EngineSnapshot s;
    s.state = static_cast<TrackingState> (lastState.load (std::memory_order_relaxed));
    s.subdivision = static_cast<Subdivision> (lastSub.load (std::memory_order_relaxed));
    s.follow = static_cast<FollowStrength> (cfg.followStrength.load (std::memory_order_relaxed));
    s.source = static_cast<FollowSource> (cfg.followSource.load (std::memory_order_relaxed));
    s.bpm = lastBpm.load (std::memory_order_relaxed);
    s.targetBpm = lastTarget.load (std::memory_order_relaxed);
    s.confidence = lastConf.load (std::memory_order_relaxed);
    s.beatPhase = lastBeat.load (std::memory_order_relaxed);
    s.barPhase = lastBar.load (std::memory_order_relaxed);
    s.barDeclared = lastBarDeclared.load (std::memory_order_relaxed);
    s.latencyMs = latencyMs.load (std::memory_order_relaxed);
    s.inputPeak = lastPeak.load (std::memory_order_relaxed);
    s.callbackMs = lastCallbackMs.load (std::memory_order_relaxed);
    s.humanization = cfg.humanization.load (std::memory_order_relaxed);
    s.reverbAmount = cfg.reverbAmount.load (std::memory_order_relaxed);
    s.shakerEnabled = cfg.shakerEnabled.load (std::memory_order_relaxed);
    s.percussionAudible = lastAudible.load (std::memory_order_relaxed);
    s.tapLocked = lastTapLock.load (std::memory_order_relaxed);
    s.followBar = static_cast<FollowBar> (lastFollowBar.load (std::memory_order_relaxed));
    s.bufferSize = lastBuffer.load (std::memory_order_relaxed);
    s.sampleRate = lastSr.load (std::memory_order_relaxed);
    s.beatsLocked = lastBeats.load (std::memory_order_relaxed);
    s.shakerVoices = lastVoices.load (std::memory_order_relaxed);
    s.aiOnnx = lastAiOnnx.load (std::memory_order_relaxed);
    s.hypValid = lastHypValid.load (std::memory_order_relaxed);
    s.neuralBpm = lastNeuralBpm.load (std::memory_order_relaxed);
    s.pBeat = lastPBeat.load (std::memory_order_relaxed);
    s.analysisPeak = lastAnalysisPeak.load (std::memory_order_relaxed);
    s.analysisGain = lastAnalysisGain.load (std::memory_order_relaxed);
    s.inputGain = cfg.inputGain.load (std::memory_order_relaxed);
    s.leakRemain = lastLeakRemain.load (std::memory_order_relaxed);
    s.badInputSamples = badInputSamples.load (std::memory_order_relaxed);
    s.analysisGaps = lastGaps.load (std::memory_order_relaxed);
    s.analysisRestarts = static_cast<int> (lastRestarts.load (std::memory_order_relaxed));
    s.analysisBacklog = lastBacklog.load (std::memory_order_relaxed);
    s.leadMs = lastLeadMs.load (std::memory_order_relaxed);
    s.attackLeadMs = lastAttackLeadMs.load (std::memory_order_relaxed);
    s.tempoRegime = lastRegime.load (std::memory_order_relaxed);
    s.combBpm = lastCombBpm.load (std::memory_order_relaxed);
    s.levelSettled = lastLevelSettled.load (std::memory_order_relaxed);
    // The level in force, which under AUTO is not the one in the settings.
    s.tempoOctave = lastOctave.load (std::memory_order_relaxed);
    s.tempoOctaveAuto = cfg.tempoOctaveAuto.load (std::memory_order_relaxed);
    s.tempoFollow = cfg.tempoFollow.load (std::memory_order_relaxed);

    s.grooveStyle = lastStyle.load (std::memory_order_relaxed);
    s.grooveStyleConfidence = lastStyleConf.load (std::memory_order_relaxed);
    s.styleEvenKick = lastStyleEvenKick.load (std::memory_order_relaxed);
    s.styleBackbeat = lastStyleBackbeat.load (std::memory_order_relaxed);
    s.styleOffHigh = lastStyleOffHigh.load (std::memory_order_relaxed);
    s.styleSync = lastStyleSync.load (std::memory_order_relaxed);
    s.styleOccupancy = lastStyleOccupancy.load (std::memory_order_relaxed);
    return s;
}

} // namespace vp
