#include "UI/MainComponent.h"
#include "Platform/IosMicPermission.h"

#include <cmath>
#include <functional>

namespace
{
    bool gDarkMode = true;

    juce::Colour bg()      { return gDarkMode ? juce::Colour (0xff050506) : juce::Colour (0xfff5f1f6); }
    juce::Colour panel()   { return gDarkMode ? juce::Colour (0xff0c0c0e) : juce::Colour (0xffffffff); }
    // The surface a control sits on. In light this has to *be* light: it was a
    // near-black in both themes, and since every button is filled with it the
    // light theme came out as a light background behind a wall of black
    // buttons - which is the whole reason "light" still looked dark.
    juce::Colour ink()     { return gDarkMode ? juce::Colour (0xff121214) : juce::Colour (0xffe8e3ea); }
    juce::Colour text()    { return gDarkMode ? juce::Colours::white : juce::Colour (0xff18141b); }
    juce::Colour sliderTrack() { return gDarkMode ? ink() : juce::Colour (0xffd9d2dc); }
    juce::Colour fuchsia() { return juce::Colour (0xffff2ec8); }
    juce::Colour mute()    { return gDarkMode ? juce::Colour (0xffa8a8b4) : juce::Colour (0xff655e6a); }
    juce::Font fontDisplay (float h)
    {
        return juce::Font (juce::FontOptions().withName ("Futura").withStyle ("Bold").withHeight (h));
    }

    juce::Font fontUi (float h, bool bold = true)
    {
        return juce::Font (juce::FontOptions().withName ("Avenir Next")
                               .withStyle (bold ? "Bold" : "Medium")
                               .withHeight (h));
    }

    juce::Colour stateColour (vp::FollowBar b)
    {
        using B = vp::FollowBar;
        switch (b)
        {
            case B::following:       return fuchsia();
            case B::followingListen: return fuchsia();
            case B::calibrating:     return text();
            case B::listening:       return text();
            case B::tapAlign:        return fuchsia();
            case B::waitBeat:        return fuchsia().withAlpha (0.75f);
            case B::waitStart:       return text();
            case B::weakFollow:      return text();
            case B::recalin:         return text();
            case B::paused:          return mute();
            case B::ready:           return mute();
        }
        return mute();
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

    void drawFlatButton (juce::Graphics& g, juce::Button& button, juce::Colour fill,
                         bool down)
    {
        auto bounds = button.getLocalBounds().toFloat();
        const bool hotFill = fill.getSaturation() > 0.35f && fill.getBrightness() > 0.35f;
        const bool active = button.getToggleState() || down || hotFill;

        g.setColour (hotFill || down ? fuchsia() : ink());
        g.fillRect (bounds);

        // PARTE / STRUMENTI are a row of squares: without an edge they read as
        // one bar. START/STOP stay flush - they are wide enough to be a pair.
        const bool compact = button.getWidth() <= button.getHeight() + 8;
        if (compact)
        {
            g.setColour (text().withAlpha (gDarkMode ? 0.22f : 0.28f));
            g.drawRect (bounds.reduced (0.5f), 1.0f);
        }

        if (active)
        {
            const float h = 3.0f;
            g.setColour (hotFill || down ? juce::Colours::white : fuchsia());
            g.fillRect (bounds.getX(), bounds.getBottom() - h, bounds.getWidth(), h);
        }
    }
}

MainComponent::AppLookAndFeel::AppLookAndFeel()
{
    refreshColours();
}

void MainComponent::AppLookAndFeel::refreshColours()
{
    // Not white in both themes: on a light surface white lettering is
    // invisible. The hot fill is the exception and sets its own.
    setColour (juce::TextButton::textColourOffId, text());
    setColour (juce::TextButton::textColourOnId, text());
    setColour (juce::Label::textColourId, mute());
}

juce::Font MainComponent::AppLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    const float dim = juce::jmin ((float) buttonHeight, (float) juce::jmax (1, button.getWidth()));
    return fontUi (juce::jmax (9.0f, dim * 0.28f));
}

void MainComponent::AppLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                                    bool, bool)
{
    const bool compact = button.getWidth() <= button.getHeight() + 8;
    auto area = button.getLocalBounds().reduced (compact ? 2 : 6, compact ? 2 : 4);
    if (area.isEmpty())
        return;

    const juce::String label = button.getButtonText();
    juce::Font f = getTextButtonFont (button, button.getHeight());
    const float w = juce::GlyphArrangement::getStringWidth (f, label);
    const float room = static_cast<float> (area.getWidth());
    if (w > room && w > 1.0f)
        f = f.withHeight (juce::jmax (7.0f, f.getHeight() * room / w));

    g.setFont (f);
    const float alpha = button.isEnabled() ? (compact && ! button.getToggleState() ? 0.55f : 1.0f) : 0.5f;
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (alpha));
    if (compact)
        g.drawFittedText (label, area, juce::Justification::centred, 2);
    else
        g.drawText (label, area, juce::Justification::centred, false);
}

void MainComponent::AppLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                          const juce::Colour& backgroundColour,
                                                          bool, bool shouldDrawButtonAsDown)
{
    drawFlatButton (g, button, backgroundColour, shouldDrawButtonAsDown);
}

int MainComponent::AppLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    if (slider.isRotary())
        return juce::jmax (8, juce::jmin (slider.getWidth(), slider.getHeight()) / 6);
    return slider.isVertical() ? 13 : 18;
}

