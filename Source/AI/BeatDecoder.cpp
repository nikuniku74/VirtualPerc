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

    // The bar-cadence octave corrector's histogram: how much it forgets per
    // accepted beat, and how much decayed evidence it needs before the shares
    // below mean anything. Same decay as BeatTracker.cpp's kVoteDecay and the
    // same reasoning: recent evidence must be able to overrule stale evidence
    // within one section of a song, not carry the whole take, and sixteen
    // accepted beats is the two-consecutive-bar span the discrete version of
    // this corrector used to require before it acts.
    constexpr float kCadenceDecay = 0.982f;
    constexpr float kCadenceBeatsToDecide = 16.0f;

    // How empty the weaker half of the histogram has to be before the grid is
    // called one octave too fast. The two interleaved sets of four slots hold
    // the two alternating classes of accepted beat; at the wrong level one of
    // them is hi-hat eighths, which have next to nothing in the low bands,
    // while at the right level both are real quarters - a kick set and a snare
    // set, and a snare has body. So the test is the depth of that alternation,
    // not its presence: presence alone cannot tell the two apart.
    //
    // Measured on the probe bench, mixer feed, steady state:
    //
    //     low bands   6      12     24
    //     50 BPM      0.53   0.50   0.43   <- the reading that is wrong
    //     100 BPM     0.60   0.73   0.85   <- the reading that is right
    //
    // Twenty-four bands is what opens the gap, and it opens it for a reason:
    // six bands reach only the kick fundamental, where a snare looks nearly as
    // empty as a hi-hat and the two readings converge. Twenty-four reach the
    // snare's body, so at the right level both halves are full and the depth
    // goes to nearly one, while a hi-hat still has little to put there.
    //
    // At 0.55 the bench does exactly what it should: the 50 BPM groove reads 50
    // instead of 100, and 76, 100, 118, 132, 140 and the syncopated and pad
    // styles are all untouched.
    //
    // And it is still wrong, which is why it is off. Half-time material at 100
    // BPM - snare on three, nothing on two and four - builds the same
    // histogram: the slots between the hits are as empty as hi-hat slots, so
    // the test halves a correct 100 into a wandering 60. That is not a
    // threshold that needs moving. A straight groove at 50 and a half-time
    // groove at 100 are the *same sound* - kick, hat, snare, hat, at the same
    // spacing, with the same low end under the same slots - so nothing measured
    // from the audio can separate them. Which of the two a listener calls "the
    // tempo" is a convention, and this app already implements one: the reported
    // range here and the octave bounds in BeatTracker.cpp. Deciding it the
    // other way needs something from outside the audio - a tap, or a control
    // the player can flip.
    //
    // Left in place, switched off, with the machinery that measured it: the
    // question is open (docs/TODO.md item 1) and this is the bench that answers
    // it in one command. Do not turn it on without reading
    // docs/HANDOFF_OCTAVE_50BPM.md first.
    constexpr bool  kCadenceCorrectionEnabled = false;
    constexpr float kCadenceHatDepth = 0.55f;

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
    // How near a whole number of octaves a disagreement has to be before it
    // counts as an argument about the metrical level at all. See the two
    // allowances in `updateTempo`.
    constexpr float kOctaveArgumentTolerance = 0.15f;
    constexpr float kOctaveSnapSalience = 0.22f;

    // How far the comb may move and still be casting the same vote.
    constexpr float kOctaveVoteHold = 0.10f;

    // The stale-grid watchdog (docs/TODO.md item 19).
    //
    // The octave snap above is looking for a *metrical level*, so it only fires
    // past kOctaveThreshold - roughly a fifth away. Measured on an abrupt
    // 120 -> 160 step, the committed grid stays on 120 and the comb settles at
    // 106.7: eleven percent out, under that bar, so nothing fires and the
    // decoder reports 120.00 at confidence 1.00 for as long as it is left
    // running. The grid rejects the real beats as off-grid and fits the
    // survivors cleanly, so residual, coverage and salience all stay healthy;
    // the comb is the only thing outside that loop and the only thing that knows.
    //
    // So: a lower bar than the octave snap, and a longer vote to pay for it.
    // The threshold has to clear the wobble healthy material shows at the top of
    // the range - measured at 1.9 per cent mean error at 170 BPM - without
    // reaching the 17 per cent of the stuck case. Deliberately above
    // kCombPullThreshold too: inside that band the ordinary pull owns the
    // question and this must not race it.
    constexpr float kStaleGridThreshold = 0.120f;   // log2, about 8.7 per cent
    constexpr float kStaleGridRelease   = 0.060f;   // hysteresis: it must actually resolve
    constexpr int   kStaleGridVoteBeats = 12;       // ~4.5 s at 160 BPM
    constexpr float kStaleGridVoteHold  = 0.10f;

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
    // Above this the state space keeps a veto over the interval branches that
    // waive the level check. See `tryFastAcquire`; measured at 180 because the
    // one regression that must survive is 168 acquiring on the interval alone.
    constexpr float kFastAcquireVetoBpm = 180.0f;
    constexpr int   kContextAcquirePeaksLine = 3;
    constexpr int   kContextAcquirePeaksRoom = 4;

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
    std::fill (cadenceHist, cadenceHist + 8, 0.0f);
    cadenceHistBeats = 0.0f;
    cadenceAnchorSec = -1.0;
    cadenceDownbeatBeatSerial = 0;
    cadenceOctaveCandidate = octaveShift;
    cadenceOctaveVotes = 0;
    metricalOctaveHint = 0;
    metricalOctaveHintValid = false;
    tempoRegime = TempoRegime::unknown;
    fastDriftBeats = 0;
    fastDriftLargeBeats = 0;
    fastDriftSign = 0;
    octaveMismatchBeats = 0;
    octaveVoteBpm = 0.0f;
    staleGridBeats = 0;
    staleGridBpm = 0.0f;
    beatsOnLevel = 0;
    lastFitResidual = 1.0f;
    lastFitCoverage = 0.0f;
    lastFitIndexGap = 1.0f;
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

    const bool cadenceCorrection = metricalOctaveHintValid
                                   && wanted == metricalOctaveHint
                                   && wanted < octaveShift
                                   && lastDownbeatSec >= 0.0;

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

    // The bar cadence did more than identify the level: it identified a true
    // quarter-grid anchor. Reusing the most recent fast-grid beat here can put
    // the new 50 BPM grid on a hi-hat offbeat, after which the on-grid gate
    // faithfully rejects every quarter. Only the cadence-backed path has this
    // stronger phase evidence; manual and range-based octave changes keep the
    // established generic behaviour below.
    if (cadenceCorrection)
    {
        gridAnchorSec = lastDownbeatSec;
        lastBeatSec = lastDownbeatSec;
        beatsInBar = 0;
    }

    beatWrite = 0;
    beatFilled = 0;
    longWrite = 0;
    longFilled = 0;
    // Every bin of the low-band histogram describes a phase mod eight accepted
    // beats *of the grid that just left*. Carrying it into the new grid would
    // fit a pattern against beats that are no longer where it measured them -
    // offbeats now, if the level just went up, or beats that no longer exist as
    // separate events if it went down. The downbeat-spacing counter and the
    // hint itself are deliberately *not* cleared here: the hint is what the
    // caller is acting on as it calls this, and the caller reads it back.
    std::fill (cadenceHist, cadenceHist + 8, 0.0f);
    cadenceHistBeats = 0.0f;
    cadenceAnchorSec = -1.0;
    // The anchor is still a beat time going up an octave and may be an offbeat
    // going down one; either way the fold settles it inside three beats, which
    // is sooner than a fit could be rebuilt to argue about it.
    foldPhaseBeats = 0;
    octaveMismatchBeats = 0;
    octaveVoteBpm = 0.0f;
    staleGridBeats = 0;
    staleGridBpm = 0.0f;
    beatsOnLevel = 0;
    lastFitResidual = 1.0f;
    lastFitCoverage = 0.0f;
    lastFitIndexGap = 1.0f;
    longFitBpm = 0.0f;
    shortFitBpm = 0.0f;
    clearTempoTransition (TempoTransitionReason::reset);
    enterRegime (TempoRegime::unknown);
}

