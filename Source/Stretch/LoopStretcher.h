#pragma once

#include <memory>
#include <vector>

namespace vp
{

/**
    Time stretch with the pitch locked, for the recorded loop player.

    Separate from `TimeStretchEngine`, which stays exactly as it is: that one is
    the prototype the loop-kit idea was proved with and it is still what
    `VirtualPercussionEngine::loadPercussionLoop` drives. This one is the
    shipping path - see TD-15 and TD-16 - and it has two obligations the
    prototype does not:

      - it must be Signalsmith Stretch when the library is vendored
        (`-DVP_USE_SIGNALSMITH=ON`, `third_party/signalsmith-stretch`), because
        a phase-vocoder that keeps transients is the difference between a conga
        that sounds played and one that sounds processed;
      - it must never allocate inside `process`. Everything it needs is sized in
        `prepare`.

    Without the library a WSOLA with a real similarity search stands in, so the
    default build still runs and the tests still mean something. It is not the
    sound the app ships: it is a floor.

    Latency is not hidden. A stretcher hands back audio that lags the input it
    was given, and a loop player that ignores that plays every stroke late by
    exactly that much. `inputLeadFrames` says how far ahead of the musical
    target the source read has to run so the stroke *lands* on the beat, in the
    loop's own frames, which is the only unit the caller can act on.
*/
class LoopStretcher
{
public:
    LoopStretcher();
    ~LoopStretcher();

    LoopStretcher (const LoopStretcher&) = delete;
    LoopStretcher& operator= (const LoopStretcher&) = delete;

    /** Sizes everything. Allocates; never call it from the audio thread.
        `maxBlock` is the largest output block `process` will be asked for and
        `maxRatio` the largest input-per-output ratio it will be asked to run
        at - together they fix the input scratch this object owns. */
    void prepare (double sampleRate, int maxBlock, float maxRatio) noexcept;

    /** Throws away the internal state. Real-time safe. */
    void reset() noexcept;

    bool isPrepared() const noexcept { return prepared; }
    /** True when the vendored Signalsmith library is what is running, false when
        the built-in WSOLA is standing in. Reported rather than assumed: a build
        that quietly fell back to the floor sound is worth a test failing on. */
    static bool isSignalsmith() noexcept;

    /** Input frames this object will consume to produce `outputFrames` of output
        at `ratio`. The caller gathers exactly this many and no more. */
    int inputFramesFor (int outputFrames, double ratio) const noexcept;

    /** How far ahead of the musical target the source read must run, in input
        frames, at this ratio. See the class comment. */
    double inputLeadFrames (double ratio) const noexcept;

    /** Stretch. `in` holds `inFrames` frames of source, `out` receives
        `outFrames`. Both are two-channel. Real-time safe. */
    void process (const float* inL, const float* inR, int inFrames,
                  float* outL, float* outR, int outFrames) noexcept;

    /** Hand the stretcher the audio that would have come immediately before the
        next `process`, so it can start somewhere new without a gap or a click.
        `in` is the run of source ending at the new read position.
        `primeFrames` says how much it wants: `primeFrames()`. Real-time safe -
        it is bounded work on buffers sized in `prepare`, and it is the whole
        reason a loop change does not have to wait for the stretcher to refill.

        The WSOLA fallback ignores the audio and simply starts clean, which it
        can afford to: its window is a few milliseconds, not a hundred. */
    void prime (const float* inL, const float* inR, int primeFrames, double ratio) noexcept;
    int  primeFrames() const noexcept;

private:
    /** Two passes of a percussive burst through this backend, at two ratios,
        to find where the burst comes back out. See LoopStretcher::Impl. */
    void calibrateLead() noexcept;
    /** The built-in floor. Only compiled in when Signalsmith is not vendored. */
    void wsola (const float* inL, const float* inR, int inFrames,
                float* outL, float* outR, int outFrames) noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl;
    double sampleRate = 48000.0;
    int  maxBlock = 1024;
    float maxRatio = 2.0f;
    bool prepared = false;
};

} // namespace vp
