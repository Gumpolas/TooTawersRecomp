import sys, pickle, random, re
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from tools.recomp.disasm import Disassembler, Instruction, Operand, BasicBlock
from tools.recomp.lifter import Lifter, lift_basic_block
shapes=pickle.load(open('shapes.pkl','rb'))
D=Disassembler()
ALL=['o','no','b','ae','e','ne','be','a','s','ns','l','ge','le','g']
ARITH=set('add sub cmp adc sbb neg xadd cmpxchg and or xor test inc dec'.split())
SHIFT=set('shl shr sar sal'.split()); ROT=set('rol ror rcl rcr'.split())
NOFL=set('mov movzx movsx lea xchg cdq cwde div idiv not sete setne seta setg setge setl setle bswap'.split())
def conds(m, insn):
    if m in ARITH: return ALL
    if m in SHIFT or m in ('shld','shrd'): return ['e','ne','s','ns','b','ae','be','a']
    if m in ROT: return ['b','ae']
    if m in ('imul','mul'): return ['o','no','b','ae']
    if m=='bsf' or m=='bsr': return ['e','ne']
    if m in ('bt','bts','btr','btc'): return ['b','ae']
    if m in NOFL: return []
    if m in VEC: return []
    return None
SKIP_MEM_SEG=True
VEC=set('addps cvtps2pi cvtss2si cvttss2si emms maxps minps movaps movd movhlps movhps movlhps movlps movntps movntq movq movss movups mulps packssdw paddd paddsw paddw pmulhw pmullw por pshufw pslld psllq psllw psraw psrld psrlq psrlw psubusw punpckhbw punpckhdq punpckhwd punpcklbw punpckldq punpcklwd pxor rcpps shufps sqrtss subps subss unpckhps unpcklps xorps'.split())
REGS=['eax','ecx','edx','ebx','esp','ebp','esi','edi']
VREGS=['mm%d'%i for i in range(8)]+['xmm%d'%i for i in range(8)]
def full(r):
    r=r.lower()
    m={'al':'eax','ah':'eax','ax':'eax','cl':'ecx','ch':'ecx','cx':'ecx','dl':'edx','dh':'edx','dx':'edx','bl':'ebx','bh':'ebx','bx':'ebx','sp':'esp','bp':'ebp','si':'esi','di':'edi'}
    return m.get(r,r)
cases=[]
want=set(sys.argv[1].split(',')) if len(sys.argv)>1 and sys.argv[1] else None
for key,exs in sorted(shapes.items()):
    m=key[0]
    if want and m not in want: continue
    for addr,hx,txt in exs:
        b=bytes.fromhex(hx)
        cs=list(D._cs.disasm(b,addr))
        if not cs: continue
        insn=D._decode_instruction(cs[0])
        cl=conds(m,insn)
        if cl is None: continue
        bad=False; setup=[]
        for o in insn.operands:
            if o.type=='mem':
                if o.mem_seg: bad=True
                if o.mem_base and o.mem_base not in REGS: bad=True
                if o.mem_index and o.mem_index not in REGS: bad=True
            if o.type=='reg' and full(o.reg) not in REGS and o.reg not in VREGS: bad=True
        if bad: continue
        cases.append((insn,cl,txt))
print(len(cases),file=sys.stderr)
c=[]; s=[]
c.append('#define RECOMP_GENERATED_CODE\n#include "harness.h"')
s.append('.intel_syntax noprefix\n.text')
tab=[]
for n,(insn,cl,txt) in enumerate(cases):
    bodies=[]
    lf=None
    for i,cc in enumerate(cl or ['__none']):
        ins=[insn]
        tgt=0x00DEAD00+i
        if cc!='__none':
            j=Instruction(insn.address+insn.size,2,'j'+cc,'0x%x'%tgt,'')
            j.operands=[Operand(type='imm',imm=tgt)]; j.jump_target=tgt
            ins.append(j)
        lf=Lifter(); lf.needs_cf=True
        try:
            stmts,_=lift_basic_block(lf,BasicBlock(start=insn.address,instructions=ins))
        except Exception as ex:
            print('LIFTFAIL',txt,ex,file=sys.stderr); bodies=None; break
        b='\n'.join(stmts)
        gap = ('_flags' in b.replace('int _flags','')) or 'RECOMP_UNIMPL' in b
        if cc=='__none':
            bodies.append((b+'\n', gap))
        else:
            bodies.append(('HS[%d]=0;\n'%i+b+'\n', gap))
    if bodies is None: continue
    # address fixups
    fix=[]
    absaddr='0'
    for o in insn.operands:
        if o.type!='mem': continue
        d=o.mem_disp & 0xFFFFFFFF
        if not o.mem_base and not o.mem_index:
            absaddr='0x%xu'%d
        else:
            idx=o.mem_index; base=o.mem_base
            if idx and base==idx:
                fix.append(f"T &= ~0xFu; r->X{base} = (T - 0x{d:x}u)/{1+o.mem_scale}u;")
            else:
                if idx: fix.append(f"r->X{idx} = rnd()%16;")
                expr=f"T - 0x{d:x}u" + (f" - r->X{idx}*{o.mem_scale}u" if idx else "")
                if base: fix.append(f"r->X{base} = {expr};")
                else: fix.append(f"r->X{idx} = (T - 0x{d:x}u)/{o.mem_scale}u; T = r->X{idx}*{o.mem_scale}u + 0x{d:x}u;")
    if insn.mnemonic in SHIFT|ROT|{'shld','shrd'} and any(o.type=='reg' and o.reg=='cl' for o in insn.operands[1:]):
        fix.insert(0,"if(!(r->Xecx&31)) r->Xecx|=1;")
    c.append(f"static void fix_{n}(Regs *r, uint32_t T){{ (void)r;(void)T; {' '.join(fix)} }}")
    lnames=[]
    gaps=[]
    for k,(b,g) in enumerate(bodies):
        c.append(f"static void lift_{n}_{k}(void){{ LIFT_PROLOGUE\n{b}\nLIFT_EPILOGUE }}")
        lnames.append(f"lift_{n}_{k}"); gaps.append('1' if g else '0')
    s.append(f".globl nat_{n}\nnat_{n}:\n NAT_PRE\n {txt}\n"+''.join(f" set{cc} byte ptr [0x{0x10000F00+i:x}]\n" for i,cc in enumerate(cl))+" NAT_POST\n")
    tab.append(f"{{fix_{n}, {{{','.join(lnames)}}}, {{{','.join(gaps)}}}, nat_{n}, {len(cl)}, {absaddr}, \"{txt}\", \"{','.join(cl)}\"}}")
for n in range(len(tab)): c.insert(1,f"extern void nat_{n}(void);") if False else None
c.append('Case CASES[] = {\n'+',\n'.join(tab)+'};\nint NCASES = %d;'%len(tab))
hdr=['extern void nat_%d(void);'%n for n in range(len(tab))]+['void sub_%08X(void){ HS[%d]=1; g_hebp=g_seh_ebp; }'%(0xDEAD00+i,i) for i in range(14)]
open('cases.c','w').write(c[0]+'\n'+'\n'.join(hdr)+'\n'+'\n'.join(c[1:]))
open('cases.S','w').write('#include "nat.h"\n'+'\n'.join(s)+'\n')
