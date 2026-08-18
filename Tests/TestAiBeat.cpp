#include "TestAiBeat.h"

#include "AI/AudioFifo.h"
#include "AI/BeatDecoder.h"
#include "AI/BeatModelConfig.h"
#include "AI/LogSpectFeatures.h"
#include "AI/NeuralBeatTracker.h"
#include "AI/ModelLocator.h"
#include "AI/OnnxBeatModel.h"
#include "AI/OnnxSession.h"
#include "AI/StubBeatModel.h"
#include "Audio/VirtualPercussionEngine.h"
#include "Platform/NativeAudioBridge.h"
#include "Stretch/StretchFactor.h"
#include "Stretch/TimeStretchEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

namespace
{
    int* gPass = nullptr;
    int* gFail = nullptr;

    void expect (bool cond, const char* name)
    {
        if (cond)
        {
            ++*gPass;
            std::printf ("  PASS  %s\n", name);
        }
        else
        {
            ++*gFail;
            std::printf ("  FAIL  %s\n", name);
        }
    }

    // Kick / snare / hats at a steady tempo, beat one landing on sample zero, so
    // the true beat phase at any sample is known exactly.
    void renderKitTrack (std::vector<float>& dest, float bpm, double sr)
    {
        const double inc = (static_cast<double> (bpm) / 60.0) / sr;
        double ph = 0.0;
        for (size_t i = 0; i < dest.size(); ++i)
        {
            const double beat = ph - std::floor (ph);
            const int bi = static_cast<int> (std::floor (ph));
            const float t = static_cast<float> (beat) / (bpm / 60.0f);
            const double eighth = beat * 2.0 - std::floor (beat * 2.0);
            float s = 0.0f;
            if (bi % 2 == 0 && beat < 0.06)
                s += std::sin (2.0f * 3.14159265f * 55.0f * t) * std::exp (-t * 22.0f);
            if (bi % 2 == 1 && beat < 0.05)
                s += (0.35f * ((i % 17) / 17.0f - 0.5f)
                      + 0.2f * std::sin (2.0f * 3.14159265f * 180.0f * t))
                     * std::exp (-t * 18.0f);
            if (eighth < 0.02)
                s += 0.18f * ((i % 11) / 11.0f - 0.5f)
                     * std::exp (static_cast<float> (-eighth) * 70.0f);
            if (beat < 0.12)
                s += 0.25f * std::sin (2.0f * 3.14159265f * 98.0f * t) * std::exp (-t * 8.0f);
            dest[i] = s;
            ph += inc;
        }
    }

    // One sharp broadband click per beat, first click on sample zero. Nothing
    // between the beats, so unlike a kit pattern there is no metrical level to
    // argue about and no subdivision an onset can be confused with: the phase the
    // engine reports can be compared against the notated beat directly.
    void renderClickTrack (std::vector<float>& dest, float bpm, double sr)
    {
        std::fill (dest.begin(), dest.end(), 0.0f);
        const double beatSamples = 60.0 / static_cast<double> (bpm) * sr;
        const int beats = static_cast<int> (static_cast<double> (dest.size()) / beatSamples);
        const int len = static_cast<int> (0.05 * sr);

        for (int b = 0; b < beats; ++b)
        {
            const size_t at = static_cast<size_t> (static_cast<double> (b) * beatSamples);
            uint32_t rng = 0x9e3779b9u ^ static_cast<uint32_t> (b);
            for (int i = 0; i < len && at + static_cast<size_t> (i) < dest.size(); ++i)
            {
                const float t = static_cast<float> (i) / static_cast<float> (sr);
                rng = rng * 1664525u + 1013904223u;
                const float noise = static_cast<float> (rng >> 8) / 8388608.0f - 1.0f;
                dest[at + static_cast<size_t> (i)] =
                    (0.5f * noise + 0.5f * std::sin (2.0f * 3.14159265f * 1000.0f * t))
                    * std::exp (-t * 120.0f) * (b % 4 == 0 ? 0.9f : 0.6f);
            }
        }
    }
}

