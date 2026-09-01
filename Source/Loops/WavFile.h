#pragma once

#include <string>
#include <vector>

namespace vp
{

/** A decoded WAV file, de-interleaved.

    Deliberately not JUCE: the loop modules are host-testable C++ with no
    framework under them, the same way the tracking and decoder layers are, and
    a bank has to be loadable inside a console test that never opens an audio
    device. It reads exactly what the format spec for the loop library allows -
    RIFF/WAVE, PCM 16/24/32-bit or IEEE float, mono or stereo - and refuses
    anything else by name rather than by producing noise. */
struct WavAudio
{
    std::vector<float> left;
    std::vector<float> right;   // a copy of `left` for a mono file
    int    frames = 0;
    int    channels = 0;
    double sampleRate = 0.0;
};

/** Decode a WAV file already in memory. `error` says why on failure. */
bool decodeWav (const unsigned char* data, size_t size, WavAudio& out, std::string& error);

/** Read and decode a file from disk. Never on the audio thread. */
bool loadWavFile (const std::string& path, WavAudio& out, std::string& error);

} // namespace vp
