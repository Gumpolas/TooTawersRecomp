/*
 * The Two Towers: hand-written guest functions (xboxrecomp manual overrides).
 *
 * Compiled like generated code so it sees the same register model and memory
 * macros. recomp_lookup_manual() in recomp_manual.c consults
 * twotowers_lookup_manual() first.
 */
#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_funcs.h"
#include <string.h>
#include <stdlib.h>
#if !defined(_WIN32)
/* The few host calls below are declared by hand (windows.h would clash with
 * the generated headers); on POSIX they come from win32_compat.c. */
#include <strings.h>
#define __declspec(x)
#define __stdcall
#define _stricmp strcasecmp
#endif
void tt_dbg(const char *fmt, ...);
int tt_dbg_on(void);
static volatile long long tt_hold2_until;
static volatile uint32_t tt_cb_last[8], tt_cb_n;

/* MSVC CRT memcpy/memmove, backward-copy tail cases (jump table 0x001011C8,
 * indexed by the remaining dword count). Each case copies one dword and falls
 * into the next; 0x001011FC onward are lifted normally. The disassembler's
 * sweep decodes the table itself as code and comes out of phase until
 * 0x001011FC, so it (correctly, by its rules) refuses these two as seeds that
 * land mid-instruction. Without them every overlapping backward memmove of
 * 7 or 8 trailing dwords returned half-done, with esi/edi/ebp and the frame
 * left on the guest stack. */
static void tt_memcpy_case_001011F4(void)
{
    eax = MEM32(esi + ecx * 4 + 0x18);
    MEM32(edi + ecx * 4 + 0x18) = eax;
    sub_001011FC();
}

static void tt_memcpy_case_001011EC(void)
{
    eax = MEM32(esi + ecx * 4 + 0x1C);
    MEM32(edi + ecx * 4 + 0x1C) = eax;
    tt_memcpy_case_001011F4();
}

/* The same memcpy's byte tails (tables 0x0010109C forward, 0x00101238
 * backward). Each copies 0-3 bytes and runs memcpy's epilogue: return dst,
 * pop esi/edi, leave, ret. Reached through RECOMP_ITAIL when the lifter
 * could not fold the table into the jumping function. */
/* The frame is memcpy's, handed over in g_seh_ebp the way lifted tail
 * targets inherit it; the caller keeps its own ebp, as lifted code does. */
#define TT_MEMCPY_EPILOGUE() do { \
        uint32_t _fp = g_seh_ebp, _old; \
        eax = MEM32(_fp + 8); \
        POP32(esp, esi); POP32(esp, edi); \
        esp = _fp; POP32(esp, _old); (void)_old; \
        esp += 4; } while (0)

static void tt_memcpy_fwd0(void) { TT_MEMCPY_EPILOGUE(); }
static void tt_memcpy_fwd1(void) { MEM8(edi) = MEM8(esi); TT_MEMCPY_EPILOGUE(); }
static void tt_memcpy_fwd2(void) { MEM8(edi) = MEM8(esi); MEM8(edi + 1) = MEM8(esi + 1); TT_MEMCPY_EPILOGUE(); }
static void tt_memcpy_fwd3(void) { MEM8(edi) = MEM8(esi); MEM8(edi + 1) = MEM8(esi + 1);
                                   MEM8(edi + 2) = MEM8(esi + 2); TT_MEMCPY_EPILOGUE(); }
static void tt_memcpy_bwd0(void) { TT_MEMCPY_EPILOGUE(); }
static void tt_memcpy_bwd1(void) { MEM8(edi + 3) = MEM8(esi + 3); TT_MEMCPY_EPILOGUE(); }
static void tt_memcpy_bwd2(void) { MEM8(edi + 3) = MEM8(esi + 3); MEM8(edi + 2) = MEM8(esi + 2); TT_MEMCPY_EPILOGUE(); }
static void tt_memcpy_bwd3(void) { MEM8(edi + 3) = MEM8(esi + 3); MEM8(edi + 2) = MEM8(esi + 2);
                                   MEM8(edi + 1) = MEM8(esi + 1); TT_MEMCPY_EPILOGUE(); }

/* The CRT memmove/memcpy itself (0x00100F50, cdecl: dst, src, n -> dst).
 *
 * Its body is a nest of computed jumps into the middle of itself -- alignment
 * heads, dword tails and byte tails, forwards and backwards -- and every one of
 * those targets the disassembler had not seen was a hole: a copy that returned
 * half done (0x00101198, reached by the Bink player's overlapping moves, is
 * the one that ended the opening movie). The function's contract is simple and
 * total, so it is implemented here instead of lifted (recomp --exclude-manual).
 * Copies touching the MMIO apertures go element by element through MEM8 so
 * the trapped-register paths still see each access. */
void sub_00100F50(void)
{
    uint32_t dst = MEM32(esp + 4), src = MEM32(esp + 8), n = MEM32(esp + 12);

    if (n) {
        if (dst >= 0xFE000000u || src >= 0xFE000000u
                || dst + n >= 0xFE000000u || src + n >= 0xFE000000u) {
            uint32_t i;
            if (dst > src && dst < src + n)
                for (i = n; i-- > 0; ) MEM8(dst + i) = MEM8(src + i);
            else
                for (i = 0; i < n; i++) MEM8(dst + i) = MEM8(src + i);
        } else {
            memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), n);
        }
    }
    eax = dst;
    esp += 4;   /* ret (cdecl: the caller pops the arguments) */
}

/* The game's frame profiler: a category stack (0x006B181C/0x006B1820) and
 * per-category rdtsc buckets (0x006B18B0), pushed and popped by the main and
 * loader threads around their work. It is not thread-safe -- on the console
 * the two threads rarely interleave inside it -- and here they do, so the
 * stack index runs off its array and the next bucket lookup dereferences
 * garbage (crash in 0x000E60B0). It only feeds a debug timing display, so
 * the three entry points do nothing. */
/* The game's developer switches (0x000E3AF0: look a name up in a table at
 * 0x4B91F0 that only the development kit's host filled; empty on retail).
 * TT_TWEAKS="SKIPMAINMENU=1;HERO=aragorn;LEVEL=01WEAT1" answers them here:
 * an all-digit value is returned as a pointer to an int, anything else as a
 * pointer to the string, as the game reads each. Values live in the unused
 * table's own memory. Without TT_TWEAKS every lookup misses, as on retail. */
extern void sub_000E3AF0_gen(void);
void sub_000E3AF0(void)
{
    static int n = -1;
    static char names[16][48];
    static uint32_t vals[16];
    const uint32_t key = MEM32(esp + 4);
    int i;
    if (n < 0) {
        const char *e = getenv("TT_TWEAKS");
        uint32_t store = 0x4B91F0u;
        n = 0;
        while (e && *e && n < 16) {
            const char *eq = strchr(e, '='), *end = strchr(e, ';');
            size_t kl, vl;
            char v[48];
            if (!end) end = e + strlen(e);
            if (!eq || eq > end) { e = *end ? end + 1 : end; continue; }
            kl = (size_t)(eq - e); vl = (size_t)(end - eq - 1);
            if (kl > 47) kl = 47;
            if (vl > 47) vl = 47;
            memcpy(names[n], e, kl); names[n][kl] = 0;
            memcpy(v, eq + 1, vl); v[vl] = 0;
            vals[n] = store;
            if (v[0] && strspn(v, "0123456789") == strlen(v)) {
                MEM32(store) = (uint32_t)atoi(v);
                store += 4;
            } else {
                size_t k;
                for (k = 0; k <= vl; k++) MEM8(store + k) = (uint8_t)v[k];
                store += (uint32_t)((vl + 4) & ~3u);
            }
            n++;
            e = *end ? end + 1 : end;
        }
    }
    eax = 0;
    if (n > 0 && key) {
        char k[64];
        for (i = 0; i < 63 && MEM8(key + i); i++) k[i] = (char)MEM8(key + i);
        k[i] = 0;
        for (i = 0; i < n; i++)
            if (!_stricmp(names[i], k)) { eax = vals[i]; break; }
    }
    esp += 4;   /* ret (cdecl) */
}

/* 0x000CF450 draws the HUD: every element of every enabled table through
 * 0x000C8940 (thiscall, ecx = the 0xDC-byte element). The E3 HUD mod hides
 * and moves a few retail elements around it and draws its own text elements
 * -- in the game's own fonts -- right after. */
extern void sub_000CF450_gen(void);
void sub_000CF450(void)
{
    extern int tt_e3hud_predraw(void);
    extern void tt_e3hud_postdraw(void);
    int on = tt_e3hud_predraw();
    { extern void tt_framelog(void); tt_framelog(); }   /* (testing: TT_HEADLOG) */
    { extern volatile unsigned long long tt_hud_drawn_ms; extern __declspec(dllimport) unsigned long long __stdcall GetTickCount64(void); tt_hud_drawn_ms = GetTickCount64(); }
    sub_000CF450_gen();
    if (on) tt_e3hud_postdraw();
}
/* 0x0008FB60 (thiscall on an actor): "does the player need a power-up" --
 * the level scripts drop health power-ups when it says yes. TT_FORCE_PICKUPS
 * (testing) makes it always yes. */
