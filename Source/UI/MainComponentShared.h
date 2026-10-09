#pragma once

// Shared, internal to the MainComponent*.cpp translation units: design tokens,
// fonts, colours and the small paint helpers they all use. Split out of the
// single MainComponent.cpp so look-and-feel, layout, painting and the overlay
// widgets can live in files of their own. Not for use outside Source/UI.

#include "UI/MainComponent.h"
#include "Platform/IosMicPermission.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>

#if defined (VP_HAS_LOOP_BANK) && VP_HAS_LOOP_BANK
 #include "VpLoopBankData.h"
#endif

#if defined (VP_HAS_HIT_SAMPLES) && VP_HAS_HIT_SAMPLES
 #include "VpHitData.h"
 #include <juce_audio_formats/juce_audio_formats.h>
 #include <juce_dsp/juce_dsp.h>
#endif

#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wunused-function"
 #pragma clang diagnostic ignored "-Wunused-variable"
 #pragma clang diagnostic ignored "-Wunused-const-variable"
#elif defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wunused-function"
 #pragma GCC diagnostic ignored "-Wunused-variable"
#endif

// One theme flag for every translation unit (the helpers below are
// per-unit copies; this must not be).
namespace vpui
{
    inline bool gDarkMode = true;
}

namespace
{
    using vpui::gDarkMode;
    constexpr const char* kHitNames[] = {
        "ABSORB", "HORN", "UPLIFTER FX", "RISER 2", "WINDCHIMES", "RISER BOOM"
    };
    constexpr const char* kHitResources[] = {
        "absorb_wav", "reggae_horn_wav", "uplifter_fx_wav", "riser_2_wav",
        "windchimes_wav", "cinematic_riser_boom_wav"
    };
    constexpr const char* kHitPrefKeys[] = {
        "absorbSample", "hornSample", "uplifterSample", "riserSample"
    };

#if defined (VP_HAS_LOOP_BANK) && VP_HAS_LOOP_BANK
    const char* embeddedLoopResource (const std::string& originalName, int& size)
    {
        for (int i = 0; i < VpLoopBankData::namedResourceListSize; ++i)
        {
            if (originalName == VpLoopBankData::originalFilenames[i])
                return VpLoopBankData::getNamedResource (
                    VpLoopBankData::namedResourceList[i], size);
        }
        size = 0;
        return nullptr;
    }
#endif

    // The input level, in the units a player already thinks in.
    //
    // Where the good band is was measured rather than chosen. Below about
    // -18 dBFS of peak the network is outside the level it was trained on and
    // the tempo goes with it - on a real recording at that level the first ten
    // seconds settle on a false high octave (docs/TODO.md item 24). Above about
    // -1 dBFS the analysis clip guard starts pulling the signal back down
    // (`kMakeupClipGuardPeak`, item 16), so turning the trim further up stops
    // buying anything. The comfortable middle is around -6 dBFS.
    constexpr float kInputLowPeak    = 0.2512f;  // -12 dBFS: bottom of the band
    constexpr float kInputHighPeak   = 0.8913f;  //  -1 dBFS: top of the band
    constexpr float kInputSilentPeak = 0.0012f;  // below this nothing is arriving
    constexpr float kMeterFloorDb    = -48.0f;

    float peakToDb (float peak) noexcept
    {
        return peak > 1.0e-6f ? 20.0f * std::log10 (peak) : -120.0f;
    }

    /** Where a peak sits across the meter. dB, not amplitude: a linear bar
        spends four fifths of its width on the top 6 dB, which is why the old
        one was full at -20 dBFS and told a player nothing. */
    float meterPosition (float peak) noexcept
    {
        return juce::jlimit (0.0f, 1.0f,
                             (peakToDb (peak) - kMeterFloorDb) / -kMeterFloorDb);
    }

