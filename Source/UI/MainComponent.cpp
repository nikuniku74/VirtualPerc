// MainComponent: state, audio, prefs and the timer.
#include "UI/MainComponentShared.h"

MainComponent::MainComponent()
{
    trackFormats.registerBasicFormats();
    trackReadThread.startThread (juce::Thread::Priority::normal);

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

    addChildComponent (tapZone);
    tapZone.setVisible (false);

    auto setupBtn = [this] (juce::TextButton& b, juce::Colour fill)
    {
        addAndMakeVisible (b);
        b.setColour (juce::TextButton::buttonColourId, fill);
        b.setColour (juce::TextButton::textColourOffId, text());
        b.setColour (juce::TextButton::buttonOnColourId, fill.brighter (0.18f));
    };

    setupBtn (startButton, ink());
    setupBtn (stopButton, ink());
    setupBtn (bpmNudgeDown, ink());
    setupBtn (bpmNudgeUp, ink());
    setupBtn (settingsButton, juce::Colour (0xff0a0a0c));
    settingsButton.setButtonText ({});
    settingsButton.getProperties().set ("gearIcon", true);
    setupBtn (naturalButton, ink());
    setupBtn (swingButton, ink());
    setupBtn (editSoundsButton, ink());
    editSoundsButton.setButtonText ({});
    editSoundsButton.setTitle ("Modifica suoni");
    editSoundsButton.getProperties().set ("pencilIcon", true);
    editSoundsButton.onClick = [this] { vp::haptic (vp::Haptic::select); setSoundEditMode (! soundEditMode); };
    setupBtn (dynamicsButton, ink());
    setupBtn (subAuto, ink());
    setupBtn (barButton, ink());
    barButton.setVisible (false);
    setupBtn (tapButton, ink());
    setupBtn (halveButton, ink());
    setupBtn (doubleButton, ink());

    addAndMakeVisible (styleSelect);
    addChildComponent (styleMenu);
    addChildComponent (soundMenu);

    // Pressing the level you are already on is the way back to AUTO: the same
    // idiom the bar button used to use, and the only way out that does not need
    // a second control. See applyTempoOctave.
    halveButton.onClick = [this]
    {
        vp::haptic (vp::Haptic::select);
        const bool mine = ! engine.settings().tempoOctaveAuto.load()
                          && engine.settings().tempoOctave.load() < 0;
        if (mine) applyTempoOctaveAuto();
        else      applyTempoOctave (vp::stepTempoOctave (
                      engine.snapshot().tempoOctave, -1));
    };
    doubleButton.onClick = [this]
    {
        vp::haptic (vp::Haptic::select);
        const bool mine = ! engine.settings().tempoOctaveAuto.load()
                          && engine.settings().tempoOctave.load() > 0;
        if (mine) applyTempoOctaveAuto();
        else      applyTempoOctave (vp::stepTempoOctave (
                      engine.snapshot().tempoOctave, 1));
    };

    // Where beat one is cannot be read reliably from what the network gives us,
    // so this moves it on by one and locks it. Tapping again while it is lit
    // hands the count back without rotating: the old five-tap unlock read as a
    // button stuck on. A TAP that declares the one still locks via the tracker.
    // See docs/TODO.md item 13. The button has one function, not two: it
    // The button "L'1 è QUI": the nearest beat is the one. The bar locks.
    // The phase is not snapped — that shortened the beat and was heard as
    // the tempo jumping. It is not a nudge and not a toggle.
    tapButton.onClick = [this] { vp::haptic (vp::Haptic::medium); tapPressed(); };
    setupBtn (sub4, ink());
    setupBtn (sub8, ink());
    setupBtn (sub16, ink());

    // One press, on touch-down, both ways: START starts and STOP stops at
    // once (the user's call, 2026-10-08: the 0.5 s hold-to-stop is gone).
    startButton.onClick = [this]
    {
        if (userWantsArmed)
            stopPressed();
        else
            startPressed();
    };
    startButton.setTriggeredOnMouseDown (true);
    // Shared looks: see buttonStyle() in the look-and-feel.
    sub4.getProperties().set ("btnStyle", 1);
    sub8.getProperties().set ("btnStyle", 1);
    sub16.getProperties().set ("btnStyle", 1);
    dynamicsButton.getProperties().set ("btnStyle", 2);
    naturalButton.getProperties().set ("btnStyle", 2);
    swingButton.getProperties().set ("btnStyle", 2);
    startButton.getProperties().set ("btnStyle", 3);
    stopButton.setVisible (false);
    followButton.onClick = [this] { applyTempoFollow (true); };
    fixedButton.onClick = [this] { applyTempoFollow (false); };
    startNowButton.onClick = [this]
    {
        applyStartImmediately (! engine.settings().startImmediately.load());
    };
    bpmNudgeDown.onClick = [this] { nudgeFixedBpm (-1.0f); };
    bpmNudgeUp.onClick = [this] { nudgeFixedBpm (1.0f); };
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
    // A second of sweep and the rig has told the app what it actually does.
    // Pressing it again clears a measurement, so a figure taken on last week's
    // rig can be got rid of without hunting for a reset.
    latencyButton.onClick = [this]
    {
        if (engine.measuredLatency() > 0.0f)
        {
            engine.setMeasuredLatency (0.0f);
            savePrefs (true);
            refreshSourceButton();
            return;
        }
        engine.startLatencyMeasurement();
        latencyButton.setButtonText ("MISURO...");
    };

    // Which input the kick is on, cycled rather than typed: on a stage this is
    // set once with the desk in front of you, and a spinner is one more thing
    // to get wrong in the dark. NO is the default and costs nothing - the whole
    // kick path is switched off until an input is named.
    kickButton.onClick = [this]
    {
        const int shown = engine.settings().kickChannel.load() + 1;   // 0 = none
        const int most = juce::jmax (2, inputChannels);
        int next = shown + 1;
        if (next < 2)
            next = 2;                       // channel 1 is the mix; the kick is never there
        if (next > most)
            next = 0;
        engine.settings().kickChannel.store (next - 1);
        // Opening more inputs is what makes a channel past the second reachable
        // at all, so the device has to be told.
        applyAudioSetup (true);
        savePrefs (true);
        refreshSourceButton();
    };

    sourceButton.onClick = [this]
    {
        const auto source = static_cast<vp::FollowSource> (
            engine.settings().followSource.load());
        const auto next = source == vp::FollowSource::kitMic
                              ? vp::FollowSource::speaker
                              : (source == vp::FollowSource::speaker
                                     ? vp::FollowSource::internalPlayer
                                     : vp::FollowSource::kitMic);
        selectFollowSource (next);
    };

    naturalButton.onClick = [this]
    {
        vp::haptic (vp::Haptic::select);
        applyShakerNatural (! engine.settings().shakerNatural.load());
    };

    swingButton.onClick = [this]
    {
        vp::haptic (vp::Haptic::select);
        applySwing (engine.settings().swing.load() <= 0.5f);
    };

    // Not in SETUP: this one is a musical choice and belongs next to the other
    // musical choices, where it can be reached mid-song.
    dynamicsButton.onClick = [this]
    {
        vp::haptic (vp::Haptic::select);
        auto& v = engine.settings().dynamicsFollow;
        v.store (! v.load());
        savePrefs (true);
        refreshStyleButtons();
    };

    subAuto.setVisible (false);
    sub4.onClick    = [this] { vp::haptic (vp::Haptic::select); applySubdivision (vp::Subdivision::quarter); };
    sub8.onClick    = [this] { vp::haptic (vp::Haptic::select); applySubdivision (vp::Subdivision::eighth); };
    sub16.onClick   = [this] { vp::haptic (vp::Haptic::select); applySubdivision (vp::Subdivision::sixteenth); };

    auto setupFader = [this] (juce::Slider& s, juce::Label& name, juce::Label& value,
                              const char* title, double minV, double maxV, double initial,
                              std::function<void (float)> apply,
                              std::function<juce::String (float)> fmt = {})
    {
        if (! fmt)
            fmt = [] (float v)
            {
                return juce::String (juce::roundToInt (static_cast<double> (v) * 100.0)) + "%";
            };
        addAndMakeVisible (name);
        name.setText (title, juce::dontSendNotification);
        name.setJustificationType (juce::Justification::centred);
        name.setColour (juce::Label::textColourId, mute());
        name.setFont (fontUi (10.0f));
        name.setInterceptsMouseClicks (false, false);

        addAndMakeVisible (value);
        value.setVisible (false);
        value.setJustificationType (juce::Justification::centred);
        value.setColour (juce::Label::textColourId, fuchsia());
        value.setFont (fontUi (13.0f));
        value.setInterceptsMouseClicks (false, false);
        value.setText (fmt (static_cast<float> (initial)), juce::dontSendNotification);

        addAndMakeVisible (s);
        s.getProperties().set ("knobName", juce::String (title));
        // Vertical drag, not circular: the old faders were up/down, and a
        // finger cannot describe an arc on a 50-point disc.
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                               juce::MathConstants<float>::pi * 2.8f, true);
        s.setMouseDragSensitivity (180);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (minV, maxV, 0.01);
        s.setValue (initial, juce::dontSendNotification);
        s.setDoubleClickReturnValue (false, 0.0);
        auto* sp = &s;
        auto* valueLab = &value;
        s.onValueChange = [this, sp, valueLab, apply, fmt]
        {
            const float v = static_cast<float> (sp->getValue());
            apply (v);
            valueLab->setText (fmt (v), juce::dontSendNotification);
            savePrefs (false);
        };
        s.onDragEnd = [this] { savePrefs(); };
    };

    setupFader (shakerVolSlider, shakerVolLabel, shakerVolValue, "SHAKER",
                0.0, 1.0, 1.00,
                [this] (float v) { engine.settings().shakerVolume.store (v); });
    setupFader (congaVolSlider, congaVolLabel, congaVolValue, "CONGAS",
                0.0, 1.0, 1.00,
                [this] (float v) { engine.settings().congaVolume.store (v); });
    setupFader (cembaloVolSlider, cembaloVolLabel, cembaloVolValue, "CEMBALO",
                0.0, 1.0, 1.00,
                [this] (float v) { engine.settings().cembaloVolume.store (v); });
    setupFader (clapVolSlider, clapVolLabel, clapVolValue, "CLAP",
                0.0, 1.0, 1.00,
                [this] (float v) { engine.settings().clapVolume.store (v); });
    auto tapVoice = [this] (std::atomic<bool>& flag)
    {
        // Arming a voice, triangle included, is a flag. The bank is already
        // at the device rate. Opening another client here is what drops a
        // Bluetooth route.
        flag.store (! flag.load());
        refreshVoiceKnobs();
        savePrefs();
    };
    // With EDIT on, a tap on a knob is "change this one" (the sound modal),
    // not the mute or the one-shot. Drag is still the level either way.
    auto tapOrEdit = [this] (int slot, bool hits, std::function<void()> act)
    {
        return [this, slot, hits, act = std::move (act)]
        {
            vp::haptic (vp::Haptic::light);
            if (! soundEditMode)
            {
                act();
                return;
            }
            styleMenu.dismiss();
            soundMenu.showFor (slot, hits);
        };
    };
    shakerVolSlider.onTap  = tapOrEdit (0, false, [this, tapVoice] { tapVoice (engine.settings().shakerEnabled); });
    congaVolSlider.onTap   = tapOrEdit (1, false, [this, tapVoice] { tapVoice (engine.settings().congasEnabled); });
    cembaloVolSlider.onTap = tapOrEdit (2, false, [this, tapVoice] { tapVoice (engine.settings().cembaloEnabled); });
    clapVolSlider.onTap    = tapOrEdit (3, false, [this, tapVoice] { tapVoice (engine.settings().clapEnabled); });
    setupFader (inputGainSlider, inputGainLabel, inputGainValue, "MIC",
                0.0, 4.0, 1.00,
                [this] (float v) { engine.settings().inputGain.store (v); },
                micGainText);
    // A trim is read in dB and turned in dB. It was linear in amplitude over
    // 0..2 across 180 points of drag, which is uneven in the units that matter:
    // one point of finger travel moved 0.10 dB at unity, 0.92 dB at -20 dBFS
    // and 2.13 dB at -28 dBFS. The quiet end - exactly where a player is
    // hunting, because that is where the level is too low - was the twitchiest
    // part of the control.
    //
    // Unity at half travel over 0..4 makes the value the square of the travel,
    // which is close enough to dB over the range that matters, and 600 points
    // of drag is a deliberate full-screen gesture: 0.06 dB per point at unity,
    // 0.18 at -20, 0.28 at -28. Between 1.6 and 7.6 times steadier, and the
    // steadiest gain is where the old one was worst. This is the one control a
    // player sets once, carefully, on a stage.
    //
    // The top is +12 dB rather than +6 because the point of the trim is to
    // reach the band the meter draws, and a stage feed sitting at -30 dBFS
    // could not get there with six. The engine already clamps to 4.
    inputGainSlider.setSkewFactorFromMidPoint (1.0);
    inputGainSlider.setMouseDragSensitivity (600);
    inputGainSlider.getProperties().set ("micMeter", true);
    // Unity is 1.0, which the disc draws as 100%. The other knobs stay where
    // a double tap left them.
    inputGainSlider.setDoubleClickReturnValue (true, 1.0);

    setupFader (absorbVolSlider, absorbHitLabel, absorbHitValue, "ABSORB",
                0.0, 1.0, 1.00,
                [this] (float v) { hitVoices[0].gain.store (v, std::memory_order_relaxed); });
    setupFader (hornVolSlider, hornHitLabel, hornHitValue, "HORN",
                0.0, 1.0, 1.00,
                [this] (float v) { hitVoices[1].gain.store (v, std::memory_order_relaxed); });
    setupFader (uplifterVolSlider, uplifterHitLabel, uplifterHitValue, "UPLIFTER FX",
                0.0, 1.0, 1.00,
                [this] (float v) { hitVoices[2].gain.store (v, std::memory_order_relaxed); });
    setupFader (riserVolSlider, riserHitLabel, riserHitValue, "RISER 2",
                0.0, 1.0, 1.00,
                [this] (float v) { hitVoices[3].gain.store (v, std::memory_order_relaxed); });
    absorbVolSlider.getProperties().set ("voiceOnFill", true);
    absorbVolSlider.getProperties().set ("hitLit", false);
    hornVolSlider.getProperties().set ("voiceOnFill", true);
    hornVolSlider.getProperties().set ("hitLit", false);
    uplifterVolSlider.getProperties().set ("voiceOnFill", true);
    uplifterVolSlider.getProperties().set ("hitLit", false);
    riserVolSlider.getProperties().set ("voiceOnFill", true);
    riserVolSlider.getProperties().set ("hitLit", false);
    for (int i = 0; i < 4; ++i)
        hitVoices[i].selected.store (i, std::memory_order_relaxed);
    refreshHitKnobs();
    auto fireHit = [this] (int i)
    {
        return [this, i] { hitVoices[i].request.fetch_add (1, std::memory_order_release); };
    };
    addChildComponent (faderZoom);
    for (auto* k : { &shakerVolSlider, &congaVolSlider, &cembaloVolSlider, &clapVolSlider,
                     &absorbVolSlider, &hornVolSlider, &uplifterVolSlider, &riserVolSlider })
    {
        k->setMouseDragSensitivity (380);
        k->onZoom = [this, k] (bool on)
        {
            if (on) faderZoom.show (*k);
            else    faderZoom.hide();
        };
    }
    inputGainSlider.onZoom = [this] (bool on)
    {
        if (on) faderZoom.show (inputGainSlider);
        else    faderZoom.hide();
    };
    absorbVolSlider.onTap = tapOrEdit (0, true, fireHit (0));
    hornVolSlider.onTap = tapOrEdit (1, true, fireHit (1));
    uplifterVolSlider.onTap = tapOrEdit (2, true, fireHit (2));
    riserVolSlider.onTap = tapOrEdit (3, true, fireHit (3));
    setupFader (intensitySlider, intensityLabel, intensityValue, "ENERGIA",
                0.0, 1.0, 0.50,
                [this] (float v) { engine.settings().intensity.store (v); });
    setupFader (reverbSlider, reverbLabel, reverbValue, "REVERB",
                0.0, 1.0, 0.30,
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
    settingsOverlay.addAndMakeVisible (intensitySlider);
    settingsOverlay.addAndMakeVisible (intensityLabel);
    settingsOverlay.addAndMakeVisible (reverbSlider);
    settingsOverlay.addAndMakeVisible (reverbLabel);

    auto setupPageBtn = [this] (juce::TextButton& b, juce::Colour fill)
    {
        settingsOverlay.addAndMakeVisible (b);
        b.setColour (juce::TextButton::buttonColourId, fill);
        b.setColour (juce::TextButton::textColourOffId, text());
        b.setColour (juce::TextButton::buttonOnColourId, fill.brighter (0.18f));
    };

    setupPageBtn (followButton, ink());
    setupPageBtn (fixedButton, ink());
    setupPageBtn (startNowButton, ink());
    setupPageBtn (settingsClose, ink());
    setupPageBtn (debugButton, juce::Colour (0xff0a0a0c));
    setupPageBtn (clickButton, juce::Colour (0xff0a0a0c));
    setupPageBtn (themeButton, ink());
    setupPageBtn (sourceButton, ink());
    setupPageBtn (trackLoadButton, ink());
    setupPageBtn (trackPlayButton, ink());
    setupPageBtn (kickButton, ink());
    setupPageBtn (latencyButton, ink());
    setupPageBtn (procButton, ink());
    setupPageBtn (loopModeButton, ink());
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
    loopModeButton.onClick = [this]
    {
        if (! loopBankReady)
            return;
        const bool enable = ! engine.recordedLoopsEnabled();
        if (enable)
        {
            // The bundled recordings and their markers are 48 kHz. A 256-frame
            // callback leaves safe headroom for both stereo loop stretchers on
            // iPad; 64/128 remain expert low-latency choices for PATTERN.
            clockHz = 48000;
            if (bufferChoice == 0 || bufferChoice < 256)
                bufferChoice = 256;
            refreshClockButtons();
            refreshBufferButtons();
        }
        engine.setRecordedLoopsEnabled (enable);
        refreshLoopModeButton();
        savePrefs (true);
        if (enable && audioOpened)
            applyAudioSetup (false);
        repaint();
    };
    trackLoadButton.onClick = [this] { chooseInternalTrack(); };
    trackPlayButton.onClick = [this] { toggleInternalTrack(); };

    // Above the settings overlay when a track is loaded: main-stage scrub bar, or
    // the INGRESSO card when SETUP is open.
    addAndMakeVisible (trackWaveform);
    trackWaveform.setVisible (false);

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
    engine.settings().shakerVolume.store (1.00f);
    engine.settings().congaVolume.store (1.00f);
    engine.settings().cembaloVolume.store (1.00f);
    engine.settings().clapVolume.store (1.00f);
    engine.settings().followStrength.store (static_cast<int> (vp::FollowStrength::high));
    engine.settings().subdivision.store (static_cast<int> (vp::Subdivision::eighth));
    engine.settings().reverbAmount.store (0.30f);

    // No disk paths on iPad: the manifest and its WAVs are part of the app and
    // are decoded before the audio device is opened. A failed bank leaves the
    // old pattern engine intact and the settings switch disabled.
    loopBankReady = loadBundledLoopBank();

    // Before the device is opened: the stored clock is what it has to be opened
    // at, and asking for it after the fact is the reconfiguration this change
    // exists to remove.
    loadPrefs();

    refreshStartButton();
    refreshStyleButtons();
    refreshSubdivisionButtons();
    refreshNaturalButton();
    refreshStartNowButton();
    refreshSwingButton();
    refreshOctaveButtons();
    refreshLoopModeButton();
    refreshThemeColours();

    startTimerHz (15);
    beatVBlank = juce::VBlankAttachment (this, [this] { updateBeatDots(); });

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
    trackTransport.stop();
    trackTransport.setSource (nullptr);
    trackReader.reset();
    trackReadThread.stopThread (2000);
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
    setLookAndFeel (nullptr);
    stopTimer();
    shutdownAudio();
}

