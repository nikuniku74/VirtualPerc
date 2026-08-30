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
// Five measurements, no model and no audio:
//   align     seconds to close a phase step, per follow strength and tempo
//   decoder   the phase the decoder reports against the notated grid
//   drift     what it costs to ride a band that is speeding up or slowing down,
//             per phase constant and per listening path
//   hole      the passage where the drummer stops and the band does not: what
//             tells it apart from an accelerando, and what the whole chain -
//             activations, decoder, clock - does to the phase through both
//   resync    whether BeatTracker's "the analysis has gone elsewhere" detector
//             can fire at all
//
// `hole` is the one that goes furthest: it is the only bench here that runs the
// decoder and the clock together and scores the *clock* against the notated
// grid, which is the number a listener hears. It is what closes the open item
// in docs/STATUS.md.
#include "AI/BeatDecoder.h"
#include "Tracking/PhaseTrust.h"
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
    std::printf ("Tempo e fase escono dallo stesso fit. Prima la fase usciva\n"
                 "dall'ultimo picco accettato, uno solo, e questa tabella\n"
                 "misurava 22 ms rms di jitter passato dritto, a scatti.\n\n");
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

    // The one that does not average out, and the one a fit cannot fix either.
    // A grid that once lands on the offbeat keeps landing there: every real
    // beat is then half a beat off its own grid and the on-grid gate throws it
    // away, so the error feeds itself, with the tempo coming out exactly right
    // the whole time. Only the fold sees the activation the gate never showed
    // anybody, so only the fold can notice.
    std::printf ("\nTempo giusto, mezzo battito fuori: il ripiegamento lo trova.\n");
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

// --- 4. following a band that does not hold still ----------------------------
//
// Everything above measures a step: the song jumps, how long does the clock
// take. A band does not step, it drifts - and the number that decides how well
// the clock rides a drift is the time constant `BeatTracker` hands to
// `setGridPhase`, which is where the analysis's phase is averaged before the
// steering loop ever sees it. That constant used to be one number for both
// listening paths.

struct Follow
{
    double meanMs = 0.0;
    double worstMs = 0.0;
};

/** Drives the clock exactly as BeatTracker does against a song whose tempo
    moves, with the analysis reporting a phase that is right on average and
    noisy in the moment. `tau` is the constant BeatTracker passes to
    setGridPhase; `noise` is the analysis's own phase scatter, which a line feed
    and a microphone in a room do not have in equal measure. */
Follow followDrift (float bpm0, double driftPctPerSec, double seconds, double tau,
                    double noise, vp::FollowStrength fs, int blk = 256)
{
    vp::TempoFollower clock;
    clock.prepare (kSr);
    clock.setPulsesPerBeat (4);
    clock.forceTempo (bpm0);
    clock.setTargetTempo (bpm0, 1.0f);
    clock.setFollowStrength (fs);
    clock.setLocked (true);
    clock.resetClock();

    Follow f;
    const double dt = blk / kSr;
    double songPhase = 0.0, t = 0.0, held = 0.0, nextRefresh = 0.0;
    double sum = 0.0;
    int n = 0, seed = 11;

    for (int i = 0; i < static_cast<int> (seconds * kSr) / blk; ++i)
    {
        const double songBpm = bpm0 * (1.0 + driftPctPerSec * t);
        if (t >= nextRefresh)
        {
            seed = seed * 1103515245 + 12345;
            held = noise * ((seed >> 16 & 0x7fff) / 16383.5 - 1.0);
            nextRefresh += 1.0 / 6.0;
        }
        const float songFrac = static_cast<float> (songPhase - std::floor (songPhase));
        const float seen = vp::wrap01 (songFrac + static_cast<float> (held));

        // Scored after the first ten seconds: the clock starts on the song, so
        // anything before that is the loop settling rather than the drift.
        if (t > 10.0)
        {
            const double err = std::fabs (vp::wrapCentered (clock.beatPhase() - songFrac))
                               * 60.0 / songBpm * 1000.0;
            sum += err;
            ++n;
            f.worstMs = std::max (f.worstMs, err);
        }

        const double songBefore = songPhase;
        songPhase += songBpm / 60.0 * dt;
        if (std::floor (songPhase) > std::floor (songBefore))
        {
            clock.observeOnsetPhase (vp::wrap01 (clock.beatPhase() - seen), 0.8f, 1);
            // The decoder's committed tempo follows the band with a lag of its
            // own; the clock is handed that, not the truth.
            clock.setTargetTempo (static_cast<float> (bpm0 * (1.0 + driftPctPerSec
                                                              * std::max (0.0, t - 1.2))),
                                  0.9f);
        }
        clock.setGridPhase (seen, static_cast<float> (tau));
        clock.advance (blk);
        t += dt;
    }
    if (n > 0)
        f.meanMs = sum / n;
    return f;
}