    juce::String micGainText (float gain)
    {
        if (gain <= 0.001f)
            return "MUTO";
        const float db = 20.0f * std::log10 (gain);
        return (db >= 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
    }

    juce::String ellipsis (const juce::String& text, const juce::Font& font, float width)
    {
        if (juce::GlyphArrangement::getStringWidth (font, text) <= width)
            return text;
        const juce::String dot = juce::String::fromUTF8 ("\xe2\x80\xa6");
        juce::String cut = text;
        while (cut.isNotEmpty()
               && juce::GlyphArrangement::getStringWidth (font, cut + dot) > width)
            cut = cut.dropLastCharacters (1);
        return cut + dot;
    }

    juce::String knobValueText (const juce::Slider& s)
    {
        const float v = static_cast<float> (s.getValue());
        if (s.getMaximum() > 1.5)
            return micGainText (v);
        return juce::String (juce::roundToInt (static_cast<double> (v) * 100.0)) + "%";
    }

    // Design tokens (docs/DESIGN_BRIEF_MAIN_SCREEN.md, restyle). Three surface
    // levels: bg < panel (cards) < ink (controls). The old panel was #0C0C0E on
    // #050506 - a card nobody could see from an arm's length in the dark.
    juce::Colour bg()      { return gDarkMode ? juce::Colour (0xff08080a) : juce::Colour (0xfff4f2f5); }
    juce::Colour panel()   { return gDarkMode ? juce::Colour (0xff141418) : juce::Colour (0xffffffff); }
    juce::Colour border()  { return gDarkMode ? juce::Colour (0xff2a2a32) : juce::Colour (0xffd8d2db); }
    // The surface a control sits on. In light this has to *be* light: it was a
    // near-black in both themes, and since every button is filled with it the
    // light theme came out as a light background behind a wall of black
    // buttons - which is the whole reason "light" still looked dark.
    juce::Colour ink()     { return gDarkMode ? juce::Colour (0xff1e1e24) : juce::Colour (0xffece8ee); }
    juce::Colour text()    { return gDarkMode ? juce::Colour (0xfff4f4f7) : juce::Colour (0xff17131a); }
    juce::Colour sliderTrack() { return gDarkMode ? ink() : juce::Colour (0xffd9d2dc); }
    // Fuchsia now means the brand and the downbeat ("the one") only. What the
    // tracker is doing is carried by the three state colours below.
    juce::Colour fuchsia() { return gDarkMode ? juce::Colour (0xffff2ec8) : juce::Colour (0xffe0129f); }
    juce::Colour mute()    { return gDarkMode ? juce::Colour (0xff9a9aa8) : juce::Colour (0xff5e5764); }
    juce::Colour stateLocked()    { return gDarkMode ? juce::Colour (0xff34d17a) : juce::Colour (0xff1fa85c); }
    juce::Colour stateSearching() { return gDarkMode ? juce::Colour (0xffffb020) : juce::Colour (0xffd98a00); }
    juce::Colour stateLost()      { return gDarkMode ? juce::Colour (0xffff4d4d) : juce::Colour (0xffe0302f); }

    /** Colour and energy the MIC knob paints for a held peak. Fuchsia inside
        the analysis band, amber then red once it is too hot. */
    struct MicLevelLook
    {
        juce::Colour colour;
        float glow = 0.0f;
        float amount = 0.0f;
    };

    MicLevelLook micLevelLook (float hold) noexcept
    {
        const float amount = meterPosition (hold);
        if (hold <= kInputSilentPeak)
            return { mute(), 0.05f, 0.0f };
        if (hold < kInputLowPeak)
            return { fuchsia(), 0.08f + 0.10f * amount, amount };
        if (hold <= kInputHighPeak)
            return { fuchsia().brighter (0.10f), 0.16f + 0.18f * amount, amount };
        const float over = juce::jlimit (0.0f, 1.0f, (peakToDb (hold) + 1.0f) / 8.0f);
        const auto amber = juce::Colour (0xffffa726);
        const auto red   = juce::Colour (0xffff3b30);
        return { amber.interpolatedWith (red, over), 0.24f + 0.18f * over, amount };
    }

    // FEEL voice knobs. Each part has its own fill so the four tiles read
    // apart without opening the label.
    juce::Colour voiceShakerOn()   { return juce::Colour (0xffaab0b8); } // grigetto
    juce::Colour voiceCongasOn()   { return juce::Colour (0xffd3925c); } // marroncino
    juce::Colour voiceCembaloOn()  { return juce::Colour (0xffe6c43c); } // dorato
    juce::Colour voiceClapOn()     { return juce::Colour (0xff62b8e4); } // azzurrino
    juce::Colour voiceTriangleOn() { return juce::Colour (0xff2ee8d0); } // turchese
    juce::Colour colourForHit (int sample) noexcept
    {
        constexpr uint32_t colours[] = {
            0xffff8a1a, 0xffff2a4a, 0xffff2ec8, 0xff9b6bff, 0xff2ee8d0, 0xffffc857
        };
        return juce::Colour (colours[juce::jlimit (0, 5, sample)]);
    }
    juce::Colour colourForKitSound (vp::KitSound s) noexcept
    {
        switch (s)
        {
            case vp::KitSound::shaker:   return voiceShakerOn();
            case vp::KitSound::congas:   return voiceCongasOn();
            case vp::KitSound::cembalo:  return voiceCembaloOn();
            case vp::KitSound::clap:     return voiceClapOn();
            case vp::KitSound::triangle: return voiceTriangleOn();
            case vp::KitSound::count:    break;
        }
        return voiceShakerOn();
    }
    // Inner disc of a FEEL knob: the accent pulled toward charcoal so it
    // reads as a pastel, not a saturated chip. Picker cells use this same
    // mix so a cell and its knob are one colour.
    juce::Colour knobInterior (juce::Colour accent) noexcept
    {
        return accent.interpolatedWith (juce::Colour (0xff0a0a0c),
                                        gDarkMode ? 0.52f : 0.36f);
    }
    juce::Colour knobInteriorForKitSound (vp::KitSound s) noexcept
    {
        return knobInterior (colourForKitSound (s));
    }
    juce::Font fontDisplay (float h)
    {
        return juce::Font (juce::FontOptions().withName ("Futura").withStyle ("Bold")
                               .withHeight (juce::jmax (1.0f, h)));
    }

    /** The condensed face every label, button and caption is set in; only the
        BPM keeps Futura Bold. Narrow and light: Futura Condensed Medium (the
        same family as the tempo), then Avenir Next Condensed Demi Bold, DIN
        Condensed, Arial Narrow, and finally the old Avenir Next. The first
        family the system actually has wins. Resolved once:
        findAllTypefaceNames walks the system font list. */
    struct CondensedFace
    {
        juce::String family, boldStyle, textStyle;
        float heightScale = 1.0f;
    };

    const CondensedFace& condensedFace()
    {
        static const CondensedFace face = []
        {
            struct Candidate { const char* family; const char* bold; const char* text; float scale; };
            // Scale below 1: these faces stand tall for their em, and 1.14 made
            // every label look too high. Widths stay narrow either way.
            // Medium weights on purpose: ExtraBold and Impact read as shouting
            // at 12-17 pt on the stand. Hierarchy comes from size and colour.
            constexpr Candidate candidates[] = {
                { "Futura",                "Condensed Medium",    "Condensed Medium", 0.94f },
                { "Avenir Next Condensed", "Demi Bold",           "Medium",           0.94f },
                { "DIN Condensed",         "Bold",                "Bold",             1.00f },
                { "Arial Narrow",          "Bold",                "Regular",          0.96f },
            };
            const auto families = juce::Font::findAllTypefaceNames();
            for (const auto& c : candidates)
            {
                if (! families.contains (c.family))
                    continue;
                const auto styles = juce::Font::findAllTypefaceStyles (c.family);
                if (styles.contains (c.bold))
                    return CondensedFace { c.family, c.bold,
                                           styles.contains (c.text) ? c.text : c.bold, c.scale };
            }
            return CondensedFace { "Avenir Next", "Bold", "Medium", 1.0f };
        }();
        return face;
    }

    /** Tight tracking: the condensed face is meant to sit close, and wide
        letter-spacing on 12 pt captions is what made the old labels long. */
    constexpr float kUiTracking = 0.03f;

    juce::Font fontUi (float h, bool bold = true)
    {
        const auto& f = condensedFace();
        return juce::Font (juce::FontOptions().withName (f.family)
                               .withStyle (bold ? f.boldStyle : f.textStyle)
                               .withHeight (juce::jmax (1.0f, h * f.heightScale))
                               .withKerningFactor (kUiTracking));
    }

    int clampW (int lo, int hi, int v) noexcept
    {
        if (lo > hi)
        {
            const int t = lo;
            lo = hi;
            hi = t;
        }
        return lo == hi ? lo : juce::jlimit (lo, hi, v);
    }

    int takeAtMost (int room, int want) noexcept
    {
        return juce::jmax (0, juce::jmin (room, want));
    }

    juce::Rectangle<int> innerCard (juce::Rectangle<int> bounds, int padX, int padY, int titleH)
    {
        const int px = juce::jmin (padX, juce::jmax (0, bounds.getWidth() / 4));
        const int py = juce::jmin (padY, juce::jmax (0, bounds.getHeight() / 4));
        auto inner = bounds.reduced (px, py);
        if (inner.getWidth() < 1 || inner.getHeight() < 1)
            return bounds;
        if (titleH > 0)
            inner.removeFromTop (juce::jmin (titleH, juce::jmax (0, inner.getHeight() - 8)));
        if (inner.getWidth() < 1 || inner.getHeight() < 1)
            return bounds;
        return inner;
    }

    juce::Colour stateColour (vp::FollowBar b)
    {
        using B = vp::FollowBar;
        // Three families, readable without reading the word: locked (green),
        // searching (amber), uncertain or lost (red). Ready and paused are
        // neither - nothing is being followed - so they stay neutral.
        switch (b)
        {
            case B::following:       return stateLocked();
            case B::followingListen: return stateLocked();
            case B::tapAlign:        return stateLocked();
            case B::calibrating:     return stateSearching();
            case B::listening:       return stateSearching();
            case B::waitBeat:        return stateSearching();
            case B::waitStart:       return stateSearching();
            case B::weakFollow:      return stateLost();
            case B::recalin:         return stateLost();
            case B::paused:          return mute();
            case B::ready:           return mute();
        }
        return mute();
    }

    /** The colour the stage shows: the trust light (item 117) wherever the
        tracker is following, or before START could be; the state family
        everywhere else. */
    juce::Colour shownColour (const vp::EngineSnapshot& s, vp::TempoTrust t)
    {
        switch (t)
        {
            case vp::TempoTrust::sure:  return stateLocked();
            case vp::TempoTrust::check: return stateSearching();
            case vp::TempoTrust::out:   return stateLost();
            case vp::TempoTrust::none:  break;
        }
        return stateColour (s.followBar);
    }

    /** And the word beside it. A red "SEGUENDO" would contradict itself, so
        while following the word is the trust; before START it says whether
        pressing it is safe. The other states keep their own words. */
    const char* shownLabel (const vp::EngineSnapshot& s, vp::TempoTrust t)
    {
        using B = vp::FollowBar;
        using T = vp::TempoTrust;
        // The guard has the part silent: say so, and say when it is on its
        // way back, so a gap never reads as the app having broken.
        if (s.driftGuarded)
            return t == T::out ? "FUORI TEMPO - IN PAUSA" : "RIENTRA ALLA BATTUTA";
        if (s.followBar == B::paused && t != T::none)
            return t == T::sure ? "AGGANCIATO - PUOI PARTIRE"
                 : t == T::check ? "IN ASCOLTO - ASPETTA" : "IN ASCOLTO - NON ANCORA";
        if (s.followBar == B::following || s.followBar == B::followingListen)
            return t == T::out ? "STA USCENDO" : t == T::check ? "CONTROLLA" : "SEGUENDO";
        return vp::toBarString (s.followBar);
    }

    bool stateIsHot (vp::FollowBar b)
    {
        using B = vp::FollowBar;
        return b == B::following || b == B::followingListen || b == B::tapAlign;
    }

    /** The glow behind the tempo and under the transport. Alphas tuned against
        a near-black ground: over a white one the same values are not a glow but
        a pink cloud sitting on top of the page, so light gets a third of it. */
    void paintRadial (juce::Graphics& g, juce::Point<float> c, float radius,
                      juce::Colour col, float alpha)
    {
        if (! gDarkMode)
            alpha *= 0.32f;

        juce::ColourGradient grad (col.withAlpha (alpha), c.x, c.y,
                                   col.withAlpha (0.0f), c.x, c.y + radius, true);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
    }

    float unitRamp (float x, float from, float to) noexcept
    {
        if (to <= from)
            return x >= to ? 1.0f : 0.0f;
        return juce::jlimit (0.0f, 1.0f, (x - from) / (to - from));
    }

    /** Where the part sits against the pulse, as a position. Display only.

        `lead` is −1 when the percussion is behind the clock and +1 when it
        is ahead. A frame of the analysis is 20 ms of jitter (`TempoFollower`),
        and a lean inside that is not something a listener hears as the part
        fighting the song, so the orb stays on the centre until 10 ms and
        reaches the edge — and the red — at 58 ms. The 8 ms phase-lock bench
        is a tighter bar than this.

        The sign is where the part sits, not whether the clock is speeding up
        or slowing down to get there. A positive `phaseErrorBeats` is the
        clock ahead of the song, so the percussion is early and the follower
        decelerates back toward the beat: the orb is on the right and slides
        in to the centre as that error closes. A negative error is the part
        behind, the clock accelerates, and the orb comes back from the left.
        A loop whose read head is past the musical target is a positive
        `loopPhaseMs`, the same right-hand side.

        The clock's rate bend is not scored: that bend is how a small error
        gets closed. `amount` is whether the orb is drawn at all. An empty
        stage stays dark rather than sitting on red before anyone has played. */
    struct TempoBloom
    {
        float lead = 0.0f;
        float amount = 0.0f;
    };

    TempoBloom tempoBloomFor (const vp::EngineSnapshot& s) noexcept
    {
        using B = vp::FollowBar;
        const bool haveTempo = s.bpm > 40.0f;
        const bool hot = stateIsHot (s.followBar);
        const bool weak = s.followBar == B::weakFollow;
        const bool searching = weak
                            || s.followBar == B::listening
                            || s.followBar == B::calibrating
                            || s.followBar == B::recalin
                            || s.followBar == B::waitBeat
                            || s.followBar == B::waitStart;
        const float heard = juce::jmax (s.inputPeak, s.analysisPeak);
        const bool silent = heard < kInputSilentPeak;

        TempoBloom out;
        if (! haveTempo && ! hot && ! searching)
            return out;

        if (silent && ! hot && s.confidence < 0.25f)
        {
            out.lead = 0.0f;
            out.amount = 0.08f;
            return out;
        }

        const float bpm = juce::jmax (40.0f, s.clockBpm > 40.0f ? s.clockBpm : s.bpm);
        // Positive is ahead of the beat. `phaseErrorBeats` is already clock
        // minus song: positive while the part is early and the rate is bent
        // down to walk it back. The loop residual is positive when the read
        // head is past the beat. A playing loop is what is heard. The rate
        // bend itself is not the position — only this residual is.
        float signedMs = s.phaseErrorBeats * 60000.0f / bpm;
        if (s.loopPlaying)
            signedMs = s.loopPhaseMs;

        // 0 inside a small pocket, 1 once the hit is far enough to fight
        // the beat. The old 30–85 ms ramp stayed full green and then the
        // display jumped; this one moves on a smaller error.
        const float mag = unitRamp (std::abs (signedMs), 10.0f, 58.0f);
        const float leadSign = signedMs < 0.0f ? -1.0f : (signedMs > 0.0f ? 1.0f : 0.0f);
        out.lead = leadSign * mag;

        if (hot)
            out.amount = 1.0f;
        else if (haveTempo && (s.hypValid || s.confidence > 0.30f))
            out.amount = 0.82f;
        else if (searching || haveTempo)
            out.amount = 0.62f;
        else
            out.amount = 0.20f;

        if (silent && ! hot)
            out.amount *= 0.35f;

        return out;
    }

    /** One small orb on the centre of the tempo. Green when the part is on
        the pulse; it slides a short way right as the percussion runs ahead
        of the clock and left as it falls behind, and the further it travels
        the redder it burns. */
    void paintTempoOrb (juce::Graphics& g, juce::Rectangle<float> lane,
                        float lead, float alpha)
    {
        if (lane.getWidth() < 8.0f || lane.getHeight() < 8.0f || alpha <= 0.004f)
            return;
        if (! gDarkMode)
            alpha *= 0.62f;

        lead = juce::jlimit (-1.0f, 1.0f, lead);
        const float mag = std::abs (lead);
        const auto red   = juce::Colour (0xffff2a4a);
        const auto amber = juce::Colour (0xffffb01a);
        const auto green = juce::Colour (0xff3dffc2);
        const auto col = mag < 0.42f ? green.interpolatedWith (amber, mag / 0.42f)
                                     : amber.interpolatedWith (red, (mag - 0.42f) / 0.58f);

        // The rail runs from ÷2 to ×2, pulled in a tenth so the extremes
        // sit inside the buttons rather than on them. On the pulse the
        // dot is in the middle.
        const float travel = juce::jmax (0.0f, (lane.getWidth() * 0.5f - 6.0f) * 0.90f);
        const float cx = lane.getCentreX() + lead * travel;
        const float cy = lane.getCentreY();
        // The lane is 14 px tall. Half of that is a 7 px disc, twice the
        // 3.5 px one: the old factor never left the 3.5 floor.
        const float r = juce::jlimit (7.0f, 14.0f, lane.getHeight() * 0.5f);

        auto bloom = [&] (float radius, float a)
        {
            if (radius < 1.0f || a <= 0.004f)
                return;
            juce::ColourGradient grad (col.withAlpha (a), cx, cy,
                                       col.withAlpha (0.0f), cx, cy + radius, true);
            g.setGradientFill (grad);
            g.fillEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
        };

        // A hairline so the slide has a centre to come back to. It stays
        // dim: the orb is the reading, the rail is only the axis.
        g.setColour (col.withAlpha (alpha * 0.16f));
        const float railY = cy - 0.6f;
        g.fillRoundedRectangle (lane.getCentreX() - travel, railY, travel * 2.0f, 1.2f, 0.6f);

        bloom (r * 2.2f, alpha * 0.22f);
        bloom (r * 1.45f, alpha * 0.48f);

        g.setColour (col.withAlpha (alpha));
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);

        const float core = r * (0.42f - 0.10f * mag);
        g.setColour (juce::Colours::white.withAlpha (alpha * (0.92f - 0.40f * mag)));
        g.fillEllipse (cx - core, cy - core, core * 2.0f, core * 2.0f);
    }

