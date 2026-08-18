#pragma once

#include "Core/Types.h"

namespace vp
{

struct ClockTick
{
    bool wrappedBeat = false;
    bool wrappedBar  = false;
    int  pulsesFired = 0;
    int  pulseIndex[8] {};
    int  pulseOffset[8] {};
    int  pulseBeatInBar[8] {};
    int  barPulse[8] {};
    float pulsePhaseError[8] {};
};

class TempoFollower
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setTargetTempo (float bpm, float confidence) noexcept;
    void forceTempo (float bpm) noexcept;
    void setFollowStrength (FollowStrength s) noexcept { follow = s; }
    void setLocked (bool on) noexcept { locked = on; }
    void setTempoTrimEnabled (bool on) noexcept;
    void setLatencyCompensationMs (float ms) noexcept;
    void resetClock() noexcept;

    void setGridPhase (float targetPhase, float amount) noexcept;
    void snapPhase (float targetPhase) noexcept;
    void snapDownbeat (float targetPhase = 0.0f) noexcept;
    void snapBeat (int beatIndex, float targetPhase = 0.0f) noexcept;
    void rotateBarIndex (int delta) noexcept;
    void observeOnsetPhase (float beatPhaseOfOnset, float strength, int gridPulses) noexcept;

    ClockTick advance (int numSamples) noexcept;

    float currentTempo() const noexcept { return tempo; }
    float targetTempo() const noexcept { return target; }
    float tempoTrimBpm() const noexcept { return tempoTrim; }
    float beatPhase() const noexcept { return static_cast<float> (phase); }
    float barPhase() const noexcept { return static_cast<float> ((beatInBar + phase) * 0.25); }
    int   beatInBarIndex() const noexcept { return beatInBar; }
    int   beatsElapsed() const noexcept { return totalBeats; }

    void setPulsesPerBeat (int n) noexcept { pulsesPerBeat = n < 1 ? 1 : n; }
    int  getPulsesPerBeat() const noexcept { return pulsesPerBeat; }

private:
    double sampleRate = 48000.0;
    double phase = 0.0;
    double lastPulsePhase = 0.0;
    float tempo = 120.0f;
    float target = 120.0f;
    float conf = 0.0f;
    int beatInBar = 0;
    int totalBeats = 0;
    int pulsesPerBeat = 4; // 4 = 16th notes per quarter
    int sameSignCount = 0;
    float lastPhaseErr = 0.0f;
    float phaseErrEma = 0.0f;
    float prevPhaseErr = 0.0f;
    float lastObservedPhaseErr = 0.0f;
    float tempoTrim = 0.0f;
    float phaseCorrectionSinceObservation = 0.0f;
    int samplesSinceObservation = 0;
    float latencyMs = 0.0f;
    float frozenLatencyMs = -1.0f;
    bool locked = false;
    bool primed = false;
    bool havePhaseObservation = false;
    bool tempoTrimEnabled = false;
    FollowStrength follow = FollowStrength::medium;
};

} // namespace vp
