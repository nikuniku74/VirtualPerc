# AGENTS.md

This repository is a C++ / JUCE iPadOS audio app, not an npm monorepo.

- Package manager: none (CMake + JUCE 8.0.15 in `third_party/JUCE`)
- Product target: iPadOS first, iPhone and Mac (Designed for iPad) from the same
  build, Android later. Apple Vision is deliberately off - see `docs/PLATFORM.md`
- Docs: `docs/`
- Core DSP: `Source/`
- Tests: `Tests/` + `VPTests` console target

## Skills - read the relevant one before you touch the code

Two shared briefs live in `.claude/skills/`. They are plain Markdown and are the
**single source of truth** for the two hard parts of this engine; Claude Code
loads them as skills, Cursor points at them from `.cursor/rules/`, and Codex
should read them directly. If you learn something new about either subject,
update the skill file - not a copy of it.

| brief | read it before touching | it answers |
|---|---|---|
| [`.claude/skills/realtime-tempo/SKILL.md`](.claude/skills/realtime-tempo/SKILL.md) | `Source/AI/`, `Source/Tracking/`, `Source/Audio/` | how the BPM, the beat phase and the bar are found and followed in real time: the ONNX worker, `BeatDecoder`'s three tempo sources, latency projection, the `TempoFollower` PLL, the octave problem, tap, and which probe measures what |
| [`.claude/skills/percussion-patterns/SKILL.md`](.claude/skills/percussion-patterns/SKILL.md) | `Source/Percussion/`, `Source/Loops/` | what the player actually plays: the 16-step groove tables, the nine articulations, per-style shaker weighting and accents, the 8-bar phrase, swing/humanize/ghosts, band dynamics, voices and attack compensation |

Both end with a "how to verify" section. In this repository a timing change with
no probe number attached, or a musical change nobody has listened to, is not
reviewable.

## Ground rules that hold everywhere

- Audio thread: no allocation, no locks, no I/O, no ONNX.
- UI contains no DSP; DSP touches no UI object. They meet at a lock-free
  snapshot of `std::atomic` fields.
- The percussion clock never restarts because the BPM changed.
- Comments in this tree carry the *reason* and usually the measurement. Keep
  that style: when you change a decision, change the comment that justified it.

## Build and test

```bash
git submodule update --init --filter=blob:none third_party/JUCE
./scripts/setup-ai.sh      # ONNX Runtime + Assets/Models/beatnet.onnx (optional)
./scripts/run-tests.sh     # TAP suite; passes without AI assets via StubBeatModel
```
