// What a user's hands do to the clock.
//
// Every other bench here sets the engine up and leaves it alone. That is not
// how it is used: the part gets turned on, a volume moves, a style changes -
// and the reported complaint is that the shaker slides off the song when it
// happens, on the iPad speaker path, where whatever the app plays comes out of
// the same speaker as the music and back into the same microphone.
//
// The measurement is a difference of differences, and it has to be.
//
// A first version compared the phase before an operation against the phase
// after it, and read the result as the operation's doing. It is not: the
// tracker's own phase wanders ten to twenty milliseconds against this material
// on its own, which is the same size as the thing being looked for - and the
// default arrangement drops the drums for four bars in sixteen, which took the
// phase out by 254 ms in a window where nothing had been touched. So the song
// holds still here, and the whole timeline is run **twice**: once performing
// the operations and once performing none of them. What the operation did is
// what is left after subtracting the run that did nothing.
#include "Audio/VirtualPercussionEngine.h"

#include "probe_song_render.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

namespace
{
using namespace vp::probe;

struct Op
{
    const char* name;
    std::function<void (vp::VirtualPercussionEngine&)> apply;
};

struct Window
{
    double sumMs = 0.0;
    double worstMs = 0.0;
    int    n = 0;
    double mean() const { return n > 0 ? sumMs / n : -1.0; }
};

struct RunResult
{
    std::vector<Window> after;
    std::vector<int> restarts, gaps;
    double lockedAt = -1.0;
    int strokes = 0;
};

// Phase error in milliseconds, against the beat the renderer actually played.
double errorMs (float reported, double truePhase, double beatSec)
{
    const float want = static_cast<float> (truePhase - std::floor (truePhase));
    return static_cast<double> (vp::wrapCentered (reported - want)) * beatSec * 1000.0;
}

struct Timeline
{
    double lead = 22.0;      // lock, come in, and settle before touching anything
    double spacing = 10.0;   // one operation every
    double settle = 2.5;     // ignore this much after each, then measure
    double window = 6.0;
};

// `only` < 0 applies every operation on its timeline; >= 0 applies just that
// one and leaves the rest alone; a control pass applies none.
RunResult drive (const std::vector<Op>& ops, bool applyOps, bool mixer,
                 float leakGain, float bpm, const Timeline& tl, int only = -1,
                 bool silentPart = false)
{
    const double sr = 48000.0;
    const int block = 256;
    const double total = tl.lead + tl.spacing * static_cast<double> (ops.size()) + 4.0;
    const int n = static_cast<int> (sr * total) / block * block;
    const unsigned seed = static_cast<unsigned> (bpm) * 7u + 13u;

    SongOptions o;
    o.bpm = bpm;
    // Uniform material on purpose - see the note at the top of this file.
    o.breakdown = false;
    o.fills = false;

    std::vector<float> song (static_cast<size_t> (n), 0.0f);
    std::vector<double> truePhase;
    renderSong (song, o, sr, seed, &truePhase);
    truePhase.resize (static_cast<size_t> (n), 0.0);
    // The input has to *start*, or the percussion is held out by design.
    for (int i = 0; i < static_cast<int> (sr * 1.0) && i < n; ++i)
        song[static_cast<size_t> (i)] *= 0.02f;
    if (! mixer)
        speakerRoomMic (song, sr, seed, 0.55f);

    vp::VirtualPercussionEngine eng;
    eng.prepare (sr, block, 1);
    eng.settings().followSource.store (static_cast<int> (
        mixer ? vp::FollowSource::kitMic : vp::FollowSource::speaker));
    eng.settings().humanization.store (0.0f);
    eng.settings().swing.store (0.0f);
    if (silentPart)
    {
        // Nothing of ours in the room, and nothing of ours for the canceller to
        // subtract either. The difference against a run with the part playing
        // is what the app costs the tracker by existing.
        eng.settings().shakerEnabled.store (false);
        eng.settings().congasEnabled.store (false);
    }
    eng.start();

    // Our own output, back through the air a few milliseconds later. This is
    // the whole point of the speaker path: the app is one of the things its own
    // microphone hears, and an operation changes what that is.
    const int acoustic = static_cast<int> (sr * 0.007);
    std::vector<float> echo (static_cast<size_t> (n + block + acoustic + 1), 0.0f);

    std::vector<float> in (static_cast<size_t> (block), 0.0f);
    std::vector<float> oL (static_cast<size_t> (block), 0.0f);
    std::vector<float> oR (static_cast<size_t> (block), 0.0f);
    float* outs[2] = { oL.data(), oR.data() };

    const double beatSec = 60.0 / static_cast<double> (bpm);
    RunResult r;
    r.after.resize (ops.size());
    r.restarts.assign (ops.size(), 0);
    r.gaps.assign (ops.size(), 0);
    int nextOp = 0;
    vp::EngineSnapshot snap {};

    for (int pos = 0; pos + block <= n; pos += block)
    {
        for (int i = 0; i < block; ++i)
            in[static_cast<size_t> (i)] = song[static_cast<size_t> (pos + i)]
                                          + echo[static_cast<size_t> (pos + i)];
        const float* ins[1] = { in.data() };
        eng.process (ins, 1, outs, 2, block);

        for (int i = 0; i < block; ++i)
        {
            const size_t at = static_cast<size_t> (pos + block + acoustic + i);
            if (at < echo.size())
                echo[at] += leakGain * 0.5f * (oL[static_cast<size_t> (i)]
                                               + oR[static_cast<size_t> (i)]);
        }

        // Repeatable: let the analysis finish this block before feeding more.
        const int hop = static_cast<int> (std::ceil (441.0 * sr / 22050.0));
        while (eng.analysisBacklog() > hop)
            ;

        snap = eng.snapshot();
        const double t = static_cast<double> (pos) / sr;
        if (r.lockedAt < 0.0 && snap.state == vp::TrackingState::following)
            r.lockedAt = t;

        // On a fixed clock, so both passes do the same thing at the same moment
        // whatever the tracker happens to be doing.
        if (nextOp < static_cast<int> (ops.size())
            && t >= tl.lead + tl.spacing * static_cast<double> (nextOp))
        {
            if (applyOps && (only < 0 || only == nextOp))
                ops[static_cast<size_t> (nextOp)].apply (eng);
            r.restarts[static_cast<size_t> (nextOp)] = snap.analysisRestarts;
            r.gaps[static_cast<size_t> (nextOp)] = snap.analysisGaps;
            ++nextOp;
        }

        if (snap.bpm > 40.0f)
        {
            const double e = std::fabs (errorMs (snap.beatPhase,
                                                 truePhase[static_cast<size_t> (pos)], beatSec));
            for (size_t k = 0; k < ops.size(); ++k)
            {
                const double at = tl.lead + tl.spacing * static_cast<double> (k);
                if (t > at + tl.settle && t <= at + tl.settle + tl.window)
                {
                    r.after[k].sumMs += e;
                    r.after[k].worstMs = std::max (r.after[k].worstMs, e);
                    ++r.after[k].n;
                }
            }
        }
    }
    r.strokes = eng.shakerHits();
    return r;
}
} // namespace

