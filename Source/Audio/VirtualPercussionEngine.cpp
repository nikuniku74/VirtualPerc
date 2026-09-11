#include "Audio/VirtualPercussionEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

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

    // This stage was boost-only - clamped to a floor of 1.0 - so an input that
    // already arrives hotter than the target peak (a line-level feed, or the
    // user's own input-gain trim turned up) was never brought back down. Full
    // symmetry with the target above (attenuating anything over 0.20 back down
    // to it) was tried and reverted: it measurably helps a hot input, but it
    // also touches material that used to pass through this stage at gain 1.0
    // untouched, and the octave/level state space downstream is not uniformly
    // indifferent to that - the octave-sweep bench in TestAiBeat.cpp (168 BPM)
    // reads its half the moment this stage moves it from 1.0 to 0.8 - a two
    // decibel move. That bench sits at a peak of ~0.25, already close to the
    // target, which is exactly the regime the boost-only floor used to leave
    // alone - undoing that costs more than it buys until this is revalidated
    // the way the target above was, across the same spread of songs and
    // styles, not just this one bench.
    //
    // So this only catches the case boost-only cannot help at all: a signal
    // already near clipping, which no amount of downstream gain can undo once
    // it happens. Below this peak, gain stays exactly as it was - 1.0, no
    // attenuation - so every song this file's tables were measured against
    // keeps the analysis level it was validated at.
    constexpr float kMakeupClipGuardPeak = 0.90f;
    constexpr float kMakeupMinGain = 1.0f / kMakeupMaxGain;

    // Seconds. Slow in both directions on purpose: this sets the network's
    // operating point, so it must not follow the music's dynamics. The gain
    // then follows the envelope quickly - the slowness belongs in one place,
    // and stacking a second multi-second smoother on top only means the first
    // seconds of a song are analysed at the wrong level, which is when the
    // metrical level is being decided.
    constexpr double kMakeupAttackSec = 0.8;
    constexpr double kMakeupReleaseSec = 4.0;
    constexpr double kMakeupGlideSec = 0.25;

    // Watching the analysis level for the moment something starts playing.
    //
    // Fast up so the start is caught inside a third of a second, slow down so
    // the gaps between hits do not read as the music stopping.
    constexpr double kLevelFastAttackSec = 0.05;
    constexpr double kLevelFastReleaseSec = 1.5;

    // How much louder than the level it has been sitting at counts as
    // something starting, and for how long. Between an empty room and a band
    // there are thirty to forty decibels; between a verse and a chorus, three
    // to eight, and a drums-out passage with the bass and the pads still in it
    // is nearer ten. Eighteen decibels sits above all of those and well below
    // the one this exists to catch, and a third of a second is longer than any
    // single hit.
    constexpr float  kLevelStepUp = 8.0f;
    constexpr double kLevelStepHoldSec = 0.30;

    // And how far below the loudest this input gets we must have been sitting
    // for the rise to be something *starting* rather than something getting
    // louder. Without it a breakdown coming back in reads as a new song, and
    // measured over thirty tracks that costs more than the whole fix gains:
    // every drums-out passage throws away a working grid.
    //
    // Twenty-four decibels. Between an empty room and a band there are thirty
    // to forty; the deepest breakdown that still has a band in it is nearer
    // twenty. How long that memory lasts has to outlive the gap between two
    // songs, which is where the silence this exists to notice actually is.
    constexpr float  kQuietFraction = 0.0625f;
    constexpr double kLoudMemorySec = 60.0;

    // Before it can say the level has changed, the watcher has to know where
    // the level is. One block is five milliseconds and can land anywhere inside
    // a kick, so a reference taken from it is a fraction of the real level and
    // the rest of that first note reads as something starting.
    constexpr double kLevelPrimeSec = 0.5;

    // How long our own part stays answerable for a rise on the input after it
    // starts. The leak comes back late - the device round trip plus, in a room,
    // the flight - and the canceller needs a moment to find it.
    constexpr double kOwnStepBlameSec = 0.75;

    // The rhythm-section watcher, and why it reads a *share* rather than a
    // level. Measured on the reference song (docs/TODO.md item 29): its first
    // thirty seconds are voice, guitar and pad with no drums and no bass, and
    // the energy below 200 Hz is 0.14-0.26 of the whole; from the entrance on
    // it is 0.53-0.61. A ratio is what survives this path - the make-up gain
    // downstream is broadband, so it cannot forge one - and it is the only
    // statistic measured on that song that separates the intro from the band
    // at all. Absolute level does not: the intro is real music, not a quiet
    // room, so the level step the epoch watcher looks for never happens.
    //
    // It is deliberately a *relative* test, and that is what makes it safe on
    // everything else. The synthetic kit the benches use sits flat at 0.17 for
    // its whole length and the click track at 0.002: neither ever steps, so
    // neither can be delayed or restarted by this. It fires on material that
    // begins without a rhythm section and then acquires one, which is exactly
    // the case it was built for.
    constexpr double kLowBandHz = 200.0;
    constexpr double kShareSmoothSec = 2.0;
    constexpr double kSharePrimeSec = 0.5;
    // The plateau follows the share down within a few seconds and back up over
    // half a minute: a step has to stay to count, and a slow drift upward must
    // not quietly raise the bar out from under it.
    constexpr double kShareFallSec = 4.0;
    constexpr double kShareRiseSec = 30.0;
    constexpr float  kShareStepUp = 2.0f;
    constexpr double kShareStepHoldSec = 1.5;
    // Below this a low band is a detail of the mix, not a section. The
    // click track's 0.002 doubles on nothing at all; this is what stops the
    // ratio from being sensitive where it has no business being.
    constexpr float  kShareFloor = 0.30f;
    // The standing fact has to be available before the part is due in, which
    // on the benches is four tenths of a second after the music starts. So the
    // energies are primed at the first block rather than released into, and the
    // hold is a third of a second: long enough that one bass note cannot say
    // there is a section, short enough not to delay an entrance.
    constexpr double kShareHighHoldSec = 0.33;
}

void VirtualPercussionEngine::prepare (double sr, int maxBlk, int numInputChannels) noexcept
{
    sampleRate = sr > 1.0 ? sr : 48000.0;
    maxBlock = std::max (512, maxBlk);
    preparedInputs = std::max (1, numInputChannels);

    mono.assign (static_cast<size_t> (maxBlock), 0.0f);
    kickScratch.assign (static_cast<size_t> (maxBlock), 0.0f);
    kickDetector.prepare (sampleRate);
    rawIn.assign (static_cast<size_t> (maxBlock), 0.0f);
    latencyProbe.prepare (sampleRate);
    bandDynamics.prepare (sampleRate);
    harmony.prepare (sampleRate);
    standingDown = false;
    wantStandDown = false;
    outL.assign (static_cast<size_t> (maxBlock), 0.0f);
    outR.assign (static_cast<size_t> (maxBlock), 0.0f);
    clickScratch.assign (static_cast<size_t> (maxBlock), 0.0f);
    for (auto& b : leakBand)
        b.assign (static_cast<size_t> (maxBlock), 0.0f);
    outRing.assign (static_cast<size_t> (ringSize), 0.0f);
    ringWrite = 0;
    // The reference the canceller fits against has just been zeroed, so any
    // evidence gathered against it describes a signal that no longer exists.
    // With the same device back at the same latency nothing else would notice:
    // the delay key would match and the old cross-products would keep pulling
    // the gain toward the old room. Measured on a restart from a 0.6 return into
    // a 0.15 one: the analysis differed from a new engine's by 0.156 of peak,
    // thirty blocks in.
    resetLeakEstimate();
    // Same boundary, same reason: the level this input arrives at is a property
    // of the device that has just been swapped out.
    resetAnalysisLevelState();

    tracker.prepare (sampleRate);
    // A new audio-device session owns a newly cleared hypothesis slot. Mirror
    // that lifecycle boundary in the public diagnostics before any new audio.
    lastHypValid.store (false, std::memory_order_relaxed);
    clearAnalysisLevelMirrors();
    lastTempoTransitionState.store (
        static_cast<int> (TempoTransitionState::stable), std::memory_order_relaxed);
    lastTempoTransitionReason.store (
        static_cast<int> (TempoTransitionReason::none), std::memory_order_relaxed);
    lastTempoTransitionBpm.store (0.0f, std::memory_order_relaxed);
    lastTempoTransitionConfidence.store (0.0f, std::memory_order_relaxed);
    lastTempoTransitionIntervals.store (0, std::memory_order_relaxed);
    percussion.prepare (sampleRate);
    hybrid.prepare (sampleRate, maxBlock);
    hybrid.setBank (loopBank.get());
    styleDetector.prepare (sampleRate);
    percussion.setSeed (0x51A4E1u);
    stretcher.prepare (sampleRate, maxBlock);
    stretch.prepare (120.0f, sampleRate);
    clickPhase = 0.0;
    lastSr.store (sampleRate, std::memory_order_relaxed);
    analysisSuspended = false;
}

bool VirtualPercussionEngine::isPreparedFor (double sr) const noexcept
{
    if (analysisSuspended)
        return false;
    const double have = lastSr.load (std::memory_order_relaxed);
    return have > 1.0 && std::abs (have - sr) < 1.0;
}

void VirtualPercussionEngine::suspendAnalysis()
{
    tracker.suspendAnalysis();
    analysisSuspended = true;
}

void VirtualPercussionEngine::resetAnalysisLevelState() noexcept
{
    // Where the analysis level is, where it has been, and how much of it is
    // ours: `applyAnalysisMakeup`'s envelope and gain, `updateAnalysisEpoch`'s
    // three level trackers and their timers, the epoch count itself, and the
    // own-output envelope the blame is drawn from.
    //
    // All of it is a description of one input on one device. A new session is a
    // new input - the route can go from a mixer aux at line level to a
    // microphone in a room forty decibels down - so carrying this across is
    // describing audio that is no longer arriving. `reset()` has always cleared
    // it; `prepare()` cleared none of it, while zeroing the very buffers it was
    // measured from. Measured on a line-level session followed by a
    // re-`prepare()` onto a source forty decibels down: the new session analysed
    // its input at a gain of 1.0 where a new engine reached 23.7, and reported
    // the old session's epoch count on its first block.
    peakEnv = 0.0f;
    makeupGain = 1.0f;
    levelFast = 0.0f;
    levelRef = 0.0f;
    levelLoud = 0.0f;
    levelStepSamples = 0;
    levelPrimeSamples = 0;
    lowLp1 = lowLp2 = 0.0f;
    lowEnergy = fullEnergy = 0.0f;
    shareBase = 0.0f;
    shareStepSamples = 0;
    sharePrimeSamples = 0;
    shareHighSamples = 0;
    rhythmSeen = false;
    lastLowShare.store (0.0f, std::memory_order_relaxed);
    analysisEpoch.store (0, std::memory_order_relaxed);
    preserveCombOnEpoch = false;
    barReentryPending.store (false, std::memory_order_relaxed);
    musicGapSamples = 0;
    musicGapArmed = false;
    ownPeakLast = 0.0f;
    ownFast = 0.0f;
    ownRef = 0.0f;
    ownStepSamples = 0;
}

