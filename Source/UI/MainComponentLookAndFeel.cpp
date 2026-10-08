// MainComponent: AppLookAndFeel and the fader zoom.
#include "UI/MainComponentShared.h"

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
    const bool chipFill = (bool) button.getProperties().getWithDefault ("chipFill", false);
    if ((bool) button.getProperties().getWithDefault ("circle", false))
        return fontUi (juce::jmax (11.0f, dim * 0.42f));
    // Kit-sound chips: a step up from the 9 px Misure floor, not a title.
    if (chipFill)
        return fontUi (juce::jmax (12.0f, dim * 0.38f));
    return fontUi (juce::jmax (9.0f, dim * 0.28f));
}

void MainComponent::AppLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                                    bool, bool)
{
    const bool chipFill = (bool) button.getProperties().getWithDefault ("chipFill", false);
    const bool circle = (bool) button.getProperties().getWithDefault ("circle", false);
    const bool compact = button.getWidth() <= button.getHeight() + 8;
    // chipFill cells hug their label; keep the 6 px Misure side inset even
    // when the chip is narrower than a square.
    const bool tight = compact && ! chipFill;
    auto area = button.getLocalBounds().reduced (circle ? 1 : (tight ? 2 : 6),
                                                 circle ? 1 : (tight ? 2 : 4));
    if (area.isEmpty())
        return;

    const juce::String label = button.getButtonText();
    juce::Font f = getTextButtonFont (button, button.getHeight());
    const float w = juce::GlyphArrangement::getStringWidth (f, label);
    const float room = static_cast<float> (area.getWidth());
    if (w > room && w > 1.0f)
        f = f.withHeight (juce::jmax (7.0f, f.getHeight() * room / w));

    if ((bool) button.getProperties().getWithDefault ("pencilIcon", false))
    {
        // EDIT: a pencil drawn as a path, tip bottom-left - tip, body and
        // eraser as three pieces so the gaps read at 16 pt. Off/on colours
        // come from the button (see setSoundEditMode).
        juce::Path pencil;
        pencil.startNewSubPath (0.0f, 0.0f);
        pencil.lineTo (0.22f, -0.13f);
        pencil.lineTo (0.22f, 0.13f);
        pencil.closeSubPath();
        pencil.addRectangle (0.27f, -0.13f, 0.52f, 0.26f);
        pencil.addRoundedRectangle (0.84f, -0.13f, 0.16f, 0.26f, 0.04f);
        pencil.applyTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::pi * 0.25f));
        auto box = button.getLocalBounds().toFloat().reduced (13.0f);
        g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                                : juce::TextButton::textColourOffId));
        g.fillPath (pencil, pencil.getTransformToScaleToFit (box, true));
        return;
    }

    if ((bool) button.getProperties().getWithDefault ("gearIcon", false))
    {
        static juce::Image gear;
        static bool gearWhite = true;
        if (! gear.isValid() || gearWhite != gDarkMode)
        {
            gearWhite = gDarkMode;
            constexpr int px = 64;
            gear = juce::Image (juce::Image::ARGB, px, px, true);
            juce::Image::BitmapData bits (gear, juce::Image::BitmapData::readWrite);
            if (! vp::copySystemGear (px, bits.data, bits.lineStride, gDarkMode))
            {
                gear = {};
            }
            else
            {
                // The system symbol comes in its own tint (blue in light mode).
                // Keep its shape - the alpha - and take the theme's text colour.
                const auto tint = text();
                for (int yy = 0; yy < px; ++yy)
                    for (int xx = 0; xx < px; ++xx)
                        bits.setPixelColour (xx, yy, tint.withAlpha (
                            static_cast<float> (bits.getPixelColour (xx, yy).getAlpha()) / 255.0f));
            }
        }
        // 20 pt glyph in whatever target it sits in: the 44 pt tap area is for
        // the thumb, the picture does not need to fill it.
        auto box = button.getLocalBounds();
        const int side = juce::jmin (20, juce::jmin (box.getWidth(), box.getHeight()));
        box = box.withSizeKeepingCentre (side, side);
        if (gear.isValid())
            g.drawImage (gear, box.toFloat(), juce::RectanglePlacement::centred);
        return;
    }

    g.setFont (f);
    const int style = buttonStyle (button);
    const float alpha = button.isEnabled()
                            ? (tight && style == 0 && ! button.getToggleState() ? 0.55f : 1.0f)
                            : 0.5f;
    juce::Colour labelCol = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                                       : juce::TextButton::textColourOffId);
    if (buttonIsSolidOn (button, button.findColour (juce::TextButton::buttonColourId)))
        labelCol = bg();                               // dark on the light face
    else if ((style == 1 || style == 2) && ! button.getToggleState())
        labelCol = mute();                             // off segments and pills are quiet
    g.setColour (labelCol.withMultipliedAlpha (alpha));
    if (style == 3)
    {
        // START / STOP: the play triangle or the stop square, then the word,
        // as one centred group.
        const bool armed = button.getToggleState();
        const float iconS = juce::jmin (static_cast<float> (area.getHeight()) * 0.34f, 22.0f);
        const float gapI = 12.0f;
        const float tw = juce::GlyphArrangement::getStringWidth (f, label);
        const float x0 = static_cast<float> (area.getCentreX()) - (iconS + gapI + tw) * 0.5f;
        const float cy = static_cast<float> (area.getCentreY());
        if (armed)
        {
            g.fillRoundedRectangle (x0, cy - iconS * 0.5f, iconS, iconS, 3.0f);
        }
        else
        {
            juce::Path tri;
            tri.addTriangle (x0 + iconS * 0.08f, cy - iconS * 0.55f,
                             x0 + iconS * 0.08f, cy + iconS * 0.55f,
                             x0 + iconS * 1.05f, cy);
            g.fillPath (tri);
        }
        g.drawText (label,
                    juce::Rectangle<float> (x0 + iconS + gapI, static_cast<float> (area.getY()),
                                            tw + 6.0f, static_cast<float> (area.getHeight())).toNearestInt(),
                    juce::Justification::centredLeft, false);
        return;
    }
    if (tight)
        g.drawFittedText (label, area, juce::Justification::centred, 2);
    else
        g.drawText (label, area, juce::Justification::centred, false);
}

