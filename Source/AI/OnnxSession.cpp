#include "AI/OnnxSession.h"

#include <algorithm>
#include <cstring>
#include <vector>

#if defined(VP_USE_ONNX) && VP_USE_ONNX
#include "onnxruntime_c_api.h"
#if defined(VP_ORT_COREML) && defined(__APPLE__)
#include "coreml_provider_factory.h"
#endif
#endif

namespace vp
{

#if defined(VP_USE_ONNX) && VP_USE_ONNX

struct OnnxSession::Impl
{
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtSessionOptions* options = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* mem = nullptr;
    std::vector<float> input;
    std::vector<float> h;
    std::vector<float> c;
    std::vector<int64_t> inShape;
    std::vector<int64_t> stateShape;
    bool loaded = false;
};

OnnxSession::OnnxSession() : impl (new Impl)
{
    impl->api = OrtGetApiBase()->GetApi (ORT_API_VERSION);
}

OnnxSession::~OnnxSession()
{
    releaseOrtHandles();
    delete impl;
    impl = nullptr;
}

bool OnnxSession::available() const noexcept
{
    return impl != nullptr && impl->api != nullptr;
}

void OnnxSession::releaseOrtHandles()
{
    if (impl == nullptr || impl->api == nullptr)
        return;
    const OrtApi* api = impl->api;
    if (impl->session != nullptr) { api->ReleaseSession (impl->session); impl->session = nullptr; }
    if (impl->options != nullptr) { api->ReleaseSessionOptions (impl->options); impl->options = nullptr; }
    if (impl->mem != nullptr) { api->ReleaseMemoryInfo (impl->mem); impl->mem = nullptr; }
    if (impl->env != nullptr) { api->ReleaseEnv (impl->env); impl->env = nullptr; }
    impl->loaded = false;
}

bool OnnxSession::beginLoad (const OnnxModelConfig& cfg)
{
    if (impl == nullptr || impl->api == nullptr)
    {
        error = "ONNX API unavailable";
        return false;
    }

    const OrtApi* api = impl->api;
    OrtStatus* st = api->CreateEnv (ORT_LOGGING_LEVEL_WARNING, "vp_beat", &impl->env);
    if (st != nullptr)
    {
        error = "CreateEnv failed";
        api->ReleaseStatus (st);
        return false;
    }

    st = api->CreateSessionOptions (&impl->options);
    if (st != nullptr)
    {
        error = "CreateSessionOptions failed";
        api->ReleaseStatus (st);
        return false;
    }

    api->SetIntraOpNumThreads (impl->options, 1);
    api->SetSessionGraphOptimizationLevel (impl->options, ORT_ENABLE_EXTENDED);

#if defined(VP_ORT_COREML) && defined(__APPLE__)
    if (cfg.useCoreMlOnIos)
    {
        const uint32_t flags = COREML_FLAG_ENABLE_ON_SUBGRAPH;
        OrtStatus* cms = OrtSessionOptionsAppendExecutionProvider_CoreML (impl->options, flags);
        if (cms != nullptr)
            api->ReleaseStatus (cms);
    }
#else
    (void) cfg;
#endif

    st = api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault, &impl->mem);
    if (st != nullptr)
    {
        error = "CreateCpuMemoryInfo failed";
        api->ReleaseStatus (st);
        return false;
    }

    impl->inShape = { 1, cfg.timeSteps, cfg.featureDim };
    impl->stateShape = { cfg.lstmLayers, 1, cfg.lstmHidden };
    impl->input.assign (static_cast<size_t> (std::max (1, cfg.timeSteps) * cfg.featureDim), 0.0f);
    impl->h.assign (static_cast<size_t> (std::max (1, cfg.lstmLayers) * cfg.lstmHidden), 0.0f);
    impl->c.assign (impl->h.size(), 0.0f);
    return true;
}

