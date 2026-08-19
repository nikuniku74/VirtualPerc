#include "Audio/VirtualPercussionEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace vp
{

namespace
{
    // Where the analysis signal is held for the network, and it is not a free
    // parameter. BeatNet's features are log10(magnitude + 1): the +1 knee means
    // the level is part of the model's input rather than something the
    // normalisation removes, and madmom feeds the network integer-scaled audio,
    // several orders of magnitude above float [-1, 1]. Too quiet and the whole
    // filterbank sits on the linear part of that knee, where the network was
    // never trained.
    //
    // Measured end to end - 30 songs, 60 to 176 BPM, four styles, counting how
    // often the tracker settles on the wrong metrical level:
    //
    //     target peak   0.04  0.06  0.09  0.12  0.16  0.20  0.28  0.40  0.60
    //     wrong octave     7    13    13     8     4     2     2     4     7
    //
    // 0.12 sat on the near side of the optimum, and its failures were the
    // half-tempo readings above 150 BPM and the double-tempo readings below 72.
    constexpr float kMakeupTargetPeak = 0.20f;

    // Below this there is nothing to normalise, only noise to amplify. Room and
    // iPad-speaker-to-mic material commonly sits around 0.001-0.008, well above
    // it, which is the case this stage exists for.
    constexpr float kMakeupFloor = 0.0004f;
    constexpr float kMakeupMaxGain = 24.0f;

    // Seconds. Slow in both directions on purpose: this sets the network's
    // operating point, so it must not follow the music's dynamics. The gain
    // then follows the envelope quickly - the slowness belongs in one place,
    // and stacking a second multi-second smoother on top only means the first
    // seconds of a song are analysed at the wrong level, which is when the
    // metrical level is being decided.
    constexpr double kMakeupAttackSec = 0.8;
    constexpr double kMakeupReleaseSec = 4.0;
    constexpr double kMakeupGlideSec = 0.25;
}

void VirtualPercussionEngine::prepare (double sr, int maxBlk, int numInputChannels) noexcept
{
    sampleRate = sr > 1.0 ? sr : 48000.0;
    maxBlock = std::max (512, maxBlk);
    preparedInputs = std::max (1, numInputChannels);

    mono.assign (static_cast<size_t> (maxBlock), 0.0f);
    outL.assign (static_cast<size_t> (maxBlock), 0.0f);
    outR.assign (static_cast<size_t> (maxBlock), 0.0f);
    clickScratch.assign (static_cast<size_t> (maxBlock), 0.0f);
    outRing.assign (static_cast<size_t> (ringSize), 0.0f);
    ringWrite = 0;

    tracker.prepare (sampleRate);
    percussion.prepare (sampleRate);
    percussion.setSeed (0x51A4E1u);
    stretcher.prepare (sampleRate, maxBlock);
    stretch.prepare (120.0f, sampleRate);
    clickPhase = 0.0;
    lastSr.store (sampleRate, std::memory_order_relaxed);
}

void VirtualPercussionEngine::reset() noexcept
{
    tracker.reset();
    percussion.reset();
    stretcher.reset();
    stretch.reset();
    clickPhase = 0.0;
    std::fill (outRing.begin(), outRing.end(), 0.0f);
    ringWrite = 0;
    leakLp = 0.0f;
    peakEnv = 0.0f;
    makeupGain = 1.0f;
    tapWrite.store (0, std::memory_order_relaxed);
    tapRead = 0;
}

bool VirtualPercussionEngine::tryLoadNeuralHypothesis (BeatHypothesis& out) const noexcept
{
    return tracker.tryLoadHypothesis (out);
}

void VirtualPercussionEngine::loadPercussionLoop (const float* left, const float* right,
                                                  int frames, float nativeBpm)
{
    stretcher.prepare (sampleRate, maxBlock);
    stretcher.loadLoop (left, right, frames);
    stretch.prepare (nativeBpm, sampleRate);
}

void VirtualPercussionEngine::clearPercussionLoop()
{
    stretcher.loadLoop (nullptr, nullptr, 0);
    stretch.prepare (120.0f, sampleRate);
}

void VirtualPercussionEngine::start() noexcept
{
    tracker.start();
    percussion.clearVoices();
}

void VirtualPercussionEngine::stop() noexcept
{
    tracker.stop();
    percussion.silence();
}

void VirtualPercussionEngine::tapAt (double timeSeconds) noexcept
{
    const unsigned int w = tapWrite.load (std::memory_order_relaxed);
    tapTimes[w % static_cast<unsigned int> (tapQSize)] = timeSeconds;
    tapWrite.store (w + 1u, std::memory_order_release);
}

void VirtualPercussionEngine::tap() noexcept
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    tapAt (std::chrono::duration<double> (now).count());
}