void VirtualPercussionEngine::clearAnalysisLevelMirrors() noexcept
{
    // The public face of the three scalars above. They are written from the
    // audio callback only, so between a `prepare()` and the first block of the
    // new session a reader - the UI, a probe, a test - was handed the *last*
    // session's epoch count, make-up gain and analysis peak, from a device that
    // is no longer open. Their neighbours in the same snapshot (`hypValid`, the
    // tempo-transition group) are already cleared at this boundary; these three
    // were the ones left describing audio that has stopped arriving.
    lastRestarts.store (0, std::memory_order_relaxed);
    lastAnalysisGain.store (1.0f, std::memory_order_relaxed);
    lastAnalysisPeak.store (0.0f, std::memory_order_relaxed);
}

void VirtualPercussionEngine::resetLeakEstimate() noexcept
{
    leakLpLow = 0.0f;
    leakLp = 0.0f;
    analysisHp = 0.0f;
    for (auto& g : leakGain)
        g = 0.0f;
    for (int b = 0; b < kLeakBands; ++b)
    {
        leakFitXy[b] = 0.0;
        for (int c = 0; c < kLeakBands; ++c)
            leakFitGram[b][c] = 0.0;
    }
    leakFitDelay = -1;
    leakDelaySamples = 0;
    leakScanCountdown = 0;
    leakDelayLocked = false;
}

void VirtualPercussionEngine::reset() noexcept
{
    tracker.reset();
    lastHypValid.store (false, std::memory_order_relaxed);
    clearAnalysisLevelMirrors();
    lastTempoTransitionState.store (
        static_cast<int> (TempoTransitionState::stable), std::memory_order_relaxed);
    lastTempoTransitionReason.store (
        static_cast<int> (TempoTransitionReason::none), std::memory_order_relaxed);
    lastTempoTransitionBpm.store (0.0f, std::memory_order_relaxed);
    lastTempoTransitionConfidence.store (0.0f, std::memory_order_relaxed);
    lastTempoTransitionIntervals.store (0, std::memory_order_relaxed);
    percussion.reset();
    hybrid.reset();
    styleDetector.reset();
    stretcher.reset();
    stretch.reset();
    clickPhase = 0.0;
    std::fill (outRing.begin(), outRing.end(), 0.0f);
    ringWrite = 0;
    resetLeakEstimate();
    resetAnalysisLevelState();
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
    hybrid.start();
}

void VirtualPercussionEngine::stop() noexcept
{
    tracker.stop();
    percussion.silence();
    hybrid.stop();
}

bool VirtualPercussionEngine::loadLoopBank (const std::string& manifestPath, std::string& error)
{
    auto bank = std::make_unique<LoopBank>();
    if (! bank->loadFromManifestFile (manifestPath, error))
        return false;

    loopBank = std::move (bank);
    hybrid.setBank (loopBank.get());
    return true;
}

bool VirtualPercussionEngine::loadLoopBankFromMemory (const std::string& manifestText,
                                                       const LoopBank::AudioLoader& loader,
                                                       std::string& error)
{
    auto bank = std::make_unique<LoopBank>();
    if (! bank->loadWithAudioLoader (manifestText, loader, error))
        return false;

    loopBank = std::move (bank);
    hybrid.setBank (loopBank.get());
    return true;
}