bool OnnxSession::load (const char* modelPath, const OnnxModelConfig& cfg)
{
    error = "";
    config = cfg;
    if (modelPath == nullptr)
    {
        error = "ONNX API unavailable";
        return false;
    }
    releaseOrtHandles();
    if (! beginLoad (cfg))
        return false;

    const OrtApi* api = impl->api;
    OrtStatus* st = api->CreateSession (impl->env, modelPath, impl->options, &impl->session);
    if (st != nullptr && cfg.useCoreMlOnIos)
    {
        api->ReleaseStatus (st);
        releaseOrtHandles();
        config.useCoreMlOnIos = false;
        if (! beginLoad (config))
            return false;
        st = api->CreateSession (impl->env, modelPath, impl->options, &impl->session);
    }
    if (st != nullptr)
    {
        error = "CreateSession failed (check model path / EP)";
        api->ReleaseStatus (st);
        return false;
    }
    impl->loaded = true;
    return true;
}

bool OnnxSession::loadFromMemory (const void* data, size_t numBytes, const OnnxModelConfig& cfg)
{
    error = "";
    config = cfg;
    if (data == nullptr || numBytes == 0)
    {
        error = "ONNX API unavailable";
        return false;
    }
    releaseOrtHandles();
    if (! beginLoad (cfg))
        return false;

    const OrtApi* api = impl->api;
    OrtStatus* st = api->CreateSessionFromArray (impl->env, data, numBytes, impl->options, &impl->session);
    if (st != nullptr && cfg.useCoreMlOnIos)
    {
        api->ReleaseStatus (st);
        releaseOrtHandles();
        config.useCoreMlOnIos = false;
        if (! beginLoad (config))
            return false;
        st = api->CreateSessionFromArray (impl->env, data, numBytes, impl->options, &impl->session);
    }
    if (st != nullptr)
    {
        error = "CreateSessionFromArray failed";
        api->ReleaseStatus (st);
        return false;
    }
    impl->loaded = true;
    return true;
}

void OnnxSession::resetState()
{
    if (impl == nullptr)
        return;
    std::fill (impl->h.begin(), impl->h.end(), 0.0f);
    std::fill (impl->c.begin(), impl->c.end(), 0.0f);
    std::fill (impl->input.begin(), impl->input.end(), 0.0f);
}

