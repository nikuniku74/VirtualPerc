#include "Audio/VirtualPercussionEngine.h"
#include "Percussion/PercussionEngine.h"
#include "TestAiBeat.h"
#include "TestLoops.h"
#include "Tracking/TempoFollower.h"

#include "../scripts/probe_song_render.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace
{
    int gFailed = 0;
    int gPassed = 0;

    void expect (bool cond, const char* name)
    {
        if (cond)
        {
            ++gPassed;
            std::printf ("  PASS  %s\n", name);
        }
        else
        {
            ++gFailed;
            std::printf ("  FAIL  %s\n", name);
        }
    }


    void feedSilence (vp::VirtualPercussionEngine& eng, double sr, int block, float seconds)
    {
        const int n = static_cast<int> (sr * static_cast<double> (seconds));
        std::vector<float> silence (static_cast<size_t> (std::max (block, n)), 0.0f);
        std::vector<float> outL (static_cast<size_t> (block), 0.0f);
        std::vector<float> outR (static_cast<size_t> (block), 0.0f);
        float* outs[2] = { outL.data(), outR.data() };
        int pos = 0;
        while (pos + block <= n)
        {
            const float* ins[1] = { silence.data() + pos };
            eng.process (ins, 1, outs, 2, block);
            pos += block;
        }
    }
    // ---------------------------------------------------------------------
    // The leak canceller bench.
    //
    // Extracted from main() so `--leak` can run it on its own: the sweep is a
    // hundred-odd engine runs and iterating on it inside the full suite means
    // waiting for the neural worker every time.
    //
    // The mic hears the app's own output delayed by the round trip, so the
    // reference has to be older than the block is long - otherwise the tail of
    // the block would need output that has not been rendered yet. 150 ms covers
    // every buffer size used here. `reportedLatencyMs` is a parameter because
    // the honest default is *zero* - nobody has measured the rig - and a
    // reference taken 8 ms back is where a grid-locked part looks most like the
    // band. See the no-leak bench below.
    // ---------------------------------------------------------------------
    struct LeakRun
    {
        /** Mean of leakRemain/inputPeak over the counted blocks: the share of
            our own return the analysis is still carrying. 1.0 is "not
            subtracted at all". */
        float remain = 1.0f;
        /** The worst single counted block. A mean hides one badly missed onset,
            and one missed onset is what the tracker follows. */
        float worstBlock = 1.0f;
        int   counted = 0;
        /** The rendered part, block by block, when asked for. The canceller is
            an analysis-only seam: switching it changes what the tracker is
            handed and must not change a sample of what the listener hears. */
        std::vector<float> output;
    };

    LeakRun leakRun (double sr, int blk, vp::FollowSource source, float acousticExtraMs,
                     int style, vp::Subdivision subdivision, bool cancellationEnabled,
                     bool captureOutput, float reportedLatencyMs = 150.0f)
    {
        vp::VirtualPercussionEngine eng;
        eng.prepare (sr, blk, 1);
        eng.settings().followSource.store (static_cast<int> (source));
        eng.settings().masterVolume.store (1.0f);
        eng.settings().reverbAmount.store (0.0f);
        eng.settings().subdivision.store (static_cast<int> (subdivision));
        eng.settings().grooveAuto.store (false);
        if (style >= 0)
            eng.settings().grooveStyle.store (style);
        eng.setLeakCancellationEnabledForTest (cancellationEnabled);
        // Hold the part still. This measures the canceller, and the canceller's
        // residual depends on how much part there is to cancel - which is why it
        // is run over every style and every subdivision rather than whichever
        // one happens to be the default. Dynamics would move it for the same
        // reason and for a reason that is an artefact here: the only thing on
        // this rig's input is the app's own return, so the band the dynamics
        // would be reading is us.
        eng.settings().dynamicsFollow.store (false);
        eng.setReportedLatencyMs (reportedLatencyMs);
        eng.setFixedBpm (120.0f);

        const double back = std::max (0.008, static_cast<double> (reportedLatencyMs) * 0.001);
        const int delay = static_cast<int> ((back + acousticExtraMs * 0.001) * sr);
        std::vector<float> history (static_cast<size_t> (delay + blk * 4), 0.0f);
        int histWrite = 0;

        std::vector<float> in (static_cast<size_t> (blk), 0.0f);
        std::vector<float> oL (static_cast<size_t> (blk), 0.0f);
        std::vector<float> oR (static_cast<size_t> (blk), 0.0f);
        float* outs[2] = { oL.data(), oR.data() };

        eng.start();
        eng.tapAt (0.0);
        eng.tapAt (0.5);
        eng.tapAt (1.0);
        eng.tapAt (1.5);

        double sum = 0.0;
        float worst = 0.0f;
        std::vector<float> rendered;
        if (captureOutput)
            rendered.reserve (static_cast<size_t> (sr * 12.0));
        int counted = 0;
        const int blocks = static_cast<int> (sr * 8.0) / blk;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blk; ++i)
            {
                const int ri = (histWrite - delay + i + static_cast<int> (history.size()))
                               % static_cast<int> (history.size());
                in[static_cast<size_t> (i)] = history[static_cast<size_t> (ri)] * 0.6f;
            }
            const float* ins[1] = { in.data() };
            eng.process (ins, 1, outs, 2, blk);
            for (int i = 0; i < blk; ++i)
            {
                history[static_cast<size_t> (histWrite)] =
                    0.5f * (oL[static_cast<size_t> (i)] + oR[static_cast<size_t> (i)]);
                histWrite = (histWrite + 1) % static_cast<int> (history.size());
            }
            // Every block, not only the counted ones: this is the listener's
            // signal, and the A/B has to cover the acquisition as well.
            if (captureOutput)
            {
                rendered.insert (rendered.end(), oL.begin(), oL.end());
                rendered.insert (rendered.end(), oR.begin(), oR.end());
            }
            const auto snap = eng.snapshot();
            if (b > blocks / 3 && snap.inputPeak > 0.08f)
            {
                const float ratio = snap.leakRemain / snap.inputPeak;
                sum += static_cast<double> (ratio);
                worst = std::max (worst, ratio);
                ++counted;
            }
        }
        LeakRun out;
        out.remain = counted > 0 ? static_cast<float> (sum / counted) : 1.0f;
        out.worstBlock = counted > 0 ? worst : 1.0f;
        out.counted = counted;
        out.output = std::move (rendered);
        return out;
    }

    /** The same closed loop, but the mic hears the app through the iPad's own
        speaker in a room, built from the reflection taps, gains, band limits and
        noise floor of `vp::probe::speakerRoomMic` - the room this repository
        measures everything else in.

        The bench above hands the canceller an exact scaled copy of its own output
        at a whole number of samples, and a converged two-band fit removes
        essentially all of it. A room is not that, and the difference matters
        enough to be measured one cause at a time. Same rig, 8 s, funk at
        eighths, `leakRemain/inputPeak`:

            exact copy, integer delay                     0.0000
            + delay 0.373 of a sample off the grid         0.0957
            + a -56 dB noise floor                         0.0045
            + 260 Hz/9 kHz band limiting, nothing else     0.8050
            + one wall at 7.3 ms and a second at 14.6 ms   0.8484
            everything, one wall                           0.8755   (off 0.9903)
            everything, the full eight-tap tail            0.9150   (off 0.9902)

        Against the cancellation-off control on each fixture, the share of the
        return actually removed: **90.2%** when the direct component reaches the
        mic spectrally unmodified at a fractional delay (0.0963 against 0.9791);
        **18.9%** once that same direct component is band-shaped by the fixture's
        260 Hz and 9 kHz limits and nothing else changes (0.8050 against 0.9928);
        **11.5%** on the complete one-wall fixture (0.8755 against 0.9903) and
        **7.6%** with the full eight-tap tail (0.9150 against 0.9902).

        So: a return at a fractional delay is still cancelled. A return whose
        *spectrum* has been reshaped inside each band is largely not - two gains
        cannot follow a 260 Hz high pass through a conga - and neither is a
        reflection that is not in the reference at any single delay. The
        0.805-0.915 residual is where those two limits of a two-band, single-delay
        model put the floor for this fixture; it is not a regression, and it is not
        a claim that the direct arrival has been taken out of a room. The 0.0000
        above is a statement about the estimator on an exact copy, and nothing
        more. */
    enum class RoomModel { fractional, oneWall, eightWalls };

    LeakRun roomLeakRun (double sr, int blk, bool cancellationEnabled, int style,
                         RoomModel model)
    {
        vp::VirtualPercussionEngine eng;
        eng.prepare (sr, blk, 1);
        eng.settings().followSource.store (static_cast<int> (vp::FollowSource::speaker));
        eng.settings().masterVolume.store (1.0f);
        eng.settings().reverbAmount.store (0.0f);
        eng.settings().subdivision.store (static_cast<int> (vp::Subdivision::eighth));
        eng.settings().grooveAuto.store (false);
        eng.settings().grooveStyle.store (style);
        eng.settings().dynamicsFollow.store (false);
        eng.setLeakCancellationEnabledForTest (cancellationEnabled);
        eng.setReportedLatencyMs (150.0f);
        eng.setFixedBpm (120.0f);

        const int taps[8] = { 411, 967, 1733, 2591, 3701, 5273, 7639, 11003 };
        const float gains[8] = { 0.42f, 0.31f, 0.26f, 0.20f, 0.16f, 0.12f, 0.09f, 0.06f };
        vp::probe::Biquad hp1, hp2, lp;
        hp1.highpass (sr, 260.0, 0.707);
        hp2.highpass (sr, 260.0, 0.707);
        lp.lowpass (sr, 9000.0, 0.707);
        std::mt19937 rng (839u ^ 0x5eedu);

        // 150 ms device plus a 25.37 ms flight: not a whole number of samples,
        // which is the first thing an exact-copy bench does not test. The
        // reference the canceller keeps is sampled on the grid, so the best any
        // integer delay can do here is 0.37 of a sample out.
        const double directFrac = 0.175373 * sr;
        const int direct = static_cast<int> (directFrac);
        const double frac = directFrac - direct;
        const int span = direct + taps[7] + blk * 4;
        std::vector<float> history (static_cast<size_t> (span), 0.0f);
        int histWrite = 0;

        std::vector<float> in (static_cast<size_t> (blk), 0.0f);
        std::vector<float> oL (static_cast<size_t> (blk), 0.0f);
        std::vector<float> oR (static_cast<size_t> (blk), 0.0f);
        float* outs[2] = { oL.data(), oR.data() };

        eng.start();
        eng.tapAt (0.0);
        eng.tapAt (0.5);
        eng.tapAt (1.0);
        eng.tapAt (1.5);

        double sum = 0.0;
        float worst = 0.0f;
        int counted = 0;
        const int size = static_cast<int> (history.size());
        const int blocks = static_cast<int> (sr * 8.0) / blk;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blk; ++i)
            {
                const int at = histWrite - direct + i;
                const float d0 = history[static_cast<size_t> ((at + size) % size)];
                const float d1 = history[static_cast<size_t> ((at - 1 + size) % size)];
                float s = static_cast<float> ((1.0 - frac)) * d0 + static_cast<float> (frac) * d1;
                // One wall at 7.3 ms and 0.35, or the whole eight-tap tail.
                if (model == RoomModel::eightWalls)
                {
                    for (int k = 0; k < 8; ++k)
                        s += gains[k]
                             * history[static_cast<size_t> ((at - taps[k] + size) % size)];
                }
                else if (model == RoomModel::oneWall)
                {
                    const int w = static_cast<int> (0.0073 * sr);
                    s += 0.35f * history[static_cast<size_t> ((at - w + size) % size)]
                         + 0.12f * history[static_cast<size_t> ((at - 2 * w + size) % size)];
                }
                if (model != RoomModel::fractional)
                    s = lp.process (hp2.process (hp1.process (s)));
                s += 0.0016f * vp::probe::noiseAt (rng);
                in[static_cast<size_t> (i)] = std::clamp (s * 0.6f, -1.0f, 1.0f);
            }
            const float* ins[1] = { in.data() };
            eng.process (ins, 1, outs, 2, blk);
            for (int i = 0; i < blk; ++i)
            {
                history[static_cast<size_t> (histWrite)] =
                    0.5f * (oL[static_cast<size_t> (i)] + oR[static_cast<size_t> (i)]);
                histWrite = (histWrite + 1) % size;
            }
            const auto snap = eng.snapshot();
            if (b > blocks / 3 && snap.inputPeak > 0.08f)
            {
                const float ratio = snap.leakRemain / snap.inputPeak;
                sum += static_cast<double> (ratio);
                worst = std::max (worst, ratio);
                ++counted;
            }
        }
        LeakRun out;
        out.remain = counted > 0 ? static_cast<float> (sum / counted) : 1.0f;
        out.worstBlock = counted > 0 ? worst : 1.0f;
        out.counted = counted;
        return out;
    }

    /** One sharp click per beat at `bpm`, first click on sample zero. Stands in
        for a band that is on the same grid as the part - which is the case the
        canceller has to be able to leave alone. */
    void renderBandClick (std::vector<float>& dest, float bpm, double sr, float amp)
    {
        std::fill (dest.begin(), dest.end(), 0.0f);
        const double beatSamples = 60.0 / static_cast<double> (bpm) * sr;
        const int beats = static_cast<int> (static_cast<double> (dest.size()) / beatSamples);
        const int len = static_cast<int> (0.05 * sr);
        for (int b = 0; b < beats; ++b)
        {
            const size_t at = static_cast<size_t> (static_cast<double> (b) * beatSamples);
            uint32_t rng = 0x9e3779b9u ^ static_cast<uint32_t> (b);
            for (int i = 0; i < len && at + static_cast<size_t> (i) < dest.size(); ++i)
            {
                const float t = static_cast<float> (i) / static_cast<float> (sr);
                rng = rng * 1664525u + 1013904223u;
                const float noise = static_cast<float> (rng >> 8) / 8388608.0f - 1.0f;
                dest[at + static_cast<size_t> (i)] =
                    (0.5f * noise + 0.5f * std::sin (2.0f * 3.14159265f * 1000.0f * t))
                    * std::exp (-t * 120.0f) * (b % 4 == 0 ? 0.9f : 0.6f) * amp;
            }
        }
    }

    struct NoLeakRun
    {
        std::vector<float> inPeak;
        std::vector<float> remain;
    };

    /** A feed that carries **no** leak at all, while the part the app is playing
        sits on the same grid as the band. Nothing here may be subtracted: our
        own output is genuinely correlated with the input, and every sample the
        canceller takes off is a real onset taken away from the tracker.

        The reported latency is left at zero on purpose - that is what a rig
        nobody has measured reports, and it puts the canceller's reference 8 ms
        back, which is where a grid-locked part looks most like the band.
        Measured: at 150 ms the same bench moves nothing at all on either path,
        so zero is both the honest default and the hard case. */
    NoLeakRun noLeakRun (double sr, int blk, vp::FollowSource source, bool cancellationEnabled)
    {
        vp::VirtualPercussionEngine eng;
        eng.prepare (sr, blk, 1);
        eng.settings().followSource.store (static_cast<int> (source));
        eng.settings().masterVolume.store (1.0f);
        eng.settings().reverbAmount.store (0.0f);
        eng.settings().subdivision.store (static_cast<int> (vp::Subdivision::eighth));
        eng.settings().grooveAuto.store (false);
        eng.settings().dynamicsFollow.store (false);
        eng.setLeakCancellationEnabledForTest (cancellationEnabled);
        eng.setReportedLatencyMs (0.0f);
        eng.setFixedBpm (120.0f);
        eng.start();
        eng.tapAt (0.0);
        eng.tapAt (0.5);
        eng.tapAt (1.0);
        eng.tapAt (1.5);

        const int blocks = static_cast<int> (sr * 8.0) / blk;
        std::vector<float> band (static_cast<size_t> (blocks * blk), 0.0f);
        renderBandClick (band, 120.0f, sr, 0.30f);
        std::vector<float> oL (static_cast<size_t> (blk), 0.0f);
        std::vector<float> oR (static_cast<size_t> (blk), 0.0f);
        float* outs[2] = { oL.data(), oR.data() };

        NoLeakRun out;
        out.inPeak.reserve (static_cast<size_t> (blocks));
        out.remain.reserve (static_cast<size_t> (blocks));
        for (int b = 0; b < blocks; ++b)
        {
            const float* ins[1] = { band.data() + static_cast<size_t> (b) * blk };
            eng.process (ins, 1, outs, 2, blk);
            const auto snap = eng.snapshot();
            out.inPeak.push_back (snap.inputPeak);
            out.remain.push_back (snap.leakRemain);
        }
        return out;
    }

    /** What the canceller did to a bus it should not have touched, measured three
        ways because one of them is not enough.

        `relRms` - the analysis peak sequence against the same sequence with the
        subtraction disabled, relative to it. This is the one that says how much
        of the tracker's input was moved overall.

        `worstAbs` - the largest single-block change, as a share of the run's own
        mean block peak. A ratio cannot say whether a change matters, and this
        gives the size of the largest change in the run's own units: an absolute
        bound that applies to every counted block, quiet ones included. What it is
        not is a *relative* bound on a quiet block - a tail at a fiftieth of the
        mean can still move by a large fraction of itself inside this figure.

        `worstGated` - the largest ratio among blocks carrying at least half the
        run's mean level. Ungated, this metric is dominated by the decay tails
        between the band's onsets, where the denominator is a few thousandths and
        a change of one part in a thousand reads as 1.2x. Measured on the shipped
        estimator through the speaker at 128 frames: 1.2049 ungated, on a block
        whose own peak is 0.0074 against a run mean of 0.045 and loud blocks of
        0.25, with an absolute change of 0.0015. The gated figure for the same run
        is 1.0374. Both are reported; the gated one is asserted, and `worstAbs`
        is what stops that gate from hiding anything. */
    struct NoLeakDamage
    {
        float relRms = 1.0f;
        float worstAbs = 1.0f;      // as a share of the mean counted block peak
        float worstGated = 1.0f;
        float worstUngated = 1.0f;
        float meanIn = 0.0f;
        int   counted = 0;
    };

    NoLeakDamage noLeakDamage (const NoLeakRun& on, const NoLeakRun& off)
    {
        NoLeakDamage d;
        const int blocks = static_cast<int> (on.remain.size());
        const int from = blocks / 3 + 1;
        double meanIn = 0.0;
        int m = 0;
        for (int b = from; b < blocks; ++b)
            if (off.inPeak[static_cast<size_t> (b)] > 0.002f)
            {
                meanIn += static_cast<double> (off.inPeak[static_cast<size_t> (b)]);
                ++m;
            }
        if (m == 0)
            return d;
        meanIn /= m;
        d.meanIn = static_cast<float> (meanIn);

        double sqDiff = 0.0, sqOff = 0.0, worstAbs = 0.0, worstGated = 0.0, worstUngated = 0.0;
        for (int b = from; b < blocks; ++b)
        {
            if (off.inPeak[static_cast<size_t> (b)] <= 0.002f)
                continue;
            const double a = on.remain[static_cast<size_t> (b)];
            const double c = off.remain[static_cast<size_t> (b)];
            sqDiff += (a - c) * (a - c);
            sqOff += c * c;
            worstAbs = std::max (worstAbs, std::fabs (a - c));
            if (c > 0.0)
            {
                worstUngated = std::max (worstUngated, a / c);
                if (static_cast<double> (off.inPeak[static_cast<size_t> (b)]) > 0.5 * meanIn)
                    worstGated = std::max (worstGated, a / c);
            }
            ++d.counted;
        }
        d.relRms = sqOff > 0.0 ? static_cast<float> (std::sqrt (sqDiff / sqOff)) : 0.0f;
        d.worstAbs = static_cast<float> (worstAbs / meanIn);
        d.worstGated = static_cast<float> (worstGated);
        d.worstUngated = static_cast<float> (worstUngated);
        return d;
    }

    void runLeakTests (double sr)
    {
        const vp::Subdivision subs[3] = { vp::Subdivision::quarter, vp::Subdivision::eighth,
                                          vp::Subdivision::sixteenth };
        const char* subNames[3] = { "quarter", "eighth", "sixteenth" };
        struct Path { const char* name; vp::FollowSource source; float extraMs; };
        const Path paths[2] = { { "MIXER", vp::FollowSource::kitMic, 0.0f },
                                { "IPAD+25ms", vp::FollowSource::speaker, 25.0f } };

        // Every style, every subdivision, both listening paths.
        //
        // This used to run on whatever `grooveStyle` and `subdivision` happen to
        // default to, and to assert a residual under 0.25 - a number that turns
        // out to belong to that part rather than to the canceller. Measured
        // across the nine styles the spread was three to one with the *sparser*
        // parts the worse of them, and worse again as the subdivision thins:
        // 0.07-0.18 at sixteenths, 0.29-0.46 at eighths - which is the shipped
        // default - and 0.62-0.74 at quarters, which the listener can select.
        //
        // That spread is not a property of the parts. It is the canceller
        // estimating its gain from one block and then smoothing the *answer*: a
        // block whose reference is silent goes down the degenerate branch,
        // returns a hard zero, and that zero - an absence of evidence, not a
        // measurement - is fed to the smoother, which forgets the path between
        // strokes. See .superpowers/sdd/sparse-leak-root-cause.md.
        //
        // So the bound is absolute, it is the same bound everywhere, and it is
        // asserted at the subdivision the app actually ships in. A canceller
        // that holds its estimate over silence cancels this bench to below
        // 0.0001 on all fifty-four rows - the echo here is an exact scaled copy
        // of our own output, so a converged fit removes essentially all of it,
        // and the room bench further down is where the honest figure lives -
        // which leaves 0.10 loose by three orders of magnitude and still tight
        // enough to catch any return to a per-block estimate.
        float worstMean = 0.0f;
        float worstBlock = 0.0f;
        float weakestControl = 1.0e9f;
        int rows = 0;
        int fewestBlocks = 1 << 30;
        const char* worstMeanRow = "";
        const char* worstBlockRow = "";
        static char meanRow[64] = {};
        static char blockRow[64] = {};
        for (int st = 0; st < static_cast<int> (vp::GrooveStyle::count); ++st)
            for (int s = 0; s < 3; ++s)
                for (int p = 0; p < 2; ++p)
                {
                    const auto on = leakRun (sr, 1024, paths[p].source, paths[p].extraMs, st,
                                             subs[s], true, false);
                    const auto off = leakRun (sr, 1024, paths[p].source, paths[p].extraMs, st,
                                              subs[s], false, false);
                    std::printf ("leak-residual  %-9s %-9s %-7s mean=%.4f worst=%.4f "
                                 "off=%.4f blocks=%d\n",
                                 paths[p].name, subNames[s],
                                 vp::toString (static_cast<vp::GrooveStyle> (st)),
                                 static_cast<double> (on.remain),
                                 static_cast<double> (on.worstBlock),
                                 static_cast<double> (off.remain), on.counted);
                    if (on.remain > worstMean)
                    {
                        worstMean = on.remain;
                        std::snprintf (meanRow, sizeof (meanRow), "%s %s %s", paths[p].name,
                                       subNames[s],
                                       vp::toString (static_cast<vp::GrooveStyle> (st)));
                        worstMeanRow = meanRow;
                    }
                    if (on.worstBlock > worstBlock)
                    {
                        worstBlock = on.worstBlock;
                        std::snprintf (blockRow, sizeof (blockRow), "%s %s %s", paths[p].name,
                                       subNames[s],
                                       vp::toString (static_cast<vp::GrooveStyle> (st)));
                        worstBlockRow = blockRow;
                    }
                    weakestControl = std::min (weakestControl, off.remain);
                    fewestBlocks = std::min (fewestBlocks, on.counted);
                    ++rows;
                }
        std::printf ("leak-residual  worst mean=%.4f on %s   worst block=%.4f on %s   "
                     "weakest off-control=%.4f   rows=%d   fewest counted blocks=%d\n",
                     static_cast<double> (worstMean), worstMeanRow,
                     static_cast<double> (worstBlock), worstBlockRow,
                     static_cast<double> (weakestControl), rows, fewestBlocks);

        expect (rows == static_cast<int> (vp::GrooveStyle::count) * 6,
                "the leak bench covers every style, every subdivision and both paths");
        // The control first: everything below is meaningless if the bench is not
        // actually presenting a leak to be cancelled, and a row that counted two
        // blocks is not a measurement. The sparsest row - quarters at 1024 - has
        // twenty-four counted blocks, so the floor is set below that.
        expect (fewestBlocks >= 20,
                "every row of the bench measured a run's worth of blocks, not a handful");
        expect (weakestControl > 0.90f,
                "with cancellation off the app's own part is all still there, on every row");
        expect (worstMean < 0.10f,
                "the app's own part is subtracted from the analysis whatever part it is "
                "playing and however dense the grid is");
        expect (worstBlock < 0.25f,
                "no single block is left carrying our own part, not just no average");

        // The seam is analysis-only. Switching the canceller changes what the
        // tracker is handed; it must not change one sample of what the listener
        // hears. Captured over the whole run, acquisition included, on both
        // paths at the shipped default.
        {
            const auto mixOn = leakRun (sr, 1024, paths[0].source, paths[0].extraMs,
                                        static_cast<int> (vp::GrooveStyle::funk),
                                        vp::Subdivision::eighth, true, true);
            const auto mixOff = leakRun (sr, 1024, paths[0].source, paths[0].extraMs,
                                         static_cast<int> (vp::GrooveStyle::funk),
                                         vp::Subdivision::eighth, false, true);
            const auto roomOn = leakRun (sr, 1024, paths[1].source, paths[1].extraMs,
                                         static_cast<int> (vp::GrooveStyle::funk),
                                         vp::Subdivision::eighth, true, true);
            const auto roomOff = leakRun (sr, 1024, paths[1].source, paths[1].extraMs,
                                          static_cast<int> (vp::GrooveStyle::funk),
                                          vp::Subdivision::eighth, false, true);
            float loudest = 0.0f;
            for (float v : mixOn.output)
                loudest = std::max (loudest, std::fabs (v));
            std::printf ("leak-seam      captured %zu samples, peak %.4f\n",
                         mixOn.output.size(), static_cast<double> (loudest));
            expect (mixOn.output.size() > static_cast<size_t> (sr) && loudest > 0.05f,
                    "the A/B captured a real rendered part to compare");
            expect (mixOn.output == mixOff.output && roomOn.output == roomOff.output,
                    "cancellation is analysis-only: the rendered part is bit for bit the "
                    "same with it on and off");
        }

        // Buffer size. `kGainSmooth` was a per-callback constant, so its time
        // constant was a function of the block length - the same trap the phase
        // constants in TempoFollower were fixed for. Measured with the per-block
        // smoother at eighths: 0.4792 at 256 frames, 0.4607 at 1024, 0.2379 at
        // 4096, because at 85 ms a block spans several strokes and is never
        // silent. Bigger buffers hid the bug. Each path is judged against
        // itself: they are different signals and comparing across them says
        // nothing.
        for (int p = 0; p < 2; ++p)
        {
            float sizes[3] = { 0.0f, 0.0f, 0.0f };
            int blocksSeen[3] = { 0, 0, 0 };
            const int blks[3] = { 256, 1024, 4096 };
            for (int i = 0; i < 3; ++i)
            {
                const auto r = leakRun (sr, blks[i], paths[p].source, paths[p].extraMs,
                                        static_cast<int> (vp::GrooveStyle::funk),
                                        vp::Subdivision::eighth, true, false);
                sizes[i] = r.remain;
                blocksSeen[i] = r.counted;
            }
            std::printf ("leak-buffer    %-9s 256=%.4f (%d blk) 1024=%.4f (%d blk) "
                         "4096=%.4f (%d blk)\n", paths[p].name,
                         static_cast<double> (sizes[0]), blocksSeen[0],
                         static_cast<double> (sizes[1]), blocksSeen[1],
                         static_cast<double> (sizes[2]), blocksSeen[2]);
            const float lo = std::min (std::min (sizes[0], sizes[1]), sizes[2]);
            const float hi = std::max (std::max (sizes[0], sizes[1]), sizes[2]);
            expect (std::min (std::min (blocksSeen[0], blocksSeen[1]), blocksSeen[2]) >= 8,
                    "every buffer size measured enough blocks to mean something");
            expect (hi < 0.10f,
                    "the subtraction covers every buffer size, not only the one it was "
                    "tuned on");
            // Within a factor of three of each other, taken against a floor so a
            // residual that has gone to zero everywhere cannot fail it.
            expect (hi <= std::max (0.01f, lo) * 3.0f,
                    "the estimator's window is a length of time, not a number of callbacks");
        }

        // And the other direction, at every buffer size. A leak is a large part
        // of the input by definition; a part that happens to be playing the same
        // rhythm as the band is not, and subtracting it costs the tracker real
        // onsets. The per-block estimate got this wrong on both counts - too slow
        // to reach the truth when the leak was real and sparse, and noisy enough
        // to sit at a few per cent when there was no leak at all. Measured with
        // the per-block smoother on a feed carrying none, at 1024: the analysis
        // moved 33% rms on the mixer path and 23% through the speaker, with
        // single blocks raised by 8.9x and 39.8x.
        for (int p = 0; p < 2; ++p)
            for (int blk : { 128, 256, 1024 })
            {
                const auto on = noLeakRun (sr, blk, paths[p].source, true);
                const auto off = noLeakRun (sr, blk, paths[p].source, false);
                const auto d = noLeakDamage (on, off);
                std::printf ("leak-noleak    %-9s blk=%4d relRms=%.4f worstAbs=%.2f%% of mean "
                             "gated=%.4f (ungated=%.4f) meanIn=%.4f blocks=%d\n",
                             paths[p].name, blk, static_cast<double> (d.relRms),
                             static_cast<double> (100.0f * d.worstAbs),
                             static_cast<double> (d.worstGated),
                             static_cast<double> (d.worstUngated),
                             static_cast<double> (d.meanIn), d.counted);
                expect (d.counted > 10 && d.meanIn > 0.01f,
                        "the no-leak bench actually presented a band to be left alone");
                expect (d.relRms < 0.02f,
                        "a feed that carries no leak is left alone, even when our own part "
                        "is playing the band's rhythm");
                // Ten per cent of the run's own mean block peak is about 0.9 dB
                // there and four thousandths of the loudest block. This bounds
                // the absolute perturbation of every counted block; with the rms
                // figure above and the half-mean gated ratio below, the loud and
                // measured blocks are held tightly. The quiet tails between
                // onsets are bounded in absolute terms only - the gated ratio
                // steps over them and this figure does not constrain them
                // relative to their own level. Measured worst: 4.21%, speaker.
                expect (d.worstAbs < 0.10f,
                        "and no block of it is moved by more than a tenth of the run's own "
                        "mean level");
                // The ratio, on blocks loud enough for a ratio to mean anything.
                // Measured worst: 1.0374.
                expect (d.worstGated <= 1.10f,
                        "no audible block of it is made louder by the subtraction");
            }

        // A device restart must not carry the fit across. `prepare()` clears the
        // reference ring, so evidence gathered against the old session's timing
        // describes a signal that is no longer there - and if the new session's
        // effective delay happens to match the old one, nothing else would ever
        // notice. Re-prepared and compared against a new engine given the same
        // input: not "close", identical, because there is no mechanism by which
        // a correct implementation could differ.
        {
            const int blk = 1024;
            vp::VirtualPercussionEngine eng;
            // Session one: a strong leak, so the fit ends up far from zero.
            {
                eng.prepare (sr, blk, 1);
                eng.settings().followSource.store (
                    static_cast<int> (vp::FollowSource::kitMic));
                eng.settings().masterVolume.store (1.0f);
                eng.settings().reverbAmount.store (0.0f);
                eng.settings().subdivision.store (static_cast<int> (vp::Subdivision::eighth));
                eng.settings().grooveAuto.store (false);
                eng.settings().dynamicsFollow.store (false);
                eng.setReportedLatencyMs (150.0f);
                eng.setFixedBpm (120.0f);
                eng.start();
                eng.tapAt (0.0);
                eng.tapAt (0.5);
                eng.tapAt (1.0);
                eng.tapAt (1.5);
                const int delay = static_cast<int> (0.150 * sr);
                std::vector<float> history (static_cast<size_t> (delay + blk * 4), 0.0f);
                int histWrite = 0;
                std::vector<float> in (static_cast<size_t> (blk), 0.0f);
                std::vector<float> oL (static_cast<size_t> (blk), 0.0f);
                std::vector<float> oR (static_cast<size_t> (blk), 0.0f);
                float* outs[2] = { oL.data(), oR.data() };
                for (int b = 0; b < static_cast<int> (sr * 8.0) / blk; ++b)
                {
                    for (int i = 0; i < blk; ++i)
                    {
                        const int ri = (histWrite - delay + i
                                        + static_cast<int> (history.size()))
                                       % static_cast<int> (history.size());
                        in[static_cast<size_t> (i)] = history[static_cast<size_t> (ri)] * 0.6f;
                    }
                    const float* ins[1] = { in.data() };
                    eng.process (ins, 1, outs, 2, blk);
                    for (int i = 0; i < blk; ++i)
                    {
                        history[static_cast<size_t> (histWrite)] =
                            0.5f * (oL[static_cast<size_t> (i)] + oR[static_cast<size_t> (i)]);
                        histWrite = (histWrite + 1) % static_cast<int> (history.size());
                    }
                }
            }

            // Session two, on the same engine: the device comes back with the
            // same sample rate, the same buffer and the same reported latency, so
            // the effective delay is unchanged and only the fit itself could
            // carry over. The room is quieter than it was - a quarter of the
            // return - which is the case that shows: evidence gathered when the
            // path was strong describes a path that no longer exists, and the
            // gain it implies over-subtracts until the new evidence outweighs it.
            auto secondSession = [&] (vp::VirtualPercussionEngine& e)
            {
                e.prepare (sr, blk, 1);
                e.settings().followSource.store (static_cast<int> (vp::FollowSource::kitMic));
                e.settings().masterVolume.store (1.0f);
                e.settings().reverbAmount.store (0.0f);
                e.settings().subdivision.store (static_cast<int> (vp::Subdivision::eighth));
                e.settings().grooveAuto.store (false);
                e.settings().dynamicsFollow.store (false);
                e.setReportedLatencyMs (150.0f);
                e.setFixedBpm (120.0f);
                e.start();
                e.tapAt (0.0);
                e.tapAt (0.5);
                e.tapAt (1.0);
                e.tapAt (1.5);
                const int blocks = static_cast<int> (sr * 4.0) / blk;
                const int delay = static_cast<int> (0.150 * sr);
                std::vector<float> history (static_cast<size_t> (delay + blk * 4), 0.0f);
                int histWrite = 0;
                std::vector<float> in (static_cast<size_t> (blk), 0.0f);
                std::vector<float> oL (static_cast<size_t> (blk), 0.0f);
                std::vector<float> oR (static_cast<size_t> (blk), 0.0f);
                float* outs[2] = { oL.data(), oR.data() };
                std::vector<float> trace;
                for (int b = 0; b < blocks; ++b)
                {
                    for (int i = 0; i < blk; ++i)
                    {
                        const int ri = (histWrite - delay + i
                                        + static_cast<int> (history.size()))
                                       % static_cast<int> (history.size());
                        in[static_cast<size_t> (i)] =
                            history[static_cast<size_t> (ri)] * 0.15f;
                    }
                    const float* ins[1] = { in.data() };
                    e.process (ins, 1, outs, 2, blk);
                    for (int i = 0; i < blk; ++i)
                    {
                        history[static_cast<size_t> (histWrite)] =
                            0.5f * (oL[static_cast<size_t> (i)] + oR[static_cast<size_t> (i)]);
                        histWrite = (histWrite + 1) % static_cast<int> (history.size());
                    }
                    const auto s = e.snapshot();
                    trace.push_back (s.inputPeak);
                    trace.push_back (s.leakRemain);
                }
                return trace;
            };

            const auto reprepared = secondSession (eng);
            vp::VirtualPercussionEngine fresh;
            const auto brandNew = secondSession (fresh);
            int firstDiff = -1;
            float worstDelta = 0.0f;
            for (size_t i = 0; i < std::min (reprepared.size(), brandNew.size()); ++i)
            {
                const float d = std::fabs (reprepared[i] - brandNew[i]);
                if (d > 0.0f && firstDiff < 0)
                    firstDiff = static_cast<int> (i / 2);
                worstDelta = std::max (worstDelta, d);
            }
            std::printf ("leak-lifecycle re-prepared vs new engine: %zu blocks, first "
                         "difference at %d, worst %.6f\n",
                         reprepared.size() / 2, firstDiff, static_cast<double> (worstDelta));
            expect (reprepared.size() == brandNew.size() && reprepared.size() > 40,
                    "the lifecycle bench ran both sessions");
            expect (reprepared == brandNew,
                    "a device restart starts the leak canceller over: prepare() leaves no "
                    "fit behind");
        }

        // Finally, the same thing in a room rather than down a wire. These are the
        // numbers to quote about a real rig: through the iPad's speaker with the
        // repository's own reflection model, most of the return is either
        // band-shaped or not in the reference at any single delay, and a two-band
        // canceller reaches little of it - 11.5% of the return on the one-wall
        // fixture, 7.6% with the full tail, against 90.2% when the direct
        // component arrives spectrally unmodified. What is asserted per fixture is
        // the mean over the run, which is the only thing these three rows measure.
        {
            const RoomModel models[3] = { RoomModel::fractional, RoomModel::oneWall,
                                          RoomModel::eightWalls };
            const char* names[3] = { "frac+noise", "one wall", "8 walls" };
            float remains[3] = { 1.0f, 1.0f, 1.0f };
            for (int m = 0; m < 3; ++m)
            {
                const auto on = roomLeakRun (sr, 1024, true,
                                             static_cast<int> (vp::GrooveStyle::funk),
                                             models[m]);
                const auto off = roomLeakRun (sr, 1024, false,
                                              static_cast<int> (vp::GrooveStyle::funk),
                                              models[m]);
                std::printf ("leak-room      %-11s on=%.4f off=%.4f worst=%.4f blocks=%d\n",
                             names[m], static_cast<double> (on.remain),
                             static_cast<double> (off.remain),
                             static_cast<double> (on.worstBlock), on.counted);
                remains[m] = on.remain;
                expect (on.counted >= 20 && off.remain > 0.90f,
                        "the room bench presented a room's worth of our own return");
                expect (on.remain < off.remain,
                        "in a room the canceller still lowers the mean share of our own "
                        "return that reaches the tracker");
            }
            // The one bound here that is about the estimator rather than about
            // the room: a return that is our own output at a delay 0.373 of a
            // sample off the sample grid, with a noise floor under it, is still
            // cancelled. Measured 0.0957 - the same 0.25 the per-block bound
            // above uses, and the misalignment is what sets it.
            expect (remains[0] < 0.25f,
                    "a leak that does not land on a whole sample is still subtracted");
        }
    }
}

