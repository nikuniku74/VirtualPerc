// Does the app know the difference between an empty room and a band?
//
// It does not, and this is what says so. The app listens from the moment it is
// opened, so before anyone plays it is analysing a room - and it locks to it:
// measured here, 99 BPM at confidence 0.91 with nobody in front of the
// microphone. `VirtualPercussionEngine::updateAnalysisEpoch` notices when the
// band actually starts and throws that away, so the wrong number lasts only
// until the first note; what it cannot do is stop the number appearing in the
// first place.
//
// Stopping it needs a rule that tells a room from a band, and every rule has to
// clear one bar: **the quietest band the host tests insist must lock is quieter
// than the room that fools the tracker** (a peak of 0.0023 against 0.006). So
// no threshold on level can work, and the rule has to be about the shape of
// what the network says. This probe measures the four candidates on both
// listening paths:
//
//   p50        the activation's floor - how quiet the network goes between
//              beats. A pulse returns to nothing; a room never does.
//   p50/p95    the same, scaled by how tall the peaks are.
//   >0.4       how often the network clears its own beat gate.
//   span       how far the reported tempo then wanders, and `fermo`, the
//              longest stretch it holds inside 1%. "Wait until the tempo has
//              settled" was the other candidate rule, and this is what killed
//              it: the quietest room here locks at 110.5 BPM and holds it dead
//              still for 31.5 s, where a real band gets as low as 5.5.
//   FOLLOWING  and what the tracker does with all of it.
//
// The answer, measured: p50 separates in MIXER (room 0.051-0.079 against music
// 0.001-0.033) and fails in IPAD, where the room path smears the gaps shut
// (room 0.055-0.106 against music 0.019-0.048). >0.4 separates in both but by
// 0.9% against 1.5%, and the room's share of that arrives in a burst in the
// first seconds - which is exactly when the decision is made. Nothing here has
// a margin worth shipping, and a rule that turns away a real band on stage is a
// far worse failure than a wrong number on a screen before the gig.
//
// So the tracker still locks to a room, and what changed instead is what that
// lock is allowed to *do*: the percussion is held out until the analysis has
// heard the input start, which is a question about the input changing rather
// than about what it is. See `inputIsLive` in BeatTracker.
#include "Audio/VirtualPercussionEngine.h"

#include "probe_song_render.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace
{
using namespace vp::probe;

// The two ways the app can be listening. Same distinction VPProbe draws, and it
// matters here more than anywhere: the room path smears the gaps between beats
// shut, which is what breaks the most promising of the rules measured below.
enum class Listening { ipad, mixer };

// A room, not digital silence. One-pole filtered noise is the awkward case on
// purpose: it fluctuates, so the network has something to answer, where a
// steady hum or a mains buzz would not.
void renderRoom (std::vector<float>& dst, float peak, unsigned seed)
{
    std::mt19937 rng (seed);
    float lp = 0.0f, hi = 0.0f;
    for (auto& v : dst)
    {
        lp += (noiseAt (rng) - lp) * 0.05f;
        v = lp;
        hi = std::max (hi, std::fabs (lp));
    }
    if (hi > 0.0f)
        for (auto& v : dst)
            v *= peak / hi;
}

void run (const char* name, std::vector<float>& in, double sr, Listening mode)
{
    const int block = 256;
    vp::VirtualPercussionEngine eng;
    eng.prepare (sr, block, 1);
    eng.settings().followSource.store (static_cast<int> (
        mode == Listening::ipad ? vp::FollowSource::speaker : vp::FollowSource::kitMic));
    // The part silent: what is being measured is what the analysis makes of the
    // input, not what our own shaker adds to it.
    eng.settings().shakerEnabled.store (false);
    eng.settings().congasEnabled.store (false);
    eng.start();

    std::vector<float> oL (static_cast<size_t> (block), 0.0f);
    std::vector<float> oR (static_cast<size_t> (block), 0.0f);
    float* outs[2] = { oL.data(), oR.data() };

    std::vector<float> act;
    vp::EngineSnapshot last {};
    double followedAt = -1.0;
    // Once it has locked, how far the reported tempo then wanders, and the
    // longest stretch it holds inside 1%. A clock worth playing to settles; the
    // question is whether a clock locked to a room ever does.
    float lo = 1.0e9f, hi = 0.0f;
    float steadyRef = 0.0f;
    double steadyFrom = -1.0, steadyBest = 0.0;
    int pos = 0;
    while (pos + block <= static_cast<int> (in.size()))
    {
        const float* ins[1] = { in.data() + pos };
        eng.process (ins, 1, outs, 2, block);

        // Same reason as VPProbe --sync: without it the worker's scheduling
        // decides which activations the decoder sees, and the run stops being
        // repeatable.
        const int hop = static_cast<int> (std::ceil (441.0 * sr / 22050.0));
        while (eng.analysisBacklog() > hop)
            ;

        last = eng.snapshot();
        // The network's first second is the LSTM warming up, and it says so.
        if (static_cast<double> (pos) / sr > 1.0)
            act.push_back (last.pBeat);
        if (followedAt < 0.0 && last.state == vp::TrackingState::following)
            followedAt = static_cast<double> (pos) / sr;

        const double t = static_cast<double> (pos) / sr;
        if (followedAt >= 0.0 && last.bpm > 40.0f)
        {
            lo = std::min (lo, last.bpm);
            hi = std::max (hi, last.bpm);
            if (steadyRef > 40.0f && std::fabs (last.bpm - steadyRef) / steadyRef < 0.01f)
            {
                steadyBest = std::max (steadyBest, t - steadyFrom);
            }
            else
            {
                steadyRef = last.bpm;
                steadyFrom = t;
            }
        }
        pos += block;
    }

    std::vector<float> sorted = act;
    std::sort (sorted.begin(), sorted.end());
    auto quantile = [&sorted] (double p)
    {
        return sorted.empty() ? 0.0f
                              : sorted[static_cast<size_t> (p * static_cast<double> (sorted.size() - 1))];
    };
    const float p50 = quantile (0.5);
    const float p95 = quantile (0.95);
    const double overGate = act.empty()
        ? 0.0
        : 100.0 * static_cast<double> (std::count_if (act.begin(), act.end(),
                                                      [] (float v) { return v > 0.40f; }))
              / static_cast<double> (act.size());

    std::printf ("%-24s %-9s %6.1f %7.2f | %6.3f %6.3f %6.2f %6.1f%% | %7.1f %6.1f\n",
                 name, followedAt < 0.0 ? "mai" : "FOLLOWING",
                 followedAt < 0.0 ? 0.0 : followedAt,
                 static_cast<double> (last.bpm),
                 static_cast<double> (p50), static_cast<double> (p95),
                 static_cast<double> (p50 / std::max (p95, 1.0e-6f)), overGate,
                 static_cast<double> (hi >= lo ? hi - lo : 0.0f), steadyBest);
}
} // namespace

