/* d3d_native.inc -- (1.9.5) the game's hottest Direct3D library functions,
 * written in C instead of lifted from the Xbox code.
 *
 * Included by twotowers_manual.c (the recompiler's override scan follows the
 * include). The Xbox Direct3D library is the game's own code: it turns each
 * draw into GPU commands written to the pushbuffer, which this port then
 * reads and draws with Direct3D 11. The functions here are the ones a
 * profile of play put on top -- drawing from vertices or indices the game
 * hands over (each one copies those into the pushbuffer), the vertex
 * stream setup before a draw, one shader constant, a dword copy. Lifted,
 * each step went through the imitated x86 stack and registers and every
 * `rep movsd` of a few dwords through an overlap check and a memcpy call;
 * here they are plain loops on host pointers. What they write is the same,
 * byte for byte: TT_D3D_NATIVE=2 checks that on every call (runs the lifted
 * function, then this one from the same state, compares the pushbuffer,
 * the library's data and the registers, and logs any difference;
 * the lifted result stands). TT_D3D_NATIVE=0 uses the lifted functions.
 *
 * The device is the library's global at 0x124930 (0x124180 points at it):
 * +0 the pushbuffer's write pointer, +4 where it must make space, +8 state
 * flags. The library's data lies in 0x122E00..0x1275B0. */

extern void sub_00118300_gen(void);
extern void sub_001184E0_gen(void);
extern void sub_00118440_gen(void);
extern void sub_00118110_gen(void);
extern void sub_00116A40_gen(void);
extern void sub_00120370_gen(void);
extern void sub_0011AA20_gen(void);
void sub_00120510(void);
void sub_0011ABA0(void);
void sub_0011ABB0(void);
void sub_0011A830(void);
void sub_001202F0(void);

#define D3DN_DATA_LO 0x00122E00u
#define D3DN_DATA_HI 0x001275B0u
#define D3DN_HP(va) ((uint32_t *)XBOX_PTR(va))

static int d3dn_mode = -1;
static inline int d3dn(void)
{
    if (d3dn_mode < 0) {
        const char *v = getenv("TT_D3D_NATIVE");
        d3dn_mode = (v && *v) ? atoi(v) : 1;
        if (d3dn_mode) tt_dbg("[D3D] the hottest Direct3D library functions run as native code%s\n",
                              d3dn_mode == 2 ? " (each call checked against the lifted code)" : "");
    }
    return d3dn_mode;
}

/* The library's "make pushbuffer space" (0x0011AA20): counted, so a check
 * knows when a call reached it (that call is not repeated). */
static volatile uint32_t d3dn_makespace_n;
void sub_0011AA20(void)
{
    d3dn_makespace_n++;
    sub_0011AA20_gen();
}

/* (0x0011ABB0, its common case inline) space for n dwords: the write pointer */
static inline uint32_t d3dn_reserve(uint32_t dev, uint32_t n, uint32_t ret)
{
    uint32_t p = MEM32(dev);
    if (p + n * 4u < MEM32(dev + 4) + 0x200u) return p;
    PUSH32(esp, n);
    ecx = dev;
    PUSH32(esp, ret);
    sub_0011ABB0();
    return eax;
}

/* (0x0011ABA0 when the write pointer is past the mark) */
static inline uint32_t d3dn_reserve0(uint32_t dev, uint32_t ret)
{
    uint32_t p = MEM32(dev);
    if (p < MEM32(dev + 4)) return p;
    PUSH32(esp, ret);
    sub_0011ABA0();
    return eax;
}

/* forward dword copy with rep movsd's meaning when the ranges overlap */
static inline void d3dn_copy(uint32_t dst, uint32_t src, uint32_t n)
{
    uint32_t *d = D3DN_HP(dst);
    const uint32_t *s = D3DN_HP(src);
    if (dst + n * 4u <= src || src + n * 4u <= dst) memcpy(d, s, (size_t)n * 4u);
    else { uint32_t i; for (i = 0; i < n; i++) d[i] = s[i]; }
}

