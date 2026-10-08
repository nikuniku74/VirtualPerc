// MainComponent: where everything goes: compact and full layouts, settings page.
#include "UI/MainComponentShared.h"

void MainComponent::relayoutSettings()
{
    if (settingsOverlay.getWidth() > 0 && settingsOverlay.getHeight() > 0)
        layoutSettings (settingsOverlay.getLocalBounds());

    layoutTrackWaveform();
}

juce::BorderSize<int> MainComponent::effectiveSafeArea() const
{
   #if JUCE_IOS
    const auto win = vp::windowSafeAreaInsets();
    juce::BorderSize<int> safe { win.top, win.left, win.bottom, win.right };

    // JUCE's copy as a floor, not as the answer: whichever source knows about
    // a given edge, that edge gets cleared.
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForRect (getScreenBounds());
    if (display == nullptr)
        display = displays.getPrimaryDisplay();
    if (display != nullptr)
    {
        const auto d = display->safeAreaInsets;
        safe = { juce::jmax (safe.getTop(), d.getTop()),
                 juce::jmax (safe.getLeft(), d.getLeft()),
                 juce::jmax (safe.getBottom(), d.getBottom()),
                 juce::jmax (safe.getRight(), d.getRight()) };
    }
    return safe;
   #else
    return {};
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
    const auto safe = effectiveSafeArea();
    pad = { juce::jmax (pad.getTop(), safe.getTop()),
            juce::jmax (pad.getLeft(), safe.getLeft()),
            juce::jmax (pad.getBottom(), safe.getBottom()),
            juce::jmax (pad.getRight(), safe.getRight()) };
    return pad.subtractedFrom (area);
   #else
    return area.reduced (30, 24);
   #endif
}

juce::Rectangle<int> MainComponent::layoutColumn() const
{
    // START owns a standard button strip along the bottom of the page.
    // Everything else is laid out in what remains.
    auto r = safePadded (getLocalBounds());
    r.removeFromBottom (juce::jmin (52, juce::jmax (0, r.getHeight() / 8)));
    return r;
}

bool MainComponent::isLandscape() const
{
    return getWidth() > getHeight();
}

bool MainComponent::isCompact() const noexcept
{
    return compactLayout;
}

void MainComponent::updateCompactLayout() noexcept
{
    const int w = getWidth();
    const int h = getHeight();
    // Enter as soon as the two-pane page would overflow. Leave only once both
    // sides are clearly large enough, so a drag across the threshold cannot
    // rebuild the page every pixel (cards.clearQuick, a handful of setVisible,
    // full relayout) and stall the message thread against the audio callback.
    if (compactLayout)
    {
        if (w >= 600 && h >= 740)
            compactLayout = false;
    }
    else if (w < 560 || h < 680)
    {
        compactLayout = true;
    }
}

juce::Rectangle<int> MainComponent::compactPadded (juce::Rectangle<int> area) const
{
   #if JUCE_IOS
    // The iPad window controls (move / resize) are drawn on top of the
    // content and are not in the safe area. 32 still left the status row
    // and the tempo orb under that bar. 80 clears it. A phone has no such
    // bar; its top is the safe area alone.
    const auto* screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const bool ipad = screen != nullptr
                      && juce::jmin (screen->totalArea.getWidth(),
                                     screen->totalArea.getHeight()) >= 700;
    const int topFloor = ipad ? 80 : 10;
    juce::BorderSize<int> pad { topFloor, 8, 8, 8 };
    const auto safe = effectiveSafeArea();
    pad = { juce::jmax (pad.getTop(), safe.getTop()),
            juce::jmax (pad.getLeft(), safe.getLeft()),
            juce::jmax (pad.getBottom(), safe.getBottom()),
            juce::jmax (pad.getRight(), safe.getRight()) };
    const int maxX = juce::jmax (0, (area.getWidth() - 8) / 2);
    // A short Split View slice cannot keep both the system status bar and the
    // home indicator. Keep the top: the in-app status row sits under it, and
    // clamping it with maxY is how the BPM ended up under the bar.
    const int top = pad.getTop();
    const int leftoverY = juce::jmax (0, area.getHeight() - 8 - top);
    pad = { top,
            juce::jmin (pad.getLeft(), maxX),
            juce::jmin (pad.getBottom(), leftoverY),
            juce::jmin (pad.getRight(), maxX) };
    return pad.subtractedFrom (area);
   #else
    const int rx = juce::jmin (10, juce::jmax (0, area.getWidth() / 4));
    const int ry = juce::jmin (8, juce::jmax (0, area.getHeight() / 4));
    return area.reduced (rx, ry);
   #endif
}

