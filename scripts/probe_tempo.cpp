// Throwaway diagnostic for TempoEstimator: octave correctness + lock latency.
#include "AI/TempoEstimator.h"

#include <cmath>
#include <cstdio>
#include <random>

namespace
{
    // BeatNet emits broad activation bumps, not single-frame spikes. Anything
    // tuned against spikes is tuned against a signal the model never produces.
    float bump (double framesFromBeat, float height, float widthFrames)
    {
        const double z = framesFromBeat / widthFrames;
        return height * static_cast<float> (std::exp (-0.5 * z * z));
    }

    // Distance to the nearest multiple of period, in frames.
    double toGrid (double frame, double period)
    {
        const double m = std::fmod (frame, period);
        return std::min (m, period - m);
    }
}

int main()
{
    const double fps = 50.0;
    const float tempos[] = { 55.0f, 68.0f, 75.0f, 90.0f, 100.0f, 120.0f, 132.0f, 140.0f, 168.0f, 190.0f };

    std::printf ("--- beat-only activation, 2%% tolerance ---\n");
    std::printf ("%-8s %-10s %-10s %-10s %-10s\n", "bpm", "t_lock", "bpm@lock", "bpm@15s", "clarity");
    int bad = 0;
    for (float bpm : tempos)
    {
        vp::TempoEstimator te;
        te.prepare (fps);
        const double period = 60.0 / bpm * fps;
        double tLock = -1.0;
        float bpmLock = 0.0f, bpmEnd = 0.0f, clarity = 0.0f;
        for (int i = 0; i < static_cast<int> (fps * 15.0); ++i)
        {
            te.push (std::max (0.03f, bump (toGrid (i, period), 0.95f, 1.6f)));
            const double t = static_cast<double> (i) / fps;
            if (tLock < 0.0 && te.ready() && std::fabs (te.bpm() - bpm) / bpm < 0.02)
            {
                tLock = t;
                bpmLock = te.bpm();
            }
        }
        bpmEnd = te.bpm();
        clarity = te.clarity();
        if (std::fabs (bpmEnd - bpm) / bpm > 0.02) ++bad;
        std::printf ("%-8.1f %-10.2f %-10.2f %-10.2f %-10.2f\n", bpm, tLock, bpmLock, bpmEnd, clarity);
    }

    // Every other event weaker: whether that means "beat plus offbeat" or
    // "kick and snare on consecutive beats" is not decidable from the
    // activation, so only require the true level while the weak events are
    // clearly weaker. Above that the estimator is allowed either level, but it
    // still has to land on one of them rather than somewhere in between.
    std::printf ("\n--- accented beat + offbeat eighths + noise ---\n");
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> noise (0.0f, 0.18f);
    for (float weak : { 0.30f, 0.45f, 0.60f, 0.80f })
    {
        std::printf ("  weak=%.2f of beat: ", weak);
        for (float bpm : { 62.0f, 84.0f, 100.0f, 128.0f, 152.0f, 178.0f })
        {
            vp::TempoEstimator te;
            te.prepare (fps);
            const double period = 60.0 / bpm * fps;
            for (int i = 0; i < static_cast<int> (fps * 15.0); ++i)
            {
                float a = noise (rng);
                a = std::max (a, bump (toGrid (i, period), 0.92f, 1.6f));
                a = std::max (a, bump (toGrid (i + period * 0.5, period), 0.92f * weak, 1.6f));
                te.push (a);
            }
            // Where the level is decidable it has to be right and precise.
            // Where it is not, the score straddles both levels, so allow the
            // wider tolerance the blend causes - the decoder refines tempo from
            // beat times and only needs the level from here.
            const float got = te.bpm();
            const bool onLevel = std::fabs (got - bpm) / bpm < 0.03;
            const bool onOctave = std::fabs (got - 2.0f * bpm) / (2.0f * bpm) < 0.05
                                  || std::fabs (got - 0.5f * bpm) / (0.5f * bpm) < 0.05;
            const bool ok = weak <= 0.5f ? onLevel : (onLevel || onOctave);
            if (! ok) ++bad;
            std::printf ("%.0f->%-6.1f%s ", bpm, got, ok ? "" : "!");
        }
        std::printf ("\n");
    }

    std::printf ("\n--- genuinely fast: every eighth equally accented at 150 (i.e. 300 too fast) ---\n");
    {
        vp::TempoEstimator te;
        te.prepare (fps);
        const double period = 60.0 / 150.0 * fps;
        for (int i = 0; i < static_cast<int> (fps * 15.0); ++i)
            te.push (std::max (0.03f, bump (toGrid (i, period), 0.92f, 1.6f)));
        std::printf ("even pulse 150 -> %.2f (want 150)\n", te.bpm());
    }

    std::printf ("\n--- live accelerando 120 -> 138 ---\n");
    {
        vp::TempoEstimator te;
        te.prepare (fps);
        double phase = 0.0;
        double sinceBeat = 1.0e9;
        for (int i = 0; i < static_cast<int> (fps * 30.0); ++i)
        {
            const double t = static_cast<double> (i) / fps;
            const double bpm = t < 6.0 ? 120.0 : (t < 18.0 ? 120.0 + (t - 6.0) / 12.0 * 18.0 : 138.0);
            phase += bpm / 60.0 / fps;
            if (phase >= 1.0) { phase -= 1.0; sinceBeat = 0.0; }
            te.push (std::max (0.03f, bump (sinceBeat, 0.92f, 1.6f)));
            sinceBeat += 1.0;
            if (std::fabs (t - 6.0) < 0.5 / fps || std::fabs (t - 12.0) < 0.5 / fps
                || std::fabs (t - 18.0) < 0.5 / fps || std::fabs (t - 22.0) < 0.5 / fps)
                std::printf ("  t=%5.1f true=%6.2f est=%6.2f\n", t, bpm, te.bpm());
        }
    }

    {
        vp::TempoEstimator te;
        te.prepare (fps);
        for (int i = 0; i < 1000; ++i)
            te.push (0.0f);
        std::printf ("\nsilence: ready=%d bpm=%.1f\n", te.ready() ? 1 : 0, te.bpm());
    }

    std::printf ("\n%d failures\n", bad);
    return bad == 0 ? 0 : 1;
}
