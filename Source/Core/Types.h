#pragma once

#include <atomic>
#include <cstdint>

namespace vp
{

enum class TrackingState : int
{
    idle = 0,
    listening,
    locking,
    following,
    lowConfidence,
    recovering,
    stopped
};

enum class FollowStrength : int
{
    low = 0,
    medium,
    high
};

enum class Subdivision : int
{
    autoDetect = 0,
    quarter,
    eighth,
    sixteenth
};

/** Which part the percussionist plays. A player does not bring a marcha to a
    rock track, so this is not a set of variations on one pattern - each has its
    own conga figure and its own shaker. */
enum class GrooveStyle : int
{
    marcha = 0,   // latin: tumbao, the paired open tones closing the bar
    rock,         // sparse, locked to the backbeat, pushing into the one
    dance,        // busy sixteenths, syncopated, offbeat-accented
    pop,          // tasteful and out of the way
    samba,        // brazilian: weight on 2 and 4, syncopated opens
    funk,         // sixteenth ghosts, snare-side slaps off the beat
    reggae,       // one-drop: space on 1, the three, offbeat skank
    bossa,        // clave-shaped, mostly space, pulling on the ands
    // Not a genre: a shape. Two strokes on one quarter, one on the next, all
    // the way round the bar, on two drums and nothing else. It is what a
    // player reaches for when the song does not want a part so much as a
    // pulse, and it is the only style the automatic chooser will never pick -
    // "keep it simple" is a decision about the gig, not about the music.
    twoOne,
    count
};

inline const char* toString (GrooveStyle s) noexcept
{
    switch (s)
    {
        case GrooveStyle::marcha: return "MARCHA";
        case GrooveStyle::rock:   return "ROCK";
        case GrooveStyle::dance:  return "DANCE";
        case GrooveStyle::pop:    return "POP";
        case GrooveStyle::samba:  return "SAMBA";
        case GrooveStyle::funk:   return "FUNK";
        case GrooveStyle::reggae: return "REGGAE";
        case GrooveStyle::bossa:  return "BOSSA";
        case GrooveStyle::twoOne: return "DUE-UNO";
        case GrooveStyle::count:  break;
    }
    return "?";
}

enum class FollowSource : int
{
    kitMic = 0,
    speaker = 1,
    /** A file decoded by the app and fed to the tracker before it is mixed to
        the output. There is no acoustic round trip and none of the app's own
        percussion can leak into this signal. */
    internalPlayer = 2
};

enum class InputMode : int
{
    acoustic = 0,
    midi,
    hybrid
};

inline const char* toString (TrackingState s) noexcept
{
    switch (s)
    {
        case TrackingState::idle:          return "IDLE";
        case TrackingState::listening:     return "LISTENING";
        case TrackingState::locking:       return "LOCKING";
        case TrackingState::following:     return "FOLLOWING";
        case TrackingState::lowConfidence: return "LOW CONF";
        case TrackingState::recovering:    return "RECOVERING";
        case TrackingState::stopped:       return "STOPPED";
    }
    return "?";
}

enum class FollowBar : int
{
    ready = 0,
    listening,
    calibrating,
    followingListen,
    following,
    tapAlign,
    weakFollow,
    recalin,
    waitBeat,
    /** Armed, with a tempo, and deliberately not playing: the analysis has
        never heard this input change since the app was opened, so what it is
        following may be the room rather than anybody playing. See
        BeatTracker::setInputEpoch. */
    waitStart,
    paused
};

/**
    Whether an abrupt tempo change is being argued about, and how far the
    argument has got.

    The long-term regime verdict cannot answer "did the tempo just step" inside
    the two beats a listener notices. This state is decided only from those two
    causal intervals: one is `suspected`, because a fill or flam can make one;
    two agreeing intervals are `rapid`, the first point at which a misplaced
    event can be distinguished from a new tempo.

    This definition lives in Core so the worker hypothesis, audio tracker and
    UI snapshot share one type without introducing an AI/Core dependency cycle.
*/
enum class TempoTransitionState : int
{
    stable = 0,
    suspected,
    rapid
};

/** Why the transition state last changed. Diagnostic only: it identifies which
    bounded gate turned a candidate away, and nothing in the audio path acts on
    the reason. */
enum class TempoTransitionReason : int
{
    none = 0,
    candidateStarted,
    confirmed,
    incoherent,
    outsideRange,
    metricalConflict,
    expired,
    reset
};

/** The tempo regime as it appears in a snapshot. Takes the int rather than the
    enum: TempoRegime belongs to the analysis layer, and this header is below
    it. 0 = not decided yet, 1 = held, 2 = following. */
inline const char* regimeLabel (int regime) noexcept
{
    switch (regime)
    {
        case 1:  return "FISSO";
        case 2:  return "VIVO";
        default: break;
    }
    return "CERCO";
}

inline const char* toBarString (FollowBar b) noexcept
{
    switch (b)
    {
        case FollowBar::ready:           return "PRONTO";
        case FollowBar::listening:       return "ASCOLTANDO";
        case FollowBar::calibrating:     return "CALIBRANDO";
        case FollowBar::followingListen: return "SEGUENDO E ASCOLTO";
        case FollowBar::following:       return "SEGUENDO";
        case FollowBar::tapAlign:        return "ALLINEO TAP";
        case FollowBar::weakFollow:      return "TEMPO INCERTO";
        case FollowBar::recalin:         return "RICALIBRO";
        case FollowBar::waitBeat:        return "ATTENDO BATTUTA";
        case FollowBar::waitStart:       return "ATTENDO CHE ATTACCHI";
        case FollowBar::paused:          return "IN ASCOLTO - SHAKER OFF";
    }
    return "?";
}

struct EngineSnapshot
{
    TrackingState state        = TrackingState::idle;
    Subdivision   subdivision  = Subdivision::autoDetect;
    FollowStrength follow      = FollowStrength::medium;
    FollowSource  source       = FollowSource::kitMic;
    float bpm                  = 0.0f;
    float targetBpm            = 0.0f;
    float confidence           = 0.0f;
    float beatPhase            = 0.0f;
    float barPhase             = 0.0f;
    float latencyMs            = 0.0f;
    float inputPeak            = 0.0f;
    float callbackMs           = 0.0f;
    float humanization         = 0.35f;
    float reverbAmount         = 0.30f;
    bool  shakerEnabled        = true;
    bool  percussionAudible    = false;
    bool  tapLocked            = false;
    /** A tap has just said where beat one is; the UI marks the one for a moment. */
    bool  barDeclared          = false;
    /** And whether the count is now the listener's to move. A tap sets this as
        well as the on-screen control does, so the control has to read it back
        rather than remember what it last asked for. */
    bool  barLocked            = false;
    FollowBar followBar        = FollowBar::ready;
    int   bufferSize           = 0;
    int   shakerVoices         = 0;
    double sampleRate          = 0.0;
    int   beatsLocked          = 0;
    bool  aiOnnx               = false;
    bool  hypValid             = false;
    float neuralBpm            = 0.0f;
    float pBeat                = 0.0f;
    float analysisPeak         = 0.0f;
    /** Gain the analysis signal is being held at for the network, and how many
        input samples had to be replaced because they were not finite. */
    float analysisGain         = 1.0f;
    /** User trim on the analysis bus, linear. 1 = as the device delivered it. */
    float inputGain            = 1.0f;
    /** Peak left on the analysis bus after leak subtraction, before makeup.
        The ratio of this to `inputPeak` is how much of our own part survived. */
    float leakRemain           = 0.0f;
    int   badInputSamples      = 0;
    /** Times the analysis lost audio because its worker fell behind. Zero on a
        healthy run; anything else is a dropout in the tracking, not the sound. */
    int   analysisGaps         = 0;
    /** Input samples fed but not yet analysed; zero means the worker is caught up. */
    int   analysisBacklog      = 0;
    /** Times the analysis has been told to start its evidence again because the
        input changed character - in practice, the room the app was listening to
        turning into a band playing. One per song is what a set looks like; one
        in the middle of a song is this watcher being fooled. */
    int   analysisRestarts     = 0;
    /** Times the automatic alignment has rotated the bar, and how well the
        analysis is fitting compared with how well it has been fitting this
        song (1 down to 0.3), with the constant the clock is therefore
        averaging its phase over. Diagnostics. */
    int   barRotations         = 0;
    float evidenceTrust        = 1.0f;
    float gridTauSec           = 0.0f;
    /** The kick channel, when one is assigned: which channel, its envelope, how
        long it has been silent, how many strikes have been counted, and whether
        the tracker is currently believing them. Negative `kickChannel` means
        none is assigned and the rest are meaningless. */
    int   kickChannel          = -1;
    float kickLevel            = 0.0f;
    float kickQuietSec         = 0.0f;
    int   kickOnsets           = 0;
    bool  kickTrusted          = false;
    /** How much the band is giving, 0..1, and whether the part has stood down
        because the passage does not want it. */
    /** Chord changes counted, and whether the bar is being placed from them
        rather than from the network. */
    int   harmonicChanges      = 0;
    bool  barFromHarmony       = false;
    float harmonyMargin        = 0.0f;
    /** What share of the recent signal carried a chord at all. */
    float harmonicShare        = 0.0f;
    float bandDynamics         = 1.0f;
    bool  dynamicsFollow       = true;
    bool  standingDown         = false;
    /** Section boundaries the band's dynamics have marked, and where the
        eight-bar sentence has got to since the last one. */
    int   sectionChanges       = 0;
    int   phraseBar            = 0;
    /** True when the kick channel says the drummer has stopped while the rest
        of the band is still playing. Not an inference: the channel is silent. */
    bool  drumsOut             = false;
    /** Measured analysis-plus-output delay the clock runs ahead by, so that what
        is played lands on the pulse the listener hears. */
    float leadMs               = 0.0f;
    /** How far ahead of the beat the clock is deliberately placing its pulses
        so that the *sound* lands on it: the slowest attack in the percussion
        bank. The clock is early by exactly this much on purpose. */
    float attackLeadMs         = 0.0f;
    /** Whether the analysis is treating the tempo as a fixed one to hold or a
        live one to follow. */
    int   tempoRegime          = 0;
    /** What the tempo fold is naming, and whether it has had enough audio for
        that to count as decided. */
    float combBpm              = 0.0f;
    bool  levelSettled         = false;
    /** How well the detected beats fit the committed grid, and how much of the
        fit's window they filled. Diagnostics only. */
    float fitResidual          = 1.0f;
    float fitCoverage          = 0.0f;
    TempoTransitionState tempoTransitionState = TempoTransitionState::stable;
    TempoTransitionReason tempoTransitionReason = TempoTransitionReason::none;
    float tempoTransitionBpm = 0.0f;
    float tempoTransitionConfidence = 0.0f;
    int   tempoTransitionIntervals = 0;
    /** Half or double the measured tempo: the level actually in force, whether
        the listener asked for it or AUTO settled on it. */
    int   tempoOctave          = 0;
    bool  tempoOctaveAuto      = true;
    /** False when the listener has locked a BPM (FISSO). Default true (SEGUI). */
    bool  tempoFollow          = true;