void MainComponent::applyCompactVisibility()
{
    const bool compact = isCompact();
    // SETUP stays visible on a phone too - it is the only way into the settings
    // page, and the compact layout gives it the status row's right side.
    settingsButton.setVisible (true);
    followButton.setVisible (true);
    fixedButton.setVisible (true);
    startNowButton.setVisible (true);
    // Visible on a phone too: it now has its own row under the dots instead of
    // having to share their width.
    barButton.setVisible (false);
    const bool showNudge = ! compact && ! engine.settings().tempoFollow.load();
    bpmNudgeDown.setVisible (showNudge);
    bpmNudgeUp.setVisible (showNudge);
    bpmEdit.setVisible (showNudge);

    if (compact && settingsOverlay.isVisible())
        setSettingsOpen (false);

    refreshTempoModeButtons();
}

void MainComponent::layoutMisure (juce::Rectangle<int> body)
{
    // Three groups with a meaning each: the style, how it is subdivided (one
    // segmented track, 1/4 - 1/8 - 1/16) and the three character switches
    // (pills). Wide: one row. Narrow: style and track, then the pills.
    const int gap = 8;
    juce::TextButton* segs[]  = { &sub4, &sub8, &sub16 };
    juce::TextButton* pills[] = { &dynamicsButton, &naturalButton, &swingButton };

    const auto placeSeg = [this, &segs] (juce::Rectangle<int> r)
    {
        subdivisionTrack = r;
        const int w = juce::jmax (1, r.getWidth() / 3);
        for (int i = 0; i < 3; ++i)
            segs[i]->setBounds (i < 2 ? r.removeFromLeft (w) : r);
    };
    const auto placePills = [this, gap, &pills] (juce::Rectangle<int> r)
    {
        // The EDIT pencil rides at the end of the pills: small, same height.
        {
            auto pen = r.removeFromRight (36);
            r.removeFromRight (gap);
            editSoundsButton.setBounds (pen);
            editSoundsButton.setVisible (true);
            editSoundsButton.toFront (false);
        }
        const int w = juce::jmax (1, (r.getWidth() - gap * 2) / 3);
        for (int i = 0; i < 3; ++i)
        {
            pills[i]->setBounds (i < 2 ? r.removeFromLeft (w) : r);
            if (i < 2)
                r.removeFromLeft (gap);
        }
    };

    const int rowH2 = juce::jmin (40, (body.getHeight() - gap) / 2);
    if (body.getWidth() < 640 && rowH2 >= 32)
    {
        auto block = body.withSizeKeepingCentre (body.getWidth(), rowH2 * 2 + gap);
        auto top = block.removeFromTop (rowH2);
        block.removeFromTop (gap);
        auto bottom = block.removeFromTop (rowH2);
        styleSelect.setBounds (top.removeFromLeft (clampW (96, 150, top.getWidth() * 36 / 100)));
        top.removeFromLeft (gap);
        placeSeg (top);
        placePills (bottom);
        return;
    }

    auto row = body.withSizeKeepingCentre (body.getWidth(),
        juce::jmin (body.getHeight(), clampW (36, 46, body.getHeight())));
    styleSelect.setBounds (row.removeFromLeft (clampW (110, 170, row.getWidth() / 5)));
    row.removeFromLeft (gap);
    placeSeg (row.removeFromLeft (clampW (200, 280, row.getWidth() * 30 / 100)));
    row.removeFromLeft (gap);
    placePills (row);
}

