// MainComponent: track waveform, style select and the two overlays.
#include "UI/MainComponentShared.h"

double MainComponent::TrackWaveform::proportionFromX (float x) const
{
    const float w = static_cast<float> (getWidth());
    if (w <= 1.0f)
        return 0.0;
    return juce::jlimit (0.0, 1.0, static_cast<double> (x) / static_cast<double> (w));
}

void MainComponent::TrackWaveform::drawPlayhead (juce::Graphics& g, juce::Rectangle<float> wave,
                                                 double proportion, juce::Colour col,
                                                 bool handle) const
{
    if (proportion < 0.0 || proportion > 1.0 || wave.isEmpty())
        return;

    const float x = wave.getX() + static_cast<float> (proportion) * wave.getWidth();
    g.setColour (col);
    g.drawLine (x, wave.getY(), x, wave.getBottom(), handle ? 2.5f : 1.5f);

    if (handle)
    {
        const float r = 9.0f;
        const float cy = wave.getCentreY();
        g.setColour (col.withAlpha (0.35f));
        g.fillEllipse (x - r - 2.0f, cy - r - 2.0f, (r + 2.0f) * 2.0f, (r + 2.0f) * 2.0f);
        g.setColour (col);
        g.fillEllipse (x - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.95f));
        g.drawEllipse (x - r, cy - r, r * 2.0f, r * 2.0f, 2.0f);
    }
}

void MainComponent::TrackWaveform::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty())
        return;

    g.setColour (ink());
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (text().withAlpha (0.10f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    const auto& peaks = owner.trackWavePeaks;
    const auto wave = bounds.reduced (6.0f, 10.0f);

    if (peaks.isEmpty() || owner.trackWaveLengthSec <= 0.0
        || (owner.trackWaveScanReader != nullptr && owner.trackWaveScanPos == 0))
    {
        g.setColour (mute());
        g.setFont (fontUi (11.0f, false));
        g.drawFittedText ("Onda in caricamento...", bounds.toNearestInt(),
                          juce::Justification::centred, 1);
        return;
    }

    const float midY = wave.getCentreY();
    const float halfH = wave.getHeight() * 0.40f;
    const int w = juce::jmax (1, (int) wave.getWidth());

    g.setColour (fuchsia().withAlpha (0.70f));
    for (int x = 0; x < w; ++x)
    {
        const int idx = (x * peaks.size()) / w;
        const float amp = peaks[juce::jlimit (0, peaks.size() - 1, idx)]
                          / juce::jmax (owner.trackWaveMaxPeak, 1.0e-9f);
        const float barH = juce::jmax (1.0f, amp * halfH * 2.0f);
        const float px = wave.getX() + static_cast<float> (x);
        g.fillRect (px, midY - barH * 0.5f, 1.0f, barH);
    }

    const double play = owner.trackWaveLengthSec > 0.0
                            ? owner.trackTransport.getCurrentPosition() / owner.trackWaveLengthSec
                            : 0.0;
    drawPlayhead (g, wave, play, fuchsia(), true);

    if (owner.trackWavePreview >= 0.0)
        drawPlayhead (g, wave, owner.trackWavePreview, text().withAlpha (0.75f), true);
}

void MainComponent::TrackWaveform::mouseEnter (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void MainComponent::TrackWaveform::mouseExit (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void MainComponent::TrackWaveform::mouseDown (const juce::MouseEvent& e)
{
    owner.trackWavePreview = proportionFromX (static_cast<float> (e.position.x));
    repaint();
    owner.repaint();
}

void MainComponent::TrackWaveform::mouseDrag (const juce::MouseEvent& e)
{
    owner.trackWavePreview = proportionFromX (static_cast<float> (e.position.x));
    repaint();
    owner.repaint();
}

void MainComponent::TrackWaveform::mouseUp (const juce::MouseEvent& e)
{
    owner.seekInternalTrack (proportionFromX (static_cast<float> (e.position.x)));
}

void MainComponent::StyleSelect::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float round = juce::jmin (12.0f, bounds.getHeight() * 0.5f);
    g.setColour (ink());
    g.fillRoundedRectangle (bounds.reduced (0.5f), round);
    g.setColour (border());
    g.drawRoundedRectangle (bounds.reduced (0.5f), round, 1.0f);

    const bool autoOn = owner.engine.settings().grooveAuto.load();
    const auto chosen = static_cast<vp::GrooveStyle> (
        owner.engine.settings().grooveStyle.load());
    // The style name is the big word. Under AUTO it is the one the detector has
    // landed on (without selecting it), and a small AUTO tag says who chose.
    juce::String mainText = juce::String (vp::toString (chosen));
    juce::String tag;
    if (autoOn)
    {
        const auto detected = static_cast<vp::GrooveStyle> (owner.snap.grooveStyle);
        if (detected != vp::GrooveStyle::count)
        {
            mainText = juce::String (vp::toString (detected));
            tag = "AUTO";
        }
        else
            mainText = "AUTO";
    }

    const float h = static_cast<float> (getHeight());
    const auto f = fontUi (juce::jlimit (13.0f, 19.0f, h * 0.36f));
    const auto tagFont = fontUi (juce::jlimit (10.0f, 13.0f, h * 0.26f), false);
    const float chevW = 26.0f;
    auto area = getLocalBounds().toFloat().reduced (12.0f, 0.0f).withTrimmedRight (chevW);
    const float mainW = juce::GlyphArrangement::getStringWidth (f, mainText);
    const float tagW = tag.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (tagFont, tag) + 8.0f;
    g.setFont (f);
    g.setColour (text());
    g.drawText (mainText, area.toNearestInt(), juce::Justification::centredLeft, false);
    if (tagW > 0.0f && mainW + tagW <= area.getWidth())
    {
        g.setFont (tagFont);
        g.setColour (mute());
        g.drawText (tag, area.toNearestInt(), juce::Justification::centredRight, false);
    }

    auto chev = juce::Rectangle<float> (static_cast<float> (getWidth()) - 20.0f,
                                        h * 0.5f - 2.5f, 8.0f, 5.0f);
    juce::Path p;
    p.addTriangle (chev.getX(), chev.getY(),
                   chev.getRight(), chev.getY(),
                   chev.getCentreX(), chev.getBottom());
    g.setColour (mute());
    g.fillPath (p);
}

void MainComponent::StyleSelect::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked() || ! getLocalBounds().contains (e.getPosition()))
        return;
    if (owner.styleMenu.isOpen())
        owner.styleMenu.dismiss();
    else
    {
        owner.soundMenu.dismiss();
        owner.styleMenu.showBelow (getBounds());
    }
}

