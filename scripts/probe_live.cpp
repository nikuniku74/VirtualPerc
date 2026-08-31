// The app against a real recording, with a real answer to score it on.
//
// Everything else in scripts/ measures the tracker on music this repository
// generated: `probe_song_render.h` writes a kick where a kick goes, a chord
// change on the bar line, and hands the bench the notated grid for free. That
// is what makes those numbers repeatable, and it is also their limit. A
// rendered kick is an impulse the network was never going to miss. A real one
// is a drummer, a room, a desk, a compressor, and the guitarist's amp bleeding
// into the overheads.
//
// So none of those benches can answer the only question that finally matters -
// does it follow *this band* - and neither can a listener, because "it felt
// late" is not a number and cannot be compared against last week's build.
//
// This one takes a recording and a truth, and reports the same phase error in
// milliseconds that VPAlign reports, so the two are directly comparable.
//
// **The truth.** Two ways, and the first is much better:
//
//   --click click.wav   a click track recorded alongside the band. If they
//                       played to a click, this is the answer, to the sample,
//                       and it costs nothing to keep: record the click output
//                       to its own track.
//   --bpm 118           a constant tempo, when the take really is to a grid
//                       and no click was kept. Beat one is taken from the
//                       first transient in the mix, which is a guess - so an
//                       offset in this mode is not evidence, only the *drift*
//                       across the take is.
//
// **The network.** On a host with ONNX Runtime this drives the real BeatNet,
// which is the whole point: the stub is a placeholder and its activations are
// not the ones the app sees on stage. The header line says which one ran, and
// it is the first thing to check before believing any number below it.
//
//   ./scripts/fetch_onnxruntime.sh --host    (once)
//   cmake -S . -B build && cmake --build build --target VPLive
//   ./build/VPLive_artefacts/Release/VPLive --mix band.wav --click click.wav
#include "Tracking/BeatTracker.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <thread>
#include <vector>

namespace
{
constexpr int kBlock = 512;

/** Reads a file to mono at its own rate. Returns false and says why on
    failure, because a bench that silently measures nothing is worse than one
    that stops. */
bool readMono (const juce::File& f, std::vector<float>& out, double& sr)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr)
    {
        std::printf ("non riesco a leggere %s\n", f.getFullPathName().toRawUTF8());
        return false;
    }
    sr = r->sampleRate;
    const int n = static_cast<int> (r->lengthInSamples);
    juce::AudioBuffer<float> buf (static_cast<int> (r->numChannels), n);
    r->read (&buf, 0, n, 0, true, true);
    out.assign (static_cast<size_t> (n), 0.0f);
    for (int c = 0; c < buf.getNumChannels(); ++c)
    {
        const float* p = buf.getReadPointer (c);
        for (int i = 0; i < n; ++i)
            out[static_cast<size_t> (i)] += p[i];
    }
    const float g = buf.getNumChannels() > 0 ? 1.0f / static_cast<float> (buf.getNumChannels()) : 1.0f;
    for (float& v : out)
        v *= g;
    return true;
}

/** Where the band actually played, taken from the recording itself.
 
    This is the mode that matters, and the reason is musical rather than
    technical. A band without a click breathes: it pushes into a chorus and
    leans back out of one, and a percussionist follows *that*, not a grid. If
    the drummer is 15 ms early, the right place to be is 15 ms early with him.
    So scoring the app against a metronome would be scoring it against something
    no player in the room is playing - the reference has to be the band.

    Which is already in the recording. And it can be read far better here than
    on stage, because this is not live: the whole file is available, so an
    attack can be timed by looking at what comes *after* it, which the tracker
    can never do.

    Deliberately every attack rather than only the beats. Which of them are
    beats is the question the app is being scored on, so deciding it here would
    be marking its own homework. What is measured instead is whether the app's
    sixteenth grid lands where the band's strokes land - and against a grid
    every stroke has an opinion, whichever subdivision it falls on. */
