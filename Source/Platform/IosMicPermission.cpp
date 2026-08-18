#include "Platform/IosMicPermission.h"

namespace vp
{

void requestMicrophoneAccess (std::function<void (bool granted)> callback)
{
    callback (true);
}

void configurePlaybackSession() {}

double sessionSampleRate() { return 0.0; }

} // namespace vp