void MainComponent::layoutVoicesRow (juce::Rectangle<int> body)
{
    juce::Component* voices[] = { &shakerVolSlider, &congaVolSlider, &cembaloVolSlider, &clapVolSlider };
    juce::Label* labels[] = { &shakerVolLabel, &congaVolLabel, &cembaloVolLabel, &clapVolLabel,
                              &absorbHitLabel, &hornHitLabel, &uplifterHitLabel, &riserHitLabel };
    juce::Label* values[] = { &shakerVolValue, &congaVolValue, &cembaloVolValue, &clapVolValue,
                              &absorbHitValue, &hornHitValue, &uplifterHitValue, &riserHitValue };
    // Names and values are painted inside each fader.
    for (auto* l : labels) l->setVisible (false);
    for (auto* v : values) v->setVisible (false);

    juce::Component* hits[] = { &absorbVolSlider, &hornVolSlider, &uplifterVolSlider, &riserVolSlider };
    // Two rows of four, 14 pt between faders so a thumb can pick one without
    // brushing its neighbour. The voices get a little more height: they are
    // the ones ridden during the song; the effects are fired.
    // The EDIT pencil has a 44 pt rail of its own on the right, centred on the
    // two rows: a real tap target, and no title strip needed to hold it.
    const int gapX = 14;
    const int gapY = 10;
    auto voiceRow = body.removeFromTop (juce::jmax (1, (body.getHeight() - gapY) * 56 / 100));
    body.removeFromTop (gapY);
    const auto placeRow = [gapX] (juce::Rectangle<int> row, juce::Component* const* cells)
    {
        const int w = juce::jmax (1, (row.getWidth() - gapX * 3) / 4);
        for (int i = 0; i < 4; ++i)
        {
            cells[i]->setBounds (row.removeFromLeft (w));
            cells[i]->setVisible (true);
            if (i < 3)
                row.removeFromLeft (gapX);
        }
    };
    placeRow (voiceRow, voices);
    placeRow (body, hits);

}

MainComponent::CompactGeom MainComponent::compactGeom() const
{
    auto r = compactPadded (getLocalBounds());
    r.removeFromBottom (juce::jmin (52, juce::jmax (0, r.getHeight() / 8)));
    CompactGeom g;
    const int n = juce::jmax (1, r.getHeight());
    const int gap = 6;
    constexpr int kChrome = 12;      // compact card padding (no title strip)
    constexpr int kMisureRow = 40 * 2 + 8;   // two rows of 40 pt
    const int misureH = kChrome + kMisureRow;
    const int knobsH = kChrome + 190;   // voices over effects, two rows of faders
    // On a phone the squares (MISURE, seven across) and the knobs (FEEL, four
    // across) are limited by their *width*, so handing their cards extra height
    // only floats them in empty space - which is where a tall portrait screen
    // was going. Size those two to their content and give the rest to the two
    // things that do grow with the room: the tempo read-out and TRASPORTO's
    // buttons. In landscape there is not even room for that, so everything
    // shrinks together rather than the last card running off the bottom.
    const int room = juce::jmax (0, n - 2 * gap);
    const int tempoMin = kCompactTempoNatural;
    int tempoH, misH, knH;
    const int natural = tempoMin + misureH + knobsH;
    if (natural > room)
    {
        const float f = static_cast<float> (room) / static_cast<float> (natural);
        tempoH = juce::jmax (40, juce::roundToInt (static_cast<float> (tempoMin) * f));
        misH = juce::jmax (30, juce::roundToInt (static_cast<float> (misureH) * f));
        knH = juce::jmax (44, juce::roundToInt (static_cast<float> (knobsH) * f));
    }
    else
    {
        tempoH = tempoMin;
        int extra = room - tempoMin - misureH - knobsH;
        misH = misureH;
        knH = knobsH + juce::jmax (0, extra);
    }
    juce::ignoreUnused (knH);
    g.tempo = r.removeFromTop (takeAtMost (r.getHeight(), tempoH));
    if (r.getHeight() > gap) r.removeFromTop (gap);
    g.transport = {};
    g.misure = r.removeFromTop (takeAtMost (r.getHeight(), misH));
    if (r.getHeight() > gap) r.removeFromTop (gap);
    // Faders stop growing at 270 pt: past that a tall phone gets a longer
    // throw and nothing else. What is left stays empty above START, which is
    // the clear space the transport wants round it anyway.
    g.knobs = r.removeFromTop (juce::jmin (r.getHeight(), 270));
    return g;
}

