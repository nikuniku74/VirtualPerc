#pragma once

#include "Audio/TrackStreamer.h"
#include "Audio/VirtualPercussionEngine.h"
#include "Platform/Subscription.h"
#include "UI/InfoPopup.h"
#include "UI/Localization.h"
#include "UI/PaywallOverlay.h"

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
    /** The offscreen screenshot tool (scripts/main_shots.cpp). */
    friend struct VPShotsAccess;

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
    void declareBar();
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
    /** Opens the file off the message thread, then hands it to loadInternalTrack. */
    void openTrackInBackground (juce::URL url);
    void loadInternalTrack (juce::URL url, std::unique_ptr<juce::AudioFormatReader> reader,
                            std::unique_ptr<juce::AudioFormatReader> waveReader);
    /** PLAY with nothing loaded: the built-in demo song (Assets/Demo). */
    void loadDemoTrack();
    void seekInternalTrack (double proportion);
    void buildTrackWaveform (std::unique_ptr<juce::AudioFormatReader> scanReader);
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
    void paintSettingsPage (juce::Graphics&);
    void paintGrooveLane (juce::Graphics&, juce::Rectangle<float> band, int quarters, bool running);
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
    void applyStartImmediately (bool on);
    void applyDriftGuard (bool on);
    void refreshGuardButton();
    void refreshStartNowButton();
    /** Swing is a switch, not a quantity: straight, or the triplet. See
        docs/TODO.md item 7 and `GrooveEngine::humanDelay`. */
    void applySwing (bool on);
    void refreshSwingButton();
    /** METRO: 4 or 3 quarters to the bar. In 3/4 the style list is the 3/4
        parts, AUTO is off and a knob on CLAP is silent. */
    void applyMeter (int beatsPerBar);
    void refreshMeterButton();
    bool meterIsThree() const noexcept { return engine.settings().beatsPerBar.load() == 3; }
    void applyStyle3 (vp::GrooveStyle3 s);
    void applyTheme (bool dark, bool manualOverride);
    void refreshThemeColours();
    void refreshBarButton();
    /** FEEL voice knobs: on/off is a tap, volume is a drag. Off is the same
        knob, slightly faded. A press held still opens the sound modal. */
    void refreshVoiceKnobs();
    void assignKitSound (int slot, vp::KitSound sound);
    std::atomic<int>& kitSoundAtomic (int slot) noexcept;
    void assignHitSample (int slot, int sample);
    void refreshHitKnobs();

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
    juce::Point<int> laidOutWindowSize { -1, -1 };
    /** Phone/wide x portrait/landscape, as last laid out (-1 before the first
        layout). A change glides the controls to their new places and fades the
        painted cards in, instead of the page jumping. */
    int layoutMode = -1;
    juce::ComponentAnimator layoutAnimator;
    /** Controls being faded in by a layout change, with the alpha they end at
        restored if the fade is cut short. */
    juce::Array<std::pair<juce::Component::SafePointer<juce::Component>, float>> layoutFadeIns;
    double layoutFadeStartMs = 0.0;
    bool layoutFading = false;
    static constexpr double kLayoutAnimMs = 280.0;
    float layoutFadeAmount() const noexcept;
    int resizeSettleTicks = 0;
    int resizeXrunsBefore = 0;
    bool a2dpResizeRecovered = false;
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
    /** ÷2 | TAP | ×2, or in FIXED, TAP | the tempo to type. */
    void placeTapRow (juce::Rectangle<int> octaveDown, juce::Rectangle<int> tap, juce::Rectangle<int> octaveUp);
    void applyCompactVisibility();
    void layoutMisure (juce::Rectangle<int> body);
    void prepareHits (double sampleRate);
    void mixHits (float* const* outs, int numChannels, int numSamples) noexcept;

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
    /** The rounded track behind the 1/4 - 1/8 - 1/16 segments. The three
        buttons only draw the chosen face; the track is the page's to paint. */
    juce::Rectangle<int> subdivisionTrack;
    juce::Array<Card> settingsCards;
    void paintCardList (juce::Graphics& g, const juce::Array<Card>& list);

    /** The settings page. A child rather than a flag the paint routine checks,
        because a full-bounds opaque child is also what stops a tap meant for
        the clock from landing on START underneath it. Everything it shows is
        still a MainComponent member - the overlay only owns the surface. */
    struct SettingsOverlay final : juce::Component
    {
        explicit SettingsOverlay (MainComponent& o) : owner (o), page (o)
        {
            setOpaque (true);
            // The cards scroll under a fixed heading when a phone cannot hold
            // them at their natural size: shrinking all of them to fit took
            // the buttons to 30 pt and the captions under their own text.
            viewport.setViewedComponent (&page, false);
            viewport.setScrollBarsShown (false, false, true, false);
            viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
            page.addMouseListener (&scrollGuard, true);
            addAndMakeVisible (viewport);
        }
        ~SettingsOverlay() override { page.removeMouseListener (&scrollGuard); }
        void paint (juce::Graphics& g) override { owner.paintSettings (g); }
        void resized() override { owner.layoutSettings (getLocalBounds()); }

        struct Page final : juce::Component
        {
            explicit Page (MainComponent& o) : owner (o) {}
            void paint (juce::Graphics& g) override { owner.paintSettingsPage (g); }
            MainComponent& owner;
        };

        /** A drag that moves the page is a scroll, not a press. JUCE leaves the
            press with the button, and the button travels with the finger, so
            the release clicked whatever the scroll started on; every step of
            a scroll puts that button back up. */
        struct ScrollGuard final : juce::MouseListener
        {
            explicit ScrollGuard (juce::Viewport& v) : viewport (v) {}
            void mouseDown (const juce::MouseEvent&) override { downY = viewport.getViewPositionY(); }
            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (std::abs (viewport.getViewPositionY() - downY) < 2)
                    return;
                if (auto* b = dynamic_cast<juce::Button*> (e.eventComponent))
                    b->setState (juce::Button::buttonNormal);
            }
            juce::Viewport& viewport;
            int downY = 0;
        };

        MainComponent& owner;
        Page page;
        juce::Viewport viewport;
        ScrollGuard scrollGuard { viewport };
    };
    SettingsOverlay settingsOverlay { *this };

    /** The landing: the launch screen's own picture (Assets/Brand, the dark
        logo at 62% of the short side on #0F0E12, as LaunchScreen.storyboard
        draws it), held a moment, then grown a little and faded onto the
        console. A tap skips it. Above every other child until it is gone. */
    struct SplashOverlay final : juce::Component, private juce::Timer
    {
        SplashOverlay();
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent&) override;
        void timerCallback() override;
        juce::Image logo;
        /** The animation's own clock, advanced only while the splash is on
            screen and by at most one frame's worth per tick: at launch iOS
            keeps the message thread busy (mic permission, the audio device,
            the network loading), and a wall clock ran the whole animation
            out before the first frame was shown. */
        double animMs = 0.0;
        double lastTickMs = 0.0;
        double growFromMs = 0.0;
    };
    SplashOverlay splash;

    /** The subscription page (7 days free, then monthly or yearly) and what it
        gates. Without an active trial or subscription START, and PLAY on a
        loaded track, open it instead of starting (Release builds; see
        VP_PAYWALL_ENFORCED in CMakeLists.txt). Platform/Subscription.h. */
    PaywallOverlay paywall;
    vp::store::Status subscription;
    bool paywallAutoShown = false;
    bool hasAccess() const noexcept;
    /** True when the audio may start; otherwise shows the paywall and says no. */
    bool requireAccess();
    void showPaywall (bool show);
    void onSubscriptionStatus (const vp::store::Status&);

    /** The parts of the settings page that are drawn rather than placed: the
        heading, the sentence under each group saying what the choice costs, and
        the block of numbers the hardware actually came back with. Computed in
        layoutSettings() and read by paintSettings(), for the same reason the
        stage rows are: a caption cannot drift away from the row it explains. */
    struct SettingsRows
    {
        juce::Rectangle<int> title, inputNote, trackWave, status;
    };
    SettingsRows settingsRows;

    /** The stage laid out row by row. Both resized() and paint() ask for it. */
    struct StageRows
    {
        juce::Rectangle<int> title, pill, bpm, bpmLabel, tempoMode, tempoNudge,
                             tempoLine, beats, trackWave, part;
        /** Under the quarters: the part's sixteenths, a row per FEEL knob. */
        juce::Rectangle<int> groove;
        /** The three columns the tempo row is divided into. The number gets the
            middle one and nothing else: given the whole row it grew until it ran
            under the two buttons and out of the column. */
        juce::Rectangle<int> octaveDown, bpmNumber, octaveUp;
        juce::Rectangle<int> barShift;
        juce::Rectangle<int> tap;
        /** Compact only: the SETUP button rides the status row's right side,
            because a phone has no title row to put it in. */
        juce::Rectangle<int> settings;
        /** Compact only: the MIC level bar on the status row, between the
            state and SETUP. Dragging it sideways is the input gain. */
        juce::Rectangle<int> meter;
        /** The phase lane (where the percussion sits against the clock), inside
            the hero card between the BPM label and the four quarters. */
        juce::Rectangle<int> lane;
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
        A press there declares the one, the same action as "L'1 è QUI".
        TAP tempo lives on its own button. */
    struct TapZone final : juce::Component
    {
        explicit TapZone (MainComponent& o) : owner (o)
        {
            setOpaque (false);
            setInterceptsMouseClicks (true, false);
            setWantsKeyboardFocus (false);
        }
        void mouseDown (const juce::MouseEvent&) override { owner.declareBar(); }
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

    juce::TextButton barButton { vpui::L ("SHIFT THE 1", "SPOSTA L'1") };
    juce::TextButton tapButton { "TAP" };
    juce::TextButton halveButton { juce::String (juce::CharPointer_UTF8 ("\xc3\xb7" "2")) };
    juce::TextButton doubleButton { juce::String (juce::CharPointer_UTF8 ("\xc3\x97" "2")) };
    juce::TextButton startButton { "START" };
    juce::TextButton stopButton { "STOP" };
    juce::TextButton followButton { vpui::L ("FOLLOW", "SEGUI") };
    juce::TextButton fixedButton { vpui::L ("FIXED", "FISSO") };
    juce::TextButton startNowButton { vpui::L ("START NOW", "START SUBITO") };
    juce::TextButton guardButton { vpui::L ("PAUSE IF OFF", "PAUSA SE FUORI") };
    juce::TextButton bpmNudgeDown { juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) };
    juce::TextButton bpmNudgeUp { "+" };
    /** The field the exact tempo is typed into: shown over bpmEntryButton
        only while it is being written. */
    juce::Label      bpmEdit;
    /** FIXED: beside TAP, in place of ÷2 and ×2, the tempo itself - touch it
        and type an exact one (50-200, the app's range). 2026-10-23. */
    juce::TextButton bpmEntryButton;
    juce::TextButton debugButton { "DBG" };
    juce::TextButton clickButton { "CLICK TEST" };
    juce::TextButton themeButton { "DARK" };
    juce::TextButton sourceButton { "SPEAKER" };
    juce::TextButton trackLoadButton { vpui::L ("LOAD", "CARICA") };
    juce::TextButton trackPlayButton { "PLAY" };
    juce::TextButton settingsButton { "SETUP" };
    juce::TextButton settingsClose { vpui::L ("CLOSE", "CHIUDI") };
    juce::TextButton accountButton { "PRO" };
    /** The (?) in the settings heading: the guide to every card on the page.
        It replaced the (i) that explained the INPUT card alone (2026-10-23). */
    juce::TextButton guideButton { "?" };
    /** The input explanation on the first launch, and the guide. */
    InfoPopup inputInfo;
    /** The input explanation, once, on the first launch ("Got it"). */
    void showInputInfo (bool firstRun);
    void showGuide();
    bool inputInfoChecked = false;
    juce::TextButton clockAuto { "AUTO" }, clock44 { "44.1k" }, clock48 { "48k" };
    juce::TextButton clock88 { "88.2k" }, clock96 { "96k" };
    juce::TextButton bufAuto { "AUTO" }, buf64 { "64" }, buf128 { "128" };
    juce::TextButton buf256 { "256" }, buf512 { "512" };
    juce::TextButton procButton { vpui::L ("PROC.  OFF", "ELAB.  OFF") };
    /** Runtime choice: the original stroke-by-stroke engine, or the recorded
        bank with the original engine as its compatibility fallback. */
    juce::TextButton loopModeButton { "LOOP" };
    juce::TextButton subAuto { "AUTO" }, sub4 { "1/4" }, sub8 { "1/8" }, sub16 { "1/16" };
    juce::TextButton naturalButton { vpui::L ("NATURAL", "NATURALE") };
    juce::TextButton swingButton { "SWING" };
    /** METRO, next to the other musical switches: reads 4/4 or 3/4. */
    juce::TextButton meterButton { "4/4" };

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
        void paint (juce::Graphics&) override;
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
        /** Phone width: the menu is a sheet from the bottom, two columns of
            52 pt rows; elsewhere it stays the dropdown under the selector. */
        juce::Rectangle<int> sheet;
        juce::Component list;
        juce::TextButton items[kCount];
    };

    StyleSelect styleSelect { *this };
    StyleMenuOverlay styleMenu { *this };

    static constexpr int kHitSampleCount = 6;
    /** Modal with the sounds (or, for the one-shot knobs, the samples) that no
        knob is using yet. Opened by holding a knob still (VoiceKnob). */
    struct SoundMenuOverlay final : juce::Component
    {
        explicit SoundMenuOverlay (MainComponent& o);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent& e) override;
        void showFor (int slot, bool hits = false);
        void dismiss();
        bool isOpen() const noexcept { return isVisible(); }
        static constexpr int kCount = kHitSampleCount > static_cast<int> (vp::KitSound::count)
                                          ? kHitSampleCount : static_cast<int> (vp::KitSound::count);
        MainComponent& owner;
        int slot = 0;
        bool hitMode = false;
        int shown = 0;
        static constexpr int kTitleH = 56;
        juce::Rectangle<int> card;
        juce::Component list;
        juce::TextButton items[kCount];
    };
    SoundMenuOverlay soundMenu { *this };

    /** Phone width: FEEL is two fader rows, the four voices over the four
        effects, all present - effects are fired mid-song, so no sheet. */
    void layoutVoicesRow (juce::Rectangle<int> body);



    /** Which input the kick drum arrives on, or none. See applyKickChannel. */
    juce::TextButton kickButton { vpui::L ("NO KICK", "CASSA NO") };
    juce::TextButton analysisButton { "IN MIX" };
    /** Measures this rig's round trip instead of taking the device's word for
        it. See Audio/LatencyProbe.h. */
    juce::TextButton latencyButton { vpui::L ("LATENCY", "LATENZA") };
    /** Whether the part follows the band's dynamics. See BandDynamics.h. */
    juce::TextButton dynamicsButton { vpui::L ("DYNAMICS", "DINAMICA") };
    juce::Slider reverbSlider;
    juce::Label  reverbLabel { {}, "REVERB" };
    juce::Label  reverbValue { {}, "30%" };
    juce::Slider intensitySlider;
    juce::Label  intensityLabel { {}, vpui::L ("ENERGY", "ENERGIA") };
    juce::Label  intensityValue { {}, "50%" };
    /** Volume knob that also arms the voice. A tap flips the enable (or fires
        the one-shot); a vertical drag is the level, with the zoom; a press held
        still for kHoldMs opens the sound modal. The old hold (450 ms, cancelled
        only by a full drag) opened the menu while a finger paused before
        dragging, which is why EDIT replaced it (docs/TODO.md item 95): now any
        movement past kHoldSlopPx cancels it, so only a finger that stays put
        gets the menu, and a drag is always the volume (item 120). */
    struct VoiceKnob final : juce::Slider, private juce::Timer
    {
        static constexpr int   kHoldMs = 600;
        static constexpr float kHoldSlopPx = 6.0f;
        std::function<void()> onTap;
        std::function<void()> onHold;
        std::function<void (bool)> onZoom;
        void mouseDown (const juce::MouseEvent& e) override
        {
            held = false;
            dragged = false;
            juce::Slider::mouseDown (e);
            if (onHold != nullptr)
                startTimer (kHoldMs);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (held)
                return;
            const float moved = e.getDistanceFromDragStart();
            if (moved > kHoldSlopPx)
                stopTimer();
            // A press that stays put is the sample (or the mute). The slider
            // otherwise treats that press as a drag and the tap never fires.
            if (moved <= 8.0f)
                return;
            if (! dragged && onZoom != nullptr)
                onZoom (true);
            dragged = true;
            juce::Slider::mouseDrag (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            stopTimer();
            juce::Slider::mouseUp (e);
            if (dragged && onZoom != nullptr)
                onZoom (false);
            if (! held && ! dragged && onTap != nullptr)
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
        bool dragged = false;
    };
    /** While a fader is being dragged, a large copy of it grows from the
        fader to the middle of the screen, so the level can be read and set
        with more travel per step. Mouse-transparent: the drag stays on the
        small fader. */
    struct FaderZoom final : juce::Component
    {
        explicit FaderZoom (MainComponent& o) : owner (o) { setInterceptsMouseClicks (false, false); }
        void show (juce::Slider& k);
        void hide();
        void paint (juce::Graphics& g) override;
        void tick();

        MainComponent& owner;
        juce::Component::SafePointer<juce::Slider> knob;
        float t = 0.0f, target = 0.0f;
        juce::Rectangle<float> from;
        std::unique_ptr<juce::VBlankAttachment> vblank;
    };
    FaderZoom faderZoom { *this };
    VoiceKnob shakerVolSlider;
    juce::Label  shakerVolLabel { {}, "SHAKER" };
    juce::Label  shakerVolValue { {}, "100%" };
    VoiceKnob congaVolSlider;
    juce::Label  congaVolLabel { {}, "CONGAS" };
    juce::Label  congaVolValue { {}, "100%" };
    VoiceKnob cembaloVolSlider;
    juce::Label  cembaloVolLabel { {}, vpui::kitName (vp::KitSound::cembalo) };
    juce::Label  cembaloVolValue { {}, "100%" };
    VoiceKnob clapVolSlider;
    juce::Label  clapVolLabel { {}, "CLAP" };
    juce::Label  clapVolValue { {}, "100%" };
    /** Input peak with a slow release, so the meter can be read against its
        target band instead of flickering. Updated on the UI timer. */
    float micHold = 0.0f;
    /** The page wash's input level, eased over about a second, and the step
        (0-8) it is drawn at. UI timer only. */
    float washEnergy = 0.0f;
    int washStep = 0;
    int lastMicStep = -1;
    /** What the last timer tick painted, so the next one repaints only what
        moved (see the end of timerCallback). */
    std::array<juce::int64, 6> lastPageKey {};
    std::array<juce::int64, 18> lastStageKey {};
    /** Smoothed copy of the tempo orb. `lead` is −1 behind the clock (drawn
        left) and +1 ahead (drawn right); it eases in both directions so the
        bloom slides instead of jumping. UI timer only — the audio thread
        never reads these. */
    float tempoBloomLead = 0.0f;
    float tempoBloomAmount = 0.0f;
    juce::Colour stateSmooth;
    float heroBloomAmt = 0.0f;
    float heroConf = 0.0f;        // eased tracker confidence, drives bloom strength
    float tapAlignFlash = 0.0f;   // fuchsia flash after declaring the one
    float armedPulse = 0.0f;      // START/STOP pulse on the beat
    /** How far the screen vouches for the part being in time (vp::tempoTrust,
        docs/TODO.md item 117), and when the confidence last broke, which its
        `steady` is counted from. UI timer only. */
    vp::TempoTrust trust = vp::TempoTrust::none;
    juce::uint32 trustLowMs = 0;
    /** The whole hero card flashes on each quarter, harder on the one, in the
        trust colour: from the stand the eye holds it against kick and snare,
        and a doubled tempo flashes twice per drummer's beat. Set per screen
        frame in updateBeatDots; `heroLive` (timer) says anything is being
        followed at all. */
    float heroFlash = 0.0f;
    bool heroLive = false;
    /** The card beats (flash, ball): live, and not a tempo the app says is
        not there yet before START. */
    bool heroBeating = false;
    /** 1 -> 0 over a second from the first entry after START: the ring out of
        the card and the number landing (paintStage). */
    float lockBurst = 0.0f;
    bool burstArmed = false;
    bool wasAudible = false;
    double lastBurstMs = 0.0;
    VoiceKnob inputGainSlider;   // a VoiceKnob only for the drag zoom; no onTap
    juce::Label  inputGainLabel { {}, "MIC" };
    juce::Label  inputGainValue { {}, "100%" };
    /** One-shot samples. Tap starts or restarts; a press held still picks an unused sample. */
    VoiceKnob absorbVolSlider;
    juce::Label absorbHitLabel { {}, "ABSORB" };
    juce::Label absorbHitValue { {}, "100%" };
    VoiceKnob hornVolSlider;
    juce::Label hornHitLabel { {}, "HORN" };
    juce::Label hornHitValue { {}, "100%" };
    VoiceKnob uplifterVolSlider;
    juce::Label uplifterHitLabel { {}, "UPLIFTER FX" };
    juce::Label uplifterHitValue { {}, "100%" };
    VoiceKnob riserVolSlider;
    juce::Label riserHitLabel { {}, "RISER 2" };
    juce::Label riserHitValue { {}, "100%" };
    struct HitVoice
    {
        std::atomic<uint32_t> request { 0 };
        std::atomic<float> gain { 1.0f };
        std::atomic<bool> sounding { false };
        std::atomic<int> selected { 0 };
        uint32_t playing = 0;
        int playingSample = 0;
        int pos = 0;
    };
    HitVoice hitVoices[4];
    juce::AudioBuffer<float> hitSamples[kHitSampleCount];
    double hitRate = 0.0;

    juce::AudioBuffer<float> inputScratch;
    juce::AudioBuffer<float> trackScratch;

    /** Internal backing-track path. The URL itself stays alive because on iOS
        it owns the security-scoped bookmark granted by the document picker. */
    juce::AudioFormatManager trackFormats;
    juce::TimeSliceThread trackReadThread { "VP track read-ahead" };
    vp::TrackStreamer trackTransport { trackReadThread };
    std::unique_ptr<juce::AudioFormatReaderSource> trackReader;
    std::unique_ptr<juce::FileChooser> trackChooser;
    juce::URL trackUrl;
    juce::String trackName;
    /** A file is being opened in the background; a newer choice bumps the
        generation so a slower, older open is dropped when it arrives. */
    bool trackLoading = false;
    int trackLoadGeneration = 0;

    /** Where the beat dots are drawn, kept so the timer can repaint that strip
        alone rather than the whole console. */
    juce::Rectangle<int> beatStrip;
    /** The hero card, kept the same way for the beat flash. */
    juce::Rectangle<int> heroCard;
    /** The lit dot is re-decided on every screen frame, from the clock less
        the output path, so it lights when the stroke is heard. The 15 Hz
        timer alone lit it up to 67 ms late, by a different amount each
        beat, and before the sound by the whole output latency (AirPods
        ~150-250 ms). */
    juce::VBlankAttachment beatVBlank;
    int dotBeat = -1;
    /** Where the clock is in the bar, 0..4 in quarters, as heard (the same
        compensated phase the dots use). The ball between the dots is drawn
        from it every frame while something is being followed; `beatAnimArea`
        is the strip plus the height of its hop and glow. */
    float beatPos = 0.0f;
    juce::Rectangle<int> beatAnimArea;
    double lastBeatAnimMs = 0.0;
    bool beatAnimating = false;
    float outputLatencyMs = 0.0f;
    void updateBeatDots();
    /** Every frame: each voice tile lights when a stroke of its knob is
        heard and fades in ~0.1 s (paintVoiceFader, "strokeGlow"). */
    void updateStrokeGlow (double nowMs);
    std::uint32_t strokeSeen[4] {};
    double strokeDueMs[4] {};
    float strokeGlow[4] {};
    float strokeGlowShown[4] {};
    double lastGlowMs = 0.0;
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
    /** Off: the app always starts dark and does not follow the system. */
    bool themeFollowsSystem = false;
    /** Written by JUCE's device lifecycle and read by the message-thread
        watchdog. A plain bool made the recovery decision a data race. */
    std::atomic<bool> audioReady { false };
    /** Last layout pass. Enter compact below 560×680; leave only once the
        window is clearly large enough (600×740) so a Split View drag does not
        rebuild the page every pixel. */
    bool compactLayout = false;
    /** Media-server rebuilds must re-prepare even at the same clock: the
        previous audio unit no longer exists. A view resize must not. */
    bool forceEnginePrepare = false;
    bool audioOpened = false;
    bool micGranted = false;
    std::atomic<bool> userWantsArmed { false };
    // STOP then START within ten seconds is the listener's own re-sync. What
    // the clock was doing before it and where it landed after goes to a log in
    // the Documents folder, so the cases can be studied (docs/TODO.md item 70).
    vp::EngineSnapshot stopSnap;
    double stopWallMs = 0.0;
    double stopTrackSec = -1.0;
    double restartWallMs = 0.0;
    bool restartLogPending = false;
    void writeRestartLog();
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
    /** Blocks of digital silence from the track reader mid-song, after a block
        that had sound: the read-ahead was not ready (a glitch in the song). */
    std::atomic<uint32_t> trackDropouts { 0 };
    bool trackLastBlockHadSound = false;
    uint32_t seenAudioBlocks = 0;
    int  stalledTicks = 0;
    int  rebuildCooldownTicks = 0;
    int  deviceRebuilds = 0;
    std::atomic<float> diagOutPeak { 0.0f }; // VPDIAG temporaneo
    /** Why the last one happened, for the diagnostics page. */
    juce::String lastRebuildWhy;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
