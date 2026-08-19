#include "Percussion/PercussionEngine.h"

#include <algorithm>
#include <cmath>

#if defined (VP_HAS_PERC_SAMPLES) && VP_HAS_PERC_SAMPLES
 #include <juce_audio_formats/juce_audio_formats.h>
 #include "VpPercussionData.h"
#endif

namespace vp
{

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    // Seconds of raised-cosine fade welded onto the end of every synthesised
    // sample. Each one is an exponential decay cut off at a fixed length, and
    // without this none of them has decayed far enough by then - the shaker
    // stopped at 21% of its peak, the open conga at 11%, the slap at 8%. That
    // step is a click on *every* hit, not a rare glitch.
    constexpr double kTailFadeSec = 0.012;

    // Fade when a voice is taken over by a new stroke of the same kind. Long
    // enough not to click, short enough that the new stroke is not muddied.
    constexpr double kStealFadeSec = 0.004;

    // Shortest gap between two strokes. A 32nd at 200 BPM is 37 ms, so anything
    // closer than this is the clock stuttering, not the groove.
    constexpr double kRetriggerGuardSec = 0.020;

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

    void normalise (std::vector<float>& L, std::vector<float>& R, float target) noexcept
    {
        float peak = 1.0e-6f;
        const int n = static_cast<int> (std::min (L.size(), R.size()));
        for (int i = 0; i < n; ++i)
            peak = std::max (peak, std::max (std::fabs (L[static_cast<size_t> (i)]),
                                             std::fabs (R[static_cast<size_t> (i)])));
        const float g = target / peak;
        for (int i = 0; i < n; ++i)
        {
            L[static_cast<size_t> (i)] *= g;
            R[static_cast<size_t> (i)] *= g;
        }
    }

    // Physical description of each articulation, before the dynamic layer
    // bends it. `noise` is how much of the stroke is hand and skin rather than
    // pitched membrane; `decay` is how fast the ring dies; `pan` is where the
    // drum sits, which is fixed per drum because a player does not move them.
    struct DrumSpec
    {
        float freq;
        float noise;
        float decay;
        float seconds;
        float pan;
    };

