// Standalone, no network: c++ -std=c++17 -O2 -ISource
// scripts/probe_recovery.cpp Source/Tracking/TempoFollower.cpp -o /tmp/vp-recovery
#include "Tracking/TempoFollower.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

int main()
{
    int failures = 0;
    // Accepted subdivision peaks have distinct serials too. They must neither
    // confirm a return prematurely nor continually replace its first beat.
    for (float bpm : {52.0f, 100.0f, 168.0f})
    for (float sign : {-1.0f, 1.0f})
    for (int buffer : {64, 256, 1024})
    {
        vp::TempoFollower c;
        c.prepare (48000); c.forceTempo (bpm); c.setLocked (true);
        c.setFollowStrength (vp::FollowStrength::high);
        c.snapPhase (vp::wrap01 (sign * 0.075f));
        auto ordinary = c;
        const double period = 60.0 / bpm, dt = buffer / 48000.0;
        double time = 0, song = 0, next = 0, confirmed = -1, recovered = -1;
        double ordinaryRecovered = -1;
        double lastPulse = -1, minGap = 10, maxGap = 0;
        float worstAfter = 0;
        unsigned serial = 0;
        while (time < period * 5)
        {
            const float error = vp::wrapCentered (c.beatPhase() - vp::wrap01 (song));
            if (time >= next)
            {
                c.observeRecoveryBeat (error, ++serial);
                next += period * 0.5;
                if (confirmed < 0 && c.phaseRecoveryActive()) confirmed = time;
            }
            c.setGridPhase (vp::wrap01 (song), 0.9f);
            ordinary.setGridPhase (vp::wrap01 (song), 0.9f);
            ordinary.advance (buffer);
            auto tick = c.advance (buffer);
            for (int p = 0; p < tick.pulsesFired; ++p)
            {
                const double at = time + tick.pulseOffset[p] / 48000.0;
                if (lastPulse >= 0)
                {
                    minGap = std::min (minGap, (at-lastPulse)/(period/4));
                    maxGap = std::max (maxGap, (at-lastPulse)/(period/4));
                }
                lastPulse = at;
            }
            time += dt; song += dt / period;
            const float ms = std::fabs (vp::wrapCentered (c.beatPhase()-vp::wrap01(song))) * period * 1000;
            if (confirmed >= 0 && recovered < 0 && ms < 8) recovered = time;
            if (recovered >= 0) worstAfter = std::max (worstAfter, ms);
            const float ordinaryMs = std::fabs (vp::wrapCentered (ordinary.beatPhase()-vp::wrap01(song))) * period * 1000;
            if (ordinaryRecovered < 0 && ordinaryMs < 8) ordinaryRecovered = time;
        }
        const bool ok = confirmed >= period * 0.9 && confirmed <= period + dt
            && recovered >= confirmed && recovered-confirmed <= period * 0.5 + dt*2
            && worstAfter < 15 && minGap > 0.7 && maxGap < 1.3;
        std::printf ("subdivisions %.0f sign=%+.0f buffer=%d confirm=%.3f recover=%.3f total=%.3f ordinary=%.3f after=%.2fms %s\n",
                     bpm,sign,buffer,confirmed,recovered-confirmed,recovered,ordinaryRecovered,worstAfter,ok?"PASS":"FAIL");
        failures += !ok;
    }
    for (bool poorPassage : {true, false})
    for (float bpm : {52.0f, 100.0f, 168.0f})
    for (float sign : {-1.0f, 1.0f})
    {
        vp::TempoFollower c;
        c.prepare (48000); c.forceTempo (bpm); c.setLocked (true);
        c.setFollowStrength (vp::FollowStrength::high);
        c.snapPhase (vp::wrap01 (sign * 0.075f));
        double song = 0, time = 0, confirmed = -1, recovered = -1;
        double nextBeat = poorPassage ? 0.4 : 0.0, lastPulse = -1, minGap = 10, maxGap = 0;
        float worstAfter = 0;
        unsigned serial = 0;
        const double period = 60.0 / bpm, dt = 256.0 / 48000;
        bool repeated = true, canceled = false;
        for (int block = 0; time < 0.4 + period * 5; ++block)
        {
            c.setTempoTrust (poorPassage && time < 0.4 ? 0.3f : 1.0f);
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
                auto octave = c; octave.setTargetTempo (bpm <= 100 ? bpm*2 : bpm/2,1);
                canceled = !bad.phaseRecoveryActive() && !snap.phaseRecoveryActive()
                    && !reset.phaseRecoveryActive() && !force.phaseRecoveryActive()
                    && !transition.phaseRecoveryActive() && !octave.phaseRecoveryActive();
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
        std::printf ("%s %.0f sign=%+.0f confirm=%.3f recover=%.3f after=%.2fms gaps=%.2f..%.2f %s\n",
            poorPassage ? "return" : "persistent",bpm,sign,confirmed,recovered-confirmed,worstAfter,minGap,maxGap,ok?"PASS":"FAIL");
        failures += !ok;
    }
    for (int scenario : {0, 1, 2, 3, 4})
    {
        vp::TempoFollower c, control;
        c.prepare (48000); c.forceTempo (100); c.setLocked (true);
        c.setFollowStrength (vp::FollowStrength::high); control = c;
        double song = 0, t = 0, next = 0, sq = 0, baseSq = 0;
        unsigned serial = 0, seed = 71;
        float noise = 0;
        bool active = false;
        for (int block = 0; block < 48000 * 12 / 256; ++block)
        {
            const float bpm = scenario == 2 ? 100 + static_cast<float> (t) * 0.2f : 100;
            if (block % 31 == 0)
            {
                seed = seed * 1664525u + 1013904223u;
                noise = (static_cast<float> (seed >> 16) / 65535 - 0.5f) * 0.06f;
            }
            const float seen = vp::wrap01 (song + noise);
            if (t >= next)
            {
                const float error = vp::wrapCentered (c.beatPhase() - seen);
                ++serial;
                // A burst of different serials within half a beat is still
                // only one independent observation. An isolated displaced
                // quarter must also fail confirmation despite intervening hats.
                const bool outlier = (scenario == 1 && serial == 7)
                    || (scenario == 3 && serial >= 7 && serial <= 10)
                    || (scenario == 4 && serial == 7);
                c.observeRecoveryBeat (outlier ? 0.10f : error, serial);
                next += 60.0 / bpm * (scenario == 3 ? 0.125 : scenario == 4 ? 0.5 : 1.0);
            }
            active |= c.phaseRecoveryActive();
            c.setTargetTempo (bpm,1); control.setTargetTempo (bpm,1);
            c.setGridPhase (seen,0.9f); control.setGridPhase (seen,0.9f);
            c.advance (256); control.advance (256);
            song += bpm / 60.0 * 256 / 48000; t += 256.0 / 48000;
            const float e = vp::wrapCentered (c.beatPhase()-vp::wrap01(song));
            const float b = vp::wrapCentered (control.beatPhase()-vp::wrap01(song));
            sq += e*e; baseSq += b*b;
        }
        const bool ok = !active && sq <= baseSq * 1.01 + 1e-9;
        std::printf ("noise/outlier/ramp scenario=%d accelerated=%d error-ratio=%.4f %s\n",
                     scenario,active,sq/std::max(1e-30,baseSq),ok?"PASS":"FAIL");
        failures += !ok;
    }
    return failures ? 1 : 0;
}
