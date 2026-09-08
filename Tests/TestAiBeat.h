#pragma once

void vpRunAiBeatTests (int& passed, int& failed);

/** Two-quarter cut / seek re-entry for the bar (docs/TODO.md item 2).
    Part of `vpRunAiBeatTests`; also `VPTests --bar`. */
void vpRunBarReentryTests (int& passed, int& failed);

/** The octave-level sweep on its own: the broad synthetic boundary scan plus
    focused 50/100 BPM kit runs through the mixer and internal-file paths.
    Part of `vpRunAiBeatTests`; also runnable alone with
    `VPTests --octave` for iterating on anything that touches the analysis
    level or the octave/level state space without paying for the rest of the
    suite. */
void vpRunOctaveSweepTest (int& passed, int& failed, const char* only = nullptr);

/** What the input level does to the tempo (docs/TODO.md item 24): a sweep of
    0 / -6 / -12 / -18 dB and a clipped case over 52 / 91 / 168 BPM, reporting
    time to the correct octave, percussion entry, `FISSO`, drift and recovery
    after a gap as five separate numbers. Also `VPTests --level`; `only` narrows
    it to one tempo ("52", "91", "168") because each run drives the real neural
    worker over thirty-eight seconds of audio.

    Deliberately NOT part of `vpRunAiBeatTests`: fifteen such runs are about
    five minutes, and the full suite is long enough. Run it by hand after
    anything that touches the analysis level, the frontend or the octave
    logic. It ships with three known failures - see docs/TODO.md item 24.

    `VP_LEVEL_TRACE=1` in the environment prints the tempo, the network, the
    fold, the residual and the regime twice a second for every run. A column
    that says "17.71 s to the right level" does not say *what it was doing
    instead*, and answering that from the summary alone costs an afternoon. */
void vpRunLevelSweepTest (int& passed, int& failed, const char* only = nullptr);

/** Fast decoder-only regressions for slow acquisition, octave-anchor feedback
    and re-entry after a musical gap. Also `VPTests --tempo-slow`. */
void vpRunSlowTempoRegressionTest (int& passed, int& failed);
void vpRunStateTimingTest (int& passed, int& failed);
void vpRunHarmonicEntryTest (int& passed, int& failed);
void vpRunHarmonicAudioTest (int& passed, int& failed);

/** The own-output / analysis-epoch benches. Part of the full suite, and
    runnable on their own with `VPTests --makeup`: they drive the neural worker
    in real time over about twenty runs, which is minutes, and iterating on them
    behind the rest of the suite is not practical.

    `only` narrows that further to one bench - "a" to "e" - for the same reason.
    The full suite passes nullptr and runs all of them. */
void vpRunMakeupTests (int& passed, int& failed, const char* only = nullptr);