/* ---- the check (TT_D3D_NATIVE=2) ---- */
typedef struct { uint32_t eax, ebx, ecx, edx, esi, edi, esp; } d3dn_regs;
static void d3dn_get(d3dn_regs *r) { r->eax = eax; r->ebx = ebx; r->ecx = ecx; r->edx = edx; r->esi = esi; r->edi = edi; r->esp = esp; }
static void d3dn_set(const d3dn_regs *r) { eax = r->eax; ebx = r->ebx; ecx = r->ecx; edx = r->edx; esi = r->esi; edi = r->edi; esp = r->esp; }
static uint8_t d3dn_s0[D3DN_DATA_HI - D3DN_DATA_LO], d3dn_s1[D3DN_DATA_HI - D3DN_DATA_LO];
static uint8_t d3dn_p1[0x40000];
static uint32_t d3dn_checked, d3dn_skipped, d3dn_bad;
static int d3dn_in;   /* inside a check: 1 the lifted pass, 2 the native pass */

static void d3dn_check(const char *name, void (*nat)(void), void (*gen)(void), int cmp_eax)
{
    uint8_t *data = (uint8_t *)XBOX_PTR(D3DN_DATA_LO);
    const uint32_t dev = 0x124930u, sz = D3DN_DATA_HI - D3DN_DATA_LO;
    d3dn_regs r0, r1, r2;
    uint32_t put0, put1, put2, ms;
    uint8_t stk0[64], stk1[64];   /* the arguments: lifted code reuses their slots */
    d3dn_get(&r0);
    memcpy(stk0, XBOX_PTR(r0.esp), 64);
    memcpy(d3dn_s0, data, sz);
    put0 = MEM32(dev);
    ms = d3dn_makespace_n;
    d3dn_in = 1;
    gen();
    d3dn_in = 0;
    d3dn_get(&r1);
    put1 = MEM32(dev);
    if (d3dn_makespace_n != ms || put1 < put0 || put1 - put0 > sizeof d3dn_p1) {
        d3dn_skipped++;   /* the pushbuffer was flushed or wrapped: not repeated */
        return;
    }
    memcpy(d3dn_s1, data, sz);
    memcpy(d3dn_p1, XBOX_PTR(put0), put1 - put0);
    memcpy(stk1, XBOX_PTR(r0.esp), 64);
    memcpy(data, d3dn_s0, sz);
    memcpy(XBOX_PTR(r0.esp), stk0, 64);
    d3dn_set(&r0);
    d3dn_in = 2;
    nat();
    d3dn_in = 0;
    d3dn_get(&r2);
    put2 = MEM32(dev);
    d3dn_checked++;
    {
        int bad = put2 != put1 || r2.esp != r1.esp || r2.ebx != r1.ebx || r2.esi != r1.esi || r2.edi != r1.edi
                  || (cmp_eax && r2.eax != r1.eax) || d3dn_makespace_n != ms;
        uint32_t at = 0, i;
        if (!bad && memcmp(d3dn_p1, XBOX_PTR(put0), put1 - put0)) {
            for (i = 0; i < put1 - put0; i++) if (d3dn_p1[i] != ((uint8_t *)XBOX_PTR(put0))[i]) break;
            at = put0 + i; bad = 2;
        }
        if (!bad && memcmp(d3dn_s1, data, sz)) {
            for (i = 0; i < sz; i++) if (d3dn_s1[i] != data[i]) break;
            at = D3DN_DATA_LO + i; bad = 3;
        }
        if (bad) {
            if (++d3dn_bad <= 40)
                tt_dbg("[D3D] %s differs (%s at %08X): put %08X/%08X esp %08X/%08X eax %08X/%08X\n", name,
                       bad == 2 ? "pushbuffer" : bad == 3 ? "library data" : "registers", at,
                       put1, put2, r1.esp, r2.esp, r1.eax, r2.eax);
#if !defined(_WIN32)
            if (d3dn_bad == 1 && getenv("TT_D3D_DUMP")) {
                extern void *fopen(const char *, const char *); extern unsigned long fwrite(const void *, unsigned long, unsigned long, void *); extern int fclose(void *);
                void *f = fopen("/tmp/claude-0/d3dn_dump.bin", "wb");
                uint32_t hdr[4] = { put0, put1, put2, 0 };
                fwrite(hdr, 4, 4, f); fwrite(d3dn_p1, 1, put1 - put0, f); fwrite(XBOX_PTR(put0), 1, put2 > put0 ? put2 - put0 : 0, f);
                fwrite(&r0, sizeof r0, 1, f); fwrite(d3dn_s0, 1, sz, f);
                fclose(f);
            }
#endif
            memcpy(data, d3dn_s1, sz);   /* the lifted result stands */
            memcpy(XBOX_PTR(put0), d3dn_p1, put1 - put0);
            memcpy(XBOX_PTR(r0.esp), stk1, 64);
            d3dn_set(&r1);
        }
    }
    if ((d3dn_checked & 0xFFFFFu) == 0)
        tt_dbg("[D3D] checked %u calls, %u differ, %u not repeated\n", d3dn_checked, d3dn_bad, d3dn_skipped);
}