void VirtualPercussionEngine::mixInputs (const float* const* inputs, int numInputs, int numSamples) noexcept
{
    std::fill (mono.begin(), mono.begin() + numSamples, 0.0f);
    if (inputs == nullptr || numInputs <= 0)
        return;

    const int assigned = cfg.analysisChannel.load (std::memory_order_relaxed);
    int used = 0;
    if (assigned >= 0 && assigned < numInputs && inputs[assigned] != nullptr)
    {
        std::memcpy (mono.data(), inputs[assigned], static_cast<size_t> (numSamples) * sizeof (float));
        used = 1;
    }
    else
    {
        for (int c = 0; c < numInputs; ++c)
        {
            if (inputs[c] == nullptr)
                continue;
            ++used;
            for (int i = 0; i < numSamples; ++i)
                mono[static_cast<size_t> (i)] += inputs[c][i];
        }
        if (used > 1)
        {
            const float g = 1.0f / static_cast<float> (used);
            for (int i = 0; i < numSamples; ++i)
                mono[static_cast<size_t> (i)] *= g;
        }
    }
}

void VirtualPercussionEngine::maybeInjectClick (int numSamples) noexcept
{
    std::fill (clickScratch.begin(), clickScratch.begin() + numSamples, 0.0f);
    if (! clickEnabled.load (std::memory_order_relaxed))
        return;

    const float bpm = std::max (40.0f, clickBpm.load (std::memory_order_relaxed));
    const double inc = (static_cast<double> (bpm) / 60.0) / sampleRate;

    for (int n = 0; n < numSamples; ++n)
    {
        const double ph = clickPhase;
        const double beat = ph - std::floor (ph);
        const int beatIndex = static_cast<int> (std::floor (ph));
        const double eighth = beat * 2.0 - std::floor (beat * 2.0);

        float kick = 0.0f;
        float snare = 0.0f;
        float hat = 0.0f;
        if (beat < 0.050)
        {
            const float t = static_cast<float> (beat) / static_cast<float> (bpm / 60.0);
            kick = std::sin (2.0f * 3.14159265f * 55.0f * t) * std::exp (-t * 24.0f);
            if ((beatIndex & 1) != 0)
            {
                const auto bits = static_cast<unsigned int> (n) * 1664525u + 1013904223u;
                const float noise = static_cast<float> ((bits >> 16) & 0x7fffu) / 32768.0f - 0.5f;
                snare = (0.55f * noise + 0.35f * std::sin (2.0f * 3.14159265f * 180.0f * t))
                        * std::exp (-t * 16.0f);
            }
        }
        if (eighth < 0.020)
        {
            const float t = static_cast<float> (eighth);
            const auto bits = static_cast<unsigned int> (n) * 1103515245u + 12345u;
            const float noise = static_cast<float> ((bits >> 16) & 0x7fffu) / 32768.0f - 0.5f;
            hat = noise * std::exp (-t * 90.0f) * 0.40f;
        }

        const float click = kick * 0.90f + snare + hat;
        clickScratch[static_cast<size_t> (n)] = click;
        mono[static_cast<size_t> (n)] += click;
        clickPhase += inc;
        if (clickPhase >= 4096.0)
            clickPhase -= 4096.0;
    }
}

