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

    // How far ahead of the other metrical levels the state space has to be
    // before its answer is used as the level, in log-probability, and how much
    // better a different octave has to look before the level is moved. The
    // second one is hysteresis: an anchor landing between two octaves must not
    // be able to flip the level from bar to bar, which is the exact complaint
    // this whole exercise started from.
    constexpr float kAnchorMargin = 2.0f;
    constexpr float kAnchorHysteresis = 0.12f;

    // And a higher bar to *acquire* on, because the two decisions cost
    // different things. Folding the comb's answer into the state space's octave
    // is reversible on the next refresh; adopting the state space's tempo
    // outright is what the grid, the fits and the bar are then built on, and
    // the state space has a change penalty, so a level taken too early is one
    // it will defend.
    // On a microphone in a room. Measured: every value below this puts a 128
    // BPM track on the wrong metrical level once the app has been listening to
    // the room first, and it is not the 76 BPM ambiguity that is everywhere
    // else in this file - it is a track that reads right at 4 and wrong at 3.5.
    constexpr float kAnchorAcquireMargin = 4.0f;

    // And on a line feed, where the activations are sharp and the margin means
    // what it says. Measured over thirty tracks: the same 2.5 that costs an
    // octave through a microphone costs none here, and takes the time to lock
    // from 2.32 s to 1.50 s. The two paths were never the same measurement -
    // docs/STATUS.md has a section on how different - and this is one more
    // place where pretending they are costs something.
    constexpr float kAnchorAcquireMarginLine = 2.5f;

    // The long fit, watched over a window of beats. A record cut to a click
    // holds its 24-beat fit inside a few tenths of a percent; a band drifts out
    // of that band and keeps going in one direction.
    //
    // These are spreads over the whole window, not beat-to-beat differences,
    // which is what makes them survive an occasional bad beat: one outlier
    // widens the spread for as long as it stays in the window and then leaves,
    // where a consecutive-beat counter would have gone back to zero.
    constexpr float kFixedSpreadRoom = 0.015f; // room onset scatter is wider
    constexpr float kFixedSpreadLine = 0.009f; // a line feed has no such excuse
    constexpr float kLiveTrend   = 0.018f;   // 1.8% across the window = moving

    // Sustained disagreement between the committed tempo and the long fit. This
    // is the slow, certain way out of a held tempo; the fast way is below.
    constexpr float kLeaveFixedError = 0.020f;
    constexpr int   kBeatsToLeaveFixed = 6;

    // The fast release path: recent intervals against the held tempo, three
    // beats agreeing on a direction. Random jitter does not accumulate a sign;
    // a band changing tempo does.
    //
    // Two tolerances, because a line feed may use a large deviation as a fast
    // exit while a gentle one needs corroboration from the long window. On the
    // room path neither stands on the recent median alone: room-smoothed onsets
    // cross both thresholds in one direction often enough to release a fixed
    // record.
    constexpr float kFastDriftTolerance = 0.024f;
    constexpr float kFastDriftLarge     = 0.045f;
    constexpr int   kFastBeatsToLeaveFixed = 3;
    constexpr int   kFastBeatsAlone = 5;


    // How fast the committed tempo moves per beat in each regime.
    //
    // The live rate used to be 0.35 and that was a third too fast for what it
    // is fed: an eight-beat fit on a microphone in a room is noisy, and chasing
    // it at a third of the distance per beat turns the noise into a tempo that
    // will not sit still. Measured over thirty tracks, dropping it to 0.22
    // takes the settled BPM range from 4.61 to 3.40 on the iPad path and the
    // beat-to-beat wobble from 0.20 to 0.15, and - the part that decides it -
    // does the same on material that genuinely moves, 5.89 to 5.21, because a
    // rate that overshoots rings afterwards and one that does not, does not.
    //
    // It is not free below this. At 0.15 the same benches are no better and the
    // step from 120 to 132 BPM takes 7.28 s to catch instead of 5.46; at 0.22
    // that step and the 120-to-140 accelerando are unchanged to the digit.
    constexpr float kRateAcquiring = 0.70f;
    constexpr float kRateLive      = 0.22f;

    // A fixed tempo may only be refined, never dragged.
    constexpr float kFixedMaxStep = 0.015f;

    // Below this the committed tempo is not moved at all. A fixed tempo that
    // keeps taking hundredth-of-a-BPM steps is a number that never stops
    // changing on screen and a clock that never stops being nudged, for a
    // correction no listener could hear: a hundredth of a BPM is 4 microseconds
    // of beat at 120.
    constexpr float kFixedDeadband = 0.05f;

    // Floor on the anchor's gain. 1/n alone would freeze the anchor solid after
    // a couple of minutes, and a record that really does creep - a live take, a
    // tape transfer - would never be caught up with.
    constexpr float kFixedAnchorFloor = 0.02f;

    // Beats a regime must last before it may be left, so the machine cannot
    // oscillate between two verdicts on adjacent beats.
    constexpr int kRegimeMinBeats = 4;

    // The 8-beat fit is centred 3.5 beats back and the 24-beat fit 11.5 beats
    // back, so their difference spans 8 beats of tempo change. Leading the short
    // fit by its own 3.5 beats therefore needs 3.5/8 of that difference.
    constexpr float kLiveLead = 3.5f / 8.0f;

    /** Smoothing on the short fit's own movement, per beat. Diagnostic only -
        see the live branch for why it cannot be used to tell a ramp from a
        step. */
    constexpr float kShortRateSmoothing = 0.35f;

    // Disagreement with the comb beyond a quarter octave means a different
    // metrical level, but the comb glitches for a frame now and then and
    // re-anchoring throws away the beat history. Make it prove itself first, and
    // ask for far more proof the longer the current grid has been working: four
    // beats is right for an octave picked badly a second ago, and nowhere near
    // enough to overturn two bars of agreement on a record cut to a click.
    constexpr float kOctaveThreshold = 0.25f;

    // Between "the same tempo" and "a different metrical level" there is a gap,
    // and the decoder used to fall into it and stay there. A click at 78 BPM
    // was tracked at 90: a quarter of an octave is 0.25, log2(90/78) is 0.21,
    // so the re-anchor never fired - and the fit cannot walk out on its own,
    // because on a grid 15% fast the fit is fitting whichever beats happen to
    // land inside its tolerance. Nothing between 3% and a quarter octave was
    // anybody's job.
    //
    // So a settled comb also gets a pull, short of a re-anchor: no history is
    // thrown away, the committed tempo is simply drawn towards the fold a
    // third of the way per beat. Precision still comes from the fit; being in
    // the right place comes from the comb.
    constexpr float kCombPullThreshold = 0.030f;
    constexpr float kCombPull = 0.35f;
    constexpr int   kOctaveSnapBeats = 4;
    constexpr int   kOctaveSnapBeatsLive = 6;
    constexpr int   kOctaveSnapBeatsFixed = 8;
    constexpr int   kOctaveSnapBeatsHealthy = 4;
    constexpr int   kOctaveSnapBeatsProvisional = 2;

    // Tenure. Beats the level has survived, divided by this, are added to the
    // proof a rival must produce - up to a ceiling, so a level can still be
    // overturned by a new song rather than merely by a long one. The counters
    // above describe how confident the decoder is in the level; this describes
    // how much the listener has already heard of it, which is the other half of
    // what a change costs.
    constexpr int kOctaveTenurePerBeat = 12;
    constexpr int kOctaveTenureMax = 10;
    constexpr float kOctaveSnapSalience = 0.22f;

    // How far the comb may move and still be casting the same vote.
    constexpr float kOctaveVoteHold = 0.10f;

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

    // How far the fold and the committed grid have to disagree about which part
    // of the beat we are on before the grid is moved, how many beats in a row it
    // has to say so, and how flat the fold may be half a period from its peak
    // and still be believed.
    //
    // A fifth of a beat is wider than anything the fit's own scatter produces
    // and narrower than the half beat this exists to catch. Three beats in a
    // row, because one bad fold must not move a working grid. And the contrast
    // gate is what keeps it off material where the offbeat genuinely is as loud
    // as the beat - measured at 0.73-0.77 on a 76 BPM mix with full eighths,
    // where nothing can tell the two apart and moving the grid would be a coin
    // toss played six times a second.
    constexpr float kFoldPhaseThreshold = 0.20f;
    constexpr int   kFoldPhaseBeats = 3;
    constexpr float kFoldPhaseContrast = 0.70f;

    // How long the fold gets to name a level before the decoder is allowed to
    // establish one from beat times alone. TempoEstimator needs a few seconds
    // of buffer before it can even see the slow half of its range, and a level
    // established before then is established from subdivisions.
    constexpr double kPeakOnlyGraceSec = 6.0;
}