namespace
{
// The other half of the complaint, and the larger one.
//
// A record is not uniform: the drums drop for four bars, a verse goes quiet, an
// intro has no kit at all. A percussionist keeps time through that; the
// question is whether this one does, and what it does when the kit comes back.
// Same song as above with the arrangement's own breakdown left in, and the
// phase reported per bar so the shape is visible rather than averaged away.
int material (bool mixer, float leakGain, float bpm)
{
    const double sr = 48000.0;
    const int block = 256;
    const double total = 75.0;
    const int n = static_cast<int> (sr * total) / block * block;
    const unsigned seed = static_cast<unsigned> (bpm) * 7u + 13u;

    SongOptions o;
    o.bpm = bpm;
    o.breakdown = true;   // the point of this run
    o.fills = true;

    std::vector<float> song (static_cast<size_t> (n), 0.0f);
    std::vector<double> truePhase;
    renderSong (song, o, sr, seed, &truePhase);
    truePhase.resize (static_cast<size_t> (n), 0.0);
    for (int i = 0; i < static_cast<int> (sr * 1.0) && i < n; ++i)
        song[static_cast<size_t> (i)] *= 0.02f;
    if (! mixer)
        speakerRoomMic (song, sr, seed, 0.55f);

    vp::VirtualPercussionEngine eng;
    eng.prepare (sr, block, 1);
    eng.settings().followSource.store (static_cast<int> (
        mixer ? vp::FollowSource::kitMic : vp::FollowSource::speaker));
    eng.settings().humanization.store (0.0f);
    eng.settings().swing.store (0.0f);
    eng.start();

    const int acoustic = static_cast<int> (sr * 0.007);
    std::vector<float> echo (static_cast<size_t> (n + block + acoustic + 1), 0.0f);
    std::vector<float> in (static_cast<size_t> (block), 0.0f);
    std::vector<float> oL (static_cast<size_t> (block), 0.0f), oR (static_cast<size_t> (block), 0.0f);
    float* outs[2] = { oL.data(), oR.data() };

    const double beatSec = 60.0 / static_cast<double> (bpm);
    const double barSec = beatSec * 4.0;
    std::vector<double> barSum, barWorst, barBpm, barConf, barRes, barCov;
    std::vector<int> barN, barRegime;

    for (int pos = 0; pos + block <= n; pos += block)
    {
        for (int i = 0; i < block; ++i)
            in[static_cast<size_t> (i)] = song[static_cast<size_t> (pos + i)]
                                          + echo[static_cast<size_t> (pos + i)];
        const float* ins[1] = { in.data() };
        eng.process (ins, 1, outs, 2, block);
        for (int i = 0; i < block; ++i)
        {
            const size_t at = static_cast<size_t> (pos + block + acoustic + i);
            if (at < echo.size())
                echo[at] += leakGain * 0.5f * (oL[static_cast<size_t> (i)]
                                               + oR[static_cast<size_t> (i)]);
        }
        const int hop = static_cast<int> (std::ceil (441.0 * sr / 22050.0));
        while (eng.analysisBacklog() > hop)
            ;

        const auto snap = eng.snapshot();
        const double t = static_cast<double> (pos) / sr;
        const int bar = static_cast<int> (t / barSec);
        if (static_cast<int> (barN.size()) <= bar)
        {
            barSum.resize (static_cast<size_t> (bar) + 1, 0.0);
            barWorst.resize (static_cast<size_t> (bar) + 1, 0.0);
            barBpm.resize (static_cast<size_t> (bar) + 1, 0.0);
            barConf.resize (static_cast<size_t> (bar) + 1, 0.0);
            barRes.resize (static_cast<size_t> (bar) + 1, 0.0);
            barCov.resize (static_cast<size_t> (bar) + 1, 0.0);
            barRegime.resize (static_cast<size_t> (bar) + 1, 0);
            barN.resize (static_cast<size_t> (bar) + 1, 0);
        }
        if (snap.bpm > 40.0f && t > 12.0)
        {
            const double e = std::fabs (errorMs (snap.beatPhase,
                                                 truePhase[static_cast<size_t> (pos)], beatSec));
            barSum[static_cast<size_t> (bar)] += e;
            barWorst[static_cast<size_t> (bar)] = std::max (barWorst[static_cast<size_t> (bar)], e);
            barBpm[static_cast<size_t> (bar)] += snap.bpm;
            barConf[static_cast<size_t> (bar)] += snap.confidence;
            barRes[static_cast<size_t> (bar)] += snap.fitResidual;
            barCov[static_cast<size_t> (bar)] += snap.fitCoverage;
            barRegime[static_cast<size_t> (bar)] = snap.tempoRegime;
            ++barN[static_cast<size_t> (bar)];
        }
    }

    std::printf ("# %s, %.0f BPM, rientro %.2f, arrangiamento con i suoi buchi\n",
                 mixer ? "MIXER (linea)" : "IPAD (cassa -> stanza -> microfono)",
                 static_cast<double> (bpm), static_cast<double> (leakGain));
    std::printf ("# fase contro la battuta suonata, per battuta, in ms\n");
    std::printf ("# la batteria esce nelle battute 8-11 di ogni 16\n\n");
    std::printf ("%-5s %-7s %9s %9s %9s %6s %8s %7s %7s\n",
                 "bat.", "kit", "media", "peggio", "bpm", "conf", "regime",
                 "resid", "coper");
    double inSum = 0.0, outSum = 0.0, backSum = 0.0;
    int inN = 0, outN = 0, backN = 0;
    double worstOut = 0.0;
    for (size_t b = 0; b < barN.size(); ++b)
    {
        if (barN[b] == 0)
            continue;
        const int phase16 = static_cast<int> (b) % 16;
        const bool drumsOut = phase16 >= 8 && phase16 < 12;
        const bool justBack = phase16 >= 12 && phase16 < 14;
        const double m = barSum[b] / barN[b];
        if (drumsOut) { outSum += m; ++outN; worstOut = std::max (worstOut, barWorst[b]); }
        else if (justBack) { backSum += m; ++backN; }
        else { inSum += m; ++inN; }
        std::printf ("%-5zu %-7s %9.2f %9.2f %9.3f %6.2f %8s %7.3f %7.2f%s\n", b,
                     drumsOut ? "fuori" : (justBack ? "rientra" : "dentro"),
                     m, barWorst[b], barBpm[b] / barN[b], barConf[b] / barN[b],
                     vp::regimeLabel (barRegime[b]),
                     barRes[b] / barN[b], barCov[b] / barN[b],
                     barWorst[b] > 40.0 ? "   <--" : "");
    }
    std::printf ("\nkit dentro   %7.2f ms\nkit fuori    %7.2f ms (peggio %.2f)\nappena torna %7.2f ms\n",
                 inN > 0 ? inSum / inN : -1.0,
                 outN > 0 ? outSum / outN : -1.0, worstOut,
                 backN > 0 ? backSum / backN : -1.0);
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    float leakGain = 0.55f;
    float bpm = 118.0f;
    bool mixer = false;
    bool cumulativeMode = false;
    bool silentPartMode = false;
    bool materialMode = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--mixer") == 0) mixer = true;
        else if (std::strcmp (argv[i], "--cumulative") == 0) cumulativeMode = true;
        else if (std::strcmp (argv[i], "--silentpart") == 0) silentPartMode = true;
        else if (std::strcmp (argv[i], "--material") == 0) materialMode = true;
        else if (std::strcmp (argv[i], "--leak") == 0 && i + 1 < argc)
            leakGain = static_cast<float> (std::atof (argv[++i]));
        else if (std::strcmp (argv[i], "--bpm") == 0 && i + 1 < argc)
            bpm = static_cast<float> (std::atof (argv[++i]));
    }

