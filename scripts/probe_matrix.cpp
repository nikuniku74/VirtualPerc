// The scoreboard: how fast the tempo is found, and how long it stays found,
// across the two things that actually vary in the listener's material - the
// tempo, and whether it swings.
//
// `probe_steady_tempo` measures stability on one impulse per beat. Real music
// at a slow tempo usually swings, and a swung off-eighth offers a competing
// metrical level at 1.5x that is *inside* the reportable range below about
// 110 BPM and outside it above (240 > kMaxBpm at 120). That is why the same
// track is fine fast and not fine slow, and it is why this bench generates the
// off-eighth as well.
//
// Two numbers per cell, because they fail independently: time to first get
// within 2% of the truth and stay there for three seconds, and how much of a
// four-minute run is spent more than 4% away afterwards.
//
// Decoder only - no neural worker, no scheduler - so it is deterministic given
// the seed and a change can be A/B'd against `git show HEAD:` in one command.
//
//   clang++ -std=c++17 -O2 -Wno-unused-function -I Source scripts/probe_matrix.cpp \
//       Source/AI/BeatDecoder.cpp Source/AI/TempoEstimator.cpp \
//       Source/AI/BeatHmm.cpp -o /tmp/probe_matrix && /tmp/probe_matrix
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
#include <string>
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