void MainComponent::powerDownAudioForBackground()
{
    if (audioPoweredDownForBackground)
        return;

    if (audioOpened)
    {
        // closeAudioDevice() first proves that the real-time producer has stopped;
        // joining the worker is then safe on this lifecycle/message thread.
        // Without the explicit stop it continues waking against an empty FIFO;
        // the real-time worker bench measured 110 loop passes per second.
        deviceManager.closeAudioDevice();
        engine.suspendAnalysis();
    }
    audioPoweredDownForBackground = true;
    stopTimer();
}

void MainComponent::handleAppSuspended()
{
    appIsSuspended = true;

    // BACKGROUND_AUDIO_ENABLED is intentional for a live performance. It must
    // not, however, turn STOP into permanent microphone + inference activity.
    if (! userWantsArmed && ! trackTransport.isPlaying())
        powerDownAudioForBackground();
}

void MainComponent::handleAppResumed()
{
    appIsSuspended = false;

    if (audioPoweredDownForBackground)
    {
        audioPoweredDownForBackground = false;
        vp::prepareAudioSession ({ requestedSampleRate(), requestedBufferFrames(),
                                   inputProcessing, true, ! internalTrackSelected() });
        deviceManager.restartLastAudioDevice();

        if (deviceManager.getCurrentAudioDevice() == nullptr)
        {
            audioOpened = false;
            openAudioDevice (micGranted);
        }
        else
        {
            applyInputProcessing();
        }
    }

    startTimerHz (15);
    repaint();
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
        &bpmNudgeDown, &bpmNudgeUp, &debugButton,
        &clickButton, &themeButton, &sourceButton, &trackLoadButton,
        &trackPlayButton, &kickButton, &latencyButton,
        &subAuto, &sub4, &sub8,
        &sub16, &naturalButton, &swingButton,
        &dynamicsButton, &halveButton, &doubleButton,
        &barButton, &tapButton,
        &settingsButton, &settingsClose, &procButton,
        &loopModeButton,
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
    // EDIT has its own text colours. Without this it kept the ones from
    // construction, before the theme was applied: dark text on a dark card.
    setSoundEditMode (soundEditMode);

    auto paintVoiceKnob = [] (juce::Slider& s, juce::Colour fill)
    {
        s.getProperties().set ("voiceOnFill", true);
        s.setColour (juce::Slider::rotarySliderFillColourId, fill);
    };
    paintVoiceKnob (shakerVolSlider, voiceShakerOn());
    paintVoiceKnob (congaVolSlider, voiceCongasOn());
    paintVoiceKnob (cembaloVolSlider, voiceCembaloOn());
    paintVoiceKnob (clapVolSlider, voiceClapOn());

    reverbLabel.setColour (juce::Label::textColourId, mute());
    reverbValue.setColour (juce::Label::textColourId, fuchsia());
    intensityLabel.setColour (juce::Label::textColourId, mute());
    intensityValue.setColour (juce::Label::textColourId, fuchsia());
    shakerVolLabel.setColour (juce::Label::textColourId, mute());
    shakerVolValue.setColour (juce::Label::textColourId, fuchsia());
    congaVolLabel.setColour (juce::Label::textColourId, mute());
    congaVolValue.setColour (juce::Label::textColourId, fuchsia());
    cembaloVolLabel.setColour (juce::Label::textColourId, mute());
    cembaloVolValue.setColour (juce::Label::textColourId, fuchsia());
    clapVolLabel.setColour (juce::Label::textColourId, mute());
    clapVolValue.setColour (juce::Label::textColourId, fuchsia());
    inputGainLabel.setColour (juce::Label::textColourId, mute());
    inputGainValue.setColour (juce::Label::textColourId, fuchsia());
    bpmEdit.setColour (juce::Label::textColourId, fuchsia());
    bpmEdit.setColour (juce::Label::backgroundColourId, ink());
    themeButton.setButtonText (darkMode ? "DARK" : "LIGHT");
    themeButton.setToggleState (! darkMode, juce::dontSendNotification);

    refreshStartButton();
    refreshStyleButtons();
    refreshSubdivisionButtons();
    refreshNaturalButton();
    refreshStartNowButton();
    refreshSwingButton();
    refreshBarButton();
    refreshTempoModeButtons();
    refreshClockButtons();
    refreshBufferButtons();
    refreshSourceButton();
    refreshProcButton();
    refreshLoopModeButton();
    refreshVoiceKnobs();
    refreshHitKnobs();
}

