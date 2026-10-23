// Not a measurement: the main page rendered to PNG at device sizes, with a band
// coming in and the part playing - what the stage looks like without a screen
// (a locked Mac, a simulator that will not run). Host only.
//
//   cmake --build build-host --target VPShots
//   ./build-host/VPShots_artefacts/Release/VPShots <out-dir> [song.wav] [it|en]
//
// It never opens the Mac's audio: a null device is registered before the page
// asks for one, and the song is handed to the page as the MIXER input from a
// high-resolution timer, a block at a time in real time - an audio thread, as
// on stage - while the page's own timers run on the normal message loop. The
// preferences live in a temporary HOME, never the user's.

#include "UI/MainComponent.h"
#include "UI/Localization.h"
#include "UI/MainComponentShared.h"   // shownLabel, for the FIXED check

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <execinfo.h>
#include <unistd.h>

namespace
{
    // A crash in a tool with no debugger at hand: say where.
    void crashed (int sig)
    {
        void* frames[64];
        const int n = backtrace (frames, 64);
        std::fprintf (stderr, "signal %d\n", sig);
        backtrace_symbols_fd (frames, n, STDERR_FILENO);
        _exit (128 + sig);
    }

    bool inputChannelTest = false;
    struct NullDevice;
    std::atomic<NullDevice*> gDevice { nullptr };
    // The manager deletes the device on the message thread while the feeder
    // may be inside it on the timer's thread: both hold this.
    juce::CriticalSection gDeviceLock;

    struct NullDevice final : juce::AudioIODevice
    {
        NullDevice() : juce::AudioIODevice ("Null", "Null") {}
        // The manager deletes and reopens it (at the rate the page asks for):
        // the tool must never hold on to the old one.
        ~NullDevice() override
        {
            const juce::ScopedLock sl (gDeviceLock);
            NullDevice* self = this;
            gDevice.compare_exchange_strong (self, nullptr);
        }
        juce::StringArray getOutputChannelNames() override { return { "L", "R" }; }
        juce::StringArray getInputChannelNames() override
        {
            juce::StringArray names;
            for (int i = 1; i <= (inputChannelTest ? 32 : 2); ++i)
                names.add (juce::String (i));
            return names;
        }
        juce::Array<double> getAvailableSampleRates() override { return { 44100.0, 48000.0 }; }
        juce::Array<int> getAvailableBufferSizes() override { return { 256, 512 }; }
        int getDefaultBufferSize() override { return 512; }
        juce::String open (const juce::BigInteger& in, const juce::BigInteger& out, double sr, int bs) override
        {
            ins = in;
            outs = out;
            rate = sr > 0.0 ? sr : 44100.0;
            block = bs > 0 ? bs : 512;
            opened = true;
            return {};
        }
        void close() override { stop(); opened = false; }
        bool isOpen() override { return opened; }
        void start (juce::AudioIODeviceCallback* cb) override
        {
            if (cb == nullptr)
                return;
            cb->audioDeviceAboutToStart (this);
            const juce::ScopedLock sl (lock);
            callback = cb;
        }
        void stop() override
        {
            juce::AudioIODeviceCallback* c = nullptr;
            {
                const juce::ScopedLock sl (lock);
                c = std::exchange (callback, nullptr);
            }
            if (c != nullptr)
                c->audioDeviceStopped();
        }
        bool isPlaying() override { return callback != nullptr; }
        juce::String getLastError() override { return {}; }
        int getCurrentBufferSizeSamples() override { return block; }
        double getCurrentSampleRate() override { return rate; }
        int getCurrentBitDepth() override { return 32; }
        juce::BigInteger getActiveOutputChannels() const override { return outs; }
        juce::BigInteger getActiveInputChannels() const override { return ins; }
        int getOutputLatencyInSamples() override { return 0; }
        int getInputLatencyInSamples() override { return 0; }

        /** One block, as the hardware's thread would deliver it: as many
            channels as were opened (JUCE sizes its own buffers by them). */
        void pump (const float* const* in, float* const* out, int n)
        {
            const juce::ScopedLock sl (lock);
            if (callback != nullptr)
                callback->audioDeviceIOCallbackWithContext (
                    in, juce::jmin (2, ins.countNumberOfSetBits()),
                    out, juce::jmin (2, outs.countNumberOfSetBits()), n, {});
        }

        juce::CriticalSection lock;
        juce::AudioIODeviceCallback* callback = nullptr;
        juce::BigInteger ins, outs;
        double rate = 44100.0;
        int block = 512;
        bool opened = false;
    };

