// Dumps the raw BeatNet activation curve for one rendered track, so the tempo
// logic can be designed against the signal the network actually produces rather
// than against a guess at it. Writes "frame pBeat pDownbeat" to stdout.
#include "AI/BeatModelConfig.h"
#include "AI/LogSpectFeatures.h"
#include "AI/ModelLocator.h"
#include "AI/OnnxBeatModel.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "probe_song_render.h"

int main (int argc, char** argv)
{
    const double sr = 48000.0;
    vp::probe::SongOptions o;
    if (argc >= 2) o.bpm = static_cast<float> (std::atof (argv[1]));
    if (argc >= 3)
    {
        const std::string st = argv[2];
        o.syncopated = st == "syncopated" || st == "sync+pad";
        o.sustained = st == "pad" || st == "sync+pad" || st == "half-time";
        o.halfTimeFeel = st == "half-time";
    }

    // Same length, same seed and same room as the full engine probe, so the
    // two are measuring one signal rather than two similar ones.
    const unsigned seed = static_cast<unsigned> (o.bpm) * 7u + 13u;
    const int n = static_cast<int> (sr * 60.0);
    std::vector<float> song (static_cast<size_t> (n), 0.0f);
    vp::probe::renderSong (song, o, sr, seed);
    vp::probe::speakerRoomMic (song, sr, seed, 0.55f);

    vp::OnnxBeatModel model;
    if (! vp::loadDefaultBeatModel (model))
    {
        std::fprintf (stderr, "no model\n");
        return 1;
    }
    if (! model.prepare (vp::LogSpectFeatures::kDim))
        return 1;
    model.reset();

    vp::LinearResampler rs;
    rs.prepare (sr, vp::kBeatModelSampleRate);
    vp::LogSpectFeatures feats;
    feats.prepare (vp::kBeatModelSampleRate, vp::kBeatModelHop);

    std::vector<float> resampled (8192, 0.0f);
    float frame[vp::LogSpectFeatures::kDim];
    float act[3] {};
    int frameIdx = 0;

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
