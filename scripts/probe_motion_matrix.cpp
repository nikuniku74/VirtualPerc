// Global tempo-motion scoreboard with a known beat grid.
//
// A handful of songs can reject a change, but must never tune it. This probe
// supplies the missing population: deterministic random tempi from 55 to 175
// BPM, several rhythmic densities, swing, onset jitter, missed beats, false
// peaks and occasional drum gaps. Every run owns its exact beat times, so the
// score is clock phase in milliseconds rather than agreement with another beat
// tracker.
//
// Build (decoder + the clock heard by the percussion engine):
//   clang++ -std=c++17 -O2 -Wno-unused-function -I Source \
//     scripts/probe_motion_matrix.cpp Source/AI/BeatDecoder.cpp \
//     Source/AI/TempoMotionTracker.cpp Source/AI/TempoMotionShape.cpp \
//     Source/AI/TempoEstimator.cpp \
//     Source/AI/BeatHmm.cpp \
//     Source/Tracking/TempoFollower.cpp -o /tmp/probe_motion_matrix
//
// Run the default 192 cases, or 48 while developing:
//   /tmp/probe_motion_matrix
//   /tmp/probe_motion_matrix --quick

#include "AI/BeatDecoder.h"
#include "Tracking/PhaseTrust.h"
#include "Tracking/TempoFollower.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace
{
constexpr double kFps = 50.0;
constexpr double kSampleRate = 48000.0;
constexpr int kSamplesPerFrame = static_cast<int> (kSampleRate / kFps);
constexpr double kDuration = 90.0;
constexpr double kWarmup = 24.0;

enum class MotionKind { flat, smooth, step };

struct Event
{
    double frame = 0.0;
    float amplitude = 0.0f;
};

struct Scenario
{
    MotionKind kind = MotionKind::flat;
    double baseBpm = 100.0;
    double motionStart = 24.0;
    double sineAmplitude = 0.0;
    double sinePeriod = 30.0;
    double sineAmplitude2 = 0.0;
    double sinePeriod2 = 47.0;
    double stepAt = 42.0;
    double stepRatio = 1.0;
    int subdivision = 1;
    double subdivisionAmplitude = 0.0;
    double swing = 0.5;
    double jitterSec = 0.0;
    double missChance = 0.0;
    double falsePeaksPerSec = 0.0;
    double gapStart = -1.0;
    double gapEnd = -1.0;

    double bpmAt (double t) const noexcept
    {
        if (kind == MotionKind::flat || t < motionStart)
            return baseBpm;
        if (kind == MotionKind::step)
            return t < stepAt ? baseBpm : baseBpm * stepRatio;

        const double x = t - motionStart;
        const double moving = baseBpm
                            * (1.0
                               + sineAmplitude * std::sin (2.0 * M_PI * x / sinePeriod)
                               + sineAmplitude2 * std::sin (2.0 * M_PI * x / sinePeriod2));
        return std::clamp (moving, 50.0, 190.0);
    }
};

struct Score
{
    std::vector<double> phaseMs;
    double tempoErrorSum = 0.0;
    double measured = 0.0;
    int phaseOver50 = 0;
    int tempoOver4 = 0;
    int fixedToLive = 0;
    int curveProofs = 0;
    int recoveryViolations = 0;
    int authorityFrames = 0;
    uint64_t traceHash = 1469598103934665603ULL;
};

void hashWord (uint64_t& hash, uint32_t word) noexcept
{
    for (int byte = 0; byte < 4; ++byte)
    {
        hash ^= static_cast<uint8_t> (word >> (byte * 8));
        hash *= 1099511628211ULL;
    }
}

void hashFloat (uint64_t& hash, float value) noexcept
{
    uint32_t word = 0;
    static_assert (sizeof (word) == sizeof (value));
    std::memcpy (&word, &value, sizeof (word));
    hashWord (hash, word);
}

double percentile (std::vector<double> values, double p)
{
    if (values.empty())
        return 0.0;
    std::sort (values.begin(), values.end());
    const size_t i = static_cast<size_t> (
        std::clamp (p, 0.0, 1.0) * static_cast<double> (values.size() - 1));
    return values[i];
}

Scenario makeScenario (MotionKind kind, unsigned seed)
{
    std::mt19937 rng (seed * 747796405u + 2891336453u);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    Scenario s;
    s.kind = kind;
    s.baseBpm = 55.0 + 120.0 * u (rng);
    s.subdivision = u (rng) < 0.28 ? 1 : (u (rng) < 0.62 ? 2 : 4);
    s.subdivisionAmplitude = s.subdivision == 1 ? 0.0 : 0.18 + 0.44 * u (rng);
    s.swing = 0.50 + 0.18 * u (rng);
    s.jitterSec = (4.0 + 24.0 * u (rng)) * 0.001;
    s.missChance = 0.18 * u (rng);
    s.falsePeaksPerSec = 0.18 * u (rng);

    // One run in four drops the kit for between one and four beats. The band
    // grid continues: this is an arrangement hole, not a tempo change.
    if ((seed & 3u) == 0u)
    {
        s.gapStart = 52.0 + 8.0 * u (rng);
        s.gapEnd = s.gapStart + (1.0 + 3.0 * u (rng)) * 60.0 / s.baseBpm;
    }

    if (kind == MotionKind::smooth)
    {
        s.sineAmplitude = (0.018 + 0.060 * u (rng)) * (u (rng) < 0.5 ? -1.0 : 1.0);
        s.sinePeriod = 16.0 + 36.0 * u (rng);
        s.sineAmplitude2 = (0.004 + 0.014 * u (rng)) * (u (rng) < 0.5 ? -1.0 : 1.0);
        s.sinePeriod2 = 31.0 + 31.0 * u (rng);
    }
    else if (kind == MotionKind::step)
    {
        s.stepAt = 34.0 + 18.0 * u (rng);
        double delta = 0.05 + 0.15 * u (rng);
        if (u (rng) < 0.5)
            delta = -delta;
        double stepped = s.baseBpm * (1.0 + delta);
        if (stepped < 50.0 || stepped > 190.0)
            delta = -delta;
        s.stepRatio = 1.0 + delta;
    }
    return s;
}

std::vector<double> makeBeatGrid (const Scenario& s)
{
    std::vector<double> beats;
    double t = 0.0;
    while (t < kDuration + 2.0)
    {
        beats.push_back (t);
        double period = 60.0 / s.bpmAt (t);
        // Midpoint refinement keeps the written grid independent of the
        // decoder while avoiding Euler lag on the fastest accelerandi.
        period = 60.0 / s.bpmAt (t + 0.5 * period);
        t += period;
    }
    return beats;
}

std::vector<Event> makeEvents (const Scenario& s, const std::vector<double>& beats,
                               unsigned seed)
{
    std::mt19937 rng (seed * 277803737u + 1171808521u);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    std::normal_distribution<double> jitter (0.0, s.jitterSec);
    std::vector<Event> events;

    for (size_t i = 0; i + 1 < beats.size(); ++i)
    {
        const double t = beats[i];
        const double span = beats[i + 1] - t;
        const bool gap = s.gapStart >= 0.0 && t >= s.gapStart && t < s.gapEnd;
        if (! gap && u (rng) >= s.missChance)
        {
            const float accent = static_cast<float> (0.72 + 0.28 * u (rng));
            events.push_back ({ (t + jitter (rng)) * kFps, 0.94f * accent });
        }

        if (! gap && s.subdivision > 1)
        {
            for (int k = 1; k < s.subdivision; ++k)
            {
                if (u (rng) < s.missChance)
                    continue;
                double frac = static_cast<double> (k) / s.subdivision;
                if (s.subdivision == 2)
                    frac = s.swing;
                else if ((k & 1) != 0)
                {
                    const double cell = std::floor (frac * 2.0) * 0.5;
                    frac = cell + 0.5 * s.swing;
                }
                events.push_back ({ (t + frac * span + jitter (rng)) * kFps,
                                    static_cast<float> (0.94 * s.subdivisionAmplitude
                                                        * (0.75 + 0.25 * u (rng))) });
            }
        }
    }

    std::exponential_distribution<double> falseGap (
        std::max (1.0e-4, s.falsePeaksPerSec));
    if (s.falsePeaksPerSec > 1.0e-4)
    {
        double t = falseGap (rng);
        while (t < kDuration)
        {
            events.push_back ({ t * kFps, static_cast<float> (0.12 + 0.34 * u (rng)) });
            t += falseGap (rng);
        }
    }

    std::sort (events.begin(), events.end(),
               [] (const Event& a, const Event& b) { return a.frame < b.frame; });
    return events;
}

Score run (const Scenario& s, unsigned seed, bool verbose)
{
    const auto beats = makeBeatGrid (s);
    const auto events = makeEvents (s, beats, seed);

    vp::BeatDecoder decoder;
    decoder.prepare (kFps);
    decoder.setLineFeed (true);
    decoder.setLevelAnchor (true);
    vp::TempoFollower clock;
    clock.prepare (kSampleRate);
    clock.setPulsesPerBeat (4);
    clock.setFollowStrength (vp::FollowStrength::high);
    clock.setLocked (true);
    clock.setTempoTrimEnabled (true);

    Score score;
    size_t eventLo = 0, truth = 0;
    uint32_t lastSerial = 0;
    bool haveSerial = false;
    bool curveProofActive = false;
    bool shadowProven = false;
    size_t excursionTruthBeat = 0;
    bool excursionOpen = false;
    bool excursionFailed = false;
    vp::TempoRegime previousRegime = vp::TempoRegime::unknown;
    std::mt19937 floorRng (seed * 2246822519u + 3266489917u);
    std::uniform_real_distribution<float> floor (0.012f, 0.032f);

    const int totalFrames = static_cast<int> (kDuration * kFps);
    for (int frame = 0; frame < totalFrames; ++frame)
    {
        while (eventLo < events.size() && events[eventLo].frame < frame - 7.0)
            ++eventLo;
        float activation = floor (floorRng);
        for (size_t e = eventLo; e < events.size() && events[e].frame <= frame + 7.0; ++e)
        {
            const double d = (frame - events[e].frame) / 1.35;
            activation = std::max (activation,
                                   events[e].amplitude
                                       * static_cast<float> (std::exp (-0.5 * d * d)));
        }

        const vp::BeatHypothesis h = decoder.observe (
            activation, 0.025f, 1.0f - activation);
        if (h.valid)
        {
            const bool cleanMotion = h.regime == vp::TempoRegime::fixed
                                     && std::fabs (h.fastTempoDeviation)
                                            > vp::kTempoMotionDeviation
                                     && h.shortFitResidual < vp::kTempoMotionResidual;
            clock.setTempoMotionHint (cleanMotion);
            const auto diagnostics = decoder.diagnostics();
            const bool curveProof = h.regime == vp::TempoRegime::fixed
                                    && diagnostics.motionFitEvidence >= 3;
            if (curveProof && ! curveProofActive)
            {
                ++score.curveProofs;
                if (verbose)
                    std::printf ("    curve@%5.2fs truth=%6.2f committed=%6.2f"
                                 " motion=%6.2f rate=%+.4f short=%6.2f long=%6.2f"
                                 " residual=%5.3f/%5.3f improve=%5.3f\n",
                                 frame / kFps, s.bpmAt (frame / kFps), h.bpm,
                                 diagnostics.motionFit, diagnostics.motionFitRate,
                                 h.shortFitBpm, h.longFitBpm,
                                 diagnostics.motionFitResidual, h.shortFitResidual,
                                 diagnostics.motionFitImprovement);
            }
            curveProofActive = curveProof;
            if (h.bpm > 50.0f)
                clock.setTargetTempo (h.bpm, h.confidence);
            if (! haveSerial)
            {
                lastSerial = h.beatSerial;
                haveSerial = true;
            }
            else if (h.beatSerial != lastSerial && h.confidence > 0.25f)
            {
                lastSerial = h.beatSerial;
                clock.observeOnsetPhase (vp::wrap01 (clock.beatPhase() - h.beatPhase),
                                         h.confidence, 1);
            }
            clock.setGridPhase (h.beatPhase,
                                vp::gridPhaseTau (cleanMotion
                                                      ? vp::kGridTauMotion
                                                      : vp::kGridTauHolding,
                                                  true, 1.0f));

            if (previousRegime == vp::TempoRegime::fixed
                && h.regime == vp::TempoRegime::live)
                ++score.fixedToLive;
            previousRegime = h.regime;
        }

        // Performance scoring starts after acquisition, but false authority is
        // a safety diagnostic and must cover the whole run. The hypothesis
        // field keeps the existing stale gate on this wider counter.
        const float motionAuthority = h.motionBridgeAuthority;
        if (motionAuthority > 0.0f)
            ++score.authorityFrames;
        shadowProven = shadowProven || motionAuthority > 0.0f;

        const double now = frame / kFps;
        while (truth + 1 < beats.size() && beats[truth + 1] <= now)
            ++truth;
        if (now >= kWarmup && truth + 1 < beats.size())
        {
            const double span = beats[truth + 1] - beats[truth];
            const float truePhase = span > 1.0e-9
                                        ? static_cast<float> ((now - beats[truth]) / span)
                                        : 0.0f;
            const double phaseBeats = std::fabs (vp::wrapCentered (
                clock.beatPhase() - truePhase));
            const double phaseMs = phaseBeats * 60000.0 / s.bpmAt (now);
            score.phaseMs.push_back (phaseMs);
            if (phaseMs > 50.0)
                ++score.phaseOver50;

            if (h.valid && h.bpm > 0.0f)
            {
                const double tempoError = std::fabs (h.bpm - s.bpmAt (now))
                                        / s.bpmAt (now) * 100.0;
                score.tempoErrorSum += tempoError;
                if (tempoError > 4.0)
                    ++score.tempoOver4;
            }
            score.measured += 1.0;

            hashFloat (score.traceHash, h.bpm);
            hashFloat (score.traceHash, clock.beatPhase());
            hashFloat (score.traceHash, clock.currentTempo());
            hashWord (score.traceHash, static_cast<uint32_t> (truth));

            if (shadowProven && phaseMs > 50.0 && ! excursionOpen)
            {
                excursionOpen = true;
                excursionFailed = false;
                excursionTruthBeat = truth;
            }
            if (excursionOpen && phaseMs <= 50.0)
            {
                excursionOpen = false;
                excursionFailed = false;
            }
            if (excursionOpen && ! excursionFailed && truth > excursionTruthBeat + 2)
            {
                ++score.recoveryViolations;
                excursionFailed = true;
            }
        }
        clock.advance (kSamplesPerFrame);
    }
    return score;
}

struct Aggregate
{
    double meanSum = 0.0;
    double p95Sum = 0.0;
    double worst = 0.0;
    double phaseOver50 = 0.0;
    double tempoError = 0.0;
    double tempoOver4 = 0.0;
    int releases = 0;
    int curveProofs = 0;
    int runs = 0;
    uint64_t traceHash = 1469598103934665603ULL;
    int recoveryViolations = 0;
    int authorityFrames = 0;
};

Aggregate printFamily (MotionKind kind, const char* label, int cases, unsigned offset,
                       bool verbose, bool csv)
{
    Aggregate a;
    for (int i = 0; i < cases; ++i)
    {
        const unsigned seed = offset + 1009u + static_cast<unsigned> (i) * 7919u
                              + static_cast<unsigned> (kind) * 104729u;
        const Scenario scenario = makeScenario (kind, seed);
        Score score = run (scenario, seed, verbose);
        double sum = 0.0;
        for (double x : score.phaseMs)
            sum += x;
        const double n = std::max (1.0, score.measured);
        a.meanSum += sum / n;
        a.p95Sum += percentile (score.phaseMs, 0.95);
        a.worst = std::max (a.worst, percentile (score.phaseMs, 0.995));
        a.phaseOver50 += score.phaseOver50 / n * 100.0;
        a.tempoError += score.tempoErrorSum / n;
        a.tempoOver4 += score.tempoOver4 / n * 100.0;
        a.releases += score.fixedToLive;
        a.curveProofs += score.curveProofs;
        a.recoveryViolations += score.recoveryViolations;
        a.authorityFrames += score.authorityFrames;
        hashWord (a.traceHash, static_cast<uint32_t> (score.traceHash & 0xffffffffULL));
        hashWord (a.traceHash, static_cast<uint32_t> (score.traceHash >> 32));
        ++a.runs;
        if (verbose)
        {
            std::printf ("  seed=%10u bpm=%6.1f sub=%d swing=%.3f jitter=%4.1fms"
                         " miss=%4.1f%% false=%4.2f/s phase=%6.1f/%6.1f/%6.1fms"
                         " bpmErr=%5.2f%% F->V=%d curve=%d\n",
                         seed, scenario.baseBpm, scenario.subdivision, scenario.swing,
                         scenario.jitterSec * 1000.0, scenario.missChance * 100.0,
                         scenario.falsePeaksPerSec, sum / n,
                         percentile (score.phaseMs, 0.95),
                         percentile (score.phaseMs, 0.995),
                         score.tempoErrorSum / n, score.fixedToLive,
                         score.curveProofs);
        }
    }

    const double n = std::max (1, a.runs);
    if (csv)
    {
        std::printf (
            "%u,%s,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%d,%d,%016llx,%d,%d\n",
            offset, label, a.runs, a.meanSum / n, a.p95Sum / n, a.worst,
            a.phaseOver50 / n, a.tempoError / n, a.tempoOver4 / n,
            a.releases, a.curveProofs,
            static_cast<unsigned long long> (a.traceHash),
            a.recoveryViolations, a.authorityFrames);
    }
    else
    {
        std::printf ("%-12s %5d %10.1f %10.1f %10.1f %9.2f%% %9.3f%% %9.2f%% %8d %8d\n",
                     label, a.runs, a.meanSum / n, a.p95Sum / n, a.worst,
                     a.phaseOver50 / n, a.tempoError / n, a.tempoOver4 / n,
                     a.releases, a.curveProofs);
    }
    return a;
}
} // namespace

