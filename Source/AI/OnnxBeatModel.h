#pragma once

#include "AI/IBeatModel.h"
#include "AI/OnnxSession.h"

namespace vp
{

class OnnxBeatModel final : public IBeatModel
{
public:
    bool loadFile (const char* modelPath, const OnnxModelConfig& cfg);
    bool loadMemory (const void* data, size_t numBytes, const OnnxModelConfig& cfg);
    bool prepare (int featureDim) override;
    void reset() override;
    bool infer (const float* features, int dim, float activations3[3]) override;
    bool usesOnnx() const noexcept override { return loaded; }

    const char* lastError() const noexcept { return session.lastError(); }
    bool ready() const noexcept { return loaded; }

private:
    OnnxSession session;
    OnnxModelConfig config {};
    bool loaded = false;
};

} // namespace vp