    const std::vector<Op> ops = {
        { "niente (controllo)",     [] (vp::VirtualPercussionEngine&) {} },
        { "master 0.90 -> 0.40",    [] (auto& e) { e.settings().masterVolume.store (0.40f); } },
        { "master 0.40 -> 1.00",    [] (auto& e) { e.settings().masterVolume.store (1.00f); } },
        { "percussione 1.0 -> 0.4", [] (auto& e) { e.settings().percussionVolume.store (0.40f); } },
        { "percussione 0.4 -> 1.0", [] (auto& e) { e.settings().percussionVolume.store (1.00f); } },
        { "congas off",             [] (auto& e) { e.settings().congasEnabled.store (false); } },
        { "congas on",              [] (auto& e) { e.settings().congasEnabled.store (true); } },
        { "riverbero 0.3 -> 0.9",   [] (auto& e) { e.settings().reverbAmount.store (0.90f); } },
        { "intensita' 0.5 -> 0.9",  [] (auto& e) { e.settings().intensity.store (0.90f); } },
        { "suddivisione 1/16",      [] (auto& e) { e.settings().subdivision.store (static_cast<int> (vp::Subdivision::sixteenth)); } },
        { "stile -> rock",          [] (auto& e) { e.settings().grooveStyle.store (static_cast<int> (vp::GrooveStyle::rock)); } },
        { "inseguimento -> low",    [] (auto& e) { e.settings().followStrength.store (static_cast<int> (vp::FollowStrength::low)); } },
        { "guadagno ingresso 1.5",  [] (auto& e) { e.settings().inputGain.store (1.5f); } },
        { "guadagno ingresso 1.0",  [] (auto& e) { e.settings().inputGain.store (1.0f); } },
    };

