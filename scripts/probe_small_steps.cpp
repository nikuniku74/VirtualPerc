// Focused small-step gate, decoder + clock on synthetic activations (no ONNX).
// c++ -std=c++17 -O2 -ISource scripts/probe_small_steps.cpp
// Source/AI/{BeatDecoder,TempoEstimator,BeatHmm}.cpp
// Source/Tracking/TempoFollower.cpp -o /tmp/vp-small-steps
// Add `--trace FROM TO` to print every accepted beat for one measured step.
#include "AI/BeatDecoder.h"
#include "Tracking/TempoFollower.h"
#include "Tracking/PhaseTrust.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main (int argc, char** argv)
{
    const bool trace = argc == 4 && std::string (argv[1]) == "--trace";
    const float traceFrom = trace ? std::strtof (argv[2], nullptr) : 0.0f;
    const float traceTo = trace ? std::strtof (argv[3], nullptr) : 0.0f;
    if (argc != 1 && ! trace)
        return 2;

    int failures = 0;
    for (float from : {52.f, 120.f, 168.f})
    for (float delta : {-12.f, -2.f, 0.f, 2.f, 12.f})
    {
        if (std::fabs (delta) > 2 && from != 120)
            continue;
        const float to = from + delta;
        if (trace && (from != traceFrom || to != traceTo))
            continue;
        const double at = 48 * 60.0/from, end = at + 18;
        std::vector<double> beats;
        for (int i=0; i<49; ++i) beats.push_back(i*60.0/from);
        while(beats.back()<end+1) beats.push_back(beats.back()+60.0/to);
        vp::BeatDecoder decoder; decoder.prepare(50); decoder.setLineFeed(true);
        decoder.setLevelAnchor(true);
        vp::TempoFollower clock; clock.prepare(48000); clock.forceTempo(from);
        clock.setLocked(true); clock.setFollowStrength(vp::FollowStrength::high);
        clock.setTempoTrimEnabled(true);
        vp::EvidenceTrust evidence;
        uint32_t serial=0;
        double rateAt=-1, stableAt=-1, phaseWorst=0, rateWorst=0;
        double baseline=0; int baseN=0;
        int transitions=0; uint32_t transitionSerial=0;
        uint32_t printedBeatSerial=0;
        int pulseViolations=0;
        size_t index=0;
        for(int frame=0; frame<end*50; ++frame)
        {
            const double now=frame/50.0;
            float activation=0.02f;
            for(double beat:beats)
            {
                const double d=(now-beat)/0.027;
                if(std::fabs(d)<5) activation=std::max(activation,0.94f*float(std::exp(-0.5*d*d)));
            }
            const auto h=decoder.observe(activation,0.02f,1-activation);
            if(h.valid)
            {
                evidence.observe(h.fitResidual,h.fitCoverage,0.02);
                clock.setTempoTrust(evidence.trust());
                if(h.transitionSerial!=transitionSerial && h.transitionState==vp::TempoTransitionState::rapid)
                {
                    clock.beginTempoTransition(h.transitionBpm);
                    transitionSerial=h.transitionSerial; ++transitions;
                }
                // Match BeatTracker's confirmed payload and rapid phase policy.
                const bool payload=clock.tempoTransitionActive() && h.transitionState==vp::TempoTransitionState::rapid;
                clock.setTargetTempo(payload?h.transitionBpm:h.bpm,payload?h.transitionConfidence:h.confidence);
                if(h.beatSerial!=serial && h.confidence>0.4)
                {
                    clock.observeRecoveryBeat(vp::wrapCentered(clock.beatPhase()-h.beatPhase),h.beatSerial);
                    clock.observeOnsetPhase(vp::wrap01(clock.beatPhase()-h.beatPhase),h.confidence,1);
                }
                serial=h.beatSerial;
                clock.setGridPhase(h.beatPhase,clock.tempoTransitionActive()?vp::kGridTauRapid:vp::gridPhaseTau(vp::kGridTauHolding,true,evidence.trust()));
                if (trace && h.beatSerial != printedBeatSerial && now >= at - 1.0)
                {
                    printedBeatSerial = h.beatSerial;
                    std::printf ("trace t=%+.3f beat=%u bpm=%.3f short=%.3f long=%.3f "
                                 "regime=%d state=%d reason=%d ints=%d serial=%u clock=%.3f phase=%.4f\n",
                                 now - at, h.beatSerial, h.bpm, h.shortFitBpm,
                                 h.longFitBpm, static_cast<int> (h.regime),
                                 static_cast<int> (h.transitionState),
                                 static_cast<int> (h.transitionReason),
                                 h.transitionIntervals, h.transitionSerial,
                                 clock.currentTempo(), h.beatPhase);
                }
            }
            const double positionBefore=clock.beatsElapsed()+clock.beatPhase();
            const auto tick=clock.advance(960);
            const double positionAfter=clock.beatsElapsed()+clock.beatPhase();
            if(positionAfter<positionBefore || tick.pulsesFired!=int(std::floor(positionAfter*4)-std::floor(positionBefore*4)))
                ++pulseViolations;
            const double current=now+0.02;
            while(index+1<beats.size() && beats[index+1]<=current) ++index;
            const double phase=(current-beats[index])/(beats[index+1]-beats[index]);
            const double raw=vp::wrapCentered(clock.beatPhase()-float(phase));
            if(now>at-4 && now<at) {baseline+=raw; ++baseN;}
            if(now>=at)
            {
                const double rateError=std::fabs(clock.currentTempo()-to);
                const double phaseMs=std::fabs(vp::wrapCentered(float(raw-baseline/std::max(1,baseN))))*60.0/to*1000;
                phaseWorst=std::max(phaseWorst,phaseMs); rateWorst=std::max(rateWorst,rateError);
                if(rateError>0.5) rateAt=-1; else if(rateAt<0) rateAt=now-at;
                if(rateError>0.5 || phaseMs>25) stableAt=-1; else if(stableAt<0) stableAt=now-at;
            }
        }
        const bool ok=stableAt>=0 && stableAt<=4*60.0/to && end-at-stableAt>=2*60.0/to && pulseViolations==0;
        std::printf("small-step %.0f->%.0f rate=%.3fs stable=%.3fs phase-worst=%.2fms rate-worst=%.2fBPM rapid=%d pulses=%d %s\n",
                    from,to,rateAt,stableAt,phaseWorst,rateWorst,transitions,pulseViolations,ok?"PASS":"FAIL");
        failures+=!ok;
    }
    return failures?1:0;
}
