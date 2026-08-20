#pragma once

#include <vector>

namespace vp
{

/**
    Beat tracking as one decision over the whole performance, instead of a fresh
    guess every refresh.

    This is the bar-pointer model - Whiteley/Cemgil/Godsill 2006, made efficient
    by Krebs/Bock/Widmer 2015, and the thing behind both madmom's DBN beat
    tracker and BeatNet's particle filter. The state is a pair: which tempo the
    music is at, and how far through a beat it is. Every frame the state
    advances by one position; at the end of a beat it may change tempo, and
    changing tempo *costs* something.

    That cost is the whole point, and it is what the machinery this replaces did
    not have. A comb filter refreshed every eight frames answers "which period
    fits the last few seconds best" and is free to answer differently the next
    time - which is heard as the tempo stepping between bars. Here, moving to a
    different tempo has to be paid for out of the evidence, so a tempo only
    moves when the music has actually moved, and it moves the way a player
    moves: to a neighbouring tempo, continuously.

    It also makes the metrical level a decision about the whole sequence rather
    than a per-window argmax. On material where the eighths are nearly as strong
    as the beats, a window-by-window score flips; an accumulated one does not.

    Causal: the forward algorithm, one pass, no lookahead. Runs on the analysis
    worker. prepare() allocates; push() does not.
*/
class BeatHmm
{
public:
    /** Reported range. The state space is built over exactly this. */
    static constexpr float kMinBpm = 50.0f;
    static constexpr float kMaxBpm = 215.0f;

    void prepare (double framesPerSecond);
    void reset() noexcept;

    /** One beat activation per analysis frame. */
    void push (float activation) noexcept;

    bool  ready()  const noexcept { return frames > warmupFrames; }
    /** The tempo of the most likely state, in BPM. Moves continuously. */
    float bpm()    const noexcept { return reportedBpm; }
    /** How far through the beat the most likely state is, 0..1. */
    float phase()  const noexcept { return reportedPhase; }
    /** True on the frame the most likely state sits on a beat. */
    bool  onBeat() const noexcept { return beatNow; }
    /** How much better the winning tempo is than the best rival at a different
        metrical level, in log-probability. Large means the level is settled. */
    float levelMargin() const noexcept { return margin; }

    /** How dearly a tempo change is paid for. Larger holds the tempo harder.
        Exposed so the probes can sweep it; the default is what they chose. */
    void setChangePenalty (float lambda) noexcept { changeLambda = lambda; }
    /** Width of the perceptual tempo prior, in octaves. A listener asked to tap
        along to something ambiguous does not pick 190 BPM. */
    void setPriorWidth (float octaves) noexcept;
    /** Where the pulse a listener would tap sits, in BPM. */
    void setPriorCentre (float bpm) noexcept;
    /** How many frames at the start of a beat count as "the beat". Zero means a
        fixed fraction of the period, which is madmom's rule and assumes an
        activation as sharp as madmom's; a fixed count assumes the bump has a
        width of its own, which BeatNet's does. Exposed because the two give
        opposite biases and only the data settles it. */
    void setBeatWidth (int frames) noexcept { beatWidthFrames = frames; }

private:
    void rebuildPrior() noexcept;


    double fps = 50.0;
    int    tauMin = 14, tauMax = 60;
    int    numTempi = 0;
    int    numStates = 0;
    int    warmupFrames = 100;
    long long frames = 0;

    float changeLambda = 200.0f;
    float priorWidth = 0.40f;
    float priorCentre = 118.0f;
    int   beatWidthFrames = 1;

    /** Where each tempo's block of phase states starts. */
    std::vector<int>   base;
    std::vector<int>   tau;
    std::vector<float> logPrior;     // per tempo, added when a beat is entered
    std::vector<float> alpha;        // forward log-probabilities, current
    std::vector<float> next;         // and the frame being built

    float reportedBpm = 0.0f;
    float reportedPhase = 0.0f;
    float margin = 0.0f;
    bool  beatNow = false;
};

} // namespace vp