MainComponent::StageRows MainComponent::compactTempoRows (juce::Rectangle<int> area) const
{
    // Natural heights, shrunk together when the slice is short and never
    // stretched when it is tall: a column that grows to fill whatever it is
    // given is what put a hole under the dots.
    const float fit = area.getHeight() < kCompactTempoNatural
                          ? static_cast<float> (area.getHeight())
                              / static_cast<float> (kCompactTempoNatural)
                          : 1.0f;
    const auto px = [fit] (int v)
    {
        return juce::jmax (1, juce::roundToInt (static_cast<float> (v) * fit));
    };

    StageRows s;
    // FOLLOWING / IN ASCOLTO. SEGUI/FISSO stay off this row: a Split View
    // column cannot spend that width, and the colour already carries the mode.
    s.pill = area.removeFromTop (takeAtMost (area.getHeight(), px (kCompactPillH)));
    // SETUP rides the status row's right side: the words are left-aligned and
    // the rest of the row is empty, and a phone has no title row to put it in.
    {
        const int side = juce::jmin (s.pill.getHeight(), 44);
        s.settings = s.pill.removeFromRight (side);
        if (s.pill.getWidth() > 8)
            s.pill.removeFromRight (6);
        // The level bar takes the right-hand part of what is left, as long as
        // the state keeps ~90 pt for its dot and word.
        const int meterW = clampW (72, 130, s.pill.getWidth() * 3 / 10);
        if (s.pill.getWidth() > meterW + 90)
        {
            s.meter = s.pill.removeFromRight (meterW);
            s.pill.removeFromRight (8);
        }
    }
    area.removeFromTop (takeAtMost (area.getHeight(), px (kCompactGapA)));
    // The phase orb rides on top of the number, inside the card.
    s.lane = area.removeFromTop (takeAtMost (area.getHeight(), px (kCompactLaneH)));
    const int bpmH = px (kCompactBpmH);
    s.bpm = area.removeFromTop (takeAtMost (area.getHeight(), bpmH));
    // The number has the whole width now: the octave buttons moved down next
    // to TAP, so "128.4" can be as large as the column lets it.
    s.bpmNumber = s.bpm.reduced (4, 0);
    s.bpmLabel = area.removeFromTop (takeAtMost (area.getHeight(), px (kCompactLabelH)));
    area.removeFromTop (takeAtMost (area.getHeight(), px (kCompactGapB)));
    s.beats = area.removeFromTop (takeAtMost (area.getHeight(), px (kCompactBeatsH)));
    s.tempoLine = area.removeFromTop (takeAtMost (area.getHeight(), px (kCompactNoteH)));
    area.removeFromTop (takeAtMost (area.getHeight(), px (kCompactGapC)));
    // ÷2 | TAP | ×2. Declaring the one is still a tap on the number.
    {
        auto row = area.removeFromTop (takeAtMost (area.getHeight(), px (kCompactBarH)));
        const int gapX = 12;
        const int octW = clampW (48, 72, row.getWidth() / 5);
        s.octaveDown = row.removeFromLeft (octW);
        s.octaveUp = row.removeFromRight (octW);
        if (row.getWidth() > gapX * 2)
            row.reduce (gapX, 0);
        s.tap = row;
        s.barShift = {};
    }
    return s;
}

juce::Rectangle<int> MainComponent::stageArea() const
{
    if (isCompact())
        return compactGeom().tempo;

    auto r = layoutColumn();
    if (isLandscape())
        return r.removeFromLeft (juce::roundToInt (static_cast<float> (r.getWidth()) * 0.44f));

    const int consoleH = juce::jlimit (330, 560,
                                       juce::roundToInt (static_cast<float> (r.getHeight()) * 0.54f));
    return r.removeFromTop (juce::jmax (240, r.getHeight() - consoleH));
}