void VirtualPercussionEngine::clearLoopBank()
{
    hybrid.setBank (nullptr);
    loopBank.reset();
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

void VirtualPercussionEngine::setTempoFollow (bool follow) noexcept
{
    const bool was = cfg.tempoFollow.exchange (follow, std::memory_order_relaxed);
    if (was && ! follow)
    {
        float bpm = lastBpm.load (std::memory_order_relaxed);
        if (bpm < 50.0f)
            bpm = cfg.userBpm.load (std::memory_order_relaxed);
        if (bpm < 50.0f)
            bpm = 120.0f;
        bpm = std::clamp (bpm, 50.0f, 200.0f);
        cfg.userBpm.store (bpm, std::memory_order_relaxed);
        cfg.userBpmGen.fetch_add (1u, std::memory_order_relaxed);
    }
}

void VirtualPercussionEngine::setFixedBpm (float bpm) noexcept
{
    if (! std::isfinite (bpm))
        return;
    bpm = std::clamp (bpm, 50.0f, 200.0f);
    cfg.tempoFollow.store (false, std::memory_order_relaxed);
    cfg.userBpm.store (bpm, std::memory_order_relaxed);
    cfg.userBpmGen.fetch_add (1u, std::memory_order_relaxed);
}

void VirtualPercussionEngine::notifyTrackSeek() noexcept
{
    // A seek is a cut, not a new song. Restarting the decoder here was
    // throwing away a tempo that is still right; the one is what moved.
    barReentryPending.store (true, std::memory_order_relaxed);
}

void VirtualPercussionEngine::mixInputs (const float* const* inputs, int numInputs, int numSamples) noexcept
{
    std::fill (mono.begin(), mono.begin() + numSamples, 0.0f);
    if (inputs == nullptr || numInputs <= 0)
    {
        tracker.setKickChannelState (false, 0.0f);
        lastKickChannel.store (-1, std::memory_order_relaxed);
        return;
    }

    // The kick channel first, and off the *raw* input.
    //
    // Everything the analysis bus does below - the leak subtraction, the
    // rumble high-pass, the make-up gain - exists to protect a microphone that
    // is hearing the app's own output in a room. A desk send of the kick has
    // neither problem, and putting a canceller and a gain rider in front of the
    // one clean transient on the stage would be giving away exactly what makes
    // it worth having.
    {
        const int kick = cfg.kickChannel.load (std::memory_order_relaxed);
        const bool directFile = cfg.followSource.load (std::memory_order_relaxed)
                                == static_cast<int> (FollowSource::internalPlayer);
        const bool assigned = ! directFile && kick >= 0 && kick < numInputs
                              && inputs[kick] != nullptr;
        if (assigned)
        {
            KickOnsetDetector::Onset on[KickOnsetDetector::kMaxOnsets];
            const int got = kickDetector.process (inputs[kick], numSamples, on,
                                                  KickOnsetDetector::kMaxOnsets);
            for (int i = 0; i < got; ++i)
                tracker.notifyKickOnset (on[i].offset, on[i].strength);
            lastKickLevel.store (kickDetector.level(), std::memory_order_relaxed);
            lastKickQuiet.store (kickDetector.quietSeconds(), std::memory_order_relaxed);
        }
        else
        {
            kickDetector.reset();
            lastKickLevel.store (0.0f, std::memory_order_relaxed);
            lastKickQuiet.store (0.0f, std::memory_order_relaxed);
        }
        tracker.setKickChannelState (assigned, kickDetector.quietSeconds());
        lastKickChannel.store (assigned ? kick : -1, std::memory_order_relaxed);
    }

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

    // Kept before anything is done to it - see the note on `rawIn`.
    std::memcpy (rawIn.data(), mono.data(),
                 static_cast<size_t> (numSamples) * sizeof (float));

    const float trim = std::clamp (cfg.inputGain.load (std::memory_order_relaxed), 0.0f, 4.0f);
    if (std::fabs (trim - 1.0f) > 1.0e-6f)
    {
        for (int i = 0; i < numSamples; ++i)
            mono[static_cast<size_t> (i)] *= trim;
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

void VirtualPercussionEngine::updateLeakDelay (int numSamples, bool speaker) noexcept
{
    if (outRing.empty() || numSamples <= 0)
        return;

    const float latMs = std::max (8.0f, latencyMs.load (std::memory_order_relaxed));
    int center = static_cast<int> (latMs * 0.001 * sampleRate);
    center = std::clamp (center, 64, ringSize - numSamples - 1);

    // The reported figure is the device round trip. Through a mixer that is
    // the leak delay. Through the iPad's own speaker the acoustic path sits
    // on top of it - typically another 10-40 ms - so a canceller pinned to
    // the device number misses the congas and the tracker follows itself.
    const int pad = static_cast<int> ((speaker ? 0.080 : 0.040) * sampleRate);
    const int lo = std::max (64, speaker ? static_cast<int> (0.008 * sampleRate)
                                         : center - pad);
    const int hi = std::min (ringSize - numSamples - 1,
                             speaker ? std::max (center + pad,
                                                 static_cast<int> (0.080 * sampleRate))
                                     : center + pad);
    if (hi < lo)
        return;

    if (leakDelaySamples < lo || leakDelaySamples > hi)
    {
        leakDelaySamples = std::clamp (center, lo, hi);
        leakDelayLocked = false;
    }

    double xx = 0.0;
    for (int i = 0; i < numSamples; ++i)
        xx += static_cast<double> (mono[static_cast<size_t> (i)])
              * mono[static_cast<size_t> (i)];

    // Do not move a delay estimate while the input block is empty. At the true
    // delay its reference is empty too (scoreAt returns the sentinel), while a
    // wrong delay may happen to contain an old stroke and score zero. Zero used
    // to beat the sentinel, so silence actively pulled the search away from the
    // right acoustic path between percussion hits.
    if (xx < 1.0e-9)
        return;

    auto scoreAt = [this, numSamples, xx] (int d) noexcept -> double
    {
        double xy = 0.0, yy = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const int ri = (ringWrite - d + i + ringSize) & (ringSize - 1);
            const float y = outRing[static_cast<size_t> (ri)];
            xy += static_cast<double> (mono[static_cast<size_t> (i)]) * y;
            yy += static_cast<double> (y) * y;
        }
        return yy > 1.0e-12 ? xy / std::sqrt (xx * yy) : -1.0e9;
    };
    auto envelopeScoreAt = [this, numSamples, xx] (int d) noexcept -> double
    {
        double xy = 0.0, yy = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const int ri = (ringWrite - d + i + ringSize) & (ringSize - 1);
            const float y = outRing[static_cast<size_t> (ri)];
            xy += static_cast<double> (std::abs (mono[static_cast<size_t> (i)]))
                  * std::abs (y);
            yy += static_cast<double> (y) * y;
        }
        return yy > 1.0e-12 ? xy / std::sqrt (xx * yy) : -1.0e9;
    };

    // What it costs to give up a delay already held.
    //
    // The search re-runs from scratch every fourth block once locked, and each
    // run decides on *this block's* correlation alone - where the gain fit next
    // door accumulates half a second before it believes anything. On a sparse
    // part that is not symmetric: between two strokes the true delay's own
    // score collapses to noise, while the part's own periodicity leaves
    // coincidental envelope peaks at other lags across a ten-thousand-sample
    // window. Measured on the fractional-delay room fixture, the search left a
    // correct, locked 8417 for 4811 on one quiet block scoring 0.1971 against
    // the incumbent's 0.0739 - both meaningless, the wrong one merely larger -
    // and then spent 1.2 s wandering (4811, 9613, 7429, 5633, 8746, 7421) with
    // the accumulators dropped at every hop, because `delay != leakFitDelay`
    // fires on each one and the fit never gets to converge. That stretch is the
    // whole difference between the residual this fixture is supposed to have
    // and the one it had: 0.0954 against 0.2794, mean over the run, and 0.13
    // against 0.99 on the worst block.
    //
    // So the acquisition floor below is what it says - the floor for finding a
    // path from nothing. Leaving one already found needs a candidate that is
    // better by a margin, not one that is nominally larger on a block where
    // neither means anything.
    constexpr double kDelaySwitchMargin = 0.15;
    const double incumbentScore = scoreAt (leakDelaySamples);
    int best = leakDelaySamples;
    double bestScore = incumbentScore;

    if (--leakScanCountdown <= 0)
    {
        // Search every block until one actually contains enough of our return
        // to identify the path. Sparse percussion can put all of its attacks
        // between a fixed every-fourth-block scan; once acquired, the cheaper
        // cadence is sufficient to follow a moving acoustic path.
        leakScanCountdown = speaker ? (leakDelayLocked ? 4 : 1) : 8;
        const int step = std::max (16, numSamples / 8);
        int coarseBest = best;
        double coarseScore = envelopeScoreAt (coarseBest);
        for (int d = lo; d <= hi; d += step)
        {
            // Search on magnitude first. A drum's raw waveform correlation is
            // a needle only a few samples wide; a coarse step can jump over it.
            // Its energy envelope is broad enough to nominate the right area,
            // then the raw sample-by-sample refinement below finds the delay.
            const double s = envelopeScoreAt (d);
            if (s > coarseScore)
            {
                coarseScore = s;
                coarseBest = d;
            }
        }
        const int refineLo = std::max (lo, coarseBest - step);
        const int refineHi = std::min (hi, coarseBest + step);
        for (int d = refineLo; d <= refineHi; ++d)
        {
            const double s = scoreAt (d);
            if (s > bestScore)
            {
                bestScore = s;
                best = d;
            }
        }
    }
    else if (! speaker)
    {
        const int step = 16;
        for (int d : { leakDelaySamples - step, leakDelaySamples + step })
        {
            if (d < lo || d > hi)
                continue;
            const double s = scoreAt (d);
            if (s > bestScore)
            {
                bestScore = s;
                best = d;
            }
        }
    }

    // A room full of unrelated music always has a largest correlation in the
    // search window; "largest" alone does not make it our return path. Keep the
    // previous estimate until the candidate actually explains the input.
    if (leakDelayLocked && best != leakDelaySamples
        && bestScore < incumbentScore + kDelaySwitchMargin)
    {
        best = leakDelaySamples;
        bestScore = incumbentScore;
    }

    if (bestScore > 0.12)
    {
        leakDelaySamples = best;
        leakDelayLocked = true;
    }
}

void VirtualPercussionEngine::applyAnalysisHpf (int numSamples) noexcept
{
    // Analysis only. The output path is untouched. ~80 Hz takes rumble and
    // handling noise off the iPad mic without eating a kick's body.
    const float coef = 1.0f - std::exp (-2.0f * 3.14159265f * 80.0f
                                        / static_cast<float> (std::max (1.0, sampleRate)));
    for (int i = 0; i < numSamples; ++i)
    {
        const float x = mono[static_cast<size_t> (i)];
        analysisHp += coef * (x - analysisHp);
        mono[static_cast<size_t> (i)] = x - analysisHp;
    }
}

void VirtualPercussionEngine::subtractSpeakerLeak (int numSamples, bool speaker) noexcept
{
    if (outRing.empty() || numSamples <= 0)
        return;

    // Mixer return is the device round trip; searching around it can lock
    // onto a musical coincidence with the click and nibble the song. The
    // iPad mic needs the search: the acoustic hop sits on top of the
    // hardware figure, and a canceller glued to latencyMs misses the congas.
    int delay;
    if (speaker)
    {
        updateLeakDelay (numSamples, true);
        delay = leakDelaySamples > 0
                    ? leakDelaySamples
                    : static_cast<int> (std::max (8.0f, latencyMs.load (std::memory_order_relaxed))
                                        * 0.001 * sampleRate);
    }
    else
    {
        delay = static_cast<int> (std::max (8.0f, latencyMs.load (std::memory_order_relaxed))
                                  * 0.001 * sampleRate);
    }
    delay = std::clamp (delay, 64, ringSize - numSamples - 1);

    // The scratch is sized in prepare(). A fixed stack buffer used to cap this
    // at 2048 samples, which left the tail of a larger block un-subtracted: the
    // step it created at the splice point is a textbook onset, and it landed
    // in the analysis signal once per callback.
    const int n = std::min (numSamples, static_cast<int> (leakBand[0].size()));
    if (n <= 0)
        return;

    // Everything accumulated below was measured against the reference at one
    // particular delay. When the accepted delay moves - the acoustic search
    // found a better path, or the host revised the latency it reports - those
    // cross-products describe an alignment that no longer exists, so they go.
    // The gains themselves stay: the old estimate is still the best guess there
    // is until new evidence has arrived to replace it.
    if (delay != leakFitDelay)
    {
        leakFitDelay = delay;
        for (int b = 0; b < kLeakBands; ++b)
        {
            leakFitXy[b] = 0.0;
            for (int c = 0; c < kLeakBands; ++c)
                leakFitGram[b][c] = 0.0;
        }
    }

    // Split our own output into kLeakBands and fit a gain to each.
    //
    // One band was not enough. The reference used to be the top end alone -
    // which is the right model for the iPad's own speaker, because that speaker
    // has no low end to leak - so on a mixer, where the return carries the whole
    // part, the shaker was cancelled and the congas went into the analysis
    // untouched.
    //
    // Two were not enough either, and the second failure is the congas' own.
    // The split at 1.5 kHz puts the whole shaker above the line and the whole
    // conga below it, so the congas got exactly one number for everything from
    // DC to 1.5 kHz - and that is the range a small speaker reshapes hardest,
    // passing the body and dropping the fundamental. One gain cannot say "none
    // of this and all of that": it fits the compromise, under-subtracting the
    // body and over-subtracting the fundamental. Measured on the one-wall room
    // fixture at eighths, the share of our own return removed was 30.4% with
    // the shaker alone against 6.0% with the congas alone, and the congas'
    // worst block reached 1.84 of the input peak - more added than removed.
    // A third band at the speaker's own roll-off gives the fit somewhere to put
    // the zero. See `VPTests --leak`, rows `leak-voice`.
    //
    // Through a mixer, where the return does carry the low end, the two lower
    // bands simply fit near the same gain and nothing changes; the split costs
    // one one-pole per sample and does not have to be told which path it is on.
    double xy[kLeakBands] = {};
    double gram[kLeakBands][kLeakBands] = {};
    double xx = 0.0;
    const float lowCoef = static_cast<float> (
        1.0 - std::exp (-2.0 * 3.14159265358979 * 250.0 / sampleRate));
    for (int i = 0; i < n; ++i)
    {
        const int ri = (ringWrite - delay + i + ringSize) & (ringSize - 1);
        const float y = outRing[static_cast<size_t> (ri)];
        leakLpLow += lowCoef * (y - leakLpLow);
        leakLp += 0.18f * (y - leakLp);
        // Low, low-mid, high. The 0.18 coefficient is the original ~1.4 kHz
        // splitter, left where it was so the shaker's band is unchanged.
        const float band[kLeakBands] = { leakLpLow, leakLp - leakLpLow, y - leakLp };
        const float x = mono[static_cast<size_t> (i)];
        xx += static_cast<double> (x) * x;
        for (int b = 0; b < kLeakBands; ++b)
        {
            leakBand[b][static_cast<size_t> (i)] = band[b];
            xy[b] += static_cast<double> (x) * band[b];
            for (int c = b; c < kLeakBands; ++c)
                gram[b][c] += static_cast<double> (band[b]) * band[c];
        }
    }

    // Least squares over the bands together, not one each: one-pole splits do
    // not make them orthogonal, and fitting them independently has each one
    // claiming part of what the others explain.
    //
    // And over half a second of them, not over this block. The gain used to be
    // solved from a single block and the *answer* smoothed towards it, which
    // sounds equivalent and is not: a block whose reference happens to be
    // silent has no answer to give, so it took the degenerate branch below and
    // returned a hard zero - an absence of evidence, not a measurement - and
    // the smoother mixed that in as though it were one. Between strokes the
    // estimate therefore decayed towards zero, and the sparser the part the
    // less of it was cancelled: across the nine styles the residual went from
    // 0.07-0.18 at sixteenths to 0.29-0.46 at eighths, which is the shipped
    // default, and 0.62-0.74 at quarters. Accumulating the normal equations
    // instead lets a silent block contribute its honest zeros to both sides,
    // which moves the answer not at all, and the same fifty-four rows come out
    // under 0.0001 (0.0179 since the three-band ridge below).
    //
    // The forgetting factor is a length of time and not a number of callbacks.
    // kGainSmooth was per callback, so on a 4096-frame buffer it forgot sixteen
    // times faster in wall-clock terms than on 256 - the trap the phase
    // constants in TempoFollower were fixed for. Measured at eighths before:
    // 0.4792 at 256 frames against 0.2379 at 4096, the big buffer looking good
    // only because at 85 ms a block is never silent.
    constexpr double kLeakFitTauSec = 0.5;
    const double alpha = std::exp (-static_cast<double> (n)
                                   / std::max (1.0, kLeakFitTauSec * sampleRate));
    for (int b = 0; b < kLeakBands; ++b)
    {
        leakFitXy[b] = alpha * leakFitXy[b] + xy[b];
        for (int c = b; c < kLeakBands; ++c)
        {
            leakFitGram[b][c] = alpha * leakFitGram[b][c] + gram[b][c];
            leakFitGram[c][b] = leakFitGram[b][c];
        }
    }

    // Signed here, clamped only where it is used, which is the whole difference
    // between cancelling a leak and inventing one. Over a block of 256 samples
    // the fit between two unrelated signals is not zero, it is zero plus a few
    // per cent of noise; clamping that at zero first keeps only the positive
    // half and averages it to a standing positive gain, so the analysis had a
    // few per cent of the app's own part subtracted from it even on a feed that
    // carried none - which costs the tracker real onsets, and was seen to put it
    // badly out on a clean line feed with the part turned up.
    //
    // No evidence at all - the opening blocks, or the first block after the
    // delay moved - leaves the previous estimate where it is instead of
    // replacing it with a zero. The +-2 rail is only there to stop a
    // pathological fit; the useful range is enforced at the point of use.
    //
    // Solved by Cholesky with a ridge that is *relative* to the diagonal, not an
    // absolute floor: these are accumulated energies, so their size depends on
    // the level and on how long the window has been running, and a number that
    // means "singular" at one level means "fine" at another. Three bands off two
    // one-poles overlap a good deal, so a near-singular window is normal rather
    // than exceptional - the ridge is what keeps it from being answered with two
    // huge gains that cancel each other. If even that fails to factor, each band
    // is fitted on its own and a band with no evidence in it keeps the gain it
    // had.
    {
        // How hard the ridge leans. A middle band taken as the difference of two
        // one-poles carries far less energy than the two either side of it, so
        // it is the least determined of the three and the one a feed with no
        // leak on it can push around: measured on the no-leak bench through the
        // speaker path, an unridged three-band fit raised the worst audible
        // block to 1.111 of what arrived, against a bound of 1.10 and a two-band
        // 1.061. At 1% of the mean band energy that block is 1.092 and the same
        // bench's 128-frame row improves on two bands (1.013 against 1.037, and
        // 5.1% of mean absolute against 11.4%, which was over its own bound).
        // It is not free: a ridge biases every gain low by roughly its own size,
        // so the fifty-four exact-copy rows go from 0.0000 to 0.0179 - one order
        // of magnitude inside their 0.10 bound instead of three. That is the
        // right way round. 0.0179 of a digitally exact return is nothing the
        // tracker can hear, and the no-leak damage is our own subtraction
        // landing on a band that never leaked.
        constexpr double kLeakRidge = 1.0e-2;

        double trace = 0.0;
        for (int b = 0; b < kLeakBands; ++b)
            trace += leakFitGram[b][b];

        double a[kLeakBands][kLeakBands];
        for (int b = 0; b < kLeakBands; ++b)
            for (int c = 0; c < kLeakBands; ++c)
                a[b][c] = leakFitGram[b][c]
                          + (b == c ? kLeakRidge * trace / kLeakBands : 0.0);

        // Cholesky in place, then forward and back substitution.
        bool ok = trace > 1.0e-9;
        for (int b = 0; ok && b < kLeakBands; ++b)
        {
            for (int c = 0; c <= b; ++c)
            {
                double sum = a[b][c];
                for (int k = 0; k < c; ++k)
                    sum -= a[b][k] * a[c][k];
                if (c == b)
                {
                    if (sum <= 0.0)
                    {
                        ok = false;
                        break;
                    }
                    a[b][b] = std::sqrt (sum);
                }
                else
                {
                    a[b][c] = sum / a[c][c];
                }
            }
        }

        if (ok)
        {
            double z[kLeakBands];
            for (int b = 0; b < kLeakBands; ++b)
            {
                double sum = leakFitXy[b];
                for (int k = 0; k < b; ++k)
                    sum -= a[b][k] * z[k];
                z[b] = sum / a[b][b];
            }
            for (int b = kLeakBands - 1; b >= 0; --b)
            {
                double sum = z[b];
                for (int k = b + 1; k < kLeakBands; ++k)
                    sum -= a[k][b] * z[k];
                z[b] = sum / a[b][b];
            }
            for (int b = 0; b < kLeakBands; ++b)
                leakGain[b] = std::clamp (static_cast<float> (z[b]), -2.0f, 2.0f);
        }
        else
        {
            for (int b = 0; b < kLeakBands; ++b)
                if (leakFitGram[b][b] > 1.0e-9)
                    leakGain[b] = std::clamp (
                        static_cast<float> (leakFitXy[b] / leakFitGram[b][b]), -2.0f, 2.0f);
        }
    }

    const float maxG = speaker ? 0.98f : 0.95f;
    float use[kLeakBands];
    for (int b = 0; b < kLeakBands; ++b)
        use[b] = std::clamp (leakGain[b], 0.0f, maxG);

    // And only when our own output actually explains a share of what came in.
    // A leak is a large part of the input by definition; an accidental
    // resemblance between our shaker and the band's hi-hat is not. Below a few
    // per cent of the input's energy there is nothing here worth subtracting,
    // and subtracting it anyway costs the tracker real onsets.
    //
    // On the iPad mic the room music often dominates, so the share of *our*
    // part can sit under that floor even while the shaker is clearly audible.
    // Correlation against the delayed reference still names the leak, which
    // is enough to subtract without eating the song.
    double explained = 0.0, yy = 0.0;
    for (int b = 0; b < kLeakBands; ++b)
    {
        explained += static_cast<double> (use[b]) * xy[b];
        yy += gram[b][b];
    }
    const double corr = (xx > 1.0e-12 && yy > 1.0e-12)
                            ? explained / std::sqrt (xx * yy)
                            : 0.0;
    const float minShare = speaker ? 0.008f : 0.02f;
    if (xx < 1.0e-9)
        return;
    if (explained / xx < static_cast<double> (minShare) && ! (speaker && corr > 0.12))
        return;

    for (int i = 0; i < n; ++i)
    {
        float sub = 0.0f;
        for (int b = 0; b < kLeakBands; ++b)
            sub += use[b] * leakBand[b][static_cast<size_t> (i)];
        mono[static_cast<size_t> (i)] -= sub;
    }
}

void VirtualPercussionEngine::applyAnalysisMakeup (int numSamples, float rawPeak,
                                                   bool levelJumped) noexcept
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
    if (levelJumped || (peakEnv < kMakeupFloor && rawPeak >= kMakeupFloor))
        peakEnv = rawPeak;   // start at the level, do not crawl up to it
    else
        peakEnv += (rawPeak - peakEnv) * (rawPeak > peakEnv ? attack : release);

    float wanted = 1.0f;
    if (peakEnv < kMakeupTargetPeak)
    {
        if (peakEnv >= kMakeupFloor)
            wanted = std::clamp (kMakeupTargetPeak / peakEnv, 1.0f, kMakeupMaxGain);
    }
    else if (peakEnv > kMakeupClipGuardPeak)
        wanted = std::clamp (kMakeupClipGuardPeak / peakEnv, kMakeupMinGain, 1.0f);

    const float smooth = 1.0f - std::exp (-static_cast<float> (numSamples)
                                          / std::max (1.0f, static_cast<float> (sampleRate * kMakeupGlideSec)));
    const float from = makeupGain;
    // A level that has genuinely changed is not something to glide towards. The
    // envelope's attack is deliberately slow - eight tenths of a second, so that
    // no drum hit moves the network's operating point - and after an empty room
    // that is eight tenths of a second of music arriving at the network twenty
    // times too hot, which is exactly the window the level has to be right in.
    if (levelJumped)
        makeupGain = wanted;
    else
        makeupGain += (wanted - makeupGain) * smooth;
    // Was `from <= 1.0001f && makeupGain <= 1.0001f`, back when this stage only
    // ever boosted: unity was the one value meaning "nothing to do". Now it can
    // also attenuate, so "nothing to do" is unity in either direction, not "at
    // or below" it - that old test would have skipped every attenuating block.
    if (std::fabs (from - 1.0f) < 1.0e-4f && std::fabs (makeupGain - 1.0f) < 1.0e-4f)
        return;

    // Ramp within the block: a gain that steps between callbacks puts an edge
    // into the analysis signal, and the network hears edges.
    const float step = (makeupGain - from) / static_cast<float> (numSamples);
    for (int i = 0; i < numSamples; ++i)
        mono[static_cast<size_t> (i)] *= from + step * static_cast<float> (i);
}

