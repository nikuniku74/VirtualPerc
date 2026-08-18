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

    So this scores candidate periods on the recency-weighted autocorrelation of
    the activation instead. A candidate collects its own harmonics - lag, twice
    the lag, three times - and is charged for the half-lags in between. That is
    what brackets the octave from both sides: the true period finds nothing
    halfway between its beats, while a candidate at twice the true period finds
    a full pulse there and loses. Candidate periods are real-valued, because a
    tempo whose period is not a whole number of frames would otherwise smear
    across the analysis window and lose to its own sub-harmonics.

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

private:
    void refresh() noexcept;
    float tempoPrior (float bpmCandidate) const noexcept;
    float lagAt (float lag) const noexcept;
    float combScore (float lag) const noexcept;

    double fps = 50.0;
    int minLag = 14;
    int maxLag = 60;
    int acfLags = 0;        // highest integer lag the autocorrelation covers
    int candidates = 0;     // fine real-valued period grid, minLag..maxLag
    int window = 0;
    int write = 0;
    int filled = 0;
    int sinceRefresh = 0;
    bool isReady = false;
    float bestBpm = 0.0f;
    float bestSalience = 0.0f;
    float bestClarity = 0.0f;

    std::vector<float> ring;      // activation history, oldest overwritten
    std::vector<float> lin;       // newest-first scratch, mean removed
    std::vector<float> weight;    // exponential by age: recent frames dominate
    std::vector<float> acf;       // recency-weighted autocorrelation by lag
    std::vector<float> score;     // comb score per candidate period
};

} // namespace vp