MainComponent::StageRows MainComponent::stageRows (juce::Rectangle<int> area) const
{
    if (isCompact())
        return compactTempoRows (area);

    // Sized first, placed second. The rows have natural heights; whatever is
    // left over goes above and below so the block sits in the upper middle of
    // whatever space the orientation gives it, rather than piling up at the top
    // and leaving a hole under it.
    //
    // And when there is *less* than the natural height, everything shrinks
    // together instead of the last rows running off the bottom. The minimums
    // below plus the fixed gaps come to about 310 points, which an iPad always
    // has and a phone in landscape does not: the stage there is nearer 290, so
    // without this PARTE would simply fall off the end.
    const int naturalBpm = juce::jlimit (72, 156, area.getHeight() / 4);
    const int naturalBeats = juce::jlimit (52, 96, area.getHeight() / 6);
    const bool follow = engine.settings().tempoFollow.load();
    // "L'1 è QUI" lives on the status row, where SEGUI/FISSO used to. The
    // 36-point row under the tempo is only the ± BPM nudge that appears
    // under FISSO.
    const int trackExtra = trackReader != nullptr ? trackWaveformHeight() + 6 : 0;
    const int natural = 44 + 22 + naturalBpm + 16 + 18 + 4
                        + naturalBeats + (follow ? 0 : 28) + 18 + 18
                        + 56 + 20 + trackExtra;
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
    // No title on the live page (it lives in SETUP). The first row is the
    // status: state on the left, MIC level and gain, then SETUP - the same
    // row the phone-width page has.
    s.pill = area.removeFromTop (px (44));
    {
        const int side = juce::jmin (s.pill.getHeight(), 44);
        s.settings = s.pill.removeFromRight (side);
        if (s.pill.getWidth() > 8)
            s.pill.removeFromRight (6);
        const int meterW = clampW (90, 180, s.pill.getWidth() * 3 / 10);
        if (s.pill.getWidth() > meterW + 120)
        {
            s.meter = s.pill.removeFromRight (meterW);
            s.pill.removeFromRight (8);
        }
    }
    s.barShift = {};
    area.removeFromTop (px (22));
    s.lane = area.removeFromTop (px (18));
    s.bpm = area.removeFromTop (bpmH);
    // The number has the whole (bounded) width; the octave buttons sit with
    // TAP on the row under the dots.
    s.bpmNumber = s.bpm.withSizeKeepingCentre (juce::jmin (s.bpm.getWidth(), 560), bpmH)
                      .reduced (8, 0);
    s.bpmLabel = area.removeFromTop (px (16));
    area.removeFromTop (px (4));
    s.beats = area.removeFromTop (beatsH);
    if (! follow)
        s.tempoNudge = area.removeFromTop (px (28));
    s.tempoLine = area.removeFromTop (px (18));
    area.removeFromTop (px (18));
    {
        // ÷2 | TAP | ×2, one 56 pt row, bounded so it does not stretch across
        // a wide column.
        auto row = area.removeFromTop (px (56));
        row = row.withSizeKeepingCentre (juce::jmin (row.getWidth(), 520), row.getHeight());
        const int octW = clampW (56, 84, row.getWidth() / 5);
        s.octaveDown = row.removeFromLeft (octW);
        s.octaveUp = row.removeFromRight (octW);
        if (row.getWidth() > 24)
            row.reduce (12, 0);
        s.tap = row;
    }
    if (trackReader != nullptr)
    {
        area.removeFromTop (px (6));
        s.trackWave = area.removeFromTop (px (trackWaveformHeight()));
    }
    s.part = area.removeFromTop (px (20));
    return s;
}