    /** Which shared look a button wears. 0: plain rounded key. 1: a segment of
        the 1/4 - 1/8 - 1/16 track. 2: a switch pill with a dot. 3: START/STOP. */
    int buttonStyle (const juce::Button& b)
    {
        return static_cast<int> (b.getProperties().getWithDefault ("btnStyle", 0));
    }

    bool buttonIsVoiceTile (const juce::Button& b)
    {
        return b.getToggleState()
               && (bool) b.getProperties().getWithDefault ("voiceOnFill", false);
    }

    bool buttonIsChip (const juce::Button& b)
    {
        return (bool) b.getProperties().getWithDefault ("chipFill", false);
    }

    /** A saturated fill asks to be "hot" (fuchsia). Voice and kit-sound tiles
        keep their own colour. */
    bool buttonIsHot (const juce::Button& b, juce::Colour fill)
    {
        return ! buttonIsVoiceTile (b) && ! buttonIsChip (b)
               && fill.getSaturation() > 0.35f && fill.getBrightness() > 0.35f;
    }

    /** Plain and segment buttons that are on get the light solid face, so the
        label has to turn dark. */
    bool buttonIsSolidOn (const juce::Button& b, juce::Colour fill)
    {
        const int st = buttonStyle (b);
        if (st == 2 || st == 3)
            return false;
        return b.getToggleState() && ! buttonIsHot (b, fill)
               && ! buttonIsVoiceTile (b) && ! buttonIsChip (b);
    }

