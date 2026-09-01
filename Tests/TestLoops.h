#pragma once

/** Recorded-loop percussionist: manifest, bank, player, hybrid renderer.
    See docs/RECORDED_LOOPS.md for what each of these is protecting. */
void vpRunLoopTests (int& passed, int& failed);

/** Counts every heap allocation made while it is armed. Defined in
    TestLoops.cpp, which replaces the global operators to do it. The audio
    callback is not allowed to make any. */
void vpBeginAllocationWatch() noexcept;
long long vpEndAllocationWatch() noexcept;
