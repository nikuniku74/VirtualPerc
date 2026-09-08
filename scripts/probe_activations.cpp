// Dumps the raw BeatNet activation curve for one rendered track, so the tempo
// logic can be designed against the signal the network actually produces rather
// than against a guess at it. Writes "frame pBeat pDownbeat" to stdout.
//
// `--wav file.wav --sweep` instead measures how the *level* of the input moves
// the network, which is the failure the live recording exposed: at about -12 dB
// the acquisition settles on a false high octave for ten seconds. The frontend
// is madmom's log10(1 + magnitude), which is not scale invariant, so the only
// way to tell a frontend bug from that compression is to print, at each gain,
// what went into the tensor and what came out of the model.
#include "AI/BeatModelConfig.h"
#include "AI/LogSpectFeatures.h"
#include "AI/ModelLocator.h"
#include "AI/OnnxBeatModel.h"
#include "Loops/WavFile.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "probe_song_render.h"

namespace
{

float dbfs (float linear)
{
    return linear > 1.0e-9f ? 20.0f * std::log10 (linear) : -180.0f;
}

struct Stats
{
    int frames = 0;
    float magMin = 1.0e9f, magMax = -1.0e9f, magSum = 0.0f;
    float diffMax = 0.0f, diffSum = 0.0f;
    float beatSum = 0.0f, beatMax = 0.0f, downSum = 0.0f;
    int beatOver = 0, downOver = 0;
    float rms = 0.0f, peak = 0.0f;
    int clipped = 0;
};

/** One pass of the offline path at one gain: the model's own copy of the
    signal is scaled, nothing else. Reports the input, the tensor and the
    activations separately, because a level effect can enter at any of the
    three and blaming the wrong one costs a day. */
Stats analyse (const std::vector<float>& song, double sr, float gain,
               vp::OnnxBeatModel& model, int maxSamples)
{
    Stats s;
    const int n = std::min (static_cast<int> (song.size()), maxSamples);

    double acc = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float v = song[static_cast<size_t> (i)] * gain;
        acc += static_cast<double> (v) * v;
        s.peak = std::max (s.peak, std::fabs (v));
        if (std::fabs (v) > 0.999f)
            ++s.clipped;
    }
    s.rms = n > 0 ? static_cast<float> (std::sqrt (acc / n)) : 0.0f;

    model.reset();
    vp::LinearResampler rs;
    rs.prepare (sr, vp::kBeatModelSampleRate);
    vp::LogSpectFeatures feats;
    feats.prepare (vp::kBeatModelSampleRate, vp::kBeatModelHop);

    std::vector<float> scaled (2048, 0.0f);
    std::vector<float> resampled (8192, 0.0f);
    float frame[vp::LogSpectFeatures::kDim];
    float act[3] {};

    const int chunk = 2048;
    for (int pos = 0; pos + chunk <= n; pos += chunk)
    {
        for (int i = 0; i < chunk; ++i)
            scaled[static_cast<size_t> (i)] = song[static_cast<size_t> (pos + i)] * gain;
        const int nr = rs.process (scaled.data(), chunk, resampled.data(),
                                   static_cast<int> (resampled.size()));
        feats.process (resampled.data(), nr);
        while (feats.popFrame (frame))
        {
            for (int b = 0; b < vp::LogSpectFeatures::kBands; ++b)
            {
                s.magMin = std::min (s.magMin, frame[b]);
                s.magMax = std::max (s.magMax, frame[b]);
                s.magSum += frame[b];
                const float d = frame[vp::LogSpectFeatures::kBands + b];
                s.diffMax = std::max (s.diffMax, d);
                s.diffSum += d;
            }
            if (! model.infer (frame, vp::LogSpectFeatures::kDim, act))
                continue;
            s.beatSum += act[0];
            s.beatMax = std::max (s.beatMax, act[0]);
            s.downSum += act[1];
            if (act[0] > 0.5f) ++s.beatOver;
            if (act[1] > 0.5f) ++s.downOver;
            ++s.frames;
        }
    }
    return s;
}

} // namespace

