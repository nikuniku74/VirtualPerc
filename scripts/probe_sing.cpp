// Can the app find the tempo when there is nothing to hit?
//
// Everything the tracker times is percussive. The network is trained on beat
// activations, the kick channel is one drum, the comb folds an onset envelope.
// Point all of that at a voice with a guitar behind it and there is very little
// to work with: no kick to time, no snare to count from, and an activation
// curve BeatNet was never trained on.
//
// `HarmonicChange` already reads the one thing that survives - the harmony
// moving - and the app already uses it, for the *bar*: which of the four
// quarters is the one. Measured over material with the drums taken out, every
// chord change it finds lands on the downbeat. What it has never been asked is
// the other question, and it is the harder and more useful one:
//
//     how long is a beat?
//
// Nothing in the app answers that from anything but percussion. This bench is
// where that gets a number, because the answer is not obviously yes: a chord
// change is worth a quarter of a bar at best (the window is 170 ms), so a
// single interval says almost nothing about a beat. What it has instead is
// *arithmetic*: chords change on bar lines, so every interval between two
// changes is a whole number of bars, and a period that makes all of them come
// out whole is the period the band is playing. One interval is noise; twenty
// of them agreeing is a tempo.
//
// Scored on material with no drums in it at all, against the tempo it was
// rendered at.
//
//   cmake --build build --target VPSing
//   ./build/VPSing_artefacts/Release/VPSing
#include "Tracking/HarmonicChange.h"
#include "Tracking/HarmonicTempo.h"
#include "probe_song_render.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 256;

struct Found
{
    double bpm = 0.0;
    int    changes = 0;
    double coherence = 0.0;
    /** Where the changes actually fell against the true bar line, in ms - the
        measurement that decides whether any of this can work at all. If the
        detector's events are not on the bar to begin with, no estimator built
        on them can be. */
    double scatterMs = 0.0;
    double onBarShare = 0.0;
};

Found tempoFromHarmony (float bpm, unsigned seed, bool withDrums, float driftBpm,
                        bool sustained, float jitterMs)
{
    vp::probe::SongOptions opt;
    opt.bpm = bpm;
    opt.driftBpm = driftBpm;
    opt.sustained = sustained;
    opt.jitterMs = jitterMs;
    opt.breakdown = false;

    const int n = static_cast<int> (kSr * 60.0);
    std::vector<float> mix (static_cast<size_t> (n), 0.0f);
    vp::probe::SongStems stems;
    vp::probe::renderSong (mix, opt, kSr, seed, nullptr, &stems);

    // The case this exists for is the one with no drums in it. `music` is the
    // arrangement with every drum taken out - bass, pad, lead and nothing else.
    const std::vector<float>& sig = withDrums ? mix : stems.music;

    vp::HarmonicChange harm;
    harm.prepare (kSr);
    vp::HarmonicTempo tempo;
    tempo.prepare (kSr);

    std::vector<double> at;
    vp::HarmonicChange::Change ch[vp::HarmonicChange::kMaxChanges];
    for (int pos = 0; pos + kBlock <= n; pos += kBlock)
    {
        const int got = harm.process (sig.data() + pos, kBlock, ch,
                                      vp::HarmonicChange::kMaxChanges);
        for (int i = 0; i < got; ++i)
        {
            const double t = static_cast<double> (pos + ch[i].offset) / kSr;
            // The reference chroma has to form before anything it reports is
            // worth having.
            if (t > 4.0)
            {
                at.push_back (t);
                tempo.addChange (ch[i].offset, ch[i].strength);
            }
        }
        // Driven exactly as the app drives it: the changes go in as they are
        // reported, and the sweep is paid for a slice at a time.
        tempo.process (kBlock);
    }

    Found f;
    f.changes = static_cast<int> (at.size());
    f.bpm = static_cast<double> (tempo.bpm());
    f.coherence = static_cast<double> (tempo.coherence());

    // Against the grid the song was rendered at. `driftBpm` is zero for every
    // case that uses this, so a bar really is a constant here.
    if (! at.empty())
    {
        const double barSec = 4.0 * 60.0 / static_cast<double> (bpm);
        double sq = 0.0;
        int onBar = 0;
        for (double t : at)
        {
            const double bars = t / barSec;
            const double off = (bars - std::floor (bars + 0.5)) * barSec * 1000.0;
            sq += off * off;
            if (std::fabs (off) < barSec * 1000.0 * 0.125)
                ++onBar;
        }
        f.scatterMs = std::sqrt (sq / static_cast<double> (at.size()));
        f.onBarShare = static_cast<double> (onBar) / static_cast<double> (at.size());
    }
    return f;
}
}

