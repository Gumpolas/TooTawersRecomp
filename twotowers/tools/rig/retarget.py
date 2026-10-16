"""Per-guest copy of a hero animation set whose inverse binds put the guest's
model (re-rigged by map) onto the hero skeleton."""
import struct, math, skel, chain
def mats(img,o):
    h=o+0x40; nb=struct.unpack_from('<I',img,h+8)[0]; mo=struct.unpack_from('<I',img,h+0xC)[0]
    return [list(struct.unpack_from('<32f',img,h+mo+b*128)) for b in range(nb)], h+mo
def mul(a,b):  # 4x4 row-major
    return [sum(a[r*4+k]*b[k*4+c] for k in range(4)) for r in range(4) for c in range(4)]
def primaries(gpos,tpos,m,s):
    prim={}
    for b,t in m.items():
        if b in (0,1): continue
        d=math.dist([c*s for c in gpos[b]],tpos[t])
        if t not in prim or d<prim[t][0]: prim[t]=(d,b)
    return {t:b for t,(d,b) in prim.items()}
def patched(timg,to,tlen,gimg,go,m,s,mode='inv'):
    blob=bytearray(timg[to:to+tlen])
    tm,tbase=mats(timg,to); gm,_=mats(gimg,go)
    tpos=[(x[28],x[29],x[30]) for x in tm]; gpos=[(x[28],x[29],x[30]) for x in gm]
    prim=primaries(gpos,tpos,m,s)
    S=[s,0,0,0, 0,s,0,0, 0,0,s,0, 0,0,0,1]
    for t,g in prim.items():
        inv=mul(gm[g][:16],S)
        off=tbase-to+t*128
        struct.pack_into('<16f',blob,off,*inv)
    return bytes(blob),prim
