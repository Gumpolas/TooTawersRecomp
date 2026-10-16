import json, sys, collections, pickle
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from tools.recomp.disasm import Disassembler
TK = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
an=json.load(open(TK+'/game_files/default_analysis.json'))
xbe=open(TK+'/game_files/default.xbe','rb').read()
secs=[(int(s['virtual_addr'],16),s['virtual_size'],int(s['raw_addr'],16)) for s in an['sections'] if s['executable']]
def rd(va,n):
    for v,sz,r in secs:
        if v<=va<v+sz: return xbe[r+va-v:r+va-v+n]
funcs=json.load(open(TK+'/tools/disasm/output/functions.json'))
D=Disassembler()
shapes=collections.defaultdict(list)
for f in funcs:
    s=int(f['start'],16); e=int(f['end'],16)
    b=rd(s,e-s)
    if not b: continue
    try: ins=D.disassemble_function(b,s,e)
    except Exception as ex: continue
    for i in ins:
        key=(i.mnemonic, tuple((o.type, o.mem_size, o.reg if o.type=='reg' else None) for o in i.operands))
        if len(shapes[key])<6: shapes[key].append((i.address,i.bytes_hex,i.mnemonic+' '+i.op_str))
pickle.dump(dict(shapes),open('shapes.pkl','wb'))
c=collections.Counter(k[0] for k in shapes)
print(len(shapes)); print(sorted(c.items()))
