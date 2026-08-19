#include "Percussion/PercussionEngine.h"

#include <algorithm>
#include <cmath>

namespace vp
{

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    // Seconds of raised-cosine fade welded onto the end of every synthesised
    // sample. Each one is an exponential decay cut off at a fixed length, and
    // none of them has decayed anywhere near far enough by then: the shaker
    // stops at 21% of its peak, the open conga at 11%, the slap at 8%. That
    // step is a click on *every* hit - not a rare glitch, the normal case - and
    // it is the first thing to fix when the percussion sounds broken up.
    constexpr double kTailFadeSec = 0.012;

    // Milliseconds of fade when a voice is taken over by a new hit of the same
    // kind. Long enough not to click, short enough that the new hit is not
    // muddied by the old one.
    constexpr double kStealFadeSec = 0.004;

    // Shortest gap between two percussion hits. A 32nd note at 200 BPM is 37 ms,
    // so anything closer than this is the clock stuttering, not the groove.
    constexpr double kRetriggerGuardSec = 0.020;

    void fadeTail (std::vector<float>& L, std::vector<float>& R, double sampleRate) noexcept
    {
        const int n = static_cast<int> (std::min (L.size(), R.size()));
        const int fade = std::min (n, std::max (1, static_cast<int> (sampleRate * kTailFadeSec)));
        for (int i = 0; i < fade; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (fade);
            const float g = 0.5f * (1.0f + std::cos (kPi * t));
            L[static_cast<size_t> (n - fade + i)] *= g;
            R[static_cast<size_t> (n - fade + i)] *= g;
        }
    }

    struct Svf
    {
        float low = 0.0f;
        float band = 0.0f;

        float bandpass (float x, float f, float q) noexcept
        {
            const float g = 2.0f * std::sin (kPi * std::min (0.49f, f));
            low += g * band;
            const float high = x - low - q * band;
            band += g * high;
            return band;
        }
    };

    void addBead (std::vector<float>& L, std::vector<float>& R,
                  int pos, int dur, float sr, float freq, float amp,
                  float pan, float decay, DeterministicRng& rng) noexcept
    {
        const int nMax = static_cast<int> (L.size());
        if (pos >= nMax || dur <= 0)
            return;
        const int end = std::min (nMax, pos + dur);
        const float wL = amp * (1.0f - pan) * 0.5f;
        const float wR = amp * (1.0f + pan) * 0.5f;
        for (int n = pos; n < end; ++n)
        {
            const float t = static_cast<float> (n - pos) / sr;
            const float env = std::exp (-t * decay);
            const float ping = std::sin (2.0f * kPi * freq * t);
            const float click = rng.nextSigned() * std::exp (-t * 900.0f);
            const float g = (ping * 0.72f + click * 0.28f) * env;
            L[static_cast<size_t> (n)] += g * wL;
            R[static_cast<size_t> (n)] += g * wR;
        }
    }
}

void PercussionEngine::setReverbAmount (float amount) noexcept
{
    amount = clamp01 (amount);
    if (std::fabs (amount - reverbAmount) < 0.002f)
        return;
    reverbAmount = amount;
    applyReverbParams();
}

void PercussionEngine::applyReverbParams() noexcept
{
    juce::Reverb::Parameters p;
    p.roomSize = 0.22f + reverbAmount * 0.30f;
    p.damping  = 0.48f + reverbAmount * 0.28f;
    p.wetLevel = reverbAmount * 0.40f;
    p.dryLevel = 0.50f - reverbAmount * 0.12f;
    p.width    = 0.82f;
    p.freezeMode = 0.0f;
    reverb.setParameters (p);
}

