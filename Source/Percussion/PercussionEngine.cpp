#include "Percussion/PercussionEngine.h"

#include <algorithm>
#include <cmath>

namespace vp
{

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

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
    }
}

void PercussionEngine::prepare (double sr) noexcept
{
    sampleRate = sr > 1.0 ? sr : 48000.0;
    // 2.5 ms of release: long enough to remove the step, short enough that the
    // next grain still starts on the beat rather than under a tail.
    voiceFadeStep = 1.0f / static_cast<float> (std::max (8, static_cast<int> (sampleRate * 0.0025)));
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
    shakerHits = 0;
    lastActive = 0;
    hardStealCount = 0;
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
    };

    synth (tumbaL, tumbaR, 82.0f, 0.12f, 9.5f, 0.28f, -0.28f);
    synth (openL, openR, 175.0f, 0.10f, 14.0f, 0.16f, 0.18f);
    synth (slapL, slapR, 320.0f, 0.55f, 28.0f, 0.09f, 0.38f);
}

void PercussionEngine::releaseKind (Kind kind, int sampleOffset) noexcept
{
    // A retriggered instrument used to have its previous voice switched off
    // between two samples, which is a step in the waveform and clicks on every
    // hit. Ramp it out instead, starting where the new note starts.
    for (auto& v : voices)
    {
        if (! v.active || v.kind != kind || v.fadeStep > 0.0f)
            continue;
        v.fadeStep = voiceFadeStep;
        v.fadeDelay = sampleOffset > 0 ? sampleOffset : 0;
    }
}

int PercussionEngine::allocateVoice() noexcept
{
    for (int i = 0; i < kNumVoices; ++i)
        if (! voices[i].active)
            return i;

    // Nothing idle. Take the quietest voice - the behaviour docs/AUDIO_ENGINE.md
    // has always described - instead of always overwriting slot 0 whatever it
    // happened to be playing.
    int quietest = 0;
    float lowest = -1.0f;
    for (int i = 0; i < kNumVoices; ++i)
    {
        const Voice& v = voices[i];
        // A voice waiting to start later in this block reads as silent but has
        // not sounded yet, so taking it would swallow a scheduled note.
        if (v.pos < 0)
            continue;
        const auto& buf = sampleL (v);
        const float s = v.pos < static_cast<int> (buf.size())
                            ? std::fabs (buf[static_cast<size_t> (v.pos)]) : 0.0f;
        const float level = s * std::max (v.gainL, v.gainR) * v.fade;
        if (lowest < 0.0f || level < lowest)
        {
            lowest = level;
            quietest = i;
        }
    }
    ++hardStealCount;
    return quietest;
}

void PercussionEngine::triggerShaker (int sampleOffset) noexcept
{
    releaseKind (Kind::shaker, sampleOffset);

    const int slot = allocateVoice();

    const float hum = humanization;
    const float vel = 0.78f + 0.14f * (1.0f - hum) + 0.08f * hum * rng.nextFloat();
    const float pan = 0.03f * rng.nextSigned();
    auto& v = voices[slot];
    v.kind = Kind::shaker;
    v.pos = -sampleOffset;
    v.take = static_cast<int> (rng.nextU32() % static_cast<std::uint32_t> (kTakes));
    v.length = static_cast<int> (shakerL[v.take].size());
    v.gainL = vel * (1.0f - pan);
    v.gainR = vel * (1.0f + pan);
    v.fade = 1.0f;
    v.fadeStep = 0.0f;
    v.fadeDelay = 0;
    v.active = true;
    ++totalHits;
    ++shakerHits;
}

void PercussionEngine::triggerConga (Kind kind, int sampleOffset, float pan) noexcept
{
    releaseKind (kind, sampleOffset);

    const int slot = allocateVoice();

    const float vel = 0.82f + 0.12f * rng.nextFloat();
    auto& v = voices[slot];
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
    v.fade = 1.0f;
    v.fadeStep = 0.0f;
    v.fadeDelay = 0;
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
        const float pulseSec = 60.0f / std::max (40.0f, grooveBpm)
                               / static_cast<float> (std::max (1, groovePulses));
        const int minGap = static_cast<int> (sampleRate * pulseSec * 0.82);
        for (int i = 0; i < tick.pulsesFired; ++i)
        {
            int offset = tick.pulseOffset[i];
            if (offset < 0)
                offset = 0;
            if (offset >= numSamples)
                offset = numSamples - 1;
            if (samplesSinceHit + offset < minGap)
                continue;

            // Every pulse of the chosen grid gets a shaker, so 1/16 really is
            // sixteen hits per bar. The congas keep their own place in the bar
            // regardless of how fine that grid is, so the pattern is written on
            // the 16th grid and each pulse is mapped onto it. With 1/4 and 1/8
            // this lands on exactly the steps the 8-step table used to give;
            // only 1/16, which used to drop every odd pulse and play eighths,
            // changes.
            const int idx = tick.pulseIndex[i];
            const int barBeat = tick.pulseBeatInBar[i];
            const int pulses = std::max (1, groovePulses);
            const int step = (barBeat * 4 + (idx * 4) / pulses) % 16;

            triggerShaker (offset);
            // Tumbao / marcha on the bar's 8ths: 1, 2, 2&, 3, 4, 4&.
            // Odd 16ths carry no conga (Kind::shaker = "shaker only").
            static constexpr Kind kConga[16] = {
                Kind::tumba,  Kind::shaker, Kind::shaker, Kind::shaker,
                Kind::open,   Kind::shaker, Kind::slap,   Kind::shaker,
                Kind::open,   Kind::shaker, Kind::shaker, Kind::shaker,
                Kind::slap,   Kind::shaker, Kind::open,   Kind::shaker
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
        float fade = v.fade;
        const float fadeStep = v.fadeStep;
        int fadeDelay = v.fadeDelay;
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
            if (fadeStep > 0.0f)
            {
                if (fadeDelay > 0)
                {
                    --fadeDelay;
                }
                else
                {
                    fade -= fadeStep;
                    if (fade <= 0.0f)
                    {
                        fade = 0.0f;
                        v.active = false;
                        break;
                    }
                }
            }
            left[n]  += bL[static_cast<size_t> (p)] * v.gainL * g * fade;
            right[n] += bR[static_cast<size_t> (p)] * v.gainR * g * fade;
        }
        v.fade = fade;
        v.fadeDelay = fadeDelay;
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
