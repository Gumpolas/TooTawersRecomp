/* (1.9.9) The cave troll's throw: the Balin's Tomb troll's own set has no
 * throw, but Fangorn Forest's troll (14FANG2, set 0x0215F674) is the same
 * creature on the same skeleton with its bones in another order (and a few
 * of its own), and its slot 66 is the troll's overhand throw. That motion,
 * its bone tracks put in the cave troll's order, is his ranged attack, in
 * place of the hero's bow drawn on his skeleton.
 * TT_TROLLBOWTHROW: the hero's, retargeted, as before. */
#ifndef TT_HEROES_TROLL_THROW_H
#define TT_HEROES_TROLL_THROW_H
#define TROLL_THROW_SET 0x0215F674u
#define TROLL_THROW_SLOT 66u
/* the cave troll's bone -> Fangorn's (same rest place); -1: none there */
static const signed char s_troll_fang_bone[56] = {
    0, 1, 2, 3, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, -1, 22, 23, 24, -1, 25, 26, 27, 28, 29, 30,
    31, 35, 32, 33, 34, 36, 37, 38, 39, 40, 41, 42, 46, 43, 44, 45, 47, 48, 49, 50, 51, 52, 53, 54, 55, -1 };
static unsigned char *troll_throw_rec(const unsigned char *G, size_t glen, size_t *outlen)
{
    char hp[700]; unsigned char *f, *out; size_t fl; uint32_t off, end, ty, st, rec, rl, bt, nb, i;
    if (getenv("TT_TROLLBOWTHROW") || glen < 0x60 || rd32(G, 0x40 + 8) != 56) return NULL;
    if (!host_path("\\Device\\CdRom0\\worlds\\14FANG2.bfa", hp, sizeof hp) || !(f = read_all(hp, &fl))) return NULL;
    out = NULL;
    if (res_find(f, fl, TROLL_THROW_SET, 150, &off, &end, &ty) && end <= fl && !memcmp(f + off, "aniz", 4)
        && rd32(f + off, 0x40 + 8) == 56 && rd16(f + off, 6) > TROLL_THROW_SLOT) {
        st = off; rec = rd32(f + st, rd32(f + st, 0x10) + 4 * TROLL_THROW_SLOT);
        if (rec && st + rec + 0x40 <= end) {
            rl = rd32(f + st + rec, 0x14); bt = rd32(f + st + rec, 0x20); nb = 56;
            if (rl >= 0x40 && st + rec + rl <= end && bt + 4 * nb <= rl && !memcmp(f + st + rec, "anim", 4) && (out = (unsigned char *)malloc(rl))) {
                memcpy(out, f + st + rec, rl);
                for (i = 0; i < nb; i++)
                    wr32(out, bt + 4 * i, s_troll_fang_bone[i] < 0 ? 0 : rd32(f + st + rec, bt + 4 * (uint32_t)s_troll_fang_bone[i]));
                *outlen = rl;
                tt_dbg("[HEROES]   the troll's throw: Fangorn's troll's %u (%u bytes, %u ms), his bones' order\n", TROLL_THROW_SLOT, rl, rd32(out, 8));
            }
        }
    }
    free(f);
    return out;
}
#endif
