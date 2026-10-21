#!/usr/bin/env python3
"""Keep the guest's integer registers in C locals inside each lifted function.

The lifted code names the x86 registers eax..esp, and recomp_types.h maps
those names to per-thread globals (g_eax..), because a register is how one
lifted function hands values to the next. Inside a function, though, a
register is just a variable -- but a global one, so the C compiler writes it
back to memory on every change: guest memory is accessed through volatile
pointers, and a volatile access is a point past which the compiler will not
hold back a store to a global. Much of the game's code was those stores.

This pass gives each lifted function locals for the registers it uses
(L_eax..), loaded from the globals on entry, and the globals are the truth
again wherever anything else can see them:

  * before every call, indirect call, tail jump and return the locals are
    written to the globals, and after a call they are read back (the
    callee returns values and moves esp through them);
  * any function called that is not known to leave the registers alone is
    treated as a call.

A function this pass cannot follow (setjmp, a call inside a condition or a
declaration) is left as lifted. Run after postproc_x87.py (generate.py
does); idempotent.
"""
import glob
import os
import re
import sys

REGS = ['eax', 'ecx', 'edx', 'ebx', 'esi', 'edi', 'esp']
FN_RE = re.compile(r'^void (sub_\w+)\(void\)\n\{\n(.*?)^\}\n', re.M | re.S)
MARK = '/* registers in locals (postproc_regs.py) */'
IDENT_CALL = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
PURE = set('''
MEM8 MEM16 MEM32 SMEM8 SMEM16 SMEM32 SMEM64 MEMF MEMD PUSH32 POP32 LO8 HI8 LO16
SET_LO8 SET_HI8 SET_LO16 RECOMP_FCMP RECOMP_FCMP_CC RECOMP_PARITY8 ZX8 ZX16
SX8 SX16 XBOX_PTR RECOMP_DF_STEP memcpy memset memmove sqrt sqrtf sin cos tan
log2 exp2 ldexp fabs atan2 fmod floor ceil recomp_fist recomp_ld80 recomp_st80
recomp_frndint recomp_cvtss2si recomp_cvttss2si recomp_fxam ROL8 ROL16 ROL32
ROR8 ROR16 ROR32 RC_ROT RC_ROT8 RC_ROT16 RC_ROT32 xbox_apu_w32 xbox_apu_r32
fp_push fp_pop fp_top fp_st fp_st1 xbox_ReadTimeStampCounter
recomp_eflags_read recomp_eflags_write recomp_cpuid RECOMP_ATOMIC_ADD32
RECOMP_ATOMIC_CAS32 BSWAP32 if while for switch sizeof return
'''.split())
PURE_PREFIX = ('CMP_', 'TEST_', 'XMM_', 'MMX_', 'SAR', 'SHL', 'SHR')
CALLS = ('RECOMP_ABI_CALL', 'RECOMP_ICALL', 'RECOMP_ICALL_SAFE', 'RECOMP_ICALL_SAFE_AT',
         'RECOMP_ITAIL', 'RECOMP_UNIMPL')


class Unsupported(Exception):
    pass


