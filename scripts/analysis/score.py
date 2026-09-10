import numpy as np, sys
S="/private/tmp/claude-501/-Users-nicolamarogna-Desktop-NK-VirtualPerc/235c30fa-57aa-4483-87f0-24d6c98450a4/scratchpad"
app=[]
for l in open(sys.argv[1]):
    if l.startswith('#') or not l.strip(): continue
    f=l.split()
    try: app.append((float(f[0]), float(f[1])))
    except: pass
app=np.array(app)
tr=[]
for l in open(f"{S}/b3-truth.txt"):
    f=l.split()
    if len(f)==3 and f[0].isdigit():
        t,b,c=float(f[0]),float(f[1]),float(f[2])
        if c>0.15: tr.append((t+6.0,b))
tr=np.array(tr)
ref=np.interp(app[:,0], tr[:,0], tr[:,1])
m=(app[:,0]>tr[0,0]) & (app[:,0]<tr[-1,0]) & (app[:,1]>50)
t=app[m,0]; pub=app[m,1]; r=ref[m]; err=(pub-r)/r*100
out=False; st=0; worst=0; n=0; tot=0; durs=[]
for i in range(len(t)):
    e=abs(err[i])
    if not out and e>4.0: out=True; st=t[i]; worst=e
    elif out:
        worst=max(worst,e)
        if e<2.0:
            out=False; n+=1; tot+=t[i]-st; durs.append(t[i]-st)
if out: n+=1; durs.append(t[-1]-st); tot+=t[-1]-st
print(f"  errore medio {np.abs(err).mean():5.2f}%   peggiore {np.abs(err).max():5.2f}%"
      f"   dev.std pubbl {pub.std()/pub.mean()*100:4.2f}%"
      f"   uscite {n}  durate {[f'{d:.0f}s' for d in durs]}  totale fuori {tot:.0f}s")