void BeatDecoder::prepare (double framesPerSecond)
{
    fps = framesPerSecond > 1.0 ? framesPerSecond : 50.0;
    tempo.prepare (fps);
    hmm.prepare (fps);
    reset();
}

void BeatDecoder::reset() noexcept
{
    tempo.reset();
    timeSec = 0.0;
    lastBeatSec = -1.0;
    lastDownbeatSec = -1.0;
    gridAnchorSec = -1.0;
    foldPhaseBeats = 0;
    bpm = 120.0f;
    refractoryFrames = 0;
    beatsInBar = 0;
    frame = 0;
    established = false;
    prevPulse = 0.0f;
    prevPrevPulse = 0.0f;
    prevDownbeat = 0.0f;
    prevPrevDownbeat = 0.0f;
    lastDownbeatStrength = 0.0f;
    lastBeatDownbeat = 0.0f;
    beatWrite = 0;
    beatFilled = 0;
    beatSerial = 0;
    downbeatSerial = 0;
    gridSerial = 0;
    tempoRegime = TempoRegime::unknown;
    fastDriftBeats = 0;
    fastDriftLargeBeats = 0;
    fastDriftSign = 0;
    octaveMismatchBeats = 0;
    octaveVoteBpm = 0.0f;
    beatsOnLevel = 0;
    lastFitResidual = 1.0f;
    lastFitCoverage = 0.0f;
    longFitBpm = 0.0f;
    shortFitBpm = 0.0f;
    longWrite = 0;
    longFilled = 0;
    fixedAnchorBpm = 0.0f;
    fixedSamples = 0;
    beatsInRegime = 0;
    fixedErrorBeats = 0;
    std::fill (longHist, longHist + kLongHistory, 0.0f);
    std::fill (beatTime, beatTime + kBeatHistory, 0.0);
    anchorBpm = 0.0f;
    hmm.reset();
    hyp = {};
}

