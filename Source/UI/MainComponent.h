#pragma once

#include "Audio/VirtualPercussionEngine.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

class MainComponent final : public juce::AudioAppComponent,
                            private juce::Timer,
                            private juce::DarkModeSettingListener
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
    void darkModeSettingChanged() override;
    void startPressed();
    void stopPressed();
    void tapPressed();
    void refreshTapButton();
    void refreshStartButton();
    void applyLatencyFromDevice();
    void openAudioDevice (bool micGranted);
    /** Pins the clock and the buffer on the open device. claimInputChannels is
        for the one case that has to move them - the microphone granted after the
        device was already open with none - because any other write to the channel
        fields makes the setup compare unequal and reopens the device for nothing. */
    void applyAudioSetup (bool claimInputChannels);
    void ensureMicrophone();
    void applyInputProcessing();

    /** What the listener asked the clock to be, and what the device should
        actually be opened at. They are not the same question: the request may
        be AUTO, and AUTO means the rate the hardware is already running at -
        on this rig the mixer's own clock. 0 from either means "no opinion",
        and nothing is written to the device. */
    double requestedSampleRate() const;
    int    requestedBufferFrames() const;
    double deviceSampleRate() const;
    int    deviceBufferFrames() const;

    void applyClock (int hz);
    void applyBufferChoice (int frames);
    void applyInputProcessingChoice (bool on);
    void refreshClockButtons();
    void refreshBufferButtons();
    void refreshSourceButton();
    void refreshProcButton();

    void setSettingsOpen (bool open);
    void paintSettings (juce::Graphics&);
    void layoutSettings (juce::Rectangle<int> area);

    void loadPrefs();
    void savePrefs();
    void applySubdivision (vp::Subdivision s);
    void refreshSubdivisionButtons();
    void applyStyle (vp::GrooveStyle s);
    void applyStyleAuto (bool on);
    void refreshStyleButtons();
    void applyTheme (bool dark, bool manualOverride);
    void refreshThemeColours();
    void applyTempoOctave (int octaves);
    void refreshOctaveButtons();

    juce::Rectangle<int> layoutColumn() const;

    /** Portrait stacks the stage over the console; landscape puts them side by
        side. Stacking in both is what used to force the feel controls off the
        screen when the iPad was turned. */
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
    juce::Array<Card> settingsCards;
    void paintCardList (juce::Graphics& g, const juce::Array<Card>& list);

    /** The settings page. A child rather than a flag the paint routine checks,
        because a full-bounds opaque child is also what stops a tap meant for
        the clock from landing on START underneath it. Everything it shows is
        still a MainComponent member - the overlay only owns the surface. */
    struct SettingsOverlay final : juce::Component
    {
        explicit SettingsOverlay (MainComponent& o) : owner (o) { setOpaque (true); }
        void paint (juce::Graphics& g) override { owner.paintSettings (g); }
        void resized() override { owner.layoutSettings (getLocalBounds()); }
        MainComponent& owner;
    };
    SettingsOverlay settingsOverlay { *this };

    /** The parts of the settings page that are drawn rather than placed: the
        heading, the sentence under each group saying what the choice costs, and
        the block of numbers the hardware actually came back with. Computed in
        layoutSettings() and read by paintSettings(), for the same reason the
        stage rows are: a caption cannot drift away from the row it explains. */
    struct SettingsRows
    {
        juce::Rectangle<int> title, clockNote, bufferNote, inputNote, status;
    };
    SettingsRows settingsRows;

    /** The stage laid out row by row. Both resized() and paint() ask for it, so
        the halve/double buttons cannot end up somewhere other than beside the
        number they apply to. */
    struct StageRows
    {
        juce::Rectangle<int> title, pill, bpm, bpmLabel, tempoLine, beats, part, meter, mic;
        /** The three columns the tempo row is divided into. The number gets the
            middle one and nothing else: given the whole row it grew until it
            ran under the two buttons and out of the column. */
        juce::Rectangle<int> octaveDown, bpmNumber, octaveUp;
        juce::Rectangle<int> barShift;
    };
    StageRows stageRows (juce::Rectangle<int> area) const;
    juce::Rectangle<int> stageArea() const;

    void paintStage (juce::Graphics& g, juce::Rectangle<int> area);
    void paintCards (juce::Graphics& g);
    juce::Rectangle<int> layoutConsole (juce::Rectangle<int> area);

    struct AppLookAndFeel : juce::LookAndFeel_V4
    {
        AppLookAndFeel();
        void refreshColours();

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        /** Sized against the width as well as the height. Scaling by height
            alone meant a button narrow enough - PARTE holds five of them - got
            a label too wide for it, and JUCE answers that with an ellipsis:
            MARCHA and DANCE came out as "MAR..." and "DAN..." the moment the
            iPad was turned. */
        void drawButtonText (juce::Graphics&, juce::TextButton&,
                             bool shouldDrawButtonAsHighlighted,
                             bool shouldDrawButtonAsDown) override;
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

    juce::TextButton halveButton { juce::String (juce::CharPointer_UTF8 ("\xc3\xb7" "2")) };
    juce::TextButton doubleButton { juce::String (juce::CharPointer_UTF8 ("\xc3\x97" "2")) };
    juce::TextButton barButton { juce::String (juce::CharPointer_UTF8 ("SPOSTA L'1")) };
    juce::TextButton startButton { "START" };
    juce::TextButton stopButton { "STOP" };
    juce::TextButton tapButton { "TAP" };
    juce::TextButton shakerButton { "SHAKER  ON" };
    juce::TextButton debugButton { "DBG" };
    juce::TextButton clickButton { "CLICK TEST" };
    juce::TextButton themeButton { "DARK" };
    juce::TextButton sourceButton { "SPEAKER" };
    juce::TextButton settingsButton { "SETUP" };
    juce::TextButton settingsClose { "CHIUDI" };
    juce::TextButton clockAuto { "AUTO" }, clock44 { "44.1k" }, clock48 { "48k" };
    juce::TextButton clock88 { "88.2k" }, clock96 { "96k" };
    juce::TextButton bufAuto { "AUTO" }, buf64 { "64" }, buf128 { "128" };
    juce::TextButton buf256 { "256" }, buf512 { "512" };
    juce::TextButton procButton { "ELAB.  OFF" };
    juce::TextButton subAuto { "AUTO" }, sub4 { "1/4" }, sub8 { "1/8" }, sub16 { "1/16" };
    juce::TextButton congasButton { "CONGAS  ON" };
    juce::TextButton styleAuto { "AUTO" };
    juce::TextButton styleMarcha { "MARCHA" }, styleRock { "ROCK" };
    juce::TextButton styleDance { "DANCE" }, stylePop { "POP" };
    juce::TextButton styleSamba { "SAMBA" }, styleFunk { "FUNK" };
    juce::TextButton styleReggae { "REGGAE" }, styleBossa { "BOSSA" };
    juce::Slider reverbSlider;
    juce::Label  reverbLabel { {}, "REVERB" };
    juce::Slider swingSlider;
    juce::Label  swingLabel { {}, "SWING" };
    juce::Slider intensitySlider;
    juce::Label  intensityLabel { {}, "ENERGIA" };
    juce::Slider shakerVolumeSlider;
    juce::Label  shakerVolumeLabel { {}, "VOLUME" };
    juce::Label  shakerVolumeValue { {}, "100%" };

    juce::AudioBuffer<float> inputScratch;

    /** Where the beat dots are drawn, kept so the timer can repaint that strip
        alone rather than the whole console. */
    juce::Rectangle<int> beatStrip;

    std::unique_ptr<juce::PropertiesFile> prefs;

    bool debugOpen = false;
    bool darkMode = true;
    bool themeFollowsSystem = true;
    bool audioReady = false;
    bool audioOpened = false;
    bool micGranted = false;
    bool userWantsArmed = false;
    int  inputChannels = 0;
    int  tapFlash = 0;

    /** 0 means AUTO for both: follow the interface rather than tell it what to
        do. That is the default because on a rig with a mixer the mixer holds
        the clock, and the one thing the app must not do is take it off it. */
    int  clockHz = 0;
    int  bufferChoice = 0;
    bool inputProcessing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