std::atomic<int>& MainComponent::kitSoundAtomic (int slot) noexcept
{
    switch (slot)
    {
        case 0:  return engine.settings().shakerSound;
        case 1:  return engine.settings().congaSound;
        case 2:  return engine.settings().cembaloSound;
        default: return engine.settings().clapSound;
    }
}

void MainComponent::assignKitSound (int slot, vp::KitSound sound)
{
    const int want = static_cast<int> (sound);
    if (want < 0 || want >= static_cast<int> (vp::KitSound::count))
        return;
    // A sample family, including triangle, is only which strokes the groove
    // emits. It must not reopen the device or write a hardware rate: on
    // Bluetooth that renegotiates the route (often onto 16 kHz SCO) and the
    // session stops. Takes that are not at the device rate are resampled in
    // PercussionEngine::loadNamedWav when the bank is built.
    // One instrument on one knob. The picker already omits ids sitting on
    // the other three; refuse here so a stale click or a prefs reload
    // cannot put the same KitSound on two slots.
    for (int s = 0; s < 4; ++s)
    {
        if (s == slot)
            continue;
        if (kitSoundAtomic (s).load() == want)
            return;
    }
    kitSoundAtomic (slot).store (want);
    refreshVoiceKnobs();
    savePrefs();
}

void MainComponent::setSoundEditMode (bool on)
{
    soundEditMode = on;
    if (! on)
        soundMenu.dismiss();
    editSoundsButton.setToggleState (on, juce::dontSendNotification);
    // A bare pencil, so the colour is the whole state: text colour when
    // the knobs are played, fuchsia while they are being edited.
    editSoundsButton.setColour (juce::TextButton::textColourOffId, text());
    editSoundsButton.setColour (juce::TextButton::textColourOnId, fuchsia());
    juce::Slider* knobs[] = {
        &shakerVolSlider, &congaVolSlider, &cembaloVolSlider, &clapVolSlider,
        &absorbVolSlider, &hornVolSlider, &uplifterVolSlider, &riserVolSlider
    };
    for (auto* k : knobs)
    {
        k->getProperties().set ("editMode", on);
        k->repaint();
    }
}

void MainComponent::refreshVoiceKnobs()
{
    auto assigned = [] (const std::atomic<int>& a, vp::KitSound identity)
    {
        const int v = a.load();
        if (v >= 0 && v < static_cast<int> (vp::KitSound::count))
            return static_cast<vp::KitSound> (v);
        return identity;
    };
    auto paint = [] (juce::Slider& s, juce::Label& name, bool on, vp::KitSound sound)
    {
        s.getProperties().set ("voiceEnabled", on);
        s.getProperties().set ("voiceOnFill", true);
        const auto fill = colourForKitSound (sound);
        s.setColour (juce::Slider::rotarySliderFillColourId, fill);
        name.setColour (juce::Label::textColourId, fill);
        name.setText (vp::toString (sound), juce::dontSendNotification);
        s.getProperties().set ("knobName", vp::toString (sound));
        name.setAlpha (on ? 1.0f : 0.42f);
        s.repaint();
    };
    auto& cfg = engine.settings();
    paint (shakerVolSlider,  shakerVolLabel,  cfg.shakerEnabled.load(),
           assigned (cfg.shakerSound, vp::KitSound::shaker));
    paint (congaVolSlider,   congaVolLabel,   cfg.congasEnabled.load(),
           assigned (cfg.congaSound, vp::KitSound::congas));
    paint (cembaloVolSlider, cembaloVolLabel, cfg.cembaloEnabled.load(),
           assigned (cfg.cembaloSound, vp::KitSound::cembalo));
    paint (clapVolSlider,    clapVolLabel,    cfg.clapEnabled.load(),
           assigned (cfg.clapSound, vp::KitSound::clap));
}

void MainComponent::assignHitSample (int slot, int sample)
{
    if (slot < 0 || slot >= 4 || sample < 0 || sample >= kHitSampleCount)
        return;
    for (int i = 0; i < 4; ++i)
        if (i != slot && hitVoices[i].selected.load (std::memory_order_relaxed) == sample)
            return;
    hitVoices[slot].selected.store (sample, std::memory_order_release);
    refreshHitKnobs();
    savePrefs();
}

void MainComponent::refreshHitKnobs()
{
    juce::Slider* knobs[] = {
        &absorbVolSlider, &hornVolSlider, &uplifterVolSlider, &riserVolSlider
    };
    juce::Label* labels[] = {
        &absorbHitLabel, &hornHitLabel, &uplifterHitLabel, &riserHitLabel
    };
    for (int i = 0; i < 4; ++i)
    {
        const int sample = juce::jlimit (0, kHitSampleCount - 1,
                                         hitVoices[i].selected.load (std::memory_order_relaxed));
        const auto colour = colourForHit (sample);
        knobs[i]->setColour (juce::Slider::rotarySliderFillColourId, colour);
        knobs[i]->getProperties().set ("knobName", kHitNames[sample]);
        labels[i]->setColour (juce::Label::textColourId, colour);
        labels[i]->setText (kHitNames[sample], juce::dontSendNotification);
        knobs[i]->repaint();
    }
}

void MainComponent::startPressed()
{
    vp::haptic (vp::Haptic::medium);
    if (! internalTrackSelected())
        ensureMicrophone();
    restartWallMs = juce::Time::getMillisecondCounterHiRes();
    restartLogPending = stopWallMs > 0.0 && restartWallMs - stopWallMs < 10000.0;
    userWantsArmed = true;
    engine.requestStart();
    refreshStartButton();
}

void MainComponent::stopPressed()
{
    vp::haptic (vp::Haptic::heavy);
    stopSnap = engine.snapshot();
    stopWallMs = juce::Time::getMillisecondCounterHiRes();
    stopTrackSec = internalTrackSelected() ? trackTransport.getCurrentPosition() : -1.0;
    restartLogPending = false;
    userWantsArmed = false;
    engine.requestStop();
    refreshStartButton();
}

void MainComponent::writeRestartLog()
{
    // ASCII only: juce::String (const char*) asserts on anything else.
    static const char* const regimes[] = { "CERCO", "FISSO", "VIVO" };
    auto describe = [] (const vp::EngineSnapshot& s)
    {
        const float bpm = juce::jmax (40.0f, s.bpm);
        return juce::String ("bpm ") + juce::String (s.bpm, 2)
             + " clock " + juce::String (s.clockBpm, 2)
             + " rete " + juce::String (s.neuralBpm, 2)
             + " pettine " + juce::String (s.combBpm, 2)
             + " " + regimes[juce::jlimit (0, 2, s.tempoRegime)]
             + " stato " + juce::String (static_cast<int> (s.state))
             + " erroreFase " + juce::String (s.phaseErrorBeats, 3) + " batt ("
             + juce::String (s.phaseErrorBeats * 60000.0f / bpm, 0) + " ms)"
             + " fiducia " + juce::String (s.evidenceTrust, 2)
             + " battuta " + juce::String (s.barPhase, 3)
             + (s.barTrusted ? " 1ok" : " 1?")
             + (s.percussionAudible ? " suona" : " muto");
    };
    const bool snapped = snap.silentSnapCount != stopSnap.silentSnapCount;
    const float bpmBefore = juce::jmax (40.0f, stopSnap.bpm);
    juce::String line;
    line << juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S")
         << " | " << (internalTrackSelected() ? trackName : juce::String ("ingresso"))
         << " @ " << juce::String (stopTrackSec, 1) << " s"
         << " | pausa " << juce::String ((restartWallMs - stopWallMs) / 1000.0, 1) << " s"
         << " | PRIMA " << describe (stopSnap)
         << " | DOPO " << describe (snap)
         << " | riallineato " << (snapped ? juce::String (snap.silentSnapBeats, 3) + " batt ("
                                              + juce::String (snap.silentSnapBeats * 60000.0f / bpmBefore, 0)
                                              + " ms) x" + juce::String (static_cast<int> (
                                                    snap.silentSnapCount - stopSnap.silentSnapCount))
                                          : juce::String ("no"))
         << " | bpm " << juce::String ((snap.bpm / bpmBefore - 1.0f) * 100.0f, 1) << "%"
         << "\n";
    juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("VirtualPercussionist-stopstart.log")
        .appendText (line);
    juce::Logger::outputDebugString ("VPRESTART " + line);
}

void MainComponent::tapPressed()
{
    tapAlignFlash = 1.0f;
    if (! internalTrackSelected())
        ensureMicrophone();
    engine.tap();
    tapFlash = 2;
}

void MainComponent::declareBar()
{
    // The nearest beat is the one. The bar locks. The phase is not snapped:
    // that shortened the beat and was heard as the tempo jumping.
    engine.settings().barDeclare.fetch_add (1);
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
        b.setColour (juce::TextButton::buttonColourId, ink());
        b.setColour (juce::TextButton::textColourOffId, text());
    };
    paint (followButton, follow);
    paint (fixedButton, ! follow);

    // Compact has no ± BPM row. The 15 Hz timer used to show these again
    // ~66 ms after applyCompactVisibility hid them, leaving leftover bounds
    // on the mini page and a layout fight on every drag.
    const bool showNudge = ! follow && ! isCompact();
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
    // START: black with white lettering (the text colour in light mode, so it
    // still reads as the dark key); STOP: fuchsia. The play triangle and stop
    // square are drawn by the look.
    startButton.setButtonText (userWantsArmed ? "STOP" : "START");
    startButton.setColour (juce::TextButton::buttonColourId,
                           userWantsArmed ? fuchsia()
                                          : (gDarkMode ? juce::Colours::black : text()));
    startButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    startButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    startButton.setToggleState (userWantsArmed, juce::dontSendNotification);
    startButton.repaint();
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

double MainComponent::snapToOfferedRate (double rate) const
{
    if (rate < 22050.0 || rate > 192000.0)
        return 0.0;

    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr)
        return rate;

    const auto rates = dev->getAvailableSampleRates();
    if (rates.isEmpty())
        return rate;

    double best = 0.0;
    double bestDist = 1.0e12;
    for (double r : rates)
    {
        if (r < 22050.0 || r > 192000.0)
            continue;
        const double d = std::abs (r - rate);
        if (d < bestDist)
        {
            bestDist = d;
            best = r;
        }
    }
    if (best >= 22050.0 && bestDist < 1.0)
        return best;

    // 48 kHz before 44.1. A Bluetooth A2DP route is often already at 48 kHz
    // and will not keep 44.1; 44.1 is what a requested rate of 0 used to
    // become, because the iOS device object is constructed at 44100 and that
    // value is in the explicit list.
    static constexpr double kPrefer[] = { 48000.0, 44100.0, 96000.0, 88200.0 };
    for (double p : kPrefer)
        for (double r : rates)
            if (std::abs (r - p) < 1.0)
                return r;

    for (double r : rates)
        if (r >= 44100.0 && r <= 192000.0)
            return r;

    return best >= 22050.0 ? best : 0.0;
}

