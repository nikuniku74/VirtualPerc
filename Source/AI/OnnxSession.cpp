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
    // Built once on the first run and reused: they wrap the buffers above,
    // which never reallocate after beginLoad. The state comes back into its
    // own pair (ONNX Runtime may still read h/c while it writes hn/cn) and is
    // copied over after the call. Only the logits are still allocated by
    // ONNX Runtime per call: their shape is the model's to choose.
    std::vector<float> hn;
    std::vector<float> cn;
    OrtValue* inT = nullptr;
    OrtValue* hT = nullptr;
    OrtValue* cT = nullptr;
    OrtValue* hnT = nullptr;
    OrtValue* cnT = nullptr;
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
    for (OrtValue** v : { &impl->inT, &impl->hT, &impl->cT, &impl->hnT, &impl->cnT })
        if (*v != nullptr) { api->ReleaseValue (*v); *v = nullptr; }
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
    impl->hn.assign (impl->h.size(), 0.0f);
    impl->cn.assign (impl->h.size(), 0.0f);
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

    // Every status is checked: ignoring one leaks the status object and then
    // hands Run a null value. A tensor that would not build is retried on the
    // next call rather than cached as null.
    const auto wrap = [&] (OrtValue*& v, std::vector<float>& buf, const std::vector<int64_t>& shape)
    {
        if (v != nullptr)
            return true;
        OrtStatus* s = api->CreateTensorWithDataAsOrtValue (
            impl->mem, buf.data(), buf.size() * sizeof (float),
            shape.data(), shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &v);
        if (s != nullptr)
        {
            api->ReleaseStatus (s);
            v = nullptr;
        }
        return v != nullptr;
    };
    if (! wrap (impl->inT, impl->input, impl->inShape))
        return false;
    if (config.hasLstmState
        && ! (wrap (impl->hT, impl->h, impl->stateShape) && wrap (impl->cT, impl->c, impl->stateShape)
              && wrap (impl->hnT, impl->hn, impl->stateShape) && wrap (impl->cnT, impl->cn, impl->stateShape)))
        return false;

    const char* inputNames[3] { config.inputName, config.stateInH, config.stateInC };
    const OrtValue* inputs[3] { impl->inT, impl->hT, impl->cT };
    const char* outputNames[3] { config.outputName, config.stateOutH, config.stateOutC };
    // The state outputs are ours (preallocated); only outputs[0] is ONNX Runtime's to free.
    OrtValue* outputs[3] { nullptr, impl->hnT, impl->cnT };
    const size_t n = config.hasLstmState ? 3 : 1;

    OrtStatus* st = api->Run (impl->session, nullptr, inputNames, inputs, n, outputNames, n, outputs);
    if (st != nullptr)
    {
        api->ReleaseStatus (st);
        // A failed Run may still have filled the logits slot.
        if (outputs[0] != nullptr)
            api->ReleaseValue (outputs[0]);
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
            api->ReleaseValue (outputs[0]);
            return false;
        }
        std::memcpy (logits, outData + (elem - want), want * sizeof (float));
    }

    if (config.hasLstmState)
    {
        std::memcpy (impl->h.data(), impl->hn.data(), impl->h.size() * sizeof (float));
        std::memcpy (impl->c.data(), impl->cn.data(), impl->c.size() * sizeof (float));
    }

    api->ReleaseValue (outputs[0]);
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
