import wave, numpy as np, sys
wav, pulses = sys.argv[1], sys.argv[2]
W = float(sys.argv[3]) if len(sys.argv)>3 else 20.0
w=wave.open(wav,'rb'); sr=w.getframerate(); nch=w.getnchannels()
x=np.frombuffer(w.readframes(w.getnframes()),dtype=np.int16).astype(np.float32)/32768.0
x=x.reshape(-1,nch).mean(1)
# low band 30-200 Hz envelope -> kick/snare body only
def onepole(fc): return 1-np.exp(-2*np.pi*fc/sr)
a1,a2=onepole(200.0),onepole(30.0)
lp=0.0; hp=0.0; env=np.empty(len(x),np.float32)
for i,v in enumerate(x):
    lp+=a1*(v-lp); hp+=a2*(lp-hp); env[i]=abs(lp-hp)
k=int(sr*0.004); env=np.convolve(env,np.ones(k)/k,'same')
d=np.maximum(np.diff(env,prepend=env[0]),0)
step=int(sr*0.001); d=d[::step]; fr=sr/step
thr=np.percentile(d,97)*0.30
ons=[]; i=1; guard=int(fr*0.12)
while i<len(d)-1:
    if d[i]>thr and d[i]>=d[i-1] and d[i]>=d[i+1]:
        ons.append(i/fr); i+=guard
    else: i+=1
ons=np.array(ons)
P=np.loadtxt(pulses, comments='#')
pt,bph,bar,bpm,snd = P[:,0],P[:,1],P[:,2],P[:,3],P[:,4]
un=lambda v: np.unwrap(v*2*np.pi)/(2*np.pi)
ph  = np.interp(ons, pt, un(bph))%1.0
barp= np.interp(ons, pt, un(bar))%1.0
bp  = np.interp(ons, pt, bpm); sn=np.interp(ons,pt,snd)
m=(bp>50)&(sn>0.5); ons,ph,barp,bp=ons[m],ph[m],barp[m],bp[m]
print(f"{len(ons)} colpi di banda bassa, {ons[0]:.0f}-{ons[-1]:.0f} s")
print(f"\n{'finestra':>11} {'colpi':>5} {'agganciato':>11} {'dispersione':>12} {'quarto':>7}  giudizio")
prev=None
for t0 in np.arange(0, ons[-1]-W, W):
    s=(ons>=t0)&(ons<t0+W)
    if s.sum()<6: continue
    a=ph[s]*2*np.pi
    R=abs(np.mean(np.exp(1j*a)))                # 0 = sparso, 1 = tutti insieme
    mu=(np.angle(np.mean(np.exp(1j*a)))/(2*np.pi))%1.0
    dev=np.sqrt(max(0.0,-2*np.log(max(R,1e-9))))/(2*np.pi)*60000/np.median(bp[s])
    q=(np.median(barp[s])*4.0)%4.0
    jump = "" if prev is None else ("   <<< L'UNO SI SPOSTA" if abs(((q-prev+2)%4)-2)>0.6 else "")
    verdict = "agganciato" if R>0.55 else ("debole" if R>0.35 else "FUORI")
    print(f"{int(t0//60)}:{int(t0%60):02d}-{int((t0+W)//60)}:{int((t0+W)%60):02d} {s.sum():5d} "
          f"{R:11.2f} {dev:9.0f} ms {q:7.2f}  {verdict}{jump}")
    prev=q
