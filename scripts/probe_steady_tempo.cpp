// Steady-tempo stability probe (docs/TODO.md item 18).
//
// Constant tempo with realistic human drift and jitter - no tempo change at all -
// asking whether the decoder ever leaves the tempo and how long it takes to come
// back. Deterministic given the seed: no neural worker, no scheduler, so what it
// measures is the decoder's own behaviour, not bench variability. Build:
//
//   clang++ -std=c++17 -O2 -I Source scripts/probe_steady_tempo.cpp \
//       Source/AI/BeatDecoder.cpp Source/AI/TempoEstimator.cpp \
//       Source/AI/BeatHmm.cpp -o /tmp/probe_steady && /tmp/probe_steady
#include "AI/BeatDecoder.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <random>
#include <algorithm>
#include <initializer_list>
#include <cstdlib>

static const double fps = 50.0;
static const float  kPeak = 0.94f;

struct Exc { double atSec, backSec, worstBpm; };

// Counting "runs with any excursion" cannot tell a bench that got worse from one
// that now corrects itself, since correcting costs a transient and the count
// charges the same for both. So measure the area, not the events: how much of
// the run was spent away from the tempo, and the error integrated over time.
struct Out { int n; double worstErrPct; double longestSec; double totalOutSec;
             double errIntegral; double measuredSec; };

