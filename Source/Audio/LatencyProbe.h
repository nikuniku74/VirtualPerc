#pragma once

#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace vp
{

/**
    The rig's own round trip, measured instead of asked for.

    What the clock has to run ahead by is the time between the app deciding to
    play a stroke and that stroke being heard next to the band. Until now that
    came from two places, and neither is a measurement of *this* rig: the
    latency the operating system reports for the device, and a 20 ms constant in
    `BeatTracker` calibrated once against a notated click through the network.
    Both are reasonable and both are guesses. On a real stage the path is an
    iPad, a USB interface, a desk, its own buffering, and whatever the desk is
    doing to the return - and the number that comes out of that is a property of
    the room the app is standing in, not of the code.

    So: play a short sweep, listen for it coming back, and cross-correlate. The
    peak is the round trip, to the sample. It is the same thing a system
    engineer does with a tape measure and a delay line, and it takes a second.

    The work is split across two threads on purpose. The audio thread does only
    what has to be sample-accurate - putting the sweep out and keeping the input
    - and the correlation, which is tens of millions of multiplies, is done by
    whoever calls `analyse()` afterwards. Nothing is allocated once `prepare`
    has run.
*/
class LatencyProbe
{
public:
    void prepare (double sr);

    /** Arm it. Safe from any thread; the audio thread starts on its next block. */
    void start() noexcept;

    /** Stop without a result - the listener closed the page, or the device
        changed under it. */
    void cancel() noexcept;

    bool isRunning() const noexcept { return state.load (std::memory_order_relaxed) == running; }

    /** The capture is complete and `analyse` has something to work on. */
    bool ready() const noexcept { return state.load (std::memory_order_relaxed) == captured; }

    /** Audio thread. Mixes the sweep into the output and keeps the input.

        `in` may be null - a rig with no input cannot be measured, and the probe
        simply never completes rather than reporting a number it did not
        measure. */
    void process (const float* in, float* outL, float* outR, int numSamples) noexcept;

    /** Whoever is not the audio thread. Correlates and returns the round trip
        in milliseconds, or a negative number when the sweep did not come back
        clearly enough to be believed - a muted return, a desk that is not
        routed back, somebody measuring in a noisy room.

        Leaves the probe idle either way, so it can be run again. */
    float analyse() noexcept;

    /** How clear the last measurement was: the correlation peak against the
        best rival peak elsewhere in the capture. Under about two the answer was
        a coincidence, and `analyse` will have refused it. */
    float lastClarity() const noexcept { return clarity; }

    /** Seconds a measurement takes, so the screen can say how long to wait. */
    static constexpr double kCaptureSeconds = 0.75;

private:
    enum State { idle = 0, running, captured };

    /** The sweep. Short enough that a listener hears a tick rather than a tone,
        long enough to correlate sharply, and swept rather than a click because
        a click puts all its energy in one sample and a PA does not like that. */
    static constexpr double kChirpSeconds = 0.030;
    static constexpr double kChirpLoHz = 300.0;
    static constexpr double kChirpHiHz = 5000.0;
    /** Quiet: it goes out over a stage, and it only has to beat the room's own
        floor once. */
    static constexpr float kChirpGain = 0.22f;
    /** Below this the return is not the sweep, it is the room agreeing with it
        by accident. */
    static constexpr float kMinClarity = 2.0f;
    /** A round trip under this is the app hearing its own output before it can
        possibly have left the device, which means the capture is not a
        measurement of anything. */
    static constexpr double kMinPlausibleSec = 0.0015;

    double sampleRate = 48000.0;
    std::vector<float> chirp;
    std::vector<float> capture;
    int chirpLen = 0;
    int captureLen = 0;
    int pos = 0;
    float clarity = 0.0f;
    std::atomic<int> state { idle };
    std::atomic<bool> wantStart { false };
};

} // namespace vp
