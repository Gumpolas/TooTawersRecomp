import numpy as np, struct
def c565(c):
    r=((c>>11)&31)*255//31; g=((c>>5)&63)*255//63; b=(c&31)*255//31
    return np.stack([r,g,b],-1).astype(np.int32)
def decode(data,w,h,fmt):
    bw,bh=max(1,w//4),max(1,h//4); bs=8 if fmt==0x0C else 16
    out=np.zeros((h,w,4),np.uint8)
    a=np.frombuffer(data[:bw*bh*bs],np.uint8).reshape(bh*bw,bs)
    col=a[:,bs-8:]
    c0=col[:,0].astype(np.int32)|(col[:,1].astype(np.int32)<<8); c1=col[:,2].astype(np.int32)|(col[:,3].astype(np.int32)<<8)
    C0=c565(c0);C1=c565(c1)
    idx=(col[:,4].astype(np.uint32)|(col[:,5].astype(np.uint32)<<8)|(col[:,6].astype(np.uint32)<<16)|(col[:,7].astype(np.uint32)<<24))
    pal=np.zeros((len(a),4,4),np.int32); pal[:,0,:3]=C0; pal[:,1,:3]=C1; pal[:,:,3]=255
    four=(c0>c1)|(fmt!=0x0C)
    pal[:,2,:3]=np.where(four[:,None],(2*C0+C1)//3,(C0+C1)//2)
    pal[:,3,:3]=np.where(four[:,None],(C0+2*C1)//3,0)
    if fmt==0x0C: pal[:,3,3]=np.where(four,255,0)
    px=np.zeros((len(a),16,4),np.int32)
    for i in range(16):
        k=(idx>>(2*i))&3
        px[:,i]=pal[np.arange(len(a)),k]
    if fmt==0x0E:
        al=a[:,:8]
        for i in range(16):
            v=(al[:,i//2]>>(4*(i%2)))&15; px[:,i,3]=v*17
    elif fmt==0x0F:
        a0=a[:,0].astype(np.int32);a1=a[:,1].astype(np.int32)
        bits=np.zeros(len(a),np.uint64)
        for j in range(6): bits|=a[:,2+j].astype(np.uint64)<<np.uint64(8*j)
        ap=np.zeros((len(a),8),np.int32); ap[:,0]=a0; ap[:,1]=a1
        big=a0>a1
        for j in range(2,8):
            ap[:,j]=np.where(big,((8-j)*a0+(j-1)*a1)//7,0)
        for j in range(2,6):
            ap[:,j]=np.where(big,ap[:,j],((6-j)*a0+(j-1)*a1)//5)
        ap[:,6]=np.where(big,ap[:,6],0); ap[:,7]=np.where(big,ap[:,7],255)
        for i in range(16):
            k=((bits>>np.uint64(3*i))&np.uint64(7)).astype(np.int64)
            px[:,i,3]=ap[np.arange(len(a)),k]
    px=px.reshape(bh,bw,4,4,4).transpose(0,2,1,3,4).reshape(bh*4,bw*4,4)
    return px[:h,:w].astype(np.uint8)