void MainComponent::AppLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                          const juce::Colour& backgroundColour,
                                                          bool, bool shouldDrawButtonAsDown)
{
    // The gear and the EDIT pencil are bare icons: no fill, no edge, no bar.
    if ((bool) button.getProperties().getWithDefault ("gearIcon", false))
        return;
    if ((bool) button.getProperties().getWithDefault ("pencilIcon", false))
    {
        // EDIT: a quiet tile that lights its rim when the mode is on.
        const auto tile = button.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (ink());
        g.fillRoundedRectangle (tile, 12.0f);
        g.setColour (button.getToggleState() ? fuchsia() : border());
        g.drawRoundedRectangle (tile, 12.0f, button.getToggleState() ? 1.6f : 1.0f);
        return;
    }

    if ((bool) button.getProperties().getWithDefault ("circle", false))
    {
        auto bounds = button.getLocalBounds().toFloat();
        const float d = juce::jmin (bounds.getWidth(), bounds.getHeight());
        auto disc = bounds.withSizeKeepingCentre (d, d);
        const bool on = button.getToggleState() || shouldDrawButtonAsDown;
        g.setColour (on ? fuchsia() : ink());
        g.fillEllipse (disc);
        g.setColour (text().withAlpha (gDarkMode ? 0.22f : 0.28f));
        g.drawEllipse (disc.reduced (0.6f), 1.0f);
        return;
    }

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
            juce::ColourGradient fill (mute(), filled.getX(), filled.getBottom(),
                                       text(), filled.getX(), filled.getY(), false);
            g.setGradientFill (fill);
            g.fillRoundedRectangle (filled, 5.0f);
        }

        const float capW = 34.0f;
        const float capH = 16.0f;
        juce::Rectangle<float> cap (cx - capW * 0.5f, sliderPos - capH * 0.5f, capW, capH);
        paintRadial (g, { cx, sliderPos }, 22.0f, text(), 0.28f);
        g.setColour (gDarkMode ? juce::Colour (0xff2a2a30) : juce::Colour (0xfff3eef4));
        g.fillRoundedRectangle (cap.translated (0.0f, 1.5f), 4.0f);
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (cap, 4.0f);
        g.setColour (text());
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
        juce::ColourGradient fill (text(), filled.getX(), filled.getY(),
                                   text(), filled.getRight(), filled.getY(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (filled, 6.0f);
    }

    juce::ignoreUnused (minSliderPos, maxSliderPos);

    const float tr = 16.0f;
    paintRadial (g, { sliderPos, cy }, 26.0f, text(), 0.22f);
    g.setColour (text());
    g.drawEllipse (sliderPos - tr, cy - tr, tr * 2.0f, tr * 2.0f, 2.4f);
    g.setColour (juce::Colours::white);
    g.fillEllipse (sliderPos - tr + 2.6f, cy - tr + 2.6f, (tr - 2.6f) * 2.0f, (tr - 2.6f) * 2.0f);
}

