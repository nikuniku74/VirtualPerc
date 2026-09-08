// What the grid rate does on live material, and how long it takes to come back.
//
// The listener's report: "ogni tanto le percussioni tendono a rallentare o a
// velocizzare e poi ci impiegano molto a rientrare nel tempo", on a recording of
// a live band.
//
// The `phase-steer` bench in TestMain already measures the grid against decoder
// phase *noise* - plus/minus 0.03 of a beat, held at the hypothesis rate - and
// that is a band standing still. Live material is two more things: the tempo
// genuinely drifts, and every so often a hypothesis is simply wrong (a fill, a
// crash, a bar where the kit is ambiguous). The second is what opens the
// steering ceiling, and the ceiling is where the audible excursions live.
//
// Metrics are the ones the reverted watchdog attempt said were missing: not
// "did any excursion happen", but how far, for how long in total, and how long
// after a bad hypothesis before the grid is back.
//
// What it answered, 2026-09-07: **the steering is not the cause of the slow
// recovery the listener reported.** It comes back inside 0.06-0.28 s from a
// two-second wrong phase at every strength, and HIGH is the fastest of the
// three. The slow part is upstream, in the decoder - see docs/TODO.md item 21.
//
// What it did find, and what is left open there: HIGH, which is the shipped
// default, makes excursions two to three times larger than LOW (6.7% against
// 2.6% worst at 144 BPM, and 0.55 against 0.06 seconds per minute outside 2%)
// for no difference in rms tracking on this bench. That is a trade nobody has
// heard yet, so nothing was changed.
//
// Decoder-free and deterministic given the seed: this is the clock alone.
//
//   VP_STYLE_SRC=scripts/probe_steer.cpp VP_PROBE_DIR=scripts \
//     cmake -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release && \
//     cmake --build build-host --target VPStyle && \
//     ./build-host/VPStyle_artefacts/Release/VPStyle
#include "Tracking/TempoFollower.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <utility>

namespace
{
struct Result
{
    double rms = 0.0;        // BPM
    double worst = 0.0;      // BPM
    double worstPct = 0.0;
    double secondsOut = 0.0; // total time |dev| > 2% of tempo
    double integrated = 0.0; // BPM * s
    double recoverMean = 0.0;// s, after an injected bad hypothesis
    double recoverWorst = 0.0;
    int    outliers = 0;
};

// A live band: the tempo itself moves, the analysis's phase is noisy, and now
// and then it is simply wrong for one hypothesis.
Result run (float baseBpm, vp::FollowStrength follow, bool withOutliers,
            unsigned seed, double seconds = 60.0)
{
    const double sr = 48000.0;
    const int blk = 256;
    vp::TempoFollower clock;
    clock.prepare (sr);
    clock.setPulsesPerBeat (4);
    clock.forceTempo (baseBpm);
    clock.setTargetTempo (baseBpm, 1.0f);
    clock.setFollowStrength (follow);
    clock.setLocked (true);
    clock.resetClock();

    const double dt = blk / sr;
    double songPhase = 0.0, t = 0.0, nextRefresh = 0.0;
    float held = 0.0f;
    unsigned s = seed | 1u;
    auto rnd = [&s] { s = s * 1103515245u + 12345u; return static_cast<float> ((s >> 16) & 0x7fff) / 16383.5f - 1.0f; };

    Result r;
    double sq = 0.0; int n = 0;
    // When a bad hypothesis was injected, and whether we are still recovering.
    double outlierAt = -1.0;
    std::vector<double> recoveries;
    double nextOutlier = withOutliers ? 7.0 : 1e9;

    for (int i = 0; i < static_cast<int> (sr * seconds) / blk; ++i)
    {
        // A live drummer: a slow wander of a couple of BPM, not a click.
        const double trueBpm = baseBpm * (1.0 + 0.018 * std::sin (t * 0.21)
                                              + 0.010 * std::sin (t * 0.07 + 1.3));
        const double before = clock.beatsElapsed() + clock.beatPhase();
        const float songFrac = static_cast<float> (songPhase - std::floor (songPhase));

        if (t >= nextRefresh)
        {
            held = 0.04f * rnd();
            if (t >= nextOutlier)
            {
                // One hypothesis that is simply wrong, of the size that opens
                // the steering ceiling. This is the event under test.
                held = (rnd() >= 0.0f ? 0.22f : -0.22f);
                outlierAt = t;
                nextOutlier = t + 7.0;
                ++r.outliers;
            }
            nextRefresh += 1.0 / 6.0;
        }
        const float seen = vp::wrap01 (songFrac + held);

        const double songBefore = songPhase;
        songPhase += trueBpm / 60.0 * dt;
        if (std::floor (songPhase) > std::floor (songBefore))
            clock.observeOnsetPhase (vp::wrap01 (clock.beatPhase() - seen), 0.6f, 1);

        clock.setGridPhase (seen, 0.90f);
        clock.advance (blk);
        t += dt;

        const double after = clock.beatsElapsed() + clock.beatPhase();
        const double rate = (after - before) / dt * 60.0;
        const double dev = rate - trueBpm;

        if (t > 3.0)
        {
            sq += dev * dev; ++n;
            r.worst = std::max (r.worst, std::fabs (dev));
            r.integrated += std::fabs (dev) * dt;
            if (std::fabs (dev) > 0.02 * trueBpm)
                r.secondsOut += dt;
        }
        // Recovery: from the bad hypothesis until the grid is inside 1% again.
        if (outlierAt >= 0.0 && t > outlierAt + 0.05 && std::fabs (dev) < 0.01 * trueBpm)
        {
            recoveries.push_back (t - outlierAt);
            outlierAt = -1.0;
        }
    }
    r.rms = n > 0 ? std::sqrt (sq / n) : 0.0;
    r.worstPct = 100.0 * r.worst / baseBpm;
    if (! recoveries.empty())
    {
        double sum = 0.0;
        for (double v : recoveries) { sum += v; r.recoverWorst = std::max (r.recoverWorst, v); }
        r.recoverMean = sum / static_cast<double> (recoveries.size());
    }
    return r;
}

void report (const char* label, float bpm, vp::FollowStrength f, bool outliers)
{
    Result acc {};
    const int seeds = 8;
    for (int k = 0; k < seeds; ++k)
    {
        const Result r = run (bpm, f, outliers, 1234u + 7919u * static_cast<unsigned> (k));
        acc.rms += r.rms / seeds;
        acc.worst = std::max (acc.worst, r.worst);
        acc.secondsOut += r.secondsOut / seeds;
        acc.integrated += r.integrated / seeds;
        acc.recoverMean += r.recoverMean / seeds;
        acc.recoverWorst = std::max (acc.recoverWorst, r.recoverWorst);
    }
    std::printf ("%-8s %3.0f BPM  rms=%5.2f  peggio=%6.2f BPM (%4.1f%%)  fuori=%5.2f s/min"
                 "  integrale=%6.1f  rientro medio=%5.2f s  peggiore=%5.2f s\n",
                 label, static_cast<double> (bpm), acc.rms, acc.worst,
                 100.0 * acc.worst / bpm, acc.secondsOut, acc.integrated,
                 acc.recoverMean, acc.recoverWorst);
}
} // namespace