double MainComponent::deviceSampleRate() const
{
    if (clockHz > 0)
        return static_cast<double> (clockHz);

    if (overrideAutoRate >= 22050.0)
        return overrideAutoRate;

    // A unit that is already calling us keeps its clock. AUTO after a manual
    // 44.1 or 48 choice inherits that live rate and does not open again.
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        const double cur = dev->getCurrentSampleRate();
        if (dev->isPlaying() && cur >= 22050.0 && cur <= 192000.0)
        {
            const double snapped = snapToOfferedRate (cur);
            return snapped >= 22050.0 ? snapped : cur;
        }
    }

    if (startupHardwareRate >= 22050.0)
    {
        const double snapped = snapToOfferedRate (startupHardwareRate);
        if (snapped >= 22050.0)
            return snapped;
    }

    const double hw = vp::sessionSampleRate();
    if (hw >= 22050.0 && hw <= 192000.0)
    {
        const double snapped = snapToOfferedRate (hw);
        if (snapped >= 22050.0)
            return snapped;
    }

    const double fallback = snapToOfferedRate (48000.0);
    return fallback >= 22050.0 ? fallback : 48000.0;
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

    // Permission completion is asynchronous and can arrive after iOS has put
    // the app in the background. Do not let that callback undo the idle power
    // down; handleAppResumed() will perform the normal open in the foreground.
    if (appIsSuspended && ! userWantsArmed && ! trackTransport.isPlaying())
    {
        audioPoweredDownForBackground = true;
        return;
    }

    const int ins = granted && ! internalTrackSelected() ? 2 : 0;

    if (! audioOpened)
    {
        audioOpened = true;

        // Session first, device second. It used to be the other way round -
        // open at whatever came out, then set the category, then ask for 48 kHz
        // and a 256-frame buffer whatever the interface was on, then close and
        // reopen. AUTO still writes no rate here: the number the hardware is
        // already on has to be read before JUCE's open. The iOS device is
        // constructed at 44100, a requested 0 becomes that 44100, and
        // setPreferredSampleRate(44100) on a 48 kHz Bluetooth route drops the
        // callback. finishInitialDeviceStart opens once more only when that
        // first open did not land on the rate just read.
        vp::prepareAudioSession ({ requestedSampleRate(), requestedBufferFrames(),
                                   inputProcessing, false, ! internalTrackSelected() });
        if (clockHz == 0)
        {
            const double hw = vp::sessionSampleRate();
            if (hw >= 22050.0 && hw <= 192000.0)
                startupHardwareRate = hw;
        }
        setAudioChannels (ins, 2);
        finishInitialDeviceStart();
        return;
    }

    auto* dev = deviceManager.getCurrentAudioDevice();
    const int nIn = dev != nullptr ? dev->getActiveInputChannels().countNumberOfSetBits() : 0;
    if ((ins > 0 && nIn <= 0) || (ins == 0 && nIn > 0))
        applyAudioSetup (true);
}

void MainComponent::applyAudioSetup (bool claimInputChannels)
{
    // AUTO with a live device passes 0: the session is already on its clock
    // and a redundant setPreferredSampleRate is a route change. An explicit
    // clock, and the one corrective first open, pass the rate they are about
    // to open at so the session and the unit agree.
    const double sessionRate = (clockHz > 0 || overrideAutoRate >= 22050.0)
                                   ? deviceSampleRate()
                                   : requestedSampleRate();
    vp::prepareAudioSession ({ sessionRate, requestedBufferFrames(),
                               inputProcessing, false, ! internalTrackSelected() });

    auto setup = deviceManager.getAudioDeviceSetup();
    if (const double sr = deviceSampleRate(); sr > 0.0)
        setup.sampleRate = sr;
    if (const int buf = deviceBufferFrames(); buf > 0)
        setup.bufferSize = buf;

    // Outside BRANO, leave channels as they are unless an input is being
    // claimed. JUCE compares the setup it is handed against
    // the one it is on and returns without touching the device when they are
    // equal, so on a rig already at the right clock this call costs nothing.
    // Rewriting the channels unconditionally - even to the same two - flips
    // useDefaultInputChannels and reopens the device for nothing.
    if (internalTrackSelected())
    {
        // The file supplies the analysis input. The iPad mic was still being
        // pulled in the AirPods/resize log although BRANO never reads it.
        setup.inputChannels.clear();
        setup.useDefaultInputChannels = false;
    }
    else if (claimInputChannels && micGranted)
    {
        // Two is what the app has always asked for and is all a microphone or a
        // stereo line feed needs. A kick send lives on a channel of its own, so
        // when the listener names one the device has to be opened wide enough
        // to reach it - asking for channels a desk does not have costs nothing,
        // JUCE gives back what exists.
        const int wantKick = engine.settings().kickChannel.load() + 1;
        setup.inputChannels.clear();
        setup.inputChannels.setRange (0, juce::jmax (2, wantKick), true);
        setup.useDefaultInputChannels = false;
    }

    const auto err = deviceManager.setAudioDeviceSetup (setup, true);
    juce::ignoreUnused (err);
    applyInputProcessing();
}

void MainComponent::finishInitialDeviceStart()
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    const double live = dev != nullptr ? dev->getCurrentSampleRate() : 0.0;
    const bool playing = dev != nullptr && dev->isPlaying();

    double want = 0.0;
    if (clockHz > 0)
        want = static_cast<double> (clockHz);
    else if (startupHardwareRate >= 22050.0)
        want = startupHardwareRate;
    else
    {
        const double hw = vp::sessionSampleRate();
        if (hw >= 22050.0 && hw <= 192000.0)
            want = hw;
        else if (live >= 22050.0 && live <= 192000.0)
            want = live;
        else
            want = 48000.0;
    }

    // The iOS device object is constructed at 44100, so a requested 0 becomes
    // 44100 even when the route is already at something else. If the unit is
    // actually running at a different full-band rate, that is the rate the
    // route accepted — keep it. The failure this exists to correct is the
    // other way round: hardware was read at 48 kHz and the open came out at
    // the constructed 44100.
    if (clockHz == 0 && playing && audioReady
        && live >= 22050.0 && live <= 192000.0
        && std::abs (live - want) >= 1.0
        && std::abs (live - 44100.0) >= 1.0
        && std::abs (want - 44100.0) < 1.0)
        want = live;

    const double snapped = snapToOfferedRate (want);
    if (snapped >= 22050.0)
        want = snapped;

    // Same test the clock buttons pass: the open rate has to be one the
    // device lists, the unit has to be running, and prepareToPlay has to
    // have run. A first open that came out at 44100 while the route was
    // already at 48 kHz looks open and is silent until someone presses
    // 44.1 or 48 — that press is this call.
    const bool healthy = playing && audioReady && live >= 22050.0
                         && std::abs (live - want) < 1.0;
    if (! healthy)
    {
        overrideAutoRate = want;
        // setAudioDeviceSetup returns without touching the unit when the
        // setup compares equal. The rate-0 open can already be sitting on
        // 44100 and still never have delivered audioDeviceAboutToStart.
        if (dev != nullptr)
            deviceManager.closeAudioDevice();
        applyAudioSetup (false);
        overrideAutoRate = 0.0;
    }
    else
        applyInputProcessing();

    startupHardwareRate = 0.0;
}

void MainComponent::rebuildAudioDevice (const char* why)
{
    if (! audioOpened)
        return;

    // Kept for the diagnostics page. A rebuild count on its own says the rig had
    // a problem; the reason says which one, and these three are different rigs.
    lastRebuildWhy = why != nullptr ? why : "";
    ++deviceRebuilds;
    stalledTicks = 0;
    // Two seconds before another one is allowed. A rig that genuinely cannot
    // hold a device open would otherwise be rebuilt fifteen times a second,
    // which is louder and less useful than being silent.
    rebuildCooldownTicks = 30;

    // The session first, because after a media server restart it has none of
    // what was set on it - category, mode, rate, buffer, all back to defaults.
    forceEnginePrepare = true;
    vp::prepareAudioSession ({ requestedSampleRate(), requestedBufferFrames(),
                               inputProcessing, true, ! internalTrackSelected() });

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
                                   inputProcessing, false, ! internalTrackSelected() });
        applyInputProcessing();
    }
    repaint();
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
    const auto source = static_cast<vp::FollowSource> (
        engine.settings().followSource.load());
    const bool speaker = source == vp::FollowSource::speaker;
    const bool internal = source == vp::FollowSource::internalPlayer;
    sourceButton.setButtonText (internal ? "BRANO" : (speaker ? "IPAD" : "MIXER"));
    paintChoice (sourceButton, speaker || internal);
    refreshInternalTrackButtons();

    // The kick channel is lit only once the strikes are actually landing on the
    // beat: a channel that is named but is not a kick is worse than no channel
    // at all, and the tracker works that out for itself - see
    // BeatTracker::kickIsTrusted.
    const int kickCh = engine.settings().kickChannel.load();
    kickButton.setButtonText (kickCh < 0 ? "CASSA NO"
                                         : "CASSA " + juce::String (kickCh + 1));
    paintChoice (kickButton, kickCh >= 0 && snap.kickTrusted);

    // And the round trip shows the number when there is one, so a listener can
    // see at a glance whether the clock is running on a measurement of this rig
    // or on what the operating system believes about the interface.
    if (! engine.latencyMeasurementRunning())
    {
        const float measured = engine.measuredLatency();
        if (measured > 0.0f)
            latencyButton.setButtonText ("RT " + juce::String (measured, 1) + " ms");
        else if (latencyButton.getButtonText().startsWith ("RT ")
                 || latencyButton.getButtonText() == "MISURO...")
            latencyButton.setButtonText ("LATENZA");
        paintChoice (latencyButton, measured > 0.0f);
    }
}

bool MainComponent::internalTrackSelected() const noexcept
{
    return engine.settings().followSource.load (std::memory_order_relaxed)
           == static_cast<int> (vp::FollowSource::internalPlayer);
}

void MainComponent::selectFollowSource (vp::FollowSource source, bool restartInput)
{
    const auto previous = static_cast<vp::FollowSource> (
        engine.settings().followSource.load (std::memory_order_relaxed));
    if (source != vp::FollowSource::internalPlayer && trackTransport.isPlaying())
        trackTransport.stop();

    engine.settings().followSource.store (static_cast<int> (source),
                                          std::memory_order_relaxed);
    if (audioOpened && (source == vp::FollowSource::internalPlayer)
                       != (previous == vp::FollowSource::internalPlayer))
        applyAudioSetup (true);
    // A mixer, the iPad speaker and the internal player are different inputs.
    // Reusing the preceding source's grid makes the new one defend its BPM.
    // File loading requests its own restart after the reader is installed.
    if (restartInput && source != previous)
        engine.notifyInputRestart();
    refreshSourceButton();
    savePrefs();
    repaint();
}

void MainComponent::chooseInternalTrack()
{
    trackChooser = std::make_unique<juce::FileChooser> (
        "Scegli un brano audio", juce::File(), trackFormats.getWildcardForAllFormats(),
        true, false, &settingsOverlay);

    juce::Component::SafePointer<MainComponent> safe (this);
    trackChooser->launchAsync (juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles,
                               [safe] (const juce::FileChooser& chooser)
    {
        if (safe == nullptr)
            return;

        auto url = chooser.getURLResult();
        safe->trackChooser.reset();
        if (! url.isEmpty())
            safe->loadInternalTrack (std::move (url));
    });
}

void MainComponent::loadInternalTrack (juce::URL url)
{
    auto stream = url.createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress));
    auto* reader = stream != nullptr ? trackFormats.createReaderFor (std::move (stream))
                                     : nullptr;
    if (reader == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon, "Brano non leggibile",
            "Il file scelto non e' un formato audio supportato oppure non e' disponibile offline.");
        return;
    }

    const bool differentTrack = trackUrl != url;
    // Release the previous file-provider stream before replacing its URL.
    trackWaveScanReader.reset();
    trackTransport.stop();
    trackTransport.setSource (nullptr);
    // A manual octave is a judgement about the previous recording, not an
    // instruction to halve every subsequently loaded song. Clear it only once
    // the old file has stopped, so the old part cannot change level mid-note.
    if (differentTrack && ! engine.settings().tempoOctaveAuto.load())
    {
        engine.settings().tempoOctave.store (0);
        engine.settings().tempoOctaveAuto.store (true);
        refreshOctaveButtons();
    }
    trackReader = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    trackUrl = std::move (url); // retains the iOS security-scoped bookmark
    trackName = trackUrl.getFileName();
    trackTransport.setSource (reader);
    selectFollowSource (vp::FollowSource::internalPlayer, false);
    // A different file is a different input, not a drift of the one before it.
    // Without this the tracker keeps the lock of the previous song, and with
    // START still on the percussion plays the old tempo for as long as the
    // decoder defends it - measured at 8-20 s, or until STOP. This restarts
    // the decoder and retires the old source's silent clock. Queue it before
    // playback starts so the new source cannot run through the old decoder
    // epoch before the restart is requested. See docs/TODO.md item 3.
    engine.notifyInputRestart();
    trackTransport.start();
    // The picture uses a second reader because the playing reader belongs to
    // the read-ahead thread. Only its setup runs here; the timer scans bounded
    // blocks after PLAY so a full-file pass cannot hold the BPM display.
    buildTrackWaveform();
    refreshInternalTrackButtons();
    relayoutSettings();
    if (settingsOverlay.isVisible())
        repaint();
}