    void drawFlatButton (juce::Graphics& g, juce::Button& button, juce::Colour fill,
                         bool down)
    {
        const auto bounds = button.getLocalBounds().toFloat();
        const int style = buttonStyle (button);
        const bool on = button.getToggleState();
        const bool voiceOn = buttonIsVoiceTile (button);
        const bool chip = buttonIsChip (button);
        const bool hot = buttonIsHot (button, fill);
        const bool solidOn = buttonIsSolidOn (button, fill);
        const float round = style >= 2 ? bounds.getHeight() * 0.5f
                                       : juce::jmin (12.0f, bounds.getHeight() * 0.5f);
        const auto body = bounds.reduced (0.5f);
        const auto pressed = [down] (juce::Colour c)
        {
            return down ? c.interpolatedWith (text(), 0.14f) : c;
        };

        if (style == 1)
        {
            // Segment: the track is painted by the page; only the chosen one
            // (or the one under the finger) has a face of its own.
            if (on || down)
            {
                g.setColour (text().withAlpha (on ? 1.0f : 0.28f));
                g.fillRoundedRectangle (bounds.reduced (3.0f), juce::jmin (9.0f, round));
            }
            return;
        }

        if (style == 3)
        {
            // START: a solid pill; STOP: fuchsia, pulsing on the beat.
            juce::Path pill;
            pill.addRoundedRectangle (body, round);
            const float pulse = static_cast<float> (button.getProperties().getWithDefault ("pulse", 0.0));
            g.setColour (pressed (pulse > 0.01f ? fill.interpolatedWith (juce::Colours::white, 0.22f * pulse)
                                                 : fill));
            g.fillPath (pill);
            if (pulse > 0.01f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.55f * pulse));
                g.drawRoundedRectangle (body.reduced (1.0f), round, 1.0f + 2.0f * pulse);
            }
            // Black on a near-black page needs its edge.
            if (fill.getBrightness() < 0.14f)
            {
                g.setColour (border());
                g.drawRoundedRectangle (body, round, 1.0f);
            }
            return;
        }

        if (style == 2)
        {
            // Switch pill: outlined and dotted when on, quiet when off.
            g.setColour (pressed (ink()));
            g.fillRoundedRectangle (body, round);
            g.setColour (on ? text() : border());
            g.drawRoundedRectangle (body, round, on ? 1.4f : 1.0f);
            if (on)
            {
                g.setColour (text());
                g.fillEllipse (bounds.getX() + 14.0f, bounds.getCentreY() - 4.0f, 8.0f, 8.0f);
            }
            return;
        }

        juce::Colour face = ink();
        if (solidOn)                face = text();
        else if (hot)               face = fuchsia();
        else if (voiceOn || chip)   face = fill;
        if (! solidOn)
            face = pressed (face);
        g.setColour (face);
        g.fillRoundedRectangle (body, round);
        if (! solidOn && ! hot && ! voiceOn && ! chip)
        {
            g.setColour (border());
            g.drawRoundedRectangle (body, round, 1.0f);
        }
    }
}

