#include "UI/MainComponent.h"
#include "Platform/IosMicPermission.h"

#include <cmath>
#include <functional>

namespace
{
    bool gDarkMode = true;

    juce::Colour bg()      { return gDarkMode ? juce::Colour (0xff050506) : juce::Colour (0xfff5f1f6); }
    juce::Colour panel()   { return gDarkMode ? juce::Colour (0xff0c0c0e) : juce::Colour (0xffffffff); }
    juce::Colour ink()     { return gDarkMode ? juce::Colour (0xff121214) : juce::Colour (0xff211d24); }
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

    void paintRadial (juce::Graphics& g, juce::Point<float> c, float radius,
                      juce::Colour col, float alpha)
    {
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
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::Label::textColourId, mute());
}

juce::Font MainComponent::AppLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return fontUi (juce::jmax (12.0f, (float) buttonHeight * 0.30f));
}

void MainComponent::AppLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                          const juce::Colour& backgroundColour,
                                                          bool, bool shouldDrawButtonAsDown)
{
    drawFlatButton (g, button, backgroundColour, shouldDrawButtonAsDown);
}

int MainComponent::AppLookAndFeel::getSliderThumbRadius (juce::Slider&)
{
    return 15;
}

void MainComponent::AppLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                                      float sliderPos, float, float,
                                                      juce::Slider::SliderStyle, juce::Slider&)
{
    const float trackH = 6.0f;
    const float cy = static_cast<float> (y) + 0.5f * static_cast<float> (height);
    juce::Rectangle<float> track (static_cast<float> (x), cy - trackH * 0.5f,
                                  static_cast<float> (width), trackH);

    g.setColour (text().withAlpha (0.12f));
    g.fillRoundedRectangle (track.expanded (1.0f), 4.0f);
    g.setColour (sliderTrack());
    g.fillRoundedRectangle (track, 3.0f);

    auto filled = track.withWidth (juce::jmax (trackH, sliderPos - static_cast<float> (x)));
    juce::ColourGradient fill (fuchsia().brighter (0.15f), filled.getX(), filled.getY(),
                               fuchsia().darker (0.1f), filled.getRight(), filled.getY(), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (filled, 3.0f);

    const float tr = 15.0f;
    paintRadial (g, { sliderPos, cy }, 28.0f, fuchsia(), 0.35f);
    g.setColour (fuchsia());
    g.drawEllipse (sliderPos - tr, cy - tr, tr * 2.0f, tr * 2.0f, 2.0f);
    g.setColour (juce::Colours::white);
    g.fillEllipse (sliderPos - tr + 3.0f, cy - tr + 3.0f, (tr - 3.0f) * 2.0f, (tr - 3.0f) * 2.0f);
    g.setColour (fuchsia());
    g.fillEllipse (sliderPos - 3.5f, cy - 3.5f, 7.0f, 7.0f);
}

juce::Font MainComponent::TapLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return fontDisplay (juce::jmax (34.0f, (float) buttonHeight * 0.38f));
}

void MainComponent::TapLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                          const juce::Colour& backgroundColour,
                                                          bool, bool shouldDrawButtonAsDown)
{
    drawFlatButton (g, button, backgroundColour, shouldDrawButtonAsDown);
}