bool VirtualPercussionEngine::updateRhythmShare (int numSamples) noexcept
{
    // Two one-poles at 200 Hz. A biquad would be tidier at the corner and this
    // does not need a tidy corner: what is being asked is whether the bottom of
    // the mix carries a comparable amount of energy to the rest of it, and the
    // answer moves by a factor of three across the entrance being looked for.
    const float coef = 1.0f - std::exp (-2.0f * 3.14159265f * static_cast<float> (kLowBandHz)
                                        / static_cast<float> (std::max (1.0, sampleRate)));
    double lowSum = 0.0, fullSum = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        const float x = mono[static_cast<size_t> (i)];
        lowLp1 += coef * (x - lowLp1);
        lowLp2 += coef * (lowLp1 - lowLp2);
        lowSum += static_cast<double> (lowLp2) * lowLp2;
        fullSum += static_cast<double> (x) * x;
    }
    const float inv = 1.0f / static_cast<float> (std::max (1, numSamples));
    const float smooth = 1.0f - std::exp (-static_cast<float> (numSamples)
                                          / std::max (1.0f, static_cast<float> (sampleRate * kShareSmoothSec)));
    const float lowNow = static_cast<float> (lowSum) * inv;
    const float fullNow = static_cast<float> (fullSum) * inv;
    if (fullEnergy <= 0.0f)
    {
        lowEnergy = lowNow;
        fullEnergy = fullNow;
    }
    else
    {
        lowEnergy += (lowNow - lowEnergy) * smooth;
        fullEnergy += (fullNow - fullEnergy) * smooth;
    }

    const float share = fullEnergy > 1.0e-12f ? lowEnergy / fullEnergy : 0.0f;
    lastLowShare.store (share, std::memory_order_relaxed);

    if (sharePrimeSamples < static_cast<int> (sampleRate * kSharePrimeSec))
    {
        // The energies are still filling. Follow, decide nothing.
        sharePrimeSamples += numSamples;
        shareBase = share;
        return false;
    }

    // Two ways to know there is a rhythm section, and they answer different
    // questions. The step is "one just walked in" and drives the epoch. This is
    // the standing fact "there is one now", which is what a track that was
    // already playing when START was pressed needs: it has no entrance to
    // detect, so a detector of entrances would leave it mute for ever.
    if (share > kShareFloor)
        shareHighSamples += numSamples;
    else
        shareHighSamples = 0;
    if (shareHighSamples > static_cast<int> (sampleRate * kShareHighHoldSec))
        rhythmSeen = true;

    const bool stepping = share > kShareFloor && share > shareBase * kShareStepUp;
    if (stepping)
        shareStepSamples += numSamples;
    else
        shareStepSamples = 0;

    if (shareStepSamples > static_cast<int> (sampleRate * kShareStepHoldSec))
    {
        shareStepSamples = 0;
        shareBase = share;
        rhythmSeen = true;
        return true;
    }

    const double towards = share < shareBase ? kShareFallSec : kShareRiseSec;
    shareBase += (share - shareBase) * (1.0f - std::exp (-static_cast<float> (numSamples)
                                                         / std::max (1.0f, static_cast<float> (sampleRate * towards))));
    return false;
}

