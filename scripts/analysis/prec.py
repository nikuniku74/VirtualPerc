import wave, numpy as np, sys
wav=sys.argv[1]; pul=sys.argv[2]; lo=float(sys.argv[3]); hi=float(sys.argv[4])
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
P=np.loadtxt(pul,comments='#')
pt,bph,bpm,sn=P[:,0],P[:,1],P[:,3],P[:,5]
un=lambda v: np.unwrap(v*2*np.pi)/(2*np.pi)
ph=np.interp(t,pt,un(bph))%1.0; bp=np.interp(t,pt,bpm); s2=np.interp(t,pt,sn)
ok=(bp>50)&(s2>0.5)&(t>lo)&(t<hi)
NB=24; W=10.0; peaks=[]; structs=[]
for t0 in np.arange(lo,hi-W,W):
    m=ok&(t>=t0)&(t<t0+W)
    if m.sum()<500 or fl[m].sum()<=0: continue
    hb,_=np.histogram(ph[m],bins=NB,range=(0,1),weights=fl[m]); hb/=max(hb.mean(),1e-9)
    structs.append(hb.max()); peaks.append(np.argmax(hb)/NB)
peaks=np.array(peaks)
d=np.array([((peaks[i]-peaks[i-1]+0.5)%1.0)-0.5 for i in range(1,len(peaks))])
med=np.median(bpm[bpm>50])
# instantaneous grid rate spread
mp=(pt>lo)&(pt<hi)&(bpm>50)&(sn>0.5)
ptt,bb,mm=pt[mp],bph[mp],bpm[mp]
u=np.unwrap(bb*2*np.pi)/(2*np.pi); k=int(0.5/np.median(np.diff(ptt)))
inst=(u[k:]-u[:-k])/(ptt[k:]-ptt[:-k])*60.0
rel=(inst-mm[k:])/mm[k:]*100
print(f"  struttura {np.mean(structs):5.2f}   "
      f"spostamento fase fra finestre: mediano {np.median(np.abs(d))*60000/med:5.1f} ms  "
      f"peggiore {np.max(np.abs(d))*60000/med:6.1f} ms   "
      f"strattoni griglia: dev.std {rel.std():4.2f}%  peggiore {np.abs(rel).max():5.1f}%")
