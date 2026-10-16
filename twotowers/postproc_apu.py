#!/usr/bin/env python3
"""Route constant-address APU register accesses in the generated C through
direct calls instead of page faults.

The APU's registers (0xFE800000-0xFE87FFFF) are unmapped on the host, so each
guest access traps into the vectored exception handler, which decodes the
x86 instruction and emulates it. DirectSound programs every voice through
these registers (wait for PIO_FREE, write the voice handle, write the method,
...), four or more trapped accesses per voice update, all while holding the
dispatch lock. With many sound sources that is tens of thousands of
exceptions a second and the audio thread falls behind (audible chugging).

Accesses whose address is a constant are rewritten:
    MEM32(C) = expr;   ->  xbox_apu_w32(C, (expr));
    ... MEM32(C) ...   ->  ... xbox_apu_r32(C) ...
Anything with a computed address keeps trapping. Run after every relift
(relift.sh does)."""
import re, sys, glob, os

LO, HI = 0xFE800000, 0xFE880000
CONST = r'(-?\d+|0x[0-9A-Fa-f]+)u?'
pat_any = re.compile(r'MEM32\(' + CONST + r'\)')
pat_w = re.compile(r'^(\s*)MEM32\(' + CONST + r'\) = (.+);(\s*(/\*.*\*/)?\s*)$')

def val(s):
    v = int(s, 0)
    return v & 0xFFFFFFFF

def is_apu(s):
    try:
        return LO <= val(s) < HI
    except ValueError:
        return False

def fix_reads(line):
    return pat_any.sub(lambda m: ('xbox_apu_r32(0x%08Xu)' % val(m.group(1))) if is_apu(m.group(1)) else m.group(0), line)

DECL = 'extern uint32_t xbox_apu_r32(uint32_t va); extern void xbox_apu_w32(uint32_t va, uint32_t v);\n'

def process(path):
    src = open(path).read().split('\n')
    out, n = [], 0
    for line in src:
        m = pat_w.match(line)
        if m and is_apu(m.group(2)):
            rhs = fix_reads(m.group(3))
            out.append('%sxbox_apu_w32(0x%08Xu, (uint32_t)(%s));%s' % (m.group(1), val(m.group(2)), rhs, m.group(4)))
            n += 1
            continue
        nl = fix_reads(line)
        if nl != line:
            n += nl.count('xbox_apu_r32') - line.count('xbox_apu_r32')
        out.append(nl)
    if n:
        text = '\n'.join(out)
        # after the last #include
        idx = text.rfind('#include')
        eol = text.find('\n', idx) + 1
        text = text[:eol] + DECL + text[eol:]
        open(path, 'w').write(text)
    return n

total = 0
for f in sorted(glob.glob(os.path.join(sys.argv[1], 'recomp_*.c'))):
    if 'xbox_apu_r32(uint32_t va);' in open(f).read():
        continue
    k = process(f)
    if k:
        print('%s: %d APU accesses' % (os.path.basename(f), k))
    total += k
print('APU constant accesses rewritten:', total)
