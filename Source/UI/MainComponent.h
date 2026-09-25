#pragma once

#include "Audio/VirtualPercussionEngine.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <functional>

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

    /** JUCE leaves application lifecycle handling to the app. When nothing is
        playing these release the microphone, audio unit and inference worker
        instead of keeping the background-audio entitlement busy. */
    void handleAppSuspended();
    void handleAppResumed();

private:
    void timerCallback() override;
    void darkModeSettingChanged() override;
    void startPressed();
    void stopPressed();
    void tapPressed();
    void applyTempoFollow (bool follow);
    void nudgeFixedBpm (float delta);
    void refreshTempoModeButtons();
    void refreshStartButton();
    void applyLatencyFromDevice();
    void openAudioDevice (bool micGranted);
    /** Pins the clock and the buffer on the open device. claimInputChannels is
        for the one case that has to move them - the microphone granted after the
        device was already open with none - because any other write to the channel
        fields makes the setup compare unequal and reopens the device for nothing. */
    void applyAudioSetup (bool claimInputChannels);
    /** After the first open. AUTO must land on a full-band rate the route
        already has — Bluetooth A2DP is often 48 kHz, not 44.1 — and the unit
        has to be started the same way a clock button does. A kit-voice change
        must not come through here. */
    void finishInitialDeviceStart();
    /** Nearest offered full-band rate, or 0 if `rate` is not one. */
    double snapToOfferedRate (double rate) const;
    void ensureMicrophone();
    void applyInputProcessing();

    /** Throw the audio device away and build a new one from the setup it is
        already on. Not a reopen: after iOS restarts its media server the audio
        unit is a handle to something that no longer exists, and only making a
        new one brings the sound back - which is what changing the clock by hand
        was doing. */
    void rebuildAudioDevice (const char* why);
    void powerDownAudioForBackground();

    /** What the listener asked the clock to be, and what the device should
        actually be opened at. They are not the same question: the request may
        be AUTO, and AUTO means the rate the hardware is already running at -
        on this rig the mixer's own clock. `requestedSampleRate()` is 0 in
        AUTO so the session is not told a number before it has one.
        `deviceSampleRate()` is never left at 0 once a route exists: a 0 is
        what JUCE turns into its constructed 44100, and that open does not
        start the callback on a route that is not 44.1. */
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
    void chooseInternalTrack();
    void loadInternalTrack (juce::URL url);
    void seekInternalTrack (double proportion);
    void buildTrackWaveform();
    void processTrackWaveform();
    void clearTrackWaveform();
    void relayoutSettings();
    void layoutTrackWaveform();
    int  trackWaveformHeight() const noexcept;
    void toggleInternalTrack();
    void selectFollowSource (vp::FollowSource source, bool restartInput = true);
    bool internalTrackSelected() const noexcept;
    void refreshInternalTrackButtons();
    void refreshProcButton();
    void refreshLoopModeButton();
    bool loadBundledLoopBank();

    void setSettingsOpen (bool open);
    void paintSettings (juce::Graphics&);
    void layoutSettings (juce::Rectangle<int> area);

    void loadPrefs();
    void savePrefs (bool flush = true);
    void applySubdivision (vp::Subdivision s);
    void refreshSubdivisionButtons();
    void applyStyle (vp::GrooveStyle s);
    void applyStyleAuto (bool on);
    void refreshStyleButtons();
    void applyShakerNatural (bool on);
    void refreshNaturalButton();
    /** Swing is a switch, not a quantity: straight, or the triplet. See
        docs/TODO.md item 7 and `GrooveEngine::humanDelay`. */
    void applySwing (bool on);
    void refreshSwingButton();
    void applyTheme (bool dark, bool manualOverride);
    void refreshThemeColours();
    void refreshBarButton();
    /** FEEL voice knobs: on/off is a tap, volume is a drag. Off is the same
        knob, slightly faded. A hold opens the sample-family picker. */
    void refreshVoiceKnobs();
    void assignKitSound (int slot, vp::KitSound sound);
    std::atomic<int>& kitSoundAtomic (int slot) noexcept;

    /** The margin every full-screen page starts from: what the design wants,
        widened per side to whatever the system says is unusable. On a phone
        that is a notch or a Dynamic Island at the top and a rounded corner
        either side; on an iPad it changes nothing. */
    /** What the system says is unusable on each side. UIKit's own answer for
        the window we are in, falling back to JUCE's display insets - the JUCE
        value is zero often enough that trusting it alone puts the status row
        under the Dynamic Island. Zero everywhere off-device. */
    juce::BorderSize<int> effectiveSafeArea() const;
    /** The insets the components were last laid out against. UIKit does not
        have them yet when the first resized() runs, so the layout has to be
        redone once they turn up - otherwise the controls sit where there was
        no notch and the painting, which reads them later, lands somewhere
        else entirely. */
    juce::BorderSize<int> laidOutSafeArea { -1, -1, -1, -1 };
    juce::Rectangle<int> safePadded (juce::Rectangle<int> area) const;
    juce::Rectangle<int> layoutColumn() const;

    /** Portrait stacks the stage over the console; landscape puts them side by
        side. Stacking in both is what used to force the feel controls off the
        screen when the iPad was turned. */
    bool isLandscape() const;
    /** Too small for the two-pane page: Split View column, Stage Manager tile,
        or a window dragged down. Then only the live-set row stays on screen.
        Hysteresis lives in compactLayout so a drag across 560×680 cannot
        flip full↔compact every pixel. */
    bool isCompact() const noexcept;
    void updateCompactLayout() noexcept;

    /** Tighter than safePadded: a narrow column cannot spend 36 pt on a
        margin the full-screen page needed for the brand. Safe-area insets still
        win per side. */
    juce::Rectangle<int> compactPadded (juce::Rectangle<int> area) const;
    void layoutFull();
    void layoutCompact();
    void applyCompactVisibility();
    void layoutTransport (juce::Rectangle<int> body);
    void layoutMisure (juce::Rectangle<int> body);
    void layoutFeelKnobs (juce::Rectangle<int> body);

    struct CompactGeom
    {
        juce::Rectangle<int> tempo, transport, misure, knobs;
    };
    CompactGeom compactGeom() const;

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
        juce::Rectangle<int> title, clockNote, bufferNote, inputNote, trackWave, status;
    };
    SettingsRows settingsRows;

    /** The stage laid out row by row. Both resized() and paint() ask for it. */
    struct StageRows
    {
        juce::Rectangle<int> title, pill, bpm, bpmLabel, tempoMode, tempoNudge,
                             tempoLine, beats, trackWave, part;
        /** The three columns the tempo row is divided into. The number gets the
            middle one and nothing else: given the whole row it grew until it ran
            under the two buttons and out of the column. */
        juce::Rectangle<int> octaveDown, bpmNumber, octaveUp;
        juce::Rectangle<int> barShift;
        /** Compact only: the SETUP button rides the status row's right side,
            because a phone has no title row to put it in. */
        juce::Rectangle<int> settings;
    };
    StageRows stageRows (juce::Rectangle<int> area) const;
    StageRows compactTempoRows (juce::Rectangle<int> area) const;
    juce::Rectangle<int> stageArea() const;

    /** The metrical level the player picked, and the way back to AUTO. There
        is material no automatic path can resolve - a straight groove at 50 BPM
        and a half-time one at 100 are the same sound - so the choice has to be
        reachable. See docs/HANDOFF_OCTAVE_50BPM.md. */
    void applyTempoOctave (int octaves);
    void applyTempoOctaveAuto();
    void refreshOctaveButtons();

    void paintStage (juce::Graphics& g, juce::Rectangle<int> area);
    void paintCards (juce::Graphics& g);
    juce::Rectangle<int> layoutConsole (juce::Rectangle<int> area);

    struct AppLookAndFeel : juce::LookAndFeel_V4
    {
        AppLookAndFeel();
        void refreshColours();

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        /** Sized against the shorter side as well as the height. MISURE is a
            row of squares, and scaling by height alone would give each label
            more type than the square can hold - JUCE's answer to that is an
            ellipsis, so MARCHA became "MAR..." on a phone. */
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
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPosProportional, float rotaryStartAngle,
                               float rotaryEndAngle, juce::Slider&) override;
    };

    /** Invisible hit target over the BPM and the quarters.
        A press anywhere in that zone is TAP; the flash is painted on the stage
        so it sits on the same numbers the player is looking at. */
    struct TapZone final : juce::Component
    {
        explicit TapZone (MainComponent& o) : owner (o)
        {
            setOpaque (false);
            setInterceptsMouseClicks (true, false);
            setWantsKeyboardFocus (false);
        }
        void mouseDown (const juce::MouseEvent&) override { owner.tapPressed(); }
        MainComponent& owner;
    };

    /** Seekable overview of the loaded backing track. Lives on the settings
        page under CARICA / PLAY; drag previews, release seeks and plays. */
    struct TrackWaveform final : juce::Component
    {
        explicit TrackWaveform (MainComponent& o) : owner (o)
        {
            setOpaque (false);
            setInterceptsMouseClicks (true, true);
        }
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
    private:
        double proportionFromX (float x) const;
        void drawPlayhead (juce::Graphics& g, juce::Rectangle<float> wave,
                           double proportion, juce::Colour col, bool handle) const;
        MainComponent& owner;
    };

    AppLookAndFeel appLaf;
    vp::VirtualPercussionEngine engine;
    vp::EngineSnapshot snap;

    juce::TextButton barButton { juce::String (juce::CharPointer_UTF8 ("SPOSTA L'1")) };
    juce::TextButton halveButton { juce::String (juce::CharPointer_UTF8 ("\xc3\xb7" "2")) };
    juce::TextButton doubleButton { juce::String (juce::CharPointer_UTF8 ("\xc3\x97" "2")) };
    juce::TextButton startButton { "START" };
    juce::TextButton stopButton { "STOP" };
    juce::TextButton followButton { "SEGUI" };
    juce::TextButton fixedButton { "FISSO" };
    juce::TextButton bpmNudgeDown { juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) };
    juce::TextButton bpmNudgeUp { "+" };
    juce::Label      bpmEdit;
    juce::TextButton debugButton { "DBG" };
    juce::TextButton clickButton { "CLICK TEST" };
    juce::TextButton themeButton { "DARK" };
    juce::TextButton sourceButton { "SPEAKER" };
    juce::TextButton trackLoadButton { "CARICA" };
    juce::TextButton trackPlayButton { "PLAY" };
    juce::TextButton settingsButton { "SETUP" };
    juce::TextButton settingsClose { "CHIUDI" };
    juce::TextButton clockAuto { "AUTO" }, clock44 { "44.1k" }, clock48 { "48k" };
    juce::TextButton clock88 { "88.2k" }, clock96 { "96k" };
    juce::TextButton bufAuto { "AUTO" }, buf64 { "64" }, buf128 { "128" };
    juce::TextButton buf256 { "256" }, buf512 { "512" };
    juce::TextButton procButton { "ELAB.  OFF" };
    /** Runtime choice: the original stroke-by-stroke engine, or the recorded
        bank with the original engine as its compatibility fallback. */
    juce::TextButton loopModeButton { "LOOP" };
    juce::TextButton subAuto { "AUTO" }, sub4 { "1/4" }, sub8 { "1/8" }, sub16 { "1/16" };
    juce::TextButton naturalButton { "NATURALE" };
    juce::TextButton swingButton { "SWING" };

    /** Custom style select. A native ComboBox on iOS/macOS is a system picker
        and would break the look; this paints with the same chrome as the
        MISURE squares. AUTO is the first row; picking a style turns AUTO
        off. DUE-UNO is manual only. See docs/TODO.md item 12. */
    struct StyleSelect final : juce::Component
    {
        explicit StyleSelect (MainComponent& o) : owner (o)
        {
            setOpaque (true);
            setWantsKeyboardFocus (false);
        }
        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void refresh();
        MainComponent& owner;
    };

    struct StyleMenuOverlay final : juce::Component
    {
        explicit StyleMenuOverlay (MainComponent& o);
        void resized() override;
        void mouseDown (const juce::MouseEvent& e) override;
        void showBelow (juce::Rectangle<int> anchorInParent);
        void dismiss();
        bool isOpen() const noexcept { return isVisible(); }
        /** AUTO plus every style, so this menu has exactly one row per
            `GrooveStyle` plus one - never a count typed by hand that can go
            stale against the enum. */
        static constexpr int kCount = 1 + static_cast<int> (vp::GrooveStyle::count);
        MainComponent& owner;
        juce::Component list;
        juce::TextButton items[kCount];
    };

    StyleSelect styleSelect { *this };
    StyleMenuOverlay styleMenu { *this };

    /** Vertical stack of unused-instrument cells, anchored to the held
        FEEL knob. Full-screen overlay, tap outside keeps the assignment.
        Not a second picker engine. */
    struct SoundMenuOverlay final : juce::Component
    {
        explicit SoundMenuOverlay (MainComponent& o);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent& e) override;
        void showFor (int slot);
        void dismiss();
        bool isOpen() const noexcept { return isVisible(); }
        static constexpr int kCount = static_cast<int> (vp::KitSound::count);
        MainComponent& owner;
        int slot = 0;
        juce::Component list;
        juce::TextButton items[kCount];
    };
    SoundMenuOverlay soundMenu { *this };
    /** Which input the kick drum arrives on, or none. See applyKickChannel. */
    juce::TextButton kickButton { "CASSA NO" };
    /** Measures this rig's round trip instead of taking the device's word for
        it. See Audio/LatencyProbe.h. */
    juce::TextButton latencyButton { "LATENZA" };
    /** Whether the part follows the band's dynamics. See BandDynamics.h. */
    juce::TextButton dynamicsButton { "DINAMICA" };
    juce::Slider reverbSlider;
    juce::Label  reverbLabel { {}, "REVERB" };
    juce::Label  reverbValue { {}, "30%" };
    juce::Slider intensitySlider;
    juce::Label  intensityLabel { {}, "ENERGIA" };
    juce::Label  intensityValue { {}, "50%" };
    /** Volume knob that also arms the voice: a tap (no drag) flips the
        enable, a vertical drag is still the level, a 450 ms hold opens
        the sample-family picker. */
    struct VoiceKnob final : juce::Slider, private juce::Timer
    {
        std::function<void()> onTap;
        std::function<void()> onHold;
        void mouseDown (const juce::MouseEvent& e) override
        {
            held = false;
            juce::Slider::mouseDown (e);
            if (onHold != nullptr)
                startTimer (450);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (held)
                return;
            if (e.getDistanceFromDragStart() > 6)
                stopTimer();
            juce::Slider::mouseDrag (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            stopTimer();
            const bool tap = ! held && ! e.mouseWasDraggedSinceMouseDown()
                             && e.getNumberOfClicks() == 1;
            juce::Slider::mouseUp (e);
            if (tap && onTap != nullptr)
                onTap();
        }
        void timerCallback() override
        {
            stopTimer();
            held = true;
            if (onHold != nullptr)
                onHold();
        }
        bool held = false;
    };
    VoiceKnob shakerVolSlider;
    juce::Label  shakerVolLabel { {}, "SHAKER" };
    juce::Label  shakerVolValue { {}, "100%" };
    VoiceKnob congaVolSlider;
    juce::Label  congaVolLabel { {}, "CONGAS" };
    juce::Label  congaVolValue { {}, "100%" };
    VoiceKnob cembaloVolSlider;
    juce::Label  cembaloVolLabel { {}, "CEMBALO" };
    juce::Label  cembaloVolValue { {}, "100%" };
    VoiceKnob clapVolSlider;
    juce::Label  clapVolLabel { {}, "CLAP" };
    juce::Label  clapVolValue { {}, "100%" };
    /** Input peak with a slow release, so the meter can be read against its
        target band instead of flickering. Updated on the UI timer. */
    float micHold = 0.0f;
    /** Smoothed copy of the tempo orb. `lead` is −1 behind the clock (drawn
        left) and +1 ahead (drawn right); it eases in both directions so the
        bloom slides instead of jumping. UI timer only — the audio thread
        never reads these. */
    float tempoBloomLead = 0.0f;
    float tempoBloomAmount = 0.0f;
    juce::Slider inputGainSlider;
    juce::Label  inputGainLabel { {}, "MIC" };
    juce::Label  inputGainValue { {}, "100%" };

    juce::AudioBuffer<float> inputScratch;
    juce::AudioBuffer<float> trackScratch;

    /** Internal backing-track path. The URL itself stays alive because on iOS
        it owns the security-scoped bookmark granted by the document picker. */
    juce::AudioFormatManager trackFormats;
    juce::TimeSliceThread trackReadThread { "VP track read-ahead" };
    juce::AudioTransportSource trackTransport;
    std::unique_ptr<juce::AudioFormatReaderSource> trackReader;
    std::unique_ptr<juce::FileChooser> trackChooser;
    juce::URL trackUrl;
    juce::String trackName;

    /** Where the beat dots are drawn, kept so the timer can repaint that strip
        alone rather than the whole console. */
    juce::Rectangle<int> beatStrip;
    juce::Rectangle<int> tapStrip;
    TapZone tapZone { *this };
    TrackWaveform trackWaveform { *this };

    juce::Array<float> trackWavePeaks;
    std::unique_ptr<juce::AudioFormatReader> trackWaveScanReader;
    juce::AudioBuffer<float> trackWaveScanBuffer;
    int64_t trackWaveScanPos = 0;
    float trackWaveMaxPeak = 0.0f;
    double trackWaveLengthSec = 0.0;
    double trackWavePreview = -1.0;

    std::unique_ptr<juce::PropertiesFile> prefs;

    bool debugOpen = false;
    bool darkMode = true;
    bool themeFollowsSystem = true;
    bool audioReady = false;
    /** Last layout pass. Enter compact below 560×680; leave only once the
        window is clearly large enough (600×740) so a Split View drag does not
        rebuild the page every pixel. */
    bool compactLayout = false;
    /** Media-server rebuilds must re-prepare even at the same clock: the
        previous audio unit no longer exists. A view resize must not. */
    bool forceEnginePrepare = false;
    bool audioOpened = false;
    bool micGranted = false;
    bool userWantsArmed = false;
    int  inputChannels = 0;
    int  tapFlash = 0;

    /** 0 means AUTO for both: follow the interface rather than tell it what to
        do. That is the default because on a rig with a mixer the mixer holds
        the clock, and the one thing the app must not do is take it off it.
        Persisted as the listener left it; the first open resolves a concrete
        rate without writing a second setting. */
    int  clockHz = 0;
    /** Session rate read before the first JUCE open, while AUTO has not yet
        substituted 44100. 0 once that open has been accepted or corrected. */
    double startupHardwareRate = 0.0;
    /** Concrete AUTO rate for the one corrective open. 0 the rest of the time,
        so a later buffer change keeps the rate the device is already on. */
    double overrideAutoRate = 0.0;
    int  bufferChoice = 0;
    bool inputProcessing = false;
    bool loopBankReady = false;
    bool appIsSuspended = false;
    bool audioPoweredDownForBackground = false;
    juce::String loopBankError;

    /** Blocks the audio callback has run, and the value the last timer tick saw.
        The device can stop calling us without telling anyone - a media server
        restart is one way, and a USB interface is a good way to provoke one - so
        the message thread watches for the count standing still and builds a new
        device when it does. How many times that has happened is on the settings
        page, because a rig that needs it every few seconds is a rig with a
        problem the app can only paper over. */
    std::atomic<uint32_t> audioBlocks { 0 };
    uint32_t seenAudioBlocks = 0;
    int  stalledTicks = 0;
    int  rebuildCooldownTicks = 0;
    int  deviceRebuilds = 0;
    /** Why the last one happened, for the diagnostics page. */
    juce::String lastRebuildWhy;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
