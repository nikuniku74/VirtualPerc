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

    // Kick vs hat on a file feed: four equal short intervals whose even/odd
    // *low-band* means disagree. Pulse amplitude even/odd at 0.07 moved
    // fisso/gradino hashes because 50 fps sampling of even clicks exceeds
    // 7%. The click bank and every probe that omits observe's fourth
    // argument pass lowBand=0, so this peek is false and tryFastAcquire
    // is not called again. Not kCadenceCorrectionEnabled: that path is
    // still off, and 50-vs-100 is not this band (rawBpm is not >145).
    constexpr float kLowBandMute = 0.05f;
    bool evenOddKickHatSubdivision (const double* beatTime,
                                    const float* beatLowBand,
                                    int history,
                                    int beatWrite,
                                    int beatFilled,
                                    bool lineFeed) noexcept
    {
        if (beatFilled < 4 || beatTime == nullptr || beatLowBand == nullptr
            || history <= 0)
            return false;
        const int i0 = (beatWrite - 4 + history) % history;
        const int i1 = (beatWrite - 3 + history) % history;
        const int i2 = (beatWrite - 2 + history) % history;
        const int i3 = (beatWrite - 1 + history) % history;
        const float b0 = beatLowBand[i0];
        const float b1 = beatLowBand[i1];
        const float b2 = beatLowBand[i2];
        const float b3 = beatLowBand[i3];
        const float present = std::max (std::max (b0, b1), std::max (b2, b3));
        if (present < kLowBandMute)
            return false;
        const float d0 = static_cast<float> (beatTime[i1] - beatTime[i0]);
        const float d1 = static_cast<float> (beatTime[i2] - beatTime[i1]);
        const float d2 = static_cast<float> (beatTime[i3] - beatTime[i2]);
        const float ioiMean = (d0 + d1 + d2) / 3.0f;
        const float timingTolerance = lineFeed ? 0.12f : 0.20f;
        const float evenTol = timingTolerance * std::max (0.05f, ioiMean);
        if (! (d0 > 0.0f && d1 > 0.0f && d2 > 0.0f
               && std::fabs (d0 - ioiMean) < evenTol
               && std::fabs (d1 - ioiMean) < evenTol
               && std::fabs (d2 - ioiMean) < evenTol))
            return false;
        const float evenMean = 0.5f * (b0 + b2);
        const float oddMean = 0.5f * (b1 + b3);
        const float mid = 0.5f * (evenMean + oddMean);
        return std::fabs (evenMean - oddMean) > 0.35f * std::max (0.05f, mid);
    }

    // Hats-only eighths on a direct feed. A flat hat has no low-band, so
    // evenOddKickHatSubdivision stays mute, BeatNet peaks every hat, and
    // the HMM near 118 publishes the eighth (76 → 152). Four equal short
    // IOIs in that band, every peak under kLowBandMute, and high-band
    // actually present: fold once to the quarter.
    //
    // highBand is observe's fifth argument and defaults to 0, the same
    // contract as lowBand. The click bank and every probe omit it, so
    // the stored three-frame max is 0 and this stays shut. kHighBandPresent
    // is mean log10(mag+1) over bands 120..135 (the top 16 of the
    // 30 Hz–17 kHz, 24/oct bank, ~10.7–17 kHz, where a hi-hat has energy
    // and a kick does not). 0.15 is above an empty band and above
    // kLowBandMute, so a residual does not count as a hat spectrum.
    //
    // This is only equal short IOIs in the eighth-ambiguous band with
    // hat spectrum and no kick. A genuine >145 BPM hat-on-quarters song
    // is the same measurement and is halved too. A 168 kick track is
    // protected by low-band and still acquires on the interval. The
    // caller anchors on the newest peak: the phase may be the levare.
    // "L'1 è QUI" is the correction; do not invent an accent.
    constexpr float kHighBandPresent = 0.15f;
    bool hatsOnlyEighthFold (const double* beatTime,
                             const float* beatLowBand,
                             const float* beatHighBand,
                             int history,
                             int beatWrite,
                             int beatFilled,
                             bool lineFeed) noexcept
    {
        if (! lineFeed || beatFilled < 4 || beatTime == nullptr
            || beatLowBand == nullptr || beatHighBand == nullptr
            || history <= 0)
            return false;
        const int i0 = (beatWrite - 4 + history) % history;
        const int i1 = (beatWrite - 3 + history) % history;
        const int i2 = (beatWrite - 2 + history) % history;
        const int i3 = (beatWrite - 1 + history) % history;
        const float b0 = beatLowBand[i0];
        const float b1 = beatLowBand[i1];
        const float b2 = beatLowBand[i2];
        const float b3 = beatLowBand[i3];
        if (! (b0 < kLowBandMute && b1 < kLowBandMute
               && b2 < kLowBandMute && b3 < kLowBandMute))
            return false;
        const float h0 = beatHighBand[i0];
        const float h1 = beatHighBand[i1];
        const float h2 = beatHighBand[i2];
        const float h3 = beatHighBand[i3];
        if (! (h0 > kHighBandPresent && h1 > kHighBandPresent
               && h2 > kHighBandPresent && h3 > kHighBandPresent))
            return false;
        const float d0 = static_cast<float> (beatTime[i1] - beatTime[i0]);
        const float d1 = static_cast<float> (beatTime[i2] - beatTime[i1]);
        const float d2 = static_cast<float> (beatTime[i3] - beatTime[i2]);
        const float ioiMean = (d0 + d1 + d2) / 3.0f;
        const float evenTol = 0.12f * std::max (0.05f, ioiMean);
        if (! (d0 > 0.0f && d1 > 0.0f && d2 > 0.0f
               && std::fabs (d0 - ioiMean) < evenTol
               && std::fabs (d1 - ioiMean) < evenTol
               && std::fabs (d2 - ioiMean) < evenTol))
            return false;
        // Same 0.82 cell as the swing branch. Agreement within 0.12 of
        // the mean can still leave one adjacent pair under that ratio.
        const auto swung = [] (float a, float b) noexcept
        {
            const float s = std::min (a, b);
            const float l = std::max (a, b);
            return l > 0.0f && s < 0.82f * l;
        };
        if (swung (d0, d1) || swung (d1, d2))
            return false;
        const float rawBpm = d2 > 0.0f ? 60.0f / d2 : 0.0f;
        return rawBpm > 145.0f && rawBpm * 0.5f >= kMinBpm;
    }

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
    //
    // Shortening this budget was tried and buys nothing, which is worth knowing
    // because it is the obvious lever. Measured with `probe_steady_tempo` at
    // 60 BPM on the ordinary 3 BPM drift, six beats against four and three: the
    // excursion durations are identical to the tenth of a second in all three.
    // The wait is not the counter, it is the long fit reaching the 2% line -
    // at a slow tempo its window is nine seconds long, so on drifting material
    // it takes about four seconds to fall that far while the short fit is
    // already 2.7% away on the first beat. What actually shortened the
    // excursion is in `updateTempo`: the fold now has a say in FISSO at all,
    // and the bar after the regime is left is spent catching up rather than
    // leaning. See both comments there.
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
    constexpr float kFastDriftToleranceRoom = 0.024f;
    constexpr float kFastDriftToleranceLine = 0.012f;
    // Two-vote FISSO leave without the 24-beat window: a ramp's
    // 4-beat still sits on the 8-beat inside 1.5%, while offset-0
    // steps have already left by 2%+. Walking the held number on
    // 4/8 agreement lit gradino; this constant only gates a regime
    // release.
    constexpr float kCurveLatticeAgree = 0.015f;
    // Unknown Door B: 0.033 lights offset-0 fisso 24766 (click IOI
    // scatter at bir 26). 0.036 missed the first pulse-aligned
    // 4-beat on the continuo log (0.0358 at t=43.5, clean 8-beat
    // still centred on the faster tempo). 0.035 is silent on
    // offset-0 fisso/gradino.
    constexpr float kUnknownIoiLead = 0.035f;
    // Door A without comb-sign: 0.012 misses 192847 t=54.56 (0.0107).
    // 0.010 is silent on offset-0 fisso/gradino. Door B stays at
    // kFastDriftToleranceLine.
    constexpr float kDoorAIoiLead = 0.010f;
    // Door D 4-beat residual. kMotionCurveResidual (0.045) includes
    // 153252 t=67.36 (r4=0.036) and fattened family p95; 0.015 is
    // silent on offset-0 fisso/gradino and keeps 169090 t=47.58
    // (r4=0.007).
    constexpr float kDoorDFourResidual = 0.015f;
    constexpr float kFastDriftLarge     = 0.045f;
    constexpr int   kFastBeatsToLeaveFixed = 3;
    constexpr int   kFastBeatsToLeaveFixedLine = 2;
    constexpr float kFastLineCleanResidual = 0.030f;
    /** A dropout's 8-beat residual sits at 0.046-0.052. A clean linear ramp
        can sit at 0.026-0.035 (VPAlign 12 s seed 3032) while a single raw
        interval disagrees and would spend the vote an IOI-backed beat just
        earned. Below this residual a *continuing* short-fit run (already at
        least one IOI-backed vote, strictly growing, quadratic agreeing at
        g>=0.08) may add a vote without that interval. The same residual and
        quadratic, without requiring growth, may *hold* an existing vote when
        the interval disagrees: 120->132 seed 1078 dropped 2->0 between t=25
        and t=26 while the 8-beat line was still 1.9% fast. The two-vote
        clean path still requires the interval this beat, or 128->120
        overshoots to 56.5 ms. Offset-0 fisso hashes stay identical only
        with the quadratic term; without it one extra F->V appeared. */
    constexpr float kLineCleanIoiOverride = 0.040f;
    constexpr int   kFastBeatsAlone = 5;

    // A quadratic over sixteen accepted line-feed beats estimates the local
    // tempo slope at the newest beat. It remains diagnostic: the global motion
    // population found that a release selector loose enough to improve all four
    // clean ramps also fires on fixed-tempo jitter. Requiring the quadratic to
    // remove half the line's squared error makes the diagnostic selective, but
    // too late to be a production release.
    constexpr float kMotionCurveRate = 0.0010f;      // BPM/beat divided by BPM
    constexpr float kMotionCurveDeviation = 0.012f;
    constexpr float kMotionCurveResidual = 0.045f;
    constexpr float kMotionCurveWalkResidual = 0.050f;
    // Same ceiling as PhaseTrust::kStrainedMotionResidualHi. The clock hint
    // already arms in this band; the decoder walk below uses it too. Do not
    // raise kMotionCurveWalkResidual to this — that wider strain release
    // moved the 12 s MIXER mean 32.7→34.1.
    constexpr float kMotionCurveStrainResidual = 0.056f;
    // Door B above 75 BPM: the 8-beat residual must already have
    // failed this hard before the 4-beat is allowed to lead. 0.075
    // lights offset-0 gradino; 0.080 does not. Below 75 BPM Door B
    // already trusts the 4-beat with no 8-beat residual veto, because
    // a clean slow 8-beat is still 3.5 beats late.
    constexpr float kMotionCurveFourBeatDirty = 0.080f;
    constexpr float kMotionCurveWalkImprovement = 0.10f;
    constexpr float kMotionCurveWalkRate = 0.0010f;
    constexpr float kMotionCurveImprovement = 0.50f;
    // Clock-only ioiLead above 75: 0.50 lights offset-0 gradino
    // (234224 t=47.76). 0.80 is 0 fisso/gradino on the Door C origin
    // log, 42 continuo frames (169090 t=43.16 before Door D).
    constexpr float kClockOnlyQuadratic = 0.80f;
    constexpr int   kMotionCurveBeats = 3;



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
    constexpr float kRateLive      = 0.30f;
    // Unknown leftover-faster comb on !haveShort: skip-to-zero
    // fattened p995; 0.30 / 0.15 / 0.10 / 0.05 each KEEP vs the
    // previous. Do not skip the commit.
    constexpr float kRateLeftoverComb = 0.05f;
    // Door D hold: 0.70 overshot 169090; 0.30 leaves phase climbing
    // through the eight-beat window. Only the hold, not A/B/C
    // (those are already kRateAcquiring via slowIoiLeads). 0.50
    // moved p95 a hair and the family mean the wrong way.
    constexpr float kRateDoorHold  = 0.45f;

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

    /** How much better the eight-beat line has to fit than the twenty-four
        beat one before the long window is presumed to be lying across a tempo
        event. Two to one. Measured on `makedip.py`: after the dip the two read
        0.004 against 0.030, seven to one; on the steady stretch of the same
        file they read 0.004 against 0.004, and on a drummerless passage both
        are bad together. */
    constexpr float kStraddleResidualRatio = 2.0f;

    /** And how far apart the two have to be on the tempo itself. Four tenths
        of a per cent: the same file reads 0.011% apart when steady and 0.82%
        apart with the dip inside the long window only. */
    constexpr float kStraddleBpmDisagree = 0.004f;

    /** How close the short fit's tempo has to be to the *committed* tempo
        before the straddle is allowed to hand the phase anchor to it. A short
        fit that is drifting away from the committed tempo is a ramp or a step
        - the tempo is genuinely moving, and the straddle's job is not that;
        it is the stale long window after a transient (a dip) has settled back.
        On the dip the short fit returns to within ~0.1% of the committed
        tempo; at the start of a ramp it is already 1.6% away and stays that
        way, which is what this separates. 0.8% sits in the middle of that
        gap. */
    constexpr float kStraddleAgreeRatio = 0.008f;

    /** And how long both halves have to agree before it is acted on. */
    constexpr double kStraddleHoldSec = 0.40;

    /** How long the phase anchor takes to walk from the long fit's to the
        short fit's, and back. Half a second: long enough to have no edge,
        short enough to be there while the event is still inside the long
        window. */
    constexpr double kAnchorBlendSec = 0.50;

    /** `updateTempo` runs once per analysis frame, and a frame is 20 ms. */
    constexpr double kFramesPerSecond = 50.0;

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
    // After kLongFit the committed pulse can sit 4-8% off the comb while
    // both lines still agree. Inside kStaleGridThreshold the ordinary
    // ruler never fires. 0.045 log2 is ~3.2%.
    constexpr float kUnknownCombApartFloor = 0.045f;

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
    // Live Door D hold only. 169090 t=47.58 accepted lastBeat 47.585 vs
    // generating quarter 47.714 (0.20 early); true crests then sit
    // 0.20 off lastBeat and 0.18 locks the offset. 0.22 still sits
    // below a sixteenth (0.25). Offset-0 fisso/gradino: 0 Door D
    // frames. Unknown Door B hold is the leftover gap — do not widen
    // there.
    constexpr double kDoorDHoldKeep = 0.22;
    // Two pulses ~16% apart sit 0.16 of a comb-beat from each other, so the
    // ordinary 0.18 gate admits both. When the comb is the ruler this has to
    // be tighter than that split and still wider than onset jitter (~0.03
    // at 60 BPM) and still below a sixteenth (0.25).
    constexpr double kCombRulerTolerance = 0.12;
    constexpr double kCombRulerMinKeep = 0.035;
    // Origin correction onto the comb fold, in comb-beats. The ordinary
    // offbeat bar is 0.20; a stale lastBeat is typically ~0.13 off after
    // one wrong interval, which that bar would ignore, and a tighter
    // keep would then reject the true peak too.
    constexpr double kCombFoldOrigin = 0.08;
    // A step's short and long disagree by construction (kGridStepMinimum
    // 2.5%). A slow drift that has left the committed grid keeps both fits
    // on the stale pulse. 4.5% sits between jitter and that floor.
    constexpr float kStaleFitsAgree = 0.045f;

    // Unless the grid itself has gone quiet. If nothing has landed on it for
    // this long the grid is the wrong one - a new song, an edit, a section that
    // dropped the beat - and the next peak re-anchors it. Counting rejections
    // instead would re-anchor on a drum fill, which is exactly the material the
    // grid is there to ride out.
    //
    // The waive does not apply while a part is sounding. Hats-only stays
    // loud, BeatNet treats those eighths as beats, and taking the first
    // one after this hole flipped the grid under the player. After the
    // same hole a crest on the committed fold, and not a half-beat off
    // lastBeat, is still the pulse — see the sounding re-open in `observe`.
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

    // And the largest. A room stays at the old quarter: beyond it, two local
    // maxima are too easily a subdivision or a swallowed beat. A direct mixer
    // feed can prove a wider non-metrical jump with the same two coherent,
    // strong intervals and abrupt-edge test. This closes the old dead zone in
    // which 120 -> 160 took 23.6 s even though four consecutive quarter peaks
    // already described 160 exactly. Whole-octave neighbourhoods are still
    // excluded in `transitionCandidateAllowed`: 60/120 and 75/140 remain a
    // level decision, because audio alone cannot name which octave is intended.
    constexpr float kTransitionMaxRelativeDeltaRoom = 0.25f;
    constexpr float kTransitionMaxRelativeDeltaLine = 0.65f;

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

    // The eight-beat fit is current before the four-second autocorrelation is.
    // Keep the latter quarantined for three more accepted beats after a
    // confirmed change; see the measured 120 -> 108 case in the live branch.
    constexpr int kTransitionCombLagBeats = 3;

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

    // Quarter-by-quarter step detector (direct feed only; see observeGridStep).
    // Sizes are relative to the beat period, evidence is in units of the
    // measured onset scatter, so none of them depends on BPM or song.
    constexpr float  kGridStepMinimum = 0.025f;      // smallest step claimed
    constexpr float  kGridStepMaximum = 0.30f;       // larger: interval detector / octave
    constexpr double kGridStepSigmaFloor = 0.005;    // same floor as line jitter
    constexpr double kGridStepEvidence = 16.0;       // (4 sigma)^2 step over offset
    constexpr double kGridStepCurvature = 0.30;      // ramp guard, share of the step
    constexpr double kGridStepPostCurvature = 0.26;  // causal ramp guard after pivot
    constexpr int    kGridStepMaxBeats = 4;
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
    longFitPeriodHeld = false;
    lastAcceptedLowBand = 0.0f;
    kitBodyHeard = false;
    kitBodyLastSec = -1.0;
    hatGridHolding = false;
    postHoleReopenSec = -1.0;
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
    prevHighBand = 0.0f;
    prevPrevHighBand = 0.0f;
    lastDownbeatStrength = 0.0f;
    lastBeatDownbeat = 0.0f;
    beatWrite = 0;
    beatFilled = 0;
    beatSerial = 0;
    downbeatSerial = 0;
    gridSerial = 0;
    resetMotionShadow (true, TempoMotionVeto::inputEpoch);
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
    motionFitBpm = 0.0f;
    motionFitRate = 0.0f;
    motionFitResidual = 1.0f;
    motionFitImprovement = 0.0f;
    motionFitEvidence = 0;
    motionFitDirection = 0;
    octaveMismatchBeats = 0;
    combHalfBeats = 0;
    octaveVoteBpm = 0.0f;
    staleGridBeats = 0;
    staleGridBpm = 0.0f;
    beatsOnLevel = 0;
    lastFitResidual = 1.0f;
    straddleSinceSec = -1.0;
    anchorBlend = 0.0;
    longWindowStraddles = false;
    lastFitCoverage = 0.0f;
    lastFitIndexGap = 1.0f;
    longFitBpm = 0.0f;
    shortFitBpm = 0.0f;
    shortFitResidual = 1.0f;
    longWrite = 0;
    longFilled = 0;
    fixedAnchorBpm = 0.0f;
    fixedSamples = 0;
    fixedWalkRun = 0;
    stepFourHoldBpm = 0.0f;
    liveFourHoldBpm = 0.0f;
    barTempoHoldBeats = 0;
    beatsInRegime = 0;
    fixedErrorBeats = 0;
    leftFixedBeats = 0;
    std::fill (longHist, longHist + kLongHistory, 0.0f);
    std::fill (beatTime, beatTime + kBeatHistory, 0.0);
    std::fill (beatStrength, beatStrength + kBeatHistory, 0.0f);
    std::fill (beatLowBand, beatLowBand + kBeatHistory, 0.0f);
    std::fill (beatHighBand, beatHighBand + kBeatHistory, 0.0f);
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
        lastAcceptedLowBand = 0.0f;
        kitBodyHeard = false;
        kitBodyLastSec = -1.0;
        hatGridHolding = false;
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
    combHalfBeats = 0;
    octaveVoteBpm = 0.0f;
    staleGridBeats = 0;
    staleGridBpm = 0.0f;
    beatsOnLevel = 0;
    lastFitResidual = 1.0f;
    straddleSinceSec = -1.0;
    anchorBlend = 0.0;
    longWindowStraddles = false;
    lastFitCoverage = 0.0f;
    lastFitIndexGap = 1.0f;
    longFitBpm = 0.0f;
    shortFitBpm = 0.0f;
    shortFitResidual = 1.0f;
    clearTempoTransition (TempoTransitionReason::reset);
    enterRegime (TempoRegime::unknown);
    // Regime exit revokes authority with a generic tenure reset first. Publish
    // the causal boundary last so diagnostics retain the octave/grid reason.
    resetMotionShadow (true, TempoMotionVeto::octaveOrGrid);
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
    const TempoRegime previous = tempoRegime;
    tempoRegime = r;
    beatsInRegime = 0;
    fixedErrorBeats = 0;
    leftFixedBeats = 0;
    fixedAnchorBpm = r == TempoRegime::fixed ? bpm : 0.0f;
    fixedSamples = 0;
    fixedWalkRun = 0;
    // The FISSO 4-beat hold is one beat old. Clearing it on the way into
    // VIVO drops the pair whose second beat is the release itself
    // (329252: t=43.86 i4=78.4 still fixed, t=44.62 already live and the
    // gap to the 8-beat has fallen under 8%). The live beat either
    // confirms that hold or clears it. Every other boundary still drops it.
    if (! (previous == TempoRegime::fixed && r == TempoRegime::live))
        stepFourHoldBpm = 0.0f;
    stepFourStraddleHoldBpm = 0.0f;
    liveFourHoldBpm = 0.0f;
    // A residual curve is meaningful only inside one uninterrupted fixed
    // tenure. Seed it from the accepted entry beat while leaving the scalar
    // interval tracker unanchored; carrying an interval across this boundary
    // made a preceding step look like continuous motion in the quick banks.
    // Seeding changes no tempo, grid or serial. Only a later quadratic verdict
    // retained into VIVO may contribute a bounded commit target; shape alone
    // must never release FISSO.
    if (r == TempoRegime::fixed)
    {
        // A direct-feed release proof belongs to one fixed tenure. Carrying
        // votes accumulated in LIVE across this boundary would let old motion
        // immediately undo a legitimate re-certification. Start the causal
        // count here, then three fresh accepted beats may release this tenure
        // without paying an unrelated fourth-beat regime dwell.
        fastDriftBeats = 0;
        fastDriftLargeBeats = 0;
        fastDriftSign = 0;
        lastFastDeviation = 0.0f;
        lastIntervalDeviation = 0.0f;
        motionBridgeAuthority = 0.0f;
        motionBridgeAnchorBpm = 0.0f;
        const int newest = (beatWrite - 1 + kBeatHistory) % kBeatHistory;
        if (beatFilled > 0)
        {
            motionTracker.beginFixedTenure (beatTime[newest], bpm);
            motionShadow = motionTracker.output();
            motionObservedBeatSerial = beatSerial;
        }
        else
        {
            resetMotionShadow (true, TempoMotionVeto::none);
        }
    }
    else if (previous == TempoRegime::fixed)
    {
        // The residual-shape tenure was seeded from the FISSO entry beat. A
        // gradual change commonly triggers the ordinary release before the
        // second quadratic win; retain that evidence into VIVO. Every actual
        // boundary (transition, octave/grid, input epoch, discontinuity or
        // stale beats) still calls resetMotionShadow and revokes it.
        motionBridgeAuthority = 0.0f;
        motionBridgeAnchorBpm = 0.0f;
        // updateMotionShadow() runs before the regime decision. Its shape
        // verdict therefore belongs to this same accepted beat but was gated
        // while the decoder still said FISSO. Re-evaluate only the authority
        // predicate now that the existing release has entered VIVO; observing
        // the beat again would double-count evidence and is deliberately not
        // done. This removes one whole beat of response without weakening a
        // proof, moving the grid or restarting the clock.
        refreshMotionBridgeAuthority();
    }
}

