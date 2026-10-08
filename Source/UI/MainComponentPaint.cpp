// MainComponent: painting the stage, cards and settings page.
#include "UI/MainComponentShared.h"

void MainComponent::paintCards (juce::Graphics& g)
{
    paintCardList (g, cards);
    if (! subdivisionTrack.isEmpty())
    {
        const auto t = subdivisionTrack.toFloat();
        const float r = juce::jmin (12.0f, t.getHeight() * 0.5f);
        g.setColour (ink());
        g.fillRoundedRectangle (t, r);
        g.setColour (border());
        g.drawRoundedRectangle (t.reduced (0.5f), r, 1.0f);
    }
}

void MainComponent::paintCardList (juce::Graphics& g, const juce::Array<Card>& list)
{
    for (const auto& c : list)
    {
        g.setColour (panel());
        g.fillRoundedRectangle (c.bounds.toFloat(), 20.0f);
        g.setColour (border());
        g.drawRoundedRectangle (c.bounds.toFloat().reduced (0.5f), 20.0f, 1.0f);
        g.setColour (mute());
        g.setFont (fontUi (12.0f));
        auto titleR = innerCard (c.bounds, 14, 9, 0);
        titleR = titleR.removeFromTop (juce::jmin (14, titleR.getHeight()));
        if (! titleR.isEmpty())
            g.drawFittedText (c.title, titleR, juce::Justification::topLeft, 1);
    }
}

