# AGENTS.md

This repository is a C++ / JUCE iPadOS audio app, not an npm monorepo.

- Package manager: none (CMake + JUCE 8.0.15 in `third_party/JUCE`)
- Product target: iPadOS first, iPhone and Mac (Designed for iPad) from the same
  build, Android later. Apple Vision is deliberately off - see `docs/PLATFORM.md`
- Docs: `docs/`
- Core DSP: `Source/`
- Tests: `Tests/` + `VPTests` console target