extern void sub_0008FB60_gen(void);
void sub_0008FB60(void)
{
    static int force = -1;
    if (force < 0) force = getenv("TT_FORCE_PICKUPS") != NULL;
    sub_0008FB60_gen();
    if (force) eax = 1;
}

/* 0x000CCA50 draws the health sword's bars; the code before it sets the
 * fill colour (0x4A0A98) as an immediate every frame. The E3 HUD mod
 * changes that colour here, just before the draw. */
extern void sub_000CCA50_gen(void);
void sub_000CCA50(void)
{
    extern void tt_e3hud_prehud(void);
    tt_e3hud_prehud();
    sub_000CCA50_gen();
}

/* 0x000CC9C0 is the level scripts' @IncVillagerRescue (Westfold, 16ROHAN2):
 * one step off the villager bar -- its count at 0x4A55BC starts at 8 and
 * the bar is drawn count/8 of its length. The ECTS HUD mod draws the
 * demo's villager icons from that count (e3hud.c); this only logs it. */
extern void sub_000CC9C0_gen(void);
extern void tt_dbg(const char *fmt, ...);
void sub_000CC9C0(void)
{
    uint32_t before = MEM32(0x4A55BCu);
    sub_000CC9C0_gen();
    tt_dbg("[VILLAGERS] lost one: %u -> %u left\n", before, MEM32(0x4A55BCu));
}

/* 0x00134940 is XACT's lock: at PASSIVE_LEVEL it enters XACT's critical
 * section (0x00139F58) and returns 1; at DISPATCH_LEVEL -- XACT's work run
 * from a DPC -- it returns 0 and takes nothing, since on the console a DPC
 * runs with every thread stopped. Here the DPCs run on the timer thread while
 * the game thread keeps going, so the two met inside XACT: the game stopping
 * a sound bank's cues (0x00135821) walked a cue list the DPC was relinking,
 * read a null link and faulted (Helm's Deep, unlocked frame rate, where cues
 * start and stop far more often per second). At DISPATCH_LEVEL the section
 * is now taken as well, the dispatch lock let go while waiting so the holder
 * can finish; after 50 ms (a holder waiting on the DPC itself) it goes ahead
 * without it, as on the console. Returns 1 when taken: the callers leave the
 * section when it says so. */
extern void sub_00134940_gen(void);
void sub_00134940(void)
{
    extern int xbox_DpcLockGuestCs(uint32_t cs_va, int max_ms);
    if (MEM8(XBOX_FS_BASE + 0x24) == 0) { sub_00134940_gen(); return; }
    eax = (uint32_t)xbox_DpcLockGuestCs(0x00139F58u, 50);
    esp += 4;   /* ret */
}

/* Test hook: TT_DEBUG_GOTO=<screen 1-11> switches the game's front end to
 * that screen once a pad script raises FLAG1 (10 = end of level, 11 =
 * upgrades). Runs from the main loop (0x000D1850) right after its own screen
 * switch, which calls 0x000E6080 with return address 0x000D19A2. */
static void tt_main_tick(void)
{
    static int target = -1;
    static unsigned done;
    extern int xbox_PadScriptFlag(int n);
    extern void tt_dbg(const char *fmt, ...);
    static const uint32_t offs[12] = { 0, 8, 0x40C, 0x3468, 0x3864, 0x3A78, 0x3C80, 0x3E90,
                                       0x8C80, 0x9078, 0x9474, 0x9A78 };
    uint32_t C = 0x4AD050u, cur, scr, old, se;
    uint32_t sa = eax, sb = ebx, sc = ecx, sd = edx, ss = esi, sdi = edi;
    if (target < 0) { const char *e = getenv("TT_DEBUG_GOTO"); target = e ? atoi(e) : 0; }
    {
        /* TT_POKE8="1B0BFD:1,..." (testing): hold guest bytes at a value every
         * frame, e.g. the debug menu's switches (0x1B0BFD "DEBUG/Drop Pickups",
         * 0x43B112 "DEBUG/Display EXP and Runes"). */
        static const char *pk = (const char *)1;
        if (pk == (const char *)1) pk = getenv("TT_POKE8");
        if (pk) {
            const char *q = pk;
            while (*q) {
                char *e1, *e2;
                unsigned long a = strtoul(q, &e1, 16), v;
                if (*e1 != ':') break;
                v = strtoul(e1 + 1, &e2, 0);
                if (a >= 0x10000u && a < 0x4000000u) MEM8((uint32_t)a) = (uint8_t)v;
                q = *e2 == ',' ? e2 + 1 : e2;
                if (e2 == e1 + 1) break;
            }
        }
    }
    {
        /* TT_MEMDUMP=prefix (testing): every TT_MEMDUMP_EVERY ms, the game's
         * data and heap (0x18B000-0x920000) to prefixNNN.bin plus the picture
         * shown at that moment to prefixNNN.bmp, for finding variables. */
        static const char *md = (const char *)1;
        static unsigned long long next;
        static int n, every;
        extern __declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);
        extern void xbox_PresentDumpRequest(const char *path);
        if (md == (const char *)1) {
            md = getenv("TT_MEMDUMP");
            every = getenv("TT_MEMDUMP_EVERY") ? atoi(getenv("TT_MEMDUMP_EVERY")) : 2000;
        }
        if (md && n < 400 && GetTickCount64() >= next) {
            /* stdio clashes with the generated names: declared by hand */
            extern void *fopen(const char *, const char *);
            extern size_t fwrite(const void *, size_t, size_t, void *);
            extern int fclose(void *);
            char nm[300];
            void *f;
            size_t pl = strlen(md) < 280 ? strlen(md) : 280;
            unsigned long long now = GetTickCount64();
            next = now + (unsigned)every;
            memcpy(nm, md, pl);
            nm[pl] = (char)('0' + n / 100 % 10); nm[pl + 1] = (char)('0' + n / 10 % 10); nm[pl + 2] = (char)('0' + n % 10);
            memcpy(nm + pl + 3, ".bin", 5);
            if ((f = fopen(nm, "wb"))) {
                if (getenv("TT_MEMDUMP_ALL")) {   /* all of RAM from 0x10000, then the contiguous window */
                    fwrite((const void *)XBOX_PTR(0x10000u), 1, 0x3FF0000u, f);
                    fwrite((const void *)XBOX_PTR(0x80000000u), 1, 0x4000000u, f);
                }
                else fwrite((const void *)XBOX_PTR(0x18B000u), 1, 0x920000u - 0x18B000u, f);
                fclose(f);
            }
            memcpy(nm + pl + 3, ".bmp", 5);
            xbox_PresentDumpRequest(nm);
            n++;
        }
    }
    {
        /* With the title screen skipped (TT_TWEAKS SKIPMAINMENU) nobody
         * picked the controller port; player 1 is port 0. */
        static int tw = -1;
        if (tw < 0) tw = getenv("TT_TWEAKS") != NULL;
        if (tw && MEM32(0x1CB8B4u) > 3u) {
            uint32_t p;
            for (p = 0; p < 4; p++)
                if (MEM32(0x6B289Cu + p * 0x14Cu)) {   /* a pad the game has opened */
                    tt_dbg("  [DEBUG] primary controller -> port %u\n", p);
                    MEM32(0x1CB8B4u) = p;
                    break;
                }
        }
    }
    if (target < 1 || target > 11) return;
    /* each pad-script flag FLAG1..FLAG31 triggers one jump */
    {
        int k, hit = 0;
        for (k = 1; k < 32; k++)
            if (!(done & (1u << k)) && xbox_PadScriptFlag(k)) { done |= 1u << k; hit = 1; break; }
        if (!hit) return;
    }
    cur = MEM32(C + 4);
    scr = C + offs[target];
    tt_dbg("  [DEBUG] screen %u (0x%08X, vtbl 0x%08X) -> screen %d (0x%08X)\n",
           MEM32(C), cur, cur ? MEM32(cur) : 0, target, scr);
    if (cur) {
        se = esp; ecx = cur;
        PUSH32(esp, 0x000D19A2u);
        RECOMP_ICALL_SAFE(MEM32(MEM32(cur) + 0xC), se);
    }
    old = MEM32(C);
    MEM32(C) = (uint32_t)target;
    MEM32(C + 4) = scr;
    se = esp;
    PUSH32(esp, old);
    ecx = scr;
    PUSH32(esp, 0x000D19A2u);
    RECOMP_ICALL_SAFE(MEM32(MEM32(scr) + 8), se + 0);
    eax = sa; ebx = sb; ecx = sc; edx = sd; esi = ss; edi = sdi;
}
/* push category */
void sub_000E6080(void)
{
    if (MEM32(esp) == 0x000D19A2u) tt_main_tick();
    esp += 4;
}
void sub_000E60B0(void) { esp += 4; }               /* switch (cdecl) */
void sub_000E6AC0(void) { esp += 4; }               /* pop category   */