void BeatDecoder::setUserOctave (int octaves) noexcept
{
    const int wanted = std::clamp (octaves, -2, 2);
    if (wanted == octaveShift)
        return;

    // Move the committed tempo with it and start the grid again. Everything
    // measured about the level just left describes a different grid: the beat
    // times are now offbeats (or half of the beats are missing), the fits would
    // be fitting neither, and the regime was decided about a tempo nobody is
    // playing any more.
    const float scale = std::pow (2.0f, static_cast<float> (wanted - octaveShift));
    octaveShift = wanted;
    ++gridSerial;
    if (bpm >= kMinBpm)
        bpm = std::clamp (bpm * scale, kMinBpm, kMaxBpm);

    beatWrite = 0;
    beatFilled = 0;
    longWrite = 0;
    longFilled = 0;
    // The anchor is still a beat time going up an octave and may be an offbeat
    // going down one; either way the fold settles it inside three beats, which
    // is sooner than a fit could be rebuilt to argue about it.
    foldPhaseBeats = 0;
    octaveMismatchBeats = 0;
    octaveVoteBpm = 0.0f;
    beatsOnLevel = 0;
    lastFitResidual = 1.0f;
    lastFitCoverage = 0.0f;
    longFitBpm = 0.0f;
    shortFitBpm = 0.0f;
    enterRegime (TempoRegime::unknown);
}

float BeatDecoder::foldToAnchor (float bpmValue) const noexcept
{
    if (! useAnchor || anchorBpm < kMinBpm || bpmValue < 1.0f)
        return bpmValue;

    float best = bpmValue;
    float bestErr = std::fabs (std::log2 (bpmValue / anchorBpm));
    for (int k = -1; k <= 1; k += 2)
    {
        const float cand = bpmValue * std::pow (2.0f, static_cast<float> (k));
        if (cand < kMinBpm || cand > kMaxBpm)
            continue;
        const float err = std::fabs (std::log2 (cand / anchorBpm));
        // Clearly better, not marginally: an anchor sitting between two octaves
        // must not be able to flip the level back and forth.
        if (err < bestErr - kAnchorHysteresis)
        {
            bestErr = err;
            best = cand;
        }
    }
    return best;
}

float BeatDecoder::applyUserOctave (float bpmValue) const noexcept
{
    if (octaveShift == 0 || bpmValue < 1.0f)
        return bpmValue;

    // Off the end of the reported range the request cannot be honoured, so it
    // is not: a halved tempo below 50 would be reported as its own double
    // anyway, which is the level the listener just asked to leave.
    const float shifted = bpmValue * std::pow (2.0f, static_cast<float> (octaveShift));
    if (shifted < kMinBpm || shifted > kMaxBpm)
        return bpmValue;
    return shifted;
}

void BeatDecoder::pushLongFit (float bpmValue) noexcept
{
    if (bpmValue < kMinBpm || bpmValue > kMaxBpm)
        return;
    longHist[longWrite] = bpmValue;
    longWrite = (longWrite + 1) % kLongHistory;
    if (longFilled < kLongHistory)
        ++longFilled;
}

bool BeatDecoder::longFitSpread (float& spread, float& trend) const noexcept
{
    const int n = std::min (longFilled, kLongHistory);
    if (n < kLongHistoryMin)
        return false;

    float lo = longHist[(longWrite - 1 + kLongHistory) % kLongHistory];
    float hi = lo;
    double sum = 0.0;
    for (int k = 0; k < n; ++k)
    {
        const float v = longHist[(longWrite - 1 - k + kLongHistory) % kLongHistory];
        lo = std::min (lo, v);
        hi = std::max (hi, v);
        sum += v;
    }
    const float mean = static_cast<float> (sum / n);
    if (mean < kMinBpm)
        return false;

    const float newest = longHist[(longWrite - 1 + kLongHistory) % kLongHistory];
    const float oldest = longHist[(longWrite - n + kLongHistory) % kLongHistory];
    spread = (hi - lo) / mean;
    trend = (newest - oldest) / mean;
    return true;
}

void BeatDecoder::enterRegime (TempoRegime r) noexcept
{
    if (r == tempoRegime)
        return;
    tempoRegime = r;
    beatsInRegime = 0;
    fixedErrorBeats = 0;
    fixedAnchorBpm = r == TempoRegime::fixed ? bpm : 0.0f;
    fixedSamples = 0;
}

BeatDecoder::Diagnostics BeatDecoder::diagnostics() const noexcept
{
    Diagnostics d;
    d.combBpm = tempo.bpm();
    d.combSalience = tempo.salience();
    d.shortFitRate = shortFitRate;
    d.longFit = longFitBpm;
    d.shortFit = shortFitBpm;
    d.residual = lastFitResidual;
    d.coverage = lastFitCoverage;
    d.octaveMismatch = octaveMismatchBeats;
    d.beatsHeld = beatsOnLevel;
    d.levelSettled = tempo.levelSettled();
    d.userOctave = octaveShift;
    return d;
}

void BeatDecoder::notifyDiscontinuity (double lostSeconds) noexcept
{
    if (lostSeconds > 0.0)
        timeSec += lostSeconds;

    // No interval spanning the hole is measurable, so the fits must not see one.
    // The grid itself is kept and simply carries on at the committed tempo:
    // `timeSec` was advanced by the hole, so extrapolating across it is right as
    // long as the tempo held, and it is the same reasoning that keeps the tempo.
    // Dropping it would freeze the reported phase until the next peak.
    lastBeatSec = -1.0;
    lastDownbeatSec = -1.0;
    foldPhaseBeats = 0;
    beatWrite = 0;
    beatFilled = 0;

    // The splice is a broadband transient. Keep the peak picker from reading it
    // as a local maximum, and hold off events until fresh frames have arrived.
    prevPulse = 0.0f;
    prevPrevPulse = 0.0f;
    prevDownbeat = 0.0f;
    prevPrevDownbeat = 0.0f;
    refractoryFrames = std::max (refractoryFrames, 3);

    // Evidence chains describe beats that are now gone. Nothing measured before
    // the hole may vouch for the grid afterwards.
    fastDriftBeats = 0;
    fastDriftLargeBeats = 0;
    fastDriftSign = 0;
    octaveMismatchBeats = 0;
    octaveVoteBpm = 0.0f;
    lastFitResidual = 1.0f;
    lastFitCoverage = 0.0f;
    longFitBpm = 0.0f;
    shortFitBpm = 0.0f;
    longWrite = 0;
    longFilled = 0;
}

