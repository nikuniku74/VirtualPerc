// Does the app know which of the four quarters is the one?
//
// Everything else in this tree measures the *beat*: how fast it is, how still
// it holds, whether a stroke doubles. None of that answers the question a
// listener asks first, because none of it can be wrong while the app still
// "goes a tempo" - and the bar can. A bar rotated by one quarter puts the
// accent on the two, and the app sounds like it is playing a different song at
// the same speed.
//
// So this drives the whole tracker over rendered material whose beat one is at
// sample zero - the renderer's grid is the ground truth - and asks two things:
//
//   entry     the quarter the percussion actually came in on, against the true
//             one. This is the moment a listener judges, and the one the
//             tracker gets exactly one attempt at.
//   held      the quarter the bar sat on over the last twenty seconds, as the
//             mode of the offset sampled every block. A bar that is right at
//             entry and rotates later is not right.
//
// Both are reported as an offset in quarters: 0 is the one, 2 is the bar
// counted from the three, and 1 or 3 is a bar counted from a backbeat.
//
// And a third thing, which is the one that says whether a miss is worth
// chasing: the downbeat activation the network produced, filed by the quarter
// the beat truly fell on. A bar decided wrongly on evidence that pointed the
// right way is the tracker's to fix. A bar decided wrongly on evidence whose
// maximum is on the wrong quarter is not, and through an iPad speaker and a
// room that is what this material gives it.
#include "AI/BeatModelConfig.h"
#include "Tracking/BeatTracker.h"

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

#include "probe_song_render.h"

using namespace vp::probe;

namespace
{
constexpr double kSr = 48000.0;
constexpr int    kBlock = 512;

// The room the app has been listening to before anybody plays, at the level
// probe_song.cpp uses for it.
constexpr float kRoomPeak = 0.0035f;

struct Result
{
    int  entryOffset = -1;      // quarters between the entry and the true one
    double entryAt = -1.0;      // seconds after the band started
    int  heldOffset = -1;       // the bar's own count over the last 20 s
    float heldShare = 0.0f;     // how much of that window it held it for
    int  rotations = 0;         // times the offset settled somewhere else
    float octave = 0.0f;        // log2 of the clock's tempo over the true one
    bool played = false;

    /** The bar cannot be right or wrong at a metrical level that is not the
        song's: at half tempo the count advances half as fast and the offset
        walks all the way round whatever the alignment does. Those runs say
        something about the octave logic and nothing about the bar, so they are
        reported and excluded rather than scored. */
    bool octaveOk() const noexcept { return std::fabs (octave) < 0.20f; }
};

/** Every beat the decoder counts, filed under the quarter of the bar it truly
    fell on. This is the evidence the bar decision is made from, before any
    decision is made: it says what there is to work with, which is the one thing
    an end-to-end score cannot. */
struct Evidence
{
    double sum[4] {};       // total downbeat activation per true quarter
    int    n[4] {};         // beats counted there
    int    gated[4] {};     // and how many of them cleared the event gate
    int    barWins[4] {};   // which quarter won its bar outright
    /** Where the tracker files each beat, against where it truly fell. The
        histogram is only as good as this mapping: a constant offset here
        rotates the whole bar however clean the evidence is. */
    int    filedAt[4] {};
    /** How near the middle of a quarter the mapping lands. A beat filed at 0.5
        is a coin toss between two quarters, and a mapping that sits there is
        what splits a histogram in two whatever the network says. */
    double borderSum = 0.0;
    int    borderN = 0, borderline = 0;

