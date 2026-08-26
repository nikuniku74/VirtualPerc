// End-to-end diagnostic on material that behaves like a record, not like a
// test signal: a full arrangement at a *perfectly* fixed tempo, played out of
// an iPad speaker, into a room, back through the iPad microphone.
//
// This is the case the user reports as broken - Spotify in the room - and the
// case none of the host tests covered, because a click and a bare drum kit both
// hand the network exactly what it wants: isolated broadband onsets, nothing
// sustained, nothing syncopated, and a kick fundamental the speaker cannot
// actually reproduce.
//
// What it measures, per track:
//   t_lock     seconds until FOLLOWING, counted from the first beat of the
//              song - so a negative one means it locked to an empty room
//              before anybody played
//   rst        times the analysis started its evidence again. One is the band
//              starting; more than one is the level watcher being fooled
//   t_2%       seconds until the reported tempo is within 2% and stays there
//   span       BPM range after settling - a fixed tempo must have none
//   wobble     mean |dBPM| per second after settling
//   oct        reported / true, as an octave ratio
//   bar-break  bars that restarted before beat four - "one, two, one"
//   phase      mean and worst distance from the notated beat
#include "AI/BeatModelConfig.h"
#include "Audio/VirtualPercussionEngine.h"

#include "probe_song_render.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace vp::probe;

struct Result
{
    double tLock = -1.0;
    /** When the decoder first published a tempo it was willing to stand behind.
        The gap to `tLock` is the tracker's own state machine, and splitting the
        two is the only way to know which of them to shorten. */
    double tValid = -1.0;
    /** Whether FOLLOWING was ever reached. `tLock` alone cannot say so any
        more: with a pre-roll it is measured from the first beat of the song, so
        a lock to the empty room is a *negative* time rather than no time. */
    bool   lockSeen = false;
    double t2pct = -1.0;
    float  bpmEnd = 0.0f;
    float  span = 0.0f;
    float  spanNn = 0.0f;
    float  wobble = 0.0f;
    float  octave = 0.0f;
    int    barBreaks = 0;
    int    bigJumps = 0;
    float  phaseMean = 0.0f;
    float  phaseWorst = 0.0f;
    int    gaps = 0;
    /** Share of the settled window the decoder spent calling the tempo fixed.
        A record cut to a click should be at 100: anything less is the tracker
        chasing a tempo that is not moving. */
    float  fixedShare = 0.0f;
    /** Times the analysis was told to start its evidence again. One is the band
        starting; more than one means the level watcher was fooled by the song
        itself. */
    int    restarts = 0;
    /** Mean and worst distance from the tempo the band is actually playing, in
        BPM. On material that holds still this is the same question as `span`;
        on material that does not, it is the only one that means anything. */
    float  errMean = 0.0f;
    float  errWorst = 0.0f;
};

// The two ways the app can be listening, and they are not the same measurement.
//
// IPAD is the hard one: the app's own speaker into the room and back through
// its microphone, which is where the level, the comb filtering and the leak all
// come from. MIXER is a line feed - the same arrangement with none of that in
// front of it - so it is what the tracker can do when the signal is not the
// problem. Both are shipped, so both get measured.
enum class Listening { ipad, mixer };

// Peak level of the room before anybody plays, before the analysis make-up
// gain. An empty room is not digital silence, and the difference matters: the
// gain has a 24x ceiling and a 0.20 target, so a room at this level reaches the
// network at very nearly the level a band does.
constexpr float kRoomPeak = 0.006f;

