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
    bool trace = false, speaker = false, loadedFile = false;
    std::string pulses, follow;
    bool startNow = false;
    // A second file loaded while START stays armed, as CARICA does mid-set.
    // --stop-gap G emulates the listener's workaround: STOP --stop-after X
    // seconds after the load (default at the load) and START again G seconds
    // later. --bpm then refers to the second file. docs/TODO.md item 48.
    std::string thenPath;
    double thenAt = -1.0, stopGap = -1.0, stopAfter = 0.0, stopAt = -1.0, lagSec = 0.0;
    // A tap on the waveform: at --seek-at seconds the file jumps to --seek-to
    // and the engine is told, as MainComponent does. --stop-gap then counts
    // from the seek. --from T scores --bpm only from T seconds on (a long
    // file with several songs).
    double seekAt = -1.0, seekTo = 0.0, scoreFrom = 0.0;
    // --octave-at T --octave N: a ÷2 (-1) or ×2 (+1) press at T seconds, as
    // the buttons do (manual level, AUTO off).
    double octaveAt = -1.0;
    int octavePress = 0;
    bool octavePressed = false;
    std::string outPath;
    bool quarters = false;
    std::vector<double> declareAt;   // --declare-at T (repeatable): "L'1 è QUI" pressed at T
    size_t declared = 0;

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
        else if (a == "--player")     loadedFile = true;
        else if (a == "--start-now")  startNow = true;   // START SUBITO
        // Where the clock actually *is*, block by block, so the strokes can be
        // scored against the drummer instead of against the published BPM.
        // A right tempo and a slipped grid sound completely different and the
        // BPM column cannot tell them apart - see docs/TODO.md item 35.
        else if (a == "--pulses")     pulses = next();
        // LOW / MEDIUM / HIGH, the phase-steering setting. Default is what
        // the app ships with; the bench needs it because how tightly the
        // clock holds a real drummer is exactly what this chooses.
        else if (a == "--follow")     follow = next();
        else if (a == "--then")       thenPath = next();
        else if (a == "--at")         thenAt = std::atof (next());
        else if (a == "--stop-gap")   stopGap = std::atof (next());
        else if (a == "--stop-after") stopAfter = std::atof (next());
        else if (a == "--stop-at")    stopAt = std::atof (next());
        else if (a == "--lag")        lagSec = std::atof (next());
        else if (a == "--seek-at")    seekAt = std::atof (next());
        else if (a == "--seek-to")    seekTo = std::atof (next());
        else if (a == "--from")       scoreFrom = std::atof (next());
        else if (a == "--octave-at")  octaveAt = std::atof (next());
        else if (a == "--octave")     octavePress = std::atoi (next());
        else if (a == "--declare-at") declareAt.push_back (std::atof (next()));
        else if (a == "--out")        outPath = next();   // the part's left channel, raw float32
        else if (a == "--quarters")   quarters = true;    // the part on quarters only: strokes apart
        else
        {
            std::printf ("uso: VPTrack --wav brano.wav [--bpm 87] [--gain dB]\n"
                         "            [--trace] [--step 2] [--until 90] [--speaker|--player]\n\n"
                         "  --bpm     il tempo vero, se lo sai: stampa quando ci arriva\n"
                         "            e quanto ci resta. Senza, riporta solo la traccia.\n"
                         "  --gain    dB sul segnale prima dell'analisi, come il knob MIC\n"
                         "  --player  brano caricato (nessun andata-ritorno, niente cancello)\n");
            return 1;
        }
    }
    if (path.empty()) { std::printf ("serve --wav\n"); return 1; }

    vp::WavAudio wav;
    std::string why;
    if (! vp::loadWavFile (path, wav, why)) { std::printf ("wav: %s\n", why.c_str()); return 1; }

    const double sr = wav.sampleRate;
    const float g = static_cast<float> (std::pow (10.0, gainDb / 20.0));
    std::vector<float> mono;
    auto append = [&] (const vp::WavAudio& w, int frames)
    {
        for (int i = 0; i < frames; ++i)
            mono.push_back (0.5f * (w.left[static_cast<size_t> (i)]
                                    + w.right[static_cast<size_t> (i)]) * g);
    };
    int switchAt = -1;
    if (! thenPath.empty())
    {
        vp::WavAudio second;
        if (! vp::loadWavFile (thenPath, second, why)) { std::printf ("wav: %s\n", why.c_str()); return 1; }
        if (second.sampleRate != wav.sampleRate) { std::printf ("--then: sample rate diverso\n"); return 1; }
        switchAt = std::clamp (static_cast<int> ((thenAt > 0.0 ? thenAt : 60.0) * sr), 0, wav.frames);
        append (wav, switchAt);
        append (second, second.frames);
    }
    else
        append (wav, wav.frames);
    const int n = static_cast<int> (mono.size());
    bool switched = false, stoppedAfter = false, restartedAfterGap = false, seeked = false;
    long readOffset = 0;

    constexpr int block = 256;
    vp::VirtualPercussionEngine eng;
    eng.prepare (sr, block, 1);
    eng.settings().followSource.store (static_cast<int> (
        speaker ? vp::FollowSource::speaker
                : (loadedFile ? vp::FollowSource::internalPlayer
                              : vp::FollowSource::kitMic)));
    if (! follow.empty())
    {
        const auto f = follow == "low"  ? vp::FollowStrength::low
                     : follow == "high" ? vp::FollowStrength::high
                                        : vp::FollowStrength::medium;
        eng.settings().followStrength.store (static_cast<int> (f));
    }
    eng.settings().startImmediately.store (startNow);
    if (quarters)
        eng.settings().subdivision.store (static_cast<int> (vp::Subdivision::quarter));
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
        std::printf ("#  t     pubbl   rete   pettine  corto  lungo  conf  resL resS  reg stato suona  "
                     "restart  gAnalisi  picco  dopoG  lowS  set  cov  1?  curva rate resC gain ev"
                     "  clock target trim phase trust recover fast interval votes dir transition\n");

    std::FILE* pulseFile = pulses.empty() ? nullptr : std::fopen (pulses.c_str(), "w");
    std::FILE* outFile = outPath.empty() ? nullptr : std::fopen (outPath.c_str(), "wb");
    if (pulseFile != nullptr)
        std::fprintf (pulseFile, "# t beatPhase barPhase bpm clockBpm suona phaseErr regime trust rete target trim trans recover stato conf pettine resid copert residCorto fiducia protetto unoFidato rullSide cassaSide rotazioni clapOk rotDispari\n");

    double lastTrace = -1.0e9, rightSince = -1.0, firstRight = -1.0;
    double trustLowAt = 0.0;   // as MainComponent keeps it for vp::tempoTrust
    double rightSeconds = 0.0, offSeconds = 0.0;
    int pos = 0, inHop = 0, restartsSeen = 0;
    std::vector<double> restartAt;
    vp::EngineSnapshot s {};

    while (pos + block <= n && pos + readOffset + block <= n && pos / sr < until)
    {
        if (switchAt >= 0 && ! switched && pos >= switchAt)
        {
            switched = true;
            eng.notifyInputRestart();
            std::printf ("# %.1f s: carico %s%s\n", pos / sr, thenPath.c_str(),
                         stopGap >= 0.0 ? " con STOP" : " (START resta acceso)");
        }
        // --stop-at T: the listener's own gesture, STOP at T and START
        // --stop-gap seconds later (1 s if not given), with no switch or seek.
        const int stopSample = stopAt >= 0.0
                                   ? static_cast<int> (stopAt * sr)
                                   : (switchAt >= 0 ? switchAt : static_cast<int> (seekAt * sr))
                                         + static_cast<int> (stopAfter * sr);
        if (stopAt >= 0.0 && stopGap < 0.0)
            stopGap = 1.0;
        if ((switched || seeked || stopAt >= 0.0) && stopGap >= 0.0 && ! stoppedAfter
            && pos >= stopSample)
        {
            stoppedAfter = true;
            eng.stop();
            std::printf ("# %.1f s: STOP\n", pos / sr);
        }
        if (stoppedAfter && ! restartedAfterGap
            && pos >= stopSample + static_cast<int> (stopGap * sr))
        {
            restartedAfterGap = true;
            eng.start();
            std::printf ("# %.1f s: START\n", pos / sr);
        }
        if (seekAt >= 0.0 && ! seeked && pos >= static_cast<int> (seekAt * sr))
        {
            seeked = true;
            readOffset = static_cast<long> (seekTo * sr) - pos;
            eng.notifyTrackSeek();
            std::printf ("# %.1f s: seek a %.1f s del file\n", pos / sr, seekTo);
        }
        if (declared < declareAt.size() && pos >= static_cast<int> (declareAt[declared] * sr))
        {
            ++declared;
            std::printf ("# %.3f s: L'1 è QUI, fase del clock %.3f\n", pos / sr,
                         (double) eng.snapshot().beatPhase);
            eng.settings().barDeclare.fetch_add (1);
        }
        if (octaveAt >= 0.0 && ! octavePressed && pos >= static_cast<int> (octaveAt * sr))
        {
            octavePressed = true;
            eng.settings().tempoOctaveAuto.store (false);
            eng.settings().tempoOctave.store (octavePress);
            std::printf ("# %.1f s: ottava manuale %+d\n", pos / sr, octavePress);
        }
        const int take = std::min ({ block, n - pos, hop - inHop, switchAt > pos ? switchAt - pos : block });
        const float* ins[1] = { mono.data() + pos + readOffset };
        eng.process (ins, 1, outs, 2, take);
        s = eng.snapshot();
        const double t = pos / sr;
        if (s.confidence < vp::kTrustConf || s.state != vp::TrackingState::following)
            trustLowAt = t;
        const auto trust = vp::tempoTrust (s, t - trustLowAt >= vp::kTrustSteadySec,
                                           std::max (s.inputPeak, s.analysisPeak) < 0.0012f);
        if (outFile != nullptr)
            std::fwrite (oL.data(), sizeof (float), static_cast<size_t> (take), outFile);

        if (s.analysisRestarts != restartsSeen)
        {
            restartsSeen = s.analysisRestarts;
            restartAt.push_back (t);
        }

        if (reference > 0.0 && s.state == vp::TrackingState::following
            && (switchAt < 0 || switched) && pos / sr >= scoreFrom)
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

        if (pulseFile != nullptr)
            std::fprintf (pulseFile, "%.4f %.5f %.5f %.3f %.3f %d %+.5f %d %.3f %.3f %.3f %+.3f %d %u %d %.3f %.3f %.4f %.3f %.4f %d %d %d %+.3f %+.3f %d %d %d\n", t,
                          (double) s.beatPhase, (double) s.barPhase, (double) s.bpm,
                          (double) s.clockBpm, s.percussionAudible ? 1 : 0,
                          (double) s.phaseErrorBeats, s.tempoRegime,
                          (double) s.evidenceTrust, (double) s.neuralBpm,
                          (double) s.targetBpm, (double) s.tempoTrimBpm,
                          (int) s.tempoTransitionState, s.phaseRecoveryEvents,
                          (int) s.followBar, (double) s.confidence,
                          (double) s.combBpm, (double) s.fitResidual,
                          (double) s.fitCoverage, (double) s.shortFitResidual,
                          (int) trust, s.driftGuarded ? 1 : 0, s.barTrusted ? 1 : 0,
                          (double) s.styleBackbeatSide, (double) s.styleKickSide,
                          s.barRotations, s.clapAllowed ? 1 : 0, s.barOddRotations);

        if (trace && t >= lastTrace + traceStep)
        {
            lastTrace = t;
            vp::BeatHypothesis hyp {};
            const bool haveHyp = eng.tryLoadNeuralHypothesis (hyp);
            std::printf ("%6.1f %7.2f %7.2f %8.2f %7.2f %7.2f  %.2f  %5.3f %5.3f   %d    %d     %s  %5d  %7.2f  %.3f  %.3f  %.3f  %d  %.2f  %s  %7.2f %+6.2f %.3f %.2f %d  %7.2f %7.2f %+6.2f %+6.3f %.2f %u %+6.3f %+6.3f %2d %+2d %d\n",
                         t, (double) s.bpm, (double) s.neuralBpm, (double) s.combBpm,
                         (double) s.shortFitBpm, (double) s.longFitBpm,
                         (double) s.confidence, (double) s.fitResidual,
                         (double) s.shortFitResidual, s.tempoRegime,
                         (int) s.state, s.percussionAudible ? "SI" : "no",
                         s.analysisRestarts, (double) s.analysisGain, (double) s.inputPeak,
                         (double) s.analysisPeak, (double) s.lowShare,
                         s.levelSettled ? 1 : 0, (double) s.fitCoverage,
                         s.barTrusted ? "SI" : "no",
                         haveHyp ? (double) hyp.motionFitBpm : 0.0,
                         haveHyp ? (double) hyp.motionFitRate : 0.0,
                         haveHyp ? (double) hyp.motionFitResidual : 1.0,
                         haveHyp ? (double) hyp.motionFitImprovement : 0.0,
                         haveHyp ? hyp.motionFitEvidence : 0,
                         (double) s.clockBpm, (double) s.targetBpm,
                         (double) s.tempoTrimBpm, (double) s.phaseErrorBeats,
                         (double) s.evidenceTrust, s.phaseRecoveryEvents,
                         (double) s.fastTempoDeviation,
                         (double) s.fastIntervalDeviation,
                         s.fastTempoEvidence, s.fastTempoDirection,
                         (int) s.tempoTransitionState);
        }

        pos += take;
        inHop += take;
        // With VP_OFFLINE_PACING the worker drains at once, so the probe can
        // wait after every block: each block then sees exactly the analysis of
        // all audio fed so far, and two runs are identical. Waiting only at
        // hop boundaries let a frame published mid-hop land on a different
        // block from run to run.
        static const bool everyBlock = std::getenv ("VP_OFFLINE_PACING") != nullptr;
        if (inHop == hop || everyBlock)
        {
            // A safety net, not pacing. At 400 ms a wait could run out (UNA
            // CANZONE 48k at -12 dB alone: 0.8% right tempo, inside the
            // six-way bench 85.9%), and a run that stops waiting is no longer
            // the run it is compared with (docs/TODO.md item 96).
            const auto until2 = std::chrono::steady_clock::now()
                                + std::chrono::milliseconds (everyBlock ? 10000 : 400);
            // --lag keeps the worker that far behind, as a device that cannot
            // keep up would: the backlog the tracker has to project across.
            while (eng.analysisCompletedSamples() < pos - static_cast<int64_t> (lagSec * sr)
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
        if (switchAt >= 0 && firstRight >= 0.0)
            std::printf ("dopo il cambio brano: %.2f s\n", firstRight - switchAt / sr);
        else if (scoreFrom > 0.0 && firstRight >= 0.0)
            std::printf ("dopo --from: %.2f s\n", firstRight - scoreFrom);
        std::printf ("tempo dentro il 2%%: %.1f%%  (%.1f s su %.1f)\n",
                     tot > 0.0 ? rightSeconds / tot * 100.0 : 0.0, rightSeconds, tot);
    }
    std::printf ("bpm finale %.2f  restart %d\n", (double) s.bpm, s.analysisRestarts);
    std::printf ("riallineamenti in silenzio %u, ultimo %+.3f battiti\n",
                 s.silentSnapCount, (double) s.silentSnapBeats);
    if (pulseFile != nullptr)
        std::fclose (pulseFile);
    if (outFile != nullptr)
        std::fclose (outFile);
    return 0;
}
