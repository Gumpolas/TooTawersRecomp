#!/usr/bin/env python3
"""macho_stack.py <binary> [bytes]: set a Mach-O executable's main-thread stack
size (LC_MAIN stacksize), as ld64's -stack_size does -- for builds linked with
lld, which does not implement that option. Default 64 MB."""
import struct, sys
path = sys.argv[1]
size = int(sys.argv[2], 0) if len(sys.argv) > 2 else 64 << 20
d = bytearray(open(path, 'rb').read())
magic, cputype, sub, ftype, ncmds, sizeofcmds, flags, res = struct.unpack_from('<IiiIIIII', d, 0)
assert magic == 0xFEEDFACF, 'not a 64-bit Mach-O'
off = 32
for _ in range(ncmds):
    cmd, csize = struct.unpack_from('<II', d, off)
    if cmd == 0x80000028:  # LC_MAIN: entryoff, stacksize
        struct.pack_into('<Q', d, off + 16, size)
        open(path, 'wb').write(d)
        print('%s: main thread stack %d MB' % (path, size >> 20))
        sys.exit(0)
    off += csize
sys.exit('no LC_MAIN')
