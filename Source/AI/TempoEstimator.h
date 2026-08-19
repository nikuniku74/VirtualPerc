#pragma once

#include <vector>

namespace vp
{

/**
    Tempo salience read straight off the beat-activation curve.

    Thresholded peaks alone cannot choose between a period and its octaves: the
    inter-onset histogram that BeatDecoder used before this class votes for
    sub-harmonics as soon as a beat is detected cleanly, because sums of
    adjacent intervals land on 2x and 3x the true period.

    Autocorrelation cannot choose either, and that is the part that is easy to
    get wrong. Measured on BeatNet activations from real material, the
    correlation at one, two, three and four beats sits between 0.78 and 0.97 for
    every one of them: a beat train correlates with itself just as well at any
    multiple of the beat. Anything that picks the level from correlation alone
    is really picking it from whatever tie-breaker sits behind it, which is how
    a preference for 120 BPM ends up reporting 60 BPM songs at 120 and 168 BPM
    songs at 84.

    So the level is decided on the activation's **amplitude** instead. Folded
    onto the true beat period the curve is tall on the beat and flat half a beat
    later; folded onto twice the true period the two halves look the same,
    because the "empty" half is a full beat. Over the same captures that ratio
    is 0.13 at the true period against 0.80 at the double, which is a decision
    rather than a tie.

    Runs on the neural worker thread. prepare() allocates; push() does not.
*/
class TempoEstimator
{
public:
    // Reported range, not the search range: the search runs wider and folds its
    // winner in by octaves, because a candidate pinned against the edge of this
    // range is the edge rather than a measurement.
    static constexpr float kMinBpm = 50.0f;
    static constexpr float kMaxBpm = 215.0f;

    void prepare (double framesPerSecond);
    void reset() noexcept;

    /** One beat-activation sample per analysis frame, ideally
        max(pBeat, pDownbeat). Rebuilds the salience curve every refresh
        interval; the cost is bounded and allocation free. */
    void push (float activation) noexcept;

    bool  ready()    const noexcept { return isReady; }
    float bpm()      const noexcept { return bestBpm; }

    /** Height of the winning comb, 0..1. Low means "no pulse in here". */
    float salience() const noexcept { return bestSalience; }

    /** How far the winner stands above the best rival period, 0..1. Low means
        the octave (or a competing metre) is nearly as plausible. */
    float clarity()  const noexcept { return bestClarity; }

    /** How strongly the winning period's own subdivision answers back, 0..1.
        Near zero the beat is unmistakable; near one the level is a coin toss
        and the decoder should not throw away a working grid over it. */
    float offbeatRatio() const noexcept { return bestOffbeat; }

private:
    void  refresh() noexcept;
    float tempoPrior (float bpmCandidate) const noexcept;
    float lagAt (float lag) const noexcept;
    float combStrength (float lag) const noexcept;
    float halfPhaseRatio (float lag, int n) const noexcept;
    float levelScore (float lag, int n, float& offbeatOut, bool& ruledOutFast) const noexcept;
    static float foldIntoRange (float bpmCandidate) noexcept;

    // Phase bins used to fold the activation onto a candidate period. Eight
    // resolves the on-beat and the half-beat with room either side, and stays
    // cheap enough to run for every candidate the search keeps.
    static constexpr int kPhaseBins = 8;

    // Candidates the fold is evaluated for. The comb is scanned on the fine
    // grid, but only its local maxima can be a metrical level, and there are
    // never many of those.
    static constexpr int kMaxPeaks = 16;

    double fps = 50.0;
    int minLag = 14;
    int maxLag = 60;
    int acfLags = 0;        // highest integer lag the autocorrelation covers
    int acfValidTop = 0;    // highest lag with enough overlap to mean anything
    int candidates = 0;     // fine real-valued period grid, minLag..maxLag
    int window = 0;
    int write = 0;
    int filled = 0;
    int sinceRefresh = 0;
    bool isReady = false;
    float bestBpm = 0.0f;
    float bestSalience = 0.0f;
    float bestClarity = 0.0f;
    float bestOffbeat = 1.0f;
    float challengerBpm = 0.0f;
    int   challengerRefreshes = 0;

    std::vector<float> ring;      // activation history, oldest overwritten
    std::vector<float> lin;       // newest-first scratch, mean removed
    std::vector<float> weight;    // exponential by age: recent frames dominate
    std::vector<float> foldWeight;// near-flat by age: the level is slow-moving
    std::vector<float> acf;       // recency-weighted autocorrelation by lag
    std::vector<float> score;     // comb strength per candidate period
};

} // namespace vp
