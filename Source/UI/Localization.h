#pragma once

// English and Italian for everything the player reads. The device language
// decides (iOS per-app language included, as CFBundleLocalizations declares
// both); anything that is not Italian gets English.
//
// Convention: L (english, italian). The Italian is the text the app always had,
// verbatim; the English is new. Words that are the same in both (START, STOP,
// TAP, SETUP, BPM, AUTO...) are written once, without L. Core/ keeps its own
// names (toString, toBarString): tests and probes match on them, so the UI maps
// them here instead.

#include "Core/Types.h"

#include <juce_core/juce_core.h>

namespace vpui
{

/** -1 follow the device, 0 English, 1 Italian. For tests and the preview tool. */
inline int& languageOverride() noexcept
{
    static int v = -1;
    return v;
}

inline bool italian()
{
    if (languageOverride() >= 0)
        return languageOverride() == 1;
    static const bool system = juce::SystemStats::getUserLanguage().startsWithIgnoreCase ("it");
    return system;
}

inline juce::String L (const char* english, const char* italianText)
{
    return juce::String (juce::CharPointer_UTF8 (italian() ? italianText : english));
}

inline juce::String styleName (vp::GrooveStyle s)
{
    switch (s)
    {
        case vp::GrooveStyle::marcha: return L ("MARCH", "MARCHA");
        case vp::GrooveStyle::twoOne: return L ("TWO-ONE", "DUE-UNO");
        default:                      break;
    }
    return juce::String (vp::toString (s));
}

/** The 3/4 parts (METRO). */
inline juce::String style3Name (vp::GrooveStyle3 s)
{
    switch (s)
    {
        case vp::GrooveStyle3::waltz:  return L ("WALTZ", "VALZER");
        case vp::GrooveStyle3::ballad: return L ("BALLAD", "BALLAD");
        case vp::GrooveStyle3::simple: return L ("SIMPLE", "SEMPLICE");
        case vp::GrooveStyle3::count:  break;
    }
    return juce::String (vp::toString (s));
}

inline juce::String kitName (vp::KitSound s)
{
    switch (s)
    {
        case vp::KitSound::cembalo:  return L ("TAMBOURINE", "CEMBALO");
        case vp::KitSound::triangle: return L ("TRIANGLE", "TRIANGOLO");
        default:                     break;
    }
    return juce::String (vp::toString (s));
}

/** The words beside the state light, from Core's Italian. */
inline juce::String barLabel (vp::FollowBar b)
{
    using B = vp::FollowBar;
    // Core still calls it "SHAKER OFF" (probes match on that), the shaker's
    // old name for the part: on the stage it read as the shaker voice being
    // switched off, beside a SHAKER fader at 88%.
    if (b == B::paused)
        return L ("LISTENING - PART STOPPED", "IN ASCOLTO - PARTE FERMA");
    const char* en = nullptr;
    switch (b)
    {
        case B::ready:           en = "READY"; break;
        case B::listening:       en = "LISTENING"; break;
        case B::calibrating:     en = "CALIBRATING"; break;
        case B::followingListen: en = "FOLLOWING AND LISTENING"; break;
        case B::following:       en = "FOLLOWING"; break;
        case B::tapAlign:        en = "ALIGNING TAP"; break;
        case B::weakFollow:      en = "TEMPO UNSURE"; break;
        case B::recalin:         en = "RECALIBRATING"; break;
        case B::waitBeat:        en = "WAITING FOR THE BEAT"; break;
        case B::waitStart:       en = "WAITING FOR THE BAND"; break;
        case B::paused:          break;   // above
    }
    return L (en != nullptr ? en : "?", vp::toBarString (b));
}

} // namespace vpui