void VirtualPercussionEngine::subtractSpeakerLeak (int numSamples) noexcept
{
    if (outRing.empty() || numSamples <= 0)
        return;

    const float latMs = std::max (8.0f, latencyMs.load (std::memory_order_relaxed));
    int delay = static_cast<int> (latMs * 0.001 * sampleRate);
    delay = std::clamp (delay, 64, ringSize - numSamples - 1);

    float xy = 0.0f, yy = 0.0f;
    float hpBuf[2048];
    const int n = std::min (numSamples, 2048);

    for (int i = 0; i < n; ++i)
    {
        const int ri = (ringWrite - delay + i + ringSize) % ringSize;
        const float y = outRing[static_cast<size_t> (ri)];
        leakLp += 0.18f * (y - leakLp);
        const float hp = y - leakLp;
        hpBuf[i] = hp;
        const float x = mono[static_cast<size_t> (i)];
        xy += x * hp;
        yy += hp * hp;
    }

    float g = yy > 1.0e-8f ? xy / yy : 0.0f;
    g = std::clamp (g, 0.0f, 0.90f);
    if (g < 0.08f)
        return;

    for (int i = 0; i < n; ++i)
        mono[static_cast<size_t> (i)] -= g * hpBuf[i];
}

void VirtualPercussionEngine::applyAnalysisMakeup (int numSamples, float rawPeak) noexcept
{
    // BeatNet's features are log10(magnitude + 1), which is not scale
    // invariant: the +1 knee means the level the analysis signal arrives at is
    // part of the model's input, not a detail the normalisation removes.
    // So this stage has two jobs, and the second one used to be missing: put the
    // signal at the level the network was validated at, and then hold it there.
    //
    // It used to take the peak instantly and release over half a second, so
    // every drum hit dropped the gain and the next half second crept back up -
    // moving the network's operating point on every beat, which is exactly the
    // input a beat tracker should never have. The envelope is slow in both
    // directions now, and the gain itself is smoothed again on top and ramped
    // across the block, so the analysis level is effectively constant over the
    // seconds the tempo estimator looks at.
    const float attack = 1.0f - std::exp (-static_cast<float> (numSamples)
                                          / std::max (1.0f, static_cast<float> (sampleRate * kMakeupAttackSec)));
    const float release = 1.0f - std::exp (-static_cast<float> (numSamples)
                                           / std::max (1.0f, static_cast<float> (sampleRate * kMakeupReleaseSec)));
    if (peakEnv < kMakeupFloor && rawPeak >= kMakeupFloor)
        peakEnv = rawPeak;   // first audio: start at the level, do not crawl up to it
    else
        peakEnv += (rawPeak - peakEnv) * (rawPeak > peakEnv ? attack : release);

    float wanted = 1.0f;
    if (peakEnv >= kMakeupFloor)
        wanted = std::clamp (kMakeupTargetPeak / std::max (peakEnv, 1.0e-5f), 1.0f, kMakeupMaxGain);

    const float smooth = 1.0f - std::exp (-static_cast<float> (numSamples)
                                          / std::max (1.0f, static_cast<float> (sampleRate * kMakeupGlideSec)));
    const float from = makeupGain;
    makeupGain += (wanted - makeupGain) * smooth;
    if (from <= 1.0001f && makeupGain <= 1.0001f)
        return;

    // Ramp within the block: a gain that steps between callbacks puts an edge
    // into the analysis signal, and the network hears edges.
    const float step = (makeupGain - from) / static_cast<float> (numSamples);
    for (int i = 0; i < numSamples; ++i)
        mono[static_cast<size_t> (i)] *= from + step * static_cast<float> (i);
}

void VirtualPercussionEngine::pushOutputToRing (int numSamples) noexcept
{
    if (outRing.empty())
        return;
    for (int i = 0; i < numSamples; ++i)
    {
        const float y = 0.5f * (outL[static_cast<size_t> (i)] + outR[static_cast<size_t> (i)]);
        outRing[static_cast<size_t> (ringWrite)] = y;
        ringWrite = (ringWrite + 1) % ringSize;
    }
}

