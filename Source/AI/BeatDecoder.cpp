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

    // The full state-space verdict deliberately waits for two complete periods.
    // Acquisition does not need to: once two accurately placed peaks have
    // supplied an interval, the still-provisional state-space winner can choose
    // between that interval and its half/double.  The margin is lower than the
    // committed anchor margin because this grid is explicitly cheap to replace.
    constexpr float kFastAcquireMarginLine = 0.55f;
    constexpr float kFastAcquireMarginRoom = 0.90f;
    constexpr float kFastAcquireMaxLevelError = 0.20f; // octaves

    // The abrupt-change detector.
    //
    // Everything above answers the question "where is this tempo going" over
    // tens of beats, which is what it has to be to tell a record from a band,
    // and it means a step of five to ten percent - a band coming out of a
    // chorus - is not described until most of an eight-beat fit is at the new
    // tempo. Two causal intervals is the earliest anything can say it: one is
    // also what a fill, a flam or a beat the mix swallowed produces.
    //
    // So the smallest change worth suspecting is one BPM, expressed against
    // whatever tempo is committed, or three times the scatter the intervals are
    // already showing, whichever is larger. The second term is the one that
    // does the work: a fifth of a percent on a line feed and three percent
    // through a microphone are both an unchanged tempo, and a fixed threshold
    // either misses the first or fires constantly on the second.
    constexpr float kTransitionMinBpmDelta = 1.0f;

    // And the largest. Beyond a quarter the candidate is not this tempo moving,
    // it is another metrical level or a missed beat - both of which arrive as
    // exact ratios and are the business of the octave machinery above, which
    // has the whole buffer to decide with instead of two intervals.
    constexpr float kTransitionMaxRelativeDelta = 0.25f;

    // How closely the two intervals have to agree before they are one tempo
    // rather than two accidents. A line feed places its peaks to a couple of
    // milliseconds; a microphone in a room does not, and asking a room for line
    // precision means never confirming anything through one.
    constexpr float kTransitionLineCoherence = 0.010f;
    constexpr float kTransitionRoomCoherence = 0.020f;

    // How long a confirmed change stays published. Accepted beats close it at
    // this boundary, while decoder frame time enforces the same maximum through
    // silence, rejected peaks and dropout-like activations. It exists so a
    // consumer can treat these beats differently, and two beats is how long
    // that is worth doing: a longer window would be a second path to the clock
    // running beside the one that is measured.
    constexpr int kTransitionRapidLifetimeBeats = 2;

    // A suspicion nothing follows up. If no eligible peak arrives within this
    // many of the candidate's own periods the evidence is stale - the fill
    // stopped, the input went away - and it is dropped rather than left to be
    // completed by whatever happens next.
    constexpr double kTransitionCandidateStaleBeats = 2.5;

    // The floor under the measured jitter, by source. It is what the estimate
    // cannot go below once it *is* an estimate: a synthetic-clean run must not
    // lower the bar to the point where its own peak-interpolation error becomes
    // a tempo change.
    constexpr float kTransitionJitterFloorLine = 0.005f;
    constexpr float kTransitionJitterFloorRoom = 0.010f;
    constexpr int   kTransitionJitterIntervals = 8;

    // The smallest step this exists for. A band coming out of a chorus moves
    // five to ten percent; below five percent the ordinary live fit is inside a
    // beat or two of right anyway, and nothing here would be worth its risk.
    //
    // It is also the ceiling on how unsteady the material may be. The threshold
    // to suspect anything is three times the measured scatter, so once that
    // exceeds the smallest step worth claiming, every candidate the detector
    // could still start is one it has no business being confident about - and
    // the consequence of being wrong is a moved grid, which is heard. Measured
    // on the 22 ms-scatter bench the intervals run at 6.7% and this stands the
    // detector down completely; a step there costs what it cost before, which
    // is the ordinary five-second path.
    constexpr float kTransitionSmallestStep = 0.05f;

    // An abrupt change has an edge: its first new interval differs from the
    // interval immediately before it. A ramp can drift far enough away from a
    // fixed committed BPM to clear the candidate delta while each adjacent
    // interval changed only a fraction of a percent; treating that accumulated
    // gap as a step bypasses the live fit and re-anchors a grid that never
    // jumped. Three percent stays below the smallest 5% step this path exists
    // to catch and above the adjacent-interval movement measured on the 4 s and
    // 12 s accelerando controls.
    constexpr float kTransitionAbruptEdge = 0.03f;

    // And how many intervals it takes before there is an estimate at all.
    //
    // This is a gate on the detector, not only on the number. Two intervals can
    // only be called a tempo change relative to how much the intervals were
    // already moving, and below this there is no measurement of that - the
    // floor stands in for one, and the floor is the cleanest material there is.
    // Measured on the 22 ms-scatter bench, the intervals really run at sixteen
    // percent, at which nothing this detector can say is worth hearing; it read
    // the one-percent floor instead, on the beats just after a history clear,
    // and confirmed a tempo change that was three dropped beats.
    //
    // Standing down when the scatter is genuinely large needs no separate rule:
    // three times sixteen percent is past the quarter that bounds a candidate,
    // so no interval can qualify.
    constexpr int kTransitionJitterMinIntervals = 4;

    // How loud a candidate peak has to be beside the beats around it, through a
    // microphone, and over how many of them that is judged. Seven tenths of the
    // median admits the ordinary beat-to-beat variation of a room - a mix does
    // not deliver every beat at the same level - and excludes the quiet local
    // maxima a room supplies between them.
    constexpr float kTransitionStrengthFraction = 0.70f;
    constexpr int   kTransitionStrengthBeats = 8;
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
    provisional = false;
    intervalAcquired = false;
    provisionalStrength = 0.0f;
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
    std::fill (beatStrength, beatStrength + kBeatHistory, 0.0f);
    anchorBpm = 0.0f;
    clearTempoTransition (TempoTransitionReason::reset);
    transitionSerial = 0;
    transitionReason = TempoTransitionReason::none;
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
    clearTempoTransition (TempoTransitionReason::reset);
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

    // No interval spans the hole, so neither does any evidence of a change
    // across it - and the splice would supply exactly the sort of odd interval
    // this detector is built to notice.
    clearTempoTransition (TempoTransitionReason::reset);
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
    provisional = false;
    intervalAcquired = false;
    provisionalStrength = 0.0f;
    clearTempoTransition (TempoTransitionReason::reset);
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
    clearTempoTransition (TempoTransitionReason::reset);
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

