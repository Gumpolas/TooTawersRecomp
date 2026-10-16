#!/usr/bin/env python3
"""extract_source.py -- the English of the game's strings, from YOUR copy of
the game, for translating (The Two Towers Recompiled 1.9.2).

    python3 extract_source.py <game folder> > source.tsv

Writes "key <TAB> English" for every string in UI/*.BFF and Worlds/*.bf? (the
menus' tables, OBSS, and the timed subtitles, OBST). The key is the one the
language files use (strings/<code>.tsv): G and the FNV-1a hash of the
English's UTF-16 units. Nothing is written into the game folder; the output
is for your own use (it is the game's text: do not publish it).
"""
import os, struct, sys


def fnv(units):
    h = 2166136261
    for u in units:
        h = ((h ^ (u & 0xFF)) * 16777619) & 0xFFFFFFFF
        h = ((h ^ (u >> 8)) * 16777619) & 0xFFFFFFFF
    return h


def u16(b, o):
    n = []
    while o + 1 < len(b):
        u = struct.unpack_from('<H', b, o)[0]
        if not u:
            break
        n.append(u)
        o += 2
    return n


def strings(path):
    b = open(path, 'rb').read()
    if len(b) < 8:
        return
    to = struct.unpack_from('<I', b, 4)[0]
    if to + 4 > len(b):
        return
    n = struct.unpack_from('<I', b, to)[0]
    for i in range(min(n, 100000)):
        t, o, _k = struct.unpack_from('<III', b, to + 4 + 12 * i)
        if t in (900, 901, 902) and b[o:o + 4] == b'OBSS':
            cnt = struct.unpack_from('<I', b, o + 12)[0]
            for j in range(cnt):
                _id, off = struct.unpack_from('<II', b, o + 16 + 8 * j)
                yield u16(b, o + off)
        elif t in (905, 906, 907) and b[o:o + 4] == b'OBST':
            cnt = struct.unpack_from('<I', b, o + 8)[0]
            for j in range(cnt):
                _h, off, _s, _e = struct.unpack_from('<IIff', b, o + 16 + 16 * j)
                yield u16(b, o + off)


def main():
    root = sys.argv[1]
    seen = set()
    for sub in ('UI', 'Worlds', 'ui', 'worlds'):
        d = os.path.join(root, sub)
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if not f.lower().endswith(('.bff', '.bfa', '.bfg', '.bfi', '.bfl')):
                continue
            for un in strings(os.path.join(d, f)):
                if not un:
                    continue
                k = 'G%08X' % fnv(un)
                if k in seen:
                    continue
                seen.add(k)
                txt = ''.join(chr(u) for u in un).replace('\n', '\\n').replace('\t', ' ')
                print('%s\t%s' % (k, txt))


if __name__ == '__main__':
    main()