    struct NullType final : juce::AudioIODeviceType
    {
        NullType() : juce::AudioIODeviceType ("Null") {}
        void scanForDevices() override {}
        juce::StringArray getDeviceNames (bool) const override { return { "Null" }; }
        int getDefaultDeviceIndex (bool) const override { return 0; }
        int getIndexOfDevice (juce::AudioIODevice*, bool) const override { return 0; }
        bool hasSeparateInputsAndOutputs() const override { return false; }
        juce::AudioIODevice* createDevice (const juce::String&, const juce::String&) override
        {
            auto* d = new NullDevice();
            const juce::ScopedLock sl (gDeviceLock);
            gDevice.store (d);
            return d;
        }
    };

    /** The song as the audio thread's input: one block per tick, looping. */
    struct Feeder final : juce::HighResolutionTimer
    {
        explicit Feeder (juce::AudioBuffer<float>& s) : song (s) {}
        ~Feeder() override { stopTimer(); }
        void hiResTimerCallback() override
        {
            const juce::ScopedLock sl (gDeviceLock);
            auto* d = gDevice.load();
            if (d == nullptr)
                return;
            const int n = d->block;

            if (in.getNumSamples() != n)
            {
                // Once per block size; a test tool, not the app's audio path.
                in.setSize (2, n);
                out.setSize (2, n);
            }
            const int at = pos % juce::jmax (1, song.getNumSamples() - n);
            for (int c = 0; c < 2; ++c)
                in.copyFrom (c, 0, song, juce::jmin (c, song.getNumChannels() - 1), at, n);
            pos += n;
            const float* ins[2] = { in.getReadPointer (0), in.getReadPointer (1) };
            float* outs[2] = { out.getWritePointer (0), out.getWritePointer (1) };
            d->pump (ins, outs, n);
        }
        juce::AudioBuffer<float>& song;
        juce::AudioBuffer<float> in, out;
        int pos = 0;
    };

    void save (const juce::Image& img, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        if (out.openedOk() && png.writeImageToStream (img, out))
            std::printf ("  %s\n", file.getFileName().toRawUTF8());
        else
            std::printf ("  FAILED %s\n", file.getFullPathName().toRawUTF8());
        std::fflush (stdout);
    }
}

/** Steps through the shots on the message thread, a tick every 50 ms. */
struct VPShotsAccess final : juce::Timer
{
    /** What a shot shows: the page playing, or SETUP (its top, its end) or
        the guide over it, with the song still coming in. */
    enum Mode { playing, settings, settingsEnd, guide, fixedTempo };
    struct Shot { const char* name; int w, h; Mode mode; };

    VPShotsAccess (MainComponent& m, const juce::File& d, std::function<void (int)> done)
        : mc (m), dir (d), finished (std::move (done))
    {
        // No first-launch popup and no landing: the page as it is once in use.
        mc.inputInfoChecked = true;
        mc.splash.setVisible (false);
        startTimer (50);
    }

