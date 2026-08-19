#include "UI/MainComponent.h"
#include "Platform/IosMicPermission.h"

#include <cmath>
#include <functional>

namespace
{
    juce::Colour bg()     { return juce::Colour (0xff0b0d10); }
    juce::Colour panel()  { return juce::Colour (0xff161a20); }
    juce::Colour amber()  { return juce::Colour (0xfff5a623); }
    juce::Colour mute()   { return juce::Colour (0xff8a9099); }
    juce::Colour live()   { return juce::Colour (0xff3dd68c); }
    juce::Colour warn()   { return juce::Colour (0xffffc14d); }
    juce::Colour accent() { return juce::Colour (0xff4da3ff); }

    juce::Colour stateColour (vp::FollowBar b)
    {
        using B = vp::FollowBar;
        switch (b)
        {
            case B::following:       return live();
            case B::followingListen: return live();
            case B::calibrating:     return warn();
            case B::listening:       return accent();
            case B::tapAlign:        return amber();
            case B::waitBeat:        return amber();
            case B::weakFollow:      return warn();
            case B::recalin:         return juce::Colour (0xffff8a4d);
            case B::paused:          return mute();
            case B::ready:           return mute();
        }
        return mute();
    }
}

MainComponent::MainComponent()
{
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

    auto setupBtn = [this] (juce::TextButton& b, juce::Colour fill)
    {
        addAndMakeVisible (b);
        b.setColour (juce::TextButton::buttonColourId, fill);
        b.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        b.setColour (juce::TextButton::buttonOnColourId, fill.brighter (0.2f));
    };

    setupBtn (startButton, juce::Colour (0xff1f8a4c));
    setupBtn (stopButton, juce::Colour (0xff6b3030));
    setupBtn (tapButton, juce::Colour (0xffc47a12));
    setupBtn (shakerButton, juce::Colour (0xff2a333e));
    setupBtn (debugButton, juce::Colour (0xff22262c));
    setupBtn (clickButton, juce::Colour (0xff22262c));
    setupBtn (sourceButton, juce::Colour (0xff2a333e));
    setupBtn (congasButton, juce::Colour (0xff2a333e));
    setupBtn (styleAuto, juce::Colour (0xff2a333e));
    setupBtn (styleMarcha, juce::Colour (0xff2a333e));
    setupBtn (styleRock, juce::Colour (0xff2a333e));
    setupBtn (styleDance, juce::Colour (0xff2a333e));
    setupBtn (stylePop, juce::Colour (0xff2a333e));
    setupBtn (subAuto, juce::Colour (0xff2a333e));
    setupBtn (halveButton, juce::Colour (0xff232a33));
    setupBtn (doubleButton, juce::Colour (0xff232a33));

    // Half and double. The one part of the metrical level that the signal does
    // not decide - full eighths under a slow tempo read as well at the double -
    // is decided here instead, by the person listening, in one tap.
    halveButton.onClick = [this]
    {
        applyTempoOctave (engine.settings().tempoOctave.load() < 0 ? 0 : -1);
    };
    doubleButton.onClick = [this]
    {
        applyTempoOctave (engine.settings().tempoOctave.load() > 0 ? 0 : 1);
    };
    setupBtn (sub4, juce::Colour (0xff2a333e));
    setupBtn (sub8, juce::Colour (0xff2a333e));
    setupBtn (sub16, juce::Colour (0xff2a333e));

    startButton.onClick = [this] { startPressed(); };
    stopButton.onClick = [this] { stopPressed(); };
    tapButton.setLookAndFeel (&tapLaf);
    tapButton.onClick = [this] { tapPressed(); };
    shakerButton.onClick = [this]
    {
        const bool on = ! engine.settings().shakerEnabled.load();
        engine.settings().shakerEnabled.store (on);
        shakerButton.setButtonText (on ? "SHAKER  ON" : "SHAKER  OFF");
    };
    debugButton.onClick = [this] { debugOpen = ! debugOpen; repaint(); };
    clickButton.onClick = [this]
    {
        static bool click = false;
        click = ! click;
        engine.setClickInjectEnabled (click);
        engine.setClickInjectBpm (120.0f);
        clickButton.setButtonText (click ? "CLICK  ON" : "CLICK TEST");
    };
    sourceButton.onClick = [this]
    {
        const bool speaker = engine.settings().followSource.load()
                             != static_cast<int> (vp::FollowSource::speaker);
        engine.settings().followSource.store (static_cast<int> (
            speaker ? vp::FollowSource::speaker : vp::FollowSource::kitMic));
        sourceButton.setButtonText (speaker ? "IPAD" : "MIXER");
        applyFollowSource();
    };

    congasButton.onClick = [this]
    {
        const bool on = ! engine.settings().congasEnabled.load();
        engine.settings().congasEnabled.store (on);
        congasButton.setButtonText (on ? "CONGAS  ON" : "CONGAS  OFF");
    };

    styleAuto.onClick   = [this] { applyStyleAuto (! engine.settings().grooveAuto.load()); };
    styleMarcha.onClick = [this] { applyStyle (vp::GrooveStyle::marcha); };
    styleRock.onClick   = [this] { applyStyle (vp::GrooveStyle::rock); };
    styleDance.onClick  = [this] { applyStyle (vp::GrooveStyle::dance); };
    stylePop.onClick    = [this] { applyStyle (vp::GrooveStyle::pop); };

    subAuto.onClick = [this] { applySubdivision (vp::Subdivision::autoDetect); };
    sub4.onClick    = [this] { applySubdivision (vp::Subdivision::quarter); };
    sub8.onClick    = [this] { applySubdivision (vp::Subdivision::eighth); };
    sub16.onClick   = [this] { applySubdivision (vp::Subdivision::sixteenth); };

    addAndMakeVisible (reverbLabel);
    reverbLabel.setJustificationType (juce::Justification::centredLeft);
    reverbLabel.setColour (juce::Label::textColourId, mute());
    reverbLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));

    addAndMakeVisible (reverbSlider);
    reverbSlider.setLookAndFeel (&sliderLaf);
    reverbSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    reverbSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    reverbSlider.setRange (0.0, 1.0, 0.01);
    reverbSlider.setValue (0.30, juce::dontSendNotification);
    reverbSlider.onValueChange = [this]
    {
        engine.settings().reverbAmount.store (static_cast<float> (reverbSlider.getValue()));
    };

    auto setupTrim = [this] (juce::Slider& s, juce::Label& lab, double initial,
                             std::function<void (float)> apply)
    {
        addAndMakeVisible (lab);
        lab.setJustificationType (juce::Justification::centredLeft);
        lab.setColour (juce::Label::textColourId, mute());
        lab.setFont (juce::FontOptions (13.0f, juce::Font::bold));

        addAndMakeVisible (s);
        s.setLookAndFeel (&sliderLaf);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (0.0, 1.0, 0.01);
        s.setValue (initial, juce::dontSendNotification);
        auto* sp = &s;
        s.onValueChange = [sp, apply] { apply (static_cast<float> (sp->getValue())); };
    };

    setupTrim (swingSlider, swingLabel, 0.00,
               [this] (float v) { engine.settings().swing.store (v); });
    setupTrim (intensitySlider, intensityLabel, 0.50,
               [this] (float v) { engine.settings().intensity.store (v); });

   #if JUCE_IOS
    engine.settings().followSource.store (static_cast<int> (vp::FollowSource::speaker));
    sourceButton.setButtonText ("IPAD");
   #else
    engine.settings().followSource.store (static_cast<int> (vp::FollowSource::kitMic));
    sourceButton.setButtonText ("MIXER");
   #endif
    // 0.00 is a sequencer: dead on the grid, every stroke the same weight.
    // A percussionist is neither, and this is the single setting that decides
    // which of the two the app sounds like.
    engine.settings().humanization.store (0.35f);
    engine.settings().intensity.store (0.50f);
    engine.settings().swing.store (0.00f);
    engine.settings().masterVolume.store (0.90f);
    engine.settings().followStrength.store (static_cast<int> (vp::FollowStrength::high));
    engine.settings().subdivision.store (static_cast<int> (vp::Subdivision::eighth));
    engine.settings().reverbAmount.store (0.30f);
    refreshStyleButtons();
    refreshOctaveButtons();
    refreshSubdivisionButtons();

    startTimerHz (15);

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
    tapButton.setLookAndFeel (nullptr);
    reverbSlider.setLookAndFeel (nullptr);
    swingSlider.setLookAndFeel (nullptr);
    intensitySlider.setLookAndFeel (nullptr);
    stopTimer();
    shutdownAudio();
}

