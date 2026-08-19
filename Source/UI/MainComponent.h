#pragma once

#include "Audio/VirtualPercussionEngine.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

class MainComponent final : public juce::AudioAppComponent,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

private:
    void timerCallback() override;
    void startPressed();
    void stopPressed();
    void tapPressed();
    void refreshTapButton();
    void refreshStartButton();
    void applyLatencyFromDevice();
    void openAudioDevice (bool micGranted);
    void applyHardwareAudioSetup (int inputChannels);
    void ensureMicrophone();
    void applyFollowSource();
    void applySubdivision (vp::Subdivision s);
    void refreshSubdivisionButtons();
    void applyStyle (vp::GrooveStyle s);
    void applyStyleAuto (bool on);
    void refreshStyleButtons();
    juce::Rectangle<int> layoutColumn() const;
    /** Height the control stack needs. `paint` reserves exactly this, so the
        two cannot drift apart. */
    int  controlsHeight() const;
    /** Landscape on an iPad leaves far less height than portrait. When there is
        not enough room for the status area above, the two trim sliders are the
        first thing to go - style and instrument selection are not negotiable. */
    bool showTrimRow() const;

    struct AppLookAndFeel : juce::LookAndFeel_V4
    {
        AppLookAndFeel();

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                                   bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown) override;
        int getSliderThumbRadius (juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
    };

    struct TapLookAndFeel final : AppLookAndFeel
    {
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                                   bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown) override;
    };

    AppLookAndFeel appLaf;
    TapLookAndFeel tapLaf;
    vp::VirtualPercussionEngine engine;
    vp::EngineSnapshot snap;

    juce::TextButton startButton { "START" };
    juce::TextButton stopButton { "STOP" };
    juce::TextButton tapButton { "TAP" };
    juce::TextButton shakerButton { "SHAKER  ON" };
    juce::TextButton debugButton { "DBG" };
    juce::TextButton clickButton { "CLICK TEST" };
    juce::TextButton sourceButton { "SPEAKER" };
    juce::TextButton subAuto { "AUTO" }, sub4 { "1/4" }, sub8 { "1/8" }, sub16 { "1/16" };
    juce::TextButton congasButton { "CONGAS  ON" };
    juce::TextButton styleAuto { "AUTO" };
    juce::TextButton styleMarcha { "MARCHA" }, styleRock { "ROCK" };
    juce::TextButton styleDance { "DANCE" }, stylePop { "POP" };
    juce::Slider reverbSlider;
    juce::Label  reverbLabel { {}, "REVERB" };
    juce::Slider swingSlider;
    juce::Label  swingLabel { {}, "SWING" };
    juce::Slider intensitySlider;
    juce::Label  intensityLabel { {}, "ENERGIA" };

    juce::AudioBuffer<float> inputScratch;

    bool debugOpen = false;
    bool audioReady = false;
    bool audioOpened = false;
    bool micGranted = false;
    bool userWantsArmed = false;
    int  inputChannels = 0;
    int  tapFlash = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
