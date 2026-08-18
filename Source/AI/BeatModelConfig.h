#pragma once

namespace vp
{
    inline constexpr double kBeatModelSampleRate = 22050.0;
    inline constexpr int    kBeatModelHop        = 441;   // 20 ms → 50 fps
    inline constexpr int    kBeatModelFrame      = 1411;  // 64 ms analysis window
    inline constexpr int    kBeatModelWindow     = 1;     // LSTM: one feature frame
    inline constexpr int    kBeatBandsPerOctave  = 24;
    inline constexpr float  kBeatFminHz          = 30.0f;
    inline constexpr float  kBeatFmaxHz          = 17000.0f; // BeatNet/madmom config; clipped to Nyquist
}
