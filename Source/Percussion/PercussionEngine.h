#pragma once

#include "Core/DeterministicRng.h"
#include "Percussion/GrooveEngine.h"
#include "Tracking/TempoFollower.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <vector>

namespace vp
{

class PercussionEngine
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setHumanization (float amount) noexcept;
    void setSwing (float amount) noexcept;
    void setIntensity (float amount) noexcept;
    void setVolume (float v) noexcept { volume = clamp01 (v); }
    void setReverbAmount (float amount) noexcept;
    void setEnabled (bool on) noexcept { enabled = on; }
    void setCongasEnabled (bool on) noexcept { groove.setCongasEnabled (on); }
    void setGroove (float bpm, int pulsesPerBeat) noexcept;
    void setShakerSubdivision (Subdivision s) noexcept { groove.setShakerSubdivision (s); }
    void setGrooveStyle (GrooveStyle s) noexcept { groove.setStyle (s); }
    void setSeed (std::uint32_t seed) noexcept { rng.reset (seed); groove.prepare (seed ^ 0x5bf03635u); }
    void clearVoices() noexcept;
    void silence() noexcept;

    int  render (float* left, float* right, int numSamples, const ClockTick& tick, bool audible) noexcept;

    /** How many articulations are sounding from a recording rather than from
        the synthesis fallback. Worth asserting on: a missing or unreadable
        asset would otherwise degrade silently back to the synthetic bank. */
    int  recordedStrokeCount() const noexcept;

    int  hitsFired() const noexcept { return totalHits; }
    int  activeVoices() const noexcept { return lastActive; }

    /** Times a stroke had to take a slot from a voice that was still sounding,
        because none was idle. A same-stroke retrigger does not count: that one
        releases its predecessor over a ramp. Non-zero means the pool is too
        small for the grid being played, so the tests assert on it rather than
        leaving it to be found by ear. */
    int  hardSteals() const noexcept { return hardStealCount; }

    /** Voices currently inside their release ramp, i.e. taken over by a newer
        stroke of the same kind and on their way out rather than switched off.
        Zero throughout a dense run would mean the ramp is not happening. */
    int  releasingVoices() const noexcept { return lastReleasing; }

private:
    // A stroke is not one sound played louder or softer. A conga slapped hard
    // is brighter and shorter than one slapped softly, not the same recording
    // with more gain on it, so each articulation is synthesised at several
    // dynamic layers - and each layer a few times over, because a percussion
    // part where every stroke is bit-identical reads as a machine no matter how
    // good the timing is.
    static constexpr int kLayers = 3;
    static constexpr int kRoundRobin = 3;
    static constexpr int kStrokes = static_cast<int> (Stroke::count);
    static constexpr int kVoices = 16;

    struct Sample
    {
        std::vector<float> left, right;
    };

    struct Voice
    {
        int   pos = 0;
        int   length = 0;
        const Sample* sample = nullptr;
        Stroke stroke = Stroke::shakerDown;
        float gainL = 0.0f;
        float gainR = 0.0f;
        bool  active = false;
        // A voice being taken over is faded out over a few milliseconds rather
        // than switched off. Cutting a sounding grain at whatever sample it had
        // reached is a step in the output, and a step is a click.
        float fade = 1.0f;
        float fadeStep = 0.0f;
        std::uint32_t age = 0;
    };

    bool   loadRecordedStroke (Stroke stroke, std::vector<float>& mono) noexcept;
    void   buildBank() noexcept;
    void   layerFromRecording (Sample& dest, const std::vector<float>& src,
                               Stroke stroke, int layer, std::uint32_t seed) noexcept;
    Voice& allocateVoice() noexcept;
    void   trigger (Stroke stroke, float velocity, int sampleOffset) noexcept;
    void   releaseStroke (Stroke stroke) noexcept;
    void   synthesizeShaker (Sample& s, Stroke stroke, int layer, std::uint32_t seed) noexcept;
    void   synthesizeDrum (Sample& s, Stroke stroke, int layer, std::uint32_t seed) noexcept;
    void   applyReverbParams() noexcept;
    const  Sample& pick (Stroke stroke, float velocity, float& gain) noexcept;

    Sample bank[kStrokes][kLayers][kRoundRobin];
    bool   recorded[kStrokes] {};   // which strokes came from a recording
    int    rrCursor[kStrokes] {};
    Voice  voices[kVoices] {};
    GrooveEngine groove;
    DeterministicRng rng { 0x51A4E1u };
    juce::Reverb reverb;
    double sampleRate = 48000.0;
    float humanization = 0.35f;
    float volume = 0.8f;
    float reverbAmount = 0.30f;
    float grooveBpm = 120.0f;
    int groovePulses = 4;
    bool enabled = true;
    int totalHits = 0;
    int hardStealCount = 0;
    int samplesSinceHit = 1000000;
    int lastActive = 0;
    int lastReleasing = 0;
    int barCounter = 0;
    int lastBarBeat = -1;
    std::uint32_t voiceClock = 0;
};

} // namespace vp