void BeatDecoder::registerBeat (double beatTimeSec, float strength) noexcept
{
    storeBeatForFit (beatTimeSec, strength);
    ++beatSerial;
}

void BeatDecoder::storeBeatForFit (double beatTimeSec, float strength) noexcept
{
    beatTime[beatWrite] = beatTimeSec;
    beatStrength[beatWrite] = strength;
    beatWrite = (beatWrite + 1) % kBeatHistory;
    if (beatFilled < kBeatHistory)
        ++beatFilled;
    if (transitionRefitBeats > 0)
        --transitionRefitBeats;
}

void BeatDecoder::dropTransitionCandidate (TempoTransitionReason reason) noexcept
{
    transitionState = TempoTransitionState::stable;
    transitionReason = reason;
    transitionFirstSec = -1.0;
    transitionLastSec = -1.0;
    transitionFirstStrength = 0.0f;
    transitionLastStrength = 0.0f;
    transitionPeriodSec = 0.0f;
    transitionConfidence = 0.0f;
    transitionIntervals = 0;
    transitionRapidBeats = 0;
    transitionRapidDeadlineSec = -1.0;
}

void BeatDecoder::clearTempoTransition (TempoTransitionReason reason) noexcept
{
    dropTransitionCandidate (reason);
    // The reference an interval would be measured from as well. At every
    // boundary this is called from, the peak before it and the peak after it
    // are not two ends of one interval.
    transitionPrevEventSec = -1.0;
    transitionPrevStrength = 0.0f;
    // The beats this was waiting for are gone with everything else, and the
    // fold is the right thing to acquire a tempo from again.
    transitionRefitBeats = 0;
}

