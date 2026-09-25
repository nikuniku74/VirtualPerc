#include "Platform/IosMicPermission.h"

namespace vp
{

void requestMicrophoneAccess (std::function<void (bool granted)> callback)
{
    callback (true);
}

/** Off-device there is no session to prepare: the host's own device manager
    picks the rate and the buffer, and MainComponent reads 0 here as "leave it
    to the device" rather than as an answer. */
void prepareAudioSession (const AudioSessionRequest&) {}

/** No window insets off-device: the desktop build pads by its own numbers. */
SafeAreaInsets windowSafeAreaInsets() { return {}; }

double sessionSampleRate()   { return 0.0; }
int    sessionBufferFrames() { return 0; }
int    sessionInputChannels()  { return 0; }
int    sessionOutputChannels() { return 0; }
std::string sessionRouteName() { return {}; }
bool   otherAudioPlaying()   { return false; }
bool   sessionInputProcessing() { return true; }
void   setMediaServicesResetHandler (std::function<void()>) {}
bool   copySystemGear (int, unsigned char*, int, bool) { return false; }

} // namespace vp