void BeatDecoder::notifyInputRestart() noexcept
{
    // The two things that decide the metrical level. Both were measuring a
    // room: over forty seconds of room noise at the level the make-up gain
    // hands the network, the fold names a tempo with a salience of 0.29 and
    // calls the level settled, and the state space sits on it with a margin of
    // 8 - which, having a change penalty, it then defends against the music.
    tempo.restartEvidence();
    hmm.reset();
    anchorBpm = 0.0f;
    anchorStrength = 0.0f;

    // The grid and everything fitted to it. The next peak re-anchors, because
    // with no last beat the on-grid gate has nothing to reject against.
    lastBeatSec = -1.0;
    lastDownbeatSec = -1.0;
    gridAnchorSec = -1.0;
    foldPhaseBeats = 0;
    beatWrite = 0;
    beatFilled = 0;
    beatsInBar = 0;
    ++gridSerial;

    // Evidence chains, and the verdict they fed. A tempo called fixed on a room
    // is the most expensive thing to keep: it is designed to be stubborn.
    fastDriftBeats = 0;
    fastDriftLargeBeats = 0;
    fastDriftSign = 0;
    octaveMismatchBeats = 0;
    octaveVoteBpm = 0.0f;
    beatsOnLevel = 0;
    lastFitResidual = 1.0f;
    lastFitCoverage = 0.0f;
    longFitBpm = 0.0f;
    shortFitBpm = 0.0f;
    longWrite = 0;
    longFilled = 0;
    established = false;
    enterRegime (TempoRegime::unknown);
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

bool BeatDecoder::fitPeriod (int maxBeats, float& period, float& residual, float& coverage,
                             double& anchorOut) const noexcept
{
    coverage = 0.0f;
    anchorOut = -1.0;
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

    // The line's position, not only its spacing. Read at the newest beat of the
    // window rather than at its centre: the centre is the better-determined end
    // of a least-squares line, but the phase is wanted *now*, and carrying the
    // centre forward means extrapolating over half the window with whatever
    // error the period has - eleven beats of it for the long fit.
    period = static_cast<float> (slope);
    residual = static_cast<float> (std::sqrt (sumSq / static_cast<double> (keep)) / slope);
    anchorOut = meanT + slope * (idx[keep - 1] - meanIdx);
    return true;
}

float BeatDecoder::gridPhaseNow (float periodSec) const noexcept
{
    if (periodSec <= 0.0f)
        return 0.0f;
    if (gridAnchorSec >= 0.0)
        return wrap01 (static_cast<float> ((timeSec - gridAnchorSec)
                                           / static_cast<double> (periodSec)));
    if (lastBeatSec >= 0.0)
        return wrap01 (static_cast<float> ((timeSec - lastBeatSec)
                                           / static_cast<double> (periodSec)));
    return 0.0f;
}

void BeatDecoder::checkGridPhase (float periodSec) noexcept
{
    // Everything that decides where a beat is goes through the on-grid gate,
    // and the gate measures against the grid itself. So a grid that once
    // anchors on an offbeat is not merely wrong, it is *stable*: every real
    // beat then sits half a beat off it and is rejected as a subdivision,
    // every subdivision lands on it and is kept, and the fits that result are
    // clean. Measured at 168 BPM with an eighth at 0.45 of the beat, the
    // decoder reported 168.00 BPM - exactly right - half a beat out, for
    // ninety seconds, and there was nothing in the chain that could notice.
    //
    // The fold is outside that loop. Folded onto the committed period the
    // activation is tall on the beat and flat half a period later, over the
    // whole buffer and with no gate in front of it, so it can say which half
    // of the beat the grid is on. It is not used for anything finer: eight
    // bins is an eighth of a beat, and the precision stays with the fit.
    if (gridAnchorSec < 0.0 || periodSec <= 0.0f || ! tempo.ready())
    {
        foldPhaseBeats = 0;
        return;
    }

    float contrast = 1.0f;
    const float foldPhase = tempo.beatPhaseFor (bpm, contrast);
    if (foldPhase < 0.0f || contrast > kFoldPhaseContrast)
    {
        // Either the buffer cannot answer, or the material is one where the
        // offbeat really is as loud as the beat - on which the fold has no
        // opinion worth acting on and says so.
        foldPhaseBeats = 0;
        return;
    }

    const double period = static_cast<double> (periodSec);
    const double want = timeSec - static_cast<double> (foldPhase) * period;
    double shift = want - gridAnchorSec;
    shift -= std::round (shift / period) * period;   // the nearest grid, not a later one

    if (std::fabs (shift) < kFoldPhaseThreshold * period)
    {
        foldPhaseBeats = 0;
        return;
    }

    if (++foldPhaseBeats < kFoldPhaseBeats)
        return;

    gridAnchorSec += shift;
    if (lastBeatSec >= 0.0)
        lastBeatSec += shift;   // so the gate starts admitting the beats it was refusing
    foldPhaseBeats = 0;
    ++gridSerial;

    // The beats behind us were the wrong ones. Keeping them would have the fit
    // pulling the grid straight back to where it was, which is the same trap
    // one level down.
    beatWrite = 0;
    beatFilled = 0;
    longWrite = 0;
    longFilled = 0;
    lastFitResidual = 1.0f;
    lastFitCoverage = 0.0f;
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
    const float combBpm = applyUserOctave (foldToAnchor (tempo.bpm()));

    // Acquisition: adopt the fold outright. Easing towards it from a default of
    // 120 is what used to make a 75 BPM song take twenty seconds to find.
    if (! established)
    {
        if (combReady)
        {
            bpm = std::clamp (combBpm, kMinBpm, kMaxBpm);
            established = true;
        }
        else if (useAnchor && hmm.ready() && anchorBpm >= kMinBpm
                 && hmm.levelMargin() > (lineFeed ? kAnchorAcquireMarginLine
                                                  : kAnchorAcquireMargin))
        {
            // The state space is clear about the level and the fold cannot
            // speak yet. It cannot speak for a while, either: it reports
            // nothing until the buffer holds five periods of the octave *below*
            // its winner, which is ten beats - 4.3 s at 140 BPM and 7.9 s at
            // 76, and measured end to end that is the whole of the time to
            // lock. The state space has been accumulating since the first
            // frame and, on real activations, names the right level with a
            // margin at 1.2-1.7 s.
            //
            // What is taken from it is the level and a starting grid, not the
            // number: its periods are whole frames, so it reads about 2% sharp
            // - 120.0 for 118, 142.9 for 140 - which is fine to lock to and not
            // fine to play on. The least-squares fit owns the tempo from the
            // fourth beat, and the fold corrects the level if it disagrees when
            // it finally arrives, at the provisional cost of two beats.
            bpm = std::clamp (applyUserOctave (anchorBpm), kMinBpm, kMaxBpm);
            established = true;
        }
        else if (beatFilled >= 4 && timeSec > kPeakOnlyGraceSec)
        {
            // No usable fold - a very sparse or very noisy activation curve.
            // Fall back to the beat times alone, but only once the fold has had
            // its chance. Beat times alone cannot tell a beat from its own
            // subdivision: a hi-hat on the eighths fits the peaks perfectly at
            // twice the tempo, and a level established that way then has to be
            // argued back out of, one bad octave at a time.
            float period = 0.0f, residual = 0.0f, coverage = 0.0f;
            double anchor = -1.0;
            if (fitPeriod (kShortFit, period, residual, coverage, anchor) && residual < 0.06f)
            {
                bpm = std::clamp (60.0f / period, kMinBpm, kMaxBpm);
                gridAnchorSec = anchor;
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

    int snapBeats = tempoRegime == TempoRegime::fixed ? kOctaveSnapBeatsFixed
                  : tempoRegime == TempoRegime::live  ? kOctaveSnapBeatsLive
                                                      : kOctaveSnapBeats;

    // Before the estimator has enough buffer to have examined the octave below
    // its own winner, the level on offer is the fastest thing the buffer could
    // see. A level adopted then is a guess, and defending a guess is what made
    // a 104 BPM track play at 208 for a quarter of a minute: the estimator
    // corrected itself after seven seconds and the decoder then charged twelve
    // more beats for the privilege. While the level is provisional, a
    // disagreeing comb is believed almost at once.
    if (! tempo.levelSettled())
        snapBeats = kOctaveSnapBeatsProvisional;

    // A grid that still lands tightly on the beats being detected has earned
    // patience - but not a veto, and not an unbounded one. How well the grid
    // fits says nothing about whether it is on the right metrical level: the
    // double of the true tempo lands on every detected peak too, so it is
    // *always* one of the healthy ones. Charging a fixed number of extra beats
    // keeps a glitching comb from costing a good grid without letting a bad
    // level defend itself forever.
    if (gridHealthy && tempo.levelSettled())
        snapBeats += kOctaveSnapBeatsHealthy;

    if (tempo.levelSettled())
        snapBeats += std::min (kOctaveTenureMax, beatsOnLevel / kOctaveTenurePerBeat);

    // Clarity is deliberately *not* a condition. Clarity measures how far the
    // winning period stands above its best rival, and when the current grid is
    // an octave out, that grid *is* the rival - so clarity is low exactly when
    // the disagreement is real, and requiring it made the octave error the
    // thing protecting the octave error.
    //
    // And this is a vote over the last few bars, not a run of consecutive
    // beats. When the level is genuinely ambiguous the comb does not disagree
    // on every single beat, it disagrees on most of them; a counter that reset
    // on the first agreeing beat never got anywhere against that, which is how
    // a level chosen in the first seconds outlived every correction.
    if (combDisagrees && tempo.salience() > kOctaveSnapSalience)
    {
        // A vote is for one specific level. On ambiguous material the comb does
        // not merely disagree with the grid, it disagrees with itself - 208,
        // then 52, then 104, then 208 again - and counting all of that as one
        // accumulating case against the grid hands the clock to whichever level
        // happened to be named on the beat the total came due. Each new level
        // starts its own vote.
        if (octaveVoteBpm < kMinBpm
            || std::fabs (std::log2 (combBpm / octaveVoteBpm)) > kOctaveVoteHold)
        {
            octaveVoteBpm = combBpm;
            octaveMismatchBeats = 1;
        }
        else
        {
            ++octaveMismatchBeats;
        }
    }
    else
    {
        --octaveMismatchBeats;
    }
    octaveMismatchBeats = std::clamp (octaveMismatchBeats, 0, 3 * snapBeats);
    if (octaveMismatchBeats == 0)
        octaveVoteBpm = 0.0f;

    if (octaveMismatchBeats >= snapBeats)
    {
        bpm = std::clamp (combBpm, kMinBpm, kMaxBpm);
        // The beat times go. Keeping them was measured and is worse: at the new
        // level half of them are offbeats, fitPeriod keeps whichever of those
        // happen to land inside its tolerance, and the fits that result are
        // noisier than no fit at all - 0.9 BPM of steady-state spread became
        // 1.7 on the same material.
        ++gridSerial;
        beatWrite = 0;
        beatFilled = 0;
        foldPhaseBeats = 0;
        octaveMismatchBeats = 0;
        octaveVoteBpm = 0.0f;
        beatsOnLevel = 0;
        enterRegime (TempoRegime::unknown);
        fastDriftBeats = 0;
        fastDriftSign = 0;
        longWrite = 0;
        longFilled = 0;
        // Nothing measured about the grid we just left describes the new one, and
        // a stale clean bill of health would let it defend itself immediately.
        lastFitResidual = 1.0f;
        lastFitCoverage = 0.0f;
        return;
    }

    float longPeriod = 0.0f, longResidual = 0.0f, longCoverage = 0.0f;
    float shortPeriod = 0.0f, shortResidual = 0.0f, shortCoverage = 0.0f;
    double longAnchor = -1.0, shortAnchor = -1.0;
    const bool haveLong = fitPeriod (kLongFit, longPeriod, longResidual, longCoverage, longAnchor);
    // How many beats the responsive fit looks back over.
    //
    // This is what actually decides how fast a tempo change is taken, and
    // nothing else came close. Leaving the fixed regime sooner and throwing the
    // poisoned beat history away both looked obviously right and both measured
    // as noise - 5.46 to 5.30 seconds on an 8% step, and one case worse. The
    // reason is arithmetic: a fit over eight beats cannot describe a new tempo
    // until most of those eight beats are at it, and at 120 BPM eight beats is
    // four seconds. The regime and the smoothing are rounding on top of that.
    //
    // And shortening it is a bad trade, measured rather than assumed. Five
    // beats instead of eight: 4.64 -> 4.38, 5.30 -> 4.97 and 16.11 -> 15.09
    // seconds on three steps, 5.25 -> 5.70 on the fourth, and the settled error
    // roughly doubles on every one of them (0.073 -> 0.114, 0.067 -> 0.168,
    // 0.032 -> 0.130). A quarter of a second sooner for twice the wobble
    // afterwards is the wrong way round for an app that is judged on staying in
    // time. Eight stays; the seam and the bench stay with it, so the next
    // person can see the trade instead of re-deriving it.
    const bool haveShort = fitPeriod (kShortFit, shortPeriod, shortResidual,
                                      shortCoverage, shortAnchor);

    // Where the grid is, from the same two fits and for the same reason the
    // tempo comes from them: the phase used to be `lastBeatSec`, one accepted
    // peak, so every beat's own timing error was handed to the clock whole and
    // as a step. Measured against 22 ms of onset jitter the reported phase
    // carried 22 ms rms of it, in jumps of up to 0.18 of a beat. Which of the
    // two fits carries it follows the regime, exactly as the tempo does: a held
    // tempo can average over twenty-four beats, a live one cannot.
    if (tempoRegime == TempoRegime::fixed && haveLong)
        gridAnchorSec = longAnchor;
    else if (haveShort)
        gridAnchorSec = shortAnchor;
    else if (haveLong)
        gridAnchorSec = longAnchor;

    longFitBpm = haveLong ? 60.0f / longPeriod : 0.0f;
    shortFitBpm = haveShort ? 60.0f / shortPeriod : 0.0f;
    if (haveShort)
    {
        if (prevShortFitBpm > kMinBpm)
            shortFitRate += (std::fabs (shortFitBpm - prevShortFitBpm) - shortFitRate)
                            * kShortRateSmoothing;
        prevShortFitBpm = shortFitBpm;
    }
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

    checkGridPhase (60.0f / std::max (kMinBpm, bpm));

    if (! haveShort)
    {
        // Not enough clean beats on the grid; let the fold carry the tempo, and
        // only while the tempo is not being held: a fixed tempo that has already
        // been measured off its own beat times is not improved by a comb whose
        // resolution is a whole frame.
        if (combReady && tempoRegime != TempoRegime::fixed)
            commit (combBpm, kRateAcquiring);
        return;
    }

    ++beatsInRegime;
    ++beatsOnLevel;
    if (haveLong)
        pushLongFit (longFitBpm);

    // Is the long fit standing still, or going somewhere? This is the whole
    // regime decision, and it is deliberately made on the *long* baseline: the
    // short fit is there for responsiveness and is far too noisy on a
    // microphone in a room to certify anything.
    float spread = 0.0f, trend = 0.0f;
    const bool haveWindow = longFitSpread (spread, trend);

    float recent = 0.0f;
    if (recentPeriod (recent))
    {
        const float fastDeviation = (60.0f / recent - bpm) / std::max (kMinBpm, bpm);
        lastFastDeviation = fastDeviation;
        if (std::fabs (fastDeviation) > kFastDriftTolerance)
        {
            const int fastSign = fastDeviation > 0.0f ? 1 : -1;
            if (fastSign == fastDriftSign)
            {
                ++fastDriftBeats;
                if (std::fabs (fastDeviation) > kFastDriftLarge)
                    ++fastDriftLargeBeats;
                else
                    fastDriftLargeBeats = 0;
            }
            else
            {
                fastDriftSign = fastSign;
                fastDriftBeats = 1;
                fastDriftLargeBeats = std::fabs (fastDeviation) > kFastDriftLarge ? 1 : 0;
            }
        }
        else
        {
            fastDriftBeats = 0;
            fastDriftLargeBeats = 0;
            fastDriftSign = 0;
        }
    }

    // Being called fixed is what earns a tempo the right to defend itself
    // against the comb and to stop moving, so it cannot be granted while the
    // comb is still naming a different metrical level, nor while the metrical
    // level itself is still provisional - either would let a bad first guess
    // make itself permanent.
    const bool mayFix = haveWindow
                        && spread < (lineFeed ? kFixedSpreadLine : kFixedSpreadRoom)
                        && lastFitResidual < 0.05f
                        && ! combDisagrees
                        && tempo.levelSettled();

    // A tempo genuinely on the move: the window's ends differ, and by more than
    // the window's own scatter, so this is a direction rather than noise.
    const bool moving = haveWindow
                        && std::fabs (trend) > kLiveTrend
                        && std::fabs (trend) > spread * 0.6f;

    switch (tempoRegime)
    {
        case TempoRegime::unknown:
            if (mayFix)
                enterRegime (TempoRegime::fixed);
            else if (moving)
                enterRegime (TempoRegime::live);
            break;

        case TempoRegime::fixed:
        {
            // Deliberately stubborn. A record cut to a click does not change
            // tempo, so anything that looks like a change here is a fill, a
            // dropout, or a missed beat until it proves otherwise. Three recent
            // intervals agreeing on a direction is that proof, and arrives well
            // before the long fit notices.
            const float anchorError = fixedAnchorBpm > kMinBpm
                                          ? (longFitBpm - fixedAnchorBpm) / fixedAnchorBpm
                                          : 0.0f;
            const bool wandered = haveLong && std::fabs (anchorError) > kLeaveFixedError;
            if (wandered)
                ++fixedErrorBeats;
            else
                fixedErrorBeats = 0;

            // The fast release has to agree with the window before it counts.
            // On its own it is a median of three intervals against a held
            // tempo, and on a microphone in a room three intervals drift 2.4%
            // in one direction often enough to break a fixed tempo out of the
            // regime several times a minute - which is most of the movement
            // left in a tempo that was otherwise correct and still. A real
            // change moves the recent intervals *and* leans the twenty-four
            // beat window the same way; jitter does only the first.
            const bool windowAgrees = haveWindow && fastDriftSign != 0
                                      && trend * static_cast<float> (fastDriftSign) > 0.0f
                                      && std::fabs (trend) > (lineFeed ? kLiveTrend * 0.25f
                                                                      : kLiveTrend)
                                      && (lineFeed || (moving
                                                       && std::fabs (trend) > spread * 0.85f));

            // Three things were tried here to make a tempo change land sooner
            // and none of them shipped. `VPAlign`'s tempo bench measures all of
            // them, so the trade is on record rather than in somebody's memory:
            //
            //   - **Leaving sooner on a line feed** (three beats of a large
            //     deviation instead of five). Measured 5.46 -> 5.30 seconds on
            //     an 8% step, which is noise, and 15.33 -> 16.11 on a 40% one,
            //     which is worse.
            //   - **Dropping the beat history across the step**, on the same
            //     argument the dropout path uses - every interval measured
            //     across it is wrong. Helps the very large step and costs the
            //     small ones eight times the settled error, because the fits
            //     then re-form from too few beats to be precise.
            //   - **A shorter responsive fit**, five beats instead of eight.
            //     A quarter of a second sooner on three cases, slower on a
            //     fourth, and roughly double the settled error on every one.
            //
            // What they have in common is that the cost is not where it looks.
            // A fit over eight beats cannot describe a new tempo until most of
            // those eight beats are at it, and at 120 BPM that is four seconds
            // - everything else is rounding on top. The honest answer is that a
            // *step* costs about five seconds and a band that actually drifts
            // or ramps costs nothing measurable: the same bench has the clock
            // never leaving 2% of an accelerando at all.
            if (beatsInRegime >= kRegimeMinBeats
                && (fixedErrorBeats >= kBeatsToLeaveFixed
                    || (fastDriftBeats >= kFastBeatsToLeaveFixed && windowAgrees)
                    || (fastDriftLargeBeats >= kFastBeatsAlone
                        && (lineFeed || windowAgrees))
                    || (haveLong && std::fabs (anchorError) > 0.06f)))
            {
                enterRegime (TempoRegime::live);
                fixedErrorBeats = 0;
            }
            break;
        }

        case TempoRegime::live:
            if (mayFix && beatsInRegime >= kRegimeMinBeats)
                enterRegime (TempoRegime::fixed);
            break;
    }

    switch (tempoRegime)
    {
        case TempoRegime::fixed:
        {
            // Refinement, not tracking. The anchor is the running mean of the
            // long fit since the tempo was called fixed, so it converges as
            // evidence accumulates instead of following the last fit around;
            // its gain floors low enough that a slow real change still moves it,
            // and it is the anchor - not the fit - that the committed tempo
            // follows. The deadband is what actually stops the number moving:
            // without it the tempo takes a hundredth of a BPM step on every
            // beat forever, which is a clock that is never quite still.
            if (haveLong)
            {
                if (fixedSamples == 0)
                    fixedAnchorBpm = longFitBpm;
                ++fixedSamples;
                const float gain = std::max (kFixedAnchorFloor,
                                             1.0f / static_cast<float> (fixedSamples));
                fixedAnchorBpm += (longFitBpm - fixedAnchorBpm) * gain;
            }
            if (fixedAnchorBpm > kMinBpm)
            {
                const float step = (fixedAnchorBpm - bpm) / std::max (kMinBpm, bpm);
                if (std::fabs (fixedAnchorBpm - bpm) > kFixedDeadband
                    && std::fabs (step) <= kFixedMaxStep)
                    bpm = std::clamp (fixedAnchorBpm, kMinBpm, kMaxBpm);
            }
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
                // Scaling this by how fast the short fit is itself moving -
                // to tell a ramp, where the gap between the fits is a rate,
                // from a step, where it is only the long fit lagging - was
                // tried and cannot work. Measured on this path the short fit's
                // own beat-to-beat movement is 0.5 to 1.15 BPM when the tempo
                // is perfectly still, and a band accelerating from 118 to 126
                // over twelve seconds moves 0.33. The signal is under the
                // noise, by a factor of two, so no threshold on it separates
                // anything. `shortFitRate` is kept because the diagnostic is
                // what showed that, and is not used here.
                const float lead = kLiveLead * (shortFitBpm - longFitBpm);
                target += std::clamp (lead, -0.04f * shortFitBpm, 0.04f * shortFitBpm);
            }
            commit (pullTowardsComb (target, combReady, combBpm), kRateLive);
            break;
        }

        case TempoRegime::unknown:
            commit (pullTowardsComb (haveLong ? longFitBpm : shortFitBpm, combReady, combBpm),
                    kRateAcquiring);
            break;
    }
}

float BeatDecoder::pullTowardsComb (float target, bool combReady, float combBpm) const noexcept
{
    if (! combReady || ! tempo.levelSettled() || target < kMinBpm || combBpm < kMinBpm)
        return target;

    const float rel = (combBpm - target) / target;
    if (std::fabs (rel) < kCombPullThreshold || std::fabs (std::log2 (combBpm / target)) > kOctaveThreshold)
        return target;   // agreed, or a different level entirely - not this path's business

    return target + (combBpm - target) * kCombPull;
}

float BeatDecoder::scoreConfidence() const noexcept
{
    if (! established)
        return std::clamp (0.35f * tempo.salience(), 0.0f, 1.0f);

    // How sure we are that there is a pulse, and that its level is not a coin
    // toss, from whichever of the two level sources can answer. Before the fold
    // has its ten beats of buffer it answers neither, and a tracker waiting for
    // confidence waits for the fold whether or not anything else already knows.
    // The state space answers both at once: its margin is the winning tempo's
    // lead over the tempi that are *not* neighbours of it, which is to say over
    // the other metrical levels.
    const float salience = std::max (std::clamp (tempo.salience() / 0.55f, 0.0f, 1.0f),
                                     anchorStrength);
    const float clarity = std::max (std::clamp (tempo.clarity() / 0.50f, 0.0f, 1.0f),
                                    anchorStrength);
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
    if (useAnchor)
    {
        hmm.push (pulseActivation);
        // Only take the anchor once the state space is clear about it. The
        // margin is the winning tempo's lead over the best tempo that is not a
        // neighbour of it - that is, over the other metrical levels.
        if (hmm.ready() && hmm.levelMargin() > kAnchorMargin)
        {
            anchorBpm = hmm.bpm();
            anchorStrength = std::clamp ((hmm.levelMargin() - kAnchorMargin) / kAnchorMargin,
                                         0.0f, 1.0f);
        }
        else
        {
            // The tempo it last named is kept - the fold is still folded onto
            // it - but a margin that has fallen back is not evidence about
            // anything now, so it stops counting as confidence.
            anchorStrength = 0.0f;
        }
    }

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

        // What the network thought of *this* beat as a candidate for the one,
        // gate or no gate. The peak is picked on max(pBeat, pDownbeat) and the
        // two curves do not always crest on the same frame - a downbeat takes
        // its mass from the beat class, so the downbeat curve can lead or lag
        // by a frame - so the window is three frames wide, which is 60 ms and
        // still well inside the shortest beat this ever runs at.
        lastBeatDownbeat = std::max (prevDownbeat,
                                     std::max (prevPrevDownbeat, pDownbeat));
        if (prevDownbeat > downThresh)
        {
            lastDownbeatStrength = prevDownbeat;
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
    const float phase = gridPhaseNow (newPeriod);

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
    hyp.gridSerial = gridSerial;
    hyp.downbeatStrength = lastDownbeatStrength;
    hyp.beatDownbeat = lastBeatDownbeat;
    hyp.periodSec = newPeriod;
    hyp.regime = tempoRegime;
    hyp.combBpm = tempo.ready() ? applyUserOctave (foldToAnchor (tempo.bpm())) : 0.0f;
    hyp.levelSettled = tempo.levelSettled();
    hyp.fitResidual = lastFitResidual;
    hyp.fitCoverage = lastFitCoverage;
    hyp.confidence = scoreConfidence();
    hyp.valid = established;

    prevPrevPulse = prevPulse;
    prevPulse = pulseActivation;
    prevPrevDownbeat = prevDownbeat;
    prevDownbeat = pDownbeat;
    return hyp;
}

} // namespace vp