float BeatDecoder::recentIntervalJitter() const noexcept
{
    const float floorJitter = lineFeed ? kTransitionJitterFloorLine
                                       : kTransitionJitterFloorRoom;
    const int n = std::min (beatFilled - 1, kTransitionJitterIntervals);
    if (n < kTransitionJitterMinIntervals)
        return floorJitter;

    float ioi[kTransitionJitterIntervals];
    for (int k = 0; k < n; ++k)
    {
        const int newer = (beatWrite - 1 - k + kBeatHistory) % kBeatHistory;
        const int older = (beatWrite - 2 - k + kBeatHistory) % kBeatHistory;
        const float raw = static_cast<float> (beatTime[newer] - beatTime[older]);
        if (raw <= 0.0f)
            return floorJitter;
        ioi[k] = raw;
    }

    // Both the centre and the spread are medians, and the centre is the half of
    // that which is easy to miss.
    //
    // One beat the mix swallowed makes one interval twice the others. Taking the
    // median of the deviations already stops *that* interval from being the
    // answer - but if the deviations are measured from the mean, the doubled
    // interval has moved the mean, so every ordinary interval is now 11% away
    // from it and the median deviation is 11% rather than nothing. Three times
    // that is past `kTransitionSmallestStep`, so a single swallowed beat stood
    // the detector down for the whole eight-interval window: measured on a
    // 120-to-132 step four beats later it was not detected within its two-beat
    // budget at all, and arrived 3.2 s late.
    //
    // A median centre does not move for one outlier, so the ordinary intervals
    // deviate from it by nothing and the swallowed beat is what it is: one
    // interval out of eight.
    float sorted[kTransitionJitterIntervals];
    std::copy (ioi, ioi + n, sorted);
    std::sort (sorted, sorted + n);
    const float centre = sorted[n / 2];
    if (centre <= 0.0f)
        return floorJitter;

    float dev[kTransitionJitterIntervals];
    for (int k = 0; k < n; ++k)
        dev[k] = std::fabs (ioi[k] - centre) / centre;
    std::sort (dev, dev + n);
    return std::max (floorJitter, dev[n / 2]);
}

float BeatDecoder::recentBeatStrengthMedian() const noexcept
{
    const int n = std::min (beatFilled, kTransitionStrengthBeats);
    if (n < kTransitionJitterMinIntervals)
        return 0.0f;

    float s[kTransitionStrengthBeats];
    for (int k = 0; k < n; ++k)
        s[k] = beatStrength[(beatWrite - 1 - k + kBeatHistory) % kBeatHistory];
    std::sort (s, s + n);
    return s[n / 2];
}

bool BeatDecoder::transitionCandidateAllowed (float candidatePeriodSec) const noexcept
{
    if (candidatePeriodSec <= 0.0f)
        return false;
    const float candidateBpm = applyUserOctave (60.0f / candidatePeriodSec);
    if (candidateBpm < kMinBpm || candidateBpm > kMaxBpm)
        return false;
    // Inside the metrical level in use. A period that is not is a subdivision
    // or a missed beat wearing a tempo's clothes, and the octave machinery -
    // which has the whole fold buffer rather than two intervals - owns that
    // question.
    return bpm >= kMinBpm
           && std::fabs (std::log2 (candidateBpm / bpm)) <= kOctaveThreshold;
}