bool OnnxSession::run (const float* features, int dim, float* logits, int numLogits)
{
    if (impl == nullptr || ! impl->loaded || features == nullptr || logits == nullptr)
        return false;
    if (dim != config.featureDim || numLogits < config.numClasses)
        return false;

    const OrtApi* api = impl->api;
    const int T = std::max (1, config.timeSteps);
    const int F = config.featureDim;

    if (T <= 1)
    {
        std::memcpy (impl->input.data(), features, static_cast<size_t> (F) * sizeof (float));
    }
    else
    {
        std::memmove (impl->input.data(),
                      impl->input.data() + F,
                      static_cast<size_t> ((T - 1) * F) * sizeof (float));
        std::memcpy (impl->input.data() + (T - 1) * F, features, static_cast<size_t> (F) * sizeof (float));
    }

    OrtValue* inTensor = nullptr;
    OrtValue* hIn = nullptr;
    OrtValue* cIn = nullptr;
    OrtStatus* st = api->CreateTensorWithDataAsOrtValue (
        impl->mem, impl->input.data(), impl->input.size() * sizeof (float),
        impl->inShape.data(), impl->inShape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inTensor);
    if (st != nullptr)
    {
        api->ReleaseStatus (st);
        return false;
    }

    const char* inputNames[3];
    const OrtValue* inputs[3];
    size_t nIn = 1;
    inputNames[0] = config.inputName;
    inputs[0] = inTensor;

    if (config.hasLstmState)
    {
        // Both statuses are checked. Ignoring them leaks the status object on
        // failure and then hands Run a null input, which fails anyway - so the
        // cost of not looking was a leak per frame, fifty times a second, for
        // as long as whatever went wrong lasted.
        OrtStatus* sh = api->CreateTensorWithDataAsOrtValue (
            impl->mem, impl->h.data(), impl->h.size() * sizeof (float),
            impl->stateShape.data(), impl->stateShape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &hIn);
        OrtStatus* sc = api->CreateTensorWithDataAsOrtValue (
            impl->mem, impl->c.data(), impl->c.size() * sizeof (float),
            impl->stateShape.data(), impl->stateShape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &cIn);
        if (sh != nullptr) api->ReleaseStatus (sh);
        if (sc != nullptr) api->ReleaseStatus (sc);
        if (hIn == nullptr || cIn == nullptr)
        {
            api->ReleaseValue (inTensor);
            if (hIn != nullptr) api->ReleaseValue (hIn);
            if (cIn != nullptr) api->ReleaseValue (cIn);
            return false;
        }
        inputNames[1] = config.stateInH;
        inputNames[2] = config.stateInC;
        inputs[1] = hIn;
        inputs[2] = cIn;
        nIn = 3;
    }

    const char* outputNames[3];
    OrtValue* outputs[3] { nullptr, nullptr, nullptr };
    size_t nOut = 1;
    outputNames[0] = config.outputName;
    if (config.hasLstmState)
    {
        outputNames[1] = config.stateOutH;
        outputNames[2] = config.stateOutC;
        nOut = 3;
    }

    st = api->Run (impl->session, nullptr, inputNames, inputs, nIn, outputNames, nOut, outputs);
    api->ReleaseValue (inTensor);
    if (hIn != nullptr) api->ReleaseValue (hIn);
    if (cIn != nullptr) api->ReleaseValue (cIn);
    if (st != nullptr)
    {
        api->ReleaseStatus (st);
        // A failed Run may still have filled some of the output slots.
        for (size_t i = 0; i < nOut; ++i)
            if (outputs[i] != nullptr)
                api->ReleaseValue (outputs[i]);
        return false;
    }

    float* outData = nullptr;
    api->GetTensorMutableData (outputs[0], reinterpret_cast<void**> (&outData));
    if (outData != nullptr)
    {
        OrtTensorTypeAndShapeInfo* info = nullptr;
        api->GetTensorTypeAndShape (outputs[0], &info);
        size_t elem = static_cast<size_t> (config.numClasses);
        if (info != nullptr)
        {
            api->GetTensorShapeElementCount (info, &elem);
            api->ReleaseTensorTypeAndShapeInfo (info);
        }
        // Read the last numClasses values. A tensor holding fewer than that is
        // a model that is not the one this was configured for, and it used to
        // be copied out of anyway - past the end of ONNX Runtime's own buffer.
        // Fail closed instead, the same as a model that would not load.
        const size_t want = static_cast<size_t> (config.numClasses);
        if (elem < want)
        {
            for (size_t i = 0; i < nOut; ++i)
                if (outputs[i] != nullptr)
                    api->ReleaseValue (outputs[i]);
            return false;
        }
        std::memcpy (logits, outData + (elem - want), want * sizeof (float));
    }

    if (config.hasLstmState && outputs[1] != nullptr && outputs[2] != nullptr)
    {
        float* hn = nullptr;
        float* cn = nullptr;
        api->GetTensorMutableData (outputs[1], reinterpret_cast<void**> (&hn));
        api->GetTensorMutableData (outputs[2], reinterpret_cast<void**> (&cn));
        if (hn != nullptr)
            std::memcpy (impl->h.data(), hn, impl->h.size() * sizeof (float));
        if (cn != nullptr)
            std::memcpy (impl->c.data(), cn, impl->c.size() * sizeof (float));
    }

    for (size_t i = 0; i < nOut; ++i)
        if (outputs[i] != nullptr)
            api->ReleaseValue (outputs[i]);

    return true;
}

#else

struct OnnxSession::Impl {};

OnnxSession::OnnxSession() : impl (nullptr) {}
OnnxSession::~OnnxSession() {}

bool OnnxSession::available() const noexcept { return false; }

bool OnnxSession::load (const char*, const OnnxModelConfig&)
{
    error = "ONNX Runtime not compiled in (VP_USE_ONNX=OFF)";
    return false;
}

bool OnnxSession::loadFromMemory (const void*, size_t, const OnnxModelConfig&)
{
    error = "ONNX Runtime not compiled in (VP_USE_ONNX=OFF)";
    return false;
}

bool OnnxSession::beginLoad (const OnnxModelConfig&)
{
    error = "ONNX Runtime not compiled in (VP_USE_ONNX=OFF)";
    return false;
}

void OnnxSession::resetState() {}

void OnnxSession::releaseOrtHandles() {}

bool OnnxSession::run (const float*, int, float*, int) { return false; }

#endif

} // namespace vp
