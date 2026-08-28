#include "Percussion/PercussionEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#if defined (VP_HAS_PERC_SAMPLES) && VP_HAS_PERC_SAMPLES
 #include <juce_audio_formats/juce_audio_formats.h>
 #include "VpPercussionData.h"
#endif

namespace vp
{

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
    // How far above the recorded pitch the drums are read, as a frequency
    // ratio. The VCSL takes are the large drums of the library and they are
    // recorded slack: at concert pitch the whole kit reads as a floor tom under
    // a band, and the tumbao's low tone in particular disappears into the bass
    // guitar instead of answering it.
    //
    // A perfect fourth (2^(5/12)) was the first correction and it was not
    // enough - the part was still the bottom of the mix rather than sitting on
    // top of it. A minor seventh puts the tumba's fundamental at 146 Hz and the
    // open tone at 317, which is a conga and a quinto rather than two toms, and
    // is where a percussionist tunes a set that has to cut through a live band.
    //
    // It is one number on purpose: it drives the synthesised bank and the
    // playback rate of the recordings together, so the two halves of the bank
    // cannot end up tuned against each other.
    constexpr float kDrumTune = 1.781797f; // 2^(10/12)

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
        // Concert-pitch figures for the recorded drums, raised by kDrumTune.
        // Shakers are unpitched and stay where they were.
        constexpr float kTune = kDrumTune;
        switch (s)
        {
            case Stroke::tumba: return {  82.0f * kTune, 0.10f,  8.5f, 0.30f, -0.30f };
            case Stroke::open:  return { 178.0f * kTune, 0.09f, 12.0f, 0.20f,  0.16f };
            case Stroke::slap:  return { 330.0f * kTune, 0.52f, 26.0f, 0.10f,  0.34f };
            case Stroke::heel:  return { 120.0f * kTune, 0.42f, 46.0f, 0.07f, -0.10f };
            case Stroke::toe:   return { 240.0f * kTune, 0.50f, 60.0f, 0.05f,  0.22f };
            case Stroke::muff:  return { 190.0f * kTune, 0.30f, 34.0f, 0.09f,  0.12f };
            default:            return { 180.0f * kTune, 0.30f, 20.0f, 0.12f,  0.0f };
        }
    }
}

