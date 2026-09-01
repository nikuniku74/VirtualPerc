#include "Loops/WavFile.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace vp
{

namespace
{
    uint32_t rd32 (const unsigned char* p) noexcept
    {
        return static_cast<uint32_t> (p[0])
             | (static_cast<uint32_t> (p[1]) << 8)
             | (static_cast<uint32_t> (p[2]) << 16)
             | (static_cast<uint32_t> (p[3]) << 24);
    }

    uint16_t rd16 (const unsigned char* p) noexcept
    {
        return static_cast<uint16_t> (static_cast<uint32_t> (p[0])
                                      | (static_cast<uint32_t> (p[1]) << 8));
    }

    bool tagIs (const unsigned char* p, const char* tag) noexcept
    {
        return std::memcmp (p, tag, 4) == 0;
    }
} // namespace

bool decodeWav (const unsigned char* data, size_t size, WavAudio& out, std::string& error)
{
    out = WavAudio{};
    error.clear();

    if (data == nullptr || size < 44)
        return (error = "not a WAV: too short"), false;
    if (! tagIs (data, "RIFF") || ! tagIs (data + 8, "WAVE"))
        return (error = "not a RIFF/WAVE file"), false;

    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t sampleRate = 0;
    const unsigned char* audio = nullptr;
    size_t audioBytes = 0;

    size_t pos = 12;
    while (pos + 8 <= size)
    {
        const unsigned char* chunk = data + pos;
        const uint32_t chunkSize = rd32 (chunk + 4);
        const size_t body = pos + 8;
        if (body + chunkSize > size)
        {
            // A truncated final chunk is a truncated file. Taking what is there
            // would hand the player a loop that is a fraction short, and a loop
            // that is a fraction short is a click once a bar.
            if (! tagIs (chunk, "data"))
                break;
            return (error = "the data chunk is truncated"), false;
        }

        if (tagIs (chunk, "fmt ") && chunkSize >= 16)
        {
            format = rd16 (chunk + 8);
            channels = rd16 (chunk + 10);
            sampleRate = rd32 (chunk + 12);
            bits = rd16 (chunk + 22);
            if (format == 0xFFFE && chunkSize >= 40)
                format = rd16 (chunk + 32); // WAVE_FORMAT_EXTENSIBLE: real tag in the GUID
        }
        else if (tagIs (chunk, "data"))
        {
            audio = chunk + 8;
            audioBytes = chunkSize;
        }

        pos = body + chunkSize + (chunkSize & 1u); // chunks are word aligned
    }

    if (format == 0 || channels == 0)
        return (error = "no fmt chunk"), false;
    if (audio == nullptr)
        return (error = "no data chunk"), false;
    if (format != 1 && format != 3)
        return (error = "only PCM and IEEE-float WAV are supported"), false;
    if (channels != 1 && channels != 2)
        return (error = "only mono and stereo are supported"), false;
    if (! (bits == 16 || bits == 24 || bits == 32))
        return (error = "only 16, 24 and 32 bit samples are supported"), false;
    if (format == 3 && bits != 32)
        return (error = "float WAV must be 32 bit"), false;

    const int bytesPerSample = bits / 8;
    const int frameBytes = bytesPerSample * channels;
    const int frames = static_cast<int> (audioBytes / static_cast<size_t> (frameBytes));
    if (frames <= 0)
        return (error = "the data chunk holds no frames"), false;

    out.frames = frames;
    out.channels = channels;
    out.sampleRate = static_cast<double> (sampleRate);
    out.left.assign (static_cast<size_t> (frames), 0.0f);
    out.right.assign (static_cast<size_t> (frames), 0.0f);

    for (int f = 0; f < frames; ++f)
    {
        for (int c = 0; c < channels; ++c)
        {
            const unsigned char* s = audio + static_cast<size_t> (f) * static_cast<size_t> (frameBytes)
                                     + static_cast<size_t> (c) * static_cast<size_t> (bytesPerSample);
            float v = 0.0f;
            if (format == 3)
            {
                uint32_t bitsAsInt = rd32 (s);
                float asFloat = 0.0f;
                std::memcpy (&asFloat, &bitsAsInt, sizeof (asFloat));
                v = asFloat;
            }
            else if (bits == 16)
            {
                v = static_cast<float> (static_cast<int16_t> (rd16 (s))) / 32768.0f;
            }
            else if (bits == 24)
            {
                int32_t i = (static_cast<int32_t> (s[0]) << 8)
                          | (static_cast<int32_t> (s[1]) << 16)
                          | (static_cast<int32_t> (s[2]) << 24);
                i >>= 8; // arithmetic shift carries the sign down from bit 31
                v = static_cast<float> (i) / 8388608.0f;
            }
            else
            {
                v = static_cast<float> (static_cast<int32_t> (rd32 (s))) / 2147483648.0f;
            }

            if (c == 0)
                out.left[static_cast<size_t> (f)] = v;
            else
                out.right[static_cast<size_t> (f)] = v;
        }
    }

    if (channels == 1)
        out.right = out.left;

    return true;
}

bool loadWavFile (const std::string& path, WavAudio& out, std::string& error)
{
    out = WavAudio{};
    std::FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr)
        return (error = "cannot open " + path), false;

    std::fseek (f, 0, SEEK_END);
    const long size = std::ftell (f);
    std::fseek (f, 0, SEEK_SET);
    if (size <= 0)
    {
        std::fclose (f);
        return (error = path + " is empty"), false;
    }

    std::vector<unsigned char> bytes (static_cast<size_t> (size));
    const size_t got = std::fread (bytes.data(), 1, bytes.size(), f);
    std::fclose (f);
    if (got != bytes.size())
        return (error = "short read on " + path), false;

    if (! decodeWav (bytes.data(), bytes.size(), out, error))
    {
        error = path + ": " + error;
        return false;
    }
    return true;
}

} // namespace vp
