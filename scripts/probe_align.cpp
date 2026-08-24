// How long the clock takes to get onto the song, and how good the thing it is
// aiming at actually is.
//
// Everything else in this tree measures the clock once it is already there:
// how still it holds, whether it doubles a stroke, whether the bar counts to
// four. None of that says how long a listener waits after the song moves under
// the app - a new track, a section that dropped the beat, the shaker armed
// against a song already playing - and none of it looks at the *phase* the
// decoder hands over, which is the thing the clock is aiming at and the ceiling
// on how well it can possibly do.
//
// Three measurements, no model and no audio:
//   align     seconds to close a phase step, per follow strength and tempo
//   decoder   the phase the decoder reports against the notated grid
//   resync    whether BeatTracker's "the analysis has gone elsewhere" detector
//             can fire at all
#include "AI/BeatDecoder.h"
#include "Tracking/TempoFollower.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace
{
constexpr double kSr = 48000.0;
constexpr double kFps = 50.0;

// --- 1. time to close a phase step -------------------------------------------

struct Align
{
    double seconds = -1.0;   // to get inside `thresh` of the song
    double beats = -1.0;
    double residual = 0.0;   // where it settles
};

/** Drives the clock exactly as BeatTracker does - a phase target every block,
    an onset observation on every song beat - against a song whose beat one is
    `offsetBeats` away from the clock's own. The error is read before either
    side moves, so a block's own advance cannot show up as one. */
Align closeStep (float bpm, double offsetBeats, int blk, double seconds,
                 vp::FollowStrength fs, double analysisNoise = 0.0,
                 double thresh = 0.03)
{
    vp::TempoFollower clock;
    clock.prepare (kSr);
    clock.setPulsesPerBeat (4);
    clock.forceTempo (bpm);
    clock.setTargetTempo (bpm, 1.0f);
    clock.setFollowStrength (fs);
    clock.setLocked (true);
    clock.resetClock();

    Align a;
    const double dt = blk / kSr;
    double songPhase = -offsetBeats;
    double t = 0.0, held = 0.0, nextRefresh = 0.0;
    int seed = 7;

    for (int i = 0; i < static_cast<int> (seconds * kSr) / blk; ++i)
    {
        // The decoder refreshes about six times a second and each hypothesis
        // carries its own phase error, so the noise is held rather than redrawn
        // per block - white noise per block would be averaged away by the
        // loop's own smoothing and would flatter it.
        if (t >= nextRefresh)
        {
            seed = seed * 1103515245 + 12345;
            held = analysisNoise * ((seed >> 16 & 0x7fff) / 16383.5 - 1.0);
            nextRefresh += 1.0 / 6.0;
        }
        const float songFrac = static_cast<float> (songPhase - std::floor (songPhase));
        const float seen = vp::wrap01 (songFrac + static_cast<float> (held));

        const double err = std::fabs (vp::wrapCentered (clock.beatPhase() - songFrac));
        if (a.seconds < 0.0 && err < thresh)
        {
            a.seconds = t;
            a.beats = t * bpm / 60.0;
        }
        a.residual = err;

        const double songBefore = songPhase;
        songPhase += bpm / 60.0 * dt;
        if (std::floor (songPhase) > std::floor (songBefore))
            clock.observeOnsetPhase (vp::wrap01 (clock.beatPhase() - seen), 0.8f, 1);
        clock.setGridPhase (seen, 0.90f);
        clock.advance (blk);
        t += dt;
    }
    return a;
}

void measureAlign()
{
    std::printf ("--- quanto ci mette il clock a mettersi sulla canzone ---\n");
    std::printf ("Uno scalino di fase, analisi pulita, 120 BPM.\n");
    std::printf ("%-8s %-10s %-10s %-12s %-10s\n",
                 "segui", "scarto", "secondi", "battiti", "residuo");
    const vp::FollowStrength strengths[] = { vp::FollowStrength::high,
                                             vp::FollowStrength::medium,
                                             vp::FollowStrength::low };
    for (vp::FollowStrength fs : strengths)
        for (double off : { 0.10, 0.25, 0.40, 0.48 })
        {
            const Align a = closeStep (120.0f, off, 256, 60.0, fs);
            std::printf ("%-8s %-10.2f %-10.2f %-12.1f %-10.4f\n",
                         vp::followLabel (fs), off, a.seconds, a.beats, a.residual);
        }

    std::printf ("\nLo stesso scarto di 0.40 a tempi diversi (HIGH):\n");
    for (float bpm : { 70.0f, 100.0f, 160.0f })
    {
        const Align a = closeStep (bpm, 0.40, 256, 60.0, vp::FollowStrength::high);
        std::printf ("  %-5.0f BPM  %.2f s  = %.1f battiti\n",
                     static_cast<double> (bpm), a.seconds, a.beats);
    }

    std::printf ("\nE con il 3%% di rumore di fase dall'analisi, per buffer (HIGH, 0.25):\n");
    for (int blk : { 64, 256, 1024 })
    {
        const Align a = closeStep (120.0f, 0.25, blk, 40.0, vp::FollowStrength::high, 0.03);
        std::printf ("  buffer=%-6d %.2f s  residuo=%.4f\n", blk, a.seconds, a.residual);
    }
    std::printf ("\n");
}

// --- 2. the phase the decoder hands over --------------------------------------

struct Phase
{
    double rms = 0.0;
    double worst = 0.0;
    double bias = 0.0;
    double meanAbs = 0.0;
    double bpmErrPct = 0.0;
    int    steps = 0;        // frame-to-frame moves beyond 0.05 of a beat
    double worstStep = 0.0;
};

/** Beat activations the shape BeatNet actually emits - broad bumps, not spikes
    - with an optional eighth between every pair, and per-beat onset jitter.
    Scores the phase the decoder reports against the notated grid. */
Phase decoderPhase (float bpm, double jitterFrames, float eighthGain,
                    unsigned seed, double seconds = 60.0, double scoreFrom = 25.0)
{
    vp::BeatDecoder dec;
    dec.prepare (kFps);

    const double period = 60.0 / static_cast<double> (bpm) * kFps;
    std::mt19937 rng (seed);
    std::normal_distribution<double> jitter (0.0, jitterFrames);
    std::uniform_real_distribution<float> floorNoise (0.0f, 0.14f);
    std::uniform_real_distribution<float> coin (0.0f, 1.0f);

    const int total = static_cast<int> (kFps * seconds);
    std::vector<double> beatAt;
    std::vector<float>  beatGain;
    for (int k = 0; static_cast<double> (k) * period < total + 4.0 * period; ++k)
    {
        beatAt.push_back (static_cast<double> (k) * period + jitter (rng));
        // One beat in twelve does not clear the gate at all, as on a dense mix.
        beatGain.push_back (coin (rng) < 0.08f ? 0.10f : 0.94f);
    }

    Phase p;
    double sq = 0.0, sum = 0.0, absSum = 0.0, prev = 0.0;
    int n = 0;
    bool havePrev = false;

    for (int i = 0; i < total; ++i)
    {
        float act = floorNoise (rng);
        for (size_t k = 0; k < beatAt.size(); ++k)
        {
            const double d = static_cast<double> (i) - beatAt[k];
            if (std::fabs (d) < 6.0)
                act = std::max (act, beatGain[k] * static_cast<float> (
                                         std::exp (-0.5 * (d / 1.6) * (d / 1.6))));
            if (eighthGain > 0.0f)
            {
                const double e = static_cast<double> (i) - (beatAt[k] + period * 0.5);
                if (std::fabs (e) < 6.0)
                    act = std::max (act, eighthGain * static_cast<float> (
                                             std::exp (-0.5 * (e / 1.6) * (e / 1.6))));
            }
        }

        const auto h = dec.observe (act, 0.03f, 1.0f - act);
        if (! h.valid || static_cast<double> (i) / kFps <= scoreFrom)
            continue;

        const double truePhase = std::fmod (static_cast<double> (i) / period, 1.0);
        const double err = vp::wrapCentered (h.beatPhase - static_cast<float> (truePhase));

        sq += err * err;
        sum += err;
        absSum += std::fabs (err);
        ++n;
        p.worst = std::max (p.worst, std::fabs (err));
        if (havePrev)
        {
            const double step = std::fabs (vp::wrapCentered (static_cast<float> (err - prev)));
            if (step > 0.05)
            {
                ++p.steps;
                p.worstStep = std::max (p.worstStep, step);
            }
        }
        prev = err;
        havePrev = true;
        p.bpmErrPct = (h.bpm - bpm) / bpm * 100.0;
    }
    if (n > 0)
    {
        p.rms = std::sqrt (sq / n);
        p.bias = sum / n;
        p.meanAbs = absSum / n;
    }
    return p;
}

void measureDecoderPhase()
{
    std::printf ("--- la fase che il decoder consegna, contro la griglia vera ---\n");
    std::printf ("Il tempo esce da un fit su ventiquattro battiti; la fase esce\n"
                 "dall'ultimo picco accettato, uno solo.\n\n");
    std::printf ("%-7s %-9s %-9s %-9s %-9s %-9s %-7s %-9s\n",
                 "bpm", "jitter", "ottavi", "rms", "peggio", "media", "scatti", "peggiore");
    for (float bpm : { 76.0f, 100.0f, 132.0f, 168.0f })
        for (double jit : { 0.0, 1.1, 2.2 })
        {
            const Phase p = decoderPhase (bpm, jit, 0.0f, 20250819u);
            std::printf ("%-7.0f %-9.1f %-9s %-9.4f %-9.4f %-+9.4f %-7d %-9.4f",
                         static_cast<double> (bpm), jit, "no",
                         p.rms, p.worst, p.bias, p.steps, p.worstStep);
            if (std::fabs (p.bpmErrPct) > 3.0)
                std::printf ("  (livello sbagliato: %+.0f%%)", p.bpmErrPct);
            std::printf ("\n");
        }

    std::printf ("\nIn millisecondi al tempo, che e' come si sente:\n");
    for (float bpm : { 76.0f, 132.0f })
        for (double jit : { 0.0, 1.1 })
        {
            const Phase p = decoderPhase (bpm, jit, 0.0f, 20250819u);
            std::printf ("  %-5.0f BPM  jitter %.0f ms  ->  rms %.1f ms, peggio %.1f ms\n",
                         static_cast<double> (bpm), jit / kFps * 1000.0,
                         p.rms * 60.0 / bpm * 1000.0, p.worst * 60.0 / bpm * 1000.0);
        }

    // The one that does not average out. Anchored on a single peak, a grid that
    // once lands on the offbeat keeps landing there: every real beat is then
    // half a beat off its own grid and the on-grid gate throws it away, so the
    // error feeds itself. The tempo comes out exactly right the whole time.
    std::printf ("\nTempo giusto, mezzo battito fuori - e non rientra:\n");
    std::printf ("%-7s %-11s %-13s %-13s %-10s\n",
                 "bpm", "ottavo a", "bpm trovato", "errore/batt", "in fase?");
    for (float bpm : { 100.0f, 132.0f, 168.0f })
        for (float g : { 0.30f, 0.45f, 0.60f })
        {
            const Phase p = decoderPhase (bpm, 0.0, g, 20250819u, 90.0, 60.0);
            const bool levelWrong = std::fabs (p.bpmErrPct) > 3.0;
            std::printf ("%-7.0f %-11.2f %-13.2f %-13.3f %-10s%s\n",
                         static_cast<double> (bpm), static_cast<double> (g),
                         bpm * (1.0 + p.bpmErrPct / 100.0), p.meanAbs,
                         p.meanAbs < 0.05 ? "si" : "NO",
                         levelWrong ? "  (livello sbagliato, la fase non vuol dire niente)" : "");
        }
    std::printf ("\n");
}

// --- 3. can the re-tune detector fire? ----------------------------------------

void measureResync()
{
    std::printf ("--- il rilevatore di \"l'analisi e' andata altrove\" ---\n");
    std::printf ("BeatTracker confronta il BPM del decoder con heldBpm, e heldBpm\n"
                 "e' il tempo del follower, che sta gia' inseguendo il decoder.\n"
                 "Servono 1.15 s netti sopra l'8%%.\n\n");
    std::printf ("%-10s %-10s %-14s %-10s\n", "nuovo bpm", "salto %", "sopra l'8% per", "ricalibra?");

    constexpr int blk = 256;
    for (float nn : { 105.0f, 115.0f, 130.0f, 150.0f, 165.0f, 175.0f, 200.0f, 60.0f, 50.0f })
    {
        vp::TempoFollower c;
        c.prepare (kSr);
        c.forceTempo (100.0f);
        c.setTargetTempo (100.0f, 1.0f);
        c.setLocked (true);
        c.snapPhase (0.0f);

        double lost = 0.0, peak = 0.0;
        const double dt = blk / kSr;
        for (int i = 0; i < static_cast<int> (12.0 * kSr) / blk; ++i)
        {
            c.setTargetTempo (nn, 0.9f);
            c.advance (blk);
            const float held = c.currentTempo();
            if (std::fabs (nn - held) / held > 0.08)
                lost += dt;
            else
                lost = std::max (0.0, lost - dt * 2.0);
            peak = std::max (peak, lost);
        }
        std::printf ("%-10.0f %-10.0f %-14.2f %-10s\n",
                     static_cast<double> (nn),
                     (static_cast<double> (nn) - 100.0),
                     peak, peak > 1.15 ? "SI" : "no");
    }
    std::printf ("\n");
}
} // namespace

int main()
{
    std::printf ("Virtual Percussionist - aggancio e allineamento\n\n");
    measureAlign();
    measureDecoderPhase();
    measureResync();
    return 0;
}