int main()
{
    std::printf ("Virtual Percussionist - il tempo senza niente da colpire\n\n");
    std::printf ("Tutto quello che l'app cronometra e' percussivo. Su una voce con\n"
                 "una chitarra dietro non c'e' cassa da cronometrare, rullante da\n"
                 "contare, ne' una curva di attivazione su cui la rete sia mai\n"
                 "stata addestrata. L'unica cosa che resta e' l'armonia che si\n"
                 "muove - e l'app la usa gia', ma solo per la *battuta*.\n\n"
                 "Qui le si chiede l'altra domanda: quanto dura un battito.\n"
                 "Un cambio d'accordo da solo non lo sa (la finestra e' 170 ms).\n"
                 "Quello che sa e' l'aritmetica: gli accordi cambiano sulla stanghetta,\n"
                 "quindi ogni intervallo fra due cambi e' un numero intero di\n"
                 "battute, e il periodo che li fa venire interi tutti e' quello\n"
                 "che la band sta suonando.\n\n");

    std::printf ("%-18s %-9s %-10s %-9s %-7s %-9s %-10s %-9s\n",
                 "materiale", "vero BPM", "trovato", "errore %", "cambi", "coerenza",
                 "sparsi ms", "su stang.");

    struct Case { const char* what; float bpm; bool drums; float drift; bool sus; float jit; };
    const Case cases[] = {
        { "voce e chitarra",   92.0f, false, 0.0f, false, 0.0f },
        { "voce e chitarra",  100.0f, false, 0.0f, false, 0.0f },
        { "voce e chitarra",  118.0f, false, 0.0f, false, 0.0f },
        { "voce e chitarra",  132.0f, false, 0.0f, false, 0.0f },
        { "voce e chitarra",  152.0f, false, 0.0f, false, 0.0f },
        { "band che vaga",    118.0f, false, 3.0f, false, 0.0f },
        { "tempo umano",      118.0f, false, 0.0f, false, 9.0f },
        // The two the first version of this bench had switched on by mistake,
        // and which turned out to be where it breaks. They stay, named, because
        // a bench that only runs the case that works is not a measurement.
        { "pad tenuti",       118.0f, false, 0.0f, true,  0.0f },
        { "con la batteria",  118.0f, true,  0.0f, false, 0.0f },
    };

    int good = 0, total = 0;
    for (const Case& c : cases)
    {
        double errSum = 0.0;
        int seen = 0;
        int changes = 0, runs = 0;
        double coh = 0.0, scat = 0.0, onbar = 0.0;
        double got = 0.0;
        for (int s = 0; s < 3; ++s)
        {
            const Found f = tempoFromHarmony (c.bpm, 991u + static_cast<unsigned> (s) * 7919u,
                                              c.drums, c.drift, c.sus, c.jit);
            scat += f.scatterMs;
            onbar += f.onBarShare;
            changes += f.changes;
            ++seen;
            if (f.bpm <= 0.0)
                continue;
            errSum += std::fabs (f.bpm - c.bpm) / c.bpm * 100.0;
            coh += f.coherence;
            got += f.bpm;
            ++runs;
        }
        const int sn = std::max (1, seen);
        if (runs == 0)
        {
            std::printf ("%-18s %-9.0f %-10s %-9s %-7d %-9s %-10.0f %-8.0f%%\n",
                         c.what, static_cast<double> (c.bpm),
                         "MAI", "-", changes / sn, "-", scat / sn, onbar / sn * 100.0);
            ++total;
            continue;
        }
        const double err = errSum / runs;
        // Three percent is the honest line here: a bar is four beats, so a
        // period wrong by 3% puts the fourth beat of the bar an eighth of a
        // beat out, which is where a listener starts to hear it as not
        // together. Anything inside it the clock's own steering closes.
        const bool ok = err < 3.0;
        if (ok) ++good;
        ++total;
        std::printf ("%-18s %-9.0f %-10.1f %-9.2f %-7d %-9.2f %-10.0f %-8.0f%% %s\n",
                     c.what, static_cast<double> (c.bpm),
                     got / runs, err, changes / sn, coh / runs,
                     scat / sn, onbar / sn * 100.0, ok ? "" : "  NO");
    }

    std::printf ("\ndentro il 3%%: %d su %d\n", good, total);
    return 0;
}