juce::Rectangle<int> MainComponent::layoutConsole (juce::Rectangle<int> area)
{
    cards.clearQuick();

    // MISURE is one row (style + squares). Leftover height goes to FEEL, whose
    // chrome is kept tight so the knobs, not the padding, take the card.
    const int gap = 10;
    const int titleH = 0;   // no card titles on the live page
    auto card = [&] (juce::Rectangle<int> bounds, const char* title,
                     int padY = 10, int titleStrip = 18)
    {
        cards.add ({ bounds, juce::String (title) });
        return bounds.reduced (12, padY).withTrimmedTop (titleStrip);
    };

    const int padY = 10;
    const int chrome = padY * 2 + titleH;
    const int btnGap = 5;
    const int innerW = juce::jmax (1, area.getWidth() - 24);

    // Style select is a compact name; leftover width goes to the squares.
    const int nMisureSq = 6;
    const int styleW = 68;
    const int misureSide = juce::jlimit (28, 52,
        (innerW - styleW - btnGap * nMisureSq) / nMisureSq);
    // Narrow console: two rows (style + track, then the pills) of 48 pt.
    const int hMisure = chrome + (innerW < 640 ? 48 * 2 + 8 : misureSide);

    {
        auto body = card (area.removeFromTop (hMisure), "", 10, 0);
        layoutMisure (body);
        area.removeFromTop (gap);
    }

    {
        // Each instrument gets its own independent level, then how loud the
        // tracker hears the room or the aux. Value sits inside the knob; the
        // name sits tight under it. Tap a voice knob to mute that part.
        // Title paint occupies y+9..y+23 of the card; pad 4 + strip 20 starts
        // the knobs 1 px under that. Labels are 11 px with a 1 px gap above.
        auto body = card (area, "", 10, 0);
        layoutVoicesRow (body);
    }

    return area;
}