bool VirtualPercussionEngine::updateAnalysisEpoch (int numSamples, float rawPeak,
                                                  bool rhythmArrived) noexcept
{
    // The make-up gain exists to hold the analysis at the one level the network
    // was validated at, which means that downstream of it an empty room and a
    // band playing arrive looking alike - by design, and measured: room noise
    // forty decibels down still reaches BeatNet amplified to the same peak, and
    // the network answers it with activations tall enough that the tempo
    // estimator names a level and calls it settled. This is the last place the
    // difference between the two still exists, so the moment has to be found
    // here and handed over.
    const float attack = 1.0f - std::exp (-static_cast<float> (numSamples)
                                          / std::max (1.0f, static_cast<float> (sampleRate * kLevelFastAttackSec)));
    const float release = 1.0f - std::exp (-static_cast<float> (numSamples)
                                           / std::max (1.0f, static_cast<float> (sampleRate * kLevelFastReleaseSec)));

    if (levelRef <= 0.0f)
    {
        // First audio. The envelope starts *at* the level rather than crawling
        // up to it, the same reason the make-up gain is primed rather than
        // released into.
        levelFast = rawPeak;
        levelRef = std::max (rawPeak, kMakeupFloor);
        levelLoud = levelRef;
        levelPrimeSamples = static_cast<int> (sampleRate * kLevelPrimeSec);
        return false;
    }

    levelFast += (rawPeak - levelFast) * (rawPeak > levelFast ? attack : release);

    if (levelPrimeSamples > 0)
    {
        // Still learning where the level is. Follow it up, decide nothing.
        levelPrimeSamples -= numSamples;
        levelRef = std::max (levelRef, levelFast);
        levelLoud = std::max (levelLoud, levelFast);
        return false;
    }

    // Our own part, on the same envelope. It reaches the microphone a little
    // after we play it and the canceller does not always find it - in mixer
    // mode the search does not cover an acoustic hop at all - so when the part
    // comes in, the level on the analysis bus can step up by more than this
    // looks for. That is us, not the room filling with a band.
    ownFast += (ownPeakLast - ownFast) * (ownPeakLast > ownFast ? attack : release);
    if (ownFast > std::max (ownRef, 1.0e-5f) * kLevelStepUp)
        ownStepSamples = static_cast<int> (sampleRate * kOwnStepBlameSec);
    else
        ownStepSamples = std::max (0, ownStepSamples - numSamples);
    ownRef = std::max (1.0e-6f, ownRef + (ownFast - ownRef) * release);

    if (ownStepSamples > 0)
    {
        // Blamed on us: veto this rise and forget any step in progress. That is
        // all this branch may do.
        //
        // It used to raise `levelRef` to the level it was vetoing as well, on
        // the theory that when the blame expired the part's own contribution
        // should not still be standing there looking like something that just
        // started. It does not need to: `wasQuiet` below already refuses a rise
        // that did not come out of a properly quiet room, and the part comes in
        // over an input that was not quiet. What the ratchet did instead was
        // move the reference permanently - it is only ever pushed *up* here, and
        // afterwards it can only decay by the four-second release - so the one
        // legitimate epoch of a session, the band starting, never cleared the
        // bar again while the part was audible.
        //
        // Measured with a 138 BPM record and no leak on the input at all: the
        // beat landed 2.96 ms differently with the master fader up than down,
        // the analysis chain differed on 7678 of 9750 blocks, a genuine
        // quiet-to-band step went completely unnoticed, and after twenty
        // seconds of empty room the app took 4.35 s longer to settle on the
        // tempo because the decoder was never told to drop the room's evidence.
        // See .superpowers/sdd/makeup-phase-root-cause.md. Afterwards: 0.02 ms,
        // zero differing blocks of 9750, the step called at 1.56 s, and the
        // veto's own cases - our 0.6 return over a steady band, the part
        // released over a quiet room - still at zero false restarts.
        //
        // The return is early, so while the blame stands `levelLoud`, the
        // "was properly quiet" test and the downward decay of `levelRef` are
        // skipped for the block rather than run. That is what the second of
        // lateness costs: a legitimate start landing inside the window is
        // called at 1.56 s with the part audible against 0.557 s with it
        // muted. It costs nothing on the twenty-second pre-roll, which settles
        // at 10.41 s either way.
        levelStepSamples = 0;
        return false;
    }

    const float loudDecay = 1.0f - std::exp (-static_cast<float> (numSamples)
                                             / std::max (1.0f, static_cast<float> (sampleRate * kLoudMemorySec)));
    levelLoud = std::max (levelFast, levelLoud + (levelFast - levelLoud) * loudDecay);

    // A rhythm section arriving on top of an intro that never had one. The two
    // conditions below cannot see it and must not be loosened until they can:
    // the intro is music, so the room was never quiet, and the entrance is a
    // change of content rather than of level. Invalidate the intro's grid and
    // re-prime make-up, but retain continuous network/comb evidence: on BLUE
    // SKY the comb already sees the band by the time this event is called.
    if (rhythmArrived)
    {
        preserveCombOnEpoch = true;
        levelRef = std::max (levelFast, kMakeupFloor);
        levelStepSamples = 0;
        analysisEpoch.fetch_add (1, std::memory_order_relaxed);
        return true;
    }

    // Two conditions, and both are needed.
    //
    // Upwards only, because a level that falls is a song ending, a break, a
    // quiet verse, and none of those is a reason to throw away what has been
    // measured: the evidence collected while it is quiet is the room's, and the
    // next rise discards it anyway.
    //
    // And out of a level that was properly quiet, not merely quieter. A rise on
    // its own cannot tell a band starting from a chorus arriving or a breakdown
    // ending, and the second and third are frequent and expensive.
    const bool wasQuiet = levelRef < levelLoud * kQuietFraction;
    if (wasQuiet && levelFast > levelRef * kLevelStepUp)
        levelStepSamples += numSamples;
    else
        levelStepSamples = 0;

    if (levelStepSamples > static_cast<int> (sampleRate * kLevelStepHoldSec))
    {
        preserveCombOnEpoch = false;
        levelRef = std::max (levelFast, kMakeupFloor);
        levelStepSamples = 0;
        analysisEpoch.fetch_add (1, std::memory_order_relaxed);
        return true;
    }

    if (levelFast < levelRef)
    {
        // The reference follows the level down, so it stays "where this input
        // has been sitting" rather than the loudest thing ever heard. A running
        // maximum would leave the bar too high for the next start to clear.
        levelRef = std::max (kMakeupFloor, levelRef + (levelFast - levelRef) * release);
    }
    return false;
}

void VirtualPercussionEngine::maybeDetectBarReentry (int numSamples, float rawPeak) noexcept
{
    // The epoch watcher needs ~4 s of quiet (levelRef decaying 24 dB) before
    // it will call a new input. A two-quarter cut is a second, so it never
    // fires, and must not: restarting the decoder is exactly what would
    // lose the tempo through the hole. This looks at the block peak against
    // the recent loud level instead, which a mute clears in one callback
    // and a fill never does.
    const auto st = static_cast<TrackingState> (lastState.load (std::memory_order_relaxed));
    const bool following = st == TrackingState::following
                        || st == TrackingState::lowConfidence
                        || st == TrackingState::recovering;
    if (! following || levelLoud < 0.02f)
    {
        musicGapSamples = 0;
        musicGapArmed = false;
        return;
    }

    const float bpm = lastBpm.load (std::memory_order_relaxed);
    const float bpmForGap = bpm > 40.0f ? bpm : 120.0f;
    const int twoQuarters = static_cast<int> (sampleRate * 2.0 * 60.0
                                              / static_cast<double> (bpmForGap));
    const bool quietBlock = rawPeak < std::max (levelLoud * 0.08f, 1.0e-4f);
    const bool loudBlock = rawPeak > std::max (levelLoud * 0.12f, 0.02f);

    if (quietBlock)
    {
        musicGapSamples += numSamples;
        if (musicGapSamples >= twoQuarters)
            musicGapArmed = true;
        return;
    }

    if (loudBlock && musicGapArmed)
        tracker.notifyBarReentry();

    musicGapSamples = 0;
    musicGapArmed = false;
}

void VirtualPercussionEngine::pushOutputToRing (int numSamples, float master) noexcept
{
    // The part as the speaker emits it, not as it was rendered.
    //
    // This used to store the mix from before the master fader while the outputs
    // got it after - so the canceller's reference was a signal that is never in
    // the room. A fader that does not move is absorbed by the gain the
    // canceller fits, which is why it survived this long; a fader that *moves*
    // is not, and every touch of the volume left the fit wrong until it
    // re-converged, with our own part in the analysis meanwhile.
    //
    // The monitor click is deliberately *not* in here. CLICK TEST adds it to
    // the analysis on purpose - that is the whole feature, a kit for the
    // tracker to lock to when there is no drummer - so a canceller that
    // subtracted it again would be undoing the one thing it is for.
    float own = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float y = master * 0.5f * (outL[static_cast<size_t> (i)]
                                   + outR[static_cast<size_t> (i)]);
        own = std::max (own, std::abs (y));
        if (! outRing.empty())
        {
            outRing[static_cast<size_t> (ringWrite)] = y;
            ringWrite = (ringWrite + 1) & (ringSize - 1);
        }
    }
    // For the level watcher next block: it runs before the part is rendered, so
    // the newest own-level it can have is the previous one - which is the right
    // one anyway, because that is the part that has had time to come back round.
    ownPeakLast = own;
}