/* D3D's KickOff (0x0011A740, thiscall: ecx = device). The one place PUT is
 * written. Reimplemented rather than wrapped for two reasons:
 *  - its first act is a PFB write-back-cache flush that sets bit 16 of
 *    0xFD100410 and SPINS until the GPU clears it. Nothing here does that but
 *    a polling thread, so every kick stalled for up to a scheduler tick --
 *    several times a frame, the main reason the movies crawled;
 *  - the GPU must consume the new commands at once (xbox_Nv2aKickNow), as
 *    hardware would, or D3D re-patches a recorded pushbuffer's return jump
 *    before the GPU has run it.
 * Everything else is the original, instruction for instruction. */
/* 0x0011A710: the same PFB flush on its own (sfence; set bit 16 of
 * 0xFD100410; spin until clear). Writes reach guest memory immediately
 * here, so there is nothing to flush and nothing to wait for. */
void sub_0011A710(void) { esp += 4; }

extern void xbox_Nv2aKickNow(void);
static uint32_t tt_w_last[4] = {0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu,0xFFFFFFFFu};
static void tt_watch(const char *where)
{
    static const uint32_t addrs[4] = {0x001E7850u, 0x001EE220u, 0x001E7820u, 0x001E7BA0u};
    int i;
    extern int tt_dbg_on(void); extern void tt_dbg(const char *fmt, ...);
    if (!tt_dbg_on()) return;
    for (i = 0; i < 4; i++) {
        uint32_t v = MEM32(addrs[i]);
        if (v != tt_w_last[i]) {
            tt_dbg("[WATCH] %s [%08X] %08X -> %08X  esp=%08X stk %08X %08X %08X %08X %08X %08X\n", where, addrs[i], tt_w_last[i], v,
                   esp, MEM32(esp), MEM32(esp + 4), MEM32(esp + 8), MEM32(esp + 12), MEM32(esp + 16), MEM32(esp + 20));
            tt_w_last[i] = v;
        }
    }
}
void sub_0011A740(void)
{
    tt_watch("kick");
    uint32_t dev = ecx;
    uint32_t flags = MEM32(dev + 8);
    uint32_t ptr = (flags & 4u) ? MEM32(dev + 0x35C) : MEM32(dev);

    MEM32(dev + 0x2C) = ptr;
    {
        static int st = -1;
        if (st < 0) st = getenv("RECOMP_SEM_TRACE") != NULL;
        if (st) tt_dbg("  [KICK] ptr %08X flags %08X issued %u done %u\n", ptr, flags, MEM32(dev + 0x30), MEM32(MEM32(dev + 0x34)));
    }
    if (!(flags & 0x2000u)) {
        uint32_t regs = MEM32(dev + 0x23BC);
        MEM32(regs + 0x40) = ptr & 0x0FFFFFFFu;            /* PUT */
        xbox_Nv2aKickNow();
        if (MEM32(0x00124184u)) {
            do {                                        /* wait for idle */
                PUSH32(esp, 0x0011A7B5u);
                sub_00117D10();
            } while (eax);
            MEM32(dev + 8) |= 0x2000u;
        }
    } else {
        uint32_t fake = 0x00124178u - 0x40u, p2;
        MEM32(dev + 0x23BC) = fake;
        p2 = MEM32(dev) & 0x0FFFFFFFu;
        MEM32(fake + 0x40) = p2;
        MEM32(fake + 0x44) = p2;
        MEM32(MEM32(dev + 0x34)) = MEM32(dev + 0x30) - 2u;
        MEM32(dev + 0x257C) = MEM32(dev + 0x2C10);
        MEM32(dev + 0x2BD8) = MEM32(dev + 0x40);
    }
    esp += 4;   /* ret */
}

__declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);
__declspec(dllimport) int __stdcall SwitchToThread(void);
/* D3D BlockOnTime(time, flags) at 0x0011A8E0 (stdcall, 2 args).
 *
 * While the title records a pushbuffer (device flags bit 2), a kickoff's
 * semaphore release lands in the RECORDING rather than the stream the GPU
 * runs, so a wait for that time can only end once the recording is played
 * -- after the wait. The Two Towers frees the movie texture while recording
 * the first level's pushbuffers and deadlocked here. The GPU in this runtime
 * has already run everything submitted, so such a wait returns at once. */
extern void sub_0011A8E0_gen(void);
static volatile uint32_t tt_bot_target, tt_bot_issued, tt_bot_done, tt_bot_flags, tt_bot_caller, tt_bot_in;
void sub_0011A8E0(void)
{
    uint32_t dev = MEM32(0x00124180u);
    if (dev) {
        uint32_t target = MEM32(esp + 4);
        tt_bot_target = target; tt_bot_issued = MEM32(dev + 0x30); tt_bot_done = MEM32(MEM32(dev + 0x34));
        tt_bot_flags = MEM32(esp + 8); tt_bot_caller = MEM32(esp); tt_bot_in = 1;
        uint32_t issued = MEM32(dev + 0x30);
        uint32_t done = MEM32(MEM32(dev + 0x34));
        (void)issued;
        if ((int32_t)(target - done) > 0 && (MEM32(dev + 8) & 4u)) {
            tt_bot_in = 0;
            esp += 12;   /* ret 8 */
            return;
        }
        /* A time issued while recording is only reached when that recording
         * is played. If the title waits for it afterwards and the GPU has
         * run everything it was given (GET == PUT), nothing will ever write
         * it: the D3D spin below never ends (seen as a hang on the loading
         * screen, done 109 / issued 123 with the GPU idle). An idle GPU has
         * finished with every resource, so report every issued time done. */
        if ((int32_t)(target - done) > 0 && target != issued) {
            unsigned long long t0 = GetTickCount64(), idle_since = 0;
            for (;;) {
                uint32_t d = MEM32(MEM32(dev + 0x34));
                unsigned long long now;
                if ((int32_t)(target - d) <= 0) break;
                now = GetTickCount64();
                if (MEM32(0xFD800040u) == MEM32(0xFD800044u)) {
                    if (!idle_since) idle_since = now;
                    else if (now - idle_since > 20) {
                        static int said;
                        if (said++ < 20)
                            tt_dbg("  [BLOCKONTIME] GPU idle, time %u never released (done %u, issued %u): completing\n",
                                   target, d, MEM32(dev + 0x30));
                        MEM32(MEM32(dev + 0x34)) = MEM32(dev + 0x30);
                        break;
                    }
                } else {
                    idle_since = 0;
                }
                if (now - t0 > 10000) break;   /* leave it to D3D */
                SwitchToThread();
            }
        }
    }
    sub_0011A8E0_gen();
    tt_bot_in = 0;
}

static volatile unsigned tt_vbh_calls, tt_vbh_isr;
void tt_dump_dev(void)
{
    uint32_t dev = MEM32(0x00124180u);
    if (!dev) return;
    tt_dbg("  [DEVSTATE] PUT %08X GET %08X issued %u done %u flags %08X put %08X\n", MEM32(0xFD800040u), MEM32(0xFD800044u), MEM32(dev + 0x30),
           MEM32(MEM32(dev + 0x34)), MEM32(dev + 8), MEM32(dev));
    {
        extern volatile unsigned g_vblank_claimed, g_vblank_declined, g_apu_irq_claimed, g_apu_irq_declined;
        tt_dbg("  [DEVSTATE] vblank handler ran %u times (%u from the interrupt)\n", tt_vbh_calls, tt_vbh_isr);
        tt_dbg("  [DEVSTATE] vblank claimed %u declined %u; PMC %08X PCRTC %08X; sound irq claimed %u declined %u\n",
               g_vblank_claimed, g_vblank_declined, MEM32(0xFD000100u), MEM32(0xFD600100u),
               g_apu_irq_claimed, g_apu_irq_declined);
    }
    {
        extern volatile int g_timer_phase;
        extern volatile unsigned g_timer_loops;
        tt_dbg("  [DEVSTATE] timer thread phase %d, loops %u\n", g_timer_phase, g_timer_loops);
    }
    tt_dbg("  [DEVSTATE] flush flag 0x1BB3BC = %u, frame flag 0x4B7601 = %u; last callbacks %u %u %u %u (newest last)\n",
           MEM32(0x1BB3BCu), MEM8(0x4B7601u), tt_cb_last[(tt_cb_n - 4) & 7], tt_cb_last[(tt_cb_n - 3) & 7],
           tt_cb_last[(tt_cb_n - 2) & 7], tt_cb_last[(tt_cb_n - 1) & 7]);
    if (tt_bot_in)
        tt_dbg("  [DEVSTATE] in BlockOnTime(%u, %u) from %08X; at entry issued %u done %u\n",
               tt_bot_target, tt_bot_flags, tt_bot_caller, tt_bot_issued, tt_bot_done);
}

/* D3D's vblank handler (0x0011DA80, thiscall device). It acknowledges the
 * interrupt by writing PCRTC_INTR and then SPINS until PMC_INTR_0 bit 24
 * (PCRTC pending) reads clear. On hardware the write clears it at once; here
 * these are plain RAM that the NV2A poll thread zeroes, so every vblank the
 * ISR spun until that thread got scheduled -- with the guest pinned to one
 * core, a large share of the CPU went to this loop (2-3 fps movies). The
 * interrupt has already been dispatched to us, so drop the pending bit
 * before the handler looks. */