void measureFollowDrift()
{
    std::printf ("--- stare addosso a una band che si muove ---\n");
    std::printf ("La fase dell'analisi viene mediata prima che l'anello la veda.\n"
                 "Su una mandata di linea il percorso e' uno e fermo, quindi quel\n"
                 "rumore e' piccolo e mediarlo a lungo e' solo ritardo; con un\n"
                 "microfono in una stanza il rumore c'e' davvero.\n\n");
    std::printf ("%-9s %-7s %-9s %-10s %-10s\n",
                 "percorso", "tau", "deriva", "media ms", "peggio ms");

    struct Path { const char* name; double noise; };
    const Path paths[] = { { "MIXER", 0.010 }, { "IPAD", 0.035 } };
    for (const Path& p : paths)
        for (double tau : { 0.90, 0.60, 0.45, 0.30, 0.20 })
            for (double drift : { 0.0015, -0.0015 })
            {
                const Follow f = followDrift (120.0f, drift, 45.0, tau, p.noise,
                                              vp::FollowStrength::high);
                std::printf ("%-9s %-7.2f %-9s %-10.2f %-10.2f\n",
                             p.name, tau,
                             drift > 0 ? "accel" : "rall", f.meanMs, f.worstMs);
            }
    std::printf ("\nE su un tempo che non si muove affatto, dove una tau corta\n"
                 "puo' solo rimettere in circolo il rumore dell'analisi:\n");
    for (const Path& p : paths)
        for (double tau : { 0.90, 0.45, 0.30, 0.20 })
        {
            const Follow f = followDrift (120.0f, 0.0, 45.0, tau, p.noise,
                                          vp::FollowStrength::high);
            std::printf ("  %-6s tau=%-6.2f fermo     media %.2f ms  peggio %.2f ms\n",
                         p.name, tau, f.meanMs, f.worstMs);
        }
    std::printf ("\n");
}

// --- 5. the hole with no drums ------------------------------------------------
//
// docs/STATUS.md leaves this one open, with the next step written down: the fit
// cannot tell "the drummer stopped" from "the band is speeding up" early enough
// to act on, and what is needed is a measure of how much pulse is in the sound
// that does not come through the fit at all. TempoEstimator has one - the
// salience of the winning comb - and nobody had ever printed it on the two
// signals it would have to separate. This prints it.

struct Hole
{
    double worstMs = 0.0;      // phase against the notated grid, in the passage
    double meanMs = 0.0;
    double bpmLow = 1000.0;    // how far the committed tempo slid
    double bpmHigh = 0.0;
    double salienceIn = 0.0;   // averaged with the kit playing
    double salienceOut = 0.0;  // averaged in the passage
    double residualIn = 0.0;   // the fit's residual, outside and inside
    double residualOut = 0.0;
    double confIn = 0.0;       // and the confidence the decoder publishes
    double confOut = 0.0;
    int    beatsLive = 0;      // beats the regime was not fixed for
    // And what a listener actually hears: the clock's own phase against the
    // notated grid, driven from these hypotheses the way BeatTracker drives it.
    double clockWorstMs = 0.0;
    double clockMeanMs = 0.0;
    double trustLow = 1.0;
};

/** Activations shaped as BeatNet emits them, over a song that either loses its
    drums for a while or speeds up without ever losing them.

    `holeFrom`/`holeTo` are seconds. Inside the hole the beats are still there -
    a pad and a bass still land on the beat - but they are quieter, wider and
    far worse placed, which is exactly what the residual sees. Outside it they
    are a kit. */
