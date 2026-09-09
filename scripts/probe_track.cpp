// A real recording through the *whole* engine, not just the tracker.
//
// `VPLive` drives `BeatTracker` directly, which is the right tool for scoring
// phase against a click but leaves out everything `VirtualPercussionEngine`
// does to the signal first: the listener's trim, the leak canceller, the
// make-up gain that holds the network's operating point, and - the one that
// changes the answer - `updateAnalysisEpoch`, which throws the analysis's
// evidence away when the level rises eightfold out of a properly quiet room.
//
// That event is not a detail on real material. Measured on the reference song
// kept for this (a track whose rhythm section enters at about thirty seconds),
// the energy below 200 Hz goes from 9.9 to 79.7 across that entrance: an eight
// times rise, which is exactly what the epoch detector exists to catch. A bench
// that cannot see it cannot say what the app does.
//
//   cmake --build build-host --target VPTrack -j4
//   ./build-host/VPTrack_artefacts/Release/VPTrack --wav /tmp/song.wav --bpm 87
#include "Audio/VirtualPercussionEngine.h"
#include "AI/BeatModelConfig.h"
#include "Loops/WavFile.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

int main (int argc, char** argv)
{
    std::string path;
    double reference = 0.0, gainDb = 0.0, traceStep = 2.0, until = 1.0e9;
    bool trace = false, speaker = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--wav")             path = next();
        else if (a == "--bpm")        reference = std::atof (next());
        else if (a == "--gain")       gainDb = std::atof (next());
        else if (a == "--trace")      trace = true;
        else if (a == "--step")       traceStep = std::atof (next());
        else if (a == "--until")      until = std::atof (next());
        else if (a == "--speaker")    speaker = true;
        else
        {
            std::printf ("uso: VPTrack --wav brano.wav [--bpm 87] [--gain dB]\n"
                         "            [--trace] [--step 2] [--until 90] [--speaker]\n\n"
                         "  --bpm  il tempo vero, se lo sai: stampa quando ci arriva\n"
                         "         e quanto ci resta. Senza, riporta solo la traccia.\n"
                         "  --gain dB sul segnale prima dell'analisi, come il knob MIC\n");
            return 1;
        }
    }
    if (path.empty()) { std::printf ("serve --wav\n"); return 1; }

    vp::WavAudio wav;
    std::string why;
    if (! vp::loadWavFile (path, wav, why)) { std::printf ("wav: %s\n", why.c_str()); return 1; }

    const double sr = wav.sampleRate;
    const int n = wav.frames;
    std::vector<float> mono (static_cast<size_t> (n));
    const float g = static_cast<float> (std::pow (10.0, gainDb / 20.0));
    for (int i = 0; i < n; ++i)
        mono[static_cast<size_t> (i)] = 0.5f * (wav.left[static_cast<size_t> (i)]
                                                + wav.right[static_cast<size_t> (i)]) * g;

    constexpr int block = 256;
    vp::VirtualPercussionEngine eng;
    eng.prepare (sr, block, 1);
    eng.settings().followSource.store (static_cast<int> (speaker ? vp::FollowSource::speaker
                                                                : vp::FollowSource::kitMic));
    eng.settings().shakerEnabled.store (true);
    eng.settings().congasEnabled.store (false);
    eng.settings().cembaloEnabled.store (false);
    eng.settings().clapEnabled.store (false);
    eng.start();

    std::vector<float> oL (block, 0.0f), oR (block, 0.0f);
    float* outs[2] = { oL.data(), oR.data() };
    const int hop = static_cast<int> (std::ceil (vp::kBeatModelHop * sr / vp::kBeatModelSampleRate));

    std::printf ("# %s  %.1f s  %.0f Hz  guadagno %+.1f dB\n",
                 path.c_str(), n / sr, sr, gainDb);
    if (trace)
        std::printf ("#  t     pubbl   rete   pettine  conf  residuo  reg stato suona  "
                     "restart  gAnalisi  picco  dopoG  lowS  set  cov\n");

    double lastTrace = -1.0e9, rightSince = -1.0, firstRight = -1.0;
    double rightSeconds = 0.0, offSeconds = 0.0;
    int pos = 0, inHop = 0, restartsSeen = 0;
    std::vector<double> restartAt;
    vp::EngineSnapshot s {};

    while (pos + block <= n && pos / sr < until)
    {
        const int take = std::min ({ block, n - pos, hop - inHop });
        const float* ins[1] = { mono.data() + pos };
        eng.process (ins, 1, outs, 2, take);
        s = eng.snapshot();
        const double t = pos / sr;

        if (s.analysisRestarts != restartsSeen)
        {
            restartsSeen = s.analysisRestarts;
            restartAt.push_back (t);
        }

        if (reference > 0.0 && s.state == vp::TrackingState::following)
        {
            const bool right = std::fabs (s.bpm - reference) <= reference * 0.02;
            (right ? rightSeconds : offSeconds) += take / sr;
            if (right)
            {
                if (rightSince < 0.0) rightSince = t;
                if (firstRight < 0.0 && t - rightSince >= 3.0) firstRight = rightSince;
            }
            else rightSince = -1.0;
        }

        if (trace && t >= lastTrace + traceStep)
        {
            lastTrace = t;
            std::printf ("%6.1f %7.2f %7.2f %8.2f  %.2f  %6.3f   %d    %d     %s  %5d  %7.2f  %.3f  %.3f  %.3f  %d  %.2f\n",
                         t, (double) s.bpm, (double) s.neuralBpm, (double) s.combBpm,
                         (double) s.confidence, (double) s.fitResidual, s.tempoRegime,
                         (int) s.state, s.percussionAudible ? "SI" : "no",
                         s.analysisRestarts, (double) s.analysisGain, (double) s.inputPeak,
                         (double) s.analysisPeak, (double) s.lowShare,
                         s.levelSettled ? 1 : 0, (double) s.fitCoverage);
        }

        pos += take;
        inHop += take;
        if (inHop == hop)
        {
            const auto until2 = std::chrono::steady_clock::now() + std::chrono::milliseconds (400);
            while (eng.analysisCompletedSamples() < pos
                   && std::chrono::steady_clock::now() < until2)
                std::this_thread::yield();
            inHop = 0;
        }
    }

    std::printf ("\nrestart dell'analisi: %d", (int) restartAt.size());
    for (double t : restartAt) std::printf ("  %.1fs", t);
    std::printf ("\n");
    if (reference > 0.0)
    {
        const double tot = rightSeconds + offSeconds;
        std::printf ("riferimento %.2f BPM (+-2%%)\n", reference);
        std::printf ("primo aggancio tenuto 3 s: %s\n",
                     firstRight >= 0.0 ? (std::to_string (firstRight) + " s").c_str() : "mai");
        std::printf ("tempo dentro il 2%%: %.1f%%  (%.1f s su %.1f)\n",
                     tot > 0.0 ? rightSeconds / tot * 100.0 : 0.0, rightSeconds, tot);
    }
    std::printf ("bpm finale %.2f  restart %d\n", (double) s.bpm, s.analysisRestarts);
    return 0;
}
