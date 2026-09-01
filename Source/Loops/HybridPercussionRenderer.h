#pragma once

#include "AI/BeatHypothesis.h"
#include "Loops/LoopBank.h"
#include "Loops/LoopPlayer.h"
#include "Percussion/PercussionEngine.h"
#include "Tracking/TempoFollower.h"

#include <vector>

namespace vp
{

/**
    Which of the two percussionists plays, and how the handover sounds.

    There are two, and they are good at different things. The recording carries
    the groove: the microtiming, the way one stroke leans on the next, the sound
    of two hands on a drum in a room - none of which is reachable by scheduling
    single samples on a grid. The single-stroke engine carries everything that
    has to be *decided* while the song is happening: coming in and going out,
    the section changing, the band dropping, and above all a tempo that is
    genuinely moving, where a recording has to be stretched further every bar
    and a scheduled stroke simply lands where the clock says.

    So the rule is the regime the decoder already publishes:

      | regime  | who plays        | why                                        |
      |---------|------------------|--------------------------------------------|
      | fixed   | the recording    | a click track: only drift needs correcting |
      | live    | single strokes   | the tempo is moving; strokes follow it free |
      | unknown | single strokes   | today's engine, until the tempo settles     |

    The handover is a musical event, not a switch: it happens on a quarter,
    preferably on a bar line, over a short crossfade, and it never touches the
    clock or the phrase count. Both engines are rendered through it, which is
    what makes it a crossfade rather than a cut.

    With `VP_ENABLE_RECORDED_LOOPS` off - which is the default - or with no bank
    loaded, this object is a thin pass-through to `PercussionEngine` and the app
    behaves exactly as it did before it existed.
*/
class HybridPercussionRenderer
{
public:
    struct Input
    {
        ClockTick   tick;
        TempoRegime regime = TempoRegime::unknown;
        /** `percussionShouldPlay && ! standingDown`, as the engine computes it. */
        bool  audible = false;
        float bpm = 120.0f;
        GrooveStyle style = GrooveStyle::dance;
        float swing = 0.0f;
        float intensity = 0.5f;
        /** How much the band is giving, 0..1. Scales the recording the same way
            it thins the single-stroke part. */
        float dynamics = 1.0f;
        bool  congasEnabled = true;
        bool  shakerEnabled = true;
        /** 0 = shaker only, 0.5 = both, 1 = congas only, as `EngineSettings`. */
        float instrumentMix = 0.5f;
        /** The band has started a new section, so the eight-bar sentence starts
            again here. Same signal `PercussionEngine::alignPhrase` gets. */
        bool  sectionChanged = false;
    };

    void prepare (double sampleRate, int maxBlock) noexcept;
    void reset() noexcept;

    /** The bank. Only while the audio callback is not running. */
    void setBank (const LoopBank* b) noexcept;
    const LoopBank* bank() const noexcept { return congas.bank(); }

    /** The runtime half of `VP_ENABLE_RECORDED_LOOPS`. Off unless the build
        turned it on *and* nothing has turned it off since. */
    void setEnabled (bool on) noexcept;
    bool isEnabled() const noexcept { return enabled; }

    /** How loud the single-stroke engine is allowed to be *over* a recording,
        for accents the recording cannot know about. Zero by default and
        deliberately: layering a synthesised stroke on a recorded groove is a
        decision to be taken with the material in front of you, and until the
        listening comparison has happened the honest default is not to. */
    void  setAccentLayer (float gain) noexcept;
    float accentLayer() const noexcept { return accentGain; }

    void start() noexcept;
    void stop() noexcept;

    /** Renders the part. `percussion` is the engine the app already owns - it
        is driven, never replaced. Returns the strokes it fired, exactly as
        `PercussionEngine::render` does. */
    int render (PercussionEngine& percussion, float* outL, float* outR,
                int numSamples, const Input& in) noexcept;

    /** True when the recording is what is being heard. Diagnostics and tests. */
    bool loopIsPlaying() const noexcept
    {
        return mode == Mode::recorded && (congas.isPlaying() || shaker.isPlaying());
    }
    /** What it would like to be playing, before the handover rules. */
    bool wantsRecorded() const noexcept { return wantRecorded; }
    int  handovers() const noexcept { return handoverCount; }
    /** How much of the part the recording currently carries, 0..1. Above zero
        the recording must be sounding: see the handover in `render`. */
    float loopBlend() const noexcept { return blend; }
    int  phraseBar() const noexcept { return barInPhrase; }
    /** The congas player. `player()` without an argument is it, because it is
        the one the phase diagnostics are read off: the two run on the same
        clock and the same map, so their phase error is the same number. */
    const LoopPlayer& player() const noexcept { return congas; }
    LoopPlayer& player() noexcept { return congas; }
    const LoopPlayer& shakerPlayer() const noexcept { return shaker; }

private:
    enum class Mode { strokes = 0, recorded };

    LoopRole roleForBar() const noexcept;
    /** Ask one stem for a part, and say whether the library had one. */
    bool aimStem (LoopPlayer& player, LoopStem stem, const Input& in) noexcept;
    /** Size the two players, but only once there is a bank for them to play.

        Deliberately not part of `prepare`. Preparing a player prepares its
        stretcher, and preparing a stretcher *measures* it - two passes of a
        test burst through the backend, per voice, per stem. That is work worth
        doing when a recording is going to be played and pure waste when the
        app is running as it always has, and it lands in the middle of opening
        the audio device, where it delays the analysis worker's first audio
        relative to everything else. Nothing that is not used should cost
        anything. Allocates: same thread rules as `setBank`. */
    void preparePlayers() noexcept;

    /** One player per stem, because the recordings are per stem and the balance
        between them stays the listener's - the same reason
        `EngineSettings::instrumentMix` exists for the synthesised bank. A single
        player could only ever have played one of the two. */
    LoopPlayer congas;
    LoopPlayer shaker;
    std::vector<float> loopL, loopR;
    std::vector<float> stemL, stemR;
    std::vector<float> strokeL, strokeR;

    double sampleRate = 48000.0;
    int    maxBlock = 1024;
    bool   prepared = false;
    bool   playersPrepared = false;
#if defined(VP_ENABLE_RECORDED_LOOPS) && VP_ENABLE_RECORDED_LOOPS
    bool   enabled = true;
#else
    bool   enabled = false;
#endif

    Mode  mode = Mode::strokes;
    bool  wantRecorded = false;
    /** Crossfade between the two engines: 0 all strokes, 1 all recording. */
    float blend = 0.0f;
    float blendStep = 0.0f;
    int   handoverCount = 0;
    /** Blocks the wish has held. A handover on the first block the regime
        flickers is a handover twice a minute. */
    int   wantHeldBlocks = 0;

    int   barInPhrase = 0;
    bool  seenBar = false;
    float accentGain = 0.0f;
    int   lastStrokes = 0;
};

} // namespace vp