void MainComponent::AppLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                                      float sliderPos, float minSliderPos, float maxSliderPos,
                                                      juce::Slider::SliderStyle style, juce::Slider&)
{
    const bool vertical = style == juce::Slider::LinearVertical
                          || style == juce::Slider::LinearBarVertical;

    if (vertical)
    {
        const float trackW = 10.0f;
        const float cx = static_cast<float> (x) + 0.5f * static_cast<float> (width);
        juce::Rectangle<float> track (cx - trackW * 0.5f, static_cast<float> (y),
                                      trackW, static_cast<float> (height));

        g.setColour (text().withAlpha (0.10f));
        g.fillRoundedRectangle (track.expanded (2.0f, 1.0f), 7.0f);
        g.setColour (sliderTrack());
        g.fillRoundedRectangle (track, 5.0f);

        const float bottom = juce::jmax (track.getY(), maxSliderPos);
        const float top = juce::jlimit (track.getY(), track.getBottom(), sliderPos);
        juce::ignoreUnused (minSliderPos);
        auto filled = juce::Rectangle<float>::leftTopRightBottom (track.getX(), top,
                                                                  track.getRight(), bottom);
        if (filled.getHeight() > 1.0f)
        {
            juce::ColourGradient fill (fuchsia().darker (0.08f), filled.getX(), filled.getBottom(),
                                       fuchsia().brighter (0.18f), filled.getX(), filled.getY(), false);
            g.setGradientFill (fill);
            g.fillRoundedRectangle (filled, 5.0f);
        }

        const float capW = 34.0f;
        const float capH = 16.0f;
        juce::Rectangle<float> cap (cx - capW * 0.5f, sliderPos - capH * 0.5f, capW, capH);
        paintRadial (g, { cx, sliderPos }, 22.0f, fuchsia(), 0.28f);
        g.setColour (gDarkMode ? juce::Colour (0xff2a2a30) : juce::Colour (0xfff3eef4));
        g.fillRoundedRectangle (cap.translated (0.0f, 1.5f), 4.0f);
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (cap, 4.0f);
        g.setColour (fuchsia());
        g.fillRoundedRectangle (cap.removeFromTop (3.5f), 2.0f);
        return;
    }

    const float trackH = 12.0f;
    const float cy = static_cast<float> (y) + 0.5f * static_cast<float> (height);
    juce::Rectangle<float> track (static_cast<float> (x), cy - trackH * 0.5f,
                                  static_cast<float> (width), trackH);

    g.setColour (text().withAlpha (0.10f));
    g.fillRoundedRectangle (track.expanded (1.5f), 8.0f);
    g.setColour (sliderTrack());
    g.fillRoundedRectangle (track, 6.0f);

    const float fillW = juce::jmax (0.0f, sliderPos - track.getX());
    if (fillW > 1.0f)
    {
        auto filled = track.withWidth (fillW);
        juce::ColourGradient fill (fuchsia().brighter (0.18f), filled.getX(), filled.getY(),
                                   fuchsia(), filled.getRight(), filled.getY(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (filled, 6.0f);
    }

    juce::ignoreUnused (minSliderPos, maxSliderPos);

    const float tr = 16.0f;
    paintRadial (g, { sliderPos, cy }, 26.0f, fuchsia(), 0.22f);
    g.setColour (fuchsia());
    g.drawEllipse (sliderPos - tr, cy - tr, tr * 2.0f, tr * 2.0f, 2.4f);
    g.setColour (juce::Colours::white);
    g.fillEllipse (sliderPos - tr + 2.6f, cy - tr + 2.6f, (tr - 2.6f) * 2.0f, (tr - 2.6f) * 2.0f);
}

void MainComponent::AppLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                                      float sliderPos, float rotaryStartAngle,
                                                      float rotaryEndAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    if (radius < 6.0f)
        return;

    const auto centre = bounds.getCentre();
    const float toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float lineW = juce::jlimit (4.5f, 9.0f, radius * 0.18f);
    const float arcRadius = radius - lineW * 0.5f;
    const float alpha = slider.isEnabled() ? 1.0f : 0.45f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (text().withAlpha (0.10f * alpha));
    g.strokePath (track, juce::PathStrokeType (lineW + 3.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setColour (sliderTrack().withMultipliedAlpha (alpha));
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    if (slider.isEnabled() && sliderPos > 0.002f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             rotaryStartAngle, toAngle, true);
        g.setColour (fuchsia());
        g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    const float innerR = juce::jmax (6.0f, arcRadius - lineW * 0.7f);
    paintRadial (g, centre, innerR * 1.7f, fuchsia(), 0.14f * alpha);
    g.setColour (juce::Colour (0xff0a0a0c).withMultipliedAlpha (alpha));
    g.fillEllipse (centre.x - innerR, centre.y - innerR, innerR * 2.0f, innerR * 2.0f);
    g.setColour (juce::Colour (0xff2a2a30).withMultipliedAlpha (alpha));
    g.drawEllipse (centre.x - innerR, centre.y - innerR, innerR * 2.0f, innerR * 2.0f, 1.2f);

    const float pointerLen = innerR * 0.70f;
    const float pointerW = juce::jlimit (2.4f, 4.0f, innerR * 0.14f);
    const auto tip = juce::Point<float> (
        centre.x + pointerLen * std::cos (toAngle - juce::MathConstants<float>::halfPi),
        centre.y + pointerLen * std::sin (toAngle - juce::MathConstants<float>::halfPi));
    g.setColour (fuchsia().withMultipliedAlpha (alpha));
    g.drawLine (centre.x, centre.y, tip.x, tip.y, pointerW);
    g.fillEllipse (centre.x - pointerW, centre.y - pointerW, pointerW * 2.0f, pointerW * 2.0f);
}

MainComponent::MainComponent()
{
    darkMode = juce::Desktop::getInstance().isDarkModeActive();
    gDarkMode = darkMode;
    appLaf.refreshColours();
    juce::Desktop::getInstance().addDarkModeSettingListener (this);

    setOpaque (true);
    // iPad portrait by default, but never larger than the display it has to
    // live on: on a desktop the old fixed size ran off the bottom of a short
    // screen, taking the feel controls with it.
    {
        int w = 834, h = 1112;
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto area = display->userArea;
            w = juce::jmin (w, juce::jmax (600, area.getWidth() - 40));
            h = juce::jmin (h, juce::jmax (520, area.getHeight() - 60));
        }
        setSize (w, h);
    }
    setLookAndFeel (&appLaf);

    addAndMakeVisible (tapZone);

    auto setupBtn = [this] (juce::TextButton& b, juce::Colour fill)
    {
        addAndMakeVisible (b);
        b.setColour (juce::TextButton::buttonColourId, fill);
        b.setColour (juce::TextButton::textColourOffId, text());
        b.setColour (juce::TextButton::buttonOnColourId, fill.brighter (0.18f));
    };

    setupBtn (startButton, ink());
    setupBtn (stopButton, ink());
    setupBtn (followButton, ink());
    setupBtn (fixedButton, ink());
    setupBtn (bpmNudgeDown, ink());
    setupBtn (bpmNudgeUp, ink());
    setupBtn (shakerButton, ink());
    setupBtn (settingsButton, juce::Colour (0xff0a0a0c));
    setupBtn (congasButton, ink());
    setupBtn (styleAuto, ink());
    setupBtn (styleMarcha, ink());
    setupBtn (styleRock, ink());
    setupBtn (styleDance, ink());
    setupBtn (stylePop, ink());
    setupBtn (styleSamba, ink());
    setupBtn (styleFunk, ink());
    setupBtn (styleReggae, ink());
    setupBtn (styleBossa, ink());
    setupBtn (subAuto, ink());
    setupBtn (halveButton, ink());
    setupBtn (doubleButton, ink());
    setupBtn (barButton, ink());

    // Where beat one is cannot be read reliably from what the network gives us,
    // so this moves it on by one. Same answer as the octave pair: a measurement
    // that will not come out is offered to the listener instead of guessed at.
    //
    // And having moved it, the listener owns it: the button lights and the
    // automatic alignment stops touching the count. Four taps take the one all
    // the way round the bar and back to where it started; the fifth hands it
    // back to the app. That is the octave pair's idiom on one button - the
    // button that turned the automatic answer off is the button that turns it
    // on - and it is the only shape that fits, because this button also has to
    // stay free to nudge two or three times in a row.
    barButton.onClick = [this]
    {
        auto& s = engine.settings();
        if (s.barLocked.load() && barTapsSinceLock >= 4)
        {
            s.barLocked.store (false);
            barTapsSinceLock = 0;
        }
        else
        {
            s.barNudge.fetch_add (1);
            s.barLocked.store (true);
            ++barTapsSinceLock;
        }
        refreshBarButton();
    };


    // Half and double. The one part of the metrical level the signal does not
    // decide - full eighths under a slow tempo read as well at the double - is
    // decided here instead, by the person listening, in one tap.
    // Lit means "the listener chose this". Pressing the lit one hands the
    // choice back rather than parking on "as measured": with AUTO in the middle
    // there is no third state to reach otherwise, and the button that turned it
    // off has to be the button that turns it on.
    halveButton.onClick = [this]
    {
        const bool mine = ! engine.settings().tempoOctaveAuto.load()
                          && engine.settings().tempoOctave.load() < 0;
        if (mine) applyTempoOctaveAuto();
        else      applyTempoOctave (-1);
    };
    doubleButton.onClick = [this]
    {
        const bool mine = ! engine.settings().tempoOctaveAuto.load()
                          && engine.settings().tempoOctave.load() > 0;
        if (mine) applyTempoOctaveAuto();
        else      applyTempoOctave (1);
    };
    setupBtn (sub4, ink());
    setupBtn (sub8, ink());
    setupBtn (sub16, ink());

    startButton.onClick = [this] { startPressed(); };
    stopButton.onClick = [this] { stopPressed(); };
    followButton.onClick = [this] { applyTempoFollow (true); };
    fixedButton.onClick = [this] { applyTempoFollow (false); };
    bpmNudgeDown.onClick = [this] { nudgeFixedBpm (-1.0f); };
    bpmNudgeUp.onClick = [this] { nudgeFixedBpm (1.0f); };
    shakerButton.onClick = [this]
    {
        const bool on = ! engine.settings().shakerEnabled.load();
        engine.settings().shakerEnabled.store (on);
        shakerButton.setToggleState (on, juce::dontSendNotification);
        savePrefs();
    };
    debugButton.onClick = [this] {
        debugOpen = ! debugOpen;
        debugButton.setToggleState (debugOpen, juce::dontSendNotification);
        // The panel is drawn over the stage, so opening it from the settings
        // page means leaving the settings page - otherwise the tap looks like
        // it did nothing.
        setSettingsOpen (false);
        repaint();
    };
    clickButton.onClick = [this]
    {
        static bool click = false;
        click = ! click;
        engine.setClickInjectEnabled (click);
        engine.setClickInjectBpm (120.0f);
        clickButton.setButtonText (click ? "CLICK  ON" : "CLICK TEST");
        clickButton.setToggleState (click, juce::dontSendNotification);
    };
    themeButton.onClick = [this] { applyTheme (! darkMode, true); savePrefs(); };
    sourceButton.onClick = [this]
    {
        const bool speaker = engine.settings().followSource.load()
                             != static_cast<int> (vp::FollowSource::speaker);
        engine.settings().followSource.store (static_cast<int> (
            speaker ? vp::FollowSource::speaker : vp::FollowSource::kitMic));
        refreshSourceButton();
        savePrefs();
    };

    congasButton.onClick = [this]
    {
        const bool on = ! engine.settings().congasEnabled.load();
        engine.settings().congasEnabled.store (on);
        congasButton.setToggleState (on, juce::dontSendNotification);
        savePrefs();
    };

    styleAuto.onClick   = [this] { applyStyleAuto (! engine.settings().grooveAuto.load()); };
    styleMarcha.onClick = [this] { applyStyle (vp::GrooveStyle::marcha); };
    styleRock.onClick   = [this] { applyStyle (vp::GrooveStyle::rock); };
    styleDance.onClick  = [this] { applyStyle (vp::GrooveStyle::dance); };
    stylePop.onClick    = [this] { applyStyle (vp::GrooveStyle::pop); };
    styleSamba.onClick  = [this] { applyStyle (vp::GrooveStyle::samba); };
    styleFunk.onClick   = [this] { applyStyle (vp::GrooveStyle::funk); };
    styleReggae.onClick = [this] { applyStyle (vp::GrooveStyle::reggae); };
    styleBossa.onClick  = [this] { applyStyle (vp::GrooveStyle::bossa); };

    subAuto.onClick = [this] { applySubdivision (vp::Subdivision::autoDetect); };
    sub4.onClick    = [this] { applySubdivision (vp::Subdivision::quarter); };
    sub8.onClick    = [this] { applySubdivision (vp::Subdivision::eighth); };
    sub16.onClick   = [this] { applySubdivision (vp::Subdivision::sixteenth); };

    auto setupFader = [this] (juce::Slider& s, juce::Label& name, juce::Label& value,
                              const char* title, double minV, double maxV, double initial,
                              double dblClick, std::function<void (float)> apply)
    {
        addAndMakeVisible (name);
        name.setText (title, juce::dontSendNotification);
        name.setJustificationType (juce::Justification::centred);
        name.setColour (juce::Label::textColourId, mute());
        name.setFont (fontUi (11.0f));
        name.setInterceptsMouseClicks (false, false);

        addAndMakeVisible (value);
        value.setJustificationType (juce::Justification::centred);
        value.setColour (juce::Label::textColourId, fuchsia());
        value.setFont (fontUi (13.0f));
        value.setInterceptsMouseClicks (false, false);
        value.setText (juce::String (juce::roundToInt (initial * 100.0)) + "%",
                       juce::dontSendNotification);

        addAndMakeVisible (s);
        // Vertical drag, not circular: the old faders were up/down, and a
        // finger cannot describe an arc on a 50-point disc.
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                               juce::MathConstants<float>::pi * 2.8f, true);
        s.setMouseDragSensitivity (180);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (minV, maxV, 0.01);
        s.setValue (initial, juce::dontSendNotification);
        s.setDoubleClickReturnValue (true, dblClick);
        auto* sp = &s;
        auto* valueLab = &value;
        s.onValueChange = [this, sp, valueLab, apply]
        {
            const float v = static_cast<float> (sp->getValue());
            apply (v);
            valueLab->setText (juce::String (juce::roundToInt (static_cast<double> (v) * 100.0)) + "%",
                               juce::dontSendNotification);
            savePrefs (false);
        };
        s.onDragEnd = [this] { savePrefs(); };
    };

    setupFader (mixSlider, mixLabel, mixValue, "SHAKER",
                0.0, 1.0, 0.50, 0.50,
                [this] (float v) { engine.settings().instrumentMix.store (v); });
    mixValue.setText ("CONGAS", juce::dontSendNotification);
    mixSlider.onValueChange = [this]
    {
        const float v = static_cast<float> (mixSlider.getValue());
        engine.settings().instrumentMix.store (v);
        refreshMixLabels();
        savePrefs (false);
    };
    setupFader (inputGainSlider, inputGainLabel, inputGainValue, "MIC",
                0.0, 2.0, 1.00, 1.0,
                [this] (float v) { engine.settings().inputGain.store (v); });
    setupFader (swingSlider, swingLabel, swingValue, "SWING",
                0.0, 1.0, 0.00, 0.0,
                [this] (float v) { engine.settings().swing.store (v); });
    setupFader (intensitySlider, intensityLabel, intensityValue, "ENERGIA",
                0.0, 1.0, 0.50, 0.50,
                [this] (float v) { engine.settings().intensity.store (v); });
    setupFader (reverbSlider, reverbLabel, reverbValue, "REVERB",
                0.0, 1.0, 0.30, 0.30,
                [this] (float v) { engine.settings().reverbAmount.store (v); });

    addAndMakeVisible (bpmEdit);
    bpmEdit.setJustificationType (juce::Justification::centred);
    bpmEdit.setColour (juce::Label::textColourId, fuchsia());
    bpmEdit.setColour (juce::Label::backgroundColourId, ink());
    bpmEdit.setFont (fontUi (16.0f));
    bpmEdit.setEditable (true, true, false);
    bpmEdit.setText ("120", juce::dontSendNotification);
    bpmEdit.onTextChange = [this]
    {
        const float v = bpmEdit.getText().getFloatValue();
        if (v >= 50.0f && v <= 200.0f)
        {
            engine.setFixedBpm (v);
            refreshTempoModeButtons();
            savePrefs (false);
        }
    };
    bpmEdit.onEditorHide = [this] { savePrefs(); };

    // The settings page and everything on it. Added after the console so it is
    // the last child: a full-bounds opaque child on top is what makes the page
    // a page rather than a set of buttons drawn over the transport.
    addChildComponent (settingsOverlay);
    settingsOverlay.toFront (false);

    auto setupPageBtn = [this] (juce::TextButton& b, juce::Colour fill)
    {
        settingsOverlay.addAndMakeVisible (b);
        b.setColour (juce::TextButton::buttonColourId, fill);
        b.setColour (juce::TextButton::textColourOffId, text());
        b.setColour (juce::TextButton::buttonOnColourId, fill.brighter (0.18f));
    };

    setupPageBtn (settingsClose, ink());
    setupPageBtn (debugButton, juce::Colour (0xff0a0a0c));
    setupPageBtn (clickButton, juce::Colour (0xff0a0a0c));
    setupPageBtn (themeButton, ink());
    setupPageBtn (sourceButton, ink());
    setupPageBtn (procButton, ink());
    for (auto* b : { &clockAuto, &clock44, &clock48, &clock88, &clock96,
                     &bufAuto, &buf64, &buf128, &buf256, &buf512 })
        setupPageBtn (*b, ink());

    settingsButton.onClick = [this] { setSettingsOpen (true); };
    settingsClose.onClick  = [this] { setSettingsOpen (false); };

    clockAuto.onClick = [this] { applyClock (0); };
    clock44.onClick   = [this] { applyClock (44100); };
    clock48.onClick   = [this] { applyClock (48000); };
    clock88.onClick   = [this] { applyClock (88200); };
    clock96.onClick   = [this] { applyClock (96000); };

    bufAuto.onClick = [this] { applyBufferChoice (0); };
    buf64.onClick   = [this] { applyBufferChoice (64); };
    buf128.onClick  = [this] { applyBufferChoice (128); };
    buf256.onClick  = [this] { applyBufferChoice (256); };
    buf512.onClick  = [this] { applyBufferChoice (512); };

    procButton.onClick = [this] { applyInputProcessingChoice (! inputProcessing); };

   #if JUCE_IOS
    engine.settings().followSource.store (static_cast<int> (vp::FollowSource::speaker));
   #else
    engine.settings().followSource.store (static_cast<int> (vp::FollowSource::kitMic));
   #endif
    // 0.00 is a sequencer: dead on the grid, every stroke the same weight.
    // A percussionist is neither, and this is the single setting that decides
    // which of the two the app sounds like.
    engine.settings().humanization.store (0.35f);
    engine.settings().intensity.store (0.50f);
    engine.settings().swing.store (0.00f);
    engine.settings().masterVolume.store (0.90f);
    engine.settings().percussionVolume.store (1.00f);
    engine.settings().instrumentMix.store (0.50f);
    engine.settings().followStrength.store (static_cast<int> (vp::FollowStrength::high));
    engine.settings().subdivision.store (static_cast<int> (vp::Subdivision::eighth));
    engine.settings().reverbAmount.store (0.30f);
    shakerButton.setToggleState (true, juce::dontSendNotification);
    congasButton.setToggleState (true, juce::dontSendNotification);

    // Before the device is opened: the stored clock is what it has to be opened
    // at, and asking for it after the fact is the reconfiguration this change
    // exists to remove.
    loadPrefs();

    refreshStartButton();
    refreshStyleButtons();
    refreshSubdivisionButtons();
    refreshThemeColours();

    startTimerHz (15);

    {
        juce::Component::SafePointer<MainComponent> safeReset (this);
        vp::setMediaServicesResetHandler ([safeReset]
        {
            if (safeReset != nullptr)
                safeReset->rebuildAudioDevice ("media services were reset");
        });
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    vp::requestMicrophoneAccess ([safe] (bool granted)
    {
        juce::MessageManager::callAsync ([safe, granted]
        {
            if (safe != nullptr)
                safe->openAudioDevice (granted);
        });
    });
}

MainComponent::~MainComponent()
{
    savePrefs();
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
    setLookAndFeel (nullptr);
    stopTimer();
    shutdownAudio();
}

void MainComponent::darkModeSettingChanged()
{
    if (themeFollowsSystem)
        applyTheme (juce::Desktop::getInstance().isDarkModeActive(), false);
}


void MainComponent::applyTheme (bool dark, bool manualOverride)
{
    darkMode = dark;
    if (manualOverride)
        themeFollowsSystem = false;
    gDarkMode = darkMode;
    appLaf.refreshColours();
    refreshThemeColours();
    repaint();
}

void MainComponent::refreshThemeColours()
{
    juce::TextButton* buttons[] = {
        &startButton, &stopButton, &followButton, &fixedButton,
        &bpmNudgeDown, &bpmNudgeUp, &shakerButton, &debugButton,
        &clickButton, &themeButton, &sourceButton, &subAuto, &sub4, &sub8,
        &sub16, &congasButton, &styleAuto, &styleMarcha, &styleRock,
        &styleDance, &stylePop, &styleSamba, &styleFunk, &styleReggae,
        &styleBossa, &halveButton, &doubleButton, &barButton,
        &settingsButton, &settingsClose, &procButton,
        &clockAuto, &clock44, &clock48, &clock88, &clock96,
        &bufAuto, &buf64, &buf128, &buf256, &buf512
    };
    for (auto* button : buttons)
    {
        button->setColour (juce::TextButton::buttonColourId, ink());
        button->setColour (juce::TextButton::buttonOnColourId,
                           gDarkMode ? ink().brighter (0.18f) : ink().darker (0.06f));
        button->setColour (juce::TextButton::textColourOffId, text());
        button->setColour (juce::TextButton::textColourOnId, text());
    }

    reverbLabel.setColour (juce::Label::textColourId, mute());
    reverbValue.setColour (juce::Label::textColourId, fuchsia());
    swingLabel.setColour (juce::Label::textColourId, mute());
    swingValue.setColour (juce::Label::textColourId, fuchsia());
    intensityLabel.setColour (juce::Label::textColourId, mute());
    intensityValue.setColour (juce::Label::textColourId, fuchsia());
    refreshMixLabels();
    inputGainLabel.setColour (juce::Label::textColourId, mute());
    inputGainValue.setColour (juce::Label::textColourId, fuchsia());
    bpmEdit.setColour (juce::Label::textColourId, fuchsia());
    bpmEdit.setColour (juce::Label::backgroundColourId, ink());
    themeButton.setButtonText (darkMode ? "DARK" : "LIGHT");
    themeButton.setToggleState (! darkMode, juce::dontSendNotification);

    refreshStartButton();
    refreshStyleButtons();
    refreshSubdivisionButtons();
    refreshOctaveButtons();
    refreshBarButton();
    refreshTempoModeButtons();
    refreshClockButtons();
    refreshBufferButtons();
    refreshSourceButton();
    refreshProcButton();
}

void MainComponent::startPressed()
{
    ensureMicrophone();
    userWantsArmed = true;
    engine.start();
    refreshStartButton();
}

void MainComponent::stopPressed()
{
    userWantsArmed = false;
    engine.stop();
    refreshStartButton();
}

void MainComponent::tapPressed()
{
    ensureMicrophone();
    engine.tap();
    tapFlash = 2;
}

void MainComponent::applyTempoFollow (bool follow)
{
    if (follow)
        engine.setTempoFollow (true);
    else
        engine.setTempoFollow (false);
    refreshTempoModeButtons();
    resized();
    savePrefs();
    repaint();
}

void MainComponent::nudgeFixedBpm (float delta)
{
    float bpm = snap.bpm > 50.0f ? snap.bpm
                                 : engine.settings().userBpm.load();
    if (bpm < 50.0f)
        bpm = 120.0f;
    engine.setFixedBpm (bpm + delta);
    refreshTempoModeButtons();
    resized();
    savePrefs();
    repaint();
}

void MainComponent::refreshTempoModeButtons()
{
    const bool follow = engine.settings().tempoFollow.load();
    auto paint = [] (juce::TextButton& b, bool on)
    {
        b.setToggleState (on, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonColourId, on ? fuchsia() : ink());
        b.setColour (juce::TextButton::textColourOffId, on ? juce::Colours::white : text());
    };
    paint (followButton, follow);
    paint (fixedButton, ! follow);

    const bool showNudge = ! follow;
    bpmNudgeDown.setVisible (showNudge);
    bpmNudgeUp.setVisible (showNudge);
    bpmEdit.setVisible (showNudge);
    if (showNudge && ! bpmEdit.isBeingEdited())
    {
        const float bpm = snap.bpm > 50.0f ? snap.bpm
                                           : engine.settings().userBpm.load();
        bpmEdit.setText (juce::String (bpm, 1), juce::dontSendNotification);
    }
}

void MainComponent::refreshStartButton()
{
    startButton.setColour (juce::TextButton::buttonColourId, userWantsArmed ? fuchsia() : ink());
    // White reads on the fuchsia fill and nowhere else.
    startButton.setColour (juce::TextButton::textColourOffId,
                           userWantsArmed ? juce::Colours::white : text());
    startButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    startButton.setToggleState (userWantsArmed, juce::dontSendNotification);
}

void MainComponent::ensureMicrophone()
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    const int nIn = dev != nullptr ? dev->getActiveInputChannels().countNumberOfSetBits() : 0;
    if (nIn > 0)
        return;

    juce::Component::SafePointer<MainComponent> safe (this);
    vp::requestMicrophoneAccess ([safe] (bool granted)
    {
        juce::MessageManager::callAsync ([safe, granted]
        {
            if (safe != nullptr)
                safe->openAudioDevice (granted);
        });
    });
}

double MainComponent::requestedSampleRate() const
{
    return clockHz > 0 ? static_cast<double> (clockHz) : 0.0;
}

int MainComponent::requestedBufferFrames() const
{
    return bufferChoice > 0 ? bufferChoice : 0;
}

double MainComponent::deviceSampleRate() const
{
    if (clockHz > 0)
        return static_cast<double> (clockHz);

    // AUTO. Not a fallback to 48 k: the number the session reports is the one
    // the interface is clocked at, and handing JUCE exactly that number is what
    // makes opening the device cost nothing. Off-device it is 0, and 0 travels
    // all the way out as "do not write a rate anywhere".
    const double hw = vp::sessionSampleRate();
    return hw > 8000.0 && hw <= 192000.0 ? hw : 0.0;
}

int MainComponent::deviceBufferFrames() const
{
    if (bufferChoice > 0)
        return bufferChoice;

    const int hw = vp::sessionBufferFrames();
    return hw >= 32 && hw <= 8192 ? hw : 0;
}

void MainComponent::openAudioDevice (bool granted)
{
    micGranted = granted;
    const int ins = granted ? 2 : 0;

    if (! audioOpened)
    {
        audioOpened = true;

        // Session first, device second. It used to be the other way round -
        // open at whatever came out, then set the category, then ask for 48 kHz
        // and a 256-frame buffer whatever the interface was on, then close and
        // reopen - and every one of those steps re-clocks an interface the whole
        // room is listening through. With the hardware already settled, the open
        // below lands on it and applyAudioSetup has nothing left to change.
        vp::prepareAudioSession ({ requestedSampleRate(), requestedBufferFrames(),
                                   inputProcessing });
        setAudioChannels (ins, 2);
        applyAudioSetup (false);
        return;
    }

    auto* dev = deviceManager.getCurrentAudioDevice();
    const int nIn = dev != nullptr ? dev->getActiveInputChannels().countNumberOfSetBits() : 0;
    if (ins > 0 && nIn <= 0)
        applyAudioSetup (true);
}

void MainComponent::applyAudioSetup (bool claimInputChannels)
{
    vp::prepareAudioSession ({ requestedSampleRate(), requestedBufferFrames(),
                               inputProcessing });

    auto setup = deviceManager.getAudioDeviceSetup();
    if (const double sr = deviceSampleRate(); sr > 0.0)
        setup.sampleRate = sr;
    if (const int buf = deviceBufferFrames(); buf > 0)
        setup.bufferSize = buf;

    // The channel fields are left exactly as the device manager has them unless
    // the microphone has just been granted and the open device has none. That is
    // what opening once rests on: JUCE compares the setup it is handed against
    // the one it is on and returns without touching the device when they are
    // equal, so on a rig already at the right clock this call costs nothing.
    // Rewriting the channels unconditionally - even to the same two - flips
    // useDefaultInputChannels and reopens the device for nothing.
    if (claimInputChannels)
    {
        setup.inputChannels.clear();
        setup.inputChannels.setRange (0, 2, true);
        setup.useDefaultInputChannels = false;
    }

    const auto err = deviceManager.setAudioDeviceSetup (setup, true);
    juce::ignoreUnused (err);
    applyInputProcessing();
}

void MainComponent::rebuildAudioDevice (const char* why)
{
    juce::ignoreUnused (why);
    if (! audioOpened)
        return;

    ++deviceRebuilds;
    stalledTicks = 0;
    // Two seconds before another one is allowed. A rig that genuinely cannot
    // hold a device open would otherwise be rebuilt fifteen times a second,
    // which is louder and less useful than being silent.
    rebuildCooldownTicks = 30;

    // The session first, because after a media server restart it has none of
    // what was set on it - category, mode, rate, buffer, all back to defaults.
    vp::prepareAudioSession ({ requestedSampleRate(), requestedBufferFrames(),
                               inputProcessing });

    // Close, not reopen. setAudioDeviceSetup keeps the device object and its
    // audio unit; after a reset that unit is a handle to something that no
    // longer exists, and starting it again is what JUCE already tried.
    deviceManager.closeAudioDevice();

    const auto setup = deviceManager.getAudioDeviceSetup();
    if (setup.outputDeviceName.isNotEmpty() || setup.inputDeviceName.isNotEmpty())
        deviceManager.restartLastAudioDevice();

    if (deviceManager.getCurrentAudioDevice() == nullptr)
    {
        // Nothing to restart from, or it refused. Go all the way back to opening
        // one from nothing, which is what the app does at launch - a reset can
        // take the device names with it, and restartLastAudioDevice has nothing
        // to work from then.
        audioOpened = false;
        openAudioDevice (micGranted);
    }

    applyInputProcessing();

    seenAudioBlocks = audioBlocks.load (std::memory_order_relaxed);
}

void MainComponent::applyInputProcessing()
{
    // Measurement mode on iOS: no AGC, no noise suppression, no echo canceller
    // between the room and the tracker. prepareAudioSession has already put the
    // session there; this is what carries the same answer to the open device
    // after a route change.
    //
    // Only when it is not already right. This runs from prepareToPlay, so it
    // runs on every device start, and setting the mode is a write to the live
    // session that iOS answers with a route change - which restarts the device,
    // which calls prepareToPlay. On an external interface that churn is not
    // free, and asking for the mode it is already in buys nothing.
    if (vp::sessionInputProcessing() == inputProcessing)
        return;

    if (auto* dev = deviceManager.getCurrentAudioDevice())
        dev->setAudioPreprocessingEnabled (inputProcessing);
}

void MainComponent::applyClock (int hz)
{
    if (clockHz == hz)
        return;

    clockHz = hz;
    refreshClockButtons();
    savePrefs();
    if (audioOpened)
        applyAudioSetup (false);
    repaint();
}

void MainComponent::applyBufferChoice (int frames)
{
    if (bufferChoice == frames)
        return;

    bufferChoice = frames;
    refreshBufferButtons();
    savePrefs();
    if (audioOpened)
        applyAudioSetup (false);
    repaint();
}

void MainComponent::applyInputProcessingChoice (bool on)
{
    if (inputProcessing == on)
        return;

    inputProcessing = on;
    refreshProcButton();
    savePrefs();

    if (audioOpened)
    {
        vp::prepareAudioSession ({ requestedSampleRate(), requestedBufferFrames(),
                                   inputProcessing });
        applyInputProcessing();
    }
    repaint();
}

namespace
{
    void paintChoice (juce::TextButton& b, bool on)
    {
        b.setToggleState (on, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonColourId, ink());
        b.setColour (juce::TextButton::textColourOffId, on ? fuchsia() : text());
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
            "microfono che sente la stanza. In IPAD l'app toglie shaker e congas "
            "da quello che ascolta. MIC sul mixer regola quanto sente. "
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

    constexpr int kStatusLines = 8;
}

void MainComponent::refreshClockButtons()
{
    paintChoice (clockAuto, clockHz == 0);
    paintChoice (clock44, clockHz == 44100);
    paintChoice (clock48, clockHz == 48000);
    paintChoice (clock88, clockHz == 88200);
    paintChoice (clock96, clockHz == 96000);
}

void MainComponent::refreshBufferButtons()
{
    paintChoice (bufAuto, bufferChoice == 0);
    paintChoice (buf64, bufferChoice == 64);
    paintChoice (buf128, bufferChoice == 128);
    paintChoice (buf256, bufferChoice == 256);
    paintChoice (buf512, bufferChoice == 512);
}

void MainComponent::refreshProcButton()
{
    procButton.setButtonText (inputProcessing ? "ELAB.  ON" : "ELAB.  OFF");
    paintChoice (procButton, inputProcessing);
}

void MainComponent::refreshSourceButton()
{
    const bool speaker = engine.settings().followSource.load()
                             == static_cast<int> (vp::FollowSource::speaker);
    sourceButton.setButtonText (speaker ? "IPAD" : "MIXER");
    paintChoice (sourceButton, speaker);
}

void MainComponent::refreshMixLabels()
{
    mixLabel.setText ("SHAKER", juce::dontSendNotification);
    mixValue.setText ("CONGAS", juce::dontSendNotification);
    const float mix = static_cast<float> (mixSlider.getValue());
    // The favoured pole takes the accent colour so the knob reads as a
    // balance, not as a volume with a number on top.
    mixLabel.setColour (juce::Label::textColourId, mix < 0.45f ? fuchsia() : mute());
    mixValue.setColour (juce::Label::textColourId, mix > 0.55f ? fuchsia() : mute());
    if (mix >= 0.45f && mix <= 0.55f)
    {
        mixLabel.setColour (juce::Label::textColourId, fuchsia());
        mixValue.setColour (juce::Label::textColourId, fuchsia());
    }
}

void MainComponent::setSettingsOpen (bool open)
{
    settingsButton.setToggleState (open, juce::dontSendNotification);
    settingsOverlay.setVisible (open);
    if (open)
    {
        settingsOverlay.toFront (false);
        settingsOverlay.setBounds (getLocalBounds());
    }
    repaint();
}

void MainComponent::loadPrefs()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "VirtualPercussionist";
    options.filenameSuffix = "settings";
    options.folderName = "VirtualPercussionist";
    options.osxLibrarySubFolder = "Application Support";
    prefs = std::make_unique<juce::PropertiesFile> (options);

    auto clamp01 = [] (double v, double fallback) -> float
    {
        if (! std::isfinite (v))
            return static_cast<float> (fallback);
        return juce::jlimit (0.0f, 1.0f, static_cast<float> (v));
    };
    auto setFader = [] (juce::Slider& s, juce::Label& value, float v)
    {
        s.setValue (static_cast<double> (v), juce::dontSendNotification);
        value.setText (juce::String (juce::roundToInt (static_cast<double> (v) * 100.0)) + "%",
                       juce::dontSendNotification);
    };

    // Only the rates the page can offer are honoured. A stored number the page
    // has no button for would be a clock the listener could see the effect of
    // and not get back off.
    const int hz = prefs->getIntValue ("clockHz", 0);
    clockHz = (hz == 44100 || hz == 48000 || hz == 88200 || hz == 96000) ? hz : 0;

    const int buf = prefs->getIntValue ("bufferFrames", 0);
    bufferChoice = (buf == 64 || buf == 128 || buf == 256 || buf == 512) ? buf : 0;

    inputProcessing = prefs->getBoolValue ("inputProcessing", false);

    const int src = prefs->getIntValue ("followSource",
                                        engine.settings().followSource.load());
    if (src == static_cast<int> (vp::FollowSource::speaker)
        || src == static_cast<int> (vp::FollowSource::kitMic))
        engine.settings().followSource.store (src);

    // -1 is the state the app ships in: follow the system rather than remember
    // a theme the listener never chose.
    const int theme = prefs->getIntValue ("theme", -1);
    if (theme == 0 || theme == 1)
        applyTheme (theme == 1, true);

    const int sub = prefs->getIntValue ("subdivision",
                                        engine.settings().subdivision.load());
    if (sub == static_cast<int> (vp::Subdivision::autoDetect)
        || sub == static_cast<int> (vp::Subdivision::quarter)
        || sub == static_cast<int> (vp::Subdivision::eighth)
        || sub == static_cast<int> (vp::Subdivision::sixteenth))
        engine.settings().subdivision.store (sub);

    const int style = prefs->getIntValue ("grooveStyle",
                                          engine.settings().grooveStyle.load());
    if (style >= 0 && style < static_cast<int> (vp::GrooveStyle::count))
        engine.settings().grooveStyle.store (style);

    engine.settings().grooveAuto.store (
        prefs->getBoolValue ("grooveAuto", engine.settings().grooveAuto.load()));

    const int oct = prefs->getIntValue ("tempoOctave",
                                        engine.settings().tempoOctave.load());
    engine.settings().tempoOctave.store (juce::jlimit (-1, 1, oct));

    engine.settings().shakerEnabled.store (
        prefs->getBoolValue ("shakerEnabled", engine.settings().shakerEnabled.load()));
    engine.settings().congasEnabled.store (
        prefs->getBoolValue ("congasEnabled", engine.settings().congasEnabled.load()));

    const float mix = clamp01 (prefs->getDoubleValue ("instrumentMix", 0.50), 0.50);
    engine.settings().instrumentMix.store (mix);
    mixSlider.setValue (static_cast<double> (mix), juce::dontSendNotification);
    refreshMixLabels();

    const float inGain = juce::jlimit (0.0, 2.0, prefs->getDoubleValue ("inputGain", 1.0));
    engine.settings().inputGain.store (static_cast<float> (inGain));
    inputGainSlider.setValue (inGain, juce::dontSendNotification);
    inputGainValue.setText (juce::String (juce::roundToInt (inGain * 100.0)) + "%",
                            juce::dontSendNotification);

    const float reverb = clamp01 (prefs->getDoubleValue ("reverbAmount", 0.30), 0.30);
    engine.settings().reverbAmount.store (reverb);
    setFader (reverbSlider, reverbValue, reverb);

    const float swing = clamp01 (prefs->getDoubleValue ("swing", 0.00), 0.00);
    engine.settings().swing.store (swing);
    setFader (swingSlider, swingValue, swing);

    const float energy = clamp01 (prefs->getDoubleValue ("intensity", 0.50), 0.50);
    engine.settings().intensity.store (energy);
    setFader (intensitySlider, intensityValue, energy);

    const bool followTempo = prefs->getBoolValue ("tempoFollow", true);
    const float storedBpm = static_cast<float> (juce::jlimit (50.0, 200.0,
                                                              prefs->getDoubleValue ("userBpm", 120.0)));
    engine.settings().userBpm.store (storedBpm);
    if (followTempo)
        engine.setTempoFollow (true);
    else
        engine.setFixedBpm (storedBpm);

    const bool shakerOn = engine.settings().shakerEnabled.load();
    shakerButton.setToggleState (shakerOn, juce::dontSendNotification);

    const bool congasOn = engine.settings().congasEnabled.load();
    congasButton.setToggleState (congasOn, juce::dontSendNotification);
}

void MainComponent::savePrefs (bool flush)
{
    if (prefs == nullptr)
        return;

    prefs->setValue ("clockHz", clockHz);
    prefs->setValue ("bufferFrames", bufferChoice);
    prefs->setValue ("inputProcessing", inputProcessing);
    prefs->setValue ("followSource", engine.settings().followSource.load());
    prefs->setValue ("theme", themeFollowsSystem ? -1 : (darkMode ? 1 : 0));
    prefs->setValue ("subdivision", engine.settings().subdivision.load());
    prefs->setValue ("grooveStyle", engine.settings().grooveStyle.load());
    prefs->setValue ("grooveAuto", engine.settings().grooveAuto.load());
    prefs->setValue ("tempoOctave", engine.settings().tempoOctave.load());
    prefs->setValue ("shakerEnabled", engine.settings().shakerEnabled.load());
    prefs->setValue ("congasEnabled", engine.settings().congasEnabled.load());
    prefs->setValue ("instrumentMix",
                     static_cast<double> (engine.settings().instrumentMix.load()));
    prefs->setValue ("inputGain",
                     static_cast<double> (engine.settings().inputGain.load()));
    prefs->setValue ("reverbAmount",
                     static_cast<double> (engine.settings().reverbAmount.load()));
    prefs->setValue ("swing", static_cast<double> (engine.settings().swing.load()));
    prefs->setValue ("intensity", static_cast<double> (engine.settings().intensity.load()));
    prefs->setValue ("tempoFollow", engine.settings().tempoFollow.load());
    prefs->setValue ("userBpm", static_cast<double> (engine.settings().userBpm.load()));

    if (flush)
        prefs->saveIfNeeded();
}

void MainComponent::applySubdivision (vp::Subdivision s)
{
    engine.settings().subdivision.store (static_cast<int> (s));
    refreshSubdivisionButtons();
    savePrefs();
}

void MainComponent::refreshSubdivisionButtons()
{
    const int cur = engine.settings().subdivision.load();
    auto paint = [cur] (juce::TextButton& b, int v)
    {
        const bool on = cur == v;
        b.setToggleState (on, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonColourId, ink());
        b.setColour (juce::TextButton::textColourOffId, on ? fuchsia() : text());
    };
    paint (subAuto, static_cast<int> (vp::Subdivision::autoDetect));
    paint (sub4,    static_cast<int> (vp::Subdivision::quarter));
    paint (sub8,    static_cast<int> (vp::Subdivision::eighth));
    paint (sub16,   static_cast<int> (vp::Subdivision::sixteenth));
}

void MainComponent::applyStyle (vp::GrooveStyle s)
{
    // Picking a part by hand is also the way out of AUTO: leaving both on would
    // mean the buttons lie about what is playing.
    engine.settings().grooveAuto.store (false);
    engine.settings().grooveStyle.store (static_cast<int> (s));
    refreshStyleButtons();
    savePrefs();
}

void MainComponent::applyTempoOctave (int octaves)
{
    engine.settings().tempoOctaveAuto.store (false);
    engine.settings().tempoOctave.store (juce::jlimit (-1, 1, octaves));
    refreshOctaveButtons();
    savePrefs();
    repaint();
}

void MainComponent::applyTempoOctaveAuto()
{
    engine.settings().tempoOctaveAuto.store (true);
    refreshOctaveButtons();
    repaint();
}

void MainComponent::refreshOctaveButtons()
{
    // Only a level the listener picked lights the button. Under AUTO the level
    // may well be halved, and the tempo line says so - but a filled button
    // means "you asked for this", and the way back is to press it again.
    const bool mine = ! engine.settings().tempoOctaveAuto.load();
    const int oct = engine.settings().tempoOctave.load();
    auto paint = [] (juce::TextButton& b, bool on)
    {
        b.setToggleState (on, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonColourId, on ? fuchsia() : ink());
        b.setColour (juce::TextButton::textColourOffId, on ? juce::Colours::white : text());
    };
    paint (halveButton, mine && oct < 0);
    paint (doubleButton, mine && oct > 0);
}

void MainComponent::applyStyleAuto (bool on)
{
    engine.settings().grooveAuto.store (on);
    refreshStyleButtons();
    savePrefs();
}

void MainComponent::refreshBarButton()
{
    // Lit means the count is the listener's. A tap on the tempo declares the one
    // as well, so this reads the engine back rather than trusting what the
    // button last asked for - press TAP and the button lights on its own.
    const bool locked = engine.settings().barLocked.load();
    if (! locked)
        barTapsSinceLock = 0;
    barButton.setButtonText (locked ? juce::String (juce::CharPointer_UTF8 ("L'1 \u00e8 QUI"))
                                    : juce::String (juce::CharPointer_UTF8 ("SPOSTA L'1")));
    barButton.setToggleState (locked, juce::dontSendNotification);
    barButton.setColour (juce::TextButton::buttonColourId, locked ? fuchsia() : ink());
    barButton.setColour (juce::TextButton::textColourOffId,
                         locked ? juce::Colours::white : text());
}

void MainComponent::refreshStyleButtons()
{
    const bool autoOn = engine.settings().grooveAuto.load();
    const int cur = engine.settings().grooveStyle.load();

    auto paint = [] (juce::TextButton& b, bool on, bool detected)
    {
        b.setToggleState (on, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonColourId, ink());
        b.setColour (juce::TextButton::textColourOffId,
                     on || detected ? fuchsia() : text());
    };

    paint (styleAuto, autoOn, false);
    // Under AUTO no button is "selected", but the one the detector has landed on
    // is tinted, so what is actually playing is still visible.
    paint (styleMarcha, ! autoOn && cur == static_cast<int> (vp::GrooveStyle::marcha),
           autoOn && snap.grooveStyle == static_cast<int> (vp::GrooveStyle::marcha));
    paint (styleRock, ! autoOn && cur == static_cast<int> (vp::GrooveStyle::rock),
           autoOn && snap.grooveStyle == static_cast<int> (vp::GrooveStyle::rock));
    paint (styleDance, ! autoOn && cur == static_cast<int> (vp::GrooveStyle::dance),
           autoOn && snap.grooveStyle == static_cast<int> (vp::GrooveStyle::dance));
    paint (stylePop, ! autoOn && cur == static_cast<int> (vp::GrooveStyle::pop),
           autoOn && snap.grooveStyle == static_cast<int> (vp::GrooveStyle::pop));
    paint (styleSamba, ! autoOn && cur == static_cast<int> (vp::GrooveStyle::samba),
           autoOn && snap.grooveStyle == static_cast<int> (vp::GrooveStyle::samba));
    paint (styleFunk, ! autoOn && cur == static_cast<int> (vp::GrooveStyle::funk),
           autoOn && snap.grooveStyle == static_cast<int> (vp::GrooveStyle::funk));
    paint (styleReggae, ! autoOn && cur == static_cast<int> (vp::GrooveStyle::reggae),
           autoOn && snap.grooveStyle == static_cast<int> (vp::GrooveStyle::reggae));
    paint (styleBossa, ! autoOn && cur == static_cast<int> (vp::GrooveStyle::bossa),
           autoOn && snap.grooveStyle == static_cast<int> (vp::GrooveStyle::bossa));
}

void MainComponent::applyLatencyFromDevice()
{
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        const double sr = dev->getCurrentSampleRate();
        const int inL = dev->getInputLatencyInSamples();
        const int outL = dev->getOutputLatencyInSamples();
        const int buf = dev->getCurrentBufferSizeSamples();
        const float ms = sr > 0.0 ? static_cast<float> ((inL + outL + buf) * 1000.0 / sr) : 0.0f;
        engine.setReportedLatencyMs (ms);
        inputChannels = dev->getActiveInputChannels().countNumberOfSetBits();
    }
}

