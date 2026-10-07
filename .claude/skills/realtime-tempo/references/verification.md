# realtime-tempo reference — Every probe/test target and the full measurement log of kept/rejected changes

Moved verbatim from SKILL.md section 8 on 2026-10-07. Chronological log: newest entries are often at the top of a section, older ones further down; grep for the symbol you are touching. When you change a decision, update the entry that justified it.

## 8. How to verify a change - do not skip this

```bash
./scripts/run-tests.sh                       # TAP suite, works without AI assets
cmake -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --target <target>
```

| target | source | question it answers |
|---|---|---|
| `probe_matrix` | `scripts/probe_matrix.cpp` | twelve kinds of material x five tempos: time to lock, and time spent off the tempo. The bank to A/B a decoder change against - **not** one song. No CMake target; build line in its header. `--ratio` prints reported/true, so an octave error is told apart from a wobble |
| `VPTests` | `Tests/` | the TAP suite; `StubBeatModel` when no ONNX assets |
| `VPTests --bar` | `Tests/TestAiBeat.cpp` | two-quarter cut / seek re-entry of the one (item 2) |
| `VPTests --phase-lock` | `Tests/TestAiBeat.cpp` | click-track heard phase at 78/100/120/138/156 BPM vs 8 ms after subtracting `attackLeadMs` |
| `VPTests --state-timing` | `Tests/TestAiBeat.cpp` | armed live input exposes an already-valid grid immediately and can enter by the first 4/4 boundary; background listening retains LOCKING; state holds are invariant across buffer sizes |
| `VPTests --tempo-step` | `Tests/TestAiBeat.cpp` | wide non-octave line steps confirm once in three intervals and hold against the stale fold |
| `VPTests --swing` | `Tests/TestMain.cpp` | the swing warp's geometry alone: straight where written, swung on 0/⅓/⅔/⅚, never early (item 7) |
| `VPTests --leak` | `Tests/TestMain.cpp` | the canceller alone in twenty seconds: 54 style x subdivision x path rows, the no-leak feed at three buffer sizes, the output A/B, the restart, three rooms |
| `VPTests --makeup` | `Tests/TestAiBeat.cpp` | the other half of the same subject: does our own output move our own analysis. Six benches - `a` phase and the analysis chain with the fader up against down at five tempos, `b` the chain block for block, `c` a real band start with the part playing, `d` our own return not being called one plus the eighteen-row veto margin, `e`/`f` what `prepare()` clears, inside and outside. Name one to run one; naming something that is not a bench fails non-zero rather than passing nothing. `dist`, `sweep` and `epoch` are probes, assert nothing and run only when named |
| `VPProbe` | `probe_song.cpp` | end-to-end on a full arrangement through a speaker into a room: lock time, drift, phase |
| `VPAlign` | `probe_align.cpp` | how long the clock takes to get onto the song, and how good the phase it is aiming at is |
| `VPBar` | `probe_bar.cpp` | does it know which quarter is the one |
| `VPTiming` | `probe_timing.cpp` | where the stroke actually **lands** in the rendered audio, not where the clock says |
| `VPRoom` | `probe_room.cpp` | empty room vs band |
| `VPActivations` | `probe_activations.cpp` | dumps the raw BeatNet curve; design against the real signal |
| `VPReplay` | `probe_replay.cpp` | replays a dumped activation file through `BeatDecoder` alone - a second per sweep instead of two minutes |
| `VPLive` | `probe_live.cpp` | the app against real band recordings with a real answer |
| `truth.py` | `scripts/analysis/truth.py` | beat truth on real recordings from an offline network (Beat This!, venv `~/.venvs/vp-teacher`), and `score TAG` of a `bench_fast.py` run against it: octave, calibration, dispersion of the clock **and** of the decoder's grid, the one. Measured 2026-10-02 (docs/TODO.md item 87): BeatNet peaks 7 ms, a plain causal line over the last 8 peaks 10 ms, decoder grid 12 ms, clock 17 ms (32% of beats > 25 ms). The network is not the ceiling for phase. Following faster in the clock changed nothing (21.6 -> 21.2 ms); the grid lags a band that leans. Kept: published phase from the line through the newest 6 accepted beats (`kPhaseLineBeats`, sounding direct feed, FISSO/VIVO), clock 21.6 -> 17.9 ms, surges 1.18 -> 0.78 /min; `--phase-lock` 156 held at ÷2 reads -9.1 ms (was -7.3). The one: BeatNet's downbeat folded on the true quarters names the right one in 29/31 files, but per bar it caps near 70-75%; the network alone may now move a trusted one by a quarter at a 0.30 share margin (`kBarNetAloneMargin`), 50.6 -> 57.5% of true ones counted as one. Since 2026-10-02 the bundled `beatnet.onnx` is BeatNet fine-tuned on the teacher's labels over the user's mixer-send gigs (`scripts/train_beatnet_finetune.py`, labels shifted -46.5 ms to BeatNet's frame clock, KD 3 to its original outputs so the decoder's tuning still holds): model o1k3s1 (4 h of the user's gigs, 1000 steps, KD 3, seed 1), 48 bench files without NONSOULFUNKY: clock 20.4 -> 15.2 ms, right level 68.6 -> 80.2%, one 69.0 -> 76.4%. The octave swings with the training seed (62-80%) and the same song at 44.1 vs 48 kHz can land on opposite octaves: the acquisition choice is knife-edge. Any decoder threshold measured before that date was measured on the old network's activations |
| `VPCpu`, `VPOps`, `VPSing`, `VPDecoderProbe` | | load, ops, sung input, decoder unit diagnostics |

Tuning loop for decoder work: dump once with `VPActivations`, then iterate with
`VPReplay`. The activations do not change when the decoder does.

For a file that starts after silence, the epoch kind is part of the input.
`VPLive` uses `setInputEpoch(1)` with `preserveComb=false`: the worker resets
BeatNet's recurrent state and the decoder's comb evidence, while retaining
the resampler and feature history. `VPActivations --silence-prelude` now emits
that cold restart by default, and `VPReplay` reads the restart marker.
`--preserve-model-on-restart` selects an arrangement entrance instead. A dump
made with the opposite epoch kind can give a different metrical level despite
the same WAV; compare the `ACT` frames from `VPLive --trace-activations` before
attributing that difference to decoder or clock code.
The tracker must retain its AUTO octave on a preserved arrangement epoch too:
resetting that shift while preserving the model and comb changes the sounding
part's pulse density without a new musical source. A cold source epoch still
clears the shift.

For a loaded recording, use `VPTrack --player`, not only `VPLive`: the latter
feeds the tracker directly and omits the engine's make-up gain and input
epochs. On a 189 s real recording the full-engine probe had one analysis epoch
at 8.7 s, reached 123 BPM after about 10 s, and `barTrusted` was false for 72
of 179 one-second samples after 10 s despite continuous percussion playback. The
clap reads that flag and went silent mid-song and near the end. Bar trust is
now latched after a reliable one is established, while a new input epoch,
STOP, or explicit bar re-entry clears it; weak downbeat votes alone do not
revoke an otherwise unchanged count. With the latch, all 154/154 one-second
samples after the first trusted bar stayed trusted, versus 107/154 before;
the initial 25 samples still wait for reliable bar evidence. The BPM trace
was byte-for-byte unchanged. The `--bar`
cut/seek/lock smoke gate still passes.
`VPTests --octave focused` currently reports 4 pass / 6 fail on the slow-kit
audio phase checks. The exact same 4/6 result and phase numbers reproduce on
an isolated `HEAD` build without these changes, so this is a pre-existing
phase limitation, not a bar-trust regression; do not treat it as a green gate.

**Bridge/fill diagnosis (2026-09-23; no engine change retained).** A listener
reports a false rise from about 123 to 127 BPM on a late drum figure and a
slow return. The full loaded-file path (`VPTrack --player`, original recording
converted to WAV) reproduces it at approximately t=141–160 s, without a new
analysis epoch. Activation replay isolates the first FISSO→VIVO release at
t=142.72 s (2 s prelude in that dump): `curveOnLatticeRelease=1`, two fast
votes, 8-beat residual 0.037, 24-beat residual 0.048, long-fit trend 0.007
against spread 0.007. `moving=0`; the comb still names 123. If the curve door
is withheld, a third vote plus weak `windowAgrees` releases one beat later.
During the figure `EvidenceTrust` is 0.30 but product direct-live mode gives
the phase servo full trust anyway; the clock briefly runs faster than the
published BPM (about 137 vs 127 in the full trace). On return, trim reaches
about −1.3 BPM, phase error reverses sign, and it takes several seconds for
both to settle. The BPM display alone misses the audible excursion.

Do not repair this with a threshold from that song. Requiring an 8-beat residual
under 0.030 on the curve door delayed the false release by one beat, then the
three-vote path still released; the current known-grid quick bank and
`VPAlign --ramps` were unchanged, so there was no reason to retain it.
Also gating the three-vote and long-window paths on that residual held the
example but worsened the known-grid quick bank: fisso
22.256/76.932→22.830/77.455 ms, continuo 36.551/91.156→36.926/92.104,
gradino 33.962/162.108→34.866/173.629;
`VPAlign --ramps` MIXER failed 4 rows. A 0.55 comb-salience alternative
passed `VPAlign --ramps` but still worsened continuo to 36.704/91.519 and
gradino to 34.490/165.654. Removing the product direct-live phase-trust
override made the optional product-direct quick bank worse on continuo
34.211/89.944→36.349/96.762 and gradino 33.502/160.108→33.924/163.707.
All three candidates were reverted. Requiring `moving` on every dirty
three-vote release and on the dirty curve door, with no lattice test, was
also reverted: offset-0 fisso 22.256/76.932→22.733/77.239, continuo
35.482/87.221→36.722/91.718, gradino 33.962/162.108→34.888/167.619.
The delayed leaves already had the 4-beat off the 8-beat (218386, 265900,
289657, 329252, continuo 129495). Restricting the hold to the fill shape
(4-beat still within 1.5% of the 8-beat, 4-beat residual under 0.045,
8-beat residual at least 0.030, `moving` false) changed only fisso seed
88118, a flat 165. That leave at t=18.18 ran 164.7→168.6 while the comb
stayed at 165.7; holding it raised that seed's mean phase by 2.4 ms and
the later leave published 170.6. Continuo and gradino traces were
identical, and the candidate was reverted. The fill's geometry is not
unique: the same 4-on-8 dirty unmoved lattice is a flat-track correction
the phase score wants to take. Aiming the live target at the long fit
whenever `moving` is false and the 8-beat residual is at least 0.030
raised continuo to 40.86/106.09 and gradino to 39.29/179.74. Restricting
that to a clean long line (`lastFitResidual` under 0.015) left fisso
identical but continuo 36.551/91.156→36.643/90.773 and gradino
33.962/162.108→33.995/162.797. Both reverted. On the current loaded-file
path the same figure is already in VIVO (half-tempo lattice about 61):
published tempo 61.4→64.4 and the clock to 67.5 while the long fit stays
near 61.6. A per-beat log of that climb shows `moving` already true and
the IOI-indexed 4-beat on the short fit, not on the long one
(t=148.8: short 62.9, long 61.6, i4 63.0, r4 0.026, moving 1;
t=151.5: short 63.4, i4 63.8, moving 1). The two beats where `moving`
falls false (t=152.5–153.5) still have the 4-beat on the fast side
(i4 64.7/64.3). A hold that waits for `moving` false, or for the 4-beat
to stay on the long fit, does not see this climb. It is the same
geometry as a ramp the control bank wants followed. Aiming the live
commit at a long fit that has stayed inside 0.8% for 4 s while the
short fit has left it by 1.5–8% was counted on
`/tmp/motion_curve_live4.csv` and not shipped: with the short residual
at least 0.030, the long fit is closer to the truth on 22 frames and
the short fit on 64. Continuo is 1 against 55 (the short fit is the
ramp). An 8 s window is 12 against 31, and continuo is still 1 against
23. The same count with the gap tightened to 2–6% makes continuo 0
against 36. A fill whose long line sits still is not separable from
the start of a ramp the bank has to follow. Aiming at a high-salience
comb that has stayed inside 1.2% for 3 s while the short fit is more
than 3% away is the same trap from the other side: on
`/tmp/motion_curve_live4.csv` it would help fisso and continuo, and
it would pull finished steps back (257981 t=43–45, short 116 and
truth 117, comb still 131). The activation dump of the same recording
(`act_everytime_full.txt`) has low-band energy at beat peaks of about
0.81 through 139–154 s, the same as 60–80 s and 120–135 s, and no
peak in that window is under `kLowBandMute` (0.05). The body-hold
never sees this climb: the kick is still in the beats. Requiring the
fast-drift vote's interval to sit within 2% of a 4-beat whose residual
is under 0.03 held the flat 165 glitch (88118 t=83.20, interval +17%,
8-beat only +1.5%, 4-beat residual 0.068) and was reverted: the 16-case
bank moved fisso 22.256/76.932→22.583/76.248, continuo
36.551/91.156→36.713/91.724, gradino 33.537/157.311→34.226/163.254.
A real exit such as 218386 already has that clean 4-beat, but 313414
releases on three dirty 4-beats and the family follows them. The safe next step
is an independent
rhythmic cue/beat-grid and a diverse fill/bridge control bank, not weaker
release or slower global phase following. `VPTrack --trace` now exposes clock,
target, trim, phase error, trust and recovery count; the known-grid matrix has
an optional `--product-direct` A/B lane. Its default hashes remain the control.

**EVERYTIME early abrupt jump (2026-09-24).** The listener marked the playhead
around 42% of the 189.2 s waveform, before the pause. The old 44.1 kHz replay
was steady near 123 BPM there, but resampling the same WAV to 48 kHz reproduced
the iPad-like event: at t=71.52 s the direct-feed interval detector confirmed
123.02 -> 130.26 BPM in one publication, and the heard clock reached 136.72
BPM at 71.9 s. The activation comb stayed at 123.20 with salience 1.00.
The three candidate strengths were 0.827/0.801/0.498 against a recent median
of 0.948; the third peak was a weak fill/tom-like onset. `lineFeed` exempted
this path from the existing 0.70 x median beat-strength gate, despite the
same rule already protecting room input. Applying that gate to both paths
eliminates the false transition: on the 48 kHz full-engine replay, t=70–90 s
published BPM spans 123.01–123.25 and the clock peaks at 123.66, versus
123.12–131.62 and 136.72 before. No song BPM or timestamp enters production.
The 16-case quick known-grid matrix is byte-identical with and without the
change in both the default and `--product-direct` lanes; `VPAlign --steps` and
`--ramps` pass, and `VPTests --tempo-step` is 14/0. The TAP suite also exercises
a quiet pair on direct feed as well as room. A partial full TAP run reached 13
failures whose assertion texts all occur in the earlier
`/tmp/vp-everytime/vptests.log`; it was stopped during the long ONNX phase-lock
section after the focused gates passed, so it is not a full-suite pass. This
guards an abrupt weak-peak
confirmation; the later EVERYTIME live-fit/phase excursion at ~141–160 s is a
separate unresolved failure described above.

**EVERYTIME loaded-file intro, later listener report (2026-09-24; read-only
comparison).** A screenshot from the listener's updated iPad build, using
CARICA/PLAY, shows 165.4 BPM near the start of the guitar and hi-hat intro,
with `CERCO` and `livello provvisorio`. In this pre-lock state the main BPM
comes directly from the decoder's neural hypothesis, not the follower glide.
The listener says the estimate recovers slowly after the kit enters. The
existing full-engine desktop replays of the same MP3 converted
to WAV do **not** reproduce that onset: both 44.1 and 48 kHz publish about 119–120
BPM at 1.6 s. The 48 kHz replay then drifts to about 114–117 while the comb
already reads about 123–124 at 5–8 s; a rhythm entrance resets analysis near
8.9 s and the published BPM reaches about 122.3 by 10.7 s. Thus the trace
supports slow recovery of a provisional intro estimate, but does not reproduce
the device's 165.4-BPM onset. Do not tune a 165 threshold against this replay.
CARICA/PLAY's message-thread load had started transport before queuing the
new-input epoch; the order is now reversed so playback cannot start before
the restart request. Also, `buildTrackWaveform` used to read the same
`AudioFormatReader` as `AudioTransportSource` *after* `setSource` had started
JUCE's read-ahead thread; building the waveform before `setSource` now keeps
those reads sequential. The transport's own `prepareToPlay` starts the
buffering client during `setSource`, so transport not yet playing did not make
the old order safe. These ordering fixes are not a measured solution to the
device-only 165.4 reading. No probe or test was run in this turn at the
listener's request. A future device trace needs source, gain, octave mode,
neural/comb BPM and analysis epoch from the first seconds.

**Late fill and bar-count guard (2026-09-24; implementation only, not
verified at the listener's request).** The existing full-engine 44.1 kHz
trace at t=141–153 s shows the long fit initially near 123 while the live
target rises through 125–130, fit trust is frequently 0.30, and the
clock-minus-analysis phase error reaches -0.201 beat. The clock reaches
about 130 BPM at t=152.4 while the published number is 128.08. This is a
phase-loop contribution on top of a false-looking live fit, not proof that
the recording changed tempo. `TempoFollower` now withholds the direct-live
phase-trust override only when the raw displacement exceeds 0.06 beat,
fit trust is below 0.50, and neither proved motion nor a confirmed
transition/recovery owns the correction. The existing poor-evidence lean
limit of 0.020 beat then applies even when the raw displacement exceeds
`kLeanIsElsewhere`; small direct-feed corrections keep their former path.
This is narrower than the previously rejected blanket removal of direct-live
phase trust. It prevents weak fill evidence from spending a large phase debt
as audible acceleration; it does not certify the decoder's BPM estimate, so
the late false live-fit climb may still require independent rhythmic evidence.
Do not claim a measured improvement until a replay is permitted.

A second route to an audible 1→2 slip existed independently of BPM: after a
trusted bar survived a pause, `notifyBarReentry` cleared the trust latch,
allowing a quarter rotation after its short half-bar-only window expired.
While sounding, `tryAlignFrom` also exempted harmony from the trusted-bar
quarter guard. Preserve the latch across a pause (clear it on seek), and
apply the quarter guard to both automatic sources. A successful automatic
placement now establishes the count even when harmony was the source; otherwise
the next chord change could move its newly placed one again. An explicit bar-button
press can still place any quarter; a supported half-bar correction remains
automatic. These edits have not been tested or listened to in this turn.

`scripts/probe_tempo.cpp` has no CMake target of its own; build any probe source
ad hoc with `VP_STYLE_SRC=scripts/probe_tempo.cpp VP_PROBE_DIR=scripts` and the
`VPStyle` target (`CMakeLists.txt:690`).

**Independent band-cue check (2026-09-23; no engine change).** The file-feed
activation dump already carries low-/high-band magnitudes at 50 fps. A causal
6 s positive-flux Fourier check over 112–140 BPM does not give an independent,
unambiguous pulse through the bridge: the low band names 122.5 BPM at 140 s,
129 at 144 s, 126 at 148 s, and 134.5 at 152 s; its normalized peak support
is only 0.07–0.09 through 140–148 s. The high band names 117, 129.5, and
117 BPM at 140, 144, and 148 s. These bands can explain *loss of evidence* but
cannot safely supply an alternate beat grid or veto a real ramp. A smooth
direct-live phase-steer rail scaled by trust (0.65 at the trust floor, 1.0 at
full trust) was also rejected: the product-direct quick known-phase bank moved
continuo 34.211/89.944 → 35.004/93.229 ms and gradino
33.502/160.108 → 33.933/164.659. The engine was restored. Do not use this
recording to set a spectral-support threshold; obtain diverse fixed/moving
audio with externally known grids before admitting a new band-cue authority.

**Three-file direct-feed control (2026-09-23; observation only).** `VPTrack
--player --trace --step 1` on the complete BLUE SKY, FEEL and SPLENDIDA
GIORNATA files supplied by the listener was deterministic on a repeat of
FEEL's first 60 s. BLUE SKY's ambiguous intro published about 52–63 BPM until
the arrangement epoch at ~40 s, then about 87 BPM over the main body (the
documented approximate tempo). FEEL published 130–134 BPM from ~9–40 s and
did not reach the documented ~104 BPM until ~46–48 s; throughout 14–40 s the
activation comb was already about 102–103 BPM. A causal 6 s Fourier check of
the raw BeatNet beat activation and high-band positive flux supports ~103–104
BPM at 20–40 s (e.g. at 20 s: normalized 104-BPM support 0.56 and 0.35,
respectively, versus high-band support 0.02 at 130). Thus the initial wrong
non-octave accepted lattice is distinct from EVERYTIME's late fill, where the
band cues become ambiguous. SPLENDIDA stays near 108 BPM across the file;
previous human taps named ~107.8 BPM over 10.8–75.4 s, but the tap timestamps
are not presently available for a fresh phase score. The high-band cue is not
globally authoritative by itself: at BLUE SKY 20 s its best six-second rate
is ~128 BPM, and at SPLENDIDA 30 s it is ~86 BPM, despite the respective
~87/~108 music tempi. Do not turn this into a song-name or band threshold.
If testing a new initial-lattice correction, require independent agreement,
check the other files and a true tempo-change grid, and keep the engine
unchanged until the global gates pass.

`VPReplay --anchor --line --sound-at 400 --trace` on FEEL's activation dump is
diagnostic only, not the product path: it initially fits ~150–154 BPM rather
than the full engine's ~130–134, while the comb reads ~102–103 with salience
often 1.00. Its non-octave mismatch counter rises to 5 and repeatedly returns
to 0 before a snap, with a first correction at ~47 s. The sounding post-hole
guard at `BeatDecoder.cpp` near `refusePostHoleComb` is a plausible cause of
that repeated refusal; the trace does not expose its stamp, so do not call it
proved for the product. Removing or merely time-limiting the guard is not a
safe fix: the documented 100→150 leftover-lattice fixture requires a persistent
veto. A candidate must distinguish a false initial lattice from a genuine
post-hole leftover and pass both fixtures before changing production.

**FEEL listener report after the three-file probe (2026-09-23).** The app showed
~50 BPM and the listener confirmed that the `÷2` button was lit. That is a
*manual octave* carried in preferences (`tempoOctave=-1`,
`tempoOctaveAuto=false`), not evidence that the direct-feed decoder independently
chose 50: its probe had reached ~103–104. `MainComponent::loadInternalTrack`
now returns to AUTO when a *different* file is chosen, preserving manual choice
when the same file is reloaded. The UI previously appended `(auto)` even to a
manual octave; that label now follows the actual flag. Do not "fix" the decoder
to force this file upward on account of the listener's 50-BPM reading. The
separate early wrong-grid (~130–134) issue remains open. Tapping an active
`÷2` during playback turns AUTO on but retains that level until safe to change
(the tracker avoids a mid-part octave snap); `×2` once requests the next level.

Probes wait on `BeatTracker::analysisBacklog()` to make a run repeatable -
otherwise the host scheduler decides how far behind the worker is and the same
build measures differently run to run.

**Kept (2026-09-24), faster rientro of a light direct-live offset.** A stable direct feed at high follow closed a constant 20 ms offset at 120 BPM in 0.70 s, and the first tenth of a second of that was still under 0.3 BPM of lean. The 0.30 s phase average and the 2%/beat slew toward the 7.5% rail were both applied to a command that only wants about 4%. While the raw error is inside 0.05 beat and the part is not in a rest, the average is now 0.15 s. The slew inside ±4% may arrive in half a beat; past that edge the old 2%/beat slope toward the rail is unchanged, and a rest still zeroes the lean in the same buffer. Standalone clock, high follow, 256 samples: 20 ms at 120 is inside 8 ms in 0.44 s with a 2.60 BPM peak lean (was 0.70 s / 2.28 BPM); 30 ms is 0.76 s / 3.83 BPM (was 1.01 s / 3.64); a 50 ms debt, which starts outside the light band, is 1.02 s / 6.24 BPM (was 1.42 s / 5.56). The same 20 ms on the hold path stays at 1.59 s. A ±0.015-beat 6 Hz wobble stays inside the phase floor (peak lean 0). `probe_recovery` is 0 failures and now gates the 20 ms direct case at ≤ 0.55 s and ≤ 3.5 BPM, the hold case at ≥ 1.2 s, and the 50 ms lean at ≤ 8 BPM. The known-phase matrix does not set this follow.

**Kept (2026-09-24), a second loaded file must not wait for STOP.** Loading another track while START stays on already queues a fresh epoch and drops the previous file's audio. The new file's own onset then publishes a later epoch before the worker reads the first, and that later epoch is a rhythm entrance: it keeps the previous comb. The drop bit now sticks, and preserve is forced off, until the worker consumes it. The same restart clears `sounding` before the decoder rebuilds, and the part may re-enter only on a grid serial the restart has not already published. STOP used to be what released the on-grid keep; the file change does that itself. The clock is not restarted for a tempo change inside one song. A new file is the other case: the part is already silent, and `follower.reset()` drops the previous tempo, phase and trim so the next song starts as cold as a fresh launch.
The remaining multi-second wait on a second file was the waveform scan: it read the whole asset on the message thread before `setSource`, so neither playback nor the restart began until that read finished. Playback and `notifyInputRestart` now run first. The scan uses a second stream, because the transport reader is already in its read-ahead thread and an MP3 reader has one shared position. The earlier "build the waveform before `setSource`" note solved that shared-reader race by blocking playback; this keeps the race closed without holding the new song.

**Kept (2026-09-25), a new file must earn its own acquisition state.** The source-drop path reset `follower` and `evidence` but retained `BeatTracker`'s beat count, listening duration and smoothed confidence. In a focused regression, a second file could pass the `listening -> following` gate after only its first provisional beat, while a cold first file had to provide two. It also retained `sounding` and a downbeat hold that could delay silent phase placement. The source-drop path now clears those acquisition and bar-hold fields; a preserved arrangement epoch and an in-song tempo change keep their continuity. `VPTests --state-timing`: new-file case FAIL before, PASS after, including immediate FOLLOWING after two fresh beats and 0.70 s of fresh listening; `--new-input`: 14/0 across 60 -> 120 -> 90 -> 150 -> 60 BPM.

**Kept (2026-09-25), a failed inference must still advance decoder time.** `NeuralBeatTracker` consumed a 20 ms feature frame when `IBeatModel::infer` returned false but skipped `BeatDecoder::observe`. Every later hypothesis then carried an analysis timestamp one hop behind the audio; each further failed call added another hop. A missing activation is now sent to the decoder as silence, which advances its frame clock without claiming a beat. The deterministic worker regression in `VPTests --new-input` failed before and passes after one injected model failure; all other calls retain the same activations. This protects rare ONNX/CoreML inference failures, not ordinary acquisition speed.

**Kept (2026-09-25), bounded work per spectral band.** `LogSpectFeatures` summed all 1023 FFT bins for each of 136 log bands every 20 ms, although each triangular filter touches only a narrow span. `buildFilterbank` now records each band's first and last nonzero bin; `processHop` keeps the same coefficients and bin order inside those bounds. On a deterministic 1,390-frame host bench, feature extraction took 153.74 -> 27.03 ms; an in-loop comparison against the original full sum found a maximum pre-log band difference of 3.81e-6 from floating-point evaluation order. `VPTests --new-input` and `--phase-lock` both pass 15/0 after the changes; the iPadOS Debug target compiles without code signing. No new allocations or waits are added to the worker. This reduces analysis CPU/headroom pressure; it does not claim a faster lock when the worker is already caught up.

**Kept (2026-09-24), a hat does not place the quarter after a hole while the part is sounding.** On a direct feed, once the gap is at least `kGridStaleBeats` (2.5 quarters), a crest with no kick body is refused and its time is kept in `refusedHatAfterHoleSec`. The anchor moves onto the following kick only when that hat was refused and the kick arrives within one period, half a beat off the old origin. A kick after an ordinary hole does not move the anchor and is not force-accepted: that shift is a phase jump, and the clock was closing it by accelerating on passages that were already stable. The click bank never sets `sounding`, so this does not run there.

**Direct-file audit (2026-09-24, no engine change).** On the current tree,
`VPTests --tempo-step` is 14/0, `--tempo-slow` 10/0, `--phase-lock` 15/0,
`--bar` 10/0, and `VPAlign --steps` and `--ramps` pass. The rebuilt
`probe_motion_matrix --quick` is red: fixed 23.0/78.1 ms mean/p95,
continuous 38.8/95.0 ms, step 30.3/125.1 ms, with 0 curvature proofs in
both fixed and continuous material (`selettore curvatura: FAIL`). The
`--product-direct` lane is also red at 22.4/72.1, 40.2/115.0 and
34.9/145.3 ms respectively. This is a baseline failure, not evidence for
loosening the curve gate.

The full-engine `VPTrack --player` replay of the listener's FEEL file at
44.1 kHz still publishes 132.19 BPM at 20 s and 133.22 at 30 s while the
activation comb reads 102.74 and 102.92; it reaches the 104-BPM reference
only around 46 s. The same path on EVERYTIME is near 123 BPM through 140 s,
then publishes 127.72 at 152 s while the sounding clock is 129.83 and the
fit trust is 0.46; the comb is on a competing 156-BPM level there. These are
observations without an annotated beat grid, so neither excerpt licenses a
new tempo or phase authority. `VPTests --ai`, accidentally removed by the
latest hat-placement commit, was restored as a test-only selector. The
restored route reached 11 failures in its first 426 output lines before its
long end-to-end portion was stopped; this is a partial run, not a suite pass.

**Broader loaded-file audit (2026-09-24, no engine change).** Four independent
16-case known-grid offsets (0/16/32/48) give continuous-motion mean/p95 phase
38.8/95.0, 46.9/101.1, 35.8/83.7 and 55.2/127.3 ms. The motion bridge has
0 authority frames in the first three offsets and 183 in the fourth; none
has authority on flat or step cases. The offset-0 selector failure is therefore
part of a sparse-authority problem, not a single song threshold. On a fresh
complete `VPTrack --player` replay of SPLENDIDA GIORNATA (44.1 kHz), the
published tempo starts near 109, wanders around 107–110 and ends at 111.5.
An independent 12 s tempogram with usable clarity reads about 106.9–109.7
over the earlier windows. `prec.py` reports structure 3.23 and worst
10 s peak movement 254.5 ms, but `hist.py` shows the peak repeatedly
alternating between phase 0 and 0.5 while the clock tempo is continuous:
this may be a kick/snare emphasis change and is not an annotated phase error.

**Rejected: prevent the long-fit phase period from re-arming on the same
frame as a hat/body hold.** `longFitPeriodHeld` is cleared for
`kitBodyHolding || hatGridHolding`, then the existing `ioiClockLead` gate can
set it again. A scratch known-grid bank with a sounding part and a hats-only
section at 52–60 s reproduced 535 ioi-lead hat frames. Adding the hold to
the re-arm gate left the normal offset-0 matrix byte-identical, but worsened
the hats bank's continuous-motion mean/p95 **74.82/214.63 -> 75.35/217.76
ms** and changed its fixed trace too. The one-line candidate was reverted.
Do not call this an isolated logic fix without a stronger phase reference and
a bank where the hats enter after varied real motion.

**Dead cadence experiment removed (2026-09-24).** The direct-feed
`observeDownbeatCadence` and `observeMetricalCadence` paths were behind the
literal `kCadenceCorrectionEnabled = false`; the latter still updated an
eight-bin histogram on every accepted beat, but neither could publish a
metrical hint. The failed 50-versus-100/half-time experiment and its fixture
remain described in `docs/HANDOFF_OCTAVE_50BPM.md` and `docs/TODO.md` item 1.
The decoder's active kick/hat low- and high-band logic is unchanged. For the
cleanup, `VPTrack --player --trace --until 65` on the loaded FEEL WAV was
byte-identical through its 64 s sample rows; the 16-case-per-family offset-0
known-grid motion CSV was also byte-identical (fixed 23.03/78.12,
continuous 38.79/95.03, step 30.28/125.05 ms mean/p95). `VPTests`
`--tempo-slow` 10/0, `--tempo-step` 14/0, `--phase-lock` 15/0,
`--bar` 10/0 and `--new-input` 3/0 passed; `VPAlign --steps` and
`--ramps` passed their gates. `VPTests --octave focused` is still red at
4/6, including file and mixer 50/100 BPM audible-alignment cases. The
motion selector still fails its curvature-proof gate at baseline. The
musician also reports persistent levare on some songs. That may be a
half-beat phase error while BPM is correct; the existing `offbeat-lock` test
and hat-to-kick half-steal path already address a narrower case. Without a
song-annotated quarter grid, an automatic half-beat phase flip on a hat-only
passage is ambiguous and has not passed a global A/B gate.

**Immediate loaded-file lock audit (2026-09-24, no engine change).**
`probe_matrix --quick` on the direct-feed decoder (12 rhythmic styles,
52/120/168 BPM, two seeds each) measures a 7.15 s mean stable acquisition,
19/72 later excursions and six never-acquired half-time runs. Lowering only
`kFastAcquireMarginLine` from 0.55 to 0.30 in a temporary build makes mean
acquisition 7.16 s, leaves excursions/never-acquired unchanged, and worsens
the swallowed-mix and gap styles by 0.09/0.08 s. Lowering only
`kAnchorAcquireMarginLine` from 2.5 to 1.5 is byte-identical on the same bank.
Neither margin is the remaining bottleneck; do not land either shortcut.
`NeuralBeatTracker` already publishes every available 20 ms model frame, and
the armed direct-feed state exposes an established grid without an extra
160 ms hold. `MainComponent::loadInternalTrack` starts playback after the
decoder restart; it does no tempo analysis before PLAY. Its synchronous
whole-file waveform scan then blocks the message thread, so the BPM display
can appear late even while the audio worker is running. The user
requires analysis to remain live, so pre-reading a loaded file is excluded.
On fresh `VPTrack --player --step 0.2` traces, SPLENDIDA publishes ~110.6 BPM
at 0.8 s and enters FOLLOWING by 1.0 s; its part waits until 8.5 s because
the low-band rhythm share does not first cross 0.30 until 6.7 s. EVERYTIME
publishes ~119.2 BPM at 1.6 s and enters FOLLOWING by 1.8 s; its music onset
creates a fresh analysis epoch at 8.9 s and the part joins the next quarter
at 9.3 s. Those waits are rhythm-entrance decisions, not a decoder or
publication queue. Removing the `rhythmSeen` guard would repeat the measured
false entrance on a non-rhythmic intro (item 29). A correct quarter/levare
choice from the live beginning still needs causal rhythmic evidence.

**Loaded-file UI assertion (2026-09-25).** The empty-waveform paint path passed
`"Onda in caricamento\u2026"` directly to JUCE's ASCII `String (const char*)`
conversion. The C++ Unicode escape makes UTF-8 bytes above 127, which that
debug constructor asserts on. This is a concrete candidate for the reported
iPad `juce_String.cpp` SIGTRAP just after switching to BRANO, when the second
waveform reader cannot supply peaks. The placeholder now uses ASCII dots.
Mac Release and Debug compiled before the later queue/waveform edits; the iPad
crash is not yet device-verified. The synchronous scan was subsequently changed
as described below and also awaits verification.

**Repeated source-change check (2026-09-25, test and trace only).** The same
engine, audio device and worker were kept running while `--new-input` changed
the synthetic grid 60→120→90→150→60 BPM without STOP. Each change advanced
the analysis epoch exactly once, reset the model's recurrent state, and
reacquired within 0.1 BPM by the end of its 5–6 s observation (`13/0`
assertions). This rules out a simple cumulative
tempo/comb state leak in that fixture; it does not model CoreML overload,
remote file reads, or a real band. The debug trace now includes FIFO queue
milliseconds, gap count and restart count so an iPad capture can distinguish
worker starvation from a wrong but timely musical hypothesis.

**Pending verification (2026-09-25): one-shot epochs and exact queue cut.**
`BeatTracker::setInputEpoch` used to re-publish the same epoch on every audio
block. The worker clears the sticky queue-drop bit with a compare/exchange;
an audio-thread load of the old word followed by a store could restore that
bit after the worker cleared it, causing another cold restart for one file.
The tracker now publishes only on a changed epoch or an explicit drop, and the
producer composes that word with a compare/exchange so a concurrent worker
clear cannot be overwritten from a stale copy.
`NeuralBeatTracker` captures the FIFO write cursor before the new file's first
block is fed and the worker discards only samples before that cursor. Its old
`discardPending()` discarded the new file's queued beginning whenever the
worker woke after playback had started. The event word release/acquire orders
the cursor, and the worker skips a superseded event before reading more audio.
These changes touch restart delivery and queue accounting, not BPM thresholds,
phase selection or the live band's continuous clock. Per the listener's
request, builds, probes, tests and listening are deferred until explicit
confirmation; no performance or regression claim is established yet.
The source button also used to switch between player, mixer and iPad without
notifying the engine of a new input; the preceding source's grid could survive
until an unrelated level epoch. It now requests a fresh input epoch on an
actual source change. File loading suppresses that one notification and sends
its existing restart after the reader has been installed, so it remains one
restart per loaded file.
The separate waveform reader now scans at most 32768 audio frames per UI tick
after PLAY rather than all samples in the load callback. The track's duration
comes from the transport reader immediately, so seeking is available before
the picture finishes. This is visual read-ahead only; BeatNet still receives
the audio being played live. The iPad file-provider latency of an individual
read and the final waveform timing remain to be verified.

**PLAY versus STOP lock investigation (2026-09-25, pending verification).**
The direct-file analysis bus does not include the generated percussion and
skips acoustic leak subtraction. STOP nevertheless changes `sounding` in the
tracker/decoder and lets the tracker snap its phase exactly while silent.
During PLAY it deliberately steers phase by rate to avoid skipped or doubled
strokes; the existing serial-change path can rejoin a displaced grid. A second,
independent STOP-only gate was found in `BeatDecoder::updateTempo`: when the
initial interval grid was provisional, an eight-beat short fit and the comb
could agree within 2.5% at a non-octave rate, but their fast correction was
forbidden as soon as the part started. That gate has been removed only for
this narrowly corroborated pre-lock case. This does not establish the cause
of every mid-song STOP improvement: a wrong phase or octave after a settled
lock takes different guarded paths. The trace now reports `level` alongside
`nn`, `clock`, `phaseErr`, `queueMs` and `audible` to distinguish them. No test
or device measurement had been run on this change at that point, per the
listener's instruction to reserve tests for the end and obtain confirmation.

Follow-up (2026-09-25): `VPTests --state-timing` passed its new-file and
bar-hold checks, `--new-input` passed 15/0, and the real ONNX `--phase-lock`
passed 15/0 at 78/100/120/138/156 BPM. These do not isolate the provisional
short-fit/comb correction *while sounding* or establish why a particular
mid-song STOP/START improves alignment. The full host suite was stopped after
unrelated existing failures (phase-steer noise, slow-level, conga patterns,
attack alignment); do not cite it as a green gate for a new phase policy.

**Transport handoff (2026-09-25).** The UI's START/STOP callbacks previously
called `VirtualPercussionEngine::start/stop` on the message thread while the
audio callback used the same BeatTracker, voices and loop players. These
mutations now cross one atomic command word and execute at the start of the
next audio callback. A STOP and START between callbacks execute in that order;
the fast manual re-entry is retained. `userWantsArmed`, read by the device
prepare callback, is atomic. This removes a data race, but is not evidence
that the race caused a particular wrong-quarter lock. `VPTests --transport`
compared the queued and direct paths sample by sample over START, STOP,
restart, and same-buffer STOP/START: 4/0. `--state-timing` passes its 15
checks; ONNX `--phase-lock` is 15/0 at 78/100/120/138/156 BPM;
`probe_recovery` still passes all 0.075/0.125/0.20-beat shifts and noise
controls. The unsigned iPadOS Debug target builds. The direct live
recording had seven false re-grabs in about 4.5 minutes when serial changes
were treated as grounds to mute. Do not add a fixed half-second mute based
only on phase error; a new automatic interruption needs evidence that
distinguishes a true grid change from a decoder phase nudge or a levare.

**iPad route recovery, 2026-09-28 (device check pending).** The listener reports
that the first drag/resize after each app launch still cracks and can leave
AirPods silent until CLOCK or BUFFER is changed, while the file advances.
The earlier JUCE interruption-end patch did not solve this. In the local JUCE
iOS device, route notifications could reach `restart()` but its live-unit
early return discarded them; `RouteConfigurationChange` did not even request
one. The route path now reconstructs RemoteIO on the message thread, with a
one-second guard for its own route-configuration notification. `close()` stops
the unit before disposal, and start failures make `isPlaying()` false so the
existing app watchdog can replace it. The analysis worker and tempo policy are
untouched. The unsigned iPadOS Debug build compiles; neither a host probe nor
the simulator can prove AirPods output after a real iPad resize. Do not label
the persistent-silence symptom fixed until the first resize after a fresh
launch is heard on the device.
Follow-up the same day (docs/TODO.md item 56): the first open passes rate
0, `chooseBestSampleRate` turns that into the constructed 44100 and the iOS
Pimpl kept it as `targetSampleRate` while the route ran at 48000; every
`restart()` asked the session for 44100 again. `open()` now pins the target
to the rate iOS granted. Device check still pending.

**Kept (2026-09-28): the post-hole refusal defends only a
corroborated lattice.** `postHoleReopenSec` never expires, so one sounding
hole used to arm `refusePostHoleComb` for the rest of the song: every later
non-octave fold correction was refused unless the 4-beat already sat on the
fold. That is the plausible cause of FEEL holding ~132 while the fold read
~103 until ~47 s. `combAgreedBpm` records the committed tempo at which the fold
last named the same pulse (modulo octaves, within `kStaleGridRelease`, at
snap salience); the refusal now applies only while `bpm` is still within
`kStaleGridThreshold` of it. Fixture D's 100 was corroborated before its
pause, so its refusal is unchanged; a lattice the fold never agreed with is
not a leftover to protect. The transition path's `afterHole` stamp is left
untouched. Against a rebuilt HEAD control: `probe_motion_matrix --quick`
(default and `--product-direct`), `probe_tempo_step`, `VPAlign --ramps` and
`--steps` byte-identical (the synthetic banks never set `sounding`);
`VPTests --tempo-step/--tempo-slow/--new-input/--bar` 14/10/15/10 pass on
both. A reconstructed post-pause fixture (100 corroborated, 3/4/6-quarter
hole, then a 150 lattice for 30 s) holds 100.00 on both. FEEL through
`VPTrack --player --bpm 104`: first 3 s lock 46.09 -> 24.88 s, time inside
2% 50.3 -> 58.8%, published 132.7 -> 102.6 at 26 s; after 60 s mean
difference 0.05 BPM. EVERYTIME 44.1k, SPLENDIDA and Sally byte-identical.
Listening still open (`docs/TODO.md` item 48).

**Kept (2026-09-28), a long FISSO is not left on fast votes through a fill.**
EVERYTIME 44.1 kHz: the grid before the bridge (110-136 s, 123.04) and after
it (166-186 s, 122.88) is one line (115 beats, -23 ms), so the song holds its
tempo. The decoder, FISSO for 204 beats, released at 141 s on two fast votes
with both fits 2-3x worse placed than the song's own (placement trust 0.30) and
the fold on 123; it published up to 129 and the clock slipped a whole beat by
156 s. `scripts/analysis/line_check.py` measures the clock against that line
(any steady-tempo recording becomes a known grid this way).

On a direct feed the fixed-regime release is now held when all witnesses say
"same tempo": `EvidenceTrust` (the tracker's class, fed per accepted beat,
restarted per grid serial) below `kPoorPlacementTrust` 0.50, tenure >=
2 * kLongFit, short-fit move below `kGridStepMinimum` (2.5%), long window not
`moving`, and the fold not following the short fit (octave-folded, same
direction, >= 1/4 of the move). Sustained/large evidence (fixedErrorBeats,
fastDriftLargeBeats, 6% anchor error) still releases.

Result: EVERYTIME 44.1k time inside 2% 94.0 -> 98.6%, clock worst in the fill
-54 ms with no slip. Quick bank offset 0 identical (both lanes); offset 32
fisso 20.9/50.5 -> 20.4/47.4; offset 48 gradino 33.7/145.6 -> 33.2/140.3;
offset 16 gradino 33.6/153.9 -> 34.0/154.2 (only broken seed 257997, 21% BPM
error). Full matrix gradino 37.6/150.7 -> 37.6/150.4, continuo unchanged.
`probe_tempo_step`, `VPAlign --ramps/--steps`, focused VPTests, FEEL,
SPLENDIDA, Sally identical.

Not solved: EVERYTIME 48k (same audio) still slips at 142-156 s. At 99.8 s the
per-beat trust flicks to 1.00 (the fill's short fit briefly clean) with a +3.9%
move, so it releases; back in FISSO the tenure restarts (22 beats at 141.7 s).
Trust hysteresis or tempo-based tenure were not tried: they would be thresholds
from one song. Rejected on the way, all against HEAD: trust alone (continuo
38.8/95.0 -> 41.8/111.1, VPAlign ramps FAIL, 140->75 13.6 -> 24 s); fold that
must reach the short fit (12 s ramp FAIL 84.1 -> 107.0); curve exemption g >=
0.50 (the fill has g 0.66 too); repeated 4-beat estimates as step proof (the
fill rushes coherently for three beats); tenure without the 2.5% cap (offset 32
seed 242175 p95 113 -> 190 ms). In-window evidence does not separate a rushing
fill from a sloppy small step; tenure and the fold are what differ.

**Kept (2026-09-28), frames after a restart are dated from the restart
sample.** On a new file (and on a FIFO overrun) the worker resets the resampler
and `LogSpectFeatures`, then compensated with a fixed `frame - hop` refill. That
ignores the partial hop and the resampler's buffered input thrown away by the
reset, so every later hypothesis was dated early by up to ~20 ms, the phase was
projected too far and the part played early - and the error accumulated with
every file loaded. `NeuralBeatTracker` now keeps `segmentInputStart` (input
sample incl. dropped audio where the extractor restarted) and
`segmentFrameBase`; `analysisSampleFor` dates frame f at segmentInputStart +
((f - base - 1) * hop + frame/2) * ratio, identical to the old formula for a
session without restarts. `VPTrack --then`: SPLENDIDA loaded second +16.07 ms
early -> 0.00 ms; a file loaded first byte-identical. New `VPTests --new-input`
assertion fails on the old worker (640 samples) and passes now; `--phase-lock`
15/0 unchanged.

`scripts/analysis/line_scan.py` turns any steady-tempo recording into a known
grid automatically (pairs of steady stretches whose lines agree) and reports
verified-span phase error and whole-beat slips. See docs/TODO.md item 51 for
the first scan of the listener's nine songs at 44.1/48 kHz (item-49 rule
neutral on all; excursions located at ASPETTANDO ~172 s, SPLENDIDA ~105 s,
LET ME LOVE YOU ~74 s, VITA ~90 s; FEEL at 48 kHz ends on the double, 208).

**Rejected (2026-09-28), anti-alias filter before `LinearResampler`.** The
resampler does not low-pass: 44.1 kHz is plain 2:1 decimation, 48 kHz linear
interpolation, so hats/cymbals above 11 kHz fold into the top bands, and fold
differently per device rate. A Kaiser FIR (-61 dB above 11.025 kHz, group delay
subtracted in `analysisSampleFor`) made the real bank worse overall: FEEL 44.1k
106 -> 209, BLUE SKY 48k 85 -> 171, EVERYTIME 44.1k slips again, VITA worst
124 -> 300 ms, `VPTests --phase-lock` reads the 156 BPM click as 78. Better
only on ASPETTANDO (190 -> 51 ms) and LET ME LOVE YOU (151 -> 77 ms). The whole
chain - thresholds, the high-band kick/hat logic, octave arbitration - is
tuned on today's aliased input; do not "fix" the input without retuning and
remeasuring everything. Also rejected the same day: sticky poor trust plus an
8.7% move bound for the item-49 hold (EVERYTIME 48k still slips, 239 -> 242 ms;
gradino offset 32 41.4/168.6 -> 42.1/176.5). docs/TODO.md item 52.

**Kept (2026-09-28), a loaded file's mid-song cold epoch keeps comb and
model.** After a near-silent break, the band's return trips the level-step
epoch in `updateAnalysisEpoch` (`preserveCombOnEpoch = false`), built for
empty room -> band. On a loaded file with the part playing it cleared comb and
model and the fresh acquisition wandered for ~10 s: the listener heard LET ME
LOVE YOU 99 -> 60 and VITA 96 -> 62 on the iPad; desktop 48 kHz reproduces
LET ME LOVE YOU at 112 s (clock to 78) and BLUE SKY at 67 s (clock to 178).
For `FollowSource::internalPlayer`, once the part has played on a settled level
(`playedOnSettledLevel`, latched; the instantaneous `levelSettled` read 0 in the
very block the break ended at 48 kHz), that epoch is sent as an arrangement
entrance. Clock over the 15 s after the restart: LET ME LOVE YOU 48k 78-98 ->
91-100, VITA 44.1k line-check worst 124 -> 72 ms, BLUE SKY 48k 85-178 -> 86-88;
the other 16 bank files unchanged. Mixer input keeps the cold epoch (a quiet gap
then a band can be the next song). A tracker-side hold of the old tempo was
tried first and rejected: it released on the old decoder's stale publication,
then on the first post-restart guess that happened to be near, and made a
111 BPM clock spike on VITA.

**Kept (2026-09-28), a sounding part keeps its octave; an analysis jump is
placed in range (docs/TODO.md item 57).** `BeatTracker::holdSoundingLevel`:
while the part sounds, a hypothesis at an exact double/half (±7%) of the tempo
being played never reaches the clock and the level is shifted back (THE
REASON 85 -> 170 after a rhythm-entrance epoch). If the analysis itself jumps by
more than ~11% (not an octave) while sounding, AUTO places the new reading in
49-168 as before entry (LET ME LOVE YOU 165 -> 190, FEEL 48k 158 -> 205, UNA
CANZONE 114 -> 180 now end on 99/104/86). Song bench: those five runs fixed, the
other 17 identical; `--octave` 2/9 identical to HEAD.

**Kept (2026-09-28), steady-tempo phase lean capped at 3% (item 58).** With
`directLivePhaseFollow` at HIGH the phase servo could bend the rate 7.5% on a
tempo that was not moving: heard as rushing/dragging. Now 3% unless the band is
moving (`tempoMotionHint`, or a locked glide under 1 s: fast/held bend/proved
curve/transition) or the error exceeds `kOpenAbove`. A flat 3% was rejected: it
let real motion lag (known-phase continuous 40.2 -> 46.9 ms). Song bench
(`scripts/analysis/bench_songs.py`, 11 songs x 44.1/48k): jerk 0.97 -> 0.93%;
matrix default lane identical, product-direct lane neutral with p99.5
continuous 280 -> 237 ms; VPAlign byte-identical; probe_recovery 0 FAIL.

**Kept (2026-09-29), octave vote by pulse class and settled-only
corroboration (docs/TODO.md item 59).** When the grid is not on the fold's
pulse by a non-octave ratio, votes for any octave of that pulse count as one
(the raw fold alternated 87/176 against a 132 grid and restarted the vote each
swap). `combAgreedBpm` is set only by a settled fold, so an early unsettled
reading cannot make the post-hole refusal defend a wrong lattice. The
slower-comb veto gains a 16-beat steady-level proof for non-octave ratios.
Synthetic banks, fixture D and VPAlign identical; song bench: ASPETTANDO 44.1k
acquisition 46.4 -> 25.4 s, all else identical. `holdSoundingLevel` acts only
in AUTO: a manual ÷2/×2 is the listener's (it had been undone).

**Fixed (2026-09-29), the 16-beat slower-level proof must not reach a
subharmonic (docs/TODO.md item 60).** Item 59 was committed without
`probe_tempo_step`, and it was red: 120 -> 160 finished at 53.3 BPM. After the
step the stale fold settled on a third of the new grid, steady for sixteen
beats once the transition quarantine had ended, and the new proof lifted the
veto. `combOtherSlower` now excludes a grid/fold ratio within `kSteadyFold` of
a whole number >= 3; 3:2 and 5:3 lattices are unaffected. `probe_tempo_step`
PASS with the pre-59 table; every other gate identical. **Always run
`probe_tempo_step` with the synthetic banks.**

**Kept (2026-09-29), an unconfirmed, starving grid under a sounding part is
still a guess (item 60).** `nonOctaveDisagreement` now also applies when the
fold has never corroborated any grid since the input began
(`combAgreedBpm < kMinBpm`), the accepted beat closed a gap of more than
`kGridStaleBeats` periods, and the part is sounding - so a non-octave fold
votes on refreshes where `levelSettled` flickers off. LET ME LOVE YOU 44.1k:
the grid lost `provisional` to a passing fold (144 on its way to 196), then
played 153 over a 98 song while a sounding part let one beat in four onto it
and every unsettled refresh took a vote back; acquisition 27.9 -> 20.3 s,
wrong-in-first-40 s 19.5 -> 10.4 s, other 21 bench cases identical. Each
condition was measured: "not confirmed at this tempo" instead of "never"
lets the stale fold after a real step vote (step family offset 16
34.05/154.2 -> 34.45/157.5); dropping "starving" snaps a swung 132 onto the
fold's 88 (seed 321349); dropping "sounding" moves the 12 s ramp of
`VPAlign --ramps` (MIXER 35.5 -> 37.2). With all three: `probe_matrix`,
`probe_motion_matrix` (four offsets, both lanes, per seed) and `VPAlign`
byte-identical. Blocking `provisional -> established` on a fold off the
grid's pulse also fixed LET ME LOVE YOU but kept grids provisional for longer
(`observeGridStep`, kit-body and hat holds, confidence) and delayed THE REASON
48k's entry by half a second: rejected. The song bench is not deterministic
under heavy concurrent load (I WANNA DANCE 44k); run it alone.

**Kept (2026-09-29), a manual /2 is published, not decoded (docs/TODO.md
item 63).** `BeatDecoder::setUserOctave (n, manual)`: a listener's slower level
(/2, /4) leaves the decoder at the natural level and divides at publication
(`publishDivided`): `bpm`, `periodSec`, the other BPM fields, `beatPhase =
(m + naturalPhase) / D` and the beat events of one class only
(`publishedBeatSerial`). Decoded at the slow level the grid rejects every other
beat as a subdivision on half the evidence, and the played beats drifted up to
130 ms from the natural-level ones for 10-20 s. The natural count is an unwrapped
phase counter (`naturalBeat`), the class is the class of the beats the network
accepts (`classWeight`), fixed at the press from the last accepted beat and
decided again by the first accepted beat after a rebuilt grid; it changes only
when the other class gathers > 4x + 5. Engaged from the reportable minimum at the
press, from 1.06x otherwise, released under 0.98x: hovering at ~100 BPM must not
flip it (VITA 96 did, 190 ms off). x2 and every level AUTO chooses are decoded
exactly as before: publishing AUTO's /2 too changed UNA CANZONE's path at 10 s
(wrong-tempo seconds 43 -> 69), so the natural/AUTO path is byte-identical to the
version before. Song bench with /2 at 5 s: rate jerk 1.08 -> 0.76 %. Nothing
looks ahead: the class is a count of beats already passed.

**Kept (2026-09-30), the direct-live rail opens only on the decoder's motion
hint (docs/TODO.md item 66).** What a listener reports is not the offset from
the drums but the part's *rate* surging and then taking time to settle.
`scripts/analysis/surge_scan.py` counts stretches where `clockBpm` is more than
3% from its own 8 s median: 91 in 85 minutes of the song bench, 80 in VIVO, the
published BPM still within 1-2%. Scored against each song's kick and snare
onsets (`onset_fit.py`), 47 of 83 had no drum offset behind them (12.7 ms before,
13.9 after): the decoder's grid had stepped and the clock chased it at 7.5%. The
item-58 small lean was gated on `bandMoving`, and in VIVO the target is retouched
every beat, so the glide read as motion almost always. Now `tempoMotionHint`
alone opens the rail, and the 3% lean holds up to `kLeanIsElsewhere`. Surges 91
-> 62, surge seconds 104 -> 67, jerk 0.92 -> 0.85%, drum offset 11.46 -> 11.50 ms
(unchanged), no run with more surges. Default matrix lane identical;
product-direct continuo 45.7/116.0 -> 49.7/124.2 (large ramps pay), fisso and
gradino level. Same cycle, all rejected with the drum offset unmoved at 11.5 ms
mean (its noise floor is ~8 ms, the smoothed decoder grid reads 8.75): full
phase trust for small errors in FISSO/CERCO, the motion trim gain after three
or five same-sign drifts (VPAlign flats FAIL), `kFixedAnchorFloor` 0.10 on a
line feed, and the mix's low band through `KickOnsetDetector`. Do not judge a
follower change on the drum offset alone: it does not move; judge it on surges.

**Kept (2026-09-30), a trim that fights the phase is halved (docs/TODO.md
item 67).** `observeOnsetPhase`, direct feed only (`directTempoDirectionGuard`):
when the clock is more than 0.08 of a beat off and the trim points the same way
as that error, the trim is halved on each observation. One displaced stroke
under a motion hint could put +2.5 BPM into the trim in a single observation
(UNA CANZONE PER TE, 67 s) and an opposite drift then counted for nothing until
three agreed, so the part ran 3% fast for seconds on a decoder that had not
moved. Drum offset on the song bench 11.50/24.13 -> 11.33/23.70 ms, lock times
identical, VPAlign ramps within 0.3 ms. 0.04 and 0.06 fail the 128 -> 120 mean.
Capping what one observation may report to the trim (2-4%) costs the 12 s ramp
and FEEL's lock: rejected. Two signals that do **not** mean "out of sync":
clock-versus-decoder error over 0.15 beat held two beats is confirmed by the
drums 8 times in 66, and on Sally the clock sits within 25 ms of the kit while
the accepted beats read +100 ms from it for twenty seconds. Do not mute or
re-anchor on either. `VPTrack --stop-at T` simulates the listener's STOP/START.

**Kept (2026-09-30), a far phase target in a standing FISSO waits two beats
(docs/TODO.md item 68).** `TempoFollower::setFixedDirectFeed`, set by
`BeatTracker` only on a stable direct feed whose decoder has been `fixed` on one
`gridSerial` for 8 s with the part sounding. There the far-target shortcut (slow
average shortened after 0.25 s) and the steering ceiling that opens with the
error both wait for the error to keep its side for two beats. EVERYTIME 48k at
113.8 s: tempo steady at 123.4, the decoder's grid stepped ~0.2 beat for half a
second, the clock braked to 104 and then ran at 131; now 117-127. Every other
run of the song bench is unchanged. Two narrower-than-obvious conditions, both
measured: without the FISSO restriction FEEL 44.1k locks at 35.8 s instead of
27.3; with the regime alone and no tenure `VPTests --bar` loses "seek re-aligns
the one" two runs in eight, because after a seek the stale hypothesis still says
fixed. The same wait on the direct-live rail past `kLeanIsElsewhere` (VIVO) was
rejected: surges 63 -> 62 and the product-direct lane pays on every family.

**Kept (2026-09-30), a step confirmed at low confidence does not jump the clock
(docs/TODO.md item 69).** `scripts/analysis/surge_sources.py` splits each clock
surge into decoder tempo, trim and phase lean, and flags transitions: the peaks
over 6% on the song bench were almost all confirmed transitions on songs that
had not stepped (INFINITO, steady at 91, handed 84.9; FEEL 100.9; BLUE SKY 84.3).
The damage is `beginTempoTransition` itself - the tempo set at once - not the
phase spend, so scaling the rapid rail by the step changed nothing. The
hypothesis already carries `transitionConfidence` (how well the confirming
intervals agree): 0.42, 0.42 and 0.54 on those three, 0.89-1.00 on VPAlign's 25
true steps. On a direct feed the tracker now calls `beginTempoTransition` only at
0.75 or more; below it the new tempo is an ordinary target. `VPAlign --steps`
identical line for line; rate jerk 0.83 -> 0.79%, INFINITO's surges gone.
Treating every step under 9.5% as ordinary instead fails the five protected
steps (47-87 ms at the third beat). Still open: a false step at confidence 1.00
(SPLENDIDA 0:47), and a grid that jumps during a transition state without a
consumed transition (EVERYTIME 44.1k 2:41).

**Measured, nothing kept (2026-09-30): the live overshoot above the fold, and the
other BeatNet weights (docs/TODO.md item 71).** The listener's own STOP/START
log (five presses on a live take without a click) shows the grid was *not*
displaced - the silent re-placement was 20-34 ms - but the tempo was 1-2% above
the fold, and came back onto it. Reproduced on the same passage: the band rises
121 -> 125 and stays, the short fit reads 129, the committed tempo 127-128.5 and
the clock 130 for about eight seconds, the fold on 125 throughout, and an
onset autocorrelation of the audio agrees with the fold. The excess feeds
itself: with the fold given more weight the short fit itself reads 124-126.
Six variants of "more authority to the fold in VIVO" were rejected: on the led
target it pulls every ramp back (VPAlign 12 s 35.5 -> 41.9 ms); gated on a fold
that stands still it loses the case; gated on short-fit-versus-fold at 60% it
fixes the passage and BLUE SKY/ASPETTANDO but adds surges on I WANNA DANCE,
delays FEEL's lock and still fails two ramps; at 35% or without the lead veto
the passage is worse than before. Do not retry a comb pull on the live target.
BeatNet `model_2` (Ballroom) and `model_3` (Rock Corpus), same network: worse
than `model_1` on the whole bench (drum offset 12.1/11.8 against 11.4 ms, lock
17.3/19.0 against 13.1 s). `VP_BEAT_MODEL` now overrides the bundled model for
such comparisons.

**Kept (2026-09-30), refused crests that form their own lattice are the pulse
(docs/TODO.md item 73).** Every sounding rule in `observe` defends `lastBeat`,
and each refusal leaves it where it is. I WANNA DANCE 44.1k, 179-190 s: VIVO
had walked to 127.8 over a song at 123.7, `lastBeat` sat on a weak crest, and
21 consecutive kick quarters at strength 0.85-1.00 were refused (roll bar at
1.111, then off keep, then off the fold of the wrong tempo); the part ran 3%
fast for ten seconds at confidence 0. `kRefusedRunBeats` 4: four refused
crests in a row with kick body, each one committed period (inside
`kStaleGridThreshold`) after the last, no accepted beat between, and less than
0.32 of a beat off the grid (the half-beat zone stays with hats and the steal)
- the fourth is accepted. Low band 0 is mute. Hole 10.2 -> 1.5 s, wrong-tempo
seconds 35.8 -> 23.4; song bench 21 of 26 runs identical, wrong seconds 464.9
-> 450.5, no slips; I WANNA DANCE 48k jerk 0.73 -> 0.83. `VPAlign --ramps`
and `--steps` pass with the same numbers. Not re-run: `probe_tempo_step`, the
motion matrix, hats/rolls/D fixtures. Still open: the brake to 117 on rejoin,
and the VIVO climb to 128 that starts it (item 71).

**Kept (2026-10-01), the projection has no ceiling short of the FIFO
(docs/TODO.md item 74).** `leadSec` was clamped to 0.60 s since the first
commit. A worker that falls behind passes it and the part plays late by the
excess at full confidence. `VPTrack --lag SEC` holds the worker SEC behind:
at 1 s VITA/INFINITO strokes outside 25 ms 5.5 -> 15.7% / 1.4 -> 27.4%
before, 6.7 / 1.9% with the ceiling at 10 s. On device, DEBUG `lead` growing
during a set means the analysis is not keeping up.

**Rejected (2026-10-01), a tempo hold in the tracker (docs/TODO.md item 75).**
Clamping the clock's target to +/-1-1.5% of a 30 s average, released after
10-16 s on one side, cut excursions over 4% from 167 to 34-50 s on the song
bench but added whole-beat slips (0 -> 1-3), later acquisition and worse
stroke placement (>25 ms 9.9 -> 10.8-12.2%, Sally live 10.2 -> 15.7%): the
clock held its rate while the decoder's grid moved. Holding has to happen in
the decoder, not downstream of it.

**Kept (2026-10-01), the first bar after a FISSO release catches up only if
the fold agrees (docs/TODO.md item 79).** The `far` branch of `live` commits
at `kRateAcquiring` toward a short fit that, right after a release, is often
noise. Under a sounding part on a direct feed it now needs the comb nearer
the target than the held tempo. Flamingo 62-66 min: 3 false jumps -> 1 (the
one the comb backs). Song bench: strokes outside 25 ms 8.96 -> 8.92%, time
over 4% off 146 -> 138 s, no run worse. Also kept (item 77): a sounding clock
that stays 0.06 beat off the published grid for two beats is re-placed by at
most 0.20 beat, as STOP/START does; against the drums the grid beats the
clock 4.2 to 9.9% of strokes outside 25 ms.

**Kept (2026-10-01), a step under a sounding part needs the fold (docs/TODO.md
item 81).** `BeatDecoder::stepLacksComb` gates every step confirmation (interval
detector, `observeGridStep`, the FISSO/VIVO 4-beat doors, the carried vote) on
a direct feed while sounding: the comb, folded onto the new tempo's octave, must
sit nearer the new tempo than the held one. Song bench: strokes outside 25 ms
8.92 -> 8.44%, time over 4% off 138 -> 105 s, no run worse. Rejected the same
day: re-placing after one beat (5-6 runs worse) and spreading the re-placement
over half or one beat (no skipped strokes, but 43% more time off the grid).

**Fast, repeatable song bench (2026-10-02, docs/TODO.md item 84).** The song
bench was not slow because of BeatNet: the analysis worker sleeps up to 8 ms
between passes (right on a device) and `VPTrack` waited for it every hop, so
it could not run faster than ~2x real time, and a frame published mid-hop
landed on different blocks run to run. `VP_OFFLINE_PACING=1` makes the worker
poll at 20 us and `VPTrack` wait after every block: a 4-minute song in ~2 s,
bit-identical across serial and parallel runs. Use
`scripts/analysis/bench_fast.py run TAG [VAR=VALUE...]` and
`bench_fast.py cmp BASE TAG...` (50 runs in ~1 minute). Numbers are not
comparable with the older slow benches; the new baseline tag is `fb0`.