void MainComponent::StyleSelect::refresh()
{
    repaint();
}

MainComponent::StyleMenuOverlay::StyleMenuOverlay (MainComponent& o)
    : owner (o)
{
    setVisible (false);
    setOpaque (false);
    setInterceptsMouseClicks (true, true);
    addAndMakeVisible (list);
    list.setOpaque (false);
    for (int i = 0; i < kCount; ++i)
    {
        list.addAndMakeVisible (items[i]);
        items[i].setButtonText (styleMenuLabel (i));
        items[i].onClick = [this, i]
        {
            if (i == 0)
                owner.applyStyleAuto (true);
            else
                owner.applyStyle (static_cast<vp::GrooveStyle> (i - 1));
            dismiss();
        };
    }
}

void MainComponent::StyleMenuOverlay::showBelow (juce::Rectangle<int> /*anchorInParent*/)
{
    setBounds (owner.getLocalBounds());
    setVisible (true);
    toFront (false);
    resized();
}

void MainComponent::StyleMenuOverlay::dismiss()
{
    setVisible (false);
}

void MainComponent::StyleMenuOverlay::mouseDown (const juce::MouseEvent& e)
{
    const auto inside = owner.isCompact() ? sheet : list.getBounds();
    if (! inside.contains (e.getPosition()))
        dismiss();
}

void MainComponent::StyleMenuOverlay::paint (juce::Graphics& g)
{
    if (! owner.isCompact() || sheet.isEmpty())
        return;
    g.fillAll (juce::Colours::black.withAlpha (gDarkMode ? 0.66f : 0.42f));
    // Runs 24 pt past the bottom edge so only the top corners round.
    const auto r = sheet.toFloat().withHeight (static_cast<float> (sheet.getHeight()) + 24.0f);
    g.setColour (panel());
    g.fillRoundedRectangle (r, 24.0f);
    g.setColour (border());
    g.drawRoundedRectangle (r.reduced (0.5f), 24.0f, 1.0f);
    g.setColour (mute());
    g.setFont (fontUi (13.0f));
    g.drawText ("STILE", sheet.withHeight (44).reduced (20, 0), juce::Justification::centredLeft, false);
}

