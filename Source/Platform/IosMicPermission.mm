#include "Platform/IosMicPermission.h"

#import <AVFoundation/AVFoundation.h>
#import <dispatch/dispatch.h>

#include <cmath>

namespace vp
{

namespace
{
    AVAudioSession* session() { return [AVAudioSession sharedInstance]; }

    /** Rates are doubles that came out of Core Audio; 48000 and 47999.99 are the
        same clock and re-clocking for the difference is a click for nothing. */
    bool sameRate (double a, double b) noexcept
    {
        return std::abs (a - b) < 1.0;
    }

    std::string portNames (NSArray<AVAudioSessionPortDescription*>* ports)
    {
        std::string out;
        for (AVAudioSessionPortDescription* p in ports)
        {
            if (! out.empty())
                out += " + ";
            out += [p.portName UTF8String];
        }
        return out;
    }
}

void requestMicrophoneAccess (std::function<void (bool granted)> callback)
{
    auto finish = [cb = std::move (callback)] (bool granted)
    {
        dispatch_async (dispatch_get_main_queue(), ^
        {
            if (cb)
                cb (granted);
        });
    };

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const auto status = session().recordPermission;
    if (status == AVAudioSessionRecordPermissionGranted)
    {
        finish (true);
        return;
    }

    [session() requestRecordPermission: ^(BOOL granted)
    {
        finish (granted);
    }];
#pragma clang diagnostic pop
}

void prepareAudioSession (const AudioSessionRequest& request)
{
    AVAudioSession* s = session();
    NSError* err = nil;

    // MixWithOthers is the whole reason the track being played along to keeps
    // playing: without it, activating a PlayAndRecord session takes the hardware
    // for this app alone and everything else is interrupted.
    // No HFP Bluetooth: that route is 8-16 kHz and makes the mix sound slow and
    // crushed. A2DP and AirPlay are fine.
    const AVAudioSessionCategoryOptions opts =
        AVAudioSessionCategoryOptionMixWithOthers
        | AVAudioSessionCategoryOptionDefaultToSpeaker
        | AVAudioSessionCategoryOptionAllowBluetoothA2DP
        | AVAudioSessionCategoryOptionAllowAirPlay;

    if (! [s.category isEqualToString: AVAudioSessionCategoryPlayAndRecord]
        || s.categoryOptions != opts)
    {
        [s setCategory: AVAudioSessionCategoryPlayAndRecord withOptions: opts error: &err];
        err = nil;
    }

    // Measurement is iOS with its hands off the signal: no AGC, no noise
    // suppression, no echo canceller between the room and the tracker.
    NSString* const wantMode = request.inputProcessing ? AVAudioSessionModeDefault
                                                       : AVAudioSessionModeMeasurement;
    if (! [s.mode isEqualToString: wantMode])
    {
        [s setMode: wantMode error: &err];
        err = nil;
    }

    if (request.sampleRate > 8000.0 && ! sameRate (s.sampleRate, request.sampleRate))
    {
        [s setPreferredSampleRate: request.sampleRate error: &err];
        err = nil;
    }

    if (request.bufferFrames > 0)
    {
        const double rate = request.sampleRate > 8000.0
                                ? request.sampleRate
                                : (s.sampleRate > 8000.0 ? s.sampleRate : 48000.0);
        const double wanted = static_cast<double> (request.bufferFrames) / rate;
        // Half a frame of tolerance: IOBufferDuration comes back as whatever
        // the hardware rounded to, never as the number that was asked for.
        if (std::abs (s.IOBufferDuration - wanted) > 0.5 / rate)
        {
            [s setPreferredIOBufferDuration: wanted error: &err];
            err = nil;
        }
    }

    [s setActive: YES error: &err];
}

double sessionSampleRate()
{
    const double sr = session().sampleRate;
    return sr > 1.0 ? sr : 0.0;
}

int sessionBufferFrames()
{
    AVAudioSession* s = session();
    const double sr = s.sampleRate;
    const double dur = s.IOBufferDuration;
    if (sr <= 1.0 || dur <= 0.0)
        return 0;
    return static_cast<int> (sr * dur + 0.5);
}

int sessionInputChannels()  { return static_cast<int> (session().inputNumberOfChannels); }
int sessionOutputChannels() { return static_cast<int> (session().outputNumberOfChannels); }

std::string sessionRouteName()
{
    AVAudioSessionRouteDescription* route = session().currentRoute;
    const std::string in = portNames (route.inputs);
    const std::string out = portNames (route.outputs);

    if (in.empty())
        return out;
    if (out.empty() || in == out)
        return in;
    return in + " / " + out;
}

bool otherAudioPlaying()
{
    return session().secondaryAudioShouldBeSilencedHint || session().isOtherAudioPlaying;
}

} // namespace vp
