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
# percussive onset strength: positive spectral flux, whole band, log-compressed
fl=np.concatenate([[0.0],np.maximum(np.diff(np.log1p(mag*40),axis=0),0).sum(1)])
fl=np.maximum(fl-np.convolve(fl,np.ones(int(frate*0.5))/int(frate*0.5),'same'),0)
t=np.arange(len(fl))/frate
P=np.loadtxt(pulses, comments='#')
pt,bph,bar,bpm,snd=P[:,0],P[:,1],P[:,2],P[:,3],P[:,4]
un=lambda v: np.unwrap(v*2*np.pi)/(2*np.pi)
ph=np.interp(t,pt,un(bph))%1.0
bp=np.interp(t,pt,bpm); sn=np.interp(t,pt,snd)
barp=np.interp(t,pt,un(bar))%1.0
ok=(bp>50)&(sn>0.5)&(t>=pt[0])&(t<=pt[-1])
print(f"{'finestra':>11} {'aggancio':>9} {'scarto':>9} {'quarto':>7}  giudizio")
prev=None
for t0 in np.arange(0, t[ok][-1]-W, W):
    m=ok&(t>=t0)&(t<t0+W)
    if m.sum()<1000: continue
    a=ph[m]*2*np.pi; wgt=fl[m]
    if wgt.sum()<=0: continue
    z=np.sum(wgt*np.exp(1j*a))/wgt.sum()
    R=abs(z); mu=(np.angle(z)/(2*np.pi))%1.0
    off=((mu+0.5)%1.0-0.5)*60000/np.median(bp[m])
    zb=np.sum(wgt*np.exp(1j*barp[m]*2*np.pi))/wgt.sum()
    q=((np.angle(zb)/(2*np.pi))%1.0)*4
    jump="" if prev is None else ("   <<< L'UNO SI SPOSTA" if abs(((q-prev+2)%4)-2)>0.55 else "")
    v="agganciato" if R>0.30 else ("debole" if R>0.15 else "FUORI")
    print(f"{int(t0//60)}:{int(t0%60):02d}-{int((t0+W)//60)}:{int((t0+W)%60):02d} "
          f"{R:9.3f} {off:+7.0f} ms {q:7.2f}  {v}{jump}")
    prev=q