void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    double sr = sampleRate;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        const double devSr = dev->getCurrentSampleRate();
        if (devSr > 8000.0)
            sr = devSr;
    }
    const double hw = vp::sessionSampleRate();
    if (sr < 24000.0 && hw > 24000.0)
        sr = hw;

    inputScratch.setSize (8, juce::jmax (samplesPerBlockExpected * 4, 8192), false, false, true);
    engine.prepare (sr, juce::jmax (samplesPerBlockExpected * 2, 2048), 2);
    if (userWantsArmed)
        engine.start();
    applyLatencyFromDevice();
    applyInputProcessing();
    audioReady = true;
}

void MainComponent::releaseResources()
{
    audioReady = false;
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    auto* buffer = bufferToFill.buffer;
    if (buffer == nullptr)
        return;

    const int n = bufferToFill.numSamples;
    const int start = bufferToFill.startSample;
    const int nCh = buffer->getNumChannels();
    const int count = juce::jmin (nCh, inputScratch.getNumChannels());
    const int nCopy = juce::jmin (n, inputScratch.getNumSamples());

    for (int c = 0; c < count; ++c)
        inputScratch.copyFrom (c, 0, *buffer, c, start, nCopy);

    const float* inPtrs[8] {};
    float* outPtrs[8] {};
    const int used = juce::jmin (count, 8);

    for (int c = 0; c < used; ++c)
    {
        inPtrs[c] = inputScratch.getReadPointer (c);
        outPtrs[c] = buffer->getWritePointer (c, start);
    }

    engine.process (inPtrs, used, outPtrs, used, nCopy);
    audioBlocks.fetch_add (1, std::memory_order_relaxed);

    if (nCopy < n)
    {
        for (int c = 0; c < nCh; ++c)
            buffer->clear (c, start + nCopy, n - nCopy);
    }
}

