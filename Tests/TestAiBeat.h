#pragma once

void vpRunAiBeatTests (int& passed, int& failed);

/** The own-output / analysis-epoch benches. Part of the full suite, and
    runnable on their own with `VPTests --makeup`: they drive the neural worker
    in real time over about twenty runs, which is minutes, and iterating on them
    behind the rest of the suite is not practical.

    `only` narrows that further to one bench - "a" to "e" - for the same reason.
    The full suite passes nullptr and runs all of them. */
void vpRunMakeupTests (int& passed, int& failed, const char* only = nullptr);