Hole drumHole (float bpm, double driftPctPerSec, double holeFrom, double holeTo,
               double seconds, unsigned seed, bool print,
               double scoreFrom = -1.0, double scoreTo = -1.0,
               bool lineFeed = true, bool trustGrid = true,
               double holeLateFrames = 2.2, bool trustClock = true,
               bool trimClock = false)
{
    vp::BeatDecoder dec;
    dec.prepare (kFps);
    dec.setLineFeed (true);

    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> floorNoise (0.0f, 0.10f);
    std::normal_distribution<double> unit (0.0, 1.0);

    // Beat times from the tempo curve, so an accelerando is a real one rather
    // than a period that steps. `beatIdeal` is the notated grid and is what the
    // phase is scored against; `beatAt` is where the activation actually crests,
    // which is not the same thing and is the whole difficulty.
    std::vector<double> beatIdeal;   // frames, notated
    std::vector<double> beatAt;      // frames, as the network would see them
    std::vector<float>  beatGain;
    std::vector<float>  beatWidth;
    {
        double t = 0.0;
        int k = 0;
        while (t < seconds + 2.0)
        {
            const double songBpm = bpm * (1.0 + driftPctPerSec * t);
            const bool inHole = t >= holeFrom && t < holeTo;
            // What a passage without a kit does to the activation: quieter,
            // wider, worse placed, and late - a pad and a bass note swell into
            // the beat where a stick lands on it - and every third beat carries
            // nothing that crosses the gate at all.
            const double jitterFrames = inHole ? 3.0 : 1.0;
            const double lateFrames = inHole ? holeLateFrames : 0.0;
            beatIdeal.push_back (t * kFps);
            beatAt.push_back (t * kFps + lateFrames + unit (rng) * jitterFrames);
            const bool dropped = inHole && (k % 3) == 2;
            beatGain.push_back (dropped ? 0.14f : (inHole ? 0.44f : 0.94f));
            beatWidth.push_back (inHole ? 3.4f : 1.6f);
            t += 60.0 / songBpm;
            ++k;
        }
    }

    // The window the passage is scored over. For a song that never loses its
    // drums this is the same stretch of time, so the two runs are compared over
    // the same seconds rather than one being compared against nothing.
    if (scoreFrom < 0.0) scoreFrom = holeFrom;
    if (scoreTo < 0.0)   scoreTo = holeTo;

    // The clock, driven from the same hypotheses and by the same rules as
    // BeatTracker::process. One analysis frame is one block here, which is what
    // makes the run repeatable; the loop's constants are all in seconds, so the
    // block size does not change the answer.
    vp::TempoFollower clock;
    clock.prepare (kSr);
    clock.setPulsesPerBeat (4);
    clock.forceTempo (bpm);
    clock.setTargetTempo (bpm, 1.0f);
    clock.setFollowStrength (vp::FollowStrength::high);
    clock.setLocked (true);
    clock.setTempoTrimEnabled (trimClock);
    clock.resetClock();
    vp::EvidenceTrust trust;
    const int blockPerFrame = static_cast<int> (kSr / kFps);
    uint32_t lastBeatSerial = 0;
    bool seenSerial = false;
    double clockSum = 0.0;
    int clockN = 0;

    Hole h;
    double sum = 0.0;
    int n = 0, salIn = 0, salOut = 0;
    double salInSum = 0.0, salOutSum = 0.0;
    double resInSum = 0.0, resOutSum = 0.0, confInSum = 0.0, confOutSum = 0.0;
    int resIn = 0, resOut = 0;
    int lastPrinted = -1;

    const int total = static_cast<int> (kFps * seconds);
    for (int i = 0; i < total; ++i)
    {
        float act = floorNoise (rng);
        for (size_t k = 0; k < beatAt.size(); ++k)
        {
            const double d = (static_cast<double> (i) - beatAt[k]) / beatWidth[k];
            if (std::fabs (d) < 5.0)
                act = std::max (act, beatGain[k] * static_cast<float> (std::exp (-0.5 * d * d)));
        }

        const auto hy = dec.observe (act, 0.03f, 1.0f - act);
        const double t = static_cast<double> (i) / kFps;

        // --- the clock, steered exactly as BeatTracker steers it ---
        if (hy.valid)
        {
            trust.observe (hy.fitResidual, hy.fitCoverage, 1.0 / kFps);
            if (! seenSerial)
            {
                lastBeatSerial = hy.beatSerial;
                seenSerial = true;
            }
            clock.setTempoTrust (trustClock ? trust.trust() : 1.0f);
            if (hy.bpm > 50.0f)
                clock.setTargetTempo (hy.bpm, hy.confidence);
            if (hy.beatSerial != lastBeatSerial && hy.confidence > 0.25f)
            {
                lastBeatSerial = hy.beatSerial;
                clock.observeOnsetPhase (vp::wrap01 (clock.beatPhase() - hy.beatPhase),
                                         hy.confidence, 1);
            }
            // The rejected alternative from Tracking/PhaseTrust.h: a shorter
            // constant on a line feed. Measured here so the note that says it
            // is not an improvement has a number behind it.
            const float baseTau = lineFeed ? 0.35f : vp::kGridTauHolding;
            clock.setGridPhase (hy.beatPhase,
                                vp::gridPhaseTau (baseTau, true,
                                                  trustGrid ? trust.trust() : 1.0f));
        }
        clock.advance (blockPerFrame);
        const auto diag = dec.diagnostics();
        const bool inHole = t >= holeFrom && t < holeTo;

        if (t > 8.0)
        {
            if (inHole) { salOutSum += diag.combSalience; ++salOut; }
            else        { salInSum  += diag.combSalience; ++salIn;  }
            if (hy.valid)
            {
                // The passage is scored from its start to six seconds after it
                // ends: the evidence a fit is built on outlives the passage by
                // its own window, and so does the damage.
                const bool inPassage = t >= scoreFrom && t < scoreTo + 6.0;
                if (inPassage) { resOutSum += hy.fitResidual; confOutSum += hy.confidence; ++resOut; }
                else           { resInSum  += hy.fitResidual; confInSum  += hy.confidence; ++resIn;  }
            }
        }

        if (! hy.valid || t <= 8.0)
            continue;

        h.bpmLow = std::min (h.bpmLow, static_cast<double> (hy.bpm));
        h.bpmHigh = std::max (h.bpmHigh, static_cast<double> (hy.bpm));

        // Where the song's beat really is at this frame - the notated grid, not
        // where the activation happened to crest.
        double truePhase = 0.0;
        {
            size_t k = 0;
            while (k + 1 < beatIdeal.size() && beatIdeal[k + 1] <= static_cast<double> (i))
                ++k;
            const double span = (k + 1 < beatIdeal.size() ? beatIdeal[k + 1]
                                                          : beatIdeal[k] + kFps)
                                - beatIdeal[k];
            truePhase = span > 1.0 ? (static_cast<double> (i) - beatIdeal[k]) / span : 0.0;
        }
        const double err = std::fabs (vp::wrapCentered (hy.beatPhase
                                                        - static_cast<float> (truePhase)))
                           * 60.0 / bpm * 1000.0;
        if (t >= scoreFrom && t < scoreTo + 6.0)
        {
            h.worstMs = std::max (h.worstMs, err);
            sum += err;
            ++n;

            const double clockErr = std::fabs (vp::wrapCentered (
                                        clock.beatPhase() - static_cast<float> (truePhase)))
                                    * 60.0 / bpm * 1000.0;
            h.clockWorstMs = std::max (h.clockWorstMs, clockErr);
            clockSum += clockErr;
            ++clockN;
            h.trustLow = std::min (h.trustLow, static_cast<double> (trust.trust()));
        }
        if (hy.regime != vp::TempoRegime::fixed && t >= scoreFrom)
            ++h.beatsLive;

        if (print)
        {
            const int sec = static_cast<int> (t);
            if (sec != lastPrinted && sec % 2 == 0)
            {
                lastPrinted = sec;
                std::printf ("   t=%-4d %-6s bpm=%-8.2f fase=%-7.1f res=%-7.3f "
                             "cop=%-6.2f sal=%-6.3f conf=%-6.2f %s\n",
                             sec, inHole ? "BUCO" : "kit",
                             static_cast<double> (hy.bpm), err,
                             static_cast<double> (hy.fitResidual),
                             static_cast<double> (hy.fitCoverage),
                             static_cast<double> (diag.combSalience),
                             static_cast<double> (hy.confidence),
                             vp::regimeLabel (static_cast<int> (hy.regime)));
            }
        }
    }
    if (n > 0)
        h.meanMs = sum / n;
    if (clockN > 0)
        h.clockMeanMs = clockSum / clockN;
    h.salienceIn = salIn > 0 ? salInSum / salIn : 0.0;
    h.salienceOut = salOut > 0 ? salOutSum / salOut : 0.0;
    h.residualIn = resIn > 0 ? resInSum / resIn : 0.0;
    h.residualOut = resOut > 0 ? resOutSum / resOut : 0.0;
    h.confIn = resIn > 0 ? confInSum / resIn : 0.0;
    h.confOut = resOut > 0 ? confOutSum / resOut : 0.0;
    return h;
}

