// Writes Assets/Demo/demo-track.wav: a short synthetic band (drums, bass, pad)
// at a steady 112 BPM, the song the app plays when PLAY is pressed with nothing
// loaded. It exists so App Review - which has no band and no mixer - can see the
// part follow a recording. Rendered by the same code the benches use
// (probe_song_render.h), so it is ours and carries no licence.
//
//   cmake --build build-host --target VPDemoTrack
//   ./build-host/VPDemoTrack_artefacts/Release/VPDemoTrack Assets/Demo/demo-track.wav
#include "probe_song_render.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdio>

using namespace vp::probe;

int main (int argc, char** argv)
{
    const double sr = 44100.0;
    const juce::File out = juce::File::getCurrentWorkingDirectory()
                               .getChildFile (argc > 1 ? argv[1] : "demo-track.wav");

    SongOptions opt;
    opt.genre = Genre::rock;
    opt.bpm = 112.0f;
    opt.breakdown = false;
    opt.fills = true;
    opt.driftBpm = 1.5f;     // a band, not a sequencer
    opt.jitterMs = 5.0f;

    const int bars = 16;
    const size_t n = static_cast<size_t> (bars * 4 * 60.0 / opt.bpm * sr);
    std::vector<float> mix (n, 0.0f);
    renderSong (mix, opt, sr, 20261016u);

    float peak = 0.0f;
    for (const float v : mix)
        peak = std::max (peak, std::abs (v));
    const float gain = peak > 0.0f ? 0.708f / peak : 1.0f;   // -3 dBFS
    for (float& v : mix)
        v *= gain;
    // A short fade so the file does not end on a click.
    const size_t fade = static_cast<size_t> (0.25 * sr);
    for (size_t i = 0; i < fade; ++i)
        mix[n - 1 - i] *= static_cast<float> (i) / static_cast<float> (fade);

    juce::WavAudioFormat wav;
    out.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (out.createOutputStream());
    if (stream == nullptr)
        return 1;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sr, 1, 16, {}, 0));
    if (writer == nullptr)
        return 1;
    stream.release();
    const float* ch[1] = { mix.data() };
    juce::AudioBuffer<float> buf (const_cast<float**> (ch), 1, static_cast<int> (n));
    writer->writeFromAudioSampleBuffer (buf, 0, static_cast<int> (n));
    writer.reset();
    std::printf ("%s: %zu samples, %.1f s, peak was %.3f\n", out.getFullPathName().toRawUTF8(), n,
                static_cast<double> (n) / sr, static_cast<double> (peak));
    return 0;
}
