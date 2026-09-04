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

/** The own-output / analysis-epoch benches. Part of the full suite, and
    runnable on their own with `VPTests --makeup`: they drive the neural worker
    in real time over about twenty runs, which is minutes, and iterating on them
    behind the rest of the suite is not practical.

    `only` narrows that further to one bench - "a" to "e" - for the same reason.
    The full suite passes nullptr and runs all of them. */
void vpRunMakeupTests (int& passed, int& failed, const char* only = nullptr);