void MainComponent::startPressed()
{
    ensureMicrophone();
    userWantsArmed = true;
    engine.start();
}

void MainComponent::stopPressed()
{
    userWantsArmed = false;
    engine.stop();
}

void MainComponent::tapPressed()
{
    ensureMicrophone();
    engine.tap();
    tapFlash = 8;
    refreshTapButton();
}

void MainComponent::refreshTapButton()
{
    const bool lit = tapFlash > 0 || snap.followBar == vp::FollowBar::tapAlign;
    tapButton.setColour (juce::TextButton::buttonColourId,
                         lit ? juce::Colour (0xffe8a21a) : juce::Colour (0xffc47a12));
    tapButton.setButtonText ("TAP");
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

void MainComponent::openAudioDevice (bool granted)
{
    micGranted = granted;
    const int ins = granted ? 2 : 0;

    if (! audioOpened)
    {
        audioOpened = true;
        setAudioChannels (ins, 2);
        applyHardwareAudioSetup (ins);
        return;
    }

    auto* dev = deviceManager.getCurrentAudioDevice();
    const int nIn = dev != nullptr ? dev->getActiveInputChannels().countNumberOfSetBits() : 0;
    if (ins > 0 && nIn <= 0)
        applyHardwareAudioSetup (ins);
}

void MainComponent::applyHardwareAudioSetup (int ins)
{
   #if JUCE_IOS
    vp::configurePlaybackSession();
   #endif

    auto setup = deviceManager.getAudioDeviceSetup();
    double sr = setup.sampleRate;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        sr = dev->getCurrentSampleRate();
    const double hw = vp::sessionSampleRate();
    if (sr < 24000.0)
        sr = hw > 24000.0 ? hw : 48000.0;
    if (sr < 8000.0 || sr > 192000.0)
        sr = 48000.0;

    setup.sampleRate = sr;
    setup.bufferSize = 256;
    setup.inputChannels.clear();
    if (ins > 0)
        setup.inputChannels.setRange (0, ins, true);
    setup.outputChannels.clear();
    setup.outputChannels.setRange (0, 2, true);
    setup.useDefaultInputChannels = ins <= 0;
    setup.useDefaultOutputChannels = false;
    const auto err = deviceManager.setAudioDeviceSetup (setup, true);
    juce::ignoreUnused (err);
    applyFollowSource();
}

void MainComponent::applyFollowSource()
{
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        dev->setAudioPreprocessingEnabled (false);
}

void MainComponent::applySubdivision (vp::Subdivision s)
{
    engine.settings().subdivision.store (static_cast<int> (s));
    refreshSubdivisionButtons();
}

void MainComponent::refreshSubdivisionButtons()
{
    const int cur = engine.settings().subdivision.load();
    auto paint = [cur] (juce::TextButton& b, int v)
    {
        const bool on = cur == v;
        b.setColour (juce::TextButton::buttonColourId,
                     on ? juce::Colour (0xff3a4a2e) : juce::Colour (0xff2a333e));
        b.setColour (juce::TextButton::textColourOffId,
                     on ? juce::Colour (0xfff5a623) : juce::Colours::white);
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
}

void MainComponent::applyTempoOctave (int octaves)
{
    engine.settings().tempoOctave.store (juce::jlimit (-1, 1, octaves));
    refreshOctaveButtons();
    repaint();
}

void MainComponent::refreshOctaveButtons()
{
    const int oct = engine.settings().tempoOctave.load();
    auto paint = [] (juce::TextButton& b, bool on)
    {
        b.setColour (juce::TextButton::buttonColourId,
                     on ? juce::Colour (0xff8a5a12) : juce::Colour (0xff232a33));
        b.setColour (juce::TextButton::textColourOffId,
                     on ? juce::Colours::white : juce::Colour (0xff9aa3ad));
    };
    paint (halveButton, oct < 0);
    paint (doubleButton, oct > 0);
}

void MainComponent::applyStyleAuto (bool on)
{
    engine.settings().grooveAuto.store (on);
    refreshStyleButtons();
}

void MainComponent::refreshStyleButtons()
{
    const bool autoOn = engine.settings().grooveAuto.load();
    const int cur = engine.settings().grooveStyle.load();

    auto paint = [] (juce::TextButton& b, bool on, bool detected)
    {
        b.setColour (juce::TextButton::buttonColourId,
                     on ? juce::Colour (0xff3a4a2e) : juce::Colour (0xff2a333e));
        b.setColour (juce::TextButton::textColourOffId,
                     on ? juce::Colour (0xfff5a623)
                        : (detected ? juce::Colour (0xff9fd6a0) : juce::Colours::white));
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
    applyFollowSource();
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
    if (tapFlash > 0)
        --tapFlash;
    refreshTapButton();
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

juce::Rectangle<int> MainComponent::layoutColumn() const
{
    auto r = getLocalBounds();
   #if JUCE_IOS
    r = r.reduced (24, 20).withTrimmedTop (16);
   #else
    r = r.reduced (30, 24);
   #endif
    return r;
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
    // left over is split above and below so the block sits in the middle of
    // whatever space the orientation happens to give it, instead of piling up
    // at the top and leaving a hole - which is what the first pass did.
    const int bpmH = juce::jlimit (72, 156, area.getHeight() / 4);
    const int beatsH = juce::jlimit (52, 96, area.getHeight() / 6);

    const int content = 18 + 6 + 40 + 12 + bpmH + 16 + 20 + 10 + beatsH + 20 + 10 + 10 + 18;
    const int slack = juce::jmax (0, area.getHeight() - content);
    // A third above rather than a half: dead centre in a tall landscape column
    // leaves the tempo sitting below the middle of the screen, and the eye
    // expects the thing it is reading to be in the upper half.
    area.removeFromTop (slack / 3);

    StageRows s;
    s.title = area.removeFromTop (18);
    area.removeFromTop (6);
    s.pill = area.removeFromTop (40).reduced (juce::jmax (0, area.getWidth() / 10), 0);
    area.removeFromTop (12);
    s.bpm = area.removeFromTop (bpmH);
    {
        // A bounded block, centred. Wider than this and the two buttons sit so
        // far from the number that they read as unrelated; narrower and the
        // number has nowhere to go.
        auto block = s.bpm.withSizeKeepingCentre (juce::jmin (s.bpm.getWidth(), 430), bpmH);
        const int octW = juce::jlimit (48, 78, block.getWidth() / 6);
        s.octaveDown = block.removeFromLeft (octW).reduced (0, bpmH / 5);
        s.octaveUp = block.removeFromRight (octW).reduced (0, bpmH / 5);
        s.bpmNumber = block.reduced (8, 0);
    }
    s.bpmLabel = area.removeFromTop (16);
    s.tempoLine = area.removeFromTop (20);
    area.removeFromTop (10);
    s.beats = area.removeFromTop (beatsH);
    s.part = area.removeFromTop (20);
    area.removeFromTop (10);
    s.meter = area.removeFromTop (10).reduced (juce::jmax (0, area.getWidth() / 6), 2);
    s.mic = area.removeFromTop (18);
    return s;
}

juce::Rectangle<int> MainComponent::layoutConsole (juce::Rectangle<int> area)
{
    cards.clearQuick();

    // Proportional, not fixed: the same five cards have to fit an iPad in
    // portrait, the same iPad turned, and a desktop window someone has dragged
    // to an odd size. Fixed row heights are what made the feel controls
    // undroppable in one orientation and impossible in the other.
    const int gap = 10;
    const int titleH = 18;
    auto card = [&] (juce::Rectangle<int> bounds, const char* title)
    {
        cards.add ({ bounds, juce::String (title) });
        return bounds.reduced (12, 10).withTrimmedTop (titleH);
    };

    const int n = area.getHeight();
    // Weights, summing to one. Transport is the biggest thing on the console
    // because it is the thing hit under pressure.
    const int hTransport = juce::roundToInt (static_cast<float> (n) * 0.30f);
    const int hPart      = juce::roundToInt (static_cast<float> (n) * 0.16f);
    const int hInst      = juce::roundToInt (static_cast<float> (n) * 0.22f);

    {
        auto body = card (area.removeFromTop (hTransport), "TRASPORTO");
        auto row = body.removeFromTop (juce::roundToInt (static_cast<float> (body.getHeight()) * 0.55f));
        startButton.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (4));
        stopButton.setBounds (row.reduced (4));
        tapButton.setBounds (body.reduced (4, 3));
        area.removeFromTop (gap);
    }

    {
        auto body = card (area.removeFromTop (hPart), "PARTE");
        const int w = body.getWidth() / 5;
        styleAuto.setBounds (body.removeFromLeft (w).reduced (3));
        styleMarcha.setBounds (body.removeFromLeft (w).reduced (3));
        styleRock.setBounds (body.removeFromLeft (w).reduced (3));
        styleDance.setBounds (body.removeFromLeft (w).reduced (3));
        stylePop.setBounds (body.reduced (3));
        area.removeFromTop (gap);
    }

    {
        auto body = card (area.removeFromTop (hInst), "STRUMENTI");
        auto top = body.removeFromTop (body.getHeight() / 2);
        shakerButton.setBounds (top.removeFromLeft (top.getWidth() / 2).reduced (3));
        congasButton.setBounds (top.reduced (3));
        const int w = body.getWidth() / 4;
        subAuto.setBounds (body.removeFromLeft (w).reduced (3));
        sub4.setBounds (body.removeFromLeft (w).reduced (3));
        sub8.setBounds (body.removeFromLeft (w).reduced (3));
        sub16.setBounds (body.reduced (3));
        area.removeFromTop (gap);
    }

    {
        // Whatever is left goes to feel. Three sliders always fit, because
        // three sliders will squeeze; the alternative was hiding them.
        auto body = card (area, "FEEL");
        const int rowH = body.getHeight() / 3;
        auto slider = [&] (juce::Label& label, juce::Slider& s, juce::Rectangle<int> row)
        {
            label.setBounds (row.removeFromLeft (96).reduced (4, 4));
            s.setBounds (row.reduced (8, 3));
        };
        slider (swingLabel, swingSlider, body.removeFromTop (rowH));
        slider (intensityLabel, intensitySlider, body.removeFromTop (rowH));
        slider (reverbLabel, reverbSlider, body);
    }

    for (auto* c : { &swingSlider, &intensitySlider })
        c->setVisible (true);
    swingLabel.setVisible (true);
    intensityLabel.setVisible (true);
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
    debugButton.setBounds (util.removeFromRight (56).reduced (2));
    util.removeFromRight (6);
    clickButton.setBounds (util.removeFromRight (112).reduced (2));
    util.removeFromRight (6);
    sourceButton.setBounds (util.removeFromRight (96).reduced (2));
    r.removeFromTop (8);
    if (isLandscape())
        r.removeFromLeft (stage.getWidth() + 16);
    else
        r.removeFromTop (stage.getHeight() + 14);

    // The halve/double pair flanks the number it applies to, close enough to
    // read as belonging to it and never overlapping it.
    const auto rows = stageRows (stage);
    halveButton.setBounds (rows.octaveDown);
    doubleButton.setBounds (rows.octaveUp);

    layoutConsole (r);
}

void MainComponent::paintStage (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto rows = stageRows (area);

    g.setColour (mute());
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawFittedText ("VIRTUAL PERCUSSIONIST", rows.title, juce::Justification::centred, 1);

    // What the tracker is doing, in one word, in a colour. This is the line a
    // player glances at between phrases.
    g.setColour (stateColour (snap.followBar));
    g.fillRoundedRectangle (rows.pill.toFloat(), 20.0f);
    g.setColour (juce::Colours::black);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawFittedText (juce::String (juce::CharPointer_UTF8 (vp::toBarString (snap.followBar))),
                      rows.pill, juce::Justification::centred, 1);

    // The tempo, sized to the room it has rather than to a constant, so it is
    // the biggest thing on the screen in portrait and still the biggest thing
    // when the iPad is turned.
    const bool haveBpm = snap.bpm > 40.0f;
    if (haveBpm)
    {
        // Sized against the width by measurement, not by drawFittedText: with
        // one line to work with that squashes to 70% and then puts an ellipsis
        // in, so a five-character tempo in a narrow landscape column came out
        // as "11...". Measure, scale, draw.
        const juce::String text (snap.bpm, 1);
        const float wanted = static_cast<float> (rows.bpm.getHeight()) * 0.92f;
        juce::Font f (juce::FontOptions (wanted, juce::Font::bold));
        const float textW = juce::GlyphArrangement::getStringWidth (f, text);
        const float roomW = static_cast<float> (rows.bpmNumber.getWidth());
        if (textW > roomW && textW > 1.0f)
            f = f.withHeight (wanted * roomW / textW);
        g.setColour (juce::Colours::white);
        g.setFont (f);
        g.drawText (text, rows.bpmNumber, juce::Justification::centred, false);
    }
    else
    {
        // Two drawn bars rather than "--" in the tempo's own font. A hyphen is
        // a hairline a tenth of its em tall, so set at the size the number
        // wants it reads as something broken rather than as a blank waiting to
        // be filled.
        const float barW = static_cast<float> (rows.bpm.getHeight()) * 0.34f;
        const float barH = juce::jmax (6.0f, static_cast<float> (rows.bpm.getHeight()) * 0.10f);
        const float gap = barW * 0.35f;
        const float cx = static_cast<float> (rows.bpmNumber.getCentreX());
        const float cy = static_cast<float> (rows.bpmNumber.getCentreY());
        g.setColour (juce::Colour (0xff2b323b));
        g.fillRoundedRectangle (cx - barW - gap * 0.5f, cy - barH * 0.5f, barW, barH, barH * 0.5f);
        g.fillRoundedRectangle (cx + gap * 0.5f, cy - barH * 0.5f, barW, barH, barH * 0.5f);
    }
    g.setColour (mute());
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawFittedText ("BPM", rows.bpmLabel, juce::Justification::centred, 1);

    // How the tempo is being held. Since the tracking work this is the most
    // useful line on the screen when something looks wrong: a tempo that says
    // FISSO and will not stop moving is a different fault from one that never
    // leaves CERCO.
    const bool held = snap.tempoRegime == 1;
    g.setColour (held ? live() : (snap.tempoRegime == 2 ? warn() : mute()));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    juce::String tempoLine = juce::String (vp::regimeLabel (snap.tempoRegime));
    if (! snap.levelSettled)
        tempoLine += juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  livello provvisorio"));
    if (snap.tempoOctave != 0)
        tempoLine += juce::String (juce::CharPointer_UTF8 (snap.tempoOctave < 0 ? "  \xc2\xb7  a met\xc3\xa0"
                                                                                : "  \xc2\xb7  doppio"));
    g.drawFittedText (tempoLine, rows.tempoLine, juce::Justification::centred, 1);

    // Four beats, the one marked. Big enough to read at arm's length on a
    // stand, which the old 28-pixel dots were not, and lit from the clock
    // rather than from whether a sample happens to be sounding - a player
    // watching the bar wants to see it turn over before START, not after.
    beatStrip = rows.beats;
    {
        const int bandW = juce::jmin (rows.beats.getWidth(), 400);
        const auto band = rows.beats.withSizeKeepingCentre (bandW, rows.beats.getHeight());
        // The lit beat wears a halo of 1.8 radii, so the radius has to leave
        // room for it inside the row - otherwise the glow spills onto the line
        // of text below, which is what it did.
        const float rad = juce::jmin (24.0f, static_cast<float> (rows.beats.getHeight()) * 0.27f);
        const float y = static_cast<float> (band.getCentreY());
        const float step = static_cast<float> (band.getWidth()) / 4.0f;
        const int beatIdx = juce::jlimit (0, 3, static_cast<int> (snap.barPhase * 4.0f));
        const bool running = snap.bpm > 40.0f;
        for (int i = 0; i < 4; ++i)
        {
            const float x = static_cast<float> (band.getX()) + step * (static_cast<float> (i) + 0.5f);
            const bool on = running && beatIdx == i;
            const bool one = i == 0;
            if (on)
            {
                g.setColour ((one ? amber() : live()).withAlpha (0.20f));
                g.fillEllipse (x - rad * 1.8f, y - rad * 1.8f, rad * 3.6f, rad * 3.6f);
            }
            g.setColour (on ? (one ? amber() : live()) : juce::Colour (0xff262d36));
            g.fillEllipse (x - rad, y - rad, rad * 2.0f, rad * 2.0f);
            if (one)
            {
                g.setColour (on ? juce::Colours::black.withAlpha (0.35f) : juce::Colour (0xff3d4854));
                g.drawEllipse (x - rad, y - rad, rad * 2.0f, rad * 2.0f, 2.0f);
            }
        }
    }

    // Which part is playing, and under AUTO how sure the detector is.
    g.setColour (mute());
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawFittedText (juce::String ("PARTE  ")
                          + vp::toString (static_cast<vp::GrooveStyle> (snap.grooveStyle))
                          + (engine.settings().grooveAuto.load()
                                 ? "   (auto " + juce::String (snap.grooveStyleConfidence, 2) + ")"
                                 : juce::String()),
                      rows.part, juce::Justification::centred, 1);

    // Input. A bar and a phrase: enough to tell "the microphone is hearing the
    // room" from "the microphone is hearing nothing", which is the only
    // question the old row of numbers was ever answering.
    g.setColour (juce::Colour (0xff232a33));
    g.fillRoundedRectangle (rows.meter.toFloat(), 4.0f);
    const float level = juce::jlimit (0.0f, 1.0f, std::sqrt (juce::jmax (0.0f, snap.inputPeak)) * 3.2f);
    const bool listening = inputChannels > 0 && micGranted;
    g.setColour (listening ? live() : juce::Colour (0xffff5a5a));
    g.fillRoundedRectangle (rows.meter.toFloat().withWidth (
                                juce::jmax (4.0f, static_cast<float> (rows.meter.getWidth()) * level)), 4.0f);

    g.setColour (mute());
    g.setFont (juce::FontOptions (12.0f));
    const juce::String micText = ! micGranted ? "MICROFONO NEGATO"
                               : (inputChannels <= 0 ? "MICROFONO SPENTO"
                               : (snap.inputPeak > 0.0012f ? "SENTO LA STANZA"
                                                           : "IN ASCOLTO"));
    const juce::String dot (juce::CharPointer_UTF8 ("   \xc2\xb7   "));
    g.drawFittedText (micText + dot + (snap.source == vp::FollowSource::speaker ? "IPAD" : "MIXER")
                          + (snap.aiOnnx ? juce::String() : dot + "AI STUB"),
                      rows.mic, juce::Justification::centred, 1);
}

void MainComponent::paintCards (juce::Graphics& g)
{
    for (const auto& c : cards)
    {
        g.setColour (panel());
        g.fillRoundedRectangle (c.bounds.toFloat(), 14.0f);
        g.setColour (mute());
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawFittedText (c.title, c.bounds.reduced (14, 8).removeFromTop (14),
                          juce::Justification::topLeft, 1);
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (bg());

    paintCards (g);
    paintStage (g, stageArea());

    if (debugOpen)
    {
        auto dbg = getLocalBounds().reduced (24).removeFromTop (300);
        g.setColour (panel().withAlpha (0.96f));
        g.fillRoundedRectangle (dbg.toFloat(), 12.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (13.0f));
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
        lines.add (juce::String (snap.tapLocked ? "tap LOCK" : "tap auto"));
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
