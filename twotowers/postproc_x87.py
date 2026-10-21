#!/usr/bin/env python3
"""Keep the x87 floating-point stack in C locals instead of the shared array.

The lifter models the x87 register stack exactly as the hardware does: eight
doubles (g_fp_stack) and a top-of-stack index (g_fp_top), both per thread.
Every float operation then reads and writes that array through a computed
index, so the C compiler can keep none of it in registers -- an `fld; fmul;
fstp` triple costs a dozen memory operations instead of three instructions.

Inside one function the stack depth at every instruction is fixed by the
code (compilers emit x87 code with a static stack layout). This pass works
it out for each lifted function and gives every stack position a local
(fpl0..fpl7), so the compiler sees ordinary double arithmetic. The shared
array is still the truth wherever another function can see it:

  * before every call, tail jump and return, the values held in locals are
    pushed onto the shared stack (a float return value leaves exactly as
    before), and the function continues from there with nothing in locals;
  * a value below what the function itself pushed (one a callee returned,
    or one the caller left) is read from and popped off the shared array;
  * the status word's TOP field (fnstsw) is reported as the hardware would.

Where two paths meet with different depths, the path that arrives is made
to match: values move between locals and the shared array on that edge.
A function this pass cannot follow (an x87 use it does not recognise, more
than eight values, a call in the middle of a float statement) is left as
the lifter wrote it.

Run after every relift (generate.py does). Idempotent: functions already
converted are recognised and skipped.
"""
import glob
import os
import re
import sys

MAXD = 8
FN_RE = re.compile(r'^void (sub_\w+)\(void\)\n\{\n(.*?)^\}\n', re.M | re.S)
TOK_RE = re.compile(
    r'fp_push\(|fp_pop\(\)|fp_top\(\)|fp_st1\(\)|fp_st\((\d+)\)'
    r'|g_fp_stack\[\(g_fp_top \+ (\d+)\) & 7u?\]'
    r'|\(g_fp_top & 7u\)'
    r'|g_fp_top|g_fp_stack')
CALL_RE = re.compile(r'RECOMP_ABI_CALL|RECOMP_ICALL|RECOMP_ITAIL|RECOMP_UNIMPL'
                     r'|\bsub_[0-9A-Fa-f]+\w*\s*\(|\breturn\b|\blongjmp\b|\bsetjmp\b')
LABEL_RE = re.compile(r'^\s*(loc_[0-9A-Fa-f]+): ;')
GOTO_RE = re.compile(r'goto (loc_[0-9A-Fa-f]+);')
MARK = '/* x87 stack in locals (postproc_x87.py) */'


class Unsupported(Exception):
    pass


def slot(i, d):
    if i < d:
        return 'fpl%d' % (d - 1 - i)
    return 'g_fp_stack[(g_fp_top + %du) & 7u]' % (i - d)


def spill(d):
    """Push the d locals onto the shared stack, deepest first."""
    return ''.join('g_fp_top = (g_fp_top + 7u) & 7u; g_fp_stack[g_fp_top] = fpl%d; ' % k
                   for k in range(d))


def fixup(a, b):
    """Code that turns a depth-a state into a depth-b state."""
    if a == b:
        return ''
    if a > b:
        k = a - b                       # the k deepest locals go to the shared stack
        code = spill(k)
        code += ''.join('fpl%d = fpl%d; ' % (j, j + k) for j in range(b))
        return code
    k = b - a                           # k values come up from the shared stack
    code = ''.join('fpl%d = fpl%d; ' % (j + k, j) for j in reversed(range(a)))
    for j in reversed(range(k)):        # first popped is the shallowest of them
        code += 'fpl%d = g_fp_stack[g_fp_top]; g_fp_top = (g_fp_top + 1u) & 7u; ' % j
    return code