void BeatDecoder::observeDownbeatCadence() noexcept
{
    // Threshold-crossing downbeats were tried as an automatic octave oracle
    // and rejected: a correct 100-BPM track on which the network misses every
    // other downbeat produces the same two eight-beat gaps as a 50-BPM groove
    // whose eighths were counted. Letting this path publish a metrical hint is
    // exactly how a clear 100 was being forced to 50. The continuous low-band
    // experiment below remains available for measurement, behind the same
    // compile-time switch; neither may control the shipped AUTO mode.
    if (! kCadenceCorrectionEnabled)
        return;

    // A microphone in a room scatters the downbeat curve too widely for this
    // count to mean what it says. The reported failure is the loaded-track and
    // mixer path; there the model sees a stable direct signal and a repeated
    // bar interval is independent evidence about the metrical level.
    if (! useAnchor || ! lineFeed)
        return;

    if (cadenceDownbeatBeatSerial == 0)
    {
        cadenceDownbeatBeatSerial = beatSerial;
        return;
    }

    const uint32_t gap = beatSerial - cadenceDownbeatBeatSerial;
    cadenceDownbeatBeatSerial = beatSerial;

    // At the correct level a 4/4 bar is four accepted beats. If hi-hat eighths
    // have become the beat it is eight. Require two complete, consecutive bars:
    // a single downbeat the network missed on an ordinary 100 BPM record also
    // makes an eight, but two misses in exactly the same place are not a level
    // decision worth moving the clock for.
    if (gap >= 7u && gap <= 9u && octaveShift > -2
        && bpm * 0.5f >= kMinBpm)
    {
        const int candidate = octaveShift - 1;
        if (candidate == cadenceOctaveCandidate)
            ++cadenceOctaveVotes;
        else
        {
            cadenceOctaveCandidate = candidate;
            cadenceOctaveVotes = 1;
        }

        if (cadenceOctaveVotes >= 2)
        {
            metricalOctaveHint = candidate;
            metricalOctaveHintValid = true;
        }
    }
    else
    {
        cadenceOctaveCandidate = octaveShift;
        cadenceOctaveVotes = 0;
    }
}

void BeatDecoder::observeMetricalCadence (double eventTimeSec, float lowBand) noexcept
{
    // A microphone in a room smears the low end - the app's own part is in it,
    // the room is in it - so this histogram would not mean what it says there.
    // The reported failure is the loaded-track and mixer path, where the band
    // energy arrives as the record has it.
    if (! useAnchor || ! lineFeed)
        return;

    if (cadenceAnchorSec < 0.0)
        cadenceAnchorSec = eventTimeSec;

    const float period = 60.0f / std::max (kMinBpm, bpm);

    // Which of the eight slots this beat falls in, from elapsed time divided
    // by the current period - not from counting accepted beats. A beat the
    // network's own gate missed (it happens, even on a clean line feed: two
    // consecutive events almost exactly 2x the usual spacing apart, measured
    // on this bench) would otherwise shift every bin after it by one, forever,
    // desynchronising the histogram from the metrical grid for no musical
    // reason. Rounding elapsed time to the nearest slot absorbs a miss the way
    // the on-grid gate elsewhere in this file already absorbs one for the
    // tempo fit.
    const double slots = (eventTimeSec - cadenceAnchorSec) / static_cast<double> (period);
    const int bin = ((static_cast<int> (std::llround (slots)) % 8) + 8) % 8;

    for (float& v : cadenceHist)
        v *= kCadenceDecay;
    cadenceHistBeats = cadenceHistBeats * kCadenceDecay + 1.0f;
    cadenceHist[bin] += std::max (0.0f, lowBand);

    // Not enough behind the histogram yet for a share of it to mean anything,
    // or an octave down would leave nothing (or go somewhere the caller cannot
    // use): say so and stop, rather than act on a stale reading from before
    // the input changed.
    if (cadenceHistBeats < kCadenceBeatsToDecide || octaveShift <= -2
        || bpm * 0.5f < kMinBpm)
    {
        if (kCadenceCorrectionEnabled)
            metricalOctaveHintValid = false;
        return;
    }

    // The two interleaved sets of four slots. If the grid is one octave too
    // fast, one set is the real quarters - kick and snare, body in the low
    // bands - and the other is the hi-hat eighths between them, which have
    // almost none. If the grid is already right, both sets are real quarters
    // and neither is anywhere near empty.
    float even = 0.0f, odd = 0.0f;
    for (int i = 0; i < 8; i += 2)
    {
        even += cadenceHist[i];
        odd  += cadenceHist[i + 1];
    }
    const float strong = std::max (even, odd);
    const float weak   = std::min (even, odd);
    if (strong < 1.0e-6f)
    {
        if (kCadenceCorrectionEnabled)
            metricalOctaveHintValid = false;
        return;
    }

    const float depth = weak / strong;

    if (! kCadenceCorrectionEnabled)
        return;   // measuring only; the counting path above owns the hint

    if (depth < kCadenceHatDepth)
    {
        metricalOctaveHint = octaveShift - 1;
        metricalOctaveHintValid = true;
    }
    else
    {
        metricalOctaveHintValid = false;
    }
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
    d.fitIndexGap = lastFitIndexGap;
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
    staleGridBeats = 0;
    staleGridBpm = 0.0f;
    lastFitResidual = 1.0f;
    lastFitCoverage = 0.0f;
    lastFitIndexGap = 1.0f;
    longFitBpm = 0.0f;
    shortFitBpm = 0.0f;
    longWrite = 0;
    longFilled = 0;

    // No interval spans the hole, so neither does any evidence of a change
    // across it - and the splice would supply exactly the sort of odd interval
    // this detector is built to notice.
    clearTempoTransition (TempoTransitionReason::reset);
}