void MainComponent::timerCallback()
{
    snap = engine.snapshot();
    applyLatencyFromDevice();

    // A tap can lock the bar without this button being touched, so the button
    // follows the engine rather than its own last press.
    if (barButton.getToggleState() != snap.barLocked)
        refreshBarButton();

    // Is the device still calling us? It can stop without saying so - iOS
    // restarting its media server leaves every audio object in the process
    // invalid, and JUCE answers that notification by starting the audio unit it
    // already has, which is a handle to something that no longer exists. The
    // sound goes and does not come back until a *new* unit is made, which is
    // what changing the clock by hand was doing.
    if (rebuildCooldownTicks > 0)
    {
        --rebuildCooldownTicks;
        seenAudioBlocks = audioBlocks.load (std::memory_order_relaxed);
    }
    else if (audioOpened)
    {
        // No device at all counts as stalled too. A rebuild that fails leaves
        // one, and without this the watchdog would never look again - the app
        // would sit silent forever having tried exactly once. Retried on the
        // same cooldown, it also means plugging the cable back in brings the
        // sound back without touching anything.
        const uint32_t now = audioBlocks.load (std::memory_order_relaxed);
        const bool haveDevice = deviceManager.getCurrentAudioDevice() != nullptr;
        const bool moving = haveDevice && audioReady && now != seenAudioBlocks;

        if (moving)
        {
            stalledTicks = 0;
            seenAudioBlocks = now;
        }
        else if (haveDevice && ! audioReady)
        {
            // Between close and prepareToPlay. Not a stall.
            stalledTicks = 0;
        }
        else
        {
            // Twelve ticks at 15 Hz: near enough a second of silence from a
            // device that is supposed to be running. Long enough that a busy
            // moment on the message thread cannot trigger it.
            if (++stalledTicks >= 12)
                rebuildAudioDevice (haveDevice ? "no audio callback for a second"
                                               : "no audio device");
        }
    }
    if (tapFlash > 0)
        --tapFlash;
    refreshStartButton();
    refreshTempoModeButtons();
    if (engine.settings().grooveAuto.load())
        refreshStyleButtons();
    repaint();

   #if JUCE_DEBUG
    static int vpLiveN = 0;
    if ((++vpLiveN % 15) == 0)
        juce::Logger::writeToLog ("VP live bpm=" + juce::String (snap.bpm, 1)
            + " nn=" + juce::String (snap.neuralBpm, 1)
            + " tgt=" + juce::String (snap.targetBpm, 1)
            + " state=" + juce::String (vp::toString (snap.state))
            + " bar=" + juce::String (juce::CharPointer_UTF8 (vp::toBarString (snap.followBar)))
            + " peak=" + juce::String (snap.inputPeak, 4)
            + " an=" + juce::String (snap.analysisPeak, 4)
            + " pBeat=" + juce::String (snap.pBeat, 2)
            + " valid=" + juce::String (snap.hypValid ? 1 : 0)
            + " onnx=" + juce::String (snap.aiOnnx ? 1 : 0)
            + " src=" + juce::String (snap.source == vp::FollowSource::speaker ? "IPAD" : "MIXER")
            + " sr=" + juce::String (snap.sampleRate, 0)
            + " hits=" + juce::String (engine.shakerHits())
            + " armed=" + juce::String (userWantsArmed ? 1 : 0));
   #endif
}