int main (int argc, char** argv)
{
    bool quick = false;
    bool verbose = false;
    bool csv = false;
    unsigned offset = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--quick") == 0)
            quick = true;
        else if (std::strcmp (argv[i], "--verbose") == 0)
            verbose = true;
        else if (std::strcmp (argv[i], "--csv") == 0)
            csv = true;
        else if (std::strcmp (argv[i], "--offset") == 0 && i + 1 < argc)
            offset = static_cast<unsigned> (std::strtoul (argv[++i], nullptr, 10));
    }
    const int cases = quick ? 16 : 64;
    if (csv && offset == 0)
    {
        std::printf (
            "offset,family,runs,mean,p95,p995,over50,bpm_error,bpm_over4,releases,"
            "curve,trace_hash,recovery_violations,authority_frames\n");
    }
    if (! csv)
    {
        std::printf ("Matrice globale del moto: verita' scritta, %d casi per famiglia, offset %u.\n",
                     cases, offset);
        std::printf ("%-12s %5s %10s %10s %10s %10s %10s %10s %8s %8s\n",
                     "famiglia", "corse", "fase media", "fase p95", "fase p99.5",
                     ">50 ms", "err BPM", ">4% BPM", "F->V", "curve");
    }
    const Aggregate fixed =
        printFamily (MotionKind::flat, "fisso", cases, offset, verbose, csv);
    const Aggregate smooth =
        printFamily (MotionKind::smooth, "continuo", cases, offset, verbose, csv);
    printFamily (MotionKind::step, "gradino", cases, offset, verbose, csv);

    if (csv)
        return 0;

    // A selector that would release a click-stable tempo even once is not a
    // production gate. The moving population must still contain qualifying
    // evidence, otherwise zero false positives was obtained by disabling the
    // detector rather than by separating motion from onset scatter.
    const bool selectorPass = fixed.curveProofs == 0 && smooth.curveProofs > 0;
    std::printf ("selettore curvatura: %s\n", selectorPass ? "PASS" : "FAIL");
    return selectorPass ? 0 : 1;
}