void VirtualPercussionEngine::process (const float* const* inputs, int numInputs,
                                       float* const* outputs, int numOutputs,
                                       int numSamples) noexcept
{
    // Denormals. Every filter in here has a tail that decays towards zero - the
    // leak canceller, the level envelope, the reverb - and a float that falls
    // into the denormal range costs a hundred times what a normal one does on
    // some cores. Silence after a loud passage is exactly when that happens,
    // and a callback that overruns its budget is a dropout.
    const juce::ScopedNoDenormals noDenormals;

    if (numSamples <= 0)
        return;

    // A host is entitled to hand over a longer block than it announced - a
    // screen lock, a route change, an AirPlay hop. Split it rather than
    // truncating: the tail of a truncated block is left holding whatever the
    // host had in the buffer.
    int offset = 0;
    while (offset < numSamples)
    {
        const int chunk = std::min (maxBlock, numSamples - offset);
        const float* inPtrs[kMaxSplitChannels];
        float* outPtrs[kMaxSplitChannels];
        const float* const* in = inputs;
        float* const* out = outputs;

        if (offset > 0 || numSamples > maxBlock)
        {
            const int nIn = std::min (numInputs, kMaxSplitChannels);
            const int nOut = std::min (numOutputs, kMaxSplitChannels);
            for (int c = 0; c < nIn; ++c)
                inPtrs[c] = (inputs != nullptr && inputs[c] != nullptr) ? inputs[c] + offset : nullptr;
            for (int c = 0; c < nOut; ++c)
                outPtrs[c] = (outputs != nullptr && outputs[c] != nullptr) ? outputs[c] + offset : nullptr;
            in = inputs != nullptr ? inPtrs : nullptr;
            out = outputs != nullptr ? outPtrs : nullptr;
            processBlock (in, nIn, out, nOut, chunk);

            // More channels than the split path carries: silence the rest
            // rather than leave the host's buffer as it was.
            if (outputs != nullptr)
                for (int c = nOut; c < numOutputs; ++c)
                    if (outputs[c] != nullptr)
                        std::fill (outputs[c] + offset, outputs[c] + offset + chunk, 0.0f);
        }
        else
        {
            processBlock (in, numInputs, out, numOutputs, chunk);
        }
        offset += chunk;
    }
}

