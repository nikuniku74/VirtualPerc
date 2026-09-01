#pragma once

#include "Core/Types.h"

#include <string>
#include <vector>

namespace vp
{

/** What a recorded performance is *for*. A player does not record one bar and
    repeat it: they record the part, the variation they go to when the section
    repeats, the bar they play on the way out of a section, and the way they
    come in. Those are four different recordings, not four gain settings on
    one. */
enum class LoopRole : int
{
    grooveA = 0,
    grooveB,
    fill,
    intro,
    count
};

/** Which instrument the file carries. Congas and shaker are recorded and kept
    apart so the balance is still the listener's - the same reason
    `EngineSettings::instrumentMix` exists for the synthesised bank. A stereo
    file with both on it cannot be balanced afterwards. */
enum class LoopStem : int
{
    congas = 0,
    shaker,
    count
};

const char* toString (LoopRole r) noexcept;
const char* toString (LoopStem s) noexcept;
bool parseLoopRole (const std::string& s, LoopRole& out) noexcept;
bool parseLoopStem (const std::string& s, LoopStem& out) noexcept;
bool parseGrooveStyle (const std::string& s, GrooveStyle& out) noexcept;
const char* manifestStyleName (GrooveStyle s) noexcept;

/**
    One recorded performance, described well enough that the player can put it
    on the song's grid without listening to it first.

    Everything here is a property of the *recording*. Nothing in it is a
    preference: `swing` is the swing the player actually played, not the swing
    the listener has asked for, and `intensity` is how hard the take was played,
    not how loud it should come out. The selection in LoopBank is the only place
    the two meet.
*/
struct LoopManifest
{
    std::string id;
    /** Audio file, relative to the directory the manifest was read from. */
    std::string file;

    GrooveStyle style = GrooveStyle::dance;
    LoopRole    role  = LoopRole::grooveA;
    LoopStem    stem  = LoopStem::congas;

    float  nativeBpm = 120.0f;
    int    bars = 2;
    int    meterNumerator = 4;
    int    meterDenominator = 4;

    /** Sample the first quarter of the loop lands on. Deliberately not assumed
        to be zero. A bar exported from a DAW normally carries a few samples of
        the stroke's own pre-attack ahead of the bar line, and a file trimmed
        hard to sample zero has had that pre-attack cut off it - which is
        audible as a click on every pass. The honest export keeps the pre-roll
        and says here where the beat is. */
    int    firstBeatSample = 0;

    /** What the audio file turned out to hold. Filled by the loader, checked
        against the manifest, and part of what `validate` refuses on. */
    int    frames = 0;
    int    channels = 2;
    double sampleRate = 48000.0;

    /** How hard the take was played, 0..1, and the swing that is *in* it -
        0 for a straight take, 0.5-0.67 for a shuffled one, measured as the
        fraction of the beat the second eighth sits at, remapped to 0..1 the
        same way `EngineSettings::swing` is. */
    float  intensity = 0.5f;
    float  swing = 0.0f;

    /** Which alternative execution of the same part this is. Two takes of one
        loop is the minimum: a part that repeats bit-identically for four
        minutes reads as a machine however good the recording is. */
    int    take = 1;

    /** One sample position per quarter, in file samples. May be empty (the
        quarters are then assumed evenly spaced from `firstBeatSample` at
        `nativeBpm`), `totalBeats()` long, or `totalBeats() + 1` long with the
        end of the loop body as its last entry. */
    std::vector<int> beatSamples;

    int beatsPerBar() const noexcept { return meterNumerator > 0 ? meterNumerator : 4; }
    int totalBeats() const noexcept { return bars * beatsPerBar(); }

    /** Frames one quarter occupies at the recorded tempo. */
    double nominalBeatFrames() const noexcept
    {
        return 60.0 / static_cast<double> (nativeBpm > 1.0f ? nativeBpm : 120.0f) * sampleRate;
    }

    /** Where quarter `beat` sits in the file. Beats past the markers are
        extrapolated at the nominal spacing, so `beatPosition (totalBeats())` is
        the end of the loop body whether or not the export listed it. */
    double beatPosition (int beat) const noexcept;

    /** First quarter to the same quarter one pass later: the length of the
        thing that actually repeats, which is not the same as `frames` when the
        file carries pre-roll or a tail. */
    double bodyFrames() const noexcept
    {
        return beatPosition (totalBeats()) - beatPosition (0);
    }

    /** Why this manifest cannot be played, or empty when it can. */
    bool validate (std::string& why) const;
};

/** A bank file: a header and the performances it lists. */
struct LoopManifestFile
{
    int version = 1;
    std::string bankName;
    std::vector<LoopManifest> loops;
};

/** Read the text manifest format documented in docs/RECORDED_LOOPS.md.

    Deliberately not JSON. The manifest is written by hand as often as it is
    generated, it is read once at load time and never on the audio thread, and a
    line-oriented format is one that a wrong character in fails on the line it
    is on rather than three sections later. `error` names the line. */
bool parseLoopManifest (const std::string& text, LoopManifestFile& out, std::string& error);

/** The same format back out, so a generated bank and a hand-written one are the
    same file. */
std::string writeLoopManifest (const LoopManifestFile& file);

} // namespace vp