void MainComponent::layoutTrackWaveform()
{
    if (trackReader == nullptr)
    {
        trackWaveform.setVisible (false);
        return;
    }

    juce::Rectangle<int> bounds;
    if (settingsOverlay.isVisible() && ! settingsRows.trackWave.isEmpty())
        bounds = getLocalArea (&settingsOverlay, settingsRows.trackWave);
    else
    {
        const auto rows = stageRows (stageArea());
        bounds = rows.trackWave.isEmpty() ? juce::Rectangle<int>{} : rows.trackWave.reduced (2, 4);
    }

    if (bounds.isEmpty())
    {
        trackWaveform.setVisible (false);
        return;
    }

    trackWaveform.setBounds (bounds);
    trackWaveform.setVisible (true);
    trackWaveform.toFront (false);
}

int MainComponent::trackWaveformHeight() const noexcept
{
    return trackReader != nullptr ? 72 : 0;
}

void MainComponent::clearTrackWaveform()
{
    trackWaveScanReader.reset();
    trackWaveScanPos = 0;
    trackWaveMaxPeak = 0.0f;
    trackWavePeaks.clearQuick();
    trackWaveLengthSec = 0.0;
    trackWavePreview = -1.0;
    trackWaveform.setVisible (false);
    trackWaveform.repaint();
}

void MainComponent::buildTrackWaveform()
{
    clearTrackWaveform();
    if (trackUrl.isEmpty() || trackReader == nullptr)
        return;

    // The transport reader already knows the duration. Seeking does not need
    // to wait for a second stream or the picture to finish.
    const auto* playingReader = trackReader->getAudioFormatReader();
    if (playingReader != nullptr && playingReader->sampleRate > 0.0
        && playingReader->lengthInSamples > 0)
        trackWaveLengthSec = static_cast<double> (playingReader->lengthInSamples)
                             / playingReader->sampleRate;

    // Not the transport's reader. setSource has already started its
    // read-ahead thread, and an MP3 reader has one shared stream position.
    auto stream = trackUrl.createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress));
    trackWaveScanReader.reset (
        stream != nullptr ? trackFormats.createReaderFor (std::move (stream)) : nullptr);
    if (trackWaveScanReader == nullptr || trackWaveScanReader->lengthInSamples <= 0)
    {
        trackWaveScanReader.reset();
        return;
    }

    constexpr int kCols = 1024;
    trackWavePeaks.resize (kCols);
    trackWavePeaks.fill (0.0f);
    trackWaveScanBuffer.setSize (juce::jmax (1, (int) trackWaveScanReader->numChannels),
                                 32768, false, false, true);
}

void MainComponent::processTrackWaveform()
{
    if (trackWaveScanReader == nullptr)
        return;

    // One bounded decode per UI tick. The old whole-file pass held the message
    // thread after PLAY, hiding BPM updates while the live worker was running.
    auto& reader = *trackWaveScanReader;
    const int64_t total = reader.lengthInSamples;
    const int n = (int) juce::jmin<int64_t> (trackWaveScanBuffer.getNumSamples(),
                                           total - trackWaveScanPos);
    if (n <= 0 || ! reader.read (&trackWaveScanBuffer, 0, n, trackWaveScanPos, true, true))
    {
        trackWaveScanReader.reset();
        return;
    }

    const int numCh = trackWaveScanBuffer.getNumChannels();
    const int kCols = trackWavePeaks.size();
    for (int i = 0; i < n; ++i)
    {
        const int64_t sampleIdx = trackWaveScanPos + i;
        const int col = (int) juce::jlimit<int64_t> (0, kCols - 1,
                                                    (sampleIdx * kCols) / total);
        float peak = 0.0f;
        for (int c = 0; c < numCh; ++c)
            peak = juce::jmax (peak, std::abs (trackWaveScanBuffer.getSample (c, i)));
        auto& current = trackWavePeaks.getReference (col);
        current = juce::jmax (current, peak);
        trackWaveMaxPeak = juce::jmax (trackWaveMaxPeak, current);
    }
    trackWaveScanPos += n;
    if (trackWaveScanPos >= total)
        trackWaveScanReader.reset();
}

void MainComponent::seekInternalTrack (double proportion)
{
    if (trackReader == nullptr || trackWaveLengthSec <= 0.0)
        return;

    proportion = juce::jlimit (0.0, 1.0, proportion);
    trackTransport.setPosition (proportion * trackWaveLengthSec);
    engine.notifyTrackSeek();

    if (! internalTrackSelected())
        selectFollowSource (vp::FollowSource::internalPlayer);

    if (! trackTransport.isPlaying())
        trackTransport.start();

    trackWavePreview = -1.0;
    refreshInternalTrackButtons();
    trackWaveform.repaint();
}

void MainComponent::toggleInternalTrack()
{
    if (trackReader == nullptr)
    {
        chooseInternalTrack();
        return;
    }

    if (! internalTrackSelected())
        selectFollowSource (vp::FollowSource::internalPlayer);

    if (trackTransport.isPlaying())
        trackTransport.stop();
    else
    {
        if (trackTransport.hasStreamFinished())
            trackTransport.setPosition (0.0);
        trackTransport.start();
    }
    refreshInternalTrackButtons();
}

void MainComponent::refreshInternalTrackButtons()
{
    const bool loaded = trackReader != nullptr;
    trackLoadButton.setButtonText (loaded ? trackName.substring (0, 14) : "CARICA");
    trackPlayButton.setButtonText (trackTransport.isPlaying() ? "PAUSA" : "PLAY");
    trackPlayButton.setEnabled (loaded);
    paintChoice (trackLoadButton, loaded && internalTrackSelected());
    paintChoice (trackPlayButton, trackTransport.isPlaying() && internalTrackSelected());
}

void MainComponent::setSettingsOpen (bool open)
{
    settingsButton.setToggleState (open, juce::dontSendNotification);
    settingsOverlay.setVisible (open);
    if (open)
    {
        styleMenu.dismiss();
        soundMenu.dismiss();
        settingsOverlay.toFront (false);
        settingsOverlay.setBounds (getLocalBounds());
        relayoutSettings();
    }
    else
        layoutTrackWaveform();

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

    // Clamped low deliberately: a remembered channel from a rig that is not
    // plugged in today would open inputs that do not exist.
    engine.settings().kickChannel.store (
        juce::jlimit (-1, 31, prefs->getIntValue ("kickChannel", -1)));
    // A round trip measured on this rig, if one was. Kept per install rather
    // than per session: the rig does not usually change between soundcheck and
    // the first song, and re-measuring is one press.
    engine.setMeasuredLatency (static_cast<float> (prefs->getDoubleValue ("measuredLatencyMs", 0.0)),
                               static_cast<float> (prefs->getDoubleValue ("measuredLatencyBaseMs", 0.0)));

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
    // AUTO was eighths. A saved AUTO becomes 1/8 so a button is lit and the
    // part does not change.
    if (sub == static_cast<int> (vp::Subdivision::autoDetect))
        engine.settings().subdivision.store (static_cast<int> (vp::Subdivision::eighth));
    else if (sub == static_cast<int> (vp::Subdivision::quarter)
             || sub == static_cast<int> (vp::Subdivision::eighth)
             || sub == static_cast<int> (vp::Subdivision::sixteenth))
        engine.settings().subdivision.store (sub);

    // The level the player last chose, and whether they chose one at all. Both
    // are saved for the current material; loading a different file clears a
    // manual choice before that file starts (see loadInternalTrack).
    const int oct = prefs->getIntValue ("tempoOctave",
                                        engine.settings().tempoOctave.load());
    engine.settings().tempoOctave.store (juce::jlimit (-1, 1, oct));
    engine.settings().tempoOctaveAuto.store (
        prefs->getBoolValue ("tempoOctaveAuto",
                             engine.settings().tempoOctaveAuto.load()));

    // DANCE became the default figure on 2026-09-28. An install that saved a
    // figure before then saved the old default: start it once from DANCE, then
    // honour whatever is chosen after that.
    const bool danceDefaultSeen = prefs->getBoolValue ("grooveDanceDefault", false);
    const int style = danceDefaultSeen
                          ? prefs->getIntValue ("grooveStyle", engine.settings().grooveStyle.load())
                          : engine.settings().grooveStyle.load();
    if (style >= 0 && style < static_cast<int> (vp::GrooveStyle::count))
        engine.settings().grooveStyle.store (style);

    engine.settings().grooveAuto.store (
        danceDefaultSeen && prefs->getBoolValue ("grooveAuto", engine.settings().grooveAuto.load()));
    engine.settings().dynamicsFollow.store (
        prefs->getBoolValue ("dynamicsFollow", engine.settings().dynamicsFollow.load()));

    engine.settings().shakerEnabled.store (
        prefs->getBoolValue ("shakerEnabled", engine.settings().shakerEnabled.load()));
    engine.settings().congasEnabled.store (
        prefs->getBoolValue ("congasEnabled", engine.settings().congasEnabled.load()));
    engine.settings().cembaloEnabled.store (
        prefs->getBoolValue ("cembaloEnabled", engine.settings().cembaloEnabled.load()));
    engine.settings().clapEnabled.store (
        prefs->getBoolValue ("clapEnabled", engine.settings().clapEnabled.load()));
    auto loadKit = [this] (const char* key, std::atomic<int>& dest, vp::KitSound identity)
    {
        const int v = prefs->getIntValue (key, static_cast<int> (identity));
        if (v >= 0 && v < static_cast<int> (vp::KitSound::count))
            dest.store (v);
    };
    loadKit ("shakerSound",  engine.settings().shakerSound,  vp::KitSound::shaker);
    loadKit ("congaSound",   engine.settings().congaSound,   vp::KitSound::congas);
    loadKit ("cembaloSound", engine.settings().cembaloSound, vp::KitSound::cembalo);
    loadKit ("clapSound",    engine.settings().clapSound,    vp::KitSound::clap);
    {
        // Older prefs could store the same family on two knobs. Keep the
        // first occupant and give later slots a still-free instrument so
        // the four-map stays unique after reload.
        const vp::KitSound identity[4] = {
            vp::KitSound::shaker, vp::KitSound::congas,
            vp::KitSound::cembalo, vp::KitSound::clap
        };
        const int nFam = static_cast<int> (vp::KitSound::count);
        bool used[static_cast<int> (vp::KitSound::count)] = {};
        for (int s = 0; s < 4; ++s)
        {
            int v = kitSoundAtomic (s).load();
            if (v < 0 || v >= nFam || used[v])
            {
                v = -1;
                const int id = static_cast<int> (identity[s]);
                if (! used[id])
                    v = id;
                else
                    for (int k = 0; k < nFam && v < 0; ++k)
                        if (! used[k])
                            v = k;
                if (v < 0)
                    v = id;
            }
            used[v] = true;
            kitSoundAtomic (s).store (v);
        }
    }
    engine.settings().shakerNatural.store (
        prefs->getBoolValue ("shakerNatural", engine.settings().shakerNatural.load()));
    engine.settings().startImmediately.store (
        prefs->getBoolValue ("startImmediately", false));

    const bool recordedLoops = loopBankReady
                               && prefs->getBoolValue ("recordedLoops", true);
    engine.setRecordedLoopsEnabled (recordedLoops);
    if (recordedLoops)
    {
        // Safe migration for installs which saved AUTO, another sample rate or
        // a very short buffer before the recorded bank was bundled.
        clockHz = 48000;
        if (bufferChoice == 0 || bufferChoice < 256)
            bufferChoice = 256;
    }

    const float shakerVol = clamp01 (prefs->getDoubleValue ("shakerVolume", 1.00), 1.00);
    engine.settings().shakerVolume.store (shakerVol);
    setFader (shakerVolSlider, shakerVolValue, shakerVol);

    const float congaVol = clamp01 (prefs->getDoubleValue ("congaVolume", 1.00), 1.00);
    engine.settings().congaVolume.store (congaVol);
    setFader (congaVolSlider, congaVolValue, congaVol);

    const float cembaloVol = clamp01 (prefs->getDoubleValue ("cembaloVolume", 1.00), 1.00);
    engine.settings().cembaloVolume.store (cembaloVol);
    setFader (cembaloVolSlider, cembaloVolValue, cembaloVol);

    const float clapVol = clamp01 (prefs->getDoubleValue ("clapVolume", 1.00), 1.00);
    engine.settings().clapVolume.store (clapVol);
    setFader (clapVolSlider, clapVolValue, clapVol);

    const float absorbVol = clamp01 (prefs->getDoubleValue ("absorbVolume", 1.00), 1.00);
    hitVoices[0].gain.store (absorbVol, std::memory_order_relaxed);
    setFader (absorbVolSlider, absorbHitValue, absorbVol);

    const float hornVol = clamp01 (prefs->getDoubleValue ("hornVolume", 1.00), 1.00);
    hitVoices[1].gain.store (hornVol, std::memory_order_relaxed);
    setFader (hornVolSlider, hornHitValue, hornVol);

    const float uplifterVol = clamp01 (prefs->getDoubleValue ("uplifterVolume", 1.00), 1.00);
    hitVoices[2].gain.store (uplifterVol, std::memory_order_relaxed);
    setFader (uplifterVolSlider, uplifterHitValue, uplifterVol);

    const float riserVol = clamp01 (prefs->getDoubleValue ("riserVolume", 1.00), 1.00);
    hitVoices[3].gain.store (riserVol, std::memory_order_relaxed);
    setFader (riserVolSlider, riserHitValue, riserVol);

    bool usedHits[kHitSampleCount] = {};
    for (int i = 0; i < 4; ++i)
    {
        int sample = prefs->getIntValue (kHitPrefKeys[i], i);
        if (sample < 0 || sample >= kHitSampleCount || usedHits[sample])
        {
            sample = i;
            if (usedHits[sample])
                for (int k = 0; k < kHitSampleCount; ++k)
                    if (! usedHits[k]) { sample = k; break; }
        }
        usedHits[sample] = true;
        hitVoices[i].selected.store (sample, std::memory_order_relaxed);
    }

    const float inGain = juce::jlimit (0.0, 4.0, prefs->getDoubleValue ("inputGain", 1.0));
    engine.settings().inputGain.store (static_cast<float> (inGain));
    inputGainSlider.setValue (inGain, juce::dontSendNotification);
    inputGainValue.setText (micGainText (static_cast<float> (inGain)),
                            juce::dontSendNotification);

    const float reverb = clamp01 (prefs->getDoubleValue ("reverbAmount", 0.30), 0.30);
    engine.settings().reverbAmount.store (reverb);
    setFader (reverbSlider, reverbValue, reverb);

    // Stored as a flag, but read through the old 0..1 double so an install that
    // had the knob somewhere in the middle comes back as "on" rather than as a
    // value the UI can no longer show. Anything past halfway is swing.
    engine.settings().swing.store (
        prefs->getDoubleValue ("swing", 0.0) > 0.5 ? 1.0f : 0.0f);
    refreshSwingButton();

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

    refreshLoopModeButton();
    refreshVoiceKnobs();
}