/* Widescreen (ini [Display] aspect, env TT_ASPECT = the shape of the picture,
 * e.g. 1.7778 for 16:9, 2.3333 for 21:9). The game builds every 3D camera's
 * projection -- and the matching culling frustum -- in 0x0004E890
 * (camera::SetPerspective(fov, aspect, near, far), aspect 4:3). Widening
 * that aspect shows more of the scene left and right at the same vertical
 * view; the renderer draws the 640x480 frame wider by the same factor and
 * the window shows it at the chosen shape. HUD art is stretched to match. */
extern void sub_0004E890_gen(void);
void sub_0004E890(void)
{
    /* the shape chosen now (the settings overlay can change it in game) */
    extern float xbox_GfxAspect(void);
    float k = xbox_GfxAspect() / (4.0f / 3.0f);
    if (getenv("TT_WIDE_TRACE")) {
        static float seen[64][2]; static int ns; int q;
        float fa = MEMF(esp + 8), ff = MEMF(esp + 4);
        for (q = 0; q < ns && !(seen[q][0] == fa && seen[q][1] == ff); q++) {}
        if (q == ns && ns < 64) { seen[ns][0] = fa; seen[ns][1] = ff; ns++;
            tt_dbg("  [WIDE] SetPerspective fov %.3f aspect %.3f near %.3f far %.3f, screen %u, from %08X\n", ff, fa, MEMF(esp + 12), MEMF(esp + 16), MEM32(0x4AD050u), MEM32(esp)); }
    }
    if (k < 1.005f) k = 1.0f;
    if (k > 4.0f) k = 4.0f;
    {
        /* Only gameplay (front-end screen 6 or 8) is widened. The menus'
         * 3D scenes (the level-select map) keep 4:3: widened, their 2D
         * highlights -- drawn at 4:3 in the middle -- no longer sat on the
         * icons under them. A camera made elsewhere also tells the renderer
         * the frames are 4:3 again. */
        uint32_t scr = MEM32(0x4AD050u);
        extern int tt_in_scene(void);
        extern int xbox_GfxGet(int id);
        /* (1.8.2) the in-engine scenes too stay 4:3 (RECOMP_WIDE_SCENES=1
         * widens them): their cameras look where the levels' skies and
         * distant cards were only made for a 4:3 view -- widened, the
         * skybox's corners and the mountain cards' edges showed */
        if (scr != 6 && scr != 8 || (tt_in_scene() && xbox_GfxGet(26) != 1)   /* (GFX_WIDE_SCENES) */) {
            extern volatile unsigned long long g_ws_cam_tick;
            g_ws_cam_tick = 0;
            k = 1.0f;
        }
    }
    if (k != 1.0f) {
        extern volatile unsigned long long g_ws_cam_tick;
        extern __declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);
        float a = MEMF(esp + 8);
        if (a > 1.2f && a < 1.5f) {
            static unsigned n;
            extern void tt_dbg(const char *fmt, ...);
            MEMF(esp + 8) = a * k; g_ws_cam_tick = GetTickCount64();
            if (++n < 5 || n % 500 == 0) tt_dbg("  [WIDE] camera %u widened at %llu ms\n", n, g_ws_cam_tick);
        }
    }
    sub_0004E890_gen();
}

extern void sub_0011DA80_gen(void);
void sub_0011DA80(void)
{
    extern int xbox_InIsr(void);
    tt_vbh_calls++;
    if (xbox_InIsr()) tt_vbh_isr++;
    /* The handler is servicing the vblank: acknowledge it the way its
     * write-1-to-clear of PCRTC_INTR does on hardware (plain RAM here), so
     * its acknowledge loop ends and the pending bit is not seen twice. */
    MEM32(0xFD600100u) &= ~1u;
    MEM32(0xFD000100u) &= ~0x01000000u;
    sub_0011DA80_gen();
    MEM32(0xFD600100u) &= ~1u;   /* its own acknowledge write, cleared */
}

/* Frame-rate modes (ini [Display] fps=30|60|unlocked, env TT_FPS).
 *
 * The game paces itself in 0x000E00E0: it sleeps until its vblank callback
 * has counted 2 vblanks (30 fps) or 1 (60 fps), chosen by the flag at
 * 0x4B75F1 that it sets itself (0 = 60 in menus and some sequences, 1 = 30 in
 * gameplay). The hook below runs at every guest sleep:
 *   60       keeps the flag at 0, so every frame waits for one vblank;
 *   unlocked fills the vblank counter, so the wait ends at once.
 * EXPERIMENTAL: if the game advances its simulation by a fixed step per
 * frame, gameplay runs faster than normal in these modes. */
static int tt_fps_mode = -1;   /* 0 = 30 (stock), 1 = 60, 2 = unlocked */
volatile unsigned long long tt_hud_drawn_ms;   /* the HUD last drawn (0x000CF450) */
/* an in-engine scene: play's screen, no film, and no HUD drawn lately */
int tt_in_scene(void)
{
    extern __declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);
    extern int xbox_MoviePlaying(void);
    return MEM32(0x4AD050u) == 6 && !xbox_MoviePlaying() && GetTickCount64() - tt_hud_drawn_ms > 300;
}
static void tt_delay_hook(void)
{
    extern int xbox_MoviePlaying(void);
    {   /* the renderer's menu fill (nv2a menu_fill_zoom): a front-end screen
         * -- anything but play (screens 6 and 8) -- is drawn inside the TV
         * safe area even where it draws 3D (the main menu, the mission
         * select, the load screen's burning edges), so it fills the screen
         * as the 2D-only ones do */
        extern volatile int xbox_frontend_hint;
        static uint32_t last = 0xFFFFFFFFu;
        uint32_t scr = MEM32(0x4AD050u);
        xbox_frontend_hint = scr != 6 && scr != 8;
        if (scr != last) { last = scr; tt_dbg("  [FILL] front-end screen %u%s\n", scr, xbox_frontend_hint ? " (menu: fills the screen)" : " (play)"); }
    }
    if (!tt_fps_mode) return;
    /* Movies pace themselves through the same wait (two vblanks a frame),
     * so they are left at stock speed. */
    if (xbox_MoviePlaying()) { if (tt_fps_mode == 1) MEM8(0x4B75F1u) = 1; return; }
    if (tt_fps_mode == 1) MEM8(0x4B75F1u) = 0;
    else if (tt_fps_mode == 2) MEM32(0x4B7618u) = 8;   /* (1.8.1: unlocked throughout again -- choose 60 before the scenes instead) */
    if (getenv("TT_HUDCOUNT")) {   /* testing: is the HUD drawn (play) or not (a scene)? */
        extern __declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);
        static unsigned long long last; static int n, nh;
        unsigned long long t = GetTickCount64();
        n++; if (t - tt_hud_drawn_ms < 100) nh++;
        if (t - last > 2000) { tt_dbg("[HUDCOUNT] screen %u: %d waits, %d with the HUD drawn lately, flag %u\n", MEM32(0x4AD050u), n, nh, MEM8(0x4B75F1u)); last = t; n = nh = 0; }
    }
}
/* GPU "flush" waits (0x000E08D0): the game inserts D3D callback 0x000DFFA0
 * with context 2 and spins until its global 0x1BB3BC reads 2. The same
 * callback also runs every frame with contexts 0 and 1 (GPU frame timing)
 * and writes them to the same global. On the console the main thread sees
 * the 2 before the next frame's callbacks run; here callbacks are delivered
 * by the timer thread and a 0/1 could land first -- the 2 was gone and the
 * game spun forever (a freeze, usually during a load; DEVSTATE showed the
 * GPU idle with two callbacks' worth of work "issued" but not "done").
 * The 2 is held after its callback runs: a later 0/1 still does its work,
 * but the global keeps reading 2 so the waiter sees it. Held for 250 ms AND
 * until the game has presented two more frames: the waiting thread presents
 * nothing until it has seen the 2, so on a busy PC -- where a sleep of the
 * waiting thread could outlast 250 ms -- the 2 can no longer be gone before
 * it looks (a freeze while loading Weathertop with three games running). */
static volatile uint32_t tt_hold2_flips;
static void tt_pb_callback_hook(uint32_t fn, uint32_t ctx)
{
    extern __declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);
    extern volatile unsigned g_nv2a_flips;
    unsigned long long t;
    if (fn != 0x000DFFA0u) return;
    tt_cb_last[tt_cb_n++ & 7] = ctx;
    t = GetTickCount64();
    if (ctx == 2) { tt_hold2_until = (long long)t + 250; tt_hold2_flips = g_nv2a_flips + 2; }
    else if ((long long)t < tt_hold2_until || (int32_t)(g_nv2a_flips - tt_hold2_flips) < 0) MEM32(0x1BB3BCu) = 2;
}
/* From the settings overlay. Leaving 60 for 30 puts the game's own 30 fps
 * flag back (gameplay's setting; menus switch it again themselves). */
