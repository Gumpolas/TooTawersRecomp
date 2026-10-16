import struct, sys, collections
def load(path):
    """-> (version, data-before-directory bytes (offsets are file offsets), entries [(offset, key, type)])
    file: u8 version 9 .. u32 @4 = dir offset n; @n: u32 count, then count x {type, offset, key}"""
    d=open(path,'rb').read()
    ver=d[0]; n=struct.unpack_from('<I',d,4)[0]
    cnt=struct.unpack_from('<I',d,n)[0]
    ents=[]
    for i in range(cnt):
        t,o,k=struct.unpack_from('<III',d,n+4+12*i); ents.append((o,k,t))
    return ver,d[:n],ents
if __name__=='__main__':
    ver,img,ents=load(sys.argv[1])
    print('ver',ver,'dir at',hex(len(img)),'entries',len(ents))
    print(sorted(collections.Counter(t for o,k,t in ents).items()))