bool BeatDecoder::observeTempoTransition (double eventTimeSec, float strength,
                                          bool acceptedByCurrentGrid) noexcept
{
    const double prevEvent = transitionPrevEventSec;
    const float prevStrength = transitionPrevStrength;

    // A change already confirmed is not re-argued; it is spent, on the beats
    // that follow it, and then it is over.
    if (transitionState == TempoTransitionState::rapid)
    {
        if (acceptedByCurrentGrid
            && ++transitionRapidBeats >= kTransitionRapidLifetimeBeats)
            dropTransitionCandidate (TempoTransitionReason::expired);
        return false;
    }

    // Nor is it re-argued in the beats after that, until the ordinary fits have
    // actually re-formed at the tempo it moved to.
    //
    // `rapid` is two beats because that is how long a *consumer* should treat
    // the tempo as in flight. It is not how long the decoder needs, and closing
    // this window with it produced a second confirmation of the same target on
    // every line-feed step measured: confirming forces the live regime and
    // empties the fit history down to the two peaks that measured the change, so
    // for the next several beats the fold - whose buffer is seconds long and
    // still describes the tempo that was left - is the strongest thing naming a
    // tempo. It pulls the committed BPM back, the next interval at the new tempo
    // reads as a fresh change against it, and the detector confirms 132 a second
    // time having just confirmed 132. Both name the right tempo, so nothing
    // sounded wrong, but `transitionSerial` moved twice for one musical event
    // and a follower keyed off the serial re-adopts on each.
    //
    // A short fit is eight beats, so eight accepted beats is what is owed. By
    // then the fits carry the new tempo themselves and the question the detector
    // answers has an honest reference again.
    if (transitionRefitBeats > 0)
    {
        if (transitionState == TempoTransitionState::suspected)
            dropTransitionCandidate (TempoTransitionReason::expired);
        return false;
    }

    // No reference interval, or no measurement of how much the intervals were
    // already moving. The second is the important one and it is easy to get
    // backwards: with too little history `recentIntervalJitter` answers with
    // its floor, which is the *cleanest* material there is, so the detector
    // would be at its most credulous exactly where it knows least - on the
    // beats right after a grid change, a dropout or its own last decision.
    if (prevEvent < 0.0 || eventTimeSec <= prevEvent
        || beatFilled - 1 < kTransitionJitterMinIntervals)
    {
        if (transitionState == TempoTransitionState::suspected)
            dropTransitionCandidate (TempoTransitionReason::incoherent);
        return false;
    }

    const float interval = static_cast<float> (eventTimeSec - prevEvent);
    const float jitter = recentIntervalJitter();

    // Too unsteady to read. See kTransitionSmallestStep: past here the smallest
    // change the detector would be willing to suspect is larger than the
    // largest one it exists to catch, so it has nothing useful left to say and
    // saying it anyway costs a grid move on a passage that never changed tempo.
    if (3.0f * jitter > kTransitionSmallestStep)
    {
        if (transitionState == TempoTransitionState::suspected)
            dropTransitionCandidate (TempoTransitionReason::incoherent);
        return false;
    }

    // Second interval of a live candidate: the one that decides it.
    if (transitionState == TempoTransitionState::suspected
        && transitionFirstSec >= 0.0
        && std::fabs (prevEvent - transitionLastSec) < 1.0e-9)
    {
        const float first = static_cast<float> (transitionLastSec - transitionFirstSec);
        const float mean = 0.5f * (first + interval);
        const float tolerance = std::max (lineFeed ? kTransitionLineCoherence
                                                   : kTransitionRoomCoherence,
                                          2.0f * jitter);
        const float deviation = mean > 0.0f
                                    ? 0.5f * std::fabs (first - interval) / mean
                                    : 1.0f;

        // Through a microphone, also insist the peaks were really peaks.
        //
        // A room supplies quiet local maxima all the time - a chair, a
        // reflection, the tail of the last stroke - and two of those happening
        // to be evenly spaced is the one way this detector can be talked into a
        // tempo nobody played. A line feed has no such supply and is not charged
        // for it.
        //
        // Measuring that against `beatThresh` alone, as this first did, tested
        // nothing: a peak only reaches here by clearing `beatThresh` in the
        // local-maximum gate, so the condition could not fail. What separates a
        // reflection from a beat is not that it clears an absolute floor - it
        // does - but how loud it is beside the beats around it, so the level is
        // relative to the median of the recent accepted ones and the absolute
        // floor is only the lower bound on it.
        const float strengthFloor = std::max (beatThresh,
                                              kTransitionStrengthFraction
                                                  * recentBeatStrengthMedian());
        const bool strongEnough = lineFeed
                                  || (strength >= strengthFloor
                                      && prevStrength >= strengthFloor
                                      && transitionFirstStrength >= strengthFloor);

        // And the change has to survive being measured properly. What opened
        // the candidate was one interval against the committed period, which is
        // the noisiest estimate available: an interval is a single beat's timing
        // error away from the truth, so on ordinary material it clears a 3%
        // threshold regularly without the band having done anything. `mean` is
        // that estimate averaged over both intervals of the candidate and is
        // the better one, so it is asked the same question again before any of
        // this is acted on.
        //
        // Without this the detector confirms its own jitter. Measured on the
        // 168 BPM song in the octave sweep it declared a change to 169.9 - 1.1%,
        // a third of the threshold that started it - and the confirmation puts
        // the decoder into `live`, which is what had been holding that song at
        // the level it is played. Two seconds later it was reading 84.
        const float meanBpm = applyUserOctave (60.0f / std::max (1.0e-6f, mean));
        const float meanDelta = std::fabs (meanBpm - bpm) / std::max (kMinBpm, bpm);
        const float meanNeeded = std::max (kTransitionMinBpmDelta / std::max (kMinBpm, bpm),
                                           3.0f * jitter);

        if (deviation <= tolerance && strongEnough && meanDelta >= meanNeeded
            && transitionCandidateAllowed (mean))
        {
            // The two peaks that measured this are behind us and the fits would
            // otherwise have to re-form over eight beats of the tempo just
            // left. They go into the history; the beat *event* for the current
            // peak is still `registerBeat`'s to announce, so nothing downstream
            // is handed a stroke for a beat that already sounded.
            beatWrite = 0;
            beatFilled = 0;
            longWrite = 0;
            longFilled = 0;
            storeBeatForFit (transitionFirstSec, transitionFirstStrength);
            storeBeatForFit (transitionLastSec, transitionLastStrength);

            bpm = std::clamp (applyUserOctave (60.0f / mean), kMinBpm, kMaxBpm);
            gridAnchorSec = eventTimeSec;
            // The phase evidence behind us describes the old spacing, and the
            // old-grid vote does too.
            foldPhaseBeats = 0;
            fastDriftBeats = 0;
            fastDriftLargeBeats = 0;
            fastDriftSign = 0;
            enterRegime (TempoRegime::live);

            transitionState = TempoTransitionState::rapid;
            transitionReason = TempoTransitionReason::confirmed;
            // Published in the tempo the rest of the hypothesis is reported in,
            // half/double request included, so a consumer can hand it straight
            // to a clock. `mean` is a measured interval and is not that.
            transitionPeriodSec = 60.0f / bpm;
            transitionIntervals = 2;
            transitionConfidence = tolerance > 0.0f
                                       ? std::clamp (1.0f - deviation / tolerance, 0.0f, 1.0f)
                                       : 0.0f;
            transitionRapidBeats = 0;
            const float reportedBpm = transitionPeriodSec > 0.0f
                                          ? 60.0f / transitionPeriodSec
                                          : 0.0f;
            transitionRapidDeadlineSec =
                std::isfinite (reportedBpm)
                    && reportedBpm >= kMinBpm && reportedBpm <= kMaxBpm
                    && std::isfinite (transitionPeriodSec)
                ? timeSec + static_cast<double> (kTransitionRapidLifetimeBeats)
                                * static_cast<double> (transitionPeriodSec)
                : timeSec;
            // Set after the two retained peaks above, so they do not spend the
            // budget they are part of paying off.
            transitionRefitBeats = kShortFit;
            transitionLastSec = eventTimeSec;
            transitionLastStrength = strength;
            ++transitionSerial;
            return true;
        }

        // Not one tempo. The interval just measured may still be the start of a
        // better candidate, so fall through rather than waiting a beat.
        dropTransitionCandidate (TempoTransitionReason::incoherent);
    }

    // Start (or restart) a candidate.
    //
    // Measured in tempo rather than in period, because `bpm` carries the
    // listener's half/double request and a measured interval does not. Compared
    // raw, a listener on double time would see every ordinary interval as a
    // hundred percent change and this detector would be switched off for them.
    const float candidateBpm = applyUserOctave (60.0f / interval);
    const float delta = std::fabs (candidateBpm - bpm) / std::max (kMinBpm, bpm);
    const float needed = std::max (kTransitionMinBpmDelta / std::max (kMinBpm, bpm),
                                   3.0f * jitter);
    if (delta < needed)
    {
        if (transitionState == TempoTransitionState::suspected)
            dropTransitionCandidate (TempoTransitionReason::incoherent);
        return false;
    }
    if (beatFilled >= 2)
    {
        const int newest = (beatWrite - 1 + kBeatHistory) % kBeatHistory;
        const int previous = (beatWrite - 2 + kBeatHistory) % kBeatHistory;
        const float priorInterval =
            static_cast<float> (beatTime[newest] - beatTime[previous]);
        const float edgeDelta = priorInterval > 0.0f
                                  ? std::fabs (interval - priorInterval) / priorInterval
                                  : 0.0f;
        if (edgeDelta < kTransitionAbruptEdge)
            return false;
    }
    if (delta > kTransitionMaxRelativeDelta)
    {
        dropTransitionCandidate (TempoTransitionReason::outsideRange);
        return false;
    }
    if (! transitionCandidateAllowed (interval))
    {
        dropTransitionCandidate (TempoTransitionReason::metricalConflict);
        return false;
    }

    transitionState = TempoTransitionState::suspected;
    transitionReason = TempoTransitionReason::candidateStarted;
    transitionFirstSec = prevEvent;
    transitionLastSec = eventTimeSec;
    transitionFirstStrength = prevStrength;
    transitionLastStrength = strength;
    transitionPeriodSec = 60.0f / candidateBpm;
    transitionIntervals = 1;
    transitionConfidence = 0.0f;
    transitionRapidBeats = 0;
    return false;
}