std::vector<double> onsetTimes (const std::vector<float>& x, double sr)
{
    std::vector<double> at;
    if (x.size() < 1024)
        return at;

    // Envelope, then rise. Crude on purpose: a band's attacks are the loudest
    // thing in the file and anything cleverer starts deciding what is a beat.
    const int hop = 256;
    const int n = static_cast<int> (x.size());
    std::vector<float> env;
    env.reserve (static_cast<size_t> (n / hop + 1));
    for (int i = 0; i + hop <= n; i += hop)
    {
        float peak = 0.0f;
        for (int k = 0; k < hop; ++k)
            peak = std::max (peak, std::fabs (x[static_cast<size_t> (i + k)]));
        env.push_back (peak);
    }
    if (env.size() < 8)
        return at;

    // Positive difference, and a floor from the whole file rather than a fixed
    // number - a quiet take and a loud one are not different music.
    std::vector<float> flux (env.size(), 0.0f);
    for (size_t i = 1; i < env.size(); ++i)
        flux[i] = std::max (0.0f, env[i] - env[i - 1]);

    std::vector<float> sorted = flux;
    std::sort (sorted.begin(), sorted.end());
    const float median = sorted[sorted.size() / 2];
    const float top = sorted[static_cast<size_t> (sorted.size() * 0.98)];
    const float gate = median + (top - median) * 0.40f;

    // Nothing in a band repeats an attack inside 50 ms - that is faster than a
    // drum roll and far faster than any stroke a grid could hold. The first
    // version used 11 ms and reported 41 attacks a second on a real take, where
    // even counting sixteenths at 97 BPM there are 6.5: the reference was
    // mostly noise, and a reference made of noise makes the app look scattered
    // whatever it does.
    const int refractory = static_cast<int> (sr * 0.05 / hop);
    int since = refractory;
    for (size_t i = 1; i + 1 < flux.size(); ++i, ++since)
    {
        if (flux[i] > gate && flux[i] >= flux[i - 1] && flux[i] >= flux[i + 1]
            && since >= refractory)
        {
            at.push_back (static_cast<double> (i * hop) / sr);
            since = 0;
        }
    }
    return at;
}

/** Where the click struck. A click track is the easiest signal in audio to
    pick: one transient per beat, nothing else on the channel, no decay worth
    speaking of. Deliberately crude - anything cleverer would be fitting the
    truth, which is the one thing that must not be fitted. */
std::vector<double> clickTimes (const std::vector<float>& x, double sr)
{
    std::vector<double> at;
    float peak = 0.0f;
    for (float v : x)
        peak = std::max (peak, std::fabs (v));
    if (peak < 1.0e-6f)
        return at;

    const float gate = peak * 0.35f;
    const int refractory = static_cast<int> (sr * 0.05);
    int since = refractory;
    for (size_t i = 0; i < x.size(); ++i, ++since)
    {
        if (std::fabs (x[i]) > gate && since >= refractory)
        {
            at.push_back (static_cast<double> (i) / sr);
            since = 0;
        }
    }
    return at;
}
}

