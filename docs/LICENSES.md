# Licenses

This project is intended to become a commercial live app. Do not add dependencies without updating this file.

| Component | Version | License | Notes |
|---|---|---|---|
| JUCE | 8.0.15 | [AGPLv3 **or** JUCE commercial](https://juce.com/legal/juce-8-licence/) | Required. App Store / closed source needs a **JUCE licence** (Starter free up to $20k revenue, Indie $800 perpetual up to $300k, Pro above; a JUCE 9 licence covers JUCE 8). Publisher confirms adequate rights (2026-10-26); supporting documents not yet supplied: see `docs/store/juce-licence.md`. Splash is off because it is unusable live. |
| Virtual Percussionist source | — | Proprietary (project owner) | Original code in `Source/`, `Tests/` |
| Percussion samples | VCSL / VSCO 2 CE (clap, cembalo only) | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) | **Conga and shaker are no longer VCSL** (since 2026-09-28): cut from SampleFocus loops, *Standard Licensing - Royalty Free*, not CC0 - see `Assets/Percussion/ATTRIBUTION.md` and `docs/APPSTORE.md` before any release. `Assets/Hits/*.wav` sources are recorded in `Assets/Hits/SOURCES.md`. Publisher confirms redistribution rights (2026-10-26); supporting documents are pending. |
| ONNX Runtime | 1.20.1 | MIT | Host dylib (GitHub release) + iOS xcframework from NuGet `Microsoft.ML.OnnxRuntime` (`./scripts/fetch_onnxruntime.sh`). |
| BeatNet BDA weights | model 1 (GTZAN), fine-tuned | [CC BY 4.0](https://github.com/mjhydri/BeatNet/blob/main/LICENSE) | Heydari, Cwitkowitz, Duan, ISMIR 2021. Since 2026-10-02 `Assets/Models/beatnet.onnx` is these weights fine-tuned (`scripts/train_beatnet_finetune.py`) on the user's own mixer-send recordings, labelled offline by Beat This! (Foscarin, Schlüter, Widmer, ISMIR 2024, MIT; used only to label, not shipped). docs/TODO.md item 87. |
| Signalsmith Stretch | 1.3.2 | MIT | Loop time-stretch, header-only, vendored at `third_party/signalsmith-stretch`. `VP_USE_SIGNALSMITH` is AUTO / ON / OFF; OFF falls back to the built-in WSOLA. |
| Signalsmith Linear | — | MIT | FFT/STFT under Signalsmith Stretch, header-only, vendored at `third_party/signalsmith-linear`. |
| Dance recorded loop library | CC0 1.0 | nicpressley, Sycopation, shpira / Freesound | Embedded from `Assets/Loops/dance/`. Exact sound pages and take mapping are recorded in `Assets/Loops/dance/SOURCES.md`. |

No SoundTouch (LGPL), Rubber Band (GPL), Essentia, or FFmpeg.

Before shipping:

1. Obtain a JUCE commercial license
2. Confirm Apple microphone privacy strings
3. Do not add copyleft libraries that conflict with App Store distribution


## Publisher confirmation — 2026-10-26

The publisher explicitly confirms rights to distribute the JUCE-based app and
the included SampleFocus conga, shaker and effect sounds. Supporting documents
will be identified by the publisher; they have not yet been examined here.
This records that confirmation, not a new interpretation of the standard terms.
