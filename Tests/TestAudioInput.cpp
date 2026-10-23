#include "TestAiBeat.h"
#include "Audio/TrackStreamer.h"
#include "Tracking/KickOnsetDetector.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>
#include <utility>

namespace
{
    class TailReader final : public juce::AudioFormatReader
    {
    public:
        TailReader (double rate, int frames, int channels) : AudioFormatReader (nullptr, "tail test")
        {
            sampleRate = rate;
            lengthInSamples = frames;
            numChannels = static_cast<unsigned int> (channels);
            bitsPerSample = 32;
            usesFloatingPointData = true;
        }

        float sample (juce::int64 frame, int channel) const
        {
            const float sign = channel == 0 ? 1.0f : -0.5f;
            return frame < lengthInSamples ? sign * (0.2f + 0.1f * std::sin (static_cast<float> (frame) * 0.01f)) : 0.0f;
        }

        bool readSamples (int* const* dest, int channels, int offset, juce::int64 start, int n) override
        {
            for (int c = 0; c < channels; ++c)
                if (dest[c] != nullptr)
                    for (int i = 0; i < n; ++i)
                        reinterpret_cast<float*> (dest[c])[offset + i] = sample (start + i, c);
            return true;
        }
    };

    bool drain (vp::TrackStreamer& stream, const TailReader& reader, double rate, int block,
                juce::int64 from = 0)
    {
        juce::AudioBuffer<float> output (2, block), expected (2, block), input (2, block * 4 + 16);
        juce::LagrangeInterpolator reference[2];
        const double ratio = reader.sampleRate / rate;
        juce::int64 pos = from;
        const int limit = static_cast<int> (std::ceil ((reader.lengthInSamples - from + 32) / ratio)) + block * 2;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (3);
        int rendered = 0;
        float error = 0.0f;
        while (! stream.hasStreamFinished() && rendered < limit)
        {
            if (std::chrono::steady_clock::now() > deadline)
                return false;
            const int underruns = stream.underrunCount();
            stream.getNextAudioBlock ({ &output, 0, block });
            if (stream.underrunCount() != underruns)
            {
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue; // the producer has not supplied this callback yet
            }
            const int want = static_cast<int> (std::ceil (block * ratio)) + 8;
            int used = 0;
            for (int c = 0; c < 2; ++c)
            {
                for (int i = 0; i < want; ++i)
                    input.setSample (c, i, reader.sample (pos + i, std::min (c, static_cast<int> (reader.numChannels) - 1)));
                used = reference[c].process (ratio, input.getReadPointer (c), expected.getWritePointer (c), block);
            }
            pos += used;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < block; ++i)
                    error = std::max (error, std::fabs (output.getSample (c, i) - expected.getSample (c, i)));
            rendered += block;
        }
        const bool ok = stream.hasStreamFinished() && ! stream.isPlaying()
                        && std::fabs (stream.getCurrentPosition() - stream.getLengthInSeconds()) < 1.0e-9
                        && pos >= reader.lengthInSamples + 8 && error < 1.0e-6f;
        if (! ok)
            std::printf ("stream-tail sr=%.0f out=%.0f frames=%lld from=%lld block=%d finished=%d pos=%.6f diff=%.3g\n",
                         reader.sampleRate, rate, reader.lengthInSamples, from, block,
                         stream.hasStreamFinished(), stream.getCurrentPosition(), error);
        return ok;
    }
}

void vpRunAudioInputTests (int& passed, int& failed)
{
    auto check = [&] (bool ok, const char* name)
    {
        (ok ? passed : failed)++;
        std::printf ("  %s  %s\n", ok ? "PASS" : "FAIL", name);
    };

    vp::KickOnsetDetector clean, dirty;
    clean.prepare (48000.0);
    dirty.prepare (48000.0);
    std::array<float, 256> a {}, b {};
    vp::KickOnsetDetector::Onset ca[8], cb[8];
    int hits = 0;
    bool same = true;
    const float bad[] { std::numeric_limits<float>::quiet_NaN(),
                        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
    for (int block = 0; block < 600; ++block)
    {
        for (int i = 0; i < 256; ++i)
        {
            const int k = (block * 256 + i) % 24000;
            a[i] = b[i] = k < 4000 ? 0.7f * std::sin (2.0f * 3.14159265f * 70.0f * k / 48000.0f)
                                             * std::exp (-k / 800.0f) : 0.0f;
        }
        if (block % 100 == 0)
        {
            a[7] = 0.0f;
            b[7] = bad[(block / 100) % 3];
        }
        const int na = clean.process (a.data(), 256, ca, 8);
        const int nb = dirty.process (b.data(), 256, cb, 8);
        same &= na == nb && std::isfinite (dirty.level()) && clean.level() == dirty.level()
                && clean.quietSeconds() == dirty.quietSeconds();
        for (int i = 0; i < std::min (na, nb); ++i)
            same &= ca[i].offset == cb[i].offset && ca[i].strength == cb[i].strength;
        hits += nb;
    }
    check (same && hits >= 6, "CASSA recovers from NaN and both infinities exactly as from zero samples");

    juce::TimeSliceThread thread ("tail test");
    thread.startThread();
    for (const auto rates : { std::pair {44100.0, 48000.0}, std::pair {48000.0, 44100.0},
                              std::pair {96000.0, 48000.0}, std::pair {48000.0, 96000.0},
                              std::pair {48000.0, 48000.0} })
        for (int block : { 1, 256 })
        {
            bool complete = true;
            for (int frames : { 0, 1, 1000, 1024, 4097 })
            for (int channels : { 1, 2 })
            {
                TailReader reader (rates.first, frames, channels);
                vp::TrackStreamer stream (thread);
                stream.prepareToPlay (block, rates.second);
                stream.setSource (&reader);
                stream.start();
                complete &= drain (stream, reader, rates.second, block);
                stream.setPosition (0.0);
                complete &= ! stream.hasStreamFinished();
                stream.start();
                complete &= drain (stream, reader, rates.second, block);
                if (frames > 19)
                {
                    stream.setPosition (19.5 / reader.sampleRate);
                    stream.start();
                    complete &= drain (stream, reader, rates.second, block, 19);
                }
            }
            check (complete, "BRANO drains EOF and interpolator, replays and seeks exactly like a direct reader");
        }
    thread.stopThread (1000);
}
