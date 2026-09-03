// A few bars of the part, as a WAV you can listen to.
//
// Everything else in scripts/ measures. This one only plays: it drives the real
// `PercussionEngine` from a real `TempoFollower` at a fixed tempo - the same two
// objects the app runs, with the same bank and the same groove tables - and
// writes what comes out. No analysis, no microphone, no network: the tempo is
// given, so what lands in the file is the part and nothing else.
//
// It exists because some of what this engine does is a musical decision rather
// than a measurable one, and the only review a musical decision can have is
// somebody hearing it. "The congas never play the first quarter's down-stroke"
// is exactly that kind of change: `--click` puts a quiet woodblock on each
// quarter, loud on the one, so the claim can be checked by ear instead of by
// reading the tables.
//
//   cmake --build build --target VPRender
//   ./build/VPRender_artefacts/Release/VPRender --style dance --bpm 124 \
//       --bars 8 --click --out dance.wav
#include "Percussion/PercussionEngine.h"
#include "Tracking/TempoFollower.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 256;

vp::GrooveStyle styleFromName (const std::string& name, bool& ok)
{
    ok = true;
    for (int i = 0; i < static_cast<int> (vp::GrooveStyle::count); ++i)
    {
        const auto s = static_cast<vp::GrooveStyle> (i);
        std::string label = vp::toString (s);
        std::string want = name;
        for (auto& c : label) c = static_cast<char> (std::tolower (c));
        for (auto& c : want)  c = static_cast<char> (std::tolower (c));
        if (label == want)
            return s;
    }
    ok = false;
    return vp::GrooveStyle::marcha;
}

/** A quarter-note woodblock, loud on the one. Deliberately not part of the
    engine: it is a ruler laid over the recording so a listener can hear where
    the bar is, and it must not be mistaken for something the app plays. */
void addClick (std::vector<float>& l, std::vector<float>& r, int at, bool downbeat)
{
    const double f = downbeat ? 1500.0 : 1000.0;
    const double decay = downbeat ? 90.0 : 130.0;
    const float gain = downbeat ? 0.22f : 0.10f;
    const int n = static_cast<int> (kSr * 0.05);
    for (int i = 0; i < n; ++i)
    {
        const int p = at + i;
        if (p < 0 || p >= static_cast<int> (l.size()))
            break;
        const double t = static_cast<double> (i) / kSr;
        const float v = gain * static_cast<float> (std::sin (2.0 * 3.14159265358979 * f * t)
                                                   * std::exp (-t * decay));
        l[static_cast<size_t> (p)] += v;
        r[static_cast<size_t> (p)] += v;
    }
}
}

