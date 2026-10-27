#!/usr/bin/env python3
"""A small SquashFS 4.0 writer (gzip, no fragments, no xattrs) for the AppImage.

    pysquashfs.py <source dir> <output image>

Regular files, directories and symlinks; every file owned by root (uid/gid 0),
permissions kept, times kept. Enough for an AppImage's AppDir, with nothing
needed beyond Python's standard library (mksquashfs is not always at hand).
"""
import os, stat, struct, sys, zlib

BLOCK_SIZE = 1 << 17          # 128 KiB data blocks
BLOCK_LOG = 17
META = 8192                   # metadata block size
NO_FRAG, NO_XATTR = 0x10, 0x200
T_DIR, T_FILE, T_SYMLINK, T_LDIR, T_LREG = 1, 2, 3, 8, 9


def compress(data):
    c = zlib.compress(data, 9)
    return (c, True) if len(c) < len(data) else (data, False)


class Meta:
    """A metadata stream (inode or directory table): 8 KiB blocks, each written
    with a 2-byte length header (bit 15 set: stored uncompressed)."""
    def __init__(self):
        self.out = bytearray()   # finished blocks, as they go in the image
        self.cur = bytearray()   # the block being filled

    def pos(self):
        return len(self.out), len(self.cur)    # (block start in the table, offset in the block)

    def add(self, b):
        self.cur += b
        while len(self.cur) >= META:
            self._flush(self.cur[:META]); del self.cur[:META]

    def _flush(self, blk):
        c, z = compress(bytes(blk))
        self.out += struct.pack('<H', len(c) | (0 if z else 0x8000)) + c

    def finish(self):
        if self.cur:
            self._flush(self.cur); self.cur = bytearray()
        return bytes(self.out)


class Out:
    def __init__(self, path):
        self.f = open(path, 'wb'); self.f.write(b'\0' * 96); self.n = 96
    def __len__(self):
        return self.n
    def extend(self, b):
        self.f.write(b); self.n += len(b)


class Node:
    def __init__(self, path, st, children=None):
        self.path, self.st, self.children = path, st, children
        self.ino = 0
        self.ref = None        # (block, offset) of its inode


def scan(path):
    st = os.lstat(path)
    if stat.S_ISDIR(st.st_mode):
        names = sorted(os.listdir(path), key=lambda n: n.encode())
        kids = []
        for n in names:
            s = os.lstat(os.path.join(path, n))
            if stat.S_ISDIR(s.st_mode) or stat.S_ISREG(s.st_mode) or stat.S_ISLNK(s.st_mode):
                kids.append((n, scan(os.path.join(path, n))))
        return Node(path, st, kids)
    return Node(path, st)


def number(node, counter):
    # children first, the directory after them: the root gets the last number
    if node.children is not None:
        for _, k in node.children:
            number(k, counter)
    counter[0] += 1
    node.ino = counter[0]


def basic_type(node):
    m = node.st.st_mode
    return T_DIR if stat.S_ISDIR(m) else T_SYMLINK if stat.S_ISLNK(m) else T_FILE


def build(src, dst):
    root = scan(src)
    counter = [0]
    number(root, counter)
    ninodes = counter[0]

    img = Out(dst)                          # (the superblock, written last)
    inodes, dirs = Meta(), Meta()

    def header(node, itype):
        st = node.st
        return struct.pack('<HHHHII', itype, st.st_mode & 0xFFF, 0, 0, int(st.st_mtime) & 0xFFFFFFFF, node.ino)

    def write(node, parent_ino):
        m = node.st.st_mode
        if stat.S_ISREG(m):
            start, sizes, total = len(img), [], 0
            with open(node.path, 'rb') as f:
                while True:
                    blk = f.read(BLOCK_SIZE)
                    if not blk:
                        break
                    total += len(blk)
                    c, z = compress(blk)
                    img.extend(c); sizes.append(len(c) | (0 if z else 1 << 24))
            data = range(total)   # (its length)
            node.ref = inodes.pos()
            if len(data) < (1 << 32) and start < (1 << 32):
                inodes.add(header(node, T_FILE) + struct.pack('<IIII', start, 0xFFFFFFFF, 0, len(data))
                           + b''.join(struct.pack('<I', s) for s in sizes))
            else:
                inodes.add(header(node, T_LREG) + struct.pack('<QQQIIII', start, len(data), 0, 1, 0xFFFFFFFF, 0, 0xFFFFFFFF)
                           + b''.join(struct.pack('<I', s) for s in sizes))
        elif stat.S_ISLNK(m):
            t = os.readlink(node.path).encode()
            node.ref = inodes.pos()
            inodes.add(header(node, T_SYMLINK) + struct.pack('<II', 1, len(t)) + t)
        else:
            for _, k in node.children:
                write(k, node.ino)
            # its listing: runs of entries under a header (same inode block, at most
            # 256, inode numbers within 16 bits of the header's)
            listing = bytearray()
            run = []
            def flush_run():
                if run:
                    blk, base = run[0][1].ref[0], run[0][1].ino
                    listing.extend(struct.pack('<III', len(run) - 1, blk, base))
                    for n, k in run:
                        nb = n.encode()
                        listing.extend(struct.pack('<HhHH', k.ref[1], k.ino - base, basic_type(k), len(nb) - 1) + nb)
                    run.clear()
            size = 0
            for n, k in node.children:
                if run and (len(run) == 256 or k.ref[0] != run[0][1].ref[0] or not -32768 <= k.ino - run[0][1].ino <= 32767
                            or size + 8 + len(n.encode()) > META):
                    flush_run(); size = 0
                run.append((n, k)); size += 8 + len(n.encode())
            flush_run()
            dblk, doff = dirs.pos()
            dirs.add(bytes(listing))
            nsub = sum(1 for _, k in node.children if k.children is not None)
            fsize = len(listing) + 3
            node.ref = inodes.pos()
            if fsize < 0xFFFF and dblk < (1 << 32):
                inodes.add(header(node, T_DIR) + struct.pack('<IIHHI', dblk, nsub + 2, fsize, doff, parent_ino))
            else:
                inodes.add(header(node, T_LDIR) + struct.pack('<IIIIHHI', nsub + 2, fsize, dblk, parent_ino, 0, doff, 0xFFFFFFFF))

    write(root, ninodes + 1)
    root_ref = (root.ref[0] << 16) | root.ref[1]

    inode_start = len(img); img.extend(inodes.finish())
    dir_start = len(img); img.extend(dirs.finish())
    # the id table: one id (0), in one metadata block, then the block's address
    ids = Meta(); ids.add(struct.pack('<I', 0)); idblk = len(img); img.extend(ids.finish())
    id_start = len(img); img.extend(struct.pack('<Q', idblk))
    used = len(img)
    sb = struct.pack('<IIIIIHHHHHHQQQQQQQQ', 0x73717368, ninodes, int(root.st.st_mtime) & 0xFFFFFFFF, BLOCK_SIZE, 0,
                     1, BLOCK_LOG, NO_FRAG | NO_XATTR, 1, 4, 0, root_ref, used, id_start,
                     0xFFFFFFFFFFFFFFFF, inode_start, dir_start, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF)
    img.extend(b'\0' * (-len(img) % 4096))
    img.f.seek(0); img.f.write(sb); img.f.close()
    return ninodes, img.n


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    n, size = build(sys.argv[1], sys.argv[2])
    print('%d inodes, %d bytes' % (n, size))