def rewrite(s, d):
    """Rewrite the x87 tokens of s in order, starting at depth d."""
    out, i = [], 0
    while True:
        m = TOK_RE.search(s, i)
        if not m:
            out.append(s[i:])
            return ''.join(out), d
        out.append(s[i:m.start()])
        t = m.group(0)
        if t == 'fp_push(':
            j, depth = m.end(), 1
            while depth:
                if j >= len(s):
                    raise Unsupported('unbalanced fp_push')
                c = s[j]
                depth += c == '('
                depth -= c == ')'
                j += 1
            arg, d2 = rewrite(s[m.end():j - 1], d)
            if d2 != d:
                raise Unsupported('stack change inside fp_push')
            if d >= MAXD:
                raise Unsupported('deeper than eight')
            out.append('(fpl%d = (%s))' % (d, arg))
            d += 1
            i = j
            continue
        if t == 'fp_pop()':
            if d > 0:
                out.append('(void)0')
                d -= 1
            else:
                out.append('(g_fp_top = (g_fp_top + 1u) & 7u)')
        elif t == 'fp_top()':
            out.append(slot(0, d))
        elif t == 'fp_st1()':
            out.append(slot(1, d))
        elif m.group(1) is not None:
            out.append(slot(int(m.group(1)), d))
        elif m.group(2) is not None:
            out.append(slot(int(m.group(2)), d))
        elif t == '(g_fp_top & 7u)':
            out.append('((g_fp_top - %du) & 7u)' % d if d else t)
        else:
            raise Unsupported('bare ' + t)
        i = m.end()


def terminates(line):
    st = line.strip()
    if st.startswith('if'):
        return False
    if st.startswith('goto '):
        return True
    return bool(re.search(r'\breturn;', st)) and not st.startswith('{')


def convert(body):
    lines = body.split('\n')
    if not any(('fp_' in l or 'g_fp_' in l) and not l.lstrip().startswith(('#', '/*', '*'))
               for l in lines):
        return None
    din = {}            # label -> depth on entry
    out = []
    d = 0
    live = True         # the previous line can fall through
    cont = False        # inside a multi-line #define
    for line in lines:
        st = line.lstrip()
        if cont or st.startswith('#'):
            cont = line.rstrip().endswith('\\')
            out.append(line)
            continue
        lm = LABEL_RE.match(line)
        if lm:
            lab = lm.group(1)
            if live:
                if lab in din:
                    fx = fixup(d, din[lab])
                    if fx:
                        out.append('    ' + fx + '/* x87 depth %d -> %d */' % (d, din[lab]))
                else:
                    din[lab] = d
            elif lab not in din:
                din[lab] = 0          # reached only by a jump not seen yet
            d = din[lab]
            live = True
            out.append(line)
            continue
        has_fp = bool(TOK_RE.search(line)) and not st.startswith(('/*', '*'))
        has_call = bool(CALL_RE.search(line))
        if has_call and has_fp:
            raise Unsupported('float and call in one statement')
        if has_call and d:
            out.append('    ' + spill(d) + '/* x87 values to the shared stack */')
            d = 0
        if has_fp:
            line, d = rewrite(line, d)
        gotos = GOTO_RE.findall(line)
        if gotos:
            for lab in gotos:
                if lab in din and din[lab] != d:
                    fx = fixup(d, din[lab])
                    old = 'goto %s;' % lab
                    line = line.replace(old, '{ %s%s }' % (fx, old), 1)
                elif lab not in din:
                    din[lab] = d
        if terminates(line):
            live = False
        out.append(line)
    if d and live:
        raise Unsupported('falls off the end with values in locals')
    return '\n'.join(out)


def process(path):
    text = open(path).read()
    if MARK in text:
        return 0, 0
    done = [0, 0]

    def one(m):
        name, body = m.group(1), m.group(2)
        try:
            nb = convert(body)
        except Unsupported:
            done[1] += 1
            return m.group(0)
        if nb is None:
            return m.group(0)
        done[0] += 1
        decl = '    double fpl0, fpl1, fpl2, fpl3, fpl4, fpl5, fpl6, fpl7; %s\n' % MARK
        return 'void %s(void)\n{\n%s%s}\n' % (name, decl, nb)

    text2 = FN_RE.sub(one, text)
    if done[0]:
        open(path, 'w').write(text2)
    return done[0], done[1]


def main():
    tot = [0, 0]
    for f in sorted(glob.glob(os.path.join(sys.argv[1], 'recomp_*.c'))):
        a, b = process(f)
        tot[0] += a
        tot[1] += b
    print('x87 stack in locals: %d functions converted, %d left as lifted' % tuple(tot))


if __name__ == '__main__':
    main()
