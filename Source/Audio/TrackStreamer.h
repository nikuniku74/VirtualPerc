#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <atomic>

namespace vp
{

/** Plays a loaded file to the audio thread without the audio thread ever
    waiting on a lock.

    juce::AudioTransportSource reads ahead through BufferingAudioSource, whose
    read thread holds `callbackLock` while it decodes a chunk of the file, and
    whose audio-side getNextAudioBlock takes the same lock. When the read
    thread is pre-empted mid-decode (an MP3 through the iOS file provider) the
    audio callback waits and misses its deadline: the xruns heard as small
    glitches in BRANO (docs/TODO.md item 80). Here the read thread decodes into
    a lock-free single-producer/single-consumer ring; the audio thread only
    ever reads from it, and a seek or a new file is handed over with a
    try-lock the audio thread never blocks on (it plays one block of silence
    instead). Same method names as AudioTransportSource so the call sites stay.
*/
class TrackStreamer final : private juce::TimeSliceClient
{
public:
    explicit TrackStreamer (juce::TimeSliceThread& t) : thread (t) {}
    ~TrackStreamer() override { setSource (nullptr); }

    /** Message thread. `reader` is not owned and must outlive the next setSource. */
    void setSource (juce::AudioFormatReader* newReader)
    {
        thread.removeTimeSliceClient (this);
        {
            const juce::ScopedLock rl (readerLock);
            const juce::SpinLock::ScopedLockType sl (swapLock);
            reader = newReader;
            playing.store (false);
            resetPositionLocked (0);
        }
        if (reader != nullptr)
            thread.addTimeSliceClient (this);
    }

    /** Message thread, before the audio device starts. */
    void prepareToPlay (int maxBlock, double sampleRate)
    {
        const juce::SpinLock::ScopedLockType sl (swapLock);
        deviceRate = sampleRate;
        // Input frames one output block can need, at up to 4:1 downsampling.
        scratch.setSize (2, maxBlock * 4 + 16, false, false, true);
        needInterpReset.store (true);
    }

    void releaseResources() {}

    void start()  { if (reader != nullptr) playing.store (true); }
    void stop()   { playing.store (false); }
    bool isPlaying() const noexcept { return playing.load(); }

    double getLengthInSeconds() const
    {
        return reader != nullptr && reader->sampleRate > 0.0
                   ? static_cast<double> (reader->lengthInSamples) / reader->sampleRate : 0.0;
    }

    double getCurrentPosition() const
    {
        return reader != nullptr && reader->sampleRate > 0.0
                   ? static_cast<double> (playPos.load()) / reader->sampleRate : 0.0;
    }

    bool hasStreamFinished() const
    {
        return reader != nullptr && playPos.load() >= reader->lengthInSamples - 8;
    }

    /** Message thread. */
    void setPosition (double seconds)
    {
        if (reader == nullptr)
            return;
        const auto frame = juce::jlimit<juce::int64> (0, reader->lengthInSamples,
            static_cast<juce::int64> (seconds * reader->sampleRate));
        const juce::ScopedLock rl (readerLock);
        const juce::SpinLock::ScopedLockType sl (swapLock);
        resetPositionLocked (frame);
    }

    /** Audio thread: never blocks, never allocates. */
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
    {
        info.clearActiveBufferRegion();
        if (! playing.load(std::memory_order_relaxed))
            return;
        const juce::SpinLock::ScopedTryLockType sl (swapLock);
        if (! sl.isLocked() || reader == nullptr || deviceRate <= 0.0 || reader->sampleRate <= 0.0)
            return;
        if (needInterpReset.exchange (false))
            for (auto& i : interp)
                i.reset();

        const double ratio = reader->sampleRate / deviceRate;
        const int numOut = info.numSamples;
        const int want = static_cast<int> (std::ceil (numOut * ratio)) + 8;
        if (want > scratch.getNumSamples())
            return;
        const int avail = fifo.getNumReady();
        if (avail < want)
        {
            // Nothing ready: the file has ended, or the read thread is late.
            if (readPos.load() < reader->lengthInSamples)
                ++underruns;
            return;
        }

        int s1, n1, s2, n2;
        fifo.prepareToRead (want, s1, n1, s2, n2);
        for (int c = 0; c < 2; ++c)
        {
            if (n1 > 0) scratch.copyFrom (c, 0, ring, c, s1, n1);
            if (n2 > 0) scratch.copyFrom (c, n1, ring, c, s2, n2);
        }
        // Both channels consume the same count: same ratio, same state.
        const int outCh = juce::jmin (2, info.buffer->getNumChannels());
        int used = 0;
        for (int c = 0; c < outCh; ++c)
            used = interp[c].process (ratio, scratch.getReadPointer (c),
                                      info.buffer->getWritePointer (c, info.startSample), numOut);
        fifo.finishedRead (used);
        playPos.fetch_add (used);
    }

    /** Audio thread reads; message thread may log it. */
    int underrunCount() const noexcept { return underruns.load(); }

private:
    static constexpr int kRingFrames = 1 << 18;   // 5.9 s at 44.1 kHz
    static constexpr int kChunk = 4096;

    void resetPositionLocked (juce::int64 frame)
    {
        fifo.reset();
        readPos.store (frame);
        playPos.store (frame);
        needInterpReset.store (true);
    }

    int useTimeSlice() override
    {
        const juce::ScopedLock rl (readerLock);
        if (reader == nullptr)
            return 100;
        const auto pos = readPos.load();
        const auto left = reader->lengthInSamples - pos;
        const int space = fifo.getFreeSpace();
        if (left <= 0 || space < kChunk)
            return 20;
        const int n = static_cast<int> (juce::jmin<juce::int64> (kChunk, left));
        readBuf.setSize (2, kChunk, false, false, true);
        reader->read (&readBuf, 0, n, pos, true, true);
        if (reader->numChannels < 2)
            readBuf.copyFrom (1, 0, readBuf, 0, 0, n);
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        for (int c = 0; c < 2; ++c)
        {
            if (n1 > 0) ring.copyFrom (c, s1, readBuf, c, 0, n1);
            if (n2 > 0) ring.copyFrom (c, s2, readBuf, c, n1, n2);
        }
        fifo.finishedWrite (n1 + n2);
        readPos.store (pos + n1 + n2);
        return 0;
    }

    juce::TimeSliceThread& thread;
    juce::AudioFormatReader* reader = nullptr;
    juce::CriticalSection readerLock;     // read thread vs message thread
    juce::SpinLock swapLock;              // message thread vs audio (try-lock only)
    juce::AbstractFifo fifo { kRingFrames };
    juce::AudioBuffer<float> ring { 2, kRingFrames };
    juce::AudioBuffer<float> readBuf { 2, kChunk };
    juce::AudioBuffer<float> scratch { 2, 16 };
    juce::LagrangeInterpolator interp[2];
    double deviceRate = 0.0;
    std::atomic<bool> playing { false };
    std::atomic<bool> needInterpReset { true };
    std::atomic<juce::int64> readPos { 0 };
    std::atomic<juce::int64> playPos { 0 };
    std::atomic<int> underruns { 0 };
};

} // namespace vp