    DrumSpec specFor (Stroke s) noexcept
    {
        switch (s)
        {
            case Stroke::tumba: return {  82.0f, 0.10f,  8.5f, 0.30f, -0.30f };
            case Stroke::open:  return { 178.0f, 0.09f, 12.0f, 0.20f,  0.16f };
            case Stroke::slap:  return { 330.0f, 0.52f, 26.0f, 0.10f,  0.34f };
            case Stroke::heel:  return { 120.0f, 0.42f, 46.0f, 0.07f, -0.10f };
            case Stroke::toe:   return { 240.0f, 0.50f, 60.0f, 0.05f,  0.22f };
            case Stroke::muff:  return { 190.0f, 0.30f, 34.0f, 0.09f,  0.12f };
            default:            return { 180.0f, 0.30f, 20.0f, 0.12f,  0.0f };
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

void PercussionEngine::setHumanization (float amount) noexcept
{
    humanization = clamp01 (amount);
    groove.setHumanize (humanization);
}

void PercussionEngine::setSwing (float amount) noexcept
{
    groove.setSwing (amount);
}

void PercussionEngine::setIntensity (float amount) noexcept
{
    groove.setIntensity (amount);
}

void PercussionEngine::synthesizeShaker (Sample& s, Stroke stroke, int layer, std::uint32_t seed) noexcept
{
    // A shaker is two strokes, not one. The down-stroke throws the beads onto
    // the far wall and rings the gourd; the up-stroke is the return, shorter
    // and duller, and it is what the ear reads as the offbeat. Playing the same
    // sample for both is why a shaker part can be perfectly in time and still
    // sound like a click track with noise on it.
    const bool down = stroke == Stroke::shakerDown;
    const float force = 0.55f + 0.45f * (static_cast<float> (layer) / static_cast<float> (kLayers - 1));

    const double seconds = down ? 0.13 : 0.085;
    const int length = std::max (256, static_cast<int> (sampleRate * seconds));
    const float sr = static_cast<float> (sampleRate);

    s.left.assign (static_cast<size_t> (length), 0.0f);
    s.right.assign (static_cast<size_t> (length), 0.0f);
    DeterministicRng local (seed);

    // Harder strokes are brighter and snap open faster: the beads arrive
    // together instead of smearing.
    const float fBead = (down ? 5400.0f : 4300.0f) * (0.82f + 0.30f * force) / sr;
    const float fBody = (down ? 1400.0f : 1150.0f) / sr;
    const float decay = (down ? 11.0f : 17.0f) * (1.10f - 0.18f * force);
    const float snap = (down ? 380.0f : 520.0f) * (0.7f + 0.6f * force);

    Svf bpL, bpR, bodyL, bodyR;
    float lpL = 0.0f, lpR = 0.0f;
    for (int n = 0; n < length; ++n)
    {
        const float t = static_cast<float> (n) / sr;
        const float env = std::exp (-t * decay) * (1.0f - std::exp (-t * snap));
        const float nL = local.nextSigned();
        const float nR = local.nextSigned();
        const float beadsL = bpL.bandpass (nL, fBead, 0.20f);
        const float beadsR = bpR.bandpass (nR, fBead, 0.20f);
        const float gourdL = bodyL.bandpass (nL, fBody, 0.35f);
        const float gourdR = bodyR.bandpass (nR, fBody, 0.35f);
        lpL += 0.012f * (nL - lpL);
        lpR += 0.012f * (nR - lpR);
        const float hand = (n < static_cast<int> (0.008 * sampleRate))
                               ? std::exp (-t * 70.0f) * 0.10f * force : 0.0f;
        const float bead = 0.26f + 0.10f * force;
        s.left[static_cast<size_t> (n)]  = (beadsL * bead + gourdL * 0.10f + lpL * hand) * env;
        s.right[static_cast<size_t> (n)] = (beadsR * bead + gourdR * 0.10f + lpR * hand) * env;
    }

    normalise (s.left, s.right, 0.92f);
    fadeTail (s.left, s.right, sampleRate);
}

void PercussionEngine::synthesizeDrum (Sample& s, Stroke stroke, int layer, std::uint32_t seed) noexcept
{
    const DrumSpec spec = specFor (stroke);
    const float force = 0.55f + 0.45f * (static_cast<float> (layer) / static_cast<float> (kLayers - 1));
    const float sr = static_cast<float> (sampleRate);

    // Force does three things to a drum, and gain is none of them: the strike
    // gets noisier, the attack gets faster, and the head is driven harder so it
    // starts sharp and falls into pitch.
    const float noiseAmt = std::clamp (spec.noise * (0.6f + 0.9f * force), 0.0f, 0.95f);
    const float decay = spec.decay * (1.12f - 0.22f * force);
    const float bend = 0.06f + 0.10f * force;
    const float attack = 300.0f + 500.0f * force;

    const int n = std::max (256, static_cast<int> (sampleRate * static_cast<double> (spec.seconds)));
    s.left.assign (static_cast<size_t> (n), 0.0f);
    s.right.assign (static_cast<size_t> (n), 0.0f);

    DeterministicRng local (seed);
    const float wL = (1.0f - spec.pan) * 0.5f;
    const float wR = (1.0f + spec.pan) * 0.5f;

    float lp = 0.0f;
    Svf crack;
    for (int i = 0; i < n; ++i)
    {
        const float t = static_cast<float> (i) / sr;
        const float env = std::exp (-t * decay) * (1.0f - std::exp (-t * attack));

        // The head: fundamental falling into pitch, plus the first overtone a
        // conga actually has (roughly a fifth above, decaying faster).
        const float f0 = spec.freq * (1.0f + bend * std::exp (-t * 28.0f));
        const float skin = std::sin (2.0f * kPi * f0 * t)
                         + 0.30f * std::sin (3.0f * kPi * f0 * t) * std::exp (-t * decay * 1.8f);

        const float nse = local.nextSigned();
        lp += 0.09f * (nse - lp);
        const float slap = crack.bandpass (nse, (2200.0f + 2600.0f * force) / sr, 0.35f)
                           * std::exp (-t * (90.0f + 60.0f * force));

        const float v = (skin * (1.0f - noiseAmt) + (lp * 0.5f + slap) * noiseAmt) * env;
        s.left[static_cast<size_t> (i)] = v * wL;
        s.right[static_cast<size_t> (i)] = v * wR;
    }

    normalise (s.left, s.right, 0.95f);
    fadeTail (s.left, s.right, sampleRate);
}

bool PercussionEngine::loadRecordedStroke (Stroke stroke, std::vector<float>& mono) noexcept
{
    mono.clear();
   #if defined (VP_HAS_PERC_SAMPLES) && VP_HAS_PERC_SAMPLES
    // Which recording stands in for which articulation. heel, toe and muff have
    // no recording of their own and are derived from the open tone below,
    // because that is physically what they are: the same drum with the hand
    // left on the head.
    const char* name = nullptr;
    switch (stroke)
    {
        case Stroke::tumba:      name = "tumba_wav"; break;
        case Stroke::open:       name = "open_wav"; break;
        case Stroke::slap:       name = "slap_wav"; break;
        case Stroke::shakerDown: name = "shaker_down_wav"; break;
        case Stroke::shakerUp:   name = "shaker_up_wav"; break;
        default: return false;
    }

    int size = 0;
    const char* data = VpPercussionData::getNamedResource (name, size);
    if (data == nullptr || size <= 0)
        return false;

    juce::WavAudioFormat wav;
    auto* stream = new juce::MemoryInputStream (data, static_cast<size_t> (size), false);
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (stream, true));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const int n = static_cast<int> (reader->lengthInSamples);
    juce::AudioBuffer<float> buf (static_cast<int> (reader->numChannels), n);
    reader->read (&buf, 0, n, 0, true, true);

    // Resample to the device rate. These are short percussive hits, so linear
    // interpolation sits far below the transient.
    const double ratio = sampleRate / reader->sampleRate;
    const int outLen = std::max (16, static_cast<int> (n * ratio));
    mono.resize (static_cast<size_t> (outLen));
    for (int i = 0; i < outLen; ++i)
    {
        const double src = static_cast<double> (i) / ratio;
        const int i0 = std::min (n - 1, static_cast<int> (src));
        const int i1 = std::min (n - 1, i0 + 1);
        const float f = static_cast<float> (src - i0);
        float a = 0.0f, b = 0.0f;
        for (int c = 0; c < buf.getNumChannels(); ++c)
        {
            a += buf.getSample (c, i0);
            b += buf.getSample (c, i1);
        }
        const float inv = 1.0f / static_cast<float> (std::max (1, buf.getNumChannels()));
        mono[static_cast<size_t> (i)] = ((1.0f - f) * a + f * b) * inv;
    }
    return true;
   #else
    juce::ignoreUnused (stroke);
    return false;
   #endif
}

void PercussionEngine::layerFromRecording (Sample& dest, const std::vector<float>& src,
                                           Stroke stroke, int layer, std::uint32_t seed) noexcept
{
    // One recording per articulation is what the source library has, so the
    // dynamic layers are derived rather than sampled. That is an approximation,
    // but it is the right-shaped one: hitting a drum softer takes the top off
    // the strike and shortens the ring, it does not just turn it down. The hard
    // layer is the recording untouched.
    const float force = static_cast<float> (layer) / static_cast<float> (kLayers - 1);
    const DrumSpec spec = specFor (stroke);
    const bool shaker = stroke == Stroke::shakerDown || stroke == Stroke::shakerUp;

    // heel, toe and muff are the open tone with the hand left on the head: most
    // of the ring gone, and darker for it.
    float extraDecay = 0.0f;
    switch (stroke)
    {
        case Stroke::heel: extraDecay = 34.0f; break;
        case Stroke::toe:  extraDecay = 46.0f; break;
        case Stroke::muff: extraDecay = 22.0f; break;
        default: break;
    }

    const int n = static_cast<int> (src.size());
    const float sr = static_cast<float> (sampleRate);
    dest.left.assign (static_cast<size_t> (n), 0.0f);
    dest.right.assign (static_cast<size_t> (n), 0.0f);

    // Softer strokes lose the top. One pole is enough - the ear reads the
    // change of brightness, not the slope of the filter.
    const float cutoff = (shaker ? 3000.0f : 900.0f) + (shaker ? 9000.0f : 5000.0f) * force;
    const float a = 1.0f - std::exp (-2.0f * kPi * cutoff / sr);
    const float softening = 0.55f + 0.45f * force;

    DeterministicRng local (seed);
    // A couple of samples of start jitter, so two round-robin takes of a
    // derived layer do not phase-cancel into the same click.
    const int jitter = static_cast<int> (local.nextU32() % 24u);

    const float wL = (1.0f - spec.pan) * 0.5f;
    const float wR = (1.0f + spec.pan) * 0.5f;

    float lp = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const int j = std::min (n - 1, i + jitter);
        lp += a * (src[static_cast<size_t> (j)] - lp);
        const float t = static_cast<float> (i) / sr;
        const float damp = extraDecay > 0.0f ? std::exp (-t * extraDecay) : 1.0f;
        const float v = ((1.0f - softening) * lp + softening * src[static_cast<size_t> (j)]) * damp;
        dest.left[static_cast<size_t> (i)] = v * wL;
        dest.right[static_cast<size_t> (i)] = v * wR;
    }

    // A damped stroke is over long before the source recording is; carrying the
    // silence would just hold a voice open.
    if (extraDecay > 0.0f)
    {
        const int keep = std::min (n, static_cast<int> (sr * (4.0f / extraDecay)));
        dest.left.resize (static_cast<size_t> (keep));
        dest.right.resize (static_cast<size_t> (keep));
    }

    normalise (dest.left, dest.right, shaker ? 0.92f : 0.95f);
    fadeTail (dest.left, dest.right, sampleRate);
}

void PercussionEngine::buildBank() noexcept
{
    // The recordings are the OLPC Berklee Sound Library (CC BY 3.0). Anything
    // they do not cover falls back to synthesis, so the app builds and plays
    // with or without Assets/Percussion.
    std::vector<float> recording, openTone;
    loadRecordedStroke (Stroke::open, openTone);

    for (int st = 0; st < kStrokes; ++st)
    {
        const Stroke stroke = static_cast<Stroke> (st);

        const bool derived = stroke == Stroke::heel || stroke == Stroke::toe || stroke == Stroke::muff;
        bool haveSource = false;
        if (derived)
        {
            recording = openTone;
            haveSource = ! recording.empty();
        }
        else
        {
            haveSource = loadRecordedStroke (stroke, recording);
        }
        recorded[st] = haveSource;

        for (int layer = 0; layer < kLayers; ++layer)
        {
            for (int rr = 0; rr < kRoundRobin; ++rr)
            {
                const std::uint32_t seed = 0x5A4E11u
                                           + static_cast<std::uint32_t> (st) * 7919u
                                           + static_cast<std::uint32_t> (layer) * 104729u
                                           + static_cast<std::uint32_t> (rr) * 1299709u;
                Sample& s = bank[st][layer][rr];
                if (haveSource)
                    layerFromRecording (s, recording, stroke, layer, seed);
                else if (stroke == Stroke::shakerDown || stroke == Stroke::shakerUp)
                    synthesizeShaker (s, stroke, layer, seed);
                else
                    synthesizeDrum (s, stroke, layer, seed);
            }
        }
    }

    // The low drum has two genuine takes in the library. Use the second one for
    // the alternate round-robin slots, which is real variation rather than the
    // same take with its start nudged.
    std::vector<float> tumbaB;
   #if defined (VP_HAS_PERC_SAMPLES) && VP_HAS_PERC_SAMPLES
    {
        int size = 0;
        if (VpPercussionData::getNamedResource ("tumba_b_wav", size) != nullptr && size > 0)
        {
            const int idx = static_cast<int> (Stroke::tumba);
            std::vector<float> keep;
            std::swap (keep, tumbaB);
            // Reuse the loader by pointing it at the second take.
            juce::WavAudioFormat wav;
            const char* data = VpPercussionData::getNamedResource ("tumba_b_wav", size);
            auto* stream = new juce::MemoryInputStream (data, static_cast<size_t> (size), false);
            std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (stream, true));
            if (reader != nullptr && reader->lengthInSamples > 0 && recorded[idx])
            {
                const int n = static_cast<int> (reader->lengthInSamples);
                juce::AudioBuffer<float> buf (static_cast<int> (reader->numChannels), n);
                reader->read (&buf, 0, n, 0, true, true);
                const double ratio = sampleRate / reader->sampleRate;
                const int outLen = std::max (16, static_cast<int> (n * ratio));
                tumbaB.resize (static_cast<size_t> (outLen));
                for (int i = 0; i < outLen; ++i)
                {
                    const double src = static_cast<double> (i) / ratio;
                    const int i0 = std::min (n - 1, static_cast<int> (src));
                    tumbaB[static_cast<size_t> (i)] = buf.getSample (0, i0);
                }
                for (int layer = 0; layer < kLayers; ++layer)
                    for (int rr = 1; rr < kRoundRobin; rr += 2)
                        layerFromRecording (bank[idx][layer][rr], tumbaB, Stroke::tumba, layer,
                                            0x1234u + static_cast<std::uint32_t> (layer * 31 + rr));
            }
        }
    }
   #endif
}