static void tt_fps_set(int mode)
{
    int old = tt_fps_mode;
    tt_fps_mode = mode;
    if (old == 1 && mode != 1) MEM8(0x4B75F1u) = 1;
    tt_dbg("  [FPS] frame-rate mode now %s\n", mode == 1 ? "60" : mode == 2 ? "unlocked" : "30");
}
void tt_fps_init(void)
{
    extern void (*volatile xbox_guest_delay_hook)(void);
    extern void (*volatile xbox_pb_callback_hook)(uint32_t fn, uint32_t ctx);
    if (!getenv("TT_NO_HOLD2")) xbox_pb_callback_hook = tt_pb_callback_hook;
    const char *v = getenv("TT_FPS");
    tt_fps_mode = 0;
    if (v && (v[0] == '6')) tt_fps_mode = 1;
    else if (v && (v[0] == 'u' || v[0] == 'U')) tt_fps_mode = 2;
    /* Always installed: the settings overlay can change the mode in game. */
    xbox_guest_delay_hook = tt_delay_hook;
    {
        extern void xbox_GfxSetFpsHandler(void (*fn)(int mode));
        xbox_GfxSetFpsHandler(tt_fps_set);
    }
    if (tt_fps_mode)
        tt_dbg("  [FPS] experimental frame-rate mode: %s\n", tt_fps_mode == 1 ? "60" : "unlocked");
}

recomp_func_t twotowers_lookup_manual(uint32_t xbox_va)
{
    switch (xbox_va) {
    case 0x00100F50u: return sub_00100F50;
    case 0x0011A740u: return sub_0011A740;
    case 0x0011A710u: return sub_0011A710;
    case 0x0001B580u: return sub_0001B580;
    case 0x0001B740u: return sub_0001B740;
    case 0x0011C522u: return sub_0011C522;
    case 0x0012F10Au: return sub_0012F10A;
    case 0x00117AD0u: return sub_00117AD0;
    case 0x0011A8E0u: return sub_0011A8E0;
    case 0x0011DA80u: return sub_0011DA80;
    case 0x0004E890u: return sub_0004E890;
    case 0x000E3AF0u: return sub_000E3AF0;
    case 0x000CCA50u: return sub_000CCA50;
    case 0x000CC9C0u: return sub_000CC9C0;
    case 0x00134940u: return sub_00134940;
    case 0x000CF450u: return sub_000CF450;
    case 0x0008FB60u: return sub_0008FB60;
    case 0x000E6080u: return sub_000E6080;
    case 0x000E60B0u: return sub_000E60B0;
    case 0x000E6AC0u: return sub_000E6AC0;
    case 0x001011ECu: return tt_memcpy_case_001011EC;
    case 0x001011F4u: return tt_memcpy_case_001011F4;
    case 0x001010ACu: return tt_memcpy_fwd0;
    case 0x001010B4u: return tt_memcpy_fwd1;
    case 0x001010C0u: return tt_memcpy_fwd2;
    case 0x001010D4u: return tt_memcpy_fwd3;
    case 0x00101248u: return tt_memcpy_bwd0;
    case 0x00101250u: return tt_memcpy_bwd1;
    case 0x00101260u: return tt_memcpy_bwd2;
    case 0x00101274u: return tt_memcpy_bwd3;
    default: return (recomp_func_t)0;
    }
}

/* ---- DEBUG: cinematic script tracing (TT_CINE_DEBUG) ---- */
extern void tt_dbg(const char *fmt, ...);
extern int tt_dbg_on(void);
extern void sub_0001B580_gen(void);
extern void sub_0001B740_gen(void);
static uint32_t tt_cine_seen[64]; static int tt_cine_nseen;
static void tt_cine_dump(uint32_t obj, const char *why)
{
    uint32_t p = MEM32(obj + 0x30), i, k;
    for (i = 0; i < (uint32_t)tt_cine_nseen; i++) if (tt_cine_seen[i] == p) return;
    if (tt_cine_nseen < 64) tt_cine_seen[tt_cine_nseen++] = p; else return;
    tt_dbg("[CINE] %s obj=%08X state=%u asset=%08X rec=%08X t=%08X\n", why, obj,
           MEM32(obj + 0x20), MEM32(obj + 0x14), p, MEM32(obj + 0x2C));
    if (!p) return;
    for (k = 0; k < 12; k++) {
        uint32_t sz = MEM16(p), ty = MEM16(p + 2);
        tt_dbg("[CINE]   +%05X size=%04X type=%04X : %08X %08X %08X %08X\n",
               p - MEM32(obj + 0x30), sz, ty, MEM32(p + 4), MEM32(p + 8), MEM32(p + 12), MEM32(p + 16));
        if (sz == 0 || sz > 0x1000) break;
        p += sz;
    }
}
void sub_0001B580(void)
{
    if (tt_dbg_on()) tt_cine_dump(ecx, "tick");
    sub_0001B580_gen();
}
void sub_0001B740(void)
{
    uint32_t a = MEM32(esp + 4);
    if (tt_dbg_on()) tt_dbg("[CINE] start arg=%08X id=%08X f8=%08X fC=%08X\n", a, MEM32(a + 4), MEM32(a + 8), MEM32(a + 12));
    sub_0001B740_gen();
    if (tt_dbg_on()) { uint32_t o; for (o = 0x1D9970; o < 0x1E5D70; o += 0x3100) if (MEM32(o + 0x20) == 3) tt_cine_dump(o, "started"); }
}

/* D3D's gamma-ramp upload (0x0011C522, thiscall, ramp* on the stack, ret 4):
 * writes R[256], G[256], B[256] into the RAMDAC palette port. There is no
 * DAC here, so keep the ramp for the presenter (video/fb_present.c). */
extern void sub_0011C522_gen(void);
extern unsigned char g_xbox_gamma[3][256];
extern volatile int g_xbox_gamma_set;
void sub_0011C522(void)
{
    uint32_t p = MEM32(esp + 4);
    int i;
    for (i = 0; i < 256; i++) {
        g_xbox_gamma[0][i] = MEM8(p + i);
        g_xbox_gamma[1][i] = MEM8(p + 256 + i);
        g_xbox_gamma[2][i] = MEM8(p + 512 + i);
    }
    g_xbox_gamma_set = 1;
    if (tt_dbg_on()) tt_dbg("[GAMMA] ramp R %u %u %u %u  G %u ..  B %u\n", MEM8(p), MEM8(p + 64), MEM8(p + 128), MEM8(p + 255), MEM8(p + 256 + 128), MEM8(p + 512 + 128));
    sub_0011C522_gen();
}

/* DirectSound voice GetStatus (0x0012F10A, thiscall voice, DWORD* out, ret 4).
 * A stopped voice stays "playing" (flags 0x8001) until the APU interrupt
 * reports the release finished, or until DirectSoundDoWork's 0.5 s timeout
 * (0x0012DB00) force-stops it. Bink stops its sound and spins on GetStatus
 * until it reads 0; with no interrupt and no DoWork on that thread it spun
 * forever (the EA-logo freeze). Run DSound's own timeout check from the
 * poll, so a stop always completes. */
extern void sub_0012F10A_gen(void);
extern void sub_0012DB00(void);
extern void sub_0012DA17(void);
__declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);
__declspec(dllimport) void __stdcall GetSystemTimeAsFileTime(void *ft);
void sub_0012F10A(void)
{
    uint32_t v = ecx;
    { static int m; if (m < 40 && tt_dbg_on()) { m++; tt_dbg("  [DSOUND] GetStatus voice 0x%08X flags 0x%04X caller 0x%08X\n", v, MEM16(v + 0x12), MEM32(esp + 0x10)); } }
    /* One voice at a time is enough to track: the one being polled. */
    static uint32_t track_v;
    static unsigned long long track_t0;
    static int n, stage;
    if ((MEM16(v + 0x12) & 0x8001u) != 0x8001u && v == track_v)
        track_v = 0;              /* it finished: start afresh next time */
    if ((MEM16(v + 0x12) & 0x8001u) == 0x8001u) {
        uint32_t se = esi, sd = edi, sb = ebx;
        unsigned long long now = GetTickCount64();
        if (track_v != v) { track_v = v; track_t0 = now; stage = 0; }
        if (n < 4 || (n & 0xFFFF) == 0) {
            unsigned long long st;
            GetSystemTimeAsFileTime(&st);
            tt_dbg("  [DSOUND] GetStatus on stopping voice 0x%08X (%d) flags %04X stop %08X%08X now %08X%08X caller %08X\n",
                   v, n, MEM16(v + 0x12), MEM32(v + 0x84), MEM32(v + 0x80),
                   (uint32_t)(st >> 32), (uint32_t)st, MEM32(esp + 0x10));
        }
        n++;
        esp -= 4; MEM32(esp) = 0;
        sub_0012DB00();
        esi = se; edi = sd; ebx = sb;
        ecx = v;
        /* Still stopping a second later: DSound's own timeout did not finish
         * it (seen on Windows: one voice polled ten million times while the
         * game's sound went quiet). Force the stop, then if even that does
         * not take, clear the "stopping" and "playing" bits ourselves. */
        if ((MEM16(v + 0x12) & 0x8001u) == 0x8001u && now - track_t0 > 1000) {
            if (stage == 0) {
                stage = 1;
                tt_dbg("  [DSOUND] voice 0x%08X (hw %04X, n %u) stuck stopping for %llu ms; forcing the stop\n", v, MEM16(v + 0x0C), MEM8(v + 0x64), (unsigned long long)(now - track_t0));
                esp -= 4; MEM32(esp) = 0;
                sub_0012DA17();
                esi = se; edi = sd; ebx = sb;
                ecx = v;
            } else if (stage == 1 && now - track_t0 > 2000) {
                /* Clearing the bits by hand (v19) froze the EA logo on
                 * Windows; leave the voice to DirectSound and just say so. */
                stage = 2;
                tt_dbg("  [DSOUND] voice 0x%08X flags %04X still stopping after the forced stop; reporting it stopped\n", v, MEM16(v + 0x12));
            }
            if (stage == 2) {
                /* Bink spins on GetStatus until the sound reads stopped, and
                 * this one never will (the end-of-movie hang). Tell the
                 * caller it has stopped without touching DirectSound's own
                 * state. */
                uint32_t out = MEM32(esp + 4);
                if (out) MEM32(out) = 0;
                eax = 0;
                esp += 8;   /* ret 4 */
                return;
            }
        }
    } 
    sub_0012F10A_gen();
}

