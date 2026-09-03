#pragma once

#include "Core/Types.h"

#include <algorithm>
#include <cmath>

namespace vp
{

/** How long the clock averages the analysis's phase before the steering loop
    ever sees it, in seconds, while it is holding onto a song. */
constexpr float kGridTauHolding = 0.90f;

/** While still acquiring, being wrong quickly beats being wrong slowly. */
constexpr float kGridTauAcquire = 0.25f;

/** A confirmed tempo step gets one beat of faster phase convergence. */
constexpr float kGridTauRapid = 0.10f;

/** However bad the evidence gets, the clock still has to be able to follow a
    band. Two and a fifth seconds is four bars at 110 BPM. */
constexpr float kGridTauMax = 2.20f;

/**
    Whether the beats the analysis is fitting are as well placed as the ones it
    has been fitting all along, and by how much they are not.

    This exists for the passage where the drummer stops playing and the band
    does not. docs/STATUS.md has the symptom - the phase reaching 63.7 ms and
    the tempo sliding 1.1% through a four-bar break - and five attempts at
    curing it, none shipped. All five worked by *holding the tempo*, and every
    one cost half a bar on an accelerando, because in the first seconds "the
    drummer stopped" and "the band is speeding up" are the same signal seen from
    inside. For an app whose job is following a live drummer that is the trade
    the wrong way round.

    `VPAlign` now drives the whole chain - activations, decoder, clock - through
    both passages and scores the clock against the notated grid, and it says the
    mechanism is not the one that file assumed. A passage without a kit does not
    make the analysis noisy so much as make it **late**: a pad and a bass note
    swell into the beat where a stick lands on it, the activation crests a
    couple of frames after the beat, and the fit follows that faithfully for as
    long as the passage lasts. Take the lateness out of the same passage and it
    costs 29.5 ms at worst, which is what an accelerando costs. Put 44 ms of it
    back and it costs 79.5 ms. It is an offset held for ten seconds, so no
    amount of averaging can remove it - only a limit on how far the clock will
    go along with it.

    That is what this drives, in three places, none of which is the tempo:

      - the constant the phase is averaged over, `gridPhaseTau` below;
      - a floor under the clock's rate glide, so a target built out of badly
        placed beats is averaged rather than taken;
      - a ceiling on how far the clock will lean off its own grid at all.

    All three are bounded and self-clearing: nothing latches, nothing has to be
    released, and the moment the fit tightens the constants are what they were.
    Measured over eight songs, the clock against the notated grid:

        passage without drums   before 23.2 ms mean / 52.4 worst
                                after  19.1 ms mean / 45.7 worst
        the same song speeding  before 20.5 ms mean / 44.5 worst
        up, drums throughout    after  20.0 ms mean / 40.1 worst

    The accelerando improves too, which is the thing the five earlier attempts
    could not manage and the reason this shape of fix is the one that ships.

    The threshold is relative, which is the half of the answer that file did get
    to. Measured on this path the residual sits at 0.04-0.06 almost all the
    time, so no absolute line separates weak evidence from good; what separates
    them is the residual against what *this song* has been giving:

        residual outside the passage   hole 0.048   accelerando 0.044
        residual inside the passage    hole 0.063   accelerando 0.040

    +31% against -9%. The same probe rules out the discriminant that file names
    as the next place to look: the comb's salience reads 0.992 in the hole
    against a saturated 1.000 on the accelerando, and the decoder's own
    confidence 0.83 against 0.86. Neither separates them.
*/
class EvidenceTrust
{
public:
    void reset() noexcept
    {
        baselineResidual = 0.0f;
        haveBaseline = false;
        current = 1.0f;
        drumsOut = false;
    }