void PercussionEngine::prepare (double sr) noexcept
{
    sampleRate = sr > 1.0 ? sr : 48000.0;
    reverb.setSampleRate (sampleRate);
    applyReverbParams();
    buildBank();
    groove.prepare (0x9E3779B9u);
    groove.setHumanize (humanization);
    reset();
}

void PercussionEngine::reset() noexcept
{
    clearVoices();
    groove.reset();
    totalHits = 0;
    hardStealCount = 0;
    lastActive = 0;
    lastReleasing = 0;
    samplesSinceHit = 1000000;
    barCounter = 0;
    lastBarBeat = -1;
    std::fill (rrCursor, rrCursor + kStrokes, 0);
    reverb.reset();
}

int PercussionEngine::recordedStrokeCount() const noexcept
{
    int n = 0;
    for (int i = 0; i < kStrokes; ++i)
        if (recorded[i])
            ++n;
    return n;
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
    groovePulses = pulsesPerBeat < 1 ? 4 : pulsesPerBeat;
}

void PercussionEngine::releaseStroke (Stroke stroke) noexcept
{
    const float step = 1.0f / std::max (1.0f, static_cast<float> (sampleRate * kStealFadeSec));
    for (auto& v : voices)
        if (v.active && v.stroke == stroke && v.fadeStep <= 0.0f)
            v.fadeStep = step;
}

