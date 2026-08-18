#include "AI/StubBeatModel.h"

namespace vp
{

bool StubBeatModel::prepare (int featureDim)
{
    dim = featureDim;
    return featureDim > 0;
}

void StubBeatModel::reset() {}

bool StubBeatModel::infer (const float* features, int featureDim, float activations3[3])
{
    if (activations3 == nullptr || features == nullptr || featureDim != dim)
        return false;

    float energy = 0.0f;
    const int start = featureDim / 2;
    for (int i = start; i < featureDim; ++i)
        energy += features[i] > 0.0f ? features[i] : 0.0f;

    const float pBeat = energy > 8.0f ? 0.85f : 0.05f;
    activations3[0] = pBeat;
    activations3[1] = 0.05f;
    activations3[2] = 1.0f - pBeat - 0.05f;
    return true;
}

} // namespace vp
