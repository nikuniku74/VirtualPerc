#pragma once

#include "AI/OnnxBeatModel.h"

#include <string>

namespace vp
{

OnnxModelConfig defaultBeatOnnxConfig() noexcept;
std::string locateBeatModelFile();
bool loadDefaultBeatModel (OnnxBeatModel& model);

}