    /** The recorded percussionist: whether a recording is what is being heard,
        how far its audio is from where the clock says it should be, and how
        many times the part has changed hands between the two engines. All
        three are zero in a build with VP_ENABLE_RECORDED_LOOPS off. */
    bool  loopPlaying          = false;
    float loopPhaseMs          = 0.0f;
    int   loopHandovers        = 0;

    int   grooveStyle          = 0;
    float grooveStyleConfidence = 0.0f;
    float styleEvenKick        = 0.0f;
    float styleBackbeat        = 0.0f;
    float styleOffHigh         = 0.0f;
    float styleSync            = 0.0f;
    float styleOccupancy       = 0.0f;
};

struct EngineSettings
{
    std::atomic<float> masterVolume    { 0.90f };
    std::atomic<float> percussionVolume{ 1.00f };
    /** Balance between the two instruments. 0 is shaker at full and congas
        silent, 0.5 is both at full, 1 is congas at full and shaker silent. */
    std::atomic<float> instrumentMix   { 0.50f };
    /** Linear gain on the mixed input, before leak subtraction and makeup.
        0 silences the tracker; 2 is +6 dB. Does not touch the output. */
    std::atomic<float> inputGain       { 1.00f };
    std::atomic<float> reverbAmount    { 0.30f };
    // A percussionist is not on the grid and is not evenly loud. 0 is a
    // sequencer; the default is a player who is not trying to be one.
    std::atomic<float> humanization    { 0.35f };
    std::atomic<float> swing           { 0.00f };
    std::atomic<float> intensity       { 0.50f };
    std::atomic<int>   followStrength  { static_cast<int> (FollowStrength::high) };
    std::atomic<int>   subdivision     { static_cast<int> (Subdivision::eighth) };
    std::atomic<bool>  shakerEnabled   { true };
    std::atomic<bool>  congasEnabled   { true };
    // Whether the part follows the band's dynamics: quieter and thinner when
    // the band comes down, silent in a passage that does not want it. On by
    // default - it is the difference between a part that is correct and a
    // player who is listening - and switchable, because a fixed part is what
    // some jobs want. See Percussion/BandDynamics.h.
    std::atomic<bool>  dynamicsFollow  { true };
    std::atomic<int>   grooveStyle     { static_cast<int> (GrooveStyle::marcha) };
    // Let the music choose the part. Off by default: measured at 3 cases in 9
    // against material whose style is known, which is no better than always
    // guessing the same style. See docs/AUDIO_ENGINE.md.
    std::atomic<bool>  grooveAuto      { false };
    // Counter, not a position: every increment moves the bar on by one beat.
    // Where beat one is cannot be read reliably from what the network gives us
    // - see docs/STATUS.md - so the listener gets to say, and saying it has to
    // be one tap however wrong the analysis currently is.
    std::atomic<int>   barNudge        { 0 };
    // And having said it, it stays said. The automatic alignment is a vote over
    // what the network calls a downbeat, and where the network is no better
    // than a coin - a microphone in a room, measured - that vote would move a
    // bar the listener has just placed by hand. Locked, nothing moves the count
    // but the listener: not the vote, not a new song, not a section change.
    //
    // Set by moving the one and by a tap that declares it, cleared by taking
    // the on-screen control all the way round the bar. It does not stop the
    // count from *following the grid* when the clock re-places it - that is
    // what keeps a locked bar on the same beat of the song rather than letting
    // it drift a quarter away. See TempoFollower::snapPhase.
    std::atomic<bool>  barLocked       { false };
    // SEGUI (true, default) lets BeatNet / tap-follow / mixer analysis drive
    // the clock. FISSO freezes a BPM and ignores neural tempo updates; tap
    // and the on-screen nudge still write the number. Generation bumps when
    // the listener sets the value so the audio thread can apply it once.
    std::atomic<bool>     tempoFollow  { true };
    std::atomic<float>    userBpm      { 120.0f };
    std::atomic<uint32_t> userBpmGen   { 0 };
    std::atomic<int>   analysisChannel { -1 }; // -1 = mix all
    /** Which input channel carries the kick drum on its own, or -1 for none.
        This is the single most useful thing a digital desk can hand the app
        and the only input it has that is one instrument: it dates the beat to
        the sample instead of to a 20 ms analysis frame, and it says when the
        drummer has stopped instead of leaving that to be inferred from a fit.
        See Tracking/KickOnsetDetector.h. */
    std::atomic<int>   kickChannel     { -1 };
    std::atomic<int>   followSource    { static_cast<int> (FollowSource::kitMic) };
};

inline float wrap01 (float x) noexcept
{
    x -= static_cast<float> (static_cast<int> (x));
    if (x < 0.0f)
        x += 1.0f;
    return x;
}

inline float wrapCentered (float x) noexcept
{
    x = wrap01 (x + 0.5f) - 0.5f;
    return x;
}

inline float clamp01 (float x) noexcept
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

inline const char* followLabel (FollowStrength s) noexcept
{
    switch (s)
    {
        case FollowStrength::low:    return "LOW";
        case FollowStrength::medium: return "MEDIUM";
        case FollowStrength::high:   return "HIGH";
    }
    return "?";
}

} // namespace vp