juce::Rectangle<int> MainComponent::safePadded (juce::Rectangle<int> area) const
{
   #if JUCE_IOS
    // The margin an iPad wanted, and then whatever the system says is actually
    // unusable, whichever is larger per side.
    //
    // On an iPad the second half changes nothing: its insets are a status bar
    // the margin already cleared. On a phone they are the whole difference
    // between a readable screen and one with the tempo under a Dynamic Island
    // in portrait and the transport under a rounded corner in landscape. Taking
    // the larger per side rather than adding them is what keeps the iPad
    // exactly as it was.
    juce::BorderSize<int> pad { 36, 24, 20, 24 };
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto safe = display->safeAreaInsets;
        pad = { juce::jmax (pad.getTop(), safe.getTop()),
                juce::jmax (pad.getLeft(), safe.getLeft()),
                juce::jmax (pad.getBottom(), safe.getBottom()),
                juce::jmax (pad.getRight(), safe.getRight()) };
    }
    return pad.subtractedFrom (area);
   #else
    return area.reduced (30, 24);
   #endif
}

juce::Rectangle<int> MainComponent::layoutColumn() const
{
    return safePadded (getLocalBounds());
}

bool MainComponent::isLandscape() const
{
    return getWidth() > getHeight();
}

juce::Rectangle<int> MainComponent::stageArea() const
{
    auto r = layoutColumn();
    r.removeFromTop (34 + 8);
    if (isLandscape())
        return r.removeFromLeft (juce::roundToInt (static_cast<float> (r.getWidth()) * 0.44f));

    const int consoleH = juce::jlimit (330, 560,
                                       juce::roundToInt (static_cast<float> (r.getHeight()) * 0.54f));
    return r.removeFromTop (juce::jmax (240, r.getHeight() - consoleH));
}