float PercussionEngine::drumTuneRatio() noexcept
{
    return kDrumTune;
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

bool PercussionEngine::loadNamedWav (const char* name, std::vector<float>& mono) noexcept
{
    mono.clear();
   #if defined (VP_HAS_PERC_SAMPLES) && VP_HAS_PERC_SAMPLES
    if (name == nullptr)
        return false;

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
    juce::ignoreUnused (name);
    return false;
   #endif
}


void PercussionEngine::layerFromRecording (Sample& dest, const std::vector<float>& src,
                                           Stroke stroke, float force, std::uint32_t seed) noexcept
{
    // `force` is 1 for a recording that already is that dynamic layer, and
    // 0..1 when the layer is derived from a louder take: hitting a drum softer
    // takes the top off the strike and shortens the ring, it does not just
    // turn it down.
    force = std::clamp (force, 0.0f, 1.0f);
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

    const int nSrc = static_cast<int> (src.size());
    const float sr = static_cast<float> (sampleRate);
    // Same interval as the synthetic bank. Reading the take faster raises the
    // membrane and shortens the ring, which is what a smaller drum does.
    const float pitch = shaker ? 1.0f : kDrumTune;
    const int n = std::max (16, static_cast<int> (static_cast<float> (nSrc) / pitch));
    dest.left.assign (static_cast<size_t> (n), 0.0f);
    dest.right.assign (static_cast<size_t> (n), 0.0f);

    // Softer strokes lose the top. One pole is enough - the ear reads the
    // change of brightness, not the slope of the filter.
    const float cutoff = (shaker ? 3000.0f : 1600.0f) + (shaker ? 9000.0f : 5200.0f) * force;
    const float a = 1.0f - std::exp (-2.0f * kPi * cutoff / sr);
    const float softening = 0.55f + 0.45f * force;

    juce::ignoreUnused (seed);

    const float wL = (1.0f - spec.pan) * 0.5f;
    const float wR = (1.0f + spec.pan) * 0.5f;

    float lp = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        // Do not vary a take by skipping samples at its start. The prepared
        // recordings put the hand transient only a few samples wide at the
        // front; the old 0..23-sample nudge skipped that transient in most
        // round-robin slots and exposed the body peak about 12 ms later.
        // Variation comes from the actual alternate takes and the per-hit
        // gain spread, without moving what the listener hears as the strike.
        const float srcPos = static_cast<float> (i) * pitch;
        const int j0 = std::min (nSrc - 1, std::max (0, static_cast<int> (srcPos)));
        const int j1 = std::min (nSrc - 1, j0 + 1);
        const float frac = srcPos - static_cast<float> (j0);
        const float x = src[static_cast<size_t> (j0)] * (1.0f - frac)
                      + src[static_cast<size_t> (j1)] * frac;
        lp += a * (x - lp);
        const float t = static_cast<float> (i) / sr;
        const float damp = extraDecay > 0.0f ? std::exp (-t * extraDecay) : 1.0f;
        const float v = ((1.0f - softening) * lp + softening * x) * damp;
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

namespace
{
    /** Ceiling on the attack compensation. */
    constexpr double kMaxAttackLeadSec = 0.025;

    /** Where a recording is heard as starting, in samples from its first.
        Taken at the point the energy envelope first reaches a large fraction of
        the peak it will reach inside the attack window - not the first sample
        above silence, which for a shaker is twelve milliseconds of rising
        rattle before anything reads as an event, and not the peak either, which
        for a drum is well inside the decay.

        The fraction is high on purpose: measured across the bundled library it
        puts a slap at 2.8 ms and a shaker at 10-13 ms, which is the difference
        a listener actually hears between them. */
    int measureAttack (const std::vector<float>& x, double sampleRate) noexcept
    {
        const int window = std::min (static_cast<int> (x.size()),
                                     static_cast<int> (sampleRate * 0.040));
        if (window < 8)
            return 0;

        const float atk = 1.0f - std::exp (-1.0f / static_cast<float> (sampleRate * 0.0005));
        float e = 0.0f, peak = 0.0f;
        std::vector<float> env (static_cast<size_t> (window), 0.0f);
        for (int i = 0; i < window; ++i)
        {
            const float v = std::fabs (x[static_cast<size_t> (i)]);
            e += (v - e) * (v > e ? 0.5f : atk);
            env[static_cast<size_t> (i)] = e;
            peak = std::max (peak, e);
        }
        if (peak < 1.0e-6f)
            return 0;

        const float target = peak * 0.80f;
        for (int i = 0; i < window; ++i)
            if (env[static_cast<size_t> (i)] >= target)
                return i;
        return 0;
    }
}

float PercussionEngine::attackMsFor (Stroke s) const noexcept
{
    const int idx = std::clamp (static_cast<int> (s), 0, kStrokes - 1);
    return static_cast<float> (bank[idx][0][0].attack)
           / static_cast<float> (sampleRate) * 1000.0f;
}

void PercussionEngine::buildBank() noexcept
{
    // Recordings are the Versilian Community Sample Library (CC0). Anything
    // they do not cover falls back to synthesis, so the app builds and plays
    // with or without Assets/Percussion.
    std::vector<float> openTone;
    loadNamedWav ("open_wav", openTone);

    static_assert (static_cast<int> (Stroke::shakerDown) == 0
                   && static_cast<int> (Stroke::muff) == 7
                   && kStrokes == 8);
    static const char* kStem[kStrokes] = {
        "shaker_down", "shaker_up", "tumba", "open", "slap",
        nullptr, nullptr, nullptr
    };

    for (int st = 0; st < kStrokes; ++st)
    {
        const Stroke stroke = static_cast<Stroke> (st);
        const bool derived = stroke == Stroke::heel || stroke == Stroke::toe || stroke == Stroke::muff;

        std::vector<float> hard, hardB, med, soft;
        if (derived)
        {
            hard = openTone;
        }
        else if (kStem[st] != nullptr)
        {
            char name[64];
            std::snprintf (name, sizeof name, "%s_wav", kStem[st]);
            loadNamedWav (name, hard);
            std::snprintf (name, sizeof name, "%s_b_wav", kStem[st]);
            loadNamedWav (name, hardB);
            std::snprintf (name, sizeof name, "%s_med_wav", kStem[st]);
            loadNamedWav (name, med);
            std::snprintf (name, sizeof name, "%s_soft_wav", kStem[st]);
            loadNamedWav (name, soft);
        }

        const bool haveSource = ! hard.empty();
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
                if (! haveSource)
                {
                    if (stroke == Stroke::shakerDown || stroke == Stroke::shakerUp)
                        synthesizeShaker (s, stroke, layer, seed);
                    else
                        synthesizeDrum (s, stroke, layer, seed);
                    continue;
                }

                const std::vector<float>* src = &hard;
                float force = static_cast<float> (layer) / static_cast<float> (kLayers - 1);

                if (layer == 0 && ! soft.empty())
                {
                    src = &soft;
                    force = 1.0f;
                }
                else if (layer == 1 && ! med.empty())
                {
                    src = &med;
                    force = 1.0f;
                }
                else if (layer == kLayers - 1)
                {
                    src = (rr == 1 && ! hardB.empty()) ? &hardB : &hard;
                    force = 1.0f;
                }
                else if (layer == 0 && ! med.empty())
                {
                    src = &med;
                    force = 0.28f;
                }

                layerFromRecording (s, *src, stroke, force, seed);
            }
        }
    }

    measureBankAttacks();
}

