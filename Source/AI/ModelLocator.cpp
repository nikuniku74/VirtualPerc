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
    // CPU only. CoreML takes 15 of the 23 nodes in 6 partitions, so every
    // frame crosses between CoreML and the CPU, and on a network this small the
    // crossing costs more than the maths. iPad Air M3, Release, a song playing
    // for 4-6 min (docs/TODO.md item 108): one call 1.93 ms with CoreML,
    // 0.36 ms CPU only; the analysis worker 11.5% of a core against 3.7%.
    cfg.useCoreMlOnIos = false;
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
