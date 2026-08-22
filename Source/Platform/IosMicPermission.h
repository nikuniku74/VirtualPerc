#pragma once

#include <functional>
#include <string>

namespace vp
{

void requestMicrophoneAccess (std::function<void (bool granted)> callback);

/** What the app asks the audio session for before the device is opened.

    A rate or a buffer of 0 means "whatever the hardware is already running
    at". That is not a shrug: on a rig where the mixer holds the clock - an
    X-Air at 48 kHz - asking for the number it is already on is the difference
    between opening silently and re-clocking the interface under everything
    else that is playing through it. */
struct AudioSessionRequest
{
    double sampleRate = 0.0;
    int    bufferFrames = 0;
    /** iOS AGC / noise suppression / echo cancellation. Off is what the
        analysis wants; the setting exists because a route that misbehaves
        under measurement mode is a real thing on external interfaces. */
    bool   inputProcessing = false;
};

/** Category, options, mode, rate and buffer, applied before the audio device
    is opened - and applied only where the session is not already there. Every
    write to a live session costs a reconfiguration of the hardware, which on an
    external interface is a click, so the ones that would change nothing are
    skipped. */
void prepareAudioSession (const AudioSessionRequest& request);

/** What the hardware actually settled on. 0 / empty off-device. */
double sessionSampleRate();
int    sessionBufferFrames();
int    sessionInputChannels();
int    sessionOutputChannels();
/** The route, as something to show a player: "X-AIR" rather than a port UID. */
std::string sessionRouteName();
/** True while another app - the track being played along to - holds audio. */
bool   otherAudioPlaying();

} // namespace vp
