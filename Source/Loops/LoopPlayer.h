#pragma once

#include "Loops/LoopBank.h"
#include "Stretch/LoopStretcher.h"
#include "Tracking/TempoFollower.h"

#include <vector>

namespace vp
{

/**
    Plays a recorded performance on the app's own clock.

    Not a loop player with automatic BPM. The clock is `TempoFollower`'s and
    nothing here can move it: the loop is pulled onto the grid, never the other
    way round. Concretely, every block:

      - the musical position is taken from the `ClockTick` - integrated between
        quarters and *placed* exactly on every quarter the clock emits, so the
        phase can never walk away from the song;
      - that position is mapped through the recording's own beat markers to a
        position in the file, which is where the audio for this block must come
        from;
      - the stretcher is asked for exactly one block of output, and the source
        read is nudged - by rate, never by a jump - until it sits where the map
        says it should. The nudge is bounded, so no stroke is ever doubled or
        skipped, for the same reason `TempoFollower` corrects phase by rate.

    Three things this owns that a naive loop player does not:

      - **Latency.** The stretcher hands back audio later than the input it was
        given. The source read therefore runs *ahead* by the amount
        `LoopStretcher` measured for this backend, or every stroke lands late by
        exactly that much.
      - **Junctions.** A loop that is cut properly is a continuous signal when
        its end is wrapped onto its start, so the source read simply wraps and
        the stretcher never sees a discontinuity. There is nothing to fade,
        which is why there is nothing to click.
      - **Changes.** Going to a different recording - other part, other take,
        other swing - is a real crossfade between two stretchers, committed on a
        quarter and preferably on a bar line, with the incoming one primed
        beforehand so it is already producing audio when the fade starts.

    Everything it needs is sized in `prepare`. `process` allocates nothing.
*/
class LoopPlayer
{
public:
    /** Sizes every buffer. Allocates; not for the audio thread. */
    void prepare (double sampleRate, int maxBlock) noexcept;

    /** The bank to play out of. Must outlive the player, and must only be
        changed while the audio callback is not running - the audio thread reads
        samples straight out of it. Passing nullptr is how the player is
        switched off from the outside. */
    void setBank (const LoopBank* b) noexcept;
    const LoopBank* bank() const noexcept { return loops; }

    void reset() noexcept;

    /** Arm. Nothing sounds until the next quarter the clock emits: a
        percussionist comes in on a beat, and so does this. */
    void start() noexcept;
    /** Immediate. Not deferred to a bar, not faded over a phrase - the same
        STOP the rest of the engine has. */
    void stop() noexcept;

    /** Ask for a part. Held until the next quarter, or the next bar line when
        one is close enough to wait for, and then crossfaded in. Safe to call
        every block with the same query: an ask that resolves to what is already
        playing costs nothing. */
    void request (const LoopQuery& q) noexcept;

    /** How long a change takes, and how hard the source read may be pulled.
        `tau` is the time constant the read position closes a phase error over -
        long in the fixed regime, where the tempo is a click track and the only
        thing that needs correcting is drift; shorter when the band is moving.
        `maxPull` bounds the rate deviation, as a fraction. */
    void setCorrection (float tauSeconds, float maxPull) noexcept;
    void setCrossfadeMs (float ms) noexcept;

    /** Renders into `outL`/`outR`, replacing what is there. Real-time safe. */
    void process (float* outL, float* outR, int numSamples, const ClockTick& tick) noexcept;

    bool isPrepared() const noexcept { return prepared; }
    /** Sounding, or fading into or out of sounding. */
    bool isPlaying() const noexcept;
    /** Armed and waiting for the quarter it will come in on. */
    bool isArmed() const noexcept { return armed; }
    /** The entry currently carrying the part, or -1. */
    int  currentIndex() const noexcept;
    float currentRatio() const noexcept { return lastRatio; }
    /** How far the audio is from where the clock says it should be, in
        milliseconds. What the phase test asserts on. */
    float phaseErrorMs() const noexcept { return lastPhaseErrMs; }
    /** Loop passes completed, and changes of recording made. Both are counters
        the tests read: a pass that produced two attacks on one beat, or a change
        that lost one, shows up as one of these disagreeing with the clock. */
    int  passes() const noexcept { return passCount; }
    int  changes() const noexcept { return changeCount; }
    /** Why the last request found nothing, when it found nothing. */
    LoopMissReason lastMiss() const noexcept { return miss; }

private:
    struct Voice
    {
        LoopStretcher stretcher;
        std::vector<float> gatherL, gatherR;
        /** The stretcher's output for this voice. Separate from the gather:
            Signalsmith reads its whole input before it has finished writing its
            output, so the two must not be the same memory. */
        std::vector<float> mixL, mixR;
        std::vector<float> primeL, primeR;
        int    index = -1;
        double cursor = 0.0;      // absolute read position, loop-body frames
        /** The swing warp already built into `cursor`. Kept per voice so the
            warp can be fed forward exactly instead of being left for the
            correction loop to discover: the loop's time constant is longer than
            a beat, so a warp inside the beat comes out about half the size
            asked for if it goes through it. */
        double warp = 0.0;
        float  gain = 0.0f;
        float  gainTarget = 0.0f;
        float  gainStep = 0.0f;
        bool   active = false;
    };

    /** Musical position, in quarters since the part came in, at the end of a
        span; and the source position that corresponds to it for one voice. */
    double sourceForBeat (const Voice& v, double songBeat, bool withSwing) const noexcept;
    double mapBeatToOffset (const LoopBank::Entry& e, double loopBeat, bool withSwing) const noexcept;
    /** How many frames of the recording the swing warp is currently displacing
        the read by, at this musical position. */
    double swingWarp (const Voice& v, double songBeat) const noexcept;
    void   gather (const LoopBank::Entry& e, double from, int count,
                   float* dstL, float* dstR) const noexcept;
    void   renderVoice (Voice& v, float* outL, float* outR, int first, int count,
                        double songBeatAtEnd, float bpm) noexcept;
    void   primeVoice (Voice& v, int index, double songBeat, float bpm) noexcept;
    void   commitPending (double songBeat, float bpm) noexcept;

    const LoopBank* loops = nullptr;
    Voice voices[2];
    int   cur = 0;

    double sampleRate = 48000.0;
    int    maxBlock = 1024;
    bool   prepared = false;

    /** Musical position at the end of the last block, in quarters since the
        player was prepared. Absolute and monotonic: the loop's own position is
        this modulo the recording's length. */
    double songBeats = 0.0;
    /** The same, as the integer quarter the last quarter-pulse declared, so the
        count can be checked against the clock's beat-in-bar rather than
        integrated forever. */
    long long absBeat = 0;
    bool  haveBeat = false;

    bool  armed = false;
    /** A request waiting for its quarter. */
    bool  havePending = false;
    LoopQuery pending {};
    LoopQuery active {};
    int   pendingIndex = -1;
    LoopMissReason miss = LoopMissReason::none;

    /** Quarters a pending change has waited for a bar line. Past a bar's worth
        it stops waiting and takes the next quarter: a listener who moved the
        control is owed the change inside a bar, not whenever the count next
        comes round. */
    int   waitedQuarters = 0;

    float corrTau = 0.35f;
    float corrMaxPull = 0.05f;
    int   xfadeSamples = 1200;

    float lastRatio = 1.0f;
    float lastPhaseErrMs = 0.0f;
    int   passCount = 0;
    int   changeCount = 0;
    long long lastPassIndex = 0;
};

} // namespace vp