/* D3DDevice_SetTexture (0x00117AD0, stdcall stage, texture; ret 8).
 * Diagnostic: a texture whose Format is 0 has never been registered; the GPU
 * gets offset 0 / format 0 and the mesh samples nothing (black characters). */
extern void sub_00117AD0_gen(void);
void sub_00117AD0(void)
{
    uint32_t tex = MEM32(esp + 8);
    if (tex && MEM32(tex + 0x0C) == 0 && tt_dbg_on()) {
        static uint32_t callers[64], counts[64];
        uint32_t c = MEM32(esp);
        int i;
        for (i = 0; i < 64 && callers[i] && callers[i] != c; i++) ;
        if (i < 64) {
            callers[i] = c;
            if (counts[i]++ < 3 || (counts[i] & 1023) == 0)
                tt_dbg("  [SETTEX] stage %u tex 0x%08X hdr %08X %08X %08X %08X %08X caller 0x%08X (%u)\n",
                       MEM32(esp + 4), tex, MEM32(tex), MEM32(tex + 4), MEM32(tex + 8), MEM32(tex + 0xC),
                       MEM32(tex + 0x10), c, counts[i]);
        }
    }
    sub_00117AD0_gen();
}

/* XInputSetState (XAPI, 0x00169845: stdcall handle, XINPUT_FEEDBACK*).
 * TT_RUMBLE_LOG=1 (testing): its first calls, what it returns and what it
 * found in the device it was given -- which way the rumble goes. */
extern void sub_00169845_gen(void);
void sub_00169845(void)
{
    static int log = -1, n;
    uint32_t h = MEM32(esp + 4), fb = MEM32(esp + 8);
    if (log < 0) log = getenv("TT_RUMBLE_LOG") != NULL;
    sub_00169845_gen();
    if (log && n < 40) {
        uint32_t dev = h ? MEM32(h + 0xA3) : 0, ext = h ? MEM32(h) : 0;
        n++;
        tt_dbg("[RUMBLE] SetState h=%08X fb=%08X L=%u R=%u -> %u; dev flags %02X; ext %08X pipe %08X type %02X outsz %02X\n",
               h, fb, fb ? MEM16(fb + 0x42) : 0, fb ? MEM16(fb + 0x44) : 0, eax,
               dev ? MEM8(dev + 0x28) : 0, ext, h ? MEM32(h + 0x10) : 0,
               ext ? MEM8(ext + 4) : 0, ext ? MEM8(ext + 0xD) : 0);
    }
}

/* The game's own debug output, which retail keeps to itself: 0x000E3CA0 is
 * its printf (formats into a 0x200-byte stack buffer, then into an in-memory
 * console nobody draws) and 0x000E5220 its on-screen debug channels (a
 * 0x100-byte buffer). TT_GAMELOG=1 (testing) copies each formatted line into
 * the log: "Create hero (Hnd=...)", the databases loading, the mission
 * loader -- what the developers saw. The buffers are read back from below
 * the caller's stack pointer after the call; nothing else runs on the guest
 * stack in between. */
static int tt_gamelog_on(void)
{
    static int on = -1;
    if (on < 0) on = getenv("TT_GAMELOG") != NULL;
    return on;
}
static void tt_gamelog_line(const char *tag, uint32_t buf, uint32_t cap)
{
    char s[600];
    uint32_t i;
    for (i = 0; i + 1 < cap && i < sizeof s - 1; i++) {
        char c = (char)MEM8(buf + i);
        if (!c) break;
        s[i] = (c == '\n' || c == '\r') ? ' ' : c;
    }
    s[i] = 0;
    if (i) tt_dbg("[GAME%s] %s\n", tag, s);
}
extern void sub_000E3CA0_gen(void);
void sub_000E3CA0(void)
{
    uint32_t e = esp;
    sub_000E3CA0_gen();
    if (tt_gamelog_on()) tt_gamelog_line("", e - 0x200u, 0x200u);
}
extern void sub_000E5220_gen(void);
void sub_000E5220(void)
{
    uint32_t e = esp;
    sub_000E5220_gen();
    if (tt_gamelog_on()) tt_gamelog_line("SCR", e - 0x100u, 0x100u);
}

/* Hooks for the playable-character work (heroes.c). Each is the retail
 * function with a call before and after: tt_hk(id, 0) before -- returning
 * nonzero skips the original -- and tt_hk(id, 1) after. They cost a call
 * each and do nothing unless heroes.c acts on them. */
extern int tt_hk(int id, int phase, uint32_t *regs);
/* script: create hero / actor / prop */
extern void sub_0001A2A0_gen(void);
void sub_0001A2A0(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(1, 0, r)) sub_0001A2A0_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(1, 1, r);
    eax = r[0];
}
/* script: give an actor a weapon (slot 0-3: right hand, left hand, ...) */
extern void sub_0001AE00_gen(void);
void sub_0001AE00(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(21, 0, r)) sub_0001AE00_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(21, 1, r);
    eax = r[0];
}
/* resource lookup by key */
extern void sub_000F5530_gen(void);
void sub_000F5530(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(2, 0, r)) sub_000F5530_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(2, 1, r);
    eax = r[0];
}
/* (1.9.2) The mission results page (0x00071F60, entered with the screen it
 * comes from) loads endlvl.BFF and sets its hero's picture and figures
 * (0x00071970) only when it comes from play (screen 6); from any other
 * screen it keeps what it had -- the last mission's. Reached another way
 * after a level (Balin's Tomb as Legolas: the last mission's Aragorn), it
 * now sets them as from play, once per level played. */
static int s_level_since_results;
extern void sub_00071F60_gen(void);
void sub_00071F60(void)
{
    extern void tt_dbg(const char *fmt, ...);
    if (MEM32(esp + 4) == 6u) s_level_since_results = 0;
    else if (s_level_since_results) {
        tt_dbg("[RESULTS] entered from screen %u after a level: set up as from play\n", MEM32(esp + 4));
        MEM32(esp + 4) = 6u;
        s_level_since_results = 0;
    }
    sub_00071F60_gen();
}

/* ClMissionLoader: open a level / UI file */
extern void sub_000D3C00_gen(void);
void sub_000D3C00(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (MEM32(esp + 8) == 0u) s_level_since_results = 1;   /* (a level) */
    if (!tt_hk(3, 0, r)) sub_000D3C00_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(3, 1, r);
    eax = r[0];
}
/* hero getter (1..4) */
extern void sub_000D2310_gen(void);
void sub_000D2310(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(4, 0, r)) sub_000D2310_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(4, 1, r);
    eax = r[0];
}
/* hero setter */
extern void sub_000D2300_gen(void);
void sub_000D2300(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(5, 0, r)) sub_000D2300_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(5, 1, r);
    eax = r[0];
}
/* character select: cursor left/up */
extern void sub_00076740_gen(void);
void sub_00076740(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(6, 0, r)) sub_00076740_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(6, 1, r);
    eax = r[0];
}
/* character select: cursor right/down */
extern void sub_00076800_gen(void);
void sub_00076800(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(7, 0, r)) sub_00076800_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(7, 1, r);
    eax = r[0];
}
/* character select: confirm */
extern void sub_00076EF0_gen(void);
void sub_00076EF0(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(8, 0, r)) sub_00076EF0_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(8, 1, r);
    eax = r[0];
}
/* upgrade screen */
extern void sub_0006F670_gen(void);
void sub_0006F670(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(9, 0, r)) sub_0006F670_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(9, 1, r);
    eax = r[0];
}
/* level resource directory loader */
extern void sub_000F5E50_gen(void);
void sub_000F5E50(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(10, 0, r)) sub_000F5E50_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(10, 1, r);
    eax = r[0];
}
/* save: load */
extern void sub_000D2000_gen(void);
void sub_000D2000(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(11, 0, r)) sub_000D2000_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(11, 1, r);
    eax = r[0];
}
/* save: write */
extern void sub_000D2130_gen(void);
void sub_000D2130(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(12, 0, r)) sub_000D2130_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(12, 1, r);
    eax = r[0];
}
/* developer switches (HERO=, LEVEL=) */
extern void sub_0007F000_gen(void);
void sub_0007F000(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(13, 0, r)) sub_0007F000_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(13, 1, r);
    eax = r[0];
}
/* level unlocked for a hero (level, hero) */
extern void sub_000D2420_gen(void);
void sub_000D2420(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(14, 0, r)) sub_000D2420_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(14, 1, r);
    eax = r[0];
}
/* Isildur available (front end) */
extern void sub_00073960_gen(void);
void sub_00073960(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(15, 0, r)) sub_00073960_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(15, 1, r);
    eax = r[0];
}
/* escort/defend target name -> HUD icon */
extern void sub_000CBDE0_gen(void);
void sub_000CBDE0(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(16, 0, r)) sub_000CBDE0_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(16, 1, r);
    eax = r[0];
}
/* front end: create a character actor (manager 0x49F440) */
extern void sub_000CA490_gen(void);
void sub_000CA490(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(17, 0, r)) sub_000CA490_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(17, 1, r);
    eax = r[0];
}
/* character select: layout and draw */
extern void sub_000758A0_gen(void);
void sub_000758A0(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(18, 0, r)) sub_000758A0_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(18, 1, r);
    eax = r[0];
}
/* character select: helper */
extern void sub_00073830_gen(void);
void sub_00073830(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(19, 0, r)) sub_00073830_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(19, 1, r);
    eax = r[0];
}
/* character select: the characters' "Level" labels */
extern void sub_00075430_gen(void);
void sub_00075430(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(24, 0, r)) sub_00075430_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(24, 1, r);
    eax = r[0];
}
/* character actor init */
extern void sub_00025980_gen(void);
void sub_00025980(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(20, 0, r)) sub_00025980_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(20, 1, r);
    eax = r[0];
}

