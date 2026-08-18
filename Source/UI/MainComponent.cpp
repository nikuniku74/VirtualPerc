#include "UI/MainComponent.h"
#include "Platform/IosMicPermission.h"

#include <cmath>

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
    setSize (834, 1112);

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
    setupBtn (subAuto, juce::Colour (0xff2a333e));
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

   #if JUCE_IOS
    engine.settings().followSource.store (static_cast<int> (vp::FollowSource::speaker));
    sourceButton.setButtonText ("IPAD");
   #else
    engine.settings().followSource.store (static_cast<int> (vp::FollowSource::kitMic));
    sourceButton.setButtonText ("MIXER");
   #endif
    engine.settings().humanization.store (0.00f);
    engine.settings().masterVolume.store (0.90f);
    engine.settings().followStrength.store (static_cast<int> (vp::FollowStrength::high));
    engine.settings().subdivision.store (static_cast<int> (vp::Subdivision::eighth));
    engine.settings().reverbAmount.store (0.30f);
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

    auto inst = r.removeFromBottom (56);
    sourceButton.setBounds (inst.removeFromLeft (inst.getWidth() / 2).reduced (4, 6));
    shakerButton.setBounds (inst.reduced (4, 6));

    auto verb = r.removeFromBottom (58);
    reverbLabel.setBounds (verb.removeFromLeft (108).reduced (4, 8));
    reverbSlider.setBounds (verb.reduced (10, 6));

    debugButton.setBounds (getLocalBounds().removeFromTop (36).removeFromRight (56).reduced (6));
    clickButton.setBounds (getLocalBounds().removeFromTop (36).removeFromRight (170).removeFromLeft (110).reduced (6));
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (bg());
    auto r = layoutColumn();
    r.removeFromBottom (384);

    g.setColour (mute());
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawFittedText ("VIRTUAL PERCUSSIONIST", r.removeFromTop (24), juce::Justification::centred, 1);

    auto stateR = r.removeFromTop (42).reduced (r.getWidth() / 12, 2);
    g.setColour (stateColour (snap.followBar));
    g.fillRoundedRectangle (stateR.toFloat(), 16.0f);
    g.setColour (juce::Colours::black);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawFittedText (juce::String (juce::CharPointer_UTF8 (vp::toBarString (snap.followBar))),
                      stateR, juce::Justification::centred, 1);

    r.removeFromTop (12);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (72.0f, juce::Font::bold));
    const auto bpmText = snap.bpm > 40.0f
                             ? juce::String (snap.bpm, 1)
                             : juce::String ("--");
    g.drawFittedText (bpmText + " BPM", r.removeFromTop (84), juce::Justification::centred, 1);

    g.setColour (snap.aiOnnx ? live() : juce::Colour (0xffff5a5a));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    const juce::String srcName = snap.source == vp::FollowSource::speaker ? "IPAD" : "MIXER";
    const juce::String nnText = snap.neuralBpm > 40.0f ? juce::String (snap.neuralBpm, 0) : juce::String ("--");
    g.drawFittedText ((snap.aiOnnx ? juce::String ("AI ONNX") : juce::String ("AI STUB"))
                          + " | " + srcName
                          + " | nn " + nnText
                          + " | p " + juce::String (snap.pBeat, 2)
                          + (snap.hypValid ? "  valid" : "  wait"),
                      r.removeFromTop (20), juce::Justification::centred, 1);

    auto meterR = r.removeFromTop (14).reduced (r.getWidth() / 5, 2);
    g.setColour (juce::Colour (0xff2c333c));
    g.fillRoundedRectangle (meterR.toFloat(), 4.0f);
    const float level = juce::jlimit (0.0f, 1.0f, std::sqrt (std::max (0.0f, snap.inputPeak)) * 3.2f);
    g.setColour (inputChannels > 0 ? live() : juce::Colour (0xffff5a5a));
    g.fillRoundedRectangle (meterR.toFloat().withWidth (static_cast<float> (meterR.getWidth()) * level), 4.0f);

    auto beats = r.removeFromTop (64);
    const float bw = static_cast<float> (beats.getWidth());
    const float y = static_cast<float> (beats.getCentreY());
    for (int i = 0; i < 4; ++i)
    {
        const float x = static_cast<float> (beats.getX()) + bw * (0.2f + 0.2f * static_cast<float> (i));
        const int beatIdx = static_cast<int> (snap.barPhase * 4.0f) & 3;
        const bool on = snap.percussionAudible && beatIdx == i;
        g.setColour (on ? amber() : juce::Colour (0xff2c333c));
        g.fillEllipse (x - 14.0f, y - 14.0f, 28.0f, 28.0f);
        if (on)
        {
            g.setColour (amber().withAlpha (0.25f));
            g.fillEllipse (x - 22.0f, y - 22.0f, 44.0f, 44.0f);
        }
    }

    g.setColour (mute());
    g.setFont (juce::FontOptions (13.0f));
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
        g.setColour (panel().withAlpha (0.94f));
        g.fillRoundedRectangle (dbg.toFloat(), 12.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (13.0f));
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
        g.drawFittedText (lines.joinIntoString ("\n"), dbg.reduced (16), juce::Justification::topLeft, 16);
    }
}
