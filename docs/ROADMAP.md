# Roadmap

## MVP 1 — NOW (this tree)

Acoustic kit, one analysis bus, neural beat/tempo/phase tracking, AUTO lock, adaptive shaker, iPadOS app.

Success: iPad Air M1 + USB interface + mic → START → play → shaker locks and follows accelerando/rallentando without loop restart. Requires a bundled `.onnx` for musical lock; TAP works without it.

## MVP 2

Per-source kick / snare / hi-hat / cymbal analysis, confidence mix, better subdivision, channel assignment UI.

## MVP 3

More instruments (tambourine, conga, bongo, clap, cowbell), kits, humanization depth, groove/microtiming.

## MVP 4

Roland TD-20 MIDI input, configurable map, MIDI/HYBRID modes. Same BeatTracker.

## MVP 5

Diagnostic recording, groove engine active, pattern variation.

## MVP 6

Android + Oboe around `vp_core`.

## Neural tracking + loop TSM

ONNX BeatNet-class tracker is the analysis path. See `docs/AI_BEAT_TRACKING.md`.

Loop stretch is no longer a placeholder: Signalsmith Stretch is vendored and the
recorded-loop percussionist is implemented and tested behind
`VP_ENABLE_RECORDED_LOOPS` (off). What is missing is the library of recordings -
see `docs/RECORDED_LOOPS.md` for the list. First delivery is DANCE only, two
stems, twelve files, for a listening comparison; the other styles follow only if
it convinces.

## Explicitly later

MIDI clock out, Ableton Link, song sections, pattern editor, user sample import, multiple players.

## Exit criteria for leaving MVP 1

- Automated clock / TAP / STOP tests green
- AI unit tests green (decoder, stretch, FIFO, kit lock with BeatNet `beatnet.onnx`)
- iOS project configures and links ONNX Runtime (NuGet xcframework + CoreML) with bundled BeatNet (see `docs/STATUS.md`)
- Device A/B on iPad Air M1 still manual (CLICK TEST first)
- No crash on route change / interruption (basic handlers in place)