void VirtualPercussionEngine::processBlock (const float* const* inputs, int numInputs,
                                            float* const* outputs, int numOutputs,
                                            int numSamples) noexcept
{
    const auto t0 = std::chrono::steady_clock::now();

    lastBuffer.store (numSamples, std::memory_order_relaxed);

    tracker.setFollowStrength (static_cast<FollowStrength> (cfg.followStrength.load (std::memory_order_relaxed)));
    tracker.setSubdivisionOverride (static_cast<Subdivision> (cfg.subdivision.load (std::memory_order_relaxed)));
    // AUTO unless the player has said otherwise. This came back with the ÷2/×2
    // controls (TODO item 15, reopened 2026-09-04): there is a class of
    // material - a straight groove at 50 against a half-time one at 100 - that
    // is the *same sound* at two metrical levels, so no automatic path can
    // decide it and the player has to be able to. See
    // docs/HANDOFF_OCTAVE_50BPM.md.
    tracker.setTempoOctaveAuto (cfg.tempoOctaveAuto.load (std::memory_order_relaxed));
    tracker.setTempoOctave (cfg.tempoOctave.load (std::memory_order_relaxed));
    {
        const bool follow = cfg.tempoFollow.load (std::memory_order_relaxed);
        tracker.setTempoFollow (follow);
        if (! follow)
            tracker.setUserTempo (cfg.userBpm.load (std::memory_order_relaxed),
                                  cfg.userBpmGen.load (std::memory_order_relaxed));
    }
    {
        const int nudge = cfg.barNudge.load (std::memory_order_relaxed);
        if (nudge != seenBarNudge)
        {
            tracker.nudgeBar (nudge - seenBarNudge);
            seenBarNudge = nudge;
        }
        // Read before the write-back below, and honoured in both directions.
        // The setting is a request from the screen *and* a read-out of what the
        // tracker decided - a tap locks the bar without this button being
        // touched - so the two have to be reconciled here, once, with the read
        // first. Written back one-way it was the tracker's answer overwriting
        // the listener's request before the request was ever looked at, and the
        // lock could not be set at all.
        const bool wantLocked = cfg.barLocked.load (std::memory_order_relaxed);
        if (wantLocked != tracker.barIsLocked())
            tracker.setBarLocked (wantLocked);
    }
    const auto source = static_cast<FollowSource> (
        cfg.followSource.load (std::memory_order_relaxed));
    const bool speaker = source == FollowSource::speaker;
    const bool directFile = source == FollowSource::internalPlayer;
    tracker.setSpeakerFollow (speaker);
    // What the clock has to run ahead of the music by, so that what is *heard*
    // lands on the pulse: the device round trip, plus the slowest attack in the
    // percussion bank. The second term is not a device property but it is the
    // same kind of delay - time between the decision and the sound - and this
    // is the one place that knows both.
    // A measured round trip beats a reported one. The device's figure is what
    // the operating system believes about the interface; the measurement is
    // what this rig actually did, desk and all. See Audio/LatencyProbe.h.
    const float measured = measuredLatencyMs.load (std::memory_order_relaxed);
    const float roundTrip = directFile ? 0.0f
                                       : (measured > 0.0f ? measured
                                                          : latencyMs.load (std::memory_order_relaxed));
    tracker.setReportedLatencyMs (roundTrip + percussion.attackLeadMs());
    percussion.setHumanization (cfg.humanization.load (std::memory_order_relaxed));
    percussion.setShakerVolume (cfg.shakerVolume.load (std::memory_order_relaxed));
    percussion.setCongaVolume (cfg.congaVolume.load (std::memory_order_relaxed));
    percussion.setClapVolume (cfg.clapVolume.load (std::memory_order_relaxed));
    percussion.setCembaloVolume (cfg.cembaloVolume.load (std::memory_order_relaxed));
    percussion.setReverbAmount (cfg.reverbAmount.load (std::memory_order_relaxed));
    // The four voices switch independently. `setEnabled` is the master gate,
    // so it may only come off once all four are off - otherwise turning the
    // shaker off would take the rest with it.
    const bool shakerOn = cfg.shakerEnabled.load (std::memory_order_relaxed);
    const bool congasOn = cfg.congasEnabled.load (std::memory_order_relaxed);
    const bool cembaloOn = cfg.cembaloEnabled.load (std::memory_order_relaxed);
    const bool clapOn = cfg.clapEnabled.load (std::memory_order_relaxed);
    percussion.setShakerEnabled (shakerOn);
    percussion.setCongasEnabled (congasOn);
    percussion.setCembaloEnabled (cembaloOn);
    percussion.setClapEnabled (clapOn);
    percussion.setEnabled (shakerOn || congasOn || cembaloOn || clapOn);
    percussion.setShakerNatural (cfg.shakerNatural.load (std::memory_order_relaxed));
    percussion.setSwing (cfg.swing.load (std::memory_order_relaxed));
    percussion.setIntensity (cfg.intensity.load (std::memory_order_relaxed));
    // The manual setting is the override; on auto the music decides.

    mixInputs (inputs, numInputs, numSamples);

    // Whatever the microphone hands over, it stops being able to hurt anything
    // here. One infinity reaching the level envelope poisons it for the rest of
    // the session - the envelope has a four second release, and inf minus inf
    // is a NaN it never leaves - and with it the gain the network is fed. Note
    // that a peak taken with std::max hides this rather than catching it:
    // max(x, NaN) is x, so the bad sample passes straight through into the
    // analysis while every meter still reads normal.
    float peak = 0.0f;
    int bad = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        float x = mono[static_cast<size_t> (i)];
        if (! std::isfinite (x))
        {
            x = 0.0f;
            mono[static_cast<size_t> (i)] = 0.0f;
            ++bad;
        }
        const float a = std::abs (x);
        if (a > peak)
            peak = a;
    }
    if (bad > 0)
        badInputSamples.fetch_add (bad, std::memory_order_relaxed);
    // Both modes. It used to run only under SPEAKER, on the reasoning that the
    // leak is the iPad's own speaker into its own microphone - but a mixer
    // hands the app its output back on the return, which is the same signal
    // with a shorter path and none of the room in front of it. Measured on a
    // 120 BPM song with the app's output returning at half level: the tempo
    // error went from 0.12 BPM with the part silent to 2.10 BPM with it up, and
    // walked 1.2 BPM over the take, because what the tracker was following was
    // its own shaker. It is adaptive and self-gating - the gain it fits is near
    // zero when there is nothing of ours on the input - so running it on a feed
    // that has no leak costs nothing.
    if (! directFile && leakCancellationEnabledForTest)
        subtractSpeakerLeak (numSamples, speaker);
    maybeInjectClick (numSamples);
    // Mic rumble only. A mixer aux and the click tests carry kick body around
    // 50-60 Hz; an 80 Hz HPF on those feeds thins the very pulse BeatNet
    // was trained on.
    if (speaker)
        applyAnalysisHpf (numSamples);

    // The level the make-up gain works from is the level of the signal it is
    // about to be applied to, which is not the level that arrived. `peak` above
    // is the input including whatever of our own part came back on it, and that
    // part has just been taken out - so driving the gain from it holds the
    // analysis below where the network expects it by exactly the amount the
    // subtraction removed. Measured on a returned feed with the part at full
    // volume, the analysis sat at 0.045 against 0.076 with the part silent: the
    // same band, quieter, for no reason the network can know about.
    float postPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        postPeak = std::max (postPeak, std::abs (mono[static_cast<size_t> (i)]));
    lastLeakRemain.store (postPeak, std::memory_order_relaxed);
    // INPUT is an analysis trim, not a claim that the room or the song changed.
    // The epoch watcher used to see the trimmed level, so moving INPUT upward
    // could look exactly like a new band entering and reset the decoder while
    // it was already following. Undo the trim for every decision about the
    // physical source; keep `postPeak` for make-up, because that stage really
    // does need the level BeatNet is about to receive. Leak subtraction is
    // linear in the input gain, so its residual scales by the same factor.
    const float inputTrim = std::clamp (
        cfg.inputGain.load (std::memory_order_relaxed), 0.0f, 4.0f);
    const float sourcePeak = inputTrim > 1.0e-6f ? postPeak / inputTrim : 0.0f;
    // This is the last honest amplitude in the path. The make-up immediately
    // below deliberately raises a quiet room to BeatNet's operating level, so
    // asking the tracker whether the source is real after that point makes a
    // room look just as loud as a record. Used only to let START join music
    // that was already playing; the normal quiet-to-loud epoch remains the
    // primary start detector.
    // A microphone in a room can produce peaks around 0.03 even with nobody
    // playing (measured by the room-start regression). A direct/mixer source
    // therefore needs the higher line-level threshold; the iPad acoustic path
    // needs the lower one because its real music is much quieter at the mic.
    // And a level alone is not enough to say it. Measured on the reference song
    // (item 29): its intro is music at a perfectly ordinary level, so this test
    // passed at four seconds, the decoder had a periodic grid over a guitar
    // figure, and the part came in on a tempo the song does not have and stayed
    // on it for a minute. What was missing was not loudness but a rhythm
    // section. `rhythmSeen` is the second fact, and it is only asked for here:
    // a source that starts from quiet still enters through its epoch, which is
    // the ordinary path and is not gated by this.
    tracker.setSourceAudible (sourcePeak > (speaker ? 0.004f : 0.040f) && rhythmSeen);
    // How much the band is giving. Taken here on purpose: our own part has
    // just been subtracted, so the dynamics cannot follow themselves, and the
    // make-up gain below - which exists to hold the network's operating point
    // and therefore flattens exactly this - has not been applied yet.
    bandDynamics.observe (sourcePeak, numSamples);

    // Before the make-up, and before the level watcher: the share the bottom of
    // the mix is carrying is the one thing measured on real material that tells
    // an intro without a rhythm section from the band coming in. See
    // updateRhythmShare.
    const bool rhythmArrived = updateRhythmShare (numSamples);
    const bool levelJumped = updateAnalysisEpoch (numSamples, sourcePeak, rhythmArrived);
    if (barReentryPending.exchange (false, std::memory_order_relaxed))
        tracker.notifyBarReentry();
    else if (! levelJumped)
        maybeDetectBarReentry (numSamples, sourcePeak);
    applyAnalysisMakeup (numSamples, postPeak, levelJumped);
    float analysisPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        analysisPeak = std::max (analysisPeak, std::abs (mono[static_cast<size_t> (i)]));

    const unsigned int tw = tapWrite.load (std::memory_order_acquire);
    // More taps than the queue holds arrived since the last callback: the oldest
    // are gone, and reading them anyway means reading slots that now hold new
    // taps, out of order. Skip to what is still there.
    if (tw - tapRead > static_cast<unsigned int> (tapQSize))
        tapRead = tw - static_cast<unsigned int> (tapQSize);
    while (tapRead != tw)
    {
        tracker.tap (tapTimes[tapRead % static_cast<unsigned int> (tapQSize)]);
        ++tapRead;
    }

    // Where the chords moved. On the analysis bus for the same reason the
    // dynamics are: our own part has been taken out of it, and a shaker is not
    // a chord change but a conga's ring is close enough to one to be worth not
    // handing to a chroma.
    {
        HarmonicChange::Change ch[HarmonicChange::kMaxChanges];
        const int got = harmony.process (mono.data(), numSamples, ch,
                                         HarmonicChange::kMaxChanges);
        for (int i = 0; i < got; ++i)
            tracker.notifyHarmonicChange (ch[i].offset, ch[i].strength);
        tracker.setHarmonicShare (harmony.tonalShare());
        lastHarmonicShare.store (harmony.tonalShare(), std::memory_order_relaxed);
    }

    tracker.setInputEpoch (analysisEpoch.load (std::memory_order_relaxed), preserveCombOnEpoch);
    const auto tr = tracker.process (mono.data(), numSamples);

    percussion.setBarTrusted (tr.barTrusted);
    if (! cfg.tempoFollow.load (std::memory_order_relaxed) && tr.bpm > 50.0f)
        cfg.userBpm.store (tr.bpm, std::memory_order_relaxed);
    // The clock's own tempo, not the BPM on the display. `tr.bpm` is blank
    // until the tracker has locked and reads 0 before then, so the percussion
    // was being told 120 while the clock ran at whatever it had actually found.
    percussion.setGroove (tr.clock.tempoBpm > 40.0f ? tr.clock.tempoBpm
                                                    : (tr.bpm > 40.0f ? tr.bpm : 120.0f),
                          tr.clockPulsesPerBeat);
    percussion.setSubdivision (tr.subdivision);

    // Fold the analysis signal onto the bar to see which part the music is
    // asking for. Only while the clock is actually on the song: folding audio
    // onto a bar the tracker has not found yet just smears every bin.
    // Folded only for the automatic style chooser. The dynamics wanted it too -
    // how full the bar is separates a quiet band from an exposed voice - and it
    // turned out they cannot have it: this runs on the analysis bus, and the
    // make-up gain on that bus exists to erase exactly the difference the
    // dynamics were trying to read. See Percussion/BandDynamics.h.
    const bool autoStyle = cfg.grooveAuto.load (std::memory_order_relaxed);
    const bool wantDynamics = cfg.dynamicsFollow.load (std::memory_order_relaxed);
    if (autoStyle || wantDynamics)
    {
        const bool clockStable = tr.state == TrackingState::following
                                 && tr.clock.tempoBpm > 40.0f;
        styleDetector.process (mono.data(), numSamples, tr.barPhase, clockStable);
    }
    const GrooveStyle chosen = autoStyle
                                   ? styleDetector.style()
                                   : static_cast<GrooveStyle> (cfg.grooveStyle.load (std::memory_order_relaxed));
    percussion.setGrooveStyle (chosen);

    // What the band is giving, and whether the passage wants anything at all.
    //
    // The stand-down is deferred to a bar line. A player who stops in the
    // middle of a figure has not made a musical decision, they have dropped
    // something - and the same going the other way: coming back in on the "a"
    // of three is not an entrance. `wrappedBar` is the clock saying the count
    // has just come round, which is the only moment either is worth doing.
    const bool followDynamics = wantDynamics;
    bool sectionJustChanged = false;
    percussion.setDynamics (followDynamics ? bandDynamics.level() : 1.0f);
    if (! followDynamics)
    {
        standingDown = false;
        wantStandDown = false;
    }
    else
    {
        wantStandDown = bandDynamics.wantsSilence();
        if (tr.clock.wrappedBar || ! tr.percussionShouldPlay)
            standingDown = wantStandDown;

        // And the form. A band's fills land on the bar before the section
        // changes; the app's eight-bar sentence was counted from wherever the
        // part happened to come in, so on average its fill missed the band's by
        // four bars. Sampling the level once a bar is enough to find a section
        // boundary - a verse does not become a chorus quietly - and starting
        // the sentence there puts the fill where the band puts theirs.
        if (tr.clock.wrappedBar && bandDynamics.sectionChangedAtBar())
        {
            percussion.alignPhrase();
            ++sectionCount;
            sectionJustChanged = true;
        }
    }


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
#if defined(VP_ENABLE_RECORDED_LOOPS) && VP_ENABLE_RECORDED_LOOPS
        // The recorded percussionist. With the flag off - which is the default -
        // this whole branch is not compiled and the call below is the only thing
        // that renders the part, exactly as it always was. See
        // docs/RECORDED_LOOPS.md and TD-16.
        HybridPercussionRenderer::Input hin;
        hin.tick = tr.clock;
        hin.regime = tr.regime;
        hin.audible = tr.percussionShouldPlay && ! standingDown;
        hin.bpm = tr.clock.tempoBpm > 40.0f ? tr.clock.tempoBpm
                                            : (tr.bpm > 40.0f ? tr.bpm : 120.0f);
        hin.style = chosen;
        hin.swing = cfg.swing.load (std::memory_order_relaxed);
        hin.intensity = cfg.intensity.load (std::memory_order_relaxed);
        hin.dynamics = followDynamics ? bandDynamics.level() : 1.0f;
        hin.congasEnabled = cfg.congasEnabled.load (std::memory_order_relaxed);
        hin.shakerEnabled = cfg.shakerEnabled.load (std::memory_order_relaxed);
        hin.shakerVolume = cfg.shakerVolume.load (std::memory_order_relaxed);
        hin.congaVolume = cfg.congaVolume.load (std::memory_order_relaxed);
        hin.sectionChanged = sectionJustChanged;
        hybrid.render (percussion, outL.data(), outR.data(), numSamples, hin);
#else
        percussion.render (outL.data(), outR.data(), numSamples, tr.clock,
                           tr.percussionShouldPlay && ! standingDown);