    if (materialMode)
        return material (mixer, leakGain, bpm);

    const Timeline tl;
    if (silentPartMode)
    {
        // Two control passes, one with the part playing and one without, and
        // nothing else touched. Any difference is the app disturbing its own
        // analysis - which at leak 0 it has no honest way of doing.
        const RunResult on = drive (ops, false, mixer, leakGain, bpm, tl, -1, false);
        const RunResult off = drive (ops, false, mixer, leakGain, bpm, tl, -1, true);
        std::printf ("# %s, %.0f BPM, rientro %.2f\n",
                     mixer ? "MIXER (linea)" : "IPAD (cassa -> stanza -> microfono)",
                     static_cast<double> (bpm), static_cast<double> (leakGain));
        std::printf ("# fase contro la battuta suonata, in ms, senza toccare niente\n\n");
        std::printf ("%-10s %10s %10s | %10s %10s | %s\n",
                     "finestra", "parte on", "peggio", "parte muta", "peggio", "differenza");
        double sum = 0.0; int cnt = 0;
        for (size_t k = 0; k < ops.size(); ++k)
        {
            const double a = on.after[k].mean(), b = off.after[k].mean();
            if (a < 0.0 || b < 0.0) continue;
            sum += a - b; ++cnt;
            std::printf ("%-10zu %10.2f %10.2f | %10.2f %10.2f | %+9.2f\n",
                         k, a, on.after[k].worstMs, b, off.after[k].worstMs, a - b);
        }
        std::printf ("\nmedia della differenza: %+.2f ms\n", cnt > 0 ? sum / cnt : 0.0);
        return 0;
    }

