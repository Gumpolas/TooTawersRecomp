import json,struct,numpy as np,sys
from PIL import Image
L=json.load(open(sys.argv[1])); bad=[]
for f,r,ga in L:
    b=open(r,'rb').read(); h,w=struct.unpack_from('<II',b,12); mc=max(1,struct.unpack_from('<I',b,28)[0])
    off=148; W,H=w,h; lv=[]
    for i in range(mc):
        sz=max(1,(W+3)//4)*max(1,(H+3)//4)*16; lv.append((W,H,off,sz)); off+=sz; W=max(1,W//2); H=max(1,H//2)
    W,H,o,sz=([l for l in lv if l[0]>=64 and l[1]>=64] or [lv[0]])[-1]
    im=np.asarray(Image.frombytes('RGBA',(max(4,(W+3)//4*4),max(4,(H+3)//4*4)),b[o:o+sz],'bcn',(7,)).crop((0,0,W,H)),dtype=float)[:,:,3]
    pa=np.array([[im[y*H//16:max(y*H//16+1,(y+1)*H//16),x*W//16:max(x*W//16+1,(x+1)*W//16)].mean() for x in range(16)] for y in range(16)])
    ga=np.array(ga); n=((ga<100)&(pa>ga+80)).sum()
    if n>=1: bad.append((f,r,int(n)))
json.dump(bad,open(sys.argv[2],'w')); print(len(L),len(bad)); [print(x) for x in bad[:80]]