int main (int argc, char** argv)
{
    double sr = 48000.0;
    vp::probe::SongOptions o;
    std::string wavPath;
    bool sweep = false;
    double seconds = 12.0;
    std::vector<float> gainsDb { 0.0f, -6.0f, -12.0f, -18.0f };
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--wav" && i + 1 < argc)        wavPath = argv[++i];
        else if (a == "--sweep")                 sweep = true;
        else if (a == "--secs" && i + 1 < argc)  seconds = std::atof (argv[++i]);
        else if (a == "--gains" && i + 1 < argc)
        {
            gainsDb.clear();
            const std::string list = argv[++i];
            size_t at = 0;
            while (at < list.size())
            {
                const size_t comma = list.find (',', at);
                gainsDb.push_back (static_cast<float> (std::atof (list.substr (at, comma - at).c_str())));
                if (comma == std::string::npos) break;
                at = comma + 1;
            }
        }
        else positional.push_back (a);
    }

    if (! positional.empty()) o.bpm = static_cast<float> (std::atof (positional[0].c_str()));
    if (positional.size() >= 2)
    {
        const std::string& st = positional[1];
        o.syncopated = st == "syncopated" || st == "sync+pad";
        o.sustained = st == "pad" || st == "sync+pad" || st == "half-time";
        o.halfTimeFeel = st == "half-time";
    }

    std::vector<float> song;
    if (! wavPath.empty())
    {
        vp::WavAudio wav;
        std::string why;
        if (! vp::loadWavFile (wavPath, wav, why))
        {
            std::fprintf (stderr, "wav: %s\n", why.c_str());
            return 1;
        }
        sr = wav.sampleRate;
        song.resize (static_cast<size_t> (wav.frames));
        for (int i = 0; i < wav.frames; ++i)
            song[static_cast<size_t> (i)] = 0.5f * (wav.left[static_cast<size_t> (i)]
                                                    + wav.right[static_cast<size_t> (i)]);
    }
    else
    {
        // Same length, same seed and same room as the full engine probe, so the
        // two are measuring one signal rather than two similar ones.
        const unsigned seed = static_cast<unsigned> (o.bpm) * 7u + 13u;
        song.assign (static_cast<size_t> (sr * 60.0), 0.0f);
        vp::probe::renderSong (song, o, sr, seed);
        vp::probe::speakerRoomMic (song, sr, seed, 0.55f);
    }

    vp::OnnxBeatModel model;
    if (! vp::loadDefaultBeatModel (model))
    {
        std::fprintf (stderr, "no model\n");
        return 1;
    }
    if (! model.prepare (vp::LogSpectFeatures::kDim))
        return 1;
    model.reset();

    if (sweep)
    {
        const int maxSamples = static_cast<int> (sr * seconds);
        std::printf ("# source %s  sr %.0f  first %.1f s  onnx %d\n",
                     wavPath.empty() ? "synthetic" : wavPath.c_str(), sr, seconds,
                     model.usesOnnx() ? 1 : 0);
        std::printf ("# gainDb rmsDb peakDb clip magMin magMax magMean diffMean diffMax "
                     "beatMean beatMax beatOver downMean downOver frames\n");
        for (float g : gainsDb)
        {
            const Stats s = analyse (song, sr, std::pow (10.0f, g / 20.0f), model, maxSamples);
            const double cells = static_cast<double> (std::max (1, s.frames))
                                 * vp::LogSpectFeatures::kBands;
            std::printf ("%6.1f %6.1f %6.1f %4d %7.4f %7.4f %7.4f %8.4f %7.4f "
                         "%8.4f %7.4f %8d %8.4f %8d %6d\n",
                         static_cast<double> (g), static_cast<double> (dbfs (s.rms)),
                         static_cast<double> (dbfs (s.peak)), s.clipped,
                         static_cast<double> (s.magMin), static_cast<double> (s.magMax),
                         s.magSum / cells, s.diffSum / cells, static_cast<double> (s.diffMax),
                         s.beatSum / std::max (1, s.frames), static_cast<double> (s.beatMax),
                         s.beatOver, s.downSum / std::max (1, s.frames), s.downOver,
                         s.frames);
        }
        return 0;
    }

    vp::LinearResampler rs;
    rs.prepare (sr, vp::kBeatModelSampleRate);
    vp::LogSpectFeatures feats;
    feats.prepare (vp::kBeatModelSampleRate, vp::kBeatModelHop);

    std::vector<float> resampled (8192, 0.0f);
    float frame[vp::LogSpectFeatures::kDim];
    float act[3] {};
    int frameIdx = 0;

    const int n = static_cast<int> (song.size());
    const int chunk = 2048;
    std::printf ("# bpm %.2f  framesPerBeat %.3f\n", static_cast<double> (o.bpm),
                 60.0 / static_cast<double> (o.bpm) * 50.0);
    for (int pos = 0; pos + chunk <= n; pos += chunk)
    {
        const int nr = rs.process (song.data() + pos, chunk, resampled.data(),
                                   static_cast<int> (resampled.size()));
        feats.process (resampled.data(), nr);
        while (feats.popFrame (frame))
        {
            if (! model.infer (frame, vp::LogSpectFeatures::kDim, act))
                continue;
            std::printf ("%d %.4f %.4f\n", frameIdx, static_cast<double> (act[0]),
                         static_cast<double> (act[1]));
            ++frameIdx;
        }
    }
    return 0;
}
