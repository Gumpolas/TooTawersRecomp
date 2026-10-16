import json,re,struct,numpy as np,sys,os,time
from PIL import Image
import esr
src,out,part,nparts=sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4])
lst=json.load(open(sys.argv[5])); os.makedirs(out,exist_ok=True); t0=time.time()
for i,f in enumerate(lst):
    if i%nparts!=part: continue
    m=re.match(r'([0-9A-F]{8})_(\d+)x(\d+)',f); w,h=int(m.group(2)),int(m.group(3)); o=os.path.join(out,m.group(1)+'.png')
    if os.path.exists(o): continue
    b=open(os.path.join(src,f),'rb').read(); a=np.frombuffer(b[54:54+w*h*4],np.uint8).reshape(h,w,4)[:,:,[2,1,0,3]]
    rgb=esr.upscale(np.ascontiguousarray(a[:,:,:3]))
    if a[:,:,3].min()<250:
        al=esr.upscale(np.repeat(a[:,:,3:4],3,2))[:,:,1]
    else: al=np.full(rgb.shape[:2],255,np.uint8)
    Image.fromarray(np.dstack([rgb,al])).save(o+'.tmp.png'); os.replace(o+'.tmp.png',o)
    print(i,f,'%.0fs'%(time.time()-t0),flush=True)