void vpRunAiBeatTests (int& passed, int& failed)
{
    gPass = &passed;
    gFail = &failed;
    std::printf ("\nAI beat tracking / TSM\n");


    {
        vp::AudioFifo fifo;
        fifo.prepare (32);
        float in[10];
        for (int i = 0; i < 10; ++i)
            in[i] = static_cast<float> (i);
        fifo.push (in, 10);
        float out[10] {};
        expect (fifo.pop (out, 10) == 10 && out[9] == 9.0f, "AudioFifo round-trip");
    }

    {
        vp::BeatDecoder dec;
        dec.prepare (50.0);
        vp::BeatHypothesis last;
        for (int i = 0; i < 400; ++i)
        {
            const bool on = (i % 25) == 0;
            last = dec.observe (on ? 0.95f : 0.05f, 0.05f, on ? 0.0f : 0.90f);
        }
        std::printf ("decoder  bpm=%.2f conf=%.2f valid=%d\n",
                     static_cast<double> (last.bpm),
                     static_cast<double> (last.confidence),
                     last.valid ? 1 : 0);
        expect (last.valid && std::fabs (last.bpm - 120.0f) < 4.0f,
                "BeatDecoder locks 120 BPM from 50 fps activations");
    }

    {
        vp::BeatDecoder dec;
        dec.prepare (50.0);
        vp::BeatHypothesis last;
        for (int i = 0; i < 400; ++i)
        {
            const bool on = (i % 25) == 0;
            last = dec.observe (on ? 0.42f : 0.04f, on ? 0.18f : 0.04f, on ? 0.10f : 0.90f);
        }
        last = dec.observe (0.04f, 0.04f, 0.90f);
        std::printf ("weak-decoder  bpm=%.2f conf=%.2f valid=%d\n",
                     static_cast<double> (last.bpm),
                     static_cast<double> (last.confidence),
                     last.valid ? 1 : 0);
        expect (last.valid && std::fabs (last.bpm - 120.0f) < 6.0f,
                "BeatDecoder locks 120 BPM from moderate BeatNet peaks");
        expect (last.confidence > 0.35f,
                "decoder confidence holds between beats once tempo is valid");
    }

    {
        vp::BeatDecoder dec;
        dec.prepare (50.0);
        vp::BeatHypothesis last;
        int peaks = 0;
        for (int i = 0; i < 400; ++i)
        {
            const bool on = (i % 25) == 0;
            const bool ghost = (i % 25) == 12;
            last = dec.observe (on ? 0.90f : (ghost ? 0.24f : 0.04f),
                                ghost ? 0.20f : 0.04f,
                                on ? 0.05f : 0.90f);
            if (last.peak)
                ++peaks;
        }
        std::printf ("ghost-decoder  bpm=%.2f peaks=%d valid=%d\n",
                     static_cast<double> (last.bpm), peaks, last.valid ? 1 : 0);
        expect (last.valid && std::fabs (last.bpm - 120.0f) < 8.0f,
                "decoder ignores mid-beat ghosts instead of jumping tempo");
        expect (peaks > 10 && peaks < 22,
                "decoder emits one peak per beat, not per frame");
    }

    {
        vp::BeatDecoder dec;
        dec.prepare (50.0);
        vp::BeatHypothesis last;
        float bpmLo = 1000.0f;
        float bpmHi = 0.0f;
        for (int i = 0; i < 1200; ++i)
        {
            const int inBeat = i % 25;
            const int beatNo = i / 25;
            const bool beat = inBeat == 0;
            const int pattern = beatNo % 8;
            const int syncOffset = pattern == 1 ? 9
                                 : pattern == 3 ? 14
                                 : pattern == 4 ? 18
                                 : pattern == 6 ? 11
                                 : -1;
            const bool syncopation = inBeat == syncOffset;
            const bool missedBeat = pattern == 2 || pattern == 5;
            last = dec.observe (beat ? (missedBeat ? 0.18f : 0.58f)
                                     : (syncopation ? 0.66f : 0.04f),
                                (beat && beatNo % 4 == 0) ? 0.72f : 0.03f,
                                beat || syncopation ? 0.08f : 0.92f);
            if (i > 300 && last.valid)
            {
                bpmLo = std::min (bpmLo, last.bpm);
                bpmHi = std::max (bpmHi, last.bpm);
            }
        }
        std::printf ("syncopated-decoder  bpm=%.2f span=%.2f valid=%d\n",
                     static_cast<double> (last.bpm),
                     static_cast<double> (bpmHi - bpmLo),
                     last.valid ? 1 : 0);
        expect (last.valid && std::fabs (last.bpm - 120.0f) < 6.0f,
                "decoder keeps the song pulse through strong syncopation");
        expect ((bpmHi - bpmLo) < 3.0f,
                "fixed-tempo neural BPM does not oscillate");
    }

    {
        vp::StretchFactor sf;
        sf.prepare (120.0f, 48000.0);
        sf.setLiveClock (120.0f, 0.0f, 0.0f);
        for (int i = 0; i < 300; ++i)
            sf.advance (128);
        expect (std::fabs (sf.ratio() - 1.0f) < 0.02f, "stretch ratio 1.0 at equal BPM");

        sf.prepare (120.0f, 48000.0);
        sf.setLiveClock (144.0f, 0.0f, 0.0f);
        for (int i = 0; i < 400; ++i)
            sf.advance (128);
        std::printf ("stretch  ratio=%.3f (expect ~1.20)\n", static_cast<double> (sf.ratio()));
        expect (sf.ratio() > 1.12f && sf.ratio() < 1.28f, "stretch ratio follows 144/120");
    }

    {
        const int n = 48000;
        std::vector<float> loop (static_cast<size_t> (n), 0.0f);
        for (int i = 0; i < n; ++i)
            loop[static_cast<size_t> (i)] = 0.2f * std::sin (2.0f * 3.14159265f * 220.0f
                                                              * static_cast<float> (i) / 48000.0f);

        vp::TimeStretchEngine tsm;
        tsm.prepare (48000.0, 512);
        tsm.loadLoop (loop.data(), loop.data(), n);
        std::vector<float> L (512, 0.0f), R (512, 0.0f);
        tsm.process (L.data(), R.data(), 512, 1.0f);
        float e = 0.0f;
        for (float s : L)
            e += s * s;
        expect (tsm.hasLoop() && e > 1.0e-4f, "WSOLA loop produces energy at ratio 1");

        tsm.process (L.data(), R.data(), 512, 1.25f);
        float e2 = 0.0f;
        for (float s : L)
            e2 += s * s;
        expect (e2 > 1.0e-4f, "WSOLA still produces energy at ratio 1.25");
    }

    {
        vp::LogSpectFeatures feat;
        feat.prepare (22050.0, 441);
        std::vector<float> x (441 * 8, 0.0f);
        for (int i = 0; i < static_cast<int> (x.size()); ++i)
            x[static_cast<size_t> (i)] = 0.1f * std::sin (2.0f * 3.14159265f * 440.0f
                                                          * static_cast<float> (i) / 22050.0f);
        feat.process (x.data(), static_cast<int> (x.size()));
        float frame[vp::LogSpectFeatures::kDim];
        int frames = 0;
        while (feat.popFrame (frame))
            ++frames;
        expect (frames >= 1, "LogSpectFeatures emits BeatNet frames");
    }

    {
        vp::OnnxSession s;
        if (! s.available())
            expect (true, "ONNX Runtime stubbed (VP_USE_ONNX=OFF)");
        else
            expect (! s.load ("missing.onnx", vp::OnnxModelConfig {}),
                    "ONNX session fails closed on a missing model");
    }

    {
        vp::NeuralBeatTracker nb;
        nb.setModel (std::make_unique<vp::StubBeatModel>());
        expect (nb.start (48000.0), "NeuralBeatTracker starts with stub model");
        std::vector<float> z (512, 0.0f);
        for (int i = 0; i < 20; ++i)
            nb.feed (z.data(), 512);
        vp::BeatHypothesis h;
        nb.tryLoad (h);
        nb.stop();
        expect (! nb.running(), "NeuralBeatTracker stops the worker");
    }

    {
        vp::VirtualPercussionEngine eng;
        eng.prepare (48000.0, 128, 1);
        vp::NativeAudioBridge bridge;
        bridge.prepare (128, 1, 2);
        std::vector<float> in (128, 0.0f), out (256, 0.0f);
        bridge.processInterleaved (eng, in.data(), 1, out.data(), 2, 128);
        vp::BeatHypothesis h;
        expect (! eng.tryLoadNeuralHypothesis (h),
                "neural hypothesis empty until the worker has audio");
    }

#if defined(VP_USE_ONNX) && VP_USE_ONNX
    {
        vp::OnnxBeatModel model;
        if (! vp::loadDefaultBeatModel (model))
        {
            expect (true, "ONNX lock skipped (no beatnet.onnx)");
        }
        else
        {
            std::printf ("onnx  loaded\n");

            // How closely does the clock sit on the song's pulse, and does it
            // stay there? Everything else in this file measures the reported
            // tempo, which can be right while the percussion plays late.
            for (float trackBpm : { 78.0f, 100.0f, 138.0f })
            {
                const double sr = 48000.0;
                const int block = 128;
                const int n = static_cast<int> (sr * 26.0);
                std::vector<float> song (static_cast<size_t> (n), 0.0f);
                renderClickTrack (song, trackBpm, sr);

                vp::VirtualPercussionEngine eng;
                eng.prepare (sr, block, 1);
                eng.start();
                std::vector<float> oL (static_cast<size_t> (block), 0.0f);
                std::vector<float> oR (static_cast<size_t> (block), 0.0f);
                float* outs[2] = { oL.data(), oR.data() };

                const double beatsPerSample = static_cast<double> (trackBpm) / 60.0 / sr;
                float worstLate = 0.0f;
                float earlyHalf = 0.0f, lateHalf = 0.0f;
                int   earlyN = 0, lateN = 0;
                int pos = 0, blocks = 0;
                vp::EngineSnapshot last;
                while (pos + block <= n)
                {
                    const float* ins[1] = { song.data() + pos };
                    eng.process (ins, 1, outs, 2, block);
                    last = eng.snapshot();
                    if (last.state == vp::TrackingState::following && last.bpm > 40.0f)
                    {
                        const double truePhase = static_cast<double> (pos) * beatsPerSample;
                        const float err = vp::wrapCentered (
                            last.beatPhase - static_cast<float> (truePhase - std::floor (truePhase)));
                        const double t = static_cast<double> (pos) / sr;
                        if (t > 8.0)
                        {
                            worstLate = std::max (worstLate, std::fabs (err));
                            // Split the run in two: a constant offset is a
                            // calibration question, a growing one is a wrong
                            // rate that will walk off the beat.
                            if (t < 17.0) { earlyHalf += err; ++earlyN; }
                            else          { lateHalf += err; ++lateN; }
                        }
                    }
                    if (std::getenv ("VP_TRACE") != nullptr && (blocks % 800) == 0)
                        std::printf ("   t=%5.2f bpm=%7.2f nn=%7.2f tgt=%7.2f regime=%d state=%s\n",
                                     static_cast<double> (pos) / sr,
                                     static_cast<double> (last.bpm),
                                     static_cast<double> (last.neuralBpm),
                                     static_cast<double> (last.targetBpm),
                                     last.tempoRegime, vp::toString (last.state));
                    pos += block;
                    if ((++blocks % 10) == 0)
                        std::this_thread::sleep_for (std::chrono::milliseconds (4));
                }
                const float meanEarly = earlyN > 0 ? earlyHalf / static_cast<float> (earlyN) : 0.0f;
                const float meanLate = lateN > 0 ? lateHalf / static_cast<float> (lateN) : 0.0f;
                const float beatMs = 60000.0f / trackBpm;
                std::printf ("phase-lock %5.1f BPM  bpm=%6.2f lead=%5.1fms  mean %+.3f -> %+.3f beat"
                             "  (%+.0f -> %+.0f ms)  worst=%.3f  regime=%d\n",
                             static_cast<double> (trackBpm), static_cast<double> (last.bpm),
                             static_cast<double> (last.leadMs),
                             static_cast<double> (meanEarly), static_cast<double> (meanLate),
                             static_cast<double> (meanEarly * beatMs),
                             static_cast<double> (meanLate * beatMs),
                             static_cast<double> (worstLate), last.tempoRegime);

                expect (std::fabs (meanEarly) < 0.05f && std::fabs (meanLate) < 0.05f,
                        "clock sits on the song pulse, not behind it");
                expect (std::fabs (meanLate - meanEarly) < 0.02f,
                        "phase alignment holds over time instead of walking off");
            }

            constexpr double sr = 48000.0;
            constexpr int block = 128;
            constexpr float bpm = 120.0f;
            const int n = static_cast<int> (sr * 10.0);
            std::vector<float> audio (static_cast<size_t> (n), 0.0f);
            const double inc = (static_cast<double> (bpm) / 60.0) / sr;
            double ph = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double beat = ph - std::floor (ph);
                const int bi = static_cast<int> (std::floor (ph));
                const float t = static_cast<float> (beat) / (bpm / 60.0f);
                const double eighth = beat * 2.0 - std::floor (beat * 2.0);
                if (beat < 0.05)
                {
                    audio[static_cast<size_t> (i)] =
                        std::sin (2.0f * 3.14159265f * 55.0f * t) * std::exp (-t * 24.0f);
                    if ((bi & 1) != 0)
                        audio[static_cast<size_t> (i)] +=
                            (0.35f * ((i % 17) / 17.0f - 0.5f)
                             + 0.2f * std::sin (2.0f * 3.14159265f * 180.0f * t))
                            * std::exp (-t * 16.0f);
                }
                if (eighth < 0.02)
                    audio[static_cast<size_t> (i)] += 0.18f * ((i % 11) / 11.0f - 0.5f)
                        * std::exp (static_cast<float> (-eighth) * 70.0f);
                ph += inc;
            }

            vp::VirtualPercussionEngine eng;
            eng.prepare (sr, block, 1);
            eng.start();
            std::vector<float> outL (static_cast<size_t> (block), 0.0f);
            std::vector<float> outR (static_cast<size_t> (block), 0.0f);
            float* outs[2] = { outL.data(), outR.data() };
            vp::EngineSnapshot last;
            int pos = 0;
            int blocks = 0;
            while (pos + block <= n)
            {
                const float* ins[1] = { audio.data() + pos };
                eng.process (ins, 1, outs, 2, block);
                last = eng.snapshot();
                pos += block;
                if ((++blocks % 10) == 0)
                    std::this_thread::sleep_for (std::chrono::milliseconds (4));
            }
            std::vector<float> z (static_cast<size_t> (block), 0.0f);
            const float* zIn[1] = { z.data() };
            for (int i = 0; i < 40; ++i)
            {
                eng.process (zIn, 1, outs, 2, block);
                last = eng.snapshot();
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
            }
            std::printf ("onnx-click  bpm=%.1f  state=%s  conf=%.2f\n",
                         static_cast<double> (last.bpm), vp::toString (last.state),
                         static_cast<double> (last.confidence));

            std::vector<float> kit (static_cast<size_t> (n), 0.0f);
            ph = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double beat = ph - std::floor (ph);
                const int bi = static_cast<int> (std::floor (ph));
                const float tBeat = static_cast<float> (beat) / (bpm / 60.0f);
                const double eighth = beat * 2.0 - std::floor (beat * 2.0);
                if (bi % 2 == 0 && beat < 0.06)
                    kit[static_cast<size_t> (i)] +=
                        std::sin (2.0f * 3.14159265f * 55.0f * tBeat) * std::exp (-tBeat * 22.0f);
                if (bi % 2 == 1 && beat < 0.05)
                    kit[static_cast<size_t> (i)] +=
                        (0.35f * ((i % 17) / 17.0f - 0.5f) + 0.2f * std::sin (2.0f * 3.14159265f * 180.0f * tBeat))
                        * std::exp (-tBeat * 18.0f);
                if (eighth < 0.02)
                    kit[static_cast<size_t> (i)] += 0.18f * ((i % 11) / 11.0f - 0.5f)
                        * std::exp (static_cast<float> (-eighth) * 70.0f);
                if (beat < 0.12)
                    kit[static_cast<size_t> (i)] +=
                        0.25f * std::sin (2.0f * 3.14159265f * 98.0f * tBeat) * std::exp (-tBeat * 8.0f);
                ph += inc;
            }

            vp::VirtualPercussionEngine kitEng;
            kitEng.prepare (sr, block, 1);
            kitEng.start();
            pos = 0;
            blocks = 0;
            float bpmLo = 1000.0f;
            float bpmHi = 0.0f;
            int bpmSamples = 0;
            while (pos + block <= n)
            {
                const float* ins[1] = { kit.data() + pos };
                kitEng.process (ins, 1, outs, 2, block);
                last = kitEng.snapshot();
                if (pos > static_cast<int> (sr * 6.0) && last.bpm > 40.0f)
                {
                    bpmLo = std::min (bpmLo, last.bpm);
                    bpmHi = std::max (bpmHi, last.bpm);
                    ++bpmSamples;
                }
                pos += block;
                if ((++blocks % 10) == 0)
                    std::this_thread::sleep_for (std::chrono::milliseconds (4));
            }
            for (int i = 0; i < 40; ++i)
            {
                kitEng.process (zIn, 1, outs, 2, block);
                last = kitEng.snapshot();
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
            }
            std::printf ("onnx-kit  bpm=%.1f  span=%.1f  state=%s  bar=%s  hits=%d  conf=%.2f\n",
                         static_cast<double> (last.bpm),
                         bpmSamples > 0 ? static_cast<double> (bpmHi - bpmLo) : -1.0,
                         vp::toString (last.state),
                         vp::toBarString (last.followBar),
                         kitEng.shakerHits(),
                         static_cast<double> (last.confidence));
            expect (last.state == vp::TrackingState::following
                        && std::fabs (last.bpm - bpm) < 12.0f,
                    "ONNX tracker locks a 120 BPM drum kit");
            expect (last.followBar != vp::FollowBar::waitBeat
                        && kitEng.shakerHits() > 8,
                    "START leaves ATTENDO BATTUTA and plays");
            expect (bpmSamples > 20 && (bpmHi - bpmLo) < 6.0f,
                    "locked BPM stays stable");

            std::vector<float> quiet (kit.size(), 0.0f);
            float quietPeak = 0.0f;
            for (size_t i = 0; i < kit.size(); ++i)
            {
                quiet[i] = kit[i] * 0.004f;
                quietPeak = std::max (quietPeak, std::abs (quiet[i]));
            }
            std::printf ("quiet-kit  peak=%.5f\n", static_cast<double> (quietPeak));

            vp::VirtualPercussionEngine quietEng;
            quietEng.prepare (sr, block, 1);
            quietEng.settings().followSource.store (static_cast<int> (vp::FollowSource::speaker));
            quietEng.start();
            pos = 0;
            blocks = 0;
            while (pos + block <= n)
            {
                const float* ins[1] = { quiet.data() + pos };
                quietEng.process (ins, 1, outs, 2, block);
                last = quietEng.snapshot();
                pos += block;
                if ((++blocks % 10) == 0)
                    std::this_thread::sleep_for (std::chrono::milliseconds (4));
            }
            for (int i = 0; i < 40; ++i)
            {
                quietEng.process (zIn, 1, outs, 2, block);
                last = quietEng.snapshot();
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
            }
            std::printf ("onnx-quiet-speaker  bpm=%.1f  nn=%.1f  state=%s  conf=%.2f  peak=%.4f  onnx=%d valid=%d\n",
                         static_cast<double> (last.bpm),
                         static_cast<double> (last.neuralBpm),
                         vp::toString (last.state),
                         static_cast<double> (last.confidence),
                         static_cast<double> (last.inputPeak),
                         last.aiOnnx ? 1 : 0,
                         last.hypValid ? 1 : 0);
            expect (last.aiOnnx, "engine uses ONNX BeatNet, not the stub");
            expect (last.state == vp::TrackingState::following
                        && std::fabs (last.bpm - bpm) < 16.0f,
                    "ONNX locks quiet SPEAKER-level 120 kit (iPad/Spotify path)");
        }
    }
#endif
}