void MainComponent::StyleMenuOverlay::resized()
{
    if (! isVisible())
        return;

    auto anchor = getLocalArea (&owner.styleSelect, owner.styleSelect.getLocalBounds());
    const int n = kCount;
    const bool sheetMode = owner.isCompact();
    juce::Rectangle<int> cell[kCount];
    int itemH = 0;
    if (sheetMode)
    {
        // Two columns, 52 pt rows, row-major: AUTO and MARCHA, ROCK and DANCE...
        const int cols = 2, gapS = 8, titleH = 44, rowH = 52;
        const int rows = (n + cols - 1) / cols;
        const int listH = rows * rowH + (rows - 1) * gapS;
        const int bottomInset = owner.effectiveSafeArea().getBottom();
        const int sheetH = titleH + listH + 20 + bottomInset;
        sheet = getLocalBounds().removeFromBottom (juce::jmin (sheetH, getHeight()));
        list.setBounds (sheet.getX() + 16, sheet.getY() + titleH, sheet.getWidth() - 32, listH);
        const int cellW = juce::jmax (1, (list.getWidth() - gapS) / cols);
        for (int i = 0; i < n; ++i)
            cell[i] = { (i % cols) * (cellW + gapS), (i / cols) * (rowH + gapS), cellW, rowH };
    }
    else
    {
        sheet = {};
        const int gap = 0;
        const int maxH = juce::jmax (1, getHeight() - 12);
        itemH = juce::jlimit (24, juce::jmax (24, anchor.getHeight()), maxH / n);
        const int listH = n * itemH + gap * (n - 1);
        const int listW = juce::jmax (anchor.getWidth(), 120);
        int y = anchor.getBottom();
        if (y + listH > getHeight() - 4)
            y = juce::jmax (4, anchor.getY() - listH);
        int x = anchor.getX();
        if (x + listW > getWidth() - 4)
            x = juce::jmax (4, getWidth() - listW - 4);
        list.setBounds (x, y, listW, listH);
    }
    repaint();

    auto row = list.getLocalBounds();
    const bool autoOn = owner.engine.settings().grooveAuto.load();
    const int cur = owner.engine.settings().grooveStyle.load();
    const int detected = owner.snap.grooveStyle;

    for (int i = 0; i < n; ++i)
    {
        items[i].setBounds (sheetMode ? cell[i] : row.removeFromTop (itemH));
        const bool on = (i == 0) ? autoOn
                                 : (! autoOn && cur == i - 1);
        // Under AUTO the detected style is tinted, not selected. DUE-UNO is
        // never chosen by the detector.
        const bool hinted = autoOn && i > 0 && i - 1 == detected
                            && i - 1 != static_cast<int> (vp::GrooveStyle::twoOne);
        items[i].setToggleState (on, juce::dontSendNotification);
        items[i].setColour (juce::TextButton::buttonColourId, ink());
        // The chosen style takes the light face (see the plain button look);
        // under AUTO the detector's pick is tinted green, "locked".
        items[i].setColour (juce::TextButton::textColourOffId, hinted ? stateLocked() : text());
        items[i].setColour (juce::TextButton::textColourOnId, text());
    }
}

MainComponent::SoundMenuOverlay::SoundMenuOverlay (MainComponent& o)
    : owner (o)
{
    setVisible (false);
    setOpaque (false);
    setInterceptsMouseClicks (true, true);
    addAndMakeVisible (list);
    list.setOpaque (false);
    for (int i = 0; i < kCount; ++i)
    {
        list.addAndMakeVisible (items[i]);
        items[i].onClick = [this, i]
        {
            if (hitMode)
                owner.assignHitSample (slot, i);
            else
                owner.assignKitSound (slot, static_cast<vp::KitSound> (i));
            dismiss();
        };
    }
}

void MainComponent::SoundMenuOverlay::showFor (int s, bool hits)
{
    slot = s;
    hitMode = hits;
    setBounds (owner.getLocalBounds());
    setVisible (true);
    toFront (false);
    resized();
}

void MainComponent::SoundMenuOverlay::dismiss()
{
    setVisible (false);
}