// A fill: the phase is wrong for two seconds, not for one hypothesis.
double fillRecovery (float bpm, vp::FollowStrength follow, unsigned seed)
{
    const double sr = 48000.0;
    const int blk = 256;
    vp::TempoFollower clock;
    clock.prepare (sr);
    clock.setPulsesPerBeat (4);
    clock.forceTempo (bpm);
    clock.setTargetTempo (bpm, 1.0f);
    clock.setFollowStrength (follow);
    clock.setLocked (true);
    clock.resetClock();
    const double dt = blk / sr;
    double songPhase = 0.0, t = 0.0, nextRefresh = 0.0;
    float held = 0.0f;
    unsigned s = seed | 1u;
    auto rnd = [&s] { s = s * 1103515245u + 12345u; return static_cast<float> ((s >> 16) & 0x7fff) / 16383.5f - 1.0f; };
    const double fillFrom = 10.0, fillTo = 12.0;
    double back = -1.0;
    for (int i = 0; i < static_cast<int> (sr * 40.0) / blk; ++i)
    {
        const double before = clock.beatsElapsed() + clock.beatPhase();
        const float songFrac = static_cast<float> (songPhase - std::floor (songPhase));
        if (t >= nextRefresh)
        {
            held = 0.04f * rnd();
            if (t >= fillFrom && t < fillTo) held = 0.25f;
            nextRefresh += 1.0 / 6.0;
        }
        const float seen = vp::wrap01 (songFrac + held);
        const double songBefore = songPhase;
        songPhase += bpm / 60.0 * dt;
        if (std::floor (songPhase) > std::floor (songBefore))
            clock.observeOnsetPhase (vp::wrap01 (clock.beatPhase() - seen), 0.6f, 1);
        clock.setGridPhase (seen, 0.90f);
        clock.advance (blk);
        t += dt;
        const double after = clock.beatsElapsed() + clock.beatPhase();
        const double rate = (after - before) / dt * 60.0;
        if (t > fillTo && back < 0.0 && std::fabs (rate - bpm) < 0.01 * bpm)
            back = t - fillTo;
    }
    return back;
}

struct ReentryResult
{
    double recoveredSec = -1.0;
    double shortestPulse = 10.0;
    double longestPulse = 0.0;
    double errorAtReturnMs = 0.0;
    double bestErrorMs = 1000.0;
};

