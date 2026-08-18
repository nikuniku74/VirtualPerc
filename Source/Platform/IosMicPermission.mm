#include "Platform/IosMicPermission.h"

#import <AVFoundation/AVFoundation.h>
#import <dispatch/dispatch.h>

namespace vp
{

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
    const auto status = [AVAudioSession sharedInstance].recordPermission;
    if (status == AVAudioSessionRecordPermissionGranted)
    {
        finish (true);
        return;
    }

    [[AVAudioSession sharedInstance] requestRecordPermission: ^(BOOL granted)
    {
        finish (granted);
    }];
#pragma clang diagnostic pop
}

void configurePlaybackSession()
{
    // Called after JUCE has opened the device. Do not enable HFP Bluetooth:
    // that route is 8–16 kHz and makes Spotify + shaker sound slow and crushed.
    AVAudioSession* session = [AVAudioSession sharedInstance];
    NSError* err = nil;
    const AVAudioSessionCategoryOptions opts =
        AVAudioSessionCategoryOptionMixWithOthers
        | AVAudioSessionCategoryOptionDefaultToSpeaker
        | AVAudioSessionCategoryOptionAllowBluetoothA2DP;
    [session setCategory:AVAudioSessionCategoryPlayAndRecord
             withOptions:opts
                   error:&err];
    [session setMode:AVAudioSessionModeMeasurement error:&err];
    [session setPreferredSampleRate:48000.0 error:&err];
    [session setPreferredIOBufferDuration:(256.0 / 48000.0) error:&err];
    [session setActive:YES error:&err];
}

double sessionSampleRate()
{
    const double sr = [AVAudioSession sharedInstance].sampleRate;
    return sr > 1.0 ? sr : 0.0;
}

} // namespace vp
