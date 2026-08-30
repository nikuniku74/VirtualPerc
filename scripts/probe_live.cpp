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

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--mix")          mixPath = next();
        else if (a == "--click")   clickPath = next();
        else if (a == "--bpm")     fixedBpm = std::atof (next().c_str());
        else if (a == "--speaker") speaker = true;
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
    if (clickPath.empty() && fixedBpm <= 0.0)
    {
        std::printf ("serve una verita': --click oppure --bpm\n");
        return 1;
    }

    std::vector<float> mix;
    double sr = 48000.0;
    if (! readMono (juce::File::getCurrentWorkingDirectory().getChildFile (mixPath), mix, sr))
        return 1;

    // The truth, as beat times in seconds.
    std::vector<double> beats;
    if (! clickPath.empty())
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
    std::printf ("verita'         %s\n", clickPath.empty()
                     ? "--bpm costante (conta la deriva, non lo scarto)"
                     : "click su traccia separata");
    std::printf ("durata          %.1f s, %d battiti nella verita'\n",
                 static_cast<double> (total) / sr, static_cast<int> (beats.size()));

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
    const bool recentre = clickPath.empty();
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

    if (recentre)
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
