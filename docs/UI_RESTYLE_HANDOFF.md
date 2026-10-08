# UI restyle — handoff

Branch `ui-restyle`, commits `19e1719` → `f912611` (all `ui:`), on top of `125fde9`.
Brief: `docs/DESIGN_BRIEF_MAIN_SCREEN.md` (untracked). Mockups: `~/Desktop/virtualperc_mockup*.html`.

**Nothing on this branch has been built or run.** Every unit passes `g++ -std=c++20 -fsyntax-only`
against JUCE (Linux VM, no Xcode), and a symbol check confirmed the split links. First job: build
for iPad + iPhone, dark + light, and walk the checklist at the bottom.

## Where things are (after the split, `c8cdd83`)

| file | contents |
|---|---|
| `Source/UI/MainComponentShared.h` | tokens (`bg/panel/ink/border/text/mute/fuchsia`, `stateLocked/Searching/Lost`, `stateColour`, `stateIsHot`), fonts (`fontUi` = condensed face with fallback chain, `fontDisplay` = Futura Bold for the BPM), `paintRadial`, `paintTempoOrb`, `paintVoiceFader`, button-style helpers, settings notes, compact-layout constants (`kCompact*`) |
| `MainComponent.cpp` | constructor, audio, prefs, actions (`startPressed/stopPressed/tapPressed`), `StopHold`, `timerCallback` (15 Hz: bloom/colour easing), `updateBeatDots` (VBlank: beat, START pulse, layout-fade repaint) |
| `MainComponentLookAndFeel.cpp` | `AppLookAndFeel` (`drawFlatButton` styles, transport icons, MIC bar), `FaderZoom` |
| `MainComponentLayout.cpp` | `compactGeom`, `compactTempoRows`, `stageRows`, `layoutMisure`, `layoutVoicesRow`, `resized` (+ layout-mode transition), `layoutSettings` |
| `MainComponentPaint.cpp` | `paint`, `paintStage` (hero card, BPM, orb, beats), `paintCards`, `paintSettings` |
| `MainComponentWidgets.cpp` | `TrackWaveform`, `StyleSelect`, `StyleMenuOverlay` (bottom sheet), `SoundMenuOverlay` |

The helpers in `MainComponentShared.h` sit in an anonymous namespace (one copy per unit, unused
warnings silenced). The **one** mutable global, the theme, is `vpui::gDarkMode` (inline,
shared) — don't add per-unit mutable state there.

## Design rules adopted

- Fuchsia = brand and "the one" only (lit beat, tap flash, armed STOP, brand mark). Everything
  else neutral (`text`/`ink`/`border`).
- State colour families: green `following/followingListen/tapAlign`, amber
  `listening/calibrating/waitBeat/waitStart`, red `weakFollow/recalin`, neutral `ready/paused`.
- Hero card (BPM + orb lane above it + beats) blooms in the state colour; strength scales with
  `snap.confidence` when locked (0.40 + conf, clamped) — **calibrate on device**: if
  `confidence` never gets near 0.6 the green always looks weak. Colour and strength ease (no
  snaps); light mode uses ~55–60 % of the dark alphas.
- Button looks via property `btnStyle`: 1 segment track, 2 pill with dot, 3 transport.
- START: black (dark) / ink (light) pill with play triangle, fires on touch-down. STOP: fuchsia
  pill with stop square, also fires on touch-down (the 0.5 s hold was removed 2026-10-08, user's
  call), pulses on the beat while armed.
- Voices and effects are vertical faders (`VoiceKnob`, tap = mute/fire/EDIT, drag = level,
  drag sensitivity 380 px); dragging shows `FaderZoom` in the middle of the screen.
- Haptics: `vp::haptic()` in `Platform/IosMicPermission.{h,mm,cpp}` (no-op off iOS).
- Layout-mode change (phone/wide × portrait/landscape): controls glide 280 ms, new ones fade in,
  painted cards fade under a page-colour veil; any resize cancels the glide.

## Checklist on device

- [ ] Fonts resolve (Futura Condensed Medium on iOS; BPM in Futura Bold, faux-bold offsets ok)
- [ ] Phone width: hero card, orb above the number, 1-2-3-4 in the lit beat, ÷2/TAP/×2 squares
- [ ] Style rows 40 pt, pencil (36 pt) after SWING, segments/pills readable
- [ ] START/STOP colours both themes; beat pulse not distracting
- [ ] Bloom: fades not snaps; tap-on-the-one fuchsia flash; light mode not a stain
- [ ] Fader zoom: grows/shrinks smoothly, value tracks finger, tap still mutes/fires
- [ ] Settings page and sound modal in both themes
- [ ] Rotation / Split View: glide looks right, no stuck alpha, no flicker on drag
- [ ] Haptics feel right (not too many)
- [ ] Stage dim (8 s idle while playing) still works with the transitions

## Open (not done)

- SEGUI/FISSO reachable on the phone stage
- Double-tap fader → 100 %, 100 % tick in the zoom
- VoiceOver labels; contrast of thin captions in light mode
- Layout/state regression tests (VPTests does not build MainComponent today)
- Pre-existing warning: `Displays::Display::userArea` deprecated

## Gotchas

- Source files mix CRLF/LF; edit byte-preserving (don't let a tool normalise the whole file).
- `third_party/JUCE` shows as modified (submodule) — not part of this work.