void PercussionEngine::synthesizeShaker() noexcept
{
        const int length = std::max (256, static_cast<int> (sampleRate * 0.14));
    const float sr = static_cast<float> (sampleRate);

    for (int take = 0; take < kTakes; ++take)
    {
        auto& L = shakerL[take];
        auto& R = shakerR[take];
        L.assign (static_cast<size_t> (length), 0.0f);
        R.assign (static_cast<size_t> (length), 0.0f);

        DeterministicRng local (0x5A4E11u + static_cast<std::uint32_t> (take) * 7919u);

        const int nBeads = 70 + static_cast<int> (local.nextU32() % 40u);
        for (int b = 0; b < nBeads; ++b)
        {
            const float u = std::max (1.0e-4f, local.nextFloat());
            const float tHit = -std::log (u) * 0.026f;
            if (tHit > 0.14f)
                continue;
            const int pos = static_cast<int> (tHit * sampleRate);
            const float freq = 2600.0f + local.nextFloat() * 5600.0f;
            const float amp = (0.35f + 0.65f * local.nextFloat()) * std::exp (-tHit * 16.0f);
            const float pan = local.nextSigned() * 0.62f;
            const float decay = 160.0f + local.nextFloat() * 260.0f;
            const int dur = static_cast<int> (0.010 * sampleRate);
            addBead (L, R, pos, dur, sr, freq, amp, pan, decay, local);
        }

        Svf bpL, bpR, bodyL, bodyR;
        float lpL = 0.0f, lpR = 0.0f;
        const float fBead = 5200.0f / sr;
        const float fBody = 1400.0f / sr;
        for (int n = 0; n < length; ++n)
        {
            const float t = static_cast<float> (n) / sr;
            const float env = std::exp (-t * 11.0f) * (1.0f - std::exp (-t * 380.0f));
            const float nL = local.nextSigned();
            const float nR = local.nextSigned();
            const float beadsL = bpL.bandpass (nL, fBead, 0.22f);
            const float beadsR = bpR.bandpass (nR, fBead, 0.22f);
            const float gourdL = bodyL.bandpass (nL, fBody, 0.35f);
            const float gourdR = bodyR.bandpass (nR, fBody, 0.35f);
            lpL += 0.012f * (nL - lpL);
            lpR += 0.012f * (nR - lpR);
            const float hand = (n < static_cast<int> (0.008 * sampleRate))
                                   ? std::exp (-t * 70.0f) * 0.10f : 0.0f;
            L[static_cast<size_t> (n)] += (beadsL * 0.28f + gourdL * 0.10f + lpL * hand) * env;
            R[static_cast<size_t> (n)] += (beadsR * 0.28f + gourdR * 0.10f + lpR * hand) * env;
        }

        float peak = 1.0e-6f;
        for (int n = 0; n < length; ++n)
        {
            peak = std::max (peak, std::fabs (L[static_cast<size_t> (n)]));
            peak = std::max (peak, std::fabs (R[static_cast<size_t> (n)]));
        }
        const float g = 0.92f / peak;
        for (int n = 0; n < length; ++n)
        {
            L[static_cast<size_t> (n)] *= g;
            R[static_cast<size_t> (n)] *= g;
        }
        fadeTail (L, R, sampleRate);
    }
}

void PercussionEngine::prepare (double sr) noexcept
{
    sampleRate = sr > 1.0 ? sr : 48000.0;
    reverb.setSampleRate (sampleRate);
    applyReverbParams();
    synthesizeShaker();
    synthesizeCongas();
    reset();
}

void PercussionEngine::reset() noexcept
{
    clearVoices();
    totalHits = 0;
    lastActive = 0;
    samplesSinceHit = 1000000;
    reverb.reset();
}

void PercussionEngine::clearVoices() noexcept
{
    for (auto& v : voices)
        v = {};
    lastActive = 0;
}

void PercussionEngine::silence() noexcept
{
    clearVoices();
    reverb.reset();
    samplesSinceHit = 1000000;
}

void PercussionEngine::setGroove (float bpm, int pulsesPerBeat) noexcept
{
    grooveBpm = std::clamp (bpm, 40.0f, 220.0f);
    groovePulses = pulsesPerBeat < 1 ? 2 : pulsesPerBeat;
}

