#include "TestLoops.h"

#include "Loops/HybridPercussionRenderer.h"
#include "Loops/LoopBank.h"
#include "Loops/LoopManifest.h"
#include "Loops/LoopPlayer.h"
#include "Loops/WavFile.h"
#include "Tracking/TempoFollower.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Allocation watch
//
// The one property of the audio callback that cannot be checked by reading the
// code with any confidence: a std::vector that grows once every few minutes
// looks exactly like one that never does. Replacing the global operators is
// heavy-handed, and it is the only thing that actually answers the question.
// ---------------------------------------------------------------------------
namespace
{
    std::atomic<long long> gAllocations { 0 };
    std::atomic<bool> gWatching { false };
}

void vpBeginAllocationWatch() noexcept
{
    gAllocations.store (0, std::memory_order_relaxed);
    gWatching.store (true, std::memory_order_relaxed);
}

long long vpEndAllocationWatch() noexcept
{
    gWatching.store (false, std::memory_order_relaxed);
    return gAllocations.load (std::memory_order_relaxed);
}

namespace
{
    inline void noteAllocation() noexcept
    {
        if (gWatching.load (std::memory_order_relaxed))
            gAllocations.fetch_add (1, std::memory_order_relaxed);
    }

    inline void* countedMalloc (std::size_t n)
    {
        noteAllocation();
        void* p = std::malloc (n != 0 ? n : 1);
        if (p == nullptr)
            throw std::bad_alloc();
        return p;
    }

    inline void* countedAligned (std::size_t n, std::size_t align)
    {
        noteAllocation();
        if (align < sizeof (void*))
            align = sizeof (void*);
        void* p = nullptr;
        if (posix_memalign (&p, align, n != 0 ? n : 1) != 0)
            throw std::bad_alloc();
        return p;
    }
}