void MainComponent::resized()
{
   #if JUCE_IOS
    const juce::Point<int> windowSize { getWidth(), getHeight() };
    if (windowSize != laidOutWindowSize)
    {
        laidOutWindowSize = windowSize;
        if (! a2dpResizeRecovered && audioOpened && internalTrackSelected()
            && trackTransport.isPlaying()
            && vp::sessionOutputIsA2DP())
        {
            if (resizeSettleTicks == 0)
                if (auto* device = deviceManager.getCurrentAudioDevice())
                    resizeXrunsBefore = device->getXRunCount();
            resizeSettleTicks = 6; // 0.4 s at 15 Hz, after the drag stops.
        }
    }
   #endif
    // A layout run always wins over a glide still in flight: a Split View
    // drag fires resized() every frame and must not fight the animator.
    layoutAnimator.cancelAllAnimations (false);
    for (auto& f : layoutFadeIns)
        if (f.first != nullptr)
            f.first->setAlpha (f.second);
    layoutFadeIns.clearQuick();
    updateCompactLayout();
    const int newMode = (compactLayout ? 1 : 0) | (isLandscape() ? 2 : 0);
    const bool animateMode = layoutMode >= 0 && newMode != layoutMode && isShowing()
                             && ! settingsOverlay.isVisible();
    layoutMode = newMode;
    const auto isOverlay = [this] (const juce::Component* c)
    {
        return c == &settingsOverlay || c == &styleMenu || c == &soundMenu || c == &faderZoom;
    };
    juce::Array<std::pair<juce::Component*, juce::Rectangle<int>>> before;
    if (animateMode)
        for (auto* c : getChildren())
            if (c->isVisible() && ! isOverlay (c))
                before.add ({ c, c->getBounds() });
    laidOutSafeArea = effectiveSafeArea();
    settingsOverlay.setBounds (getLocalBounds());
    if (settingsOverlay.isVisible())
        layoutSettings (settingsOverlay.getLocalBounds());

    if (isCompact())
        layoutCompact();
    else
        layoutFull();

    {
        auto full = isCompact() ? compactPadded (getLocalBounds())
                                : safePadded (getLocalBounds());
        const int strip = juce::jmin (52, juce::jmax (0, full.getHeight() / 8));
        auto row = full.removeFromBottom (strip).reduced (10, 4);
        const int side = juce::jmax (1, row.getHeight());
        // Phone width: the MIC level and gain live on the status row, so START
        // has the whole strip to itself and nothing touchable sits beside it.
        const auto meterBar = isCompact() ? compactTempoRows (compactGeom().tempo).meter
                                          : stageRows (stageArea()).meter;
        const bool gainInBar = ! meterBar.isEmpty();
        inputGainSlider.getProperties().set ("micBar", gainInBar);
        inputGainSlider.setSliderStyle (gainInBar ? juce::Slider::RotaryHorizontalDrag
                                                  : juce::Slider::RotaryVerticalDrag);
        inputGainSlider.setMouseDragSensitivity (gainInBar ? 220 : 600);
        if (gainInBar)
        {
            inputGainSlider.setBounds (meterBar);
        }
        else
        {
            inputGainSlider.setBounds (row.removeFromRight (side));
            if (row.getWidth() > 8)
                row.removeFromRight (8);
        }
        startButton.setBounds (row);
        inputGainLabel.setVisible (false);
        inputGainValue.setVisible (false);
        inputGainSlider.setVisible (true);
        startButton.toFront (false);
        inputGainSlider.toFront (false);
    }

    applyCompactVisibility();
    if (stageDim)
        setStageDim (true);   // re-fit the guard to the new geometry (drops out if no longer compact)
    layoutTrackWaveform();
    // SETUP sits over the painted stage in the compact layout, so it has to be
    // above it either way.
    settingsButton.toFront (false);
    if (styleMenu.isOpen())
        styleMenu.setBounds (getLocalBounds());
    if (soundMenu.isOpen())
        soundMenu.setBounds (getLocalBounds());

    if (animateMode)
    {
        // Controls that were on screen glide from where they were; ones that
        // just appeared fade in where they are. Ease in and out, 280 ms.
        for (auto* c : getChildren())
        {
            if (! c->isVisible() || isOverlay (c))
                continue;
            const auto target = c->getBounds();
            const auto* was = std::find_if (before.begin(), before.end(),
                                            [c] (const auto& p) { return p.first == c; });
            if (was == before.end())
            {
                const float endAlpha = c->getAlpha();
                layoutFadeIns.add ({ c, endAlpha });
                c->setAlpha (0.0f);
                layoutAnimator.animateComponent (c, target, endAlpha, (int) kLayoutAnimMs, false, 0.0, 0.0);
            }
            else if (was->second != target)
            {
                c->setBounds (was->second);
                layoutAnimator.animateComponent (c, target, c->getAlpha(), (int) kLayoutAnimMs, false, 0.0, 0.0);
            }
        }
        layoutFadeStartMs = juce::Time::getMillisecondCounterHiRes();
        layoutFading = true;
        repaint();
    }
}

float MainComponent::layoutFadeAmount() const noexcept
{
    if (! layoutFading)
        return 1.0f;
    const double p = (juce::Time::getMillisecondCounterHiRes() - layoutFadeStartMs) / kLayoutAnimMs;
    const float x = juce::jlimit (0.0f, 1.0f, static_cast<float> (p));
    return x * x * (3.0f - 2.0f * x);
}