// --- 6. how fast the clock takes a tempo change ---------------------------
//
// The one thing a live percussionist is judged on. A band picks up out of a
// chorus, drags into a ballad, or the drummer simply pushes - and the whole
// question is how many bars the app spends being wrong about it.
//
// Measured through the real chain, decoder and clock, against activations from
// a song whose tempo genuinely moves. The clock's own tempo is compared with
// the song's at every block, and the answer is the time from the change until
// it is inside one percent and stays there.

/** How close counts as "taken the change".

    One percent was the first choice and it turned out to be a measurement of
    the decoder's own noise rather than of its speed: on this material the
    eight-beat fit wobbles +/-1.5 BPM, which at 128 is 1.2%, so a clock sitting
    exactly on the answer fails a 1% test at random. Two percent is 2.5 BPM at
    128 - still well inside what a listener hears as the same tempo - and is
    above the floor, so what the column measures is adaptation. */
constexpr double kInsideEnough = 2.0;

struct TempoStep
{
    double secondsToLock = -1.0;   // to within 1% and stay
    double worstErrPct = 0.0;      // how far off it got on the way
    double settledErrPct = 0.0;    // and where it ended up
};

/** `rampSec` of zero is a step; anything else is a ramp taking that long. */
TempoStep tempoChange (float fromBpm, float toBpm, double atSec, double rampSec,
                       double seconds, unsigned seed, bool print = false,
                       bool quickStep = true)
{
    vp::BeatDecoder dec;
    dec.prepare (kFps);
    dec.setLineFeed (true);
    dec.setQuickStep (quickStep);

    vp::TempoFollower clock;
    clock.prepare (kSr);
    clock.setPulsesPerBeat (4);
    clock.forceTempo (fromBpm);
    clock.setTargetTempo (fromBpm, 1.0f);
    clock.setFollowStrength (vp::FollowStrength::high);
    clock.setLocked (true);
    clock.resetClock();

    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> floorNoise (0.0f, 0.10f);
    std::normal_distribution<double> jitter (0.0, 1.1);

    auto bpmAt = [&] (double t)
    {
        if (t <= atSec)
            return static_cast<double> (fromBpm);
        if (rampSec <= 0.0)
            return static_cast<double> (toBpm);
        const double u = std::min (1.0, (t - atSec) / rampSec);
        return fromBpm + (toBpm - fromBpm) * u;
    };

    // Beat times from the tempo curve, so a ramp is a real one.
    std::vector<double> beatAt;
    {
        double t = 0.0;
        while (t < seconds + 2.0)
        {
            beatAt.push_back (t * kFps + jitter (rng));
            t += 60.0 / bpmAt (t);
        }
    }

    TempoStep r;
    const int blockPerFrame = static_cast<int> (kSr / kFps);
    const int total = static_cast<int> (kFps * seconds);
    uint32_t lastSerial = 0;
    bool seenSerial = false;
    double lockedAt = -1.0;
    int lastPrinted = -1;

    for (int i = 0; i < total; ++i)
    {
        float act = floorNoise (rng);
        for (size_t k = 0; k < beatAt.size(); ++k)
        {
            const double dd = (static_cast<double> (i) - beatAt[k]) / 1.6;
            if (std::fabs (dd) < 5.0)
                act = std::max (act, 0.94f * static_cast<float> (std::exp (-0.5 * dd * dd)));
        }

        const auto hy = dec.observe (act, 0.03f, 1.0f - act);
        const double t = static_cast<double> (i) / kFps;

        if (hy.valid)
        {
            if (! seenSerial) { lastSerial = hy.beatSerial; seenSerial = true; }
            if (hy.bpm > 50.0f)
                clock.setTargetTempo (hy.bpm, hy.confidence);
            if (hy.beatSerial != lastSerial && hy.confidence > 0.25f)
            {
                lastSerial = hy.beatSerial;
                clock.observeOnsetPhase (vp::wrap01 (clock.beatPhase() - hy.beatPhase),
                                         hy.confidence, 1);
            }
            clock.setGridPhase (hy.beatPhase,
                                vp::gridPhaseTau (vp::kGridTauHolding, true, 1.0f));
        }
        clock.advance (blockPerFrame);

        if (t <= atSec)
            continue;

        const double want = bpmAt (t);
        const double errPct = std::fabs (clock.currentTempo() - want) / want * 100.0;
        r.worstErrPct = std::max (r.worstErrPct, errPct);
        r.settledErrPct = errPct;
        if (errPct < kInsideEnough)
        {
            if (lockedAt < 0.0)
                lockedAt = t;
        }
        else
        {
            lockedAt = -1.0;
        }
        if (lockedAt >= 0.0 && r.secondsToLock < 0.0 && t - lockedAt > 1.0)
            r.secondsToLock = lockedAt - atSec;

        if (print)
        {
            const int sec = static_cast<int> (t);
            if (sec != lastPrinted && sec % 2 == 0)
            {
                lastPrinted = sec;
                const auto dg = dec.diagnostics();
                std::printf ("   t=%-4d vero=%-7.2f orologio=%-7.2f decoder=%-7.2f "
                             "corto=%-7.2f lungo=%-7.2f rate=%-5.2f  scarto %.2f%%  %s\n",
                             sec, want, static_cast<double> (clock.currentTempo()),
                             static_cast<double> (hy.bpm),
                             static_cast<double> (dg.shortFit),
                             static_cast<double> (dg.longFit),
                             static_cast<double> (dg.shortFitRate),
                             errPct, vp::regimeLabel (static_cast<int> (hy.regime)));
            }
        }
    }
    return r;
}

