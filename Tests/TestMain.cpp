#include "Audio/VirtualPercussionEngine.h"
#include "Percussion/PercussionEngine.h"
#include "TestAiBeat.h"
#include "Tracking/TempoFollower.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    int gFailed = 0;
    int gPassed = 0;

    void expect (bool cond, const char* name)
    {
        if (cond)
        {
            ++gPassed;
            std::printf ("  PASS  %s\n", name);
        }
        else
        {
            ++gFailed;
            std::printf ("  FAIL  %s\n", name);
        }
    }


    void feedSilence (vp::VirtualPercussionEngine& eng, double sr, int block, float seconds)
    {
        const int n = static_cast<int> (sr * static_cast<double> (seconds));
        std::vector<float> silence (static_cast<size_t> (std::max (block, n)), 0.0f);
        std::vector<float> outL (static_cast<size_t> (block), 0.0f);
        std::vector<float> outR (static_cast<size_t> (block), 0.0f);
        float* outs[2] = { outL.data(), outR.data() };
        int pos = 0;
        while (pos + block <= n)
        {
            const float* ins[1] = { silence.data() + pos };
            eng.process (ins, 1, outs, 2, block);
            pos += block;
        }
    }
}

int main()
{
    constexpr double sr = 48000.0;
    constexpr int block = 128;

    std::printf ("Virtual Percussionist — engine / clock / AI tests\n");

    {
        vp::TempoFollower clock;
        clock.prepare (sr);
        clock.setPulsesPerBeat (4);
        clock.setTargetTempo (120.0f, 1.0f);
        clock.setLocked (true);
        clock.snapPhase (0.0f);

        auto wrap01 = [] (float x)
        {
            x -= std::floor (x);
            return x;
        };
        auto distToZero = [&] (float x)
        {
            x = wrap01 (x);
            return std::min (x, 1.0f - x);
        };

        float worstDownbeat = 0.0f;
        int downbeats = 0;
        for (int i = 0; i < 2000; ++i)
        {
            const float before = clock.beatPhase();
            const auto tick = clock.advance (block);
            const float after = clock.beatPhase();
            float delta = after - before;
            if (delta < -0.5f) delta += 1.0f;
            if (delta >  0.5f) delta -= 1.0f;
            for (int p = 0; p < tick.pulsesFired; ++p)
            {
                if (tick.pulseIndex[p] != 0)
                    continue;
                const float frac = (static_cast<float> (tick.pulseOffset[p]) + 0.5f)
                                   / static_cast<float> (block);
                const float ph = wrap01 (before + delta * frac);
                worstDownbeat = std::max (worstDownbeat, distToZero (ph));
                ++downbeats;
            }
        }
        std::printf ("dot-clock  downbeats=%d  worst=%.3f\n",
                     downbeats, static_cast<double> (worstDownbeat));
        expect (downbeats > 8 && worstDownbeat < 0.05f,
                "shaker downbeats follow the orange-dot clock");
    }

    {
        vp::TempoFollower clock;
        clock.prepare (sr);
        clock.setTargetTempo (80.0f, 1.0f);
        clock.setLocked (true);
        clock.setTempoTrimEnabled (true);
        clock.snapPhase (0.0f);

        constexpr float songBpm = 81.0f;
        double songPhase = 0.0;
        for (int i = 0; i < static_cast<int> (32.0 * sr / block); ++i)
        {
            const double inc = (static_cast<double> (songBpm) / 60.0)
                               * (static_cast<double> (block) / sr);
            const double next = songPhase + inc;
            if (std::floor (next) > std::floor (songPhase))
            {
                const double boundary = std::floor (songPhase) + 1.0;
                const float frac = static_cast<float> ((boundary - songPhase) / inc);
                const float atOnset = vp::wrap01 (clock.beatPhase()
                    + frac * static_cast<float> (block) * clock.currentTempo()
                      / (60.0f * static_cast<float> (sr)));
                clock.observeOnsetPhase (atOnset, 2.0f, 1);
            }
            clock.advance (block);
            songPhase = next;
        }

        std::printf ("pll-fine-tune  tempo=%.3f trim=%.3f\n",
                     static_cast<double> (clock.currentTempo()),
                     static_cast<double> (clock.tempoTrimBpm()));
        expect (std::fabs (clock.currentTempo() - songBpm) < 0.60f
                    && clock.tempoTrimBpm() > 0.40f,
                "quarter-phase drift fine-tunes a manually anchored BPM");
    }

    {
        vp::TempoFollower clock;
        clock.prepare (sr);
        clock.setTargetTempo (120.0f, 1.0f);
        clock.setLocked (true);
        clock.snapPhase (0.001f);
        for (int i = 0; i < 12; ++i)
            clock.observeOnsetPhase (0.20f, 1.0f, 1);

        const float before = clock.barPhase();
        clock.advance (1);
        const float after = clock.barPhase();
        float backwards = before - after;
        if (backwards < 0.0f)
            backwards += 1.0f;
        std::printf ("phase-zero  bar %.4f->%.4f  backwards=%.4f\n",
                     static_cast<double> (before), static_cast<double> (after),
                     static_cast<double> (backwards));
        expect (backwards < 0.02f,
                "PLL correction across zero preserves the quarter counter");
    }

    {
        vp::VirtualPercussionEngine eng;
        eng.prepare (sr, block, 1);
        eng.tapAt (0.0);
        eng.tapAt (0.5);
        eng.tapAt (1.0);
        feedSilence (eng, sr, block, 0.1f);
        const auto afterThree = eng.snapshot();
        expect (afterThree.bpm < 1.0f
                    && afterThree.state != vp::TrackingState::following,
                "the first three TAPs leave automatic listening untouched");

        eng.tapAt (1.5);
        feedSilence (eng, sr, block, 2.0f);
        const auto last = eng.snapshot();
        std::printf ("tap-tempo stopped  bpm=%.1f  state=%s  hits=%d\n",
                     static_cast<double> (last.bpm), vp::toString (last.state),
                     eng.shakerHits());
        expect (std::fabs (last.bpm - 120.f) < 2.5f && eng.shakerHits() == 0,
                "tap sets BPM without START, shaker stays muted");
    }

    {
        vp::VirtualPercussionEngine eng;
        eng.prepare (sr, block, 1);
        eng.start();
        eng.tapAt (0.0);
        eng.tapAt (0.5);
        eng.tapAt (1.0);
        eng.tapAt (1.5);
        feedSilence (eng, sr, block, 4.0f);
        const auto last = eng.snapshot();
        std::printf ("tap-tempo start  bpm=%.1f  state=%s  hits=%d\n",
                     static_cast<double> (last.bpm), vp::toString (last.state),
                     eng.shakerHits());
        expect (std::fabs (last.bpm - 120.f) < 2.5f
                    && last.state == vp::TrackingState::following
                    && eng.shakerHits() > 8,
                "TAP then START holds tap tempo and plays the shaker");
    }

    {
        vp::VirtualPercussionEngine eng;
        eng.prepare (sr, block, 1);
        for (int i = 0; i < 8; ++i)
            eng.tapAt (0.05 * i);
        feedSilence (eng, sr, block, 1.0f);
        expect (eng.shakerHits() == 0 && eng.snapshot().bpm < 1.0f,
                "fast taps do not start the shaker");
    }

    {
        vp::VirtualPercussionEngine eng;
        eng.prepare (sr, block, 1);
        eng.start();
        eng.tapAt (0.0);
        eng.tapAt (0.5);
        eng.tapAt (1.0);
        eng.tapAt (1.5);
        feedSilence (eng, sr, block, 2.0f);
        const int hitsBefore = eng.shakerHits();
        eng.stop();
        feedSilence (eng, sr, block, 1.0f);
        const auto last = eng.snapshot();
        expect (eng.shakerHits() == hitsBefore && ! last.percussionAudible,
                "STOP is immediately silent but keeps the clock");
    }


    // B1 and B2 - grid density per subdivision, and the beat-of-bar label on
    // every pulse - are covered by `perc-grid` and `perc-grid-label` in
    // Tests/TestAiBeat.cpp, which arrived at the same two faults independently.
    // Duplicating them here would only give two places to update.

    // B3 - a stroke taken over by the next one of its kind must ring out over a
    // ramp, not be switched off between two samples, and no stroke may take a
    // slot from a voice that is still sounding. docs/AUDIO_ENGINE.md names
    // these two as the ones left uncovered.
    //
    // Counting voices is not enough on its own: the shaker alternates a down and
    // an up stroke, and those two overlap perfectly legitimately. What separates
    // a release from a cut is whether any voice is ever *inside* its ramp.
    {
        constexpr float bpm = 200.0f;
        constexpr int pulses = 4;

        vp::TempoFollower clock;
        clock.prepare (sr);
        clock.setPulsesPerBeat (pulses);
        clock.forceTempo (bpm);
        clock.setTargetTempo (bpm, 1.0f);
        clock.setLocked (true);
        clock.snapDownbeat (0.0f);

        vp::PercussionEngine perc;
        perc.prepare (sr);
        perc.setGroove (bpm, pulses);
        perc.setShakerSubdivision (vp::Subdivision::sixteenth);
        perc.setReverbAmount (0.0f);
        perc.setVolume (1.0f);

        std::vector<float> l (static_cast<size_t> (block), 0.0f);
        std::vector<float> r (static_cast<size_t> (block), 0.0f);

        int releasingBlocks = 0;
        int maxVoices = 0;
        const int total = static_cast<int> (sr * 30.0);
        for (int pos = 0; pos + block <= total; pos += block)
        {
            const auto tick = clock.advance (block);
            perc.render (l.data(), r.data(), block, tick, true);
            maxVoices = std::max (maxVoices, perc.activeVoices());
            if (perc.releasingVoices() > 0)
                ++releasingBlocks;
        }

        std::printf ("voice-steal  maxVoices=%d  releasingBlocks=%d  hardSteals=%d\n",
                     maxVoices, releasingBlocks, perc.hardSteals());
        expect (releasingBlocks > 0 && perc.hardSteals() == 0,
                "a retriggered stroke rings out over a ramp, and no voice is stolen mid-note");
    }

    // B4 - the speaker-leak subtraction has to cover the whole block. It used to
    // stop at 2048 samples, so on a larger buffer the tail of every block went
    // through untouched and the splice between the two halves put a step into
    // the analysis signal once per callback.
    //
    // The mic hears the app's own output delayed by the round trip, so the
    // reference has to be older than the block is long - otherwise the tail of
    // the block would need output that has not been rendered yet. 150 ms covers
    // every buffer size used here.
    {
        auto leakThrough = [&] (int blk)
        {
            vp::VirtualPercussionEngine eng;
            eng.prepare (sr, blk, 1);
            eng.settings().followSource.store (static_cast<int> (vp::FollowSource::speaker));
            eng.settings().masterVolume.store (1.0f);
            eng.settings().reverbAmount.store (0.0f);
            eng.setReportedLatencyMs (150.0f);

            const int delay = static_cast<int> (0.150 * sr);
            std::vector<float> history (static_cast<size_t> (delay + blk * 4), 0.0f);
            int histWrite = 0;

            std::vector<float> in (static_cast<size_t> (blk), 0.0f);
            std::vector<float> oL (static_cast<size_t> (blk), 0.0f);
            std::vector<float> oR (static_cast<size_t> (blk), 0.0f);
            float* outs[2] = { oL.data(), oR.data() };

            eng.start();
            eng.tapAt (0.0);
            eng.tapAt (0.5);
            eng.tapAt (1.0);
            eng.tapAt (1.5);

            double sum = 0.0;
            int counted = 0;
            const int blocks = static_cast<int> (sr * 8.0) / blk;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < blk; ++i)
                {
                    const int ri = (histWrite - delay + i + static_cast<int> (history.size()))
                                   % static_cast<int> (history.size());
                    in[static_cast<size_t> (i)] = history[static_cast<size_t> (ri)] * 0.6f;
                }
                const float* ins[1] = { in.data() };
                eng.process (ins, 1, outs, 2, blk);
                for (int i = 0; i < blk; ++i)
                {
                    history[static_cast<size_t> (histWrite)] =
                        0.5f * (oL[static_cast<size_t> (i)] + oR[static_cast<size_t> (i)]);
                    histWrite = (histWrite + 1) % static_cast<int> (history.size());
                }
                const auto snap = eng.snapshot();
                // Loud blocks only: below 0.25 the analysis make-up gain kicks in
                // and the ratio would stop describing the subtraction.
                if (b > blocks / 3 && snap.inputPeak > 0.25f)
                {
                    sum += static_cast<double> (snap.analysisPeak / snap.inputPeak);
                    ++counted;
                }
            }
            return counted > 0 ? static_cast<float> (sum / counted) : 1.0f;
        };

        const float small = leakThrough (1024);
        const float large = leakThrough (4096);
        std::printf ("leak-residual  through@1024=%.3f  through@4096=%.3f\n", small, large);
        // Fixed, the big block cancels slightly better than the small one
        // (0.57 vs 0.62 here); truncated, the untouched tail pushes it to 0.79.
        expect (large <= small * 1.10f,
                "speaker-leak subtraction covers a block larger than the old 2048 cap");
    }

    vpRunAiBeatTests (gPassed, gFailed);

    std::printf ("\n%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