Result run (const SongOptions& opt, double sr, unsigned seed, float level, bool trace,
            Listening mode, double preSeconds, bool sync)
{
    const int block = 256;
    const double seconds = 60.0;
    const int n = static_cast<int> (sr * seconds);

    // The room before the band starts. On a device the app has been listening
    // since it was opened, so by the time anyone plays, the analysis has been
    // running for minutes on an empty room - and every guard that counts
    // *frames* rather than music has already been satisfied by it. Starting the
    // song at sample zero is the one case that never happens in use.
    const int preN = (static_cast<int> (sr * std::max (0.0, preSeconds)) / block) * block;

    std::vector<float> song (static_cast<size_t> (preN + n), 0.0f);
    std::vector<double> truePhase;
    {
        std::vector<float> body (static_cast<size_t> (n), 0.0f);
        renderSong (body, opt, sr, seed, &truePhase);
        std::copy (body.begin(), body.end(), song.begin() + preN);
    }
    if (preN > 0)
    {
        std::mt19937 rng (seed ^ 0x4f6f6du);
        float lp = 0.0f, peak = 0.0f;
        for (int i = 0; i < preN; ++i)
        {
            lp += (noiseAt (rng) - lp) * 0.05f;
            song[static_cast<size_t> (i)] = lp;
            peak = std::max (peak, std::fabs (lp));
        }
        if (peak > 0.0f)
            for (int i = 0; i < preN; ++i)
                song[static_cast<size_t> (i)] *= kRoomPeak / peak;
        truePhase.insert (truePhase.begin(), static_cast<size_t> (preN), 0.0);
    }

    if (mode == Listening::ipad)
        speakerRoomMic (song, sr, seed, level);

    vp::VirtualPercussionEngine eng;
    eng.prepare (sr, block, 1);
    eng.settings().followSource.store (static_cast<int> (
        mode == Listening::ipad ? vp::FollowSource::speaker : vp::FollowSource::kitMic));
    eng.start();

    std::vector<float> oL (static_cast<size_t> (block), 0.0f);
    std::vector<float> oR (static_cast<size_t> (block), 0.0f);
    float* outs[2] = { oL.data(), oR.data() };

    Result r;
    // The true tempo at any moment, read off the phase the renderer actually
    // used. On drifting material this is a curve, so it is differenced rather
    // than assumed.
    auto trueBpmAt = [&truePhase, sr] (int pos)
    {
        const int step = static_cast<int> (sr * 0.5);
        const int a = std::max (0, pos - step / 2);
        const int b = std::min (static_cast<int> (truePhase.size()) - 1, a + step);
        if (b <= a) return 0.0;
        return (truePhase[static_cast<size_t> (b)] - truePhase[static_cast<size_t> (a)])
               / (static_cast<double> (b - a) / sr) * 60.0;
    };

    float lo = 1.0e9f, hi = 0.0f;
    float nnLo = 1.0e9f, nnHi = 0.0f;
    float prevBpm = 0.0f;
    double lastSampleT = -1.0;
    double dbpmAcc = 0.0;
    int    dbpmN = 0;
    double phaseAcc = 0.0;
    int    phaseN = 0;
    double errAcc = 0.0;
    int    errN = 0;
    int    prevBeatInBar = -1;
    int    regimeSamples = 0, fixedSamples = 0;
    double settleAt = -1.0;

    int pos = 0, blocks = 0;
    while (pos + block <= n)
    {
        const float* ins[1] = { song.data() + pos };
        eng.process (ins, 1, outs, 2, block);

        // Wait for the analysis to catch up with the audio before feeding it
        // any more.
        //
        // Without this the worker runs on its own thread against a feeder that
        // is faster than real time, so how far behind it happens to be is
        // decided by the host's scheduler - and that decides which activations
        // the decoder sees for a given block. Measured on one unchanged build,
        // three runs gave mean spans of 8.7, 9.2 and 12.1 BPM: a noise floor
        // wider than most of the differences anyone would want to measure. With
        // the backlog drained every block the same build gives the same answer,
        // which is what makes a before and an after comparable at all.
        if (sync)
        {
            // One analysis hop, not one block, and read live: the worker cannot
            // make a frame out of less than a hop, and the backlog inside the
            // snapshot is as of the last `process` and therefore never moves
            // while this spins.
            const int hop = static_cast<int> (std::ceil (vp::kBeatModelHop * sr
                                                         / vp::kBeatModelSampleRate));
            const auto until = std::chrono::steady_clock::now()
                               + std::chrono::milliseconds (50);
            while (eng.analysisBacklog() > hop
                   && std::chrono::steady_clock::now() < until)
                std::this_thread::yield();
        }

        const auto snap = eng.snapshot();
        // Time zero is the first beat of the song, not the first sample fed to
        // the engine: everything before that is the room.
        const double t = static_cast<double> (pos - preN) / sr;

        if (r.tValid < 0.0 && snap.hypValid && snap.neuralBpm > 40.0f)
            r.tValid = t;
        if (! r.lockSeen && snap.state == vp::TrackingState::following)
        {
            r.lockSeen = true;
            r.tLock = t;
        }

        if (snap.bpm > 40.0f)
        {
            const bool close = std::fabs (snap.bpm - trueBpmAt (pos)) / opt.bpm < 0.02f;
            if (close && r.t2pct < 0.0)
                r.t2pct = t;
            if (! close)
                r.t2pct = -1.0;   // has to stay there, not merely pass through
        }

        // Steady state starts at twenty-five seconds, the same window the
        // decoder replay uses, so the two are comparable. Anything the tracker
        // is still doing to the tempo this far in is not acquisition.
        if (settleAt < 0.0)
            settleAt = 25.0;

        if (settleAt >= 0.0 && t > settleAt && snap.bpm > 40.0f)
        {
            lo = std::min (lo, snap.bpm);
            hi = std::max (hi, snap.bpm);
            if (snap.neuralBpm > 40.0f)
            {
                nnLo = std::min (nnLo, snap.neuralBpm);
                nnHi = std::max (nnHi, snap.neuralBpm);
            }
            if (t - lastSampleT >= 0.5)
            {
                if (prevBpm > 40.0f)
                {
                    const float d = std::fabs (snap.bpm - prevBpm);
                    dbpmAcc += d;
                    ++dbpmN;
                    if (d > 0.5f)
                        ++r.bigJumps;
                }
                prevBpm = snap.bpm;
                lastSampleT = t;
            }

            ++regimeSamples;
            fixedSamples += snap.tempoRegime == 1 ? 1 : 0;

            const double tp = truePhase[static_cast<size_t> (pos)];
            const float err = vp::wrapCentered (
                snap.beatPhase - static_cast<float> (tp - std::floor (tp)));
            phaseAcc += std::fabs (err);
            ++phaseN;
            r.phaseWorst = std::max (r.phaseWorst, std::fabs (err));

            const double want = trueBpmAt (pos);
            if (want > 40.0)
            {
                const double e = std::fabs (static_cast<double> (snap.bpm) - want);
                errAcc += e;
                ++errN;
                r.errWorst = std::max (r.errWorst, static_cast<float> (e));
            }

            // The bar counter. A bar that goes back to one before it reached
            // four is the "uno, due, uno" the user hears.
            const int beatInBar = std::clamp (static_cast<int> (snap.barPhase * 4.0f), 0, 3);
            if (beatInBar != prevBeatInBar)
            {
                if (beatInBar == 0 && prevBeatInBar >= 0 && prevBeatInBar != 3)
                    ++r.barBreaks;
                prevBeatInBar = beatInBar;
            }
        }

        if (trace && (blocks % 100) == 0)
            std::printf ("   t=%5.1f  bpm=%7.2f nn=%7.2f tgt=%7.2f  regime=%d state=%-9s bar=%.2f\n",
                         t, static_cast<double> (snap.bpm), static_cast<double> (snap.neuralBpm),
                         static_cast<double> (snap.targetBpm), snap.tempoRegime,
                         vp::toString (snap.state), static_cast<double> (snap.barPhase));

        r.bpmEnd = snap.bpm;
        r.gaps = snap.analysisGaps;
        r.restarts = snap.analysisRestarts;
        pos += block;

        // Note what this pacing does and does not give you. The worker runs on
        // its own thread, so how far behind it happens to be is decided by the
        // host's scheduler - and that decides which activations the decoder
        // sees for a given block. The same build measured six times came out
        // anywhere between 15.6 and 24.4 BPM of mean span. Read a single run as
        // an indication, never as a comparison; and when comparing two builds,
        // check first whether the code that changed is even on this path (the
        // `gaps` column says whether the analysis ever lost audio, which is the
        // usual answer to "did the FIFO have anything to do with it").
        if (! sync && (++blocks % 8) == 0)
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
        else if (sync)
            ++blocks;
    }

    r.span = (hi >= lo) ? hi - lo : 0.0f;
    r.spanNn = (nnHi >= nnLo) ? nnHi - nnLo : 0.0f;
    r.wobble = dbpmN > 0 ? static_cast<float> (dbpmAcc / dbpmN) : 0.0f;
    r.fixedShare = regimeSamples > 0
                       ? 100.0f * static_cast<float> (fixedSamples)
                             / static_cast<float> (regimeSamples)
                       : -1.0f;
    r.octave = r.bpmEnd > 1.0f ? std::log2 (r.bpmEnd / opt.bpm) : 0.0f;
    r.phaseMean = phaseN > 0 ? static_cast<float> (phaseAcc / phaseN) : 0.0f;
    r.errMean = errN > 0 ? static_cast<float> (errAcc / errN) : 0.0f;
    return r;
}

} // namespace