MainComponent::StageRows MainComponent::stageRows (juce::Rectangle<int> area) const
{
    // Sized first, placed second. The rows have natural heights; whatever is
    // left over goes above and below so the block sits in the upper middle of
    // whatever space the orientation gives it, rather than piling up at the top
    // and leaving a hole under it.
    //
    // And when there is *less* than the natural height, everything shrinks
    // together instead of the last rows running off the bottom. The minimums
    // below plus the fixed gaps come to about 340 points, which an iPad always
    // has and a phone in landscape does not: the stage there is nearer 290, so
    // without this the meter and the microphone line simply fall off the end.
    const int naturalBpm = juce::jlimit (72, 156, area.getHeight() / 4);
    const int naturalBeats = juce::jlimit (52, 96, area.getHeight() / 6);
    const bool follow = engine.settings().tempoFollow.load();
    // SEGUI/FISSO live on the status row now, so the old 36-point mode row is
    // only kept for the ± BPM nudge that appears under FISSO.
    const int natural = 18 + 6 + 36 + 6 + naturalBpm + 16
                        + (follow ? 0 : 28) + 18 + 10
                        + naturalBeats + 20 + 10 + 10 + 18;
    const float fit = natural > area.getHeight() && natural > 0
                          ? static_cast<float> (area.getHeight()) / static_cast<float> (natural)
                          : 1.0f;
    const auto px = [fit] (int v)
    {
        return juce::jmax (1, juce::roundToInt (static_cast<float> (v) * fit));
    };

    const int bpmH = px (naturalBpm);
    const int beatsH = px (naturalBeats);

    const int content = px (natural);
    const int slack = juce::jmax (0, area.getHeight() - content);
    area.removeFromTop (slack / 3);

    StageRows s;
    s.title = area.removeFromTop (px (18));
    area.removeFromTop (px (6));
    s.pill = area.removeFromTop (px (36));
    {
        const int btnW = juce::jlimit (56, 84, s.pill.getWidth() / 6);
        s.tempoMode = s.pill.removeFromRight (btnW * 2 + 4);
        s.pill.removeFromRight (8); // keep the status line off the two buttons
    }
    area.removeFromTop (px (6));
    s.bpm = area.removeFromTop (bpmH);
    {
        // A bounded block, centred. Wider than this and the two octave buttons
        // sit so far from the number that they read as unrelated; narrower and
        // the number has nowhere to go.
        auto block = s.bpm.withSizeKeepingCentre (juce::jmin (s.bpm.getWidth(), 430), bpmH);
        const int octW = juce::jlimit (48, 78, block.getWidth() / 6);
        s.octaveDown = block.removeFromLeft (octW).reduced (0, bpmH / 5);
        s.octaveUp = block.removeFromRight (octW).reduced (0, bpmH / 5);
        s.bpmNumber = block.reduced (8, 0);
    }
    s.bpmLabel = area.removeFromTop (px (16));
    if (! follow)
        s.tempoNudge = area.removeFromTop (px (28));
    s.tempoLine = area.removeFromTop (px (18));
    area.removeFromTop (px (10));
    s.beats = area.removeFromTop (beatsH);
    // Beside the dots, because that is what it moves.
    s.barShift = s.beats.removeFromRight (juce::jmin (96, s.beats.getWidth() / 4))
                        .reduced (2, beatsH / 4);
    s.part = area.removeFromTop (px (20));
    area.removeFromTop (px (10));
    s.meter = area.removeFromTop (px (10)).reduced (juce::jmax (0, area.getWidth() / 6), 2);
    s.mic = area.removeFromTop (px (18));
    return s;
}

juce::Rectangle<int> MainComponent::layoutConsole (juce::Rectangle<int> area)
{
    cards.clearQuick();

    // PARTE and STRUMENTI are one row of squares, sized to that row; leftover
    // height goes to FEEL. Two fat rows of wide buttons is what would not fit
    // a phone, and stretching the squares to fill leftover height would undo
    // the space we just recovered.
    const int gap = 10;
    const int titleH = 18;
    auto card = [&] (juce::Rectangle<int> bounds, const char* title)
    {
        cards.add ({ bounds, juce::String (title) });
        return bounds.reduced (12, 10).withTrimmedTop (titleH);
    };

    const int padY = 10;
    const int chrome = padY * 2 + titleH;
    const int btnGap = 5;
    const int innerW = juce::jmax (1, area.getWidth() - 24);
    auto squareFor = [innerW, btnGap] (int count)
    {
        return juce::jlimit (24, 40, (innerW - btnGap * (count - 1)) / juce::jmax (1, count));
    };

    auto placeSquareRow = [btnGap] (juce::Rectangle<int> body,
                                    std::initializer_list<juce::TextButton*> bs)
    {
        const int nBtn = static_cast<int> (bs.size());
        const int side = juce::jmin (body.getHeight(),
                                     (body.getWidth() - btnGap * (nBtn - 1)) / juce::jmax (1, nBtn));
        const int total = nBtn * side + btnGap * juce::jmax (0, nBtn - 1);
        auto row = body.withSizeKeepingCentre (total, side);
        int i = 0;
        for (auto* b : bs)
        {
            b->setBounds (row.removeFromLeft (side));
            if (++i < nBtn)
                row.removeFromLeft (btnGap);
        }
    };

    const int n = area.getHeight();
    const int hTransport = juce::roundToInt (static_cast<float> (n) * 0.20f);
    const int hPart      = chrome + squareFor (9);
    const int hInst      = chrome + squareFor (6);

    {
        auto body = card (area.removeFromTop (hTransport), "TRASPORTO");
        startButton.setBounds (body.removeFromLeft (body.getWidth() / 2).reduced (4));
        stopButton.setBounds (body.reduced (4));
        area.removeFromTop (gap);
    }

    {
        auto body = card (area.removeFromTop (hPart), "PARTE");
        placeSquareRow (body, { &styleAuto, &styleMarcha, &styleRock, &styleDance, &stylePop,
                                &styleSamba, &styleFunk, &styleReggae, &styleBossa });
        area.removeFromTop (gap);
    }

    {
        auto body = card (area.removeFromTop (hInst), "STRUMENTI");
        placeSquareRow (body, { &shakerButton, &congasButton, &subAuto, &sub4, &sub8, &sub16 });
        area.removeFromTop (gap);
    }

    {
        // Mix on the left: SHAKER / CONGAS still name the two poles, equal
        // when the pointer is at noon. Then how loud the tracker hears the
        // room or the aux.
        auto body = card (area, "FEEL");
        const int nKnobs = 5;
        const int knobColW = body.getWidth() / nKnobs;
        auto placeKnob = [&] (juce::Label& val, juce::Label& name, juce::Slider& s)
        {
            auto col = body.removeFromLeft (knobColW);
            val.setBounds (col.removeFromTop (18));
            name.setBounds (col.removeFromBottom (16));
            s.setBounds (col.reduced (2, 2));
        };
        placeKnob (mixValue, mixLabel, mixSlider);
        placeKnob (inputGainValue, inputGainLabel, inputGainSlider);
        placeKnob (swingValue, swingLabel, swingSlider);
        placeKnob (intensityValue, intensityLabel, intensitySlider);
        placeKnob (reverbValue, reverbLabel, reverbSlider);
    }

    swingSlider.setVisible (true);
    swingLabel.setVisible (true);
    swingValue.setVisible (true);
    intensitySlider.setVisible (true);
    intensityLabel.setVisible (true);
    intensityValue.setVisible (true);
    reverbSlider.setVisible (true);
    reverbLabel.setVisible (true);
    reverbValue.setVisible (true);
    mixSlider.setVisible (true);
    mixLabel.setVisible (true);
    mixValue.setVisible (true);
    inputGainSlider.setVisible (true);
    inputGainLabel.setVisible (true);
    inputGainValue.setVisible (true);
    return area;
}

void MainComponent::resized()
{
    auto r = layoutColumn();

    // The utility row goes in a corner of the *stage*, not across the top of
    // everything: pushed to the right in landscape it lands on the console and
    // reads as part of the transport card, which is the one place a stray tap
    // does damage.
    const auto stage = stageArea();
    auto util = r.removeFromTop (34);
    if (isLandscape())
        util = util.withWidth (stage.getWidth());
    // One button now. The four that used to live here - source, theme, click
    // test, debug - are all things a player sets before the set and never
    // during it, and every one of them was a stray tap away from the transport.
    settingsButton.setBounds (util.removeFromRight (96).reduced (2));
    r.removeFromTop (8);

    settingsOverlay.setBounds (getLocalBounds());

    if (isLandscape())
        r.removeFromLeft (stage.getWidth() + 16);
    else
        r.removeFromTop (stage.getHeight() + 14);

    // The halve/double pair flanks the number it applies to, close enough to
    // read as belonging to it and never overlapping it.
    const auto rows = stageRows (stage);
    halveButton.setBounds (rows.octaveDown);
    doubleButton.setBounds (rows.octaveUp);
    barButton.setBounds (rows.barShift);

    tapStrip = juce::Rectangle<int>::leftTopRightBottom (stage.getX(), rows.bpm.getY(),
                                                         stage.getRight(), rows.meter.getBottom());
    tapZone.setBounds (tapStrip);

    {
        const bool follow = engine.settings().tempoFollow.load();
        auto mode = rows.tempoMode;
        const int btnW = juce::jmax (1, mode.getWidth() / 2);
        followButton.setBounds (mode.removeFromLeft (btnW).reduced (2, 2));
        fixedButton.setBounds (mode.reduced (2, 2));
        if (! follow && ! rows.tempoNudge.isEmpty())
        {
            auto nudge = rows.tempoNudge;
            const int nudgeW = juce::jmax (36, nudge.getHeight());
            const int editW = 78;
            const int total = nudgeW * 2 + editW + 8;
            auto block = nudge.withSizeKeepingCentre (juce::jmin (nudge.getWidth(), total),
                                                      nudge.getHeight());
            bpmNudgeDown.setBounds (block.removeFromLeft (nudgeW).reduced (2));
            bpmEdit.setBounds (block.removeFromLeft (editW).reduced (2, 4));
            bpmNudgeUp.setBounds (block.removeFromLeft (nudgeW).reduced (2));
        }
    }

    layoutConsole (r);
}


void MainComponent::paintCards (juce::Graphics& g)
{
    paintCardList (g, cards);
}

void MainComponent::paintCardList (juce::Graphics& g, const juce::Array<Card>& list)
{
    for (const auto& c : list)
    {
        g.setColour (panel());
        g.fillRoundedRectangle (c.bounds.toFloat(), 14.0f);
        g.setColour (text().withAlpha (0.07f));
        g.drawRoundedRectangle (c.bounds.toFloat().reduced (0.5f), 14.0f, 1.0f);
        g.setColour (mute());
        g.setFont (fontUi (10.5f));
        g.drawFittedText (c.title, c.bounds.reduced (14, 9).removeFromTop (14),
                          juce::Justification::topLeft, 1);
    }
}