    void add (const Evidence& o) noexcept
    {
        for (int i = 0; i < 4; ++i)
        {
            sum[i] += o.sum[i];
            n[i] += o.n[i];
            gated[i] += o.gated[i];
            barWins[i] += o.barWins[i];
            filedAt[i] += o.filedAt[i];
            if (i == 0)
            {
                borderSum += o.borderSum;
                borderN += o.borderN;
                borderline += o.borderline;
            }
        }
    }
};

Result run (SongOptions opt, unsigned seed, bool speaker, double preSeconds,
            double seconds, double startAt, Evidence* evidence = nullptr,
            bool trace = false)
{
    const int n = static_cast<int> (kSr * seconds);
    const int preN = (static_cast<int> (kSr * preSeconds) / kBlock) * kBlock;

    std::vector<float> song (static_cast<size_t> (preN + n), 0.0f);
    std::vector<double> truePhase;
    {
        std::vector<float> body (static_cast<size_t> (n), 0.0f);
        renderSong (body, opt, kSr, seed, &truePhase);
        std::copy (body.begin(), body.end(), song.begin() + preN);
    }
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

    if (speaker)
        speakerRoomMic (song, kSr, seed, 0.55f);

    vp::BeatTracker tracker;
    tracker.prepare (kSr);
    tracker.setSpeakerFollow (speaker);
    tracker.stop();

    Result r;
    int  histogram[4] {};
    int  candidate = -1, settled = -1, candidateSamples = 0;
    bool started = false;
    double bpmAcc = 0.0;
    int    bpmN = 0;

    // An offset is only a rotation once it has held. Sampled every block, the
    // quarter the clock is in and the quarter the song is in change a few
    // milliseconds apart, so the raw offset flickers on every beat boundary and
    // counting those made a bar that never moved look like it moved two hundred
    // times.
    const int settleSamples = static_cast<int> (kSr * 0.30);

    const int hop = static_cast<int> (std::ceil (vp::kBeatModelHop * kSr
                                                 / vp::kBeatModelSampleRate));
    const double holdFrom = seconds - 20.0;

    uint32_t lastSerial = 0;
    bool seenSerial = false;
    float barRun[4] {};
    int   barRunQuarter[4] {};
    int   barRunFilled = 0;

    for (int pos = 0; pos + kBlock <= preN + n; pos += kBlock)
    {
        // The engine raises the epoch when the input changes character, which
        // is what a room turning into a band looks like to it. Here the moment
        // is known exactly, so it is declared rather than detected: what is
        // being measured is the bar, not the level detector.
        tracker.setInputEpoch (pos >= preN ? 1u : 0u);

        const double t = static_cast<double> (pos - preN) / kSr;
        if (! started && t >= startAt)
        {
            tracker.start();
            started = true;
        }

        const auto out = tracker.process (song.data() + pos, kBlock);

        // Drain the analysis before feeding it more, so the run is repeatable:
        // otherwise how far behind the worker happens to be is the host's
        // scheduler's decision, and that decides which activations the decoder
        // sees. See the same wait in probe_song.cpp.
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds (50);
        while (tracker.analysisBacklog() > hop
               && std::chrono::steady_clock::now() < until)
            std::this_thread::yield();

        if (evidence != nullptr)
        {
            vp::BeatHypothesis hyp;
            if (tracker.tryLoadHypothesis (hyp) && hyp.valid)
            {
                if (! seenSerial)
                {
                    lastSerial = hyp.beatSerial;
                    seenSerial = true;
                }
                else if (hyp.beatSerial != lastSerial)
                {
                    lastSerial = hyp.beatSerial;
                    const auto at = static_cast<size_t> (hyp.analysisSample);
                    if (at < truePhase.size() && hyp.analysisSample >= preN)
                    {
                        // Rounded, not floored: a beat sits exactly on the
                        // boundary between two quarters, so flooring there lets
                        // twenty milliseconds of pipeline geometry decide which
                        // quarter a beat is filed under.
                        const int q = static_cast<int> (std::llround (truePhase[at])) & 3;
                        // The same arithmetic BeatTracker does, so the
                        // mapping it uses is measured rather than assumed.
                        const float beatSec = hyp.periodSec > 0.01f ? hyp.periodSec : 0.5f;
                        const float leadBeats = out.leadMs * 0.001f / beatSec;
                        const float songPhase = vp::wrap01 (hyp.beatPhase + leadBeats);
                        const float posNow = out.barPhase * 4.0f;
                        const int nearest = static_cast<int> (std::lround (posNow - songPhase));
                        const int at = ((nearest % 4) + 4) & 3;
                        ++evidence->filedAt[((at - q) % 4 + 4) & 3];
                        const double x = posNow - songPhase;
                        const double dist = std::fabs (x - std::round (x));
                        evidence->borderSum += dist;
                        ++evidence->borderN;
                        evidence->borderline += dist > 0.35 ? 1 : 0;

                        evidence->sum[q] += hyp.beatDownbeat;
                        ++evidence->n[q];
                        evidence->gated[q] += hyp.beatDownbeat > 0.40f;

                        // Four beats in a row, whichever quarters they turn out
                        // to be: the winner of a group of four is the same
                        // quarter however the group is aligned, so this needs no
                        // bar to have been found yet.
                        barRun[barRunFilled & 3] = hyp.beatDownbeat;
                        barRunQuarter[barRunFilled & 3] = q;
                        ++barRunFilled;
                        if (barRunFilled >= 4 && (barRunFilled & 3) == 0)
                        {
                            int best = 0;
                            for (int i = 1; i < 4; ++i)
                                if (barRun[i] > barRun[best])
                                    best = i;
                            ++evidence->barWins[barRunQuarter[best]];
                        }
                    }
                }
            }
        }

        if (pos < preN)
            continue;

        // Where the bar is, and where it should be. Both as a quarter index, so
        // the difference is a rotation and not a phase.
        const double tp = truePhase[static_cast<size_t> (pos)];
        const int trueBeat = static_cast<int> (std::floor (tp)) & 3;
        const int clockBeat = std::clamp (static_cast<int> (out.barPhase * 4.0f), 0, 3);
        const int offset = ((clockBeat - trueBeat) % 4 + 4) & 3;

        if (trace && t > startAt - 3.0 && t < startAt + 4.0)
            std::printf ("   t=%6.2f  bar=%.2f clockQ=%d trueQ=%d off=%d  stato=%d  suona=%d  lead=%.0fms (%.2f battiti)\n",
                         t, static_cast<double> (out.barPhase), clockBeat, trueBeat, offset,
                         static_cast<int> (out.state), out.percussionShouldPlay ? 1 : 0,
                         static_cast<double> (out.leadMs),
                         out.bpm > 40.0f ? out.leadMs * 0.001 * out.bpm / 60.0 : 0.0);

        if (out.percussionShouldPlay)
        {
            if (! r.played)
            {
                r.played = true;
                r.entryAt = t;

                // Read off the entry pulse itself, not off the block. The part
                // comes in *on* a beat, so a comparison made at the block's own
                // boundary is comparing two quantities that are both changing
                // at that instant, and it reported a whole quarter of error
                // that was nothing but which side of the boundary each one had
                // reached. The pulse carries its own sample offset and its own
                // count of the bar, so both sides of the comparison are exact.
                r.entryOffset = offset;
                for (int i = 0; i < out.clock.pulsesFired; ++i)
                {
                    if (out.clock.pulseIndex[i] != 0)
                        continue;
                    const auto at = static_cast<size_t> (pos + out.clock.pulseOffset[i]);
                    if (at >= truePhase.size())
                        break;
                    const int trueHere = static_cast<int> (std::llround (truePhase[at])) & 3;
                    r.entryOffset = ((out.clock.pulseBeatInBar[i] - trueHere) % 4 + 4) & 3;
                    break;
                }
            }

            if (offset == candidate)
                candidateSamples += kBlock;
            else
            {
                candidate = offset;
                candidateSamples = 0;
            }
            if (candidateSamples >= settleSamples && candidate != settled)
            {
                if (settled >= 0)
                    ++r.rotations;
                settled = candidate;
            }

            if (t >= holdFrom)
            {
                ++histogram[settled >= 0 ? settled : offset];
                if (out.bpm > 40.0f)
                {
                    bpmAcc += out.bpm;
                    ++bpmN;
                }
            }
        }
    }

    if (bpmN > 0)
        r.octave = static_cast<float> (std::log2 ((bpmAcc / bpmN) / static_cast<double> (opt.bpm)));

    int total = 0, best = 0;
    for (int i = 0; i < 4; ++i)
    {
        total += histogram[i];
        if (histogram[i] > histogram[best])
            best = i;
    }
    if (total > 0)
    {
        r.heldOffset = best;
        r.heldShare = static_cast<float> (histogram[best]) / static_cast<float> (total);
    }
    return r;
}
} // namespace