#define D3DN_ENTRY(addr, cmp_eax) \
    extern void sub_##addr##_gen(void); \
    static void d3dn_##addr(void); \
    void sub_##addr(void) { \
        int m = d3dn(); \
        if (m == 1 || d3dn_in == 2) d3dn_##addr(); \
        else if (d3dn_in == 1) sub_##addr##_gen(); \
        else if (m == 2) d3dn_check(#addr, d3dn_##addr, sub_##addr##_gen, cmp_eax); \
        else sub_##addr##_gen(); \
    }

/* 0x00118110 (dst, src, n): n dwords copied forward; stdcall */
D3DN_ENTRY(00118110, 1)
static void d3dn_00118110(void)
{
    uint32_t dst = MEM32(esp + 4), src = MEM32(esp + 8), n = MEM32(esp + 12);
    uint32_t a = ((0u - dst) & 0x1Fu) >> 2;
    if ((int32_t)n <= (int32_t)a) { if ((int32_t)n < 0) { sub_00118110_gen(); return; } a = n; }
    d3dn_copy(dst, src, n);
    /* rep movsd leaves ecx 0; edx what the block loop left */
    ecx = 0;
    edx = (n > a) ? n - a : n;
    esp += 16;
}

/* 0x00116A40 (ecx = register, edx = 4 floats): one vertex shader constant */
D3DN_ENTRY(00116A40, 1)
static void d3dn_00116A40(void)
{
    uint32_t reg = ecx, src = edx, p, c;
    for (;;) {
        p = MEM32(0x124930u) + 0x1Cu;
        if (p < MEM32(0x124934u)) break;
        PUSH32(esp, src); PUSH32(esp, reg);
        PUSH32(esp, 0x00116A9Bu); sub_0011ABA0();
        esp += 8;
    }
    MEM32(0x124930u) = p;
    {
        uint32_t *o = D3DN_HP(p - 28u), *k;
        const uint32_t *s = D3DN_HP(src);
        c = (reg << 4) + 0x125588u;
        k = D3DN_HP(c);
        o[0] = 0x41EA4u; o[1] = reg; o[2] = 0x100B80u;
        o[3] = k[0] = s[0]; o[4] = k[1] = s[1]; o[5] = k[2] = s[2]; o[6] = k[3] = s[3];
    }
    eax = p; ecx = c; edx = src;
    esp += 4;
}

/* 0x00120370 (ecx = device; arg: base vertex): the vertex streams' formats
 * and addresses written before a draw when they changed; thiscall */