#endif
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

    // The sweep goes out after the master gain and is deliberately not pushed
    // to the leak reference below: it is not part of the part, and the
    // canceller has no business trying to remove it from anything.
    if (outputs != nullptr && numOutputs > 0)
        latencyProbe.process (rawIn.data(), outputs[0],
                              numOutputs > 1 ? outputs[1] : nullptr, numSamples);

    pushOutputToRing (numSamples, master);

    lastBpm.store (tr.bpm, std::memory_order_relaxed);
    lastTarget.store (tr.targetBpm, std::memory_order_relaxed);
    lastClockBpm.store (tr.clock.tempoBpm, std::memory_order_relaxed);
    lastConf.store (tr.confidence, std::memory_order_relaxed);
    lastBeat.store (tr.beatPhase, std::memory_order_relaxed);
    lastBar.store (tr.barPhase, std::memory_order_relaxed);
    lastBarDeclared.store (tr.barDeclared, std::memory_order_relaxed);
    // Straight back into the setting, because a tap locks the bar too and the
    // control on screen has to light up for that as well as for its own press.
    lastBarLocked.store (tr.barLocked, std::memory_order_relaxed);
    cfg.barLocked.store (tr.barLocked, std::memory_order_relaxed);
    lastGaps.store (static_cast<int> (tr.analysisGaps), std::memory_order_relaxed);
    lastBarRotations.store (tr.barRotations, std::memory_order_relaxed);
    lastBarTrusted.store (tr.barTrusted, std::memory_order_relaxed);
    lastBarReentry.store (tr.barReentry, std::memory_order_relaxed);
    lastKickOnsets.store (tr.kickOnsets, std::memory_order_relaxed);
    lastKickTrusted.store (tr.kickTrusted, std::memory_order_relaxed);
    lastDrumsOut.store (tr.drumsOut, std::memory_order_relaxed);
    lastHarmonicChanges.store (tr.harmonicChanges, std::memory_order_relaxed);
    lastBarFromHarmony.store (tr.barFromHarmony, std::memory_order_relaxed);
    lastHarmonyMargin.store (tr.harmonyMargin, std::memory_order_relaxed);
    lastDynamics.store (followDynamics ? bandDynamics.level() : 1.0f,
                        std::memory_order_relaxed);
    lastStandingDown.store (standingDown, std::memory_order_relaxed);
    lastSections.store (sectionCount, std::memory_order_relaxed);
    lastPhraseBar.store (percussion.phraseBar(), std::memory_order_relaxed);
    lastEvidenceTrust.store (tr.evidenceTrust, std::memory_order_relaxed);
    lastGridTauSec.store (tr.gridTauSec, std::memory_order_relaxed);
    lastRestarts.store (analysisEpoch.load (std::memory_order_relaxed),
                        std::memory_order_relaxed);
    lastBacklog.store (tr.analysisBacklog, std::memory_order_relaxed);
    lastPeak.store (peak, std::memory_order_relaxed);
    lastAnalysisPeak.store (analysisPeak, std::memory_order_relaxed);
    lastAnalysisGain.store (makeupGain, std::memory_order_relaxed);
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
    lastOctave.store (tr.tempoOctave, std::memory_order_relaxed);
    lastCombBpm.store (tr.combBpm, std::memory_order_relaxed);
    lastLevelSettled.store (tr.levelSettled, std::memory_order_relaxed);
    lastFitResidual.store (tr.fitResidual, std::memory_order_relaxed);
    lastFitCoverage.store (tr.fitCoverage, std::memory_order_relaxed);
    lastShortFitBpm.store (tr.shortFitBpm, std::memory_order_relaxed);
    lastLongFitBpm.store (tr.longFitBpm, std::memory_order_relaxed);
    lastShortFitResidual.store (tr.shortFitResidual, std::memory_order_relaxed);
    lastTempoTransitionState.store (static_cast<int> (tr.tempoTransitionState),
                                    std::memory_order_relaxed);
    lastTempoTransitionReason.store (static_cast<int> (tr.tempoTransitionReason),
                                     std::memory_order_relaxed);
    lastTempoTransitionBpm.store (tr.tempoTransitionBpm, std::memory_order_relaxed);
    lastTempoTransitionConfidence.store (tr.tempoTransitionConfidence,
                                         std::memory_order_relaxed);
    lastTempoTransitionIntervals.store (tr.tempoTransitionIntervals,
                                        std::memory_order_relaxed);
    lastStyle.store (static_cast<int> (chosen), std::memory_order_relaxed);
    lastLoopPlaying.store (hybrid.loopIsPlaying(), std::memory_order_relaxed);
    lastLoopPhaseMs.store (hybrid.player().phaseErrorMs(), std::memory_order_relaxed);
    lastHandovers.store (hybrid.handovers(), std::memory_order_relaxed);
    lastStyleConf.store (styleDetector.confidence(), std::memory_order_relaxed);
    // Everything the UI shows is published from here. Reading it off the
    // objects instead - as the style features and the hit count were - is the
    // message thread reading five floats while this thread writes them, which
    // is a race whose visible form is a meter showing a number that was never
    // true at any single moment.
    {
        const auto f = styleDetector.features();
        lastStyleEvenKick.store (f.evenKick, std::memory_order_relaxed);
        lastStyleBackbeat.store (f.alternation, std::memory_order_relaxed);
        lastStyleOffHigh.store (f.offHigh, std::memory_order_relaxed);
        lastStyleSync.store (f.syncopation, std::memory_order_relaxed);
        lastStyleOccupancy.store (f.occupancy, std::memory_order_relaxed);
    }
    lastHits.store (percussion.hitsFired(), std::memory_order_relaxed);
    lastAttackLeadMs.store (percussion.attackLeadMs(), std::memory_order_relaxed);

    const auto t1 = std::chrono::steady_clock::now();
    const float ms = std::chrono::duration<float, std::milli> (t1 - t0).count();
    lastCallbackMs.store (ms, std::memory_order_relaxed);
}

float VirtualPercussionEngine::finishLatencyMeasurement() noexcept
{
    const float ms = latencyProbe.analyse();
    if (ms > 0.0f)
        measuredLatencyMs.store (ms, std::memory_order_relaxed);
    return ms;
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
    s.clockBpm = lastClockBpm.load (std::memory_order_relaxed);
    s.confidence = lastConf.load (std::memory_order_relaxed);
    s.beatPhase = lastBeat.load (std::memory_order_relaxed);
    s.barPhase = lastBar.load (std::memory_order_relaxed);
    s.barDeclared = lastBarDeclared.load (std::memory_order_relaxed);
    s.barLocked = lastBarLocked.load (std::memory_order_relaxed);
    s.barRotations = lastBarRotations.load (std::memory_order_relaxed);
    s.barTrusted = lastBarTrusted.load (std::memory_order_relaxed);
    s.barReentry = lastBarReentry.load (std::memory_order_relaxed);
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
    s.analysisGain = lastAnalysisGain.load (std::memory_order_relaxed);
    s.inputGain = cfg.inputGain.load (std::memory_order_relaxed);
    s.leakRemain = lastLeakRemain.load (std::memory_order_relaxed);
    s.lowShare = lastLowShare.load (std::memory_order_relaxed);
    s.badInputSamples = badInputSamples.load (std::memory_order_relaxed);
    s.analysisGaps = lastGaps.load (std::memory_order_relaxed);
    s.kickChannel = lastKickChannel.load (std::memory_order_relaxed);
    s.kickLevel = lastKickLevel.load (std::memory_order_relaxed);
    s.kickQuietSec = lastKickQuiet.load (std::memory_order_relaxed);
    s.kickOnsets = lastKickOnsets.load (std::memory_order_relaxed);
    s.kickTrusted = lastKickTrusted.load (std::memory_order_relaxed);
    s.drumsOut = lastDrumsOut.load (std::memory_order_relaxed);
    s.harmonicChanges = lastHarmonicChanges.load (std::memory_order_relaxed);
    s.barFromHarmony = lastBarFromHarmony.load (std::memory_order_relaxed);
    s.harmonyMargin = lastHarmonyMargin.load (std::memory_order_relaxed);
    s.harmonicShare = lastHarmonicShare.load (std::memory_order_relaxed);
    s.bandDynamics = lastDynamics.load (std::memory_order_relaxed);
    s.dynamicsFollow = cfg.dynamicsFollow.load (std::memory_order_relaxed);
    s.standingDown = lastStandingDown.load (std::memory_order_relaxed);
    s.sectionChanges = lastSections.load (std::memory_order_relaxed);
    s.phraseBar = lastPhraseBar.load (std::memory_order_relaxed);
    s.evidenceTrust = lastEvidenceTrust.load (std::memory_order_relaxed);
    s.gridTauSec = lastGridTauSec.load (std::memory_order_relaxed);
    s.analysisRestarts = static_cast<int> (lastRestarts.load (std::memory_order_relaxed));
    s.analysisBacklog = lastBacklog.load (std::memory_order_relaxed);
    s.leadMs = lastLeadMs.load (std::memory_order_relaxed);
    s.attackLeadMs = lastAttackLeadMs.load (std::memory_order_relaxed);
    s.tempoRegime = lastRegime.load (std::memory_order_relaxed);
    s.combBpm = lastCombBpm.load (std::memory_order_relaxed);
    s.levelSettled = lastLevelSettled.load (std::memory_order_relaxed);
    s.fitResidual = lastFitResidual.load (std::memory_order_relaxed);
    s.fitCoverage = lastFitCoverage.load (std::memory_order_relaxed);
    s.shortFitBpm = lastShortFitBpm.load (std::memory_order_relaxed);
    s.longFitBpm = lastLongFitBpm.load (std::memory_order_relaxed);
    s.shortFitResidual = lastShortFitResidual.load (std::memory_order_relaxed);
    s.tempoTransitionState = static_cast<TempoTransitionState> (
        lastTempoTransitionState.load (std::memory_order_relaxed));
    s.tempoTransitionReason = static_cast<TempoTransitionReason> (
        lastTempoTransitionReason.load (std::memory_order_relaxed));
    s.tempoTransitionBpm = lastTempoTransitionBpm.load (std::memory_order_relaxed);
    s.tempoTransitionConfidence =
        lastTempoTransitionConfidence.load (std::memory_order_relaxed);
    s.tempoTransitionIntervals =
        lastTempoTransitionIntervals.load (std::memory_order_relaxed);
    // The level in force, which under AUTO is not the one in the settings.
    s.tempoOctave = lastOctave.load (std::memory_order_relaxed);
    s.tempoOctaveAuto = cfg.tempoOctaveAuto.load (std::memory_order_relaxed);
    s.tempoFollow = cfg.tempoFollow.load (std::memory_order_relaxed);

    s.loopPlaying = lastLoopPlaying.load (std::memory_order_relaxed);
    s.loopPhaseMs = lastLoopPhaseMs.load (std::memory_order_relaxed);
    s.loopHandovers = lastHandovers.load (std::memory_order_relaxed);
    s.grooveStyle = lastStyle.load (std::memory_order_relaxed);
    s.grooveStyleConfidence = lastStyleConf.load (std::memory_order_relaxed);
    s.styleEvenKick = lastStyleEvenKick.load (std::memory_order_relaxed);
    s.styleBackbeat = lastStyleBackbeat.load (std::memory_order_relaxed);
    s.styleOffHigh = lastStyleOffHigh.load (std::memory_order_relaxed);
    s.styleSync = lastStyleSync.load (std::memory_order_relaxed);
    s.styleOccupancy = lastStyleOccupancy.load (std::memory_order_relaxed);
    return s;
}

} // namespace vp
