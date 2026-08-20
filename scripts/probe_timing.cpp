// Where the stroke actually lands, measured on the audio the engine produces
// rather than on the clock that schedules it.
//
// Everything the tests measured so far was the *clock*: the phase it reports
// against the notated beat. A listener does not hear the clock. Between the
// pulse and the ear there is the sample's own attack, the block offset the
// voice starts at, and whatever the output path adds - and a stroke can be a
// clean twelve milliseconds late with a clock that is perfect.
//
// Two measurements:
//   attack   trigger to perceptual onset for each articulation, in isolation
//   land     rendered onset against the notated beat, whole engine, on a song
#include "Audio/VirtualPercussionEngine.h"
#include "Percussion/PercussionEngine.h"

#include "probe_song_render.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace
{
using namespace vp::probe;

/** Where a burst begins to the ear: the first point the envelope passes a
    fraction of the peak it is about to reach. The peak itself is later than
    the onset for anything with a rise, and the rise is exactly what is being
    measured here, so peak-picking would hide it. */
double onsetIndex (const std::vector<float>& x, int from, int to, double sr, float frac = 0.20f)
{
    from = std::max (0, from);
    to = std::min (static_cast<int> (x.size()), to);
    if (to - from < 8)
        return -1.0;

    // Fast envelope: rectify, then a one-pole with a 1 ms attack.
    const float a = 1.0f - std::exp (-1.0f / static_cast<float> (sr * 0.001));
    std::vector<float> env (static_cast<size_t> (to - from), 0.0f);
    float e = 0.0f;
    for (int i = from; i < to; ++i)
    {
        const float v = std::fabs (x[static_cast<size_t> (i)]);
        e += (v - e) * (v > e ? 0.6f : a);
        env[static_cast<size_t> (i - from)] = e;
    }

    const float peak = *std::max_element (env.begin(), env.end());
    if (peak < 1.0e-5f)
        return -1.0;

    const float thresh = peak * frac;
    for (size_t i = 0; i < env.size(); ++i)
        if (env[i] >= thresh)
            return static_cast<double> (from + static_cast<int> (i));
    return -1.0;
}

const char* strokeName (int s)
{
    static const char* names[] = { "shakerDown", "shakerUp", "shakerAccent",
                                   "tumba", "openTone", "slap", "heel", "toe", "muff" };
    return s >= 0 && s < 9 ? names[s] : "?";
}

// --- 1. how late each articulation's own attack is ---------------------------
void measureAttacks (double sr)
{
    std::printf ("--- attacco del campione: dal trigger a quando si sente ---\n");
    std::printf ("%-14s %-8s %-8s %-8s %-8s %-9s\n",
                 "stroke", "20%", "50%", "80%", "picco", "baricentro");

    for (int s = 0; s < static_cast<int> (vp::Stroke::count); ++s)
    {
        vp::PercussionEngine perc;
        perc.prepare (sr);
        perc.setSeed (1234u);
        perc.setHumanization (0.0f);
        perc.setReverbAmount (0.0f);
        perc.setVolume (1.0f);
        perc.setGroove (120.0f, 4);

        // Drive one stroke through the public path by hand: a tick with no
        // pulses, then the voice started at offset zero.
        const int n = static_cast<int> (sr * 0.5);
        std::vector<float> l (static_cast<size_t> (n), 0.0f), r (static_cast<size_t> (n), 0.0f);
        vp::ClockTick silentTick;
        perc.triggerForTest (static_cast<vp::Stroke> (s), 0.9f, 0);
        perc.render (l.data(), r.data(), n, silentTick, true);

        int peakAt = 0;
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
            if (std::fabs (l[static_cast<size_t> (i)]) > peak)
            {
                peak = std::fabs (l[static_cast<size_t> (i)]);
                peakAt = i;
            }

        // The centre of energy over the first fifty milliseconds. For a burst
        // with a rise this sits where the sound actually reads as happening,
        // which the first sample of the rise does not.
        const int win = std::min (n, static_cast<int> (sr * 0.05));
        double num = 0.0, den = 0.0;
        for (int i = 0; i < win; ++i)
        {
            const double e = static_cast<double> (l[static_cast<size_t> (i)])
                             * static_cast<double> (l[static_cast<size_t> (i)]);
            num += e * i;
            den += e;
        }

        auto ms = [&] (double idx) { return idx >= 0.0 ? idx / sr * 1000.0 : -1.0; };
        std::printf ("%-14s %-8.2f %-8.2f %-8.2f %-8.2f %-9.2f\n", strokeName (s),
                     ms (onsetIndex (l, 0, n, sr, 0.20f)),
                     ms (onsetIndex (l, 0, n, sr, 0.50f)),
                     ms (onsetIndex (l, 0, n, sr, 0.80f)),
                     ms (peakAt),
                     den > 0.0 ? num / den / sr * 1000.0 : -1.0);
    }
    std::printf ("\n");
}

// --- 2. where the stroke lands on a song -------------------------------------
struct Landing
{
    double meanMs = 0.0;
    double medianMs = 0.0;
    double spreadMs = 0.0;
    int    hits = 0;
    int    windows = 0;
    double heardMs = 0.0;
    double clockMs = 0.0;   // the clock's own signed phase error, for comparison
    int    barOffset = -1;  // clock's beat-one against the song's, in beats
    double barAgree = 0.0;  // fraction of the run the bar was aligned
    int    entryBeat = -1;  // true beat of the bar the part came in on
    int    hintRot = -1;    // what the folded bar was saying at the end
    float  hintConf = 0.0f;
    int    trueRot = -1;    // what it should have said
};

// Which of the two the app is listening through. IPAD puts its own speaker, the
// room and its microphone in front of the arrangement; MIXER is a line feed.
// Both ship, so the stroke has to land on the beat in both.
enum class Listening { ipad, mixer };

Landing measureLanding (const SongOptions& opt, double sr, unsigned seed, bool trace,
                        Listening mode)
{
    const int block = 256;
    const int n = static_cast<int> (sr * 60.0);

    std::vector<float> song (static_cast<size_t> (n), 0.0f);
    renderSong (song, opt, sr, seed);
    if (mode == Listening::ipad)
        speakerRoomMic (song, sr, seed, 0.55f);

    vp::VirtualPercussionEngine eng;
    eng.prepare (sr, block, 1);
    eng.settings().followSource.store (static_cast<int> (
        mode == Listening::ipad ? vp::FollowSource::speaker : vp::FollowSource::kitMic));
    // Isolate the shaker on the quarters: one stroke per beat, no conga, no
    // feel scatter and no reverb tail, so every onset in the output is a stroke
    // that should be sitting exactly on a notated beat.
    eng.settings().congasEnabled.store (false);
    eng.settings().subdivision.store (static_cast<int> (vp::Subdivision::quarter));
    eng.settings().humanization.store (0.0f);
    eng.settings().swing.store (0.0f);
    eng.settings().reverbAmount.store (0.0f);
    eng.settings().intensity.store (0.7f);
    eng.start();

    std::vector<float> outMono (static_cast<size_t> (n), 0.0f);
    std::vector<float> oL (static_cast<size_t> (block), 0.0f);
    std::vector<float> oR (static_cast<size_t> (block), 0.0f);
    float* outs[2] = { oL.data(), oR.data() };

    const double beatSamples = 60.0 / static_cast<double> (opt.bpm) * sr;

    Landing out;
    double clockAcc = 0.0;
    int clockN = 0;
    int barSame = 0, barTotal = 0;
    int barOffsetVotes[4] = { 0, 0, 0, 0 };
    bool playing = false;

    int pos = 0, blocks = 0;
    while (pos + block <= n)
    {
        const float* ins[1] = { song.data() + pos };
        eng.process (ins, 1, outs, 2, block);
        for (int i = 0; i < block; ++i)
            outMono[static_cast<size_t> (pos + i)] = 0.5f * (oL[static_cast<size_t> (i)]
                                                             + oR[static_cast<size_t> (i)]);

        const auto snap = eng.snapshot();
        const double t = static_cast<double> (pos) / sr;

        if (! playing && snap.percussionAudible)
        {
            playing = true;
            const double beats = static_cast<double> (pos) / beatSamples;
            out.entryBeat = static_cast<int> (std::llround (beats)) & 3;
        }

        if (t > 25.0 && snap.bpm > 40.0f)
        {
            const double truePhase = static_cast<double> (pos) / beatSamples;
            clockAcc += static_cast<double> (vp::wrapCentered (
                snap.beatPhase - static_cast<float> (truePhase - std::floor (truePhase))));
            ++clockN;

            const int trueBeat = static_cast<int> (std::floor (truePhase)) & 3;
            const int clockBeat = juce::jlimit (0, 3, static_cast<int> (snap.barPhase * 4.0f));
            barOffsetVotes[((clockBeat - trueBeat) & 3)] += 1;
            out.trueRot = (clockBeat - trueBeat) & 3;
            barSame += clockBeat == trueBeat;
            ++barTotal;
        }

        pos += block;
        if ((++blocks % 8) == 0)
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }

    // Every notated beat past the settling point: where did the stroke land?
    //
    // A window with no stroke in it must be skipped, not measured: the detector
    // will happily latch onto a reverb tail or the noise floor and report an
    // offset anywhere inside the window, and a handful of those swamp the mean
    // with values the size of half a beat. So a window has to contain something
    // clearly louder than the run's own background before it counts.
    const int firstBeat = static_cast<int> (std::ceil (sr * 28.0 / beatSamples));
    const int lastBeat = static_cast<int> (std::floor ((n - beatSamples) / beatSamples));

    double sumSq = 0.0;
    int nSq = 0;
    for (int i = static_cast<int> (sr * 28.0); i < n; i += 7)
    {
        sumSq += static_cast<double> (outMono[static_cast<size_t> (i)])
                 * static_cast<double> (outMono[static_cast<size_t> (i)]);
        ++nSq;
    }
    const float rms = nSq > 0 ? static_cast<float> (std::sqrt (sumSq / nSq)) : 0.0f;
    const float floorLevel = std::max (1.0e-4f, rms * 3.0f);

    std::vector<double> offsets;      // start of sound, 20% of the rise
    std::vector<double> heard;        // where it reads as happening, 80%
    int windows = 0;
    for (int b = firstBeat; b < lastBeat; ++b)
    {
        ++windows;
        const double centre = static_cast<double> (b) * beatSamples;
        // Fifty milliseconds either side. Wider and the neighbouring
        // subdivision walks into the window - the part plays more than one
        // stroke per beat whatever the shaker density is set to - and the
        // detector then reports a sixteenth as a beat a hundred milliseconds
        // early. The effect being measured is a handful of milliseconds, so
        // this is still an order of magnitude of headroom.
        const int half = static_cast<int> (std::min (sr * 0.050, beatSamples * 0.30));
        const int from = static_cast<int> (centre) - half;
        const int to = static_cast<int> (centre) + half;
        float wPeak = 0.0f;
        for (int i = std::max (0, from); i < std::min (n, to); ++i)
            wPeak = std::max (wPeak, std::fabs (outMono[static_cast<size_t> (i)]));
        if (wPeak < floorLevel)
            continue;
        const double on = onsetIndex (outMono, from, to, sr);
        if (on < 0.0)
            continue;
        offsets.push_back ((on - centre) / sr * 1000.0);
        // The same criterion the attack compensation is defined by, so the two
        // measure the same thing: this is the column that has to read zero.
        const double hd = onsetIndex (outMono, from, to, sr, 0.80f);
        if (hd >= 0.0)
            heard.push_back ((hd - centre) / sr * 1000.0);
    }
    out.windows = windows;
    if (! heard.empty())
    {
        std::vector<double> sorted (heard);
        std::sort (sorted.begin(), sorted.end());
        out.heardMs = sorted[sorted.size() / 2];
    }

    if (! offsets.empty())
    {
        double sum = 0.0;
        for (double v : offsets) sum += v;
        out.meanMs = sum / static_cast<double> (offsets.size());
        std::vector<double> sorted (offsets);
        std::sort (sorted.begin(), sorted.end());
        out.medianMs = sorted[sorted.size() / 2];
        out.spreadMs = sorted[static_cast<size_t> (sorted.size() * 0.9)]
                       - sorted[static_cast<size_t> (sorted.size() * 0.1)];
        out.hits = static_cast<int> (offsets.size());
    }
    out.clockMs = clockN > 0 ? clockAcc / clockN * (60.0 / static_cast<double> (opt.bpm)) * 1000.0 : 0.0;
    out.barAgree = barTotal > 0 ? static_cast<double> (barSame) / barTotal : 0.0;
    int best = 0;
    for (int i = 1; i < 4; ++i)
        if (barOffsetVotes[i] > barOffsetVotes[best])
            best = i;
    out.barOffset = barTotal > 0 ? best : -1;

    if (trace)
        std::printf ("   hits=%d  mean=%.2f ms  median=%.2f  spread=%.2f\n",
                     out.hits, out.meanMs, out.medianMs, out.spreadMs);
    return out;
}
} // namespace