int main (int argc, char** argv)
{
    constexpr double sr = 48000.0;
    constexpr int block = 128;

    // The recorded-loop tests on their own. The suite drives the neural worker
    // in real time and takes minutes; the loop tests take seconds, and being
    // able to run them alone is the difference between iterating on them and
    // not. The default - no arguments - is still the whole suite.
    if (argc > 1 && std::string (argv[1]) == "--loops")
    {
        vpRunLoopTests (gPassed, gFailed);
        std::printf ("\n%d passed, %d failed\n", gPassed, gFailed);
        return gFailed == 0 ? 0 : 1;
    }

    // Same argument for the leak canceller bench: a hundred-odd engine runs
    // that finish in seconds, behind minutes of neural worker tests.
    if (argc > 1 && std::string (argv[1]) == "--leak")
    {
        runLeakTests (sr);
        std::printf ("\n%d passed, %d failed\n", gPassed, gFailed);
        return gFailed == 0 ? 0 : 1;
    }

    // And the own-output / analysis-epoch benches, which are the other end of
    // the same subject: twenty real-time runs of the neural worker, and the
    // question they ask is whether the app's own part moves its own analysis.
    if (argc > 1 && std::string (argv[1]) == "--makeup")
    {
        vpRunMakeupTests (gPassed, gFailed, argc > 2 ? argv[2] : nullptr);
        std::printf ("\n%d passed, %d failed\n", gPassed, gFailed);
        return gFailed == 0 ? 0 : 1;
    }

    std::printf ("Virtual Percussionist — engine / clock / AI tests\n");

    {
        vp::TempoFollower clock;
        clock.prepare (sr);
        clock.setPulsesPerBeat (4);
        clock.setTargetTempo (120.0f, 1.0f);
        clock.setLocked (true);
        clock.snapPhase (0.0f);

        auto wrap01 = [] (float x)
        {
            x -= std::floor (x);
            return x;
        };
        auto distToZero = [&] (float x)
        {
            x = wrap01 (x);
            return std::min (x, 1.0f - x);
        };

        float worstDownbeat = 0.0f;
        int downbeats = 0;
        for (int i = 0; i < 2000; ++i)
        {
            const float before = clock.beatPhase();
            const auto tick = clock.advance (block);
            const float after = clock.beatPhase();
            float delta = after - before;
            if (delta < -0.5f) delta += 1.0f;
            if (delta >  0.5f) delta -= 1.0f;
            for (int p = 0; p < tick.pulsesFired; ++p)
            {
                if (tick.pulseIndex[p] != 0)
                    continue;
                const float frac = (static_cast<float> (tick.pulseOffset[p]) + 0.5f)
                                   / static_cast<float> (block);
                const float ph = wrap01 (before + delta * frac);
                worstDownbeat = std::max (worstDownbeat, distToZero (ph));
                ++downbeats;
            }
        }
        std::printf ("dot-clock  downbeats=%d  worst=%.3f\n",
                     downbeats, static_cast<double> (worstDownbeat));
        expect (downbeats > 8 && worstDownbeat < 0.05f,
                "shaker downbeats follow the orange-dot clock");
    }

    {
        vp::TempoFollower clock;
        clock.prepare (sr);
        clock.setTargetTempo (80.0f, 1.0f);
        clock.setLocked (true);
        clock.setTempoTrimEnabled (true);
        clock.snapPhase (0.0f);

        constexpr float songBpm = 81.0f;
        double songPhase = 0.0;
        for (int i = 0; i < static_cast<int> (32.0 * sr / block); ++i)
        {
            const double inc = (static_cast<double> (songBpm) / 60.0)
                               * (static_cast<double> (block) / sr);
            const double next = songPhase + inc;
            if (std::floor (next) > std::floor (songPhase))
            {
                const double boundary = std::floor (songPhase) + 1.0;
                const float frac = static_cast<float> ((boundary - songPhase) / inc);
                const float atOnset = vp::wrap01 (clock.beatPhase()
                    + frac * static_cast<float> (block) * clock.currentTempo()
                      / (60.0f * static_cast<float> (sr)));
                clock.observeOnsetPhase (atOnset, 2.0f, 1);
            }
            clock.advance (block);
            songPhase = next;
        }

        std::printf ("pll-fine-tune  tempo=%.3f trim=%.3f\n",
                     static_cast<double> (clock.currentTempo()),
                     static_cast<double> (clock.tempoTrimBpm()));
        // The whole error, not half of it. The trim is measured on the drift
        // that is left after the trim already applied, so it has to be added to
        // that trim rather than eased towards as if it were the whole answer -
        // done the second way it converges on half the error and stops, which
        // this test used to pass with 0.50 against a song that is 1.00 away.
        expect (std::fabs (clock.currentTempo() - songBpm) < 0.15f
                    && clock.tempoTrimBpm() > 0.85f,
                "quarter-phase drift fine-tunes a manually anchored BPM");
    }

    {
        vp::TempoFollower clock;
        clock.prepare (sr);
        clock.setTargetTempo (120.0f, 1.0f);
        clock.setLocked (true);
        clock.snapPhase (0.001f);
        for (int i = 0; i < 12; ++i)
            clock.observeOnsetPhase (0.20f, 1.0f, 1);

        // A correction pending right on top of the bar line, which is where it
        // used to be able to do damage: the loop subtracted it straight from the
        // phase, the phase went below zero, and the quarter counter had to be
        // walked back with it. The counter is what the bar is read off, so
        // getting that wrong moved the whole bar rather than the phase inside
        // it.
        //
        // The correction is a rate change now, so the phase cannot step
        // backwards at all and the wrap cannot happen. What is asserted is the
        // property either mechanism has to have and only one of them did: over
        // one sample the bar moves by a hair, in whichever direction, and never
        // jumps a quarter.
        const float before = clock.barPhase();
        clock.advance (1);
        const float after = clock.barPhase();
        const float moved = std::fabs (vp::wrapCentered (after - before));
        std::printf ("phase-zero  bar %.4f->%.4f  moved=%.4f\n",
                     static_cast<double> (before), static_cast<double> (after),
                     static_cast<double> (moved));
        expect (moved < 0.02f,
                "a phase correction on the bar line never jumps the quarter counter");
    }

    // What the shaker actually plays, while the loop is correcting continuously
    // against a decoder whose phase is not exact.
    //
    // Phase used to be corrected by subtracting the error from the clock's own
    // position. That moves the grid out from under a part already playing on it,
    // and the pulses for the block are read off the moved grid: a position just
    // passed could be passed again, or stepped over unplayed. Measured over two
    // minutes at 3% phase wobble it put roughly one pulse in a hundred either on
    // top of its neighbour or nowhere - a shaker that occasionally doubles a
    // stroke or drops one and then sounds like it has lost the beat. Correcting
    // by rate instead closes the same error while the grid stays monotonic.
    {
        auto sweepStumbles = [&] (float songBpm, float jitterBeats, vp::FollowStrength strength)
        {
            const double pulseSec = 60.0 / static_cast<double> (songBpm) / 4.0;

            vp::TempoFollower clock;
            clock.prepare (sr);
            clock.setPulsesPerBeat (4);
            clock.forceTempo (songBpm);
            clock.setTargetTempo (songBpm, 0.9f);
            clock.setLocked (true);
            clock.setFollowStrength (strength);
            clock.resetClock();

            // A fixed, ordinary wobble on the analysis phase. Deterministic, so
            // a failure here is reproducible rather than a bad afternoon.
            vp::DeterministicRng rng (0x51ED2701u);
            double songPhase = 0.0, lastPulse = -1.0;
            int stumbles = 0, counted = 0;
            const int blocks = static_cast<int> (sr * 60.0 / block);
            for (int b = 0; b < blocks; ++b)
            {
                const double t = static_cast<double> (b * block) / sr;
                const double inc = (static_cast<double> (songBpm) / 60.0)
                                   * (static_cast<double> (block) / sr);
                const double next = songPhase + inc;
                if (std::floor (next) > std::floor (songPhase))
                {
                    const float truth = static_cast<float> (next - std::floor (next));
                    const float seen = vp::wrap01 (truth + jitterBeats * rng.nextSigned());
                    clock.observeOnsetPhase (vp::wrap01 (clock.beatPhase() - seen), 0.8f, 1);
                }
                songPhase = next;

                const auto tick = clock.advance (block);
                for (int i = 0; i < tick.pulsesFired; ++i)
                {
                    const double pt = t + static_cast<double> (tick.pulseOffset[i]) / sr;
                    if (lastPulse > 0.0)
                    {
                        ++counted;
                        if (std::fabs (pt - lastPulse - pulseSec) / pulseSec > 0.20)
                            ++stumbles;
                    }
                    lastPulse = pt;
                }
            }
            std::printf ("pulse-spacing  %.0f BPM wobble %.0f%%  pulses=%d  stumbles=%d\n",
                         static_cast<double> (songBpm), jitterBeats * 100.0f, counted, stumbles);
            return counted > 200 && stumbles == 0;
        };

        bool clean = true;
        clean &= sweepStumbles (76.0f, 0.03f, vp::FollowStrength::high);
        clean &= sweepStumbles (120.0f, 0.03f, vp::FollowStrength::high);
        clean &= sweepStumbles (120.0f, 0.06f, vp::FollowStrength::high);
        clean &= sweepStumbles (120.0f, 0.03f, vp::FollowStrength::medium);
        expect (clean,
                "the grid never doubles a stroke or drops one while correcting phase");
    }

    // A tap moves the grid under a part that is already playing. What the
    // listener asked for is a stroke on the one they just tapped; what they used
    // to get was a hole where it should have been, because a pulse landing
    // exactly on the newly snapped phase fell outside the half-open span the
    // block emits from and was dropped. Measured over 32 snap positions at 76
    // BPM the gap around the correction reached 1.89x the sixteenth being
    // played - very nearly a skipped stroke - which is heard as the shaker
    // stumbling rather than re-aligning.
    {
        auto snapFrom = [&] (float bpm, float stopAt)
        {
            vp::TempoFollower clock;
            clock.prepare (sr);
            clock.setPulsesPerBeat (4);
            clock.forceTempo (bpm);
            clock.setTargetTempo (bpm, 1.0f);
            clock.setLocked (true);
            clock.resetClock();
            for (int i = 0; i < 40000; ++i)
            {
                if (clock.beatPhase() >= stopAt && clock.beatPhase() < stopAt + 0.01f)
                    break;
                clock.advance (block);
            }
            clock.snapPhase (0.0f);
            return clock.advance (block);
        };

        // Far enough past the last sixteenth that a stroke on the one is a
        // stroke and not a thickening of the one before it.
        const auto declared = snapFrom (100.0f, 0.40f);
        expect (declared.pulsesFired > 0 && declared.pulseIndex[0] == 0
                    && declared.pulseOffset[0] == 0,
                "a tap snap plays the downbeat it just declared");

        // The one case where dropping it is right: the previous pulse sounded
        // ~17 ms ago, and two strokes that close fuse into one thick attack.
        const auto crowded = snapFrom (100.0f, 0.78f);
        expect (crowded.pulsesFired == 0 || crowded.pulseOffset[0] > 0,
                "a snap landing on top of a pulse does not double it");
    }

    // The spacing either side of a re-align, swept across every position in the
    // beat the tap could fall on. Half a pulse is the floor this can reach: the
    // grid either plays the declared one early or waits for the next, and the
    // guard sits at the midpoint, so neither branch can be worse than the other.
    {
        for (float bpm : { 60.0f, 76.0f, 120.0f, 168.0f })
        {
            const double pulseSec = 60.0 / static_cast<double> (bpm) / 4.0;
            double worst = 0.0;
            for (int k = 0; k < 64; ++k)
            {
                vp::TempoFollower clock;
                clock.prepare (sr);
                clock.setPulsesPerBeat (4);
                clock.forceTempo (bpm);
                clock.setTargetTempo (bpm, 1.0f);
                clock.setLocked (true);
                clock.resetClock();

                const float at = static_cast<float> (k) / 64.0f;
                double last = -1.0, firstAfter = -1.0;
                bool snapped = false;
                for (int i = 0; i < 40000 && firstAfter < 0.0; ++i)
                {
                    const double t = static_cast<double> (i * block) / sr;
                    if (! snapped && t > 4.0 && clock.beatPhase() >= at
                        && clock.beatPhase() < at + 0.02f)
                    {
                        clock.snapPhase (0.0f);
                        snapped = true;
                    }
                    const auto tick = clock.advance (block);
                    for (int p = 0; p < tick.pulsesFired; ++p)
                    {
                        const double pt = t + static_cast<double> (tick.pulseOffset[p]) / sr;
                        if (! snapped)
                            last = pt;
                        else if (firstAfter < 0.0)
                            firstAfter = pt;
                    }
                }
                if (last > 0.0 && firstAfter > 0.0)
                    worst = std::max (worst,
                                      std::fabs (firstAfter - last - pulseSec) / pulseSec);
            }
            std::printf ("tap-realign  %.0f BPM  worst spacing error=%.0f%%\n",
                         static_cast<double> (bpm), worst * 100.0);
            expect (worst < 0.55,
                    "re-aligning never stretches or crowds a pulse by more than half");
        }
    }

    // Half and double are the same pulse counted at another level, so every
    // stroke the part is playing stays exactly where it is and only the spacing
    // changes. Easing between them walks the grid through half a second of
    // tempos the music is not at, which is heard as the percussion running away
    // and catching up. A tempo the band actually moved to is still eased into.
    {
        vp::TempoFollower clock;
        clock.prepare (sr);
        clock.setPulsesPerBeat (4);
        clock.forceTempo (76.0f);
        clock.setTargetTempo (76.0f, 1.0f);
        clock.setLocked (true);
        clock.resetClock();
        for (int i = 0; i < 800; ++i)
            clock.advance (block);

        clock.setTargetTempo (152.0f, 1.0f);
        bool between = false;
        for (int i = 0; i < 400; ++i)
        {
            const auto tick = clock.advance (block);
            if (tick.tempoBpm > 80.0f && tick.tempoBpm < 148.0f)
                between = true;
        }
        std::printf ("octave-switch  tempo=%.1f  passedThrough=%s\n",
                     static_cast<double> (clock.currentTempo()), between ? "yes" : "no");
        expect (! between && std::fabs (clock.currentTempo() - 152.0f) < 0.1f,
                "an octave flip is taken at once, not glided through");

        clock.setTargetTempo (164.0f, 1.0f);
        const auto eased = clock.advance (block);
        expect (eased.tempoBpm > 152.0f && eased.tempoBpm < 155.0f,
                "a tempo the band really moved to is still eased into");
    }

    // The decoder refreshes six times a second and its estimate wobbles inside
    // its own tolerance. That wobble used to be applied whole, on the block it
    // arrived: anything inside 2.5 BPM was assigned straight to the clock, so
    // the grid rate was stepping several times a second and the part never sat
    // still. It is still fast enough to be inaudible as a lag.
    {
        vp::TempoFollower clock;
        clock.prepare (sr);
        clock.setPulsesPerBeat (4);
        clock.forceTempo (100.0f);
        clock.setTargetTempo (100.0f, 1.0f);
        clock.setLocked (true);
        clock.resetClock();
        for (int i = 0; i < 400; ++i)
            clock.advance (block);

        float worstStep = 0.0f;
        float prev = clock.currentTempo();
        for (int i = 0; i < 1200; ++i)
        {
            clock.setTargetTempo (i % 2 == 0 ? 101.8f : 98.2f, 1.0f);
            const auto tick = clock.advance (block);
            worstStep = std::max (worstStep, std::fabs (tick.tempoBpm - prev));
            prev = tick.tempoBpm;
        }
        std::printf ("tempo-wobble  worst single-block step=%.3f BPM\n",
                     static_cast<double> (worstStep));
        expect (worstStep < 0.5f,
                "decoder wobble inside its tolerance no longer steps the grid");
    }

    // The analysis publishes at about 6 Hz, not every audio callback. Holding
    // each slightly different estimate until the next publication is the real
    // shape of its jitter, and used to make the played BPM visibly breathe.
    // Acquisition must remain quick at the same time: stability after lock is
    // not permission to make the first lock sluggish.
    {
        vp::TempoFollower clock;
        clock.prepare (sr);
        clock.setPulsesPerBeat (4);
        clock.forceTempo (100.0f);
        clock.setLocked (true);

        float lo = 1000.0f, hi = 0.0f;
        const int refreshBlocks = static_cast<int> (0.166 * sr / block);
        const int totalBlocks = static_cast<int> (8.0 * sr / block);
        for (int i = 0; i < totalBlocks; ++i)
        {
            if ((i % refreshBlocks) == 0)
                clock.setTargetTempo (((i / refreshBlocks) & 1) == 0 ? 101.8f : 98.2f, 1.0f);
            const float bpm = clock.advance (block).tempoBpm;
            if (i > static_cast<int> (2.0 * sr / block))
            {
                lo = std::min (lo, bpm);
                hi = std::max (hi, bpm);
            }
        }

        std::printf ("tempo-held   6 Hz target span=%.3f BPM\n",
                     static_cast<double> (hi - lo));
        expect (hi - lo < 1.8f,
                "a held pulse does not replay the decoder's 6 Hz BPM wobble");

        // Ten BPM is not jitter. The clock should adopt it causally, but in
        // less than a second rather than spending several seconds between the
        // old and new rates.
        clock.forceTempo (100.0f);
        clock.setLocked (true);
        clock.setTargetTempo (110.0f, 1.0f);
        for (int i = 0; i < static_cast<int> (0.8 * sr / block); ++i)
            clock.advance (block);
        expect (std::fabs (clock.currentTempo() - 110.0f) < 1.0f,
                "a sustained live tempo move is adopted inside a second");

        clock.forceTempo (100.0f);
        clock.setLocked (false);
        clock.setTargetTempo (112.0f, 1.0f);
        for (int i = 0; i < static_cast<int> (0.8 * sr / block); ++i)
            clock.advance (block);
        expect (std::fabs (clock.currentTempo() - 112.0f) < 0.25f,
                "initial tempo acquisition stays fast before the part enters");
    }

    {
        vp::VirtualPercussionEngine eng;
        eng.prepare (sr, block, 1);
        eng.tapAt (0.0);
        eng.tapAt (0.5);
        eng.tapAt (1.0);
        feedSilence (eng, sr, block, 0.1f);
        const auto afterThree = eng.snapshot();
        expect (afterThree.bpm < 1.0f
                    && afterThree.state != vp::TrackingState::following,
                "the first three TAPs leave automatic listening untouched");

        eng.tapAt (1.5);
        feedSilence (eng, sr, block, 2.0f);
        const auto last = eng.snapshot();
        std::printf ("tap-tempo stopped  bpm=%.1f  state=%s  hits=%d\n",
                     static_cast<double> (last.bpm), vp::toString (last.state),
                     eng.shakerHits());
        expect (std::fabs (last.bpm - 120.f) < 2.5f && eng.shakerHits() == 0,
                "tap sets BPM without START, shaker stays muted");
    }

    {
        vp::VirtualPercussionEngine eng;
        eng.prepare (sr, block, 1);
        feedSilence (eng, sr, block, 1.0f);
        const float before = eng.snapshot().barPhase;
        eng.tapAt (1.0);
        feedSilence (eng, sr, block, 0.05f);
        const auto after = eng.snapshot();
        std::printf ("first-tap-quarter  before=%.3f  after=%.3f  declared=%d\n",
                     static_cast<double> (before),
                     static_cast<double> (after.barPhase),
                     after.barDeclared ? 1 : 0);
        expect (before > 0.20f,
                "the clock had left beat one before the tap");
        expect (after.barPhase < 0.10f && after.barDeclared,
                "the first tap of a tempo group starts the first quarter");
    }

    {
        vp::VirtualPercussionEngine eng;
        eng.prepare (sr, block, 1);
        eng.start();
        eng.tapAt (0.0);
        eng.tapAt (0.5);
        eng.tapAt (1.0);
        eng.tapAt (1.5);
        feedSilence (eng, sr, block, 2.2f);
        const float before = eng.snapshot().barPhase;
        eng.tapAt (3.8);
        feedSilence (eng, sr, block, 0.05f);
        const auto after = eng.snapshot();
        std::printf ("retap-quarter  before=%.3f  after=%.3f  declared=%d\n",
                     static_cast<double> (before),
                     static_cast<double> (after.barPhase),
                     after.barDeclared ? 1 : 0);
        expect (before > 0.15f,
                "a later group starts away from beat one");
        expect (after.barPhase < 0.10f && after.barDeclared,
                "the first tap of a new group is the one, even after a count-in");
    }

    {
        vp::VirtualPercussionEngine eng;
        eng.prepare (sr, block, 1);
        eng.start();
        eng.tapAt (0.0);
        eng.tapAt (0.5);
        eng.tapAt (1.0);
        eng.tapAt (1.5);
        feedSilence (eng, sr, block, 4.0f);
        const auto last = eng.snapshot();
        std::printf ("tap-tempo start  bpm=%.1f  state=%s  hits=%d\n",
                     static_cast<double> (last.bpm), vp::toString (last.state),
                     eng.shakerHits());
        expect (std::fabs (last.bpm - 120.f) < 2.5f
                    && last.state == vp::TrackingState::following
                    && eng.shakerHits() > 8,
                "TAP then START holds tap tempo and plays the shaker");
    }

    {
        vp::VirtualPercussionEngine eng;
        eng.prepare (sr, block, 1);
        for (int i = 0; i < 8; ++i)
            eng.tapAt (0.05 * i);
        feedSilence (eng, sr, block, 1.0f);
        expect (eng.shakerHits() == 0 && eng.snapshot().bpm < 1.0f,
                "fast taps do not start the shaker");
    }

    {
        vp::VirtualPercussionEngine eng;
        eng.prepare (sr, block, 1);
        eng.start();
        eng.tapAt (0.0);
        eng.tapAt (0.5);
        eng.tapAt (1.0);
        eng.tapAt (1.5);
        feedSilence (eng, sr, block, 0.2f);
        expect (std::fabs (eng.snapshot().bpm - 120.f) < 2.5f,
                "count-in still locks 120 before the tempo moves");

        // No 2 s pause: keep tapping at 150 BPM (0.4 s).
        eng.tapAt (1.9);
        eng.tapAt (2.3);
        eng.tapAt (2.7);
        eng.tapAt (3.1);
        feedSilence (eng, sr, block, 0.5f);
        const auto last = eng.snapshot();
        std::printf ("tap-tempo follow  bpm=%.1f\n", static_cast<double> (last.bpm));
        expect (std::fabs (last.bpm - 150.f) < 5.0f
                    && last.state == vp::TrackingState::following,
                "continuing taps at a new rate take the tempo without a pause");
    }

    {
        vp::VirtualPercussionEngine eng;
        eng.prepare (sr, block, 1);
        expect (eng.settings().tempoFollow.load(),
                "tempo follow is the default");

        eng.setFixedBpm (100.0f);
        expect (! eng.settings().tempoFollow.load(),
                "setFixedBpm switches to FISSO");
        eng.start();
        feedSilence (eng, sr, block, 0.3f);
        expect (std::fabs (eng.snapshot().bpm - 100.f) < 2.5f,
                "fixed BPM is held before any analysis");

        eng.setClickInjectEnabled (true);
        eng.setClickInjectBpm (120.0f);
        feedSilence (eng, sr, block, 4.0f);
        const auto last = eng.snapshot();
        std::printf ("fixed-tempo vs click  bpm=%.1f  nn=%.1f  follow=%d\n",
                     static_cast<double> (last.bpm),
                     static_cast<double> (last.neuralBpm),
                     last.tempoFollow ? 1 : 0);
        expect (std::fabs (last.bpm - 100.f) < 3.0f,
                "fixed 100 is not stolen by a 120 click");
    }

    {
        vp::VirtualPercussionEngine eng;
        eng.prepare (sr, block, 1);
        eng.start();
        eng.tapAt (0.0);
        eng.tapAt (0.5);
        eng.tapAt (1.0);
        eng.tapAt (1.5);
        feedSilence (eng, sr, block, 2.0f);
        const int hitsBefore = eng.shakerHits();
        eng.stop();
        feedSilence (eng, sr, block, 1.0f);
        const auto last = eng.snapshot();
        expect (eng.shakerHits() == hitsBefore && ! last.percussionAudible,
                "STOP is immediately silent but keeps the clock");
    }


    // B1 and B2 - grid density per subdivision, and the beat-of-bar label on
    // every pulse - are covered by `perc-grid` and `perc-grid-label` in
    // Tests/TestAiBeat.cpp, which arrived at the same two faults independently.
    // Duplicating them here would only give two places to update.

    // B3 - a stroke taken over by the next one of its kind must ring out over a
    // ramp, not be switched off between two samples, and no stroke may take a
    // slot from a voice that is still sounding. docs/AUDIO_ENGINE.md names
    // these two as the ones left uncovered.
    //
    // Counting voices is not enough on its own: the shaker alternates a down and
    // an up stroke, and those two overlap perfectly legitimately. What separates
    // a release from a cut is whether any voice is ever *inside* its ramp.
    {
        constexpr float bpm = 200.0f;
        constexpr int pulses = 4;

        vp::TempoFollower clock;
        clock.prepare (sr);
        clock.setPulsesPerBeat (pulses);
        clock.forceTempo (bpm);
        clock.setTargetTempo (bpm, 1.0f);
        clock.setLocked (true);
        clock.snapDownbeat (0.0f);

        vp::PercussionEngine perc;
        perc.prepare (sr);
        perc.setGroove (bpm, pulses);
        perc.setSubdivision (vp::Subdivision::sixteenth);
        perc.setReverbAmount (0.0f);
        perc.setVolume (1.0f);

        std::vector<float> l (static_cast<size_t> (block), 0.0f);
        std::vector<float> r (static_cast<size_t> (block), 0.0f);

        int releasingBlocks = 0;
        int maxVoices = 0;
        const int total = static_cast<int> (sr * 30.0);
        for (int pos = 0; pos + block <= total; pos += block)
        {
            const auto tick = clock.advance (block);
            perc.render (l.data(), r.data(), block, tick, true);
            maxVoices = std::max (maxVoices, perc.activeVoices());
            if (perc.releasingVoices() > 0)
                ++releasingBlocks;
        }

        std::printf ("voice-steal  maxVoices=%d  releasingBlocks=%d  hardSteals=%d\n",
                     maxVoices, releasingBlocks, perc.hardSteals());
        expect (releasingBlocks > 0 && perc.hardSteals() == 0,
                "a retriggered stroke rings out over a ramp, and no voice is stolen mid-note");
    }

    // B4 - the leak canceller: what it takes out when there is a leak, and
    // what it leaves alone when there is not. See runLeakTests above; `--leak`
    // runs just this.
    runLeakTests (sr);

    // Input trim is analysis-only: twice the gain, twice the peak the tracker
    // is handed, and the output of the part does not move with it.
    {
        auto peakAt = [&] (float gain)
        {
            vp::VirtualPercussionEngine eng;
            eng.prepare (sr, 256, 1);
            eng.settings().inputGain.store (gain);
            eng.settings().masterVolume.store (0.0f);
            std::vector<float> in (256, 0.10f);
            std::vector<float> oL (256, 1.0f), oR (256, 1.0f);
            const float* ins[1] = { in.data() };
            float* outs[2] = { oL.data(), oR.data() };
            float last = 0.0f;
            for (int b = 0; b < 20; ++b)
            {
                std::fill (oL.begin(), oL.end(), 1.0f);
                std::fill (oR.begin(), oR.end(), 1.0f);
                eng.process (ins, 1, outs, 2, 256);
                last = eng.snapshot().inputPeak;
            }
            return last;
        };

        const float atOne = peakAt (1.0f);
        const float atTwo = peakAt (2.0f);
        const float atZero = peakAt (0.0f);
        std::printf ("input-gain    1.0=%.3f  2.0=%.3f  0.0=%.3f\n", atOne, atTwo, atZero);
        expect (atOne > 0.05f && atTwo > atOne * 1.6f && atTwo < atOne * 2.4f,
                "input gain scales the analysis peak and leaves the output alone");
        expect (atZero < 0.002f, "input gain at zero silences the analysis bus");

        vp::VirtualPercussionEngine muteCheck;
        muteCheck.prepare (sr, 256, 1);
        muteCheck.settings().inputGain.store (0.0f);
        muteCheck.settings().masterVolume.store (1.0f);
        muteCheck.settings().percussionVolume.store (1.0f);
        muteCheck.start();
        muteCheck.tapAt (0.0);
        muteCheck.tapAt (0.5);
        muteCheck.tapAt (1.0);
        muteCheck.tapAt (1.5);
        std::vector<float> silence (256, 0.0f);
        std::vector<float> oL (256, 0.0f), oR (256, 0.0f);
        const float* ins[1] = { silence.data() };
        float* outs[2] = { oL.data(), oR.data() };
        float outPeak = 0.0f;
        for (int b = 0; b < static_cast<int> (sr * 2.0) / 256; ++b)
        {
            muteCheck.process (ins, 1, outs, 2, 256);
            for (float s : oL)
                outPeak = std::max (outPeak, std::abs (s));
        }
        expect (outPeak > 0.01f,
                "input gain does not mute shaker or congas");
    }

    // Same music, two buffer sizes. A small block puts one pulse in each
    // callback; a large one puts several into the same callback, where the
    // retrigger guard measures them against each other rather than against the
    // block before. The part is the same music either way, so the hit counts
    // have to match: a guard that fires on a legitimate pulse would show up
    // here as the large buffer playing less than the small one.
    {
        auto run = [&] (int blk, float bpm, int styleIdx)
        {
            vp::TempoFollower clock;
            clock.prepare (sr);
            clock.setPulsesPerBeat (4);
            clock.forceTempo (bpm);
            clock.setTargetTempo (bpm, 1.0f);
            clock.setLocked (true);
            clock.resetClock();

            vp::PercussionEngine perc;
            perc.prepare (sr);
            perc.setReverbAmount (0.0f);
            perc.setHumanization (0.0f);
            perc.setSwing (0.0f);
            perc.setGroove (bpm, 4);
            perc.setSubdivision (vp::Subdivision::sixteenth);
            perc.setGrooveStyle (static_cast<vp::GrooveStyle> (styleIdx));

            std::vector<float> L (static_cast<size_t> (blk)), R (static_cast<size_t> (blk));
            const int total = static_cast<int> (sr * 20.0);
            int pulses = 0;
            for (int pos = 0; pos + blk <= total; pos += blk)
            {
                const auto tick = clock.advance (blk);
                pulses += tick.pulsesFired;
                perc.render (L.data(), R.data(), blk, tick, true);
            }
            std::printf ("    blk=%5d  pulses=%5d  hits=%5d\n", blk, pulses, perc.hitsFired());
            return perc.hitsFired();
        };

        bool sameEitherWay = true;
        for (float bpm : { 100.0f, 168.0f, 200.0f })
        {
            const int small = run (128, bpm, 0);
            const int big   = run (4096, bpm, 0);
            std::printf ("buffer-size  %.0f BPM  small=%d big=%d\n",
                         static_cast<double> (bpm), small, big);
            if (small != big)
                sameEitherWay = false;
        }
        expect (sameEitherWay,
                "the part does not change with the size of the audio buffer");
    }

    // The phrase has to survive the part being switched off and back on. Every
    // bar of the four-bar sentence plays a different figure and the fourth takes
    // a fill, so where the count is matters as much as where the beat is.
    //
    // The bar count used to live inside the branch that decides whether anything
    // is audible, so it stopped dead whenever the congas were switched off, the
    // part was waiting to come in, or playback was paused - and picked up again
    // from wherever it had been left. Measured with the part muted for two bars,
    // the phrase came back exactly two bars behind the song and stayed there:
    // the figure belonged to the wrong bar and the fill landed mid-sentence,
    // which is heard as the percussion having lost the form rather than as one
    // wrong stroke.
    {
        auto hitsPerBar = [&] (bool withGap, int muteFromBar, int muteBars)
        {
            vp::TempoFollower clock;
            clock.prepare (sr);
            clock.setPulsesPerBeat (4);
            clock.forceTempo (120.0f);
            clock.setTargetTempo (120.0f, 1.0f);
            clock.setLocked (true);
            clock.resetClock();

            vp::PercussionEngine perc;
            perc.prepare (sr);
            perc.setReverbAmount (0.0f);
            perc.setHumanization (0.0f);
            perc.setSwing (0.0f);
            // Ghost notes are a coin toss per sixteenth, so they would swamp the
            // thing being measured. At zero intensity the part is deterministic.
            perc.setIntensity (0.0f);
            perc.setGroove (120.0f, 4);
            perc.setSubdivision (vp::Subdivision::sixteenth);

            std::vector<float> L (static_cast<size_t> (block)), R (static_cast<size_t> (block));
            std::vector<int> perBar;
            int bar = 0, inBar = 0;
            const int total = static_cast<int> (sr * 40.0);
            for (int pos = 0; pos + block <= total; pos += block)
            {
                const auto tick = clock.advance (block);
                const bool muted = withGap && bar >= muteFromBar && bar < muteFromBar + muteBars;
                const int before = perc.hitsFired();
                perc.render (L.data(), R.data(), block, tick, ! muted);
                inBar += perc.hitsFired() - before;
                if (tick.wrappedBar)
                {
                    perBar.push_back (muted ? -1 : inBar);
                    inBar = 0;
                    ++bar;
                }
            }
            return perBar;
        };

        const auto clean = hitsPerBar (false, 0, 0);
        const auto gapped = hitsPerBar (true, 4, 2);
        int mismatches = 0, compared = 0;
        for (size_t i = 6; i < clean.size() && i < gapped.size(); ++i)
        {
            ++compared;
            if (clean[i] != gapped[i])
                ++mismatches;
        }
        std::printf ("phrase-across-mute  bars compared=%d  mismatched=%d\n",
                     compared, mismatches);
        expect (compared > 8 && mismatches == 0,
                "the phrase comes back where the song is, not where it was muted");
    }

    // Live controls must not split one beat between two timing maps. Swing used
    // to change immediately, even when an offbeat from the old value was
    // already waiting in the voice queue; changing style did the same to the
    // pattern. Stop appeared to repair both because it happened to clear those
    // pending voices. A player changes the figure on the next quarter.
    {
        vp::PercussionEngine perc;
        perc.prepare (sr);
        perc.setReverbAmount (0.0f);
        perc.setHumanization (0.0f);
        perc.setGrooveStyle (vp::GrooveStyle::rock);
        perc.setSwing (0.0f);

        std::vector<float> L (static_cast<size_t> (block));
        std::vector<float> R (static_cast<size_t> (block));
        vp::ClockTick tick;
        tick.tempoBpm = 120.0f;
        tick.pulsesFired = 1;
        tick.pulseIndex[0] = 1;
        tick.pulseBeatInBar[0] = 0;
        perc.render (L.data(), R.data(), block, tick, true);

        perc.setGrooveStyle (vp::GrooveStyle::dance);
        perc.setSwing (1.0f);
        expect (perc.currentGrooveStyle() == vp::GrooveStyle::rock
                    && std::fabs (perc.currentSwing()) < 1.0e-6f,
                "part and swing wait rather than changing halfway through a beat");

        tick.pulseIndex[0] = 2;
        perc.render (L.data(), R.data(), block, tick, true);
        expect (perc.currentGrooveStyle() == vp::GrooveStyle::rock
                    && std::fabs (perc.currentSwing()) < 1.0e-6f,
                "the old beat remains internally consistent through its offbeat");

        tick.pulseIndex[0] = 0;
        tick.pulseBeatInBar[0] = 1;
        perc.render (L.data(), R.data(), block, tick, true);
        expect (perc.currentGrooveStyle() == vp::GrooveStyle::dance
                    && std::fabs (perc.currentSwing() - 1.0f) < 1.0e-6f,
                "part and swing commit together on the next quarter");
    }

    // A phase snap invalidates only strokes which have not happened yet. At
    // full swing the off-eighth is queued thousands of samples ahead; keeping
    // it after a re-anchor produces one hit on the old beat beside the new one.
    {
        vp::PercussionEngine perc;
        perc.prepare (sr);
        perc.setReverbAmount (0.0f);
        perc.setHumanization (0.0f);
        perc.setSwing (1.0f);
        perc.setGroove (120.0f, 4);

        std::vector<float> L (static_cast<size_t> (block));
        std::vector<float> R (static_cast<size_t> (block));
        vp::ClockTick oldGrid;
        oldGrid.tempoBpm = 120.0f;
        oldGrid.pulsesFired = 1;
        oldGrid.pulseIndex[0] = 2;
        oldGrid.pulseBeatInBar[0] = 0;
        perc.render (L.data(), R.data(), block, oldGrid, true);
        const int queued = perc.activeVoices();

        vp::ClockTick newGrid;
        newGrid.tempoBpm = 120.0f;
        newGrid.reanchored = true;
        perc.render (L.data(), R.data(), block, newGrid, true);
        std::printf ("grid-reanchor  queued-before=%d active-after=%d\n",
                     queued, perc.activeVoices());
        expect (queued > 0 && perc.activeVoices() == 0,
                "a re-anchor cannot play a delayed stroke from the old grid");
    }

    // What the grid does while the clock is aligning itself, which is what a
    // listener hears - and which nothing measured, because every other test
    // reads `tick.tempoBpm`, the tempo *before* the steer. The pulses come out
    // at `tempo * (1 - steer)`, so the whole phase correction was invisible.
    //
    // The input the loop really gets is not the truth. The decoder hands over a
    // hypothesis about six times a second and each one carries its own phase
    // error, of the order of three hundredths of a beat. Fed that, the loop used
    // to sit at its steering limit in both directions permanently - +/-6 BPM at
    // 120, 3.7 BPM rms, on a band that never moved - because the gain reached
    // that limit on an error of 0.022 of a beat, which is smaller than the
    // uncertainty of the thing it was measuring.
    {
        auto wobble = [&] (float trueBpm, int blk)
        {
            vp::TempoFollower clock;
            clock.prepare (sr);
            clock.setPulsesPerBeat (4);
            clock.forceTempo (trueBpm);
            clock.setTargetTempo (trueBpm, 1.0f);
            clock.setFollowStrength (vp::FollowStrength::high);
            clock.setLocked (true);
            clock.resetClock();

            // A deterministic stand-in for the decoder's phase error: held for a
            // sixth of a second at a time, because that is how often a new
            // hypothesis arrives. White noise per block would be averaged away
            // by the loop's own smoothing and would flatter it.
            double songPhase = -0.04;
            double t = 0.0, nextRefresh = 0.0;
            float held = 0.0f;
            int seed = 1;
            double sqAcc = 0.0;
            int n = 0;
            float worst = 0.0f;

            const double dt = blk / sr;
            for (int i = 0; i < static_cast<int> (sr * 20.0) / blk; ++i)
            {
                const double before = clock.beatsElapsed() + clock.beatPhase();
                const float songFrac = static_cast<float> (songPhase - std::floor (songPhase));
                if (t >= nextRefresh)
                {
                    seed = seed * 1103515245 + 12345;
                    held = 0.03f * (static_cast<float> ((seed >> 16) & 0x7fff) / 16383.5f - 1.0f);
                    nextRefresh += 1.0 / 6.0;
                }
                const float seen = vp::wrap01 (songFrac + held);

                const double songBefore = songPhase;
                songPhase += trueBpm / 60.0 * dt;
                if (std::floor (songPhase) > std::floor (songBefore))
                    clock.observeOnsetPhase (vp::wrap01 (clock.beatPhase() - seen), 0.6f, 1);

                clock.setGridPhase (seen, 0.90f);
                clock.advance (blk);
                t += dt;

                const double after = clock.beatsElapsed() + clock.beatPhase();
                const double rate = (after - before) / dt * 60.0;
                if (t > 2.0)
                {
                    const double dev = rate - trueBpm;
                    sqAcc += dev * dev;
                    ++n;
                    worst = std::max (worst, static_cast<float> (std::fabs (dev)));
                }
            }
            return std::pair<float, float> { n > 0 ? static_cast<float> (std::sqrt (sqAcc / n)) : 0.0f,
                                             worst };
        };

        bool steady = true;
        for (float bpm : { 80.0f, 120.0f, 160.0f })
        {
            const auto [rms, worst] = wobble (bpm, 256);
            std::printf ("phase-steer  %.0f BPM  rms=%.2f BPM  worst=%.2f BPM\n",
                         static_cast<double> (bpm), static_cast<double> (rms),
                         static_cast<double> (worst));
            // Four thousandths of the tempo. Not a perceptual threshold - it is
            // where this loop actually sits once it is not chasing noise, with
            // the margin to say so: it measures 0.0016 of the tempo, the gains
            // it shipped with measured 0.0065, and the per-block smoothing they
            // shipped with measured 0.031. Anything that loosens the loop back
            // towards its input's own uncertainty crosses this.
            if (rms > bpm * 0.004f)
                steady = false;
        }
        expect (steady,
                "the grid does not chase the analysis's own phase noise as if it were the band");

        // And the same music has to come out the same whatever the buffer is.
        // The smoothing used to be a fixed blend applied once per callback, so
        // its time constant was whatever the buffer happened to be: measured at
        // 4.2 BPM rms on 64 frames against 2.2 on 1024, same song. With the
        // buffer now something the listener can change on the settings page,
        // that is a setting that quietly retunes the tracker.
        const float small = wobble (120.0f, 64).first;
        const float large = wobble (120.0f, 1024).first;
        std::printf ("phase-steer  buffer 64=%.2f  1024=%.2f BPM rms\n",
                     static_cast<double> (small), static_cast<double> (large));
        expect (std::fabs (small - large) < 0.25f,
                "the phase loop behaves the same whatever the buffer size");
    }

    // And how long it takes to get there in the first place.
    //
    // Phase is corrected by bending the rate, never by moving the grid, so how
    // long an error takes to close is `error / limit` beats and nothing else -
    // and the limit used to be the same 3.5-5% of the tempo whether the clock
    // was a hundredth of a beat out or half of one. Measured at 120 BPM on
    // HIGH, half a beat took 4.85 s to close, and on LOW 13.3 s: a new track,
    // an edit, or a re-lock left the part audibly beside the song for that
    // long. The ceiling opens with the error now, above the point where the
    // analysis's own noise could have produced it.
    //
    // What must not change is that the grid stays monotonic while it does that.
    // The correction is still a rate, `1 - steer` is still nowhere near zero,
    // so no pulse may be emitted twice or stepped over - which is the whole
    // reason the loop bends the rate instead of moving the grid.
    {
        constexpr float bpm = 120.0f;
        constexpr int blk = 256;

        auto align = [&] (double offsetBeats, vp::FollowStrength fs, double& worstGap)
        {
            vp::TempoFollower clock;
            clock.prepare (sr);
            clock.setPulsesPerBeat (4);
            clock.forceTempo (bpm);
            clock.setTargetTempo (bpm, 1.0f);
            clock.setFollowStrength (fs);
            clock.setLocked (true);
            clock.resetClock();

            const double dt = blk / sr;
            double songPhase = -offsetBeats, t = 0.0, sincePulse = -1.0;
            double got = -1.0;
            worstGap = 0.0;
            const double pulseSec = 60.0 / static_cast<double> (bpm) / 4.0;

            for (int i = 0; i < static_cast<int> (40.0 * sr) / blk; ++i)
            {
                const float songFrac = static_cast<float> (songPhase - std::floor (songPhase));
                if (got < 0.0
                    && std::fabs (vp::wrapCentered (clock.beatPhase() - songFrac)) < 0.03f)
                    got = t;

                const double songBefore = songPhase;
                songPhase += bpm / 60.0 * dt;
                if (std::floor (songPhase) > std::floor (songBefore))
                    clock.observeOnsetPhase (vp::wrap01 (clock.beatPhase() - songFrac), 0.8f, 1);
                clock.setGridPhase (songFrac, 0.90f);
                const auto tick = clock.advance (blk);

                for (int k = 0; k < tick.pulsesFired; ++k)
                {
                    const double at = t + tick.pulseOffset[k] / sr;
                    if (sincePulse >= 0.0)
                        worstGap = std::max (worstGap, (at - sincePulse) / pulseSec);
                    sincePulse = at;
                }
                t += dt;
            }
            return got;
        };

        double gapHigh = 0.0, gapLow = 0.0;
        const double high = align (0.48, vp::FollowStrength::high, gapHigh);
        const double low = align (0.48, vp::FollowStrength::low, gapLow);
        std::printf ("phase-align  half a beat: HIGH %.2f s  LOW %.2f s"
                     "  longest gap %.2f / %.2f pulses\n",
                     high, low, gapHigh, gapLow);

        // Measured 2.81 s on HIGH and 5.92 s on LOW, against 4.85 and 13.34
        // before the ceiling was allowed to open. The bounds sit between the
        // two pairs.
        expect (high > 0.0 && high < 4.0, "half a beat of phase closes in a few beats, not five seconds");
        expect (low > 0.0 && low < 9.0, "and LOW is gentler about it without giving up on it");
        // Under two pulses is one pulse's worth of stretch and no more; a
        // dropped pulse shows up here as a gap of two, a doubled one as a gap
        // near zero, and neither may happen.
        expect (gapHigh < 1.9 && gapLow < 1.9,
                "and the grid never drops a stroke while it catches up");
    }

    // The part must not be able to switch the tracker off.
    //
    // The loudness gate that decides whether there is music to follow reads the
    // *analysis* signal - after the leak subtraction and after the make-up gain
    // - and both of those moved when the subtraction was made to work. Two ways
    // that goes wrong, and the symptom of either is the same: the part plays for
    // a moment, everything goes quiet, and it stops.
    //
    //   - on a feed carrying none of our output, a subtraction that fires at all
    //     is eating the band;
    //   - on a feed that does carry it, taking it out leaves less signal, and
    //     the make-up gain was being driven by the level *before* the
    //     subtraction, so nothing put the remainder back where the network
    //     expects it.
    {
        auto analysisLevel = [&] (float percVolume, float feedback, vp::FollowSource source)
        {
            const int blk = 256;
            vp::VirtualPercussionEngine eng;
            eng.prepare (sr, blk, 1);
            eng.settings().followSource.store (static_cast<int> (source));
            eng.settings().percussionVolume.store (percVolume);
            eng.settings().masterVolume.store (1.0f);
            eng.settings().reverbAmount.store (0.0f);
            eng.setReportedLatencyMs (12.0f);
            eng.start();
            // Four taps put the clock on 120 without waiting for the network,
            // so the part is playing for the whole of the measured window.
            eng.tapAt (0.0); eng.tapAt (0.5); eng.tapAt (1.0); eng.tapAt (1.5);

            const int delay = static_cast<int> (0.012 * sr);
            std::vector<float> history (static_cast<size_t> (delay + blk * 4), 0.0f);
            int histWrite = 0;

            std::vector<float> in (static_cast<size_t> (blk), 0.0f);
            std::vector<float> oL (static_cast<size_t> (blk), 0.0f);
            std::vector<float> oR (static_cast<size_t> (blk), 0.0f);
            float* outs[2] = { oL.data(), oR.data() };

            // A band: a broadband stroke on every eighth at 120, steady level.
            const int beatSamples = static_cast<int> (sr * 0.25);
            unsigned rng = 22222u;
            double sum = 0.0;
            int counted = 0;
            const int blocks = static_cast<int> (sr * 12.0) / blk;
            for (int bi = 0; bi < blocks; ++bi)
            {
                for (int i = 0; i < blk; ++i)
                {
                    const int pos = bi * blk + i;
                    const int intoBeat = pos % beatSamples;
                    rng = rng * 1664525u + 1013904223u;
                    const float noise = static_cast<float> ((rng >> 9) & 0xffff) / 32767.5f - 1.0f;
                    const float env = std::exp (-static_cast<float> (intoBeat) / (sr * 0.030f));
                    const float band = 0.25f * noise * env;
                    const int ri = (histWrite - delay + i + static_cast<int> (history.size()))
                                   % static_cast<int> (history.size());
                    in[static_cast<size_t> (i)] = band + feedback * history[static_cast<size_t> (ri)];
                }
                const float* ins[1] = { in.data() };
                eng.process (ins, 1, outs, 2, blk);
                for (int i = 0; i < blk; ++i)
                {
                    history[static_cast<size_t> (histWrite)] =
                        0.5f * (oL[static_cast<size_t> (i)] + oR[static_cast<size_t> (i)]);
                    histWrite = (histWrite + 1) % static_cast<int> (history.size());
                }
                if (bi > blocks / 2)
                {
                    sum += static_cast<double> (eng.snapshot().analysisPeak);
                    ++counted;
                }
            }
            return counted > 0 ? static_cast<float> (sum / counted) : 0.0f;
        };

        // The gate the tracker uses. Below this there is no music as far as the
        // state machine is concerned, and the part is muted.
        constexpr float kMixerGate = 0.0020f;
        constexpr float kSpeakerGate = 0.0010f;

        for (const auto& mode : { std::pair<vp::FollowSource, const char*>
                                      { vp::FollowSource::kitMic, "MIXER" },
                                  { vp::FollowSource::speaker, "IPAD" } })
        {
            const float quiet = analysisLevel (0.0f, 0.0f, mode.first);
            const float loud  = analysisLevel (1.0f, 0.0f, mode.first);
            const float loopedQuiet = analysisLevel (0.0f, 0.5f, mode.first);
            const float loopedLoud  = analysisLevel (1.0f, 0.5f, mode.first);
            std::printf ("analysis-level  %-6s clean: part off=%.4f on=%.4f   "
                         "returned: off=%.4f on=%.4f\n",
                         mode.second, static_cast<double> (quiet), static_cast<double> (loud),
                         static_cast<double> (loopedQuiet), static_cast<double> (loopedLoud));

            const float gate = mode.first == vp::FollowSource::speaker ? kSpeakerGate : kMixerGate;
            expect (loud > gate * 4.0f && loopedLoud > gate * 4.0f,
                    mode.first == vp::FollowSource::speaker
                        ? "IPAD: the analysis stays well above the gate with the part at full volume"
                        : "MIXER: the analysis stays well above the gate with the part at full volume");
            // And turning the part up must not quietly cost the band - on either
            // kind of feed. On a clean one there is nothing of ours to take out,
            // so any loss is the band. On a returned one the subtraction does
            // remove signal, and the make-up gain has to put the remainder back
            // where the network expects it: driven by the level *before* the
            // subtraction it did not, and the same band arrived at 0.045 instead
            // of 0.076 for no reason the network could know about.
            expect (loopedLoud > loopedQuiet * 0.8f,
                    mode.first == vp::FollowSource::speaker
                        ? "IPAD: taking our own part out does not leave the band quieter"
                        : "MIXER: taking our own part out does not leave the band quieter");
            expect (loud > quiet * 0.9f,
                    mode.first == vp::FollowSource::speaker
                        ? "IPAD: playing the part does not eat the band on a feed that has no return"
                        : "MIXER: playing the part does not eat the band on a feed that has no return");
        }
    }

    vpRunAiBeatTests (gPassed, gFailed);
    vpRunMakeupTests (gPassed, gFailed);
    vpRunLoopTests (gPassed, gFailed);

    std::printf ("\n%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