void BeatDecoder::notifyInputRestart (bool preserveComb) noexcept
{
    // The two things that decide the metrical level. Both were measuring a
    // room: over forty seconds of room noise at the level the make-up gain
    // hands the network, the fold names a tempo with a salience of 0.29 and
    // calls the level settled, and the state space sits on it with a margin of
    // 8 - which, having a change penalty, it then defends against the music.
    // An arrangement entrance can arrive after the fold already contains the
    // band. The engine explicitly identifies that case; discard the old grid
    // below but retain the independent activation history. Ordinary source
    // changes still discard everything, including confident room evidence.
    if (! preserveComb)
        tempo.restartEvidence();
    hmm.reset();
    anchorBpm = 0.0f;
    anchorStrength = 0.0f;
    std::fill (cadenceHist, cadenceHist + 8, 0.0f);
    cadenceHistBeats = 0.0f;
    cadenceAnchorSec = -1.0;
    cadenceDownbeatBeatSerial = 0;
    cadenceOctaveCandidate = octaveShift;
    cadenceOctaveVotes = 0;
    metricalOctaveHint = 0;
    metricalOctaveHintValid = false;

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
    staleGridBeats = 0;
    staleGridBpm = 0.0f;
    beatsOnLevel = 0;
    lastFitResidual = 1.0f;
    lastFitCoverage = 0.0f;
    lastFitIndexGap = 1.0f;
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
                             double& anchorOut, float* indexGapOut) const noexcept
{
    return fitPeriodBefore (maxBeats, period, residual, coverage, anchorOut, indexGapOut, 0);
}