void measureTempoChange()
{
    std::printf ("--- quanto ci mette a prendere un cambio di tempo ---\n");
    std::printf ("La cosa su cui un percussionista dal vivo viene giudicato. Il\n"
                 "tempo cambia a 20 s; la colonna e' i secondi da li' a stare\n"
                 "dentro il 2%% e restarci - vedi kInsideEnough per perche' non\n"
                 "l'1%%.\n\n");
    std::printf ("%-26s %-11s %-11s %-11s %-11s\n",
                 "cambio", "prima s", "adesso s", "peggio %", "residuo %");

    struct Case { const char* name; float from, to; double ramp; };
    const Case cases[] = {
        { "118 -> 124, gradino",     118.0f, 124.0f, 0.0 },
        { "118 -> 128, gradino",     118.0f, 128.0f, 0.0 },
        { "100 -> 140, gradino",     100.0f, 140.0f, 0.0 },
        { "128 -> 120, gradino",     128.0f, 120.0f, 0.0 },
        { "118 -> 126 in 4 s",       118.0f, 126.0f, 4.0 },
        { "118 -> 126 in 12 s",      118.0f, 126.0f, 12.0 },
    };
    // One case traced, so where the seconds actually go is visible rather than
    // guessed at.
    std::printf ("\n  Il gradino 118 -> 128, battuta per battuta:\n");
    tempoChange (118.0f, 128.0f, 18.0, 0.0, 40.0, 101u, true);
    std::printf ("\n");

    for (const Case& c : cases)
    {
        double t[2] = { 0.0, 0.0 };
        int found[2] = { 0, 0 };
        double worst = 0.0, resid = 0.0;
        for (int q = 0; q < 2; ++q)
            for (int s = 0; s < 4; ++s)
            {
                const TempoStep r = tempoChange (c.from, c.to, 20.0, c.ramp, 70.0,
                                                 101u + static_cast<unsigned> (s) * 977u,
                                                 false, q == 1);
                if (r.secondsToLock >= 0.0) { t[q] += r.secondsToLock; ++found[q]; }
                if (q == 1)
                {
                    worst = std::max (worst, r.worstErrPct);
                    resid += r.settledErrPct;
                }
            }
        char before[16], after[16];
        std::snprintf (before, sizeof before, found[0] > 0 ? "%.2f" : "MAI",
                       t[0] / std::max (1, found[0]));
        std::snprintf (after, sizeof after, found[1] > 0 ? "%.2f" : "MAI",
                       t[1] / std::max (1, found[1]));
        std::printf ("%-26s %-11s %-11s %-11.2f %-11.3f\n",
                     c.name, before, after, worst, resid / 4.0);
    }
    std::printf ("\n");
}

