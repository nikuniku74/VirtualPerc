#include "AI/ModelLocator.h"
#include "AI/BeatModelConfig.h"
#include "AI/LogSpectFeatures.h"

#include <cstdlib>
#include <filesystem>

#if defined(VP_HAS_BEAT_MODEL)
#include <VpBeatModelData.h>
#endif

namespace vp
{

OnnxModelConfig defaultBeatOnnxConfig() noexcept
{
    OnnxModelConfig cfg;
    cfg.inputName = "features";
    cfg.outputName = "logits";
    cfg.featureDim = LogSpectFeatures::kDim;
    cfg.timeSteps = 1;
    cfg.numClasses = 3;
    cfg.hasLstmState = true;
    cfg.lstmLayers = 2;
    cfg.lstmHidden = 150;
#if defined(VP_ORT_COREML)
    // VP_NO_COREML=1 (Xcode scheme, Environment Variables) runs the network
    // on the CPU only, to compare the two on the device: DEBUG shows "rete".
    cfg.useCoreMlOnIos = std::getenv ("VP_NO_COREML") == nullptr;
#else
    cfg.useCoreMlOnIos = false;
#endif
    cfg.useNnapiOnAndroid = false;
    return cfg;
}

std::string locateBeatModelFile()
{
    if (const char* env = std::getenv ("VP_BEAT_MODEL"))
    {
        if (env[0] != '\0' && std::filesystem::exists (env))
            return env;
    }

    const char* candidates[] = {
        "Assets/Models/beatnet.onnx",
        "../Assets/Models/beatnet.onnx",
        "../../Assets/Models/beatnet.onnx",
        "beatnet.onnx",
    };
    for (const char* c : candidates)
        if (std::filesystem::exists (c))
            return std::filesystem::absolute (c).string();

    return {};
}

bool loadDefaultBeatModel (OnnxBeatModel& model)
{
    const auto cfg = defaultBeatOnnxConfig();

    // A model named in VP_BEAT_MODEL wins over the bundled one, so a probe can
    // compare weights without rebuilding (docs/TODO.md item 71). Nothing sets
    // it in the app.
    if (const char* env = std::getenv ("VP_BEAT_MODEL"))
        if (env[0] != '\0' && std::filesystem::exists (env)
            && model.loadFile (env, cfg))
            return true;

#if defined(VP_HAS_BEAT_MODEL)
    int sz = 0;
    const char* data = VpBeatModelData::getNamedResource ("beatnet_onnx", sz);
    if (data != nullptr && sz > 0
        && model.loadMemory (data, static_cast<size_t> (sz), cfg))
        return true;
#endif

    const std::string path = locateBeatModelFile();
    if (path.empty())
        return false;
    return model.loadFile (path.c_str(), cfg);
}

} // namespace vp