/* a projectile's launch (this: the projectile) */
extern void sub_000D9460_gen(void);
void sub_000D9460(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(22, 0, r)) sub_000D9460_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(22, 1, r);
    eax = r[0];
}
/* a projectile's update, each frame (this: the projectile) */
extern void sub_000DA180_gen(void);
void sub_000DA180(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    if (!tt_hk(23, 0, r)) sub_000DA180_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(23, 1, r);
    eax = r[0];
}

/* the menu's character manager made (this, array, room 8): the Extra
 * heroes mod asks for more room (heroes.c, tt_fe_room) */
extern void sub_000CA6D0_gen(void);
extern uint32_t tt_fe_room(uint32_t room);
void sub_000CA6D0(void)
{
    MEM32(esp + 8) = tt_fe_room(MEM32(esp + 8));
    sub_000CA6D0_gen();
}

/* a vector's transform (D3DX-style: out, v, m) -- the guests' blades for
 * the melee hit test (heroes.c, tt_wvec) */
extern void sub_000FF13D_gen(void);
extern int tt_wvec(uint32_t out, uint32_t v, uint32_t m, uint32_t ret);
void sub_000FF13D(void)
{
    if (tt_wvec(MEM32(esp + 4), MEM32(esp + 8), MEM32(esp + 0xC), MEM32(esp))) { eax = MEM32(esp + 4); esp += 16; return; }   /* ret 12 */
    sub_000FF13D_gen();
}

/* a script callback (OnBreakShield, OnKillingBlow, ...): testing counts */
extern void sub_0009E9D0_gen(void);
extern void tt_cbev(uint32_t obj, uint32_t ev);
void sub_0009E9D0(void)
{
    tt_cbev(MEM32(esp + 4), MEM16(esp + 8));
    sub_0009E9D0_gen();
}

/* a particle spawn, and the LightGlowEffect script native (testing logs) */
extern void sub_0008E470_gen(void);
extern void sub_000B4220_gen(void);
extern void tt_partlog(int kind, uint32_t a, uint32_t b, uint32_t c);
/* (1.8.2) more particle effects at once. The game keeps 64 emitters
 * (0x0008E270 makes them: 0x310 bytes each, on the free list at 0x43AD2C;
 * the ones playing are listed at 0x43AD1C) and a spawn with 64 playing ends
 * the one with the least time left -- with the extra heroes' beams and
 * burning rocks, the archers' fire pots' flames went out. More emitters are
 * made as the game makes its own, and the spawn is let past its 64 while
 * any is free. RECOMP_FX_MAX (default 512) the most; 64 as the game. */
int tt_fx_total = 64;
extern uint32_t tt_gcall(uint32_t va, uint32_t thisptr, int n, const uint32_t *args);
extern void sub_000E6B50(void), sub_0008E5D0(void), sub_0008E6A0(void), sub_000FF763(void),
            sub_00097A80(void), sub_000EB600(void), sub_000D09D0(void);
static uint32_t fx_call(void (*fn)(void), uint32_t thisptr, int n, const uint32_t *args)
{
    uint32_t sa = eax, sb = ebx, sc = ecx, sd = edx, ss = esi, sdi = edi, sp = esp, r;
    int i;
    for (i = n - 1; i >= 0; i--) PUSH32(esp, args[i]);
    PUSH32(esp, 0x000D19A2u);
    ecx = thisptr;
    fn();
    r = eax;
    eax = sa; ebx = sb; ecx = sc; edx = sd; esi = ss; edi = sdi; esp = sp;
    return r;
}
static uint32_t s_fx_saved[3], s_fx_arena, s_fx_need, s_fx_left;
static void fx_grow(void)
{
    static uint32_t pool_key;
    static int want = -1;
    uint32_t key = MEM32(0x43AC08u), i, sp, buf;
    int fx_fail = 0;
    if (want < 0) { const char *e = getenv("RECOMP_FX_MAX"); want = e ? atoi(e) : 512; if (want < 64) want = 64; if (want > 2048) want = 2048; }
    if (!key) return;
    /* (a level that is loaded again can put the game's 64 back at the same
     * places; the lists then hold only those 64, so grow again) */
    if (key == pool_key && !(tt_fx_total > 64 && (int32_t)(MEM32(0x43AD1Cu) + MEM32(0x43AD2Cu)) <= 64)) return;
    pool_key = key; tt_fx_total = 64;
    sp = esp; buf = (sp - 0x4000u) & ~0xFu;
    /* (the game's allocator, 0x000E6B50, is a bump allocator whose level
     * arena is spent once a level is loaded: its second arena -- the flag at
     * 0x6B1CEC, the place at 0x6B1D00, a word holding what is left at
     * [0x6B1D04] -- is pointed at memory of ours while the emitters are made) */
    {
        extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment);
        static uint32_t arena, left_word;
        uint32_t need = (uint32_t)(want - 64) * 0x2000u + 0x1000u;
        s_fx_saved[0] = MEM8(0x6B1CEC); s_fx_saved[1] = MEM32(0x6B1D00); s_fx_saved[2] = MEM32(0x6B1D04);
        if (!arena) { arena = xbox_ContiguousAlloc(need, 16u); left_word = xbox_ContiguousAlloc(16u, 16u); }
        if (!arena || !left_word) return;
        MEM32(left_word) = need; MEM32(0x6B1D00) = arena; MEM32(0x6B1D04) = left_word; MEM8(0x6B1CEC) = 1;
        s_fx_arena = arena; s_fx_need = need; s_fx_left = left_word;
    }
    for (i = 64; i < (uint32_t)want; i++) {
        uint32_t a1[1], obj, node;
        a1[0] = 0x310; obj = fx_call(sub_000E6B50, 0, 1, a1);
        if (!obj) { fx_fail = 1; break; }
        obj = fx_call(sub_0008E5D0, obj, 0, NULL);
        if (!obj) { fx_fail = 2; break; }
        fx_call(sub_0008E6A0, obj, 0, NULL);
        { uint32_t a3[3]; a3[0] = buf; a3[1] = 0x174754u; a3[2] = i; fx_call(sub_000FF763, 0, 3, a3); }
        MEM32(obj + 0x114) = 9;
        a1[0] = buf; fx_call(sub_00097A80, obj + 0x10, 1, a1);
        fx_call(sub_0008E6A0, obj, 0, NULL);
        a1[0] = 0x1C; node = fx_call(sub_000E6B50, 0, 1, a1);
        if (!node) { fx_fail = 3; break; }
        node = fx_call(sub_000EB600, node, 0, NULL);
        if (!node) { fx_fail = 4; break; }
        MEM32(node + 0xC) = obj;
        a1[0] = node; fx_call(sub_000D09D0, 0x43AD2Cu, 1, a1);
        tt_fx_total++;
    }
    MEM8(0x6B1CEC) = (uint8_t)s_fx_saved[0]; MEM32(0x6B1D00) = s_fx_saved[1]; MEM32(0x6B1D04) = s_fx_saved[2];
    if (getenv("TT_GAMELOG")) { extern void tt_dbg(const char *fmt, ...); tt_dbg("  [FX] particle effects: %d at once (the game's 64; stop %d; %u bytes of %u)\n", tt_fx_total, fx_fail, s_fx_need - MEM32(s_fx_left), s_fx_need); }
}
void sub_0008E470(void)
{
    uint32_t c, d = 0;
    tt_partlog(0, MEM32(esp + 4), MEM32(esp + 8), MEM32(esp + 12));
    fx_grow();
    c = MEM32(0x43AD1Cu);
    if ((int32_t)c >= 63 && (int32_t)MEM32(0x43AD2Cu) > 0) { d = c - 63; MEM32(0x43AD1Cu) = 63; }   /* (one free: the game takes it) */
    sub_0008E470_gen();
    if (getenv("TT_FXDBG") && c >= 60) { static int nn; extern void tt_dbg(const char *fmt, ...); if (nn++ < 40) tt_dbg("  [FXD] c %u free %d d %u -> now %u ret %08X\n", c, (int32_t)MEM32(0x43AD2Cu), d, MEM32(0x43AD1Cu), eax); }
    if (d) MEM32(0x43AD1Cu) += d;
    {   /* (testing log: the most playing at once, by 32) */
        static int peak; int nnow = (int)MEM32(0x43AD1Cu);
        if (nnow > peak + 31 && getenv("TT_GAMELOG")) { extern void tt_dbg(const char *fmt, ...); peak = nnow; tt_dbg("  [FX] %d playing\n", nnow); }
    }
}
void sub_000B4220(void)
{
    tt_partlog(1, MEM32(esp + 4), MEM32(esp + 8), MEM32(esp + 12));
    sub_000B4220_gen();
}

