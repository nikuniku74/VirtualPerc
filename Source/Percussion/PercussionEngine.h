#pragma once

#include "Core/DeterministicRng.h"
#include "Tracking/TempoFollower.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace vp
{

class PercussionEngine
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setHumanization (float amount) noexcept { humanization = clamp01 (amount); }
    void setVolume (float v) noexcept { volume = clamp01 (v); }
    void setReverbAmount (float amount) noexcept;
    void setEnabled (bool on) noexcept { enabled = on; }
    void setGroove (float bpm, int pulsesPerBeat) noexcept;
    void setSeed (std::uint32_t seed) noexcept { rng.reset (seed); }
    void clearVoices() noexcept;
    void silence() noexcept;

    int  render (float* left, float* right, int numSamples, const ClockTick& tick, bool audible) noexcept;

    int  hitsFired() const noexcept { return totalHits; }
    int  activeVoices() const noexcept { return lastActive; }

private:
    enum class Kind : int { shaker = 0, tumba, open, slap };

    struct Voice
    {
        int   pos = 0;
        int   length = 0;
        int   take = 0;
        Kind  kind = Kind::shaker;
        float gainL = 0.0f;
        float gainR = 0.0f;
        bool  active = false;
    };

    void triggerShaker (int sampleOffset) noexcept;
    void triggerConga (Kind kind, int sampleOffset, float pan) noexcept;
    void stealKind (Kind kind) noexcept;
    void synthesizeShaker() noexcept;
    void synthesizeCongas() noexcept;
    void applyReverbParams() noexcept;
    const std::vector<float>& sampleL (const Voice& v) const noexcept;
    const std::vector<float>& sampleR (const Voice& v) const noexcept;

    static constexpr int kTakes = 4;
    std::vector<float> shakerL[kTakes];
    std::vector<float> shakerR[kTakes];
    std::vector<float> tumbaL, tumbaR, openL, openR, slapL, slapR;
    Voice voices[12] {};
    DeterministicRng rng { 0x51A4E1u };
    juce::Reverb reverb;
    double sampleRate = 48000.0;
    float humanization = 0.00f;
    float volume = 0.8f;
    float reverbAmount = 0.30f;
    float grooveBpm = 120.0f;
    int groovePulses = 2;
    bool enabled = true;
    int totalHits = 0;
    int samplesSinceHit = 1000000;
    int lastActive = 0;
};

} // namespace vp
