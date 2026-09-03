#pragma once

#include "Loops/LoopManifest.h"
#include "Loops/WavFile.h"

#include <functional>
#include <string>
#include <vector>

namespace vp
{

/** What the player is asking the bank for. Everything in it is a *wish*: the
    tempo the song is at, the swing the listener has dialled, how hard the band
    is playing. What comes back is the recording that is nearest to it, or
    nothing. */
struct LoopQuery
{
    GrooveStyle style = GrooveStyle::dance;
    LoopRole    role  = LoopRole::grooveA;
    LoopStem    stem  = LoopStem::congas;
    float bpm = 120.0f;
    float swing = 0.0f;
    float intensity = 0.5f;
    /** A loop already playing, so the bank can prefer a different take of the
        same part when it has one. Two takes exist precisely so the second time
        round is not the first time round again. */
    int   avoidIndex = -1;
};

/** Why the bank had nothing, when it had nothing. The distinction matters: a
    tempo the library does not reach and a swing the library does not have are
    different problems with the same answer for this song (play single strokes)
    and different answers for the library. */
enum class LoopMissReason : int
{
    none = 0,
    noSuchPart,     // no recording of this style/role/stem at all
    tempoTooFar,    // there are recordings, all outside the stretch limit
    swingTooFar     // there are recordings at the tempo, none near this swing
};

struct LoopSelection
{
    int   index = -1;
    /** Input frames per output frame the player will have to run at. */
    float ratio = 1.0f;
    LoopMissReason miss = LoopMissReason::noSuchPart;
    bool  ok() const noexcept { return index >= 0; }
};

/**
    Every recorded performance the app has, in memory, plus the rule for picking
    one.

    Loaded whole, off the audio thread, before anything plays. Once loaded it is
    const: the audio thread reads samples straight out of it and never asks it
    for anything that could move. That is deliberate and it is the reason a bank
    is swapped by handing the player a different `LoopBank*` while the device is
    closed, rather than by mutating this one.
*/
class LoopBank
{
public:
    /** Supplies one decoded WAV by the filename written in the manifest.
        Used by the app for files embedded in its binary; disk banks use the
        same path through `load`. Called only while the audio device is closed. */
    using AudioLoader = std::function<bool (const std::string& file,
                                            WavAudio& audio,
                                            std::string& error)>;

    struct Entry
    {
        LoopManifest manifest;
        std::vector<float> left;
        std::vector<float> right;
        /** First quarter, and the same quarter one pass later, in file frames.
            Cached because the player asks for them once a block. */
        double bodyStart = 0.0;
        double bodyEnd = 0.0;
    };

    /** Read a manifest and every file it names. `directory` is where the audio
        paths are relative to. Returns false and fills `error` on the first
        thing that is wrong; a bank is all there or it is not used. */
    bool loadFromManifestFile (const std::string& manifestPath, std::string& error);

    /** The same, for a manifest already in memory. `directory` may be empty
        when the entries carry absolute paths. */
    bool load (const std::string& manifestText, const std::string& directory, std::string& error);

    /** Load a manifest whose audio comes from something other than the file
        system, such as JUCE BinaryData embedded in an iOS app bundle. */
    bool loadWithAudioLoader (const std::string& manifestText, const AudioLoader& loader,
                              std::string& error);

    /** Add one entry whose audio is already in memory. For the tests, and for a
        future bundled bank that arrives as binary data rather than as files. */
    void addEntry (const LoopManifest& manifest, std::vector<float> left, std::vector<float> right);

    void clear() noexcept;

    int size() const noexcept { return static_cast<int> (entries.size()); }
    bool empty() const noexcept { return entries.empty(); }
    const Entry& entry (int i) const noexcept { return entries[static_cast<size_t> (i)]; }
    const std::string& name() const noexcept { return bankName; }
    double sampleRate() const noexcept { return bankSampleRate; }

    /** How far the tempo may be pulled. A recording stretched much past this
        stops sounding like the room it was recorded in, whatever the algorithm
        is; past it the app is better off playing single strokes. Defaults to
        roughly a minor third of nothing - +/-12% - which with three native
        tempos per style covers a style end to end. */
    void  setStretchLimit (float maxUp, float maxDown) noexcept;
    float stretchUpLimit() const noexcept { return limitUp; }
    float stretchDownLimit() const noexcept { return limitDown; }

    /** How far the recorded swing may be from the asked-for swing before the
        bank refuses. Past it the answer is single strokes, not a straight loop
        with a swung part played over it. */
    void  setSwingTolerance (float t) noexcept;
    float swingTolerance() const noexcept { return swingTol; }

    /** The nearest recording, or a miss with a reason. Real-time safe: it reads
        the manifests it already holds and allocates nothing. */
    LoopSelection select (const LoopQuery& q) const noexcept;

    /** Longest loop body in the bank, in frames. What the player sizes its
        gather scratch from. */
    int longestBodyFrames() const noexcept;

private:
    std::vector<Entry> entries;
    std::string bankName;
    double bankSampleRate = 0.0;
    float limitUp = 1.12f;
    float limitDown = 0.89f;
    float swingTol = 0.18f;
};

} // namespace vp