void MainComponent::paintStage (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto rows = stageRows (area);
    const float energy = juce::jlimit (0.0f, 1.0f,
                                       std::sqrt (juce::jmax (0.0f, snap.inputPeak)) * 3.2f);

    // Title, with the brand mark and the rule under it.
    {
        auto titleR = rows.title;
        auto brand = titleR.removeFromLeft (20);
        g.setColour (fuchsia());
        g.fillRoundedRectangle (brand.withSizeKeepingCentre (8, 8).toFloat(), 1.8f);
        g.setColour (text());
        g.setFont (fontUi (13.0f));
        g.drawFittedText ("VIRTUAL PERCUSSIONIST", titleR, juce::Justification::centredLeft, 1);
        g.setColour (fuchsia().withAlpha (0.55f));
        g.fillRect ((float) rows.title.getX(), (float) rows.title.getBottom() + 2.0f,
                    (float) rows.title.getWidth(), 1.2f);
    }

    // What the tracker is doing: a coloured dot and the words, nothing else.
    // The old filled pill ate a row a phone does not have, and shouted the
    // same fact the colour already carries.
    {
        const auto stCol = stateColour (snap.followBar);
        const bool hot = stateIsHot (snap.followBar);
        const juce::String label (juce::CharPointer_UTF8 (vp::toBarString (snap.followBar)));
        const auto f = fontUi (12.0f, false);
        const float textW = juce::GlyphArrangement::getStringWidth (f, label);
        const float dotR = 4.5f;
        const float gapDot = 7.0f;
        const float totalW = juce::jmin (static_cast<float> (rows.pill.getWidth()),
                                         dotR * 2.0f + gapDot + textW);
        // Left of the leftover pill so SEGUI/FISSO own the right edge.
        const float x0 = static_cast<float> (rows.pill.getX());
        const float cy = static_cast<float> (rows.pill.getCentreY());
        const juce::Point<float> dot { x0 + dotR, cy };
        if (hot)
            paintRadial (g, dot, 13.0f, stCol, 0.55f);
        g.setColour (stCol);
        g.fillEllipse (dot.x - dotR, dot.y - dotR, dotR * 2.0f, dotR * 2.0f);
        auto textR = juce::Rectangle<float> (x0 + dotR * 2.0f + gapDot,
                                            static_cast<float> (rows.pill.getY()),
                                            juce::jmax (8.0f, totalW - dotR * 2.0f - gapDot),
                                            static_cast<float> (rows.pill.getHeight()))
                         .toNearestInt();
        g.setFont (f);
        g.setColour (hot ? text() : mute());
        g.drawFittedText (label, textR, juce::Justification::centredLeft, 1);
    }

    // The tempo, sized to the room it has rather than to a constant, so it is
    // the biggest thing on the screen in portrait and still the biggest thing
    // when the iPad is turned.
    paintRadial (g, rows.bpmNumber.getCentre().toFloat(),
                 static_cast<float> (rows.bpm.getHeight()) * 1.6f, fuchsia(),
                 0.10f + 0.18f * energy);

    const bool haveBpm = snap.bpm > 40.0f;
    if (haveBpm)
    {
        // Sized against the width by measurement, not by drawFittedText: with
        // one line to work with that squashes to 70% and then puts an ellipsis
        // in, so a five-character tempo in a narrow landscape column came out
        // as "11...". Measure, scale, draw.
        const juce::String bpmText (snap.bpm, 1);
        const float wanted = static_cast<float> (rows.bpm.getHeight()) * 0.88f;
        juce::Font f = fontDisplay (wanted);
        const float textW = juce::GlyphArrangement::getStringWidth (f, bpmText);
        const float roomW = static_cast<float> (rows.bpmNumber.getWidth());
        if (textW > roomW && textW > 1.0f)
            f = f.withHeight (wanted * roomW / textW);

        g.setFont (f);
        // The offset copy behind the digits is a glow on a dark ground and a
        // smear on a white one, so light gets a fainter one.
        g.setColour (fuchsia().withAlpha (gDarkMode ? 0.40f : 0.16f));
        g.drawText (bpmText, rows.bpmNumber.translated (0, gDarkMode ? 3 : 2),
                    juce::Justification::centred, false);
        g.setColour (text());
        g.drawText (bpmText, rows.bpmNumber, juce::Justification::centred, false);
    }
    else
    {
        // Two drawn bars rather than "--" in the tempo's own font. A hyphen is
        // a hairline a tenth of its em tall, so set at the size the number
        // wants it reads as something broken rather than as a blank waiting to
        // be filled.
        const float barW = static_cast<float> (rows.bpm.getHeight()) * 0.30f;
        const float barH = juce::jmax (6.0f, static_cast<float> (rows.bpm.getHeight()) * 0.09f);
        const float gap = barW * 0.35f;
        const float cx = static_cast<float> (rows.bpmNumber.getCentreX());
        const float cy = static_cast<float> (rows.bpmNumber.getCentreY());
        g.setColour (text().withAlpha (0.16f));
        g.fillRoundedRectangle (cx - barW - gap * 0.5f, cy - barH * 0.5f, barW, barH, barH * 0.5f);
        g.fillRoundedRectangle (cx + gap * 0.5f, cy - barH * 0.5f, barW, barH, barH * 0.5f);
    }

    g.setColour (fuchsia());
    g.setFont (fontUi (11.5f));
    g.drawFittedText ("BPM", rows.bpmLabel, juce::Justification::centred, 1);

    // How the tempo is being held. SEGUI shows what the analysis thinks;
    // FISSO is the listener's lock, so the analysis label would only confuse.
    {
        const bool userFixed = ! snap.tempoFollow;
        const bool held = userFixed || snap.tempoRegime == 1;
        g.setColour (held ? fuchsia() : mute());
        g.setFont (fontUi (12.0f));
        juce::String tempoLine = userFixed
                                     ? juce::String ("TEMPO FISSO")
                                     : juce::String (vp::regimeLabel (snap.tempoRegime));
        if (! userFixed && ! snap.levelSettled)
            tempoLine += juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  livello provvisorio"));
        if (snap.tempoOctave != 0)
        {
            tempoLine += juce::String (juce::CharPointer_UTF8 (snap.tempoOctave < 0
                                                                  ? "  \xc2\xb7  a met\xc3\xa0"
                                                                  : "  \xc2\xb7  doppio"));
            if (snap.tempoOctaveAuto)
                tempoLine += " (auto)";
        }
        g.drawFittedText (tempoLine, rows.tempoLine, juce::Justification::centred, 1);
    }

    // Four beats, the one marked. Big enough to read at arm's length on a
    // stand, and lit from the clock rather than from whether a sample happens
    // to be sounding: a player watching the bar wants to see it turn over
    // before START, not after.
    beatStrip = rows.beats;
    {
        // A compact cluster, centred in the row: the four quarters are a
        // count, not a full-width ruler.
        const int bandW = juce::jmin (rows.beats.getWidth(),
                                      juce::jmax (200, rows.beats.getHeight() * 6));
        const auto band = rows.beats.withSizeKeepingCentre (bandW, rows.beats.getHeight());
        // The lit beat wears a halo of 1.8 radii, so the radius has to leave
        // room for it inside the row - otherwise the glow spills onto the line
        // of text below, which is what it did.
        const float rad = juce::jmin (24.0f, static_cast<float> (rows.beats.getHeight()) * 0.27f);
        const float y = static_cast<float> (band.getCentreY());
        const float step = static_cast<float> (band.getWidth()) / 4.0f;
        const int beatIdx = juce::jlimit (0, 3, static_cast<int> (snap.barPhase * 4.0f));
        const bool running = snap.bpm > 40.0f || snap.barDeclared;

        g.setColour (text().withAlpha (0.10f));
        g.fillRoundedRectangle (static_cast<float> (band.getX()) + step * 0.5f, y - 1.0f,
                                step * 3.0f, 2.0f, 1.0f);

        for (int i = 0; i < 4; ++i)
        {
            const float x = static_cast<float> (band.getX()) + step * (static_cast<float> (i) + 0.5f);
            const bool on = running && beatIdx == i;
            const bool one = i == 0;
            const float rr = one ? rad : rad * 0.82f;
            if (on)
            {
                paintRadial (g, { x, y }, rr * 2.6f, fuchsia(), 0.45f);
                g.setColour (fuchsia());
                g.fillEllipse (x - rr, y - rr, rr * 2.0f, rr * 2.0f);
                g.setColour (juce::Colours::white);
                g.fillEllipse (x - rr * 0.38f, y - rr * 0.38f, rr * 0.76f, rr * 0.76f);
            }
            else
            {
                g.setColour (text().withAlpha (0.10f));
                g.fillEllipse (x - rr, y - rr, rr * 2.0f, rr * 2.0f);
                g.setColour (text().withAlpha (one ? 0.35f : 0.18f));
                g.drawEllipse (x - rr, y - rr, rr * 2.0f, rr * 2.0f, 1.4f);
            }

            // A tap on the one redraws the bar under the player's hands, and a
            // bar is a slow thing to see move: without a mark here the gesture
            // looks like it did nothing until the next downbeat comes round.
            if (one && snap.barDeclared)
            {
                const float hr = rr + 6.0f;
                g.setColour (fuchsia());
                g.drawEllipse (x - hr, y - hr, hr * 2.0f, hr * 2.0f, 2.2f);
            }
        }
    }

    // Which part is playing, and under AUTO how sure the detector is.
    g.setColour (mute());
    g.setFont (fontUi (12.0f));
    g.drawFittedText (juce::String ("PARTE  ")
                          + vp::toString (static_cast<vp::GrooveStyle> (snap.grooveStyle))
                          + (engine.settings().grooveAuto.load()
                                 ? "   (auto " + juce::String (snap.grooveStyleConfidence, 2) + ")"
                                 : juce::String()),
                      rows.part, juce::Justification::centred, 1);

    // Input. A bar and a phrase: enough to tell "the microphone is hearing the
    // room" from "the microphone is hearing nothing", which is the only
    // question the old row of numbers was ever answering.
    g.setColour (sliderTrack());
    g.fillRoundedRectangle (rows.meter.toFloat(), 5.0f);
    auto fillM = rows.meter.toFloat().withWidth (
                     static_cast<float> (rows.meter.getWidth()) * energy);
    if (fillM.getWidth() > 2.0f)
    {
        // Fuchsia into the ink of the theme: white in dark, but in light
        // `text()` is a near-black, and a meter that fades to black reads as a
        // fault rather than as a level.
        juce::ColourGradient mg (fuchsia().brighter (0.2f), fillM.getX(), fillM.getY(),
                                 gDarkMode ? text() : fuchsia().darker (0.45f),
                                 fillM.getRight(), fillM.getY(), false);
        g.setGradientFill (mg);
        g.fillRoundedRectangle (fillM, 5.0f);
    }

    if (tapFlash > 0 && ! tapStrip.isEmpty())
    {
        g.setColour (fuchsia().withAlpha (gDarkMode ? 0.32f : 0.20f));
        g.fillRoundedRectangle (tapStrip.toFloat(), 14.0f);
        g.setColour (fuchsia().withAlpha (0.70f));
        g.drawRoundedRectangle (tapStrip.toFloat().reduced (0.5f), 14.0f, 2.4f);
    }

    g.setColour (mute());
    g.setFont (fontUi (11.5f, false));
    const juce::String micText = ! micGranted ? "MICROFONO NEGATO"
                               : (inputChannels <= 0 ? "MICROFONO SPENTO"
                               : (snap.inputPeak > 0.0012f ? "SENTO LA STANZA"
                                                           : "IN ASCOLTO"));
    const juce::String dot (juce::CharPointer_UTF8 ("   \xc2\xb7   "));
    g.drawFittedText (micText + dot + (snap.source == vp::FollowSource::speaker ? "IPAD" : "MIXER")
                          + (snap.aiOnnx ? juce::String() : dot + "AI STUB"),
                      rows.mic, juce::Justification::centred, 1);
}