void VirtualPercussionEngine::process (const float* const* inputs, int numInputs,
                                       float* const* outputs, int numOutputs,
                                       int numSamples) noexcept
{
    const auto t0 = std::chrono::steady_clock::now();

    if (numSamples > maxBlock)
        numSamples = maxBlock;
    if (numSamples <= 0)
        return;

    lastBuffer.store (numSamples, std::memory_order_relaxed);

    tracker.setFollowStrength (static_cast<FollowStrength> (cfg.followStrength.load (std::memory_order_relaxed)));
    tracker.setSubdivisionOverride (static_cast<Subdivision> (cfg.subdivision.load (std::memory_order_relaxed)));
    const bool speaker = cfg.followSource.load (std::memory_order_relaxed)
                         == static_cast<int> (FollowSource::speaker);
    tracker.setSpeakerFollow (speaker);
    tracker.setReportedLatencyMs (latencyMs.load (std::memory_order_relaxed));
    percussion.setHumanization (cfg.humanization.load (std::memory_order_relaxed));
    percussion.setVolume (cfg.percussionVolume.load (std::memory_order_relaxed));
    percussion.setReverbAmount (cfg.reverbAmount.load (std::memory_order_relaxed));
    percussion.setEnabled (cfg.shakerEnabled.load (std::memory_order_relaxed));
    percussion.setSwing (cfg.swing.load (std::memory_order_relaxed));
    percussion.setIntensity (cfg.intensity.load (std::memory_order_relaxed));
    percussion.setCongasEnabled (cfg.congasEnabled.load (std::memory_order_relaxed));
    percussion.setGrooveStyle (static_cast<GrooveStyle> (cfg.grooveStyle.load (std::memory_order_relaxed)));

    mixInputs (inputs, numInputs, numSamples);
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = std::max (peak, std::abs (mono[static_cast<size_t> (i)]));
    if (speaker)
        subtractSpeakerLeak (numSamples);
    maybeInjectClick (numSamples);
    applyAnalysisMakeup (numSamples, peak);
    float analysisPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        analysisPeak = std::max (analysisPeak, std::abs (mono[static_cast<size_t> (i)]));

    const unsigned int tw = tapWrite.load (std::memory_order_acquire);
    while (tapRead != tw)
    {
        tracker.tap (tapTimes[tapRead % static_cast<unsigned int> (tapQSize)]);
        ++tapRead;
    }

    const auto tr = tracker.process (mono.data(), numSamples);
    // The clock's own tempo, not the BPM on the display. `tr.bpm` is blank
    // until the tracker has locked and reads 0 before then, so the percussion
    // was being told 120 while the clock ran at whatever it had actually found.
    percussion.setGroove (tr.clock.tempoBpm > 40.0f ? tr.clock.tempoBpm
                                                    : (tr.bpm > 40.0f ? tr.bpm : 120.0f),
                          tr.clockPulsesPerBeat);
    percussion.setShakerSubdivision (tr.subdivision);

    if (stretcher.hasLoop())
    {
        stretch.setLiveClock (tr.bpm, tr.beatPhase, tr.confidence);
        const float ratio = stretch.advance (numSamples);
        stretcher.process (outL.data(), outR.data(), numSamples, ratio);
        if (! tr.percussionShouldPlay)
        {
            std::fill (outL.begin(), outL.begin() + numSamples, 0.0f);
            std::fill (outR.begin(), outR.begin() + numSamples, 0.0f);
        }
    }
    else
    {
        percussion.render (outL.data(), outR.data(), numSamples, tr.clock, tr.percussionShouldPlay);
    }

    const bool monitorClick = clickEnabled.load (std::memory_order_relaxed);
    const float master = cfg.masterVolume.load (std::memory_order_relaxed);
    if (outputs != nullptr)
    {
        for (int c = 0; c < numOutputs; ++c)
        {
            if (outputs[c] == nullptr)
                continue;
            const float* src = (c & 1) != 0 ? outR.data() : outL.data();
            for (int i = 0; i < numSamples; ++i)
            {
                float s = src[i];
                if (monitorClick)
                    s += clickScratch[static_cast<size_t> (i)] * 0.45f;
                outputs[c][i] = s * master;
            }
        }
    }

    pushOutputToRing (numSamples);

    lastBpm.store (tr.bpm, std::memory_order_relaxed);
    lastTarget.store (tr.targetBpm, std::memory_order_relaxed);
    lastConf.store (tr.confidence, std::memory_order_relaxed);
    lastBeat.store (tr.beatPhase, std::memory_order_relaxed);
    lastBar.store (tr.barPhase, std::memory_order_relaxed);
    lastPeak.store (peak, std::memory_order_relaxed);
    lastAnalysisPeak.store (analysisPeak, std::memory_order_relaxed);
    lastState.store (static_cast<int> (tr.state), std::memory_order_relaxed);
    lastSub.store (static_cast<int> (tr.subdivision), std::memory_order_relaxed);
    lastBeats.store (tr.beatsElapsed, std::memory_order_relaxed);
    lastAudible.store (tr.percussionShouldPlay, std::memory_order_relaxed);
    lastTapLock.store (tr.tapLocked, std::memory_order_relaxed);
    lastFollowBar.store (static_cast<int> (tr.followBar), std::memory_order_relaxed);
    lastVoices.store (percussion.activeVoices(), std::memory_order_relaxed);
    lastAiOnnx.store (tr.aiOnnx, std::memory_order_relaxed);
    lastHypValid.store (tr.hypValid, std::memory_order_relaxed);
    lastNeuralBpm.store (tr.neuralBpm, std::memory_order_relaxed);
    lastPBeat.store (tr.pBeat, std::memory_order_relaxed);
    lastLeadMs.store (tr.leadMs, std::memory_order_relaxed);
    lastRegime.store (static_cast<int> (tr.regime), std::memory_order_relaxed);

    const auto t1 = std::chrono::steady_clock::now();
    const float ms = std::chrono::duration<float, std::milli> (t1 - t0).count();
    lastCallbackMs.store (ms, std::memory_order_relaxed);
}