int main (int argc, char** argv)
{
    const double sr = 48000.0;
    bool attacksOnly = false;
    Listening mode = Listening::ipad;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--attacks") == 0) attacksOnly = true;
        else if (std::strcmp (argv[i], "--mixer") == 0) mode = Listening::mixer;
        else if (std::strcmp (argv[i], "--ipad") == 0) mode = Listening::ipad;
    }

    measureAttacks (sr);
    if (attacksOnly)
        return 0;

    std::printf ("--- dove cade il colpo, sull'audio prodotto (%s) ---\n",
                 mode == Listening::ipad ? "IPAD, cassa -> stanza -> microfono"
                                         : "MIXER, linea");
    std::printf ("%-11s %-5s %-9s %-9s %-8s %-9s %-7s %-8s %-7s %-6s\n",
                 "style", "bpm", "inizio", "SENTITO", "spread", "colpi", "clock", "bar_off",
                 "bar_ok", "entra");

    std::vector<SongOptions> styles;
    { SongOptions o; styles.push_back (o); }
    { SongOptions o; o.syncopated = true; styles.push_back (o); }
    { SongOptions o; o.sustained = true; styles.push_back (o); }

    const float tempos[] = { 92.0f, 104.0f, 118.0f, 128.0f };

    double soundAcc = 0.0, clockAcc = 0.0, agreeAcc = 0.0, heardAcc = 0.0;
    int nRuns = 0, aligned = 0, enteredOnOne = 0;

    for (const auto& style : styles)
    {
        for (float bpm : tempos)
        {
            SongOptions o = style;
            o.bpm = bpm;
            const Landing L = measureLanding (o, sr, static_cast<unsigned> (bpm) * 7u + 13u,
                                              false, mode);
            ++nRuns;
            soundAcc += L.medianMs;
            heardAcc += L.heardMs;
            clockAcc += L.clockMs;
            agreeAcc += L.barAgree;
            aligned += L.barOffset == 0;
            enteredOnOne += L.entryBeat == 0;

            std::printf ("%-11s %-5.0f %-9.2f %-9.2f %-8.2f %-9s %-7.2f %-8d %-7.0f %-10s\n",
                         styleName (o), static_cast<double> (bpm), L.medianMs, L.heardMs,
                         L.spreadMs,
                         (juce::String (L.hits) + "/" + juce::String (L.windows)).toRawUTF8(),
                         L.clockMs, L.barOffset, L.barAgree * 100.0,
                         juce::String (L.entryBeat).toRawUTF8());
            std::fflush (stdout);
        }
    }

    std::printf ("\n=== %d brani ===\n", nRuns);
    std::printf ("inizio del suono (mediana) %+.2f ms\n", soundAcc / nRuns);
    std::printf ("attacco SENTITO (mediana)  %+.2f ms   <- deve essere ~0\n", heardAcc / nRuns);
    std::printf ("errore medio del clock    %+.2f ms\n", clockAcc / nRuns);
    std::printf ("battuta allineata         %d/%d  (media %.0f%% del tempo)\n",
                 aligned, nRuns, agreeAcc / nRuns * 100.0);
    std::printf ("entra sull'uno            %d/%d\n", enteredOnOne, nRuns);
    return 0;
}
