#pragma once

#include "Core/Types.h"

namespace vp
{

struct ClockTick
{
    /** The tempo the clock is actually running at over this block. */
    float tempoBpm = 0.0f;
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
    void resetClock() noexcept;

    /** Where the analysis says the song's pulse is, and how long the loop
        should take to believe it, in seconds.

        Seconds rather than a per-block blend. This is called once per audio
        callback, so a fixed blend made the whole time constant a function of
        the buffer size: measured on the same music, the grid rate wobbled by
        4.2 BPM rms on a 64-frame buffer against 2.2 on 1024. It also has to be
        longer than the interval at which the analysis refreshes - about a sixth
        of a second - or nothing is averaged at all and the loop chases the
        decoder's own uncertainty as if it were the band moving. */
    void setGridPhase (float targetPhase, float tauSeconds) noexcept;
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
    float tempo = 120.0f;
    float target = 120.0f;
    float conf = 0.0f;
    int beatInBar = 0;
    int totalBeats = 0;
    int pulsesPerBeat = 4; // 4 = 16th notes per quarter
    float phaseErrEma = 0.0f;
    /** The raw error the last callback reported, and its time constant. Held
        rather than blended on the spot so that `advance` - the only place that
        knows how much time a block is - does the smoothing. */
    float phaseTarget = 0.0f;
    float phaseTargetTau = 0.5f;
    bool  havePhaseTarget = false;
    float prevPhaseErr = 0.0f;
    float lastObservedPhaseErr = 0.0f;
    float tempoTrim = 0.0f;
    float phaseCorrectionSinceObservation = 0.0f;
    int samplesSinceObservation = 0;
    // Time since a pulse was last emitted, so that a re-anchor can tell whether
    // the pulse it is about to place would be heard as a stroke of its own or as
    // a flam on the one before it.
    int samplesSincePulse = 0;
    bool locked = false;
    // Set by a snap: the next block re-anchors the pulse grid to the new phase
    // and emits the pulse sitting at it, instead of waiting for the next one.
    bool reanchor = false;
    bool havePhaseObservation = false;
    bool tempoTrimEnabled = false;
    FollowStrength follow = FollowStrength::medium;
};

} // namespace vp
