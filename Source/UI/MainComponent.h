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
    void applyTempoOctave (int octaves);
    void refreshOctaveButtons();

    juce::Rectangle<int> layoutColumn() const;

    /** Portrait stacks the stage over the console; landscape puts them side by
        side. The old layout only ever stacked, so in landscape the stage was
        squeezed until the feel controls had to be dropped outright - which is
        why they used to disappear when the iPad was turned. */
    bool isLandscape() const;

    /** A titled group of controls. The console is a handful of these rather
        than one column of identical rows: what a player reaches for mid-song
        is a *place*, not a position in a list. resized() computes them and
        paint() draws them, so a card and the controls inside it cannot drift
        apart. */
    struct Card
    {
        juce::Rectangle<int> bounds;
        juce::String title;
    };
    juce::Array<Card> cards;

    /** The stage laid out row by row. Both resized() and paint() ask for it, so
        the halve/double buttons cannot end up somewhere other than beside the
        number they apply to - which is what happened when each worked the
        geometry out for itself. */
    struct StageRows
    {
        juce::Rectangle<int> title, pill, bpm, bpmLabel, tempoLine, beats, part, meter, mic;
        /** The three columns the tempo row is divided into. The number gets the
            middle one and nothing else: given the whole row it grew until it
            ran under the two buttons and out of the column. */
        juce::Rectangle<int> octaveDown, bpmNumber, octaveUp;
    };
    StageRows stageRows (juce::Rectangle<int> area) const;
    juce::Rectangle<int> stageArea() const;

    void paintStage (juce::Graphics& g, juce::Rectangle<int> area);
    void paintCards (juce::Graphics& g);
    juce::Rectangle<int> layoutConsole (juce::Rectangle<int> area);

    struct BigButtonLookAndFeel final : juce::LookAndFeel_V4
    {
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
        {
            return juce::Font (juce::FontOptions (juce::jmax (32.0f, (float) buttonHeight * 0.36f),
                                                 juce::Font::bold));
        }
    };

    struct SliderLookAndFeel final : juce::LookAndFeel_V4
    {
        int getSliderThumbRadius (juce::Slider&) override { return 16; }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                               float sliderPos, float, float, juce::Slider::SliderStyle,
                               juce::Slider& slider) override
        {
            juce::ignoreUnused (slider);
            const float trackH = 8.0f;
            const float cy = static_cast<float> (y) + 0.5f * static_cast<float> (height);
            juce::Rectangle<float> track (static_cast<float> (x), cy - trackH * 0.5f,
                                          static_cast<float> (width), trackH);
            g.setColour (juce::Colour (0xff2c333c));
            g.fillRoundedRectangle (track, 4.0f);
            auto filled = track.withWidth (juce::jmax (trackH, sliderPos - static_cast<float> (x)));
            g.setColour (juce::Colour (0xfff5a623));
            g.fillRoundedRectangle (filled, 4.0f);
            g.setColour (juce::Colours::white);
            g.fillEllipse (sliderPos - 16.0f, cy - 16.0f, 32.0f, 32.0f);
        }
    };

    BigButtonLookAndFeel tapLaf;
    SliderLookAndFeel sliderLaf;
    vp::VirtualPercussionEngine engine;
    vp::EngineSnapshot snap;

    juce::TextButton halveButton { juce::String (juce::CharPointer_UTF8 ("\xc3\xb7" "2")) };
    juce::TextButton doubleButton { juce::String (juce::CharPointer_UTF8 ("\xc3\x97" "2")) };
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

    /** Where the beat dots are drawn, so the timer can repaint that strip alone
        at the frame rate the eye needs without redrawing the whole console. */
    juce::Rectangle<int> beatStrip;

    bool debugOpen = false;
    bool audioReady = false;
    bool audioOpened = false;
    bool micGranted = false;
    bool userWantsArmed = false;
    int  inputChannels = 0;
    int  tapFlash = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