void PercussionEngine::synthesizeCongas() noexcept
{
    const float sr = static_cast<float> (sampleRate);
    auto synth = [&] (std::vector<float>& L, std::vector<float>& R,
                      float freq, float noiseAmt, float decay, float seconds, float pan)
    {
        const int n = std::max (256, static_cast<int> (sampleRate * static_cast<double> (seconds)));
        L.assign (static_cast<size_t> (n), 0.0f);
        R.assign (static_cast<size_t> (n), 0.0f);
        DeterministicRng local (0xC0A7u + static_cast<std::uint32_t> (freq));
        const float wL = (1.0f - pan) * 0.5f;
        const float wR = (1.0f + pan) * 0.5f;
        float lp = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float t = static_cast<float> (i) / sr;
            const float env = std::exp (-t * decay) * (1.0f - std::exp (-t * 420.0f));
            const float skin = std::sin (2.0f * kPi * freq * t * (1.0f - 0.08f * t));
            const float nse = local.nextSigned();
            lp += 0.08f * (nse - lp);
            const float slap = nse * std::exp (-t * 90.0f);
            const float s = (skin * (1.0f - noiseAmt) + (lp + slap) * noiseAmt) * env;
            L[static_cast<size_t> (i)] = s * wL;
            R[static_cast<size_t> (i)] = s * wR;
        }
        float peak = 1.0e-6f;
        for (int i = 0; i < n; ++i)
            peak = std::max (peak, std::max (std::fabs (L[static_cast<size_t> (i)]),
                                             std::fabs (R[static_cast<size_t> (i)])));
        const float g = 0.95f / peak;
        for (int i = 0; i < n; ++i)
        {
            L[static_cast<size_t> (i)] *= g;
            R[static_cast<size_t> (i)] *= g;
        }
        fadeTail (L, R, sampleRate);
    };

    synth (tumbaL, tumbaR, 82.0f, 0.12f, 9.5f, 0.28f, -0.28f);
    synth (openL, openR, 175.0f, 0.10f, 14.0f, 0.16f, 0.18f);
    synth (slapL, slapR, 320.0f, 0.55f, 28.0f, 0.09f, 0.38f);
}

void PercussionEngine::releaseKind (Kind kind) noexcept
{
    // Fade the outgoing voice instead of switching it off mid-sample.
    const float step = 1.0f / std::max (1.0f, static_cast<float> (sampleRate * kStealFadeSec));
    for (auto& v : voices)
    {
        if (v.active && v.kind == kind && v.fadeStep <= 0.0f)
            v.fadeStep = step;
    }
}

PercussionEngine::Voice& PercussionEngine::allocateVoice() noexcept
{
    // A free slot if there is one, otherwise the oldest voice - which is the
    // one furthest into its decay, so it is the least missed. Falling back to
    // slot 0 the way this used to meant a busy bar overwrote whichever voice
    // happened to be first, part-way through, at full amplitude.
    int slot = -1;
    for (int i = 0; i < kVoices; ++i)
    {
        if (! voices[i].active)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        slot = 0;
        for (int i = 1; i < kVoices; ++i)
            if (voices[i].age < voices[slot].age)
                slot = i;
    }
    voices[slot] = {};
    voices[slot].age = ++voiceClock;
    return voices[slot];
}

void PercussionEngine::triggerShaker (int sampleOffset) noexcept
{
    releaseKind (Kind::shaker);

    const float hum = humanization;
    const float vel = 0.78f + 0.14f * (1.0f - hum) + 0.08f * hum * rng.nextFloat();
    const float pan = 0.03f * rng.nextSigned();
    auto& v = allocateVoice();
    v.kind = Kind::shaker;
    v.pos = -sampleOffset;
    v.take = static_cast<int> (rng.nextU32() % static_cast<std::uint32_t> (kTakes));
    v.length = static_cast<int> (shakerL[v.take].size());
    v.gainL = vel * (1.0f - pan);
    v.gainR = vel * (1.0f + pan);
    v.active = true;
    ++totalHits;
}

void PercussionEngine::triggerConga (Kind kind, int sampleOffset, float pan) noexcept
{
    releaseKind (kind);

    const float vel = 0.82f + 0.12f * rng.nextFloat();
    auto& v = allocateVoice();
    v.kind = kind;
    v.pos = -sampleOffset;
    v.take = 0;
    switch (kind)
    {
        case Kind::tumba: v.length = static_cast<int> (tumbaL.size()); break;
        case Kind::open:  v.length = static_cast<int> (openL.size()); break;
        case Kind::slap:  v.length = static_cast<int> (slapL.size()); break;
        default:          v.length = 0; break;
    }
    v.gainL = vel * (1.0f - pan);
    v.gainR = vel * (1.0f + pan);
    v.active = true;
    ++totalHits;
}