bool MainComponent::loadBundledLoopBank()
{
#if defined (VP_HAS_LOOP_BANK) && VP_HAS_LOOP_BANK
    int manifestBytes = 0;
    const char* manifest = embeddedLoopResource ("bank.vploops", manifestBytes);
    if (manifest == nullptr || manifestBytes <= 0)
    {
        loopBankError = "manifest incorporato mancante";
        engine.setRecordedLoopsEnabled (false);
        return false;
    }

    std::string error;
    const bool ok = engine.loadLoopBankFromMemory (
        std::string (manifest, static_cast<size_t> (manifestBytes)),
        [] (const std::string& filename, vp::WavAudio& audio, std::string& why)
        {
            int bytes = 0;
            const char* data = embeddedLoopResource (filename, bytes);
            if (data == nullptr || bytes <= 0)
            {
                why = "embedded WAV not found: " + filename;
                return false;
            }
            return vp::decodeWav (reinterpret_cast<const unsigned char*> (data),
                                  static_cast<size_t> (bytes), audio, why);
        },
        error);

    loopBankError = ok ? juce::String() : juce::String (error);
    if (! ok)
        engine.setRecordedLoopsEnabled (false);
    return ok;
#else
    loopBankError = "banco non incluso in questa build";
    engine.setRecordedLoopsEnabled (false);
    return false;
#endif
}

void MainComponent::refreshLoopModeButton()
{
    const bool loops = loopBankReady && engine.recordedLoopsEnabled();
    loopModeButton.setEnabled (loopBankReady);
    loopModeButton.setButtonText (loopBankReady ? (loops ? "LOOP" : "PATTERN")
                                                : "LOOP N/D");
    paintChoice (loopModeButton, loops);
}

void MainComponent::savePrefs (bool flush)
{
    if (prefs == nullptr)
        return;

    prefs->setValue ("clockHz", clockHz);
    prefs->setValue ("bufferFrames", bufferChoice);
    prefs->setValue ("inputProcessing", inputProcessing);
    prefs->setValue ("followSource", engine.settings().followSource.load());
    prefs->setValue ("kickChannel", engine.settings().kickChannel.load());
    prefs->setValue ("measuredLatencyMs", static_cast<double> (engine.measuredLatency()));
    prefs->setValue ("measuredLatencyBaseMs", static_cast<double> (engine.measuredLatencyBase()));
    prefs->setValue ("theme", themeFollowsSystem ? -1 : (darkMode ? 1 : 0));
    prefs->setValue ("subdivision", engine.settings().subdivision.load());
    prefs->setValue ("tempoOctave", engine.settings().tempoOctave.load());
    prefs->setValue ("tempoOctaveAuto", engine.settings().tempoOctaveAuto.load());
    prefs->setValue ("grooveStyle", engine.settings().grooveStyle.load());
    prefs->setValue ("grooveAuto", engine.settings().grooveAuto.load());
    prefs->setValue ("grooveDanceDefault", true);
    prefs->setValue ("dynamicsFollow", engine.settings().dynamicsFollow.load());
    prefs->setValue ("shakerEnabled", engine.settings().shakerEnabled.load());
    prefs->setValue ("congasEnabled", engine.settings().congasEnabled.load());
    prefs->setValue ("cembaloEnabled", engine.settings().cembaloEnabled.load());
    prefs->setValue ("clapEnabled", engine.settings().clapEnabled.load());
    prefs->setValue ("shakerSound", engine.settings().shakerSound.load());
    prefs->setValue ("congaSound", engine.settings().congaSound.load());
    prefs->setValue ("cembaloSound", engine.settings().cembaloSound.load());
    prefs->setValue ("clapSound", engine.settings().clapSound.load());
    prefs->setValue ("shakerNatural", engine.settings().shakerNatural.load());
    prefs->setValue ("startImmediately", engine.settings().startImmediately.load());
    prefs->setValue ("recordedLoops", engine.recordedLoopsEnabled());
    prefs->setValue ("shakerVolume",
                     static_cast<double> (engine.settings().shakerVolume.load()));
    prefs->setValue ("congaVolume",
                     static_cast<double> (engine.settings().congaVolume.load()));
    prefs->setValue ("cembaloVolume",
                     static_cast<double> (engine.settings().cembaloVolume.load()));
    prefs->setValue ("clapVolume",
                     static_cast<double> (engine.settings().clapVolume.load()));
    prefs->setValue ("absorbVolume",
                     static_cast<double> (hitVoices[0].gain.load (std::memory_order_relaxed)));
    prefs->setValue ("hornVolume",
                     static_cast<double> (hitVoices[1].gain.load (std::memory_order_relaxed)));
    prefs->setValue ("uplifterVolume",
                     static_cast<double> (hitVoices[2].gain.load (std::memory_order_relaxed)));
    prefs->setValue ("riserVolume",
                     static_cast<double> (hitVoices[3].gain.load (std::memory_order_relaxed)));
    for (int i = 0; i < 4; ++i)
        prefs->setValue (kHitPrefKeys[i], hitVoices[i].selected.load (std::memory_order_relaxed));
    prefs->setValue ("inputGain",
                     static_cast<double> (engine.settings().inputGain.load()));
    prefs->setValue ("reverbAmount",
                     static_cast<double> (engine.settings().reverbAmount.load()));
    prefs->setValue ("swing",
                     engine.settings().swing.load() > 0.5f ? 1.0 : 0.0);
    prefs->setValue ("intensity", static_cast<double> (engine.settings().intensity.load()));
    prefs->setValue ("tempoFollow", engine.settings().tempoFollow.load());
    prefs->setValue ("userBpm", static_cast<double> (engine.settings().userBpm.load()));

    if (flush)
        prefs->saveIfNeeded();
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
    savePrefs();
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
        b.setColour (juce::TextButton::buttonColourId, ink());
        b.setColour (juce::TextButton::textColourOffId, text());
    };
    paint (halveButton, mine && oct < 0);
    paint (doubleButton, mine && oct > 0);
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
        b.setColour (juce::TextButton::textColourOffId, text());
    };
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

void MainComponent::applyStyleAuto (bool on)
{
    engine.settings().grooveAuto.store (on);
    refreshStyleButtons();
    savePrefs();
}

void MainComponent::refreshBarButton()
{
    // The button has one function and one label: it declares the one *here*.
    // It lights while the count is the listener's (a tap declares the one too,
    // so this reads the engine back rather than trusting the last press), but
    // the label does not change and a press never unlocks or nudges.
    // See docs/TODO.md item 13.
    const bool locked = engine.settings().barLocked.load();
    barButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("L'1 \u00e8 QUI")));
    barButton.setToggleState (locked, juce::dontSendNotification);
    barButton.setColour (juce::TextButton::buttonColourId, ink());
    barButton.setColour (juce::TextButton::textColourOffId, text());
}

void MainComponent::refreshStyleButtons()
{
    styleSelect.refresh();
    if (styleMenu.isOpen())
        styleMenu.resized();

    auto paint = [] (juce::TextButton& b, bool on, bool detected)
    {
        b.setToggleState (on, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonColourId, ink());
        b.setColour (juce::TextButton::textColourOffId,
                     detected && ! on ? stateLocked() : text());
    };

    // Lit while it is on; the text says what it is doing right now, because
    // "the part went quiet" is the kind of thing a player wants confirmed
    // rather than wondered about.
    const bool followDyn = engine.settings().dynamicsFollow.load();
    dynamicsButton.setButtonText (! followDyn ? "DINAMICA"
                                              : (snap.standingDown ? "IN ASCOLTO"
                                                                   : "DINAMICA"));
    paint (dynamicsButton, followDyn, followDyn && snap.standingDown);
}

void MainComponent::applyShakerNatural (bool on)
{
    engine.settings().shakerNatural.store (on);
    refreshNaturalButton();
    savePrefs();
}

void MainComponent::applyStartImmediately (bool on)
{
    engine.settings().startImmediately.store (on);
    refreshStartNowButton();
    savePrefs();
}

void MainComponent::refreshStartNowButton()
{
    const bool on = engine.settings().startImmediately.load();
    startNowButton.setToggleState (on, juce::dontSendNotification);
    startNowButton.setColour (juce::TextButton::buttonColourId, ink());
    startNowButton.setColour (juce::TextButton::textColourOffId, text());
}

void MainComponent::refreshNaturalButton()
{
    const bool on = engine.settings().shakerNatural.load();
    naturalButton.setToggleState (on, juce::dontSendNotification);
    naturalButton.setColour (juce::TextButton::buttonColourId, ink());
    naturalButton.setColour (juce::TextButton::textColourOffId, text());
}

void MainComponent::applySwing (bool on)
{
    // Straight or swung, and nothing between: a percussionist does not play 37%
    // of a shuffle. The engine still takes a 0..1 amount because the warp is
    // written in terms of it, so what the switch does is stop offering the
    // values nobody wants - and then hold the one value that is worth having.
    //
    // Which is not the full triplet. Measured on the reference this was tuned
    // against (an Afrobeats shaker at 106 BPM, `docs/TODO.md` item 7), the
    // sixteenth inside each eighth lands at **61.6%** of it - 57.4% in one
    // eighth and 65.7% in the other, because it is a person playing - against
    // 50% straight and 66.7% for the triplet. 0.65 puts the written position at
    // 60.8% and, with the default humanize on top, renders at about 63%: inside
    // the reference's own spread, where the full triplet sits above all of it.
    //
    // Chasing the 61.6% to the decimal would be false precision - the two halves
    // of the reference's own bar disagree by eight points. This is the value a
    // listener chose with the three renders in front of them, not a fit.
    constexpr float kSwingOnAmount = 0.65f;
    engine.settings().swing.store (on ? kSwingOnAmount : 0.0f);
    refreshSwingButton();
    savePrefs();
}

