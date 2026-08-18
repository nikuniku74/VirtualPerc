#pragma once

#include <atomic>
#include <cstdint>

namespace vp
{

enum class TrackingState : int
{
    idle = 0,
    listening,
    locking,
    following,
    lowConfidence,
    recovering,
    stopped
};

enum class FollowStrength : int
{
    low = 0,
    medium,
    high
};

enum class Subdivision : int
{
    autoDetect = 0,
    quarter,
    eighth,
    sixteenth
};

enum class FollowSource : int
{
    kitMic = 0,
    speaker = 1
};

enum class InputMode : int
{
    acoustic = 0,
    midi,
    hybrid
};

inline const char* toString (TrackingState s) noexcept
{
    switch (s)
    {
        case TrackingState::idle:          return "IDLE";
        case TrackingState::listening:     return "LISTENING";
        case TrackingState::locking:       return "LOCKING";
        case TrackingState::following:     return "FOLLOWING";
        case TrackingState::lowConfidence: return "LOW CONF";
        case TrackingState::recovering:    return "RECOVERING";
        case TrackingState::stopped:       return "STOPPED";
    }
    return "?";
}

enum class FollowBar : int
{
    ready = 0,
    listening,
    calibrating,
    followingListen,
    following,
    tapAlign,
    weakFollow,
    recalin,
    waitBeat,
    paused
};

inline const char* toBarString (FollowBar b) noexcept
{
    switch (b)
    {
        case FollowBar::ready:           return "PRONTO";
        case FollowBar::listening:       return "ASCOLTANDO";
        case FollowBar::calibrating:     return "CALIBRANDO";
        case FollowBar::followingListen: return "SEGUENDO E ASCOLTO";
        case FollowBar::following:       return "SEGUENDO";
        case FollowBar::tapAlign:        return "ALLINEO TAP";
        case FollowBar::weakFollow:      return "TEMPO INCERTO";
        case FollowBar::recalin:         return "RICALIBRO";
        case FollowBar::waitBeat:        return "ATTENDO BATTUTA";
        case FollowBar::paused:          return "IN ASCOLTO - SHAKER OFF";
    }
    return "?";
}

struct EngineSnapshot
{
    TrackingState state        = TrackingState::idle;
    Subdivision   subdivision  = Subdivision::autoDetect;
    FollowStrength follow      = FollowStrength::medium;
    FollowSource  source       = FollowSource::kitMic;
    float bpm                  = 0.0f;
    float targetBpm            = 0.0f;
    float confidence           = 0.0f;
    float beatPhase            = 0.0f;
    float barPhase             = 0.0f;
    float latencyMs            = 0.0f;
    float inputPeak            = 0.0f;
    float callbackMs           = 0.0f;
    float humanization         = 0.00f;
    float reverbAmount         = 0.30f;
    bool  shakerEnabled        = true;
    bool  percussionAudible    = false;
    bool  tapLocked            = false;
    FollowBar followBar        = FollowBar::ready;
    int   bufferSize           = 0;
    int   shakerVoices         = 0;
    double sampleRate          = 0.0;
    int   beatsLocked          = 0;
    bool  aiOnnx               = false;
    bool  hypValid             = false;
    float neuralBpm            = 0.0f;
    float pBeat                = 0.0f;
    float analysisPeak         = 0.0f;
    /** Measured analysis-plus-output delay the clock runs ahead by, so that what
        is played lands on the pulse the listener hears. */
    float leadMs               = 0.0f;
    /** Whether the analysis is treating the tempo as a fixed one to hold or a
        live one to follow. */
    int   tempoRegime          = 0;
};

struct EngineSettings
{
    std::atomic<float> masterVolume    { 0.90f };
    std::atomic<float> percussionVolume{ 1.00f };
    std::atomic<float> reverbAmount    { 0.30f };
    std::atomic<float> humanization    { 0.00f };
    std::atomic<int>   followStrength  { static_cast<int> (FollowStrength::high) };
    std::atomic<int>   subdivision     { static_cast<int> (Subdivision::eighth) };
    std::atomic<bool>  shakerEnabled   { true };
    std::atomic<int>   analysisChannel { -1 }; // -1 = mix all
    std::atomic<int>   followSource    { static_cast<int> (FollowSource::kitMic) };
};

inline float wrap01 (float x) noexcept
{
    x -= static_cast<float> (static_cast<int> (x));
    if (x < 0.0f)
        x += 1.0f;
    return x;
}

inline float wrapCentered (float x) noexcept
{
    x = wrap01 (x + 0.5f) - 0.5f;
    return x;
}

inline float clamp01 (float x) noexcept
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

inline const char* followLabel (FollowStrength s) noexcept
{
    switch (s)
    {
        case FollowStrength::low:    return "LOW";
        case FollowStrength::medium: return "MEDIUM";
        case FollowStrength::high:   return "HIGH";
    }
    return "?";
}

} // namespace vp