// --- 6b. the phase a ramp leaves behind ----------------------------------
//
// Section 6 above measures the tempo, in percent, and that is the wrong
// quantity for a ramp - which is what a band actually does. A rise of 10 BPM
// spread over half a minute is 0.33 BPM a second: a clock that is a full second
// behind the band all the way through it never reads worse than 0.3% and passes
// every threshold in that table with room to spare. What a listener hears is
// not the tempo, it is the *phase* - and a clock running 0.3% slow for ten
// seconds is a twentieth of a beat out and still going.
//
// This exists because a user said so before any bench did: that on a band going
// gradually from 100 to 110 the app "goes out and stays lost for a while", and
// that the tempo lines up with the band better after STOP than while playing.
// Both are true, and section 6 could not see either.
//
// So: phase, in milliseconds, against the notated grid, three ways.
//
//   LEANA    the loop as the app runs it while the part is sounding: the grid
//            is never moved, only the rate is bent towards it
//   TRIM     the same, plus the rate integrator - which the app switches off in
//            the live regime, which is to say switches off exactly when the
//            band is speeding up
//   POSA     what the app does while nothing is playing: the grid is *placed*
//            on the song. This is what a listener gets after pressing STOP.
//
// The two things the numbers say:
//
// **A proportional loop cannot be inside a ramp.** It closes an error by
// leaning, so it needs a standing error to lean at all, and the faster the band
// moves the further out it has to sit to keep up. The `coda` column - the
// signed phase over the last third of the ramp - shows it directly: behind on
// every ramp that speeds up, ahead on the one that slows down, in proportion to
// how fast. That is not a threshold to tune, it is what the loop is, and the
// only cure is a term that remembers.
//
// **Placing beats leaning**, which is why STOP sounds better aligned. Placing
// while sounding is not available: it was tried, bounded so that no jump could
// cross a pulse, and it measured 21/38/22/18 ms against LEANA's 31/59/31/26 -
// and broke four of the invariants the lean exists to protect. A move that
// cannot double or drop a stroke can still *stretch* one, and applying the
// whole correction on the block it arrives makes the loop's behaviour a
// function of the buffer size. Both are tested for, both failed, and the tests
// are right. What is left for the sounding path is the rate.

enum class RampMode { lean, trim, place };

struct RampPhase
{
    double meanMs = 0.0;    // mean |phase| through the ramp
    double worstMs = 0.0;   // and the worst of it
    double tailMs = 0.0;    // signed, over the last third of the ramp: the lag
    double afterMs = 0.0;   // mean |phase| over the eight seconds after it ends
};

/** The same chain as `tempoChange` - real decoder, real clock - scored on
    phase instead of on tempo. The truth is the un-jittered beat grid: the
    jitter is the analysis's noise, not the band moving. */
RampPhase rampPhase (float fromBpm, float toBpm, double atSec, double rampSec,
                     double seconds, unsigned seed, RampMode mode, bool print = false)
{
    vp::BeatDecoder dec;
    dec.prepare (kFps);
    dec.setLineFeed (true);

    vp::TempoFollower clock;
    clock.prepare (kSr);
    clock.setPulsesPerBeat (4);
    clock.forceTempo (fromBpm);
    clock.setTargetTempo (fromBpm, 1.0f);
    clock.setFollowStrength (vp::FollowStrength::high);
    clock.setLocked (true);
    clock.setTempoTrimEnabled (mode == RampMode::trim);
    clock.resetClock();

    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> floorNoise (0.0f, 0.10f);
    std::normal_distribution<double> jitter (0.0, 1.1);

    auto bpmAt = [&] (double t)
    {
        if (t <= atSec)
            return static_cast<double> (fromBpm);
        if (rampSec <= 0.0)
            return static_cast<double> (toBpm);
        const double u = std::min (1.0, (t - atSec) / rampSec);
        return fromBpm + (toBpm - fromBpm) * u;
    };

    // Two grids from one tempo curve: the one the band plays, and the one the
    // activations land on. They differ only by the jitter.
    std::vector<double> trueAt, beatAt;
    {
        double t = 0.0;
        while (t < seconds + 2.0)
        {
            trueAt.push_back (t * kFps);
            beatAt.push_back (t * kFps + jitter (rng));
            t += 60.0 / bpmAt (t);
        }
    }

    // Where the song is, as a phase, at this frame.
    size_t cursor = 0;
    auto truePhaseAt = [&] (double frame) -> double
    {
        while (cursor + 1 < trueAt.size() && trueAt[cursor + 1] <= frame)
            ++cursor;
        if (cursor + 1 >= trueAt.size())
            return 0.0;
        const double span = trueAt[cursor + 1] - trueAt[cursor];
        return span > 1.0e-9 ? (frame - trueAt[cursor]) / span : 0.0;
    };

    RampPhase r;
    const int blockPerFrame = static_cast<int> (kSr / kFps);
    const int total = static_cast<int> (kFps * seconds);
    uint32_t lastSerial = 0;
    bool seenSerial = false;
    double sum = 0.0, tailSum = 0.0, afterSum = 0.0;
    int n = 0, tailN = 0, afterN = 0;
    int lastPrinted = -1;

    for (int i = 0; i < total; ++i)
    {
        float act = floorNoise (rng);
        for (size_t k = 0; k < beatAt.size(); ++k)
        {
            const double dd = (static_cast<double> (i) - beatAt[k]) / 1.6;
            if (std::fabs (dd) < 5.0)
                act = std::max (act, 0.94f * static_cast<float> (std::exp (-0.5 * dd * dd)));
        }

        const auto hy = dec.observe (act, 0.03f, 1.0f - act);
        const double t = static_cast<double> (i) / kFps;

        if (hy.valid)
        {
            if (! seenSerial) { lastSerial = hy.beatSerial; seenSerial = true; }
            if (hy.bpm > 50.0f)
                clock.setTargetTempo (hy.bpm, hy.confidence);
            if (hy.beatSerial != lastSerial && hy.confidence > 0.25f)
            {
                lastSerial = hy.beatSerial;
                clock.observeOnsetPhase (vp::wrap01 (clock.beatPhase() - hy.beatPhase),
                                         hy.confidence, 1);
            }
            const float gridErr = vp::wrapCentered (clock.beatPhase() - hy.beatPhase);
            if (mode == RampMode::place && std::fabs (gridErr) > 0.04f)
                clock.snapPhase (hy.beatPhase, true);
            else
                clock.setGridPhase (hy.beatPhase,
                                    vp::gridPhaseTau (vp::kGridTauHolding, true, 1.0f));
        }

        // Read before the block advances, so a block's own travel is not
        // counted as an error.
        const double bp = truePhaseAt (static_cast<double> (i));
        const double errBeats = vp::wrapCentered (
            static_cast<float> (clock.beatPhase() - bp));
        const double ms = errBeats * 60.0 / bpmAt (t) * 1000.0;
        clock.advance (blockPerFrame);

        if (t > atSec && t <= atSec + std::max (0.001, rampSec))
        {
            sum += std::fabs (ms);
            r.worstMs = std::max (r.worstMs, std::fabs (ms));
            ++n;
            if (t > atSec + rampSec * 2.0 / 3.0)
            {
                tailSum += ms;
                ++tailN;
            }
        }
        else if (t > atSec + rampSec && t <= atSec + rampSec + 8.0)
        {
            afterSum += std::fabs (ms);
            ++afterN;
        }

        if (print)
        {
            const int sec = static_cast<int> (t);
            if (sec != lastPrinted && sec % 4 == 0)
            {
                lastPrinted = sec;
                std::printf ("   t=%-4d vero=%-7.2f orologio=%-7.2f  fase %+7.1f ms\n",
                             sec, bpmAt (t), static_cast<double> (clock.currentTempo()), ms);
            }
        }
    }

    r.meanMs = n > 0 ? sum / n : 0.0;
    r.tailMs = tailN > 0 ? tailSum / tailN : 0.0;
    r.afterMs = afterN > 0 ? afterSum / afterN : 0.0;
    return r;
}