D3DN_ENTRY(00120370, 0)
static void d3dn_00120370(void)
{
    const uint32_t dev = ecx, arg = MEM32(esp + 4), ret_esp = esp + 8;
    uint32_t fl = MEM32(0x12418Cu), vs, tab, p, i, m;
    MEM32(0x12418Cu) = fl & 0xFFFFFFAFu;
    if (fl & 0x3FFFFF8Fu) {
        ecx = dev;
        PUSH32(esp, 0x00120398u); sub_001202F0();
    }
    if (fl & 0x40000000u) { esp = ret_esp; return; }
    if (!(fl & 0x40u) && MEM32(dev + 0x20) == arg) { esp = ret_esp; return; }
    vs = MEM32(dev + 0x380);
    MEM32(dev + 0x20) = arg;
    tab = (MEM32(vs + 4) & 0x10u) + 0x1D8580u;
    p = d3dn_reserve0(dev, 0x001203DFu);
    if (fl & 0x10u) {
        uint32_t *o = D3DN_HP(p);
        o[0] = 0x401760u;
        for (i = 0; i < 16; i++) {
            uint32_t e = vs + ((uint32_t)MEM8(tab + i) << 4);
            o[1 + i] = (MEM32(0x124628u + MEM32(e + 0x14) * 12u) << 8) + MEM32(e + 0x1C);
        }
        p += 4 + 64;
    }
    for (i = 0, m = 0x41720u; m < 0x41760u; i++, m += 4) {
        uint32_t e = vs + ((uint32_t)MEM8(tab + i) << 4), s, vb, v;
        if (MEM32(e + 0x1C) == 2u) continue;
        s = 0x124628u + MEM32(e + 0x14) * 12u;
        vb = MEM32(s + 8);
        if (!vb) continue;
        v = MEM32(vb + 4) + MEM32(e + 0x18) + MEM32(s + 4);
        if (arg) v += MEM32(s) * arg;
        MEM32(p) = m; MEM32(p + 4) = v;
        p += 8;
    }
    MEM32(dev) = p;
    eax = dev; ecx = p;
    esp = ret_esp;
}

/* 0x00118440 (primitive, first vertex, count): DrawVertices; stdcall */
D3DN_ENTRY(00118440, 1)
static void d3dn_00118440(void)
{
    const uint32_t dev = MEM32(0x124180u);
    const uint32_t prim = MEM32(esp + 4), start = MEM32(esp + 8), ret_esp = esp + 16;
    uint32_t cnt = MEM32(esp + 12), nb, p, s = start;
    uint32_t *o;
    if (cnt == 0) { sub_00118440_gen(); return; }
    PUSH32(esp, 0); ecx = dev; PUSH32(esp, 0x00118452u); sub_00120370();
    nb = ((cnt - 1u) >> 8) + 1u;
    p = d3dn_reserve(dev, nb + 5u, 0x00118468u);
    o = D3DN_HP(p);
    o[0] = 0x417FCu; o[1] = prim; o[2] = (nb << 18) + 0x40001810u;
    o += 3;
    while (cnt > 0x100u) { *o++ = s | 0xFF000000u; s += 0x100u; cnt -= 0x100u; }
    *o++ = ((cnt - 1u) << 24) | s;
    o[0] = 0x417FCu; o[1] = 0;
    p += (uint32_t)((uint8_t *)(o + 2) - (uint8_t *)D3DN_HP(p));
    MEM32(dev) = p;
    eax = p; ecx = s;
    esp = ret_esp;
}

/* 0x00118300 (primitive, count, 16-bit indices, base offset, stride):
 * DrawIndexedVerticesUP -- each indexed vertex copied into the pushbuffer as
 * inline vertex data, in batches; stdcall */