bool BeatDecoder::tryFastAcquire() noexcept
{
    // A period is not present in one isolated event. Two events are the first
    // instant at which a causal system can measure one; through a microphone we
    // ask for a third event because a reflection/transient pair is common. A
    // direct feed has no acoustic echo and can use the first interval.
    const int minimum = lineFeed ? 2 : 3;
    if (! useAnchor || beatFilled < minimum || hmm.bpm() < kMinBpm)
        return false;

    const float margin = hmm.levelMargin();
    const float required = lineFeed ? kFastAcquireMarginLine : kFastAcquireMarginRoom;

    const int newest = (beatWrite - 1 + kBeatHistory) % kBeatHistory;
    const int older = (beatWrite - 2 + kBeatHistory) % kBeatHistory;
    const float raw = static_cast<float> (beatTime[newest] - beatTime[older]);
    if (raw <= 0.0f)
        return false;

    const float rawBpm = 60.0f / raw;
    const bool fastOctaveAmbiguous = rawBpm > 145.0f && rawBpm * 0.5f >= kMinBpm;
    if (fastOctaveAmbiguous && beatFilled < 3)
        return false;

    // A peak interval may be the pulse or a subdivision. Keep both causal
    // readings alive and let the accumulated state-space path choose the level.
    // Do not infer a *missed* beat here: two consecutive intervals at the same
    // spacing are evidence that this spacing exists, while doubling a slow
    // pulse on the strength of the early 120-BPM prior is exactly how 76 BPM
    // was briefly reported as 152. A genuinely missing-beat sequence is left
    // to the longer estimator, which has enough context to prove it.
    const float level = hmm.bpm();
    float bestPeriod = 0.0f;
    float bestError = 99.0f;
    bool intervalSelfSufficient = false;
    constexpr float scales[] = { 1.0f, 2.0f };
    for (float scale : scales)
    {
        const float candidatePeriod = raw * scale;
        const float candidateBpm = 60.0f / candidatePeriod;
        if (candidateBpm < kMinBpm || candidateBpm > kMaxBpm)
            continue;
        const float error = std::fabs (std::log2 (candidateBpm / level));
        if (error < bestError)
        {
            bestError = error;
            bestPeriod = candidatePeriod;
        }
    }
    // Below 90 BPM, doubling a clean sequence merely because the very young
    // state space is still near its 118-BPM prior is a known error (76 -> 152).
    // With no intervening peaks the observed spacing is the only causal fact,
    // so prefer it and let the long estimator revisit missed-beat material.
    if (rawBpm < 90.0f && rawBpm >= kMinBpm)
    {
        bestPeriod = raw;
        bestError = 0.0f;
        intervalSelfSufficient = true;
    }

    // At the opposite end, three alternating peak heights are direct evidence
    // that the short spacing is a subdivision: strong-weak-strong (or its
    // inverse) repeats only after two intervals. This resolves slow music with
    // loud eighths without making genuinely fast, evenly weighted music wait
    // for the long fold.
    if (fastOctaveAmbiguous && beatFilled >= 3)
    {
        const int oldest = (beatWrite - 3 + kBeatHistory) % kBeatHistory;
        const float a = beatStrength[oldest];
        const float b = beatStrength[older];
        const float c = beatStrength[newest];
        const float ends = 0.5f * (a + c);
        const bool endsAgree = std::fabs (a - c) < 0.16f * std::max (0.1f, ends);
        const bool alternates = endsAgree
                                && std::fabs (b - ends) > 0.18f * std::max (0.1f, ends);
        if (alternates)
        {
            bestPeriod = raw * 2.0f;
            bestError = 0.0f;
            intervalSelfSufficient = true;
        }
    }

    if (margin < required && ! intervalSelfSufficient)
        return false;

    if (bestPeriod <= 0.0f || bestError > kFastAcquireMaxLevelError)
        return false;

    // In the room path, make the two measured intervals corroborate the same
    // grid. This rejects a pair made from an impact and its reflection without
    // adding a bar-sized observation window.
    if (! lineFeed)
    {
        const int oldest = (beatWrite - 3 + kBeatHistory) % kBeatHistory;
        const float previousRaw = static_cast<float> (beatTime[older] - beatTime[oldest]);
        if (previousRaw <= 0.0f)
            return false;
        const float ratio = previousRaw / raw;
        if (std::fabs (ratio - 1.0f) > 0.14f)
            return false;
    }

    bpm = std::clamp (applyUserOctave (60.0f / bestPeriod), kMinBpm, kMaxBpm);
    gridAnchorSec = beatTime[newest];
    established = true;
    provisional = true;
    intervalAcquired = true;
    provisionalStrength = std::clamp ((margin - required) / 2.0f + 0.55f, 0.55f, 0.90f);
    return true;
}