    void timerCallback() override
    {
        if (inputChannelTest)
        {
            elapsed += 0.05;
            if (elapsed < 1.0 || gDevice.load() == nullptr)
                return;
            stopTimer();
            finished (checkInputChannels() ? 0 : 1);
            return;
        }
        // The page's frame (ball, lit tiles, ring) has no display link offscreen.
        mc.updateBeatDots();
        elapsed += 0.05;
        if (index >= static_cast<int> (std::size (shots)))
        {
            stopTimer();
            finished (0);
            return;
        }
        const auto& s = shots[index];
        if (phase == 0)
        {
            if (gDevice.load() == nullptr || elapsed < 1.0)
                return;
            mc.setSize (s.w, s.h);
            mc.resized();
            elapsed = 0.0;
            if (s.mode == fixedTempo)
            {
                // FIXED: TAP and the tempo to type; then the field open.
                mc.applyTempoFollow (false);
                strokesAtStart = 0;
                for (int k = 0; k < 4; ++k)
                    strokesAtStart += mc.engine.partStrokes (k);
                firstStrokeSec = -1.0;
                mc.startPressed();
                entryShot = false;
                phase = 4;
                return;
            }
            if (s.mode != playing)
            {
                mc.setSettingsOpen (true);
                if (s.mode == settingsEnd)
                    mc.settingsOverlay.viewport.setViewPosition (0, 100000);
                if (s.mode == guide)
                    mc.showGuide();
                phase = 3;
                return;
            }
            mc.startPressed();
            phase = 1;
            entryShot = false;
            return;
        }
        if (phase == 4)
        {
            if (firstStrokeSec < 0.0)
            {
                std::uint32_t n = 0;
                for (int k = 0; k < 4; ++k)
                    n += mc.engine.partStrokes (k);
                if (n != strokesAtStart)
                {
                    firstStrokeSec = elapsed;
                    const auto snap = mc.engine.snapshot();
                    const auto trust = vp::tempoTrust (snap, true, false);
                    std::printf ("  FIXED: first stroke %.2f s after START, label \"%s\", trust %d\n",
                                 elapsed, shownLabel (snap, trust).toRawUTF8(), static_cast<int> (trust));
                }
            }
            if (elapsed >= 4.0)
            {
                shoot (s.name);
                // Shot at once: offscreen the field has no window to keep
                // its focus and closes again on the next message.
                mc.bpmEntryButton.onClick();
                save (mc.createComponentSnapshot (mc.getLocalBounds(), true, 2.0f),
                      dir.getChildFile (juce::String (s.name) + "-scrivi.png"));
                mc.bpmEdit.hideEditor (true);
                // The typed tempo has to stick: type, press return, read the
                // engine (the bug of 2026-10-23: return put the old one back).
                // Return is handled on a later message, so the result is read
                // a few ticks on (phase 5).
                typedStep = 0;
                phase = 5;
                elapsed = 0.0;
            }
            return;
        }
        if (phase == 5)
        {
            const char* const typed[] = { "98,5", "300", "40" };
            if (elapsed < 0.3)
                return;
            if (typedStep > 0)
                std::printf ("  typed %s -> fixed tempo %.1f, button \"%s\"\n", typed[typedStep - 1],
                             static_cast<double> (mc.engine.settings().userBpm.load()),
                             mc.bpmEntryButton.getButtonText().toRawUTF8());
            if (typedStep < 3)
            {
                mc.bpmEntryButton.onClick();
                if (auto* ed = mc.bpmEdit.getCurrentTextEditor())
                {
                    ed->setText (typed[typedStep]);
                    ed->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                }
                ++typedStep;
                elapsed = 0.0;
                return;
            }
            mc.stopPressed();
            mc.applyTempoFollow (true);
            entryShot = false;
            phase = 2;
            elapsed = 0.0;
            return;
        }
        if (phase == 3)
        {
            if (elapsed >= 1.0)
            {
                shoot (s.name);
                mc.inputInfo.close();
                mc.setSettingsOpen (false);
                phase = 2;
                elapsed = 0.0;
            }
            return;
        }
        if (phase == 1)
        {
            if (! entryShot && mc.lockBurst > 0.35f && mc.lockBurst < 0.70f)
            {
                entryShot = true;
                shoot (juce::String (s.name) + "-entrata");
            }
            if (elapsed >= 22.0)
            {
                shoot (s.name);
                mc.stopPressed();
                phase = 2;
                elapsed = 0.0;
            }
            return;
        }
        if (elapsed >= 1.5)
        {
            ++index;
            phase = 0;
            elapsed = 1.0;
        }
    }

    bool checkInputChannels()
    {
        mc.engine.settings().followSource.store (static_cast<int> (vp::FollowSource::kitMic));
        mc.engine.settings().analysisChannel.store (-1);
        mc.engine.settings().kickChannel.store (-1);
        mc.micGranted = true;
        mc.audioOpened = true;
        mc.applyAudioSetup (true);
        const bool initialTwo = mc.inputChannels == 2;
        bool analysis32 = false, kick32 = false;
        for (int i = 0; i < 32; ++i)
        {
            mc.analysisButton.onClick();
            analysis32 |= mc.engine.settings().analysisChannel.load() == 31;
        }
        for (int i = 0; i < 31; ++i)
        {
            mc.kickButton.onClick();
            kick32 |= mc.engine.settings().kickChannel.load() == 31;
        }
        mc.kickButton.onClick();
        const bool kickWrap = mc.engine.settings().kickChannel.load() == -1;
        mc.engine.settings().kickChannel.store (30);
        mc.engine.settings().analysisChannel.store (31);
        mc.engine.settings().inputGain.store (1.0f);
        mc.applyAudioSetup (true);
        mc.analysisButton.onClick(); // wrap to MIX
        bool skipsKick = true;
        for (int i = 0; i < 32; ++i)
        {
            mc.analysisButton.onClick();
            skipsKick &= mc.engine.settings().analysisChannel.load() != 30;
        }
        mc.engine.settings().analysisChannel.store (31);
        juce::AudioBuffer<float> audio (32, 256);
        for (int b = 0; b < 200; ++b)
        {
            audio.clear();
            for (int i = 0; i < 256; ++i)
            {
                audio.setSample (31, i, 0.5f);
                const int k = (b * 256 + i) % 24000;
                audio.setSample (30, i, k < 4000 ? 0.7f * std::sin (2.0f * 3.14159265f * 70 * k / 48000)
                                                          * std::exp (-k / 800.0f) : 0.0f);
            }
            mc.getNextAudioBlock ({ &audio, 0, 256 });
        }
        const auto snap = mc.engine.snapshot();
        const bool routed = snap.inputPeak > 0.49f && snap.kickChannel == 30 && snap.kickOnsets > 0;
        const bool ok = initialTwo && analysis32 && kick32 && kickWrap && skipsKick && routed;
        std::printf ("input-channels initial2=%d IN32=%d KICK32=%d wrap=%d skip=%d peak=%.3f kick=%d hits=%d %s\n",
                     initialTwo, analysis32, kick32, kickWrap, skipsKick, snap.inputPeak,
                     snap.kickChannel, snap.kickOnsets, ok ? "PASS" : "FAIL");
        std::fflush (stdout);
        return ok;
    }

