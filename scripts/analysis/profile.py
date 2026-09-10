import wave, numpy as np, sys
p=sys.argv[1]; upto=float(sys.argv[2])*60
w=wave.open(p,'rb'); sr=w.getframerate(); nch=w.getnchannels()
win=2048; hop=1024
need=int(upto*sr)
x=np.frombuffer(w.readframes(need),dtype=np.int16).astype(np.float32)/32768.0
x=x.reshape(-1,nch).mean(1)
nf=(len(x)-win)//hop; h=np.hanning(win); frate=sr/hop
mag=np.empty((nf,win//2+1),np.float32)
for i in range(nf): mag[i]=np.abs(np.fft.rfft(x[i*hop:i*hop+win]*h))
f=np.fft.rfftfreq(win,1/sr); lo=f<200
E=mag**2; lowS=E[:,lo].sum(1)/np.maximum(E.sum(1),1e-20)
flux=np.concatenate([[0.0],np.maximum(np.diff(mag,axis=0),0).sum(1)])
def bpm(t0,t1):
    seg=flux[int(t0*frate):int(t1*frate)]
    if len(seg)<128: return 0,0
    seg=seg-seg.mean()
    if seg.std()<1e-9: return 0,0
    ac=np.correlate(seg,seg,'full')[len(seg)-1:]; ac/=ac[0]
    a=int(frate*60/200); b=min(int(frate*60/55),len(ac)-1)
    if b<=a: return 0,0
    k=a+int(np.argmax(ac[a:b])); return 60*frate/k, float(ac[k])
W=10.0
print(f"{'min:sec':>8} {'rms':>7} {'lowS':>6} {'BPM':>7} {'sal':>5}")
for t in np.arange(0, upto-W, 10.0):
    seg=x[int(t*sr):int((t+W)*sr)]
    b,s=bpm(t,t+W)
    ls=lowS[int(t*frate):int((t+W)*frate)].mean()
    print(f"{int(t//60):5d}:{int(t%60):02d} {np.sqrt((seg**2).mean()):7.4f} {ls:6.3f} {b:7.1f} {s:5.2f}")
