#include "AI/BeatDecoder.h"

#include "Core/Types.h"

#include <algorithm>
#include <cmath>

namespace vp
{

namespace
{
    constexpr float kMinBpm = TempoEstimator::kMinBpm;
    constexpr float kMaxBpm = TempoEstimator::kMaxBpm;

    // Below this the fold found no pulse worth trusting.
    constexpr float kSalienceFloor = 0.14f;

    // Beats agreeing this closely count as "the tempo has not moved".
    constexpr float kStableTolerance = 0.010f;

    // Beats disagreeing this much, repeatedly and in the same direction, mean
    // the tempo really is moving.
    constexpr float kDriftTolerance = 0.014f;

    // Consecutive agreeing beats before a tempo is called fixed. Eight beats is
    // two bars: long enough to rule out a fill, short enough to settle fast.
    constexpr int kBeatsToFixed = 8;

    // Consecutive drifting beats before a fixed tempo is released.
    constexpr int kBeatsToLive = 3;
    constexpr int kBeatsToLeaveFixed = 4;

    // The fast release path: recent intervals are noisy, so the bar is set well
    // above their spread and three beats must agree on the direction. Random
    // jitter does not accumulate a sign; a band changing tempo does.
    constexpr float kFastDriftTolerance = 0.024f;
    constexpr int   kFastBeatsToLeaveFixed = 3;

    // How fast the committed tempo moves per beat in each regime.
    constexpr float kRateAcquiring = 0.70f;
    constexpr float kRateLive      = 0.50f;
    constexpr float kRateFixed     = 0.12f;

    // A fixed tempo may only be refined, never dragged.
    constexpr float kFixedMaxStep = 0.015f;

    // The 8-beat fit is centred 3.5 beats back and the 24-beat fit 11.5 beats
    // back, so their difference spans 8 beats of tempo change. Leading the short
    // fit by its own 3.5 beats therefore needs 3.5/8 of that difference.
    constexpr float kLiveLead = 3.5f / 8.0f;

    // Disagreement with the comb beyond a quarter octave means a different
    // metrical level, but the comb glitches for a frame now and then and
    // re-anchoring throws away the beat history. Make it prove itself first, and
    // ask for far more proof the longer the current grid has been working: four
    // beats is right for an octave picked badly a second ago, and nowhere near
    // enough to overturn two bars of agreement on a record cut to a click.
    constexpr float kOctaveThreshold = 0.25f;
    constexpr int   kOctaveSnapBeats = 4;
    constexpr int   kOctaveSnapBeatsLive = 8;
    constexpr int   kOctaveSnapBeatsFixed = 16;
    constexpr float kOctaveSnapClarity = 0.18f;
    constexpr float kOctaveSnapSalience = 0.22f;

    // Once a tempo is established, a peak has to land on the grid to count as a
    // beat. Subdivisions clear the activation threshold all the time - a hi-hat
    // pattern puts one halfway between every pair of beats - and taking those as
    // beats moves the phase reference half a beat back and forth, which reads as
    // a clock that will not settle.
    //
    // This has to be tighter than the finest subdivision it must turn away, or a
    // sixteenth beside a beat the fill drowned out lands inside the tolerance
    // and pulls the grid a quarter beat sideways - from where every real beat
    // looks off-grid too. Sixteenths sit at 0.25 of a beat, triplets at 0.33.
    constexpr double kOnGridTolerance = 0.18;

    // Unless the grid itself has gone quiet. If nothing has landed on it for
    // this long the grid is the wrong one - a new song, an edit, a section that
    // dropped the beat - and the next peak re-anchors it. Counting rejections
    // instead would re-anchor on a drum fill, which is exactly the material the
    // grid is there to ride out.
    constexpr double kGridStaleBeats = 2.5;