static float gSwing = -1.0f;      // <0 straight, else swing amount on the subdivision
static int   gSubdiv = 1;         // 1 = quarters only, 2 = eighths, 4 = sixteenths
static float gSubdivAmp = 0.45f;  // how loud the subdivision is beside the quarter
static float gQuarterAmp[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static float gMissChance = 0.0f;  // beats the mix simply swallows
static int   gDropoutEvery = 0;   // every Nth bar the band stops
static bool  gLockOnly = false;
static bool  gRatioOnly = false;
static double gRatioSum = 0.0; static int gRatioN = 0;
static double gLockAt = -1.0, gLockDone = -1.0;
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

    // One bar of material, described as what a kit actually puts where.
    //
    // The axes are the ones that change between records, not between takes of
    // one: how densely the subdivision is filled, whether that subdivision
    // swings, where the accents sit, and how much of the bar is silent. Tuning
    // on one song is how a tracker gets good at one song; this is the spread a
    // change has to survive before it can be called an improvement.
    std::uniform_real_distribution<double> uni (0.0, 1.0);
    std::vector<std::pair<double,float>> beats;
    double t = 0.0;
    int bar = 0;
    while (t < duration + 1.0) {
        const double period = 60.0 / bpmAt (t);
        for (int q = 0; q < 4; ++q) {
            const double qt = t + q * period;
            if (qt > duration + 1.0) break;
            // A bar the band drops out of. Real arrangements do this and the
            // tracker has to keep time through it rather than re-acquire.
            const bool silentBar = gDropoutEvery > 0 && (bar % gDropoutEvery) == (gDropoutEvery - 1);
            if (! silentBar) {
                // The quarter itself, accented per the style's pattern.
                float amp = gQuarterAmp[q];
                if (amp > 0.0f && uni (rng) >= gMissChance) {
                    const double sh = jitterMs > 0.0f ? jit (rng) : 0.0;
                    beats.emplace_back ((qt + sh) * fps, kPeak * amp);
                }
                // The subdivision between the quarters: none, eighths, or
                // sixteenths, straight or swung.
                if (gSubdiv >= 2) {
                    const int n = gSubdiv;   // 2 = eighths, 4 = sixteenths
                    for (int k = 1; k < n; ++k) {
                        double frac = (double) k / n;
                        if (gSwing >= 0.0f) {
                            // Swing warps the span the subdivision lives in:
                            // the eighth for sixteenths, the beat for eighths.
                            const double d = (double) gSwing / 6.0;
                            const double span = (n == 4) ? 0.5 : 1.0;
                            const double x = frac / span - std::floor (frac / span);
                            const double half = std::floor (frac / span) * span;
                            const double w = x < 0.5 ? x * (0.5 + d) / 0.5
                                                     : (0.5 + d) + (x - 0.5) * (0.5 - d) / 0.5;
                            frac = half + span * w;
                        }
                        if (uni (rng) < gMissChance) continue;
                        const double sh = jitterMs > 0.0f ? jit (rng) : 0.0;
                        beats.emplace_back ((qt + frac * period + sh) * fps,
                                            kPeak * gSubdivAmp);
                    }
                }
            }
        }
        t += 4.0 * period;
        ++bar;
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
        if (verbose && h.peak && now < 16.0)
        {
            const auto d = dec.diagnostics();
            printf ("      beat %5.2f  read=%6.2f regime=%d short=%6.2f comb=%6.2f settled=%d gap=%.1f vote=%d\n",
                    now, static_cast<double> (h.bpm), static_cast<int> (dec.regime()),
                    static_cast<double> (d.shortFit), static_cast<double> (d.combBpm),
                    d.levelSettled ? 1 : 0, static_cast<double> (d.fitIndexGap), d.octaveMismatch);
        }
        if (! h.valid) continue;
        if (gRatioOnly) {
            if (now > 25.0) { const double tr = bpmAt (now);
                gRatioSum += h.bpm / tr; ++gRatioN; }
            continue;
        }
        if (gLockOnly) {
            const double tr0 = bpmAt (now);
            if (std::fabs (h.bpm - tr0) / tr0 < 0.02) { if (gLockAt < 0.0) gLockAt = now; }
            else gLockAt = -1.0;
            if (gLockAt >= 0.0 && now - gLockAt > 3.0 && gLockDone < 0.0) gLockDone = gLockAt;
            continue;
        }
        if (now < 12.0) continue;      // let it acquire first
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

// The material a tracker meets. Each row is a shape records actually have,
// not a variation on a metronome - and the point of the list is that a change
// has to hold on all of it, not on the one somebody happened to be listening
// to.
struct Style
{
    const char* name;
    int   subdiv;        // 1 quarters, 2 eighths, 4 sixteenths
    float subdivAmp;
    float swing;         // <0 straight
    float q[4];          // accent per quarter, 0 = silent
    float jitterMs;
    float missChance;
    int   dropoutEvery;  // 0 = never
};

static const Style kStyles[] = {
    // name              sub  amp  swing   accents               jit  miss drop
    { "metronomo",         1, 0.0f, -1.0f, {1.0f,1.0f,1.0f,1.0f}, 10.f, 0.00f, 0 },
    { "rock 8vi",          2, 0.45f,-1.0f, {1.0f,0.8f,0.9f,0.8f}, 10.f, 0.00f, 0 },
    { "rock 16mi",         4, 0.35f,-1.0f, {1.0f,0.8f,0.9f,0.8f}, 10.f, 0.00f, 0 },
    { "backbeat secco",    1, 0.0f, -1.0f, {1.0f,0.9f,0.6f,0.9f}, 12.f, 0.00f, 0 },
    { "half-time",         2, 0.40f,-1.0f, {1.0f,0.0f,0.9f,0.0f}, 12.f, 0.00f, 0 },
    { "swing 8vi",         2, 0.50f, 0.65f,{1.0f,0.8f,0.9f,0.8f}, 12.f, 0.00f, 0 },
    { "shuffle 16mi",      4, 0.40f, 0.65f,{1.0f,0.8f,0.9f,0.8f}, 12.f, 0.00f, 0 },
    { "swing pieno",       2, 0.55f, 1.00f,{1.0f,0.8f,0.9f,0.8f}, 12.f, 0.00f, 0 },
    { "band larga",        2, 0.45f,-1.0f, {1.0f,0.8f,0.9f,0.8f}, 25.f, 0.00f, 0 },
    { "mix che ingoia",    2, 0.45f,-1.0f, {1.0f,0.8f,0.9f,0.8f}, 12.f, 0.18f, 0 },
    { "con vuoti",         2, 0.45f,-1.0f, {1.0f,0.8f,0.9f,0.8f}, 12.f, 0.00f, 4 },
    { "solo accordi",      1, 0.0f, -1.0f, {0.6f,0.5f,0.55f,0.5f},22.f, 0.10f, 0 },
};

static void applyStyle (const Style& st)
{
    gSubdiv = st.subdiv; gSubdivAmp = st.subdivAmp; gSwing = st.swing;
    for (int i = 0; i < 4; ++i) gQuarterAmp[i] = st.q[i];
    gMissChance = st.missChance; gDropoutEvery = st.dropoutEvery;
}

static void ratioTable();

int main (int argc, char** argv)
{
    if (argc > 1 && std::string (argv[1]) == "--ratio") { ratioTable(); return 0; }
    const bool steady = argc > 1 && std::string (argv[1]) == "--steady";
    const bool quick = steady || (argc > 1 && std::string (argv[1]) == "--quick");
    const std::vector<float> tempos = quick ? std::vector<float>{52.f,120.f,168.f}
                                           : std::vector<float>{60.f,81.f,100.f,128.f,165.f};
    const int seeds = quick ? 2 : 6;
    printf ("Banco materiali: %d stili x %d tempi x %d semi.\n",
            (int) (sizeof kStyles / sizeof kStyles[0]),
            (int) tempos.size(), seeds);
    printf ("aggancio = entro il 2%% e ci resta 3 s;  uscite = corse con errore > 4%% in %d secondi\n\n",quick?60:240);
    printf ("Deriva musicale: %.0f BPM\n",steady?0.0:3.0);
    printf ("%-16s %8s %8s %9s %9s %10s\n", "materiale", "agg.med", "agg.peg", "uscite", "%fuori", "err.medio%");
    printf ("------------------------------------------------------------\n");
    double gLock = 0; int gLockN = 0, gExc = 0, gNever = 0; double gOut = 0; int cells = 0;
    for (const auto& st : kStyles) {
        double lockSum = 0, lockWorst = 0, outSum = 0, meas = 0, errorSum = 0;
        int lockN = 0, never = 0, exc = 0, runs = 0;
        for (float bpm : tempos) {
            for (unsigned seed = 1; (int) seed <= seeds; ++seed) {
                applyStyle (st);
                gLockOnly = true; gLockAt = -1.0; gLockDone = -1.0;
                run (bpm, 60.0, steady?0.0f:3.0f, st.jitterMs, seed, false);
                gLockOnly = false;
                if (gLockDone >= 0.0) { lockSum += gLockDone; ++lockN;
                                        lockWorst = std::max (lockWorst, gLockDone); }
                else ++never;
                applyStyle (st);
                Out o = run (bpm, quick ? 60.0 : 240.0, steady?0.0f:3.0f, st.jitterMs, seed, false);
                if (o.n > 0) ++exc;
                outSum += o.totalOutSec; meas += o.measuredSec; errorSum += o.errIntegral;
                ++runs;
            }
        }
        const double om = meas > 0 ? outSum / meas * 100.0 : 0.0;
        printf ("%-16s %8.2f %8.2f %6d/%-3d %8.2f%% %10.4f%s\n", st.name,
                lockN ? lockSum / lockN : -1.0, lockWorst, exc, runs, om,
                meas > 0 ? errorSum/meas : 0,
                never ? "   (qualche corsa non aggancia)" : "");
        if (lockN) { gLock += lockSum / lockN; ++gLockN; }
        gExc += exc; gNever += never; gOut += om; ++cells;
    }
    printf ("\nTOTALE  aggancio medio %.2f s   uscite %d   mai-agganciato %d   fuori medio %.2f%%\n",
            gLockN ? gLock / gLockN : -1.0, gExc, gNever, gOut / cells);
    return 0;
}

static void ratioTable()
{
    const float tempos[] = { 60.f, 81.f, 100.f, 128.f, 165.f };
    printf ("Rapporto medio riportato/vero. 1.00 giusto, 2.00 ottava sopra, 0.50 sotto.\n\n");
    printf ("%-16s", "materiale");
    for (float b : tempos) printf ("%9.0f", b);
    printf ("\n------------------------------------------------------------------\n");
    for (const auto& st : kStyles) {
        printf ("%-16s", st.name);
        for (float bpm : tempos) {
            double acc = 0; int n = 0;
            for (unsigned seed = 1; seed <= 6; ++seed) {
                applyStyle (st);
                gRatioOnly = true; gRatioSum = 0; gRatioN = 0;
                run (bpm, 120.0, 3.0f, st.jitterMs, seed, false);
                gRatioOnly = false;
                if (gRatioN > 0) { acc += gRatioSum / gRatioN; ++n; }
            }
            printf ("%9.2f", n ? acc / n : -1.0);
        }
        printf ("\n");
    }
}
