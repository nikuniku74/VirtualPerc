#pragma once

#include <cstddef>

namespace vp
{

struct OnnxModelConfig
{
    const char* inputName     = "features";
    const char* outputName    = "logits";
    const char* stateInH      = "h0";
    const char* stateInC      = "c0";
    const char* stateOutH     = "hn";
    const char* stateOutC     = "cn";
    int featureDim            = 272;
    int timeSteps             = 1;
    int numClasses            = 3;
    int lstmLayers            = 2;
    int lstmHidden            = 150;
    bool hasLstmState         = true;
    bool useCoreMlOnIos       = false;
    bool useNnapiOnAndroid    = false;
};

class OnnxSession
{
public:
    OnnxSession();
    ~OnnxSession();

    OnnxSession (const OnnxSession&) = delete;
    OnnxSession& operator= (const OnnxSession&) = delete;

    bool available() const noexcept;
    bool load (const char* modelPath, const OnnxModelConfig& cfg);
    bool loadFromMemory (const void* data, size_t numBytes, const OnnxModelConfig& cfg);
    void resetState();
    bool run (const float* features, int dim, float* logits, int numLogits);

    const char* lastError() const noexcept { return error; }

private:
    bool beginLoad (const OnnxModelConfig& cfg);
    void releaseOrtHandles();

    struct Impl;
    Impl* impl = nullptr;
    OnnxModelConfig config {};
    const char* error = "ONNX Runtime not compiled in (VP_USE_ONNX=OFF)";
};

} // namespace vp