// A small but audible displacement held while the fit is poor, followed by
// clean evidence returning. This is the edge produced by a fill or by changing
// which percussion dominates the input, and was missing from the older fill
// probe above (whose trust stays at 1 throughout).
ReentryResult cleanEvidenceReturns (float bpm)
{
    constexpr double sr = 48000.0;
    constexpr int blk = 256;
    vp::TempoFollower clock;
    clock.prepare (sr);
    clock.setPulsesPerBeat (4);
    clock.forceTempo (bpm);
    clock.setTargetTempo (bpm, 1.0f);
    clock.setFollowStrength (vp::FollowStrength::high);
    clock.setLocked (true);
    clock.resetClock();

    const double dt = blk / sr;
    const double pulseSec = 60.0 / bpm / 4.0;
    double song = 0.0, t = 0.0, lastPulse = -1.0;
    ReentryResult result;

    for (int i = 0; i < static_cast<int> (sr * 4.0) / blk; ++i)
    {
        if (t >= 1.0 && t < 1.0 + dt)
            clock.snapPhase (vp::wrap01 (static_cast<float> (song + 0.075)));

        const bool poor = t >= 1.0 && t < 1.40;
        clock.setTempoTrust (poor ? 0.30f : 1.0f);
        clock.setGridPhase (vp::wrap01 (static_cast<float> (song)),
                            poor ? 2.20f : 0.90f);
        const auto tick = clock.advance (blk);
        for (int p = 0; p < tick.pulsesFired; ++p)
        {
            const double at = t + tick.pulseOffset[p] / sr;
            if (lastPulse >= 0.0 && at >= 1.40)
            {
                const double ratio = (at - lastPulse) / pulseSec;
                result.shortestPulse = std::min (result.shortestPulse, ratio);
                result.longestPulse = std::max (result.longestPulse, ratio);
            }
            lastPulse = at;
        }

        song += bpm / 60.0 * dt;
        t += dt;
        const float error = std::fabs (vp::wrapCentered (
            clock.beatPhase() - vp::wrap01 (static_cast<float> (song))));
        if (t >= 1.40 && t < 1.40 + dt)
            result.errorAtReturnMs = error * 60000.0 / bpm;
        if (t >= 1.40)
            result.bestErrorMs = std::min (result.bestErrorMs,
                                           static_cast<double> (error * 60000.0 / bpm));
        const float tolerance = 0.008f * bpm / 60.0f;
        if (t >= 1.40 && result.recoveredSec < 0.0 && error <= tolerance)
            result.recoveredSec = t - 1.40;
    }
    return result;
}

int main (int argc, char** argv)
{
    if (argc > 1 && std::strcmp (argv[1], "--reentry") == 0)
    {
        std::fprintf (stderr, "Legacy fixture has no fresh beat serials; use scripts/probe_recovery.cpp.\n");
        return 2;
        bool ok = true;
        for (float bpm : { 52.0f, 96.0f, 120.0f, 168.0f })
        {
            const auto r = cleanEvidenceReturns (bpm);
            const double halfBeat = 30.0 / bpm;
            std::printf ("phase-reentry %3.0f BPM  error=%5.1f ms  within-8ms=%5.3f s"
                         "  best=%4.1f ms  pulse=%4.2f..%4.2f x\n",
                         static_cast<double> (bpm), r.errorAtReturnMs,
                         r.recoveredSec, r.bestErrorMs,
                         r.shortestPulse, r.longestPulse);
            ok = ok && r.recoveredSec >= 0.0
                 && r.recoveredSec <= halfBeat + 2.0 * 256.0 / 48000.0
                 && r.shortestPulse > 0.50 && r.longestPulse < 1.50;
        }
        return ok ? 0 : 1;
    }

    const bool outliers = ! (argc > 1 && std::strcmp (argv[1], "--clean") == 0);
    std::printf ("# clock da solo, 60 s x 8 semi, band live (deriva ~2%%, fase +-0.04 di beat)%s\n",
                 outliers ? ", una ipotesi sbagliata da 0.22 di beat ogni 7 s" : ", nessuna ipotesi sbagliata");
    std::printf ("# \"fuori\" = secondi al minuto con la griglia oltre il 2%% dal tempo vero\n\n");
    for (float bpm : { 96.0f, 120.0f, 144.0f })
    {
        report ("BASSO",  bpm, vp::FollowStrength::low, outliers);
        report ("MEDIO",  bpm, vp::FollowStrength::medium, outliers);
        report ("ALTO*",  bpm, vp::FollowStrength::high, outliers);
        std::printf ("\n");
    }
    std::printf ("* ALTO e' il default spedito (Types.h, MainComponent)\n\n");

    std::printf ("# un fill: fase sbagliata di 0.25 di beat per DUE secondi, non per una ipotesi\n");
    std::printf ("%-8s %12s %12s %12s\n", "", "96 BPM", "120 BPM", "144 BPM");
    const std::pair<const char*, vp::FollowStrength> fs[3] = {
        { "BASSO", vp::FollowStrength::low }, { "MEDIO", vp::FollowStrength::medium },
        { "ALTO*", vp::FollowStrength::high } };
    for (const auto& [name, f] : fs)
    {
        std::printf ("%-8s", name);
        for (float bpm : { 96.0f, 120.0f, 144.0f })
        {
            double sum = 0.0; int k = 0;
            for (unsigned sd = 0; sd < 6; ++sd)
            { const double v = fillRecovery (bpm, f, 991u + 7919u * sd); if (v >= 0.0) { sum += v; ++k; } }
            std::printf ("  rientro %5.2f s", k ? sum / k : -1.0);
        }
        std::printf ("\n");
    }

    return 0;
}