void MainComponent::layoutFull()
{
    auto r = layoutColumn();

    // The utility row goes in a corner of the *stage*, not across the top of
    // everything: pushed to the right in landscape it lands on the console and
    // reads as part of the transport card, which is the one place a stray tap
    // does damage.
    const auto stage = stageArea();
    // SETUP rides the status row (see stageRows) rather than a row of its own.
    if (isLandscape())
        r.removeFromLeft (stage.getWidth() + 16);
    else
        r.removeFromTop (stage.getHeight() + 14);

    const auto rows = stageRows (stage);
    halveButton.setBounds (rows.octaveDown);   // the whole cell, 56 pt tall
    doubleButton.setBounds (rows.octaveUp);
    settingsButton.setBounds (rows.settings);
    barButton.setBounds ({});
    tapButton.setBounds (rows.tap);
    tapStrip = {};
    tapZone.setBounds (juce::Rectangle<int>::leftTopRightBottom (
        stage.getX(), rows.bpm.getY(), stage.getRight(), rows.beats.getBottom()));
    tapZone.setVisible (true);
    tapZone.toBack();

    {
        const bool follow = engine.settings().tempoFollow.load();
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

void MainComponent::layoutCompact()
{
    cards.clearQuick();
    const auto g = compactGeom();
    const auto rows = compactTempoRows (g.tempo);
    auto placeOctave = [] (juce::Rectangle<int> col, juce::TextButton& b)
    {
        b.setBounds (col);   // the whole cell: 56 pt tall, never the old 26-36 chip
    };
    placeOctave (rows.octaveDown, halveButton);
    placeOctave (rows.octaveUp, doubleButton);
    settingsButton.setBounds (rows.settings);
    barButton.setBounds ({});
    tapButton.setBounds (rows.tap);
    tapStrip = {};
    tapZone.setBounds (juce::Rectangle<int>::leftTopRightBottom (
        g.tempo.getX(), rows.bpm.getY(), g.tempo.getRight(), rows.beats.getBottom()));
    tapZone.setVisible (true);
    tapZone.toBack();

    auto card = [&] (juce::Rectangle<int> bounds, const char* title)
    {
        cards.add ({ bounds, juce::String (title) });
        return innerCard (bounds, 8, 6, 0);
    };

    layoutMisure (card (g.misure, ""));
    layoutVoicesRow (card (g.knobs, ""));
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
    // has seven short cards, and split in two neither side had enough to reach
    // the bottom. Turned, the same six get shorter instead of narrower: the
    // captions stop wrapping.
    const int bodyW = juce::jmax (80, r.getWidth() - 24);
    const int chrome = padY * 2 + titleH;

    // Sized against their contents, placed second - the same order the stage
    // rows are computed in. A share of the column each gave a two-line caption
    // the same room as a seven-line read-out.
    enum { kTempo = 0, kClock, kBuffer, kInput, kFeel, kTests, kStatus, kCards };
    int h[kCards] = {
        chrome + rowH,
        chrome + rowH + noteGap + noteHeight (clockNote(), bodyW),
        chrome + rowH + noteGap + noteHeight (bufferNote(), bodyW),
        chrome + rowH + (trackWaveformHeight() > 0 ? trackWaveformHeight() + 6 : 0)
            + noteGap + noteHeight (inputNote(), bodyW),
        chrome + 88,
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
        auto body = card (take (h[kTempo]), "TEMPO");
        buttonRow (body.removeFromTop (juce::jmin (rowH, body.getHeight())),
                   { &followButton, &fixedButton, &startNowButton });
    }

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
                   { &sourceButton, &trackLoadButton, &trackPlayButton,
                     &procButton, &kickButton });
        if (trackWaveformHeight() > 0)
        {
            const int waveH = juce::jmin (trackWaveformHeight(), juce::jmax (40, body.getHeight() - noteGap - 24));
            settingsRows.trackWave = body.removeFromTop (waveH).reduced (2, 4);
        }
        else
            settingsRows.trackWave = {};

        settingsRows.inputNote = body.withTrimmedTop (noteGap);
    }

    {
        auto body = card (take (h[kFeel]), "FEEL");
        const int nKnobs = 2;
        const int colW = juce::jmin (body.getWidth() / nKnobs, juce::jmax (72, body.getHeight() + 12));
        auto row = body.withSizeKeepingCentre (colW * nKnobs, body.getHeight());
        auto placeKnob = [&] (juce::Label& name, juce::Slider& s)
        {
            name.setVisible (false);
            s.setBounds (row.removeFromLeft (colW));
        };
        placeKnob (intensityLabel, intensitySlider);
        placeKnob (reverbLabel, reverbSlider);
    }

    buttonRow (card (take (h[kTests]), "PERCUSSIONI / PROVE"),
               { &loopModeButton, &themeButton, &clickButton, &latencyButton, &debugButton });

    // The numbers are the point of the page: a clock the listener chose and a
    // clock the hardware gave are not the same thing, and only one of them is
    // what the engine is running at.
    settingsRows.status = card (take (h[kStatus]), "STATO");
    layoutTrackWaveform();
}