bool BeatDecoder::fitPeriodBefore (int maxBeats, float& period, float& residual, float& coverage,
                                   double& anchorOut, float* indexGapOut, int skipNewest) const noexcept
{
    coverage = 0.0f;
    anchorOut = -1.0;
    if (indexGapOut != nullptr)
        *indexGapOut = 1.0f;
    const int n = std::min (beatFilled - skipNewest, maxBeats);
    if (n < 4 || bpm < kMinBpm)
        return false;

    // Newest n beat times, oldest first.
    double t[kBeatHistory];
    const int oldest = (beatWrite - skipNewest - n + kBeatHistory) % kBeatHistory;
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

    // And the half of that picture coverage cannot supply, which is the other
    // direction. A grid an octave too *fast* throws nothing away - every beat
    // lands on every other tick - so it reads coverage 1.0 and a low residual
    // and looks perfect. What gives it away is the indices those beats landed
    // on: 0, 2, 4, 6 instead of 0, 1, 2, 3.
    //
    // Median, not mean, so one missed beat is one gap of two among many of one
    // rather than a shifted average. Measured at 60 BPM on material with a
    // single impulse per beat and silence between: the committed grid read
    // 122.2 with coverage 1.00 and residual 0.030, and the gap read 2.
    if (indexGapOut != nullptr && keep >= 2)
    {
        double gaps[kBeatHistory];
        int ng = 0;
        for (int i = 1; i < keep; ++i)
            gaps[ng++] = idx[i] - idx[i - 1];
        std::sort (gaps, gaps + ng);
        *indexGapOut = static_cast<float> (gaps[ng / 2]);
    }

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
    if (slope < 60.0 / kMaxBpm - 1.0e-9 || slope > 60.0 / kMinBpm + 1.0e-9)
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
    lastFitIndexGap = 1.0f;
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

float BeatDecoder::recentStrengthAlternation() const noexcept
{
    // Split the recent accepted beats by parity and compare the two medians.
    //
    // On a grid that is the pulse, every accepted beat is a beat, so the two
    // halves are the same population and the answer is near zero - accents move
    // it a little, and a backbeat kit measured here reads 0.1-0.2. On a grid an
    // octave too fast because a hi-hat fills the subdivision, one parity is the
    // quarters and the other is the hat: measured on eighths at 0.45 against
    // quarters at 0.8-1.0, it reads about 0.5.
    //
    // Medians, so one swallowed beat or one crash does not decide it.
    constexpr int kAlternationBeats = 12;
    const int n = std::min (beatFilled, kAlternationBeats);
    if (n < 6)
        return 0.0f;

    float even[kAlternationBeats], odd[kAlternationBeats];
    int ne = 0, no = 0;
    for (int k = 0; k < n; ++k)
    {
        const float v = beatStrength[(beatWrite - 1 - k + kBeatHistory) % kBeatHistory];
        if ((k & 1) == 0)
            even[ne++] = v;
        else
            odd[no++] = v;
    }
    if (ne < 3 || no < 3)
        return 0.0f;
    std::sort (even, even + ne);
    std::sort (odd, odd + no);
    const float a = even[ne / 2];
    const float b = odd[no / 2];
    const float hi = std::max (a, b);
    const float lo = std::min (a, b);
    if (hi <= 1.0e-6f)
        return 0.0f;
    return (hi - lo) / hi;
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
        // Two intervals of one new period differ only by the material's own
        // scatter, and two draws with relative spread `jitter` differ by about
        // 1.1 of it on average. So the bar is a small multiple of the jitter -
        // but it was four times it, not two, and the factor of two that is easy
        // to miss is in `deviation` below: that is *half* the relative
        // difference, so a tolerance of `2 * jitter` accepts intervals that
        // disagree by four.
        //
        // Measured at the five remaining false confirmations on
        // `probe_steady_tempo` (constant tempo, --live settings): two of them
        // were pairs like 0.4635 s and 0.4826 s - 4.1% apart, as far from each
        // other as the step they were claiming - called one tempo because 2.0%
        // cleared a 2.4% bar. That is not a period measured twice; it is two
        // different numbers whose mean happens to sit away from the committed
        // tempo.
        //
        // At `1.0f * jitter` the pair must agree within twice the scatter,
        // which a genuine step clears about five times out of six, and a
        // candidate that misses gets the next pair - the code falls through to
        // restart rather than waiting a beat. The absolute floors are left
        // where they are: they are the cleanest-material case and were not what
        // was loose.
        const float tolerance = std::max (lineFeed ? kTransitionLineCoherence
                                                   : kTransitionRoomCoherence,
                                          1.0f * jitter);
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
    // And never below the smallest step this path exists to catch.
    //
    // `3 * jitter` alone let the detector open candidates *under* the floor the
    // file states two hundred lines up: on ordinary material the median scatter
    // reads about 1.4%, so `needed` came out at 4.2% while
    // `kTransitionSmallestStep` says 5% is the smallest change worth claiming.
    // Everything between the two is a size the detector was willing to act on
    // and unwilling to defend.
    //
    // What lives in that gap is not a tempo change. Two consecutive intervals
    // drawn from the same jittered material will now and then agree with each
    // other while both sit 4-5% off the true period; the coherence test then
    // passes, because it asks whether the two agree, not whether they are
    // right. Measured on `probe_steady_tempo` at constant tempo with the --live
    // settings, 300 s x 10 seeds: eight such excursions across 110-160 BPM, all
    // of them 4.0-5.5%, lasting 1.8-3.0 s, and - the part that says they are not
    // acquisition - at 80, 88, 155, 171, 243, 270, 282 and 295 seconds into the
    // run. The comb read the true tempo throughout each one; nothing asked it.
    //
    // A grid that moves 4% for three seconds under a percussionist is heard.
    // The listener's words were "ogni tanto rallentano o accelerano e poi ci
    // mettono molto a rientrare".
    const float needed = std::max (kTransitionSmallestStep,
                                   std::max (kTransitionMinBpmDelta / std::max (kMinBpm, bpm),
                                             3.0f * jitter));
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
    // A period is not present in one isolated event, and one interval is not
    // enough context to distinguish a quarter from one half of a swung pair.
    // A direct feed therefore waits for three peaks; a microphone asks for a
    // fourth, which also rejects a reflection/transient pair. Both still answer
    // inside one 4/4 bar.
    const int minimum = lineFeed ? kContextAcquirePeaksLine : kContextAcquirePeaksRoom;
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

    double acquireAnchorSec = beatTime[newest];
    bool pairedSubdivision = false;

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

    // On a direct feed, strong-weak-strong already closes one complete swung
    // cell. That is enough to count its sum as the quarter without waiting for
    // the fourth event used by the more defensive room path below.
    if (lineFeed && beatFilled == 3)
    {
        const int i0 = (beatWrite - 3 + kBeatHistory) % kBeatHistory;
        const int i1 = (beatWrite - 2 + kBeatHistory) % kBeatHistory;
        const int i2 = newest;
        const float d0 = static_cast<float> (beatTime[i1] - beatTime[i0]);
        const float d1 = static_cast<float> (beatTime[i2] - beatTime[i1]);
        const float pairPeriod = d0 + d1;
        const float shortIoi = std::min (d0, d1);
        const float longIoi = std::max (d0, d1);
        const float ends = 0.5f * (beatStrength[i0] + beatStrength[i2]);
        const bool accentedCell = std::fabs (beatStrength[i0] - beatStrength[i2])
                                      < 0.20f * std::max (0.10f, ends)
                                  && std::fabs (beatStrength[i1] - ends)
                                      > 0.14f * std::max (0.10f, ends);
        const float pairBpm = pairPeriod > 0.0f ? 60.0f / pairPeriod : 0.0f;
        if (shortIoi < 0.82f * longIoi && accentedCell
            && pairBpm >= kMinBpm && pairBpm <= kMaxBpm)
        {
            bestPeriod = pairPeriod;
            bestError = 0.0f;
            intervalSelfSufficient = true;
            pairedSubdivision = true;
            acquireAnchorSec = d0 >= d1 ? beatTime[i0] : beatTime[i1];
        }
    }

    // A swung eighth is not a bad quarter: it is a repeated long-short pair.
    // Reading either half alone produces the familiar 1.5x answer (81 -> 123)
    // and then makes the slow comb spend several bars disproving it. Four
    // peaks are enough to see two overlapping cells: d0+d1 and d1+d2 have the
    // same duration when d0 and d2 agree, while the unequal adjacent intervals
    // say this is not an ordinary train at that faster rate. This is the causal
    // information a player uses to count the quarter before joining.
    //
    // Require the two amplitude parities to be internally coherent as well.
    // Their means need not differ: some swung parts accent every eighth evenly;
    // coherence is useful because a fill's four unrelated hits should not earn
    // a new grid. The room tolerance is wider because reflections move the
    // detected crest, but both paths still need a genuinely uneven pair.
    if (beatFilled >= 4)
    {
        const int i0 = (beatWrite - 4 + kBeatHistory) % kBeatHistory;
        const int i1 = (beatWrite - 3 + kBeatHistory) % kBeatHistory;
        const int i2 = (beatWrite - 2 + kBeatHistory) % kBeatHistory;
        const int i3 = newest;
        const float d0 = static_cast<float> (beatTime[i1] - beatTime[i0]);
        const float d1 = static_cast<float> (beatTime[i2] - beatTime[i1]);
        const float d2 = static_cast<float> (beatTime[i3] - beatTime[i2]);
        const float outerMean = 0.5f * (d0 + d2);
        const float pairPeriod = d1 + d2;
        const float shortIoi = std::min (d1, d2);
        const float longIoi = std::max (d1, d2);
        const float timingTolerance = lineFeed ? 0.12f : 0.20f;
        const bool timingRepeats = d0 > 0.0f && d1 > 0.0f && d2 > 0.0f
                                   && std::fabs (d0 - d2)
                                          < timingTolerance * std::max (0.05f, outerMean)
                                   && shortIoi < 0.82f * longIoi;

        const float evenMean = 0.5f * (beatStrength[i0] + beatStrength[i2]);
        const float oddMean = 0.5f * (beatStrength[i1] + beatStrength[i3]);
        const bool evenCoherent = std::fabs (beatStrength[i0] - beatStrength[i2])
                                  < 0.28f * std::max (0.10f, evenMean);
        const bool oddCoherent = std::fabs (beatStrength[i1] - beatStrength[i3])
                                 < 0.28f * std::max (0.10f, oddMean);
        const float pairBpm = pairPeriod > 0.0f ? 60.0f / pairPeriod : 0.0f;

        if (timingRepeats && evenCoherent && oddCoherent
            && pairBpm >= kMinBpm && pairBpm <= kMaxBpm)
        {
            bestPeriod = pairPeriod;
            bestError = 0.0f;
            intervalSelfSufficient = true;
            pairedSubdivision = true;
            // In ordinary swing the beat begins the long interval and the
            // off-eighth begins the short return. That convention supplies the
            // phase even when every eighth has the same strength; accents are
            // still used above to reject four unrelated hits.
            acquireAnchorSec = d2 >= d1 ? beatTime[i2] : beatTime[i3];
        }
    }

    // At the opposite end, three alternating peak heights are direct evidence
    // that the short spacing is a subdivision: strong-weak-strong (or its
    // inverse) repeats only after two intervals. This resolves slow music with
    // loud eighths without making genuinely fast, evenly weighted music wait
    // for the long fold.
    if (fastOctaveAmbiguous && beatFilled >= 3 && ! pairedSubdivision)
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
    if (! lineFeed && ! pairedSubdivision)
    {
        const int oldest = (beatWrite - 3 + kBeatHistory) % kBeatHistory;
        const float previousRaw = static_cast<float> (beatTime[older] - beatTime[oldest]);
        if (previousRaw <= 0.0f)
            return false;
        const float ratio = previousRaw / raw;
        if (std::fabs (ratio - 1.0f) > 0.14f)
            return false;
    }

    // The two intervals are already present at acquisition. When they agree
    // on a direct feed, use both dates instead of freezing the last interval's
    // timing error until the longer fit becomes available. Preserve the level
    // chosen above and leave swung cells on their own phase/period evidence.
    // INFINITO central excerpt: at +2 s, 95.43 -> 92.27 BPM (reference ~91);
    // quick material bank: excursion count unchanged, 18, mean lock +0.02 s.
    if (lineFeed && ! pairedSubdivision && beatFilled >= 3)
    {
        const int oldest = (beatWrite - 3 + kBeatHistory) % kBeatHistory;
        const float previousRaw = static_cast<float> (beatTime[older] - beatTime[oldest]);
        if (previousRaw > 0.0f && std::fabs (previousRaw / raw - 1.0f) < 0.14f)
            bestPeriod *= 0.5f * (1.0f + previousRaw / raw);
    }
    const float acquiredRawBpm = 60.0f / bestPeriod;

    // The top of the range is where the interval alone cannot tell a pulse from
    // a subdivision and where being wrong costs the most. The paired and
    // alternating branches above deliberately set `bestError` to zero, which
    // waives the level check - correctly, so that loud eighths at 76 BPM do not
    // have to argue with a state space still sitting near its 118-BPM prior.
    //
    // Up here that waiver is what published 212 BPM. Measured on a real
    // recording at about -12 dB, where the quiet feed puts the network out of
    // the distribution it was trained on: the detected peaks were 141 ms apart,
    // the alternation test correctly said "this spacing is a subdivision" and
    // folded it once to 212 - which is still faster than any pulse in the take,
    // and still passes the same test, so nothing folded it again. The state
    // space was naming 103.45 at that moment, a full octave away, and no branch
    // looked at it. The song was 91, and the fold took eight more seconds to
    // say so while the part played at 212.
    //
    // So in this band the state space keeps a veto. It does not have to be
    // confident - it was not, at 0.047 of margin - but if it is naming a level
    // an octave off, this is not the moment to publish one. Below the band
    // nothing changes: 168 still acquires on the interval alone.
    if (acquiredRawBpm > kFastAcquireVetoBpm
        && std::fabs (std::log2 (acquiredRawBpm / level)) > kOctaveThreshold)
        return false;

    bpm = std::clamp (applyUserOctave (acquiredRawBpm), kMinBpm, kMaxBpm);
    gridAnchorSec = acquireAnchorSec;
    established = true;
    provisional = true;
    intervalAcquired = true;
    provisionalStrength = std::clamp ((margin - required) / 2.0f + 0.55f, 0.55f, 0.90f);

    // The HMM can speak after two periods of *its winner*, which at the lower
    // edge means it may provisionally name 104 before two 52-BPM beats have
    // even happened. Once the actual interval exists it is the more direct
    // fact. Feed an octave-sized correction back into the HMM immediately or
    // its confident answer rewrites the anchor on the following frame.
    if (useAnchor && anchorBpm >= kMinBpm
        && std::fabs (std::log2 (acquiredRawBpm / anchorBpm)) > kOctaveThreshold)
    {
        hmm.anchorMetricalLevel (acquiredRawBpm);
        anchorBpm = std::clamp (acquiredRawBpm, kMinBpm, kMaxBpm);
        anchorStrength = 0.0f;
    }
    return true;
}

void BeatDecoder::updateTempo() noexcept
{
    const bool combReady = tempo.ready() && tempo.salience() > kSalienceFloor;
    const float combBpm = applyUserOctave (foldToAnchor (tempo.bpm()));
    // The same reading with the anchor fold left off: what the fold actually
    // measured, carrying only the listener's own half/double request. Used to
    // decide whether the committed level is wrong - see `combDisagrees` below.
    const float combRawBpm = applyUserOctave (tempo.bpm());

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
                 && beatFilled >= (lineFeed ? kContextAcquirePeaksLine
                                            : kContextAcquirePeaksRoom)
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

    // A provisional HMM acquisition may precede the first measurable interval
    // at a slow tempo. Revisit it as soon as the second detected beat arrives;
    // otherwise `tryFastAcquire` is never called again simply because the less
    // precise source happened to answer first. At 52 BPM this moves the first
    // correct lock from about 12.5 s to the second interval (~2.3 s).
    if (provisional && ! intervalAcquired)
        tryFastAcquire();

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
    // A provisional interval grid that uses every other tick has already
    // supplied the evidence `levelSettled()` is waiting many slow seconds to
    // collect: the measured beats are one octave below the grid. Let the raw
    // comb corroborate that case immediately. At 52 BPM, waiting for the fold's
    // ordinary slower-octave window left the clock at ~104 for about twelve
    // seconds even though the fitted index gap was already exactly two.
    const bool provisionalDoubledGrid = provisional && intervalAcquired
                                        && lastFitIndexGap >= 1.5f;
    // How far apart the two sources are, and whether the argument is about a
    // metrical level at all. Computed here rather than beside the snap because
    // `combMayCorrect` below now needs the answer: the reason it waits for
    // `levelSettled()` is an octave reason, and it should not be charged for an
    // argument that is not about an octave.
    const float disagreement = combRawBpm > kMinBpm && bpm > kMinBpm
                                   ? std::fabs (std::log2 (bpm / combRawBpm))
                                   : 0.0f;
    const bool octaveArgument =
        std::fabs (disagreement - std::round (disagreement)) < kOctaveArgumentTolerance;
    // The arbitration between the two tempo sources, for the case where they do
    // not disagree about an octave.
    //
    // `levelSettled()` means the fold's buffer has held enough audio to have
    // examined the slower octave of its own winner, and every reason to wait for
    // it is an octave reason: without it the comb may be naming the double, and
    // adopting that outright is how loud eighths at 76 BPM became 152. None of
    // that applies when the two are a fifth apart. Whichever octave the comb is
    // on, the committed grid is on neither, so waiting for the fold to settle
    // the octave question is waiting for the answer to a question nobody asked.
    //
    // Measured through the engine on the reference song (docs/TODO.md item 29),
    // after the rhythm section walks in and the analysis restarts: the comb is
    // ready and reading 87.7-88.0 from 47.0 s while the network crawls 55.9 to
    // 58.2, and `levelSettled()` does not arrive until 49.0. Those two seconds
    // were being paid for nothing.
    //
    // Only against a *provisional* level, and that restriction was measured, not
    // assumed. Without it the same relaxation reaches an established grid, and
    // `VPAlign` shows what that costs: 132 BPM at 2.2 ms of jitter went from an
    // rms of 0.07 beats to 0.23 with a worst case of half a beat - the wrong
    // metrical level, arrived at by exactly the argument this is meant to
    // settle - and the 100 to 110 ramp in twelve seconds lost 5 ms of decoder
    // phase. A level that is still provisional is a guess and has nothing to
    // defend; one that is established has tenure, and the vote below is what
    // tenure is for.
    const bool nonOctaveDisagreement = combReady && provisional && ! octaveArgument
                                       && disagreement > kOctaveThreshold
                                       && tempo.salience() > kOctaveSnapSalience;
    const bool combMayCorrect = tempo.levelSettled()
                                || (provisional && ! intervalAcquired)
                                || provisionalDoubledGrid
                                || nonOctaveDisagreement;
    // Direction matters. A grid twice too fast can look healthy because every
    // detected beat lands on every other tick. A grid twice too slow cannot:
    // it would have to discard every other event. If this grid was built from
    // actual intervals and still covers those events tightly, a late 120 -> 60
    // fold is not evidence against it. This is the room-then-band failure that
    // otherwise appeared fourteen seconds after a correct lock.
    //
    // But only while the grid's own beats say it is the pulse. `gridHealthy` is
    // residual and coverage, and the sentence above says in as many words why
    // neither can see a grid that is twice too fast: every detected beat lands
    // on every other tick, so nothing is thrown away and nothing is out of
    // place. Using that as evidence *for* the fast grid is the wrong way round,
    // and it is what kept a doubled grid at slow tempo permanently: measured on
    // one impulse per beat with silence between, 60 BPM read 122.2 for the
    // whole run, ten runs out of ten, while the comb sat at 61.3 at salience
    // 1.00 - the right answer, vetoed here every frame.
    //
    // The index gap is the missing half. It is 1 when the beats populate the
    // grid and 2 when they use every other tick, so the veto now asks whether
    // this grid is dense before it defends it. A genuine 120 with a late comb
    // fold to 60 - the room-then-band failure the veto exists for - has a dense
    // grid and is protected exactly as before. See docs/TODO.md item 22.
    const bool gridIsDense = lastFitIndexGap < 1.5f;
    // Dense is not the same as right.
    //
    // The index gap catches a grid that uses every other tick, which is what a
    // doubled grid looks like when there is silence between the beats. It
    // cannot catch the commonest case of all: a hi-hat on the eighths *fills*
    // those ticks, so the doubled grid is dense, its coverage is 1.00 and its
    // residual is 0.03, and every test the veto has says the grid is fine.
    // Measured on kit-shaped material at 81 BPM - quarters at 0.8-1.0, eighths
    // at 0.45 - the fold read 82 at salience 1.00 for a whole minute while the
    // committed tempo sat at 164 and `octaveMismatch` never left zero.
    //
    // What the two cases do not share is the weight of the beats. On the pulse
    // they are all beats; an octave up, every other one is a hat. So the veto
    // stands down when the grid's own beats alternate loud and quiet *and* the
    // fold is naming something close to half of it - the shape of a grid built
    // on a subdivision, not of a tempo the fold got wrong. It only lifts the
    // veto: the snap still needs the fold's salience and `snapBeats` of votes.
    constexpr float kSubdivisionAlternation = 0.35f;
    const float halfError = combRawBpm > kMinBpm
                                ? std::fabs (std::log2 (bpm / combRawBpm) - 1.0f)
                                : 1.0f;
    const bool gridLooksLikeSubdivision =
        recentStrengthAlternation() > kSubdivisionAlternation && halfError < 0.20f;
    const bool unprovenSlowerOctave = intervalAcquired && gridHealthy && gridIsDense
                                      && ! gridLooksLikeSubdivision
                                      && combRawBpm < bpm * 0.70f;
    // Against the fold's *raw* answer, not the one already folded onto the
    // anchor - and this is the whole of why a doubled grid at slow tempo was
    // permanent.
    //
    // `combBpm` is `foldToAnchor (tempo.bpm())`, which moves the fold's reading
    // onto the level in use so the published number stays in the level the
    // listener is in. That is right for publishing and fatal here: it is
    // subtracted the octave *before* the test that exists to notice an octave.
    // Measured at 60 BPM, one impulse per beat and silence between: the fold
    // read 61.3 at salience 1.00 all run, the anchor sat at 122, so the value
    // this line saw was 122.6 and `log2(122.2 / 122.6)` is 0.004 - no
    // disagreement, every frame, forever. Ten runs out of ten never came back.
    //
    // It is the same trap `checkGridPhase` documents for phase - "a grid that
    // once anchors on an offbeat is not merely wrong, it is stable ... and
    // there was nothing in the chain that could notice" - and it was solved
    // there by putting the fold outside the gate. For the rate the fold *was*
    // the gate.
    //
    // Everything downstream still uses the anchored `combBpm`; only the
    // question "is the level itself wrong" is asked of the unfolded answer, and
    // it is still answered by salience, by repeated agreement over
    // `snapBeats`, and now by a grid that admits it is using every other tick.
    // See docs/TODO.md item 22.
    const bool combDisagrees = combReady && combMayCorrect && ! unprovenSlowerOctave
                               && bpm > kMinBpm
                               && std::fabs (std::log2 (bpm / combRawBpm)) > kOctaveThreshold;

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
    //
    // But only a grid that is actually on the pulse. The paragraph above says
    // it itself - the double lands on every detected peak too, so it is always
    // one of the healthy ones - and then hands it the patience anyway, because
    // until `fitPeriod` reported the index gap there was no way to tell the two
    // apart. There is now: a grid using every other tick has not earned
    // anything. Measured on swung material at 81 BPM, where acquisition lands
    // on the 1.5x level the swing implies, this is most of the wait.
    // Both allowances below are about *octaves*. A grid an octave too fast lands
    // on every detected beat, so it always looks healthy, and choosing between
    // the two levels is genuinely ambiguous - that is what the patience buys.
    //
    // A disagreement that is not near a whole number of octaves has no such
    // excuse. Nobody hears 67.7 and 90.9 as the same pulse counted differently:
    // one of them is simply the wrong grid. Measured on the level bench at
    // 91 BPM and full level, acquisition landed on three quarters of the pulse,
    // the fold named 90.91 from 8.5 s and held it, and the decoder spent ten
    // more seconds paying octave tenure for an argument that was never about an
    // octave - 17.71 s to the right level against 2.5-3.4 s at every quieter
    // level. See docs/TODO.md item 24.
    //
    // When the fold agrees, or disagrees by a real octave, the distance is near
    // zero and both allowances apply exactly as before. `disagreement` and
    // `octaveArgument` are computed above, beside `combMayCorrect`.
    if (octaveArgument && gridHealthy && gridIsDense && tempo.levelSettled())
        snapBeats += kOctaveSnapBeatsHealthy;

    if (octaveArgument && tempo.levelSettled())
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
    // The level is chosen while nothing is sounding and held for as long as the
    // part plays. `BeatTracker::updateAutoOctave` says why, at length: halving
    // or doubling under a percussionist is not a tempo correction, it is the
    // grid they are playing against moving, and the density of the part, where
    // the bar falls and what the display says are all wrong at once.
    //
    // That rule could only ever cover the tracker's own shift. Measured twice
    // here, from both ends of the level sweep: a clipped 168 BPM kit runs
    // fourteen seconds of a healthy grid (residual 0.014, level settled) and
    // then the *fold* names 84.03 and the tempo halves under a playing part;
    // and the same recording at -18 dB locks 91 and then goes to 182.37 for
    // fourteen seconds. In both the sources agree with each other on the wrong
    // answer, so no arbitration between them can help - what is wrong is the
    // moment, not the evidence.
    //
    // Only an argument about the octave is refused, and only once the level has
    // stopped being provisional. A tempo that genuinely changed is not near a
    // whole number of octaves and still gets through; a new input clears
    // `established` through `notifyInputRestart` and is not covered by this at
    // all; and if the held level is the wrong one, the way out is the same one
    // item 17 names - the listener taps ÷2 or ×2.
    const bool levelHeldWhilePlaying = sounding && ! provisional && octaveArgument;

    if (combDisagrees && ! levelHeldWhilePlaying
        && tempo.salience() > kOctaveSnapSalience)
    {
        // A vote is for one specific level. On ambiguous material the comb does
        // not merely disagree with the grid, it disagrees with itself - 208,
        // then 52, then 104, then 208 again - and counting all of that as one
        // accumulating case against the grid hands the clock to whichever level
        // happened to be named on the beat the total came due. Each new level
        // starts its own vote.
        // The vote is about the level the fold actually measured, for the same
        // reason the disagreement above is: `combBpm` has already been folded
        // onto the level under suspicion, so voting on it is voting for the
        // thing being argued against.
        if (octaveVoteBpm < kMinBpm
            || std::fabs (std::log2 (combRawBpm / octaveVoteBpm)) > kOctaveVoteHold)
        {
            octaveVoteBpm = combRawBpm;
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
        // Snap to what the fold measured, not to its reading folded back onto
        // the level being left. With `combBpm` here the snap was a no-op in the
        // only dimension it exists for: it threw the beat history away and
        // committed the same doubled tempo again, which is why a 60 BPM grid at
        // 122 survived a counter that reached its bar over and over.
        bpm = std::clamp (combRawBpm, kMinBpm, kMaxBpm);
        // And move both owners of the level with it. Moving only `anchorBpm`
        // lasted one frame: a confident HMM still at the doubled level wrote
        // its answer back over the correction in `observe`. The comb vote is
        // independent level evidence, so reweight the HMM's tempo marginal
        // while retaining its phase evidence. `anchorBpm` is in the HMM's raw
        // level (before the listener's manual octave), hence `tempo.bpm()`.
        if (useAnchor && anchorBpm >= kMinBpm)
        {
            hmm.anchorMetricalLevel (tempo.bpm());
            anchorBpm = std::clamp (tempo.bpm(), kMinBpm, kMaxBpm);
            anchorStrength = 0.0f;
        }
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
        lastFitIndexGap = 1.0f;
        return;
    }

    // The stale-grid watchdog. Runs only after the octave snap has declined the
    // beat, so a real metrical disagreement is still that path's to answer.
    //
    // What it must not do is adopt `combBpm`. In the stuck case the comb is not
    // right either - it reads 106.7 while the band plays 160 - because it is
    // describing a grid that is rejecting most of the beats it should be made
    // of. Both numbers are downstream of the same bad grid, so the only honest
    // move is to stop defending it and measure again: drop the grid, the fits
    // and the fold's evidence, and let the next beats re-acquire from the
    // activation. That is what an input restart does, and it is why moving the
    // microphone gain was the only thing that ever unstuck this.
    if (combReady && combMayCorrect && bpm > kMinBpm && combBpm > kMinBpm)
    {
        const float apart = std::fabs (std::log2 (combBpm / bpm));
        // Strictly the band *below* the octave snap. Past kOctaveThreshold the
        // disagreement is a metrical level and that path owns it - it has the
        // tenure, salience and vote-hold rules for exactly that argument. Taking
        // those cases here instead was measured and is worse: on a 170 BPM bench
        // where some runs acquire at 85, this watchdog fired on the 2:1 and
        // re-acquired repeatedly, taking time-out from 3.1 to 6.7 per cent of the
        // run and mean error from 1.9 to 3.7. This exists only for the gap that
        // nothing else covers.
        if (apart > kStaleGridThreshold && apart < kOctaveThreshold)
        {
            if (staleGridBpm < kMinBpm
                || std::fabs (std::log2 (combBpm / staleGridBpm)) > kStaleGridVoteHold)
            {
                staleGridBpm = combBpm;
                staleGridBeats = 1;
            }
            else
            {
                ++staleGridBeats;
            }
        }
        else if (apart < kStaleGridRelease)
        {
            staleGridBeats = 0;
            staleGridBpm = 0.0f;
        }

        if (staleGridBeats >= kStaleGridVoteBeats)
        {
            staleGridBeats = 0;
            staleGridBpm = 0.0f;
            tempo.restartEvidence();
            ++gridSerial;
            clearTempoTransition (TempoTransitionReason::reset);
            lastBeatSec = -1.0;
            gridAnchorSec = -1.0;
            foldPhaseBeats = 0;
            beatWrite = 0;
            beatFilled = 0;
            longWrite = 0;
            longFilled = 0;
            intervalAcquired = false;
            established = false;
            provisional = false;
            provisionalStrength = 0.0f;
            octaveMismatchBeats = 0;
            octaveVoteBpm = 0.0f;
            beatsOnLevel = 0;
            fastDriftBeats = 0;
            fastDriftLargeBeats = 0;
            fastDriftSign = 0;
            lastFitResidual = 1.0f;
            lastFitCoverage = 0.0f;
    lastFitIndexGap = 1.0f;
            enterRegime (TempoRegime::unknown);
            return;
        }
    }

    float longPeriod = 0.0f, longResidual = 0.0f, longCoverage = 0.0f;
    float shortPeriod = 0.0f, shortResidual = 0.0f, shortCoverage = 0.0f;
    double longAnchor = -1.0, shortAnchor = -1.0;
    float longIndexGap = 1.0f, shortIndexGap = 1.0f;
    const bool haveLong = fitPeriod (kLongFit, longPeriod, longResidual, longCoverage,
                                     longAnchor, &longIndexGap);
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
                                      shortCoverage, shortAnchor, &shortIndexGap);

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
        lastFitIndexGap = longIndexGap;
    }
    else if (haveShort)
    {
        lastFitResidual = shortResidual;
        lastFitCoverage = shortCoverage;
        lastFitIndexGap = shortIndexGap;
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

    auto bringSlowFitCurrent = [recent, this] (float target) noexcept
    {
        if (recent <= 0.0f || bpm >= 75.0f || target < kMinBpm)
            return target;
        const float recentBpm = 60.0f / recent;
        const float weight = std::clamp ((75.0f - bpm) / 20.0f, 0.0f, 1.0f);
        const float boundedRecent = std::clamp (recentBpm,
                                                target * 0.96f,
                                                target * 1.04f);
        return target + (boundedRecent - target) * weight;
    };

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
            // A vote over the last few bars, not a run of consecutive beats -
            // the same lesson the octave snap below already learned, in the
            // same file, for the same reason: "a counter that reset on the
            // first agreeing beat never got anywhere against that, which is how
            // a level chosen in the first seconds outlived every correction".
            //
            // A band that drifts does not clear this bar on every beat, it
            // clears it on most of them, and the error wanders back under the
            // line often enough that six *consecutive* wandered beats never
            // arrived. Measured at 81 BPM on material drifting 3 BPM: the
            // regime went fixed at a flat part of the drift and then held
            // 82.09 while the truth fell to 80.23 - the comb, the long fit and
            // the short fit all following it down and only the published number
            // frozen - for ten seconds, reaching 4.3%. Decrementing instead
            // lets a drift accumulate its case the way a wrong octave does.
            if (wandered)
                ++fixedErrorBeats;
            else
                --fixedErrorBeats;
            fixedErrorBeats = std::clamp (fixedErrorBeats, 0, 3 * kBeatsToLeaveFixed);

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
            // And `moving` on its own, which was computed for the acquisition
            // branch and never asked here.
            //
            // Every other term above is a size: how far the tempo has already
            // got from the anchor. On a band that drifts, the size arrives late
            // by construction - the error starts at nothing and grows - so the
            // regime went on holding a frozen number through the part of the
            // drift where it was still small, and by the time 2% had
            // accumulated the listener had been out for seconds. `moving` is
            // not a size, it is a *direction*: the window's ends differ by more
            // than the window's own scatter. A record cut to a click cannot
            // produce it; a band can produce it before the error is audible at
            // all.
            //
            // Measured at 81 BPM on material drifting 3 BPM: the regime fixed
            // at a flat part of the sine and then held 82.09 while the truth
            // fell to 80.23 - comb, long fit and short fit all following it
            // down, only the published number frozen - for ten seconds.
            if (beatsInRegime >= kRegimeMinBeats
                && (moving
                    || fixedErrorBeats >= kBeatsToLeaveFixed
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
            // A small, clean change can stay below every regime-release
            // threshold. Four consecutive quarters provide three intervals;
            // require their fit uncertainty to be far below the rate change.
            // This is a bounded refinement on a direct feed, not an octave
            // decision or permission to follow a noisy short fit continuously.
            float recentFit = 0, recentResidual = 0, recentCoverage = 0, recentGap = 0;
            float beforeFit = 0, beforeResidual = 0, beforeCoverage = 0, beforeGap = 0;
            double recentAnchor = -1;
            double beforeAnchor = -1;
            if (lineFeed && !provisional && beatsInRegime >= kRegimeMinBeats
                && fitPeriod (4, recentFit, recentResidual, recentCoverage, recentAnchor, &recentGap)
                && recentCoverage == 1.0f && recentGap == 1.0f
                && fitPeriodBefore (4, beforeFit, beforeResidual, beforeCoverage, beforeAnchor, &beforeGap, 3)
                && beforeCoverage == 1.0f && beforeGap == 1.0f)
            {
                const float measured = 60.0f / recentFit;
                const float difference = std::fabs (measured - bpm);
                bool consecutive = true;
                for (int i = 0; i < 6; ++i)
                {
                    const int newest = (beatWrite - 1 - i + kBeatHistory) % kBeatHistory;
                    const int previous = (newest - 1 + kBeatHistory) % kBeatHistory;
                    const float period = i < 3 ? recentFit : beforeFit;
                    consecutive &= std::fabs ((beatTime[newest] - beatTime[previous]) / period - 1.0) < 0.10;
                }
                if (consecutive
                    && std::fabs (60.0f / beforeFit - bpm) < 0.35f
                    && difference > std::max (0.5f, 6.0f * (recentResidual + beforeResidual) * bpm)
                    && difference < std::min (3.0f, 0.04f * bpm))
                {
                    bpm = fixedAnchorBpm = std::clamp (measured, kMinBpm, kMaxBpm);
                    gridAnchorSec = recentAnchor;
                    // The preceding intervals describe the old rate. Seed the
                    // normal precision fit with the four proven new quarters;
                    // keep serials, metrical level and musical clock intact.
                    beatFilled = 4;
                    longFilled = longWrite = 0;
                    fixedSamples = 0;

                    // And say so, in the one word the clock listens to.
                    //
                    // Rewriting `bpm` here is only half the job: the clock eases
                    // towards a new target with its own time constant, so a step
                    // this function has already *proven* still arrived as a
                    // gentle lean. Measured on the small-step bench at 52 BPM,
                    // the rate was right 3.6 s after the change and the phase
                    // needed three more seconds to follow it - and the whole
                    // point of the gate is that those four quarters were clean
                    // enough not to have to guess.
                    //
                    // Same publication a confirmed rapid transition uses, so a
                    // consumer needs to know nothing new; the regime stays
                    // `fixed`, because a record cut to a click that changes by
                    // two BPM is still a record cut to a click.
                    transitionState = TempoTransitionState::rapid;
                    transitionReason = TempoTransitionReason::confirmed;
                    transitionPeriodSec = 60.0f / bpm;
                    transitionIntervals = 3;
                    transitionConfidence = 1.0f;
                    transitionRapidBeats = 0;
                    transitionRapidDeadlineSec =
                        timeSec + static_cast<double> (kTransitionRapidLifetimeBeats)
                                      * static_cast<double> (transitionPeriodSec);
                    transitionRefitBeats = kShortFit;
                    transitionLastSec = gridAnchorSec;
                    ++transitionSerial;
                    return;
                }
            }
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

            // Eight beats are a four-second window at 120 BPM but more than
            // nine seconds at 52. Its least-squares precision is still useful;
            // treating it as equally current is not. At slow live tempi blend
            // towards the median of the last three intervals, which is centred
            // roughly one beat back, and cap its authority to the range of a
            // musical drift so one displaced onset cannot pull the clock.
            // The blend fades to zero by 75 BPM, leaving the faster-tempo
            // stability tuning untouched.
            target = bringSlowFitCurrent (target);
            commit (pullTowardsComb (target, combReady, combBpm), kRateLive);
            break;
        }

        case TempoRegime::unknown:
            commit (pullTowardsComb (bringSlowFitCurrent (
                                         haveLong ? longFitBpm : shortFitBpm),
                                     combReady, combBpm),
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

BeatHypothesis BeatDecoder::observe (float pBeat, float pDownbeat, float pNone,
                                     float lowBand) noexcept
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
        const float hmmAtUserLevel = applyUserOctave (hmm.bpm());
        const bool agreesWithCommittedLevel = ! established || bpm < kMinBpm
                                               || (hmmAtUserLevel >= kMinBpm
                                                   && std::fabs (std::log2 (
                                                          hmmAtUserLevel / bpm))
                                                          < kOctaveThreshold);
        if (hmm.ready() && hmm.levelMargin() > kAnchorMargin
            && agreesWithCommittedLevel)
        {
            anchorBpm = hmm.bpm();
            anchorStrength = std::clamp ((hmm.levelMargin() - kAnchorMargin) / kAnchorMargin,
                                         0.0f, 1.0f);
        }
        else
        {
            // The tempo it last named is kept - the fold is still folded onto
            // it - but a margin that has fallen back, or an HMM that has moved
            // to another metrical level without the comb's repeated vote, is
            // not evidence about the committed grid. In particular this stops
            // the 52 -> 104 state-space bias from overwriting a confirmed slow
            // level later in the same take.
            anchorStrength = 0.0f;
        }
    }

    const float period = 60.0f / std::max (kMinBpm, bpm);
    // Before a grid exists, do not use the 120-BPM default to suppress evidence
    // that may be a swung off-eighth. At 120 BPM full swing returns only 167 ms
    // after the beat, below the old 200 ms gate. The fastest legal pulse defines
    // the causal minimum during acquisition; once established, the musical grid
    // resumes owning the refractory window.
    const float eventReferencePeriod = established ? period : 60.0f / kMaxBpm;
    const int minRefr = std::max (2, static_cast<int> (
        0.4f * eventReferencePeriod * static_cast<float> (fps)));

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
                                         >= 0.4 * static_cast<double> (eventReferencePeriod));

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
        // And how much body this beat had, over the same three-frame window and
        // for the same reason. This is what says "kick or snare" rather than
        // "hi-hat", which is the one question the metrical level turns on and
        // the one the probabilities above cannot answer.
        const float beatLowBand = std::max (prevLowBand,
                                            std::max (prevPrevLowBand, lowBand));
        observeMetricalCadence (eventTimeSec, beatLowBand);
        if (prevDownbeat > downThresh)
        {
            lastDownbeatStrength = prevDownbeat;
            lastDownbeatSec = eventTimeSec;
            beatsInBar = 0;
            ++downbeatSerial;
            observeDownbeatCadence();
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
    hyp.metricalOctaveHint = metricalOctaveHint;
    hyp.metricalOctaveHintValid = metricalOctaveHintValid;
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
    prevPrevLowBand = prevLowBand;
    prevLowBand = lowBand;
    return hyp;
}

} // namespace vp