void measureRampPhase()
{
    std::printf ("--- la fase che una rampa lascia indietro ---\n");
    std::printf ("Il tempo in percentuale non misura una rampa: 10 BPM in 30 s\n"
                 "sono 0.33 BPM al secondo, e un orologio indietro di un secondo\n"
                 "sta dentro lo 0.3%%. Quello che si sente e' la fase. Qui e' in\n"
                 "millisecondi contro la griglia vera, nei tre modi che l'app ha:\n"
                 "LEANA (come suona), TRIM (con l'integratore che in regime VIVO\n"
                 "e' spento) e POSA (griglia posata, cioe' quello che si sente a\n"
                 "STOP).\n\n");
    std::printf ("%-24s %-9s %-11s %-11s %-11s %-11s\n",
                 "rampa", "modo", "media ms", "peggio ms", "coda ms", "dopo ms");

    struct Case { const char* name; float from, to; double ramp; };
    const Case cases[] = {
        { "100 -> 110 in 30 s", 100.0f, 110.0f, 30.0 },
        { "100 -> 110 in 12 s", 100.0f, 110.0f, 12.0 },
        { "120 -> 132 in 20 s", 120.0f, 132.0f, 20.0 },
        { "128 -> 120 in 20 s", 128.0f, 120.0f, 20.0 },
    };
    const struct { RampMode m; const char* label; } modes[] = {
        { RampMode::lean,  "LEANA" },
        { RampMode::trim,  "TRIM" },
        { RampMode::place, "POSA" },
    };

    for (const Case& c : cases)
    {
        for (const auto& md : modes)
        {
            double mean = 0.0, worst = 0.0, tail = 0.0, after = 0.0;
            constexpr int kSeeds = 4;
            for (int s = 0; s < kSeeds; ++s)
            {
                const RampPhase r = rampPhase (c.from, c.to, 20.0, c.ramp,
                                               20.0 + c.ramp + 10.0,
                                               101u + static_cast<unsigned> (s) * 977u,
                                               md.m);
                mean += r.meanMs;
                worst = std::max (worst, r.worstMs);
                tail += r.tailMs;
                after += r.afterMs;
            }
            std::printf ("%-24s %-9s %-11.1f %-11.1f %-+11.1f %-11.1f\n",
                         md.m == RampMode::lean ? c.name : "", md.label,
                         mean / kSeeds, worst, tail / kSeeds, after / kSeeds);
        }
    }
    std::printf ("\n");
}