int main (int argc, char** argv)
{
    std::string mixPath, clickPath;
    double fixedBpm = 0.0;
    bool speaker = false;
    bool autoMode = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--mix")          mixPath = next();
        else if (a == "--click")   clickPath = next();
        else if (a == "--bpm")     fixedBpm = std::atof (next().c_str());
        else if (a == "--speaker") speaker = true;
        else if (a == "--auto")    autoMode = true;
        else
        {
            std::printf ("uso: VPLive --mix band.wav [--click click.wav | --bpm 118] [--speaker]\n\n"
                         "  --mix     la registrazione della band (il feed del mixer, o il mix)\n"
                         "  --click   il click a cui hanno suonato, su traccia separata: e' la\n"
                         "            verita' al campione, ed e' il modo giusto\n"
                         "  --bpm     un tempo costante, se il click non e' stato tenuto. In\n"
                         "            questo caso conta la *deriva*, non lo scarto assoluto\n"
                         "  --speaker seguendo l'altoparlante dell'iPad invece del mixer\n");
            return 1;
        }
    }

    if (mixPath.empty())
    {
        std::printf ("serve --mix\n");
        return 1;
    }
    // With neither a click nor a stated tempo, score against the band itself.
    // That is the right default for a live take and the usual case: nobody
    // records a click they did not play to.
    if (clickPath.empty() && fixedBpm <= 0.0)
        autoMode = true;

    std::vector<float> mix;
    double sr = 48000.0;
    if (! readMono (juce::File::getCurrentWorkingDirectory().getChildFile (mixPath), mix, sr))
        return 1;

    // The truth, as beat times in seconds. Not needed in auto mode, where the
    // reference is the band's own strokes.
    std::vector<double> beats;
    if (autoMode)
    {
        // nothing to build
    }
    else if (! clickPath.empty())
    {
        std::vector<float> click;
        double csr = sr;
        if (! readMono (juce::File::getCurrentWorkingDirectory().getChildFile (clickPath), click, csr))
            return 1;
        if (std::fabs (csr - sr) > 1.0)
        {
            std::printf ("il click e' a %.0f Hz e il mix a %.0f: devono essere allineati\n", csr, sr);
            return 1;
        }
        beats = clickTimes (click, csr);
        if (beats.size() < 8)
        {
            std::printf ("nel click ho trovato solo %d colpi: non bastano\n",
                         static_cast<int> (beats.size()));
            return 1;
        }
    }
    else
    {
        // Beat one from the first transient in the mix. A guess, and said to be.
        float peak = 0.0f;
        for (float v : mix) peak = std::max (peak, std::fabs (v));
        double first = 0.0;
        for (size_t i = 0; i < mix.size(); ++i)
            if (std::fabs (mix[i]) > peak * 0.30f) { first = static_cast<double> (i) / sr; break; }
        const double period = 60.0 / fixedBpm;
        for (double t = first; t < static_cast<double> (mix.size()) / sr; t += period)
            beats.push_back (t);
    }

    std::vector<double> onsets;
    if (autoMode)
    {
        onsets = onsetTimes (mix, sr);
        if (onsets.size() < 32)
        {
            std::printf ("nel mix ho trovato solo %d attacchi: non bastano\n",
                         static_cast<int> (onsets.size()));
            return 1;
        }
    }

    vp::BeatTracker tracker;
    tracker.prepare (sr);
    tracker.setTempoFollow (true);
    tracker.setSpeakerFollow (speaker);
    tracker.start();

    // The song's true phase at a time, from the beat list.
    size_t cursor = 0;
    auto truePhaseAt = [&] (double t) -> double
    {
        while (cursor + 1 < beats.size() && beats[cursor + 1] <= t)
            ++cursor;
        if (cursor + 1 >= beats.size())
            return -1.0;
        const double span = beats[cursor + 1] - beats[cursor];
        if (span < 1.0e-6)
            return -1.0;
        return (t - beats[cursor]) / span;
    };

    double sumAbs = 0.0, worst = 0.0;
    int scored = 0;
    double lockedAt = -1.0;
    bool onnx = false;
    std::vector<double> errs;
    double bpmSum = 0.0;
    int bpmN = 0;
    size_t onsetCursor = 0;
    std::vector<std::pair<double,float>> bpmTrack;
    double lastTrack = -100.0;
    float lastBpm = 0.0f;
    int jumps = 0, octaveJumps = 0;
    std::vector<std::pair<double,float>> history;
    float was = 0.0f;
    bool inMove = false;

    const int total = static_cast<int> (mix.size());
    for (int pos = 0; pos + kBlock <= total; pos += kBlock)
    {
        const auto out = tracker.process (mix.data() + pos, kBlock);
        onnx = onnx || out.aiOnnx;

        // The analysis runs on its own thread. Draining it every block is what
        // makes the run repeatable - see the same spin in Tests/TestAiBeat.cpp.
        for (int spin = 0; spin < 20000; ++spin)
        {
            if (tracker.analysisBacklog() <= kBlock)
                break;
            std::this_thread::yield();
        }

        const double t = static_cast<double> (pos) / sr;

        if (autoMode)
        {
            // Every stroke the band played inside this block, against the grid
            // the app is running right now. The app's sixteenth nearest each
            // stroke is the one it would have played it on, so the distance to
            // it is what a listener hears as together or not.
            while (onsetCursor < onsets.size()
                   && onsets[onsetCursor] < t + static_cast<double> (kBlock) / sr)
            {
                const double ot = onsets[onsetCursor];
                ++onsetCursor;
                if (ot < t || out.bpm < 40.0f || t < 8.0)
                    continue;
                const double beatSec = 60.0 / static_cast<double> (out.bpm);
                // The app's phase at the instant of the stroke, not at the top
                // of the block.
                const double into = (ot - t) / beatSec;
                const double ph = static_cast<double> (out.beatPhase) + into;
                const double six = ph * 4.0;
                const double off = (six - std::floor (six + 0.5)) / 4.0;
                // Beyond an eighth of a beat this stroke is not on the grid at
                // all - a grace note, a flam, a bass note - and it is not
                // evidence about alignment either way.
                if (std::fabs (off) > 0.125)
                    continue;
                const double ms = off * beatSec * 1000.0;
                errs.push_back (ms);
                sumAbs += std::fabs (ms);
                worst = std::max (worst, std::fabs (ms));
                ++scored;
            }
            if (out.bpm >= 40.0f && t > 8.0)
            {
                bpmSum += out.bpm;
                ++bpmN;
                // The tempo it was showing, once every ten seconds, and every
                // time it jumped. A listener does not hear 3 ms of phase; they
                // hear the part change speed, or halve, or stop being on the
                // song. That is what this catches and no average can.
                if (bpmTrack.empty() || t - lastTrack > 5.0)
                {
                    bpmTrack.push_back ({ t, out.bpm });
                    lastTrack = t;
                }
                // Against where it was five seconds ago, not against the last
                // block. The clock glides into a new tempo on purpose, so
                // block-to-block every step is tiny and a count of those
                // reports zero however far the tempo has actually travelled -
                // measured on a real take that walked from 129 to 111, which is
                // 14%, this said "no jumps".
                history.push_back ({ t, out.bpm });
                while (! history.empty() && t - history.front().first > 5.0)
                {
                    was = history.front().second;
                    history.erase (history.begin());
                }
                if (was > 40.0f)
                {
                    const double moved = std::fabs (out.bpm - was) / was;
                    if (moved > 0.06 && ! inMove) { ++jumps; inMove = true; }
                    if (moved < 0.03) inMove = false;
                    if (moved > 0.35) ++octaveJumps;
                }
                lastBpm = out.bpm;
            }
            continue;
        }

        const double tp = truePhaseAt (t);
        if (tp < 0.0 || out.bpm < 40.0f)
            continue;

        // Seconds from the top of the take to the first block that is inside a
        // twentieth of a beat and stays there. That is "it has found the song".
        const double err = static_cast<double> (
            vp::wrapCentered (out.beatPhase - static_cast<float> (tp)));
        const double beatSec = 60.0 / std::max (40.0f, out.bpm);
        const double ms = err * beatSec * 1000.0;

        if (std::fabs (err) < 0.05)
        {
            if (lockedAt < 0.0)
                lockedAt = t;
        }
        else
        {
            lockedAt = -1.0;
        }

        // Scored only after it has had a chance to find the song at all, so the
        // opening seconds do not swamp the number that describes the take.
        if (t > 8.0)
        {
            sumAbs += std::fabs (ms);
            worst = std::max (worst, std::fabs (ms));
            errs.push_back (ms);
            ++scored;
            bpmSum += out.bpm;
            ++bpmN;
        }
    }

    std::printf ("\n=== %s ===\n", mixPath.c_str());
    std::printf ("rete            %s\n", onnx ? "BeatNet ONNX (quella vera)"
                                              : "STUB - i numeri qui sotto NON descrivono l'app sul palco");
    std::printf ("verita'         %s\n",
                 autoMode ? "la band stessa: dove sono caduti i suoi colpi"
                 : clickPath.empty() ? "--bpm costante (conta la deriva, non lo scarto)"
                                     : "click su traccia separata");
    std::printf ("durata          %.1f s\n", static_cast<double> (total) / sr);

    if (scored == 0)
    {
        std::printf ("\nnon ha mai agganciato: nessun blocco da misurare.\n");
        return 0;
    }

    std::vector<double> sorted = errs;
    std::sort (sorted.begin(), sorted.end());
    const double median = sorted[sorted.size() / 2];

    // With --bpm, beat one came from the first transient in the mix, which is a
    // guess and lands wherever the take happens to start. That guess is a
    // constant offset on every reading, so the absolute number says nothing and
    // only its *spread* is evidence. Taking the median out is what turns the
    // mode from decorative into usable: what is left is the drift across the
    // take, which is exactly what a wrong tempo produces and a wrong starting
    // point does not.
    //
    // Never done with --click: there the offset is the measurement. A part that
    // is reliably 40 ms late is late, and a bench that quietly recentres it has
    // thrown away the only number a listener would have complained about.
    const bool recentre = clickPath.empty() && ! autoMode;
    double driftSum = 0.0, driftWorst = 0.0;
    for (double e : errs)
    {
        const double d = recentre ? e - median : e;
        driftSum += std::fabs (d);
        driftWorst = std::max (driftWorst, std::fabs (d));
    }

    std::printf ("\nBPM medio       %.2f", bpmSum / bpmN);
    if (! clickPath.empty() || fixedBpm > 0.0)
    {
        const double want = fixedBpm > 0.0 ? fixedBpm : 0.0;
        if (want > 0.0)
            std::printf ("   (vero %.0f, scarto %.2f%%)",
                         want, std::fabs (bpmSum / bpmN - want) / want * 100.0);
    }
    std::printf ("\n");

    if (autoMode)
    {
        // The median is the whole answer here, and its sign is the half a
        // listener would have complained about: the app is playing that many
        // milliseconds after the band, or before it. The spread says whether it
        // is together or merely on average together.
        double spread = 0.0;
        for (double e : errs)
            spread += (e - median) * (e - median);
        spread = std::sqrt (spread / static_cast<double> (errs.size()));
        int within20 = 0;
        for (double e : errs)
            if (std::fabs (e - median) < 20.0)
                ++within20;

        const double perSec = static_cast<double> (onsets.size())
                              / (static_cast<double> (total) / sr);
        std::printf ("colpi misurati  %d (su %d attacchi, %.1f al secondo)%s\n",
                     scored, static_cast<int> (onsets.size()), perSec,
                     perSec > 12.0 ? "   <-- TROPPI: il riferimento sta prendendo rumore,\n"
                                     "                    i numeri qui sotto non valgono" : "");
        std::printf ("scarto medio    %+.1f ms   <-- l'app suona dopo la band se e' positivo\n",
                     median);
        std::printf ("dispersione     %.1f ms   (quanto e' *insieme*, non solo in media)\n", spread);
        std::printf ("entro 20 ms     %.0f%%\n",
                     100.0 * within20 / static_cast<double> (errs.size()));
        std::printf ("peggiore        %.1f ms\n", worst);

        std::printf ("\ncambi di tempo netti: %d  (un brano nuovo e' uno di questi)",  jumps);
        if (octaveJumps > 0)
            std::printf (", di cui %d di ottava (meta'/doppio)  <-- questo si sente", octaveJumps);
        std::printf ("\n");
        // How still it holds while the band is on the same piece.
        //
        // The first version of this reported the whole take's range and warned
        // when it exceeded 8%, which was wrong: a set has more than one song in
        // it, and a band that goes from 129 to 111 because they started the
        // next number has not made a mistake, and neither has a tracker that
        // follows them. Measured on a real take that did exactly that, the
        // bench called a correct 5-second adaptation a fault.
        //
        // What a percussionist is actually judged on is the other thing: with
        // the band settled on one tempo - which still breathes by a couple of
        // BPM - does the part sit still, or does it hunt? So the range is
        // reported without a verdict, and what carries the verdict is the
        // wobble *within* a stretch, measured around the local trend so that a
        // band genuinely speeding up does not read as instability.
        float lo = 999.0f, hi = 0.0f;
        for (const auto& b : bpmTrack) { lo = std::min (lo, b.second); hi = std::max (hi, b.second); }
        if (hi > lo)
            std::printf ("da %.0f a %.0f BPM nella presa   (un set ha piu' brani: non e' un difetto)\n",
                         static_cast<double> (lo), static_cast<double> (hi));

        // Wobble around the local trend, over windows of about twenty seconds.
        std::vector<double> wob;
        const int win = 5;   // samples, at one every five seconds
        for (size_t i = 0; i + win <= bpmTrack.size(); ++i)
        {
            double mean = 0.0;
            for (int k = 0; k < win; ++k)
                mean += bpmTrack[i + k].second;
            mean /= win;
            // A window straddling a change of song is not instability.
            double spanLo = 999.0, spanHi = 0.0;
            for (int k = 0; k < win; ++k)
            {
                spanLo = std::min (spanLo, static_cast<double> (bpmTrack[i + k].second));
                spanHi = std::max (spanHi, static_cast<double> (bpmTrack[i + k].second));
            }
            if ((spanHi - spanLo) / spanLo > 0.06)
                continue;
            double sq = 0.0;
            for (int k = 0; k < win; ++k)
                sq += (bpmTrack[i + k].second - mean) * (bpmTrack[i + k].second - mean);
            wob.push_back (std::sqrt (sq / win) / mean * 100.0);
        }
        if (! wob.empty())
        {
            std::sort (wob.begin(), wob.end());
            const double w = wob[wob.size() / 2];
            std::printf ("tiene il tempo? oscilla dello %.2f%% mentre la band sta sul pezzo"
                         "   (%.1f BPM a 110)%s\n",
                         w, w * 1.10,
                         w < 0.5 ? "   fermo come un percussionista"
                                 : w < 1.2 ? "   accettabile" : "   <-- si sente");
        }
        // An independent second opinion on the tempo was tried here and is not
        // in the build. The strokes were asked for their own period by the same
        // phase-coherence test HarmonicTempo uses, which works there because
        // chord changes are sparse and regular. A band's attacks are neither:
        // they are dense and land on every subdivision, and a shorter period
        // always has more chances to look coherent by accident, so the search
        // walks off to whatever short period the noise favours. Measured, it
        // answered 173/113/138/163/142 BPM on a take the app held at a steady
        // 110 for seven minutes - obvious nonsense, and only obvious because it
        // was that far out.
        //
        // Correcting it means normalising the score against what random events
        // would give at each period, which is a beat tracker rather than a
        // check on one. Until that exists this bench cannot say whether a tempo
        // that walks was the band or the app - and saying so is better than a
        // number that looks like an answer.
        std::printf ("il tempo nel tempo: ");
        for (size_t i = 0; i < bpmTrack.size(); ++i)
            std::printf ("%.0f%s", static_cast<double> (bpmTrack[i].second),
                         i + 1 < bpmTrack.size() ? " -> " : "");
        std::printf ("\n");
    }
    else if (recentre)
    {
        std::printf ("scarto fisso    %+.1f ms   (da dove ho indovinato il primo battito:\n"
                     "                            non e' una misura, e' il modo --bpm)\n", median);
        std::printf ("deriva media    %.1f ms   <-- questo e' il numero che conta\n",
                     driftSum / scored);
        std::printf ("deriva peggiore %.1f ms\n", driftWorst);
    }
    else
    {
        std::printf ("aggancio        %s\n",
                     lockedAt >= 0.0 ? (std::to_string (lockedAt) + " s").c_str() : "mai stabile");
        std::printf ("fase media      %.1f ms\n", sumAbs / scored);
        std::printf ("fase mediana    %+.1f ms   (il segno dice avanti o indietro)\n", median);
        std::printf ("fase peggiore   %.1f ms\n", worst);
    }

    std::printf ("\nPer confronto, sul materiale sintetico VPAlign misura 19-23 ms di media\n"
                 "nei passaggi difficili e 3-6 ms a tempo fermo.\n");
    if (! onnx)
        std::printf ("\nATTENZIONE: ha girato lo stub, non BeatNet. Vedi\n"
                     "  ./scripts/fetch_onnxruntime.sh --host\n"
                     "e ricompila: senza quello questi numeri non dicono niente sul palco.\n");
    return 0;
}
