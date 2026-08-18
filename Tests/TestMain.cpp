#include "Audio/VirtualPercussionEngine.h"
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
        clock.setLatencyCompensationMs (80.0f);
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

    vpRunAiBeatTests (gPassed, gFailed);

    std::printf ("\n%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
