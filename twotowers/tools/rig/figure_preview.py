import sys; sys.path.insert(0,'/tmp/claude-0/rend'); sys.path.insert(0,'/tmp/claude-0/rig2'); sys.path.insert(0,'/tmp/claude-0/rb/src/twotowers/tools/rig')
import numpy as np, mesh, aniz, skel, chain, math
from PIL import Image
G={'gandalf':('04balin1.bfa',0x3E74698A,0xDDAEA31C,0xF2BE2DCC),'boromir':('04balin1.bfa',0xA6841986,0xF9C62B16,0xFD8B0956),
   'frodo':('04balin1.bfa',0xD7848F26,0x4346DB86,0xD4263554),'lurtz':('13amon1.bfa',0x282C7492,0xBC42B3A8,0x2D8D24C0)}
def desc(par,b):
    out={b}; ch=True
    while ch:
        ch=False
        for i,p in enumerate(par):
            if p in out and i not in out: out.add(i); ch=True
    return out
def render(g,ang=65):
    f,mk,ak,tk=G[g]; b,e=mesh.res(f); o=e[(150,ak)]
    par=chain.parents(b,o); _,pos=skel.skel(b,o); an=chain.anatomy(par,pos,None)
    V,T,P,own=mesh.load_mesh(b,e[(100,mk)]); tex=mesh.load_tex(b,e[(300,tk)]); T=T[T.max(1)<len(V)]
    tab={s:bn for bn,s in P[0]['tab']}
    X=V[:,:3].copy(); N=V[:,3:6].copy()
    for side,sg in (('armR',1),('armL',-1)):
        sh=an[side][0]; D=desc(par,sh); piv=np.array(pos[sh])
        a=math.radians(ang)*sg
        R=np.array([[math.cos(a),0,math.sin(a)],[0,1,0],[-math.sin(a),0,math.cos(a)]])
        for i,v in enumerate(V):
            w=0
            for si,wi in ((8,9),(10,11)):
                if v[wi]>0 and tab.get(int(v[si])//3) in D: w+=v[wi]
            if w>0:
                X[i]=X[i]*(1-w)+w*((R@(V[i,:3]-piv))+piv); N[i]=N[i]*(1-w)+w*(R@V[i,3:6])
    return Image.fromarray(mesh.raster(X,N,V[:,6:8],T,tex,W=128,H=256))
if __name__=='__main__':
    ims=[render(g) for g in G]
    W=Image.new('RGBA',(512,256),(255,0,255,255))
    for i,im in enumerate(ims): W.alpha_composite(im,(i*128,0))
    W.save('/tmp/claude-0/rend/pose.png')
