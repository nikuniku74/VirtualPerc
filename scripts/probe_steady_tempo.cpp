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

static const double fps = 50.0;
static const float  kPeak = 0.94f;

struct Exc { double atSec, backSec, worstBpm; };

struct Out { int n; double worstErrPct; double longestSec; double totalOutSec; };

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

    Out o { 0, 0.0, 0.0, 0.0 };
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
        if (! h.valid || now < 12.0) continue;      // let it acquire first
        const double truth = bpmAt (now);
        const double errPct = std::fabs (h.bpm - truth) / truth * 100.0;
        if (! out && errPct > 4.0) { out = true; outStart = now; worstThis = errPct; }
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

int main()
{
    const double dur = 300.0;
    printf ("Tempo COSTANTE (deriva %.0f BPM, jitter %.0f ms = impostazioni --live), %.0f s per corsa,\n", 3.0, 10.0, dur);
    printf ("10 semi diversi per tempo. \"Fuori\" = errore > 4%%, \"rientrato\" = < 2%%.\n\n");
    printf ("%-7s %8s %10s %12s %12s\n", "BPM", "corse", "con uscite", "peggiore", "tempo fuori");
    printf ("%s\n", "------------------------------------------------------------");
    for (float bpm : { 60.f, 75.f, 90.f, 100.f, 110.f, 120.f, 132.f, 140.f, 150.f, 160.f, 170.f }) {
        int runsWithOut = 0, tot = 0; double worst = 0, longest = 0, outSum = 0;
        for (unsigned s = 1; s <= 10; ++s) {
            Out o = run (bpm, dur, 3.0f, 10.0f, s, false);
            ++tot; if (o.n > 0) ++runsWithOut;
            worst = std::max (worst, o.worstErrPct);
            longest = std::max (longest, o.longestSec);
            outSum += o.totalOutSec;
        }
        printf ("%-7.0f %8d %10d %11.1f%% %9.1f s max\n",
                bpm, tot, runsWithOut, worst, longest);
    }
    return 0;
}