    /** One hypothesis' fit residual and coverage, and how much time has passed
        since the last call. Coverage gates the *baseline* only: a residual
        measured over a fit hardly any beats landed in describes too little to
        move what "good" means, but it is still the residual in force and still
        decides the trust. */
    void observe (float residual, float coverage, double seconds) noexcept
    {
        if (! std::isfinite (residual) || residual <= 0.0f || residual >= 1.0f)
            return;

        if (! haveBaseline)
        {
            baselineResidual = residual;
            haveBaseline = true;
        }
        else if (coverage > kCoverageToLearn)
        {
            // Asymmetric on purpose. A song that tightens up is believed
            // quickly; a song that loosens is believed slowly, so a passage
            // cannot redefine what good looks like inside its own length. Ten
            // seconds of pads against a baseline with a time constant of half a
            // minute move that baseline by a few percent, which is the point.
            const bool worse = residual > baselineResidual;
            const double tau = worse ? kBaselineRiseSec : kBaselineFallSec;
            const float a = 1.0f - std::exp (-static_cast<float> (seconds / tau));
            baselineResidual += (residual - baselineResidual) * a;
        }

        const float base = std::max (kResidualFloor, baselineResidual);
        const float ratio = residual / base;
        // 1 while the evidence is as good as this song has been giving, falling
        // to the floor as it gets half again as bad. Continuous: there is no
        // point at which the behaviour switches, so there is no point at which
        // it can switch back and forth.
        const float t = (kRatioTolerated - ratio) / (kRatioTolerated - 1.0f);
        current = std::clamp (t, kMinTrust, 1.0f);
    }

    /** The analysis has thrown its grid away and built another - a new song, a
        metrical level it no longer believes, an input that has just changed
        character. What this song was giving was that song's, and holding onto
        it would meet the new one with a baseline it never earned: on a song
        that fits worse than the last, every hypothesis would read as poor
        evidence, and the clock would be slowest to move at exactly the moment
        it has the furthest to go. */
    void restart() noexcept { reset(); }

    /** The kick channel says the drummer has stopped.

        This is the same conclusion the residual above is groping for, arrived
        at by looking instead of by inferring: a channel carrying one drum is
        silent for exactly as long as that drum is not being played. When it is
        available it is not evidence to be weighed against the fit, it is the
        answer, so it takes the trust straight to the floor. */
    void setDrumsOut (bool on) noexcept { drumsOut = on; }
    bool drumsAreOut() const noexcept { return drumsOut; }


    /** 1 when the analysis is fitting as well as it has been, down to
        `kMinTrust` when it is fitting much worse. */
    float trust() const noexcept { return drumsOut ? kMinTrust : current; }

    /** What the song has been giving, for the debug panel and the probes. */
    float baseline() const noexcept { return haveBaseline ? baselineResidual : 0.0f; }

private:
    static constexpr float kResidualFloor = 0.012f;
    static constexpr float kCoverageToLearn = 0.60f;
    static constexpr double kBaselineRiseSec = 30.0;
    static constexpr double kBaselineFallSec = 3.0;
    static constexpr float kRatioTolerated = 1.60f;
    static constexpr float kMinTrust = 0.30f;

    float baselineResidual = 0.0f;
    float current = 1.0f;
    bool  haveBaseline = false;
    bool  drumsOut = false;
};

/** The constant `BeatTracker` hands to `TempoFollower::setGridPhase`. Kept here
    rather than inline at the call site so the probes steer the clock on the
    same numbers the app does instead of on a copy of them that drifts.

    `baseTau` is a parameter and not a constant only because `VPAlign` measures
    alternatives to it. One of those was a shorter constant on a line feed,
    where the propagation path is single and steady and the analysis's phase is
    correspondingly quiet - docs/CORE_TIMING_AUDIT.md all but proposes it, since
    the 0.90 was there to swallow phase steps that no longer exist. Measured end
    to end over eight songs it is not an improvement: 23.2 -> 23.8 ms mean and
    52.4 -> 54.1 worst through a passage, and inside a millisecond of nothing on
    an accelerando. The decoder's phase already lags a moving tempo, so
    averaging it less follows the lag more closely rather than the band. It is
    not shipped, and this is where that is written down so it is not proposed
    again from the theory alone. */
inline float gridPhaseTau (float baseTau, bool holding, float trust) noexcept
{
    if (! holding)
        return kGridTauAcquire;
    const float t = std::clamp (trust, 0.05f, 1.0f);
    return std::min (kGridTauMax, baseTau / t);
}

} // namespace vp
