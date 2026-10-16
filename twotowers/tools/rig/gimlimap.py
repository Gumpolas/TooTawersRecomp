import sys, struct, math
sys.path.insert(0,'/tmp/claude-0/rb/src/twotowers/tools/rig')
import chain, skel
def res(path):
    b=open(path,'rb').read(); d=struct.unpack_from('<I',b,4)[0]; n=struct.unpack_from('<I',b,d)[0]
    return b,{(t,k):o for t,o,k in [struct.unpack_from('<III',b,d+4+12*i) for i in range(n)]}
W='/home/claude/run2/game_files/Worlds/'
gb,ge=res(W+'14fang2.bfg'); go=ge[(150,0xAAE0DF66)]
tpar=chain.parents(gb,go); _,tpos=skel.skel(gb,go)
T={'legR':[5,7,8,9],'legL':[4,10,11,12],'armR':[17,18,19,20,24],'armL':[25,26,27,30,31],'spine':[3,13,16,14,33],'head':(33,14),'neck':16,'pelvis':3}
src,key=sys.argv[1],int(sys.argv[2],16)
fb,fe=res(W+src); fo=fe[(150,key)]
gpar=chain.parents(fb,fo); _,gpos=skel.skel(fb,fo)
an=chain.anatomy(gpar,gpos,None)
m={}
for k in ('legR','legL','armR','armL'): m.update(chain.map_chain(gpos,an[k],tpos,T[k]))
pz=min(gpos[b][2] for b in an['hipbase']); neck_g=max(gpos[b][2] for b in an['chest'])
neck_t=tpos[T['neck']][2]; pel_t=tpos[T['pelvis']][2]
def central(b):
    z=gpos[b][2]
    if z<=neck_g: f=(z-pz)/((neck_g-pz) or 1); zt=pel_t+f*(neck_t-pel_t)
    else: zt=neck_t+(z-neck_g)*(neck_t-pel_t)/((neck_g-pz) or 1)
    return min(T['spine'],key=lambda t:abs(tpos[t][2]-zt))
limb=dict(m)
for b in range(len(gpar)):
    if b in limb: continue
    p=gpar[b]; anc=None; seen=0
    while p>=0 and seen<100:
        if p in limb: anc=p; break
        p=gpar[p] if gpar[p]!=p else -1; seen+=1
    m[b]=limb[anc] if anc is not None else central(b)
for b in range(len(gpar)):
    if b in limb: continue
    p=gpar[b]; seen=0
    while p>=0 and seen<100:
        if m.get(b) in T['head']: break
        if m.get(p) in T['head'] and p not in limb: m[b]=m[p]; break
        p=gpar[p] if gpar[p]!=p else -1; seen+=1
print(an, file=sys.stderr)
print('{ '+', '.join(str(m[i]) for i in range(len(gpar)))+' }')