void MainComponent::paintStage (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto rows = stageRows (area);

    // Title, with the brand mark and the rule under it.
    if (! rows.title.isEmpty())
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
    if (! rows.pill.isEmpty())
    {
        const auto stCol = stateSmooth.getAlpha() == 0 ? stateColour (snap.followBar) : stateSmooth;
        const bool hot = stateIsHot (snap.followBar);
        const juce::String label (juce::CharPointer_UTF8 (vp::toBarString (snap.followBar)));
        const bool bigPill = rows.pill.getHeight() >= 40;
        const auto f = bigPill ? fontUi (17.0f)
                               : fontUi (rows.pill.getHeight() < 28 ? 11.0f : 12.0f, false);
        const float dotR = bigPill ? 7.0f : 4.5f;
        const float gapDot = bigPill ? 10.0f : 7.0f;
        // The word gets the rest of the row, so a longer state never moves it.
        const float x0 = static_cast<float> (rows.pill.getX());
        const float cy = static_cast<float> (rows.pill.getCentreY());
        const juce::Point<float> dot { x0 + dotR, cy };
        if (hot)
            paintRadial (g, dot, 13.0f, stCol, 0.55f);
        g.setColour (stCol);
        g.fillEllipse (dot.x - dotR, dot.y - dotR, dotR * 2.0f, dotR * 2.0f);
        auto textR = juce::Rectangle<float> (x0 + dotR * 2.0f + gapDot,
                                            static_cast<float> (rows.pill.getY()),
                                            juce::jmax (8.0f, static_cast<float> (rows.pill.getWidth()) - dotR * 2.0f - gapDot),
                                            static_cast<float> (rows.pill.getHeight()))
                         .toNearestInt();
        g.setFont (f);
        g.setColour (bigPill || hot ? text() : mute());
        g.drawFittedText (label, textR, juce::Justification::centredLeft, 1);
    }

    // The tempo, sized to the room it has rather than to a constant, so it is
    // the biggest thing on the screen in portrait and still the biggest thing
    // when the iPad is turned.
    const auto numberR = rows.bpmNumber;

    // The hero card is dark in both themes, so a light page still reads its
    // tempo the same way from the stand. Everything drawn inside it takes the
    // dark tokens until the beats are done.
    const bool lightTheme = ! gDarkMode;
    gDarkMode = true;

    // The hero: tempo, phase lane and the four quarters in one bordered card,
    // with the state colour blooming from inside it. From a metre away the
    // whole card is the reading (green / amber / red); neutral states get none.
    {
        auto hero = rows.bpm.getUnion (rows.beats);
        for (const auto& part : { rows.bpmLabel, rows.lane, rows.tempoNudge, rows.tempoLine })
            if (! part.isEmpty())
                hero = hero.getUnion (part);
        const auto card = hero.expanded (0, 10).toFloat();
        juce::Path cardPath;
        cardPath.addRoundedRectangle (card, 24.0f);
        g.setColour (panel());
        g.fillPath (cardPath);
        if (heroBloomAmt > 0.01f)
        {
            // After declaring the one the bloom flashes fuchsia, then settles
            // into whatever the tracker really thinks.
            const auto bloom = tapAlignFlash > 0.02f
                                   ? stateSmooth.interpolatedWith (fuchsia(), tapAlignFlash * 0.85f)
                                   : stateSmooth;
            const float big = juce::jmax (card.getWidth(), card.getHeight());
            g.saveState();
            g.reduceClipRegion (cardPath);
            paintRadial (g, { card.getCentreX(), card.getY() + card.getHeight() * 0.36f },
                         big * 0.80f, bloom, 0.46f * heroBloomAmt);
            paintRadial (g, numberR.getCentre().toFloat(), big * 0.42f, bloom, 0.30f * heroBloomAmt);
            g.restoreState();
        }
        g.setColour (border());
        g.drawRoundedRectangle (card.reduced (0.5f), 24.0f, 1.0f);
    }

    const bool haveBpm = snap.bpm > 40.0f;
    if (haveBpm)
    {
        // Sized against the width by measurement, not by drawFittedText: with
        // one line to work with that squashes to 70% and then puts an ellipsis
        // in, so a five-character tempo in a narrow landscape column came out
        // as "11...". Measure, scale, draw.
        const juce::String bpmText (snap.bpm, 1);
        const float wanted = static_cast<float> (numberR.getHeight()) * 1.0f;
        juce::Font f = fontDisplay (wanted);
        const float textW = juce::GlyphArrangement::getStringWidth (f, bpmText);
        const float roomW = static_cast<float> (numberR.getWidth());
        if (textW > roomW && textW > 1.0f)
            f = f.withHeight (juce::jmax (1.0f, wanted * roomW / textW));

        g.setFont (f);
        // The state colour is the first thing read from a metre away: a halo
        // behind the tempo in green / amber / red, the digits dimmed when the
        // tracker has lost the tempo. Neutral (ready, paused) draws no halo.
        const auto heroCol = stateSmooth.getAlpha() == 0 ? stateColour (snap.followBar) : stateSmooth;
        const bool heroLost = stateColour (snap.followBar) == stateLost();
        // The offset copy behind the digits glows on the dark card.
        g.setColour (heroCol.withAlpha (0.40f));
        g.drawText (bpmText, numberR.translated (0, 3),
                    juce::Justification::centred, false);
        // Faux extra weight: the same digits nudged a hair each way.
        g.setColour (text().withAlpha (heroLost ? 0.35f : 1.0f));
        const float fw = juce::jmax (0.6f, f.getHeight() * 0.014f);
        const auto nr = numberR.toFloat();
        for (const auto& d : { juce::Point<float> (-fw, 0.0f), juce::Point<float> (fw, 0.0f),
                               juce::Point<float> (0.0f, -fw * 0.6f), juce::Point<float> (0.0f, fw * 0.6f),
                               juce::Point<float> (0.0f, 0.0f) })
            g.drawText (bpmText, nr.translated (d.x, d.y), juce::Justification::centred, false);
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
        const float cx = static_cast<float> (numberR.getCentreX());
        const float cy = static_cast<float> (numberR.getCentreY());
        g.setColour (text().withAlpha (0.16f));
        g.fillRoundedRectangle (cx - barW - gap * 0.5f, cy - barH * 0.5f, barW, barH, barH * 0.5f);
        g.fillRoundedRectangle (cx + gap * 0.5f, cy - barH * 0.5f, barW, barH, barH * 0.5f);
    }

    // Above the digits, from ÷2 across to ×2. On the pulse it sits in
    // the middle and burns green; ahead of the clock it slides right,
    // behind it slides left, and the colour follows the distance.
    if (tempoBloomAmount > 0.02f && ! rows.lane.isEmpty())
    {
        // Inside the hero card, under the BPM label.
        const auto laneR = rows.lane.reduced (rows.lane.getWidth() / 12, 0);
        paintTempoOrb (g,
                       { static_cast<float> (laneR.getX()),
                         static_cast<float> (laneR.getCentreY()) - 7.0f,
                         static_cast<float> (laneR.getWidth()), 14.0f },
                       tempoBloomLead, 0.85f * tempoBloomAmount);
    }

    if (! rows.bpmLabel.isEmpty())
    {
        g.setColour (mute());
        g.setFont (fontUi (12.0f));
        g.drawFittedText ("BPM", rows.bpmLabel, juce::Justification::centred, 1);
    }

    // How the tempo is being held. SEGUI shows what the analysis thinks;
    // FISSO is the listener's lock, so the analysis label would only confuse.
    if (! rows.tempoLine.isEmpty())
    {
        const bool userFixed = ! snap.tempoFollow;
        const bool held = userFixed || snap.tempoRegime == 1;
        g.setColour (held ? text() : mute());
        g.setFont (fontUi (12.0f));
        // Notes only when there is one; otherwise the line says what a tap on
        // the number does. The state itself is the pill's job, not this line's.
        juce::StringArray parts;
        if (userFixed)
            parts.add ("TEMPO FISSO");
        else if (! snap.levelSettled)
            parts.add ("livello provvisorio");
        if (snap.tempoOctave != 0)
            parts.add (juce::String (juce::CharPointer_UTF8 (snap.tempoOctave < 0 ? "a met\xc3\xa0"
                                                                                  : "doppio"))
                       + (snap.tempoOctaveAuto ? " (auto)" : " (manuale)"));
        if (! parts.isEmpty())
        {
            g.setColour (text());
            g.drawFittedText (parts.joinIntoString (juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "))),
                              rows.tempoLine, juce::Justification::centred, 1);
        }
    }

    // Four beats, the one marked. Big enough to read at arm's length on a
    // stand, and lit from the clock rather than from whether a sample happens
    // to be sounding: a player watching the bar wants to see it turn over
    // before START, not after.
    beatStrip = rows.beats;
    if (! rows.beats.isEmpty())
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
        const int beatIdx = juce::jlimit (0, 3, dotBeat);
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
                paintRadial (g, { x, y }, rr * 3.4f, fuchsia(), 0.60f);
                g.setColour (fuchsia());
                g.fillEllipse (x - rr, y - rr, rr * 2.0f, rr * 2.0f);
                g.setColour (juce::Colours::white);
                g.setFont (fontUi (rr * 1.25f, true));
                g.drawText (juce::String (i + 1),
                            juce::Rectangle<float> (x - rr, y - rr, rr * 2.0f, rr * 2.0f),
                            juce::Justification::centred, false);
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

    gDarkMode = ! lightTheme;

    // Which part is playing, and under AUTO how sure the detector is.
    if (! rows.part.isEmpty())
    {
        g.setColour (mute());
        g.setFont (fontUi (12.0f));
        g.drawFittedText (juce::String ("PARTE  ")
                              + vp::toString (static_cast<vp::GrooveStyle> (snap.grooveStyle))
                              + (engine.settings().grooveAuto.load()
                                     ? "   (auto " + juce::String (snap.grooveStyleConfidence, 2) + ")"
                                     : juce::String()),
                          rows.part, juce::Justification::centred, 1);
    }

    if (tapFlash > 0 && ! tapStrip.isEmpty())
    {
        g.setColour (fuchsia().withAlpha (gDarkMode ? 0.32f : 0.20f));
        g.fillRoundedRectangle (tapStrip.toFloat(), 14.0f);
        g.setColour (fuchsia().withAlpha (0.70f));
        g.drawRoundedRectangle (tapStrip.toFloat().reduced (0.5f), 14.0f, 2.4f);
    }
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
        g.drawFittedText (juce::String (juce::CharPointer_UTF8 ("VIRTUAL PERCUSSIONIST  \xc2\xb7  IMPOSTAZIONI")),
                          titleR, juce::Justification::centredLeft, 1);
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
        juce::String partStatus;
        if (! loopBankReady)
            partStatus = "PATTERN  (banco non disponibile)";
        else if (! engine.recordedLoopsEnabled())
            partStatus = "PATTERN";
        else if (snap.sampleRate > 1000.0 && std::fabs (snap.sampleRate - 48000.0) >= 1.0)
            partStatus = "LOOP  (serve 48 kHz)";
        else if (engine.settings().swing.load (std::memory_order_relaxed) > 0.18f)
            // SWING is a switch now, so this is not a corner any more: on, it is
            // always past what the bank has takes for (`LoopBank::swingTolerance`),
            // and the part falls back to the stroke engine rather than going
            // quiet. Say so - a listener who turns SWING on with LOOP selected is
            // owed the reason the recordings stopped. See docs/TODO.md item 7.
            partStatus = "PATTERN  (SWING: il banco non ha prese swingate)";
        else if (snap.bpm > 1.0f && (snap.bpm < 98.0f || snap.bpm > 155.0f))
            partStatus = "LOOP  (BPM fuori banco)";
        else
            partStatus = snap.loopPlaying ? "LOOP  (in riproduzione)"
                                          : "LOOP  (attesa clock)";
        lines.add ("parte      " + partStatus);
        // A rig that needs this is a rig with a problem the app is papering
        // over, so the number is on the page rather than in a log nobody reads.
        lines.add (juce::String ("riavvii    ") + juce::String (deviceRebuilds)
                   + (deviceRebuilds > 0 ? "   " + lastRebuildWhy : ""));

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

    // Eased and stepped on the timer, so a steady band does not force a
    // full-window repaint every tick.
    const float energy = static_cast<float> (washStep) / 8.0f;
    const float follow = stateIsHot (snap.followBar) ? 1.0f : 0.42f;
    const float wash = 0.12f + 0.20f * energy * follow;

    juce::ColourGradient floor (gDarkMode ? juce::Colour (0xff0a0a0c) : juce::Colour (0xffffffff),
                                full.getCentreX(), full.getY(),
                                bg(), full.getCentreX(), full.getBottom(), false);
    g.setGradientFill (floor);
    g.fillRect (full);

    const auto stage = stageArea();
    // The page tint follows the tracker's state (green / amber / red), not the
    // brand: fuchsia is reserved for the downbeat. Fuchsia only returns under a
    // tap, as the flash.
    paintRadial (g, { stage.toFloat().getCentreX(), full.getY() + 28.0f },
                 full.getWidth() * 0.60f,
                 stateSmooth.getAlpha() == 0 ? stateColour (snap.followBar) : stateSmooth, wash * 0.45f * (gDarkMode ? 1.0f : 0.6f));
    if (tapFlash > 0)
        paintRadial (g, { full.getCentreX(), full.getBottom() - 80.0f },
                     full.getWidth() * 0.45f, fuchsia(), 0.22f);

    paintCards (g);
    paintStage (g, stage);

    if (debugOpen)
    {
        auto dbg = getLocalBounds().reduced (24).removeFromTop (330);
        g.setColour (panel().withAlpha (0.97f));
        g.fillRoundedRectangle (dbg.toFloat(), 12.0f);
        g.setColour (text().withAlpha (0.12f));
        g.drawRoundedRectangle (dbg.toFloat().reduced (0.5f), 12.0f, 1.0f);
        g.setColour (text());
        g.setFont (fontUi (12.0f, false));
        juce::StringArray lines;
        lines.add ("DEBUG");
        lines.add (juce::String (snap.aiOnnx ? "AI ONNX BeatNet" : "AI STUB (modello NON caricato)"));
        lines.add (juce::String (snap.source == vp::FollowSource::internalPlayer
                                     ? "source BRANO/FILE"
                                     : (snap.source == vp::FollowSource::speaker
                                            ? "source IPAD/SPEAKER" : "source MIXER")));
        lines.add ("BPM " + juce::String (snap.bpm, 2) + "  nn " + juce::String (snap.neuralBpm, 2)
                   + "  target " + juce::String (snap.targetBpm, 2)
                   + "  clock " + juce::String (snap.clockBpm, 2));
        lines.add ("tempo " + juce::String (vp::regimeLabel (snap.tempoRegime))
                   + "  livello " + juce::String (snap.levelSettled ? "deciso" : "provvisorio")
                   + "  fold " + (snap.combBpm > 1.0f ? juce::String (snap.combBpm, 1)
                                                      : juce::String ("--"))
                   + "  ottava " + juce::String (snap.tempoOctave));
        lines.add ("pBeat " + juce::String (snap.pBeat, 3) + "  valid " + juce::String (snap.hypValid ? 1 : 0)
                   + "  conf " + juce::String (snap.confidence, 3));
        lines.add ("beat " + juce::String (snap.beatPhase, 3) + "  bar " + juce::String (snap.barPhase, 3)
                   + "  rot " + juce::String (snap.barRotations));
        // How well the analysis is fitting against how well it has been fitting
        // this song, and the constant the clock is therefore averaging its
        // phase over. Below one is a passage the fit is finding hard - the
        // drummer out, usually - and the two numbers together say whether the
        // app has noticed something the listener can hear.
        lines.add ("armonia  cambi " + juce::String (snap.harmonicChanges)
                   + "  margine " + juce::String (snap.harmonyMargin, 2)
                   + "  tonale " + juce::String (snap.harmonicShare, 2)
                   + (snap.barFromHarmony ? "   BATTUTA DALL'ARMONIA" : ""));
        lines.add (juce::String ("dinamica ") + (snap.dynamicsFollow ? "ON " : "OFF")
                   + "  banda " + juce::String (snap.bandDynamics, 2)
                   + (snap.standingDown ? "  IN ASCOLTO" : ""));
        lines.add ("latenza  dispositivo " + juce::String (snap.latencyMs, 1)
                   + " ms   misurata " + (engine.measuredLatency() > 0.0f
                                              ? juce::String (engine.measuredLatency(), 1) + " ms"
                                              : juce::String ("--")));
        lines.add ("cassa " + (snap.kickChannel < 0
                                  ? juce::String ("--")
                                  : juce::String (snap.kickChannel + 1))
                   + "  liv " + juce::String (snap.kickLevel, 4)
                   + "  muto " + juce::String (snap.kickQuietSec, 2) + " s"
                   + "  colpi " + juce::String (snap.kickOnsets)
                   + (snap.kickTrusted ? "  CREDUTO" : "")
                   + (snap.drumsOut ? "  KIT FUORI" : ""));
        lines.add ("prove " + juce::String (snap.evidenceTrust, 2)
                   + "  tau " + juce::String (snap.gridTauSec, 2) + " s"
                   + "  fit " + juce::String (snap.fitResidual, 3)
                   + "/" + juce::String (snap.fitCoverage, 2)
                   + "  cambio "
                   + juce::String (static_cast<int> (snap.tempoTransitionState))
                   + "/" + juce::String (static_cast<int> (snap.tempoTransitionReason))
                   + " " + juce::String (snap.tempoTransitionBpm, 1)
                   + "@" + juce::String (snap.tempoTransitionConfidence, 2)
                   + " x" + juce::String (snap.tempoTransitionIntervals));
        lines.add ("state " + juce::String (vp::toString (snap.state)));
        lines.add ("callback " + juce::String (snap.callbackMs, 2) + " ms  lead "
                   + juce::String (snap.leadMs, 1) + " ms  rete "
                   + juce::String (engine.analysisInferMs(), 2) + " ms "
                   + (engine.analysisUsesCoreMl() ? "CoreML" : "CPU")
                   + "  analisi " + juce::String (engine.analysisLoadPercent(), 1) + "% core");
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
        g.drawFittedText (lines.joinIntoString ("\n"), dbg.reduced (16), juce::Justification::topLeft, 22);
    }

    // The painted cards fade in after a layout change while the controls glide
    // (see resized()): a veil of the page colour, lifted over 280 ms.
    if (layoutFading)
    {
        const float k = layoutFadeAmount();
        if (k < 1.0f)
            g.fillAll (bg().withAlpha (1.0f - k));
    }
}
