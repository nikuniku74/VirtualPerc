#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vp
{

/**
    When the harmony moves.

    Everything the app listens to until here is percussive: the network is
    trained on beat activations, the kick channel is one drum, the level meter
    does not care what note it is. Nothing looks at **pitch**, and that leaves
    the single strongest cue about where a bar begins on the table.

    Harmony changes on bar lines. Not always and not only - a chord can arrive
    on the three, and a whole section can sit on one chord - but across ordinary
    material the distribution is not close: chord changes cluster on the
    downbeat the way nothing else in the signal does. The app's worst measured
    failure is exactly this question. Through an iPad's own speaker the
    network's downbeat vote is no better than a coin (docs/AUDIO_ENGINE.md), and
    a coin is what decides which quarter the part comes in on.

    It is also the only cue on this list that survives when the drums do not. A
    voice with a guitar behind it has no kick, no snare, and a beat activation
    curve the network was never trained for - but it still changes chord, and it
    still changes it on the bar.

    **How.** A chromagram, and the distance between what is sounding now and
    what has been sounding. Twelve pitch classes over three octaves, evaluated
    by Goertzel on a decimated signal, folded and normalised; the change
    function is one minus the cosine similarity against a slow average of
    itself. Pitch classes rather than a spectrum on purpose: a drum is
    broadband, so it lands on every chroma bin at once and moves the *direction*
    of the vector hardly at all. A chord change moves it a long way. That is the
    whole reason this is chroma and not a spectral flux, which fires on every
    snare.

    **What it is not.** It is not sharp. The window is 170 ms and a chord does
    not begin at a sample the way a stick does, so an event here is worth a
    quarter of a bar and no more - which is exactly what a downbeat histogram
    needs and is why no attempt is made to time it better.

    Runs on the audio thread. Allocation is in `prepare`; `process` is a
    decimating accumulate plus, once a hop, thirty-six Goertzel evaluations.
*/
class HarmonicChange
{
public:
    struct Change
    {
        /** Samples from the start of the block this was reported in, and it may
            be negative: the window that saw the change ended in this block but
            the change itself is half a window earlier. */
        int   offset = 0;
        /** How far the harmony moved, 0..1. */
        float strength = 0.0f;
    };

    static constexpr int kMaxChanges = 4;

    void prepare (double sr)
    {
        sampleRate = sr > 1.0 ? sr : 48000.0;
        decimate = std::max (1, static_cast<int> (std::lround (sampleRate / kWorkRate)));
        workRate = sampleRate / decimate;

        ring.assign (static_cast<size_t> (kWindow), 0.0f);
        window.assign (static_cast<size_t> (kWindow), 0.0f);
        for (int i = 0; i < kWindow; ++i)
            window[static_cast<size_t> (i)] = static_cast<float> (
                0.5 - 0.5 * std::cos (2.0 * kPi * i / (kWindow - 1)));

        // Three octaves from C3. Low enough to hold a bass note, high enough to
        // hold the chord a guitar is playing, and short of where cymbals live.
        coeff.assign (static_cast<size_t> (kBins), 0.0f);
        binChroma.assign (static_cast<size_t> (kBins), 0);
        for (int i = 0; i < kBins; ++i)
        {
            const double midi = kLowestMidi + i;
            const double hz = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
            coeff[static_cast<size_t> (i)] = static_cast<float> (
                2.0 * std::cos (2.0 * kPi * hz / workRate));
            binChroma[static_cast<size_t> (i)] = i % 12;
        }
        reset();
    }

    void reset() noexcept
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        std::fill (chroma, chroma + 12, 0.0f);
        std::fill (reference, reference + 12, 0.0f);
        write = 0;
        filled = 0;
        acc = 0.0f;
        accN = 0;
        sinceHop = 0;
        absPos = 0;
        refractory = 0;
        heldHops = 0;
        haveReference = false;
        lastChange = 0.0f;
        typicalMove = 0.0f;
        peakHeld = 0.0f;
        changeStartedAt = 0;
        usable = false;
        loudestWindow = 0.0f;
        usableShare = 0.0f;
        histWrite = 0;
        histFilled = 0;
        for (auto& row : history)
            std::fill (row, row + 12, 0.0f);
    }

    /** One block of the analysis signal. Returns how many harmonic changes were
        found, each with its offset inside this block. */
    int process (const float* x, int numSamples, Change* out, int maxOut) noexcept
    {
        int n = 0;
        if (x == nullptr || out == nullptr || maxOut <= 0 || ring.empty())
            return 0;

        const std::int64_t blockStart = absPos;

        for (int i = 0; i < numSamples; ++i, ++absPos)
        {
            // Decimate by averaging. A box of `decimate` samples is a poor
            // anti-alias filter and a perfectly good one here: what is above
            // 6 kHz is cymbals and consonants, and neither is harmony.
            acc += x[i];
            if (++accN < decimate)
                continue;
            const float s = acc / static_cast<float> (decimate);
            acc = 0.0f;
            accN = 0;

            ring[static_cast<size_t> (write)] = s;
            write = (write + 1) % kWindow;
            if (filled < kWindow)
                ++filled;

            if (++sinceHop < kHop || filled < kWindow)
                continue;
            sinceHop = 0;

            const float moved = analyseWindow();
            if (! usable)
            {
                // Nothing to read. Hold everything rather than letting a quiet
                // or a purely percussive window count against a change that is
                // in progress.
                continue;
            }
            lastChange = moved;

            if (refractory > 0)
            {
                --refractory;
                heldHops = 0;
                continue;
            }

            // A chord is a step; a drum is an impulse.
            //
            // This is the whole discriminator, and the first version did not
            // have it: peak-picking the change function fired on the snare,
            // because a broadband hit lands on thirty-six arbitrary Goertzel
            // bins and moves the chroma vector's direction as much as a chord
            // does *for one window*. Measured on the drum stem alone it
            // produced 41 changes where there were none. What a drum cannot do
            // is stay moved: the window after it, the harmony is back where it
            // was. So a change has to hold for several hops before it counts,
            // and it is timed from where it started rather than from where it
            // was confirmed.
            // Relative to what this material has been giving, not to a fixed
            // line.
            //
            // `kEnter` alone is an absolute threshold on a quantity whose floor
            // depends on the arrangement, and this repository has learned that
            // lesson three times now - see PhaseTrust.h, and STATUS.md on the
            // fit residual. On a sustained accompaniment - strings, an organ, a
            // pad holding through the bar line - the chroma is more tonal and
            // its ordinary hop-to-hop movement sits higher, so 0.10 lands
            // inside the noise: measured, 74 changes reported where the
            // arrangement has 28. Each false one lands on any phase at all,
            // which is exactly what makes the tempo unreadable from them
            // (Tracking/HarmonicTempo.h).
            //
            // So the floor is lifted by what the change function has typically
            // been doing when nothing was happening. Learned only below the
            // gate, so a run of real chord movement cannot raise the bar
            // against itself, and slowly, so one loud bar cannot either.
            const float enterNow = std::max (kEnter, typicalMove * kOverTypical);
            if (moved <= enterNow)
                typicalMove += (moved - typicalMove) * kTypicalRise;

            if (moved > enterNow)
            {
                if (heldHops == 0)
                    changeStartedAt = absPos;
                ++heldHops;
                peakHeld = std::max (peakHeld, moved);
                if (heldHops >= kHoldHops)
                {
                    if (n < maxOut)
                    {
                        // Half a window before the hop that first saw it: the
                        // change is somewhere inside that window and its middle
                        // is the best guess available. Nothing finer is claimed
                        // - see the class note.
                        // Back by half a window, and by the median's own
                        // centre: a median over five hops describes the moment
                        // two hops ago, not the one that has just ended.
                        const std::int64_t at = changeStartedAt
                                                - static_cast<std::int64_t> (
                                                      kWindow / 2 * decimate)
                                                - static_cast<std::int64_t> (
                                                      (kMedianHops / 2) * kHop * decimate);
                        out[n].offset = static_cast<int> (at - blockStart);
                        out[n].strength = std::clamp (peakHeld / kFullChange, 0.0f, 1.0f);
                        ++n;
                    }
                    // Adopt it. The reference is what the harmony *is* now, so
                    // holding the old chord would keep reporting the same
                    // change for as long as the new one lasted.
                    std::copy (chroma, chroma + 12, reference);
                    refractory = kRefractoryHops;
                    heldHops = 0;
                    peakHeld = 0.0f;
                }
            }
            else
            {
                heldHops = 0;
                peakHeld = 0.0f;
            }
        }
        return n;
    }

    /** The chroma as it stands, twelve pitch classes starting at C. For the
        debug panel and the probes. */
    const float* chromaNow() const noexcept { return chroma; }
    /** How far the harmony has moved from its own recent average, 0..1. */
    float changeNow() const noexcept { return lastChange; }

    /** What the change function has typically been doing below the gate, for
        the probes. */
    float typicalMovement() const noexcept { return typicalMove; }

    /** What share of recent windows carried a chord at all, 0..1.

        This is a property of the **material**, and that is why it exists. The
        histogram of where changes land is not: measured on the same kit track
        across five runs its winning margin came out 0.07, 0.10, 0.36, 0.38 and
        0.58, because a chroma pointed at drums finds a different arbitrary
        answer each time. A threshold on that separates nothing, and one was
        tried and let the harmony rotate the bar on a drum track three times.

        How often the signal is tonal at all does separate them, and it barely
        moves: on a band with a chord under it nearly every window passes, on a
        kit track hardly any do. It is the question "is there harmony here",
        asked before "where does it change". */
    float tonalShare() const noexcept { return usableShare; }

private:
    static constexpr double kPi = 3.14159265358979;
    /** Everything harmonic is under 6 kHz, so the whole thing runs there. */
    static constexpr double kWorkRate = 12000.0;
    /** 170 ms at the work rate: a semitone at C3 is 7.8 Hz apart and this
        resolves 5.9, which is the reason the window is not shorter. */
    static constexpr int kWindow = 2048;
    static constexpr int kHop = 512;
    static constexpr int kBins = 36;
    static constexpr double kLowestMidi = 48.0;   // C3
    /** How far the harmony has to move before it is worth watching. */
    static constexpr float kEnter = 0.10f;
    /** How far above what this material ordinarily moves a window has to go to
        count as a chord change rather than the arrangement breathing.

        Four, and it is set by making the two cases equivalent rather than by
        taste. Measured, the change function's floor is 0.012 to 0.024 on
        material the detector reads well and 0.041 on a sustained
        accompaniment - so the fixed 0.10 stands at seven times the floor in the
        first case and only 2.4 times it in the second, which is why the
        sustained case drowned. At four the gate is unchanged wherever the floor
        is low (four times 0.024 is still under 0.10, so `kEnter` wins and
        nothing moves) and lifts only where the material is genuinely noisier.

        Higher was measured and is worse in a way worth recording: at seven the
        detector starts answering on material *with* drums, and at nine it
        answers 132.8 BPM on a song playing 118. That case must stay silent -
        the network and the kick channel own it - and a gate that lets it speak
        has stopped measuring the harmony. */
    static constexpr float kOverTypical = 4.00f;
    /** And how slowly that reference is learned. */
    static constexpr float kTypicalRise = 0.02f;
    /** And for how many hops it has to stay moved before it is a chord change
        rather than a drum. Three hops is 130 ms: longer than any transient,
        far shorter than any chord. */
    static constexpr int kHoldHops = 3;
    /** And this far to count as a whole one, for the reported strength. */
    static constexpr float kFullChange = 0.45f;
    /** After one, half a second before another. A chord that arrives in two
        stages - the bass first, the guitar after - is one change. */
    static constexpr int kRefractoryHops = 12;
    /** A window under this share of the loudest recent one is not being read.
        Below it the chroma is arithmetic rather than music. */
    static constexpr float kWindowQuiet = 0.12f;
    /** And the loudest recent one forgets over about ten seconds. */
    static constexpr float kWindowLoudDecay = 0.995f;
    /** How peaked the chroma has to be to be a chord rather than a hit. Flat
        over twelve bins is 0.289. */
    static constexpr float kTonalAtLeast = 0.45f;
    /** Windows the per-bin median runs over. Five hops is 215 ms: longer than
        any drum, shorter than any chord. */
    static constexpr int kMedianHops = 5;
    /** Per hop, so about fifteen seconds to turn over. */
    static constexpr float kShareRate = 0.003f;

    float analyseWindow() noexcept
    {
        // Goertzel, one pass per bin over the window. Cheaper than an FFT here
        // because thirty-six bins is far fewer than a transform would give and
        // every one of them is wanted.
        float raw[12] = {};
        for (int b = 0; b < kBins; ++b)
        {
            const float c = coeff[static_cast<size_t> (b)];
            float s1 = 0.0f, s2 = 0.0f;
            int idx = write;   // oldest sample: the ring is exactly one window
            for (int i = 0; i < kWindow; ++i)
            {
                const float v = ring[static_cast<size_t> (idx)]
                                * window[static_cast<size_t> (i)];
                const float s0 = v + c * s1 - s2;
                s2 = s1;
                s1 = s0;
                idx = idx + 1 == kWindow ? 0 : idx + 1;
            }
            const float mag2 = s1 * s1 + s2 * s2 - c * s1 * s2;
            raw[binChroma[static_cast<size_t> (b)]] += std::sqrt (std::max (0.0f, mag2));
        }

        // Take the percussion out before reading it.
        //
        // In the time-frequency plane a drum is a vertical line - broadband and
        // brief - and a chord is a horizontal one: narrow and sustained. The
        // cheapest thing that separates them is a median over time per bin: a
        // hit shows up in one window and is outvoted, a chord shows up in all
        // of them and survives. It is the idea behind harmonic-percussive
        // separation, at the resolution this actually needs.
        //
        // Measured: on a full mix, without it the chord changes landed on the
        // downbeat 35% of the time - a coin with a lean on it - because the kit
        // was moving the chroma inside every window. The drums are the whole
        // difference between that and the 100% this gets on the same
        // arrangement with the kit taken out.
        for (int i = 0; i < 12; ++i)
        {
            history[static_cast<size_t> (histWrite)][i] = raw[i];
        }
        histWrite = (histWrite + 1) % kMedianHops;
        if (histFilled < kMedianHops)
            ++histFilled;
        if (histFilled >= kMedianHops)
        {
            for (int i = 0; i < 12; ++i)
            {
                float v[kMedianHops];
                for (int h = 0; h < kMedianHops; ++h)
                    v[h] = history[static_cast<size_t> (h)][i];
                std::sort (v, v + kMedianHops);
                raw[i] = v[kMedianHops / 2];
            }
        }

        // Is this window worth reading at all?
        //
        // Two ways it is not, and both were found by measurement rather than
        // foresight. **Energy**: a near-silent window normalises to a unit
        // vector made of arithmetic noise, whose direction wanders freely - run
        // over a drum stem, which is mostly silence between hits, the first
        // version reported 59 chord changes in a signal that has no chords in
        // it at all. **Tonality**: a broadband hit spreads across all twelve
        // pitch classes, so its chroma is flat, and flat is not a chord. A
        // unit vector spread evenly over twelve bins has every component at
        // 0.289; anything that is really a chord has a component far above it.
        float rawSum = 0.0f, norm = 1.0e-9f;
        for (int i = 0; i < 12; ++i)
        {
            rawSum += raw[i];
            norm += raw[i] * raw[i];
        }
        norm = std::sqrt (norm);

        loudestWindow = std::max (loudestWindow * kWindowLoudDecay, rawSum);
        const bool loudEnough = rawSum > kWindowQuiet * loudestWindow;

        float peak = 0.0f;
        for (int i = 0; i < 12; ++i)
            peak = std::max (peak, raw[i] / norm);
        const bool tonal = peak > kTonalAtLeast;

        usable = loudEnough && tonal;
        // Over about fifteen seconds, so a chorus does not have to prove it
        // again and a passage that stops being harmonic stops answering.
        usableShare += ((usable ? 1.0f : 0.0f) - usableShare) * kShareRate;
        if (! usable)
            return 0.0f;

        // Normalised to a direction. Loudness is the level meter's job; what
        // matters here is only which pitch classes, in what proportion.
        for (int i = 0; i < 12; ++i)
            chroma[i] = raw[i] / norm;

        if (! haveReference)
        {
            std::copy (chroma, chroma + 12, reference);
            haveReference = true;
            return 0.0f;
        }

        float dot = 0.0f, refNorm = 1.0e-9f;
        for (int i = 0; i < 12; ++i)
        {
            dot += chroma[i] * reference[i];
            refNorm += reference[i] * reference[i];
        }
        const float moved = std::clamp (1.0f - dot / std::sqrt (refNorm), 0.0f, 1.0f);

        // The reference follows, slowly, and only while nothing is happening:
        // letting it chase during a change would close the gap it is there to
        // measure before the change had held long enough to count.
        if (heldHops == 0)
            for (int i = 0; i < 12; ++i)
                reference[i] += (chroma[i] - reference[i]) * kReferenceRate;
        return moved;
    }

    /** Per hop, so about 43 ms: a second and a half to forget a chord. */
    static constexpr float kReferenceRate = 0.06f;

    double sampleRate = 48000.0;
    double workRate = 12000.0;
    int decimate = 4;

    std::vector<float> ring, window, coeff;
    std::vector<int>   binChroma;
    float chroma[12] {};
    float reference[12] {};
    int write = 0, filled = 0, accN = 0, sinceHop = 0, refractory = 0, heldHops = 0;
    float acc = 0.0f, lastChange = 0.0f, peakHeld = 0.0f;
    float typicalMove = 0.0f;
    bool  haveReference = false, usable = false;
    float loudestWindow = 0.0f;
    float usableShare = 0.0f;
    float history[kMedianHops][12] {};
    int   histWrite = 0, histFilled = 0;
    std::int64_t absPos = 0, changeStartedAt = 0;
};

} // namespace vp
