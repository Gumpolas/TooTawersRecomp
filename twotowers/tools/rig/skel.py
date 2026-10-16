import sys,struct; sys.path.insert(0,'/tmp/claude-0/v14')
import bf
def skel(img,o):
    """anim set at o -> (parents, world positions, inverse binds raw)"""
    h=o+0x40
    nb=struct.unpack_from('<I',img,h+8)[0]
    mo,po=struct.unpack_from('<II',img,h+0xC)[0],struct.unpack_from('<I',img,h+0x10)[0]
    par=list(img[h+po:h+po+nb])
    pos=[]
    for b in range(nb):
        f=struct.unpack_from('<32f',img,h+mo+b*128)
        pos.append((f[28],f[29],f[30]))
    return par,pos
def mesh_pairs(img,o):
    n=struct.unpack_from('<I',img,o+0x50)[0]
    return [(img[o+0x54+2*i],img[o+0x55+2*i]) for i in range(n)]
def load(path):
    ver,img,ents=bf.load(path)
    idx={(k,t):o for o,k,t in ents}
    return img,idx