EngineSnapshot VirtualPercussionEngine::snapshot() const noexcept
{
    EngineSnapshot s;
    s.state = static_cast<TrackingState> (lastState.load (std::memory_order_relaxed));
    s.subdivision = static_cast<Subdivision> (lastSub.load (std::memory_order_relaxed));
    s.follow = static_cast<FollowStrength> (cfg.followStrength.load (std::memory_order_relaxed));
    s.source = static_cast<FollowSource> (cfg.followSource.load (std::memory_order_relaxed));
    s.bpm = lastBpm.load (std::memory_order_relaxed);
    s.targetBpm = lastTarget.load (std::memory_order_relaxed);
    s.confidence = lastConf.load (std::memory_order_relaxed);
    s.beatPhase = lastBeat.load (std::memory_order_relaxed);
    s.barPhase = lastBar.load (std::memory_order_relaxed);
    s.latencyMs = latencyMs.load (std::memory_order_relaxed);
    s.inputPeak = lastPeak.load (std::memory_order_relaxed);
    s.callbackMs = lastCallbackMs.load (std::memory_order_relaxed);
    s.humanization = cfg.humanization.load (std::memory_order_relaxed);
    s.reverbAmount = cfg.reverbAmount.load (std::memory_order_relaxed);
    s.shakerEnabled = cfg.shakerEnabled.load (std::memory_order_relaxed);
    s.percussionAudible = lastAudible.load (std::memory_order_relaxed);
    s.tapLocked = lastTapLock.load (std::memory_order_relaxed);
    s.followBar = static_cast<FollowBar> (lastFollowBar.load (std::memory_order_relaxed));
    s.bufferSize = lastBuffer.load (std::memory_order_relaxed);
    s.sampleRate = lastSr.load (std::memory_order_relaxed);
    s.beatsLocked = lastBeats.load (std::memory_order_relaxed);
    s.shakerVoices = lastVoices.load (std::memory_order_relaxed);
    s.aiOnnx = lastAiOnnx.load (std::memory_order_relaxed);
    s.hypValid = lastHypValid.load (std::memory_order_relaxed);
    s.neuralBpm = lastNeuralBpm.load (std::memory_order_relaxed);
    s.pBeat = lastPBeat.load (std::memory_order_relaxed);
    s.analysisPeak = lastAnalysisPeak.load (std::memory_order_relaxed);
    s.leadMs = lastLeadMs.load (std::memory_order_relaxed);
    s.tempoRegime = lastRegime.load (std::memory_order_relaxed);
    return s;
}

} // namespace vp
