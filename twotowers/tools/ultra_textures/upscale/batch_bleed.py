import json,re,struct,numpy as np,sys,os
from PIL import Image
import esrg
from scipy import ndimage
lst=json.load(open(sys.argv[1])); out=sys.argv[2]; os.makedirs(out,exist_ok=True)
for f in lst:
    m=re.match(r'([0-9A-F]{8})_(\d+)x(\d+)',f); w,h=int(m.group(2)),int(m.group(3))
    b=open('texture_dump/'+f,'rb').read(); a=np.frombuffer(b[54:54+w*h*4],np.uint8).reshape(h,w,4)[:,:,[2,1,0,3]].copy()
    al=a[:,:,3]; solid=al>=128
    if solid.any() and not solid.all():
        # colour bleed: transparent texels take the nearest solid texel's colour
        idx=ndimage.distance_transform_edt(~solid,return_distances=False,return_indices=True)
        a[:,:,:3]=a[idx[0],idx[1],:3]
    rgb=esrg.upscale(np.ascontiguousarray(a[:,:,:3]))
    binary=((al<16)|(al>239)).mean()>0.97
    A=np.asarray(Image.fromarray(al,'L').resize((w*4,h*4),Image.BICUBIC)).astype(np.float32)
    if binary: A=np.clip((A-128)*4+128,0,255)   # (a cut-out: kept crisp)
    Image.fromarray(np.dstack([rgb,A.astype(np.uint8)])).save(out+'/'+m.group(1)+'.png'); print(f,flush=True)
