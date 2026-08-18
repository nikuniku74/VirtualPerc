#pragma once

#include <functional>

namespace vp
{

void requestMicrophoneAccess (std::function<void (bool granted)> callback);
void configurePlaybackSession();
double sessionSampleRate();

} // namespace vp