def mask_comments(s):
    """s with comments and string literals blanked (same length)."""
    out = list(s)
    i, n = 0, len(s)
    while i < n:
        if s.startswith('/*', i):
            j = s.find('*/', i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != '\n':
                    out[k] = ' '
            i = j
        elif s.startswith('//', i):
            j = s.find('\n', i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = ' '
            i = j
        elif s[i] == '"':
            j = i + 1
            while j < n and s[j] != '"':
                j += 2 if s[j] == '\\' else 1
            for k in range(i + 1, min(j, n)):
                out[k] = ' '
            i = j + 1
        else:
            i += 1
    return ''.join(out)


def is_pure(name):
    return name in PURE or name.startswith(PURE_PREFIX)


def convert(body):
    m = mask_comments(body)
    if re.search(r'\bsetjmp\w*\b|\brecomp_setjmp_slot\b', m):
        raise Unsupported('setjmp')
    used = [r for r in REGS if re.search(r'\b%s\b' % r, m) or (r == 'esp' and re.search(r'\bg_esp\b', m))]
    if not used:
        return None
    out_code = ' '.join('g_%s = L_%s;' % (r, r) for r in used)
    in_code = ' '.join('L_%s = g_%s;' % (r, r) for r in used)

    # rename the registers (code only)
    edits = []      # (start, end, replacement)
    for mm in re.finditer(r'\b(g_esp|eax|ecx|edx|ebx|esi|edi|esp)\b', m):
        r = mm.group(1)
        edits.append((mm.start(), mm.end(), 'L_esp' if r == 'g_esp' else 'L_' + r))

    # calls: wrap their statements
    for mm in IDENT_CALL.finditer(m):
        name = mm.group(1)
        if is_pure(name):
            continue
        if not (name in CALLS or name.startswith('sub_') or True):
            continue
        # the call must start its statement
        k = mm.start() - 1
        while k >= 0 and m[k] in ' \t':
            k -= 1
        prev = m[k] if k >= 0 else '\n'
        if prev not in ';{}\n:':
            # `if (...) CALL(...);` is fine too
            if prev == ')' and re.search(r'\b(if|else)\b', m[max(0, k - 400):k]):
                pass
            elif prev == 'e' and m[max(0, k - 3):k + 1] == 'else':
                pass
            else:
                raise Unsupported('call inside an expression: ' + name)
        # its end: the matching paren, then ';'
        j, depth = mm.end(), 1
        while depth:
            if j >= len(m):
                raise Unsupported('unbalanced')
            depth += m[j] == '('
            depth -= m[j] == ')'
            j += 1
        while j < len(m) and m[j] in ' \t':
            j += 1
        if j >= len(m) or m[j] != ';':
            raise Unsupported('call not a statement: ' + name)
        edits.append((mm.start(), mm.start(), '{ ' + out_code + ' '))
        edits.append((j + 1, j + 1, ' ' + in_code + ' }'))

    # returns
    for mm in re.finditer(r'\breturn\s*;', m):
        edits.append((mm.start(), mm.start(), '{ ' + out_code + ' '))
        edits.append((mm.end(), mm.end(), ' }'))

    edits.sort(key=lambda e: (e[0], e[1]))
    res, at = [], 0
    for s0, e0, rep in edits:
        if s0 < at:
            raise Unsupported('overlapping edits')
        res.append(body[at:s0])
        res.append(rep)
        at = e0
    res.append(body[at:])
    decl = '    recomp_reg_t %s; %s\n' % (', '.join('L_%s = g_%s' % (r, r) for r in used), MARK)
    return decl + ''.join(res) + '    ' + out_code + '\n'


def process(path, stats):
    text = open(path).read()
    if MARK in text:
        return

    def one(mo):
        name, body = mo.group(1), mo.group(2)
        try:
            nb = convert(body)
        except Unsupported as e:
            stats['left'] += 1
            stats.setdefault('why', {}).setdefault(str(e).split(':')[0], 0)
            stats['why'][str(e).split(':')[0]] += 1
            if ':' in str(e):
                stats.setdefault('names', {}).setdefault(str(e).split(': ')[1], 0)
                stats['names'][str(e).split(': ')[1]] += 1
            return mo.group(0)
        if nb is None:
            return mo.group(0)
        stats['done'] += 1
        return 'void %s(void)\n{\n%s}\n' % (name, nb)

    text2 = FN_RE.sub(one, text)
    if text2 != text:
        open(path, 'w').write(text2)


def main():
    stats = {'done': 0, 'left': 0}
    for f in sorted(glob.glob(os.path.join(sys.argv[1], 'recomp_*.c'))):
        process(f, stats)
    print('registers in locals: %d functions converted, %d left as lifted' % (stats['done'], stats['left']))
    if '-v' in sys.argv:
        print(stats.get('why'), sorted(stats.get('names', {}).items(), key=lambda kv: -kv[1])[:30])


if __name__ == '__main__':
    main()
