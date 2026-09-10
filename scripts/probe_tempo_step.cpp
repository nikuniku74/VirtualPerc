// Tempo-jump re-acquisition probe (docs/TODO.md item 19).
//
// Not wired into CMake on purpose - it links three translation units and needs
// no JUCE, so it builds in a couple of seconds on its own:
//
//   clang++ -std=c++17 -O2 -I Source scripts/probe_tempo_step.cpp \
//       Source/AI/BeatDecoder.cpp Source/AI/TempoEstimator.cpp \
//       Source/AI/BeatHmm.cpp -o /tmp/probe_tempo_step && /tmp/probe_tempo_step
//
// Same harness shape as runDecoderStep in Tests/TestAiBeat.cpp - one Gaussian
// bump per beat, spacing changing at 18 s - but it sweeps the size of the jump
// instead of only measuring 120->132, and it runs each jump twice: once plain,
// once with notifyInputRestart() at the change, which is what moving the mic
// gain ends up doing through the analysis epoch. The second column is the
// listener's workaround, measured.
#include "AI/BeatDecoder.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <initializer_list>

static const double fps = 50.0;
static const double changeAt = 18.0;
static const double duration = 150.0;  // long enough to see the stale-grid watchdog fire
static const float  kStrongPeak = 0.94f;

struct Res
{
    double lockSec;
    float finalBpm;
    bool everLocked;
    int confirmedIntervals;
    int transitions;
};

static Res run (float fromBpm, float toBpm, bool lineFeed, bool restartAtChange)
{
    vp::BeatDecoder dec;
    dec.prepare (fps);
    dec.setLevelAnchor (true);
    dec.setLineFeed (lineFeed);

    std::vector<std::pair<double,float>> beats;
    double t = 0.0;
    while (t < duration + 1.0)
    {
        const bool changed = t >= changeAt;
        beats.emplace_back (t * fps, kStrongPeak);
        t += 60.0 / (double) (changed ? toBpm : fromBpm);
    }
    std::sort (beats.begin(), beats.end(),
               [](const auto&a, const auto&b){ return a.first < b.first; });

    Res r { -1.0, 0.0f, false, 0, 0 };
    bool restarted = false;
    double heldSince = -1.0;
    uint32_t lastTransitionSerial = 0;
    for (int frame = 0; frame < (int)(duration * fps); ++frame)
    {
        const double now = (double) frame / fps;
        float activation = 0.03f;
        for (const auto& b : beats)
        {
            const double d = ((double) frame - b.first) / 1.35;
            if (std::fabs (d) < 5.0)
                activation = std::max (activation,
                    b.second * (float) std::exp (-0.5 * d * d));
        }
        if (restartAtChange && ! restarted && now >= changeAt)
        {
            dec.notifyInputRestart();   // what a mic-gain step ends up doing
            restarted = true;
        }
        const vp::BeatHypothesis h = dec.observe (activation, 0.02f, 1.0f - activation);
        if (h.valid)
        {
            if (h.transitionSerial != lastTransitionSerial)
            {
                lastTransitionSerial = h.transitionSerial;
                ++r.transitions;
                r.confirmedIntervals = h.transitionIntervals;
            }
            r.finalBpm = h.bpm;
            const bool onTarget = std::fabs (h.bpm - toBpm) / toBpm < 0.02f;
            if (now > changeAt)
            {
                if (onTarget) { if (heldSince < 0.0) heldSince = now; }
                else heldSince = -1.0;
                // "found it" = on target and stayed there 3 s
                if (! r.everLocked && heldSince > 0.0 && now - heldSince > 3.0)
                { r.everLocked = true; r.lockSec = heldSince - changeAt; }
            }
        }
    }
    return r;
}

int main()
{
    int failures = 0;
    printf ("%-16s %-6s | %-22s | %-22s\n", "salto", "delta", "normale", "con input-restart");
    printf ("%s\n", "-------------------------------------------------------------------------------");
    struct P { float a, b; };
    for (P p : { P{120,132}, P{120,150}, P{120,160}, P{100,160}, P{160,100},
                 P{120,90}, P{90,120}, P{120,60}, P{60,120}, P{140,75}, P{75,140} })
    {
        const float delta = (p.b - p.a) / p.a * 100.0f;
        for (bool line : { true })
        {
            Res n = run (p.a, p.b, line, false);
            Res e = run (p.a, p.b, line, true);
            auto fmt = [] (const Res& r, char* buf) {
                if (r.everLocked) std::snprintf (buf, 64, "%5.1f s  (bpm %.1f)", r.lockSec, r.finalBpm);
                else              std::snprintf (buf, 64, "MAI     (bpm %.1f)", r.finalBpm);
            };
            char b1[64], b2[64];
            fmt (n, b1); fmt (e, b2);
            char lab[32]; std::snprintf (lab, 32, "%.0f -> %.0f", p.a, p.b);
            printf ("%-16s %+5.0f%% | %-22s | %-22s\n", lab, delta, b1, b2);

            const float octaveDistance = std::fabs (
                std::fabs (std::log2 (p.b / p.a))
                - std::round (std::fabs (std::log2 (p.b / p.a))));
            const bool wideNonOctave = std::fabs (delta) >= 25.0f
                                       && std::fabs (delta) <= 65.0f
                                       && octaveDistance >= 0.15f;
            if (wideNonOctave
                && (! n.everLocked || n.lockSec > 2.5
                    || std::fabs (n.finalBpm - p.b) > 1.0f
                    || n.transitions != 1 || n.confirmedIntervals != 3))
                ++failures;
        }
    }
    printf ("\nviolent non-octave changes: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
