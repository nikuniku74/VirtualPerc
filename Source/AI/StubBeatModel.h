#pragma once

#include "AI/IBeatModel.h"

namespace vp
{

class StubBeatModel final : public IBeatModel
{
public:
    bool prepare (int featureDim) override;
    void reset() override;
    bool infer (const float* features, int dim, float activations3[3]) override;

private:
    int dim = 0;
};

} // namespace vp
