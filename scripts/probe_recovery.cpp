// Standalone, no network: c++ -std=c++17 -O2 -ISource
// scripts/probe_recovery.cpp Source/Tracking/TempoFollower.cpp -o /tmp/vp-recovery
#include "Tracking/TempoFollower.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

int main()
{
    int failures = 0;
    for (float bpm : {52.0f, 100.0f, 168.0f})
    for (float sign : {-1.0f, 1.0f})
    {
        vp::TempoFollower c;
        c.prepare (48000); c.forceTempo (bpm); c.setLocked (true);
        c.setFollowStrength (vp::FollowStrength::high);
        c.snapPhase (vp::wrap01 (sign * 0.075f));
        double song = 0, time = 0, confirmed = -1, recovered = -1;
        double nextBeat = 0.4, lastPulse = -1, minGap = 10, maxGap = 0;
        float worstAfter = 0;
        unsigned serial = 0;
        const double period = 60.0 / bpm, dt = 256.0 / 48000;
        bool repeated = true, canceled = false;
        for (int block = 0; time < 0.4 + period * 5; ++block)
        {
            c.setTempoTrust (time < 0.4 ? 0.3f : 1.0f);
            const float err = vp::wrapCentered (c.beatPhase() - vp::wrap01 (song));
            if (time >= nextBeat)
            {
                c.observeRecoveryBeat (err, ++serial);
                nextBeat += period;
                if (c.phaseRecoveryActive() && confirmed < 0) confirmed = time;
            }
            const bool before = c.phaseRecoveryActive();
            for (int n = 0; n < 4; ++n) c.observeRecoveryBeat (err, serial);
            repeated &= before == c.phaseRecoveryActive();
            if (c.phaseRecoveryActive() && ! canceled)
            {
                auto bad = c; bad.setTempoTrust (0.3f);
                auto snap = c; snap.snapPhase (0);
                auto reset = c; reset.reset();
                auto force = c; force.forceTempo (bpm);
                auto transition = c; transition.beginTempoTransition (bpm + 5);
                canceled = !bad.phaseRecoveryActive() && !snap.phaseRecoveryActive()
                    && !reset.phaseRecoveryActive() && !force.phaseRecoveryActive()
                    && !transition.phaseRecoveryActive();
            }
            c.setGridPhase (vp::wrap01 (song), 0.90f);
            auto tick = c.advance (256);
            for (int p = 0; p < tick.pulsesFired; ++p)
            {
                const double at = time + tick.pulseOffset[p] / 48000.0;
                if (lastPulse >= 0 && confirmed >= 0)
                {
                    minGap = std::min (minGap, (at-lastPulse)/(period/4));
                    maxGap = std::max (maxGap, (at-lastPulse)/(period/4));
                }
                lastPulse = at;
            }
            song += dt / period; time += dt;
            const float ms = std::fabs (vp::wrapCentered (c.beatPhase()-vp::wrap01(song))) * period * 1000;
            if (confirmed >= 0 && recovered < 0 && ms < 8) recovered = time;
            if (recovered >= 0) worstAfter = std::max (worstAfter, ms);
        }
        const bool ok = repeated && canceled && recovered >= confirmed && confirmed >= 0
            && recovered-confirmed <= period * 0.5 + dt*2 && worstAfter < 15
            && minGap > 0.7 && maxGap < 1.3;
        std::printf ("%.0f sign=%+.0f confirm=%.3f recover=%.3f after=%.2fms gaps=%.2f..%.2f %s\n",
            bpm,sign,confirmed,recovered-confirmed,worstAfter,minGap,maxGap,ok?"PASS":"FAIL");
        failures += !ok;
    }
    return failures ? 1 : 0;
}