    // A grid that still lands on the beats being detected, and lands tightly, is
    // not the wrong grid - and the double of the true tempo always is one of
    // those, so agreeing with the comb is not on its own a reason to move.
    constexpr float kGridHealthyResidual = 0.035f;
    constexpr float kGridHealthyCoverage = 0.80f;
}

void BeatDecoder::prepare (double framesPerSecond)
{
    fps = framesPerSecond > 1.0 ? framesPerSecond : 50.0;
    tempo.prepare (fps);
    reset();
}

void BeatDecoder::reset() noexcept
{
    tempo.reset();
    timeSec = 0.0;
    lastBeatSec = -1.0;
    lastDownbeatSec = -1.0;
    bpm = 120.0f;
    refractoryFrames = 0;
    beatsInBar = 0;
    frame = 0;
    established = false;
    prevPulse = 0.0f;
    prevPrevPulse = 0.0f;
    prevDownbeat = 0.0f;
    beatWrite = 0;
    beatFilled = 0;
    beatSerial = 0;
    downbeatSerial = 0;
    tempoRegime = TempoRegime::unknown;
    stableBeats = 0;
    driftBeats = 0;
    driftSign = 0;
    fastDriftBeats = 0;
    fastDriftSign = 0;
    octaveMismatchBeats = 0;
    lastFitResidual = 1.0f;
    lastFitCoverage = 0.0f;
    longFitBpm = 0.0f;
    shortFitBpm = 0.0f;
    std::fill (beatTime, beatTime + kBeatHistory, 0.0);
    hyp = {};
}

float BeatDecoder::foldToPeriod (float ioiSec, float reference) const noexcept
{
    // A missed beat doubles the interval, a ghost halves it. Snap the interval
    // onto the metrical level nearest the reference instead of believing it.
    static constexpr float divisors[] = { 0.5f, 1.0f, 2.0f, 3.0f, 4.0f };
    float best = ioiSec;
    float bestErr = std::fabs (ioiSec - reference);
    for (float d : divisors)
    {
        const float candidate = ioiSec / d;
        const float err = std::fabs (candidate - reference);
        if (err < bestErr)
        {
            bestErr = err;
            best = candidate;
        }
    }
    return best;
}

bool BeatDecoder::recentPeriod (float& period) const noexcept
{
    // Median of the last few intervals. Far noisier than a fit, but it sees a
    // tempo change three beats after it starts instead of eight, which is what
    // decides how quickly a held tempo is allowed to be released. Interpolating
    // the activation peaks is what makes this usable at all: on the raw 20 ms
    // frame grid a single interval carries 4% of error at 120 BPM.
    if (beatFilled < kRecentIoi + 1 || bpm < kMinBpm)
        return false;

    const float reference = 60.0f / bpm;
    float ioi[kRecentIoi];
    for (int k = 0; k < kRecentIoi; ++k)
    {
        const int newer = (beatWrite - 1 - k + kBeatHistory) % kBeatHistory;
        const int older = (beatWrite - 2 - k + kBeatHistory) % kBeatHistory;
        const float raw = static_cast<float> (beatTime[newer] - beatTime[older]);
        if (raw <= 0.0f)
            return false;
        ioi[k] = foldToPeriod (raw, reference);
    }

    std::sort (ioi, ioi + kRecentIoi);
    period = ioi[kRecentIoi / 2];
    return period > 60.0f / kMaxBpm && period < 60.0f / kMinBpm;
}

bool BeatDecoder::fitPeriod (int maxBeats, float& period, float& residual, float& coverage) const noexcept
{
    coverage = 0.0f;
    const int n = std::min (beatFilled, maxBeats);
    if (n < 4 || bpm < kMinBpm)
        return false;

    // Newest n beat times, oldest first.
    double t[kBeatHistory];
    const int oldest = (beatWrite - n + kBeatHistory) % kBeatHistory;
    for (int i = 0; i < n; ++i)
        t[i] = beatTime[(oldest + i) % kBeatHistory];

    const double guess = 60.0 / static_cast<double> (bpm);

    // Index each beat on the committed grid, dropping anything that does not
    // sit on it. One spurious peak must not tilt the whole fit.
    double idx[kBeatHistory];
    int keep = 0;
    for (int i = 0; i < n; ++i)
    {
        const double beats = (t[i] - t[0]) / guess;
        const double rounded = std::round (beats);
        if (std::fabs (beats - rounded) > 0.28)
            continue;
        idx[keep] = rounded;
        t[keep] = t[i];
        ++keep;
    }
    if (keep < 4 || idx[keep - 1] - idx[0] < 3.0)
        return false;

    // How much of the detected pulse this grid actually accounts for. A grid an
    // octave too slow has to throw away every second beat to fit, and that is
    // the difference between a wrong grid and a merely imprecise one.
    coverage = static_cast<float> (keep) / static_cast<float> (n);

    double meanIdx = 0.0, meanT = 0.0;
    for (int i = 0; i < keep; ++i)
    {
        meanIdx += idx[i];
        meanT += t[i];
    }
    meanIdx /= static_cast<double> (keep);
    meanT /= static_cast<double> (keep);

    double sxy = 0.0, sxx = 0.0;
    for (int i = 0; i < keep; ++i)
    {
        const double dx = idx[i] - meanIdx;
        sxy += dx * (t[i] - meanT);
        sxx += dx * dx;
    }
    if (sxx < 1.0e-9)
        return false;

    const double slope = sxy / sxx;
    if (slope < 60.0 / kMaxBpm || slope > 60.0 / kMinBpm)
        return false;

    double sumSq = 0.0;
    for (int i = 0; i < keep; ++i)
    {
        const double predicted = meanT + slope * (idx[i] - meanIdx);
        const double e = t[i] - predicted;
        sumSq += e * e;
    }

    period = static_cast<float> (slope);
    residual = static_cast<float> (std::sqrt (sumSq / static_cast<double> (keep)) / slope);
    return true;
}

void BeatDecoder::commit (float candidateBpm, float rate) noexcept
{
    if (candidateBpm < kMinBpm || candidateBpm > kMaxBpm)
        return;
    bpm = std::clamp (bpm + (candidateBpm - bpm) * rate, kMinBpm, kMaxBpm);
}

void BeatDecoder::registerBeat (double beatTimeSec) noexcept
{
    beatTime[beatWrite] = beatTimeSec;
    beatWrite = (beatWrite + 1) % kBeatHistory;
    if (beatFilled < kBeatHistory)
        ++beatFilled;
    ++beatSerial;
}

void BeatDecoder::updateTempo() noexcept
{
    const bool combReady = tempo.ready() && tempo.salience() > kSalienceFloor;
    const float combBpm = tempo.bpm();

    // Acquisition: adopt the fold outright. Easing towards it from a default of
    // 120 is what used to make a 75 BPM song take twenty seconds to find.
    if (! established)
    {
        if (combReady)
        {
            bpm = std::clamp (combBpm, kMinBpm, kMaxBpm);
            established = true;
        }
        else if (beatFilled >= 4)
        {
            // No usable fold - a very sparse or very noisy activation curve.
            // Fall back to the beat times alone.
            float period = 0.0f, residual = 0.0f, coverage = 0.0f;
            if (fitPeriod (kShortFit, period, residual, coverage) && residual < 0.06f)
            {
                bpm = std::clamp (60.0f / period, kMinBpm, kMaxBpm);
                established = true;
            }
        }
        if (! established)
            return;
    }

    // The comb owns the metrical level. If the committed tempo has left its
    // octave, something changed underneath us - a new song, a half-time section,
    // or an octave we got wrong on acquisition - and the fit is now fitting the
    // wrong grid. Re-anchor, but only on repeated, confident disagreement: a
    // single bad comb frame must not cost us the beat history.
    const bool gridHealthy = lastFitResidual < kGridHealthyResidual
                             && lastFitCoverage > kGridHealthyCoverage;
    const bool combDisagrees = combReady && bpm > kMinBpm
                               && std::fabs (std::log2 (bpm / combBpm)) > kOctaveThreshold;

    if (combDisagrees
        && tempo.clarity() > kOctaveSnapClarity
        && tempo.salience() > kOctaveSnapSalience
        && ! (tempoRegime == TempoRegime::fixed && gridHealthy))
        ++octaveMismatchBeats;
    else
        octaveMismatchBeats = 0;

    const int snapBeats = tempoRegime == TempoRegime::fixed ? kOctaveSnapBeatsFixed
                        : tempoRegime == TempoRegime::live  ? kOctaveSnapBeatsLive
                                                            : kOctaveSnapBeats;

    if (octaveMismatchBeats >= snapBeats)
    {
        bpm = std::clamp (combBpm, kMinBpm, kMaxBpm);
        beatWrite = 0;
        beatFilled = 0;
        octaveMismatchBeats = 0;
        tempoRegime = TempoRegime::unknown;
        stableBeats = 0;
        driftBeats = 0;
        driftSign = 0;
        fastDriftBeats = 0;
        fastDriftSign = 0;
        // Nothing measured about the grid we just left describes the new one, and
        // a stale clean bill of health would let it defend itself immediately.
        lastFitResidual = 1.0f;
        lastFitCoverage = 0.0f;
        return;
    }

    float longPeriod = 0.0f, longResidual = 0.0f, longCoverage = 0.0f;
    float shortPeriod = 0.0f, shortResidual = 0.0f, shortCoverage = 0.0f;
    const bool haveLong = fitPeriod (kLongFit, longPeriod, longResidual, longCoverage);
    const bool haveShort = fitPeriod (kShortFit, shortPeriod, shortResidual, shortCoverage);

    longFitBpm = haveLong ? 60.0f / longPeriod : 0.0f;
    shortFitBpm = haveShort ? 60.0f / shortPeriod : 0.0f;
    if (haveLong)
    {
        lastFitResidual = longResidual;
        lastFitCoverage = longCoverage;
    }
    else if (haveShort)
    {
        lastFitResidual = shortResidual;
        lastFitCoverage = shortCoverage;
    }

    if (! haveShort)
    {
        // Not enough clean beats on the grid; let the fold carry the tempo.
        if (combReady)
            commit (combBpm, tempoRegime == TempoRegime::fixed ? kRateFixed : kRateAcquiring);
        return;
    }

    // Regime: does the recent tempo still agree with the established one? The
    // short fit is the one that moves when a player speeds up.
    const float reference = haveLong ? longFitBpm : bpm;
    const float deviation = (shortFitBpm - reference) / std::max (kMinBpm, reference);
    const int sign = deviation > 0.0f ? 1 : -1;

    if (std::fabs (deviation) < kStableTolerance)
    {
        ++stableBeats;
        driftBeats = 0;
        driftSign = 0;
    }
    else if (std::fabs (deviation) > kDriftTolerance)
    {
        stableBeats = 0;
        if (sign == driftSign)
            ++driftBeats;
        else
        {
            driftSign = sign;
            driftBeats = 1;
        }
    }

    float recent = 0.0f;
    if (recentPeriod (recent))
    {
        const float fastDeviation = (60.0f / recent - bpm) / std::max (kMinBpm, bpm);
        if (std::fabs (fastDeviation) > kFastDriftTolerance)
        {
            const int fastSign = fastDeviation > 0.0f ? 1 : -1;
            if (fastSign == fastDriftSign)
                ++fastDriftBeats;
            else
            {
                fastDriftSign = fastSign;
                fastDriftBeats = 1;
            }
        }
        else
        {
            fastDriftBeats = 0;
            fastDriftSign = 0;
        }
    }

    // Being called fixed is what earns a tempo the right to defend itself
    // against the comb, so it cannot be granted while the comb is still naming
    // a different metrical level: that would let a bad first guess make itself
    // permanent.
    const bool mayFix = stableBeats >= kBeatsToFixed
                        && lastFitResidual < 0.05f
                        && ! combDisagrees;

    switch (tempoRegime)
    {
        case TempoRegime::unknown:
            if (mayFix)
                tempoRegime = TempoRegime::fixed;
            else if (driftBeats >= kBeatsToLive)
                tempoRegime = TempoRegime::live;
            break;

        case TempoRegime::fixed:
            // Deliberately stubborn. A record cut to a click does not change
            // tempo, so anything that looks like a change here is a fill, a
            // dropout, or a missed beat until it proves otherwise. Three recent
            // intervals agreeing on a direction is that proof, and arrives well
            // before the eight-beat fit notices.
            if (driftBeats >= kBeatsToLeaveFixed
                || fastDriftBeats >= kFastBeatsToLeaveFixed
                || std::fabs (deviation) > 0.06f)
            {
                tempoRegime = TempoRegime::live;
                stableBeats = 0;
            }
            break;

        case TempoRegime::live:
            if (mayFix)
                tempoRegime = TempoRegime::fixed;
            break;
    }

    switch (tempoRegime)
    {
        case TempoRegime::fixed:
        {
            // Refine towards the long, precise baseline, but never let it be
            // dragged: this is what keeps a Spotify track from wandering.
            const float target = haveLong ? longFitBpm : shortFitBpm;
            const float step = (target - bpm) / std::max (kMinBpm, bpm);
            if (std::fabs (step) <= kFixedMaxStep)
                commit (target, kRateFixed);
            break;
        }

        case TempoRegime::live:
        {
            // A least-squares fit reports the tempo at the centre of its own
            // window, so through an accelerando every fit is behind the player.
            // The gap between the short and long baselines is proportional to
            // how fast the tempo is moving, which is exactly what is needed to
            // extrapolate the short fit forward to now.
            float target = shortFitBpm;
            if (haveLong)
            {
                const float lead = kLiveLead * (shortFitBpm - longFitBpm);
                target += std::clamp (lead, -0.04f * shortFitBpm, 0.04f * shortFitBpm);
            }
            commit (target, kRateLive);
            break;
        }

        case TempoRegime::unknown:
            commit (haveLong ? longFitBpm : shortFitBpm, kRateAcquiring);
            break;
    }
}

float BeatDecoder::scoreConfidence() const noexcept
{
    if (! established)
        return std::clamp (0.35f * tempo.salience(), 0.0f, 1.0f);

    const float salience = std::clamp (tempo.salience() / 0.55f, 0.0f, 1.0f);
    const float clarity = std::clamp (tempo.clarity() / 0.50f, 0.0f, 1.0f);
    const float tightness = 1.0f - std::clamp (lastFitResidual / 0.08f, 0.0f, 1.0f);
    const float coverage = std::clamp (static_cast<float> (beatFilled) / 8.0f, 0.0f, 1.0f);

    float score = 0.34f * salience
                + 0.20f * clarity
                + 0.28f * tightness
                + 0.18f * coverage;

    // Beats should keep arriving. If the last one is long overdue the grid is
    // stale, whatever the fold still says.
    if (lastBeatSec >= 0.0 && bpm > kMinBpm)
    {
        const double period = 60.0 / static_cast<double> (bpm);
        const double overdue = (timeSec - lastBeatSec) / period;
        if (overdue > 2.0)
            score *= static_cast<float> (std::max (0.0, 1.0 - (overdue - 2.0) / 4.0));
    }

    if (tempoRegime == TempoRegime::fixed)
        score = std::min (1.0f, score * 1.10f);

    return std::clamp (score, 0.0f, 1.0f);
}

BeatHypothesis BeatDecoder::observe (float pBeat, float pDownbeat, float pNone) noexcept
{
    (void) pNone;
    const double hopSec = 1.0 / fps;
    timeSec += hopSec;
    ++frame;

    if (refractoryFrames > 0)
        --refractoryFrames;

    // BeatNet's official inference gate uses max(beat, downbeat): a downbeat is
    // also a beat, and looking only at pBeat drops bar accents.
    const float pulseActivation = std::max (pBeat, pDownbeat);
    tempo.push (pulseActivation);

    const float period = 60.0f / std::max (kMinBpm, bpm);
    const int minRefr = std::max (2, static_cast<int> (0.4f * period * static_cast<float> (fps)));

    // Activations are broad curves, so emit one causal event at a local maximum
    // rather than retriggering while the curve stays above threshold.
    const bool localMaximum = frame >= 3
                              && prevPulse >= beatThresh
                              && prevPulse >= prevPrevPulse
                              && prevPulse > pulseActivation;

    // The peak sits between frames far more often than on one. Interpolating it
    // takes the timing error from +/-10 ms of frame quantisation to a couple of
    // ms, which the tempo fit and the phase both need.
    double eventTimeSec = timeSec - hopSec;
    if (localMaximum)
    {
        const float denom = prevPrevPulse - 2.0f * prevPulse + pulseActivation;
        if (std::fabs (denom) > 1.0e-9f)
        {
            const float shift = std::clamp (0.5f * (prevPrevPulse - pulseActivation) / denom,
                                            -0.5f, 0.5f);
            eventTimeSec += static_cast<double> (shift) * hopSec;
        }
    }

    bool peak = localMaximum && refractoryFrames == 0
                && (lastBeatSec < 0.0
                    || (eventTimeSec - lastBeatSec) >= 0.4 * static_cast<double> (period));

    if (peak && established && lastBeatSec >= 0.0)
    {
        const double beats = (eventTimeSec - lastBeatSec) / static_cast<double> (period);
        if (std::fabs (beats - std::round (beats)) > kOnGridTolerance && beats < kGridStaleBeats)
            peak = false;
    }

    if (peak)
    {
        registerBeat (eventTimeSec);
        lastBeatSec = eventTimeSec;
        refractoryFrames = minRefr;
        beatsInBar = (beatsInBar + 1) % 4;
        if (prevDownbeat > downThresh)
        {
            lastDownbeatSec = eventTimeSec;
            beatsInBar = 0;
            ++downbeatSerial;
        }
        updateTempo();
    }
    else if (! established && (frame % 8) == 0)
    {
        // The fold can name a tempo before any peak clears the gate, which is
        // most of the head start on locking.
        updateTempo();
    }

    const float newPeriod = 60.0f / std::max (kMinBpm, bpm);
    float phase = 0.0f;
    if (lastBeatSec >= 0.0)
        phase = wrap01 (static_cast<float> ((timeSec - lastBeatSec) / static_cast<double> (newPeriod)));

    hyp.bpm = bpm;
    hyp.beatPhase = phase;
    hyp.barPhase = wrap01 ((static_cast<float> (beatsInBar) + phase) * 0.25f);
    hyp.pBeat = pBeat;
    hyp.pDownbeat = pDownbeat;
    hyp.frameIndex = frame;
    hyp.peak = peak;
    hyp.downbeat = peak && prevDownbeat > downThresh;
    hyp.beatSerial = beatSerial;
    hyp.downbeatSerial = downbeatSerial;
    hyp.periodSec = newPeriod;
    hyp.regime = tempoRegime;
    hyp.confidence = scoreConfidence();
    hyp.valid = established;

    prevPrevPulse = prevPulse;
    prevPulse = pulseActivation;
    prevDownbeat = pDownbeat;
    return hyp;
}

} // namespace vp