void* operator new (std::size_t n) { return countedMalloc (n); }
void* operator new[] (std::size_t n) { return countedMalloc (n); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept
{
    noteAllocation();
    return std::malloc (n != 0 ? n : 1);
}
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept
{
    noteAllocation();
    return std::malloc (n != 0 ? n : 1);
}
void* operator new (std::size_t n, std::align_val_t a) { return countedAligned (n, static_cast<std::size_t> (a)); }
void* operator new[] (std::size_t n, std::align_val_t a) { return countedAligned (n, static_cast<std::size_t> (a)); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }

// ---------------------------------------------------------------------------

namespace
{
    int gPassed = 0;
    int gFailed = 0;

    void expect (bool cond, const char* name)
    {
        if (cond)
        {
            ++gPassed;
            std::printf ("  PASS  %s\n", name);
        }
        else
        {
            ++gFailed;
            std::printf ("  FAIL  %s\n", name);
        }
    }

    constexpr double kSr = 48000.0;

    // --- synthetic material ------------------------------------------------

    /** A loop that is a run of percussive clicks, one on each quarter, the
        first of them twice as loud. Perfectly cut: the body is exactly
        `beats * beatFrames` and the last click has decayed before it ends. */
    void makeClickLoop (std::vector<float>& l, std::vector<float>& r,
                        int beats, int beatFrames)
    {
        const int n = beats * beatFrames;
        l.assign (static_cast<size_t> (n), 0.0f);
        for (int b = 0; b < beats; ++b)
        {
            const float amp = (b % 4 == 0) ? 0.9f : 0.45f;
            for (int i = 0; i < 400; ++i)
            {
                const double t = static_cast<double> (i) / kSr;
                const double env = std::exp (-t * 320.0);
                const double s = std::sin (6.283185307179586 * 1600.0 * t);
                l[static_cast<size_t> (b * beatFrames + i)] += static_cast<float> (amp * env * s);
            }
        }
        r = l;
    }

    /** The same, with a stroke on every eighth: the beat and the "&". What the
        swing warp is measured on - there is nothing to measure in a part whose
        strokes are all on the quarter. */
    void makeEighthLoop (std::vector<float>& l, std::vector<float>& r,
                         int beats, int beatFrames)
    {
        const int n = beats * beatFrames;
        l.assign (static_cast<size_t> (n), 0.0f);
        for (int e = 0; e < beats * 2; ++e)
        {
            const float amp = (e % 2 == 0) ? 0.9f : 0.6f;
            const int at = (e * beatFrames) / 2;
            for (int i = 0; i < 300; ++i)
            {
                const double t = static_cast<double> (i) / kSr;
                const double env = std::exp (-t * 420.0);
                const double sig = std::sin (6.283185307179586 * 1900.0 * t);
                l[static_cast<size_t> (at + i)] += static_cast<float> (amp * env * sig);
            }
        }
        r = l;
    }

    /** A loop that is one smooth tone, at a frequency chosen so that a whole
        number of cycles fits the body. Wrapping it is continuous to the sample,
        so any step in the output is the player's doing and nothing else's. */
    void makeToneLoop (std::vector<float>& l, std::vector<float>& r,
                       int beats, int beatFrames)
    {
        const int n = beats * beatFrames;
        l.assign (static_cast<size_t> (n), 0.0f);
        const int cycles = 1600;                     // 400 Hz over 4 s at 48 kHz
        for (int i = 0; i < n; ++i)
            l[static_cast<size_t> (i)] = 0.5f * static_cast<float> (
                std::sin (6.283185307179586 * static_cast<double> (cycles)
                          * static_cast<double> (i) / static_cast<double> (n)));
        r = l;
    }

    vp::LoopManifest clickManifest (const char* id, float bpm, int beats,
                                    vp::LoopRole role, vp::LoopStem stem,
                                    float swing, float intensity, int take)
    {
        vp::LoopManifest m;
        m.id = id;
        m.file = std::string (id) + ".wav";
        m.style = vp::GrooveStyle::dance;
        m.role = role;
        m.stem = stem;
        m.nativeBpm = bpm;
        m.bars = beats / 4;
        m.meterNumerator = 4;
        m.meterDenominator = 4;
        m.firstBeatSample = 0;
        m.channels = 2;
        m.sampleRate = kSr;
        m.swing = swing;
        m.intensity = intensity;
        m.take = take;
        return m;
    }

    int beatFramesFor (float bpm)
    {
        return static_cast<int> (std::lround (60.0 / static_cast<double> (bpm) * kSr));
    }

    void addClickLoop (vp::LoopBank& bank, const char* id, float bpm, int beats,
                       vp::LoopRole role, vp::LoopStem stem,
                       float swing = 0.0f, float intensity = 0.5f, int take = 1)
    {
        std::vector<float> l, r;
        makeClickLoop (l, r, beats, beatFramesFor (bpm));
        bank.addEntry (clickManifest (id, bpm, beats, role, stem, swing, intensity, take),
                       std::move (l), std::move (r));
    }

    // --- measuring ---------------------------------------------------------

    /** Where the strokes are in a rendered part: local maxima of the rectified
        signal, above a fraction of the loudest one, no two closer than 80 ms.
        Crude on purpose - a stroke that this misses is a stroke a listener
        would have trouble hearing too. */
    std::vector<int> findOnsets (const std::vector<float>& x, double threshold = 0.25)
    {
        std::vector<int> out;
        float peak = 0.0f;
        for (float v : x)
            peak = std::max (peak, std::fabs (v));
        if (peak < 1.0e-6f)
            return out;

        const float gate = static_cast<float> (threshold) * peak;
        const int refractory = static_cast<int> (kSr * 0.08);
        int i = 0;
        const int n = static_cast<int> (x.size());
        while (i < n)
        {
            if (std::fabs (x[static_cast<size_t> (i)]) < gate)
            {
                ++i;
                continue;
            }
            int best = i;
            const int end = std::min (n, i + refractory);
            for (int j = i; j < end; ++j)
                if (std::fabs (x[static_cast<size_t> (j)]) > std::fabs (x[static_cast<size_t> (best)]))
                    best = j;
            out.push_back (best);
            i = best + refractory;
        }
        return out;
    }

    float rms (const std::vector<float>& x, int from = 0, int to = -1)
    {
        if (to < 0)
            to = static_cast<int> (x.size());
        double s = 0.0;
        int n = 0;
        for (int i = std::max (0, from); i < to && i < static_cast<int> (x.size()); ++i, ++n)
            s += static_cast<double> (x[static_cast<size_t> (i)]) * static_cast<double> (x[static_cast<size_t> (i)]);
        return n > 0 ? static_cast<float> (std::sqrt (s / static_cast<double> (n))) : 0.0f;
    }

    /** Drives a clock and a player together and returns everything rendered.
        `bpmAt` lets a test move the tempo; `onBlock` is where a test pokes the
        player mid-run. */
    struct RunResult
    {
        std::vector<float> left, right;
        std::vector<int>   quarterOffsets;   // absolute sample of every quarter
        int blocks = 0;
    };

    template <typename OnBlock>
    RunResult runPlayer (vp::LoopPlayer& player, int block, double seconds,
                         float bpm, OnBlock&& onBlock)
    {
        RunResult out;
        vp::TempoFollower clock;
        clock.prepare (kSr);
        clock.setPulsesPerBeat (4);
        clock.forceTempo (bpm);
        clock.setTargetTempo (bpm, 1.0f);
        clock.setLocked (true);
        clock.snapPhase (0.0f);

        const int total = static_cast<int> (kSr * seconds);
        std::vector<float> l (static_cast<size_t> (block), 0.0f);
        std::vector<float> r (static_cast<size_t> (block), 0.0f);

        int pos = 0;
        while (pos + block <= total)
        {
            const float want = onBlock (out.blocks, player, clock);
            if (want > 0.0f)
                clock.setTargetTempo (want, 1.0f);
            const auto tick = clock.advance (block);
            for (int p = 0; p < tick.pulsesFired; ++p)
                if (tick.pulseIndex[p] == 0)
                    out.quarterOffsets.push_back (pos + tick.pulseOffset[p]);

            player.process (l.data(), r.data(), block, tick);
            out.left.insert (out.left.end(), l.begin(), l.end());
            out.right.insert (out.right.end(), r.begin(), r.end());
            pos += block;
            ++out.blocks;
        }
        return out;
    }

    /** The smallest gap between each onset and any quarter, in milliseconds. */
    float worstOnsetError (const std::vector<int>& onsets, const std::vector<int>& quarters,
                           int ignoreBefore)
    {
        float worst = 0.0f;
        for (int o : onsets)
        {
            if (o < ignoreBefore)
                continue;
            int best = 1 << 30;
            for (int q : quarters)
                best = std::min (best, std::abs (o - q));
            worst = std::max (worst, static_cast<float> (best) / static_cast<float> (kSr) * 1000.0f);
        }
        return worst;
    }
} // namespace

// ---------------------------------------------------------------------------

void vpRunLoopTests (int& passed, int& failed)
{
    gPassed = 0;
    gFailed = 0;
    std::printf ("\nRecorded loops — manifest / bank / player / hybrid\n");
    std::printf ("  stretcher backend: %s\n",
                 vp::LoopStretcher::isSignalsmith() ? "Signalsmith" : "built-in WSOLA");

    // --- 1. the manifest ---------------------------------------------------
    {
        const char* text =
            "version 1\n"
            "bank dance\n"
            "\n"
            "[loop]\n"
            "id dance_a_120_congas_t1\n"
            "file dance_a_120_congas_t1.wav\n"
            "style dance\n"
            "role grooveA\n"
            "stem congas\n"
            "bpm 120\n"
            "bars 2\n"
            "meter 4/4\n"
            "firstBeatSample 0\n"
            "frames 192000\n"
            "channels 2\n"
            "sampleRate 48000\n"
            "intensity 0.5\n"
            "swing 0.0\n"
            "take 1\n"
            "beats 0 24000 48000 72000 96000 120000 144000 168000\n";

        vp::LoopManifestFile f;
        std::string err;
        const bool ok = vp::parseLoopManifest (text, f, err);
        expect (ok, "manifest: a well formed bank parses");
        expect (ok && f.loops.size() == 1 && f.loops[0].totalBeats() == 8,
                "manifest: two bars of 4/4 are eight quarters");
        expect (ok && std::fabs (f.loops[0].bodyFrames() - 192000.0) < 1.0,
                "manifest: the body runs from the first quarter to the same quarter a pass later");

        // And the same file back out of the writer parses to the same thing.
        vp::LoopManifestFile again;
        std::string err2;
        const bool round = ok && vp::parseLoopManifest (vp::writeLoopManifest (f), again, err2);
        expect (round && again.loops.size() == f.loops.size()
                && again.loops[0].id == f.loops[0].id
                && again.loops[0].beatSamples == f.loops[0].beatSamples,
                "manifest: written and read back is the same bank");

        // Refusals. Each of these is a way a hand-written bank goes wrong, and
        // each of them is silent damage if it is not caught here.
        auto refuses = [] (const std::string& body)
        {
            vp::LoopManifestFile m;
            std::string e;
            return ! vp::parseLoopManifest (body, m, e);
        };
        std::string base = text;
        expect (refuses (base + "\n[loop]\nid x\nfile x.wav\nbpm 400\nbars 1\n"),
                "manifest: a tempo nobody plays at is refused");
        expect (refuses ("version 1\n[loop]\nid x\nfile x.wav\nbpm 120\nbars 1\n"
                         "beats 0 24000 12000 36000\n"),
                "manifest: beat markers that go backwards are refused");
        expect (refuses ("version 2\nbank x\n[loop]\nid x\nfile x.wav\n"),
                "manifest: a version this build does not know is refused");
        expect (refuses ("version 1\n[loop]\nid x\nfile x.wav\nwibble 3\n"),
                "manifest: a key nobody recognises is refused rather than ignored");
    }

    // --- 2. the WAV reader -------------------------------------------------
    {
        // A 16-bit stereo file built by hand, decoded back.
        const int frames = 512;
        std::vector<unsigned char> wav;
        auto put32 = [&wav] (uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                wav.push_back (static_cast<unsigned char> ((v >> (8 * i)) & 0xFFu));
        };
        auto put16 = [&wav] (uint16_t v)
        {
            wav.push_back (static_cast<unsigned char> (v & 0xFFu));
            wav.push_back (static_cast<unsigned char> ((v >> 8) & 0xFFu));
        };
        auto tag = [&wav] (const char* t) { for (int i = 0; i < 4; ++i) wav.push_back (static_cast<unsigned char> (t[i])); };

        const uint32_t dataBytes = static_cast<uint32_t> (frames) * 2u * 2u;
        tag ("RIFF"); put32 (36u + dataBytes); tag ("WAVE");
        tag ("fmt "); put32 (16u); put16 (1); put16 (2); put32 (48000u);
        put32 (48000u * 4u); put16 (4); put16 (16);
        tag ("data"); put32 (dataBytes);
        for (int i = 0; i < frames; ++i)
        {
            const int16_t a = static_cast<int16_t> (i * 32);
            put16 (static_cast<uint16_t> (a));
            put16 (static_cast<uint16_t> (-a));
        }

        vp::WavAudio audio;
        std::string err;
        const bool ok = vp::decodeWav (wav.data(), wav.size(), audio, err);
        expect (ok && audio.frames == frames && audio.channels == 2
                    && std::fabs (audio.sampleRate - 48000.0) < 1.0,
                "wav: a 16 bit stereo file decodes to what was written");
        expect (ok && std::fabs (audio.left[100] - 3200.0f / 32768.0f) < 1.0e-4f
                    && std::fabs (audio.right[100] + 3200.0f / 32768.0f) < 1.0e-4f,
                "wav: the two channels come back the right way round");

        std::vector<unsigned char> broken (wav.begin(), wav.begin() + 40);
        vp::WavAudio bad;
        expect (! vp::decodeWav (broken.data(), broken.size(), bad, err),
                "wav: a truncated file is refused, not half decoded");
    }

    // --- 3. choosing a recording ------------------------------------------
    {
        vp::LoopBank bank;
        addClickLoop (bank, "a100", 100.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::congas);
        addClickLoop (bank, "a120", 120.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::congas);
        addClickLoop (bank, "a140", 140.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::congas);
        addClickLoop (bank, "a120sw", 120.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::congas, 0.9f);
        addClickLoop (bank, "sh120", 120.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::shaker);

        vp::LoopQuery q;
        q.style = vp::GrooveStyle::dance;
        q.role = vp::LoopRole::grooveA;
        q.stem = vp::LoopStem::congas;
        q.bpm = 126.0f;
        auto sel = bank.select (q);
        expect (sel.ok() && bank.entry (sel.index).manifest.id == "a120",
                "bank: the recording with the nearest native tempo is the one chosen");

        q.bpm = 138.0f;
        sel = bank.select (q);
        expect (sel.ok() && bank.entry (sel.index).manifest.id == "a140",
                "bank: and it changes over when another recording is nearer");

        q.bpm = 200.0f;
        sel = bank.select (q);
        expect (! sel.ok() && sel.miss == vp::LoopMissReason::tempoTooFar,
                "bank: a tempo past the stretch limit is a miss, not a recording stretched to death");

        q.bpm = 120.0f;
        q.swing = 0.45f;
        sel = bank.select (q);
        expect (! sel.ok() && sel.miss == vp::LoopMissReason::swingTooFar,
                "bank: a swing no recording is near is a miss - the part goes back to single strokes");

        q.swing = 0.85f;
        sel = bank.select (q);
        expect (sel.ok() && bank.entry (sel.index).manifest.id == "a120sw",
                "bank: the swung take is chosen for a swung song");

        q.swing = 0.0f;
        q.stem = vp::LoopStem::shaker;
        sel = bank.select (q);
        expect (sel.ok() && bank.entry (sel.index).manifest.id == "sh120",
                "bank: congas and shaker are separate and stay separate");

        q.stem = vp::LoopStem::congas;
        q.role = vp::LoopRole::fill;
        sel = bank.select (q);
        expect (! sel.ok() && sel.miss == vp::LoopMissReason::noSuchPart,
                "bank: a part the library does not have says so");
    }

    // --- 3b. the stretcher's own latency -----------------------------------
    {
        // The number the player reads its source ahead by. It has to be the same
        // whatever block size the device happens to open at, or the part is late
        // by a stretcher block on a 4096-frame buffer and on time on a 128-frame
        // one - which is what happened, and what this now stops happening.
        float leads[3] = { 0.0f, 0.0f, 0.0f };
        const int blocks[3] = { 128, 512, 4096 };
        for (int i = 0; i < 3; ++i)
        {
            vp::LoopStretcher st;
            st.prepare (kSr, blocks[i], 2.0f);
            leads[i] = static_cast<float> (st.inputLeadFrames (1.0));
        }
        std::printf ("        measured lead at 128 / 512 / 4096: %.0f / %.0f / %.0f frames\n",
                     static_cast<double> (leads[0]), static_cast<double> (leads[1]),
                     static_cast<double> (leads[2]));
        const float worst = std::max ({ leads[0], leads[1], leads[2] })
                            - std::min ({ leads[0], leads[1], leads[2] });
        expect (worst < static_cast<float> (kSr) * 0.002f,
                "stretcher: the measured lead is the same at every buffer size");
        expect (leads[0] > 0.0f,
                "stretcher: and it is a real number, not a backend that measured nothing");
    }

    // --- 4. coming in on the beat -----------------------------------------
    {
        vp::LoopBank bank;
        addClickLoop (bank, "a120", 120.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::congas);

        vp::LoopPlayer player;
        player.prepare (kSr, 256);
        player.setBank (&bank);
        player.setCorrection (0.3f, 0.05f);

        vp::LoopQuery q;
        q.style = vp::GrooveStyle::dance;
        q.role = vp::LoopRole::grooveA;
        q.stem = vp::LoopStem::congas;
        q.bpm = 120.0f;
        player.request (q);
        player.start();

        auto run = runPlayer (player, 256, 8.0, 120.0f,
                              [] (int, vp::LoopPlayer&, vp::TempoFollower&) { return 0.0f; });

        const auto onsets = findOnsets (run.left);
        expect (! onsets.empty(), "player: the recording actually sounds");

        // Nothing before the first quarter, and the first stroke on it.
        const int firstQuarter = run.quarterOffsets.empty() ? 0 : run.quarterOffsets.front();
        float beforeEnergy = 0.0f;
        for (int i = 0; i < std::min (firstQuarter, static_cast<int> (run.left.size())); ++i)
            beforeEnergy = std::max (beforeEnergy, std::fabs (run.left[static_cast<size_t> (i)]));
        expect (beforeEnergy < 0.02f,
                "player: nothing sounds before the quarter the part comes in on");

        const float worst = worstOnsetError (onsets, run.quarterOffsets,
                                             static_cast<int> (kSr * 0.5));
        std::printf ("        worst stroke-to-quarter error: %.2f ms\n",
                     static_cast<double> (worst));
        expect (worst < 5.0f,
                "player: every stroke lands within 5 ms of the quarter the clock put it on");
        std::printf ("        steady-state phase error: %.2f ms\n",
                     static_cast<double> (player.phaseErrorMs()));
        expect (std::fabs (player.phaseErrorMs()) < 5.0f,
                "player: and the read position is inside 5 ms of where the map says");
    }

    // --- 5. the junction ---------------------------------------------------
    {
        vp::LoopBank bank;
        const int beatFrames = beatFramesFor (120.0f);
        std::vector<float> l, r;
        makeToneLoop (l, r, 8, beatFrames);
        bank.addEntry (clickManifest ("tone120", 120.0f, 8, vp::LoopRole::grooveA,
                                      vp::LoopStem::congas, 0.0f, 0.5f, 1),
                       std::move (l), std::move (r));

        vp::LoopPlayer player;
        player.prepare (kSr, 256);
        player.setBank (&bank);

        vp::LoopQuery q;
        q.bpm = 120.0f;
        player.request (q);
        player.start();

        auto run = runPlayer (player, 256, 14.0, 120.0f,
                              [] (int, vp::LoopPlayer&, vp::TempoFollower&) { return 0.0f; });

        expect (player.passes() >= 2, "player: the run went round the loop more than once");

        // A 400 Hz tone at 48 kHz moves at most about 0.033 per sample. Anything
        // several times that is a step, and a step is the click this test exists
        // to catch. Measured after the entry fade.
        float worstStep = 0.0f;
        const int from = static_cast<int> (kSr * 1.0);
        for (int i = from + 1; i < static_cast<int> (run.left.size()); ++i)
            worstStep = std::max (worstStep, std::fabs (run.left[static_cast<size_t> (i)]
                                                        - run.left[static_cast<size_t> (i - 1)]));
        std::printf ("        worst sample step across %d passes: %.4f\n",
                     player.passes(), static_cast<double> (worstStep));
        expect (worstStep < 0.12f,
                "player: closing the loop leaves no step in the waveform - no click at the junction");
    }

    // --- 6. no allocation in the callback ----------------------------------
    {
        vp::LoopBank bank;
        addClickLoop (bank, "a120", 120.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::congas);
        addClickLoop (bank, "b120", 120.0f, 8, vp::LoopRole::grooveB, vp::LoopStem::congas);

        vp::LoopPlayer player;
        player.prepare (kSr, 512);
        player.setBank (&bank);

        vp::LoopQuery q;
        q.bpm = 120.0f;
        player.request (q);
        player.start();

        vp::TempoFollower clock;
        clock.prepare (kSr);
        clock.setPulsesPerBeat (4);
        clock.forceTempo (120.0f);
        clock.setTargetTempo (120.0f, 1.0f);
        clock.setLocked (true);
        clock.snapPhase (0.0f);

        std::vector<float> l (512, 0.0f), rr (512, 0.0f);

        // Warm every path first: entry, a pass boundary, and a change of
        // recording. Then arm the watch and do all three again.
        for (int i = 0; i < 600; ++i)
        {
            if (i == 300)
            {
                q.role = vp::LoopRole::grooveB;
                player.request (q);
            }
            const auto tick = clock.advance (512);
            player.process (l.data(), rr.data(), 512, tick);
        }

        vpBeginAllocationWatch();
        for (int i = 0; i < 900; ++i)
        {
            if (i == 100)
            {
                q.role = vp::LoopRole::grooveA;
                player.request (q);
            }
            if (i == 500)
            {
                q.role = vp::LoopRole::grooveB;
                player.request (q);
            }
            const auto tick = clock.advance (512);
            player.process (l.data(), rr.data(), 512, tick);
        }
        const long long allocs = vpEndAllocationWatch();
        std::printf ("        allocations in 900 callbacks, two loop changes: %lld\n", allocs);
        expect (allocs == 0, "player: the audio callback allocates nothing, changes included");
    }

    // --- 7. the same part at every buffer size ------------------------------
    {
        vp::LoopBank bank;
        addClickLoop (bank, "a120", 120.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::congas);

        // Compared over a window that every buffer size renders in full, and
        // compared for the two things that are musical: where the strokes are,
        // and how loud the part is. Sample-identical output is not the claim and
        // could not be - a stretcher fed 4096 frames at a time makes different
        // internal blocking decisions than one fed 128 - but a part that is on
        // the same quarters at the same level is the same part.
        constexpr int kFrom = static_cast<int> (kSr * 2.0);
        constexpr int kTo   = static_cast<int> (kSr * 8.0);

        struct Rendered { int strokes; float worstMs; float level; };
        auto renderAt = [&bank] (int block) -> Rendered
        {
            vp::LoopPlayer player;
            player.prepare (kSr, block);
            player.setBank (&bank);
            vp::LoopQuery q;
            q.bpm = 120.0f;
            player.request (q);
            player.start();
            auto run = runPlayer (player, block, 10.0, 120.0f,
                                  [] (int, vp::LoopPlayer&, vp::TempoFollower&) { return 0.0f; });

            const auto all = findOnsets (run.left);
            Rendered out { 0, 0.0f, rms (run.left, kFrom, kTo) };
            std::vector<int> inWindow;
            for (int o : all)
                if (o >= kFrom && o < kTo)
                    inWindow.push_back (o);
            out.strokes = static_cast<int> (inWindow.size());
            out.worstMs = worstOnsetError (inWindow, run.quarterOffsets, kFrom);
            return out;
        };

        const Rendered small = renderAt (128);
        const Rendered mid = renderAt (512);
        const Rendered big = renderAt (4096);

        std::printf ("        128 / 512 / 4096: %d / %d / %d strokes, "
                     "worst %.2f / %.2f / %.2f ms, level %.4f / %.4f / %.4f\n",
                     small.strokes, mid.strokes, big.strokes,
                     static_cast<double> (small.worstMs), static_cast<double> (mid.worstMs),
                     static_cast<double> (big.worstMs),
                     static_cast<double> (small.level), static_cast<double> (mid.level),
                     static_cast<double> (big.level));

        expect (small.strokes == mid.strokes && mid.strokes == big.strokes,
                "player: 128, 512 and 4096 sample buffers play the same number of strokes");
        expect (small.worstMs < 5.0f && mid.worstMs < 5.0f && big.worstMs < 5.0f,
                "player: and every one of them is on its quarter at every buffer size");
        const float loudest = std::max ({ small.level, mid.level, big.level });
        const float quietest = std::min ({ small.level, mid.level, big.level });
        expect (loudest - quietest < 0.08f * loudest,
                "player: and the part is the same level at every buffer size");
    }

    // --- 7b. swing, taken inside the beat ----------------------------------
    {
        // A straight recording, asked for a little swing. The off-eighth has to
        // move; the quarters must not. Past the bank's tolerance the answer is a
        // miss, and the part goes back to single strokes rather than a swung
        // pattern played on top of a straight recording.
        vp::LoopBank bank;
        const int beatFrames = beatFramesFor (120.0f);
        std::vector<float> l, r;
        makeEighthLoop (l, r, 8, beatFrames);
        bank.addEntry (clickManifest ("straight120", 120.0f, 8, vp::LoopRole::grooveA,
                                      vp::LoopStem::congas, 0.0f, 0.5f, 1),
                       std::move (l), std::move (r));

        auto offEighthMs = [&bank] (float swing) -> float
        {
            vp::LoopPlayer player;
            player.prepare (kSr, 256);
            player.setBank (&bank);
            player.setCorrection (0.25f, 0.06f);
            vp::LoopQuery q;
            q.bpm = 120.0f;
            q.swing = swing;
            player.request (q);
            player.start();
            auto run = runPlayer (player, 256, 10.0, 120.0f,
                                  [] (int, vp::LoopPlayer&, vp::TempoFollower&) { return 0.0f; });

            // Every stroke that is not on a quarter, timed from the quarter
            // before it. Averaged, because one is noise.
            const auto onsets = findOnsets (run.left, 0.2);
            double sum = 0.0;
            int n = 0;
            for (int o : onsets)
            {
                if (o < static_cast<int> (kSr * 2.0))
                    continue;
                int prev = -1;
                for (int q2 : run.quarterOffsets)
                    if (q2 <= o && (prev < 0 || q2 > prev))
                        prev = q2;
                if (prev < 0)
                    continue;
                const double ms = static_cast<double> (o - prev) / kSr * 1000.0;
                if (ms > 120.0 && ms < 400.0)   // the "&", not the quarter itself
                {
                    sum += ms;
                    ++n;
                }
            }
            return n > 0 ? static_cast<float> (sum / n) : -1.0f;
        };

        const float straight = offEighthMs (0.0f);
        const float leaning = offEighthMs (0.15f);
        std::printf ("        off-eighth at swing 0.00 / 0.15: %.1f ms / %.1f ms "
                     "(a quarter is 500 ms)\n",
                     static_cast<double> (straight), static_cast<double> (leaning));
        expect (straight > 240.0f && straight < 260.0f,
                "swing: a straight recording plays its off-eighth halfway through the beat");
        // 0.15 of the way to a triplet is 0.15/6 of a beat = 12.5 ms at 120.
        expect (leaning - straight > 6.0f && leaning - straight < 20.0f,
                "swing: a little swing moves the off-eighth later by the amount asked for");

        // And the refusal.
        vp::LoopQuery far;
        far.bpm = 120.0f;
        far.swing = 0.6f;
        expect (! bank.select (far).ok(),
                "swing: a swing this recording cannot reach is refused, not approximated");

        vp::HybridPercussionRenderer hybrid;
        hybrid.prepare (kSr, 256);
        hybrid.setBank (&bank);
        hybrid.setEnabled (true);
        vp::PercussionEngine percussion;
        percussion.prepare (kSr);
        percussion.setGroove (120.0f, 4);
        vp::TempoFollower clock;
        clock.prepare (kSr);
        clock.setPulsesPerBeat (4);
        clock.forceTempo (120.0f);
        clock.setTargetTempo (120.0f, 1.0f);
        clock.setLocked (true);
        clock.snapPhase (0.0f);
        std::vector<float> ol (256, 0.0f), orr (256, 0.0f);
        vp::HybridPercussionRenderer::Input in;
        in.audible = true;
        in.bpm = 120.0f;
        in.regime = vp::TempoRegime::fixed;
        in.swing = 0.6f;
        in.shakerEnabled = false;   // the reason must be the swing, not a missing stem
        hybrid.start();
        for (int i = 0; i < 1200; ++i)
        {
            in.tick = clock.advance (256);
            hybrid.render (percussion, ol.data(), orr.data(), 256, in);
        }
        expect (! hybrid.loopIsPlaying() && ! hybrid.wantsRecorded(),
                "swing: and the hybrid keeps the part on single strokes rather than swinging a straight loop");
    }

    // --- 8. changing recording without losing or doubling a stroke ---------
    {
        vp::LoopBank bank;
        addClickLoop (bank, "a120", 120.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::congas);
        addClickLoop (bank, "a120t2", 120.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::congas,
                      0.0f, 0.9f, 2);

        vp::LoopPlayer player;
        player.prepare (kSr, 256);
        player.setBank (&bank);
        vp::LoopQuery q;
        q.bpm = 120.0f;
        q.intensity = 0.5f;
        player.request (q);
        player.start();

        auto run = runPlayer (player, 256, 12.0, 120.0f,
                              [&q] (int block, vp::LoopPlayer& p, vp::TempoFollower&)
                              {
                                  // Mid-song, ask for the harder take. No STOP,
                                  // no gap: the change has to land on a bar.
                                  if (block == 800)
                                  {
                                      q.intensity = 0.95f;
                                      p.request (q);
                                  }
                                  return 0.0f;
                              });

        expect (player.changes() >= 1, "player: the part changed recording without a STOP");

        const auto onsets = findOnsets (run.left);
        const int expected = static_cast<int> (run.quarterOffsets.size());
        std::printf ("        strokes %zu against %d quarters, %d change(s)\n",
                     onsets.size(), expected, player.changes());
        // One either way is the entry and the very last quarter falling outside
        // the rendered span; anything more is a doubled or a dropped stroke.
        expect (std::abs (static_cast<int> (onsets.size()) - expected) <= 1,
                "player: a change of recording neither doubles nor drops a stroke");
        expect (worstOnsetError (onsets, run.quarterOffsets, static_cast<int> (kSr * 0.5)) < 5.0f,
                "player: and every stroke around the change is still on its quarter");
    }

    // --- 9. a tempo that moves a little ------------------------------------
    {
        vp::LoopBank bank;
        addClickLoop (bank, "a120", 120.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::congas);

        vp::LoopPlayer player;
        player.prepare (kSr, 256);
        player.setBank (&bank);
        player.setCorrection (1.0f, 0.05f);
        vp::LoopQuery q;
        q.bpm = 120.0f;
        player.request (q);
        player.start();

        auto run = runPlayer (player, 256, 16.0, 120.0f,
                              [] (int block, vp::LoopPlayer& p, vp::TempoFollower&)
                              {
                                  // The band drifts up by three BPM over the run,
                                  // which is what a band does.
                                  const float bpm = 120.0f + 3.0f * static_cast<float> (block) / 3000.0f;
                                  vp::LoopQuery qq;
                                  qq.bpm = bpm;
                                  p.request (qq);
                                  return bpm;
                              });

        const auto onsets = findOnsets (run.left);
        const float worst = worstOnsetError (onsets, run.quarterOffsets, static_cast<int> (kSr * 1.0));
        std::printf ("        worst error through a 3 BPM drift: %.2f ms\n",
                     static_cast<double> (worst));
        expect (worst < 5.0f,
                "player: a slow tempo drift is followed without the strokes leaving the grid");
        expect (rms (run.left, static_cast<int> (kSr * 2.0)) > 0.01f,
                "player: and it is still playing at the end of it");
    }

    // --- 10. STOP ----------------------------------------------------------
    {
        vp::LoopBank bank;
        addClickLoop (bank, "a120", 120.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::congas);

        vp::LoopPlayer player;
        player.prepare (kSr, 128);
        player.setBank (&bank);
        vp::LoopQuery q;
        q.bpm = 120.0f;
        player.request (q);
        player.start();

        auto run = runPlayer (player, 128, 6.0, 120.0f,
                              [] (int block, vp::LoopPlayer& p, vp::TempoFollower&)
                              {
                                  if (block == 1000)
                                      p.stop();
                                  return 0.0f;
                              });

        // Half a second either side, because the material is strokes with
        // silence between them: a window shorter than the gap between two
        // quarters proves nothing about either.
        const int stopAt = 1000 * 128;
        const int half = static_cast<int> (kSr * 0.5);
        expect (rms (run.left, stopAt - half, stopAt) > 0.002f,
                "player: it was playing right up to the STOP");
        expect (rms (run.left, stopAt, stopAt + half) < 1.0e-6f,
                "player: STOP is immediate - not the next quarter, not the next bar");
        expect (! player.isPlaying(), "player: and it stays stopped");
    }

    // --- 11. the hybrid ----------------------------------------------------
    {
        vp::LoopBank bank;
        addClickLoop (bank, "a120", 120.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::congas);
        addClickLoop (bank, "sh120", 120.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::shaker);

        vp::HybridPercussionRenderer hybrid;
        hybrid.prepare (kSr, 256);
        hybrid.setBank (&bank);
        hybrid.setEnabled (true);

        vp::PercussionEngine percussion;
        percussion.prepare (kSr);
        percussion.setSeed (0x51A4E1u);
        percussion.setGroove (120.0f, 4);

        vp::TempoFollower clock;
        clock.prepare (kSr);
        clock.setPulsesPerBeat (4);
        clock.forceTempo (120.0f);
        clock.setTargetTempo (120.0f, 1.0f);
        clock.setLocked (true);
        clock.snapPhase (0.0f);

        std::vector<float> l (256, 0.0f), r (256, 0.0f);
        vp::HybridPercussionRenderer::Input in;
        in.audible = true;
        in.bpm = 120.0f;
        in.style = vp::GrooveStyle::dance;
        hybrid.start();

        // The invariant that makes the handover a crossfade rather than a gap:
        // the strokes may only be faded down once the recording is actually
        // sounding. It comes in on a quarter of its own, which can be a whole
        // beat after the decision to hand over.
        bool fadedIntoSilence = false;
        auto runFor = [&] (int blocks, vp::TempoRegime regime)
        {
            in.regime = regime;
            for (int i = 0; i < blocks; ++i)
            {
                in.tick = clock.advance (256);
                hybrid.render (percussion, l.data(), r.data(), 256, in);
                // Any blend at all means the recording's share is audible, so
                // a player must be running - on the way in and on the way out
                // alike, which is why this asks the players and not the mode.
                if (hybrid.loopBlend() > 0.0f
                    && ! (hybrid.player().isPlaying() || hybrid.shakerPlayer().isPlaying()))
                    fadedIntoSilence = true;
            }
        };

        runFor (400, vp::TempoRegime::unknown);
        expect (! hybrid.loopIsPlaying(),
                "hybrid: with the tempo not yet decided, today's engine keeps the part");

        runFor (900, vp::TempoRegime::fixed);
        expect (hybrid.loopIsPlaying(),
                "hybrid: a tempo that has held still hands the part to the recording");
        const int afterFixed = hybrid.handovers();

        runFor (900, vp::TempoRegime::live);
        expect (! hybrid.loopIsPlaying(),
                "hybrid: a tempo that is moving hands it straight back to single strokes");
        expect (hybrid.handovers() == afterFixed + 1,
                "hybrid: and that is one handover, not a flicker between the two");

        expect (! fadedIntoSilence,
                "hybrid: the strokes are never faded down before the recording is sounding");

        // Nothing here may have touched the clock.
        expect (clock.getPulsesPerBeat() == 4 && clock.currentTempo() > 119.0f
                    && clock.currentTempo() < 121.0f,
                "hybrid: the handover leaves the clock exactly where it was");

        // Half a part is two percussionists. With the shaker switched on and no
        // shaker recorded, the whole part stays on single strokes rather than
        // splitting the instruments between the two engines.
        vp::LoopBank congasOnly;
        addClickLoop (congasOnly, "a120", 120.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::congas);
        hybrid.setBank (&congasOnly);
        hybrid.start();
        runFor (900, vp::TempoRegime::fixed);
        expect (! hybrid.loopIsPlaying() && ! hybrid.wantsRecorded(),
                "hybrid: with only half the instruments recorded, the part stays on single strokes");

        // Switch the shaker off and the same bank is enough.
        in.shakerEnabled = false;
        runFor (900, vp::TempoRegime::fixed);
        expect (hybrid.loopIsPlaying(),
                "hybrid: and it is enough once the shaker is switched off");
        in.shakerEnabled = true;
        hybrid.setBank (&bank);

        // With the flag off it is the app as it was.
        hybrid.setEnabled (false);
        runFor (400, vp::TempoRegime::fixed);
        expect (! hybrid.loopIsPlaying() && ! hybrid.wantsRecorded(),
                "hybrid: switched off, nothing but PercussionEngine plays");
    }

    // --- 12. the hybrid allocates nothing either ---------------------------
    {
        vp::LoopBank bank;
        addClickLoop (bank, "a120", 120.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::congas);
        addClickLoop (bank, "b120", 120.0f, 8, vp::LoopRole::grooveB, vp::LoopStem::congas);
        addClickLoop (bank, "f120", 120.0f, 8, vp::LoopRole::fill, vp::LoopStem::congas);
        addClickLoop (bank, "sh120", 120.0f, 8, vp::LoopRole::grooveA, vp::LoopStem::shaker);
        addClickLoop (bank, "shb120", 120.0f, 8, vp::LoopRole::grooveB, vp::LoopStem::shaker);

        vp::HybridPercussionRenderer hybrid;
        hybrid.prepare (kSr, 256);
        hybrid.setBank (&bank);
        hybrid.setEnabled (true);

        vp::PercussionEngine percussion;
        percussion.prepare (kSr);
        percussion.setSeed (0x51A4E1u);
        percussion.setGroove (120.0f, 4);

        vp::TempoFollower clock;
        clock.prepare (kSr);
        clock.setPulsesPerBeat (4);
        clock.forceTempo (120.0f);
        clock.setTargetTempo (120.0f, 1.0f);
        clock.setLocked (true);
        clock.snapPhase (0.0f);

        std::vector<float> l (256, 0.0f), r (256, 0.0f);
        vp::HybridPercussionRenderer::Input in;
        in.audible = true;
        in.bpm = 120.0f;
        in.regime = vp::TempoRegime::fixed;
        hybrid.start();

        for (int i = 0; i < 2000; ++i)
        {
            in.tick = clock.advance (256);
            hybrid.render (percussion, l.data(), r.data(), 256, in);
        }

        vpBeginAllocationWatch();
        for (int i = 0; i < 2000; ++i)
        {
            in.regime = (i / 400) % 2 == 0 ? vp::TempoRegime::fixed : vp::TempoRegime::live;
            in.tick = clock.advance (256);
            hybrid.render (percussion, l.data(), r.data(), 256, in);
        }
        const long long allocs = vpEndAllocationWatch();
        std::printf ("        allocations in 2000 hybrid callbacks with handovers: %lld\n", allocs);
        expect (allocs == 0,
                "hybrid: handing the part between the two engines allocates nothing either");
    }

    passed += gPassed;
    failed += gFailed;
}
