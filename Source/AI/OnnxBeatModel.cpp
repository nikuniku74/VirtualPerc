#include "AI/OnnxBeatModel.h"

#include <algorithm>
#include <cmath>

namespace vp
{

bool OnnxBeatModel::loadFile (const char* modelPath, const OnnxModelConfig& cfg)
{
    config = cfg;
    loaded = session.load (modelPath, cfg);
    return loaded;
}

bool OnnxBeatModel::loadMemory (const void* data, size_t numBytes, const OnnxModelConfig& cfg)
{
    config = cfg;
    loaded = session.loadFromMemory (data, numBytes, cfg);
    return loaded;
}

bool OnnxBeatModel::prepare (int featureDim)
{
    return loaded && featureDim == config.featureDim;
}

void OnnxBeatModel::reset()
{
    session.resetState();
}

bool OnnxBeatModel::infer (const float* features, int dim, float activations3[3])
{
    if (! loaded || features == nullptr || activations3 == nullptr)
        return false;

    float logits[4] {};
    if (! session.run (features, dim, logits, 3))
        return false;

    const float m = std::max (logits[0], std::max (logits[1], logits[2]));
    float e0 = std::exp (logits[0] - m);
    float e1 = std::exp (logits[1] - m);
    float e2 = std::exp (logits[2] - m);
    const float s = e0 + e1 + e2;
    activations3[0] = e0 / s;
    activations3[1] = e1 / s;
    activations3[2] = e2 / s;
    return true;
}

} // namespace vp
