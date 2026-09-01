#include "Loops/LoopManifest.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace vp
{

namespace
{
    std::string lower (std::string s)
    {
        for (auto& c : s)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return s;
    }

    std::string trim (const std::string& s)
    {
        size_t a = 0;
        size_t b = s.size();
        while (a < b && std::isspace (static_cast<unsigned char> (s[a])))
            ++a;
        while (b > a && std::isspace (static_cast<unsigned char> (s[b - 1])))
            --b;
        return s.substr (a, b - a);
    }

    /** Splits "key rest of line" into the two. The value keeps its inner
        spaces, which is what makes `beats 0 12000 24000` one entry rather than
        four. */
    void splitKey (const std::string& line, std::string& key, std::string& value)
    {
        size_t i = 0;
        while (i < line.size() && ! std::isspace (static_cast<unsigned char> (line[i])))
            ++i;
        key = lower (line.substr (0, i));
        value = trim (line.substr (i));
    }

    bool toFloat (const std::string& s, float& out)
    {
        try
        {
            size_t used = 0;
            const float v = std::stof (s, &used);
            if (used == 0)
                return false;
            out = v;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool toInt (const std::string& s, int& out)
    {
        try
        {
            size_t used = 0;
            const int v = std::stoi (s, &used);
            if (used == 0)
                return false;
            out = v;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool toMeter (const std::string& s, int& num, int& den)
    {
        const size_t slash = s.find ('/');
        if (slash == std::string::npos)
            return false;
        return toInt (trim (s.substr (0, slash)), num)
            && toInt (trim (s.substr (slash + 1)), den);
    }
} // namespace

const char* toString (LoopRole r) noexcept
{
    switch (r)
    {
        case LoopRole::grooveA: return "grooveA";
        case LoopRole::grooveB: return "grooveB";
        case LoopRole::fill:    return "fill";
        case LoopRole::intro:   return "intro";
        case LoopRole::count:   break;
    }
    return "?";
}

const char* toString (LoopStem s) noexcept
{
    switch (s)
    {
        case LoopStem::congas: return "congas";
        case LoopStem::shaker: return "shaker";
        case LoopStem::count:  break;
    }
    return "?";
}

bool parseLoopRole (const std::string& s, LoopRole& out) noexcept
{
    const std::string v = lower (trim (s));
    if (v == "groovea" || v == "groove_a" || v == "a")      { out = LoopRole::grooveA; return true; }
    if (v == "grooveb" || v == "groove_b" || v == "b")      { out = LoopRole::grooveB; return true; }
    if (v == "fill")                                        { out = LoopRole::fill;    return true; }
    if (v == "intro")                                       { out = LoopRole::intro;   return true; }
    return false;
}

bool parseLoopStem (const std::string& s, LoopStem& out) noexcept
{
    const std::string v = lower (trim (s));
    if (v == "congas" || v == "conga")   { out = LoopStem::congas; return true; }
    if (v == "shaker" || v == "shakers") { out = LoopStem::shaker; return true; }
    return false;
}

const char* manifestStyleName (GrooveStyle s) noexcept
{
    switch (s)
    {
        case GrooveStyle::marcha: return "marcha";
        case GrooveStyle::rock:   return "rock";
        case GrooveStyle::dance:  return "dance";
        case GrooveStyle::pop:    return "pop";
        case GrooveStyle::samba:  return "samba";
        case GrooveStyle::funk:   return "funk";
        case GrooveStyle::reggae: return "reggae";
        case GrooveStyle::bossa:  return "bossa";
        case GrooveStyle::twoOne: return "twoone";
        case GrooveStyle::count:  break;
    }
    return "?";
}

bool parseGrooveStyle (const std::string& s, GrooveStyle& out) noexcept
{
    const std::string v = lower (trim (s));
    for (int i = 0; i < static_cast<int> (GrooveStyle::count); ++i)
    {
        const auto g = static_cast<GrooveStyle> (i);
        if (v == manifestStyleName (g))
        {
            out = g;
            return true;
        }
    }
    if (v == "due-uno" || v == "two-one" || v == "2-1")
    {
        out = GrooveStyle::twoOne;
        return true;
    }
    return false;
}

double LoopManifest::beatPosition (int beat) const noexcept
{
    if (beat < 0)
        beat = 0;
    const double spacing = nominalBeatFrames();
    if (beatSamples.empty())
        return static_cast<double> (firstBeatSample) + static_cast<double> (beat) * spacing;

    const int n = static_cast<int> (beatSamples.size());
    if (beat < n)
        return static_cast<double> (beatSamples[static_cast<size_t> (beat)]);

    return static_cast<double> (beatSamples[static_cast<size_t> (n - 1)])
           + static_cast<double> (beat - (n - 1)) * spacing;
}

bool LoopManifest::validate (std::string& why) const
{
    auto fail = [&why] (const char* msg)
    {
        why = msg;
        return false;
    };

    if (id.empty())
        return fail ("id is empty");
    if (file.empty())
        return fail ("file is empty");
    if (! (nativeBpm > 30.0f && nativeBpm < 260.0f))
        return fail ("bpm outside 30..260");
    if (bars < 1 || bars > 16)
        return fail ("bars outside 1..16");
    if (meterNumerator < 1 || meterNumerator > 16 || meterDenominator < 1)
        return fail ("meter is not sane");
    if (firstBeatSample < 0)
        return fail ("firstBeatSample is negative");
    if (! (intensity >= 0.0f && intensity <= 1.0f))
        return fail ("intensity outside 0..1");
    if (! (swing >= 0.0f && swing <= 1.0f))
        return fail ("swing outside 0..1");
    if (channels != 1 && channels != 2)
        return fail ("channels must be 1 or 2");

    if (frames > 0)
    {
        // Silence played back is not a loop, and a body that runs past the end
        // of the file is a manifest describing a different export than the one
        // on disk. Both are caught here rather than at the first junction.
        const double end = beatPosition (totalBeats());
        if (end <= beatPosition (0) + 1.0)
            return fail ("the loop body is empty");
        if (end > static_cast<double> (frames) + 1.0)
            return fail ("the loop body runs past the end of the file");
    }

    if (! beatSamples.empty())
    {
        const int want = totalBeats();
        if (static_cast<int> (beatSamples.size()) != want
            && static_cast<int> (beatSamples.size()) != want + 1)
            return fail ("beats must list one marker per quarter, or one more for the end");
        for (size_t i = 1; i < beatSamples.size(); ++i)
            if (beatSamples[i] <= beatSamples[i - 1])
                return fail ("beat markers must increase");
        if (beatSamples.front() != firstBeatSample)
            return fail ("the first beat marker must equal firstBeatSample");
    }

    why.clear();
    return true;
}

bool parseLoopManifest (const std::string& text, LoopManifestFile& out, std::string& error)
{
    out = LoopManifestFile{};
    error.clear();

    std::istringstream in (text);
    std::string raw;
    int lineNo = 0;
    bool inLoop = false;
    LoopManifest cur;

    auto lineError = [&error, &lineNo] (const std::string& msg)
    {
        error = "line " + std::to_string (lineNo) + ": " + msg;
        return false;
    };

    auto commit = [&]() -> bool
    {
        if (! inLoop)
            return true;
        std::string why;
        if (! cur.validate (why))
            return lineError ("loop \"" + cur.id + "\": " + why);
        out.loops.push_back (cur);
        cur = LoopManifest{};
        inLoop = false;
        return true;
    };

    while (std::getline (in, raw))
    {
        ++lineNo;
        // A comment is a whole line. Trailing "#" is left alone: a file name may
        // contain one and silently truncating it is worse than not offering it.
        std::string line = trim (raw);
        if (line.empty() || line[0] == '#')
            continue;

        if (line == "[loop]")
        {
            if (! commit())
                return false;
            inLoop = true;
            cur = LoopManifest{};
            continue;
        }

        std::string key, value;
        splitKey (line, key, value);
        if (value.empty())
            return lineError ("\"" + key + "\" has no value");

        if (! inLoop)
        {
            if (key == "version")
            {
                if (! toInt (value, out.version))
                    return lineError ("version is not a number");
                if (out.version != 1)
                    return lineError ("unsupported manifest version");
            }
            else if (key == "bank")
            {
                out.bankName = value;
            }
            else
            {
                return lineError ("\"" + key + "\" before any [loop]");
            }
            continue;
        }

        if (key == "id")            cur.id = value;
        else if (key == "file")     cur.file = value;
        else if (key == "style")
        {
            if (! parseGrooveStyle (value, cur.style))
                return lineError ("unknown style \"" + value + "\"");
        }
        else if (key == "role")
        {
            if (! parseLoopRole (value, cur.role))
                return lineError ("unknown role \"" + value + "\"");
        }
        else if (key == "stem")
        {
            if (! parseLoopStem (value, cur.stem))
                return lineError ("unknown stem \"" + value + "\"");
        }
        else if (key == "bpm")
        {
            if (! toFloat (value, cur.nativeBpm))
                return lineError ("bpm is not a number");
        }
        else if (key == "bars")
        {
            if (! toInt (value, cur.bars))
                return lineError ("bars is not a number");
        }
        else if (key == "meter")
        {
            if (! toMeter (value, cur.meterNumerator, cur.meterDenominator))
                return lineError ("meter must be written as 4/4");
        }
        else if (key == "firstbeatsample")
        {
            if (! toInt (value, cur.firstBeatSample))
                return lineError ("firstBeatSample is not a number");
        }
        else if (key == "frames")
        {
            if (! toInt (value, cur.frames))
                return lineError ("frames is not a number");
        }
        else if (key == "channels")
        {
            if (! toInt (value, cur.channels))
                return lineError ("channels is not a number");
        }
        else if (key == "samplerate")
        {
            float sr = 48000.0f;
            if (! toFloat (value, sr))
                return lineError ("sampleRate is not a number");
            cur.sampleRate = static_cast<double> (sr);
        }
        else if (key == "intensity")
        {
            if (! toFloat (value, cur.intensity))
                return lineError ("intensity is not a number");
        }
        else if (key == "swing")
        {
            if (! toFloat (value, cur.swing))
                return lineError ("swing is not a number");
        }
        else if (key == "take")
        {
            if (! toInt (value, cur.take))
                return lineError ("take is not a number");
        }
        else if (key == "beats")
        {
            cur.beatSamples.clear();
            std::istringstream vals (value);
            std::string tok;
            while (vals >> tok)
            {
                int v = 0;
                if (! toInt (tok, v))
                    return lineError ("beat marker \"" + tok + "\" is not a number");
                cur.beatSamples.push_back (v);
            }
            if (cur.beatSamples.empty())
                return lineError ("beats listed nothing");
        }
        else
        {
            return lineError ("unknown key \"" + key + "\"");
        }
    }

    if (! commit())
        return false;

    if (out.loops.empty())
    {
        error = "the manifest lists no loops";
        return false;
    }
    return true;
}

std::string writeLoopManifest (const LoopManifestFile& file)
{
    std::ostringstream o;
    o << "# VirtualPerc loop bank - see docs/RECORDED_LOOPS.md\n";
    o << "version " << file.version << "\n";
    if (! file.bankName.empty())
        o << "bank " << file.bankName << "\n";

    char num[64];
    for (const auto& l : file.loops)
    {
        o << "\n[loop]\n";
        o << "id " << l.id << "\n";
        o << "file " << l.file << "\n";
        o << "style " << manifestStyleName (l.style) << "\n";
        o << "role " << toString (l.role) << "\n";
        o << "stem " << toString (l.stem) << "\n";
        std::snprintf (num, sizeof (num), "%.4f", static_cast<double> (l.nativeBpm));
        o << "bpm " << num << "\n";
        o << "bars " << l.bars << "\n";
        o << "meter " << l.meterNumerator << "/" << l.meterDenominator << "\n";
        o << "firstBeatSample " << l.firstBeatSample << "\n";
        if (l.frames > 0)
            o << "frames " << l.frames << "\n";
        o << "channels " << l.channels << "\n";
        std::snprintf (num, sizeof (num), "%.0f", l.sampleRate);
        o << "sampleRate " << num << "\n";
        std::snprintf (num, sizeof (num), "%.3f", static_cast<double> (l.intensity));
        o << "intensity " << num << "\n";
        std::snprintf (num, sizeof (num), "%.3f", static_cast<double> (l.swing));
        o << "swing " << num << "\n";
        o << "take " << l.take << "\n";
        if (! l.beatSamples.empty())
        {
            o << "beats";
            for (int b : l.beatSamples)
                o << " " << b;
            o << "\n";
        }
    }
    return o.str();
}

} // namespace vp