void MainComponent::FaderZoom::show (juce::Slider& k)
{
    knob = &k;
    setBounds (owner.getLocalBounds());
    from = owner.getLocalArea (&k, k.getLocalBounds()).toFloat();
    target = 1.0f;
    setVisible (true);
    toFront (false);
    if (vblank == nullptr)
        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { tick(); });
}

void MainComponent::FaderZoom::hide()
{
    target = 0.0f;
}

void MainComponent::FaderZoom::tick()
{
    if (! isVisible())
        return;
    t += (target - t) * 0.20f;
    if (std::abs (target - t) < 0.004f)
        t = target;
    if (t <= 0.0f && target <= 0.0f)
    {
        setVisible (false);
        return;
    }
    repaint();
}

void MainComponent::FaderZoom::paint (juce::Graphics& g)
{
    if (t <= 0.001f || knob == nullptr)
        return;
    const float e = 1.0f - std::pow (1.0f - t, 3.0f);
    const auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.58f * e));
    g.fillRect (b);

    const float w = juce::jlimit (130.0f, 190.0f, b.getWidth() * 0.42f);
    const float h = juce::jmin (b.getHeight() * 0.64f, 460.0f);
    const auto to = juce::Rectangle<float> (w, h).withCentre (b.getCentre());
    const auto lerp = [e] (float a, float z) { return a + (z - a) * e; };
    const juce::Rectangle<float> r (lerp (from.getX(), to.getX()), lerp (from.getY(), to.getY()),
                                    lerp (from.getWidth(), to.getWidth()),
                                    lerp (from.getHeight(), to.getHeight()));

    auto& s = *knob;
    const bool voiceOff = ! (bool) s.getProperties().getWithDefault ("voiceEnabled", true);
    const bool hitKnob = s.getProperties().contains ("hitLit");
    const bool hitLit = (bool) s.getProperties().getWithDefault ("hitLit", false);
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    paintRadial (g, r.getCentre(), r.getHeight() * 0.7f, accent, 0.22f * e);
    paintVoiceFader (g, r, static_cast<float> (s.valueToProportionOfLength (s.getValue())),
                     s, accent, voiceOff, hitKnob, hitLit);
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
    const bool voiceKnob = (bool) slider.getProperties().getWithDefault ("voiceOnFill", false);
    const bool voiceOff = voiceKnob
                          && ! (bool) slider.getProperties().getWithDefault ("voiceEnabled", true);
    // Sample knobs carry hitLit. Their accents dim to 0.40 when idle;
    // the centre stays black whether the sample is playing or not.
    const bool hitKnob = slider.getProperties().contains ("hitLit");
    const bool hitLit = (bool) slider.getProperties().getWithDefault ("hitLit", false);
    const bool hitDark = hitKnob && ! hitLit;
    const juce::Colour accent = voiceKnob
        ? slider.findColour (juce::Slider::rotarySliderFillColourId)
        : fuchsia();
    const float alpha = slider.isEnabled() ? ((voiceOff || hitDark) ? 0.40f : 1.0f) : 0.45f;

    // Voices and effects are faders now; only the MIC trim keeps its own look.
    if (voiceKnob && ! (bool) slider.getProperties().getWithDefault ("micMeter", false))
    {
        paintVoiceFader (g, juce::Rectangle<int> (x, y, width, height).toFloat(), sliderPos,
                         slider, accent, voiceOff, hitKnob, hitLit);
        return;
    }

    const float innerR = juce::jmax (6.0f, arcRadius - lineW * 0.7f);
    const bool micMeter = (bool) slider.getProperties().getWithDefault ("micMeter", false);
    const auto micLook = micMeter
        ? micLevelLook (static_cast<float> (slider.getProperties().getWithDefault ("micHold", 0.0)))
        : MicLevelLook{};

    if (micMeter && (bool) slider.getProperties().getWithDefault ("micBar", false))
    {
        // Phone-width status row: the same meter laid flat, with the gain as a
        // marker you drag sideways. Fuchsia inside the analysis band, then
        // amber and red once it is too hot - the colours the disc used.
        const auto full = juce::Rectangle<int> (x, y, width, height).toFloat();
        const float barH = 12.0f;
        const auto bar = full.withSizeKeepingCentre (juce::jmax (8.0f, full.getWidth() - 10.0f), barH);
        const float rr = barH * 0.5f;
        const float a = slider.isEnabled() ? 1.0f : 0.45f;
        g.setColour (sliderTrack().withMultipliedAlpha (a));
        g.fillRoundedRectangle (bar, rr);

        const float bandFrom = meterPosition (kInputLowPeak);
        const float bandTo = meterPosition (kInputHighPeak);
        g.setColour (fuchsia().withAlpha ((gDarkMode ? 0.22f : 0.18f) * a));
        g.fillRect (bar.getX() + bar.getWidth() * bandFrom, bar.getY(),
                    bar.getWidth() * (bandTo - bandFrom), bar.getHeight());

        if (micLook.amount > 0.012f)
        {
            juce::ColourGradient grad (fuchsia(), bar.getX(), 0.0f,
                                       juce::Colour (0xffff3b30), bar.getRight(), 0.0f, false);
            grad.addColour (0.62, fuchsia());
            grad.addColour (0.82, juce::Colour (0xffffa726));
            g.saveState();
            g.reduceClipRegion (bar.withWidth (bar.getWidth() * micLook.amount).toNearestInt());
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bar, rr);
            g.restoreState();
        }

        const float mx = bar.getX() + bar.getWidth() * sliderPos;
        g.setColour (text().withMultipliedAlpha (a));
        g.fillRoundedRectangle (mx - 3.0f, full.getCentreY() - 14.0f, 6.0f, 28.0f, 3.0f);
        return;
    }

    if (micMeter)
    {
        const float span = rotaryEndAngle - rotaryStartAngle;
        const float bandFrom = meterPosition (kInputLowPeak);
        const float bandTo = meterPosition (kInputHighPeak);
        const float levelAngle = rotaryStartAngle + micLook.amount * span;

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (text().withAlpha (0.10f * alpha));
        g.strokePath (track, juce::PathStrokeType (lineW + 3.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        g.setColour (sliderTrack().withMultipliedAlpha (alpha));
        g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

        // Where the analysis wants to live: the stripe the old bar carried.
        juce::Path band;
        band.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                            rotaryStartAngle + bandFrom * span,
                            rotaryStartAngle + bandTo * span, true);
        g.setColour (fuchsia().withAlpha ((gDarkMode ? 0.22f : 0.18f) * alpha));
        g.strokePath (band, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        if (micLook.amount > 0.012f)
        {
            juce::Path level;
            level.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                rotaryStartAngle, levelAngle, true);
            g.setColour (micLook.colour.withMultipliedAlpha (alpha));
            g.strokePath (level, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        g.setColour (juce::Colour (0xff0a0a0c).withMultipliedAlpha (alpha));
        g.fillEllipse (centre.x - innerR, centre.y - innerR, innerR * 2.0f, innerR * 2.0f);

        if (micLook.amount > 0.01f)
        {
            // Clip to the disc: a glow larger than the knob is cut to the
            // slider's rectangle and reads as a pink square behind it.
            juce::Path disc;
            disc.addEllipse (centre.x - innerR, centre.y - innerR, innerR * 2.0f, innerR * 2.0f);
            g.saveState();
            g.reduceClipRegion (disc);
            const float glowA = juce::jmin (0.42f, micLook.glow) * alpha;
            juce::ColourGradient core (
                micLook.colour.withAlpha (glowA),
                centre.x, centre.y,
                micLook.colour.withAlpha (0.0f),
                centre.x, centre.y + innerR * 0.90f, true);
            g.setGradientFill (core);
            g.fillEllipse (centre.x - innerR, centre.y - innerR, innerR * 2.0f, innerR * 2.0f);
            g.restoreState();
        }

        g.setColour ((micLook.amount > 0.08f ? micLook.colour : juce::Colour (0xff2a2a30))
                         .withMultipliedAlpha (alpha));
        g.drawEllipse (centre.x - innerR, centre.y - innerR, innerR * 2.0f, innerR * 2.0f, 1.2f);
    }
    else
    {
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
            g.setColour (accent.withMultipliedAlpha (alpha));
            g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        paintRadial (g, centre, innerR * 1.7f, accent, (voiceKnob ? 0.28f : 0.14f) * alpha);
        const auto disc = hitKnob ? juce::Colours::black
                                  : (voiceKnob ? knobInterior (accent) : juce::Colour (0xff0a0a0c));
        g.setColour (hitKnob ? disc : disc.withMultipliedAlpha (alpha));
        g.fillEllipse (centre.x - innerR, centre.y - innerR, innerR * 2.0f, innerR * 2.0f);
        g.setColour ((voiceKnob ? accent : juce::Colour (0xff2a2a30))
                         .withMultipliedAlpha (alpha));
        g.drawEllipse (centre.x - innerR, centre.y - innerR, innerR * 2.0f, innerR * 2.0f,
                       voiceKnob ? 2.0f : 1.2f);
    }

    const juce::Colour needle = micMeter && micLook.amount > 0.08f ? micLook.colour : accent;
    const float pointerInner = innerR * 0.48f;
    const float pointerLen = innerR * 0.82f;
    const float pointerW = juce::jlimit (2.4f, 4.0f, innerR * 0.14f);
    const float ang = toAngle - juce::MathConstants<float>::halfPi;
    const auto origin = juce::Point<float> (
        centre.x + pointerInner * std::cos (ang),
        centre.y + pointerInner * std::sin (ang));
    const auto tip = juce::Point<float> (
        centre.x + pointerLen * std::cos (ang),
        centre.y + pointerLen * std::sin (ang));
    g.setColour (needle.withMultipliedAlpha (alpha));
    g.drawLine (origin.x, origin.y, tip.x, tip.y, pointerW);

    const auto valueText = knobValueText (slider);
    const bool longText = valueText.length() > 4;
    const float th = juce::jlimit (7.0f, longText ? 9.0f : 11.0f,
                                   innerR * (longText ? 0.32f : 0.40f));
    const juce::String name = slider.getProperties().getWithDefault ("knobName", {}).toString();
    const float nameH = name.isEmpty() ? 0.0f : juce::jlimit (7.0f, 9.0f, innerR * 0.26f);
    const float block = th + (nameH > 0.0f ? nameH + 1.0f : 0.0f);
    const float top = centre.y - block * 0.5f;
    const auto valueCol = (micMeter && micLook.amount > 0.18f ? juce::Colours::white : needle)
                              .withMultipliedAlpha (alpha);
    g.setFont (fontUi (th, true));
    g.setColour (valueCol);
    g.drawFittedText (valueText,
                      juce::Rectangle<float> (centre.x - innerR * 0.78f, top,
                                              innerR * 1.56f, th).toNearestInt(),
                      juce::Justification::centred, 1);
    if (nameH > 0.0f)
    {
        auto nameFont = fontUi (nameH, false);
        g.setFont (nameFont);
        g.setColour (valueCol.withMultipliedAlpha (0.72f));
        g.drawText (ellipsis (name, nameFont, innerR * 1.56f),
                    juce::Rectangle<float> (centre.x - innerR * 0.78f, top + th + 1.0f,
                                            innerR * 1.56f, nameH).toNearestInt(),
                    juce::Justification::centred, false);
    }

    // EDIT is on: this knob will open the sound modal. A dashed ring just
    // outside the arc says "tappable to change" without touching the fills.
    if ((bool) slider.getProperties().getWithDefault ("editMode", false))
    {
        juce::Path ring, dashed;
        const float rr = radius + 1.5f;
        ring.addEllipse (centre.x - rr, centre.y - rr, rr * 2.0f, rr * 2.0f);
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, ring, dashes, 2);
        g.setColour (fuchsia());
        g.strokePath (dashed, juce::PathStrokeType (1.0f));
    }
}