void measureDrumHole (double kHoleLate)
{
    std::printf ("=== ritardo sistematico del passaggio: %.1f frame (%.0f ms) ===\n",
                 kHoleLate, kHoleLate / kFps * 1000.0);
    std::printf ("--- il buco senza batteria, e cosa lo distingue da un accelerando ---\n");
    std::printf ("docs/STATUS.md lascia aperta questa: il residuo del fit non separa\n"
                 "\"la batteria e' uscita\" da \"la band accelera\" abbastanza presto.\n"
                 "La salience del pettine non passa dal fit. Questi sono i suoi valori\n"
                 "sui due segnali che dovrebbe separare.\n\n");

    std::printf ("  Brano a 118, la batteria esce fra 20 e 30 s:\n");
    const Hole hole = drumHole (118.0f, 0.0, 20.0, 30.0, 50.0, 4242u, true,
                                -1.0, -1.0, true, true, kHoleLate, true);
    std::printf ("\n  Lo stesso brano che accelera dell'1.5%% al secondo, batteria sempre dentro:\n");
    const Hole accel = drumHole (118.0f, 0.0015, 1000.0, 1000.0, 50.0, 4242u, true,
                                 20.0, 30.0, true, true, kHoleLate, true);

    std::printf ("\n%-22s %-11s %-11s %-11s\n",
                 "", "buco", "accelerando", "separa?");
    std::printf ("%-22s %-11.3f %-11.3f %s\n", "salience dentro",
                 hole.salienceIn, accel.salienceIn, "");
    std::printf ("%-22s %-11.3f %-11.3f %s\n", "salience nel passaggio",
                 hole.salienceOut, accel.salienceOut,
                 hole.salienceOut < accel.salienceOut * 0.75 ? "SI" : "no");
    std::printf ("%-22s %-11.3f %-11.3f\n", "residuo fuori",
                 hole.residualIn, accel.residualIn);
    std::printf ("%-22s %-11.3f %-11.3f %s\n", "residuo nel passaggio",
                 hole.residualOut, accel.residualOut,
                 hole.residualOut > hole.residualIn * 1.25
                     && accel.residualOut < accel.residualIn * 1.25 ? "SI" : "no");
    std::printf ("%-22s %-11.2f %-11.2f %s\n", "confidenza nel passaggio",
                 hole.confOut, accel.confOut,
                 hole.confOut < accel.confOut * 0.85 ? "SI" : "no");
    std::printf ("%-22s %-11.1f %-11.1f\n", "fase peggiore ms",
                 hole.worstMs, accel.worstMs);
    std::printf ("%-22s %-11.1f %-11.1f\n", "fase media ms",
                 hole.meanMs, accel.meanMs);
    std::printf ("%-22s %-11.2f %-11.2f\n", "bpm piu' basso",
                 hole.bpmLow, accel.bpmLow);

    // And the number a listener actually hears: the clock, not the decoder.
    // Each row is the whole chain, with the phase constant fixed at what it was
    // before and then as it is now.
    // And the number a listener actually hears: the clock, not the decoder.
    // Averaged over eight songs, because one is a coin: the placement errors
    // are drawn, and a single seed moves these figures by a couple of
    // milliseconds on its own.
    //
    // Two changes are separated here rather than measured together, which is
    // how the phase constant nearly got shipped on the strength of the other
    // one's numbers: `line` shortens the constant on a line feed, `trust`
    // opens it up - and floors the rate glide - while this song's own fit is
    // going wide.
    std::printf ("\n  L'orologio, non il decoder: fase contro la griglia scritta,\n"
                 "  media su otto brani.\n");
    std::printf ("  %-26s %-12s %-12s %-12s %-12s\n",
                 "", "buco media", "buco peggio", "accel media", "accel peggio");
    struct Variant { const char* name; bool line; bool grid; bool clock; bool trim; };
    const Variant variants[] = {
        { "prima",                   false, false, false, false },
        { "prove: solo tau griglia", false, true,  false, false },
        { "prove: solo orologio",    false, false, true,  false },
        { "prove: tutte e due",      false, true,  true,  false },
        { "tau linea, senza prove",  true,  false, false, false },
        { "tau linea + prove",       true,  true,  true,  false },
        // And the one this table is here to guard. The trim integrates a phase
        // slope into a rate, and the passage without a drummer is exactly where
        // a slope appears that the band did not play: the activation slides
        // late as the kit goes out and slides back as it returns. If that costs
        // the hole more than the ramp gains, it does not ship.
        { "prove + trim",            false, true,  true,  true  },
    };
    constexpr int kSeeds = 8;
    for (const Variant& v : variants)
    {
        double bm = 0.0, bw = 0.0, am = 0.0, aw = 0.0, tl = 1.0;
        for (int k = 0; k < kSeeds; ++k)
        {
            const unsigned sd = 4242u + static_cast<unsigned> (k) * 7919u;
            const Hole b = drumHole (118.0f, 0.0, 20.0, 30.0, 50.0, sd, false,
                                     -1.0, -1.0, v.line, v.grid, kHoleLate, v.clock, v.trim);
            const Hole a = drumHole (118.0f, 0.0015, 1000.0, 1000.0, 50.0, sd, false,
                                     20.0, 30.0, v.line, v.grid, kHoleLate, v.clock, v.trim);
            bm += b.clockMeanMs; bw += b.clockWorstMs;
            am += a.clockMeanMs; aw += a.clockWorstMs;
            tl = std::min (tl, b.trustLow);
        }
        std::printf ("  %-26s %-12.1f %-12.1f %-12.1f %-12.1f  (fiducia min %.2f)\n",
                     v.name, bm / kSeeds, bw / kSeeds, am / kSeeds, aw / kSeeds, tl);
    }
    std::printf ("\n");
}

// --- 3. can the re-tune detector fire? ----------------------------------------

void measureResync()
{
    std::printf ("--- il vecchio rilevatore di \"l'analisi e' andata altrove\" ---\n");
    std::printf ("Confrontava il BPM del decoder con heldBpm, che e' il tempo del\n"
                 "follower, che sta gia' inseguendo il decoder: servivano 1.15 s\n"
                 "netti sopra l'8%% e nessun gradino ci arriva. Ora il decoder dice\n"
                 "da se' quando butta via la griglia (gridSerial); questa tabella\n"
                 "resta perche' e' il motivo per cui non si torna a misurarlo qui.\n\n");
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
    measureFollowDrift();
    measureDrumHole (0.0);
    measureDrumHole (2.2);
    measureTempoChange();
    measureRampPhase();
    measureResync();
    return 0;
}
