# Licenses

This project is intended to become a commercial live app. Do not add dependencies without updating this file.

| Component | Version | License | Notes |
|---|---|---|---|
| JUCE | 8.0.15 | [AGPLv3 **or** JUCE commercial](https://juce.com/legal/juce-8-licence/) | Required. App Store / closed source needs a **commercial JUCE license**. Splash is off because it is unusable live. |
| Virtual Percussionist source | — | Proprietary (project owner) | Original code in `Source/`, `Tests/` |
| Percussion samples | VCSL / VSCO 2 CE | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) | Conga, tumba, quinto, shaker one-shots in `Assets/Percussion/`. See ATTRIBUTION.md. |
| ONNX Runtime | 1.20.1 | MIT | Host dylib (GitHub release) + iOS xcframework from NuGet `Microsoft.ML.OnnxRuntime` (`./scripts/fetch_onnxruntime.sh`). |
| BeatNet BDA weights | model 1 (GTZAN) | [CC BY 4.0](https://github.com/mjhydri/BeatNet/blob/main/LICENSE) | Heydari, Cwitkowitz, Duan, ISMIR 2021. Exported to `Assets/Models/beatnet.onnx`. |
| Signalsmith Stretch | optional | MIT | Loop time-stretch (`-DVP_USE_SIGNALSMITH=ON`). Not vendored in MVP 1. |

No SoundTouch (LGPL), Rubber Band (GPL), Essentia, or FFmpeg.

Before shipping:

1. Obtain a JUCE commercial license
2. Confirm Apple microphone privacy strings
3. Do not add copyleft libraries that conflict with App Store distribution