void PercussionEngine::measureBankAttacks() noexcept
{
    int slowest = 0;
    for (int st = 0; st < kStrokes; ++st)
    {
        // Averaged over the articulation's takes, not taken per take.
        //
        // The prepared alternate takes share one transient placement but differ
        // in the body of the sound. Keep one timing value for the articulation
        // so changing round-robin slot cannot change where a written stroke
        // lands; per-take compensation would also conceal a badly aligned asset
        // instead of letting the attack regression catch it.
        double sum = 0.0;
        int counted = 0;
        // Averaged over the hard layer's takes only. The soft/medium layers are
        // either a quieter recording or a filtered one, and folding them into
        // the mean pulls the compensation off the stroke the clock actually
        // places - a slap that then lands three milliseconds before a shaker
        // written on the same pulse.
        const int layer = kLayers - 1;
        for (int rr = 0; rr < kRoundRobin; ++rr)
        {
            const int a = measureAttack (bank[st][layer][rr].left, sampleRate);
            if (a > 0)
            {
                sum += a;
                ++counted;
            }
        }
        const int mean = counted > 0 ? static_cast<int> (sum / counted) : 0;
        for (int ly = 0; ly < kLayers; ++ly)
            for (int rr = 0; rr < kRoundRobin; ++rr)
                bank[st][ly][rr].attack = mean;
        slowest = std::max (slowest, mean);
    }

    // Bounded. A compensation of tens of milliseconds would be the clock
    // running ahead of the music by more than the effect it is correcting, and
    // a damaged asset must not be able to drag the whole part early.
    bankAttackLead = std::min (slowest, static_cast<int> (sampleRate * kMaxAttackLeadSec));
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
        if (v.active && v.stroke == stroke && v.fadeStep <= 0.0f && v.pos >= 0)
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
    // Every stroke is held back by the difference between the slowest attack in
    // the bank and its own, so a slap and a shaker started for the same beat are
    // *heard* together instead of eleven milliseconds apart. The clock supplies
    // the matching lead, so the pair lands on the beat rather than after it.
    const int hold = std::max (0, bankAttackLead - s.attack);

    auto& v = allocateVoice();
    v.stroke = stroke;
    v.sample = &s;
    v.pos = -(sampleOffset + hold);
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
        const bool playing = enabled && audible;

        for (int i = 0; i < tick.pulsesFired; ++i)
        {
            int offset = tick.pulseOffset[i];
            if (offset < 0)
                offset = 0;
            if (offset >= numSamples)
                offset = numSamples - 1;

            const int barBeat = tick.pulseBeatInBar[i];
            const int idx = tick.pulseIndex[i];

            // Bars are counted off the beat-in-bar wrapping back to zero, so
            // the two-bar phrase and the fill stay aligned to the song's bar
            // even across a re-lock.
            //
            // This is bookkeeping about where the *song* is, so it has to run on
            // every pulse the clock emits - including the ones nothing is played
            // on. It used to sit below both the guard below and the audible
            // check that wrapped this whole loop, so the count stopped dead
            // whenever the part was switched off or was waiting to come in, and
            // started again from wherever it had been left. Measured with the
            // part muted for two bars, the phrase came back two bars behind the
            // song and stayed there: the four-bar figure was playing the wrong
            // bar and the fill landed in the middle of the phrase, which is
            // heard as the percussion having lost the form.
            if (barBeat == 0 && idx == 0 && lastBarBeat != 0)
                ++barCounter;
            if (idx == 0)
                lastBarBeat = barBeat;

            if (! playing)
                continue;
            if (samplesSinceHit + offset < minGap)
                continue;

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

    // Steal at the moment the new stroke is heard, not when it was queued.
    // A swung 16th is scheduled on the previous pulse and starts after the
    // next one has already fired; releasing then deleted it before it sounded.
    {
        const float steal = 1.0f / std::max (1.0f, static_cast<float> (sampleRate * kStealFadeSec));
        for (int i = 0; i < kVoices; ++i)
        {
            auto& v = voices[i];
            if (! v.active || v.sample == nullptr)
                continue;
            int startAt = 0;
            if (v.pos >= 0)
                continue;
            if (v.pos + numSamples <= 0)
                continue;
            startAt = -v.pos;
            for (int j = 0; j < kVoices; ++j)
            {
                if (i == j)
                    continue;
                auto& o = voices[j];
                if (! o.active || o.stroke != v.stroke || o.fadeStep > 0.0f)
                    continue;
                const bool earlier = o.pos >= 0
                                  || (o.pos < 0 && -o.pos < startAt);
                if (earlier)
                    o.fadeStep = steal;
            }
        }
    }

    int active = 0;
    int releasing = 0;
    // Centre: both sit at full. Past the centre the quieter side falls
    // linearly to silence; the louder side stays at one, so moving the mix
    // never makes the instrument you are turning toward quieter.
    const float shakerG = instrumentMix <= 0.5f ? 1.0f : 2.0f * (1.0f - instrumentMix);
    const float congaG  = instrumentMix >= 0.5f ? 1.0f : 2.0f * instrumentMix;
    for (auto& v : voices)
    {
        if (! v.active || v.sample == nullptr)
            continue;
        ++active;
        if (v.fadeStep > 0.0f)
            ++releasing;
        const bool shaker = v.stroke == Stroke::shakerDown || v.stroke == Stroke::shakerUp;
        const float g = volume * (shaker ? shakerG : congaG);
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