void BeatDecoder::updateTempo() noexcept
{
    const bool combReady = tempo.ready() && tempo.salience() > kSalienceFloor;
    const float combBpm = applyUserOctave (foldToAnchor (tempo.bpm()));

    // Acquisition: adopt the fold outright. Easing towards it from a default of
    // 120 is what used to make a 75 BPM song take twenty seconds to find.
    if (! established)
    {
        if (combReady && tempo.levelSettled())
        {
            // `ready()` only says that one candidate period is measurable.
            // Before `levelSettled()` the buffer has not yet held enough audio
            // to test that candidate's slower octave, so adopting it outright
            // is how loud eighths at 76 BPM briefly became 152 BPM. The fast
            // interval/HMM path below is specifically built for this interval.
            bpm = std::clamp (combBpm, kMinBpm, kMaxBpm);
            established = true;
            provisional = false;
            intervalAcquired = false;
        }
        else if (tryFastAcquire())
        {
            // Interpolated event times provide the precise period; the state
            // path only chooses its metrical level. This precedes adopting the
            // state-space number because an alternating eighth pattern contains
            // more direct level evidence than its whole-frame early winner.
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
            provisional = true;
            intervalAcquired = false;
            provisionalStrength = std::max (0.55f, anchorStrength);
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
                provisional = true;
                intervalAcquired = true;
                provisionalStrength = 0.50f;
            }
        }
        if (! established)
            return;
    }

    // The short-window grid is allowed to start the clock; it becomes an
    // ordinary committed grid only when the long-window fold has actually
    // examined the slower octave. Until then the correction logic below keeps
    // its shorter tenure.
    if (provisional && combReady && tempo.levelSettled())
    {
        provisional = false;
        provisionalStrength = 0.0f;
    }

    // The comb owns the metrical level. If the committed tempo has left its
    // octave, something changed underneath us - a new song, a half-time section,
    // or an octave we got wrong on acquisition - and the fit is now fitting the
    // wrong grid. Re-anchor, but only on repeated, confident disagreement: a
    // single bad comb frame must not cost us the beat history.
    const bool gridHealthy = lastFitResidual < kGridHealthyResidual
                             && lastFitCoverage > kGridHealthyCoverage;
    const bool combMayCorrect = tempo.levelSettled()
                                || (provisional && ! intervalAcquired);
    // Direction matters. A grid twice too fast can look healthy because every
    // detected beat lands on every other tick. A grid twice too slow cannot:
    // it would have to discard every other event. If this grid was built from
    // actual intervals and still covers those events tightly, a late 120 -> 60
    // fold is not evidence against it. This is the room-then-band failure that
    // otherwise appeared fourteen seconds after a correct lock.
    const bool unprovenSlowerOctave = intervalAcquired && gridHealthy
                                      && combBpm < bpm * 0.70f;
    const bool combDisagrees = combReady && combMayCorrect && ! unprovenSlowerOctave
                               && bpm > kMinBpm
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
        clearTempoTransition (TempoTransitionReason::reset);
        beatWrite = 0;
        beatFilled = 0;
        intervalAcquired = false;
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
        // resolution is a whole frame. The same applies to a newly measured
        // interval: before the fold has examined the slower octave, pulling the
        // exact first-quarter measurement towards it recreates the multi-second
        // acquisition this path exists to remove.
        //
        // And not across a change that has just been confirmed. The fold's
        // buffer is seconds long, so for the first beats after a step it is
        // still describing the tempo that has been left - measured on a 120 to
        // 132 step it names 120 for several seconds - and pulling seven tenths
        // of the way towards it every beat would undo the measurement that has
        // just been made from the two intervals that actually carry the change.
        // The window closes no later than two reported periods, even if silence
        // or off-grid peaks mean no accepted beat arrives to close it sooner.
        const bool foldMayPull = ! provisional || ! intervalAcquired
                                 || tempo.levelSettled();
        if (combReady && foldMayPull && tempoRegime != TempoRegime::fixed
            && transitionState != TempoTransitionState::rapid)
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

    if (provisional)
    {
        // Enough to enter LOCKING, deliberately below a mature grid. Input
        // level and event count are still enforced by BeatTracker, so this is
        // not permission for silence or one transient to start the part.
        const float events = std::clamp (static_cast<float> (beatFilled) / 4.0f, 0.0f, 1.0f);
        return std::clamp (0.20f + 0.34f * provisionalStrength + 0.12f * events,
                           0.0f, 0.62f);
    }

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

    // This is deliberately independent of peak eligibility. A confirmed
    // transition must publish for the confirmation frame so the audio thread
    // can consume its serial once, then become stable no later than two periods
    // of the reported (user-octaved) tempo even through complete silence.
    if (transitionState == TempoTransitionState::rapid
        && std::isfinite (transitionRapidDeadlineSec)
        && transitionRapidDeadlineSec >= 0.0
        && timeSec >= transitionRapidDeadlineSec)
        dropTransitionCandidate (TempoTransitionReason::expired);

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

    // A completed causal maximum that is far enough from the last beat to be a
    // separate event. This is everything the analysis knows about; what the
    // grid then makes of it is a second question, and the abrupt-change
    // detector has to be asked the first one - on a step large enough to
    // matter, every peak carrying the evidence is one the grid rejects.
    const bool eligiblePeak = localMaximum && refractoryFrames == 0
                              && (lastBeatSec < 0.0
                                  || (eventTimeSec - lastBeatSec)
                                         >= 0.4 * static_cast<double> (period));

    bool acceptedByCurrentGrid = eligiblePeak;
    if (acceptedByCurrentGrid && established && lastBeatSec >= 0.0)
    {
        const double beats = (eventTimeSec - lastBeatSec) / static_cast<double> (period);
        if (std::fabs (beats - std::round (beats)) > kOnGridTolerance && beats < kGridStaleBeats)
            acceptedByCurrentGrid = false;
    }

    // A change is a change *from* something, so the grid has to be one. While
    // it is still provisional the committed tempo is a state-space reading with
    // whole-frame periods - about 2% sharp - and the first accurate intervals
    // legitimately disagree with it by more than this detector's threshold.
    // Measured on a 128 BPM line feed it confirmed a "change" to 128 at 1.42 s,
    // which is the acquisition path being reported as a tempo step. Acquisition
    // is the ordinary machinery's job and it is already fast.
    const bool confirmedTransition =
        eligiblePeak && established && ! provisional
        && observeTempoTransition (eventTimeSec, prevPulse, acceptedByCurrentGrid);

    if (eligiblePeak)
    {
        transitionPrevEventSec = eventTimeSec;
        transitionPrevStrength = prevPulse;
    }
    else if (transitionState == TempoTransitionState::suspected
             && transitionLastSec >= 0.0
             && transitionPeriodSec > 0.0f
             && timeSec - transitionLastSec
                    > kTransitionCandidateStaleBeats * static_cast<double> (transitionPeriodSec))
    {
        // Nothing came to finish the argument. One changed interval on its own
        // is what a fill leaves behind, and it does not get to wait for whatever
        // happens next to complete it.
        dropTransitionCandidate (TempoTransitionReason::expired);
    }

    const bool peak = acceptedByCurrentGrid || confirmedTransition;

    if (peak)
    {
        registerBeat (eventTimeSec, prevPulse);
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
    hyp.transitionState = transitionState;
    hyp.transitionReason = transitionReason;
    hyp.transitionBpm = transitionPeriodSec > 0.0f ? 60.0f / transitionPeriodSec : 0.0f;
    hyp.transitionConfidence = transitionConfidence;
    hyp.transitionIntervals = transitionIntervals;
    hyp.transitionSerial = transitionSerial;
    hyp.confidence = scoreConfidence();
    hyp.valid = established;

    prevPrevPulse = prevPulse;
    prevPulse = pulseActivation;
    prevPrevDownbeat = prevDownbeat;
    prevDownbeat = pDownbeat;
    return hyp;
}

} // namespace vp
