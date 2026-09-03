#include "Loops/LoopBank.h"
#include "Loops/WavFile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vp
{

namespace
{
    std::string directoryOf (const std::string& path)
    {
        const size_t slash = path.find_last_of ("/\\");
        if (slash == std::string::npos)
            return std::string {};
        return path.substr (0, slash);
    }

    std::string join (const std::string& dir, const std::string& file)
    {
        if (dir.empty() || (! file.empty() && (file[0] == '/' || file[0] == '\\')))
            return file;
        return dir + "/" + file;
    }

    std::string readWholeFile (const std::string& path, bool& ok)
    {
        ok = false;
        std::FILE* f = std::fopen (path.c_str(), "rb");
        if (f == nullptr)
            return {};
        std::string out;
        char buf[4096];
        size_t n = 0;
        while ((n = std::fread (buf, 1, sizeof (buf), f)) > 0)
            out.append (buf, n);
        std::fclose (f);
        ok = true;
        return out;
    }
} // namespace

void LoopBank::clear() noexcept
{
    entries.clear();
    bankName.clear();
    bankSampleRate = 0.0;
}

void LoopBank::setStretchLimit (float maxUp, float maxDown) noexcept
{
    limitUp = std::clamp (maxUp, 1.0f, 2.0f);
    limitDown = std::clamp (maxDown, 0.5f, 1.0f);
}

void LoopBank::setSwingTolerance (float t) noexcept
{
    swingTol = std::clamp (t, 0.0f, 1.0f);
}

void LoopBank::addEntry (const LoopManifest& manifest, std::vector<float> left, std::vector<float> right)
{
    Entry e;
    e.manifest = manifest;
    e.manifest.frames = static_cast<int> (left.size());
    e.left = std::move (left);
    e.right = std::move (right);
    if (e.right.size() != e.left.size())
        e.right = e.left;
    e.bodyStart = e.manifest.beatPosition (0);
    e.bodyEnd = e.manifest.beatPosition (e.manifest.totalBeats());
    if (bankSampleRate <= 0.0)
        bankSampleRate = e.manifest.sampleRate;
    entries.push_back (std::move (e));
}

bool LoopBank::load (const std::string& manifestText, const std::string& directory, std::string& error)
{
    return loadWithAudioLoader (
        manifestText,
        [directory] (const std::string& file, WavAudio& audio, std::string& why)
        {
            return loadWavFile (join (directory, file), audio, why);
        },
        error);
}

bool LoopBank::loadWithAudioLoader (const std::string& manifestText,
                                    const AudioLoader& loader,
                                    std::string& error)
{
    clear();

    if (! loader)
    {
        error = "no audio loader was supplied";
        return false;
    }

    LoopManifestFile file;
    if (! parseLoopManifest (manifestText, file, error))
        return false;

    bankName = file.bankName;

    for (const auto& m : file.loops)
    {
        WavAudio audio;
        std::string why;
        if (! loader (m.file, audio, why))
        {
            error = "loop \"" + m.id + "\": " + why;
            clear();
            return false;
        }

        // The manifest is the contract and the file has to keep it. A rate that
        // does not match is not a detail: every marker in the manifest is a
        // sample position, so a 44.1 kHz file described at 48 puts every quarter
        // 8.8% away from where it says it is.
        LoopManifest checked = m;
        checked.sampleRate = audio.sampleRate;
        checked.channels = audio.channels;
        checked.frames = audio.frames;

        if (std::fabs (audio.sampleRate - m.sampleRate) > 1.0)
        {
            error = "loop \"" + m.id + "\": the file is at "
                    + std::to_string (static_cast<int> (audio.sampleRate))
                    + " Hz, the manifest says "
                    + std::to_string (static_cast<int> (m.sampleRate));
            clear();
            return false;
        }
        if (bankSampleRate > 0.0 && std::fabs (audio.sampleRate - bankSampleRate) > 1.0)
        {
            error = "loop \"" + m.id + "\": the bank is at "
                    + std::to_string (static_cast<int> (bankSampleRate))
                    + " Hz and this file is not";
            clear();
            return false;
        }

        if (! checked.validate (why))
        {
            error = "loop \"" + m.id + "\": " + why;
            clear();
            return false;
        }

        addEntry (checked, std::move (audio.left), std::move (audio.right));
    }

    if (entries.empty())
    {
        error = "the manifest listed no usable loops";
        return false;
    }
    return true;
}

bool LoopBank::loadFromManifestFile (const std::string& manifestPath, std::string& error)
{
    bool ok = false;
    const std::string text = readWholeFile (manifestPath, ok);
    if (! ok)
    {
        error = "cannot read " + manifestPath;
        return false;
    }
    return load (text, directoryOf (manifestPath), error);
}

int LoopBank::longestBodyFrames() const noexcept
{
    int longest = 0;
    for (const auto& e : entries)
        longest = std::max (longest, static_cast<int> (std::ceil (e.bodyEnd)));
    return longest;
}

LoopSelection LoopBank::select (const LoopQuery& q) const noexcept
{
    LoopSelection best;
    best.miss = LoopMissReason::noSuchPart;

    float bestCost = 1.0e30f;
    bool sawPart = false;
    bool sawInRange = false;

    for (int i = 0; i < static_cast<int> (entries.size()); ++i)
    {
        const auto& m = entries[static_cast<size_t> (i)].manifest;
        if (m.style != q.style || m.role != q.role || m.stem != q.stem)
            continue;
        sawPart = true;

        // Input frames per output frame: above 1 the loop is being played
        // faster than it was recorded.
        const float ratio = q.bpm / m.nativeBpm;
        if (ratio > limitUp || ratio < limitDown)
            continue;
        sawInRange = true;

        const float swingGap = std::fabs (q.swing - m.swing);
        if (swingGap > swingTol)
            continue;

        // Tempo first, and in octaves rather than in BPM so that 6 BPM at 90
        // counts for what it is worth against 6 BPM at 160. Then the swing,
        // heavily: a shuffled part on a straight song is wrong in a way that a
        // slightly-too-hard take is not. Intensity last, and gently - it is a
        // colour, and the dynamics layer can still move it afterwards.
        const float tempoCost = std::fabs (std::log2 (ratio)) * 6.0f;
        const float swingCost = swingGap * 4.0f;
        const float intensityCost = std::fabs (q.intensity - m.intensity) * 1.0f;
        // And, all else equal, not the take that is already playing.
        const float repeatCost = (i == q.avoidIndex) ? 0.35f : 0.0f;

        const float cost = tempoCost + swingCost + intensityCost + repeatCost;
        if (cost < bestCost)
        {
            bestCost = cost;
            best.index = i;
            best.ratio = ratio;
            best.miss = LoopMissReason::none;
        }
    }

    if (best.index < 0)
        best.miss = ! sawPart ? LoopMissReason::noSuchPart
                  : (! sawInRange ? LoopMissReason::tempoTooFar
                                  : LoopMissReason::swingTooFar);
    return best;
}

} // namespace vp