MainComponent::MainComponent()
{
    darkMode = juce::Desktop::getInstance().isDarkModeActive();
    gDarkMode = darkMode;
    appLaf.refreshColours();
    tapLaf.refreshColours();
    juce::Desktop::getInstance().addDarkModeSettingListener (this);

    setOpaque (true);
    setSize (834, 1112);
    setLookAndFeel (&appLaf);

    auto setupBtn = [this] (juce::TextButton& b, juce::Colour fill)
    {
        addAndMakeVisible (b);
        b.setColour (juce::TextButton::buttonColourId, fill);
        b.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        b.setColour (juce::TextButton::buttonOnColourId, fill.brighter (0.18f));
    };

    setupBtn (startButton, ink());
    setupBtn (stopButton, ink());
    setupBtn (tapButton, ink());
    setupBtn (shakerButton, ink());
    setupBtn (debugButton, juce::Colour (0xff0a0a0c));
    setupBtn (clickButton, juce::Colour (0xff0a0a0c));
    setupBtn (themeButton, ink());
    setupBtn (sourceButton, ink());
    setupBtn (congasButton, ink());
    setupBtn (styleAuto, ink());
    setupBtn (styleMarcha, ink());
    setupBtn (styleRock, ink());
    setupBtn (styleDance, ink());
    setupBtn (stylePop, ink());
    setupBtn (subAuto, ink());
    setupBtn (sub4, ink());
    setupBtn (sub8, ink());
    setupBtn (sub16, ink());

    startButton.onClick = [this] { startPressed(); };
    stopButton.onClick = [this] { stopPressed(); };
    tapButton.setLookAndFeel (&tapLaf);
    tapButton.onClick = [this] { tapPressed(); };
    shakerButton.onClick = [this]
    {
        const bool on = ! engine.settings().shakerEnabled.load();
        engine.settings().shakerEnabled.store (on);
        shakerButton.setButtonText (on ? "SHAKER  ON" : "SHAKER  OFF");
        shakerButton.setToggleState (on, juce::dontSendNotification);
    };
    debugButton.onClick = [this] {
        debugOpen = ! debugOpen;
        debugButton.setToggleState (debugOpen, juce::dontSendNotification);
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
    themeButton.onClick = [this] { applyTheme (! darkMode, true); };
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
        congasButton.setToggleState (on, juce::dontSendNotification);
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
    reverbLabel.setFont (fontUi (13.0f));

    addAndMakeVisible (reverbSlider);
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
        lab.setFont (fontUi (12.0f));

        addAndMakeVisible (s);
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
    shakerButton.setToggleState (true, juce::dontSendNotification);
    congasButton.setToggleState (true, juce::dontSendNotification);
    refreshStartButton();
    refreshStyleButtons();
    refreshSubdivisionButtons();
    refreshThemeColours();

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
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
    tapButton.setLookAndFeel (nullptr);
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
    tapLaf.refreshColours();
    refreshThemeColours();
    repaint();
}

void MainComponent::refreshThemeColours()
{
    juce::TextButton* buttons[] = {
        &startButton, &stopButton, &tapButton, &shakerButton, &debugButton,
        &clickButton, &themeButton, &sourceButton, &subAuto, &sub4, &sub8,
        &sub16, &congasButton, &styleAuto, &styleMarcha, &styleRock,
        &styleDance, &stylePop
    };
    for (auto* button : buttons)
    {
        button->setColour (juce::TextButton::buttonColourId, ink());
        button->setColour (juce::TextButton::buttonOnColourId, ink().brighter (0.18f));
        button->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        button->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }

    reverbLabel.setColour (juce::Label::textColourId, mute());
    swingLabel.setColour (juce::Label::textColourId, mute());
    intensityLabel.setColour (juce::Label::textColourId, mute());
    themeButton.setButtonText (darkMode ? "DARK" : "LIGHT");
    themeButton.setToggleState (! darkMode, juce::dontSendNotification);

    refreshTapButton();
    refreshStartButton();
    refreshStyleButtons();
    refreshSubdivisionButtons();
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
    tapFlash = 1;
    refreshTapButton();
}

void MainComponent::refreshTapButton()
{
    const bool lit = tapFlash > 0;
    tapButton.setColour (juce::TextButton::buttonColourId, lit ? fuchsia() : ink());
    tapButton.setButtonText ("TAP");
}

void MainComponent::refreshStartButton()
{
    startButton.setColour (juce::TextButton::buttonColourId, userWantsArmed ? fuchsia() : ink());
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
        b.setToggleState (on, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonColourId, ink());
        b.setColour (juce::TextButton::textColourOffId, on ? fuchsia() : juce::Colours::white);
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
        b.setToggleState (on, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonColourId, ink());
        b.setColour (juce::TextButton::textColourOffId,
                     on || detected ? fuchsia() : juce::Colours::white);
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

namespace
{
    // start/stop + TAP + subdivisions + style + instruments + reverb
    constexpr int kControlsBase = 86 + 118 + 48 + 48 + 56 + 58;
    constexpr int kTrimRow = 58;
    // What the status block above needs: title, state pill, BPM, engine line,
    // meter, beat dots and the mic line.
    constexpr int kStatusMin = 300;
}

bool MainComponent::showTrimRow() const
{
    return layoutColumn().getHeight() - (kControlsBase + kTrimRow) >= kStatusMin;
}

int MainComponent::controlsHeight() const
{
    return kControlsBase + (showTrimRow() ? kTrimRow : 0);
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
    refreshStartButton();
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
    if (auto* d = getPeer())
        juce::ignoreUnused (d);
    r = r.reduced (28, 24).withTrimmedTop (18);
   #else
    r = r.reduced (36, 28);
   #endif
    return r;
}

void MainComponent::resized()
{
    auto r = layoutColumn();
    auto bottom = r.removeFromBottom (86);
    startButton.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (6));
    stopButton.setBounds (bottom.reduced (6));

    tapButton.setBounds (r.removeFromBottom (118).reduced (4, 6));

    auto subs = r.removeFromBottom (48);
    const int sw = subs.getWidth() / 4;
    subAuto.setBounds (subs.removeFromLeft (sw).reduced (4));
    sub4.setBounds (subs.removeFromLeft (sw).reduced (4));
    sub8.setBounds (subs.removeFromLeft (sw).reduced (4));
    sub16.setBounds (subs.reduced (4));

    // The part comes before the instruments: it is the biggest single choice
    // the player makes, and three of the four were unreachable before.
    auto styles = r.removeFromBottom (48);
    const int stw = styles.getWidth() / 5;
    styleAuto.setBounds (styles.removeFromLeft (stw).reduced (3));
    styleMarcha.setBounds (styles.removeFromLeft (stw).reduced (3));
    styleRock.setBounds (styles.removeFromLeft (stw).reduced (3));
    styleDance.setBounds (styles.removeFromLeft (stw).reduced (3));
    stylePop.setBounds (styles.reduced (3));

    auto inst = r.removeFromBottom (56);
    const int iw = inst.getWidth() / 3;
    sourceButton.setBounds (inst.removeFromLeft (iw).reduced (4, 6));
    shakerButton.setBounds (inst.removeFromLeft (iw).reduced (4, 6));
    congasButton.setBounds (inst.reduced (4, 6));

    auto verb = r.removeFromBottom (58);
    reverbLabel.setBounds (verb.removeFromLeft (108).reduced (4, 8));
    reverbSlider.setBounds (verb.reduced (10, 6));

    const bool trims = showTrimRow();
    swingSlider.setVisible (trims);
    swingLabel.setVisible (trims);
    intensitySlider.setVisible (trims);
    intensityLabel.setVisible (trims);
    if (trims)
    {
        auto trim = r.removeFromBottom (58);
        auto left = trim.removeFromLeft (trim.getWidth() / 2);
        swingLabel.setBounds (left.removeFromLeft (86).reduced (4, 8));
        swingSlider.setBounds (left.reduced (8, 6));
        intensityLabel.setBounds (trim.removeFromLeft (96).reduced (4, 8));
        intensitySlider.setBounds (trim.reduced (8, 6));
    }

    auto utility = getLocalBounds().removeFromTop (36);
    debugButton.setBounds (utility.removeFromRight (56).reduced (6));
    clickButton.setBounds (utility.removeFromRight (114).reduced (6));
    themeButton.setBounds (utility.removeFromRight (82).reduced (6));
}

void MainComponent::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    g.fillAll (bg());

    const float energy = juce::jlimit (0.0f, 1.0f, std::sqrt (std::max (0.0f, snap.inputPeak)) * 3.2f);
    const float follow = stateIsHot (snap.followBar) ? 1.0f : 0.42f;
    const float wash = 0.12f + 0.20f * energy * follow;

    juce::ColourGradient floor (gDarkMode ? juce::Colour (0xff0a0a0c) : juce::Colour (0xffffffff),
                                full.getCentreX(), full.getY(),
                                bg(), full.getCentreX(), full.getBottom(), false);
    g.setGradientFill (floor);
    g.fillRect (full);

    paintRadial (g, { full.getCentreX(), full.getY() + 28.0f },
                 full.getWidth() * 0.70f, fuchsia(), wash);
    paintRadial (g, { full.getCentreX(), full.getBottom() - 80.0f },
                 full.getWidth() * 0.50f, fuchsia(), 0.08f + 0.14f * (tapFlash > 0 ? 1.0f : energy));

    auto r = layoutColumn();
    r.removeFromBottom (controlsHeight());

    auto titleR = r.removeFromTop (24);
    auto brand = titleR.removeFromLeft (22);
    g.setColour (fuchsia());
    g.fillRoundedRectangle (brand.withSizeKeepingCentre (8, 8).toFloat(), 1.8f);
    g.setColour (text());
    g.setFont (fontUi (14.0f));
    g.drawFittedText ("VIRTUAL PERCUSSIONIST", titleR, juce::Justification::centredLeft, 1);
    g.setColour (fuchsia().withAlpha (0.75f));
    g.fillRect ((float) titleR.getX(), (float) titleR.getBottom() - 1.0f,
                (float) (r.getWidth() - 22), 1.2f);

    auto stateR = r.removeFromTop (42).reduced (r.getWidth() / 12, 2);
    const auto stCol = stateColour (snap.followBar);
    const bool hot = stateIsHot (snap.followBar);
    if (hot)
    {
        g.setColour (stCol.withAlpha (0.28f));
        g.fillRoundedRectangle (stateR.toFloat().expanded (4.0f), 22.0f);
        g.setColour (stCol);
        g.fillRoundedRectangle (stateR.toFloat(), 18.0f);
        g.setColour (juce::Colours::white);
    }
    else
    {
        g.setColour (text().withAlpha (0.08f));
        g.fillRoundedRectangle (stateR.toFloat(), 18.0f);
        g.setColour (stCol);
        g.drawRoundedRectangle (stateR.toFloat().reduced (0.6f), 18.0f, 1.4f);
        g.setColour (stCol);
    }
    g.setFont (fontUi (14.0f));
    g.drawFittedText (juce::String (juce::CharPointer_UTF8 (vp::toBarString (snap.followBar))),
                      stateR, juce::Justification::centred, 1);

    r.removeFromTop (12);
    auto bpmR = r.removeFromTop (84);
    paintRadial (g, bpmR.getCentre().toFloat(), 140.0f, fuchsia(), 0.12f + 0.18f * energy);

    const auto bpmText = snap.bpm > 40.0f
                             ? juce::String (snap.bpm, 1)
                             : juce::String ("--");
    g.setFont (fontDisplay (78.0f));
    g.setColour (fuchsia().withAlpha (0.40f));
    g.drawFittedText (bpmText, bpmR.translated (0, 3).withTrimmedBottom (20),
                      juce::Justification::centred, 1);
    g.setColour (text());
    g.drawFittedText (bpmText, bpmR.withTrimmedBottom (20), juce::Justification::centred, 1);
    g.setColour (fuchsia());
    g.setFont (fontUi (12.0f));
    g.drawFittedText ("BPM", bpmR.removeFromBottom (18), juce::Justification::centred, 1);

    g.setColour (snap.aiOnnx ? text() : fuchsia());
    g.setFont (fontUi (12.0f));
    const juce::String srcName = snap.source == vp::FollowSource::speaker ? "IPAD" : "MIXER";
    const juce::String nnText = snap.neuralBpm > 40.0f ? juce::String (snap.neuralBpm, 0) : juce::String ("--");
    g.drawFittedText ((snap.aiOnnx ? juce::String ("AI ONNX") : juce::String ("AI STUB"))
                          + "  |  " + srcName
                          + "  |  nn " + nnText
                          + "  |  p " + juce::String (snap.pBeat, 2)
                          + (snap.hypValid ? "  valid" : "  wait"),
                      r.removeFromTop (20), juce::Justification::centred, 1);

    // Which part is actually playing. Under AUTO the buttons cannot say, so the
    // detector's choice and how sure it is are spelled out here.
    const auto activeStyle = static_cast<vp::GrooveStyle> (snap.grooveStyle);
    g.setColour (mute());
    g.setFont (fontUi (12.0f));
    g.drawFittedText (juce::String ("PARTE  ") + vp::toString (activeStyle)
                          + (engine.settings().grooveAuto.load()
                                 ? "   (auto " + juce::String (snap.grooveStyleConfidence, 2) + ")"
                                 : juce::String()),
                      r.removeFromTop (18), juce::Justification::centred, 1);

    auto meterR = r.removeFromTop (14).reduced (r.getWidth() / 5, 2);
    g.setColour (text().withAlpha (0.10f));
    g.fillRoundedRectangle (meterR.toFloat().expanded (1.0f), 6.0f);
    g.setColour (sliderTrack());
    g.fillRoundedRectangle (meterR.toFloat(), 5.0f);
    const float level = energy;
    auto fillM = meterR.toFloat().withWidth (static_cast<float> (meterR.getWidth()) * level);
    if (fillM.getWidth() > 2.0f)
    {
        juce::ColourGradient mg (fuchsia().brighter (0.2f), fillM.getX(), fillM.getY(),
                                 text(), fillM.getRight(), fillM.getY(), false);
        g.setGradientFill (mg);
        g.fillRoundedRectangle (fillM, 5.0f);
    }

    auto beats = r.removeFromTop (64);
    const float bw = static_cast<float> (beats.getWidth());
    const float y = static_cast<float> (beats.getCentreY());
    g.setColour (text().withAlpha (0.10f));
    g.fillRoundedRectangle (static_cast<float> (beats.getX()) + bw * 0.14f, y - 1.0f,
                            bw * 0.72f, 2.0f, 1.0f);

    for (int i = 0; i < 4; ++i)
    {
        const float x = static_cast<float> (beats.getX()) + bw * (0.2f + 0.2f * static_cast<float> (i));
        const int beatIdx = static_cast<int> (snap.barPhase * 4.0f) & 3;
        const bool on = snap.percussionAudible && beatIdx == i;
        const bool downbeat = i == 0;
        const float rad = downbeat ? 16.0f : 13.0f;

        if (on)
        {
            paintRadial (g, { x, y }, rad * 2.6f, fuchsia(), 0.45f);
            g.setColour (fuchsia());
            g.fillEllipse (x - rad, y - rad, rad * 2.0f, rad * 2.0f);
            g.setColour (juce::Colours::white);
            g.fillEllipse (x - rad * 0.38f, y - rad * 0.38f, rad * 0.76f, rad * 0.76f);
        }
        else
        {
            g.setColour (downbeat ? fuchsia().withAlpha (0.55f) : text().withAlpha (0.28f));
            g.drawEllipse (x - rad, y - rad, rad * 2.0f, rad * 2.0f, downbeat ? 2.0f : 1.4f);
        }
    }

    g.setColour (mute());
    g.setFont (fontUi (12.0f, false));
    const juce::String micText = ! micGranted ? "MIC DENIED"
                                : (inputChannels <= 0 ? "MIC OFF"
                                : (snap.inputPeak > 0.0012f ? "MIC LIVE"
                                                 : "MIC ON"));
    g.drawFittedText (micText + "   in " + juce::String (snap.inputPeak, 3)
                          + "   ch " + juce::String (inputChannels)
                          + "   Latency " + juce::String (snap.latencyMs, 1) + " ms",
                      r.removeFromTop (20), juce::Justification::centred, 1);

    if (debugOpen)
    {
        auto dbg = getLocalBounds().reduced (24).removeFromTop (280);
        g.setColour (panel().withAlpha (0.96f));
        g.fillRoundedRectangle (dbg.toFloat(), 16.0f);
        g.setColour (fuchsia().withAlpha (0.7f));
        g.drawRoundedRectangle (dbg.toFloat().reduced (0.5f), 16.0f, 1.4f);
        g.setColour (text());
        g.setFont (fontUi (12.0f, false));
        juce::StringArray lines;
        lines.add ("DEBUG");
        lines.add (juce::String (snap.aiOnnx ? "AI ONNX BeatNet" : "AI STUB (modello NON caricato)"));
        lines.add (juce::String (snap.source == vp::FollowSource::speaker ? "source IPAD/SPEAKER" : "source MIXER"));
        lines.add ("BPM " + juce::String (snap.bpm, 2) + "  nn " + juce::String (snap.neuralBpm, 2)
                   + "  target " + juce::String (snap.targetBpm, 2));
        lines.add ("pBeat " + juce::String (snap.pBeat, 3) + "  valid " + juce::String (snap.hypValid ? 1 : 0)
                   + "  conf " + juce::String (snap.confidence, 3));
        lines.add ("beat " + juce::String (snap.beatPhase, 3) + "  bar " + juce::String (snap.barPhase, 3));
        lines.add ("state " + juce::String (vp::toString (snap.state)));
        lines.add ("callback " + juce::String (snap.callbackMs, 2) + " ms");
        lines.add ("sr " + juce::String (snap.sampleRate, 0)
                   + "  mic " + juce::String (snap.inputPeak, 4)
                   + "  analysis " + juce::String (snap.analysisPeak, 4));
        lines.add ("inCh " + juce::String (inputChannels) + "  micGranted " + juce::String (micGranted ? 1 : 0));
        lines.add ("hits " + juce::String (engine.shakerHits())
                   + "  voices " + juce::String (snap.shakerVoices));
        lines.add (juce::String (snap.tapLocked ? "tap LOCK" : "tap auto"));
        lines.add ("bar " + juce::String (juce::CharPointer_UTF8 (vp::toBarString (snap.followBar))));
        lines.add ("reverb " + juce::String (snap.reverbAmount, 2));
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
