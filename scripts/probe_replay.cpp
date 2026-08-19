// Replays a recorded BeatNet activation dump through BeatDecoder alone.
//
// The full probe renders a minute of audio and runs the network for every case,
// which is two minutes a sweep - too slow to tune a decision rule against. The
// activations do not change when the decoder does, so record them once and
// replay them: same signal, same measurements, a second a sweep.
//
// Usage: VPReplay <dump.txt> [...]   (dumps come from VPActivations)
#include "AI/BeatDecoder.h"
#include "AI/TempoEstimator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
struct Dump
{
    std::string name;
    float bpm = 0.0f;
    std::vector<float> pBeat, pDown;
};

bool loadDump (const char* path, Dump& d)
{
    FILE* f = std::fopen (path, "r");
    if (f == nullptr)
        return false;
    d.name = path;
    const size_t slash = d.name.find_last_of ('/');
    if (slash != std::string::npos)
        d.name = d.name.substr (slash + 1);
    if (d.name.rfind ("act_", 0) == 0)
        d.name = d.name.substr (4);
    if (d.name.size() > 4 && d.name.substr (d.name.size() - 4) == ".txt")
        d.name = d.name.substr (0, d.name.size() - 4);

    char line[256];
    while (std::fgets (line, sizeof line, f) != nullptr)
    {
        if (line[0] == '#')
        {
            double bpm = 0.0;
            if (std::sscanf (line, "# bpm %lf", &bpm) == 1)
                d.bpm = static_cast<float> (bpm);
            continue;
        }
        int idx = 0;
        double b = 0.0, dn = 0.0;
        if (std::sscanf (line, "%d %lf %lf", &idx, &b, &dn) == 3)
        {
            d.pBeat.push_back (static_cast<float> (b));
            d.pDown.push_back (static_cast<float> (dn));
        }
    }
    std::fclose (f);
    return d.bpm > 1.0f && ! d.pBeat.empty();
}

const char* regimeName (vp::TempoRegime r)
{
    switch (r)
    {
        case vp::TempoRegime::unknown: return "unk";
        case vp::TempoRegime::fixed:   return "FIX";
        case vp::TempoRegime::live:    return "live";
    }
    return "?";
}

struct Stats
{
    double tValid = -1.0, t2 = -1.0;
    float  bpmEnd = 0.0f, span = 0.0f, wobble = 0.0f;
    int    jumps = 0, reanchors = 0;
    double fixedFraction = 0.0;
};

Stats replay (const Dump& d, bool trace)
{
    constexpr double fps = 50.0;
    vp::BeatDecoder dec;
    dec.prepare (fps);

    Stats s;
    float lo = 1.0e9f, hi = 0.0f;
    float prev = 0.0f;
    double prevT = -1.0;
    double acc = 0.0;
    int accN = 0, fixedFrames = 0, steadyFrames = 0;
    float lastBpm = 0.0f;

    for (size_t i = 0; i < d.pBeat.size(); ++i)
    {
        const auto h = dec.observe (d.pBeat[i], d.pDown[i], 0.0f);
        const double t = static_cast<double> (i) / fps;

        if (s.tValid < 0.0 && h.valid)
            s.tValid = t;
        if (h.valid)
        {
            const bool close = std::fabs (h.bpm - d.bpm) / d.bpm < 0.02f;
            if (close && s.t2 < 0.0) s.t2 = t;
            if (! close) s.t2 = -1.0;
            if (lastBpm > 1.0f && std::fabs (h.bpm - lastBpm) / lastBpm > 0.10f)
                ++s.reanchors;
            lastBpm = h.bpm;
        }

        // Steady state: everything past twenty-five seconds, the same window
        // the engine probe uses. A record does not change tempo, so from here
        // on any movement at all is the defect.
        if (t > 25.0 && h.valid)
        {
            ++steadyFrames;
            fixedFrames += h.regime == vp::TempoRegime::fixed;
            lo = std::min (lo, h.bpm);
            hi = std::max (hi, h.bpm);
            if (t - prevT >= 0.5)
            {
                if (prev > 1.0f)
                {
                    const float delta = std::fabs (h.bpm - prev);
                    acc += delta;
                    ++accN;
                    if (delta > 0.5f) ++s.jumps;
                }
                prev = h.bpm;
                prevT = t;
            }
        }
        if (trace && (i % 25) == 0)
        {
            const auto g = dec.diagnostics();
            std::printf ("   t=%5.1f bpm=%7.2f %-4s conf=%.2f | comb=%7.2f sal=%.2f set=%d"
                         " long=%7.2f short=%7.2f res=%.3f cov=%.2f mism=%d\n", t,
                         static_cast<double> (h.bpm), regimeName (h.regime),
                         static_cast<double> (h.confidence),
                         static_cast<double> (g.combBpm), static_cast<double> (g.combSalience),
                         g.levelSettled ? 1 : 0,
                         static_cast<double> (g.longFit), static_cast<double> (g.shortFit),
                         static_cast<double> (g.residual), static_cast<double> (g.coverage),
                         g.octaveMismatch);
        }
        s.bpmEnd = h.bpm;
    }

    s.span = hi >= lo ? hi - lo : 0.0f;
    s.wobble = accN > 0 ? static_cast<float> (acc / accN) : 0.0f;
    s.fixedFraction = steadyFrames > 0 ? static_cast<double> (fixedFrames) / steadyFrames : 0.0;
    return s;
}
} // namespace

