import sys, pickle, re
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from tools.recomp.disasm import Disassembler, Instruction, Operand, BasicBlock
from tools.recomp.lifter import Lifter, lift_basic_block
shapes=pickle.load(open('shapes.pkl','rb'))
D=Disassembler()
PUSH={'fld','fild','fld1','fldz','fldpi','fldl2e','fldlg2','fldln2','fsincos','fptan'}
POP={'faddp','fsubp','fsubrp','fmulp','fdivp','fdivrp','fstp','fistp','fcomp','fpatan','fyl2x','ffreep','fbstp'}
POP2={'fcompp'}
CMP={'fcom','fcomp','fcompp','ftst','fxam'}
SKIP={'fnsave','frstor','fldcw','fnclex','fbstp','ffreep','fprem','fnstcw','f2xm1','fscale','fxam'}
REGS=['eax','ecx','edx','ebx','esp','ebp','esi','edi']
def mk(addr,mn,txt,ops,size=6):
    i=Instruction(addr,size,mn,txt,''); i.operands=ops; return i
cases=[]
for key,exs in sorted(shapes.items()):
    m=key[0]
    if not m.startswith('f') or m in SKIP: continue
    for addr,hx,txt in exs[:3]:
        cs=list(D._cs.disasm(bytes.fromhex(hx),addr))
        if not cs: continue
        insn=D._decode_instruction(cs[0])
        bad=False
        for o in insn.operands:
            if o.type=='mem' and (o.mem_seg or (o.mem_base and o.mem_base not in REGS) or (o.mem_index and o.mem_index not in REGS)): bad=True
        if bad: continue
        cases.append((insn,txt))
c=['#define RECOMP_GENERATED_CODE\n#include "harness.h"']; s=['.intel_syntax noprefix\n.text']; tab=[]
IN=0x10000E00; OUT=0x10000E80
for n,(insn,txt) in enumerate(cases):
    m=insn.mnemonic
    depth=7 if m in PUSH else 8
    after=depth+(1 if m in PUSH else 0)-(1 if m in POP else 0)-(2 if m in POP2 else 0)
    pre=[]; post=[]; ptxt=[]; qtxt=[]
    for k in range(depth):
        a=IN+8*k
        pre.append(mk(addr-0x100+k*6,'fld','qword ptr [0x%x]'%a,[Operand(type='mem',mem_disp=a,mem_size=8)]))
        ptxt.append('fld qword ptr [0x%x]'%a)
    extra=[]
    if m in CMP or m=='fnstsw':
        pass
    if m in CMP:
        extra=[mk(insn.address+insn.size,'fnstsw','ax',[Operand(type='reg',reg='ax')],2)]
    for k in range(after):
        a=OUT+8*k
        post.append(mk(insn.address+0x40+k*6,'fstp','qword ptr [0x%x]'%a,[Operand(type='mem',mem_disp=a,mem_size=8)]))
        qtxt.append('fstp qword ptr [0x%x]'%a)
    seq=pre+[insn]+extra+post
    lf=Lifter(); lf.needs_cf=True
    try: stmts,_=lift_basic_block(lf,BasicBlock(start=seq[0].address,instructions=seq))
    except Exception as ex: print('LIFTFAIL',txt,ex,file=sys.stderr); continue
    fix=[]; absaddr='0'
    for o in insn.operands:
        if o.type!='mem': continue
        d=o.mem_disp&0xFFFFFFFF
        if not o.mem_base and not o.mem_index: absaddr='0x%xu'%d
        else:
            idx=o.mem_index; base=o.mem_base
            if idx: fix.append(f"r->X{idx} = rnd()%16;")
            expr=f"T - 0x{d:x}u"+(f" - r->X{idx}*{o.mem_scale}u" if idx else "")
            if base: fix.append(f"r->X{base} = {expr};")
            else: fix.append(f"r->X{idx} = (T - 0x{d:x}u)/{o.mem_scale}u;")
    c.append(f"static void fix_{n}(Regs *r, uint32_t T){{ (void)r;(void)T; {' '.join(fix)} }}")
    c.append(f"static void lift_{n}_0(void){{ LIFT_PROLOGUE\n"+'\n'.join(stmts)+"\nLIFT_EPILOGUE }")
    body='\n '.join(ptxt+[txt]+(['fnstsw ax'] if extra else [])+qtxt)
    s.append(f".globl nat_{n}\nnat_{n}:\n NAT_PRE\n fninit\n {body}\n NAT_POST\n")
    tab.append(f"{{fix_{n}, {{lift_{n}_0}}, {{0}}, nat_{n}, 0, {absaddr}, \"{txt}\", \"x87:{after}:{1 if extra else 0}\"}}")
hdr=['extern void nat_%d(void);'%n for n in range(len(tab))]
c.append('Case CASES[] = {\n'+',\n'.join(tab)+'};\nint NCASES = %d;'%len(tab))
open('cases.c','w').write(c[0]+'\n'+'\n'.join(hdr)+'\n'+'\n'.join(c[1:]))
open('cases.S','w').write('#include "nat.h"\n'+'\n'.join(s)+'\n')
print(len(tab),file=sys.stderr)
