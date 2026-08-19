// Turn the downloaded recordings into the app's stroke assets: find the
// transient, trim the pre-roll, truncate before any second hit, normalise,
// fade out. The pre-roll matters most - 25 ms of silence in front of a sample
// is 25 ms of lateness on every stroke, and the clock is calibrated to 3 ms.
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 5) { std::printf ("usage: in.mp3 out.wav maxSeconds outSampleRate\n"); return 1; }
    const char* in = argv[1];
    const char* out = argv[2];
    const double maxSec = atof (argv[3]);
    const double outSr = atof (argv[4]);

    juce::AudioFormatManager fm; fm.registerBasicFormats();
    auto cwd = juce::File::getCurrentWorkingDirectory();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (cwd.getChildFile (juce::String (in))));
    if (r == nullptr) { std::printf ("cannot read %s\n", in); return 1; }

    const int n = (int) r->lengthInSamples;
    juce::AudioBuffer<float> buf ((int) r->numChannels, n);
    r->read (&buf, 0, n, 0, true, true);
    std::vector<float> m ((size_t) n);
    for (int i = 0; i < n; ++i) {
        float s = 0; for (int c = 0; c < buf.getNumChannels(); ++c) s += buf.getSample (c, i);
        m[(size_t) i] = s / (float) buf.getNumChannels();
    }

    float peak = 0; for (float v : m) peak = std::max (peak, std::fabs (v));
    // The transient: first sample reaching a fifth of the peak, then walk back
    // to where it left the noise floor so the attack is not clipped.
    int onset = 0;
    for (int i = 0; i < n; ++i) if (std::fabs (m[(size_t)i]) > 0.20f * peak) { onset = i; break; }
    while (onset > 0 && std::fabs (m[(size_t)(onset - 1)]) > 0.004f * peak) --onset;
    onset = std::max (0, onset - (int) (0.001 * r->sampleRate));   // 1 ms of air

    int len = std::min (n - onset, (int) (maxSec * r->sampleRate));

    // Stop before a second hit: a rise after the decay has set in.
    const int hop = (int) (0.005 * r->sampleRate);
    float prev = 1.0e9f;
    for (int i = onset + 20 * hop; i + hop < onset + len; i += hop) {
        float pk = 0; for (int j = 0; j < hop; ++j) pk = std::max (pk, std::fabs (m[(size_t)(i+j)]));
        if (pk > 3.0f * prev && pk > 0.08f * peak) { len = i - onset - hop; break; }
        prev = std::max (pk, 1.0e-6f);
    }
    len = std::max (len, (int) (0.03 * r->sampleRate));

    // Resample to the app's rate with linear interpolation - these are short
    // percussive hits, so the artefacts sit far under the transient.
    const double ratio = outSr / r->sampleRate;
    const int outLen = (int) (len * ratio);
    std::vector<float> o ((size_t) outLen);
    for (int i = 0; i < outLen; ++i) {
        const double src = i / ratio;
        const int i0 = (int) src; const double f = src - i0;
        const int a = onset + i0, b = std::min (onset + i0 + 1, onset + len - 1);
        o[(size_t) i] = (float) ((1.0 - f) * m[(size_t) a] + f * m[(size_t) b]);
    }

    float op = 1.0e-6f; for (float v : o) op = std::max (op, std::fabs (v));
    const float g = 0.95f / op;
    for (float& v : o) v *= g;

    // Fade the tail to zero: a truncated decay is a click on every stroke.
    const int fade = std::min (outLen, (int) (0.010 * outSr));
    for (int i = 0; i < fade; ++i)
        o[(size_t)(outLen - fade + i)] *= 0.5f * (1.0f + std::cos (3.14159265f * i / (float) fade));

    juce::File f = cwd.getChildFile (juce::String (out));
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.release(), outSr, 1, 16, {}, 0));
    if (w == nullptr) { std::printf ("no writer for %s\n", out); return 1; }
    const float* ch[1] = { o.data() };
    w->writeFromFloatArrays (ch, 1, outLen);
    w.reset();
    std::printf ("%-14s -> %-34s  trimmed %5.1f ms of pre-roll, %6.3f s kept\n",
                 in, out, 1000.0 * onset / r->sampleRate, outLen / outSr);
    return 0;
}