PercussionEngine::Voice& PercussionEngine::allocateVoice() noexcept
{
    // A free slot if there is one, otherwise the oldest voice - the one
    // furthest into its decay, so it is the least missed.
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
        ++hardStealCount;
    }
    voices[slot] = {};
    voices[slot].age = ++voiceClock;
    return voices[slot];
}

const PercussionEngine::Sample& PercussionEngine::pick (Stroke stroke, float velocity, float& gain) noexcept
{
    const int st = std::clamp (static_cast<int> (stroke), 0, kStrokes - 1);

    // Which dynamic layer the stroke belongs to, and then how far into it: the
    // layer sets the timbre and the remainder sets the level, so a crescendo
    // moves smoothly instead of stepping between layers.
    const float v = std::clamp (velocity, 0.0f, 1.0f);
    const float scaled = v * static_cast<float> (kLayers);
    int layer = std::clamp (static_cast<int> (scaled), 0, kLayers - 1);
    const float within = scaled - static_cast<float> (layer);

    // Level within the layer spans the band the layer covers, so the loudest
    // stroke of layer 0 and the quietest of layer 1 meet rather than jump.
    const float lo = static_cast<float> (layer) / static_cast<float> (kLayers);
    gain = std::clamp (lo + within / static_cast<float> (kLayers), 0.08f, 1.0f);

    const int rr = rrCursor[st];
    rrCursor[st] = (rr + 1) % kRoundRobin;
    return bank[st][layer][rr];
}