int main (int argc, char** argv)
{
    std::string styleName = "dance";
    std::string out = "groove.wav";
    double bpm = 124.0;
    int bars = 8;
    float humanize = 0.35f, swing = 0.0f, intensity = 0.5f;
    float mix = 0.5f, reverb = 0.30f;
    bool click = false;
    bool congas = true, shaker = true;
    float dynamics = 1.0f;
    bool arc = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--style")          styleName = next();
        else if (a == "--out")       out = next();
        else if (a == "--bpm")       bpm = std::stod (next());
        else if (a == "--bars")      bars = std::stoi (next());
        else if (a == "--humanize")  humanize = std::stof (next());
        else if (a == "--swing")     swing = std::stof (next());
        else if (a == "--intensity") intensity = std::stof (next());
        else if (a == "--mix")       mix = std::stof (next());
        else if (a == "--reverb")    reverb = std::stof (next());
        else if (a == "--click")     click = true;
        else if (a == "--dynamics")  dynamics = std::stof (next());
        else if (a == "--arc")       arc = true;
        else if (a == "--no-congas") congas = false;
        else if (a == "--no-shaker") shaker = false;
        else
        {
            std::printf ("uso: VPRender [--style marcha|rock|dance|pop|samba|funk|reggae|bossa]\n"
                         "              [--bpm 124] [--bars 8] [--out file.wav] [--click]\n"
                         "              [--humanize 0.35] [--swing 0] [--intensity 0.5]\n"
                         "              [--mix 0.5] [--reverb 0.30] [--no-congas] [--no-shaker]\n"
                         "              [--dynamics 1.0] [--arc]\n");
            return 1;
        }
    }

    bool ok = false;
    const vp::GrooveStyle style = styleFromName (styleName, ok);
    if (! ok)
    {
        std::printf ("stile sconosciuto: %s\n", styleName.c_str());
        return 1;
    }

    vp::PercussionEngine perc;
    perc.prepare (kSr);
    perc.setSeed (0x5EED17u);
    perc.setGrooveStyle (style);
    perc.setGroove (static_cast<float> (bpm), 4);
    perc.setSubdivision (vp::Subdivision::eighth);
    perc.setHumanization (humanize);
    perc.setSwing (swing);
    perc.setIntensity (intensity);
    perc.setInstrumentMix (mix);
    perc.setDynamics (dynamics);
    perc.setReverbAmount (reverb);
    perc.setCongasEnabled (congas);
    perc.setShakerEnabled (shaker);
    perc.setEnabled (congas || shaker);
    perc.setVolume (0.90f);

    vp::TempoFollower clock;
    clock.prepare (kSr);
    clock.setPulsesPerBeat (4);
    clock.forceTempo (static_cast<float> (bpm));
    clock.setTargetTempo (static_cast<float> (bpm), 1.0f);
    clock.setLocked (true);
    // Places the pulse sitting on the phase it starts from, so the very first
    // downbeat of the file is played rather than stepped over.
    clock.resetClock();

    // A whole bar of run-up, not a beat of it: the clock counts through the
    // run-up as well, so the part has to be let in on a boundary the count
    // agrees with or the file opens in the middle of a bar. Then a beat of tail
    // so the last stroke's ring is not cut off at the end.
    const double beatSec = 60.0 / bpm;
    const int lead = static_cast<int> (kSr * beatSec * 4.0);
    const int body = static_cast<int> (kSr * beatSec * 4.0 * bars);
    const int tail = static_cast<int> (kSr * beatSec);
    const int total = lead + body + tail;

    std::vector<float> L (static_cast<size_t> (total), 0.0f);
    std::vector<float> R (static_cast<size_t> (total), 0.0f);
    std::vector<float> bl (kBlock), br (kBlock);

    int hits = 0;
    for (int pos = 0; pos < total; pos += kBlock)
    {
        const int n = std::min (kBlock, total - pos);
        const vp::ClockTick tick = clock.advance (n);
        // Silent through the run-up: the clock is already counting, so the part
        // comes in on a downbeat instead of wherever the file happens to start.
        const bool audible = pos >= lead;
        // `--arc` walks the dynamics down and back over the take, so what the
        // part does as a band comes off and returns can be heard in one file
        // rather than inferred from three. The engine gets this from
        // BandDynamics; here it is simply swept, because this program is about
        // the part and not about the listening.
        if (arc && audible)
        {
            const double through = static_cast<double> (pos - lead)
                                   / static_cast<double> (std::max (1, body));
            const double u = 1.0 - std::cos (2.0 * 3.14159265358979 * through);
            perc.setDynamics (static_cast<float> (0.06 + 0.94 * (u * 0.5)));
        }
        perc.render (bl.data(), br.data(), n, tick, audible);
        std::memcpy (L.data() + pos, bl.data(), sizeof (float) * static_cast<size_t> (n));
        std::memcpy (R.data() + pos, br.data(), sizeof (float) * static_cast<size_t> (n));

        if (click && audible)
            for (int i = 0; i < tick.pulsesFired; ++i)
                if (tick.pulseIndex[i] == 0)
                    addClick (L, R, pos + tick.pulseOffset[i], tick.pulseBeatInBar[i] == 0);
    }
    hits = perc.hitsFired();

    // Peak-normalise to a comfortable level. The engine's own gain staging is
    // set for a stage, not for a file somebody opens on a phone.
    float peak = 1.0e-6f;
    for (int i = 0; i < total; ++i)
        peak = std::max (peak, std::max (std::fabs (L[static_cast<size_t> (i)]),
                                         std::fabs (R[static_cast<size_t> (i)])));
    const float g = 0.89f / peak;
    for (int i = 0; i < total; ++i)
    {
        L[static_cast<size_t> (i)] *= g;
        R[static_cast<size_t> (i)] *= g;
    }

    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (out));
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
    {
        std::printf ("non riesco a scrivere %s\n", file.getFullPathName().toRawUTF8());
        return 1;
    }
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.release(), kSr, 2, 16, {}, 0));
    if (writer == nullptr)
    {
        std::printf ("non riesco a creare il writer\n");
        return 1;
    }
    const float* chans[2] = { L.data(), R.data() };
    writer->writeFromFloatArrays (chans, 2, total);
    writer.reset();

    std::printf ("%s  %s  %.0f BPM  %d battute  %.1f s  %d colpi  "
                 "(recorded %d/%d, accordatura x%.3f)\n",
                 file.getFileName().toRawUTF8(), vp::toString (style), bpm, bars,
                 static_cast<double> (total) / kSr, hits,
                 perc.recordedStrokeCount(), static_cast<int> (vp::Stroke::count),
                 static_cast<double> (vp::PercussionEngine::drumTuneRatio()));
    return 0;
}
