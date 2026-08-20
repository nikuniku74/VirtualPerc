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
//   t_lock     seconds until FOLLOWING
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
};

Result run (const SongOptions& opt, double sr, unsigned seed, float level, bool trace)
{
    const int block = 256;
    const double seconds = 60.0;
    const int n = static_cast<int> (sr * seconds);

    std::vector<float> song (static_cast<size_t> (n), 0.0f);
    renderSong (song, opt, sr, seed);
    speakerRoomMic (song, sr, seed, level);

    vp::VirtualPercussionEngine eng;
    eng.prepare (sr, block, 1);
    eng.settings().followSource.store (static_cast<int> (vp::FollowSource::speaker));
    eng.start();

    std::vector<float> oL (static_cast<size_t> (block), 0.0f);
    std::vector<float> oR (static_cast<size_t> (block), 0.0f);
    float* outs[2] = { oL.data(), oR.data() };

    Result r;
    const double beatsPerSample = static_cast<double> (opt.bpm) / 60.0 / sr;

    float lo = 1.0e9f, hi = 0.0f;
    float nnLo = 1.0e9f, nnHi = 0.0f;
    float prevBpm = 0.0f;
    double lastSampleT = -1.0;
    double dbpmAcc = 0.0;
    int    dbpmN = 0;
    double phaseAcc = 0.0;
    int    phaseN = 0;
    int    prevBeatInBar = -1;
    double settleAt = -1.0;

    int pos = 0, blocks = 0;
    while (pos + block <= n)
    {
        const float* ins[1] = { song.data() + pos };
        eng.process (ins, 1, outs, 2, block);
        const auto snap = eng.snapshot();
        const double t = static_cast<double> (pos) / sr;

        if (r.tLock < 0.0 && snap.state == vp::TrackingState::following)
            r.tLock = t;

        if (snap.bpm > 40.0f)
        {
            const bool close = std::fabs (snap.bpm - opt.bpm) / opt.bpm < 0.02f;
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

            const double truePhase = static_cast<double> (pos) * beatsPerSample;
            const float err = vp::wrapCentered (
                snap.beatPhase - static_cast<float> (truePhase - std::floor (truePhase)));
            phaseAcc += std::fabs (err);
            ++phaseN;
            r.phaseWorst = std::max (r.phaseWorst, std::fabs (err));

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
        if ((++blocks % 8) == 0)
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }

    r.span = (hi >= lo) ? hi - lo : 0.0f;
    r.spanNn = (nnHi >= nnLo) ? nnHi - nnLo : 0.0f;
    r.wobble = dbpmN > 0 ? static_cast<float> (dbpmAcc / dbpmN) : 0.0f;
    r.octave = r.bpmEnd > 1.0f ? std::log2 (r.bpmEnd / opt.bpm) : 0.0f;
    r.phaseMean = phaseN > 0 ? static_cast<float> (phaseAcc / phaseN) : 0.0f;
    return r;
}

} // namespace

int main (int argc, char** argv)
{
    const bool trace = argc > 1 && std::strcmp (argv[1], "--trace") == 0;
    const double sr = 48000.0;

    // --trace <style> <bpm> runs one case with a full trace, for looking at how
    // a single track behaves rather than at the aggregate.
    if (trace && argc >= 4)
    {
        SongOptions o;
        const std::string st = argv[2];
        o.syncopated = st == "syncopated" || st == "sync+pad";
        o.sustained = st == "pad" || st == "sync+pad" || st == "half-time";
        o.halfTimeFeel = st == "half-time";
        o.bpm = static_cast<float> (std::atof (argv[3]));
        const Result r = run (o, sr, static_cast<unsigned> (o.bpm) * 7u + 13u, 0.55f, true);
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

    std::printf ("%-11s %-6s %-7s %-7s %-8s %-7s %-6s %-6s %-6s %-7s %-5s\n",
                 "style", "bpm", "t_lock", "t_2%", "bpm_end", "span", "spanNN", "jumps", "bars", "phase", "gaps");

    int nRuns = 0, nOctave = 0, nUnstable = 0, nSlow = 0, totalBars = 0, totalJumps = 0, totalGaps = 0;
    double spanAcc = 0.0, spanNnAcc = 0.0, wobbleAcc = 0.0, lockAcc = 0.0;

    for (const auto& style : styles)
    {
        for (float bpm : tempos)
        {
            SongOptions o = style;
            o.bpm = bpm;
            const Result r = run (o, sr, static_cast<unsigned> (bpm) * 7u + 13u, 0.55f, trace);

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
            if (r.tLock >= 0.0) lockAcc += r.tLock;

            totalGaps += r.gaps;
            std::printf ("%-11s %-6.0f %-7.1f %-7.1f %-8.2f %-7.2f %-6.2f %-6d %-6d %-7.3f %-5d %s%s%s\n",
                         styleName (o), static_cast<double> (bpm), r.tLock, r.t2pct,
                         static_cast<double> (r.bpmEnd), static_cast<double> (r.span),
                         static_cast<double> (r.spanNn), r.bigJumps, r.barBreaks,
                         static_cast<double> (r.phaseMean), r.gaps,
                         octaveBad ? "OCT " : "", unstable ? "WOBBLE " : "", slow ? "SLOW" : "");
            std::fflush (stdout);
        }
    }

    std::printf ("\n=== %d runs ===\n", nRuns);
    std::printf ("wrong octave      %d\n", nOctave);
    std::printf ("unstable (>1.5)   %d\n", nUnstable);
    std::printf ("slow (>12s / never) %d\n", nSlow);
    std::printf ("bar restarts      %d\n", totalBars);
    std::printf ("analysis gaps     %d\n", totalGaps);
    std::printf ("bpm jumps >1      %d\n", totalJumps);
    std::printf ("mean span         %.2f BPM\n", spanAcc / nRuns);
    std::printf ("mean wobble       %.2f BPM/0.5s\n", wobbleAcc / nRuns);
    std::printf ("mean span (decoder only) %.2f BPM\n", spanNnAcc / nRuns);
    std::printf ("mean t_lock       %.2f s\n", lockAcc / nRuns);
    return 0;
}
