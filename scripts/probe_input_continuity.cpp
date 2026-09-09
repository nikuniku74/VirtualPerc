// Decoder-side contract for an arrangement entrance versus a new source.
// The real network/engine result is measured separately with VPTrack.
// Build with BeatDecoder.cpp, TempoEstimator.cpp and BeatHmm.cpp, -ISource.
#include "AI/BeatDecoder.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>

int main()
{
    int failures = 0;
    for (float bpm : { 52.0f, 87.0f, 120.0f, 168.0f })
    {
        vp::BeatDecoder d;
        d.prepare (50.0);
        d.setLineFeed (true);
        d.setLevelAnchor (true);
        for (int frame = 0; frame < 1500; ++frame)
        {
            const double beat = frame * 0.02 * bpm / 60.0;
            const double distance = (beat - std::round (beat)) * 60.0 / bpm;
            const float pulse = 0.94f * std::exp (-0.5 * distance * distance / (0.027 * 0.027));
            d.observe (std::max (0.03f, pulse), 0.0f, 0.03f);
        }
        const auto before = d.diagnostics();
        d.notifyInputRestart (true);
        const auto retained = d.diagnostics();
        const bool kept = before.levelSettled && before.combSalience > 0.14f
                       && retained.combBpm == before.combBpm
                       && retained.combSalience == before.combSalience
                       && retained.levelSettled
                       && retained.coverage == 0.0f;
        // A subsequent new source must still clear retained evidence. The
        // default call is the existing API used by all ordinary restart tests.
        d.notifyInputRestart();
        const auto cleared = d.diagnostics();
        const bool reset = ! cleared.levelSettled && cleared.combSalience == 0.0f;
        std::printf ("%.0f BPM continuity=%s subsequent-source-reset=%s\n",
                     bpm, kept ? "PASS" : "FAIL", reset ? "PASS" : "FAIL");
        failures += ! kept;
        failures += ! reset;
    }
    return failures ? 1 : 0;
}
