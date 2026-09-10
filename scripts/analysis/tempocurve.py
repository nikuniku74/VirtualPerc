import wave, numpy as np, sys
p=sys.argv[1]; lo=float(sys.argv[2]); hi=float(sys.argv[3])
w=wave.open(p,'rb'); sr=w.getframerate(); nch=w.getnchannels()
x=np.frombuffer(w.readframes(w.getnframes()),dtype=np.int16).astype(np.float32)/32768.0
x=x.reshape(-1,nch).mean(1)
win=1024; hop=256; frate=sr/hop
nf=(len(x)-win)//hop; h=np.hanning(win)
mag=np.empty((nf,win//2+1),np.float32)
for i in range(nf): mag[i]=np.abs(np.fft.rfft(x[i*hop:i*hop+win]*h))
flux=np.concatenate([[0.0],np.maximum(np.diff(np.log1p(mag*40),axis=0),0).sum(1)])
flux=flux-np.convolve(flux,np.ones(int(frate*0.5))/int(frate*0.5),'same')
flux=np.maximum(flux,0)
W=12.0   # window for the tempogram
lags=np.arange(int(frate*60/hi), int(frate*60/lo))
print(f"{'t':>6} {'BPM':>7} {'nitid':>6}")
for t in np.arange(0, len(x)/sr-W, 10.0):
    seg=flux[int(t*frate):int((t+W)*frate)]
    if len(seg)<max(lags)*2: continue
    seg=seg-seg.mean()
    ac=np.correlate(seg,seg,'full')[len(seg)-1:]
    ac=ac/max(ac[0],1e-9)
    v=ac[lags]
    k=int(np.argmax(v)); lag=lags[k]
    a,b,c=(ac[lag-1],ac[lag],ac[lag+1])
    d=(a-c)/(2*(a-2*b+c)) if (a-2*b+c)!=0 else 0.0
    bpm=60*frate/(lag+d)
    srt=np.sort(v)[::-1]
    clarity=float(srt[0]-np.median(v))
    print(f"{t:6.0f} {bpm:7.2f} {clarity:6.2f}")
