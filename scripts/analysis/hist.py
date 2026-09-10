import wave, numpy as np, sys
wav, pulses = sys.argv[1], sys.argv[2]
W = float(sys.argv[3]) if len(sys.argv)>3 else 20.0
w=wave.open(wav,'rb'); sr=w.getframerate(); nch=w.getnchannels()
x=np.frombuffer(w.readframes(w.getnframes()),dtype=np.int16).astype(np.float32)/32768.0
x=x.reshape(-1,nch).mean(1)
win=1024; hop=128; frate=sr/hop
nf=(len(x)-win)//hop; h=np.hanning(win)
mag=np.empty((nf,win//2+1),np.float32)
for i in range(nf): mag[i]=np.abs(np.fft.rfft(x[i*hop:i*hop+win]*h))
fl=np.concatenate([[0.0],np.maximum(np.diff(np.log1p(mag*40),axis=0),0).sum(1)])
fl=np.maximum(fl-np.convolve(fl,np.ones(int(frate*0.5))/int(frate*0.5),'same'),0)
t=np.arange(len(fl))/frate
P=np.loadtxt(pulses,comments='#')
pt,bph,bar,bpm,snd=P[:,0],P[:,1],P[:,2],P[:,3],P[:,4]
un=lambda v: np.unwrap(v*2*np.pi)/(2*np.pi)
ph=np.interp(t,pt,un(bph))%1.0; barp=np.interp(t,pt,un(bar))%1.0
bp=np.interp(t,pt,bpm); sn=np.interp(t,pt,snd)
ok=(bp>50)&(sn>0.5)&(t>=pt[0])&(t<=pt[-1])
NB=24
print(f"{'finestra':>11} {'struttura':>10} {'picco a':>9} {'deriva':>8}  giudizio")
prev=None
for t0 in np.arange(0, t[ok][-1]-W, W):
    m=ok&(t>=t0)&(t<t0+W)
    if m.sum()<1000 or fl[m].sum()<=0: continue
    hb,_=np.histogram(ph[m],bins=NB,range=(0,1),weights=fl[m])
    hb=hb/max(hb.mean(),1e-9)
    pk=int(np.argmax(hb)); peak=pk/NB
    # circular: refine peak with neighbours
    struct=hb.max()
    drift = None if prev is None else ((peak-prev+0.5)%1.0-0.5)
    ms = 0 if drift is None else drift*60000/np.median(bp[m])
    v = "agganciato" if struct>2.0 else ("debole" if struct>1.5 else "FUORI")
    slip = "" if drift is None else ("   <<< SLITTA" if abs(drift)>0.12 else "")
    print(f"{int(t0//60)}:{int(t0%60):02d}-{int((t0+W)//60)}:{int((t0+W)%60):02d} "
          f"{struct:10.2f} {peak:9.2f} {('' if drift is None else f'{ms:+6.0f}ms'):>8}  {v}{slip}")
    prev=peak