static Out run (float nominal, double duration, float driftBpm, float jitterMs,
                unsigned seed, bool verbose)
{
    vp::BeatDecoder dec; dec.prepare (fps);
    dec.setLevelAnchor (true); dec.setLineFeed (true);

    // Same shape as probe_song_render.h: slow sinusoid around nominal,
    // plus per-beat Gaussian scatter.
    const double driftHalf = 0.5 * (double) driftBpm;
    auto bpmAt = [&] (double sec) {
        return driftHalf <= 0.0 ? (double) nominal
             : (double) nominal + driftHalf * std::sin (2.0 * M_PI * sec / 40.0);
    };
    std::mt19937 rng (seed);
    std::normal_distribution<double> jit (0.0, (double) jitterMs * 0.001);

    std::vector<std::pair<double,float>> beats;
    double t = 0.0;
    while (t < duration + 1.0) {
        const double shift = jitterMs > 0.0f ? jit (rng) : 0.0;
        beats.emplace_back ((t + shift) * fps, kPeak);
        t += 60.0 / bpmAt (t);
    }
    std::sort (beats.begin(), beats.end(),
               [](const auto&a,const auto&b){ return a.first < b.first; });

    Out o { 0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    bool out = false; double outStart = 0.0; double worstThis = 0.0;
    for (int f = 0; f < (int)(duration*fps); ++f) {
        const double now = (double) f / fps;
        float a = 0.03f;
        for (const auto& b : beats) {
            const double d = ((double) f - b.first) / 1.35;
            if (std::fabs (d) < 5.0)
                a = std::max (a, b.second * (float) std::exp (-0.5*d*d));
        }
        const auto h = dec.observe (a, 0.02f, 1.0f - a);
        if (verbose && h.peak && (now < 16.0 || out))
        {
            const auto d = dec.diagnostics();
            printf ("      beat %5.2f  read=%6.2f regime=%d short=%6.2f comb=%6.2f settled=%d gap=%.1f vote=%d\n",
                    now, static_cast<double> (h.bpm), static_cast<int> (dec.regime()),
                    static_cast<double> (d.shortFit), static_cast<double> (d.combBpm),
                    d.levelSettled ? 1 : 0, static_cast<double> (d.fitIndexGap), d.octaveMismatch);
        }
        if (! h.valid || now < 12.0) continue;      // let it acquire first
        const double truth = bpmAt (now);
        const double errPct = std::fabs (h.bpm - truth) / truth * 100.0;
        // Every frame contributes, in and out of an excursion alike.
        o.errIntegral += errPct / fps;
        o.measuredSec += 1.0 / fps;
        if (! out && errPct > 4.0) {
            out = true; outStart = now; worstThis = errPct;
            if (verbose) {
                const auto d = dec.diagnostics();
                printf ("      fuori a %6.1f s true=%5.2f read=%5.2f regime=%d short=%5.2f long=%5.2f comb=%5.2f\n",
                        now, truth, static_cast<double> (h.bpm), static_cast<int> (dec.regime()),
                        static_cast<double> (d.shortFit), static_cast<double> (d.longFit),
                        static_cast<double> (d.combBpm));
            }
        }
        else if (out) {
            worstThis = std::max (worstThis, errPct);
            if (errPct < 2.0) {
                out = false; ++o.n;
                const double dur = now - outStart;
                o.totalOutSec += dur;
                o.longestSec = std::max (o.longestSec, dur);
                o.worstErrPct = std::max (o.worstErrPct, worstThis);
                if (verbose) printf ("      uscita a %6.1f s  per %5.1f s  errore max %.1f%%\n",
                                     outStart, dur, worstThis);
            }
        }
    }
    if (out) { ++o.n; const double dur = duration - outStart;
               o.totalOutSec += dur; o.longestSec = std::max (o.longestSec, dur);
               o.worstErrPct = std::max (o.worstErrPct, worstThis);
               if (verbose) printf ("      uscita a %6.1f s  e NON rientra (%.0f s)  errore max %.1f%%\n",
                                    outStart, dur, worstThis); }
    return o;
}

int main (int argc, char** argv)
{
    const bool focused = argc > 1;
    // Keep focused runs concise by default; pass any fourth argument when the
    // beat-by-beat decoder diagnostics are actually needed.
    const bool verbose = argc > 4;
    const float focusedBpm = focused ? std::strtof (argv[1], nullptr) : 0.0f;
    const double dur = argc > 2 ? std::strtod (argv[2], nullptr) : 300.0;
    const unsigned seeds = argc > 3 ? static_cast<unsigned> (std::max (1, std::atoi (argv[3]))) : 10u;
    printf ("Tempo COSTANTE (deriva %.0f BPM, jitter %.0f ms = impostazioni --live), %.0f s per corsa,\n", 3.0, 10.0, dur);
    printf ("%u semi diversi per tempo. \"Fuori\" = errore > 4%%, \"rientrato\" = < 2%%.\n\n", seeds);
    printf ("%-6s %8s %10s %11s %11s %13s\n",
            "BPM", "corse", "con usc.", "tempo fuori", "err medio", "peggiore");
    printf ("%s\n", "---------------------------------------------------------------------");
    const std::vector<float> tempos = focused
                                      ? std::vector<float> { focusedBpm }
                                      : std::vector<float> { 60.f, 75.f, 90.f, 100.f, 110.f, 120.f,
                                                             132.f, 140.f, 150.f, 160.f, 170.f };
    for (float bpm : tempos) {
        int runsWithOut = 0, tot = 0;
        double worst = 0, outSum = 0, errSum = 0, measSum = 0;
        for (unsigned s = 1; s <= seeds; ++s) {
            Out o = run (bpm, dur, 3.0f, 10.0f, s, verbose);
            ++tot; if (o.n > 0) ++runsWithOut;
            worst = std::max (worst, o.worstErrPct);
            outSum += o.totalOutSec;
            errSum += o.errIntegral;
            measSum += o.measuredSec;
        }
        // Share of measured time spent out, and mean error over all of it.
        const double outPct = measSum > 0.0 ? outSum / measSum * 100.0 : 0.0;
        const double meanErr = measSum > 0.0 ? errSum / measSum : 0.0;
        printf ("%-6.0f %8d %10d %10.1f%% %10.2f%% %12.1f%%\n",
                bpm, tot, runsWithOut, outPct, meanErr, worst);
    }
    return 0;
}