void PercussionEngine::trigger (Stroke stroke, float velocity, int sampleOffset) noexcept
{
    releaseStroke (stroke);

    float gain = 1.0f;
    const Sample& s = pick (stroke, velocity, gain);
    if (s.left.empty())
        return;

    // A stroke sits where the drum sits; the tiny random spread on top is the
    // hand not landing in exactly the same place twice.
    const float spread = 0.04f * rng.nextSigned() * humanization;
    auto& v = allocateVoice();
    v.stroke = stroke;
    v.sample = &s;
    v.pos = -sampleOffset;
    v.length = static_cast<int> (s.left.size());
    v.gainL = gain * (1.0f - spread);
    v.gainR = gain * (1.0f + spread);
    v.active = true;
    ++totalHits;
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
        // more. It used to be 82% of the nominal pulse derived from the
        // *displayed* BPM, which is 120 until the tracker locks, so at any real
        // tempo above ~145 the guard was longer than the actual pulse and
        // swallowed every second stroke.
        const int minGap = static_cast<int> (sampleRate * kRetriggerGuardSec);
        const float beatSamples = 60.0f / std::max (40.0f, grooveBpm)
                                  * static_cast<float> (sampleRate);

        for (int i = 0; i < tick.pulsesFired; ++i)
        {
            int offset = tick.pulseOffset[i];
            if (offset < 0)
                offset = 0;
            if (offset >= numSamples)
                offset = numSamples - 1;
            if (samplesSinceHit + offset < minGap)
                continue;

            const int barBeat = tick.pulseBeatInBar[i];
            const int idx = tick.pulseIndex[i];

            // Bars are counted off the beat-in-bar wrapping back to zero, so
            // the two-bar phrase and the fill stay aligned to the song's bar
            // even across a re-lock.
            if (barBeat == 0 && idx == 0 && lastBarBeat != 0)
                ++barCounter;
            if (idx == 0)
                lastBarBeat = barBeat;

            // Map the clock's pulse onto the groove's sixteenth grid, whatever
            // resolution the clock is running at.
            const int per = std::max (1, groovePulses);
            const int step = (barBeat * 4 + (idx * 4) / per) % GrooveEngine::kStepsPerBar;

            GrooveEvent events[GrooveEngine::kMaxEvents];
            const int n = groove.eventsAt (barCounter, step, events, GrooveEngine::kMaxEvents);
            for (int e = 0; e < n; ++e)
            {
                const int delay = static_cast<int> (events[e].delayBeats * beatSamples);
                trigger (events[e].stroke, events[e].velocity, offset + delay);
            }
            if (n > 0)
                samplesSinceHit = -offset;
        }
    }

    int active = 0;
    int releasing = 0;
    const float g = volume;
    for (auto& v : voices)
    {
        if (! v.active || v.sample == nullptr)
            continue;
        ++active;
        if (v.fadeStep > 0.0f)
            ++releasing;
        const auto& bL = v.sample->left;
        const auto& bR = v.sample->right;
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
    lastReleasing = releasing;
    return active;
}

} // namespace vp