int main (int argc, char** argv)
{
    bool speaker = true, trace = false;
    double preSeconds = 8.0, seconds = 60.0, startAt = 12.0;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--trace") trace = true;
        else if (a == "--mixer") speaker = false;
        else if (a == "--ipad") speaker = true;
        else if (a == "--seconds" && i + 1 < argc) seconds = std::atof (argv[++i]);
        else if (a == "--start" && i + 1 < argc) startAt = std::atof (argv[++i]);
    }

    std::vector<SongOptions> styles;
    { SongOptions o; styles.push_back (o); }
    { SongOptions o; o.syncopated = true; styles.push_back (o); }
    { SongOptions o; o.sustained = true; styles.push_back (o); }
    { SongOptions o; o.syncopated = true; o.sustained = true; styles.push_back (o); }
    { SongOptions o; o.halfTimeFeel = true; o.sustained = true; styles.push_back (o); }

    const float tempos[] = { 76.0f, 92.0f, 104.0f, 118.0f, 128.0f, 140.0f };

    std::printf ("# ascolto: %s   START a t=%.0f s, la band attacca a t=0\n",
                 speaker ? "iPad (speaker + stanza + microfono)" : "mandata di linea", startAt);
    std::printf ("# offset in quarti: 0 = l'uno, 2 = la battuta contata dal tre\n");
    std::printf ("# ev / marg: il quarto su cui la rete somma il downbeat, e di quanto stacca il secondo\n");
    std::printf ("%-11s %-6s %-7s %-7s %-7s %-6s %-5s %-6s %-5s %-6s\n",
                 "style", "bpm", "entry", "t_in", "held", "share", "rot", "oct", "ev", "marg");

    Evidence total;
    int nRuns = 0, entryOne = 0, heldOne = 0, nPlayed = 0, totalRot = 0;
    int nScored = 0, nOctave = 0;
    int entryHist[4] {}, heldHist[4] {};
    double entryAcc = 0.0;

    for (const auto& style : styles)
    {
        for (float bpm : tempos)
        {
            SongOptions o = style;
            o.bpm = bpm;
            Evidence ev;
            const Result r = run (o, static_cast<unsigned> (bpm) * 7u + 13u, speaker,
                                  preSeconds, seconds, startAt, &ev, trace);
            total.add (ev);
            ++nRuns;
            nOctave += ! r.octaveOk();
            if (r.octaveOk())
            {
                ++nScored;
                if (r.played)
                {
                    ++nPlayed;
                    entryAcc += r.entryAt;
                    ++entryHist[r.entryOffset];
                    entryOne += r.entryOffset == 0;
                    totalRot += r.rotations;
                }
                if (r.heldOffset >= 0)
                {
                    ++heldHist[r.heldOffset];
                    heldOne += r.heldOffset == 0;
                }
            }

            // What the network gave this run to work with: the quarter its
            // downbeat activation adds up on, and by how much it leads the
            // runner-up. A miss with a wide margin here is the tracker's; a
            // miss with the margin on another quarter is the network's, and no
            // amount of counting downstream can turn it round.
            double share[4] {}, tot = 0.0;
            for (int i = 0; i < 4; ++i)
                tot += ev.n[i] > 0 ? ev.sum[i] : 0.0;
            for (int i = 0; i < 4; ++i)
                share[i] = tot > 0.0 ? ev.sum[i] / tot : 0.25;
            int evBest = 0;
            for (int i = 1; i < 4; ++i)
                if (share[i] > share[evBest]) evBest = i;
            double runnerUp = 0.0;
            for (int i = 0; i < 4; ++i)
                if (i != evBest) runnerUp = std::max (runnerUp, share[i]);

            std::printf ("%-11s %-6.0f %-7d %-7.2f %-7d %-6.2f %-5d %-6.2f %-5d %-6.2f %s%s%s\n",
                         styleName (o), static_cast<double> (bpm), r.entryOffset, r.entryAt,
                         r.heldOffset, static_cast<double> (r.heldShare), r.rotations,
                         static_cast<double> (r.octave),
                         evBest, share[evBest] - runnerUp,
                         ! r.octaveOk() ? "OCT" : "",
                         r.octaveOk() && r.entryOffset != 0 ? "ENTRY " : "",
                         r.octaveOk() && r.heldOffset != 0 ? "HELD" : "");
            std::fflush (stdout);
        }
    }

    std::printf ("\n=== %d brani, %s ===\n", nRuns,
                 speaker ? "iPad" : "linea");
    if (nOctave > 0)
        std::printf ("livello metrico sbagliato %d   <- esclusi: a meta' tempo la battuta non e' ne' giusta ne' storta\n",
                     nOctave);
    std::printf ("entra sull'uno    %d/%d\n", entryOne, nPlayed);
    std::printf ("tiene l'uno       %d/%d\n", heldOne, nScored);
    std::printf ("offset all'entrata  0:%d 1:%d 2:%d 3:%d\n",
                 entryHist[0], entryHist[1], entryHist[2], entryHist[3]);
    std::printf ("offset a regime     0:%d 1:%d 2:%d 3:%d\n",
                 heldHist[0], heldHist[1], heldHist[2], heldHist[3]);
    std::printf ("rotazioni mentre suona %d\n", totalRot);
    std::printf ("attesa media prima di entrare %.2f s\n",
                 nPlayed > 0 ? entryAcc / nPlayed : -1.0);

    // What there was to work with, before anything decided anything. Filed by
    // the quarter the beat truly fell on, so quarter 0 is the one.
    std::printf ("\n--- l'attivazione di downbeat, per quarto vero ---\n");
    int nAll = 0, gatedAll = 0, winsAll = 0;
    for (int i = 0; i < 4; ++i)
    {
        nAll += total.n[i];
        gatedAll += total.gated[i];
        winsAll += total.barWins[i];
    }
    std::printf ("%-10s %-9s %-9s %-9s\n", "quarto", "media", "oltre 0.40", "vince");
    for (int i = 0; i < 4; ++i)
        std::printf ("%-10d %-9.3f %-9.2f %-9.2f\n", i,
                     total.n[i] > 0 ? total.sum[i] / total.n[i] : 0.0,
                     gatedAll > 0 ? static_cast<double> (total.gated[i]) / gatedAll : 0.0,
                     winsAll > 0 ? static_cast<double> (total.barWins[i]) / winsAll : 0.0);
    std::printf ("%d battiti, %d oltre la soglia (%.0f%%)\n", nAll, gatedAll,
                 nAll > 0 ? 100.0 * gatedAll / nAll : 0.0);
    std::printf ("dove il tracker archivia ogni battito, meno dove e' caduto davvero: "
                 "0:%d 1:%d 2:%d 3:%d\n",
                 total.filedAt[0], total.filedAt[1], total.filedAt[2], total.filedAt[3]);
    std::printf ("quanto e' netto l'incasellamento: %.3f dal quarto piu' vicino, "
                 "%.0f%% oltre 0.35 (una moneta)\n",
                 total.borderN > 0 ? total.borderSum / total.borderN : -1.0,
                 total.borderN > 0 ? 100.0 * total.borderline / total.borderN : -1.0);
    return 0;
}