void MainComponent::SoundMenuOverlay::paint (juce::Graphics& g)
{
    // Settings is an opaque page (fillAll(bg)), not a blur of the editor.
    // A live Gaussian would snapshot the parent every frame on iPad. This
    // modal keeps the editor visible behind it; veil it enough that the
    // Misure cells do not merge with the knobs underneath.
    auto full = getLocalBounds().toFloat();
    g.setColour ((gDarkMode ? juce::Colour (0xff050506) : juce::Colour (0xfff5f1f6))
                     .withAlpha (gDarkMode ? 0.78f : 0.70f));
    g.fillRect (full);

    // Same card as the pages: rounded 24, panel fill, hairline border, a soft
    // brand bloom coming off the top edge.
    const auto c = card.toFloat();
    g.setColour (panel());
    g.fillRoundedRectangle (c, 24.0f);
    {
        juce::Path cp;
        cp.addRoundedRectangle (c, 24.0f);
        g.saveState();
        g.reduceClipRegion (cp);
        paintRadial (g, { c.getCentreX(), c.getY() }, c.getWidth() * 0.9f, fuchsia(),
                     gDarkMode ? 0.12f : 0.07f);
        g.restoreState();
    }
    g.setColour (border());
    g.drawRoundedRectangle (c.reduced (0.5f), 24.0f, 1.0f);

    // Title and what is being replaced. The knob keeps its name until a
    // choice is made, so the player sees which one this modal is for.
    juce::Slider* kit[] = { &owner.shakerVolSlider, &owner.congaVolSlider,
                            &owner.cembaloVolSlider, &owner.clapVolSlider };
    juce::Slider* hit[] = { &owner.absorbVolSlider, &owner.hornVolSlider,
                            &owner.uplifterVolSlider, &owner.riserVolSlider };
    const auto current = (hitMode ? hit : kit)[juce::jlimit (0, 3, slot)]
                             ->getProperties().getWithDefault ("knobName", {}).toString();
    auto head = card.reduced (16, 0).withTrimmedTop (12).removeFromTop (kTitleH - 12);
    g.setColour (text());
    g.setFont (fontUi (15.0f, true));
    g.drawText (hitMode ? "CAMPIONI" : "SUONI", head.removeFromTop (18),
                juce::Justification::centredLeft, false);
    g.setColour (mute());
    g.setFont (fontUi (10.5f));
    g.drawText (juce::String ("al posto di ") + current, head,
                juce::Justification::centredLeft, false);

    if (shown == 0)
    {
        g.setColour (mute());
        g.setFont (fontUi (12.0f));
        g.drawFittedText (hitMode ? "Nessun campione libero"
                                  : "Nessun suono libero",
                          list.getBounds(), juce::Justification::centred, 2);
    }
}

void MainComponent::SoundMenuOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (! card.contains (e.getPosition()))
        dismiss();
}

void MainComponent::SoundMenuOverlay::resized()
{
    if (! isVisible())
        return;

    // Offer only sounds not assigned to another knob. The current assignment
    // is omitted too; tapping outside keeps it.
    const int count = hitMode ? kHitSampleCount : static_cast<int> (vp::KitSound::count);
    bool taken[kCount] = {};
    for (int s = 0; s < 4; ++s)
    {
        const int v = hitMode ? owner.hitVoices[s].selected.load (std::memory_order_relaxed)
                              : owner.kitSoundAtomic (s).load();
        if (v >= 0 && v < count)
            taken[v] = true;
    }
    int vis[kCount];
    int n = 0;
    for (int i = 0; i < count; ++i)
        if (! taken[i])
            vis[n++] = i;
    shown = n;
    for (int i = 0; i < kCount; ++i)
        items[i].setVisible (false);

    // A centred modal, not a menu hanging off the knob: the knob row is small
    // and sits at the bottom of the screen, and a finger is about to cover it.
    // Chips are touch-sized (up to 40 pt, shrunk to fit a short window).
    const int btnGap = 6;
    const int pad = 16;
    const int margin = 12;
    const int rows = juce::jmax (1, n);
    const int availH = getHeight() - 2 * margin - kTitleH - pad;
    const int cellH = juce::jlimit (28, 40, (availH - btnGap * (rows - 1)) / rows);
    const float chipFontH = juce::jmax (12.0f, (float) cellH * 0.38f);
    const auto measureFont = fontUi (chipFontH, true);
    int labelW = 0;
    for (int k = 0; k < n; ++k)
    {
        const auto name = juce::String (hitMode ? kHitNames[vis[k]]
            : vp::toString (static_cast<vp::KitSound> (vis[k])));
        labelW = juce::jmax (labelW, juce::GlyphArrangement::getStringWidthInt (measureFont, name));
    }
    const int listW = juce::jmin (getWidth() - 2 * (margin + pad),
                                  juce::jmax (220, labelW + 48));
    const int listH = rows * cellH + btnGap * (rows - 1);

    card = juce::Rectangle<int> (listW + 2 * pad, kTitleH + listH + pad)
               .withCentre (getLocalBounds().getCentre());
    list.setBounds (card.getX() + pad, card.getY() + kTitleH, listW, listH);

    auto row = list.getLocalBounds();
    for (int k = 0; k < n; ++k)
    {
        const int i = vis[k];
        auto cell = row.removeFromTop (cellH);
        if (k + 1 < n)
            row.removeFromTop (btnGap);
        items[i].setVisible (true);
        items[i].setBounds (cell);
        items[i].setButtonText (hitMode ? kHitNames[i]
            : vp::toString (static_cast<vp::KitSound> (i)));
        const auto fill = hitMode ? knobInterior (colourForHit (i))
                                  : knobInteriorForKitSound (static_cast<vp::KitSound> (i));
        items[i].setToggleState (false, juce::dontSendNotification);
        items[i].getProperties().set ("chipFill", true);
        items[i].setColour (juce::TextButton::buttonColourId, fill);
        items[i].setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        items[i].setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }
    repaint();
}
