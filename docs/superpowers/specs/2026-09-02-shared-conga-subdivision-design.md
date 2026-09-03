# Shared shaker and conga subdivision

## Goal

The existing AUTO / 1/4 / 1/8 / 1/16 control must govern the density of both
the shaker and synthesized congas. At 1/8, congas must not continue playing the
intermediate sixteenths that make a double-speed pattern sound too busy.

## Musical behavior

The setting thins events; it never adds or moves them.

- 1/4 keeps steps 0, 4, 8 and 12.
- 1/8 keeps every even step: quarters and offbeat `&` eighths.
- 1/16 keeps all sixteen steps.
- AUTO continues resolving to 1/8.

The same rule applies to written conga patterns, fill bars and probabilistic
conga ghost notes. Since ghosts are generated only on odd sixteenth steps, they
are available at 1/16 and suppressed at 1/8 and 1/4. Fill bars receive no
exception.

The rule applies only to synthesized events from `GrooveEngine`. Recorded WAV
loop stems are unchanged and remain outside this work.

## Architecture

`GrooveEngine::eventsAt` is the single filtering boundary. A bounded
`stepAllowedForSubdivision` predicate replaces the shaker-only condition and
is reused before emitting:

1. shaker events;
2. written conga events, including the selected fill table;
3. conga ghosts.

The internal field and setters become subdivision-oriented rather than
shaker-oriented. The existing setting flow remains:

`MainComponent` → `EngineSettings` → `BeatTracker::effectiveSubdivision` →
`VirtualPercussionEngine` → `PercussionEngine` → `GrooveEngine`.

The clock remains at four pulses per beat. No timing, tempo-following, swing,
dynamics, style-selection or phrase-counter behavior changes.

## Compatibility

- Existing sixteenth patterns retain every current event.
- Eighth patterns retain musically important `&` answers while removing only
  `e`/`a` sixteenth chatter.
- Quarter patterns intentionally become sparse and keep only quarter-grid
  congas.
- Dynamics thinning remains an independent later gate.
- CASSA routing and recorded-loop playback are unaffected.

## Verification

Deterministic tests must prove:

- shaker thinning still follows 1/4, 1/8 and 1/16;
- ordinary conga table hits retain even eighth steps and remove odd sixteenths;
- fill congas use the same rule;
- ghost congas occur only at 1/16;
- AUTO matches 1/8;
- quarter mode emits congas only on quarter steps;
- sixteenth mode preserves existing style and fill figures;
- existing density, phrase alignment, clock-grid, buffer-size and loop tests do
  not regress.

The implementation must remain allocation-free, lock-free and bounded on the
audio path.