void MainComponent::layoutSettings (juce::Rectangle<int> area)
{
    settingsCards.clearQuick();

    // The same margin the stage uses, for the same reason: this page is
    // full-screen too, and its close button is the first thing a Dynamic Island
    // would sit on.
    auto r = safePadded (area);

    auto head = r.removeFromTop (34);
    settingsClose.setBounds (head.removeFromRight (juce::jmin (120, head.getWidth() / 3))
                                 .reduced (2));
    settingsRows.title = head.withTrimmedRight (8);
    r.removeFromTop (14);

    const int gap = 10;
    const int titleH = 18;
    const int padY = 10;
    const int noteGap = 8;
    const int rowH = 58;

    auto card = [&] (juce::Rectangle<int> bounds, const char* title)
    {
        settingsCards.add ({ bounds, juce::String (title) });
        return bounds.reduced (12, padY).withTrimmedTop (titleH);
    };

    // Five across is the widest this page gets, and the buttons have to stay
    // tappable at that width on the narrow side of a portrait iPad. The
    // remainder goes to the last one rather than to a gap on the right.
    auto buttonRow = [] (juce::Rectangle<int> row, std::initializer_list<juce::TextButton*> bs)
    {
        const int n = static_cast<int> (bs.size());
        int i = 0;
        for (auto* b : bs)
        {
            const int w = row.getWidth() / (n - i);
            b->setBounds ((++i == n ? row : row.removeFromLeft (w)).reduced (3));
        }
    };

    // One column at full width in both orientations. Two columns is what the
    // console does, because the console has enough in it to fill them; this page
    // has five short cards, and split in two neither side had enough to reach
    // the bottom. Turned, the same five get shorter instead of narrower: the
    // captions stop wrapping.
    const int bodyW = juce::jmax (80, r.getWidth() - 24);
    const int chrome = padY * 2 + titleH;

    // Sized against their contents, placed second - the same order the stage
    // rows are computed in. A share of the column each gave a two-line caption
    // the same room as a seven-line read-out.
    enum { kClock = 0, kBuffer, kInput, kTests, kStatus, kCards };
    int h[kCards] = {
        chrome + rowH + noteGap + noteHeight (clockNote(), bodyW),
        chrome + rowH + noteGap + noteHeight (bufferNote(), bodyW),
        chrome + rowH + noteGap + noteHeight (inputNote(), bodyW),
        chrome + rowH,
        chrome + juce::roundToInt (fontUi (12.0f, false).getHeight() * 1.32f) * kStatusLines
    };

    int want = (kCards - 1) * gap;
    for (const int n : h)
        want += n;

    int cardGap = gap;
    if (want > r.getHeight() && want > 0)
    {
        for (int& n : h)
            n = n * r.getHeight() / want;
    }
    else
    {
        // What is left over goes three ways, in this order: a third of their own
        // height to the cards, up to 24 px to each gap, and whatever is still
        // spare above and below - so a page shorter than the screen sits in the
        // middle of it rather than stretched down it. Grown any further, a card
        // is a title with a hole under it.
        int slack = r.getHeight() - want;
        const int grow = juce::jmin (slack, want / 3);
        for (int& n : h)
            n += grow * n / juce::jmax (1, want);
        slack -= grow;

        const int extraGap = juce::jmin (24, slack / (kCards - 1));
        cardGap += extraGap;
        slack -= extraGap * (kCards - 1);

        r.removeFromTop (juce::jmin (slack / 2, 40));
    }

    auto take = [&r, cardGap] (int height)
    {
        auto out = r.removeFromTop (height);
        r.removeFromTop (cardGap);
        return out;
    };

    {
        auto body = card (take (h[kClock]), "CLOCK");
        buttonRow (body.removeFromTop (juce::jmin (rowH, body.getHeight())),
                   { &clockAuto, &clock44, &clock48, &clock88, &clock96 });
        settingsRows.clockNote = body.withTrimmedTop (noteGap);
    }

    {
        auto body = card (take (h[kBuffer]), "BUFFER");
        buttonRow (body.removeFromTop (juce::jmin (rowH, body.getHeight())),
                   { &bufAuto, &buf64, &buf128, &buf256, &buf512 });
        settingsRows.bufferNote = body.withTrimmedTop (noteGap);
    }

    {
        auto body = card (take (h[kInput]), "INGRESSO");
        buttonRow (body.removeFromTop (juce::jmin (rowH, body.getHeight())),
                   { &sourceButton, &procButton });
        settingsRows.inputNote = body.withTrimmedTop (noteGap);
    }

    buttonRow (card (take (h[kTests]), "PROVE"),
               { &themeButton, &clickButton, &debugButton });

    // The numbers are the point of the page: a clock the listener chose and a
    // clock the hardware gave are not the same thing, and only one of them is
    // what the engine is running at.
    settingsRows.status = card (take (h[kStatus]), "STATO");
}

void MainComponent::paintSettings (juce::Graphics& g)
{
    const auto full = settingsOverlay.getLocalBounds().toFloat();
    g.fillAll (bg());

    juce::ColourGradient floor (gDarkMode ? juce::Colour (0xff0a0a0c) : juce::Colour (0xffffffff),
                                full.getCentreX(), full.getY(),
                                bg(), full.getCentreX(), full.getBottom(), false);
    g.setGradientFill (floor);
    g.fillRect (full);
    paintRadial (g, { full.getCentreX(), full.getY() + 28.0f },
                 full.getWidth() * 0.55f, fuchsia(), 0.10f);

    paintCardList (g, settingsCards);

    {
        auto titleR = settingsRows.title;
        auto brand = titleR.removeFromLeft (20);
        g.setColour (fuchsia());
        g.fillRoundedRectangle (brand.withSizeKeepingCentre (8, 8).toFloat(), 1.8f);
        g.setColour (text());
        g.setFont (fontUi (13.0f));
        g.drawFittedText ("IMPOSTAZIONI", titleR, juce::Justification::centredLeft, 1);
        g.setColour (fuchsia().withAlpha (0.55f));
        g.fillRect ((float) settingsRows.title.getX(),
                    (float) settingsRows.title.getBottom() + 2.0f,
                    (float) settingsRows.title.getWidth(), 1.2f);
    }

    g.setColour (mute());
    g.setFont (noteFont());
    g.drawFittedText (clockNote(), settingsRows.clockNote, juce::Justification::centredLeft, 8);
    g.drawFittedText (bufferNote(), settingsRows.bufferNote, juce::Justification::centredLeft, 8);
    g.drawFittedText (inputNote(), settingsRows.inputNote, juce::Justification::centredLeft, 8);

    // Status: what came back, not what was asked for.
    {
        auto* dev = deviceManager.getCurrentAudioDevice();
        const double devSr = dev != nullptr ? dev->getCurrentSampleRate() : 0.0;
        const int devBuf = dev != nullptr ? dev->getCurrentBufferSizeSamples() : 0;
        const int outs = dev != nullptr
                             ? dev->getActiveOutputChannels().countNumberOfSetBits() : 0;
        const double blockMs = devSr > 0.0 && devBuf > 0
                                   ? devBuf * 1000.0 / devSr : 0.0;
        const juce::String route (vp::sessionRouteName());

        juce::StringArray lines;
        lines.add (juce::String ("clock      ")
                   + (devSr > 0.0 ? juce::String (devSr, 0) + " Hz" : juce::String ("--"))
                   + (clockHz > 0 ? juce::String ("   (chiesto ") + juce::String (clockHz) + ")"
                                  : juce::String ("   (auto)")));
        lines.add (juce::String ("buffer     ")
                   + (devBuf > 0 ? juce::String (devBuf) + "  ->  "
                                       + juce::String (blockMs, 1) + " ms"
                                 : juce::String ("--")));
        lines.add (juce::String ("latenza    ") + juce::String (snap.latencyMs, 1) + " ms");
        lines.add (juce::String ("canali     in ") + juce::String (inputChannels)
                   + "   out " + juce::String (outs));
        if (route.isNotEmpty())
            lines.add ("uscita     " + route);
        lines.add (juce::String ("altre app  ")
                   + (vp::otherAudioPlaying() ? "in riproduzione" : "ferme"));
        lines.add (juce::String ("motore     ")
                   + (snap.aiOnnx ? "ONNX BeatNet" : "AI STUB"));
        // A rig that needs this is a rig with a problem the app is papering
        // over, so the number is on the page rather than in a log nobody reads.
        lines.add (juce::String ("riavvii    ") + juce::String (deviceRebuilds)
                   + (deviceRebuilds > 0 ? "   (audio ricostruito)" : ""));

        g.setColour (mute());
        g.setFont (fontUi (12.0f, false));
        g.drawFittedText (lines.joinIntoString ("\n"), settingsRows.status,
                          juce::Justification::centredLeft, kStatusLines);
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    g.fillAll (bg());

    const float energy = juce::jlimit (0.0f, 1.0f,
                                       std::sqrt (juce::jmax (0.0f, snap.inputPeak)) * 3.2f);
    const float follow = stateIsHot (snap.followBar) ? 1.0f : 0.42f;
    const float wash = 0.12f + 0.20f * energy * follow;

    juce::ColourGradient floor (gDarkMode ? juce::Colour (0xff0a0a0c) : juce::Colour (0xffffffff),
                                full.getCentreX(), full.getY(),
                                bg(), full.getCentreX(), full.getBottom(), false);
    g.setGradientFill (floor);
    g.fillRect (full);

    const auto stage = stageArea();
    paintRadial (g, { stage.toFloat().getCentreX(), full.getY() + 28.0f },
                 full.getWidth() * 0.60f, fuchsia(), wash);
    paintRadial (g, { full.getCentreX(), full.getBottom() - 80.0f },
                 full.getWidth() * 0.45f, fuchsia(),
                 0.08f + 0.14f * (tapFlash > 0 ? 1.0f : energy));

    paintCards (g);
    paintStage (g, stage);

    if (debugOpen)
    {
        auto dbg = getLocalBounds().reduced (24).removeFromTop (300);
        g.setColour (panel().withAlpha (0.97f));
        g.fillRoundedRectangle (dbg.toFloat(), 12.0f);
        g.setColour (text().withAlpha (0.12f));
        g.drawRoundedRectangle (dbg.toFloat().reduced (0.5f), 12.0f, 1.0f);
        g.setColour (text());
        g.setFont (fontUi (12.0f, false));
        juce::StringArray lines;
        lines.add ("DEBUG");
        lines.add (juce::String (snap.aiOnnx ? "AI ONNX BeatNet" : "AI STUB (modello NON caricato)"));
        lines.add (juce::String (snap.source == vp::FollowSource::speaker ? "source IPAD/SPEAKER" : "source MIXER"));
        lines.add ("BPM " + juce::String (snap.bpm, 2) + "  nn " + juce::String (snap.neuralBpm, 2)
                   + "  target " + juce::String (snap.targetBpm, 2));
        lines.add ("tempo " + juce::String (vp::regimeLabel (snap.tempoRegime))
                   + "  livello " + juce::String (snap.levelSettled ? "deciso" : "provvisorio")
                   + "  fold " + (snap.combBpm > 1.0f ? juce::String (snap.combBpm, 1)
                                                      : juce::String ("--"))
                   + "  ottava " + juce::String (snap.tempoOctave));
        lines.add ("pBeat " + juce::String (snap.pBeat, 3) + "  valid " + juce::String (snap.hypValid ? 1 : 0)
                   + "  conf " + juce::String (snap.confidence, 3));
        lines.add ("beat " + juce::String (snap.beatPhase, 3) + "  bar " + juce::String (snap.barPhase, 3));
        lines.add ("state " + juce::String (vp::toString (snap.state)));
        lines.add ("callback " + juce::String (snap.callbackMs, 2) + " ms  lead "
                   + juce::String (snap.leadMs, 1) + " ms");
        lines.add ("sr " + juce::String (snap.sampleRate, 0)
                   + "  mic " + juce::String (snap.inputPeak, 4)
                   + "  analysis " + juce::String (snap.analysisPeak, 4));
        lines.add ("inCh " + juce::String (inputChannels) + "  micGranted " + juce::String (micGranted ? 1 : 0));
        lines.add ("hits " + juce::String (engine.shakerHits())
                   + "  voices " + juce::String (snap.shakerVoices));
        lines.add (juce::String (snap.tapLocked ? "tap LOCK" : "tap auto")
                   + (snap.tempoFollow ? "  SEGUI" : "  FISSO"));
        lines.add ("bar " + juce::String (juce::CharPointer_UTF8 (vp::toBarString (snap.followBar))));
        lines.add ("part " + juce::String (vp::toString (static_cast<vp::GrooveStyle> (snap.grooveStyle)))
                   + (engine.settings().grooveAuto.load() ? "  AUTO" : "  manual")
                   + "  conf " + juce::String (snap.grooveStyleConfidence, 2));
        lines.add ("style kick " + juce::String (snap.styleEvenKick, 2)
                   + "  back " + juce::String (snap.styleBackbeat, 2)
                   + "  offHi " + juce::String (snap.styleOffHigh, 2)
                   + "  sync " + juce::String (snap.styleSync, 2)
                   + "  occ " + juce::String (snap.styleOccupancy, 2));
        g.drawFittedText (lines.joinIntoString ("\n"), dbg.reduced (16), juce::Justification::topLeft, 16);
    }
}