void MainComponent::refreshSwingButton()
{
    const bool on = engine.settings().swing.load() > 0.5f;
    swingButton.setToggleState (on, juce::dontSendNotification);
    swingButton.setColour (juce::TextButton::buttonColourId, ink());
    swingButton.setColour (juce::TextButton::textColourOffId, text());
}

void MainComponent::updateBeatDots()
{
    if (layoutFading)
    {
        if (layoutFadeAmount() >= 1.0f)
            layoutFading = false;
        repaint();
    }
    // The clock's bar phase is the position being rendered; the stroke on it
    // is heard after the output path and the attack lead the clock runs
    // ahead by. Same in BRANO and with a live band: in BRANO the song shares
    // that output path, live the band is already in the room.
    const float bpm = juce::jmax (40.0f, snap.bpm);
    const float delayBars = (outputLatencyMs + engine.attackLeadMs()) * 0.001f
                            * bpm / 60.0f * 0.25f;
    float bar = engine.clockBarPhase() - delayBars;
    bar -= std::floor (bar);
    const int beat = juce::jlimit (0, 3, static_cast<int> (bar * 4.0f));
    {
        const float frac = bar * 4.0f - std::floor (bar * 4.0f);
        const float pulse = userWantsArmed ? std::exp (-frac * 3.2f) : 0.0f;
        if (std::abs (pulse - armedPulse) > 0.02f || (pulse == 0.0f && armedPulse != 0.0f))
        {
            armedPulse = pulse;
            startButton.getProperties().set ("pulse", static_cast<double> (pulse));
            startButton.repaint();
        }
    }
    if (beat != dotBeat)
    {
        dotBeat = beat;
        if (! beatStrip.isEmpty())
            repaint (beatStrip.expanded (8));
    }
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
        outputLatencyMs = sr > 0.0 ? static_cast<float> ((outL + buf) * 1000.0 / sr) : 0.0f;
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

    const int scratchSize = juce::jmax (samplesPerBlockExpected * 4, 8192);
    if (inputScratch.getNumSamples() < scratchSize || inputScratch.getNumChannels() < 8)
        inputScratch.setSize (8, scratchSize, false, false, true);
    if (trackScratch.getNumSamples() < scratchSize || trackScratch.getNumChannels() < 2)
        trackScratch.setSize (2, scratchSize, false, false, true);
    trackTransport.prepareToPlay (scratchSize, sr);
    prepareHits (sr);
    // Same clock, analysis still alive: skip prepare(). It zeros the leak
    // ring, resets BeatTracker (the clock) and start() then clearVoices()
    // every sounding stroke - the crack on a Split View / window reset.
    const bool sameClock = ! forceEnginePrepare && engine.isPreparedFor (sr);
    forceEnginePrepare = false;
    if (! sameClock)
        engine.prepare (sr, juce::jmax (samplesPerBlockExpected * 2, 2048), 2);
    if (userWantsArmed && ! sameClock)
        engine.start();
    applyLatencyFromDevice();
    applyInputProcessing();
    audioReady = true;
}

void MainComponent::prepareHits (double sampleRate)
{
    if (sampleRate < 8000.0)
        return;
    if (std::abs (hitRate - sampleRate) < 1.0 && hitSamples[0].getNumSamples() > 0)
        return;
    hitRate = sampleRate;

   #if defined (VP_HAS_HIT_SAMPLES) && VP_HAS_HIT_SAMPLES
    auto load = [sampleRate] (juce::AudioBuffer<float>& pcm, const char* data, int size)
    {
        pcm.setSize (0, 0);
        if (data == nullptr || size <= 0)
            return;
        auto* stream = new juce::MemoryInputStream (data, static_cast<size_t> (size), false);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (stream, true));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return;

        const int srcN = static_cast<int> (reader->lengthInSamples);
        juce::AudioBuffer<float> src (static_cast<int> (reader->numChannels), srcN + 4);
        src.clear();
        reader->read (&src, 0, srcN, 0, true, true);
        const double step = reader->sampleRate / sampleRate;
        const int outN = juce::jmax (1, static_cast<int> (std::ceil (srcN / step)));
        pcm.setSize (src.getNumChannels(), outN);
        for (int c = 0; c < src.getNumChannels(); ++c)
        {
            juce::LagrangeInterpolator interp;
            interp.reset();
            interp.process (step, src.getReadPointer (c), pcm.getWritePointer (c), outN);
        }
    };

    for (int i = 0; i < kHitSampleCount; ++i)
    {
        int size = 0;
        const char* data = VpHitData::getNamedResource (kHitResources[i], size);
        load (hitSamples[i], data, size);
    }
    for (auto& voice : hitVoices)
        voice.pos = 0;
   #else
    juce::ignoreUnused (sampleRate);
   #endif
}

