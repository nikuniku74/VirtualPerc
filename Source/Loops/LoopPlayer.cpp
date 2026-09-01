#include "Loops/LoopPlayer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vp
{

namespace
{
    /** The most the source read is ever asked to run at, which is what sizes
        the gather scratch. Well past the bank's own stretch limit: the limit
        decides which recording is *chosen*, this decides whether the buffer is
        big enough if it is ever asked for more. */
    constexpr float kMaxRatio = 2.0f;

    /** Where the second eighth sits, as a fraction of the beat, for a swing
        amount of 0..1. The same map GrooveEngine uses (`kFullSwingBeats`), so
        a loop and a single-stroke pattern shuffle by the same amount when the
        listener asks for the same number. */
    float eighthPosition (float swing) noexcept
    {
        return 0.5f + clamp01 (swing) * (1.0f / 6.0f);
    }
} // namespace

void LoopPlayer::prepare (double sr, int maxBlk) noexcept
{
    sampleRate = sr > 1.0 ? sr : 48000.0;
    maxBlock = std::max (32, maxBlk);

    const int maxIn = static_cast<int> (std::ceil (static_cast<float> (maxBlock) * kMaxRatio)) + 8;
    for (auto& v : voices)
    {
        v.stretcher.prepare (sampleRate, maxBlock, kMaxRatio);
        v.gatherL.assign (static_cast<size_t> (maxIn), 0.0f);
        v.gatherR.assign (static_cast<size_t> (maxIn), 0.0f);
        v.mixL.assign (static_cast<size_t> (maxBlock), 0.0f);
        v.mixR.assign (static_cast<size_t> (maxBlock), 0.0f);
        const int prime = std::max (1, v.stretcher.primeFrames());
        v.primeL.assign (static_cast<size_t> (prime), 0.0f);
        v.primeR.assign (static_cast<size_t> (prime), 0.0f);
    }

    xfadeSamples = std::max (16, static_cast<int> (sampleRate * 0.025));
    prepared = true;
    reset();
}

void LoopPlayer::setBank (const LoopBank* b) noexcept
{
    loops = b;
    reset();
}

void LoopPlayer::reset() noexcept
{
    for (auto& v : voices)
    {
        v.stretcher.reset();
        v.index = -1;
        v.cursor = 0.0;
        v.warp = 0.0;
        v.gain = 0.0f;
        v.gainTarget = 0.0f;
        v.gainStep = 0.0f;
        v.active = false;
    }
    cur = 0;
    songBeats = 0.0;
    absBeat = 0;
    haveBeat = false;
    armed = false;
    havePending = false;
    pendingIndex = -1;
    waitedQuarters = 0;
    miss = LoopMissReason::none;
    lastRatio = 1.0f;
    lastPhaseErrMs = 0.0f;
    passCount = 0;
    changeCount = 0;
    lastPassIndex = 0;
}

void LoopPlayer::start() noexcept
{
    armed = true;
    haveBeat = false;
}

void LoopPlayer::stop() noexcept
{
    // Immediate, and it means immediate: no fade, no bar line, no phrase to
    // finish. STOP is the control a player reaches for when the song has
    // stopped, and anything it defers is the app still playing over silence.
    armed = false;
    havePending = false;
    pendingIndex = -1;
    waitedQuarters = 0;
    for (auto& v : voices)
    {
        v.active = false;
        v.gain = 0.0f;
        v.gainTarget = 0.0f;
        v.gainStep = 0.0f;
        v.index = -1;
        v.stretcher.reset();
    }
}

void LoopPlayer::setCorrection (float tauSeconds, float maxPull) noexcept
{
    corrTau = std::clamp (tauSeconds, 0.02f, 20.0f);
    corrMaxPull = std::clamp (maxPull, 0.001f, 0.25f);
}

void LoopPlayer::setCrossfadeMs (float ms) noexcept
{
    xfadeSamples = std::max (16, static_cast<int> (sampleRate * static_cast<double> (ms) * 0.001));
}

bool LoopPlayer::isPlaying() const noexcept
{
    return voices[0].active || voices[1].active;
}

int LoopPlayer::currentIndex() const noexcept
{
    return voices[cur].active ? voices[cur].index : -1;
}

void LoopPlayer::request (const LoopQuery& q) noexcept
{
    if (loops == nullptr)
    {
        miss = LoopMissReason::noSuchPart;
        return;
    }

    LoopQuery asked = q;
    asked.avoidIndex = voices[cur].active ? voices[cur].index : -1;
    const LoopSelection sel = loops->select (asked);
    miss = sel.miss;
    if (! sel.ok())
    {
        // Nothing near enough. The player does not improvise a substitute: the
        // hybrid renderer above it takes the part back to single strokes, which
        // is the honest answer to "there is no recording of this".
        havePending = false;
        pendingIndex = -1;
        return;
    }

    if (voices[cur].active && voices[cur].index == sel.index)
    {
        // Already playing it. Keep the query so the swing warp tracks the
        // control, but do not schedule a change.
        active = q;
        havePending = false;
        pendingIndex = -1;
        return;
    }

    if (havePending && pendingIndex == sel.index)
        return;

    pending = q;
    pendingIndex = sel.index;
    havePending = true;
}

double LoopPlayer::mapBeatToOffset (const LoopBank::Entry& e, double loopBeat, bool withSwing) const noexcept
{
    const int total = e.manifest.totalBeats();
    if (total <= 0)
        return 0.0;

    int b = static_cast<int> (std::floor (loopBeat));
    if (b < 0) b = 0;
    if (b >= total) b = total - 1;
    double f = loopBeat - static_cast<double> (b);
    f = std::clamp (f, 0.0, 1.0);

    // Small swing adjustments, taken inside the beat. Not a substitute for
    // recording the shuffled take - past `LoopBank::swingTolerance` the bank
    // refuses and the part goes back to single strokes - but the difference
    // between a straight take and a song at swing 0.12 is a warp this size, and
    // warping it is better than playing it straight or refusing it.
    const float recPos = eighthPosition (e.manifest.swing);
    const float wantPos = eighthPosition (active.swing);
    if (withSwing && std::fabs (recPos - wantPos) > 1.0e-4f)
    {
        // Where in the *recording* the moment the listener hears at `f` is.
        if (f <= static_cast<double> (wantPos))
            f = f * static_cast<double> (recPos) / static_cast<double> (wantPos);
        else
            f = static_cast<double> (recPos)
                + (f - static_cast<double> (wantPos))
                      * (1.0 - static_cast<double> (recPos))
                      / (1.0 - static_cast<double> (wantPos));
        f = std::clamp (f, 0.0, 1.0);
    }

    const double a = e.manifest.beatPosition (b);
    const double bnext = e.manifest.beatPosition (b + 1);
    return (a + (bnext - a) * f) - e.bodyStart;
}

double LoopPlayer::sourceForBeat (const Voice& v, double songBeat, bool withSwing) const noexcept
{
    const auto& e = loops->entry (v.index);
    const int total = e.manifest.totalBeats();
    const double body = e.bodyEnd - e.bodyStart;
    if (total <= 0 || body <= 1.0)
        return 0.0;

    const double passes = std::floor (songBeat / static_cast<double> (total));
    const double within = songBeat - passes * static_cast<double> (total);
    return passes * body + mapBeatToOffset (e, within, withSwing);
}

double LoopPlayer::swingWarp (const Voice& v, double songBeat) const noexcept
{
    return sourceForBeat (v, songBeat, true) - sourceForBeat (v, songBeat, false);
}

void LoopPlayer::gather (const LoopBank::Entry& e, double from, int count,
                         float* dstL, float* dstR) const noexcept
{
    const double body = e.bodyEnd - e.bodyStart;
    const int frames = static_cast<int> (e.left.size());
    if (body <= 1.0 || frames <= 1)
    {
        std::fill (dstL, dstL + count, 0.0f);
        std::fill (dstR, dstR + count, 0.0f);
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        double p = from + static_cast<double> (i);
        // The body is what repeats. Wrapping it is not a special case handled at
        // the end of a pass - it is how the read always works, which is why
        // there is no junction to click at.
        p = std::fmod (p, body);
        if (p < 0.0)
            p += body;

        const double abs0 = e.bodyStart + p;
        int i0 = static_cast<int> (abs0);
        const float t = static_cast<float> (abs0 - static_cast<double> (i0));
        int i1 = i0 + 1;
        // One frame past the end of the body is the first frame of the next
        // pass, not the file's tail: interpolating into a tail would read
        // material the loop does not contain.
        if (static_cast<double> (i1) >= e.bodyEnd)
            i1 = static_cast<int> (e.bodyStart);
        if (i0 < 0) i0 = 0;
        if (i0 >= frames) i0 = frames - 1;
        if (i1 < 0) i1 = 0;
        if (i1 >= frames) i1 = frames - 1;

        const float l0 = e.left[static_cast<size_t> (i0)];
        const float l1 = e.left[static_cast<size_t> (i1)];
        const float r0 = e.right[static_cast<size_t> (i0)];
        const float r1 = e.right[static_cast<size_t> (i1)];
        dstL[i] = l0 + (l1 - l0) * t;
        dstR[i] = r0 + (r1 - r0) * t;
    }
}

void LoopPlayer::primeVoice (Voice& v, int index, double songBeat, float bpm) noexcept
{
    v.index = index;
    v.stretcher.reset();

    const auto& e = loops->entry (index);
    const double ratio = static_cast<double> (bpm) / static_cast<double> (e.manifest.nativeBpm);
    const double lead = v.stretcher.inputLeadFrames (ratio);
    v.warp = swingWarp (v, songBeat);
    v.cursor = sourceForBeat (v, songBeat, true) + lead;

    const int prime = std::min (static_cast<int> (v.primeL.size()), v.stretcher.primeFrames());
    if (prime > 0)
    {
        gather (e, v.cursor - static_cast<double> (prime), prime,
                v.primeL.data(), v.primeR.data());
        v.stretcher.prime (v.primeL.data(), v.primeR.data(), prime, ratio);
    }
    v.active = true;
}

void LoopPlayer::renderVoice (Voice& v, float* outL, float* outR, int first, int count,
                              double songBeatAtEnd, float bpm) noexcept
{
    if (! v.active || v.index < 0 || count <= 0)
        return;

    const auto& e = loops->entry (v.index);
    const double nominal = static_cast<double> (bpm) / static_cast<double> (e.manifest.nativeBpm);
    const double lead = v.stretcher.inputLeadFrames (nominal);
    const double want = sourceForBeat (v, songBeatAtEnd, true) + lead;

    // The swing warp is known, so it is added rather than discovered: how far
    // the warp has moved across this span goes straight into the rate, and the
    // correction loop below is left with the part that is actually an error.
    // Routed through the loop instead, an adjustment that happens inside one
    // beat comes out at about half the amount asked for - measured at 5.4 ms
    // where 12.5 was wanted.
    const double warpNow = swingWarp (v, songBeatAtEnd);
    const double warpStep = warpNow - v.warp;

    // Where the read would be at the end of this span if nothing corrected it,
    // and the rate change that closes the difference over `corrTau`. Rate, not
    // a jump: a jump moves the source under a stroke that is already sounding.
    const double free = v.cursor + nominal * static_cast<double> (count) + warpStep;
    const double err = want - free;
    const double perSample = err / (static_cast<double> (corrTau) * sampleRate);
    const double pull = std::clamp (perSample, -static_cast<double> (corrMaxPull) * nominal,
                                    static_cast<double> (corrMaxPull) * nominal);
    const double ratio = std::clamp (nominal + pull + warpStep / static_cast<double> (count),
                                     0.25, static_cast<double> (kMaxRatio));

    const int inFrames = std::min (static_cast<int> (v.gatherL.size()),
                                   v.stretcher.inputFramesFor (count, ratio));
    gather (e, v.cursor, inFrames, v.gatherL.data(), v.gatherR.data());

    v.stretcher.process (v.gatherL.data(), v.gatherR.data(), inFrames,
                         v.mixL.data(), v.mixR.data(), count);

    // Summed rather than written: the other voice may already have put its half
    // of a crossfade here.
    float* dstL = outL + first;
    float* dstR = outR + first;
    for (int i = 0; i < count; ++i)
    {
        if (v.gainStep != 0.0f)
        {
            v.gain += v.gainStep;
            if ((v.gainStep > 0.0f && v.gain >= v.gainTarget)
                || (v.gainStep < 0.0f && v.gain <= v.gainTarget))
            {
                v.gain = v.gainTarget;
                v.gainStep = 0.0f;
            }
        }
        // Equal power, so two voices half way through a crossfade are as loud
        // together as one is alone. A linear fade dips in the middle, and a dip
        // in the middle of a bar is heard as the part backing off.
        const float g = std::sqrt (clamp01 (v.gain));
        dstL[i] += v.mixL[static_cast<size_t> (i)] * g;
        dstR[i] += v.mixR[static_cast<size_t> (i)] * g;
    }

    v.cursor += static_cast<double> (inFrames);
    v.warp = warpNow;
    lastRatio = static_cast<float> (ratio);

    if (v.gain <= 0.0f && v.gainTarget <= 0.0f)
    {
        v.active = false;
        v.index = -1;
    }
}

void LoopPlayer::commitPending (double songBeat, float bpm) noexcept
{
    if (! havePending || pendingIndex < 0)
        return;

    Voice& outgoing = voices[cur];
    Voice& incoming = voices[1 - cur];

    // The voice that is on its way out has to be free. It is, unless a change
    // was asked for inside the length of the last crossfade, and in that case
    // the honest thing is to let the first one finish rather than to cut it.
    if (incoming.active && incoming.gain > 0.0f && incoming.gainStep != 0.0f)
        return;

    primeVoice (incoming, pendingIndex, songBeat, bpm);
    active = pending;

    const float step = 1.0f / static_cast<float> (std::max (1, xfadeSamples));
    if (outgoing.active)
    {
        outgoing.gainTarget = 0.0f;
        outgoing.gainStep = -step;
        incoming.gain = 0.0f;
        incoming.gainTarget = 1.0f;
        incoming.gainStep = step;
        ++changeCount;
    }
    else
    {
        // Coming in from nothing. A recorded loop starts on an attack, so the
        // fade exists only to keep the first sample from being a step, and it
        // is a few milliseconds rather than a crossfade.
        const float fast = 1.0f / static_cast<float> (std::max (1, static_cast<int> (sampleRate * 0.003)));
        incoming.gain = 0.0f;
        incoming.gainTarget = 1.0f;
        incoming.gainStep = fast;
    }

    cur = 1 - cur;
    havePending = false;
    pendingIndex = -1;
}

void LoopPlayer::process (float* outL, float* outR, int numSamples, const ClockTick& tick) noexcept
{
    if (outL == nullptr || numSamples <= 0)
        return;
    if (outR == nullptr)
        outR = outL;

    std::fill (outL, outL + numSamples, 0.0f);
    if (outR != outL)
        std::fill (outR, outR + numSamples, 0.0f);

    if (! prepared || loops == nullptr || loops->empty())
        return;

    const float bpm = tick.tempoBpm > 40.0f ? tick.tempoBpm : 120.0f;
    const double beatsPerSample = static_cast<double> (bpm) / 60.0 / sampleRate;

    // --- 1. Where the song is, and where its quarters fall in this block ----
    //
    // The position is *placed* on every quarter the clock emits and integrated
    // only in between. That is the phase lock: however the clock steers itself,
    // the loop's beat one and the song's beat one are the same sample.
    struct Quarter { int offset; long long beat; bool barStart; };
    Quarter quarters[8];
    int numQuarters = 0;

    for (int p = 0; p < tick.pulsesFired && numQuarters < 8; ++p)
    {
        if (tick.pulseIndex[p] != 0)
            continue;
        const int barBeat = tick.pulseBeatInBar[p];
        long long beat;
        if (! haveBeat)
        {
            // The first quarter after START. The part comes in on the quarter
            // of the bar the song is on, not on the loop's beat one: a
            // percussionist told to come in does not wait for the next bar and
            // then play the bar from its start.
            beat = static_cast<long long> (barBeat);
            haveBeat = true;
        }
        else
        {
            beat = absBeat + 1;
            // The clock's own count is the authority on which beat of the bar
            // this is. It only ever disagrees when the bar has been rotated
            // under us, and then the loop follows the song rather than its own
            // arithmetic.
            const int have = static_cast<int> (((beat % 4) + 4) % 4);
            if (have != barBeat && barBeat == 0)
                beat += ((0 - have) + 4) % 4;
        }
        absBeat = beat;
        quarters[numQuarters++] = { tick.pulseOffset[p], beat, barBeat == 0 };
    }

    const double songBeatsAtEnd = numQuarters > 0
        ? static_cast<double> (quarters[numQuarters - 1].beat)
              + static_cast<double> (numSamples - quarters[numQuarters - 1].offset) * beatsPerSample
        : songBeats + static_cast<double> (numSamples) * beatsPerSample;

    // --- 2. Come in, or change recording, on a quarter ----------------------
    //
    // Preferably a bar line: a part that changes on the third beat is a part
    // that changed in the middle of a figure. A quarter is accepted when the
    // change has already waited a bar's worth of quarters for a bar line.
    int commitAt = -1;
    double commitBeat = 0.0;
    if (armed && (havePending || ! isPlaying()))
    {
        // Coming in takes the first quarter there is. Changing recording waits
        // for a bar line, but not for ever: after a bar's worth of quarters it
        // takes the next one, because a listener who moved a control is owed the
        // change inside a bar rather than whenever the count next comes round.
        const bool wantBar = isPlaying();
        for (int i = 0; i < numQuarters; ++i)
        {
            if (wantBar && ! quarters[i].barStart && waitedQuarters < 4)
            {
                ++waitedQuarters;
                continue;
            }
            commitAt = quarters[i].offset;
            commitBeat = static_cast<double> (quarters[i].beat);
            break;
        }
    }

    // --- 3. Render, split at the commit point when there is one -------------
    const int split = commitAt >= 0 ? commitAt : 0;

    if (split > 0)
    {
        const double beatAtSplit = songBeats + static_cast<double> (split) * beatsPerSample;
        for (auto& v : voices)
            renderVoice (v, outL, outR, 0, split, beatAtSplit, bpm);
    }

    if (commitAt >= 0)
    {
        if (armed && ! isPlaying() && ! havePending && pendingIndex < 0)
        {
            // Armed with nothing scheduled: whatever was last asked for.
            LoopQuery q = active;
            const LoopSelection sel = loops->select (q);
            miss = sel.miss;
            if (sel.ok())
            {
                pendingIndex = sel.index;
                pending = q;
                havePending = true;
            }
        }
        commitPending (commitBeat, bpm);
        waitedQuarters = 0;
    }

    const int rest = numSamples - split;
    if (rest > 0)
    {
        for (auto& v : voices)
            renderVoice (v, outL, outR, split, rest, songBeatsAtEnd, bpm);
    }

    songBeats = songBeatsAtEnd;

    // --- 4. Diagnostics ----------------------------------------------------
    const Voice& v = voices[cur];
    if (v.active && v.index >= 0)
    {
        const auto& e = loops->entry (v.index);
        const double nominal = static_cast<double> (bpm) / static_cast<double> (e.manifest.nativeBpm);
        const double want = sourceForBeat (v, songBeats, true) + v.stretcher.inputLeadFrames (nominal);
        const double errFrames = v.cursor - want;
        // Frames of the *recording*, so the audible error is that divided by the
        // rate it is being played back at.
        lastPhaseErrMs = static_cast<float> (errFrames / nominal / sampleRate * 1000.0);

        const int total = e.manifest.totalBeats();
        if (total > 0)
        {
            const long long pass = static_cast<long long> (std::floor (songBeats / static_cast<double> (total)));
            if (pass != lastPassIndex)
            {
                if (pass > lastPassIndex)
                    ++passCount;
                lastPassIndex = pass;
            }
        }
    }
    else
    {
        lastPhaseErrMs = 0.0f;
    }
}

} // namespace vp