    const RunResult without = drive (ops, false, mixer, leakGain, bpm, tl);

    // One pass per operation, each applying only its own, so a row is that
    // operation and not the pile of everything before it. Cumulative mode is
    // still there behind --cumulative, because "I changed four things and it
    // went off" is also a thing that happens.
    std::vector<RunResult> isolated;
    RunResult cumulative;
    if (cumulativeMode)
        cumulative = drive (ops, true, mixer, leakGain, bpm, tl);
    else
        for (size_t k = 0; k < ops.size(); ++k)
            isolated.push_back (drive (ops, true, mixer, leakGain, bpm, tl,
                                       static_cast<int> (k)));
    const RunResult& with = cumulativeMode ? cumulative : isolated.front();

    std::printf ("# %s, %.0f BPM, rientro %.2f\n",
                 mixer ? "MIXER (linea)" : "IPAD (cassa -> stanza -> microfono)",
                 static_cast<double> (bpm), static_cast<double> (leakGain));
    std::printf ("# %s; aggancio a %.1f s, colpi %d; la colonna che conta e' l'ultima\n",
                 cumulativeMode ? "operazioni cumulative"
                                : "una passata per operazione, piu' un controllo",
                 with.lockedAt, with.strokes);
    std::printf ("# fase contro la battuta suonata, in millisecondi\n\n");
    std::printf ("%-26s %9s %9s | %9s %9s | %s\n",
                 "operazione", "con", "peggio", "controllo", "peggio", "differenza");

    double worst = 0.0;
    const char* worstName = "-";
    for (size_t k = 0; k < ops.size(); ++k)
    {
        const RunResult& run = cumulativeMode ? cumulative : isolated[k];
        const double a = run.after[k].mean();
        const double b = without.after[k].mean();
        const double d = (a >= 0.0 && b >= 0.0) ? a - b : 0.0;
        if (d > worst) { worst = d; worstName = ops[k].name; }
        std::printf ("%-26s %9.2f %9.2f | %9.2f %9.2f | %+9.2f%s\n",
                     ops[k].name, a, run.after[k].worstMs, b, without.after[k].worstMs,
                     d, d > 3.0 ? "  <-- l'operazione sposta" : "");
    }
    std::printf ("\npeggiore: %s, %+.2f ms\n", worstName, worst);
    if (with.gaps.back() != 0 || without.gaps.back() != 0)
        std::printf ("ATTENZIONE: l'analisi ha perso audio (gap %d / %d)\n",
                     with.gaps.back(), without.gaps.back());
    return 0;
}