BeatDecoder::Diagnostics BeatDecoder::diagnostics() const noexcept
{
    Diagnostics d;
    d.combBpm = tempo.bpm();
    d.combSalience = tempo.salience();
    d.combReady = tempo.ready();
    d.shortFitRate = shortFitRate;
    d.motionFit = motionFitBpm;
    d.motionFitRate = motionFitRate;
    d.motionFitResidual = motionFitResidual;
    d.motionFitImprovement = motionFitImprovement;
    d.motionFitEvidence = motionFitEvidence;
    d.motionFitDirection = motionFitDirection;
    d.motionShadowBpm = motionShadow.predictedBpm;
    d.motionShadowPeriodDelta = motionShadow.periodDeltaPerBeat;
    d.motionShadowUncertainty = motionShadow.uncertainty;
    d.motionShadowAuthority = motionShadow.authority;
    d.motionShadowState = static_cast<int> (motionShadow.state);
    d.motionShadowVeto = static_cast<int> (motionShadow.veto);
    d.motionFirstStrictProof = motionShadow.firstStrictProof;
    d.motionShapeModel = static_cast<int> (motionShadow.shapeModel);
    d.motionShapeBpm = motionShadow.shapePredictedBpm;
    d.motionShapeQuadraticVsHinge = motionShadow.shapeQuadraticVsHinge;
    d.motionShapeEvidenceMargin = motionShadow.shapeEvidenceMargin;
    d.motionShapeQuadraticWins = motionShadow.shapeQuadraticWins;
    d.motionShapeQuarantineBeats = motionShadow.shapeQuarantineBeats;
    d.motionBridgeAuthority = motionBridgeAuthority;
    d.longFit = longFitBpm;
    d.shortFit = shortFitBpm;
    d.residual = lastFitResidual;
    d.coverage = lastFitCoverage;
    d.fitIndexGap = lastFitIndexGap;
    d.octaveMismatch = octaveMismatchBeats;
    d.beatsHeld = beatsOnLevel;
    d.levelSettled = tempo.levelSettled();
    d.userOctave = octaveShift;
    d.beatsInRegime = beatsInRegime;
    float recent = 0.0f;
    if (recentPeriod (recent))
    {
        d.recentIoiBpm = 60.0f / recent;
        float period = 0.0f, residual = 1.0f, coverage = 0.0f;
        double anchor = -1.0;
        if (fitPeriodBefore (kShortFit, period, residual, coverage, anchor, nullptr, 0,
                             static_cast<double> (recent)))
        {
            d.ioiIndexedFit = 60.0f / period;
            d.ioiIndexedResidual = residual;
        }
        if (fitPeriodBefore (4, period, residual, coverage, anchor, nullptr, 0,
                             static_cast<double> (recent)))
        {
            d.ioiIndexedFit4 = 60.0f / period;
            d.ioiIndexedResidual4 = residual;
        }
    }
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
    longFitPeriodHeld = false;
    lastAcceptedLowBand = 0.0f;
    postHoleReopenSec = -1.0;
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
    prevHighBand = 0.0f;
    prevPrevHighBand = 0.0f;
    refractoryFrames = std::max (refractoryFrames, 3);

    // Evidence chains describe beats that are now gone. Nothing measured before
    // the hole may vouch for the grid afterwards.
    fastDriftBeats = 0;
    fastDriftLargeBeats = 0;
    fastDriftSign = 0;
    motionFitBpm = 0.0f;
    motionFitRate = 0.0f;
    motionFitResidual = 1.0f;
    motionFitImprovement = 0.0f;
    motionFitEvidence = 0;
    motionFitDirection = 0;
    octaveMismatchBeats = 0;
    combHalfBeats = 0;
    octaveVoteBpm = 0.0f;
    staleGridBeats = 0;
    staleGridBpm = 0.0f;
    lastFitResidual = 1.0f;
    straddleSinceSec = -1.0;
    anchorBlend = 0.0;
    longWindowStraddles = false;
    lastFitCoverage = 0.0f;
    lastFitIndexGap = 1.0f;
    longFitBpm = 0.0f;
    shortFitBpm = 0.0f;
    shortFitResidual = 1.0f;
    longWrite = 0;
    longFilled = 0;

    // No interval spans the hole, so neither does any evidence of a change
    // across it - and the splice would supply exactly the sort of odd interval
    // this detector is built to notice.
    clearTempoTransition (TempoTransitionReason::reset);
    resetMotionShadow (false, TempoMotionVeto::discontinuity);
    stepFourHoldBpm = 0.0f;
    liveFourHoldBpm = 0.0f;
    barTempoHoldBeats = 0;
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
    longFitPeriodHeld = false;
    lastAcceptedLowBand = 0.0f;
    kitBodyHeard = false;
    kitBodyLastSec = -1.0;
    hatGridHolding = false;
    stepFourHoldBpm = 0.0f;
    liveFourHoldBpm = 0.0f;
    barTempoHoldBeats = 0;
    postHoleReopenSec = -1.0;
    lastDownbeatSec = -1.0;
    gridAnchorSec = -1.0;
    foldPhaseBeats = 0;
    beatWrite = 0;
    beatFilled = 0;
    beatsInBar = 0;
    prevHighBand = 0.0f;
    prevPrevHighBand = 0.0f;
    ++gridSerial;

    // Evidence chains, and the verdict they fed. A tempo called fixed on a room
    // is the most expensive thing to keep: it is designed to be stubborn.
    fastDriftBeats = 0;
    fastDriftLargeBeats = 0;
    fastDriftSign = 0;
    motionFitBpm = 0.0f;
    motionFitRate = 0.0f;
    motionFitResidual = 1.0f;
    motionFitImprovement = 0.0f;
    motionFitEvidence = 0;
    motionFitDirection = 0;
    octaveMismatchBeats = 0;
    combHalfBeats = 0;
    octaveVoteBpm = 0.0f;
    staleGridBeats = 0;
    staleGridBpm = 0.0f;
    beatsOnLevel = 0;
    lastFitResidual = 1.0f;
    straddleSinceSec = -1.0;
    anchorBlend = 0.0;
    longWindowStraddles = false;
    lastFitCoverage = 0.0f;
    lastFitIndexGap = 1.0f;
    longFitBpm = 0.0f;
    shortFitBpm = 0.0f;
    shortFitResidual = 1.0f;
    longWrite = 0;
    longFilled = 0;
    established = false;
    provisional = false;
    intervalAcquired = false;
    provisionalStrength = 0.0f;
    clearTempoTransition (TempoTransitionReason::reset);
    enterRegime (TempoRegime::unknown);
    resetMotionShadow (true, TempoMotionVeto::inputEpoch);
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

bool BeatDecoder::stalePulseCombRuler() const noexcept
{
    // Comb as *target* followed rate and left phase behind, and moved
    // gradino hashes. This only changes which activations count as beats
    // and how those times are indexed. Only unknown: live is where a
    // confirmed step rebuilds, and the offset-0 gradino hash moved when
    // the ruler was allowed there. FISSO keeps rejecting subdivisions on
    // the committed grid.
    if (! lineFeed || tempoRegime != TempoRegime::unknown)
        return false;
    if (transitionState == TempoTransitionState::rapid || transitionRefitBeats > 0)
        return false;
    if (shortFitBpm < kMinBpm || longFitBpm < kMinBpm)
        return false;
    if (shortFitResidual <= kMotionCurveWalkResidual)
        return false;
    if (std::fabs (shortFitBpm - longFitBpm)
            >= kStaleFitsAgree * std::max (kMinBpm, longFitBpm))
        return false;
    if (! tempo.ready() || ! tempo.levelSettled())
        return false;
    // Door B already uses this comb at sal 0.129 (216604 t=52).
    // The 0.14 floor left that frame on the 69.9 lattice; snap-geom
    // below the floor is 1 offset-0 continuo frame, 0 fisso/gradino.
    // Walk residual 0.050 (not strain 0.056): t=51.10 is 0.051 and
    // 0 offset-0 fisso/gradino. Clean-lattice ruler (no residual
    // floor) fattened p995. 0.035 snapped the 73 yank (t=50.18)
    // and dumped the ring (mean/p95 up, p995 278→222).
    const float comb = applyUserOctave (foldToAnchor (tempo.bpm()));
    if (comb < kMinBpm)
        return false;
    const float apart = std::fabs (std::log2 (comb / shortFitBpm));
    const float floor = beatsInRegime > kLongFit ? kUnknownCombApartFloor
                                                 : kStaleGridThreshold;
    return apart > floor && apart < kOctaveThreshold;
}

double BeatDecoder::pulseIndexGuess() const noexcept
{
    if (! stalePulseCombRuler())
        return 0.0;
    const float comb = applyUserOctave (foldToAnchor (tempo.bpm()));
    return 60.0 / static_cast<double> (comb);
}

double BeatDecoder::stalePulseKeep (double rulerPeriod) const noexcept
{
    const double combPeriod = pulseIndexGuess();
    if (combPeriod <= 0.0 || rulerPeriod <= 0.0 || beatsInRegime <= kLongFit
        || shortFitBpm < kMinBpm)
        return -1.0;
    if (std::fabs (rulerPeriod - combPeriod) > 1.0e-9)
        return -1.0;
    const float combBpm = 60.0f / static_cast<float> (rulerPeriod);
    if (combBpm < kMinBpm)
        return -1.0;
    const float split = std::fabs (1.0f - shortFitBpm / combBpm);
    if (split <= 0.0f || split > static_cast<float> (kCombRulerTolerance))
        return -1.0;
    return std::max (kCombRulerMinKeep, 0.40 * static_cast<double> (split));
}

void BeatDecoder::snapStalePulseToCombFold() noexcept
{
    // The keep that splits two pulses is measured from lastBeat. If that
    // origin is still the stale pulse, a tighter keep rejects the true
    // peak as well. The fold at the comb period is the one origin that
    // does not go through the on-grid gate. Same unknown-ruler gate as
    // pulseIndexGuess, past kLongFit so an unknown step is left alone.
    if (! stalePulseCombRuler() || beatsInRegime <= kLongFit)
        return;
    if (gridAnchorSec < 0.0 || lastBeatSec < 0.0 || ! tempo.ready())
        return;
    const float comb = applyUserOctave (foldToAnchor (tempo.bpm()));
    if (comb < kMinBpm)
        return;
    const double period = 60.0 / static_cast<double> (comb);
    float contrast = 1.0f;
    const float foldPhase = tempo.beatPhaseFor (comb, contrast);
    if (foldPhase < 0.0f || contrast > kFoldPhaseContrast)
        return;
    const double want = timeSec - static_cast<double> (foldPhase) * period;
    double shift = want - gridAnchorSec;
    shift -= std::round (shift / period) * period;
    if (std::fabs (shift) < kCombFoldOrigin * period)
        return;

    gridAnchorSec += shift;
    lastBeatSec += shift;
    ++gridSerial;
    clearTempoTransition (TempoTransitionReason::reset);

    // Dumping the ring leaves a quiet grid for seconds when the next
    // peaks are also missing (kit gap). Shifting every stored time
    // with the origin keeps the stale period (measured 312→402 ms
    // p95). Re-gate onto the new comb lattice instead: times that
    // already sit on the fold stay, the other pulse does not.
    const double keepSec = kCombRulerMinKeep * period;
    double keptTime[kBeatHistory];
    float keptStrength[kBeatHistory];
    float keptLowBand[kBeatHistory];
    float keptHighBand[kBeatHistory];
    int nKept = 0;
    for (int i = 0; i < beatFilled; ++i)
    {
        const int idx = (beatWrite - beatFilled + i + kBeatHistory) % kBeatHistory;
        const double t = beatTime[idx];
        double err = t - gridAnchorSec;
        err -= std::round (err / period) * period;
        if (std::fabs (err) <= keepSec)
        {
            keptTime[nKept] = t;
            keptStrength[nKept] = beatStrength[idx];
            keptLowBand[nKept] = beatLowBand[idx];
            keptHighBand[nKept] = beatHighBand[idx];
            ++nKept;
        }
    }
    beatWrite = 0;
    beatFilled = 0;
    longWrite = 0;
    longFilled = 0;
    for (int i = 0; i < nKept; ++i)
        storeBeatForFit (keptTime[i], keptStrength[i], keptLowBand[i],
                         keptHighBand[i]);
    if (nKept > 0)
    {
        lastBeatSec = keptTime[nKept - 1];
        lastAcceptedLowBand = keptLowBand[nKept - 1];
    }
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
                                   double& anchorOut, float* indexGapOut, int skipNewest,
                                   double guessPeriod) const noexcept
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

    // Production always indexes on the committed period. A caller may pass the
    // median of the newest raw intervals instead, which is the only guess that
    // can still land later beats inside the 0.28-beat keep gate once the
    // committed grid has already left the pulse.
    const double guess = guessPeriod > 0.0 ? guessPeriod
                                           : 60.0 / static_cast<double> (bpm);
    double keepTol = 0.28;
    if (const double splitKeep = stalePulseKeep (guess); splitKeep > 0.0)
        keepTol = splitKeep;

    // Index each beat on the committed grid, dropping anything that does not
    // sit on it. One spurious peak must not tilt the whole fit.
    double idx[kBeatHistory];
    int keep = 0;
    for (int i = 0; i < n; ++i)
    {
        const double beats = (t[i] - t[0]) / guess;
        const double rounded = std::round (beats);
        if (std::fabs (beats - rounded) > keepTol)
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

bool BeatDecoder::fitPeriodCurve (int maxBeats, float& periodNow, float& bpmPerBeat,
                                  float& residual, float& improvement) const noexcept
{
    periodNow = 0.0f;
    bpmPerBeat = 0.0f;
    residual = 1.0f;
    improvement = 0.0f;
    const int n = std::min (beatFilled, maxBeats);
    if (n < 6 || bpm < kMinBpm)
        return false;

    // Same admitted events as the ordinary fit. A quadratic can explain an
    // outlier better than a line, so letting it see peaks that the production
    // grid rejected would make its apparent responsiveness meaningless.
    double t[kBeatHistory];
    const int oldest = (beatWrite - n + kBeatHistory) % kBeatHistory;
    for (int i = 0; i < n; ++i)
        t[i] = beatTime[(oldest + i) % kBeatHistory];

    double guess = pulseIndexGuess();
    if (guess <= 0.0)
        guess = 60.0 / static_cast<double> (bpm);
    double keepTol = 0.28;
    if (const double splitKeep = stalePulseKeep (guess); splitKeep > 0.0)
        keepTol = splitKeep;
    double idx[kBeatHistory];
    int keep = 0;
    for (int i = 0; i < n; ++i)
    {
        const double beats = (t[i] - t[0]) / guess;
        const double rounded = std::round (beats);
        if (std::fabs (beats - rounded) > keepTol)
            continue;
        idx[keep] = rounded;
        t[keep] = t[i];
        ++keep;
    }
    if (keep < 6 || idx[keep - 1] - idx[0] < 5.0)
        return false;

    // Fit y = a + b*x + c*x^2 after centring both axes on the newest beat.
    // Then b is the causal period at that beat and 2c is its change per beat.
    // This is deliberately diagnostic first: extrapolating a quadratic to the
    // edge amplifies onset jitter, and the probes must quantify that cost.
    const double newestIdx = idx[keep - 1];
    const double newestTime = t[keep - 1];
    double m[3][4] {};
    double sumX = 0.0, sumY = 0.0, sumXX = 0.0, sumXY = 0.0;
    for (int i = 0; i < keep; ++i)
    {
        const double x = idx[i] - newestIdx;
        const double x2 = x * x;
        const double y = t[i] - newestTime;
        sumX += x;
        sumY += y;
        sumXX += x2;
        sumXY += x * y;
        const double v[3] { 1.0, x, x2 };
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
                m[row][col] += v[row] * v[col];
            m[row][3] += v[row] * y;
        }
    }

    for (int col = 0; col < 3; ++col)
    {
        int pivot = col;
        for (int row = col + 1; row < 3; ++row)
            if (std::fabs (m[row][col]) > std::fabs (m[pivot][col]))
                pivot = row;
        if (std::fabs (m[pivot][col]) < 1.0e-12)
            return false;
        if (pivot != col)
            for (int k = col; k < 4; ++k)
                std::swap (m[col][k], m[pivot][k]);
        const double scale = m[col][col];
        for (int k = col; k < 4; ++k)
            m[col][k] /= scale;
        for (int row = 0; row < 3; ++row)
        {
            if (row == col)
                continue;
            const double factor = m[row][col];
            for (int k = col; k < 4; ++k)
                m[row][k] -= factor * m[col][k];
        }
    }

    const double a = m[0][3];
    const double b = m[1][3];
    const double c = m[2][3];
    if (b < 60.0 / kMaxBpm - 1.0e-9 || b > 60.0 / kMinBpm + 1.0e-9)
        return false;

    double sumSq = 0.0;
    double linearSumSq = 0.0;
    const double count = static_cast<double> (keep);
    const double linearDen = count * sumXX - sumX * sumX;
    if (std::fabs (linearDen) < 1.0e-12)
        return false;
    const double linearB = (count * sumXY - sumX * sumY) / linearDen;
    const double linearA = (sumY - linearB * sumX) / count;
    for (int i = 0; i < keep; ++i)
    {
        const double x = idx[i] - newestIdx;
        const double y = t[i] - newestTime;
        const double predicted = a + b * x + c * x * x;
        const double e = y - predicted;
        sumSq += e * e;
        const double linearE = y - (linearA + linearB * x);
        linearSumSq += linearE * linearE;
    }

    periodNow = static_cast<float> (b);
    bpmPerBeat = static_cast<float> (-120.0 * c / (b * b));
    residual = static_cast<float> (std::sqrt (sumSq / static_cast<double> (keep)) / b);
    improvement = linearSumSq > 1.0e-12
                      ? static_cast<float> (1.0 - sumSq / linearSumSq)
                      : 0.0f;
    return std::isfinite (periodNow) && std::isfinite (bpmPerBeat)
           && std::isfinite (residual) && std::isfinite (improvement);
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
    // While a part is sounding the on-grid gate holds lastBeat so hats
    // cannot steal it. The fold's buffer is twelve seconds, so after a
    // hats hole it still names the off-beat. Three resume quarters then
    // look like a flipped grid, this path slides lastBeat onto the hats,
    // and the sounding keep freezes there (fixture B: phase 0.514 at
    // t=30, then no accepted pulse). Same hold as the octave snap: do
    // not re-argue the half under the player. The synthetic bank never
    // sets sounding. The listener's "L'1 è QUI" names the nearest beat
    // already on this grid; it does not move lastBeat. Automatic crests
    // still cannot.
    if (sounding)
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
    resetMotionShadow (true, TempoMotionVeto::octaveOrGrid);

    // The beats behind us were the wrong ones. Keeping them would have the fit
    // pulling the grid straight back to where it was, which is the same trap
    // one level down.
    clearTempoTransition (TempoTransitionReason::reset);
    beatWrite = 0;
    beatFilled = 0;
    longWrite = 0;
    longFilled = 0;
    lastFitResidual = 1.0f;
    straddleSinceSec = -1.0;
    anchorBlend = 0.0;
    longWindowStraddles = false;
    lastFitCoverage = 0.0f;
    lastFitIndexGap = 1.0f;
    longFitBpm = 0.0f;
    shortFitBpm = 0.0f;
    shortFitResidual = 1.0f;
}

void BeatDecoder::declarePulseHere() noexcept
{
    // Listener said the one is here. The clock places that on the nearest
    // beat it is already playing; this must not publish a different grid.
    // Setting the anchor to this sample makes beat phase 0 from the middle
    // of a beat, and once the 0.70 s tap hold ends the clock spends up to
    // half a beat of that error as a rate bend — the tempo jump the press
    // was heard as. The lattice stays. The count matches the clock: phase
    // in (0.5, 1) means the next beat is the one. Fits, the long-fit phase
    // hold and the tempo are left alone; wiping them was what stopped a
    // moved lattice pulling back, and this command no longer moves it.
    // Automatic hats still cannot steal lastBeat.
    if (bpm < kMinBpm)
        return;
    const float period = 60.0f / bpm;
    const float p = gridPhaseNow (period);
    beatsInBar = p <= 0.5f ? 0 : 3;
    hyp.beatPhase = p;
    hyp.barPhase = wrap01 ((static_cast<float> (beatsInBar) + p) * 0.25f);
    hyp.periodSec = period;
}

void BeatDecoder::commit (float candidateBpm, float rate) noexcept
{
    if (candidateBpm < kMinBpm || candidateBpm > kMaxBpm)
        return;
    bpm = std::clamp (bpm + (candidateBpm - bpm) * rate, kMinBpm, kMaxBpm);
}

bool BeatDecoder::kitBodyHolding (double nowSec) const noexcept
{
    if (! kitBodyHeard || ! established || provisional
        || kitBodyLastSec < 0.0 || bpm < kMinBpm)
        return false;
    // A hat one beat after a kick is the groove. The next crest with the
    // kick body still gone is the drummer out: the tempo already counted
    // stays, and that crest only confirms the grid. Click bank and the
    // motion matrix pass lowBand 0, so kitBodyHeard never arms there.
    const double periodSec = 60.0 / static_cast<double> (bpm);
    return nowSec - kitBodyLastSec > 1.05 * periodSec;
}

void BeatDecoder::registerBeat (double beatTimeSec, float strength,
                                float lowBand, float highBand) noexcept
{
    storeBeatForFit (beatTimeSec, strength, lowBand, highBand);
    ++beatSerial;
}

void BeatDecoder::storeBeatForFit (double beatTimeSec, float strength,
                                   float lowBand, float highBand) noexcept
{
    beatTime[beatWrite] = beatTimeSec;
    beatStrength[beatWrite] = strength;
    beatLowBand[beatWrite] = lowBand;
    beatHighBand[beatWrite] = highBand;
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
    strongOffGridPeakSec = -1.0;
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
    if (bpm < kMinBpm)
        return false;

    const float apart = std::fabs (std::log2 (candidateBpm / bpm));
    if (apart <= kOctaveThreshold)
        return true;

    // On the line path, a large jump which is *not* near a whole octave has an
    // owner: the causal interval detector. Previously everything beyond a
    // quarter octave was delegated to octave correction, although that path
    // quite correctly only understands metrical levels. The gap left ordinary
    // 120 -> 160 and 160 -> 100 changes with no fast recovery at all.
    //
    // Do not weaken the octave boundary itself. Near 1x/2x the same sound has
    // two valid tempo names and must stay with the fold/user control, however
    // clean the feed is.
    const float octaveDistance = std::fabs (apart - std::round (apart));
    return lineFeed && octaveDistance >= kOctaveArgumentTolerance;
}

bool BeatDecoder::observeTempoTransition (double eventTimeSec, float strength,
                                          bool acceptedByCurrentGrid) noexcept
{
    const double prevEvent = transitionPrevEventSec;
    const float prevStrength = transitionPrevStrength;
    const auto opposesProvenMotion = [this] (float candidateBpm) noexcept
    {
        if (! lineFeed || fastDriftBeats < 2 || fastDriftSign == 0
            || ! std::isfinite (candidateBpm) || bpm < kMinBpm)
            return false;
        const float delta = candidateBpm - bpm;
        return std::fabs (delta) > kTransitionMinBpmDelta
               && (delta > 0.0f ? 1 : -1) != fastDriftSign;
    };

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
    // A short fit is eight beats, so that is the first part of what is owed.
    // The autocorrelation spans four seconds and trails it: three further
    // accepted beats keep that older opinion out until it names the new tempo
    // too. Then the question the detector answers has an honest reference
    // again.
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
        const float first = transitionIntervals >= 2
                                ? transitionPeriodSec
                                : static_cast<float> (transitionLastSec - transitionFirstSec);
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
            && ! opposesProvenMotion (meanBpm)
            && transitionCandidateAllowed (mean))
        {
            const float apart = std::fabs (std::log2 (
                meanBpm / std::max (kMinBpm, bpm)));
            // Post-pause leftover lattice: off-grid crests after a sounding
            // hole form a coherent step while the comb still names the
            // held tempo (fixture D: 100→150 at t=25.2, comb 100). A
            // finite window just delayed the same yank (8 beats, then
            // 150 at t=36). Real steps have no 2.5-beat hole. The
            // synthetic bank never sets sounding. Octave-class or past
            // the stale-grid bar; leftover 6% (216604) is below it.
            // Door D is not this detector. IOI+4 already on the
            // candidate is a real step the fits have seen.
            const double heldPeriod =
                static_cast<double> (60.0f / std::max (kMinBpm, bpm));
            const bool afterHole =
                (postHoleReopenSec >= 0.0
                 && transitionFirstSec + 1.0e-9 >= postHoleReopenSec)
                || (lastBeatSec >= 0.0
                    && (transitionFirstSec - lastBeatSec)
                           >= kGridStaleBeats * heldPeriod);
            if (sounding && afterHole && apart > kStaleGridThreshold)
            {
                bool fourOnCandidate = false;
                float recent = 0.0f, p4 = 0.0f, r4 = 1.0f, c4 = 0.0f;
                double a4 = -1.0;
                if (recentPeriod (recent) && recent > 0.0f
                    && fitPeriodBefore (4, p4, r4, c4, a4, nullptr, 0,
                                        static_cast<double> (recent))
                    && r4 < kMotionCurveResidual && p4 > 0.0f)
                {
                    const float ioiBpm = 60.0f / recent;
                    const float fourBpm = 60.0f / p4;
                    fourOnCandidate =
                        std::fabs (fourBpm - ioiBpm)
                            < kFastDriftToleranceLine * std::max (kMinBpm, ioiBpm)
                        && std::fabs (fourBpm - meanBpm)
                               < kFastDriftToleranceLine * std::max (kMinBpm, meanBpm);
                }
                if (! fourOnCandidate)
                {
                    dropTransitionCandidate (TempoTransitionReason::incoherent);
                    return false;
                }
            }
            // Wider-than-ordinary line changes get one extra causal interval.
            // That still answers 120 -> 160 in about one second, while making
            // a triplet/fill pair insufficient to move the whole grid. The
            // ordinary 5-25% path keeps its measured two-interval latency.
            if (lineFeed && apart > kOctaveThreshold && transitionIntervals < 2)
            {
                transitionPeriodSec = mean;
                transitionIntervals = 2;
                transitionFirstSec = transitionLastSec;
                transitionFirstStrength = transitionLastStrength;
                transitionLastSec = eventTimeSec;
                transitionLastStrength = strength;
                return false;
            }

            const int confirmedIntervals = transitionIntervals >= 2 ? 3 : 2;
            // The last two peaks that measured this are behind us and the fits
            // would
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
            // This publication already rewrote the tempo. The one-beat FISSO
            // hold is only for the release that did not.
            stepFourHoldBpm = 0.0f;

            transitionState = TempoTransitionState::rapid;
            transitionReason = TempoTransitionReason::confirmed;
            // Published in the tempo the rest of the hypothesis is reported in,
            // half/double request included, so a consumer can hand it straight
            // to a clock. `mean` is a measured interval and is not that.
            transitionPeriodSec = 60.0f / bpm;
            transitionIntervals = confirmedIntervals;
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
            transitionRefitBeats =
                kShortFit + (lineFeed ? kTransitionCombLagBeats : 0);
            transitionLastSec = eventTimeSec;
            transitionLastStrength = strength;
            ++transitionSerial;
            resetMotionShadow (false, TempoMotionVeto::transition);
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
    // The abrupt detector deliberately sees off-grid peaks, because a real
    // step initially puts every new beat off the old grid. That same privilege
    // lets a fill present two coherent subdivisions while the accepted beats
    // already prove a ramp in the opposite direction. Such a pair is not a
    // new tempo: it must first replace the existing two-beat causal direction.
    // A step out of stable tempo has no direction to oppose and is unchanged.
    if (opposesProvenMotion (candidateBpm))
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
    const float maxDelta = lineFeed ? kTransitionMaxRelativeDeltaLine
                                    : kTransitionMaxRelativeDeltaRoom;
    if (delta > maxDelta)
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
    motionTracker.quarantineShape();
    motionShadow = motionTracker.output();
    motionBridgeAuthority = 0.0f;
    motionBridgeAnchorBpm = 0.0f;
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

bool BeatDecoder::observeGridStep() noexcept
{
    // The interval detector compares consecutive intervals, so its noise is
    // twice the onset scatter and it stands down entirely once 3 * jitter
    // passes 5%. Measured with 6 ms of onset scatter at 120 BPM, a +10% step
    // never opened a candidate and took ~4 s through the ordinary release;
    // clean 3-5% steps took 8-13 s because they are below its smallest step.
    //
    // This asks a different question of the same accepted beats. Extrapolate
    // the line fitted through the beats up to a pivot; after a real step the
    // newest beats leave that line by k * step (k = 1, 2, 3...), while a
    // drummer drop or a late mix leaves it by a constant, and one pushed
    // stroke leaves it once. Two to four quarters, earliest that proves it.
    if (! lineFeed || ! established || provisional || bpm < kMinBpm
        || transitionState == TempoTransitionState::rapid
        || transitionRefitBeats > 0)
        return false;

    for (int m = 2; m <= kGridStepMaxBeats; ++m)
    {
        if (beatFilled < kShortFit + m)
            return false;

        // p4/p4b: the last four beats before the pivot and the four before
        // those. A step comes out of a steady tempo; a ramp or a window tilted
        // by an onset offset does not.
        float p8 = 0, res8 = 0, cov8 = 0, gap8 = 1, p4 = 0, p4b = 0, res4 = 0, cov4 = 0;
        double anchor = -1, anchor4 = -1;
        if (! fitPeriodBefore (kShortFit, p8, res8, cov8, anchor, &gap8, m)
            || cov8 < 0.85f || gap8 != 1.0f
            || ! fitPeriodBefore (4, p4, res4, cov4, anchor4, nullptr, m)
            || ! fitPeriodBefore (4, p4b, res4, cov4, anchor4, nullptr, m + 4))
            continue;
        const double period = p8;

        // Scatter of this song, not of these eight beats alone: the 24-beat
        // residual before the pivot when there is one. Floor as the line path.
        double sigma = std::max (static_cast<double> (res8), kGridStepSigmaFloor);
        float pl = 0, resl = 0, covl = 0;
        double anchorl = -1;
        if (fitPeriodBefore (kLongFit, pl, resl, covl, anchorl, nullptr, m))
            sigma = std::max (sigma, static_cast<double> (resl));
        sigma *= period;
        const double variance = sigma * sigma;

        const int pivot = (beatWrite - 1 - m + kBeatHistory) % kBeatHistory;
        if (std::fabs (beatTime[pivot] - anchor) > 2.5 * sigma)
            continue;   // the pivot itself is not on the old line
        // A beat-strength peak the grid rejected between these quarters means
        // the accepted ones may be every other beat of a much faster pulse:
        // 75 -> 140 lands every second new beat 7% late on the old grid, and
        // calling that a step delayed the octave path from 10.7 to 23.6 s.
        if (strongOffGridPeakSec > beatTime[pivot])
            return false;

        // Beat-strength quarters only, the rule the interval detector already
        // applies to a room: a subdivision or a ghost standing in for a missed
        // beat moved a 119.5 BPM sixteenth-note grid to 136.9 on two peaks.
        const float strengthFloor = kTransitionStrengthFraction * recentBeatStrengthMedian();
        double r[kGridStepMaxBeats + 1] {};
        double sk = 0, skk = 0;
        double previous = beatTime[pivot];
        bool consecutive = true;
        for (int k = 1; k <= m; ++k)
        {
            const double t = beatTime[(pivot + k) % kBeatHistory];
            const double interval = t - previous;
            previous = t;
            consecutive &= interval > 0.7 * period && interval < 1.3 * period
                           && beatStrength[(pivot + k) % kBeatHistory] >= strengthFloor;
            r[k] = t - (anchor + k * period);
            sk += k * r[k];
            skk += k * k;
        }
        if (! consecutive)
            continue;

        const double step = sk / skk;
        const double rel = step / period;
        if (std::fabs (rel) < kGridStepMinimum || std::fabs (rel) > kGridStepMaximum)
            continue;

        // Every new quarter leans the same way: one pushed stroke followed by
        // one dragged stroke is not a tempo.
        bool sameWay = true;
        double sseStep = 0;
        previous = beatTime[pivot];
        double firstNewInterval = 0.0;
        double lastNewInterval = 0.0;
        for (int k = 1; k <= m; ++k)
        {
            const double t = beatTime[(pivot + k) % kBeatHistory];
            const double newInterval = t - previous;
            if (k == 1)
                firstNewInterval = newInterval;
            lastNewInterval = newInterval;
            sameWay &= (newInterval - period) * step > 0.0;
            previous = t;
            sseStep += (r[k] - step * k) * (r[k] - step * k);
        }
        // The competing explanation is a displacement that starts at any of
        // these quarters (on the grid before it, constant after): measured on
        // a +44 ms drummer drop, a shift at the second quarter plus jitter
        // passed a step test that only compared against a shift at the first.
        double sseOffset = 1.0e30;
        for (int from = 1; from <= m; ++from)
        {
            double sse = 0, shift = 0;
            for (int k = from; k <= m; ++k)
                shift += r[k];
            shift /= m - from + 1;
            for (int k = 1; k <= m; ++k)
                sse += k < from ? r[k] * r[k] : (r[k] - shift) * (r[k] - shift);
            sseOffset = std::min (sseOffset, sse);
        }

        // Explained by a step through the pivot, not by an offset or an
        // outlier, and not a ramp. Looking only behind the pivot is not
        // sufficient: a clean ramp which begins there has a perfectly flat
        // pre-history. A real step makes the new intervals stationary; a
        // ramp keeps bending them. Measured on the clean 118 -> 126 / 4 s
        // alignment case, the old test falsely published rapid on interval
        // three and pulled the clock 53 ms behind the song.
        const double postCurvature = std::fabs (lastNewInterval - firstNewInterval);
        // On a genuinely clean line the residual is at its 0.5%-period floor,
        // so a slightly tighter causal check is meaningful. With measured
        // onset scatter retain the wider guard: noise can bend two intervals
        // without turning a real step into a ramp.
        const double postCurvatureShare =
            sigma <= 1.25 * kGridStepSigmaFloor * period
                ? kGridStepPostCurvature : kGridStepCurvature;
        if (! sameWay
            || sseStep > (m + 2) * variance
            || sseOffset - sseStep < kGridStepEvidence * variance
            || std::fabs (p4 - p4b) > kGridStepCurvature * std::fabs (step)
            || postCurvature > postCurvatureShare * std::fabs (step))
            continue;

        const float newBpm = static_cast<float> (60.0 / (period + step));
        if (newBpm < kMinBpm || newBpm > kMaxBpm
            || std::fabs (newBpm - bpm) < 0.5f * kGridStepMinimum * bpm)
            continue;
        // Same veto as the interval detector: a proven causal ramp is not
        // reversed by a step candidate until accepted beats reverse it.
        if (fastDriftBeats >= 2 && fastDriftSign != 0
            && (newBpm > bpm ? 1 : -1) != fastDriftSign)
            return false;

        // Confirmed. Same publication as the interval detector, but the fit
        // keeps the pivot and every new quarter, and the grid is placed on the
        // fitted line, not on the newest onset.
        double keepTime[kGridStepMaxBeats + 1];
        float keepStrength[kGridStepMaxBeats + 1];
        float keepLowBand[kGridStepMaxBeats + 1];
        float keepHighBand[kGridStepMaxBeats + 1];
        for (int k = 0; k <= m; ++k)
        {
            keepTime[k] = beatTime[(pivot + k) % kBeatHistory];
            keepStrength[k] = beatStrength[(pivot + k) % kBeatHistory];
            keepLowBand[k] = beatLowBand[(pivot + k) % kBeatHistory];
            keepHighBand[k] = beatHighBand[(pivot + k) % kBeatHistory];
        }
        beatWrite = 0;
        beatFilled = 0;
        longWrite = 0;
        longFilled = 0;
        for (int k = 0; k <= m; ++k)
            storeBeatForFit (keepTime[k], keepStrength[k], keepLowBand[k],
                             keepHighBand[k]);

        bpm = std::clamp (newBpm, kMinBpm, kMaxBpm);
        gridAnchorSec = anchor + m * (period + step);
        foldPhaseBeats = 0;
        fastDriftBeats = 0;
        fastDriftLargeBeats = 0;
        fastDriftSign = 0;
        enterRegime (TempoRegime::live);
        stepFourHoldBpm = 0.0f;

        transitionState = TempoTransitionState::rapid;
        transitionReason = TempoTransitionReason::confirmed;
        transitionPeriodSec = 60.0f / bpm;
        transitionIntervals = m;
        transitionConfidence = static_cast<float> (
            std::clamp (1.0 - sseStep / ((m + 2) * variance), 0.0, 1.0));
        transitionRapidBeats = 0;
        transitionRapidDeadlineSec =
            timeSec + static_cast<double> (kTransitionRapidLifetimeBeats)
                          * static_cast<double> (transitionPeriodSec);
        transitionRefitBeats = kShortFit + kTransitionCombLagBeats;
        transitionFirstSec = keepTime[0];
        transitionLastSec = keepTime[m];
        ++transitionSerial;
        resetMotionShadow (false, TempoMotionVeto::transition);
        return true;
    }
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
        else if (fastOctaveAmbiguous && evenOddKickHatSubdivision (
                     beatTime, beatLowBand, kBeatHistory, beatWrite,
                     beatFilled, lineFeed))
        {
            // 76 BPM kick+hats publish 152 at the third peak. Fold only
            // when low-band even/odd means are kick vs hat. Amplitude
            // even/odd at 0.07 moved the click hashes.
            bestPeriod = raw * 2.0f;
            bestError = 0.0f;
            intervalSelfSufficient = true;
            pairedSubdivision = true;
            const int i0 = (beatWrite - 4 + kBeatHistory) % kBeatHistory;
            const int i1 = (beatWrite - 3 + kBeatHistory) % kBeatHistory;
            const float evenMean = 0.5f * (beatLowBand[i0] + beatLowBand[i2]);
            const float oddMean = 0.5f * (beatLowBand[i1] + beatLowBand[i3]);
            acquireAnchorSec = evenMean >= oddMean ? beatTime[i0]
                                                   : beatTime[i1];
        }
        else if (hatsOnlyEighthFold (beatTime, beatLowBand, beatHighBand,
                                     kBeatHistory, beatWrite, beatFilled,
                                     lineFeed))
        {
            // Flat hats: no low-band, so the kick/hat fold stays mute
            // and the HMM near 118 keeps the eighth. Fold once to the
            // quarter. A genuine >145 BPM hat-on-quarters song is the
            // same measurement and is halved too; a 168 kick track is
            // protected by low-band. Newest peak may be the levare.
            bestPeriod = raw * 2.0f;
            bestError = 0.0f;
            intervalSelfSufficient = true;
            pairedSubdivision = true;
            acquireAnchorSec = beatTime[newest];
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

void BeatDecoder::updateMotionShadow() noexcept
{
    if (motionObservedBeatSerial == beatSerial || beatFilled < 1)
        return;
    motionObservedBeatSerial = beatSerial;

    const int newest = (beatWrite - 1 + kBeatHistory) % kBeatHistory;
    int steps = 1;
    if (beatFilled >= 2)
    {
        const int previous = (newest - 1 + kBeatHistory) % kBeatHistory;
        const double elapsed = beatTime[newest] - beatTime[previous];
        const double reference = 60.0 / std::max (kMinBpm, bpm);
        steps = std::clamp (static_cast<int> (std::llround (elapsed / reference)),
                            1, 4);
    }

    TempoMotionObservation o;
    o.beatTimeSec = beatTime[newest];
    o.beatStrength = beatStrength[newest];
    o.gridQuarterSteps = steps;
    o.committedBpm = bpm;
    o.shortFitBpm = shortFitBpm;
    o.longFitBpm = longFitBpm;
    o.shortFitResidual = shortFitResidual;
    o.fitCoverage = lastFitCoverage;
    o.fitIndexGap = lastFitIndexGap;
    o.intervalJitter = recentIntervalJitter();
    o.transitionState = transitionState;
    o.transitionRefitBeats = transitionRefitBeats;
    o.lineFeed = lineFeed;
    o.fixedRegime = tempoRegime == TempoRegime::fixed;
    motionShadow = motionTracker.observe (o);

    refreshMotionBridgeAuthority();
}

void BeatDecoder::refreshMotionBridgeAuthority() noexcept
{

    // Model selection says which residual shape best explains the fixed-entry
    // window; it does not by itself guarantee that its endpoint extrapolation
    // points the same way as the newest causal tempo estimate. On real music a
    // quadratic briefly won while both short and long fits were slowing, yet
    // its endpoint predicted an acceleration and the bridge pushed the clock
    // away from the band. A professional follower may lead a proven direction,
    // never contradict the responsive fit that is already observing it.
    const float shapeDelta = motionShadow.shapePredictedBpm - bpm;
    const float shortDelta = shortFitBpm - bpm;
    const bool shapeDirectionAgrees = std::isfinite (shortFitBpm)
                                       && shortFitBpm >= kMinBpm
                                       && shortFitBpm <= kMaxBpm
                                       && std::fabs (shortDelta) > 1.0e-6f
                                       && shapeDelta * shortDelta > 0.0f;
    const bool shapeCanLead = lineFeed
                              && tempoRegime == TempoRegime::live
                              && transitionState == TempoTransitionState::stable
                              && transitionRefitBeats == 0
                              && motionShadow.shapeModel
                                     == TempoMotionShapeModel::quadratic
                              && motionShadow.shapeQuadraticWins >= 2
                              && motionShadow.shapeEvidenceMargin
                                     >= TempoMotionShape::kEvidenceMarginBic
                              && motionShadow.shapeQuadraticVsHinge
                                     >= TempoMotionShape::kEvidenceMarginBic
                              && shapeDirectionAgrees
                              && motionShadow.shapeQuarantineBeats == 0
                              && std::isfinite (motionShadow.shapePredictedBpm)
                              && motionShadow.shapePredictedBpm >= kMinBpm
                              && motionShadow.shapePredictedBpm <= kMaxBpm;
    if (shapeCanLead)
    {
        if (motionBridgeAuthority == 0.0f)
            motionBridgeAnchorBpm = bpm;
        // One quadratic window can briefly beat the hinge after an ordinary
        // step release (offset-0 gradino accumulated 17 frames of 35%
        // authority from that first verdict). Two consecutive wins is the
        // same rule that already reserved the full rail; it is still a
        // model-selection count, not a song or seed threshold. The 12 s
        // MIXER ramp already had auth=0 through its lag, so this does not
        // wait extra on the measured ramps. Neither level may jump:
        // bridgedMotionTarget() owns the hard rails.
        motionBridgeAuthority = 1.0f;
    }
    else
    {
        motionBridgeAuthority = 0.0f;
        motionBridgeAnchorBpm = 0.0f;
    }
}

void BeatDecoder::resetMotionShadow (bool full, TempoMotionVeto reason) noexcept
{
    motionTracker.reset (full, reason);
    motionShadow = motionTracker.output();
    motionObservedBeatSerial = beatSerial;
    motionBridgeAuthority = 0.0f;
    motionBridgeAnchorBpm = 0.0f;
    ioiClockLead = false;
    ioiClockLeadBeats = 0;
    ioiTargetHoldBpm = 0.0f;
    ioiTargetHoldBeats = 0;
}

float BeatDecoder::bridgedMotionTarget (float ordinaryTarget) const noexcept
{
    if (motionBridgeAuthority <= 0.0f
        || motionBridgeAnchorBpm < kMinBpm
        || ! std::isfinite (ordinaryTarget)
        || ! std::isfinite (motionShadow.shapePredictedBpm))
        return ordinaryTarget;

    const float totalRail = 0.04f * motionBridgeAnchorBpm;
    const float predicted = std::clamp (motionShadow.shapePredictedBpm,
                                        motionBridgeAnchorBpm - totalRail,
                                        motionBridgeAnchorBpm + totalRail);
    // Limit the *additional* motion contribution. The ordinary live fit keeps
    // its existing freedom; once it has already caught up, this bridge must
    // neither hold it at the four-percent rail nor pull it backwards.
    const float ordinaryDelta = ordinaryTarget - bpm;
    const float predictedDelta = predicted - bpm;
    if (predictedDelta * ordinaryDelta < 0.0f
        || std::fabs (predictedDelta) <= std::fabs (ordinaryDelta))
        return ordinaryTarget;

    const float beatRail = 0.0075f * std::max (kMinBpm, bpm);
    const float correction = std::clamp (
        motionBridgeAuthority * (predicted - ordinaryTarget), -beatRail, beatRail);
    return std::clamp (ordinaryTarget + correction,
                       std::max (kMinBpm, bpm - beatRail),
                       std::min (kMaxBpm, bpm + beatRail));
}

void BeatDecoder::updateTempo() noexcept
{
    // "L'1 è QUI" no longer wipes the fits: the lattice stays, so there
    // is no new window to hold the tempo across. If a hold is still
    // armed, restore the tempo (and the fixed anchor the next beat
    // would chase) until it runs out.
    struct PinDeclaredTempo
    {
        BeatDecoder& self;
        const float heldBpm;
        const float heldAnchor;
        const bool on;
        ~PinDeclaredTempo() noexcept
        {
            if (! on)
                return;
            self.bpm = heldBpm;
            self.fixedAnchorBpm = heldAnchor;
            if (self.barTempoHoldBeats > 0)
                --self.barTempoHoldBeats;
        }
    } declaredPin { *this, bpm, fixedAnchorBpm, barTempoHoldBeats > 0 };

    // Keep the proven tau for one short window after a door so the
    // PLL does not drop back to 0.90 s between the retarget and the
    // peak. Unknown persists too, but not onto a post-gap 8-beat that
    // is the same lattice as the 4-beat — that is the 73 yank.
    const bool persistLead = ioiClockLeadBeats > 0
                             && (tempoRegime == TempoRegime::live
                                 || tempoRegime == TempoRegime::unknown);
    ioiClockLead = false;
    if (tempoRegime == TempoRegime::fixed)
    {
        ioiTargetHoldBpm = 0.0f;
        ioiTargetHoldBeats = 0;
    }
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
    else if (provisional && intervalAcquired && beatFilled >= 4 && lineFeed
             && (evenOddKickHatSubdivision (beatTime, beatLowBand, kBeatHistory,
                                            beatWrite, beatFilled, lineFeed)
                 || hatsOnlyEighthFold (beatTime, beatLowBand, beatHighBand,
                                        kBeatHistory, beatWrite, beatFilled,
                                        lineFeed)))
    {
        // Once: after the fold the committed tempo is no longer the
        // eighth. Even clicks and the matrix pass lowBand=0 and
        // highBand=0, so both peeks are false and tryFastAcquire is
        // not called again.
        const int newestI = (beatWrite - 1 + kBeatHistory) % kBeatHistory;
        const int olderI = (beatWrite - 2 + kBeatHistory) % kBeatHistory;
        const float ioi = static_cast<float> (beatTime[newestI]
                                              - beatTime[olderI]);
        const float lastBpm = ioi > 0.0f ? 60.0f / ioi : 0.0f;
        const bool stillOnEighth = lastBpm > 145.0f
                                   && lastBpm * 0.5f >= kMinBpm
                                   && std::fabs (std::log2 (std::max (kMinBpm, bpm)
                                                            / lastBpm))
                                          < 0.12f;
        if (stillOnEighth)
            tryFastAcquire();
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
    // The alternation above fails on a full mix: a live band washes out the
    // loud/quiet between quarters and hats, so the veto never stands down and a
    // doubled grid plays double for the whole acquisition. Measured on the live
    // Sally segment: the fold read 104 at salience ~1.0 for twenty seconds
    // while the committed sat at 208 and the alternation stayed under 0.35.
    //
    // Persistence is the second proof. A fold that names the slower octave, at
    // high salience, beat after beat, is not a glitch and not a transient
    // subharmonic - it is the level. This only lifts the veto; the snap below
    // still wants its own `snapBeats` of votes and the fold's salience.
    constexpr int kProvenSlowerOctaveBeats = 10;
    const bool transitionOwnsRate = transitionState == TempoTransitionState::rapid
                                    || transitionRefitBeats > 0;
    const bool combSlower = combRawBpm > kMinBpm && combRawBpm < bpm * 0.70f;
    // The persistence proof is only for a *clean* half - the octave. A comb
    // reading a third or a fourth (a subharmonic) never earns it: that is a
    // stale comb, not a level, and the 120 -> 160 step depends on it staying
    // vetoed while the fit rebuilds.
    const bool combCleanHalf = combSlower && halfError < 0.25f;
    if (combCleanHalf && ! transitionOwnsRate && tempo.salience() > kOctaveSnapSalience)
        combHalfBeats = std::min (combHalfBeats + 1, kProvenSlowerOctaveBeats);
    else
        combHalfBeats = std::max (0, combHalfBeats - 1);
    const bool halfProven = combHalfBeats >= kProvenSlowerOctaveBeats;
    const bool unprovenSlowerOctave = intervalAcquired && gridHealthy && gridIsDense
                                      && ! gridLooksLikeSubdivision
                                      && combSlower
                                      && ! halfProven;
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
    // A confirmed causal transition owns the rate until its eight-beat fit has
    // re-formed. The fold averages seconds of activations and necessarily still
    // names the tempo just left during this window. Letting that stale answer
    // enter the snap vote undid a newly proven 120 -> 160 change after six
    // beats, sent the decoder to 53 BPM and never recovered. This is not an
    // octave veto: once fresh fits exist the ordinary level arbitration resumes.
    const bool combDisagrees = combReady && combMayCorrect && ! unprovenSlowerOctave
                               && ! transitionOwnsRate
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
        // Post-pause leftover comb: 100 vs 150 is not an octave argument
        // (log2 0.585), so sounding does not hold the level, and eight
        // fixed-regime votes snap onto the leftover (fixture D t=36.02,
        // unknown, history wiped). Real steps have no 2.5-beat hole.
        // The synthetic bank never sets sounding. IOI+4 on that comb
        // is a real level the fits have seen.
        bool refusePostHoleComb = false;
        if (sounding && postHoleReopenSec >= 0.0
            && combRawBpm > kMinBpm && bpm > kMinBpm
            && std::fabs (std::log2 (combRawBpm / bpm)) > kStaleGridThreshold)
        {
            bool fourOnComb = false;
            float recent = 0.0f, p4 = 0.0f, r4 = 1.0f, c4 = 0.0f;
            double a4 = -1.0;
            if (recentPeriod (recent) && recent > 0.0f
                && fitPeriodBefore (4, p4, r4, c4, a4, nullptr, 0,
                                    static_cast<double> (recent))
                && r4 < kMotionCurveResidual && p4 > 0.0f)
            {
                const float ioiBpm = 60.0f / recent;
                const float fourBpm = 60.0f / p4;
                fourOnComb =
                    std::fabs (fourBpm - ioiBpm)
                        < kFastDriftToleranceLine * std::max (kMinBpm, ioiBpm)
                    && std::fabs (fourBpm - combRawBpm)
                           < kFastDriftToleranceLine * std::max (kMinBpm, combRawBpm);
            }
            refusePostHoleComb = ! fourOnComb;
        }
        if (refusePostHoleComb)
        {
            octaveMismatchBeats = 0;
            octaveVoteBpm = 0.0f;
        }
        else
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
        combHalfBeats = 0;
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
        straddleSinceSec = -1.0;
        anchorBlend = 0.0;
        longWindowStraddles = false;
        lastFitCoverage = 0.0f;
        lastFitIndexGap = 1.0f;
        longFitBpm = 0.0f;
        shortFitBpm = 0.0f;
        shortFitResidual = 1.0f;
        resetMotionShadow (true, TempoMotionVeto::octaveOrGrid);
        return;
        }
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
            longFitPeriodHeld = false;
            lastAcceptedLowBand = 0.0f;
            kitBodyHeard = false;
            kitBodyLastSec = -1.0;
            hatGridHolding = false;
            stepFourHoldBpm = 0.0f;
    liveFourHoldBpm = 0.0f;
            postHoleReopenSec = -1.0;
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
            combHalfBeats = 0;
            octaveVoteBpm = 0.0f;
            beatsOnLevel = 0;
            fastDriftBeats = 0;
            fastDriftLargeBeats = 0;
            fastDriftSign = 0;
            lastFitResidual = 1.0f;
            straddleSinceSec = -1.0;
            anchorBlend = 0.0;
            longWindowStraddles = false;
            lastFitCoverage = 0.0f;
            lastFitIndexGap = 1.0f;
            longFitBpm = 0.0f;
            shortFitBpm = 0.0f;
            shortFitResidual = 1.0f;
            enterRegime (TempoRegime::unknown);
            resetMotionShadow (true, TempoMotionVeto::octaveOrGrid);
            return;
        }
    }

    float longPeriod = 0.0f, longResidual = 0.0f, longCoverage = 0.0f;
    float shortPeriod = 0.0f, shortResidual = 0.0f, shortCoverage = 0.0f;
    double longAnchor = -1.0, shortAnchor = -1.0;
    float longIndexGap = 1.0f, shortIndexGap = 1.0f;
    const double rulerGuess = pulseIndexGuess();
    const bool haveLong = fitPeriodBefore (kLongFit, longPeriod, longResidual, longCoverage,
                                           longAnchor, &longIndexGap, 0, rulerGuess);
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
    bool haveShort = fitPeriodBefore (kShortFit, shortPeriod, shortResidual,
                                            shortCoverage, shortAnchor, &shortIndexGap,
                                            0, rulerGuess);

    // Where the grid is, from the same two fits and for the same reason the
    // tempo comes from them: the phase used to be `lastBeatSec`, one accepted
    // peak, so every beat's own timing error was handed to the clock whole and
    // as a step. Measured against 22 ms of onset jitter the reported phase
    // carried 22 ms rms of it, in jumps of up to 0.18 of a beat. Which of the
    // two fits carries it follows the regime, exactly as the tempo does: a held
    // tempo can average over twenty-four beats, a live one cannot.
    // Whether the twenty-four beat window is lying across a tempo event.
    // See kStraddleResidualRatio; computed here because the *phase* anchor
    // has the same problem as the tempo and for the same reason. On the dip
    // fixture the true displacement peaks at 107 ms and the decoder reported
    // 46 - the clock cannot give back what it is not told about, and what it
    // was being told came from the straddling line.
    //
    // The last clause is the one that separates a dip from a ramp. On a ramp
    // the short fit is *drifting away* from the tempo the decoder has already
    // committed to, and the straddle must not chase it - the long window is
    // not lying, it is simply longer, and handing the phase anchor to the
    // noisy short fit is what put the anchor wobble into the ramp (measured
    // 146 -> 237 ms on `120 -> 132 in 20 s`). Only a short fit that has come
    // back *to the committed tempo* - the event has settled - proves the long
    // window stale.
    const bool shortAgreesCommitted =
        std::fabs (shortPeriod - 60.0f / std::max (kMinBpm, bpm))
        < kStraddleAgreeRatio * (60.0f / std::max (kMinBpm, bpm));
    const bool straddleNow =
        haveLong && haveShort && longPeriod > 0.0f && shortPeriod > 0.0f
        && shortResidual * kStraddleResidualRatio < longResidual
        && std::fabs (shortPeriod - longPeriod)
               > kStraddleBpmDisagree * longPeriod
        && shortAgreesCommitted;
    // Held, not taken on sight. Both halves can line up for a frame or two
    // on perfectly steady material - the eight-beat line genuinely does fit
    // its own eight beats better now and then - and acting on that put 2.5
    // times more jitter into the phase of a click-steady fixture (1.5 ms
    // rms to 3.7). A real straddle lasts as long as the event is inside the
    // long window, which is seconds. Noise does not hold for four tenths of
    // one, which is the same argument the trim's kDriftAgreeing makes.
    if (! straddleNow)
        straddleSinceSec = -1.0;
    else if (straddleSinceSec < 0.0)
        straddleSinceSec = timeSec;
    longWindowStraddles = straddleNow && straddleSinceSec >= 0.0
                          && timeSec - straddleSinceSec >= kStraddleHoldSec;

    // Eased between the two, never switched between them.
    //
    // Changing which fit names the phase is a *step* in the published
    // target, and a step is the one thing this chain is built never to
    // make: `BeatTracker` stops and rejoins above a third of a beat. Taken
    // as a switch it cost grid jerk on two of the five live extracts
    // (2.40% -> 2.73% and 2.65% -> 3.39% rms) - the lurch this exists to
    // remove, reintroduced by the removal. A weight that walks across in
    // about half a second is the same correction with no edge in it.
    const double step = 1.0 / std::max (1.0, kAnchorBlendSec * kFramesPerSecond);
    anchorBlend = std::clamp (anchorBlend + (longWindowStraddles ? step : -step),
                              0.0, 1.0);
    if (tempoRegime == TempoRegime::fixed && haveLong)
        gridAnchorSec = haveShort
                            ? longAnchor + (shortAnchor - longAnchor) * anchorBlend
                            : longAnchor;
    else if (haveShort)
        gridAnchorSec = shortAnchor;
    else if (haveLong)
        gridAnchorSec = longAnchor;

    longFitBpm = haveLong ? 60.0f / longPeriod : 0.0f;
    shortFitBpm = haveShort ? 60.0f / shortPeriod : 0.0f;
    shortFitResidual = haveShort ? shortResidual : 1.0f;
    // 161171 Door D recovers, then !haveShort at t=69.16 (ioi/i4
    // gone, long gone) and coasts to ph 116 at t=70.70. The hold
    // already commits BPM; gridAnchor does not move without a
    // fit. Re-arm the short window from this crest: IOI, else a
    // clean 4-beat on that IOI, else the held 4-beat. Live Door D
    // hold only — 0 offset-0 fisso/gradino Door D frames; bir>=8
    // live !haveShort lights 1009 and 210467.
    if (! haveShort && lineFeed
        && tempoRegime == TempoRegime::live
        && ioiTargetHoldBeats > 0 && ioiTargetHoldBpm > kMinBpm
        && lastBeatSec >= 0.0)
    {
        float rearm = ioiTargetHoldBpm;
        float recent = 0.0f;
        if (recentPeriod (recent) && recent > 0.0f)
        {
            float p4 = 0.0f, r4 = 1.0f, c4 = 0.0f;
            double a4 = -1.0;
            if (fitPeriodBefore (4, p4, r4, c4, a4, nullptr, 0,
                                 static_cast<double> (recent))
                && r4 < kDoorDFourResidual && p4 > 0.0f)
                rearm = 60.0f / p4;
            else
                rearm = 60.0f / recent;
        }
        if (rearm > kMinBpm)
        {
            shortFitBpm = rearm;
            shortFitResidual = 1.0f;
            gridAnchorSec = lastBeatSec;
            haveShort = true;
        }
    }
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

    // The ordinary eight-beat line reports the average slope of its window,
    // about three and a half beats in the past. A smooth tempo change has a
    // second signature: the beat dates curve, so a longer quadratic can ask
    // for the slope at the newest accepted beat. Onset jitter makes that
    // endpoint too noisy to drive the tempo directly (the fixed controls show
    // isolated +/-0.3%/beat readings), so curvature remains diagnostic only.
    // The globally safe selector did not justify releasing a held tempo, and
    // the room path never enters this diagnostic.
    float motionPeriod = 0.0f;
    motionFitBpm = 0.0f;
    motionFitRate = 0.0f;
    motionFitResidual = 1.0f;
    motionFitImprovement = 0.0f;
    const bool haveMotionCurve = lineFeed
                                 && fitPeriodCurve (16, motionPeriod, motionFitRate,
                                                    motionFitResidual,
                                                    motionFitImprovement);
    if (haveMotionCurve)
        motionFitBpm = 60.0f / motionPeriod;

    const float motionDeviation = haveMotionCurve && bpm > kMinBpm
                                      ? (motionFitBpm - bpm) / bpm
                                      : 0.0f;
    const float shortDeviation = haveShort && bpm > kMinBpm
                                     ? (shortFitBpm - bpm) / bpm
                                     : 0.0f;
    const bool coherentMotionCurve = haveMotionCurve && haveShort
                                     && motionFitResidual < kMotionCurveResidual
                                     && shortResidual < kMotionCurveResidual
                                     && motionFitImprovement
                                            >= kMotionCurveImprovement
                                     && std::fabs (motionFitRate / bpm)
                                            > kMotionCurveRate
                                     && std::fabs (motionDeviation)
                                            > kMotionCurveDeviation
                                     && std::fabs (shortDeviation)
                                            > kMotionCurveDeviation
                                     && motionFitRate * motionDeviation > 0.0f
                                     && motionFitRate * shortDeviation > 0.0f;
    if (coherentMotionCurve)
    {
        const int sign = motionFitRate > 0.0f ? 1 : -1;
        if (sign == motionFitDirection)
            motionFitEvidence = std::min (motionFitEvidence + 1, kMotionCurveBeats);
        else
        {
            motionFitDirection = sign;
            motionFitEvidence = 1;
        }
    }
    else
    {
        motionFitEvidence = 0;
        motionFitDirection = 0;
    }

    if (tempoRegime == TempoRegime::fixed && lineFeed && haveShort && haveMotionCurve
        && bpm > kMinBpm)
    {
        const float heldDev = (shortFitBpm - bpm) / bpm;
        const bool gatedBeat =
            std::fabs (heldDev) > kFastDriftToleranceLine
            && motionFitImprovement >= kMotionCurveWalkImprovement
            && motionFitImprovement < kMotionCurveImprovement
            && shortFitResidual > kMotionCurveResidual
            && shortFitResidual < kMotionCurveWalkResidual
            && motionFitResidual < kMotionCurveResidual
            && std::fabs (motionFitRate / bpm) > kMotionCurveWalkRate
            && motionFitRate * heldDev > 0.0f;
        if (gatedBeat)
            fixedWalkRun = std::min (fixedWalkRun + 1, 8);
        else
            fixedWalkRun = 0;
    }

    updateMotionShadow();
    checkGridPhase (60.0f / std::max (kMinBpm, bpm));

    if (! haveShort)
    {
        // The fit is gone and the newest interval is a hole below the
        // legal tempo. The quarter before it and the comb already name
        // the same new tempo, and the committed number has not moved.
        // 210467 at t=53.90: hole 20.7, previous quarter 54.6, comb 53.9,
        // bpm still 64. The ordinary pull takes seven tenths of that and
        // leaves the clock fast. Taking the comb in full on this frame
        // only — the grid stays where it is. Moving the grid onto the
        // beat that closed the hole held the phase near 400 ms and raised
        // the mean, 108.7 → 130.3. Spending the same comb at 1.0 with no
        // hole test moved a ramp. One hit in the quick bank.
        if (lineFeed && tempoRegime == TempoRegime::live
            && beatFilled >= 3 && bpm > kMinBpm
            && combReady && combBpm > kMinBpm)
        {
            const int n0 = (beatWrite - 1 + kBeatHistory) % kBeatHistory;
            const int n1 = (beatWrite - 2 + kBeatHistory) % kBeatHistory;
            const int n2 = (beatWrite - 3 + kBeatHistory) % kBeatHistory;
            const float newer = static_cast<float> (beatTime[n0] - beatTime[n1]);
            const float older = static_cast<float> (beatTime[n1] - beatTime[n2]);
            if (newer > 0.0f && older > 0.0f)
            {
                const float olderBpm = 60.0f / older;
                const float newerBpm = 60.0f / newer;
                const bool combLeft = std::fabs (std::log2 (combBpm / bpm)) > std::log2 (1.08f);
                const bool olderAgrees = olderBpm > kMinBpm
                                      && std::fabs (std::log2 (olderBpm / combBpm)) < std::log2 (1.04f);
                const bool newerIsGap = std::fabs (std::log2 (newerBpm / combBpm)) > 0.40f;
                if (combLeft && olderAgrees && newerIsGap)
                {
                    commit (combBpm, 1.0f);
                    if (persistLead)
                    {
                        ioiClockLead = true;
                        --ioiClockLeadBeats;
                    }
                    else
                        ioiClockLeadBeats = 0;
                    return;
                }
            }
        }
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
        if (ioiTargetHoldBeats > 0 && ioiTargetHoldBpm > kMinBpm
            && tempoRegime != TempoRegime::fixed
            && transitionState != TempoTransitionState::rapid)
        {
            // Unknown Door B hold through a kit gap: the comb is the
            // leftover lattice (216604 t=47-49 at 71 vs T 66). Live
            // A/B/C hold fattened family p95; this is the Door D hold, in
            // unknown, while the 4-beat is already on the pulse.
            // Do not spend the beat window here: a kit-gap false
            // peak is not a Door D beat. Offset-0 identity vs
            // decrementing; keep the window for haveShort.
            commit (ioiTargetHoldBpm, kRateDoorHold);
            ioiClockLead = true;
            ioiClockLeadBeats = kShortFit;
            return;
        }
        if (combReady && foldMayPull && tempoRegime != TempoRegime::fixed
            && transitionState != TempoTransitionState::rapid)
        {
            // Unknown kit-gap: leftover comb is faster than the line
            // (216604 t=47, 71 vs T 67). Skipping that commit
            // fattened p995 (t=48.56 143→11 ms is the pull).
            // Acquiring climbed 70.1→71.2 while T fell through 67;
            // live rate still follows it. When the comb has already
            // receded *below* the committed number the leftover
            // lattice is gone; 0.70 left 1.6 BPM on the table at
            // t=51.56. bpm<75, kLongFit, comb<bpm, >0.5%: 0
            // offset-0 fisso/gradino (48523 is 77 BPM; 119794 is 0.0%).
            float gapRate = kRateAcquiring;
            if (lineFeed && tempoRegime == TempoRegime::unknown
                && bpm < 75.0f && bpm > kMinBpm
                && beatsInRegime >= kLongFit
                && combBpm > kMinBpm && combBpm < bpm
                && (bpm - combBpm) > 0.005f * bpm)
                gapRate = 1.0f;
            else if (lineFeed && tempoRegime == TempoRegime::unknown
                     && bpm < 75.0f && bpm > kMinBpm
                     && beatsInRegime >= kLongFit
                     && combBpm > bpm
                     && (combBpm - bpm) > 0.005f * bpm)
            {
                // Skip leftover comb entirely fattened p995 (t=48.56
                // 143→11 ms is that commit). Live rate still follows
                // it; 0.70 climbed 70.1→71.2 while T fell through 67.
                // 0.30 / 0.15 / 0.10 / 0.05 each KEEP vs the previous.
                // 0.5% floor: fisso 119794 is 0.0%.
                gapRate = kRateLeftoverComb;
            }
            commit (combBpm, gapRate);
        }
        if (persistLead)
        {
            ioiClockLead = true;
            --ioiClockLeadBeats;
        }
        else
            ioiClockLeadBeats = 0;
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
    // These are published to the clock. Do not let a value from the last valid
    // interval survive a dropout or a history reset and masquerade as current
    // motion while no responsive measurement exists.
    const float prevFastDeviation = lastFastDeviation;
    lastFastDeviation = 0.0f;
    lastIntervalDeviation = 0.0f;
    if (recentPeriod (recent))
    {
        // On a direct feed the eight-beat fit is both smooth enough to carry a
        // persistent direction and current enough to release a held tempo. The
        // median of three raw intervals is faster on paper but one interpolated
        // peak can reverse its sign, repeatedly erasing the very run a ramp is
        // trying to prove. Keep that raw median for room input, where the long
        // fit cannot make reflections into clean evidence.
        const float intervalBpm = 60.0f / recent;
        const float intervalDeviation = (intervalBpm - bpm) / std::max (kMinBpm, bpm);
        lastIntervalDeviation = intervalDeviation;
        const float recentBpm = lineFeed && haveShort ? shortFitBpm : intervalBpm;
        const float fastDeviation = (recentBpm - bpm) / std::max (kMinBpm, bpm);
        lastFastDeviation = fastDeviation;
        const float fastTolerance = lineFeed ? kFastDriftToleranceLine
                                             : kFastDriftToleranceRoom;
        // The short fit alone cannot distinguish a real rate move from an
        // acoustic phase offset: when a drummer drops out, the accepted peaks
        // may all slide late and the fit window reports a temporary slowdown
        // even though the new intervals are still at the old tempo. A ramp is
        // different: its newest intervals keep moving in the same direction.
        // Require that causal evidence on a direct feed. Its small floor is
        // below the 1.2% fit decision but above interpolation dust. A tightly
        // placed line-feed fit earns release after two net votes; a looser fit
        // still needs three, and both require the long-window direction below.
        constexpr float kLineIntervalSupport = 0.004f;
        const bool intervalSupports = ! lineFeed
                                      || (std::fabs (intervalDeviation) > kLineIntervalSupport
                                          && intervalDeviation * fastDeviation > 0.0f);
        const bool sameWay = fastDriftSign != 0
                             && fastDeviation * static_cast<float> (fastDriftSign) > 0.0f;
        const bool growingClean = lineFeed && haveShort
            && fastDriftBeats >= 1
            && haveMotionCurve
            && shortFitResidual <= kLineCleanIoiOverride
            && std::fabs (fastDeviation) > fastTolerance
            && std::fabs (prevFastDeviation) > fastTolerance
            && fastDeviation * prevFastDeviation > 0.0f
            && std::fabs (fastDeviation) > std::fabs (prevFastDeviation) + 1.0e-4f
            && motionFitRate * fastDeviation > 0.0f
            && std::fabs (motionFitRate / bpm) > kMotionCurveWalkRate
            && motionFitImprovement >= 0.08f;
        if (std::fabs (fastDeviation) > fastTolerance
            && (intervalSupports || growingClean))
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
        else if (lineFeed && haveShort && haveMotionCurve
                 && fastDriftBeats > 0 && sameWay
                 && std::fabs (fastDeviation) > fastTolerance
                 && shortFitResidual <= kLineCleanIoiOverride
                 && motionFitRate * fastDeviation > 0.0f
                 && motionFitImprovement >= 0.08f)
        {
            // Short fit still on the same side with a clean residual and an
            // agreeing quadratic: the newest interval is the liar. Spending
            // here dropped 120->132 seed 1078 from two votes to zero between
            // t=25 and t=26 while the 8-beat line was still 1.9% fast.
            // The two-vote path still requires intervalAgreesNow, so holding
            // cannot repeat the 128->120 56.5 ms overshoot.
        }
        else
        {
            // One jittered line-feed interval may fail the support test inside
            // a genuine ramp. Spend one vote rather than erasing all the
            // preceding evidence; room input retains the strict reset.
            if (lineFeed && fastDriftBeats > 0)
            {
                --fastDriftBeats;
                fastDriftLargeBeats = std::max (0, fastDriftLargeBeats - 1);
                if (fastDriftBeats == 0)
                    fastDriftSign = 0;
            }
            else
            {
                fastDriftBeats = 0;
                fastDriftLargeBeats = 0;
                fastDriftSign = 0;
            }
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

    // After a direct-feed motion release, a quiet long window can look fixed
    // again while the responsive fit is already following the next part of the
    // accelerando/rallentando. Re-entering fixed there freezes the older rate
    // and turns a smooth change into a late correction. Reuse the existing
    // cross-window agreement rail before *re*-certifying stability; acquisition
    // keeps its established behaviour and room/speaker input is untouched.
    const bool directFitsSettled = ! lineFeed
                                   || (haveShort && haveLong
                                       && std::fabs (shortFitBpm - longFitBpm)
                                              <= kStraddleBpmDisagree
                                                     * std::max (kMinBpm,
                                                                 longFitBpm));

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
            // The fold is deliberately NOT consulted here, and that was
            // measured twice rather than assumed.
            //
            // `anchorError` is the long fit against a running mean of the long
            // fit, so both sides come from the number the held tempo is derived
            // from; the obvious repair is to ask the one source outside that
            // loop, which is what the stale-grid watchdog's own comment says
            // the fold is for. It works on the case it was written for - at
            // 60 BPM the fold and the short fit both sat 2.7% under a frozen
            // 61.08 for seconds while `anchorError` read 0.34% - and it breaks
            // a genuine step. On `probe_tempo_step`'s unprotected 120 -> 160
            // the fold names 120 for seconds after the change, so this test
            // reads 25% the wrong way, the regime is released with the stale
            // fold as the loudest voice, and the run collapses to 53.3 BPM and
            // never returns, against 23.6 s to the right tempo. Standing the
            // term down during a confirmed transition does not save it: the
            // unprotected step has no transition to stand down for.
            //
            // This is the same wall the skill records for the transition gate -
            // at a change the fold is right about the tempo that has been left,
            // and nothing in it separates that from being right about a tempo
            // that never moved. The exit budget below is a bar instead, which
            // costs no discrimination at all.
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
            // In a room its median of three intervals may drift 2.4% one way
            // several times a minute, so that path keeps the conservative
            // threshold. On a direct feed the smoother eight-beat fit carries
            // the size and direction, while the newest raw intervals only
            // prove causality. A real change also leans the twenty-four-beat
            // window the same way; ordinary jitter does not.
            const bool windowAgrees = haveWindow && fastDriftSign != 0
                                      && trend * static_cast<float> (fastDriftSign) > 0.0f
                                      && std::fabs (trend) > (lineFeed ? kLiveTrend * 0.25f
                                                                      : kLiveTrend)
                                      && (lineFeed || (moving
                                                       && std::fabs (trend) > spread * 0.85f));

            // Three net direct-feed votes already contain two independent
            // causal facts: the responsive fit has left the held tempo and the
            // newest measured intervals support the same direction. Waiting
            // for the twenty-four-beat window as a third copy of that fact was
            // audible on iPad: at 95.54 s the fit/interval read -2.87/-2.27%
            // with three votes, yet FISSO held until 97.21 s. Keep the quicker
            // two-vote path dependent on a very clean fit plus the long window;
            // only the full three-vote proof may release a direct feed alone.
            const bool causalLineRelease =
                lineFeed && fastDriftBeats >= kFastBeatsToLeaveFixed;
            // One kFixedMaxStep walk in this residual band moved seed 101 at
            // t=25 and still left the 12 s four-seed mean 40.3->41.1. Releasing
            // to VIVO on the same two-beat strain (residual just above
            // kMotionCurveResidual, weak quadratic, one vote) lets the live
            // rate chase instead. Offset-0 matrix hashes were identical with
            // this gate as a walk, so it does not fire on noisy flats.
            const bool strainLineRelease =
                lineFeed && fixedWalkRun >= 2 && fastDriftBeats >= 1;
            // One agreeing interval plus a 50% quadratic, without
            // waiting for the 24-beat window, when the 4-beat is still
            // on the 8-beat. A second 1.2% vote arrives after the
            // 4-beat has left that lattice (offset-0 ramps 1.9%) and
            // the gate closes. A step's 4-beat has already left
            // (offset-0 gradino 2.0/3.7/14.9%). The previous FISSO
            // walk on 4/8 agreement lit gradino because it moved the
            // held number; this only releases the regime. Silent on
            // offset-0 fisso/gradino.
            bool curveOnLatticeRelease = false;
            if (lineFeed && haveShort && haveMotionCurve
                && beatsInRegime >= kLongFit
                && fastDriftBeats >= 1
                && motionFitImprovement >= kMotionCurveImprovement
                && std::fabs (motionDeviation) > kLeaveFixedError)
            {
                float p4 = 0.0f, r4 = 1.0f, c4 = 0.0f;
                double a4 = -1.0;
                // Same IOI-indexed 4-beat the silent census used. The
                // committed-period fit sits on the held grid and
                // misses a 4-beat that is already on the moving pulse.
                if (recent > 0.0f
                    && fitPeriodBefore (4, p4, r4, c4, a4, nullptr, 0,
                                        static_cast<double> (recent))
                    && r4 < kMotionCurveResidual && p4 > 0.0f)
                {
                    const float fourBpm = 60.0f / p4;
                    curveOnLatticeRelease =
                        std::fabs (fourBpm - shortFitBpm)
                            < kCurveLatticeAgree
                                  * std::max (kMinBpm, shortFitBpm);
                }
            }

            bool slowIoiFixedRelease = false;
            if (lineFeed && haveShort && recent > 0.0f
                && bpm < 75.0f && bpm > kMinBpm
                && beatsInRegime >= kShortFit + 4
                && haveMotionCurve
                && motionFitImprovement >= 0.45f
                && fastDriftBeats >= 1)
            {
                const float ioiBpm = 60.0f / recent;
                // 0.035 + g 0.50 is KEEP (192847 t=29.92). 0.028 with
                // |short−held|>1.2% and no extra quadratic was
                // identity on p95 when the hole was this seed's tail.
                // The leave frame is now the seed max (145 ms at
                // t=29.92). g>=0.45 and |IOI−held|>0.025: 0 offset-0
                // fisso/gradino, one continuo (192847 t=29.04 g=0.49).
                // g 0.40 / 0.020 is identity (t=28.06 fails
                // intervalAgreesNow).
                slowIoiFixedRelease =
                    std::fabs (ioiBpm - bpm) / bpm > 0.025f;
            }

            // Three earlier things were tried here to make a tempo change land
            // sooner and none shipped. `VPAlign`'s tempo bench measures all of
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
            // *step* costs about five seconds. That percentage-only probe hid
            // phase debt on ramps: `VPAlign --ramps` later measured a direct
            // 100 -> 110 / 30 s ramp at 140.7 ms worst before this causal
            // release and 84.7 ms after it, while fixed 100/130 controls stayed
            // at 33.3/22.0 ms. That is why the direct-feed exception above is
            // narrower than simply lowering the room thresholds.
            //
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
            const int votesToLeave = (lineFeed && shortFitResidual < kFastLineCleanResidual)
                                         ? kFastBeatsToLeaveFixedLine
                                         : kFastBeatsToLeaveFixed;
            // Growing-clean votes may raise the count without the newest
            // interval. The two-vote line path must still see that interval
            // this beat: otherwise 128->120 seed 1078 left at two clean votes
            // and the clock overshot to 56.5 ms. Three IOI-less votes still
            // take causalLineRelease above.
            const bool intervalAgreesNow = ! lineFeed
                || (std::fabs (lastIntervalDeviation) > 0.004f
                    && lastIntervalDeviation * lastFastDeviation > 0.0f);
            const bool releaseFixed =
                causalLineRelease
                || strainLineRelease
                || (curveOnLatticeRelease && intervalAgreesNow)
                || (slowIoiFixedRelease && intervalAgreesNow)
                || (beatsInRegime >= kRegimeMinBeats
                    && (moving
                    || fixedErrorBeats >= kBeatsToLeaveFixed
                    || (fastDriftBeats >= votesToLeave
                        && windowAgrees
                        && (votesToLeave > kFastBeatsToLeaveFixedLine
                            || intervalAgreesNow))
                    || (fastDriftLargeBeats >= kFastBeatsAlone
                        && (lineFeed || windowAgrees))
                    || (haveLong && std::fabs (anchorError) > 0.06f)));

            if (releaseFixed)
            {
                enterRegime (TempoRegime::live);
                fixedErrorBeats = 0;
                // Every way out of here means the same thing: the number being
                // held is stale. `moving`, the accumulated error, the fast
                // drift release and the 6% anchor error are four ways of saying
                // it. So the first bar back is spent catching up rather than
                // leaning, which is what the ordinary live rate is for once the
                // clock is already with the band. Bounded to a bar: past that
                // it is ordinary following again.
                leftFixedBeats = kBeatsToLeaveFixed;
            }
            break;
        }

        case TempoRegime::live:
            if (mayFix && directFitsSettled && beatsInRegime >= kRegimeMinBeats)
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
                    fixedWalkRun = 0;

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
                    transitionRefitBeats =
                        kShortFit + (lineFeed ? kTransitionCombLagBeats : 0);
                    transitionLastSec = gridAnchorSec;
                    ++transitionSerial;
                    resetMotionShadow (false, TempoMotionVeto::transition);
                    stepFourHoldBpm = 0.0f;
    liveFourHoldBpm = 0.0f;
                    return;
                }
            }

            // The block above reads the 4-beat on the committed period, so a
            // step that has already filled that window never reaches it: the
            // fit stays on the held tempo. The IOI-indexed 4-beat is the one
            // that has left. Two beats, residual under 0.03, the recent
            // interval within 2% and on the same side, the 8-beat still
            // within 3% of the held number, and the 4-beat more than 5.5%
            // off that 8-beat. Offset-0 curve log: 0 fisso, 0 continuo.
            // One frame on 250062 (next 4-beat 186 against a truth of 146)
            // fails the second beat. 242143 and 305495 stay on the new
            // tempo for eight beats while the published number does not.
            if (lineFeed && ! provisional && haveShort && bpm > kMinBpm
                && recent > 0.0f)
            {
                float p4 = 0.0f, r4 = 1.0f, c4 = 0.0f;
                double a4 = -1.0;
                const float held = std::max (kMinBpm, bpm);
                if (fitPeriodBefore (4, p4, r4, c4, a4, nullptr, 0,
                                     static_cast<double> (recent))
                    && r4 < 0.03f && p4 > 0.0f && a4 >= 0.0)
                {
                    const float fourBpm = 60.0f / p4;
                    const float ioiBpm = 60.0f / recent;
                    const float shortBpm = std::max (kMinBpm, shortFitBpm);
                    const bool octave = std::fabs (std::log2 (fourBpm / held))
                                        > kOctaveThreshold;
                    const bool offHeld = std::fabs (fourBpm - bpm) >= 0.05f * held;
                    const bool shortHeld = std::fabs (shortFitBpm - bpm) <= 0.03f * held;
                    const bool leftShort = std::fabs (fourBpm - shortFitBpm)
                                            > 0.055f * shortBpm;
                    const bool agree = std::fabs (ioiBpm - fourBpm)
                                       <= 0.02f * std::max (kMinBpm, fourBpm);
                    const bool sameSide = (fourBpm - bpm) * (ioiBpm - bpm) > 0.0f;
                    const bool pass = ! octave && offHeld && shortHeld
                                      && leftShort && agree && sameSide;
                    const bool confirmed = pass && stepFourHoldBpm > kMinBpm
                        && std::fabs (fourBpm - stepFourHoldBpm)
                               <= 0.02f * fourBpm;
                    if (confirmed)
                    {
                        bpm = fixedAnchorBpm = std::clamp (fourBpm, kMinBpm, kMaxBpm);
                        gridAnchorSec = a4;
                        beatFilled = 4;
                        longFilled = longWrite = 0;
                        fixedSamples = 0;
                        fixedWalkRun = 0;
                        stepFourHoldBpm = 0.0f;
    liveFourHoldBpm = 0.0f;
                        transitionState = TempoTransitionState::rapid;
                        transitionReason = TempoTransitionReason::confirmed;
                        transitionPeriodSec = 60.0f / bpm;
                        transitionIntervals = 3;
                        transitionConfidence = 1.0f;
                        transitionRapidBeats = 0;
                        transitionRapidDeadlineSec =
                            timeSec + static_cast<double> (kTransitionRapidLifetimeBeats)
                                          * static_cast<double> (transitionPeriodSec);
                        transitionRefitBeats =
                            kShortFit + (lineFeed ? kTransitionCombLagBeats : 0);
                        transitionLastSec = gridAnchorSec;
                        ++transitionSerial;
                        resetMotionShadow (false, TempoMotionVeto::transition);
                        return;
                    }
                    stepFourHoldBpm = pass ? fourBpm : 0.0f;
                }
                else
                    stepFourHoldBpm = 0.0f;
    liveFourHoldBpm = 0.0f;

                // A 4-beat that straddles a step is too dirty for the 0.03
                // door and already names the new tempo, while the 8-beat is
                // still on the held number. Two beats, residual in
                // [0.06, 0.10), at least 8% off the held tempo, the 8-beat
                // within 2% of it, the interval within 5% of the 4-beat.
                // The second beat must be further from the held tempo than
                // the first. Opening the same door from 0.03 took a flat
                // 128.6 (seed 64361) to 140 on two almost-clean beats
                // (residual 0.031 and 0.033) and moved fisso
                // 22.256/76.932 → 22.967/81.952. The real step's pair
                // sits at 0.099 then 0.085.
                if (fitPeriodBefore (4, p4, r4, c4, a4, nullptr, 0,
                                     static_cast<double> (recent))
                    && r4 >= 0.06f && r4 < 0.10f && p4 > 0.0f && a4 >= 0.0)
                {
                    const float fourBpm = 60.0f / p4;
                    const float ioiBpm = 60.0f / recent;
                    const float gap = std::fabs (fourBpm - bpm) / held;
                    const bool octave = std::fabs (std::log2 (fourBpm / held))
                                        > kOctaveThreshold;
                    const bool far = gap >= 0.08f;
                    const bool shortHeld = std::fabs (shortFitBpm - bpm) <= 0.02f * held;
                    const bool agree = std::fabs (ioiBpm - fourBpm)
                                       <= 0.05f * std::max (kMinBpm, fourBpm);
                    const bool sameSide = (fourBpm - bpm) * (ioiBpm - bpm) > 0.0f;
                    const bool pass = ! octave && far && shortHeld && agree && sameSide;
                    const float heldGap = std::fabs (stepFourStraddleHoldBpm - bpm)
                                        / std::max (kMinBpm, bpm);
                    const bool further = stepFourStraddleHoldBpm > kMinBpm
                                      && gap > heldGap + 0.005f;
                    const bool confirmed = pass && further
                        && std::fabs (fourBpm - stepFourStraddleHoldBpm)
                               <= 0.04f * fourBpm;
                    if (confirmed)
                    {
                        bpm = fixedAnchorBpm = std::clamp (fourBpm, kMinBpm, kMaxBpm);
                        gridAnchorSec = a4;
                        beatFilled = 4;
                        longFilled = longWrite = 0;
                        fixedSamples = 0;
                        fixedWalkRun = 0;
                        stepFourHoldBpm = 0.0f;
                        stepFourStraddleHoldBpm = 0.0f;
                        liveFourHoldBpm = 0.0f;
                        transitionState = TempoTransitionState::rapid;
                        transitionReason = TempoTransitionReason::confirmed;
                        transitionPeriodSec = 60.0f / bpm;
                        transitionIntervals = 3;
                        transitionConfidence = 1.0f;
                        transitionRapidBeats = 0;
                        transitionRapidDeadlineSec =
                            timeSec + static_cast<double> (kTransitionRapidLifetimeBeats)
                                          * static_cast<double> (transitionPeriodSec);
                        transitionRefitBeats =
                            kShortFit + (lineFeed ? kTransitionCombLagBeats : 0);
                        transitionLastSec = gridAnchorSec;
                        ++transitionSerial;
                        resetMotionShadow (false, TempoMotionVeto::transition);
                        return;
                    }
                    stepFourStraddleHoldBpm = pass ? fourBpm : 0.0f;
                }
                else
                    stepFourStraddleHoldBpm = 0.0f;
            }
            else
                stepFourHoldBpm = 0.0f;
    liveFourHoldBpm = 0.0f;

            // Refinement, not tracking. The anchor is the running mean of the
            // long fit since the tempo was called fixed, so it converges as
            // evidence accumulates instead of following the last fit around;
            // its gain floors low enough that a slow real change still moves it,
            // and it is the anchor - not the fit - that the committed tempo
            // follows. The deadband is what actually stops the number moving:
            // without it the tempo takes a hundredth of a BPM step on every
            // beat forever, which is a clock that is never quite still.
            // But not from a window that is lying across a tempo event.
            //
            // A twenty-four beat line spans sixteen seconds at 90 BPM, so a
            // two-second dip stays inside it - and inside this running mean -
            // for all sixteen, long after the band has recovered. Measured on
            // `scripts/analysis/makedip.py`: eight seconds after the tempo was
            // back at 90.00 the short fit read 90.00 with a residual of 0.004
            // while the long fit read 89.26 with 0.030, and the published
            // tempo followed the long one down to 89.61. The grid then ran at
            // 89.10 for twelve seconds and manufactured 120 ms of phase error
            // that no steering loop had any way to refuse - which is what a
            // listener hears as the percussion going out *after* an
            // inflection, not during it.
            //
            // The two windows say so themselves when one of them straddles
            // something: they disagree about the tempo *and* the short one
            // fits far better. Neither half is enough on its own - on steady
            // material the residuals alone separate by three to one with the
            // two fits inside a hundredth of a BPM of each other - so both
            // are required. A passage with the drummer out fails the
            // residual half by construction: there the short fit is bad too
            // (0.046-0.052 against 0.054, measured in `VPAlign`), which is
            // the same separation ChatGPT's `shortFitResidual` was built on
            // and is why this can reuse it.
            //
            // It only ever *withholds* an update. The anchor holds the value
            // it already had, which in FISSO - a record cut to a click - is
            // the tempo that is actually true. Nothing jumps, no stroke can
            // be doubled or skipped, and the moment the long window clears
            // the event the two fits agree again and this stands down.
            //
            // Two causal votes and a strain-shaped 8-beat line: the held
            // number is already known to be wrong, but the third vote / clean
            // two-vote door has not opened. One kFixedMaxStep toward the short
            // fit, then skip the long-fit anchor that would pull it back.
            // Releasing on this class, or walking it without the two votes,
            // made the 12 s MIXER mean worse. The clock hint already arms here.
            const bool strainedTwoVoteWalk =
                lineFeed && haveShort && haveMotionCurve && fastDriftBeats >= 2
                && motionFitImprovement >= kMotionCurveWalkImprovement
                && motionFitImprovement < kMotionCurveImprovement
                && shortFitResidual > kMotionCurveResidual
                && shortFitResidual < kMotionCurveStrainResidual
                && motionFitResidual < kMotionCurveResidual
                && std::fabs (motionFitRate / std::max (kMinBpm, bpm)) > kMotionCurveWalkRate
                && (shortFitBpm - bpm) * motionFitRate > 0.0f
                && std::fabs ((shortFitBpm - bpm) / bpm) > kFastDriftToleranceLine;
            if (strainedTwoVoteWalk)
            {
                const float cap = kFixedMaxStep * bpm;
                const float delta = std::clamp (shortFitBpm - bpm, -cap, cap);
                if (std::fabs (delta) > kFixedDeadband)
                    bpm = std::clamp (bpm + delta, kMinBpm, kMaxBpm);
            }
            else if (haveLong)
            {
                if (fixedSamples == 0)
                    fixedAnchorBpm = longFitBpm;
                ++fixedSamples;
                const float gain = std::max (kFixedAnchorFloor,
                                             1.0f / static_cast<float> (fixedSamples));
                fixedAnchorBpm += (longFitBpm - fixedAnchorBpm) * gain;
            }
            if (! strainedTwoVoteWalk && fixedAnchorBpm > kMinBpm)
            {
                // And the fold gets a say here too, which it did not have.
                //
                // FISSO was the one regime with no second opinion at all:
                // `live` and `unknown` both hand their target through
                // `pullTowardsComb`, this branch followed `fixedAnchorBpm` and
                // nothing else. The anchor is a running mean of the *long* fit,
                // which at a slow tempo spans nine seconds, so on material that
                // drifts - which is most material, and which FISSO is entered on
                // anyway when the drift is slow enough - the anchor follows a
                // number that is itself behind, and no third source ever
                // contradicts it.
                //
                // Measured with `probe_steady_tempo` at 60 BPM, constant tempo
                // with the ordinary 3 BPM drift: the reading sat at 61.08 while
                // the truth was 58.73 and the fold said 59.70, for **6.5 s** -
                // against a bar of 4.0 s. The fold was right and was not being
                // asked. Three of the bench's seven excursions were this.
                //
                // `pullTowardsComb` is the same function the other two regimes
                // use and carries its own guards: under `kCombPullThreshold`
                // (3%) it returns the target untouched, so a record cut to a
                // click - where the fold reads the tempo to a tenth at salience
                // 1.00 - is not moved by this at all, which is the whole point
                // of the regime. Past an octave it also stands down and leaves
                // the argument to the snap. What is left is exactly the band
                // where FISSO had nothing: a fold that disagrees by more than
                // its own scatter and less than a metrical level.
                // Bounded well inside an octave, which `pullTowardsComb`'s own
                // limit is not. Reusing it here as-is took `VPTests --octave`
                // from 7/4 to 6/5: its ceiling is `kOctaveThreshold`, so it
                // will pull across eight to nineteen per cent, which is where
                // an argument about the metrical level lives and which the
                // octave snap owns. Below `kStaleGridThreshold` there is no
                // level to confuse - that constant exists for exactly this
                // band - and the 60 BPM case this is for sits at 2 to 3%.
                float anchored = fixedAnchorBpm;
                if (combReady && tempo.levelSettled()
                    && combBpm > kMinBpm && fixedAnchorBpm > kMinBpm)
                {
                    const float apart = std::fabs (std::log2 (combBpm / fixedAnchorBpm));
                    const float rel = (combBpm - fixedAnchorBpm) / fixedAnchorBpm;
                    if (std::fabs (rel) > kCombPullThreshold && apart < kStaleGridThreshold)
                        anchored = fixedAnchorBpm + (combBpm - fixedAnchorBpm) * kCombPull;
                }
                const float step = (anchored - bpm) / std::max (kMinBpm, bpm);
                if (std::fabs (anchored - bpm) > kFixedDeadband
                    && std::fabs (step) <= kFixedMaxStep)
                    bpm = std::clamp (anchored, kMinBpm, kMaxBpm);
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

            // Slow live with a clean 8-beat line is still 3.5 beats late.
            // At 60 BPM that is seconds of integrated phase (seed 192847:
            // short 3-5% off, IOI and a 4-beat line on that IOI period sit
            // on the pulse). The ordinary IOI blend is capped at 4% and
            // then committed at kRateLive, so the lag never unwinds.
            //
            // Two doors, both measured silent on the offset-0 control log
            // (fisso 0, gradino 0). Shared: both 8/24 fits still agree,
            // |IOI−short| > the line drift bar.
            //
            // Door A: quadratic improvement already at the production
            // curve bar, and the comb names the same *direction* as the
            // IOI vs the short fit. Without that comb-sign term, slow
            // gradino catch-up at 61-63 BPM fires (bir 7-15). After
            // kLongFit the waive is silent on offset-0 fisso/gradino.
            //
            // Door B: past kLongFit the 4-beat line is closer to the IOI
            // than to the 8-beat line, and that 4-beat residual is clean.
            // Comb-sign is not required here: on a slow deceleration the
            // fold is the last to turn, so the sign is false while i4
            // already sits on the pulse. Below 75 BPM the 8-beat residual
            // is not a veto (a clean slow 8-beat is still 3.5 beats late).
            // Above 75 BPM the 8-beat is current enough unless it is
            // already this dirty (kMotionCurveFourBeatDirty 0.080; 0.075
            // lights offset-0 gradino). Raising Door A's residual ceiling
            // instead lights gradino 210467 at bir 7-8.
            //
            // Door C: one beat before Door B (kLongFit-1) a clean 4-beat
            // that is *not* closer to the IOI still sits between the
            // late 8-beat and the IOI (192847 t=57.42, i4=60.6, short
            // 59.5, IOI 61.8, truth 63.5, fold unturned). Taking it at
            // |IOI−short| > kUnknownIoiLead is silent on offset-0
            // fisso/gradino; 0.012 and 0.020 light gradino 281738.
            // Door A's comb-sign waive after two short windows (20)
            // at kDoorAIoiLead is KEEP only stacked with dirty unknown
            // Door B at kRateAcquiring; 0.012 missed t=54.56.
            bool slowIoiLeads = false;
            bool doorATake = false;
            if (lineFeed && haveShort && haveLong
                && recent > 0.0f && bpm > kMinBpm
                && beatsInRegime >= 4
                && combReady && combBpm > kMinBpm
                && std::fabs (shortFitBpm - longFitBpm)
                       < kStaleFitsAgree * std::max (kMinBpm, longFitBpm))
            {
                const float ioiBpm = 60.0f / recent;
                const float ioiDev = (ioiBpm - shortFitBpm)
                    / std::max (kMinBpm, shortFitBpm);
                if (std::fabs (ioiDev) > kDoorAIoiLead)
                {
                    float p4 = 0.0f, r4 = 1.0f, c4 = 0.0f;
                    double a4 = -1.0;
                    const bool have4 = fitPeriodBefore (
                                           4, p4, r4, c4, a4, nullptr, 0,
                                           static_cast<double> (recent))
                                       && r4 < kMotionCurveResidual && p4 > 0.0f;
                    const float fourBpm = have4 ? 60.0f / p4 : 0.0f;
                    const bool combSign = (ioiBpm - shortFitBpm)
                        * (combBpm - shortFitBpm) > 0.0f;
                    const bool residualClean = shortFitResidual > 0.0f
                        && shortFitResidual < kMotionCurveResidual;
                    const bool curveOk = residualClean && haveMotionCurve
                        && motionFitImprovement >= kMotionCurveImprovement
                        && bpm < 75.0f
                        && (combSign || beatsInRegime >= kShortFit * 2 + 4);
                    const bool longCleanFour = have4
                        && beatsInRegime >= kLongFit
                        && std::fabs (ioiDev) > kFastDriftToleranceLine
                        && std::fabs (fourBpm - ioiBpm)
                               < std::fabs (fourBpm - shortFitBpm)
                        && (bpm < 75.0f
                            || shortFitResidual >= kMotionCurveFourBeatDirty)
                        // A mixed post-gap 8-beat can sit an octave-ish
                        // from an IOI-indexed 4-beat (offset-0 live
                        // Door B: 0 fisso/gradino). That 4-beat is not
                        // a refinement of the line; the octave snap
                        // owns disagreements this wide.
                        && std::fabs (std::log2 (fourBpm / std::max (kMinBpm, shortFitBpm)))
                               < kOctaveThreshold;
                    const bool midFour = have4
                        && beatsInRegime >= kLongFit - 1
                        && bpm < 75.0f
                        && std::fabs (ioiDev) > kUnknownIoiLead
                        && std::fabs (fourBpm - ioiBpm)
                               >= std::fabs (fourBpm - shortFitBpm);
                    if (curveOk || longCleanFour || midFour)
                    {
                        target = have4 ? fourBpm : ioiBpm;
                        // Door C's 4-beat sits between the late 8-beat and
                        // the IOI. Taking the 4-beat leaves the more current
                        // interval on the table; taking the IOI on every
                        // Door C frame yanks when the 4-beat has not moved
                        // (a single displaced onset). Require the 4-beat to
                        // have left the 8-beat by the line drift bar, same
                        // sign as the IOI. Offset-0 live fisso/gradino: 0
                        // Door C frames.
                        if (midFour
                            && (fourBpm - shortFitBpm) * (ioiBpm - shortFitBpm) > 0.0f
                            && std::fabs (fourBpm - shortFitBpm)
                                   > kFastDriftToleranceLine
                                         * std::max (kMinBpm, shortFitBpm))
                        {
                            // The IOI is still the centre of the last
                            // interval. The short-to-long gap is the
                            // same rate kLiveLead already uses on the
                            // 8-beat; apply it to the 4-beat→IOI gap
                            // (192847 t=57.42 i4=60.6 IOI=61.8 T=63.5).
                            // Door C at 1.0 REJECT; this only moves
                            // the target. 0 offset-0 Door C frames.
                            target = ioiBpm;
                            const float doorCLead = kLiveLead * (ioiBpm - fourBpm);
                            const float cap = 0.04f * ioiBpm;
                            target += std::clamp (doorCLead, -cap, cap);
                        }
                        slowIoiLeads = true;
                        // The live block is already behind bir>=4, so
                        // Door A does not run on fisso 1009 (bir=1).
                        // Offset-0 fisso/gradino: 0 Door A frames.
                        ioiClockLead = true;
                        // After two short windows: 0 offset-0
                        // fisso/gradino Door A frames (210467 is bir 13).
                        // Unknown Door C at 1.0 fattened p95; this is
                        // Door A below 75 with a clean 4-beat on the IOI.
                        if (curveOk && beatsInRegime >= kShortFit * 2 + 4)
                            doorATake = true;
                    }
                    // Door A above 75 retargets BPM and overshoots
                    // (177009 t=53.04 i4=124 vs T=119). This only
                    // arms the 0.01 s tau, so the 8-beat number is
                    // unchanged. 0.80 quadratic, 4-on-IOI, r4 clean.
                    // kLongFit is KEEP. Two short windows (16) is
                    // KEEP. One short window (8) is still 0 offset-0
                    // fisso/gradino (44 vs 42 continuo on the g80 log).
                    // 12 s VPAlign still does not hit 0.80+4-on-IOI.
                    else if (bpm >= 75.0f
                             && beatsInRegime >= kShortFit
                             && haveMotionCurve
                             && motionFitImprovement >= kClockOnlyQuadratic
                             && have4 && r4 < kFastLineCleanResidual
                             && std::fabs (fourBpm - ioiBpm)
                                    < kFastDriftToleranceLine
                                          * std::max (kMinBpm, ioiBpm)
                             && std::fabs (fourBpm - ioiBpm)
                                    < std::fabs (fourBpm - shortFitBpm))
                    {
                        ioiClockLead = true;
                    }
                    // Below 75 live: i4 sits on the IOI pulse while the
                    // 8-beat lags and the quadratic is too weak for
                    // clock-only 0.80 / Door A 0.50 (137414 t=32.20
                    // g=0.40; 224523 t=38–40 g=0.00–0.11). Four-lead
                    // needs IOI quiet; Door A needs combSign (comb is
                    // still tied to the late 8-beat, |comb−short|<0.5%).
                    // Live-rate toward i4 + origin, not Door A 0.70.
                    // Offset-0 census t≥0, fold=combRaw≈held: 0 fisso
                    // (1009) / 0 gradino (210467); 4 continuo frames.
                    else if (bpm < 75.0f
                             && beatsInRegime >= kShortFit
                             && beatsInRegime < kLongFit
                             && have4 && r4 < kMotionCurveResidual
                             && motionFitImprovement < kClockOnlyQuadratic
                             && fourBpm > shortFitBpm
                             && ioiBpm > shortFitBpm
                             && std::fabs (fourBpm - ioiBpm)
                                    < kFastDriftToleranceLine
                                          * std::max (kMinBpm, ioiBpm)
                             && std::fabs (fourBpm - ioiBpm)
                                    < std::fabs (fourBpm - shortFitBpm)
                             && std::fabs (fourBpm - shortFitBpm)
                                    > kDoorAIoiLead
                                          * std::max (kMinBpm, shortFitBpm)
                             && std::fabs (combBpm - shortFitBpm)
                                    < 0.005f * std::max (kMinBpm, shortFitBpm)
                             && std::fabs (ioiDev) < kUnknownIoiLead)
                    {
                        target = fourBpm;
                        ioiClockLead = true;
                    }
                    // 75<=bpm<90 live i4-on-pulse, g in [0.50, 0.80).
                    // Unbounded above-75 was 0 offset-0 fisso/gradino
                    // but VPAlign 120→132 MIXER mean 25.8→26.8 (ramp
                    // curvature sits in that g band at 120). bpm<90
                    // is 0 dump hops on VPAlign --ramps (12 s still
                    // 32.1/82.0; 120→132 starts at 120). --quick 16
                    // dump: 0 fisso/gradino, 4 continuo (113657
                    // t=66.14/66.80, 200766 t=85.30, 208685 t=74.68).
                    else if (bpm >= 75.0f && bpm < 90.0f
                             && beatsInRegime >= kShortFit
                             && haveMotionCurve
                             && motionFitImprovement >= kMotionCurveImprovement
                             && motionFitImprovement < kClockOnlyQuadratic
                             && have4 && r4 < kFastLineCleanResidual
                             && fourBpm > shortFitBpm
                             && ioiBpm > shortFitBpm
                             && std::fabs (ioiDev) < kUnknownIoiLead
                             && std::fabs (fourBpm - ioiBpm)
                                    < kFastDriftToleranceLine
                                          * std::max (kMinBpm, ioiBpm)
                             && std::fabs (fourBpm - ioiBpm)
                                    < std::fabs (fourBpm - shortFitBpm)
                             && std::fabs (fourBpm - shortFitBpm)
                                    > kDoorAIoiLead
                                          * std::max (kMinBpm, shortFitBpm)
                             && std::fabs (std::log2 (
                                    combBpm / std::max (kMinBpm, bpm)))
                                    < kOctaveThreshold)
                    {
                        target = fourBpm;
                        ioiClockLead = true;
                    }
                }
                // Decelerando: the 4-beat turns while the folded IOI
                // is still the old period (192847 t=68.44 i4=65.3 vs
                // IOI 66.0, T 64.2). Door A waits on |IOI−short|.
                // Clock-only, live, bpm<75, quadratic 50%, 4-beat
                // already off the 8-beat by Door A's bar: 0 offset-0
                // fisso (1009 is FISSO) / gradino.
                else if (bpm < 75.0f && beatsInRegime >= kShortFit
                         && haveMotionCurve
                         && motionFitImprovement >= kMotionCurveImprovement)
                {
                    float p4 = 0.0f, r4 = 1.0f, c4 = 0.0f;
                    double a4 = -1.0;
                    if (fitPeriodBefore (4, p4, r4, c4, a4, nullptr, 0,
                                         static_cast<double> (recent))
                        && r4 < kMotionCurveResidual && p4 > 0.0f)
                    {
                        const float fourBpm = 60.0f / p4;
                        const float ioiBpm = 60.0f / recent;
                        if (std::fabs (fourBpm - shortFitBpm)
                                > kDoorAIoiLead
                                      * std::max (kMinBpm, shortFitBpm)
                            && std::fabs (fourBpm - ioiBpm)
                                   < std::fabs (fourBpm - shortFitBpm))
                        {
                            ioiClockLead = true;
                        }
                    }
                }
            }

            // Door D: Door B never opens when the 8-beat and 24-beat
            // disagree — that guard is what keeps a step from taking an
            // IOI. On a mixed window the IOI-indexed 4-beat can still
            // sit on the pulse (169090 t=47.58: short 95.8, long 103.4,
            // i4 91.1 vs truth 91.2). Comb-sign, |IOI−short| >
            // kUnknownIoiLead, 4-closer, bir >= kLongFit are silent on
            // offset-0 fisso/gradino. kDoorDFourResidual (not
            // kMotionCurveResidual): 153252 t=67.36 r4=0.036 took the
            // 4-beat through a 200 ms overshoot and fattened family
            // p95. Acquiring-rate yank fattened 169090 p995; this only
            // retargets, at kRateLive.
            if (! slowIoiLeads && lineFeed && haveShort && haveLong
                && recent > 0.0f && bpm > kMinBpm
                && beatsInRegime >= kLongFit
                && combReady && combBpm > kMinBpm
                && std::fabs (shortFitBpm - longFitBpm)
                       >= kStaleFitsAgree * std::max (kMinBpm, longFitBpm))
            {
                const float ioiBpm = 60.0f / recent;
                const float ioiDev = (ioiBpm - shortFitBpm)
                    / std::max (kMinBpm, shortFitBpm);
                if (std::fabs (ioiDev) > kUnknownIoiLead
                    && (ioiBpm - shortFitBpm) * (combBpm - shortFitBpm) > 0.0f)
                {
                    float p4 = 0.0f, r4 = 1.0f, c4 = 0.0f;
                    double a4 = -1.0;
                    if (fitPeriodBefore (4, p4, r4, c4, a4, nullptr, 0,
                                         static_cast<double> (recent))
                        && p4 > 0.0f)
                    {
                        const float fourBpm = 60.0f / p4;
                        const bool fourOnIoi =
                            std::fabs (fourBpm - ioiBpm)
                                < kFastDriftToleranceLine
                                      * std::max (kMinBpm, ioiBpm);
                        // r4<0.015 is KEEP. 0.045 took 153252 t=65.06
                        // (i4=153 vs T=132) and fattened p95. The
                        // leftover band is a dirty 8-beat (sres>=0.080)
                        // whose 4-beat already sits on the IOI
                        // (153252 t=67.36 r4=0.036, i4=132.7 vs T=131.3):
                        // 0 offset-0 fisso/gradino.
                        const bool fourClean = r4 < kDoorDFourResidual
                            || (r4 < kMotionCurveResidual
                                && shortFitResidual >= kMotionCurveFourBeatDirty
                                && fourOnIoi);
                        if (fourClean
                            && std::fabs (fourBpm - ioiBpm)
                                   < std::fabs (fourBpm - shortFitBpm))
                        {
                            target = fourBpm;
                            ioiClockLead = true;
                            // One frame: the next beat the 8/24 fits agree
                            // again and Door B is blocked above 75 (sres
                            // < 0.080). Hold the 4-beat at kRateLive.
                            // Offset-0 fisso/gradino: 0 Door D frames.
                            ioiTargetHoldBpm = fourBpm;
                            ioiTargetHoldBeats = kShortFit;
                        }
                    }
                }
            }

            // Door D is one frame. Keep aiming at that 4-beat while live
            // so the 0.30 rate can walk there after the 8/24 fits agree
            // again. Not slowIoiLeads: acquiring overshot 169090.
            const bool doorHoldRate = ioiTargetHoldBpm > kMinBpm
                                      && ioiTargetHoldBeats > 0;
            if (! slowIoiLeads && ioiTargetHoldBeats > 0 && ! ioiClockLead
                && ioiTargetHoldBpm > kMinBpm)
            {
                float p4 = 0.0f, r4 = 1.0f, c4 = 0.0f;
                double a4 = -1.0;
                if (recent > 0.0f
                    && fitPeriodBefore (4, p4, r4, c4, a4, nullptr, 0,
                                        static_cast<double> (recent))
                    && r4 < kDoorDFourResidual && p4 > 0.0f)
                {
                    const float fourBpm = 60.0f / p4;
                    const float ioiBpm = 60.0f / recent;
                    if (std::fabs (fourBpm - ioiBpm)
                            < std::fabs (fourBpm - shortFitBpm))
                        ioiTargetHoldBpm = fourBpm;
                }
                target = ioiTargetHoldBpm;
                ioiClockLead = true;
                --ioiTargetHoldBeats;
            }
            else if (ioiTargetHoldBeats > 0 && ioiClockLead)
            {
                // Fired this beat; count it against the window.
                --ioiTargetHoldBeats;
            }

            // Two states in which the committed number is stale by
            // construction, and in both the ordinary live rate is the wrong
            // tool: it exists for a clock that is already with the band.
            //
            // Leaving FISSO is the first. Measured at 60 BPM, the exit now
            // costs two beats and the rest of the 4.4 s excursion was the lean
            // back from 61.08 to 58.7 at 22% a beat while the short fit and the
            // fold both already said 59.1.
            //
            // The second is the bar after a confirmed tempo transition, while
            // `transitionRefitBeats` says the fits are still being rebuilt from
            // the beats after the change. On a direct feed it also quarantines
            // the slower comb for three extra beats; room input keeps the old
            // refit length. Measured at 160 BPM, where a
            // transition that should not have been confirmed drops both fits
            // and publishes 166.9: the fold read 159.6-160.0 from the first
            // beat and the committed number walked down 166.9, 166.2, 165.5,
            // 164.6, 164.0, 163.4 - 0.7 BPM a beat, five and a half beats to
            // come back against a bar of four.
            //
            // This does not change *where* the tempo goes, only how fast it
            // gets there: the target is the same rebuilt short fit either way.
            // That is what makes it safe on a genuine step, where the same
            // rebuilt fit is converging on the real new tempo and arriving
            // sooner is the point - and it is why the fold is deliberately not
            // given more authority here. After a real step the fold names the
            // tempo that has been left for seconds, and `pullTowardsComb`'s 35%
            // cap is exactly what stops it dragging a true change back.
            // Live, the 8-beat can still name the tempo that was left
            // while two IOI-indexed 4-beats already agree on the new
            // one: more than 7.5% off the 8-beat, the newest interval
            // within 2.5% and on the same side, residual under 0.03, the
            // two 4-beats within 4% of each other. The 2% hold missed
            // 234224 (137 then 142, 3.3% apart, truth 142). Widening
            // only that agreement: fisso and continuo hashes identical,
            // gradino 33.360/154.375 → 33.270/153.677, and the only
            // seed that moved is 234224, 71.7/158.2 → 70.3/147.1.
            // 289657 then sat just outside both the 8% gap (7.7%) and
            // the 2% interval (2.1%). Opening those two to 7.5% and
            // 2.5% adds only that pair: 42.8/276.3 → 40.2/256.4, and
            // fisso and continuo stay put. The carried vote still
            // uses the 2% interval test. A 3–4% gap to the 8-beat is
            // a ramp overshoot and stays on the 8-beat. Not an octave.
            // Rate stays kRateLive.
            // Door D's 0.45 on this aim: fisso hash changed, continuo
            // 36.551/91.156 → 52.481/128.505 with 2 recovery
            // violations, gradino 33.537/157.311 → 38.979/159.143.
            // Reverted.
            bool liveFourAim = false;
            if (lineFeed && haveShort && recent > 0.0f && shortFitBpm > kMinBpm)
            {
                float p4 = 0.0f, r4 = 1.0f, c4 = 0.0f;
                double a4 = -1.0;
                const float shortBpm = std::max (kMinBpm, shortFitBpm);
                if (fitPeriodBefore (4, p4, r4, c4, a4, nullptr, 0,
                                     static_cast<double> (recent))
                    && r4 < 0.03f && p4 > 0.0f)
                {
                    const float fourBpm = 60.0f / p4;
                    const float ioiBpm = 60.0f / recent;
                    const bool octave = std::fabs (std::log2 (fourBpm / shortBpm))
                                        > kOctaveThreshold;
                    const bool leftShort = std::fabs (fourBpm - shortFitBpm) > 0.075f * shortBpm;
                    const bool ioiAgrees = std::fabs (ioiBpm - fourBpm)
                                           < 0.02f * std::max (kMinBpm, fourBpm);
                    // 289657's two beats sit just outside the old pair:
                    // the interval is 2.1% off the 4-beat, then the 4-beat
                    // is 7.7% off the 8-beat. Both name the new tempo.
                    // The carried vote keeps the 2% interval test. No other
                    // quick-bank pair appears when the live gap is 7.5%
                    // and the live interval test is 2.5%.
                    const bool ioiAgreesLive = std::fabs (ioiBpm - fourBpm)
                                               < 0.025f * std::max (kMinBpm, fourBpm);
                    const bool sameSide = (fourBpm - shortFitBpm) * (ioiBpm - shortFitBpm) > 0.0f;
                    const bool pass = ! octave && leftShort && ioiAgreesLive && sameSide;
                    // Two clean 4-beats 3.3% apart are the same step still
                    // settling (234224 t=40.64 at 137, t=41.06 at 142). The
                    // 2% hold missed that pair. Nothing else in the quick
                    // bank sits between 2% and 4%.
                    const bool confirmed = pass && liveFourHoldBpm > kMinBpm
                        && std::fabs (fourBpm - liveFourHoldBpm)
                               <= 0.04f * fourBpm;
                    // A FISSO beat already passed the 5.5% door and stored
                    // its 4-beat. This is the beat that left, so the gap to
                    // the 8-beat may now be under 8% and the live pair never
                    // starts. Agreeing with that stored beat is the second
                    // vote, and it publishes the way the fixed door does:
                    // the tempo is taken, not walked at kRateLive. Walking
                    // left 329252 at 83.3 against a truth of 78.4. Taking
                    // it: fisso and continuo hashes unchanged, gradino
                    // 33.270/153.677 → 32.651/152.146, only that seed
                    // 35.0/179.9 → 25.1/155.4.
                    const bool carried = ! octave && ioiAgrees
                        && stepFourHoldBpm > kMinBpm
                        && std::fabs (fourBpm - stepFourHoldBpm)
                               <= 0.02f * fourBpm;
                    stepFourHoldBpm = 0.0f;
                    liveFourHoldBpm = pass ? fourBpm : 0.0f;
                    // The carried vote is the second beat of a fixed-regime
                    // 4-beat that already passed the stricter door.
                    // Taking the ordinary live pair the same way (234224,
                    // 289657, 297576 all named the truth) left fisso and
                    // continuo hashes identical and improved 297576, but
                    // 234224's phase got worse: 70.3/147.1 → 66.9/156.1.
                    // The live pair keeps walking at kRateLive.
                    if (carried && a4 >= 0.0)
                    {
                        bpm = std::clamp (fourBpm, kMinBpm, kMaxBpm);
                        gridAnchorSec = a4;
                        beatFilled = 4;
                        longFilled = longWrite = 0;
                        fixedSamples = 0;
                        fixedWalkRun = 0;
                        stepFourHoldBpm = 0.0f;
                        liveFourHoldBpm = 0.0f;
                        transitionState = TempoTransitionState::rapid;
                        transitionReason = TempoTransitionReason::confirmed;
                        transitionPeriodSec = 60.0f / bpm;
                        transitionIntervals = 3;
                        transitionConfidence = 1.0f;
                        transitionRapidBeats = 0;
                        transitionRapidDeadlineSec =
                            timeSec + static_cast<double> (kTransitionRapidLifetimeBeats)
                                          * static_cast<double> (transitionPeriodSec);
                        transitionRefitBeats =
                            kShortFit + (lineFeed ? kTransitionCombLagBeats : 0);
                        transitionLastSec = gridAnchorSec;
                        ++transitionSerial;
                        resetMotionShadow (false, TempoMotionVeto::transition);
                        return;
                    }
                    if (confirmed)
                    {
                        target = fourBpm;
                        liveFourAim = true;
                        ioiClockLead = true;
                    }
                }
                else
                {
                    liveFourHoldBpm = 0.0f;
                    stepFourHoldBpm = 0.0f;
                }
            }
            else
            {
                liveFourHoldBpm = 0.0f;
                stepFourHoldBpm = 0.0f;
            }

            const bool stale = leftFixedBeats > 0 || transitionRefitBeats > 0;
            // The comb spans seconds of audio and therefore still names the
            // tempo that was left while the short fit is rebuilding from a
            // confirmed step. Letting it pull here immediately undid the
            // causal interval measurement: 120 -> 132 was confirmed after two
            // intervals at +0.92 s, then the stale 120-BPM comb dragged the
            // committed value to 129.2 on the very next beat and the sounding
            // clock did not settle for 8.9 s. During the bounded refit window,
            // the new-beat fit is the only rate source that can be current.
            //
            // The live doors are the same situation: the 8-beat is late and
            // the comb is later (Door C: fold unturned). Pulling 35% toward
            // it undoes the 4-beat/IOI the door just named, and the Door D
            // hold. Offset-0 fisso/gradino never fire those doors.
            const float wanted = lineFeed
                                 && (transitionRefitBeats > 0
                                     || slowIoiLeads || ioiClockLead)
                                     ? target
                                     : pullTowardsComb (target, combReady, combBpm);
            // And only while the gap is actually one a stale number would leave.
            //
            // Without this the catch-up fires on every beat of the window,
            // including the ones where the committed tempo is already right and
            // the target is moving by jitter, and on loose material that is
            // most of them: `probe_matrix` went from 96 time-outs to 100, all
            // of the cost on `band larga` (25 ms of scatter), `swing pieno` and
            // `shuffle 16mi`. The same 2% line the FISSO exit uses separates
            // them - a number left behind by a regime or a transition is four
            // to five per cent out, a fit wandering under a loose band is not.
            const bool far = stale && bpm > kMinBpm
                             && std::fabs ((wanted - bpm) / bpm) > kLeaveFixedError;
            if (leftFixedBeats > 0)
                --leftFixedBeats;
            const float motionTarget = bridgedMotionTarget (wanted);
            const bool shapeLeads = motionBridgeAuthority > 0.0f
                                    && std::fabs (motionTarget - wanted) > 1.0e-6f;
            commit (motionTarget,
                    shapeLeads ? 1.0f
                               : (doorATake ? 1.0f
                                  : (liveFourAim ? kRateLive
                                     : (far || slowIoiLeads ? kRateAcquiring
                                        : (doorHoldRate ? kRateDoorHold : kRateLive)))));
            break;
        }

        case TempoRegime::unknown:
        {
            // Questo ramo serve due popolazioni diverse, e finora dava a
            // entrambe la risposta della prima.
            //
            // La prima e' l'acquisizione vera, per cui il regime esiste: pochi
            // battiti di storia, e il fit **lungo** e' il piu' preciso che ci
            // sia. La seconda l'ha trovata il banco reale (docs/TODO.md item
            // 38): una band dal vivo che sta qui per *minuti*, perche' il suo
            // scatter umano e' troppo largo perche' `mayFix` la chiami fissa
            // (spread oltre 0.015) e la sua deriva troppo lenta dentro la
            // finestra perche' `moving` la chiami in movimento (trend sotto
            // 0.018). Cade nel mezzo e ci resta: il brano 4 del banco ci passa
            // il **69%** della sua durata.
            //
            // Per quella seconda popolazione il fit lungo grezzo e' la stima
            // peggiore possibile: ventiquattro battiti, centrata dodici battiti
            // indietro, che a 86 BPM sono **8.4 s** di puro ritardo di
            // centratura - e il brano 4 misura +7.50 s di ritardo sulla deriva
            // del batterista.
            //
            // Le due si separano senza nessuna soglia sul segnale, che e' il
            // muro documentato nel ramo `live` qui sotto: si separano su **da
            // quanto tempo siamo qui**. Chi sta acquisendo ha pochi battiti in
            // regime; chi vive qui ne ha centinaia. Sotto una finestra lunga
            // intera resta il fit lungo e la sua precisione; oltre, si applica
            // lo stesso anticipo che il ramo `live` usa gia' - misurato
            // funzionante li' (0.447% del tempo, mai clampato).
            float target = haveLong ? longFitBpm : shortFitBpm;
            if (haveLong && haveShort && beatsInRegime > kLongFit)
            {
                const float lead = kLiveLead * (shortFitBpm - longFitBpm);
                target = shortFitBpm
                       + std::clamp (lead, -0.04f * shortFitBpm, 0.04f * shortFitBpm);
            }
            target = bringSlowFitCurrent (target);
            float rate = kRateAcquiring;
            // Door B in unknown at kRateLive. A dirty 8-beat is not
            // required: the 406 ms tail is integrated rate while the
            // 8-beat is still clean and 3.5 beats late (t=43.5). The
            // fold at sal 0.13 is not a usable origin. Acquiring-rate
            // yank raised the family mean. kUnknownIoiLead (0.035)
            // is silent on offset-0 fisso/gradino; 0.033 lights
            // fisso 24766.
            if (lineFeed && haveShort && haveLong && recent > 0.0f
                && bpm < 75.0f && bpm > kMinBpm
                && beatsInRegime >= kLongFit
                && combBpm > kMinBpm
                && std::fabs (shortFitBpm - longFitBpm)
                       < kStaleFitsAgree * std::max (kMinBpm, longFitBpm))
            {
                const float ioiBpm = 60.0f / recent;
                const float ioiDev = (ioiBpm - shortFitBpm)
                    / std::max (kMinBpm, shortFitBpm);
                const bool combSign = (ioiBpm - shortFitBpm)
                    * (combBpm - shortFitBpm) > 0.0f;
                float p4 = 0.0f, r4 = 1.0f, c4 = 0.0f;
                double a4 = -1.0;
                const bool have4 = fitPeriodBefore (
                                       4, p4, r4, c4, a4, nullptr, 0,
                                       static_cast<double> (recent))
                                   && p4 > 0.0f;
                const float fourBpm = have4 ? 60.0f / p4 : 0.0f;
                const bool fourCloser = have4
                    && std::fabs (fourBpm - ioiBpm)
                           < std::fabs (fourBpm - shortFitBpm);
                if (std::fabs (ioiDev) > kUnknownIoiLead
                    && combSign && fourCloser
                    && r4 < kMotionCurveResidual)
                {
                    target = fourBpm;
                    // t=43.5 is a clean 8-beat (residual 0.010):
                    // acquiring overshot and raised the family
                    // mean. t=52 is already this dirty and 322 ms
                    // off; live rate cannot unwind it before the
                    // next gap. 0.70 left 1.7 BPM on the table;
                    // 1.0 takes the 4-beat this frame. Strain is
                    // silent on offset-0 fisso/gradino.
                    rate = shortFitResidual >= kMotionCurveStrainResidual
                               ? 1.0f
                               : kRateLive;
                    ioiClockLead = true;
                    // Door D hold, unknown: the next beats (and the
                    // kit gap) would otherwise take the leftover comb
                    // or the post-gap 8-beat. Live A/B/C hold fattened
                    // family p95; unknown Door B is silent on offset-0
                    // fisso/gradino (0 unknown Door B frames).
                    ioiTargetHoldBpm = fourBpm;
                    ioiTargetHoldBeats = kShortFit;
                }
                else if (std::fabs (ioiDev) > kFastDriftToleranceLine
                         && combSign && fourCloser
                         && r4 >= kMotionCurveResidual
                         && r4 < kMotionCurveFourBeatDirty
                         && std::fabs (fourBpm - ioiBpm)
                                < kFastDriftToleranceLine
                                      * std::max (kMinBpm, ioiBpm))
                {
                    // Dirty 4-beat still on the IOI: arm the proven
                    // tau, do not retarget BPM. Taking that 4-beat as
                    // tempo fattened family p95 through the post-gap
                    // 8-beat. Silent on offset-0 fisso/gradino
                    // (bpm<75, kLongFit).
                    ioiClockLead = true;
                }
                else if (std::fabs (ioiDev) > kUnknownIoiLead
                         && combSign && have4 && ! fourCloser
                         && r4 >= kMotionCurveResidual
                         && r4 < kMotionCurveFourBeatDirty)
                {
                    // Live Door C, unknown: the dirty 4-beat has not
                    // left the 8-beat so it is not Door B, but the IOI
                    // and comb already have. Taking that IOI at 0.035
                    // is silent on offset-0 fisso/gradino (the 73 yank
                    // is a same-lattice 4-beat, r4 clean). 1.0 took
                    // 216604 t=51.10 (mean 42.231→42.034, p995
                    // 318→313) and fattened family p95 97.784→98.046.
                    target = ioiBpm;
                    rate = kRateAcquiring;
                    ioiClockLead = true;
                    ioiTargetHoldBpm = 0.0f;
                    ioiTargetHoldBeats = 0;
                }
                else if (std::fabs (ioiDev) > kUnknownIoiLead
                         && combSign && have4
                         && std::fabs (fourBpm - shortFitBpm)
                                < kFastDriftToleranceLine
                                      * std::max (kMinBpm, shortFitBpm)
                         && std::fabs (combBpm - shortFitBpm)
                                > 0.03f * std::max (kMinBpm, shortFitBpm))
                {
                    // Same-lattice post-gap 8-beat; comb already left
                    // it. Taking the IOI fattened family p95 (G1).
                    // Commit the comb, do not arm tau: abortYank must
                    // still drop the 73 lattice. Arming tau on this
                    // comb commit locked the 73 peak and fattened
                    // p95/p995 (98.010→98.194, 319→326). |comb-short|>3%
                    // is 0 unknown frames on offset-0 fisso (24766 is 0.14%).
                    target = combBpm;
                }
            }
            if (! ioiClockLead && ioiTargetHoldBeats > 0
                && ioiTargetHoldBpm > kMinBpm)
            {
                float p4 = 0.0f, r4 = 1.0f, c4 = 0.0f;
                double a4 = -1.0;
                if (recent > 0.0f
                    && fitPeriodBefore (4, p4, r4, c4, a4, nullptr, 0,
                                        static_cast<double> (recent))
                    && r4 < kMotionCurveResidual && p4 > 0.0f)
                    ioiTargetHoldBpm = 60.0f / p4;
                target = ioiTargetHoldBpm;
                rate = kRateDoorHold;
                ioiClockLead = true;
                --ioiTargetHoldBeats;
            }
            else if (ioiTargetHoldBeats > 0 && ioiClockLead)
                --ioiTargetHoldBeats;
            // Live doors already skip the comb: it is later than the
            // 4-beat. Unknown Door B is the same geometry at t=52
            // (i4≈comb, no-op) and the opposite at t=43.5 (comb toward
            // T=70). Measured, not assumed.
            commit (ioiClockLead ? target
                                 : pullTowardsComb (target, combReady, combBpm),
                    rate);
            break;
        }
    }

    if (ioiClockLead)
        ioiClockLeadBeats = kShortFit;
    else if (persistLead)
    {
        bool abortYank = false;
        if (tempoRegime == TempoRegime::unknown && shortFitBpm > kMinBpm)
        {
            float recent = 0.0f;
            float p4 = 0.0f, r4 = 1.0f, c4 = 0.0f;
            double a4 = -1.0;
            if (recentPeriod (recent)
                && fitPeriodBefore (4, p4, r4, c4, a4, nullptr, 0,
                                    static_cast<double> (recent))
                && p4 > 0.0f && shortFitBpm > kMinBpm)
            {
                const float fourBpm = 60.0f / p4;
                abortYank = std::fabs (fourBpm - shortFitBpm)
                    < kFastDriftToleranceLine
                          * std::max (kMinBpm, shortFitBpm);
            }
        }
        if (abortYank && ioiTargetHoldBeats <= 0)
            ioiClockLeadBeats = 0;
        else
        {
            ioiClockLead = true;
            --ioiClockLeadBeats;
        }
    }
    else
        ioiClockLeadBeats = 0;

    // Live already publishes the 8-beat origin. Door D can name a 4-beat
    // on the pulse (169090 t=47.58 i4≈T) while the clock still follows
    // that late origin at 0.01 s tau, so phase climbs 130→190 ms as BPM
    // catches. Pull the origin toward the same 4-beat, at most one
    // comb-ruler beat. fourCloser is Door D; live fourBetween is Door C
    // (192847 t=57.42). Unknown persist is not fourBetween: pulling
    // 216604 t=51.10 onto i4=70 while IOI was 67 fattened that p95.
    // Offset-0 fisso/gradino never set ioiClockLead. Switching the
    // origin outright was grid jerk (anchorBlend comment).
    if (lineFeed && ioiClockLead && tempoRegime != TempoRegime::fixed
        && haveShort && bpm > kMinBpm && gridAnchorSec >= 0.0)
    {
        float recent = 0.0f, p4 = 0.0f, r4 = 1.0f, c4 = 0.0f;
        double a4 = -1.0;
        if (recentPeriod (recent) && recent > 0.0f
            && fitPeriodBefore (4, p4, r4, c4, a4, nullptr, 0,
                                static_cast<double> (recent))
            && r4 < kMotionCurveResidual && p4 > 0.0f && a4 >= 0.0)
        {
            const float fourBpm = 60.0f / p4;
            const float ioiBpm = 60.0f / recent;
            const float fourToIoi = std::fabs (fourBpm - ioiBpm);
            const float fourToShort = std::fabs (fourBpm - shortFitBpm);
            // fourCloser is KEEP (Door D 169090). Door C's 4-beat sits
            // between the late 8-beat and the IOI (192847 t=57.42
            // i4=60.6, short 59.5, IOI 61.8), so the closer-to-IOI
            // gate skipped the p95 frame. Live-only: unknown persist
            // through leftover comb (216604 t=51.10) is Door C too
            // and pulling there fattened that seed's p95 146→176.
            const bool fourCloser = fourToIoi < fourToShort;
            const bool fourBetween = tempoRegime == TempoRegime::live
                && (fourBpm - shortFitBpm) * (ioiBpm - shortFitBpm) > 0.0f
                && fourToShort > kFastDriftToleranceLine
                       * std::max (kMinBpm, shortFitBpm)
                && fourToIoi >= fourToShort;
            if (fourCloser || fourBetween)
            {
                // Leftover comb is faster than the line while the
                // band is already down (216604 t=46.14 i4=69.3 vs
                // comb 71.5, T 68.4). Pulling that 4-beat origin
                // fattened this seed's p95 146→176. Live-only
                // origin restrict failed the family mean; skip
                // only the leftover-faster unknown frames.
                // Offset-0 fisso/gradino never set ioiClockLead.
                const bool leftoverUnknown =
                    tempoRegime == TempoRegime::unknown
                    && combReady && combBpm > bpm
                    && (combBpm - bpm) > 0.005f * bpm;
                if (! leftoverUnknown)
                {
                    // fourCloser and fourBetween: lastBeat is the
                    // current interval (Door D 169090 t=47.58 a4 is
                    // the mixed-window intercept, still on the late
                    // grid; closerAfterGap KEEP is the same geometry
                    // after a hole). Cap unchanged. 0 offset-0
                    // fisso/gradino.
                    const double origin = lastBeatSec >= 0.0
                        ? lastBeatSec : a4;
                    const double period = lastBeatSec >= 0.0
                        ? static_cast<double> (recent)
                        : static_cast<double> (p4);
                    double shift = origin - gridAnchorSec;
                    shift -= std::round (shift / period) * period;
                    const double maxShift = kCombRulerTolerance * period;
                    gridAnchorSec += std::clamp (shift, -maxShift, maxShift);
                }
            }
            else if (tempoRegime == TempoRegime::live && lastBeatSec >= 0.0)
            {
                // Persist ioiLead after clock-only: the 4-beat has
                // left the IOI (169090 t=45.60 i4=98 vs IOI 94) so
                // fourCloser/fourBetween miss. lastBeat is still
                // the interval. Unknown Door C lastBeat REJECT.
                // Offset-0 fisso/gradino never set ioiClockLead.
                const double period = static_cast<double> (recent);
                double shift = lastBeatSec - gridAnchorSec;
                shift -= std::round (shift / period) * period;
                const double maxShift = kCombRulerTolerance * period;
                gridAnchorSec += std::clamp (shift, -maxShift, maxShift);
            }
        }
    }

    // Live fourBetween lastBeat origin before ioiClockLead can arm
    // (bir < kShortFit). 192847 t=29.92 is the FISSO-leave max
    // (i4=64.5 between short 63.2 and IOI 66.2, bir=1, phase 134 ms).
    // Clock-only ioiLead at bir<4 REJECT; this only pulls origin, same
    // cap. Offset-0 fisso/gradino: 0 frames (t≥0, fold already in i4).
    if (lineFeed && ! ioiClockLead && tempoRegime == TempoRegime::live
        && bpm < 75.0f && bpm > kMinBpm
        && beatsInRegime > 0 && beatsInRegime < kShortFit
        && haveShort && lastBeatSec >= 0.0 && gridAnchorSec >= 0.0)
    {
        float recent = 0.0f, p4 = 0.0f, r4 = 1.0f, c4 = 0.0f;
        double a4 = -1.0;
        if (recentPeriod (recent) && recent > 0.0f
            && fitPeriodBefore (4, p4, r4, c4, a4, nullptr, 0,
                                static_cast<double> (recent))
            && r4 < kMotionCurveResidual && p4 > 0.0f && a4 >= 0.0)
        {
            const float fourBpm = 60.0f / p4;
            const float ioiBpm = 60.0f / recent;
            const float fourToIoi = std::fabs (fourBpm - ioiBpm);
            const float fourToShort = std::fabs (fourBpm - shortFitBpm);
            const bool fourBetween =
                (fourBpm - shortFitBpm) * (ioiBpm - shortFitBpm) > 0.0f
                && fourToShort > kFastDriftToleranceLine
                       * std::max (kMinBpm, shortFitBpm)
                && fourToIoi >= fourToShort;
            if (fourBetween)
            {
                const double period = static_cast<double> (recent);
                double shift = lastBeatSec - gridAnchorSec;
                shift -= std::round (shift / period) * period;
                const double maxShift = kCombRulerTolerance * period;
                gridAnchorSec += std::clamp (shift, -maxShift, maxShift);
            }
        }
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
                                     float lowBand, float highBand) noexcept
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
    // 0.40 of the committed period is the usual separate-event floor.
    // 169090's true quarter is 0.20 after the early lattice lastBeat, so
    // it never becomes eligiblePeak and 0.18 keep never sees it. During
    // live Door D hold only, drop to 0.18 — still above the 2-frame
    // Gaussian retrigger floor, still below a sixteenth. 0 offset-0
    // fisso/gradino Door D frames.
    float refrFrac = 0.4f;
    if (lineFeed && tempoRegime == TempoRegime::live
        && ioiTargetHoldBeats > 0 && ioiTargetHoldBpm > kMinBpm)
        refrFrac = 0.18f;
    const int minRefr = std::max (2, static_cast<int> (
        refrFrac * eventReferencePeriod * static_cast<float> (fps)));

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
                                         >= static_cast<double> (refrFrac)
                                                * static_cast<double> (eventReferencePeriod));

    snapStalePulseToCombFold();

    bool acceptedByCurrentGrid = eligiblePeak;
    if (acceptedByCurrentGrid && established && lastBeatSec >= 0.0)
    {
        // The committed period is the usual ruler. When it has already left
        // the pulse, the same comb that names the true tempo is the ruler
        // instead, with a tighter keep so the stale pulse (0.16 of a
        // comb-beat away at ~16%) cannot sneak in beside the true one.
        const double ruler = pulseIndexGuess();
        const double gatePeriod = ruler > 0.0 ? ruler : static_cast<double> (period);
        double keep = ruler > 0.0 ? kCombRulerTolerance : kOnGridTolerance;
        if (const double splitKeep = stalePulseKeep (ruler); splitKeep > 0.0)
            keep = splitKeep;
        // True quarters on 169090 sit 0.20 off the early lattice once
        // Door D has named the 4-beat period. Widen only that hold.
        if (lineFeed && tempoRegime == TempoRegime::live
            && ioiTargetHoldBeats > 0 && ioiTargetHoldBpm > kMinBpm)
            keep = std::max (keep, kDoorDHoldKeep);
        // lastBeat stays the origin: a 5–20% step's next quarter is still
        // inside keep of that interval (gating on gridAnchor rejected those
        // peaks and moved offset-0 fisso/gradino). The hole-waive
        // (beats >= kGridStaleBeats) is what hats-only used: BeatNet treats
        // eighths as beats, so the first crest after 2.5 quiet quarters is
        // an off-beat hat, lastBeat flips onto it, true quarters are then
        // rejected, and checkGridPhase cannot unflip (fold contrast gone,
        // kFoldPhaseContrast 0.70). While a part is sounding, the committed
        // pulse is the one being played against — same hold the octave snap
        // already uses. The synthetic bank never sets sounding, so offset-0
        // fisso/gradino keep the waive. A confirmed transition still enters
        // through eligiblePeak.
        //
        // That hold is also a freeze: updateTempo / checkGridPhase run
        // only on an accepted peak, so refusing every off-lastBeat crest
        // leaves lastBeat and the fits stuck. Mixer/file witness: with
        // sounding, a ~123 BPM lock sat 28 s at conf 0 (max gap 29.3 s)
        // and never re-accepted; without sounding the same activations
        // re-anchored (max gap 2.0 s). Re-open after lastBeat is already
        // stale only for a crest that sits on the fold of the committed
        // tempo and is not a half-beat off lastBeat. On-fold alone
        // follows hats-only (fixture A, frac 0.467). The 0.40 bar keeps
        // the committed origin; the fold supplies a live period after
        // lastBeat has drifted.
        const double beats = (eventTimeSec - lastBeatSec) / gatePeriod;
        bool hatKickHalfSteal = false;
        if (std::fabs (beats - std::round (beats)) > keep
            && (beats < kGridStaleBeats || sounding))
        {
            acceptedByCurrentGrid = false;
            if (sounding && beats >= kGridStaleBeats && bpm > kMinBpm
                && tempo.ready())
            {
                float contrast = 1.0f;
                const float foldPhase = tempo.beatPhaseFor (bpm, contrast);
                if (foldPhase >= 0.0f && contrast <= kFoldPhaseContrast)
                {
                    const double foldPeriod = 60.0 / static_cast<double> (bpm);
                    const float eventPhase = wrap01 (
                        foldPhase - static_cast<float> (
                            (timeSec - eventTimeSec) / foldPeriod));
                    const double offLast = std::fabs (beats - std::round (beats));
                    // The fold's peak is whichever pulse is loud. Hats-only
                    // makes that the off-beat, so on-fold alone would flip
                    // lastBeat (fixture A, 2 hats, frac 0.467). Keep the
                    // committed origin: refuse a half-beat off lastBeat.
                    // A drifted true pulse sits inside 0.40 and on the fold.
                    if (offLast < 0.40
                        && std::fabs (static_cast<double> (wrapCentered (eventPhase)))
                               <= keep)
                    {
                        acceptedByCurrentGrid = true;
                        postHoleReopenSec = eventTimeSec;
                    }
                }
            }
            // FEEL hats-then-Q: lastBeat parked on hats, first quarter 0.5
            // later — not a hole, so the reopen above never runs. On-fold
            // would follow the hats. Product sounding is false until
            // following, so file-start is the snd=0 path which already
            // unfreezes; this is percussion-in from t=0. Mute when
            // lowBand is 0 (click bank, hats/rolls HOLD probes). Hats A
            // lastBeat is kick-class on a file feed, so hats after a
            // hole do not steal.
            if (! acceptedByCurrentGrid && sounding && bpm > kMinBpm)
            {
                const float eventLowBand = std::max (prevLowBand,
                                                     std::max (prevPrevLowBand, lowBand));
                const double offLast = std::fabs (beats - std::round (beats));
                if (lastAcceptedLowBand < kLowBandMute
                    && eventLowBand >= kLowBandMute
                    && offLast > 0.5 - kOnGridTolerance
                    && offLast < 0.5 + kOnGridTolerance)
                {
                    acceptedByCurrentGrid = true;
                    hatKickHalfSteal = true;
                    if (gridAnchorSec >= 0.0)
                    {
                        const double periodSec = 60.0 / static_cast<double> (bpm);
                        double shift = eventTimeSec - gridAnchorSec;
                        shift -= std::round (shift / periodSec) * periodSec;
                        gridAnchorSec += shift;
                        ++gridSerial;
                    }
                }
            }
        }
        else if (acceptedByCurrentGrid && sounding && beats >= kGridStaleBeats
                 && bpm > kMinBpm)
        {
            // On-grid after a hole: leftover lattice's first crest sits
            // on the committed fold (fixture D t=24.02, 7.0 beats at
            // 100, then 150 IOIs). The reopen stamp above only fires
            // when the crest was off lastBeat.
            postHoleReopenSec = eventTimeSec;
        }
        // Syncopated snare-roll: crests ~0.87 of a beat off lastBeat still
        // sit inside keep 0.18 and ratchet lastBeat off the fold (32nd/
        // sync fixtures: frac 0.03→0.47, then a live ~10% yank). On-fold
        // on every sounding accept rejected resume quarters after hats
        // (fold buffer still sees the hats). Refuse a lastBeat-keep pass
        // whose interval is past the stale-grid bar from the nearest
        // integer — 0.87 is log2 0.20, a true quarter is 1.0. Hats are
        // 0.5 and already fail keep. The synthetic bank never sets
        // sounding. The hat-to-kick half steal is 0.5 by construction
        // and would trip this log2 bar; it already named the pulse.
        if (acceptedByCurrentGrid && sounding && beats < kGridStaleBeats
            && beats > 0.5 && ! hatKickHalfSteal)
        {
            const double nearest = std::round (beats);
            if (nearest >= 1.0
                && std::fabs (std::log2 (beats / nearest)) > kStaleGridThreshold)
                acceptedByCurrentGrid = false;
        }
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
        if (! acceptedByCurrentGrid && ! confirmedTransition
            && prevPulse >= kTransitionStrengthFraction * recentBeatStrengthMedian())
            strongOffGridPeakSec = eventTimeSec;
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
        const float beatLowBandNow = std::max (prevLowBand,
                                               std::max (prevPrevLowBand, lowBand));
        const float beatHighBandNow = std::max (prevHighBand,
                                                std::max (prevPrevHighBand, highBand));
        // Drummer out, or only the hat is speaking. A percussionist who
        // already has the time does not recompute it from a hat that
        // crests early: that confirms the pulse. Writing the crest into
        // the fit is what accelerates. The kick-body hold still needs a
        // body heard first, and it waits 1.05 periods, so a hat between
        // two kicks does not arm it. A hat with no body — charleston
        // instead of kick and snare — used to keep tracking, and the
        // eighths walked the tempo off the quarter. Once the tempo is
        // established, a hat-class crest (low band under the mute, high
        // band present) is stored on the counted grid and does not call
        // updateTempo. The kicks, when they are there, still do. High
        // band defaults to 0, so the click bank and the matrix never
        // take it. While the grid is still provisional the eighth-fold
        // still has to see the raw intervals.
        const bool hatClass = lineFeed
                           && beatLowBandNow < kLowBandMute
                           && beatHighBandNow > kHighBandPresent;
        const bool holdHats = hatClass && established && ! provisional;
        const bool holdPulse = (beatLowBandNow < kLowBandMute
                                && kitBodyHolding (eventTimeSec))
                            || holdHats;
        if (beatLowBandNow >= kLowBandMute)
        {
            kitBodyHeard = true;
            kitBodyLastSec = eventTimeSec;
            hatGridHolding = false;
        }
        if (holdPulse)
        {
            if (holdHats)
                hatGridHolding = true;
            if (transitionState != TempoTransitionState::stable)
                dropTransitionCandidate (TempoTransitionReason::expired);
            const double periodSec = 60.0 / static_cast<double> (std::max (kMinBpm, bpm));
            const double origin = gridAnchorSec >= 0.0 ? gridAnchorSec : lastBeatSec;
            double snapped = eventTimeSec;
            if (origin >= 0.0)
            {
                const double n = std::round ((eventTimeSec - origin) / periodSec);
                snapped = origin + n * periodSec;
            }
            registerBeat (snapped, prevPulse, beatLowBandNow, beatHighBandNow);
            lastBeatSec = snapped;
            lastAcceptedLowBand = beatLowBandNow;
            refractoryFrames = minRefr;
            beatsInBar = (beatsInBar + 1) % 4;
        }
        else
        {
        registerBeat (eventTimeSec, prevPulse, beatLowBandNow, beatHighBandNow);
        lastBeatSec = eventTimeSec;
        lastAcceptedLowBand = beatLowBandNow;
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
        observeMetricalCadence (eventTimeSec, beatLowBandNow);
        if (prevDownbeat > downThresh)
        {
            lastDownbeatStrength = prevDownbeat;
            lastDownbeatSec = eventTimeSec;
            beatsInBar = 0;
            ++downbeatSerial;
            observeDownbeatCadence();
        }
        observeGridStep();
        updateTempo();
        // Door D hold is armed in updateTempo, after this peak already
        // took the 0.40-period refractory. 169090's true quarter is
        // 0.20 later and would stay blocked. Clamp to the hold floor.
        if (lineFeed && tempoRegime == TempoRegime::live
            && ioiTargetHoldBeats > 0 && ioiTargetHoldBpm > kMinBpm)
        {
            const int holdRefr = std::max (2, static_cast<int> (
                0.18f * period * static_cast<float> (fps)));
            if (refractoryFrames > holdRefr)
                refractoryFrames = holdRefr;
        }
        }
    }
    else if (! established && (frame % 8) == 0)
    {
        // The fold can name a tempo before any peak clears the gate, which is
        // most of the head start on locking.
        updateTempo();
    }

    const float newPeriod = 60.0f / std::max (kMinBpm, bpm);
    // Long-fit phase only while ioiLead on an interval-acquired grid.
    // Ungated (every ioiLead frame) was 36.096/87.459 → 35.631/88.098:
    // 216604 and 169090, comb/HMM grids with intervalAcquired clear
    // on all 668 and 1705 ioiLead frames, were the whole p95 rise.
    // 145333 (146/146), 113657 (1459/1459) and 224523 (2147/2147)
    // have it set. Gated: continuo 35.571/87.276, those two p95
    // unchanged (147.0, 121.9). Same 1.5-period current test as below.
    //
    // resetMotionShadow clears ioiClockLead on that 1.5-period boundary
    // and on other boundaries, while the next beats often keep
    // fastMotionCurrent true and longFitBpm still valid. Publishing
    // 60/longFitBpm on every fast-motion frame (no ioiLead) was
    // 35.375/87.738 and moved fisso/gradino. Hold the period only after
    // this same four-term gate has opened, until fast motion ends.
    // declarePulseHere also clears the hold: it sets lastBeat to now, so
    // fast motion would not end, and the next frames would keep walking
    // the pre-button period. ioiLead and the 0.01 s tau stay on
    // ioiClockLead alone.
    const bool fastMotionCurrent = lastBeatSec >= 0.0
                                   && timeSec - lastBeatSec
                                          <= 1.5 * static_cast<double> (newPeriod);
    // While the kick body is gone, or the pulse is only hats, the long
    // fit must not keep a faster period in the published phase: the
    // clock closes phase by bending its rate, so a short period here is
    // the rush. Hats-only never sets kitBodyHeard, so the second flag
    // is what covers that passage.
    if (kitBodyHolding (timeSec) || hatGridHolding)
        longFitPeriodHeld = false;
    const bool longFitPeriodGate = fastMotionCurrent && ioiClockLead
                                    && intervalAcquired && longFitBpm >= kMinBpm;
    if (longFitPeriodGate)
        longFitPeriodHeld = true;
    else if (! fastMotionCurrent)
        longFitPeriodHeld = false;
    const bool useLongFitPeriod = longFitPeriodHeld
                                  && intervalAcquired && longFitBpm >= kMinBpm;
    const float phasePeriod = useLongFitPeriod ? 60.0f / longFitBpm : newPeriod;
    const float phase = gridPhaseNow (phasePeriod);

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
    hyp.shortFitBpm = shortFitBpm;
    hyp.longFitBpm = longFitBpm;
    hyp.shortFitResidual = shortFitResidual;
    // `updateTempo` only runs on an accepted beat. Its last motion reading is
    // useful between ordinary beats, but after one and a half expected periods
    // it describes an onset train that is no longer present. Publish silence,
    // not stale authority, so a drummer dropout cannot leave the faster clock
    // loop armed until the next accepted peak happens to arrive.
    if (! fastMotionCurrent && motionShadow.veto != TempoMotionVeto::staleBeats)
        resetMotionShadow (false, TempoMotionVeto::staleBeats);
    hyp.fastTempoDeviation = fastMotionCurrent ? lastFastDeviation : 0.0f;
    hyp.fastIntervalDeviation = fastMotionCurrent ? lastIntervalDeviation : 0.0f;
    hyp.fastTempoEvidence = fastMotionCurrent ? fastDriftBeats : 0;
    hyp.fastTempoDirection = fastMotionCurrent ? fastDriftSign : 0;
    hyp.motionFitBpm = fastMotionCurrent ? motionFitBpm : 0.0f;
    hyp.motionFitRate = fastMotionCurrent ? motionFitRate : 0.0f;
    hyp.motionFitResidual = fastMotionCurrent ? motionFitResidual : 1.0f;
    hyp.motionFitImprovement = fastMotionCurrent ? motionFitImprovement : 0.0f;
    hyp.motionFitEvidence = fastMotionCurrent ? motionFitEvidence : 0;
    hyp.motionFitDirection = fastMotionCurrent ? motionFitDirection : 0;
    hyp.motionShadowBpm = fastMotionCurrent ? motionShadow.predictedBpm : 0.0f;
    hyp.motionShadowPeriodDelta =
        fastMotionCurrent ? motionShadow.periodDeltaPerBeat : 0.0f;
    hyp.motionShadowUncertainty = fastMotionCurrent ? motionShadow.uncertainty : 1.0f;
    hyp.motionShadowAuthority = fastMotionCurrent ? motionShadow.authority : 0.0f;
    hyp.motionShadowState =
        fastMotionCurrent ? static_cast<int> (motionShadow.state)
                          : static_cast<int> (TempoMotionShadowState::idle);
    hyp.motionShadowVeto =
        fastMotionCurrent ? static_cast<int> (motionShadow.veto)
                          : static_cast<int> (TempoMotionVeto::staleBeats);
    hyp.motionFirstStrictProof = fastMotionCurrent
                                     ? motionShadow.firstStrictProof : false;
    hyp.motionShapeModel =
        fastMotionCurrent ? static_cast<int> (motionShadow.shapeModel)
                          : static_cast<int> (TempoMotionShapeModel::insufficient);
    hyp.motionShapeBpm = fastMotionCurrent ? motionShadow.shapePredictedBpm : 0.0f;
    hyp.motionShapeQuadraticVsHinge =
        fastMotionCurrent ? motionShadow.shapeQuadraticVsHinge : 0.0f;
    hyp.motionShapeEvidenceMargin =
        fastMotionCurrent ? motionShadow.shapeEvidenceMargin : 0.0f;
    hyp.motionShapeQuadraticWins =
        fastMotionCurrent ? motionShadow.shapeQuadraticWins : 0;
    hyp.motionShapeQuarantineBeats =
        fastMotionCurrent ? motionShadow.shapeQuarantineBeats : 0;
    hyp.motionBridgeAuthority = fastMotionCurrent ? motionBridgeAuthority : 0.0f;
    hyp.ioiLead = fastMotionCurrent && ioiClockLead;
    // One and a half periods with no accepted beat, or the kick body has
    // been gone long enough that the crests still arriving are hats and
    // voice. Either way the part is already playing and the pulse is the
    // one counted: the direct-live rail would spend a phase debt at 7.5%
    // (about 8 BPM near 107) through a rest that has not changed tempo.
    // A hat one beat after a kick does not arm this — the body hold
    // itself waits 1.05 periods. The bank passes lowBand 0, so the body
    // is never heard there.
    hyp.beatGap = sounding && established && lastBeatSec >= 0.0
                  && (! fastMotionCurrent || kitBodyHolding (timeSec));
    hyp.transitionState = transitionState;
    hyp.transitionReason = transitionReason;
    hyp.transitionBpm = transitionPeriodSec > 0.0f ? 60.0f / transitionPeriodSec : 0.0f;
    hyp.transitionConfidence = transitionConfidence;
    hyp.transitionIntervals = transitionIntervals;
    hyp.transitionRefitBeats = transitionRefitBeats;
    hyp.transitionSerial = transitionSerial;
    hyp.confidence = scoreConfidence();
    hyp.valid = established;

    prevPrevPulse = prevPulse;
    prevPulse = pulseActivation;
    prevPrevDownbeat = prevDownbeat;
    prevDownbeat = pDownbeat;
    prevPrevLowBand = prevLowBand;
    prevLowBand = lowBand;
    prevPrevHighBand = prevHighBand;
    prevHighBand = highBand;
    return hyp;
}

} // namespace vp