D3DN_ENTRY(00118300, 1)
static void d3dn_00118300(void)
{
    const uint32_t dev = MEM32(0x124180u);
    const uint32_t prim = MEM32(esp + 4), base_off = MEM32(esp + 16), stride = MEM32(esp + 20), ret_esp = esp + 24;
    uint32_t rem = MEM32(esp + 8), idx = MEM32(esp + 12), p, vsz, bmax, npairs, base, fl;
    const uint32_t *pairs;
    if (rem == 0 || MEM32(dev + 0x804) == 0 || MEM32(dev + 0x778) == 0) { sub_00118300_gen(); return; }
    ecx = dev; PUSH32(esp, 0x00118319u); sub_00120510();
    p = d3dn_reserve0(dev, 0x00118325u);
    MEM32(p) = 0x417FCu; MEM32(p + 4) = prim;
    MEM32(dev) = p + 8;
    MEM32(dev + 8) |= 0x800u;
    base = MEM32(dev + 0x77C) + base_off;
    npairs = MEM32(dev + 0x804);
    pairs = D3DN_HP(dev + 0x784);
    vsz = MEM32(dev + 0x778);
    bmax = rem > 0x10u ? 0x7FFu / vsz : 0x10u;
    for (;;) {
        uint32_t n = bmax < rem ? bmax : rem, sz = vsz * n, v, k;
        uint32_t *o;
        const uint16_t *ix;
        rem -= n;
        p = d3dn_reserve(dev, sz + 3u, 0x001183A9u);
        o = D3DN_HP(p);
        *o++ = (sz << 18) + 0x40001818u;
        ix = (const uint16_t *)XBOX_PTR(idx);
        for (v = 0; v < n; v++) {
            uint32_t src = (uint32_t)ix[v] * stride + base;
            for (k = 0; k < npairs; k++) {
                uint32_t c = pairs[2 * k];
                memcpy(o, D3DN_HP(src), (size_t)c * 4u);
                o += c;
                src += c * 4u + pairs[2 * k + 1];
            }
        }
        idx += n * 2u;
        p += (uint32_t)((uint8_t *)o - (uint8_t *)D3DN_HP(p));
        if (!rem) break;
        MEM32(dev) = p;
    }
    MEM32(p) = 0x417FCu; MEM32(p + 4) = 0;
    MEM32(dev) = p + 8;
    if (MEM32(dev + 8) & 0x1000u) {
        ecx = 0;
        PUSH32(esp, 1); PUSH32(esp, 0x00118428u); sub_0011A830();
    }
    fl = MEM32(dev + 8) & 0xFFFFE7FFu;
    MEM32(dev + 8) = fl;
    eax = fl; ecx = 0; edx = idx;
    esp = ret_esp;
}

/* 0x001184E0 (primitive, count, 16-bit indices): DrawIndexedVertices --
 * the indices written into the pushbuffer two to a dword, the long runs
 * 32-byte aligned the way the library lays them out; stdcall */
D3DN_ENTRY(001184E0, 1)
static void d3dn_001184E0(void)
{
    const uint32_t dev = MEM32(0x124180u);
    const uint32_t prim = MEM32(esp + 4), ret_esp = esp + 16;
    uint32_t cnt = MEM32(esp + 8), idx = MEM32(esp + 12), b, a, fl;
    PUSH32(esp, MEM32(dev + 0x1C)); ecx = dev; PUSH32(esp, 0x001184FDu); sub_00120370();
    MEM32(dev + 8) |= 0x800u;
    b = d3dn_reserve(dev, 0x209u, 0x00118515u);
    MEM32(b) = 0x417FCu; MEM32(b + 4) = prim;
    b += 8;
    a = ((0u - b) >> 2) & 7u;
    if (cnt >= a * 2u + 0x3Eu) {
        uint32_t d;
        if (a) {
            a--;
            MEM32(b) = (a << 18) + 0x40001800u;
            d3dn_copy(b + 4, idx, a);
            idx += a * 4u; b += a * 4u + 4u; cnt -= a * 2u;
        }
        {
            uint32_t r = cnt % 0x3FEu;
            d = r < 0x3Eu ? 0x1FFu : ((((r >> 1) + 1u) & 0xFFFFFFF0u) - 1u);
        }
        for (;;) {
            MEM32(b) = (d << 18) + 0x40001800u;
            d3dn_copy(b + 4, idx, d);
            idx += d * 4u; cnt -= d * 2u;
            MEM32(dev) = b + d * 4u + 4u;
            b = d3dn_reserve(dev, 0x204u, 0x001186AEu);
            d = 0x1FFu;
            if (cnt < 0x3FEu) break;
        }
    }
    {
        uint32_t pr = cnt >> 1;
        MEM32(b) = (pr << 18) + 0x40001800u;
        d3dn_copy(b + 4, idx, pr);
        idx += pr * 4u;
        b += pr * 4u + 4u;
        if (cnt & 1u) { MEM32(b) = 0x41808u; MEM32(b + 4) = MEM16(idx); b += 8; }
    }
    MEM32(b) = 0x417FCu; MEM32(b + 4) = 0;
    b += 8;
    MEM32(dev) = b;
    if (MEM32(dev + 8) & 0x1000u) {
        ecx = idx;
        PUSH32(esp, 1); PUSH32(esp, 0x00118733u); sub_0011A830();
    }
    fl = MEM32(dev + 8) & 0xFFFFE7FFu;
    MEM32(dev + 8) = fl;
    eax = fl;
    esp = ret_esp;
}
