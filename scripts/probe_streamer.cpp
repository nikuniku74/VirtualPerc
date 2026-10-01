// TrackStreamer check (docs/TODO.md item 80): the lock-free track player must
// give the audio thread exactly what a direct read + the same interpolator
// gives, with no underruns once primed, and seek cleanly.
//
//   VP_STYLE_SRC=scripts/probe_streamer.cpp VP_PROBE_DIR=scripts \
//     cmake -B build-alt && cmake --build build-alt --target VPStyle
//   build-alt/VPStyle_artefacts/Release/VPStyle some.wav
#include "Audio/TrackStreamer.h"

#include <cstdio>
#include <thread>

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: VPStyle file.wav\n"); return 2; }
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (argv[1])));
    std::unique_ptr<juce::AudioFormatReader> ref (fm.createReaderFor (juce::File (argv[1])));
    if (r == nullptr || ref == nullptr) { std::printf ("cannot read\n"); return 2; }

    juce::TimeSliceThread t ("read");
    t.startThread();
    vp::TrackStreamer s (t);
    const double dev = 48000.0;
    const int block = 256;
    s.prepareToPlay (8192, dev);
    s.setSource (r.get());
    std::this_thread::sleep_for (std::chrono::milliseconds (300));   // prime
    s.start();

    const double ratio = r->sampleRate / dev;
    juce::LagrangeInterpolator li;
    juce::AudioBuffer<float> out (2, block), in (2, 16384), want (1, block);
    juce::int64 refPos = 0;
    double maxErr = 0.0;
    int blocks = 0, silent = 0;
    const int total = static_cast<int> (20.0 * dev / block);   // 20 s of callbacks
    for (int b = 0; b < total; ++b)
    {
        juce::AudioSourceChannelInfo info (&out, 0, block);
        s.getNextAudioBlock (info);
        // reference: same interpolator on a direct read
        const int need = static_cast<int> (std::ceil (block * ratio)) + 8;
        ref->read (&in, 0, need, refPos, true, true);
        const int used = li.process (ratio, in.getReadPointer (0), want.getWritePointer (0), block);
        refPos += used;
        for (int i = 0; i < block; ++i)
            maxErr = std::max (maxErr, (double) std::abs (out.getSample (0, i) - want.getSample (0, i)));
        if (out.getMagnitude (0, 0, block) == 0.0f) ++silent;
        ++blocks;
        std::this_thread::sleep_for (std::chrono::microseconds (900));   // ~5x real time
    }
    const double pos = s.getCurrentPosition();
    std::printf ("blocks %d  silent %d  underruns %d  max |diff| vs direct %.2e  position %.3f s (ref %.3f s)\n",
                 blocks, silent, s.underrunCount(), maxErr, pos, refPos / r->sampleRate);

    // seek
    s.setPosition (30.0);
    std::this_thread::sleep_for (std::chrono::milliseconds (300));
    juce::AudioSourceChannelInfo info (&out, 0, block);
    s.getNextAudioBlock (info);
    std::printf ("after seek to 30 s: position %.3f s, block magnitude %.4f\n",
                 s.getCurrentPosition(), out.getMagnitude (0, 0, block));
    s.setSource (nullptr);
    t.stopThread (1000);
    const bool ok = maxErr < 1.0e-6 && s.underrunCount() == 0 && std::abs (pos - refPos / r->sampleRate) < 1.0e-6;
    std::printf ("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
