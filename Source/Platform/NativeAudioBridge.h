#pragma once

#include "Audio/VirtualPercussionEngine.h"

#include <vector>

namespace vp
{

class NativeAudioBridge
{
public:
    void prepare (int maxFrames, int maxInChannels, int maxOutChannels);
    void processInterleaved (VirtualPercussionEngine& engine,
                             const float* inInterleaved, int inChannels,
                             float* outInterleaved, int outChannels,
                             int numFrames) noexcept;

private:
    std::vector<std::vector<float>> inPlanar;
    std::vector<std::vector<float>> outPlanar;
    std::vector<const float*> inPtrs;
    std::vector<float*> outPtrs;
    int maxFrames = 0;
};

} // namespace vp

/*
  iOS (JUCE already wraps Core Audio RemoteIO):

    void MainComponent::getNextAudioBlock (const AudioSourceChannelInfo& info)
    {
        engine.process (inPtrs, nIn, outPtrs, nOut, info.numSamples);
    }

  iOS (raw AURemoteIO, if vp_core is used without JUCE):

    OSStatus render (void* ref, AudioUnitRenderActionFlags*,
                     const AudioTimeStamp*, UInt32, UInt32 frames,
                     AudioBufferList* out)
    {
        auto* self = static_cast<NativeAudioBridge*> (ref);
        // render input into inList, then:
        self->processInterleaved (engine, in, inCh, out, outCh, (int) frames);
        return noErr;
    }

  Android (Oboe, MVP 6):

    DataCallbackResult onAudioReady (AudioStream* s, void* data, int32_t frames)
    {
        bridge.processInterleaved (engine,
                                   static_cast<const float*> (input), inCh,
                                   static_cast<float*> (data), outCh,
                                   frames);
        return DataCallbackResult::Continue;
    }

  Never call OrtRun, file I/O, or malloc in these callbacks.
*/
