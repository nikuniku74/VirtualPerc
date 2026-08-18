#pragma once

namespace vp
{

class IBeatModel
{
public:
    virtual ~IBeatModel() = default;
    virtual bool prepare (int featureDim) = 0;
    virtual void reset() = 0;
    virtual bool infer (const float* features, int dim, float activations3[3]) = 0;
    virtual bool usesOnnx() const noexcept { return false; }
};

} // namespace vp