    void shoot (const juce::String& name)
    {
        mc.timerCallback();
        mc.updateBeatDots();
        save (mc.createComponentSnapshot (mc.getLocalBounds(), true, 2.0f), dir.getChildFile (name + ".png"));
    }

    MainComponent& mc;
    juce::File dir;
    std::function<void (int)> finished;
    static constexpr Shot shots[] = {
        { "iphone", 393, 852, playing },
        { "iphone-impostazioni", 393, 852, settings },
        { "iphone-impostazioni-fine", 393, 852, settingsEnd },
        { "iphone-guida", 393, 852, guide },
        { "iphone-fisso", 393, 852, fixedTempo },
        { "ipad", 834, 1194, playing },
        { "ipad-impostazioni", 834, 1194, settings },
        { "ipad-guida", 834, 1194, guide },
        { "ipad-fisso", 834, 1194, fixedTempo },
        { "ipad-orizzontale", 1194, 834, playing },
    };
    int index = 0, phase = 0;
    double elapsed = 0.0;
    bool entryShot = false;
    int typedStep = 0;
    std::uint32_t strokesAtStart = 0;
    double firstStrokeSec = -1.0;
};

class ShotsApp final : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override { return "VPShots"; }
    const juce::String getApplicationVersion() override { return "1"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override { quit(); }
    void unhandledException (const std::exception*, const juce::String&, int) override {}

    void initialise (const juce::String&) override
    {
        std::signal (SIGSEGV, crashed);
        std::signal (SIGBUS, crashed);
        const auto args = getCommandLineParameterArray();
        if (args.isEmpty())
        {
            std::printf ("usage: VPShots <out-dir> [song.wav] [it|en] | --input-channels\n");
            setApplicationReturnValue (2);
            quit();
            return;
        }
        inputChannelTest = args[0] == "--input-channels";
        const auto dir = inputChannelTest ? juce::File::getSpecialLocation (juce::File::tempDirectory)
                                         : juce::File::getCurrentWorkingDirectory().getChildFile (args[0]);
        dir.createDirectory();
        const auto songFile = args.size() > 1
            ? juce::File::getCurrentWorkingDirectory().getChildFile (args[1])
            : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                  .getChildFile ("vp-bench/wav/39_I_WANNA_DANCE_44k.wav");
        vpui::languageOverride() = (args.size() > 2 && args[2] == "en") ? 0 : 1;

        if (! inputChannelTest)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (songFile));
            if (reader == nullptr)
            {
                std::printf ("cannot read %s\n", songFile.getFullPathName().toRawUTF8());
                setApplicationReturnValue (1);
                quit();
                return;
            }
            song.setSize (2, static_cast<int> (juce::jmin<juce::int64> (reader->lengthInSamples,
                                                                         static_cast<juce::int64> (reader->sampleRate * 60.0))));
            reader->read (&song, 0, song.getNumSamples(), 0, true, true);

        }

        // The page's preferences in a temporary home, not the user's.
        const auto home = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("vpshots-home");
        home.deleteRecursively();
        home.createDirectory();
        setenv ("HOME", home.getFullPathName().toRawUTF8(), 1);

        page = std::make_unique<MainComponent>();
        // Registered and chosen before the page's own request to open audio
        // comes off the message queue: that open lands on the null device.
        page->deviceManager.addAudioDeviceType (std::make_unique<NullType>());
        page->deviceManager.setCurrentAudioDeviceType ("Null", true);
        if (! inputChannelTest)
            feeder = std::make_unique<Feeder> (song);
        if (feeder != nullptr) feeder->startTimer (10);   // ~ a 512-frame block; the page sees blocks as they come
        steps = std::make_unique<VPShotsAccess> (*page, dir, [this] (int rc)
        {
            setApplicationReturnValue (rc);
            quit();
        });
    }

    void shutdown() override
    {
        steps.reset();
        feeder.reset();
        page.reset();
    }

private:
    juce::AudioBuffer<float> song;
    std::unique_ptr<MainComponent> page;
    std::unique_ptr<Feeder> feeder;
    std::unique_ptr<VPShotsAccess> steps;
};

START_JUCE_APPLICATION (ShotsApp)