/* the melee sweep (testing: heroes.c counts its hits) */
extern void sub_000BA110_gen(void);
extern void tt_sweep(uint32_t a0, double res);
void sub_000BA110(void)
{
    uint32_t a0 = MEM32(esp + 4), a1 = MEM32(esp + 8);
    extern void tt_sweep2(uint32_t, uint32_t, double);
    sub_000BA110_gen();
    tt_sweep(a0, g_fp_stack[g_fp_top]);
    tt_sweep2(a0, a1, g_fp_stack[g_fp_top]);
}

/* Call a guest function from host code (heroes.c): args pushed right to
 * left, ecx = this; every register and the stack are put back afterwards,
 * whatever the convention. Returns eax. */
uint32_t tt_gcall(uint32_t va, uint32_t thisptr, int n, const uint32_t *args)
{
    uint32_t sa = eax, sb = ebx, sc = ecx, sd = edx, ss = esi, sdi = edi, sp = esp, r;
    int i;
    recomp_func_t fn = recomp_lookup(va);
    if (!fn) return 0;
    for (i = n - 1; i >= 0; i--) PUSH32(esp, args[i]);
    PUSH32(esp, 0x000D19A2u);
    ecx = thisptr;
    fn();
    r = eax;
    eax = sa; ebx = sb; ecx = sc; edx = sd; esi = ss; edi = sdi; esp = sp;
    return r;
}
uint32_t tt_guest_esp(void) { return esp; }

/* ---- the menus (menus.c): the elements drawn, the main menu's Exit ---- */
extern void sub_000C8940_gen(void);
extern void tt_menu_el(uint32_t el, uint32_t ret);
extern void tt_menu_el_done(uint32_t el);
void sub_000C8940(void)
{
    uint32_t e = ecx;
    tt_menu_el(e, MEM32(esp));
    sub_000C8940_gen();
    tt_menu_el_done(e);
}
extern void sub_00078F50_gen(void);
extern void tt_mm_draw(uint32_t scr);
void sub_00078F50(void)
{
    uint32_t s = ecx;
    sub_00078F50_gen();
    {
        uint32_t sa = eax, sb = ebx, sc = ecx, sd = edx, ss = esi, sdi = edi, sp = esp;
        tt_mm_draw(s);
        eax = sa; ebx = sb; ecx = sc; edx = sd; esi = ss; edi = sdi; esp = sp;
    }
}
extern void sub_00079180_gen(void);
extern int tt_mm_down(uint32_t scr);
void sub_00079180(void)
{
    uint32_t sa = eax, sb = ebx, sc = ecx, sd = edx, ss = esi, sdi = edi, sp = esp;
    if (tt_mm_down(ecx)) { eax = sa; ebx = sb; ecx = sc; edx = sd; esi = ss; edi = sdi; esp = sp + 4; return; }
    eax = sa; ebx = sb; ecx = sc; edx = sd; esi = ss; edi = sdi; esp = sp;
    sub_00079180_gen();
}
extern void sub_00079870_gen(void);
extern void tt_mm_colors_pre(uint32_t scr), tt_mm_colors_post(uint32_t scr);
void sub_00079870(void)
{
    uint32_t s = ecx;
    tt_mm_colors_pre(s);
    sub_00079870_gen();
    {
        uint32_t sa = eax, sb = ebx, sc = ecx, sd = edx, ss = esi, sdi = edi, sp = esp;
        tt_mm_colors_post(s);
        eax = sa; ebx = sb; ecx = sc; edx = sd; esi = ss; edi = sdi; esp = sp;
    }
}
/* the text box and the text drawn in it (menus.c: text not drawn as an element, e.g. dialogs) */
extern void sub_0007E500_gen(void);
extern void sub_0003F1C0_gen(void);
extern void tt_menu_box(int x, int y, int w, int h);
extern void tt_menu_text(uint32_t txt, uint32_t ret);
void sub_0007E500(void)
{
    tt_menu_box((int)MEM32(esp + 4), (int)MEM32(esp + 8), (int)MEM32(esp + 12), (int)MEM32(esp + 16));
    sub_0007E500_gen();
}
void sub_0003F1C0(void)
{
    tt_menu_text(MEM32(esp + 4), MEM32(esp));
    sub_0003F1C0_gen();
}
/* the Options page's drawing: our own pages in its place (options.c) */
extern void sub_0007A4D0_gen(void);
extern int tt_opt_draw(uint32_t obj);
void sub_0007A4D0(void)
{
    uint32_t sa = eax, sb = ebx, sc = ecx, sd = edx, ss = esi, sdi = edi, sp = esp;
    if (tt_opt_draw(ecx)) { eax = sa; ebx = sb; ecx = sc; edx = sd; esi = ss; edi = sdi; esp = sp + 4; return; }
    eax = sa; ebx = sb; ecx = sc; edx = sd; esi = ss; edi = sdi; esp = sp;
    sub_0007A4D0_gen();
}
/* the game's wait for the next frame (in the front end and in play): the
 * menus' frame (menus.c) */
extern void sub_000E00E0_gen(void);
void sub_000E00E0(void)
{
    extern void tt_menus_tick(void);
    uint32_t sa = eax, sb = ebx, sc = ecx, sd = edx, ss = esi, sdi = edi, sp = esp;
    tt_menus_tick();
    eax = sa; ebx = sb; ecx = sc; edx = sd; esi = ss; edi = sdi; esp = sp;
    sub_000E00E0_gen();
}
/* a list (the dialogs' and others': Yes / No, ...) drawn at (x, y) (menus.c) */
extern void sub_000D0700_gen(void);
extern void tt_menu_list(uint32_t obj, int x, int y);
void sub_000D0700(void)
{
    tt_menu_list(ecx, (int)MEM32(esp + 4), (int)MEM32(esp + 8));
    sub_000D0700_gen();
}

/* ---- (1.9.2) the languages (lang.c) ---- */
/* The front end's files are loaded into areas of fixed sizes (0x000D3C00):
 * startup.BFF's is 0xD4800 bytes, made by 0x000D3990 (this = 0x4ACCB0, the
 * size, 1). A language's copy of the file is larger (its text, and the
 * letters added to its fonts): the area made as large as the copy. */
extern void sub_000D3990_gen(void);
extern uint32_t tt_lang_startup_size(void);
void sub_000D3990(void)
{
    if (ecx == 0x4ACCB0u && MEM32(esp + 8) == 1u) {
        uint32_t need = tt_lang_startup_size();
        if (need > MEM32(esp + 4)) {
            tt_dbg("[LANG] startup.BFF's area: %u -> %u bytes\n", MEM32(esp + 4), need);
            MEM32(esp + 4) = need;
        }
    }
    sub_000D3990_gen();
}
/* A line of text is drawn by 0x0003F210 (this = the text block 0x3A7820;
 * args: the font, x, y, the line {width, first, end} -- UTF-16 in the
 * game's memory). Written right to left (Arabic, Hebrew): the line's
 * characters turned to the order they are seen in, for the drawing only. */
extern void sub_0003F210_gen(void);
extern int tt_lang_line(uint32_t first, uint32_t end, uint32_t *nfirst, uint32_t *nend);
void sub_0003F210(void)
{
    uint32_t line = MEM32(esp + 16), f = 0, e = 0, of = 0, oe = 0;
    int turned = 0;
    if (line >= 0x10000u) {
        of = MEM32(line + 4); oe = MEM32(line + 8);
        turned = tt_lang_line(of, oe, &f, &e);
        if (turned) { MEM32(line + 4) = f; MEM32(line + 8) = e; }
    }
    sub_0003F210_gen();
    if (turned) { extern void tt_lang_line_back(void); tt_lang_line_back(); MEM32(line + 4) = of; MEM32(line + 8) = oe; }
}
