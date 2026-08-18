// Throwaway: per-second trace of the decoder on a single tempo trajectory.
#include "AI/BeatDecoder.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

int main (int argc, char** argv)
{
    const double fps = 50.0;
    const double bpmA = argc > 1 ? std::atof (argv[1]) : 160.0;
    const double bpmB = argc > 2 ? std::atof (argv[2]) : bpmA;
    const double rampStart = argc > 3 ? std::atof (argv[3]) : 8.0;
    const double rampEnd = argc > 4 ? std::atof (argv[4]) : 23.0;
    const float offbeat = argc > 5 ? static_cast<float> (std::atof (argv[5])) : 0.0f;

    vp::BeatDecoder dec;
    dec.prepare (fps);
    std::mt19937 rng (99);
    double phase = 0.0, sinceBeat = 1.0e9;
    int beatIndex = -1;

    std::printf ("%-6s %-8s %-8s %-9s %-6s\n", "t", "true", "bpm", "regime", "conf");
    for (int i = 0; i < static_cast<int> (fps * 32.0); ++i)
    {
        const double t = static_cast<double> (i) / fps;
        double bpm = bpmA;
        if (t > rampStart)
            bpm = t >= rampEnd ? bpmB : bpmA + (t - rampStart) / (rampEnd - rampStart) * (bpmB - bpmA);

        phase += bpm / 60.0 / fps;
        if (phase >= 1.0) { phase -= 1.0; sinceBeat = 0.0; ++beatIndex; }

        const auto bump = [] (double d, float h) {
            const double z = d / 1.6;
            return h * static_cast<float> (std::exp (-0.5 * z * z));
        };
        float a = offbeat > 0.0f ? std::uniform_real_distribution<float> (0.0f, 0.14f) (rng) : 0.03f;
        a = std::max (a, bump (sinceBeat, 0.92f));
        if (offbeat > 0.0f)
            a = std::max (a, bump (sinceBeat - 60.0 / bpm * fps * 0.5, offbeat));
        const float d = (beatIndex % 4) == 0 ? std::max (0.05f, bump (sinceBeat, 0.70f)) : 0.03f;
        sinceBeat += 1.0;

        const auto h = dec.observe (a, d, 1.0f - a);
        if (i % 25 == 0)
        {
            const char* r = h.regime == vp::TempoRegime::fixed ? "FIXED"
                          : h.regime == vp::TempoRegime::live ? "live" : "unknown";
            std::printf ("%-6.2f %-8.2f %-8.2f %-9s %-6.2f\n", t, bpm, h.bpm, r, h.confidence);
        }
    }
    return 0;
}