// Replays the activation into TempoEstimator alone and prints, every half
// second, what each of the three plausible metrical levels is scoring. This is
// the argument the estimator is actually having.
void levelTrace (const Dump& d)
{
    constexpr double fps = 50.0;
    vp::TempoEstimator te;
    te.prepare (fps);
    std::printf ("# %s  true %.1f BPM\n", d.name.c_str(), static_cast<double> (d.bpm));
    std::printf ("%-6s %-8s %-4s | %-28s | %-28s | %-28s\n", "t", "reports", "set",
                 "half", "true", "double");
    for (size_t i = 0; i < d.pBeat.size(); ++i)
    {
        te.push (std::max (d.pBeat[i], d.pDown[i]));
        if ((i % 25) != 0)
            continue;
        const auto lo = te.scoreFor (d.bpm * 0.5f);
        const auto mid = te.scoreFor (d.bpm);
        const auto hi = te.scoreFor (d.bpm * 2.0f);
        auto show = [] (const vp::TempoEstimator::CandidateScore& c)
        {
            std::printf ("s%.3f c%.2f self%.2f dbl%.2f%s ",
                         static_cast<double> (c.score), static_cast<double> (c.comb),
                         static_cast<double> (c.halfAtSelf), static_cast<double> (c.halfAtDouble),
                         c.evaluable ? " " : "?");
        };
        std::printf ("%-6.1f %-8.2f %-4d | ", static_cast<double> (i) / fps,
                     static_cast<double> (te.bpm()), te.levelSettled() ? 1 : 0);
        show (lo); std::printf ("| "); show (mid); std::printf ("| "); show (hi);
        std::printf ("\n");
    }
}

int main (int argc, char** argv)
{
    bool trace = false;
    bool levels = false;
    std::vector<const char*> files;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--trace") == 0) trace = true;
        else if (std::strcmp (argv[i], "--levels") == 0) levels = true;
        else files.push_back (argv[i]);
    }
    if (files.empty())
    {
        std::fprintf (stderr, "usage: VPReplay [--trace] dump.txt...\n");
        return 2;
    }

    std::printf ("%-18s %-6s %-7s %-7s %-8s %-7s %-6s %-6s %-7s %-6s\n",
                 "track", "bpm", "t_val", "t_2%", "bpm_end", "span", "wobl", "jumps", "reanch", "%FIX");

    int n = 0, bad = 0, unstable = 0, slow = 0;
    double spanAcc = 0.0, t2Acc = 0.0;
    for (const char* p : files)
    {
        Dump d;
        if (! loadDump (p, d))
        {
            std::fprintf (stderr, "skip %s\n", p);
            continue;
        }
        if (levels)
        {
            levelTrace (d);
            continue;
        }
        const Stats s = replay (d, trace);
        const bool octaveBad = s.bpmEnd > 1.0f
                               && std::fabs (std::log2 (s.bpmEnd / d.bpm)) > 0.20f;
        const bool isUnstable = s.span > 1.0f;
        const bool isSlow = s.t2 < 0.0 || s.t2 > 12.0;
        ++n;
        bad += octaveBad;
        unstable += isUnstable;
        slow += isSlow;
        spanAcc += s.span;
        if (s.t2 >= 0.0) t2Acc += s.t2;

        std::printf ("%-18s %-6.0f %-7.1f %-7.1f %-8.2f %-7.2f %-6.2f %-6d %-7d %-6.0f %s%s%s\n",
                     d.name.c_str(), static_cast<double> (d.bpm), s.tValid, s.t2,
                     static_cast<double> (s.bpmEnd), static_cast<double> (s.span),
                     static_cast<double> (s.wobble), s.jumps, s.reanchors,
                     s.fixedFraction * 100.0,
                     octaveBad ? "OCT " : "", isUnstable ? "WOBBLE " : "", isSlow ? "SLOW" : "");
    }

    std::printf ("\n=== %d tracks ===  octave %d   unstable %d   slow %d   mean span %.2f   mean t_2%% %.1f\n",
                 n, bad, unstable, slow, spanAcc / std::max (1, n), t2Acc / std::max (1, n));
    return 0;
}