int main (int argc, char** argv)
{
    bool trace = false;
    float drift = 0.0f, jitter = 0.0f;
    // A second of room in front of the song even when nobody asks for one. The
    // percussion is held out until the analysis has seen the input *start*, and
    // a device is never handed music from sample zero, so a run with no room in
    // front of it is a run in which nothing is ever played. `--pre 0` still
    // gives the old behaviour for anyone who wants to see it.
    double preSeconds = 1.0;
    bool sync = false;
    Listening mode = Listening::ipad;
    std::vector<const char*> rest;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--trace") == 0) trace = true;
        else if (std::strcmp (argv[i], "--mixer") == 0) mode = Listening::mixer;
        else if (std::strcmp (argv[i], "--ipad") == 0) mode = Listening::ipad;
        else if (std::strcmp (argv[i], "--drift") == 0 && i + 1 < argc) drift = static_cast<float> (std::atof (argv[++i]));
        else if (std::strcmp (argv[i], "--jitter") == 0 && i + 1 < argc) jitter = static_cast<float> (std::atof (argv[++i]));
        else if (std::strcmp (argv[i], "--live") == 0) { drift = 3.0f; jitter = 10.0f; }
        else if (std::strcmp (argv[i], "--pre") == 0 && i + 1 < argc) preSeconds = std::atof (argv[++i]);
        else if (std::strcmp (argv[i], "--sync") == 0) sync = true;
        else rest.push_back (argv[i]);
    }
    const char* modeName = mode == Listening::ipad ? "IPAD (cassa -> stanza -> microfono)"
                                                   : "MIXER (linea, nessuna stanza)";
    char material[128];
    std::snprintf (material, sizeof material,
                   drift > 0.0f || jitter > 0.0f
                       ? "band dal vivo: deriva %.1f BPM, scarto umano %.0f ms"
                       : "sequencer: tempo perfettamente fisso%.0f%.0f",
                   static_cast<double> (drift), static_cast<double> (jitter));
    const double sr = 48000.0;

    // --trace <style> <bpm> runs one case with a full trace, for looking at how
    // a single track behaves rather than at the aggregate.
    if (trace && rest.size() >= 2)
    {
        SongOptions o;
        const std::string st = rest[0];
        o.syncopated = st == "syncopated" || st == "sync+pad";
        o.sustained = st == "pad" || st == "sync+pad" || st == "half-time";
        o.halfTimeFeel = st == "half-time";
        o.bpm = static_cast<float> (std::atof (rest[1]));
        o.driftBpm = drift;
        o.jitterMs = jitter;
        std::printf ("# %s  |  %s\n", modeName, material);
        const Result r = run (o, sr, static_cast<unsigned> (o.bpm) * 7u + 13u, 0.55f, true, mode,
                              preSeconds, sync);
        std::printf ("\n%s %.0f: lock=%.1f t2=%.1f end=%.2f span=%.2f bars=%d jumps=%d\n",
                     st.c_str(), static_cast<double> (o.bpm), r.tLock, r.t2pct,
                     static_cast<double> (r.bpmEnd), static_cast<double> (r.span),
                     r.barBreaks, r.bigJumps);
        return 0;
    }

    std::vector<SongOptions> styles;
    { SongOptions o; styles.push_back (o); }
    { SongOptions o; o.syncopated = true; styles.push_back (o); }
    { SongOptions o; o.sustained = true; styles.push_back (o); }
    { SongOptions o; o.syncopated = true; o.sustained = true; styles.push_back (o); }
    { SongOptions o; o.halfTimeFeel = true; o.sustained = true; styles.push_back (o); }

    const float tempos[] = { 76.0f, 92.0f, 104.0f, 118.0f, 128.0f, 140.0f };

    std::printf ("# ascolto: %s\n# materiale: %s\n", modeName, material);
    if (preSeconds > 1.5)
        std::printf ("# stanza vuota per %.0f s prima che la band attacchi; t=0 e' la prima battuta\n",
                     preSeconds);
    if (sync)
        std::printf ("# analisi sincronizzata con l'audio a ogni blocco: il giro e' ripetibile\n");
    std::printf ("%-11s %-6s %-7s %-7s %-7s %-7s %-7s %-6s %-6s %-7s %-5s\n",
                 "style", "bpm", "t_val", "t_lock", "t_2%", "err", "span", "jumps", "%fisso", "phase", "rst");

    int nRuns = 0, nOctave = 0, nUnstable = 0, nSlow = 0, totalBars = 0, totalJumps = 0, totalGaps = 0;
    int nLocked = 0, nEarlyLock = 0, totalRestarts = 0, nValid = 0, nFixed = 0, nT2 = 0;
    double validAcc = 0.0, fixedAcc = 0.0, t2Acc = 0.0;
    double spanAcc = 0.0, spanNnAcc = 0.0, wobbleAcc = 0.0, lockAcc = 0.0, errAcc = 0.0;

    for (const auto& style : styles)
    {
        for (float bpm : tempos)
        {
            SongOptions o = style;
            o.bpm = bpm;
            o.driftBpm = drift;
            o.jitterMs = jitter;
            const Result r = run (o, sr, static_cast<unsigned> (bpm) * 7u + 13u, 0.55f, trace, mode,
                                  preSeconds, sync);

            const bool octaveBad = std::fabs (r.octave) > 0.20f;
            const bool unstable = r.span > 1.5f;
            const bool slow = r.t2pct < 0.0 || r.t2pct > 12.0;
            ++nRuns;
            nOctave += octaveBad;
            nUnstable += unstable;
            nSlow += slow;
            totalBars += r.barBreaks;
            totalJumps += r.bigJumps;
            spanAcc += r.span;
            spanNnAcc += r.spanNn;
            wobbleAcc += r.wobble;
            if (r.lockSeen) { lockAcc += r.tLock; ++nLocked; }
            if (r.tValid >= 0.0) { validAcc += r.tValid; ++nValid; }
            if (r.fixedShare >= 0.0f) { fixedAcc += r.fixedShare; ++nFixed; }
            if (r.t2pct >= 0.0) { t2Acc += r.t2pct; ++nT2; }
            nEarlyLock += r.lockSeen && r.tLock < 0.0;

            totalGaps += r.gaps;
            totalRestarts += r.restarts;
            errAcc += r.errMean;
            std::printf ("%-11s %-6.0f %-7.2f %-7.2f %-7.1f %-7.2f %-7.2f %-6d %-6d %-7.3f %-5d %s%s%s\n",
                         styleName (o), static_cast<double> (bpm), r.tValid, r.tLock, r.t2pct,
                         static_cast<double> (r.errMean),
                         static_cast<double> (r.span), r.bigJumps,
                         static_cast<int> (r.fixedShare),
                         static_cast<double> (r.phaseMean), r.restarts,
                         octaveBad ? "OCT " : "", unstable ? "WOBBLE " : "", slow ? "SLOW" : "");
            std::fflush (stdout);
        }
    }

    std::printf ("\n=== %d runs, %s ===\n", nRuns, modeName);
    std::printf ("wrong octave      %d\n", nOctave);
    std::printf ("unstable (>1.5)   %d\n", nUnstable);
    std::printf ("slow (>12s / never) %d\n", nSlow);
    std::printf ("bar restarts      %d\n", totalBars);
    std::printf ("analysis gaps     %d\n", totalGaps);
    std::printf ("ripartenze        %d   <- una a brano e' la band che attacca; di piu' e' il livello che inganna\n",
                 totalRestarts);
    std::printf ("bpm jumps >1      %d\n", totalJumps);
    std::printf ("errore medio      %.2f BPM   <- distanza dal tempo che la band suona davvero\n", errAcc / nRuns);
    std::printf ("mean span         %.2f BPM\n", spanAcc / nRuns);
    std::printf ("mean wobble       %.2f BPM/0.5s\n", wobbleAcc / nRuns);
    std::printf ("mean span (decoder only) %.2f BPM\n", spanNnAcc / nRuns);
    if (preSeconds > 1.5)
        std::printf ("lock sulla stanza vuota %d   <- agganciati prima che qualcuno suonasse\n",
                     nEarlyLock);
    std::printf ("mean t_2%%         %.2f s (%d brani)   <- e ci resta entro il 2%%\n",
                 nT2 > 0 ? t2Acc / nT2 : -1.0, nT2);
    std::printf ("tempo fisso       %.0f%% del tempo a regime\n",
                 nFixed > 0 ? fixedAcc / nFixed : -1.0);
    std::printf ("mean t_val        %.2f s   <- il decoder pubblica un tempo\n",
                 nValid > 0 ? validAcc / nValid : -1.0);
    std::printf ("mean t_lock       %.2f s (%d agganciati)   <- e il tracker lo accetta\n",
                 nLocked > 0 ? lockAcc / nLocked : -1.0, nLocked);
    return 0;
}
