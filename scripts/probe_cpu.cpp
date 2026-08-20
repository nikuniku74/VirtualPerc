// How much of the audio callback's budget the engine actually uses, and how
#include <ctime>
// much of a core the analysis worker costs, measured rather than assumed.
#include "Audio/VirtualPercussionEngine.h"
#include "probe_song_render.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

int main (int argc, char** argv)
{
    const double sr = 48000.0;
    const int block = argc > 1 ? std::atoi (argv[1]) : 256;
    const double seconds = 30.0;
    const int n = static_cast<int> (sr * seconds);

    vp::probe::SongOptions o;
    o.bpm = 118.0f;
    std::vector<float> song (static_cast<size_t> (n), 0.0f);
    vp::probe::renderSong (song, o, sr, 991u);
    vp::probe::speakerRoomMic (song, sr, 991u, 0.55f);

    vp::VirtualPercussionEngine eng;
    eng.prepare (sr, block, 1);
    eng.settings().followSource.store (static_cast<int> (vp::FollowSource::speaker));
    eng.start();

    std::vector<float> oL (static_cast<size_t> (block), 0.0f), oR (static_cast<size_t> (block), 0.0f);
    float* outs[2] = { oL.data(), oR.data() };

    std::vector<double> us;
    us.reserve (static_cast<size_t> (n / block));
    const auto wall0 = std::chrono::steady_clock::now();
    const auto cpu0 = std::clock();
    int pos = 0, blocks = 0;
    while (pos + block <= n)
    {
        const float* ins[1] = { song.data() + pos };
        const auto a = std::chrono::steady_clock::now();
        eng.process (ins, 1, outs, 2, block);
        const auto b = std::chrono::steady_clock::now();
        us.push_back (std::chrono::duration<double, std::micro> (b - a).count());
        pos += block;
        // Let the worker run the way it would on a device: the callback returns
        // and the thread sleeps until the next buffer is due.
        if ((++blocks % 8) == 0)
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - wall0).count();
    const double cpu = static_cast<double> (std::clock() - cpu0) / CLOCKS_PER_SEC;

    std::sort (us.begin(), us.end());
    auto pct = [&us] (double p) { return us[static_cast<size_t> (p * (us.size() - 1))]; };
    const double budget = static_cast<double> (block) / sr * 1.0e6;
    double sum = 0.0;
    for (double v : us) sum += v;

    std::printf ("block %d  budget %.0f us\n", block, budget);
    std::printf ("callback  mean %6.1f us (%.2f%%)  p50 %6.1f  p95 %6.1f  p99 %6.1f  max %7.1f (%.1f%%)\n",
                 sum / us.size(), sum / us.size() / budget * 100.0,
                 pct (0.50), pct (0.95), pct (0.99), us.back(), us.back() / budget * 100.0);
    // Wall time here is not real time - the probe feeds audio far faster than a
    // device would - so the honest figure is CPU seconds per second of audio,
    // which is what a core costs on the device. Callback and worker together.
    std::printf ("whole app  %.2f s CPU for %.0f s of audio = %.1f%% of a core"
                 "  (wall %.2f s, i.e. %.0fx real time)\n",
                 cpu, seconds, cpu / seconds * 100.0, wall, seconds / wall);
    return 0;
}
