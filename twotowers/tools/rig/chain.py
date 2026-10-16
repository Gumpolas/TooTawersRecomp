import skel, math
def parents(img,o):
    import struct
    h=o+0x40; nb=struct.unpack_from('<I',img,h+8)[0]; po=struct.unpack_from('<I',img,h+0x10)[0]
    return [img[h+po+b]-1 for b in range(nb)]
def mesh_tables(img,o):
    import struct
    n=struct.unpack_from('<I',img,o+0x2C)[0]; p=o+0x30; out=[]
    for _ in range(n):
        c=struct.unpack_from('<I',img,p+0x20)[0]
        out.append((p+0x24-o,[(img[p+0x24+2*i],img[p+0x25+2*i]) for i in range(c)]))
        p=(p+0x24+2*c+3)&~3
    return out
def path_up(par,b):
    out=[b]
    while par[out[-1]]>=0 and par[out[-1]]!=out[-1]: out.append(par[out[-1]])
    return out
def children(par):
    ch={i:[] for i in range(len(par))}
    for i,p in enumerate(par):
        if p>=0 and p!=i: ch[p].append(i)
    return ch
def leaves(par):
    ch=children(par); return [i for i in ch if not ch[i]]
# Aragorn's anatomy (level/menu skeleton, 51 bones)
A = {'pelvis':2,'spine':[2,15,16,39,40],'legR':[5,9,10,11],'legL':[4,12,13,14],
     'armR':[21,22,24,25,28],'armL':[30,31,33,34,37],'head':40}
def anatomy(par,pos,used):
    """find pelvis, legs, arms, spine for a guest skeleton"""
    lv=leaves(par)
    def side_leaf(sign,key):
        c=[b for b in lv if (pos[b][0]>0)==(sign>0) and abs(pos[b][0])>0.15]
        return key(c)
    footR=side_leaf(1,lambda c:min(c,key=lambda b:(pos[b][2],-abs(pos[b][0]))))
    footL=side_leaf(-1,lambda c:min(c,key=lambda b:(pos[b][2],-abs(pos[b][0]))))
    handR=side_leaf(1,lambda c:max(c,key=lambda b:abs(pos[b][0])))
    handL=side_leaf(-1,lambda c:max(c,key=lambda b:abs(pos[b][0])))
    def limb(tip):
        p=path_up(par,tip)   # tip .. root
        # stop at the first bone near the centre line
        out=[]
        for b in p:
            if abs(pos[b][0])<0.2: break
            out.append(b)
        return list(reversed(out)), b   # root-side first, and the centre bone it hangs from
    legR,pr=limb(footR); legL,pl=limb(footL); armR,cr=limb(handR); armL,cl=limb(handL)
    return dict(legR=legR,legL=legL,armR=armR,armL=armL,hipbase=(pr,pl),chest=(cr,cl))
def arc(pos,chain):
    d=[0.0]
    for a,b in zip(chain,chain[1:]): d.append(d[-1]+math.dist(pos[a],pos[b]))
    t=d[-1] or 1
    return [x/t for x in d]
def map_chain(gpos,gch,tpos,tch):
    ga=arc(gpos,gch); ta=arc(tpos,tch); m={}
    for b,f in zip(gch,ga):
        m[b]=tch[min(range(len(tch)),key=lambda i:abs(ta[i]-f))]
    return m
def build(gpar,gpos,gused,tpos):
    an=anatomy(gpar,gpos,gused)
    m={}
    for k in ('legR','legL','armR','armL'): m.update(map_chain(gpos,an[k],tpos,A[k]))
    # centre line: pelvis .. head by height
    pz=min(gpos[b][2] for b in an['hipbase']); 
    # head top: highest central used bone below any obvious hair spike: use neck = chest base of arms parent chain
    tz=[tpos[b][2] for b in A['spine']]
    neck_g=max(gpos[b][2] for b in an['chest'])   # where arms attach (upper chest)
    neck_t=tpos[18][2]
    pel_t=tpos[2][2]
    def central(b):
        z=gpos[b][2]
        # piecewise: pelvis..arm base -> target pelvis..arm base; above -> scaled
        if z<=neck_g: f=(z-pz)/((neck_g-pz) or 1); zt=pel_t+f*(neck_t-pel_t)
        else: zt=neck_t+(z-neck_g)*(neck_t-pel_t)/((neck_g-pz) or 1)
        return min(A['spine'],key=lambda t:abs(tpos[t][2]-zt))
    limb=dict(m)
    for b in range(len(gpar)):
        if b in limb: continue
        p=gpar[b]; anc=None; seen=0
        while p>=0 and seen<100:
            if p in limb: anc=p; break
            p=gpar[p] if gpar[p]!=p else -1; seen+=1
        m[b]=limb[anc] if anc is not None else central(b)
    # anything hanging from the head (beard, hair, jaw) moves with the head
    for b in range(len(gpar)):
        if b in limb: continue
        p=gpar[b]; seen=0
        while p>=0 and seen<100:
            if m.get(b) in (39,40): break
            if m.get(p) in (39,40) and p not in limb: m[b]=m[p]; break
            p=gpar[p] if gpar[p]!=p else -1; seen+=1
    return m,an
