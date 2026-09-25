#include "Platform/IosMicPermission.h"

#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#import <dispatch/dispatch.h>

#include <cmath>
#include <cstring>
#include <utility>

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

    bool gSessionActive = false;
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

    bool changed = false;

    if (! [s.category isEqualToString: AVAudioSessionCategoryPlayAndRecord]
        || s.categoryOptions != opts)
    {
        [s setCategory: AVAudioSessionCategoryPlayAndRecord withOptions: opts error: &err];
        err = nil;
        changed = true;
    }

    // Measurement is iOS with its hands off the signal: no AGC, no noise
    // suppression, no echo canceller between the room and the tracker.
    NSString* const wantMode = request.inputProcessing ? AVAudioSessionModeDefault
                                                       : AVAudioSessionModeMeasurement;
    if (! [s.mode isEqualToString: wantMode])
    {
        [s setMode: wantMode error: &err];
        err = nil;
        changed = true;
    }

    if (request.sampleRate > 8000.0 && ! sameRate (s.sampleRate, request.sampleRate))
    {
        [s setPreferredSampleRate: request.sampleRate error: &err];
        err = nil;
        changed = true;
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
            changed = true;
        }
    }

    // A live session that is already on these settings does not need another
    // setActive:YES. iPadOS answers a redundant activate with a brief IO restart,
    // which is the crack on a Split View resize or a window reset.
    if (changed || request.forceActivate || ! gSessionActive)
    {
        [s setActive: YES error: &err];
        gSessionActive = (err == nil);
    }
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

bool sessionInputProcessing()
{
    return [session().mode isEqualToString: AVAudioSessionModeDefault];
}

namespace
{
    id gResetObserver = nil;
    std::function<void()> gResetHandler;
}

void setMediaServicesResetHandler (std::function<void()> handler)
{
    gResetHandler = std::move (handler);

    if (gResetObserver != nil)
        return;

    // Delivered on the main queue, which is the message thread: rebuilding the
    // device is not something to start from whichever thread the notification
    // happens to arrive on.
    gResetObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName: AVAudioSessionMediaServicesWereResetNotification
                    object: nil
                     queue: [NSOperationQueue mainQueue]
                usingBlock: ^(NSNotification*)
    {
        gSessionActive = false;
        if (gResetHandler)
            gResetHandler();
    }];
}

SafeAreaInsets windowSafeAreaInsets()
{
    SafeAreaInsets out;
    UIWindow* window = nil;

    // The key window of the foreground scene. Split View and Stage Manager
    // both give the process more than one, and only the one we are actually
    // in knows where its own notch and home indicator are.
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
    {
        if (! [scene isKindOfClass: [UIWindowScene class]])
            continue;

        for (UIWindow* w in ((UIWindowScene*) scene).windows)
        {
            if (w.isKeyWindow)
            {
                window = w;
                break;
            }
            if (window == nil)
                window = w;     // a usable stand-in until a key window turns up
        }

        if (window != nil && window.isKeyWindow)
            break;
    }

    if (window == nil)
        return out;

    // Rounded up: a pixel of the status bar over the status row is still the
    // status bar over the status row.
    const UIEdgeInsets insets = window.safeAreaInsets;
    const auto pts = [] (CGFloat v)
    {
        return v > 0.0 ? static_cast<int> (std::ceil (static_cast<double> (v))) : 0;
    };
    out.top    = pts (insets.top);
    out.left   = pts (insets.left);
    out.bottom = pts (insets.bottom);
    out.right  = pts (insets.right);
    return out;
}

bool copySystemGear (int px, unsigned char* argb, int lineStride, bool white)
{
    if (px < 8 || argb == nullptr || lineStride < px * 4)
        return false;

    UIImageSymbolConfiguration* cfg =
        [UIImageSymbolConfiguration configurationWithPointSize: (CGFloat) px
                                                        weight: UIImageSymbolWeightRegular];
    UIColor* ink = white ? UIColor.whiteColor : UIColor.blackColor;
    UIImage* symbol = [[UIImage systemImageNamed: @"gearshape" withConfiguration: cfg]
                          imageWithTintColor: ink
                               renderingMode: UIImageRenderingModeAlwaysOriginal];
    if (symbol == nil)
        return false;

    UIGraphicsBeginImageContextWithOptions (CGSizeMake (px, px), false, 1.0);
    [symbol drawInRect: CGRectMake (0, 0, px, px)];
    UIImage* drawn = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    if (drawn.CGImage == nil)
        return false;

    std::memset (argb, 0, static_cast<size_t> (lineStride) * static_cast<size_t> (px));
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate (argb, (size_t) px, (size_t) px, 8,
                                              (size_t) lineStride, space,
                                              kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease (space);
    if (ctx == nullptr)
        return false;

    CGContextDrawImage (ctx, CGRectMake (0, 0, px, px), drawn.CGImage);
    CGContextRelease (ctx);
    return true;
}

} // namespace vp