int main (int argc, char** argv)
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr * 40.0);
    const bool quick = argc > 1 && std::strcmp (argv[1], "--quick") == 0;

    // How far down the band is turned. 0.05 is about where a quiet feed sits
    // once the make-up gain is against its ceiling, which is the case the host
    // tests already pin.
    const float bandScale = 0.05f;

    for (int ipad = 0; ipad < 2; ++ipad)
    {
        const auto mode = ipad ? Listening::ipad : Listening::mixer;
        std::printf ("\n# %s\n", ipad ? "IPAD (cassa -> stanza -> microfono)"
                                      : "MIXER (linea, nessuna stanza)");
        std::printf ("%-24s %-9s %6s %7s | %6s %6s %6s %7s | %7s %6s\n",
                     "ingresso", "stato", "a (s)", "bpm", "p50", "p95", "p50/95",
                     ">0.4", "span", "fermo");

        for (float peak : { 0.0006f, 0.006f, 0.03f })
        {
            std::vector<float> room (static_cast<size_t> (n), 0.0f);
            renderRoom (room, peak, 99u);
            if (ipad)
                speakerRoomMic (room, sr, 99u, 0.55f);
            char name[64];
            std::snprintf (name, sizeof name, "stanza vuota, picco %.4f",
                           static_cast<double> (peak));
            run (name, room, sr, mode);
        }

        const char* styles[] = { "straight", "syncopated", "pad", "sync+pad", "half-time" };
        const float tempos[] = { 76.0f, 120.0f, 140.0f };
        for (const char* style : styles)
        {
            for (float bpm : tempos)
            {
                if (quick && std::strcmp (style, "straight") != 0)
                    continue;
                SongOptions o;
                o.bpm = bpm;
                o.syncopated = std::strcmp (style, "syncopated") == 0
                               || std::strcmp (style, "sync+pad") == 0;
                o.sustained = std::strcmp (style, "pad") == 0
                              || std::strcmp (style, "sync+pad") == 0
                              || std::strcmp (style, "half-time") == 0;
                o.halfTimeFeel = std::strcmp (style, "half-time") == 0;

                const unsigned seed = static_cast<unsigned> (bpm) * 7u + 13u;
                std::vector<float> band (static_cast<size_t> (n), 0.0f);
                renderSong (band, o, sr, seed);
                if (ipad)
                    speakerRoomMic (band, sr, seed, 0.55f);
                for (auto& v : band)
                    v *= bandScale;

                char name[64];
                std::snprintf (name, sizeof name, "band %s %.0f", style,
                               static_cast<double> (bpm));
                run (name, band, sr, mode);
            }
        }
    }
    return 0;
}