void MainComponent::mixHits (float* const* outs, int numChannels, int numSamples) noexcept
{
    if (outs == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    for (auto& voice : hitVoices)
    {
        const uint32_t req = voice.request.load (std::memory_order_acquire);
        if (req != voice.playing)
        {
            voice.playing = req;
            voice.pos = 0;
            voice.playingSample = juce::jlimit (0, kHitSampleCount - 1,
                voice.selected.load (std::memory_order_acquire));
        }
        const auto& pcm = hitSamples[voice.playingSample];
        const int total = pcm.getNumSamples();
        if (voice.playing == 0 || total <= 0 || voice.pos >= total)
        {
            voice.sounding.store (false, std::memory_order_relaxed);
            continue;
        }
        voice.sounding.store (true, std::memory_order_relaxed);

        const int n = juce::jmin (numSamples, total - voice.pos);
        const int srcCh = pcm.getNumChannels();
        for (int c = 0; c < numChannels; ++c)
        {
            if (outs[c] == nullptr)
                continue;
            const float* src = pcm.getReadPointer (juce::jmin (c, srcCh - 1), voice.pos);
            juce::FloatVectorOperations::addWithMultiply (
                outs[c], src, voice.gain.load (std::memory_order_relaxed), n);
        }
        voice.pos += n;
        if (voice.pos >= total)
            voice.sounding.store (false, std::memory_order_relaxed);
    }
}

void MainComponent::releaseResources()
{
    for (auto& voice : hitVoices)
        voice.sounding.store (false, std::memory_order_relaxed);
    trackTransport.releaseResources();
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
    const int scratchN = inputScratch.getNumSamples();
    const int trackN = trackScratch.getNumSamples();
    const bool directFile = internalTrackSelected();
    const int chunkMax = directFile ? juce::jmin (scratchN, trackN) : scratchN;

    if (n <= 0)
        return;

    // A host is entitled to grow the IO callback past what prepareToPlay last
    // announced - Split View and a window reset both do it. The old path
    // copied min(n, scratch) and silenced the tail, which is a dropout.
    // Chunk through the existing scratch; do not allocate here.
    if (chunkMax <= 0 || count <= 0)
    {
        buffer->clear (start, n);
        audioBlocks.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    int done = 0;
    while (done < n)
    {
        const int chunk = juce::jmin (n - done, chunkMax);
        const int at = start + done;

        if (directFile)
        {
            trackScratch.clear();
            trackTransport.getNextAudioBlock ({ &trackScratch, 0, chunk });
            if (trackTransport.isPlaying())
            {
                float mag = 0.0f;
                for (int c = 0; c < trackScratch.getNumChannels(); ++c)
                    mag = juce::jmax (mag, trackScratch.getMagnitude (c, 0, chunk));
                const double pos = trackTransport.getCurrentPosition();
                if (mag == 0.0f && trackLastBlockHadSound && pos > 1.0
                    && pos < trackTransport.getLengthInSeconds() - 1.0)
                    trackDropouts.fetch_add (1, std::memory_order_relaxed);
                trackLastBlockHadSound = mag > 0.0f;
            }
        }
        else
        {
            for (int c = 0; c < count; ++c)
                inputScratch.copyFrom (c, 0, *buffer, c, at, chunk);
        }

        const float* inPtrs[8] {};
        float* outPtrs[8] {};
        const int used = directFile ? juce::jmin (count, trackScratch.getNumChannels())
                                    : juce::jmin (count, 8);

        for (int c = 0; c < used; ++c)
        {
            inPtrs[c] = directFile ? trackScratch.getReadPointer (c)
                                   : inputScratch.getReadPointer (c);
            outPtrs[c] = buffer->getWritePointer (c, at);
        }

        engine.process (inPtrs, used, outPtrs, used, chunk);
        mixHits (outPtrs, used, chunk);

        // The tracker gets the unattenuated file above. Playback keeps headroom for
        // the generated percussion; this affects only the BRANO path.
        if (directFile)
            for (int c = 0; c < nCh; ++c)
                buffer->addFrom (c, at, trackScratch,
                                 juce::jmin (c, trackScratch.getNumChannels() - 1),
                                 0, chunk, 0.70f);

        done += chunk;
    }

   #if JUCE_DEBUG && JUCE_IOS // VPDIAG
    {
        float pk = 0.0f;
        for (int c = 0; c < nCh; ++c)
            pk = juce::jmax (pk, buffer->getMagnitude (c, start, n));
        if (pk > diagOutPeak.load (std::memory_order_relaxed))
            diagOutPeak.store (pk, std::memory_order_relaxed);
    }
   #endif
    audioBlocks.fetch_add (1, std::memory_order_relaxed);
}

void MainComponent::timerCallback()
{
    snap = engine.snapshot();
    if (restartLogPending
        && juce::Time::getMillisecondCounterHiRes() - restartWallMs > 4000.0)
    {
        restartLogPending = false;
        writeRestartLog();
    }
    // VPLAG: once every 5 s while the part sounds - is the analysis keeping up
    // over a long set (docs/TODO.md item 74)? lead = the projection actually
    // applied, coda = audio still waiting for the worker. Both should stay
    // flat; if they climb, the device is falling behind.
    {
        static uint32_t lastNudges = 0;
        if (snap.phaseNudgeCount != lastNudges)
        {
            lastNudges = snap.phaseNudgeCount;
            juce::Logger::outputDebugString ("VPNUDGE ricentro n." + juce::String (static_cast<int> (lastNudges))
                 + "  bpm " + juce::String (snap.bpm, 1));
        }
    }
    {
        static int lagTicks = 0;
        static const juce::uint32 lagStartMs = juce::Time::getMillisecondCounter();
        if (snap.percussionAudible && ++lagTicks >= 75)
        {
            lagTicks = 0;
            const double sr = snap.sampleRate > 0.0 ? snap.sampleRate : 48000.0;
            const int up = static_cast<int> ((juce::Time::getMillisecondCounter() - lagStartMs) / 1000);
            const float beatMs = 60000.0f / juce::jmax (40.0f, snap.bpm);
            auto* lagDevice = deviceManager.getCurrentAudioDevice();
            juce::Logger::outputDebugString ("VPLAG t " + juce::String (up / 60) + ":"
                 + juce::String (up % 60).paddedLeft ('0', 2)
                 + "  lead " + juce::String (snap.leadMs, 0) + " ms"
                 + "  coda " + juce::String (snap.analysisBacklog * 1000.0 / sr, 0) + " ms"
                 + "  callback " + juce::String (snap.callbackMs, 2) + " ms"
                 + "  rete " + juce::String (engine.analysisInferMs(), 2) + " ms"
                 + (engine.analysisUsesCoreMl() ? " CoreML" : " CPU")
                 + "  analisi " + juce::String (engine.analysisLoadPercent(), 1) + "%"
                 + "  buchi " + juce::String (snap.analysisGaps)
                 + "  restart " + juce::String (snap.analysisRestarts)
                 + "  bpm " + juce::String (snap.bpm, 1)
                 + "  clock " + juce::String (snap.clockBpm, 1)
                 + "  conf " + juce::String (snap.confidence, 2)
                 + "  fase " + juce::String (snap.phaseErrorBeats * beatMs, 0) + " ms"
                 + "  ricentri " + juce::String (static_cast<int> (snap.phaseNudgeCount))
                 + "  uscita " + juce::String (outputLatencyMs, 0) + " ms"
                 + "  xrun " + juce::String (lagDevice != nullptr ? lagDevice->getXRunCount() : -1)
                 + "  tagli " + juce::String (engine.hardSteals())
                 + "  vuoti brano " + juce::String (static_cast<int> (trackDropouts.load (std::memory_order_relaxed))));
        }
    }
   #if JUCE_DEBUG && JUCE_IOS // VPDIAG temporaneo: crack/silenzio al ridimensionamento (TODO item 56)
    {
        static int diagTicks = 0;
        static uint32_t diagBlocks = 0;
        if (++diagTicks >= 15)
        {
            diagTicks = 0;
            auto* d = deviceManager.getCurrentAudioDevice();
            const uint32_t b = audioBlocks.load (std::memory_order_relaxed);
            juce::Logger::outputDebugString ("VPDIAG sess " + juce::String (vp::sessionSampleRate())
                 + " / " + juce::String (vp::sessionBufferFrames())
                 + "  dev " + juce::String (d != nullptr ? d->getCurrentSampleRate() : 0.0)
                 + " / " + juce::String (d != nullptr ? d->getCurrentBufferSizeSamples() : 0)
                 + "  xrun " + juce::String (d != nullptr ? d->getXRunCount() : -1)
                 + "  play " + juce::String (d != nullptr && d->isPlaying() ? 1 : 0)
                 + "  ready " + juce::String (audioReady ? 1 : 0)
                 + "  blocchi/s " + juce::String ((int) (b - diagBlocks))
                 + "  outpk " + juce::String (diagOutPeak.exchange (0.0f), 4)
                 + "  rebuild " + juce::String (deviceRebuilds));
            diagBlocks = b;
        }
    }
   #endif

    // Both directions ease, and at the same rate: the orb is a position,
    // so rushing it one way and lagging the other would read as a bias.
    // At 15 Hz a factor of 0.22 is most of the way across in about a third
    // of a second — a slide, not a snap, and not so slow that a real lean
    // arrives after the bar has already moved on.
    {
        const auto bloom = tempoBloomFor (snap);
        tempoBloomLead += (bloom.lead - tempoBloomLead) * 0.22f;
        tempoBloomAmount += (bloom.amount - tempoBloomAmount) * 0.35f;

        // The state colour and its bloom fade rather than switch.
        const auto target = stateColour (snap.followBar);
        const bool neutral = snap.followBar == vp::FollowBar::ready
                          || snap.followBar == vp::FollowBar::paused;
        stateSmooth = stateSmooth.getAlpha() == 0 ? target : stateSmooth.interpolatedWith (target, 0.10f);
        // Green is earned: a locked tracker with low confidence blooms
        // half-strength, so a drifting grid reads before it turns amber.
        heroConf += (juce::jlimit (0.0f, 1.0f, snap.confidence) - heroConf) * 0.10f;
        const float strength = stateIsHot (snap.followBar)
                                   ? juce::jlimit (0.40f, 1.0f, 0.40f + heroConf)
                                   : 1.0f;
        heroBloomAmt += ((neutral ? 0.0f : strength) - heroBloomAmt) * 0.10f;
        tapAlignFlash *= 0.90f;
    }

    // iOS hands over the safe area after the first resized() has already run,
    // and it changes again on rotation and on a Split View drag. Nothing calls
    // resized() for it, so the controls stay where they were placed when there
    // was no notch while the painting - which reads the insets when it runs -
    // moves down without them. That is one bug wearing three hats: SETUP under
    // the status bar, the octave buttons off the tempo, and "L'1 e QUI" on top
    // of the dots. Lay out again the moment the number changes.
    if (const auto safe = effectiveSafeArea(); safe != laidOutSafeArea)
    {
        resized();
        repaint();
    }

    // Peak hold with a slow release. The raw block peak of a band is a
    // flickering thing at fifteen frames a second - a snare hit and the gap
    // after it are 20 dB apart - and a bar that flickers cannot be read against
    // a target band. Rise instantly so a transient is never missed, fall about
    // 20 dB a second so what is on screen is the level of the *playing*, not of
    // the last buffer.
    const float peak = juce::jmax (0.0f, snap.inputPeak);
    micHold = peak > micHold ? peak : juce::jmax (peak, micHold * 0.79f);
    inputGainSlider.getProperties().set ("micHold", (double) micHold);
    if (const int micStep = juce::roundToInt (meterPosition (micHold) * 256.0f); micStep != lastMicStep)
    {
        lastMicStep = micStep;
        inputGainSlider.repaint();
    }
    // The page wash follows the level of the playing, not of the last block:
    // eased over about a second (0.06 at 15 Hz) and held to eight steps.
    washEnergy += (juce::jlimit (0.0f, 1.0f, std::sqrt (peak) * 3.2f) - washEnergy) * 0.06f;
    washStep = juce::roundToInt (washEnergy * 8.0f);
    {
        const auto look = micLevelLook (micHold);
        inputGainLabel.setColour (juce::Label::textColourId,
                                  look.amount < 0.02f ? mute() : look.colour);
    }
    if (trackTransport.hasStreamFinished() && trackTransport.isPlaying())
        trackTransport.stop();

    // A backing track can finish after the app entered the background. At that
    // point there is no longer a reason to keep the audio session and AI alive.
    if (appIsSuspended && ! userWantsArmed && ! trackTransport.isPlaying())
    {
        powerDownAudioForBackground();
        return;
    }

    if (! appIsSuspended)
    {
        refreshInternalTrackButtons();
        applyLatencyFromDevice();
    }

    // The correlation is tens of millions of multiplies and has no business on
    // the audio thread, so it is done here, once, when the capture is complete.
    if (! appIsSuspended && engine.latencyMeasurementReady())
    {
        const float ms = engine.finishLatencyMeasurement();
        if (ms > 0.0f)
        {
            savePrefs (true);
            latencyButton.setButtonText ("RT " + juce::String (ms, 1) + " ms");
        }
        else
        {
            // Refused rather than guessed. The commonest cause on a stage is
            // the send not being routed back, so say that rather than a number.
            latencyButton.setButtonText ("NIENTE RITORNO");
        }
        refreshSourceButton();
    }

    // A tap can lock the bar without this button being touched, so the button
    // follows the engine rather than its own last press.
    if (! appIsSuspended && barButton.getToggleState() != snap.barLocked)
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
        auto* device = deviceManager.getCurrentAudioDevice();
        const bool haveDevice = device != nullptr;
        const bool playing = haveDevice && device->isPlaying();
        const bool moving = playing && audioReady && now != seenAudioBlocks;

        if (moving)
        {
            stalledTicks = 0;
            seenAudioBlocks = now;
        }
        else if (haveDevice && ! audioReady)
        {
            // Between close and prepareToPlay, which is a tick or two - the
            // start is synchronous, so `audioReady` is normally back before the
            // next timer fires.
            //
            // It used to be excused with no bound at all, and that is a state
            // the app can sit in for ever. JUCE's AudioSourcePlayer calls
            // getNextAudioBlock from the device callback whether or not
            // audioDeviceAboutToStart has run; with `inputScratch` still empty
            // `nCopy` is zero, so every block is cleared and `audioBlocks`
            // keeps counting. The device looks alive, the watchdog is told this
            // is not a stall, and nothing is heard. The only way out was to
            // change the clock by hand and put it back - which fixes it because
            // it *reopens the device*, not because of the rate: on this desktop
            // path AUTO writes no rate at all, so the two settings are the same
            // number and the reopen is the whole of the cure.
            //
            // Reported by the listener as "sometimes there is no sound and I
            // have to switch AUTO to 44100 and back". Bounded, the app does that
            // reopen itself after a second.
            if (++stalledTicks >= 12)
                rebuildAudioDevice ("device open but never prepared");
        }
        else if (haveDevice && ! playing)
        {
            // RemoteIO may reject a restart after an iOS interruption. Its
            // callback count can still look recent; the device itself knows
            // that it is no longer playing. Allow transient route changes a
            // second before replacing the failed unit.
            if (++stalledTicks >= 12)
                rebuildAudioDevice ("audio device stopped after interruption");
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
   #if JUCE_IOS
    if (resizeSettleTicks > 0 && --resizeSettleTicks == 0)
    {
        auto* device = deviceManager.getCurrentAudioDevice();
       #if JUCE_DEBUG
        juce::Logger::outputDebugString ("VPDIAG resize xruns "
            + juce::String (resizeXrunsBefore) + " -> "
            + juce::String (device != nullptr ? device->getXRunCount() : -1));
       #endif
        if (! a2dpResizeRecovered && rebuildCooldownTicks == 0
            && device != nullptr && audioReady
            && internalTrackSelected() && trackTransport.isPlaying()
            && vp::sessionOutputIsA2DP()
            && device->getXRunCount() > resizeXrunsBefore)
        {
            // iPadOS can keep calling a RemoteIO unit that plays distorted
            // A2DP audio after resize xruns. CLOCK/BUFFER fixes it by opening
            // a fresh unit; do that once after the gesture settles, keeping
            // the same rate so the percussion clock is not prepared again.
            lastRebuildWhy = "AirPods resize xrun";
            ++deviceRebuilds;
            rebuildCooldownTicks = 30;
            a2dpResizeRecovered = true;
            // Rate 0 makes JUCE reopen the existing device, then choose its
            // current rate again. No second device probe or tempo reset.
            auto setup = deviceManager.getAudioDeviceSetup();
            setup.sampleRate = 0.0;
            const auto err = deviceManager.setAudioDeviceSetup (setup, false);
            juce::ignoreUnused (err);
            seenAudioBlocks = audioBlocks.load (std::memory_order_relaxed);
        }
    }
   #endif
    // Hidden controls and a full-window paint at 15 Hz buy nothing in the
    // background. The timer remains alive only as the audio-device watchdog
    // while a performance is intentionally continuing.
    if (appIsSuspended)
        return;

    if (tapFlash > 0)
        --tapFlash;
    processTrackWaveform();
    if (trackReader != nullptr)
        trackWaveform.repaint();
    refreshStartButton();
    refreshTempoModeButtons();
    if (engine.settings().grooveAuto.load())
        refreshStyleButtons();
    {
        juce::Slider* hitKnobs[] = { &absorbVolSlider, &hornVolSlider, &uplifterVolSlider, &riserVolSlider };
        for (int i = 0; i < 4; ++i)
        {
            const bool lit = hitVoices[i].sounding.load (std::memory_order_relaxed);
            if ((bool) hitKnobs[i]->getProperties().getWithDefault ("hitLit", false) != lit)
            {
                hitKnobs[i]->getProperties().set ("hitLit", lit);
                hitKnobs[i]->repaint();
            }
        }
    }

    // Repaint only what moved. A blanket repaint() here redrew the whole
    // window - page gradients, every card, every fader - fifteen times a
    // second on a CPU renderer, with the band silent too. The page key is
    // what paint() draws across the window; the stage key is what paintStage
    // draws. Anything new that paint or paintStage reads from the timer must
    // join its key, or it will only show when something else moves.
    {
        const auto q = [] (float v) { return static_cast<juce::int64> (juce::roundToInt (v * 256.0f)); };
        const std::array<juce::int64, 6> page {
            washStep, static_cast<juce::int64> (stateSmooth.getARGB()),
            stateIsHot (snap.followBar) ? 1 : 0, tapFlash > 0 ? 1 : 0,
            gDarkMode ? 1 : 0, debugOpen ? 1 : 0
        };
        const std::array<juce::int64, 15> stage {
            static_cast<juce::int64> (snap.followBar), juce::roundToInt (snap.bpm * 10.0f),
            q (tempoBloomLead), q (tempoBloomAmount), q (heroBloomAmt), q (tapAlignFlash),
            snap.tempoFollow ? 1 : 0, snap.levelSettled ? 1 : 0, snap.tempoRegime,
            snap.tempoOctave, snap.tempoOctaveAuto ? 1 : 0, snap.barDeclared ? 1 : 0,
            snap.grooveStyle, q (snap.grooveStyleConfidence),
            engine.settings().grooveAuto.load() ? 1 : 0
        };
        // The debug panel prints live numbers over the stage: repaint it all.
        if (debugOpen || page != lastPageKey)
            repaint();
        else if (stage != lastStageKey)
            repaint (stageArea().expanded (24));
        lastPageKey = page;
        lastStageKey = stage;
    }
    // The settings page shows live numbers (latency, tempo, part).
    if (settingsOverlay.isVisible())
        settingsOverlay.repaint();
}