const std::vector<float>& PercussionEngine::sampleL (const Voice& v) const noexcept
{
    switch (v.kind)
    {
        case Kind::tumba: return tumbaL;
        case Kind::open:  return openL;
        case Kind::slap:  return slapL;
        case Kind::shaker:
        default:
        {
            const int t = std::clamp (v.take, 0, kTakes - 1);
            return shakerL[t];
        }
    }
}

const std::vector<float>& PercussionEngine::sampleR (const Voice& v) const noexcept
{
    switch (v.kind)
    {
        case Kind::tumba: return tumbaR;
        case Kind::open:  return openR;
        case Kind::slap:  return slapR;
        case Kind::shaker:
        default:
        {
            const int t = std::clamp (v.take, 0, kTakes - 1);
            return shakerR[t];
        }
    }
}

int PercussionEngine::render (float* left, float* right, int numSamples,
                              const ClockTick& tick, bool audible) noexcept
{
    if (left == nullptr || right == nullptr)
        return 0;

    std::fill (left, left + numSamples, 0.0f);
    std::fill (right, right + numSamples, 0.0f);

    if (enabled && audible)
    {
        // A guard against firing the same grid position twice - which the clock
        // can do when a phase correction steps the grid backwards - and nothing
        // more. It used to be 82% of the nominal pulse derived from `grooveBpm`,
        // which is the *displayed* BPM: before a lock that is 120 whatever the
        // clock is really doing, so at any real tempo above ~145 the guard was
        // longer than the actual pulse and swallowed every second hit. Dropped
        // notes are exactly the symptom this had to stop causing, so the guard
        // is now a short fixed one that no musical subdivision can reach.
        const int minGap = static_cast<int> (sampleRate * kRetriggerGuardSec);
        for (int i = 0; i < tick.pulsesFired; ++i)
        {
            int offset = tick.pulseOffset[i];
            if (offset < 0)
                offset = 0;
            if (offset >= numSamples)
                offset = numSamples - 1;
            if (samplesSinceHit + offset < minGap)
                continue;

            const int idx = tick.pulseIndex[i];
            const int barBeat = tick.pulseBeatInBar[i];
            int step = -1;
            if (groovePulses <= 1)
                step = (barBeat * 2) % 8;
            else if (groovePulses == 2)
                step = tick.barPulse[i] % 8;
            else
            {
                if ((idx & 1) != 0)
                    continue;
                step = ((barBeat * 2) + (idx / 2)) % 8;
            }
            if (step < 0)
                continue;

            triggerShaker (offset);
            // Tumbao / marcha on the bar's 8ths: 1, 1&, 2, 2&, 4, 4&
            static constexpr Kind kConga[8] = {
                Kind::tumba, Kind::shaker, Kind::open, Kind::slap,
                Kind::open,  Kind::shaker, Kind::slap, Kind::open
            };
            const Kind ck = kConga[step];
            if (ck != Kind::shaker)
            {
                const float pan = (ck == Kind::tumba) ? -0.28f
                                : (ck == Kind::slap)  ?  0.36f : 0.16f;
                triggerConga (ck, offset, pan);
            }
            samplesSinceHit = -offset;
        }
    }

    int active = 0;
    const float g = volume;
    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        ++active;
        const auto& bL = sampleL (v);
        const auto& bR = sampleR (v);
        const int nBuf = static_cast<int> (std::min (bL.size(), bR.size()));
        for (int n = 0; n < numSamples; ++n)
        {
            const int p = v.pos + n;
            if (p < 0)
                continue;
            if (p >= v.length || p >= nBuf)
            {
                v.active = false;
                break;
            }
            if (v.fadeStep > 0.0f)
            {
                v.fade -= v.fadeStep;
                if (v.fade <= 0.0f)
                {
                    v.fade = 0.0f;
                    v.active = false;
                    break;
                }
            }
            const float a = v.fade * g;
            left[n]  += bL[static_cast<size_t> (p)] * v.gainL * a;
            right[n] += bR[static_cast<size_t> (p)] * v.gainR * a;
        }
        if (v.active)
        {
            v.pos += numSamples;
            if (v.pos >= v.length)
                v.active = false;
        }
    }

    if (reverbAmount > 0.001f)
        reverb.processStereo (left, right, numSamples);

    samplesSinceHit += numSamples;
    if (samplesSinceHit > 10000000)
        samplesSinceHit = 10000000;
    lastActive = active;
    return active;
}

} // namespace vp
