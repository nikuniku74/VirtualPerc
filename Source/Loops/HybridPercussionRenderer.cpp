#include "Loops/HybridPercussionRenderer.h"

#include <algorithm>
#include <cmath>

namespace vp
{

namespace
{
    /** How long the two engines overlap when the part changes hands. Long
        enough not to be a cut, short enough that no stroke is heard twice. */
    constexpr float kHandoverMs = 45.0f;

    /** Blocks the regime has to hold its answer before the part changes hands.
        The decoder's regime is refreshed about six times a second; a handover
        on the first block that flickers is a handover twice a minute, and every
        one of them is audible. */
    constexpr int kHoldBlocks = 24;

    /** How the recording's read position is corrected in each regime.
        `fixed` is a click track: the only error there is drift, and closing it
        slowly is inaudible. A live band can move, so residual phase error must
        be closed sooner; the changing clock still supplies the main rate. */
    constexpr float kTauFixedSec = 8.0f;
    constexpr float kPullFixed = 0.012f;
    constexpr float kTauLiveSec = 2.0f;
    constexpr float kPullLive = 0.030f;
} // namespace

void HybridPercussionRenderer::prepare (double sr, int maxBlk) noexcept
{
    sampleRate = sr > 1.0 ? sr : 48000.0;
    maxBlock = std::max (32, maxBlk);

    for (LoopPlayer* p : { &congas, &shaker })
    {
        p->prepare (sampleRate, maxBlock);
        p->setCorrection (kTauFixedSec, kPullFixed);
        p->setCrossfadeMs (25.0f);
    }

    loopL.assign (static_cast<size_t> (maxBlock), 0.0f);
    loopR.assign (static_cast<size_t> (maxBlock), 0.0f);
    stemL.assign (static_cast<size_t> (maxBlock), 0.0f);
    stemR.assign (static_cast<size_t> (maxBlock), 0.0f);
    strokeL.assign (static_cast<size_t> (maxBlock), 0.0f);
    strokeR.assign (static_cast<size_t> (maxBlock), 0.0f);

    prepared = true;
    reset();
}

void HybridPercussionRenderer::reset() noexcept
{
    congas.reset();
    shaker.reset();
    mode = Mode::strokes;
    wantRecorded = false;
    blend = 0.0f;
    blendStep = 0.0f;
    handoverCount = 0;
    wantHeldBlocks = 0;
    barInPhrase = 0;
    seenBar = false;
    lastStrokes = 0;
}

void HybridPercussionRenderer::setBank (const LoopBank* b) noexcept
{
    congas.setBank (b);
    shaker.setBank (b);
    mode = Mode::strokes;
    blend = 0.0f;
    blendStep = 0.0f;
}

void HybridPercussionRenderer::setEnabled (bool on) noexcept
{
    if (enabled == on)
        return;
    enabled = on;
    if (! on)
    {
        congas.stop();
        shaker.stop();
        mode = Mode::strokes;
        blend = 0.0f;
        blendStep = 0.0f;
    }
}

void HybridPercussionRenderer::setAccentLayer (float gain) noexcept
{
    accentGain = clamp01 (gain);
}

void HybridPercussionRenderer::start() noexcept
{
    congas.start();
    shaker.start();
}

void HybridPercussionRenderer::stop() noexcept
{
    // Immediate on both sides. `PercussionEngine::silence` is the engine's own
    // STOP and is called by the layer above; this one is the recording's.
    congas.stop();
    shaker.stop();
    mode = Mode::strokes;
    blend = 0.0f;
    blendStep = 0.0f;
}

bool HybridPercussionRenderer::aimStem (LoopPlayer& player, LoopStem stem, const Input& in) noexcept
{
    const LoopBank* bank = player.bank();
    if (bank == nullptr)
        return false;

    LoopQuery q;
    q.style = in.style;
    q.role = roleForBar();
    q.stem = stem;
    q.bpm = in.bpm;
    q.swing = in.swing;
    q.intensity = clamp01 (in.intensity * in.dynamics);

    LoopSelection sel = bank->select (q);
    if (! sel.ok() && q.role != LoopRole::grooveA)
    {
        // No fill, or no B part, recorded for this style and stem. That is a gap
        // in the library, not a reason to stop playing: fall back to the part.
        q.role = LoopRole::grooveA;
        sel = bank->select (q);
    }
    if (! sel.ok())
        return false;

    player.request (q);
    return true;
}

LoopRole HybridPercussionRenderer::roleForBar() const noexcept
{
    // The eight-bar sentence, the same shape the single-stroke engine phrases
    // over: state it, answer it, state it, and a fill on the way out.
    if (barInPhrase == 7)
        return LoopRole::fill;
    if (barInPhrase >= 4)
        return LoopRole::grooveB;
    return LoopRole::grooveA;
}

int HybridPercussionRenderer::render (PercussionEngine& percussion, float* outL, float* outR,
                                      int numSamples, const Input& in) noexcept
{
    if (outL == nullptr || numSamples <= 0)
        return 0;
    if (outR == nullptr)
        outR = outL;

    // Where the sentence has got to. Counted here rather than read back off
    // PercussionEngine: while the recording is playing that engine is being
    // rendered silent, and a count that only advances when it is audible is a
    // count that stops.
    if (in.sectionChanged)
    {
        barInPhrase = 0;
        seenBar = true;
    }
    else if (in.tick.wrappedBar)
    {
        barInPhrase = seenBar ? (barInPhrase + 1) % 8 : 0;
        seenBar = true;
    }

    const LoopBank* bank = congas.bank();
    // The first bank and all its sample markers are authored at 48 kHz. Until
    // this path owns an explicit sample-rate converter, using those frames at
    // another device rate changes timing and can drive correction into audible
    // failure. Refuse it cleanly and leave the proven pattern renderer audible.
    const bool rateCompatible = bank != nullptr
                                && std::fabs (bank->sampleRate() - sampleRate) < 1.0;
    const bool haveBank = enabled && prepared && bank != nullptr && ! bank->empty()
                          && rateCompatible;

    // --- what the music is asking for --------------------------------------
    //
    // `audible` is the clock's real go/no-go decision: the original pattern
    // engine is already playing from that exact signal. The fixed/live regime
    // classification deliberately needs more history and can still be unknown
    // for several seconds after the beat grid is usable. Gating on it therefore
    // made LOOP lag behind PATTERN for no musical reason.
    wantRecorded = haveBank && in.audible;

    const bool fixedTempo = in.regime == TempoRegime::fixed;
    const float correctionTau = fixedTempo ? kTauFixedSec : kTauLiveSec;
    const float correctionPull = fixedTempo ? kPullFixed : kPullLive;
    congas.setCorrection (correctionTau, correctionPull);
    shaker.setCorrection (correctionTau, correctionPull);

    if (haveBank && in.audible)
    {
        // Each stem is asked separately, because each is a separate recording.
        // The part goes to the recording only if every instrument the listener
        // has switched on has one: half the part recorded and half played as
        // single strokes is two percussionists, not one.
        bool ok = false;
        if (in.congasEnabled)
            ok = aimStem (congas, LoopStem::congas, in);
        if (in.shakerEnabled)
        {
            const bool sh = aimStem (shaker, LoopStem::shaker, in);
            ok = in.congasEnabled ? (ok && sh) : sh;
        }
        if (! in.congasEnabled && ! in.shakerEnabled)
            ok = false;
        if (! ok)
            wantRecorded = false;   // nothing near enough: single strokes it is
    }

    if (wantRecorded != (mode == Mode::recorded))
        ++wantHeldBlocks;
    else
        wantHeldBlocks = 0;

    // --- and when the part may change hands ---------------------------------
    //
    // On a quarter, and on a bar line when there is one in this block. Never in
    // the middle of a figure, and never as a side effect of anything the clock
    // is doing: nothing here touches the clock or the phrase.
    bool quarterHere = false;
    bool barHere = false;
    for (int p = 0; p < in.tick.pulsesFired; ++p)
    {
        if (in.tick.pulseIndex[p] != 0)
            continue;
        quarterHere = true;
        if (in.tick.pulseBeatInBar[p] == 0)
            barHere = true;
    }

    const bool mayHandover = quarterHere && (barHere || wantHeldBlocks > kHoldBlocks * 4);
    const float blendStepSize =
        1.0f / std::max (1.0f, static_cast<float> (sampleRate) * kHandoverMs * 0.001f);

    if (mayHandover && wantHeldBlocks > kHoldBlocks && wantRecorded != (mode == Mode::recorded))
    {
        mode = wantRecorded ? Mode::recorded : Mode::strokes;
        ++handoverCount;
        wantHeldBlocks = 0;
        if (wantRecorded)
        {
            // Armed, not faded in. The recording comes in on a quarter of its
            // own, which may be later in this block or in the next one; starting
            // the fade here would take the single strokes away before there was
            // anything to replace them with, and the gap is up to a beat long.
            // The fade starts below, on the block the recording is actually
            // sounding.
            congas.start();
            shaker.start();
        }
        else
        {
            blendStep = -blendStepSize;
        }
    }

    // Going out: the recording is only told to stop once it is fully faded, so
    // the fade is real audio rather than a mute with a ramp drawn on it.
    const bool loopNeeded = mode == Mode::recorded || blend > 0.0f;

    // --- render both, then mix ---------------------------------------------
    //
    // The two stems are balanced by exactly the curve PercussionEngine uses on
    // the synthesised bank, so moving the control does the same thing whichever
    // percussionist is playing.
    if (loopNeeded)
    {
        const float mix = clamp01 (in.instrumentMix);
        const float shakerG = mix <= 0.5f ? 1.0f : 2.0f * (1.0f - mix);
        const float congaG = mix >= 0.5f ? 1.0f : 2.0f * mix;

        std::fill (loopL.begin(), loopL.begin() + numSamples, 0.0f);
        std::fill (loopR.begin(), loopR.begin() + numSamples, 0.0f);

        if (in.congasEnabled && congaG > 0.0f)
        {
            congas.process (stemL.data(), stemR.data(), numSamples, in.tick);
            for (int i = 0; i < numSamples; ++i)
            {
                loopL[static_cast<size_t> (i)] += stemL[static_cast<size_t> (i)] * congaG;
                loopR[static_cast<size_t> (i)] += stemR[static_cast<size_t> (i)] * congaG;
            }
        }
        if (in.shakerEnabled && shakerG > 0.0f)
        {
            shaker.process (stemL.data(), stemR.data(), numSamples, in.tick);
            for (int i = 0; i < numSamples; ++i)
            {
                loopL[static_cast<size_t> (i)] += stemL[static_cast<size_t> (i)] * shakerG;
                loopR[static_cast<size_t> (i)] += stemR[static_cast<size_t> (i)] * shakerG;
            }
        }
    }

    // The single-stroke engine is always rendered, even at zero gain. It is the
    // fallback, and a fallback that has not been running is a fallback that
    // comes in cold: its voices, its round-robin and its phrase would all start
    // from wherever they were left when the recording took over.
    const bool strokesAudible = in.audible && (mode == Mode::strokes || blend < 1.0f
                                               || accentGain > 0.0f);
    lastStrokes = percussion.render (strokeL.data(), strokeR.data(), numSamples,
                                     in.tick, strokesAudible);

    // The recording is sounding: now the strokes may go. See the handover above.
    if (mode == Mode::recorded && blend < 1.0f && blendStep <= 0.0f
        && (congas.isPlaying() || shaker.isPlaying()))
        blendStep = blendStepSize;

    // What the band is giving. Applied to the recording's share only:
    // PercussionEngine has already been told the dynamics and has thinned
    // itself, so scaling its output here would take the reduction twice.
    const float dyn = clamp01 (in.dynamics);

    for (int i = 0; i < numSamples; ++i)
    {
        if (blendStep != 0.0f)
        {
            blend += blendStep;
            if (blend >= 1.0f) { blend = 1.0f; blendStep = 0.0f; }
            if (blend <= 0.0f) { blend = 0.0f; blendStep = 0.0f; }
        }
        // Equal power across the handover, so the part does not dip through it.
        const float gLoop = std::sqrt (clamp01 (blend)) * dyn;
        const float gStroke = std::sqrt (clamp01 (1.0f - blend));
        // And what the single strokes are allowed to add *over* a recording -
        // zero unless somebody has turned it up. See setAccentLayer.
        const float strokeMix = std::max (gStroke, accentGain * clamp01 (blend));

        outL[i] = (loopNeeded ? loopL[static_cast<size_t> (i)] * gLoop : 0.0f)
                  + strokeL[static_cast<size_t> (i)] * strokeMix;
        outR[i] = (loopNeeded ? loopR[static_cast<size_t> (i)] * gLoop : 0.0f)
                  + strokeR[static_cast<size_t> (i)] * strokeMix;
    }

    if (mode == Mode::strokes && blend <= 0.0f)
    {
        congas.stop();
        shaker.stop();
    }

    return lastStrokes;
}

} // namespace vp