namespace
{
    /** A voice or an effect as a vertical fader: the tile *is* the level, filled
        from the bottom in the part's colour. Same gesture as the disc it
        replaces (vertical drag = level, tap = mute or fire), but a thumb can
        read it at a glance and eight identical dials are gone. Name on top,
        value at the foot; the text flips to dark where the fill reaches it. */
    void paintVoiceFader (juce::Graphics& g, juce::Rectangle<float> area, float pos,
                          const juce::Slider& slider, juce::Colour accent, bool off,
                          bool hitKnob, bool hitLit)
    {
        area = area.reduced (1.0f);
        if (area.getWidth() < 12.0f || area.getHeight() < 24.0f)
            return;
        const float alpha = slider.isEnabled() ? 1.0f : 0.45f;
        const float radius = juce::jlimit (8.0f, 14.0f, area.getWidth() * 0.22f);
        const float level = juce::jlimit (0.0f, 1.0f, pos);

        g.setColour (ink().withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (area, radius);

        if (level > 0.004f)
        {
            juce::Path clip;
            clip.addRoundedRectangle (area, radius);
            g.saveState();
            g.reduceClipRegion (clip);
            g.setColour (accent.withMultipliedAlpha ((off ? 0.16f : 0.92f) * alpha));
            g.fillRect (area.withTrimmedTop (area.getHeight() * (1.0f - level)));
            g.restoreState();
        }

        // A fired effect wears its colour round the edge for as long as it sounds.
        g.setColour ((hitKnob && hitLit ? accent : border()).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (area.reduced (0.5f), radius, hitKnob && hitLit ? 2.4f : 1.0f);

        const juce::Colour onFill = juce::Colour (0xff0a0a0c);
        const bool nameOnFill = ! off && level > 0.88f;
        const bool valueOnFill = ! off && level > 0.14f;
        const juce::String name = slider.getProperties().getWithDefault ("knobName", {}).toString();
        const float nameH = juce::jlimit (11.0f, 16.0f, area.getWidth() * 0.19f);
        const float valueH = juce::jlimit (12.0f, 18.0f, area.getWidth() * 0.22f);

        const auto nameFont = fontUi (nameH);
        g.setFont (nameFont);
        g.setColour ((nameOnFill ? onFill : text()).withMultipliedAlpha ((off ? 0.55f : 1.0f) * alpha));
        g.drawText (ellipsis (name, nameFont, area.getWidth() - 8.0f),
                    area.withHeight (nameH * 1.6f).translated (0.0f, 6.0f).toNearestInt(),
                    juce::Justification::centredTop, false);

        const juce::String valueText = off ? juce::String ("MUTO") : knobValueText (slider);
        g.setFont (fontUi (valueH));
        g.setColour ((valueOnFill ? onFill : mute()).withMultipliedAlpha (alpha));
        g.drawText (valueText,
                    area.withTop (area.getBottom() - valueH * 1.8f).toNearestInt(),
                    juce::Justification::centred, false);
    }
}

namespace
{
    void paintChoice (juce::TextButton& b, bool on)
    {
        b.setToggleState (on, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonColourId, ink());
        b.setColour (juce::TextButton::textColourOffId, text());
    }

    juce::Font noteFont() { return fontUi (11.5f, false); }

    /** The sentence under each group of settings. Shared because layoutSettings
        needs its height and paintSettings needs its text, and a card sized
        against one string and filled with another is how a caption ends up
        clipped on the narrow side of the page. */
    juce::String clockNote()
    {
        return juce::String (juce::CharPointer_UTF8 (
            "AUTO segue il clock dell'interfaccia e non lo tocca: con il mixer a "
            "48 kHz l'app si apre a 48 kHz. Un valore fisso lo chiede "
            "all'interfaccia, e cambiarlo mentre suona la fa ripartire."));
    }

    juce::String bufferNote()
    {
        return juce::String (juce::CharPointer_UTF8 (
            "Piu' corto, meno ritardo e piu' rischio di buchi. AUTO prende quello "
            "che l'interfaccia sta gia' dando."));
    }

    juce::String inputNote()
    {
        return juce::String (juce::CharPointer_UTF8 (
            "MIXER per un microfono vicino o una mandata del banco, IPAD per il "
            "microfono che sente la stanza. BRANO legge un file e lo manda sia "
            "all'analisi sia all'uscita, anche nelle AirPods. CARICA sceglie il "
            "file; l'onda sotto (o sul palco) si trascina per saltare. PLAY "
            "mette in pausa. In IPAD l'app toglie shaker e congas "
            "da quello che ascolta. MIC regola quanto sente l'analisi: alzalo "
            "finche' l'anello del knob entra nella fascia fucsia. Sotto, "
            "l'analisi sbaglia il tempo; sopra, ambra e poi rosso, e non "
            "guadagna piu' niente. "
            "ELAB. OFF toglie guadagno automatico ed eco di iOS: e' quello che "
            "vuole l'analisi."));
    }

    /** How tall that sentence comes out at a given width. Measured rather than
        assumed: the same three sentences wrap to two lines beside a portrait
        iPad and to five in a landscape column half as wide. */
    int noteHeight (const juce::String& text, int width)
    {
        const juce::Font f = noteFont();
        const float total = juce::GlyphArrangement::getStringWidth (f, text);
        // Wrapping breaks at spaces, so a line never fills to the last pixel.
        const float usable = juce::jmax (1.0f, static_cast<float> (width) * 0.94f);
        const int lines = juce::jlimit (1, 8, static_cast<int> (std::ceil (total / usable)));
        return juce::roundToInt (f.getHeight() * 1.28f) * lines;
    }

    constexpr int kStatusLines = 9;
}

namespace
{
    // The compact tempo column, top to bottom, at its natural size. Fixed
    // numbers rather than fractions of the area: the column must not grow to
    // fill whatever it is handed, or the space below the dots becomes a hole.
    //
    // Restyle: the status row is a 44 pt target row (state, SETUP), the number
    // is the biggest thing, and "÷2 · TAP · ×2" share one 56 pt row under the
    // dots so the three controls a player reaches for are at the same height,
    // under the thumb. The note line ("TEMPO FISSO", "a meta (auto)") gets its
    // own row instead of living on three lines of small text.
    constexpr int kCompactPillH  = 44;
    //
    // The tempo, its label, the phase lane, the four quarters and the note all
    // sit in one bordered hero card (see paintStage), 10 pt of padding inside:
    // hence the 22 above it and the 18 below it.
    constexpr int kCompactBpmH   = 128;
    constexpr int kCompactLabelH = 16;
    constexpr int kCompactLaneH  = 22;
    constexpr int kCompactBeatsH = 46;
    constexpr int kCompactNoteH  = 18;
    constexpr int kCompactBarH   = 56;   // ÷2 | TAP | ×2, one row
    constexpr int kCompactGapA   = 22;  // status row -> hero card
    constexpr int kCompactGapB   = 2;   // lane -> dots
    constexpr int kCompactGapC   = 18;  // hero card -> control row
    constexpr int kCompactTempoNatural =
        kCompactPillH + kCompactGapA + kCompactBpmH + kCompactLabelH + kCompactLaneH
        + kCompactGapB + kCompactBeatsH + kCompactNoteH + kCompactGapC + kCompactBarH;
}

namespace
{
    // Row 0 is AUTO; every row after it is `vp::toString (GrooveStyle (index - 1))`
    // - one source for the label text, so a style added to the enum shows up
    // here with its real name instead of "?".
    const char* styleMenuLabel (int index) noexcept
    {
        if (index == 0)
            return "AUTO";
        return vp::toString (static_cast<vp::GrooveStyle> (index - 1));
    }
}

#if defined (__clang__)
 #pragma clang diagnostic pop
#elif defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif
