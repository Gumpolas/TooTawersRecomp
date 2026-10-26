/*
 * The Two Towers: hand-written guest functions (xboxrecomp manual overrides).
 *
 * Compiled like generated code so it sees the same register model and memory
 * macros. recomp_lookup_manual() in recomp_manual.c consults
 * twotowers_lookup_manual() first.
 */
#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_funcs.h"
#if defined(_WIN32)
typedef unsigned long tt_dword;
#else
typedef uint32_t tt_dword;
#endif
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "tt_env.h"   /* (1.9.3) getenv looked up once per name */
#if !defined(_WIN32)
/* The few host calls below are declared by hand (windows.h would clash with
 * the generated headers); on POSIX they come from win32_compat.c. */
#include <strings.h>
#define __declspec(x)
#define __stdcall
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
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
    { extern void tt_coopprobe_frame(void); tt_coopprobe_frame(); }   /* (coop-anything research probe: TT_COOPPROBE) */
    { extern void tt_coop_frame(void); tt_coop_frame(); }   /* (coop-anything prototype: TT_COOP) */
    { extern volatile unsigned long long tt_hud_drawn_ms; extern __declspec(dllimport) unsigned long long __stdcall GetTickCount64(void); tt_hud_drawn_ms = GetTickCount64(); }
    { extern void tt_coop_hud_pre(void); tt_coop_hud_pre(); }   /* (coop-anything: player 2's health sword, after the E3 HUD took its slot) */
    sub_000CF450_gen();
    { extern void tt_coop_hud_post(void); tt_coop_hud_post(); }
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
/* co-op post-level: a stats screen moves its hero's level experience into the global points to come [0x4AB67C]
 * (0x000D2630: the record's +0x64 plus its +0x90; flag [0x4AB678], [0x4AB270]) and levels the record up; the shop then
 * works from those. Player 2's stats screen, set up after player 1's, overwrote them and left player 1's record as before
 * his level-up: player 1's shop showed Gimli's points and his old level (TT_COOP_RECWATCH: 7875 / level 1 after his
 * stats, 3875 / level 0 after player 2's set-up). Each player's record and those globals are kept when his stats screen
 * ends and put back before his shop. which: 1/2 keep player 1's / 2's, -1/-2 put back. TT_COOP_NOPOSTKEEP: off. */
static uint32_t coop_pk_rec[2][0x348 / 4], coop_pk_g[2][3]; static int coop_pk_ok[2];
static void coop_post_keep(int which, int hb)
{
    int w = (which < 0 ? -which : which) - 1; uint32_t r = 0x4AB598u + (uint32_t)hb * 0x348u, j;
    if (getenv("TT_COOP_NOPOSTKEEP") || hb < 1 || hb > 4 || w < 0 || w > 1) return;
    if (which > 0) {
        for (j = 0; j < 0x348u / 4; j++) coop_pk_rec[w][j] = MEM32(r + 4 * j);
        coop_pk_g[w][0] = MEM32(0x4AB67Cu); coop_pk_g[w][1] = MEM8(0x4AB678u); coop_pk_g[w][2] = MEM32(0x4AB270u); coop_pk_ok[w] = 1;
        tt_dbg("[COOPPOST]   player %d's state kept (hero %d: level %d, points to come %d)\n", w + 1, hb, (int32_t)MEM32(r + 0x78), (int32_t)coop_pk_g[w][0]);
    } else if (coop_pk_ok[w]) {
        for (j = 0; j < 0x348u / 4; j++) MEM32(r + 4 * j) = coop_pk_rec[w][j];
        MEM32(0x4AB67Cu) = coop_pk_g[w][0]; MEM8(0x4AB678u) = (uint8_t)coop_pk_g[w][1]; MEM32(0x4AB270u) = coop_pk_g[w][2];
        tt_dbg("[COOPPOST]   player %d's state put back for his shop (hero %d: level %d, points to come %d)\n", w + 1, hb, (int32_t)MEM32(r + 0x78), (int32_t)coop_pk_g[w][0]);
    }
}
/* (bot suite) a checksum of a hero record's saved part (+0x64..+0x348, the level's running experience +0x90 left
 * out: zeroed when a level starts, and +0x68, not saved): player 2's when his shop closes and when his record is loaded, compared by the
 * suite (local/bot/suite.ps1) across a save and a reload */
static uint32_t coop_rec_sum(const uint8_t *r)
{
    uint32_t h = 2166136261u, j;
    if (getenv("TT_COOP_RECHEX")) { tt_dbg("[COOPRECHEX]"); for (j = 0x64; j < 0x348; j += 4) tt_dbg(" %X:%08X", j, *(const uint32_t *)(r + j)); tt_dbg("\n"); }
    for (j = 0x64; j < 0x348; j++) if ((j < 0x90 || j >= 0x94) && (j < 0x68 || j >= 0x6C) && (j < 0x204 || j >= 0x208)) h = (h ^ r[j]) * 16777619u;   /* (+0x204 too: a per-level working value, 0, 0x10000 or 0x20000 at the shop's close from run to run, 0 after loading -- observed with TT_COOP_RECHEX, with and without the weapon sink) */   /* (+0x68 too: a working value, the next level's threshold, 0 after loading -- observed with TT_COOP_RECHEX; every other word came back) */
    return h;
}
/* (1.9.7, after Athelas) the More page's field of view: "CAMERA/Desired FOV", a field of the level's camera tuning
 * ([[0x43B164] + 0x18] + 0x14, degrees): gameplay cameras follow it, cut-scene cameras set their own. Scaled from the
 * value the level gave it; written again when the level or the game changes it. F1 hides the HUD ("GRAPHICS/Draw HUD",
 * the byte 0x4A8C74) */
static void tt_fov_hud_tick(void)
{
    extern float xbox_GfxGetF(int id); extern int xbox_GfxFind(const char *key); extern volatile int xbox_hud_hidden;
    static int it = -2; static uint32_t field; static float original, written; float pc; uint32_t root, tun, va;
    {   static int was; if (xbox_hud_hidden) { MEM8(0x4A8C74u) = 0; was = 1; } else if (was) { MEM8(0x4A8C74u) = 1; was = 0; } }   /* (shown again once: the game's own hiding is its own) */
    (void)it; (void)field; (void)original; (void)written; (void)pc; (void)root; (void)tun; (void)va;   /* (the field of view: dropped) */
}
static void tt_main_tick(void)
{
    { extern void tt_vs_exp_restore(void); tt_vs_exp_restore(); }
    { extern volatile float xbox_apu_stream_gain, xbox_apu_3d_gain; extern int tt_vs_on(void); if (!tt_vs_on()) { xbox_apu_stream_gain = 1.0f; xbox_apu_3d_gain = 1.0f; } }
    tt_fov_hud_tick();
    if (getenv("TT_CAMLOG")) { extern __declspec(dllimport) tt_dword __stdcall GetTickCount(void); static uint32_t tl; uint32_t cm = MEM32(0x43B164u), h = MEM32(0x43B15Cu);
        if ((uint32_t)GetTickCount() - tl > 700u && cm >= 0x10000u && h >= 0x10000u) { tl = (uint32_t)GetTickCount();
            tt_dbg("[CAMLOG] look %.2f %.2f %.2f eye %.2f %.2f %.2f | hero %.2f %.2f %.2f fwd %.2f %.2f st %u\n", MEMF(cm + 0x40), MEMF(cm + 0x44), MEMF(cm + 0x48), MEMF(cm + 0x50), MEMF(cm + 0x54), MEMF(cm + 0x58),
                   MEMF(h + 0xF0), MEMF(h + 0xF4), MEMF(h + 0xF8), MEMF(h + 0x70), MEMF(h + 0x74), MEM32(h + 0x408)); } }
    static int target = -1;
    static unsigned done;
    extern int xbox_PadScriptFlag(int n);
    extern void tt_dbg(const char *fmt, ...);
    static const uint32_t offs[12] = { 0, 8, 0x40C, 0x3468, 0x3864, 0x3A78, 0x3C80, 0x3E90,
                                       0x8C80, 0x9078, 0x9474, 0x9A78 };
    uint32_t C = 0x4AD050u, cur, scr, old, se;
    {   /* (research) TT_COOP_SHOPDUMP=dir: while a shop is up (screen 10), every second the save data 0x4AB000-0x4AD000
         * (all hero records) and the front end 0x4AD050-0x4B9050 to <dir>/s<n>.bin, logged with the hero byte: where a
         * bought upgrade is kept */
        extern __declspec(dllimport) tt_dword __stdcall GetTickCount(void); extern uint32_t coop_rec_addr;
        const char *dir = getenv("TT_COOP_SHOPDUMP"); static uint32_t last; static int n;
        if (dir && MEM32(0x4AD050u) == 10 && n < 400 && GetTickCount() - last >= 1000u) {
            extern void *fopen(const char *, const char *); extern size_t fwrite(const void *, size_t, size_t, void *); extern int fclose(void *); extern int sprintf(char *, const char *, ...);
            char pth[300]; void *fp; last = GetTickCount();
            if (strlen(dir) < 280) { sprintf(pth, "%s\\s%03d.bin", dir, n);
                if ((fp = fopen(pth, "wb"))) { fwrite((void *)XBOX_PTR(0x4AB000u), 1, 0x2000, fp); fwrite((void *)XBOX_PTR(0x4AD050u), 1, 0xC000, fp); fclose(fp); }
                tt_dbg("[SHOPDUMP] s%03d: shop up, hero byte %u, points of the record at %08X: %d\n", n, MEM8(0x4AB69Du), coop_rec_addr, coop_rec_addr ? (int32_t)MEM32(coop_rec_addr + 0x64) : -1); n++; }
        }
    }
    {   /* (research) TT_COOP_RECWATCH: both heroes' records (experience +0x6C, level +0x78, the level's +0x90) logged at
         * each change, with the front-end screen and the hero byte */
        static int rw = -1; static int32_t le[2] = { -1, -1 }, ll[2] = { -1, -1 }; static uint32_t lr[2]; extern uint32_t coop_rec_addr; int w;
        if (rw < 0) rw = getenv("TT_COOP_RECWATCH") != 0;
        for (w = 0; rw && w < 2 && coop_rec_addr; w++) {
            extern int tt_heroes_retail_byte(const char *skin); extern const char *tt_coop_p2_char_now;
            int hb = w ? tt_heroes_retail_byte(tt_coop_p2_char_now) : (int)((coop_rec_addr - 0x4AB598u) / 0x348u);
            uint32_t r = 0x4AB598u + (uint32_t)hb * 0x348u, rf = MEM32(r + 0x90);
            {   static uint32_t lp[2], lg[3];
                if (hb > 0 && ((int32_t)MEM32(r + 0x6C) != le[w] || (int32_t)MEM32(r + 0x78) != ll[w] || rf != lr[w] || MEM32(r + 0x64) != lp[w] || (!w && (MEM32(0x4AB67Cu) != lg[0] || MEM8(0x4AB678u) != lg[1] || MEM32(0x4AB270u) != lg[2])))) {
                    le[w] = (int32_t)MEM32(r + 0x6C); ll[w] = (int32_t)MEM32(r + 0x78); lr[w] = rf; lp[w] = MEM32(r + 0x64); if (!w) { lg[0] = MEM32(0x4AB67Cu); lg[1] = MEM8(0x4AB678u); lg[2] = MEM32(0x4AB270u); }
                    tt_dbg("[RECWATCH] P%d's slot (hero %d): +64 %d experience %d level %d level's %.0f | [4AB67C] %d [4AB678] %u [4AB270] %d | screen %u, hero byte %u, depth %d\n", w + 1, hb, (int32_t)lp[w], le[w], ll[w], MEMF(r + 0x90),
                           (int32_t)MEM32(0x4AB67Cu), MEM8(0x4AB678u), (int32_t)MEM32(0x4AB270u), MEM32(0x4AD050u), MEM8(0x4AB69Du), 0);
                }
            }
        }
    }
    {   /* (research, a playtest: no character sounds until the cave troll's scene) the sound fade flags: [0x43BD21] set by
         * 0x000C35E0 (fade out), cleared by 0x000C3610 (fade in); [0x43BD69] gates the script sounds. Each change logged
         * with the front-end screen and the time. TT_COOP_SNDLOG off. */
        static int sl = -1; static uint8_t l21 = 0xEE, l69 = 0xEE;
        if (sl < 0) sl = !getenv("TT_COOP_SNDLOG");
        if (sl && (MEM8(0x43BD21u) != l21 || MEM8(0x43BD69u) != l69)) {
            extern __declspec(dllimport) tt_dword __stdcall GetTickCount(void);
            tt_dbg("[SNDFLAG] t %lu screen %u: fade [43BD21] %u -> %u, [43BD69] %u -> %u, level [1BB2A4] %d\n", GetTickCount(), MEM32(0x4AD050u), l21, MEM8(0x43BD21u), l69, MEM8(0x43BD69u), (int32_t)MEM32(0x1BB2A4u));
            l21 = MEM8(0x43BD21u); l69 = MEM8(0x43BD69u);
        }
    }
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
        if (tw && MEM32(0x1CB8B4u) > 3u && getenv("TT_COOP_P1PORT")) {   /* (bot test) player 1 on this port: with
            * it unset the game reads every pad for him, player 2's scripted one too (two real pads each have their own) */
            extern int coop_in_p2_ctx(void);
            if (!coop_in_p2_ctx()) { MEM32(0x1CB8B4u) = (uint32_t)atoi(getenv("TT_COOP_P1PORT")); tt_dbg("  [DEBUG] test: player 1's controller port -> %u\n", MEM32(0x1CB8B4u)); }
        }
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
    {   /* co-op, after a level: player 2's mission stats and upgrade shop after player 1's (in this order: P1 stats, P2 stats,
         * P1 upgrades, P2 upgrades, then the save). Each screen object's +4 is the next screen the main loop goes to when it
         * ends (screen 9 the stats, object +0x9474; 10 the shop, +0x9A78; 7 then the save). When player 1 continues, the
         * same screen comes again with the hero byte [0x4AB69D] set to player 2's hero and his record in that hero's
         * runtime record (0x4AB598 + byte * 0x348, which holds the save's block for him from +0x64): what he earns and
         * buys is his hero's, and the game's own save keeps it. Retail heroes only (an extra hero has no slot there).
         * TT_COOP_NOP2SCREENS: off. */
        static int ph, p2set, p2shoplog, p1shopseen; static uint8_t hb1; static int hb2s;
        /* (a shop is done only once it has been seen open, its +4 at 0: the shop object keeps the last shop's +4 -- 7, the
         * save -- and from the second level of a session player 1's shop was taken as done the moment it came up: player
         * 2's state in it, player 1 unable to buy, player 2's own never shown -- a playtest; the log: "player 1's upgrades
         * done" with the screen's first frame. Cleared below as player 1's shop comes) */
        extern int tt_heroes_retail_byte(const char *skin); extern const char *tt_coop_p2_char_now; extern uint8_t *coop_p2_state_w(void);
        uint32_t fe = MEM32(C), o9 = C + 0x9474u, o10 = C + 0x9A78u;
        if (fe == 6 || fe == 1 || fe == 4) ph = p2set = p2shoplog = p1shopseen = 0;
        if (ph == 2 && fe == 10 && MEM32(o10 + 4) == 0u) p1shopseen = 1;
        { extern int tt_coop_post_ph; tt_coop_post_ph = ph; }   /* (for menus.c: 1 = player 2's stats screen) */
        extern int tt_coop_post_on(void);
        if (tt_coop_post_on() && !getenv("TT_COOP_NOP2SCREENS")) {
            int hb2 = ph ? hb2s : tt_heroes_retail_byte(tt_coop_p2_char_now);
            if (ph == 0 && fe == 9 && hb2 && MEM32(o9 + 4) == 10u) {
                uint32_t j; uint8_t *s2 = coop_p2_state_w() + 0x400;
                hb1 = MEM8(0x4AB69Du); hb2s = hb2;
                if (hb2 != hb1) {
                    coop_post_keep(1, hb1);
                    for (j = 0; j < 0x348u; j += 4) MEM32(0x4AB598u + (uint32_t)hb2 * 0x348u + j) = *(uint32_t *)(s2 + j);
                    MEM8(0x4AB69Du) = (uint8_t)hb2; MEM32(o9 + 4) = 9u; ph = 1;
                    tt_dbg("[COOPPOST] player 1's stats done: player 2's (hero %d, %s: experience %d, level %d, the level's %.0f)\n", hb2, tt_coop_p2_char_now,
                           (int32_t)MEM32(0x4AB598u + (uint32_t)hb2 * 0x348u + 0x6C), (int32_t)MEM32(0x4AB598u + (uint32_t)hb2 * 0x348u + 0x78), MEMF(0x4AB598u + (uint32_t)hb2 * 0x348u + 0x90));
                }
            } else if (ph == 1 && fe == 9 && MEM32(o9 + 4) != 9u && !p2set) {
                /* his stats screen has been entered (its enter put +4 back): its hero name, portrait and the rest are set up by
                 * 0x00071970, which the enter (0x00071F60) runs only while endlvl.bff is still to load -- not the second time */
                extern uint32_t tt_gcall(uint32_t va, uint32_t thisptr, int n, const uint32_t *args);
                p2set = 1; tt_gcall(0x00071970u, o9, 0, NULL); tt_dbg("[COOPPOST] player 2's stats screen set up for hero %d\n", hb2s);
            } else if (ph == 1 && fe == 9 && MEM32(o9 + 4) == 10u) {
                MEM8(0x4AB69Du) = hb1; ph = 2; tt_dbg("[COOPPOST] player 2's stats done: player 1's upgrades\n");
                /* each player's shop gets the state his own stats screen left (coop_post_keep; a playtest: "post level stats
                 * showed a level up but not in p1 upgrade shop") */
                coop_post_keep(2, hb2s); coop_post_keep(-1, hb1);
                if (getenv("TT_COOP_TESTSTALE")) { MEM32(o10 + 4) = 7u; tt_dbg("[COOPPOST] test: the shop's +4 left at 7, as after a save\n"); }   /* (bot test: the second level's case in one) */
                MEM32(o10 + 4) = 0u;   /* (the shop's next screen cleared before player 1's shop: the shop keeps the last one's, 7 after a save, and
                                        * does not clear it as it opens -- a bot run with it left at 7: his shop stayed at the save question) */
            } else if (ph == 2 && p1shopseen && fe == 10 && MEM32(o10 + 0x3A0) == 4u) {
                /* player 1's shop: no "Do you want to save your game?" (its dialog state +0x3A0 = 4, opened when he says he is
                 * finished): closed at once and the shop leaves as its Yes does (0x0006FF57: +4 next screen, the fade state +0x57C
                 * = 3, +0x580 = 1, 0x000C35E0), but for player 2's shop (his Continue goes straight there; player 2's shop
                 * asks it) */
                extern uint32_t tt_gcall(uint32_t va, uint32_t thisptr, int n, const uint32_t *args);
                /* (not its 0x000C35E0: that fades the music and sound out, [0x43BD21] = 1, for the save screen -- player 2's
                 * shop was silent in a playtest) */
                MEM32(o10 + 0x3A0) = 0; MEM32(o10 + 4) = 10u; MEM32(o10 + 0x57C) = 3u; MEM32(o10 + 0x580) = 0x3F800000u;
                MEM8(0x4AB69Du) = (uint8_t)hb2s; ph = 3; tt_dbg("[COOPPOST] player 1's upgrades done (no save question): player 2's (audio fade flag %u)\n", MEM8(0x43BD21u));
                coop_post_keep(-2, hb2s);
            } else if (ph == 2 && p1shopseen && fe == 10 && MEM32(o10 + 4) == 7u) {
                MEM8(0x4AB69Du) = (uint8_t)hb2s; MEM32(o10 + 4) = 10u; ph = 3; tt_dbg("[COOPPOST] player 1's upgrades done: player 2's\n");
                coop_post_keep(-2, hb2s);
            } else if (ph == 3 && fe == 10 && MEM32(o10 + 4) == 0u && !p2shoplog) {
                p2shoplog = 1; tt_dbg("[COOPPOST] player 2's shop open: audio fade flag [43BD21] %u\n", MEM8(0x43BD21u));
            } else if (ph == 3 && p2shoplog && fe == 10 && (MEM32(o10 + 4) == 7u || MEM32(o10 + 4) == 1u)) {   /* (7: save; 1: no save, to the map) */
                uint32_t j; uint8_t *s2 = coop_p2_state_w() + 0x400;
                for (j = 0; j < 0x348u; j += 4) *(uint32_t *)(s2 + j) = MEM32(0x4AB598u + (uint32_t)hb2s * 0x348u + j);
                MEM8(0x4AB69Du) = hb1; ph = 4;
                tt_dbg("[COOPPOST] player 2's upgrades done (experience %d, level %d): player 1's hero back for the save\n", *(int32_t *)(s2 + 0x6C), *(int32_t *)(s2 + 0x78));
                tt_dbg("[COOPREC] player 2's record sum %08X (his shop closed)\n", coop_rec_sum(s2));
            }
        }
    }
    if (getenv("TT_SCREENLOG")) {   /* (research) each front-end screen change: its object, vtable, the screen it came from, the hero byte */
        static uint32_t last = 0xFFFFFFFFu;
        if (MEM32(C) != last) { uint32_t o = MEM32(C + 4); tt_dbg("[SCREEN] %u -> %u: object %08X vt %08X (+4 %u +8 %u), hero byte [4AB69D] %u, [4AB69C] %u, level [1BB2A4] %d\n", last, MEM32(C), o, o ? MEM32(o) : 0, o ? MEM32(o + 4) : 0, o ? MEM32(o + 8) : 0, MEM8(0x4AB69Du), MEM8(0x4AB69Cu), (int32_t)MEM32(0x1BB2A4u)); last = MEM32(C); }
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
/* The game's frame time, in whole milliseconds with the remainder carried
 * (sub_000E3350 derives the game timer -- dt in ms at 0x4B8B98, in seconds
 * at 0x4B8BA0 -- from the clock each frame).
 *
 * The game's per-frame world update (0x000F1240 / 0x000F1310 / 0x000F2FF0)
 * cuts the frame time down to whole milliseconds and drops the rest. At 30
 * fps that lost 1% (33.3 -> 33 ms); at 400 fps half of every frame (2.5 ->
 * 2 ms), so above the monitor's rate everything in the world ran slow and
 * felt delayed, and under a millisecond it did not move at all. Here every
 * frame's time is whole milliseconds and the fractions add up into the next
 * frame, so game time keeps real time at any frame rate.
 * TT_NO_MSCARRY=1: as the game did it. */
extern void sub_000E3350_gen(void);
static void tt_frametime(void);
/* (1.9.5) The game's own clock: the sum of the whole-ms frame times it was
 * given (testing: RECOMP_PAD_GAMETIME presses a pad script's buttons by it;
 * TT_DUMP_GAME_MS=n writes a picture every n ms of it to TT_DUMP_DIR -- so
 * runs at different fixed frame times, TT_FAKE_FRAME_MS, can be compared
 * moment for moment) */
static volatile uint32_t tt_game_ms, tt_lvl_ms;
static float tt_real_ms;   /* (the frame's time before any TT_FAKE_FRAME_MS) */
static uint32_t tt_game_clock(void) { return getenv("TT_PADCLOCK_LVL") ? tt_lvl_ms : tt_game_ms; }
/* testing: TT_MEMDUMP_GAME=prefix, TT_MEMDUMP_AT=t1,t2,... -- all of RAM at
 * those ms of game time after the last long frame (a level's load ends with
 * one): two runs at different fixed frame times compared word for word find
 * what in the game keeps time per frame instead of per millisecond */
static void tt_memdump_game(uint32_t before, uint32_t now)
{
    static const char *pre = (const char *)1; static uint32_t at[16]; static int nat;
    int i;
    if (pre == (const char *)1) {
        const char *e = getenv("TT_MEMDUMP_AT");
        pre = getenv("TT_MEMDUMP_GAME");
        while (e && *e && nat < 16) { at[nat++] = (uint32_t)atoi(e); while (*e && *e != ',') e++; if (*e) e++; }
    }
    if (!pre) return;
    for (i = 0; i < nat; i++) if (before < at[i] && now >= at[i]) {
        extern void *fopen(const char *, const char *);
        extern size_t fwrite(const void *, size_t, size_t, void *);
        extern int fclose(void *);
        char nm[300]; void *f;
        size_t pl = strlen(pre) < 280 ? strlen(pre) : 280;
        memcpy(nm, pre, pl); nm[pl] = (char)('0' + i / 10); nm[pl + 1] = (char)('0' + i % 10); memcpy(nm + pl + 2, ".bin", 5);
        if ((f = fopen(nm, "wb"))) { fwrite((const void *)XBOX_PTR(0x10000u), 1, 0x3FF0000u, f); fclose(f); }
        memcpy(nm + pl + 2, "c.bin", 6);   /* (and the contiguous window at 0x80000000 -- separate memory here, where many objects live) */
        if ((f = fopen(nm, "wb"))) { fwrite((const void *)XBOX_PTR(0x80000000u), 1, 0x4000000u, f); fclose(f); }
        tt_dbg("  [MEMDUMP] %s at %u ms of game time (frame %u ms)\n", nm, now, now - before);
    }
}
void sub_000E3350(void)
{
    extern uint32_t (*volatile xbox_pad_clock)(void);
    float ms; uint32_t w;
    if (!xbox_pad_clock) xbox_pad_clock = tt_game_clock;
    tt_frametime();
    w = MEM32(0x4B8B98u); memcpy(&ms, &w, 4);
    if (tt_real_ms >= 100.0f) { static int lg = -1; if (lg < 0) lg = getenv("TT_LVLLOG") != NULL; if (lg) tt_dbg("  [LVLRESET] at game %u ms, level clock was %u (frame %.0f ms)\n", tt_game_ms, tt_lvl_ms, tt_real_ms); tt_lvl_ms = 0; }
    if (ms >= 1.0f && ms < 100.0f) {
        uint32_t lb = tt_lvl_ms;
        tt_lvl_ms += (uint32_t)ms;
        tt_memdump_game(lb, tt_lvl_ms);
        {   /* testing: TT_POSLOG=n -- the player's place every n ms of game time ([POS] lines) */
            static int pl = -1;
            if (pl < 0) { const char *e = getenv("TT_POSLOG"); pl = e ? atoi(e) : 0; }
            if (pl > 0 && lb / (uint32_t)pl != tt_lvl_ms / (uint32_t)pl) {
                uint32_t p = MEM32(0x43B15Cu);
                if (p) { float x, y, z; uint32_t v; v = MEM32(p + 0xF0); memcpy(&x, &v, 4); v = MEM32(p + 0xF4); memcpy(&y, &v, 4); v = MEM32(p + 0xF8); memcpy(&z, &v, 4);
                    tt_dbg("[POS] %u %.3f %.3f %.3f\n", tt_lvl_ms, x, y, z); }
            }
        }
        {   /* testing: TT_ACTLOG=n -- every n ms of game time, each actor (vtable 0x001741C0, in the
             * contiguous window) whose height moved by over 0.5 since the last look ([ACT] t addr x y z) */
            static int al = -1; static uint32_t aaddr[512]; static float az[512]; static int an;
            if (al < 0) { const char *e = getenv("TT_ACTLOG"); al = e ? atoi(e) : 0; }
            if (al > 0 && lb / (uint32_t)al != tt_lvl_ms / (uint32_t)al) {
                const uint32_t *w = (const uint32_t *)XBOX_PTR(0x80000000u);
                uint32_t i;
                for (i = 0; i < 0x4000000u / 4u - 0x200u; i++) if (w[i] == 0x001741C0u) {
                    uint32_t o = 0x80000000u + i * 4u; float x, y, z; int k;
                    memcpy(&x, &w[i + 0xF0 / 4], 4); memcpy(&y, &w[i + 0xF4 / 4], 4); memcpy(&z, &w[i + 0xF8 / 4], 4);
                    for (k = 0; k < an && aaddr[k] != o; k++) {}
                    if (k == an && an < 512) { aaddr[an] = o; az[an] = z; an++; tt_dbg("[ACT] %u %08X new %.2f %.2f %.2f\n", tt_lvl_ms, o, x, y, z); }
                    else if (k < an && (z - az[k] > 0.5f || az[k] - z > 0.5f)) { az[k] = z; tt_dbg("[ACT] %u %08X %.2f %.2f %.2f\n", tt_lvl_ms, o, x, y, z); }
                }
            }
        }
#if !defined(_WIN32)
        {   /* testing: TT_WATCH=guest address, from TT_WATCH_AT ms of game time ([WATCH] lines: who writes it) */
            static int on = -1; static uint32_t wa, wat;
            if (on < 0) { const char *e = getenv("TT_WWATCH"); on = e != NULL; if (e) wa = (uint32_t)strtoul(e, NULL, 16); e = getenv("TT_WWATCH_AT"); wat = e ? (uint32_t)atoi(e) : 10000; }
            if (on && tt_lvl_ms >= wat) { extern void xbox_WatchArm(void *); static int said; if (!said++) tt_dbg("[WATCH] arming %08X at %u ms\n", wa, tt_lvl_ms); { static const uint32_t al[6] = { 0, 0x04000000u, 0x08000000u, 0x0C000000u, 0x80000000u, 0xF0000000u }; int q; for (q = 0; q < 6; q++) xbox_WatchArm((void *)XBOX_PTR((wa & 0x03FFFFFFu) | al[q])); } }
        }
#endif
        {   /* testing: TT_GOD -- player 1's health kept full (a level left to run by itself) */
            static int god = -1; uint32_t p = MEM32(0x43B15Cu);
            if (god < 0) god = getenv("TT_GOD") != NULL;
            if (god && p >= 0x10000u && (int32_t)MEM32(p + 0x300) > 0) MEM32(p + 0x2FC) = MEM32(p + 0x300);
        }
        static int every = -1, bylvl = -1; static const char *dir;
        uint32_t before = tt_game_ms;
        tt_game_ms += (uint32_t)ms;
        if (bylvl < 0) bylvl = getenv("TT_DUMP_LVL") != NULL;   /* testing: pictures by the time since the level's load ended */
        if (bylvl) { static uint32_t mx; if (lb == 0 && mx > 30000u && every > 0) every = 0; if (tt_lvl_ms > mx) mx = tt_lvl_ms; before = lb; }   /* (until the level clock restarts after 30 s) */
        if (every < 0) { const char *e = getenv("TT_DUMP_GAME_MS"); every = e ? atoi(e) : 0; dir = getenv("TT_DUMP_DIR"); if (!dir) dir = "."; }
        if (every > 0 && before / (uint32_t)every != (bylvl ? tt_lvl_ms : tt_game_ms) / (uint32_t)every) {
            {   /* testing: TT_MEMLOG=addr[,addr...] (hex) -- with each picture, those words and, when one points into memory,
                 * the object's +0x2FC / +0x300 ([MEMLOG]) */
                const char *ml = getenv("TT_MEMLOG");
                while (ml && *ml) { char *e2; uint32_t a2 = (uint32_t)strtoul(ml, &e2, 16), v2 = MEM32(a2);
                    tt_dbg("[MEMLOG] t %u %08X = %08X (%d, %.3f)%s", tt_game_ms, a2, v2, (int32_t)v2, MEMF(a2), "");
                    if (v2 >= 0x10000u && v2 < 0x84000000u && (v2 < 0x04000000u || v2 >= 0x80000000u)) { uint32_t vt = MEM32(v2), k2; tt_dbg(" -> vt %08X f98 %08X fA4 %08X +2FC %d +300 %d", vt, vt >= 0x10000u ? MEM32(vt + 0x98) : 0, vt >= 0x10000u ? MEM32(vt + 0xA4) : 0, (int32_t)MEM32(v2 + 0x2FC), (int32_t)MEM32(v2 + 0x300)); for (k2 = 0; k2 < 0x600u; k2 += 4) if (MEM32(v2 + k2) == MEM32(0x4ACB50u) && MEM32(0x4ACB50u)) tt_dbg(" [+%X]", k2); }
                    tt_dbg("\n"); ml = *e2 == ',' ? e2 + 1 : ""; }
            }
            extern void xbox_PresentDumpRequest(const char *path);
            static char path[600];
            uint32_t v = (bylvl ? tt_lvl_ms : tt_game_ms) / (uint32_t)every * (uint32_t)every, n = 0, k;
            char num[8];
            for (k = 0; k < 7; k++) { num[6 - k] = (char)('0' + v % 10); v /= 10; }
            num[7] = 0;
            while (dir[n] && n < 500) { path[n] = dir[n]; n++; }
            memcpy(path + n, "/g_", 3); memcpy(path + n + 3, num, 7); memcpy(path + n + 10, ".bmp", 5);
            xbox_PresentDumpRequest(path);
        }
    }
}
static void tt_frametime(void)
{
    static int off = -1;
    static double carry;
    float ms;
    sub_000E3350_gen();
    { uint32_t w0 = MEM32(0x4B8B98u); memcpy(&tt_real_ms, &w0, 4); }
    {   /* testing: TT_FAKE_FRAME_MS=x -- every frame's time is x ms, as at 1000/x fps */
        static float fake = -1.0f;
        if (fake < 0.0f) { const char *e = getenv("TT_FAKE_FRAME_MS"); fake = e ? (float)atof(e) : 0.0f; }
        if (fake > 0.0f) {
            float fs = fake * 0.001f; uint32_t w;
            memcpy(&w, &fake, 4); if (MEM32(0x4B8B98u)) MEM32(0x4B8B98u) = w;
            memcpy(&w, &fs, 4); if (MEM32(0x4B8BA0u)) MEM32(0x4B8BA0u) = w;
        }
    }
    if (off < 0) off = getenv("TT_NO_MSCARRY") != NULL;
    if (off) return;
    { uint32_t w = MEM32(0x4B8B98u); memcpy(&ms, &w, 4); }
    if (!(ms > 0.0f) || ms >= 100.0f) { carry = 0; return; }
    /* (steps far under a millisecond are not frames: a loading screen ticks
     * the timer in a tight loop, and the world's step was 0 there -- left so) */
    /* (1.9.5: no longer left out -- frames a catch-up let through back to
     * back could be under half a millisecond, and the world lost them) */
    {   /* for the log: the world's whole-ms time against the real time, every 10 s */
        static double real, world_before, world_after; static unsigned frames;
        real += ms; world_before += (int)ms; frames++;
        world_after += (int)(carry + ms);
        if (real > 10000.0) {
            extern volatile unsigned g_nv2a_flips;
            static unsigned lf;
            tt_dbg("  [FRAMETIME] %.0f fps (%u steps, %u frames shown): the world moved %.1f%% of real time (%.1f%% without the carry)\n",
                   frames * 1000.0 / real, frames, g_nv2a_flips - lf, world_after * 100.0 / real, world_before * 100.0 / real);
            lf = g_nv2a_flips;
            real = world_before = world_after = 0; frames = 0;
        }
    }
    carry += ms;
    if (carry >= 1.0) {
        int whole = (int)carry;
        float fm = (float)whole, fs = (float)whole * 0.001f;
        carry -= whole;
        memcpy((void *)XBOX_PTR(0x4B8B98u), &fm, 4);
        memcpy((void *)XBOX_PTR(0x4B8BA0u), &fs, 4);
    }
    else {
        /* (under a millisecond so far: the world's whole-ms step is 0 this
         * frame -- 1.9.5: written as 0, where the world had rounded the frame's
         * own fraction UP to a millisecond (fistp) while animations cut it to
         * nothing, and the carry then gave it again: above 1000 fps the world
         * ran fast and animations slow beside it. The seconds keep the frame's
         * small time, so nothing is divided by zero; unlocked stays at 1000
         * fps or under, so this is rare) */
        float z = 0.0f; memcpy((void *)XBOX_PTR(0x4B8B98u), &z, 4);
    }
}

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
            /* (1.9.3) the render thread first through what it was given, so
             * a resource freed after this is no longer being drawn from */
            extern int xbox_Nv2aDrained(void);
            unsigned long long w0 = GetTickCount64();
            extern void xbox_PbWaitProgress(unsigned ms);
            while (!xbox_Nv2aDrained() && GetTickCount64() - w0 < 2000) xbox_PbWaitProgress(1);
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
                extern int xbox_Nv2aDrained(void);
                if (xbox_Nv2aDrained()) {   /* (GET == PUT, and the render thread through it all) */
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
                {   extern void xbox_PbWaitProgress(unsigned ms); xbox_PbWaitProgress(1); }   /* (1.9.5: not a spin) */
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
/* co-op: the shared camera's zoom-out (tt_coop_camera_post sets it from the heroes' distance), applied to the field
 * of view below: fov' = 2 atan(tan(fov / 2) * zoom), so the view takes in zoom times as much around the midpoint */
float coop_cam_zoom = 1.0f; unsigned coop_persp_n; float coop_persp_fov[2];
static uint32_t coop_persp_obj, coop_persp_args[4]; static int coop_persp_ours; static float coop_persp_applied = 1.0f;
extern void sub_0004E890_gen(void);
void sub_0004E890(void);
uint32_t coop_persp_obj_get(void);
void coop_persp_reapply(void)   /* the game sets a camera's projection only when a shot starts: re-issued with the shot's own arguments when the zoom changes */
{
    uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx;
    if (!coop_persp_obj || (coop_cam_zoom - coop_persp_applied < 0.01f && coop_persp_applied - coop_cam_zoom < 0.01f)) return;
    if (MEM32(0x4AD050u) != 6 && MEM32(0x4AD050u) != 8) { coop_persp_obj = 0; return; }   /* out of gameplay: forget the shot (its camera may go away) */
    coop_persp_ours = 1;
    PUSH32(esp, coop_persp_args[3]); PUSH32(esp, coop_persp_args[2]); PUSH32(esp, coop_persp_args[1]); PUSH32(esp, coop_persp_args[0]); PUSH32(esp, 0x0004E890u);
    ecx = coop_persp_obj; sub_0004E890();
    esp = sp; eax = r0; ecx = r1; edx = r2; coop_persp_ours = 0;
}
uint32_t coop_persp_obj_get(void) { return coop_persp_obj; }
void sub_0004E890(void)
{
    coop_persp_n++;
    if (!coop_persp_ours && (MEM32(0x4AD050u) == 6 || MEM32(0x4AD050u) == 8)) { int i; coop_persp_obj = ecx; for (i = 0; i < 4; i++) coop_persp_args[i] = MEM32(esp + 4 + 4 * i); }   /* the shot's own (unzoomed, unwidened) arguments */
    coop_persp_applied = coop_cam_zoom;
    if (coop_cam_zoom > 1.001f && (MEM32(0x4AD050u) == 6 || MEM32(0x4AD050u) == 8)) {   /* gameplay only */
        extern int tt_in_scene(void);
        float f = MEMF(esp + 4), deg = f > 3.2f ? 57.29578f : 1.0f, r = f / deg, r2 = 2.0f * atanf(tanf(0.5f * r) * coop_cam_zoom);
        if (getenv("TT_COOP_CAMLOG")) { extern __declspec(dllimport) tt_dword __stdcall GetTickCount(void); static uint32_t lt; if ((uint32_t)GetTickCount() - lt > 3000) { lt = (uint32_t)GetTickCount(); tt_dbg("[COOPCAM] SetPerspective fov %.3f (%s) in scene %d, from %06X\n", f, deg > 1.5f ? "degrees" : "radians", tt_in_scene(), MEM32(esp)); } }
        if (!tt_in_scene() && r > 0.1f && r < 2.5f) { if (r2 > 2.6f) r2 = 2.6f; MEMF(esp + 4) = r2 * deg; coop_persp_fov[0] = f; coop_persp_fov[1] = r2 * deg; }
    }
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
static int tt_fps_mode = -1;   /* 0 = 30 (stock), 1 = 60, 2 = unlocked (up to tt_fps_cap, or 1000), 3 = unlocked up to the monitor's refresh rate */
static int tt_fps_cap;          /* (1.9.3) frames a second for mode 2: 75 .. 300 from the list; 0 = unlocked */
/* the settings list (gfx_menu.c): 30, 60, 75, 90, 100, 120, 144, 165, 200, 240, 300, unlocked */
static const int tt_fps_list[12] = { 30, 60, 75, 90, 100, 120, 144, 165, 200, 240, 300, 0 };
volatile unsigned long long tt_hud_drawn_ms;   /* the HUD last drawn (0x000CF450) */
/* an in-engine scene: play's screen, no film, and no HUD drawn lately */
int tt_in_scene(void)
{
    extern __declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);
    extern int xbox_MoviePlaying(void);
    return MEM32(0x4AD050u) == 6 && !xbox_MoviePlaying() && GetTickCount64() - tt_hud_drawn_ms > 300;
}
/* testing: TT_DTSCAN=1 -- find the game's clock: guest memory words (float
 * seconds or milliseconds, or whole ms) that advance as fast as the time the
 * game reads, sampled every 2 s of it; then TT_CLOCKVA=<addr>,<kind> logs how
 * fast that clock runs against real time. */
/* testing: TT_WATCH=addr,addr,... (hex, whole numbers) -- printed with the
 * game clock every 2 s of it in play */
static void tt_valwatch(void)
{
    static int n = -1; static uint32_t ad[8]; static float last;
    float c;
    if (n < 0) {
        const char *e = getenv("TT_WATCH"); char *end;
        n = 0;
        while (e && *e && n < 8) { ad[n++] = (uint32_t)strtoul(e, &end, 16); e = *end == ',' ? end + 1 : end; if (end == e && *e != ',') break; }
    }
    if (!n || MEM32(0x4AD050u) != 6) return;
    { uint32_t w = MEM32(0x4B8B9Cu); memcpy(&c, &w, 4); }
    if (c - last < 2.0f && c >= last) return;
    last = c;
    { int i; tt_dbg("[WATCH] clock %.3f", (double)c); for (i = 0; i < n; i++) tt_dbg(" %08X=%u", ad[i], MEM32(ad[i])); tt_dbg("\n"); }
}
static void tt_dtscan(void)
{
    /* TT_DTSCAN=2: per frame, words matching this frame's step of the game
     * clock (0x4B8B9C, float seconds) as float s / float ms / x30 / x60 */
    static int on = -1; static unsigned frames; static float lc;
    static uint32_t *cand; static uint8_t *kind; static size_t nc;
    const uint32_t lo = 0x10000u, hi = 0x3FF0000u;
    float c, f; double dt;
    if (on < 0) { const char *e = getenv("TT_DTSCAN"); on = e ? atoi(e) : 0; }
    if (on != 2 || MEM32(0x4AD050u) != 6) { lc = 0; return; }
    { uint32_t w = MEM32(0x4B8B9Cu); memcpy(&c, &w, 4); }
    if (lc == 0 || c <= lc) { lc = c; return; }
    dt = (double)c - (double)lc; lc = c;
    if (dt > 0.1) return;   /* (a clamp may apply: those frames left out) */
    if (++frames < 150) return;
    if (!cand) {
        size_t cap = 1u << 22, a2;
        cand = (uint32_t *)malloc(cap * 4); kind = (uint8_t *)malloc(cap);
        for (a2 = lo; a2 < hi && nc < cap; a2 += 4) {
            uint32_t w = MEM32(a2); int k;
            memcpy(&f, &w, 4);
            for (k = 1; k <= 4; k++) {
                double m = k == 1 ? 1 : k == 2 ? 1000 : k == 3 ? 30 : 60, v = (double)f / m;
                if (v > dt * 0.97 && v < dt * 1.03) { cand[nc] = (uint32_t)a2; kind[nc] = (uint8_t)k; nc++; break; }
            }
        }
        tt_dbg("[DTSCAN] dt %.5f: %u candidates\n", dt, (unsigned)nc);
        return;
    }
    {
        size_t i, j = 0; unsigned nmiss = 0;
        for (i = 0; i < nc; i++) {
            uint32_t w = MEM32(cand[i]); int k = kind[i];
            double m = k == 1 ? 1 : k == 2 ? 1000 : k == 3 ? 30 : 60, v;
            memcpy(&f, &w, 4); v = (double)f / m;
            if (v > dt * 0.97 && v < dt * 1.03) { cand[j] = cand[i]; kind[j] = kind[i]; j++; }
            else nmiss++;
        }
        nc = j;
        if (frames % 20 == 0 || nc < 10) {
            tt_dbg("[DTSCAN] dt %.5f: %u left", dt, (unsigned)nc);
            for (i = 0; i < nc && i < 10; i++) { uint32_t w = MEM32(cand[i]); memcpy(&f, &w, 4); tt_dbg(" %08X(%d)=%g", cand[i], kind[i], (double)f); }
            tt_dbg("\n");
        }
    }
}
static void tt_delay_hook(void)
{
    extern int xbox_MoviePlaying(void);
    {   /* (1.9.3) the game's two waits on the GPU, with the GPU done -- the
         * pushbuffer run to its end and no D3D callback left to run -- for
         * 300 ms: the signal the GPU's callback gives was lost (a later
         * frame's callback wrote over it before the waiter looked, more
         * often at high frame rates), and nothing will ever give it. Given
         * here, so the game goes on:
         *  - a flush (0x000E08D0: sleeps until 0x1BB3BC reads 2) -- "Load
         *    game" sat on a black screen;
         *  - the last frame done (0x000E00E0: sleeps until 0x4B7601 reads 0)
         *    -- a hang while loading into a level. Two seconds for this one:
         *    a slow frame can keep the signal back for longer than 300 ms. */
        extern int xbox_Nv2aDrained(void);
        extern int xbox_PbCallbacksPending(void);
        extern __declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);
        static unsigned long long since[2];
        static int off = -1;
        int k, w = -1;
        if (off < 0) off = getenv("TT_NO_GPUDOG") != NULL;
        int inwait = 0;
        for (k = 0; k < 48 && w < 0 && !inwait; k++) {
            uint32_t r = MEM32(esp + 4 * k);
            if (r == 0x000E08F7u || r == 0x000E012Bu || r == 0x000E015Cu || r == 0x000E0177u) inwait = 1;
            if (off) continue;
            if (r == 0x000E08F7u && MEM32(0x1BB3BCu) != 2) w = 0;
            else if (r == 0x000E012Bu && MEM8(0x4B7601u) != 0) w = 1;
        }
        {   /* (1.9.3) time in the game's waits (the GPU's flush and frame
             * signals, the frame clock), for the log's [PERF] line */
            extern void xbox_PerfWaitNote(int inwait);
            xbox_PerfWaitNote(inwait);
        }
        if (w != 0) since[0] = 0;
        if (w != 1) since[1] = 0;
        if (w >= 0) {
            unsigned long long t = GetTickCount64();
            if (!since[w]) since[w] = t;
            else if (t - since[w] > (w ? 2000u : 300u) && xbox_Nv2aDrained() && !xbox_PbCallbacksPending()) {
                if (w == 0) {
                    tt_dbg("  [GPUWAIT] the flush wait had lost its signal (flag %u, %llu ms): given\n", MEM32(0x1BB3BCu), t - since[w]);
                    MEM32(0x1BB3BCu) = 2;
                } else {
                    tt_dbg("  [GPUWAIT] the frame wait had lost its signal (%llu ms): given\n", t - since[w]);
                    MEM8(0x4B7601u) = 0;
                }
                since[w] = 0;
            }
        }
    }
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
    else if (tt_fps_mode >= 2) {
        /* (unlocked: 1.8.1 -- unlocked throughout again; choose 60 before the scenes instead) */
        /* (1.9.3) A frame no sooner than one period after the last: the
         * frame wait is ended (the vblank counter filled) once a period,
         * not at every sleep. Mode 3: the monitor's refresh period.
         * TT_FPS_CAP=<n>: that many frames a second instead. */
        extern int xbox_DisplayRefreshHz(void);
        extern __declspec(dllimport) int __stdcall QueryPerformanceCounter(long long *);
        extern __declspec(dllimport) int __stdcall QueryPerformanceFrequency(long long *);
        static long long fq, next; static int cap = -1;
        long long now, period; int hz;
        if (cap < 0) { const char *c = getenv("TT_FPS_CAP"); cap = c ? atoi(c) : 0; QueryPerformanceFrequency(&fq); }
        /* (1.9.3) unlocked: up to 1000 frames a second -- a frame under a
         * millisecond is one the game's world does not move in */
        hz = cap > 0 ? cap : tt_fps_mode == 3 ? xbox_DisplayRefreshHz() : tt_fps_cap > 0 ? tt_fps_cap : 1000;
        if (hz < 30) hz = 1000;   /* (rate unknown) */
        period = fq / hz;
        QueryPerformanceCounter(&now);
        if (now - next > period * 4 || next - now > period * 4) next = now;   /* (a pause, or the rate changed) */
        if (now >= next) {
            MEM32(0x4B7618u) = 8;
            /* (1.9.5) At 500 frames a second and up, the next frame no sooner
             * than a period from now: catching up on a late frame used to let
             * the next few through back to back, under a millisecond apart --
             * frames the game's clock gives no whole millisecond, where its
             * world rounded the step up and animations cut it to nothing, so
             * at an unlocked rate characters ran (the Westfold intro's run)
             * with their animations behind their feet. */
            if (hz >= 500) next = now + period; else next += period;
        }
    }
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
    if (ctx == 1) { tt_dtscan(); tt_valwatch(); }
    tt_cb_last[tt_cb_n++ & 7] = ctx;
    t = GetTickCount64();
    if (ctx == 2) { tt_hold2_until = (long long)t + 250; tt_hold2_flips = g_nv2a_flips + 2; }
    else if ((long long)t < tt_hold2_until || (int32_t)(g_nv2a_flips - tt_hold2_flips) < 0) MEM32(0x1BB3BCu) = 2;
}
/* From the settings overlay. Leaving 60 for 30 puts the game's own 30 fps
 * flag back (gameplay's setting; menus switch it again themselves). */
/* (1.9.4) the same limit where frames go out (nv2a_pb_exec.c frame_limit),
 * so the menus -- which draw without the game's frame wait -- keep it too:
 * the chosen rate, 1000 unlocked, 60 at 30 / 60 (the Xbox's menus' rate) */
static void tt_fps_present_cap(void)
{
    extern volatile int xbox_frame_cap_hz;
    xbox_frame_cap_hz = tt_fps_mode >= 2 ? (tt_fps_cap > 0 ? tt_fps_cap : 1000) : 60;
}
static void tt_fps_set(int idx)
{
    int old = tt_fps_mode, mode;
    /* (1.9.3) the settings list's entry: 30, 60, a cap, or unlocked */
    if (idx < 0 || idx > 11) idx = 0;
    mode = idx == 0 ? 0 : idx == 1 ? 1 : 2;
    tt_fps_cap = idx >= 2 ? tt_fps_list[idx] : 0;
    tt_fps_mode = mode;
    tt_fps_present_cap();
    if (old == 1 && mode != 1) MEM8(0x4B75F1u) = 1;
    if (mode == 2 && tt_fps_cap) tt_dbg("  [FPS] frame-rate mode now up to %d fps\n", tt_fps_cap);
    else tt_dbg("  [FPS] frame-rate mode now %s\n", mode == 1 ? "60" : mode == 2 ? "unlocked" : "30");
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
    else if (v && (v[0] == 'r' || v[0] == 'R')) tt_fps_mode = 2;   /* (the old "refresh": unlocked) */
    else if (v && atoi(v) > 60) {   /* a frame rate from the list */
        int f = atoi(v), k;
        for (k = 2; k < 11 && tt_fps_list[k] != f; k++) {}
        tt_fps_mode = 2; tt_fps_cap = k < 11 ? f : 0;
    }
    tt_fps_present_cap();
    /* Always installed: the settings overlay can change the mode in game. */
    xbox_guest_delay_hook = tt_delay_hook;
    {
        extern void xbox_GfxSetFpsHandler(void (*fn)(int mode));
        xbox_GfxSetFpsHandler(tt_fps_set);
    }
    if (tt_fps_mode)
        tt_dbg("  [FPS] experimental frame-rate mode: %s (cap %d)\n", tt_fps_mode == 1 ? "60" : "unlocked", tt_fps_cap);
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
    case 0x000E3350u: return sub_000E3350;
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

/* ---- (1.9.5) The controller, read natively ----------------------------
 *
 * The game reads its controllers through the Xbox's input library (XAPI, in
 * the XPP section): XInitDevices starts the console's USB stack, and from
 * then on the USB driver talks to an imitation of the OHCI controller chip
 * (src/usb) -- about 1,300 trapped register accesses a second -- so that
 * XInputGetState can copy out the pad's last report. These replacements do
 * what the library's functions do, from the PC's own input (XInput, the
 * keyboard and mouse, the bindings and the settings overlay, all as
 * before: xbox_InputGetState): no USB stack is started, nothing is trapped.
 * One controller, on port 1, as the USB imitation presented.
 *
 *   0x0016989A  XInitDevices(count, types)            stdcall, 8
 *   0x0016989F  XGetDevices(type)                     stdcall, 4
 *   0x001698C1  XGetDeviceChanges(type, &ins, &rem)    stdcall, 12
 *   0x00169599  XInputOpen(type, port, slot, params)  stdcall, 16
 *   0x001695EF  XInputClose(handle)                   stdcall, 4
 *   0x001695FB  XInputGetCapabilities(handle, caps)   stdcall, 8
 *   0x001697D9  XInputGetState(handle, state)         stdcall, 8
 *   0x00169845  XInputSetState(handle, feedback)      stdcall, 8
 *
 * The pad's device type is the table at 0x00168634. TT_USB_HLE=0: the
 * library and the USB imitation, as before. */
/* (src/input/xinput_xbox.h's types, declared here: its header brings the
 * runtime's Windows definitions, which clash with this file's) */
typedef struct { uint32_t dwPacketNumber; struct { uint16_t wButtons; uint8_t bAnalogButtons[8];
    int16_t sThumbLX, sThumbLY, sThumbRX, sThumbRY; } Gamepad; } XBOX_INPUT_STATE;
extern tt_dword xbox_InputGetState(tt_dword port, XBOX_INPUT_STATE *s);
extern void xbox_InputSetVibrationDeferred(tt_dword port, unsigned short l, unsigned short r);
extern void sub_0016989A_gen(void);
extern void sub_0016989F_gen(void);
extern void sub_001698C1_gen(void);
extern void sub_00169599_gen(void);
extern void sub_001695EF_gen(void);
extern void sub_001695FB_gen(void);
extern void sub_001697D9_gen(void);
extern void sub_00169845_gen(void);
#define TT_PAD_TYPE   0x00168634u
#define TT_PAD_HANDLE 0x00F0CA00u        /* (a token: the game only passes it back) */
static int tt_usb_hle(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("TT_USB_HLE");
        on = !(v && v[0] == '0');
        tt_dbg("  [INPUT] controllers %s\n", on ? "read directly (no USB imitation)" : "through the console's USB stack (TT_USB_HLE=0)");
    }
    return on;
}
static int tt_pad_open;
void sub_0016989A(void)   /* XInitDevices */
{
    extern uint32_t xbox_ContiguousAllocatedBytes(void);
    if (!tt_usb_hle()) {
        uint32_t c0 = xbox_ContiguousAllocatedBytes();
        sub_0016989A_gen();
        tt_dbg("  [INPUT] the USB stack took %u bytes of contiguous memory\n", xbox_ContiguousAllocatedBytes() - c0);
        return;
    }
    {   /* The 8 KB the USB stack would have taken, taken anyway: the
         * contiguous memory after it keeps its addresses. Without it the
         * pushbuffer began at physical 0, which the GPU code reads as
         * "none" -- every frame's commands were lost. */
        extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment);
        xbox_ContiguousAlloc(8192, 4096);
    }
    esp += 12;
}
void sub_0016989F(void)   /* XGetDevices: the ports with a device of that type */
{
    if (!tt_usb_hle()) { sub_0016989F_gen(); return; }
    eax = MEM32(esp + 4) == TT_PAD_TYPE ? 1u : 0u;
    esp += 8;
}
void sub_001698C1(void)   /* XGetDeviceChanges: nothing comes or goes */
{
    uint32_t ins, rem;
    if (!tt_usb_hle()) { sub_001698C1_gen(); return; }
    ins = MEM32(esp + 8); rem = MEM32(esp + 12);
    if (ins) MEM32(ins) = 0;
    if (rem) MEM32(rem) = 0;
    eax = 0;
    esp += 16;
}
void sub_00169599(void)   /* XInputOpen */
{
    if (!tt_usb_hle()) { sub_00169599_gen(); return; }
    if (MEM32(esp + 4) == TT_PAD_TYPE && MEM32(esp + 8) == 0) { tt_pad_open = 1; eax = TT_PAD_HANDLE; }
    else eax = 0;
    esp += 20;
}
void sub_001695EF(void)   /* XInputClose */
{
    if (!tt_usb_hle()) { sub_001695EF_gen(); return; }
    if (MEM32(esp + 4) == TT_PAD_HANDLE) tt_pad_open = 0;
    esp += 8;
}
void sub_001695FB(void)   /* XInputGetCapabilities: an Xbox controller, every input and both motors */
{
    uint32_t c;
    if (!tt_usb_hle()) { sub_001695FB_gen(); return; }
    c = MEM32(esp + 8);
    if (MEM32(esp + 4) != TT_PAD_HANDLE) { eax = 1167; esp += 12; return; }   /* ERROR_DEVICE_NOT_CONNECTED */
    {   /* {BYTE SubType; WORD Reserved; XINPUT_GAMEPAD In; XINPUT_RUMBLE Out} */
        uint32_t k;
        MEM8(c) = 1;                     /* XINPUT_DEVSUBTYPE_GC_GAMEPAD */
        MEM8(c + 1) = 0; MEM16(c + 2) = 0;
        MEM16(c + 4) = 0x00FF;           /* the digital buttons */
        for (k = 0; k < 8; k++) MEM8(c + 6 + k) = 0xFF;
        for (k = 0; k < 4; k++) MEM16(c + 14 + 2 * k) = 0xFFFF;
        MEM16(c + 22) = 0xFFFF; MEM16(c + 24) = 0xFFFF;
    }
    eax = 0;
    esp += 12;
}
/* (co-op) the co-op's XInputGetState (below) answers player 2's virtual pad
 * and calls this for the others */
static void sub_001697D9_hle(void)   /* XInputGetState: {DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad} */
{
    static uint32_t packet;
    static uint8_t last[18];
    XBOX_INPUT_STATE st;
    uint32_t p;
    uint8_t g[18];
    int k;
    if (!tt_usb_hle()) { sub_001697D9_gen(); return; }
    p = MEM32(esp + 8);
    if (MEM32(esp + 4) != TT_PAD_HANDLE || !tt_pad_open) { eax = 1167; esp += 12; return; }
    {
        extern volatile tt_dword g_usb_report_ms;
        extern __declspec(dllimport) tt_dword __stdcall GetTickCount(void);
        g_usb_report_ms = GetTickCount() | 1;   /* (the input watchdog's "a report went out") */
    }
    memset(&st, 0, sizeof st);
    if (xbox_InputGetState(0, &st) != 0) memset(&st, 0, sizeof st);
    g[0] = (uint8_t)(st.Gamepad.wButtons & 0xFF); g[1] = (uint8_t)(st.Gamepad.wButtons >> 8);
    for (k = 0; k < 8; k++) g[2 + k] = st.Gamepad.bAnalogButtons[k];
    g[10] = (uint8_t)st.Gamepad.sThumbLX; g[11] = (uint8_t)((uint16_t)st.Gamepad.sThumbLX >> 8);
    g[12] = (uint8_t)st.Gamepad.sThumbLY; g[13] = (uint8_t)((uint16_t)st.Gamepad.sThumbLY >> 8);
    g[14] = (uint8_t)st.Gamepad.sThumbRX; g[15] = (uint8_t)((uint16_t)st.Gamepad.sThumbRX >> 8);
    g[16] = (uint8_t)st.Gamepad.sThumbRY; g[17] = (uint8_t)((uint16_t)st.Gamepad.sThumbRY >> 8);
    if (memcmp(g, last, 18)) { memcpy(last, g, 18); packet++; }
    MEM32(p) = packet;
    for (k = 0; k < 18; k++) MEM8(p + 4 + k) = g[k];
    eax = 0;
    esp += 12;
}
/* XInputSetState (handle, XINPUT_FEEDBACK*): the motors at +0x42 (left,
 * heavy) and +0x44 (right, light), passed to the PC's controller; the
 * request is done at once (Header.dwStatus at +0). TT_RUMBLE_LOG=1
 * (testing): its first calls. */
void sub_00169845(void)
{
    static int log = -1, n;
    uint32_t h = MEM32(esp + 4), fb = MEM32(esp + 8);
    if (log < 0) log = getenv("TT_RUMBLE_LOG") != NULL;
    if (h == 0xFEED0001u) { eax = 0; esp += 12; return; }   /* (coop-anything: player 2's virtual pad has no device) */
    if (!tt_usb_hle()) {
        sub_00169845_gen();
        if (log && n < 40) {
            uint32_t dev = h ? MEM32(h + 0xA3) : 0, ext = h ? MEM32(h) : 0;
            n++;
            tt_dbg("[RUMBLE] SetState h=%08X fb=%08X L=%u R=%u -> %u; dev flags %02X; ext %08X pipe %08X type %02X outsz %02X\n",
                   h, fb, fb ? MEM16(fb + 0x42) : 0, fb ? MEM16(fb + 0x44) : 0, eax,
                   dev ? MEM8(dev + 0x28) : 0, ext, h ? MEM32(h + 0x10) : 0,
                   ext ? MEM8(ext + 4) : 0, ext ? MEM8(ext + 0xD) : 0);
        }
        return;
    }
    if (h != TT_PAD_HANDLE || !fb) { eax = 1167; esp += 12; return; }
    {
        static int said;
        uint16_t l = MEM16(fb + 0x42), r = MEM16(fb + 0x44);
        if (!said && (l || r)) { said = 1; tt_dbg("  [INPUT] rumble from the game (left %u, right %u): passed to the controller\n", l, r); }
        if (log && n < 40) { n++; tt_dbg("[RUMBLE] SetState L=%u R=%u\n", l, r); }
        xbox_InputSetVibrationDeferred(0, l, r);
    }
    MEM32(fb) = 0;          /* Header.dwStatus: ERROR_SUCCESS (done) */
    eax = 997;              /* ERROR_IO_PENDING, as the library answers */
    esp += 12;
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
int coop_moves_hero_swap(uint32_t ret, uint8_t *keep);
void sub_000D2310(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp }; uint8_t keep = 0; int sw = coop_moves_hero_swap(MEM32(esp), &keep);
    if (!tt_hk(4, 0, r)) sub_000D2310_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(4, 1, r);
    eax = r[0];
    if (sw) MEM8(0x4AB69Du) = keep;
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
    /* a launch with no save goes to the main menu, not straight into the prologue: at the end of its start-up
     * screen, if this object's +0x3F8 is 0 and 0xE8300 says first run (no save), the game sets level 0, starts a new
     * game (0xD1C10) and plays (screen 6); otherwise it shows the main menu (screen 4). New Game still plays the prologue.
     * TT_FIRSTRUN_PROLOGUE keeps the game's way. */
    if (ecx >= 0x10000u && !getenv("TT_FIRSTRUN_PROLOGUE")) MEM8(ecx + 0x3F8) = 1;
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
    uint32_t pj = MEM32(esp + 0x18), pid = MEM32(esp + 0x10);   /* (the shooter, the projectile's id) */
    if (!tt_hk(22, 0, r)) sub_000D9460_gen(); else { eax = r[0]; esp = r[3]; }
    r[0] = eax; r[1] = ecx; r[2] = edx; r[3] = esp;
    tt_hk(22, 1, r);
    { extern void tt_vs_shot_launched(uint32_t pj, uint32_t pid); tt_vs_shot_launched(pj, pid); }
    eax = r[0];
}
/* a projectile's hit (this: the projectile; see coop_proj_pass) */
extern void sub_000DA180_gen(void);
void sub_000DA180(void)
{
    uint32_t r[4] = { eax, ecx, edx, esp };
    { extern int coop_proj_pass(void); if (coop_proj_pass()) { esp += 12; return; } }   /* (co-op: a hero's projectile through characters it isn't meant for) */
    /* (Versus) a hero's arrow or thrown axe: the update skips any character of the shooter's side (roles 1, 2, 0x17)
     * it touches. For its update the other hero is an enemy (role 3), then a hero again */
    extern uint32_t tt_vs_proj_foe(uint32_t proj); uint32_t foe = getenv("TT_VS_PROJFLIP") ? tt_vs_proj_foe(ecx) : 0, frole = foe ? MEM32(foe + 0x114) : 0;
    if (foe) MEM32(foe + 0x114) = 3;
    if (!tt_hk(23, 0, r)) sub_000DA180_gen(); else { eax = r[0]; esp = r[3]; }
    if (foe) MEM32(foe + 0x114) = frole;
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

/* ---- (1.9.5) the hottest Direct3D library functions as native code ---- */
#include "d3d_native.inc"

/* (1.9.5) An animated object's clock at any frame rate.
 *
 * Each frame the animation update (0x0002B420) hands every animated object
 * the frame time in whole ms, and 0x00028310 turns that into the object's
 * step: whole ms of (frame time x the animation's speed, +0x534), the
 * fraction cut off (__ftol) -- and every animation layer, blend and timed
 * event of the object moves by that step. At 30 fps a 0.9-speed animation
 * lost 0.7 of 30 ms; at 500 fps it lost 0.8 of every 1.8 ms, so animations
 * played at half speed or less, and a slow layer (a turn of the head toward
 * what a character looks at) did not move at all once the frame was short
 * enough -- the heads that did not turn in the prologue above 60 fps.
 * Here the fractions are kept per object and add up into later steps, so
 * an animation keeps its speed at any frame rate. (The step reaches the
 * original by its speed: for the one multiply the speed is set so the cut
 * gives the step wanted, then put back.) TT_NO_ANIMCARRY=1: as before. */
extern void sub_00028310_gen(void);
#define TT_ANIMC_N 2048u
static uint32_t tt_animc_key[TT_ANIMC_N];
static float tt_animc_frac[TT_ANIMC_N];
static uint32_t g_tt_anim_lastdt;
static double g_tt_anim_deliv;
void sub_00028310(void)
{
    static int off = -1;
    uint32_t obj = ecx, h, i, spw, tmpw;
    int32_t dt;
    float spd, tmp;
    double exact, whole;
    if (off < 0) off = getenv("TT_NO_ANIMCARRY") != NULL;
    dt = (int32_t)MEM32(esp + 4);
    if (obj >= 0x10000u && dt > 0 && dt < 100) {   /* for the log: how much animation time the cut loses, every 10 s of game time */
        static double want, cut, gt, swant, scut; static uint32_t calls, slow;
        float s; uint32_t w = MEM32(obj + 0x534); memcpy(&s, &w, 4);
        if (s == s && s < 1000.0f && s > -1000.0f) {
            want += (double)dt * s; cut += (double)(int32_t)((double)dt * s); calls++;
            if (s > 0.0f && s < 1.0f) { slow++; swant += (double)dt * s; scut += (double)(int32_t)((double)dt * s); }
        }
        if (g_tt_anim_lastdt != MEM32(0x4B8B94u)) { g_tt_anim_lastdt = MEM32(0x4B8B94u); gt += dt; }
        if (gt >= 10000.0) {
            tt_dbg("  [ANIMSTEP] %u object steps: the original's cut keeps %.1f%% of the animation time, %.1f%% for the %u at under full speed; here %.1f%%\n",
                   calls, want > 0 ? cut * 100.0 / want : 100.0, swant > 0 ? scut * 100.0 / swant : 100.0, slow,
                   want > 0 ? (off ? cut : g_tt_anim_deliv) * 100.0 / want : 100.0);
            want = cut = gt = swant = scut = 0; calls = slow = 0; g_tt_anim_deliv = 0;
        }
    }
    if (off || obj < 0x10000u || dt <= 0 || dt >= 100) { sub_00028310_gen(); return; }
    spw = MEM32(obj + 0x534); memcpy(&spd, &spw, 4);
    if (!(spd == spd) || spd > 1000.0f || spd < -1000.0f) { sub_00028310_gen(); return; }
    h = (obj >> 4) * 2654435761u;
    for (i = 0; i < 8; i++) {
        uint32_t k = (h + i) & (TT_ANIMC_N - 1u);
        if (tt_animc_key[k] == obj || tt_animc_key[k] == 0) { h = k; break; }
    }
    if (i == 8) { h &= TT_ANIMC_N - 1u; tt_animc_frac[h] = 0.0f; }   /* (full here: take the slot) */
    if (tt_animc_key[h] != obj) { tt_animc_key[h] = obj; tt_animc_frac[h] = 0.0f; }
    exact = (double)dt * (double)spd + (double)tt_animc_frac[h];
    whole = exact >= 0.0 ? __builtin_floor(exact) : __builtin_ceil(exact);   /* (__ftol cuts toward zero) */
    tt_animc_frac[h] = (float)(exact - whole);
    if (tt_animc_frac[h] >= 1.0f || tt_animc_frac[h] <= -1.0f) tt_animc_frac[h] = 0.0f;
    /* the speed that makes the original's cut of dt x speed give `whole` */
    tmp = (float)((whole + (whole >= 0.0 ? 0.5 : -0.5)) / (double)dt);
    if (whole == 0.0) tmp = 0.0f;
    memcpy(&tmpw, &tmp, 4);
    MEM32(obj + 0x534) = tmpw;
    sub_00028310_gen();
    g_tt_anim_deliv += (double)(int32_t)MEM32(obj + 0x530);
    if (MEM32(obj + 0x534) == tmpw) MEM32(obj + 0x534) = spw;   /* (unless the update set a new speed) */
}

/* testing: TT_ADTLOG=1 -- the ms each animated object's update (0x00026AE0) is given, by caller, against the world's */
extern void sub_00026AE0_gen(void);
void sub_00026AE0(void)
{
    static int on = -1;
    if (on < 0) on = getenv("TT_ADTLOG") != NULL;
    if (on) {
        static uint32_t callers[8], n[8], lastfr; static double got[8], world, ws;
        uint32_t c = MEM32(esp), fr = MEM32(0x4B8B94u); int32_t d = (int32_t)MEM32(esp + 4); int k;
        if (fr != lastfr) { float m, s; uint32_t w = MEM32(0x4B8B98u); memcpy(&m, &w, 4); w = MEM32(0x4B8BB0u); memcpy(&s, &w, 4); lastfr = fr; world += m; ws += s * 1000.0f; }
        for (k = 0; k < 8 && callers[k] && callers[k] != c; k++) {}
        if (k < 8) { callers[k] = c; n[k]++; got[k] += d; }
        if (world >= 10000.0) {
            for (k = 0; k < 8 && callers[k]; k++) tt_dbg("  [ADT] caller %08X: %u calls, %.0f ms given; world %.0f ms (real-time copy %.0f)\n", callers[k], n[k], got[k], world, ws);
            memset(n, 0, sizeof n); memset(got, 0, sizeof got); world = ws = 0;
        }
    }
    sub_00026AE0_gen();
}

/* testing: TT_SPLOG=1 -- who sets an actor's position (0x000977A0, thiscall x,y,z), every 10 s;
 * TT_SPLOG=addr: that object's every move, with the caller */
extern void sub_000977A0_gen(void);
void sub_000977A0(void)
{
    static int on = -1; static uint32_t one;
    if (on < 0) { const char *e = getenv("TT_SPLOG"); on = e != NULL; if (e) one = (uint32_t)strtoul(e, NULL, 16); if (e && !strcmp(e, "j")) one = 1u; else if (one < 0x10000u) one = 0; }
    if (on) {
        uint32_t c = MEM32(esp), obj = ecx;
        if (one == 1u) {   /* TT_SPLOG=1j: anything coming down off the far plateau (z over 40 to under 60), its moves for 4 s */
            static uint32_t jo[64], jt[64]; static int nj; int k; float z, oz; uint32_t w;
            w = MEM32(esp + 12); memcpy(&z, &w, 4); w = MEM32(obj + 0xF8); memcpy(&oz, &w, 4);
            for (k = 0; k < nj && jo[k] != obj; k++) {}
            if (k == nj && oz > 40.0f && z < 60.0f && nj < 64) { jo[nj] = obj; jt[nj] = tt_lvl_ms; nj++; }
            if (k < nj && tt_lvl_ms - jt[k] < 4000u) {
                float x, y; w = MEM32(esp + 4); memcpy(&x, &w, 4); w = MEM32(esp + 8); memcpy(&y, &w, 4);
                tt_dbg("[SPJ] %u %08X %08X/%08X %.3f %.3f %.3f (z %+.3f)\n", tt_lvl_ms, obj, c, MEM32(esp + 0x14), x, y, z, z - oz);
#if !defined(_WIN32)
                if (z - oz < -0.5f) {   /* a big step down: the host call stack (tools/wsym-style: offsets from tt_frametime) */
                    extern int backtrace(void **, int);
                    void *bt[24]; char b[600]; int q, n = 0, k2 = backtrace(bt, 24);
                    for (q = 1; q < k2 && n < 560; q++) n += __builtin_snprintf(b + n, sizeof b - n, " %+ld", (long)((char *)bt[q] - (char *)tt_frametime));
                    b[n] = 0; tt_dbg("[SPJS]%s\n", b);
                }
#endif
            }
        } else if (one) {
            if (obj == one) {
                float x, y, z, oz; uint32_t w;
                w = MEM32(esp + 4); memcpy(&x, &w, 4); w = MEM32(esp + 8); memcpy(&y, &w, 4); w = MEM32(esp + 12); memcpy(&z, &w, 4);
                w = MEM32(obj + 0xF8); memcpy(&oz, &w, 4);
                tt_dbg("[SP] %u %08X %.3f %.3f %.3f (z %+.3f)\n", tt_lvl_ms, c, x, y, z, z - oz);
#if !defined(_WIN32)
                {   static uint32_t f0 = 1, f1; if (f0 == 1) { const char *e = getenv("TT_SPLOG_FROM"); f0 = e ? (uint32_t)atoi(e) : 0; e = getenv("TT_SPLOG_TO"); f1 = e ? (uint32_t)atoi(e) : 0; }
                    if (tt_lvl_ms >= f0 && tt_lvl_ms <= f1) {
                        extern int backtrace(void **, int);
                        void *bt[24]; char b[600]; int q, n = 0, k2 = backtrace(bt, 24);
                        for (q = 1; q < k2 && n < 560; q++) n += __builtin_snprintf(b + n, sizeof b - n, " %+ld", (long)((char *)bt[q] - (char *)tt_frametime));
                        b[n] = 0; tt_dbg("[SPJS]%s\n", b);
                    } }
#endif
            }
        } else {
            static uint32_t callers[32], n[32]; static uint32_t t0; int k;
            for (k = 0; k < 32 && callers[k] && callers[k] != c; k++) {}
            if (k < 32) { callers[k] = c; n[k]++; }
            if (tt_lvl_ms - t0 >= 10000u) {
                for (k = 0; k < 32 && callers[k]; k++) tt_dbg("  [SPLOG] caller %08X: %u\n", callers[k], n[k]);
                memset(callers, 0, sizeof callers); memset(n, 0, sizeof n); t0 = tt_lvl_ms;
            }
        }
    }
    sub_000977A0_gen();
}

/* testing: TT_GSLOG=1 -- an actor's ground snap (0x000821F0, thiscall, 4 args) moving it more than 0.3
 * (called from the co-op's wrapper of 0x000821F0, below) */
extern void sub_000821F0_gen(void);
static void sub_000821F0_log(void)
{
    static int on = -1;
    uint32_t obj = ecx, a1 = MEM32(esp + 4), a2 = MEM32(esp + 8), a3 = MEM32(esp + 12), a4 = MEM32(esp + 16);
    float z0 = 0, z1, x, y; uint32_t w;
    if (on < 0) on = getenv("TT_GSLOG") != NULL;
    if (on) { w = MEM32(obj + 0xF8); memcpy(&z0, &w, 4); }
    sub_000821F0_gen();
    if (on) {
        w = MEM32(obj + 0xF8); memcpy(&z1, &w, 4);
        if (z1 - z0 > 0.3f || z1 - z0 < -0.3f) {
            w = MEM32(obj + 0xF0); memcpy(&x, &w, 4); w = MEM32(obj + 0xF4); memcpy(&y, &w, 4);
            float a[4]; int k; for (k = 0; k < 4; k++) { uint32_t pa = k == 0 ? a1 : k == 1 ? a2 : k == 2 ? a3 : a4; if (pa > 0x10000u && pa < 0xF0000000u) { w = MEM32(pa + (k == 0 ? 8 : 0)); memcpy(&a[k], &w, 4); } else a[k] = -999; }
            tt_dbg("[GS] %u %08X %.3f %.3f z %.3f -> %.3f  args %08X %08X %08X %08X (%.3f %.3f %.3f %.3f) v %.3f\n", tt_lvl_ms, obj, x, y, z0, z1, a1, a2, a3, a4, a[0], a[1], a[2], a[3],
                   (double)(*(float *)XBOX_PTR(obj + 0x2B4)));
        }
    }
}

/* (1.9.5) Falling at any frame rate.
 *
 * An actor's gravity step (0x000B9530, thiscall; the actor at arg+8) keeps
 * two numbers: +0x2B0, which grows more negative by 0.3 a second (scaled by
 * the frame time), and +0x2B4, the fall speed, to which +0x2B0 is added once
 * a FRAME. The drop each frame is then fall speed x frame time. At 30 fps
 * that is a fall; at 500 fps the fall speed builds 16 times as fast (a
 * second's worth of frames adds 16 times as many terms), so anything that
 * stepped off a ledge or jumped came down many times faster than it should,
 * and landed deep: the orcs leaping across the prologue's chasm ended up
 * half into the ground. Here the once-a-frame add is scaled to the frame
 * time (x frame time / 30 fps frame) -- exactly the original at 30 fps.
 * TT_NO_FALLFIX=1: as before. TT_FALLLOG=1: each landing ([LAND]). */
extern void sub_000B9530_gen(void);
void sub_000B9530(void)
{
    static int on = -1, off = -1;
    uint32_t a = 0, w; float v0 = 0, v1, z0 = 0, z1, x, y;
    static uint32_t fo[256]; static float ftop[256]; static uint32_t fn[256];
    if (on < 0) on = getenv("TT_FALLLOG") != NULL;
    if (off < 0) off = getenv("TT_NO_FALLFIX") != NULL;
    a = MEM32(MEM32(esp + 4) + 8);
    if (on && a) { w = MEM32(a + 0x2B4); memcpy(&v0, &w, 4); w = MEM32(a + 0xF8); memcpy(&z0, &w, 4); }
    if (!off && a && ecx) {
        uint32_t fl = MEM32(a + 0x110), wd = MEM32(ecx + 8);
        if ((fl & 0x400u) && (fl & 0x1000u) && (fl & 0x4000u) && wd) {
            float dt, acc, v, s;
            w = MEM32(wd + 0x32004u); memcpy(&dt, &w, 4);
            if (dt > 0.0f && dt < 0.1f) {
                /* the same step the original takes, then its fall speed set so that adding acc gives v + acc x s */
                w = MEM32(a + 0x2B0); memcpy(&acc, &w, 4);
                acc = acc - dt * 0.3f; if (acc < -1.0f) acc = -1.0f;
                w = MEM32(a + 0x2B4); memcpy(&v, &w, 4);
                s = dt * 30.0f;
                v = v + acc * s - acc;
                memcpy(&w, &v, 4); MEM32(a + 0x2B4) = w;
            }
        }
    }
    {   /* testing: TT_FALLLOG=1 also counts the steps whose move (this frame's position less the last) is under 0.01 -- which the original drops */
        static unsigned calls, tiny, moved; static uint32_t t0;
        if (on && a && (MEM32(a + 0x110) & 0x5400u) == 0x5400u) {
            float p[3], q[3]; int k; for (k = 0; k < 3; k++) { uint32_t w2 = MEM32(a + 0xF0 + 4 * k); memcpy(&p[k], &w2, 4); w2 = MEM32(a + 0x140 + 4 * k); memcpy(&q[k], &w2, 4); }
            float d = (p[0]-q[0])*(p[0]-q[0]) + (p[1]-q[1])*(p[1]-q[1]) + (p[2]-q[2])*(p[2]-q[2]);
            calls++; if (d > 0.0f) moved++; if (d > 0.0f && d < 0.0001f) tiny++;
            if (tt_lvl_ms - t0 >= 10000u) { tt_dbg("  [FALLSTEP] %u steps, %u moving, %u of them under 0.01\n", calls, moved, tiny); calls = tiny = moved = 0; t0 = tt_lvl_ms; }
        }
    }
    {   /* (1.9.5) the move this step must make before the collision takes it: 0.01 at 30 fps, and as much less as the
         * frame is shorter (a fixed 0.01 dropped most of every move above about 300 fps, so things stood still or caught
         * on the ground). The constant at 0x1702FC is the original's; it is set for the call and put back. */
        uint32_t keep = MEM32(0x1702FCu), wd = ecx ? MEM32(ecx + 8) : 0;
        float dt = 0.0f;
        if (!off && wd) { w = MEM32(wd + 0x32004u); memcpy(&dt, &w, 4); }
        if (!off && dt > 0.0f && dt < 1.0f / 30.0f) { float eps = 0.01f * dt * 30.0f; memcpy(&w, &eps, 4); MEM32(0x1702FCu) = w; }
        sub_000B9530_gen();
        MEM32(0x1702FCu) = keep;
    }
    if (on && a) {
        uint32_t h = (a >> 4) & 255;
        w = MEM32(a + 0x2B4); memcpy(&v1, &w, 4); w = MEM32(a + 0xF8); memcpy(&z1, &w, 4);
        if (fo[h] != a) { fo[h] = a; ftop[h] = z0; fn[h] = 0; }
        if (v0 >= -0.1001f) { ftop[h] = z0; fn[h] = 0; }     /* (on the ground before: a fall starts here) */
        else fn[h]++;
        if (v0 < -0.1001f && v1 >= -0.1001f && v1 <= -0.0999f && ftop[h] - z1 > 0.2f) {
            w = MEM32(a + 0xF0); memcpy(&x, &w, 4); w = MEM32(a + 0xF4); memcpy(&y, &w, 4);
            tt_dbg("[LAND] %u %08X %.3f %.3f fell %.3f -> %.3f in %u steps (v %.3f)\n", tt_lvl_ms, a, x, y, ftop[h], z1, fn[h], v0);
        }
    }
}
/* ---- co-op research probe (coop-anything, Phase 3; not part of the port) ----------------
 * Only active with TT_COOPPROBE set. Logs, through tt_dbg ([COOP] lines):
 *   0x000F1230  mission manager ([0x8D2484]) "set end state" (code at +0x4808), with caller
 *   0x000838E0  hero vtable slot 36 (0x1741C0 / 0x1743C8): the hero's die method
 *   0x00069130  camera ([0x43B164]) "player died": death cam (+0x18B) or EndMission(5)
 * plus, every ~2 s from the HUD draw: player [0x43B15C], its vtable, health (+0x2FC), place
 * (+0xF0), control flag 0x43B1C0, camera [0x43B164] and the pending end state.
 * TT_PROBE_HP=n: 10 s after the player first appears, set his health to n (once). */
#include <stdlib.h>
extern void tt_dbg(const char *fmt, ...);
extern __declspec(dllimport) tt_dword __stdcall GetTickCount(void);
static int coop_probe_on(void) { static int on = -1; if (on < 0) on = getenv("TT_COOPPROBE") != NULL; return on; }
static uint32_t coop_ms(void) { static uint32_t t0; uint32_t t = GetTickCount(); if (!t0) t0 = t; return t - t0; }

extern void sub_000F1230_gen(void);
void sub_000F1230(void)
{
    { extern int coop_end_seen; if (MEM32(esp + 4) == 5) coop_end_seen = 1; }
    if (coop_probe_on() || getenv("TT_COOP")) {
        uint32_t mm = ecx;
        tt_dbg("[COOP] %7u ms  end-state %u  (mgr %08X, was %d, caller %08X)\n", coop_ms(), MEM32(esp + 4), mm,
               mm ? (int32_t)MEM32(mm + 0x4808) : -1, MEM32(esp));
    }
    sub_000F1230_gen();
}

extern void sub_000838E0_gen(void);
void sub_000838E0(void)
{
    if (coop_probe_on() || getenv("TT_COOP")) {
        uint32_t a = ecx;
        tt_dbg("[COOP] %7u ms  hero die: actor %08X vt %08X player? %d hp %d deathcam %u caller %08X\n", coop_ms(), a, MEM32(a),
               a == MEM32(0x43B15Cu), (int32_t)MEM32(a + 0x2FC), MEM32(0x43B164u) ? MEM8(MEM32(0x43B164u) + 0x18B) : 99, MEM32(esp));
    }
    { extern int coop_p1_die_hook(void); extern uint32_t coop_p1_actor(void); if (ecx == coop_p1_actor() && ecx && coop_p1_die_hook()) { esp += 4; return; } }   /* ret */
    {   /* player 2: the level script's death check runs in his context (he is [0x43B15C] then) and calls this on him as the player: death animation to state 50, his weapons dropped, the death cam (observed). His going down is ours (coop_death_frame): skipped. TT_COOP_P2DIE lets it run. */
        extern uint32_t coop_p2_actor(void); static unsigned n; if (ecx && ecx == coop_p2_actor() && !getenv("TT_COOP_P2DIE")) { if (n++ < 4) tt_dbg("[COOPDEATH] die method on P2 (from %06X): skipped, the respawn handles him\n", MEM32(esp)); esp += 4; return; } }
    sub_000838E0_gen();
}

extern void sub_00069130_gen(void);
void sub_00069130(void)
{
    if (coop_probe_on() || getenv("TT_COOP"))
        tt_dbg("[COOP] %7u ms  camera player-died: cam %08X deathcam %u\n", coop_ms(), ecx, MEM8(ecx + 0x18B));
    sub_00069130_gen();
}

void tt_coopprobe_frame(void)
{
    static uint32_t last, seen_at, hp_done, last_p, frames;
    uint32_t now, p, c, mm;
    if (!coop_probe_on()) return;
    now = coop_ms();
    p = MEM32(0x43B15Cu); c = MEM32(0x43B164u); mm = MEM32(0x8D2484u);
    if (p != last_p) { tt_dbg("[COOP] %7u ms  player pointer %08X -> %08X (vt %08X)\n", now, last_p, p, p >= 0x10000u ? MEM32(p) : 0); last_p = p; seen_at = p ? now + 1 : 0; }
    if (p >= 0x10000u && seen_at && !hp_done && now - seen_at > 10000 && getenv("TT_PROBE_HP")) {
        hp_done = 1; tt_dbg("[COOP] %7u ms  TT_PROBE_HP: health %d -> %s\n", now, (int32_t)MEM32(p + 0x2FC), getenv("TT_PROBE_HP"));
        MEM32(p + 0x2FC) = (uint32_t)atoi(getenv("TT_PROBE_HP"));
    }
    frames++;
    if (now - last < 2000) return;
    last = now;
    { extern void tt_coopprobe_count_dump(unsigned); tt_coopprobe_count_dump(frames); frames = 0; }
    { extern void tt_coopprobe_input_dump(void); tt_coopprobe_input_dump(); }
    { extern void tt_coopprobe_cam_dump(void); tt_coopprobe_cam_dump(); }
    if (p >= 0x10000u)
        tt_dbg("[COOP] %7u ms  player %08X vt %08X hp %d pos %.1f %.1f %.1f ctl %u | cam %08X vt %08X hero %u death %u | end %d\n",
               now, p, MEM32(p), (int32_t)MEM32(p + 0x2FC), MEMF(p + 0xF0), MEMF(p + 0xF4), MEMF(p + 0xF8), MEM8(0x43B1C0u),
               c, c >= 0x10000u ? MEM32(c) : 0, c >= 0x10000u ? MEM8(c + 0x18A) : 0, c >= 0x10000u ? MEM8(c + 0x18B) : 0,
               mm >= 0x10000u ? (int32_t)MEM32(mm + 0x4808) : -1);
}


/* ---- COOP-INPUT begin (generated by local/re/gen_input_hooks.py; research only) ---- */
#define COOP_IN 128
static uint32_t coop_ifn[COOP_IN], coop_iret[COOP_IN]; static unsigned coop_icnt[COOP_IN], coop_ipl[COOP_IN]; static int coop_iused;
static void coop_in(uint32_t fn) { uint32_t r = MEM32(esp); int i; for (i = 0; i < coop_iused; i++) if (coop_ifn[i] == fn && coop_iret[i] == r) break;
    if (i == coop_iused) { if (coop_iused >= COOP_IN) return; coop_ifn[i] = fn; coop_iret[i] = r; coop_icnt[i] = coop_ipl[i] = 0; coop_iused++; }
    coop_icnt[i]++; if (ecx == MEM32(0x43B15Cu)) coop_ipl[i]++; }
void tt_coopprobe_input_dump(void)
{
    int i; tt_dbg("[COOPIN] window: %d (query, return address) pairs\n", coop_iused);
    for (i = 0; i < coop_iused; i++) tt_dbg("[COOPIN]   %08X <- %08X n=%u ecx==player %u\n", coop_ifn[i], coop_iret[i], coop_icnt[i], coop_ipl[i]);
    coop_iused = 0;
}
extern void sub_000E8360_gen(void); void sub_000E8360(void) { if (coop_probe_on()) coop_in(0x000E8360u); sub_000E8360_gen(); }
extern void sub_000E8460_gen(void); void sub_000E8460(void) { if (coop_probe_on()) coop_in(0x000E8460u); sub_000E8460_gen(); }
extern void sub_000E84D0_gen(void); void sub_000E84D0(void) { if (coop_probe_on()) coop_in(0x000E84D0u); sub_000E84D0_gen(); }
extern void sub_000E8510_gen(void); void sub_000E8510(void) { if (coop_probe_on()) coop_in(0x000E8510u); sub_000E8510_gen(); }
extern void sub_000E8540_gen(void); void sub_000E8540(void) { if (coop_probe_on()) coop_in(0x000E8540u); sub_000E8540_gen(); }
extern void sub_000E8620_gen(void); void sub_000E8620(void) { if (coop_probe_on()) coop_in(0x000E8620u); sub_000E8620_gen(); }
extern void sub_000E86D0_gen(void); void sub_000E86D0(void) { if (coop_probe_on()) coop_in(0x000E86D0u); sub_000E86D0_gen(); }
extern void sub_000E8740_gen(void); void sub_000E8740(void) { if (coop_probe_on()) coop_in(0x000E8740u); sub_000E8740_gen(); }
extern void sub_000E88B0_gen(void); void sub_000E88B0(void) { if (coop_probe_on()) coop_in(0x000E88B0u); sub_000E88B0_gen(); }
extern void sub_000E8960_gen(void); void sub_000E8960(void) { if (coop_probe_on()) coop_in(0x000E8960u); sub_000E8960_gen(); }
extern void sub_000E89B0_gen(void); void sub_000E89B0(void) { if (coop_probe_on()) coop_in(0x000E89B0u); sub_000E89B0_gen(); }
extern void sub_000E8A20_gen(void); void sub_000E8A20(void) { if (coop_probe_on()) coop_in(0x000E8A20u); sub_000E8A20_gen(); }
extern void sub_000E8AA0_gen(void); void sub_000E8AA0(void) { if (coop_probe_on()) coop_in(0x000E8AA0u); sub_000E8AA0_gen(); }
extern void sub_000E8B20_gen(void); void sub_000E8B20(void) { if (coop_probe_on()) coop_in(0x000E8B20u); sub_000E8B20_gen(); }
/* a film (0x000E8B70: its second argument 0 or 2 lets a button end it, 1 doesn't -- the game's first showing of a
 * level's film). Every film may be skipped the first time too (a playtest). TT_NOFILMSKIP: as the game */
extern void sub_000E8B70_gen(void); void sub_000E8B70(void) {
    uint32_t a1 = MEM32(esp + 4), m = MEM32(esp + 8);
    if (coop_probe_on()) coop_in(0x000E8B70u);
    tt_dbg("[FILM] %s, mode %u%s\n", a1 >= 0x10000u && a1 < 0x84000000u ? (const char *)XBOX_PTR(a1) : "?", m, m == 1 && !getenv("TT_NOFILMSKIP") ? ": may be skipped (was not)" : "");
    if (m == 1 && !getenv("TT_NOFILMSKIP")) MEM32(esp + 8) = 0;
    { extern __declspec(dllimport) tt_dword __stdcall GetTickCount(void); uint32_t t0 = (uint32_t)GetTickCount(); sub_000E8B70_gen(); tt_dbg("[FILM] over after %u ms%c", (uint32_t)GetTickCount() - t0, 10); } }
extern void sub_000D3ED0_gen(void); void sub_000D3ED0(void) { if (coop_probe_on()) coop_in(0x000D3ED0u); sub_000D3ED0_gen(); }
extern void sub_000D3F30_gen(void); void sub_000D3F30(void) { if (coop_probe_on()) coop_in(0x000D3F30u); sub_000D3F30_gen(); }
extern void sub_000D3F60_gen(void); void sub_000D3F60(void) { if (coop_probe_on()) coop_in(0x000D3F60u); sub_000D3F60_gen(); }
extern void sub_000D3F70_gen(void); void sub_000D3F70(void) { if (coop_probe_on()) coop_in(0x000D3F70u); sub_000D3F70_gen(); }
extern void sub_000D3F80_gen(void); void sub_000D3F80(void) { if (coop_probe_on()) coop_in(0x000D3F80u); sub_000D3F80_gen(); }
extern void sub_000D3F90_gen(void); void sub_000D3F90(void) { if (coop_probe_on()) coop_in(0x000D3F90u); sub_000D3F90_gen(); }
/* ---- COOP-INPUT end ---- */

/* ---- COOP-PROBE4 (research only): camera target and level actors ----
 * 0x00068DC0 camera update (thiscall on [0x43B164], called by the world update right after the
 *   player control step 0x9C380). TT_PROBE_CAMSHIFT=dx: player 1's x (+0xF0) is moved by dx for
 *   the length of the call only, then put back -- if the camera follows, it reads its target there.
 * 0x000C5E20 level loader registers each placed actor with 0x43C5E8 (arg = actor). Logged with
 *   vtable, +0x114 (1 = may become the player), +0x49C and position. */
static uint32_t coop_cam_calls;
extern void sub_00068DC0_gen(void);
void sub_00068DC0(void)
{
    static float dx = -1e9f; uint32_t p = MEM32(0x43B15Cu); float x0 = 0;
    if (dx == -1e9f) dx = getenv("TT_PROBE_CAMSHIFT") ? (float)atof(getenv("TT_PROBE_CAMSHIFT")) : 0.0f;
    if (coop_probe_on()) coop_cam_calls++;
    if (coop_probe_on() && dx != 0 && p >= 0x10000u) { x0 = MEMF(p + 0xF0); MEMF(p + 0xF0) = x0 + dx; }
    {   extern int tt_coop_camera_pre(uint32_t *saved); extern void tt_coop_camera_post(const uint32_t *saved);
        uint32_t sv[4]; int r = tt_coop_camera_pre(sv);
        if (r < 0) { eax = 0; esp += 8; return; }   /* inside player 2's swapped context: no camera update (ret 4) */
        sub_00068DC0_gen();
        if (r > 0) tt_coop_camera_post(sv);
        { extern int tt_vs_camera_after(void); if (tt_vs_camera_after() && !getenv("TT_VS_NOCAMREDO")) {   /* (Versus: ours, before the frame's view is made of it) */
            uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx, cm = MEM32(0x43B164u);
            if (cm >= 0x10000u) { ecx = cm; PUSH32(esp, 0x000684C0u); sub_000684C0(); esp = sp;
                if (MEM32(cm + 0x18) >= 0x10000u) { ecx = MEM32(cm + 0x18); PUSH32(esp, 0x00060960u); sub_00060960(); esp = sp; } }
            eax = r0; ecx = r1; edx = r2; } }
    }
    if (coop_probe_on() && dx != 0 && p >= 0x10000u) MEMF(p + 0xF0) = x0;
}
void tt_coopprobe_cam_dump(void)
{
    uint32_t c = MEM32(0x43B164u), p = MEM32(0x43B15Cu);
    if (c < 0x10000u || p < 0x10000u) return;
    tt_dbg("[COOPCAM] calls %u | player %.2f %.2f %.2f | cam+40 %.2f %.2f %.2f %.2f | +50 %.2f %.2f %.2f %.2f | +64 %.2f %.2f %.2f %.2f | +74 %.2f %.2f %.2f\n",
           coop_cam_calls, MEMF(p + 0xF0), MEMF(p + 0xF4), MEMF(p + 0xF8),
           MEMF(c + 0x40), MEMF(c + 0x44), MEMF(c + 0x48), MEMF(c + 0x4C), MEMF(c + 0x50), MEMF(c + 0x54), MEMF(c + 0x58), MEMF(c + 0x5C),
           MEMF(c + 0x64), MEMF(c + 0x68), MEMF(c + 0x6C), MEMF(c + 0x70), MEMF(c + 0x74), MEMF(c + 0x78), MEMF(c + 0x7C));
    coop_cam_calls = 0;
}
extern void sub_000C5E20_gen(void);
void sub_000C5E20(void)
{
    { extern void tt_coop_note_actor(uint32_t); tt_coop_note_actor(MEM32(esp + 4)); }
    if (coop_probe_on()) {
        uint32_t a = MEM32(esp + 4);
        if (a >= 0x10000u) {
            char nm[40]; uint32_t h = MEM32(a + 0x18); int q = 0;
            if (h >= 0x10000u && h < 0x4000000u) for (; q < 39 && MEM8(h + q) >= 0x20 && MEM8(h + q) < 0x7F; q++) nm[q] = (char)MEM8(h + q);
            nm[q] = 0;
            tt_dbg("[COOPACT] reg %08X vt %08X +114 %u +49C %08X pos %.1f %.1f %.1f player %08X name '%s'\n", a, MEM32(a), MEM32(a + 0x114), MEM32(a + 0x49C),
                   MEMF(a + 0xF0), MEMF(a + 0xF4), MEMF(a + 0xF8), MEM32(0x43B15Cu), nm); }
    }
    sub_000C5E20_gen();
}
/* ---- COOP-PROBE4 end ---- */

/* ---- COOP-P2 (coop-anything prototype, Phase 5; only with TT_COOP set) ------------------
 * Player 2 for shared-screen co-op. See games/lotr-two-towers/NOTES.md section 8.
 *
 * Pad: Xbox port 1's record in the game's pad snapshot (0x6B2758 + 0x14C) gets a virtual device
 *   handle, and the game's own XInputGetState (0x001697D9) is answered for that handle with
 *   player 2's pad: the PC's second pad (host port 1), or TT_COOP_PAD2SCRIPT for tests.
 *   The game's poll (0x000E7DB0) then builds port 1's record as for any controller.
 * Control: after the game's player control step (0x0009C380) runs for player 1, it runs again
 *   with player 2 swapped into the globals it uses: the player pointer 0x43B15C, his +0x494
 *   component 0x43B160, player 1's port 0x1CB8B4 and the per-player state SetPlayer (0x00099000)
 *   resets. The camera pointer 0x43B164 and the control flag 0x43B1C0 stay player 1's.
 * Who: TT_COOP=ally takes the level's friendly character (vtable 0x1741C0, +0x114 == 2) as a
 *   stand-in player 2 until the spawn is done.
 * TT_COOP_PAD2SCRIPT=file: lines "<from ms> <to ms> <control> <value>", times from the moment
 *   player 2 takes over; control LX LY RX RY (-32767..32767), A B X Y BLACK WHITE LT RT (0..255),
 *   UP DOWN LEFT RIGHT START BACK (any value: held).
 * Logged as [COOP2] lines. */
#define COOP_VPAD 0xFEED0001u
#define COOP_PADREC(port) (0x6B2758u + 0x14Cu * (port))
extern unsigned coop_herohits[2];
/* co-op on: TT_COOP (testing: from the start, for every level) or the main menu's Co-op (menus.c sets tt_coop_menu_on
 * when it is chosen, clears it for a solo New Game or Load Game). Off: every co-op hook stands aside (solo as retail). */
volatile int tt_coop_menu_on;
static int coop_mode(void) { static int m = -1; if (m < 0) { const char *v = getenv("TT_COOP"); m = !v ? 0 : !strcmp(v, "ally") ? 2 : !strcmp(v, "clone") ? 3 : 1; } return m ? m : tt_coop_menu_on ? 3 : 0; }
int tt_coop_active(void) { return coop_mode() != 0; }
/* ---- (1.9.7) Versus: the co-op's second hero against player 1 ----
 * A co-op game (coop_mode 3) with the heroes on opposite sides: their attack boxes hit each other (sub_000BA020), the
 * level's enemies and story scripts are kept out, and rounds are played to a best of three. On from the main menu's
 * Versus (menus.c, tt_vs_menu_on) or TT_VERSUS=1 (testing, with TT_COOP=clone). */
extern volatile int tt_vs_menu_on;
int tt_vs_on(void) { static int e = -1; if (e < 0) e = getenv("TT_VERSUS") != NULL; return coop_mode() == 3 && (e || tt_vs_menu_on); }
/* the Co-op Options (options.c): 0 difficulty (easy, medium, hard), 1 player icons, 2 camera zoom (off, normal, wide) */
extern int tt_coop_setting(int i);
/* a downed hero's wait (ms): TT_COOP_RESPAWN, else by the difficulty -- easy 15 s, medium 30 s, hard 45 s */
static uint32_t coop_respawn_wait(void)
{
    static const uint32_t w[3] = { 15000u, 30000u, 45000u };
    int d = tt_coop_setting(0);
    if (tt_vs_on()) return 0xFFFFFFFFu;   /* (Versus: no respawn: the round ends, vs_frame) */
    if (getenv("TT_COOP_RESPAWN")) return (uint32_t)strtoul(getenv("TT_COOP_RESPAWN"), NULL, 10);
    return w[d >= 0 && d < 3 ? d : d == 3 ? 2 : 1];
}
/* the difficulty's enemy damage and players' health (on top of the solo game's): easy 0.75x / 1.5x, medium 1x / 1x,
 * hard 1.5x / 0.75x. TT_COOP_DMGSCALE / TT_COOP_HPSCALE set one for a test run */
static float coop_dmg_scale(void)
{
    static const float f[3] = { 0.75f, 1.0f, 1.5f };
    int d = tt_coop_setting(0);
    if (getenv("TT_COOP_DMGSCALE")) return (float)atof(getenv("TT_COOP_DMGSCALE"));
    return f[d >= 0 && d < 3 ? d : d == 3 ? 2 : 1];
}
static float coop_hp_scale(void)
{
    static const float f[3] = { 1.5f, 1.0f, 0.75f };
    int d = tt_coop_setting(0);
    if (getenv("TT_COOP_HPSCALE")) return (float)atof(getenv("TT_COOP_HPSCALE"));
    if (tt_vs_on()) return 1.0f;   /* (Versus: even health, vs_frame) */
    return f[d >= 0 && d < 3 ? d : d == 3 ? 2 : 1];
}
static uint32_t coop_p2, coop_p2_ally, coop_p2_t0;
static unsigned coop_level_n;   /* levels loaded (coop_level_reset): once-a-level code compares it */
static int coop_depth;
static int coop_triglog_on(void) { static int tl = -1; if (tl < 0) { extern int fclose(void *); void *tf = fopen("triglog.on", "rb"); tl = getenv("TT_COOP_TRIGLOG") || tf; if (tf) fclose(tf); } return tl; }   /* (or a file triglog.on in the game folder: a playtest) */
static uint32_t coop_p2_wpnarg_ent, coop_p2_wpnarg_k, coop_p2_wpnarg_v, coop_p2_wpnarg_p1, coop_p2_wpnarg_learn; static unsigned coop_p2_wpnarg_lk;   /* (coop_replay: player 2's weapon argument while it is patched) */
static uint32_t coop_lurtz_hit; uint32_t coop_lurtz_p2_hits;   /* (sub_000849F0: player 2's hit on Lurtz, handled as player 1's) */
static int coop_head_cmd;   /* a heading command run in player 2's context (coop_replay): its lookup of player 1 answers him (sub_000B4690) */
int coop_in_p2_ctx(void) { return coop_depth; }
/* bought moves per player (player 2 only does what he bought). A hero's upgrades are bytes at 0x4AC5B0 + hero * 0x50
 * (0xD2710; 0xD25B0 asks one, 0xD2570 sets one: the shop's buy), and the player's move picks (0x99A90, 0x9A1F0-0x9C37E:
 * e.g. 0x9B950 -- upgrade 15 bought: move 0xE9, else 0xE7) ask the hero getter 0xD2310 ([0x4AB69D]) for whose upgrades
 * and which hero's branch. Inside player 2's context they got player 1's hero: Gimli moved with player 1's upgrades.
 * For those callers there, the getter sees player 2's hero byte for the call. Not for the record (0xD26F0: his shadow
 * record sits in player 1's slot) nor a guest hero (no byte). TT_COOP_NOP2MOVES: off. */
/* (research) TT_COOP_MOVELOG: each upgrade asked inside player 2's context (0x9CAB0, thiscall on a hero's block, the
 * upgrade; ret 4): whose block, which upgrade, the answer -- once per (hero, upgrade, answer) */
extern void sub_0009CAB0_gen(void);
void sub_0009CAB0(void)
{
    static int on = -1; static uint32_t seen[128]; static int ns; uint32_t blk = ecx, it = MEM32(esp + 4), ret = MEM32(esp);
    if (on < 0) on = getenv("TT_COOP_MOVELOG") != 0;
    sub_0009CAB0_gen();
    if (tt_vs_on() && blk >= 0x4AC5B0u && blk < 0x4AC800u && !getenv("TT_VS_OWNUPGRADES")) eax = (eax & ~0xFFu) | 1u;
    if (getenv("TT_COOP_TESTALLMOVES") && blk >= 0x4AC5B0u && blk < 0x4AC800u) eax = (eax & ~0xFFu) | 1u;   /* (test) every move bought, both heroes */   /* (Versus: fixed loadouts -- every hero with every move bought) */
    if (on && coop_depth && blk >= 0x4AC5B0u && blk < 0x4AC800u && ns < 128) {
        uint32_t key = ((blk - 0x4AC5B0u) / 0x50u) << 16 | (it & 0xFF) << 8 | (eax & 0xFF); int i;
        for (i = 0; i < ns && seen[i] != key; i++) ;
        if (i == ns) { seen[ns++] = key; tt_dbg("[COOPMOVES] player 2 asked: hero %u's upgrade %u = %u (from %06X)\n", (blk - 0x4AC5B0u) / 0x50u, it, eax & 0xFF, ret); }
    }
}
int coop_moves_hero_swap(uint32_t ret, uint8_t *keep)
{
    static int off = -1; static unsigned n; extern int tt_heroes_retail_byte(const char *skin); extern const char *tt_coop_p2_char_now; int b;
    if (!coop_depth) return 0;
    if (!((ret >= 0x99A90u && ret < 0x99AABu) || (ret >= 0x9A1F0u && ret < 0x9A594u) || (ret >= 0x9AA90u && ret < 0x9B170u) ||
          (ret >= 0x9B4D0u && ret < 0x9BE96u) || (ret >= 0x9BFD0u && ret < 0x9C37Eu))) return 0;
    if (off < 0) off = getenv("TT_COOP_NOP2MOVES") != 0;
    b = tt_heroes_retail_byte(tt_coop_p2_char_now);
    if (off || b < 1 || b > 4 || b == MEM8(0x4AB69Du)) return 0;
    *keep = MEM8(0x4AB69Du); MEM8(0x4AB69Du) = (uint8_t)b;
    if ((n++ & 1023) == 0) tt_dbg("[COOPMOVES] player 2's move pick (from %06X) asks his own hero %d's upgrades, not player 1's hero %d (%u so far)\n", ret, b, *keep, n);
    return 1;
}
static int coop_in_attach;   /* inside @AttachToBone */
static int coop_attach_bone_seen;   /* its slot (the "bone": 0 the main hand, 1 the other, 2 and 3 carried) */
static int coop_resolved, coop_replaying;
static int coop_wid_on; static uint32_t coop_wid_obj, coop_wid_snap[0x200];   /* (research, TT_COOP_WLOG) @SetWeaponID's effect */
static int coop_in_obb; static uint32_t coop_obb_obj;
static uint32_t coop_attach_child_seen, coop_new_weapon_now;
static uint32_t coop_wpn_copy[4], coop_wpn_orig[4]; static int coop_nwpn;   /* player 2's weapon copies and player 1's originals */   /* the weapon player 1's @AttachToBone resolved; the twin's copy during the replay */
static uint32_t coop_p2w[16]; static int coop_np2w;   /* every weapon built for player 2 this level (copies and his extra slots) */
static int coop_p2w_slot[16];   /* (each one's slot, @AttachToBone's second argument) */
#define COOP_MAXCHARS 256   /* (24 filled up in Gates of Moria before its enemies came: the P2 Bot saw none and they gave no kill credit, in a playtest) */
static uint32_t coop_chars[COOP_MAXCHARS]; static int coop_nchars;   /* the level's characters of vtable 0x1741C0 */
static unsigned coop_n_step, coop_n_enter, coop_n_leave;   /* (diagnostics) */
/* research: which call moves player 1 (outermost tracked call only) */
static uint32_t coop_now(void);
int coop_phase;   /* COOP-TRACE: 1 inside player 1's slot 1 (or control step), 2 inside player 2's */
static int coop_s1d;
static struct { uint32_t ra, obj, id; } coop_lk[64]; static unsigned coop_lkn;   /* (research) the last object lookups (0xB4690), dumped at an enemy hit */
static void coop_lk_dump(void)
{
    unsigned i; for (i = coop_lkn >= 32 ? coop_lkn - 32 : 0; i < coop_lkn; i++) { uint32_t o = coop_lk[i & 63].obj, h = o >= 0x10000u ? MEM32(o + 0x18) : 0; char nm[40]; int q;
        for (q = 0; q < 39 && h >= 0x10000u && MEM8(h + q) >= 32 && MEM8(h + q) < 127; q++) nm[q] = (char)MEM8(h + q);
        nm[q] = 0; tt_dbg("[COOPLK]   lookup from %06X -> %08X vt %08X '%s'%s\n", coop_lk[i & 63].ra, o, o >= 0x10000u ? MEM32(o) : 0, nm, o == MEM32(0x43B15Cu) ? " (P1)" : o == coop_p2 && o ? " (twin)" : ""); }
}
static int imp_rule(int32_t h0, int32_t mx, int32_t dmg);
static int coop_p1d; static uint32_t coop_p1fn; static float coop_p1pre[2];
static struct { uint32_t self; int32_t hp; } coop_hpg[64];
static struct { uint32_t self; int32_t hp; float s; } coop_hps[64];   /* the last hero standing (the game's own hit, death and game over): his health divided by the damage scale for the hit, multiplied back after */
static struct { uint32_t fn; unsigned n; float d; } coop_p1mv[40]; static int coop_p1mvn;
void tt_coop_p1_pre(uint32_t self, uint32_t fn)
{
    if (!coop_mode()) { if (coop_p1d >= 0 && coop_p1d < 64) coop_hpg[coop_p1d].self = 0; coop_p1d++; coop_p1fn = 0; return; }   /* solo: nothing of ours (the depth still counted: tt_coop_p1_post) */
    if (fn == 0x0008C850u && self >= 0x10000u && MEM32(self) == 0x174980u) {   /* (research) a weapon's stats: argument and caller */
        static int n; uint32_t h = MEM32(self + 0x18); if (n++ < 30) tt_dbg("[COOPWSTATS] 0x8C850 on weapon %08X '%s' arg %08X caller %08X %08X %08X\n", self, h >= 0x10000u ? (const char *)XBOX_PTR(h) : "?", MEM32(esp + 8), MEM32(esp + 4), MEM32(esp + 12), MEM32(esp + 16));
    }
    if (fn == 0x00084220u && self >= 0x10000u && MEM32(self + 0x114) == 3) {   /* (research) an enemy is hit: who called */
        static int n; uint32_t k, cs[30] = { 0 }; int m = 0;
        for (k = 0; k < 0x800 && m < 30; k += 4) { uint32_t v = MEM32(esp + k); if (v >= 0x12000u && v < 0x170000u) cs[m++] = v; }
        if (n++ < 12) tt_dbg("[COOPENEMYHIT] slot 42 on enemy %08X (hp %d) args %08X %08X | player %08X depth %d | stack %X %X %X %X %X %X %X %X %X %X\n", self, (int32_t)MEM32(self + 0x2FC), MEM32(esp + 8), MEM32(esp + 12), MEM32(0x43B15Cu), coop_depth,
                           cs[0], cs[1], cs[2], cs[3], cs[4], cs[5], cs[6], cs[7], cs[8], cs[9]);
        if (n <= 2) coop_lk_dump();
        if (n <= 3) tt_dbg("[COOPENEMYHIT]   deeper: %X %X %X %X %X %X %X %X %X %X | %X %X %X %X %X %X %X %X %X %X\n", cs[10], cs[11], cs[12], cs[13], cs[14], cs[15], cs[16], cs[17], cs[18], cs[19], cs[20], cs[21], cs[22], cs[23], cs[24], cs[25], cs[26], cs[27], cs[28], cs[29]);
    }
    if (fn == 0x00084220u && self >= 0x10000u && coop_p2 && (self == coop_p2 || (self == MEM32(0x43B15Cu) && !coop_depth))) { coop_herohits[self == coop_p2]++; }   /* hits on each hero, for the respawn check */
    if (fn == 0x00084220u && self >= 0x10000u && (self == coop_p2 || self == MEM32(0x43B15Cu))) {   /* (research) slot 42 = hit/damage */
        static int n; if (n++ < 30) tt_dbg("[COOPHIT] slot 42 on %s %08X: args %08X %08X, hp %d, caller %08X, player now %08X\n", self == coop_p2 ? "P2" : "P1", self, MEM32(esp + 8), MEM32(esp + 12), (int32_t)MEM32(self + 0x2FC), MEM32(esp + 4), MEM32(0x43B15Cu));
    }
    if (fn == 0x000843F0u && self >= 0x10000u && (self == coop_p2 || self == MEM32(0x43B15Cu))) {   /* slot 1's activity gate */
        static uint32_t last[2]; int w = self == coop_p2; uint32_t now = coop_now();
        if (now - last[w] > 1000) { last[w] = now; tt_dbg("[COOPGATE1] %s %08X slot 1: +464 %u +110 %08X (bit 2000 %u) +11A %u +11C %08X\n", w ? "P2" : "P1", self, MEM8(self + 0x464), MEM32(self + 0x110), (MEM32(self + 0x110) >> 13) & 1, MEM8(self + 0x11A), MEM32(self + 0x11C)); }
    }
    if (coop_p1d < 64) {   /* player 2's health guard (slot 42 = the hit): see tt_coop_p1_post */
        coop_hpg[coop_p1d].self = 0; coop_hps[coop_p1d].self = 0;
        extern int coop_hero_down(int); extern uint32_t coop_p1_actor(void);   /* player 1 too, while player 2 is up (with both down his death is the game's own: game over) */
        if (fn == 0x00084220u && coop_p2 && self >= 0x10000u && (self == coop_p2 || (self == coop_p1_actor() && !coop_hero_down(1))) && coop_mode() == 3 && !getenv("TT_COOP_NOHPGUARD")) {
            coop_hpg[coop_p1d].self = self; coop_hpg[coop_p1d].hp = (int32_t)MEM32(self + 0x2FC);
            MEM32(self + 0x2FC) = (uint32_t)(coop_hpg[coop_p1d].hp + 100000); }
        else if (fn == 0x00084220u && coop_p2 && self >= 0x10000u && self == coop_p1_actor() && coop_hero_down(1) && !coop_hero_down(0) && coop_mode() == 3) {
            float sc = coop_dmg_scale(); int32_t h0 = (int32_t)MEM32(self + 0x2FC);
            if (sc > 0.01f && sc != 1.0f && h0 > 0) { int32_t t = (int32_t)(h0 / sc + 0.5f); coop_hps[coop_p1d].self = self; coop_hps[coop_p1d].hp = t; coop_hps[coop_p1d].s = sc; MEM32(self + 0x2FC) = (uint32_t)(t < 1 ? 1 : t); }
        }
    }
    uint32_t p1 = MEM32(0x43B15Cu);
    if (fn == 0x000843F0u && !coop_phase && self >= 0x10000u) {   /* slot 1: count its callees by hero */
        if (self == coop_p2 && coop_p2) { coop_phase = 2; coop_s1d = coop_p1d + 1; }
        else if (self == p1) { coop_phase = 1; coop_s1d = coop_p1d + 1; }
    }
    if (coop_p1d++ || self != p1 || p1 < 0x10000u || coop_depth) { if (coop_p1d == 1) coop_p1fn = 0; return; }
    coop_p1fn = fn; coop_p1pre[0] = MEMF(p1 + 0xF0); coop_p1pre[1] = MEMF(p1 + 0xF4);
}
void tt_coop_p1_post(void)
{
    uint32_t p1 = MEM32(0x43B15Cu);
    if (coop_p1d >= 1 && coop_p1d <= 64 && coop_hpg[coop_p1d - 1].self) {   /* player 2's health guard: the hit ran with his health raised by 100000, so it never saw him at 0 (at 0 it starts the death animation, 44/45 -> 50, which drops his weapons and is not undone by the respawn, observed). The real damage applied here; at 0 or less he is down at 1 health, as player 1 is */
        extern int coop_hero_down(int); extern void coop_p2_down_now(void); extern void coop_p1_down_now(void);
        uint32_t a = coop_hpg[coop_p1d - 1].self; int32_t h0 = coop_hpg[coop_p1d - 1].hp, now = (int32_t)MEM32(a + 0x2FC), dmg = h0 + 100000 - now, h;
        coop_hpg[coop_p1d - 1].self = 0;
        if (dmg < 0) dmg = 0;
        { extern int tt_impossible(void); if (dmg > 0 && tt_impossible()) { int32_t mx = (int32_t)MEM32(a + 0x300); dmg = h0 - imp_rule(h0, mx, dmg); if (dmg < 1) dmg = 1; } else   /* (Impossible: the second blow fells) */
        if (dmg > 0) { int32_t d0 = dmg; dmg = (int32_t)(dmg * coop_dmg_scale() + 0.5f); if (dmg < 1) dmg = 1;
            { static int n; if (d0 != dmg && n++ < 6) tt_dbg("[COOPLEVEL] enemy damage %d -> %d (x%.2f)\n", d0, dmg, coop_dmg_scale()); } } }
        int w = a == coop_p2;
        { extern void tt_vs_real_hit(int victim); if (dmg > 0) tt_vs_real_hit(w); }   /* (a hit of 0: the arena's hero takes none -- judged instead) */
        { extern int tt_vs_no_blows(void); extern int tt_vs_dmg_scale_pct(int victim); extern int tt_vs_shield_take(int victim, int dmg); if (tt_vs_no_blows()) dmg = 0; else dmg = tt_vs_shield_take(w, dmg * tt_vs_dmg_scale_pct(w) / 100); }
        {   /* (1.9.7) a kick (the knock back, B) all but harmless: it throws him back, the blows do the harm -- at most
             * TT_VS_KICKPCT (2) per cent of his health a kick, whatever the game's contact said (Eomer's felled the
             * Uruk-hai at a blow, reported) */
            extern int tt_vs_on(void); extern int tt_vs_last_box_kick(int who);
            if (tt_vs_on() && dmg > 0 && tt_vs_last_box_kick(w ^ 1)) { int32_t mx = (int32_t)MEM32(a + 0x300), cap = mx * (getenv("TT_VS_KICKPCT") ? atoi(getenv("TT_VS_KICKPCT")) : 2) / 100;
                if (dmg > cap) { static int n; if (n++ < 10) tt_dbg("[VS] P%d: a kick's %d held to %d\n", w + 1, dmg, cap); dmg = cap; } }
        }
        { extern int tt_vs_absorb(int v); if (tt_vs_absorb(w)) { static int n; if (n++ < 10) tt_dbg("[VS] P%d: the game's hit of a bolt (%d) not counted: ours is\n", w + 1, dmg); dmg = 0; } }
        h = coop_hero_down(w) ? h0 : h0 - dmg;   /* while down: no damage */
        { extern void tt_vs_note_hit(int victim, int dmg, int ranged); extern int tt_vs_on(void); if (tt_vs_on() && !coop_hero_down(w)) tt_vs_note_hit(w, dmg, 0); }
        { static int n; if (n++ < 12 || (h <= 0 && n < 40)) tt_dbg("[COOPDEATH] hit on P%d: health %d, damage %d -> %d%s\n", w ? 2 : 1, h0, dmg, h, h <= 0 ? " (down at 1)" : ""); }
        if (h <= 0) { MEM32(a + 0x2FC) = 1; if (!coop_hero_down(w)) { if (w) coop_p2_down_now(); else coop_p1_down_now(); } }
        else MEM32(a + 0x2FC) = (uint32_t)h;
    }
    if (coop_p1d >= 1 && coop_p1d <= 64 && coop_hps[coop_p1d - 1].self) {   /* the last hero standing: his health back to scale (0 or less: the game's own death) */
        uint32_t a = coop_hps[coop_p1d - 1].self; int32_t t = coop_hps[coop_p1d - 1].hp, now = (int32_t)MEM32(a + 0x2FC);
        coop_hps[coop_p1d - 1].self = 0;
        if (now > 0) { int32_t h = (int32_t)(now * coop_hps[coop_p1d - 1].s + 0.5f), h0 = (int32_t)(t * coop_hps[coop_p1d - 1].s + 0.5f);
            if (h > h0) h = h0; if (h < 1) h = 1; MEM32(a + 0x2FC) = (uint32_t)h;
            { static int n; if (n++ < 6) tt_dbg("[COOPLEVEL] last hero hit: damage %d scaled x%.2f, health -> %d\n", t - now, coop_hps[coop_p1d - 1].s, h); } }
    }
    if (coop_phase && coop_s1d && coop_p1d == coop_s1d) { coop_phase = 0; coop_s1d = 0; }
    if (--coop_p1d || !coop_p1fn || p1 < 0x10000u) return;
    { float dx = MEMF(p1 + 0xF0) - coop_p1pre[0], dy = MEMF(p1 + 0xF4) - coop_p1pre[1], d = dx * dx + dy * dy; int i;
      if (d > 1e-8f) { for (i = 0; i < coop_p1mvn && coop_p1mv[i].fn != coop_p1fn; i++) ; if (i == coop_p1mvn && coop_p1mvn < 40) { coop_p1mv[i].fn = coop_p1fn; coop_p1mv[i].n = 0; coop_p1mv[i].d = 0; coop_p1mvn++; } if (i < 40) { coop_p1mv[i].n++; coop_p1mv[i].d += d; } } }
    coop_p1fn = 0;
}
void tt_coop_p1_dump(void)
{
    int i; for (i = 0; i < coop_p1mvn; i++) tt_dbg("[COOP2]          P1 moved by %08X: %u calls, sum of squared steps %.4f\n", coop_p1mv[i].fn, coop_p1mv[i].n, coop_p1mv[i].d);
    coop_p1mvn = 0;
}static uint32_t coop_cur_fn; static float coop_pre[3];
static struct { uint32_t fn; unsigned n; float d; } coop_mv[32]; static int coop_mvn;   /* which of player 2's methods move him */
static uint32_t coop_dbrec; static int coop_dbrec_seen;
static uint32_t coop_world, coop_rec, coop_key, coop_last_char, coop_hero_sel, coop_twin, coop_first_char;   /* player 1's placement record, for the clone */
/* player 2's character type (+0x2F0) his own hero's: 0x0008C850(type) sets health, max health, ammo +0x304 and its max
 * +0x306 from it. The level script's @SetEnemyID (repeated on the twin) and player 1's database record gave him player
 * 1's (Legolas 7: 60 arrows): player 2 had player 1's ammo -- the "doubled and shared" ammo seen in play. Measured (TT_COOP_NEARLOG):
 * Aragorn 0 (30), Legolas 7 (60), Gimli 8 (25). Isildur and the extra heroes: unchanged. TT_COOP_NOP2TYPE: off. */
static void coop_p2_own_type(const char *when)
{
    extern const char *tt_coop_p2_char_now; extern uint32_t tt_gcall(uint32_t va, uint32_t thisptr, int n, const uint32_t *args);
    const char *w = tt_coop_p2_char_now; int ty = -1; uint32_t a1, was;
    if (!w || coop_twin < 0x10000u || getenv("TT_COOP_NOP2TYPE")) return;
    ty = !strcmp(w, "aragorn") ? 0 : !strcmp(w, "legolas") ? 7 : !strcmp(w, "gimli") ? 8 : -1;
    if (ty < 0 || MEM32(coop_twin + 0x2F0) == (uint32_t)ty) return;
    was = MEM32(coop_twin + 0x2F0); a1 = (uint32_t)ty;
    tt_gcall(0x0008C850u, coop_twin, 1, &a1);
    tt_dbg("[COOP2] twin's type %u -> %d (%s, %s): health %d, ammo %d/%d\n", was, ty, w, when, (int32_t)MEM32(coop_twin + 0x2FC), (int16_t)MEM16(coop_twin + 0x304), (int16_t)MEM16(coop_twin + 0x306));
}
/* player 2's weapons' id (+0x2F4, set by @SetWeaponID; 0x0006DD70(id) gives the weapon's data: +0x44/+0x48 its blade's
 * extent, the attack box's reach in 0x000BE0D0). His copies are replayed with player 1's id, so player 2 Gimli swung
 * with Legolas' knife (reach 1.5, Gimli's axe 2.25): his attack boxes touched 2 times in 278 tests (player 1 Gimli
 * 103 in 261), so few hits and no rating meter. Ids by slot, observed in Balin's Tomb (TT_COOP_WLOG): Aragorn sword
 * 0x01, dagger (slot 3) 0x1D; Legolas knives 0x1F, dagger (slot 2) 0x1D; Gimli axe 0x17. Isildur: unknown (left as
 * replayed). A guest: Aragorn's. Player 2 Legolas' bow copies (slot 7) and arrow (slot 10, coop_p2_arrow): player 1 Legolas'
 * bow 0x27, arrow 0x09 (TT_COOP_TAKELOG at the Gates of Moria); the copies had none (FFFFFFFF). TT_COOP_NOWPNID: off. */
static int coop_p2_p1wpn;   /* this level, player 2's weapons are player 1's (their models and ids), whatever his hero */
static uint32_t coop_p2_wid(int b)
{
    static const uint32_t ids[3][4] = { { 0x01u, 0, 0, 0x1Du }, { 0x1Fu, 0x1Fu, 0x1Du, 0 }, { 0x17u, 0, 0, 0 } };
    extern const char *tt_coop_p2_char_now; const char *w = tt_coop_p2_char_now;
    if (!w || b < 0 || coop_p2_p1wpn) return 0;   /* (player 1's weapons: their ids as replayed) */
    if (b == 7 || b == 10) return !strcmp(w, "legolas") ? (b == 7 ? 0x27u : 0x09u) : 0;
    if (b > 3) return 0;
    if (!strcmp(w, "isildur")) return 0;
    return ids[!strcmp(w, "legolas") ? 1 : !strcmp(w, "gimli") ? 2 : 0][b];
}
static void coop_p2_weapon_ids(const char *when)
{
    int i; if (getenv("TT_COOP_NOWPNID")) return;
    for (i = 0; i < coop_np2w; i++) {
        uint32_t w = coop_p2w[i], id = coop_p2_wid(coop_p2w_slot[i]);
        if (w < 0x10000u || !id || MEM32(w) != 0x174980u || MEM32(w + 0x2F4) == id) continue;
        tt_dbg("[COOPWEAPON] player 2's %08X (slot %d) id %X -> %X (%s)\n", w, coop_p2w_slot[i], MEM32(w + 0x2F4), id, when);
        MEM32(w + 0x2F4) = id;
    }
}
static uint32_t coop_now(void) { extern __declspec(dllimport) tt_dword __stdcall GetTickCount(void); return (uint32_t)GetTickCount(); }

/* -- player 2's pad -- */
typedef struct { uint16_t buttons; uint8_t analog[8]; int16_t lx, ly, rx, ry; } CoopPad;
typedef struct { uint32_t packet; CoopPad pad; } CoopHostState;
static struct { uint32_t a, b; char what[8]; int v; } coop_ps[256];
static int coop_psn = -1;
static void coop_pad2(CoopPad *o)
{
    memset(o, 0, sizeof *o);
    if (coop_psn < 0) {
        extern void *fopen(const char *, const char *); extern int fclose(void *); extern size_t fread(void *, size_t, size_t, void *);
        const char *fn = getenv("TT_COOP_PAD2SCRIPT"); void *fp = fn ? fopen(fn, "rb") : NULL;
        static char txt[16384]; size_t n = 0; char *s, *e;
        coop_psn = 0;
        if (fp) { n = fread(txt, 1, sizeof txt - 1, fp); fclose(fp); }
        txt[n] = 0; s = txt;
        while (coop_psn < 256 && *s) {   /* "<from> <to> <control> <value>" per line */
            int k = 0;
            while (*s == 32 || *s == 9 || *s == 13 || *s == 10) s++;
            if (!*s) break;
            coop_ps[coop_psn].a = (uint32_t)strtoul(s, &e, 10); if (e == s) break; s = e;
            coop_ps[coop_psn].b = (uint32_t)strtoul(s, &e, 10); if (e == s) break; s = e;
            while (*s == 32 || *s == 9) s++;
            while (*s > 32 && k < 7) coop_ps[coop_psn].what[k++] = *s++;
            coop_ps[coop_psn].what[k] = 0;
            coop_ps[coop_psn].v = (int)strtol(s, &e, 10); if (e == s) break; s = e;
            coop_psn++;
        }
        tt_dbg("[COOP2] pad 2: %s (%d script lines)\n", fn ? fn : "host port 1", coop_psn);
    }
    if (getenv("TT_COOP_PAD2SCRIPT")) {
        uint32_t t = coop_p2_t0 ? coop_now() - coop_p2_t0 : 0; int i;
        static const char *an[8] = { "A", "B", "X", "Y", "BLACK", "WHITE", "LT", "RT" };
        static const char *dn[6] = { "UP", "DOWN", "LEFT", "RIGHT", "START", "BACK" };
        if (!coop_p2_t0) return;
        for (i = 0; i < coop_psn; i++) {
            int k;
            if (t < coop_ps[i].a || t >= coop_ps[i].b) continue;
            if (!strcmp(coop_ps[i].what, "LX")) o->lx = (int16_t)coop_ps[i].v;
            else if (!strcmp(coop_ps[i].what, "LY")) o->ly = (int16_t)coop_ps[i].v;
            else if (!strcmp(coop_ps[i].what, "RX")) o->rx = (int16_t)coop_ps[i].v;
            else if (!strcmp(coop_ps[i].what, "RY")) o->ry = (int16_t)coop_ps[i].v;
            for (k = 0; k < 8; k++) if (!strcmp(coop_ps[i].what, an[k])) o->analog[k] = (uint8_t)coop_ps[i].v;
            for (k = 0; k < 6; k++) if (!strcmp(coop_ps[i].what, dn[k])) o->buttons |= (uint16_t)(1u << k);
        }
    } else {
        CoopHostState s;
        if (xbox_InputGetState(1, (XBOX_INPUT_STATE *)&s) == 0) *o = s.pad;
    }
    if (getenv("TT_COOP_PAD2SEEK") && coop_p2 >= 0x10000u && coop_p2_t0) {   /* bot test: steer player 2 to his target and attack */
        extern void coop_seek(CoopPad *o); extern void coop_seek_near(CoopPad *o);
        if (!strcmp(getenv("TT_COOP_PAD2SEEK"), "2")) coop_seek_near(o); else coop_seek(o);
    }
    { extern void coop_p2bot(CoopPad *o); coop_p2bot(o); }   /* the Co-op menu's P2 Bot */
}
/* closed-loop steering bot (test only): TT_COOP_PAD2SEEK=1 for player 2 (pad 2), TT_COOP_P1SEEK=1 for player 1
 * (port 0's pad reads). Goal: the hero's target (component +0x98), else the nearest living enemy. The stick is
 * camera-relative, so each bot learns the stick-to-world rotation: every 250 ms, if its hero moved more than 0.25
 * with the stick held, the angle between stick and actual motion goes into a running average. Within TT_COOP_SEEKR
 * (default 4.6; bodies stop ~4 apart, centre to centre) the stick is let go (a push cancels the swing) and the attack
 * button is pressed 200 ms every 600 ms (TT_COOP_SEEKBTN = X, Y or XY; X alone makes no attack boxes, for player 1
 * too). Logged as [COOPSEEK] once a second per bot. */
typedef struct { float c, s, px0, py0, sa0; uint32_t t0, tl, tlog; int held, btn; unsigned samples, taps; } CoopSeek;
static CoopSeek coop_sk[2] = { { 1, 0 }, { 1, 0 } };
static void coop_seek_for(CoopPad *o, uint32_t hero, int who)
{
    CoopSeek *k = &coop_sk[who];
    uint32_t c = MEM32(hero + 0x494), g = c >= 0x10000u ? MEM32(c + 0x98) : 0, now = coop_now(); int i;
    {   /* TT_COOP_SEEKWIN1 / TT_COOP_SEEKWIN2 = "from-to" ms after player 2 takes over: the bot is idle outside it */
        const char *wv = getenv(who ? "TT_COOP_SEEKWIN2" : "TT_COOP_SEEKWIN1");
        if (wv && coop_p2_t0) { char *e; uint32_t a = (uint32_t)strtoul(wv, &e, 10), b = *e == '-' ? (uint32_t)strtoul(e + 1, NULL, 10) : 0xFFFFFFFFu, t = now - coop_p2_t0; if (t < a || t >= b) return; }
    }
    float px = MEMF(hero + 0xF0), py = MEMF(hero + 0xF4), dx, dy, d, R;
    { static float r = -1; if (r < 0) { const char *v = getenv("TT_COOP_SEEKR"); r = v ? (float)atof(v) : 4.6f; } R = r; }
    if (g < 0x10000u || MEM32(g + 0x114) != 3 || (int32_t)MEM32(g + 0x2FC) <= 0) { float bd = 1e18f; g = 0;
        for (i = 0; i < coop_nchars; i++) { uint32_t e = coop_chars[i]; float ex, ey; if (MEM32(e + 0x114) != 3 || (int32_t)MEM32(e + 0x2FC) <= 0) continue;
            ex = MEMF(e + 0xF0) - px; ey = MEMF(e + 0xF4) - py; if (ex * ex + ey * ey < bd) { bd = ex * ex + ey * ey; g = e; } } }
    if (tt_vs_on()) g = who ? coop_first_char : coop_p2;   /* (Versus test: the other hero) */
    if (!g || g < 0x10000u) return;
    dx = MEMF(g + 0xF0) - px; dy = MEMF(g + 0xF4) - py; d = sqrtf(dx * dx + dy * dy);
    if (now - k->t0 >= 250) {
        float mx = px - k->px0, my = py - k->py0, md = sqrtf(mx * mx + my * my);
        if (k->held && md > 0.25f) {   /* rotation = motion angle - stick angle, as a unit complex number, averaged */
            float sc = cosf(k->sa0), ss = sinf(k->sa0), wc = mx / md, ws = my / md, rc = wc * sc + ws * ss, rs = ws * sc - wc * ss, n;
            k->c = 0.7f * k->c + 0.3f * rc; k->s = 0.7f * k->s + 0.3f * rs;
            n = sqrtf(k->c * k->c + k->s * k->s); if (n > 1e-3f) { k->c /= n; k->s /= n; }
            k->samples++;
        }
        k->px0 = px; k->py0 = py; k->t0 = now; k->held = 0;
    }
    if (d > R) {   /* stick = world direction rotated back by the learned rotation */
        float wc = dx / d, ws = dy / d, sc = wc * k->c + ws * k->s, ss = ws * k->c - wc * k->s;
        o->lx = (int16_t)(sc * 30000.0f); o->ly = (int16_t)(ss * 30000.0f);
        k->sa0 = atan2f(ss, sc); k->held = 1;
    } else {
        const char *bv = getenv("TT_COOP_SEEKBTN");
        if (now % 600 < 200) { if (now - k->tl > 400) { k->tl = now; k->taps++; k->btn = bv && !strcmp(bv, "X") ? 2 : bv && !strcmp(bv, "Y") ? 3 : bv && !strcmp(bv, "B") ? 1 : bv && !strcmp(bv, "A") ? 0 : (k->taps & 1) ? 2 : 3; } o->analog[k->btn] = 255; }
    }
    if (now - k->tlog > 1000) { k->tlog = now; tt_dbg("[COOPSEEK] P%d goal %08X %.2f away, rotation %.0f deg (%u samples), presses %u\n", who + 1, g, d, atan2f(k->s, k->c) * 57.29578f, k->samples, k->taps); }
}
void coop_seek(CoopPad *o) { coop_seek_for(o, coop_p2, 1); }

/* player 2 played by the co-op code (the Co-op menu's P2 Bot, ini [Coop] p2_bot; a stopgap until a proper controller
 * test setting). Pad 2 is written here after the real controller is read:
 *  - any real input on pad 2 (stick, buttons, triggers) hands it to the player for 3 s;
 *  - an enemy alive within 9 units of player 2 and 15 of player 1: go at it, and in reach (4.6) attack, X and Y in turn;
 *  - else follow: player 1 further than 6 units away starts it, closer than 3.5 stops it (beside him); the goal is
 *    where player 1 was 0.5 s ago, so it moves off half a second after him.
 * The stick is camera-relative: the stick-to-world rotation is learnt from player 2's own motion, as the test bot's.
 * Off when a test pad script drives pad 2 (TT_COOP_PAD2SCRIPT) or the test bot runs (TT_COOP_PAD2SEEK), unless
 * TT_COOP_P2BOT is set for the run. Logged as [P2BOT] once a second. */
typedef struct { float x, y; uint32_t t; } CoopTrail;
static int coop_place_frames;   /* (coop_place: player 2 put beside player 1 for this many frames) */
static uint32_t coop_bot_drove_t;   /* the P2 Bot last wrote pad 2 (the leash is off then) */
/* the bot's way to a goal over player 2's pathmap (the AI's own walkable cells, as the allies walk them; in a playtest it ran
 * into the rocks going straight): a breadth-first search over walkable cells (8 ways, no corner cut past a blocked
 * cell) within a 64 x 64 box round player 2's cell, then the furthest cell up to 4 steps on that is in a straight
 * walkable line from him. Without a pathmap, a blocked goal or start, or no way within the box: straight at it.
 * TT_COOP_BOTNOPATH: always straight. Player 2's bot: straight unless TT_COOP_BOTPATH (2026-10-25). */
static uint32_t coop_pathmap(uint32_t actor); static int coop_pm_cell(uint32_t pm, int c, int r);
#define COOP_PM_WALK(v) ((v) == 0 || (v) == 2)   /* walkable pathmap states (2: player 1 stands on them) */
static int coop_pm_line(uint32_t pm, int c0, int r0, int c1, int r1)   /* the cells on the line from (c0,r0) to (c1,r1) all walkable */
{
    int n = abs(c1 - c0) > abs(r1 - r0) ? abs(c1 - c0) : abs(r1 - r0), i;
    for (i = 0; i <= n; i++) { int c = c0 + (n ? (c1 - c0) * i / n : 0), r = r0 + (n ? (r1 - r0) * i / n : 0); if (!COOP_PM_WALK(coop_pm_cell(pm, c, r))) return 0; }
    return 1;
}
static int coop_bot_route(uint32_t hero, float gx, float gy, float *wx, float *wy)
{
    enum { B = 64, H = B / 2 };
    static short prev[B * B]; static int q[B * B];
    uint32_t pm = coop_pathmap(hero); float ox, oy, sz; int c0, r0, c1, r1, qh = 0, qt = 0, i, found = 0, path[B * 2], np = 0, k;
    *wx = gx; *wy = gy;
    /* (player 2 straight at his goal, off the companions' pathmaps: a playtest, Gates of Moria and Amon Hen, "remove p2 bot from
     * the npc pathmaps and revert the bot to simply follow p1 group radius and p1 when alone"; TT_COOP_BOTPATH: routed as before.
     * The test bots for player 1 keep the route) */
    if (hero == coop_p2 && !getenv("TT_COOP_BOTPATH")) return 0;
    if (!pm || getenv("TT_COOP_BOTNOPATH") || getenv("TT_COOP_BOTFOOTSTEPS")) return 0;   /* (TT_COOP_BOTFOOTSTEPS: player 1's footsteps instead, coop_steps -- tried, the owner went back to this) */
    ox = MEMF(pm + 0x48u); oy = MEMF(pm + 0x4Cu); sz = (float)(int)MEM32(pm + 0x68u);
    c0 = (int)floorf((MEMF(hero + 0xF0) - ox) / sz); r0 = (int)floorf((MEMF(hero + 0xF4) - oy) / sz);
    c1 = (int)floorf((gx - ox) / sz); r1 = (int)floorf((gy - oy) / sz);
    if (abs(c1 - c0) >= H || abs(r1 - r0) >= H || !COOP_PM_WALK(coop_pm_cell(pm, c0, r0)) || !COOP_PM_WALK(coop_pm_cell(pm, c1, r1))) return 0;
    if (coop_pm_line(pm, c0, r0, c1, r1)) return 1;   /* in a straight walkable line already */
    for (i = 0; i < B * B; i++) prev[i] = -1;
    prev[H * B + H] = (short)(H * B + H); q[qt++] = H * B + H;
    while (qh < qt && !found) {
        int cur = q[qh++], cc = cur % B, cr = cur / B, d;
        static const int dc[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, dr[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
        for (d = 0; d < 8; d++) {
            int nc = cc + dc[d], nr = cr + dr[d], wc = c0 + nc - H, wr = r0 + nr - H, ni;
            if (nc < 0 || nr < 0 || nc >= B || nr >= B || prev[ni = nr * B + nc] >= 0) continue;
            if (!COOP_PM_WALK(coop_pm_cell(pm, wc, wr))) continue;
            if (d >= 4 && (!COOP_PM_WALK(coop_pm_cell(pm, wc - dc[d], wr)) || !COOP_PM_WALK(coop_pm_cell(pm, wc, wr - dr[d])))) continue;   /* (no corner cut) */
            prev[ni] = (short)cur; q[qt++] = ni;
            if (wc == c1 && wr == r1) { found = 1; break; }
        }
    }
    if (!found) return 0;
    for (k = (r1 - r0 + H) * B + (c1 - c0 + H); np < B * 2; k = prev[k]) { path[np++] = k; if (k == H * B + H) break; }   /* goal .. start */
    for (i = np - 2, k = -1; i >= 0 && i >= np - 8; i--) { int cc = path[i] % B - H + c0, cr = path[i] / B - H + r0; if (coop_pm_line(pm, c0, r0, cc, cr)) k = i; }
    if (k < 0) k = np - 2 >= 0 ? np - 2 : 0;
    *wx = ox + (path[k] % B - H + c0 + 0.5f) * sz; *wy = oy + (path[k] / B - H + r0 + 0.5f) * sz;
    return 2;
}
static float coop_bot_steer(CoopSeek *k, CoopPad *o, uint32_t hero, float tx, float ty, float R)
{
    uint32_t now = coop_now();
    float px = MEMF(hero + 0xF0), py = MEMF(hero + 0xF4), dx = tx - px, dy = ty - py, d = sqrtf(dx * dx + dy * dy);
    if (now - k->t0 >= 250) {
        float mx = px - k->px0, my = py - k->py0, md = sqrtf(mx * mx + my * my);
        if (k->held && md > 0.25f) {
            float sc = cosf(k->sa0), ss = sinf(k->sa0), wc = mx / md, ws = my / md, rc = wc * sc + ws * ss, rs = ws * sc - wc * ss, n;
            k->c = 0.7f * k->c + 0.3f * rc; k->s = 0.7f * k->s + 0.3f * rs;
            n = sqrtf(k->c * k->c + k->s * k->s); if (n > 1e-3f) { k->c /= n; k->s /= n; }
            k->samples++;
        }
        k->px0 = px; k->py0 = py; k->t0 = now; k->held = 0;
    }
    if (d > R) {
        float wc = dx / d, ws = dy / d, sc, ss, sp = d > R + 1.5f ? 32767.0f : 20000.0f;   /* (full run; slower right at the goal) */
        {   /* smoothed: half of the last frame's way kept (each new route cell made it zig-zag) */
            static float pw[2][2]; int b = hero == coop_p2 ? 0 : 1; float n;
            if (pw[b][0] * wc + pw[b][1] * ws > 0.0f) { wc = 0.5f * wc + 0.5f * pw[b][0]; ws = 0.5f * ws + 0.5f * pw[b][1]; n = sqrtf(wc * wc + ws * ws); if (n > 1e-3f) { wc /= n; ws /= n; } }
            pw[b][0] = wc; pw[b][1] = ws;
        }
        sc = wc * k->c + ws * k->s; ss = ws * k->c - wc * k->s;
        {   /* the shared camera [0x43B164] (+0x40 what it looks at, +0x50 its eye, Z up, as coop_project): stick up = its
             * forward on the ground, stick right = its right; the learnt rotation only without it */
            uint32_t cm = MEM32(0x43B164u);
            if (cm >= 0x10000u && !getenv("TT_COOP_BOTLEARN")) {
                float fx = MEMF(cm + 0x40) - MEMF(cm + 0x50), fy = MEMF(cm + 0x44) - MEMF(cm + 0x54), fn = sqrtf(fx * fx + fy * fy);
                if (fn > 1e-3f) { fx /= fn; fy /= fn; ss = wc * fx + ws * fy; sc = wc * fy - ws * fx; }   /* right = (fy, -fx) */
            }
        }
        o->lx = (int16_t)(sc * sp); o->ly = (int16_t)(ss * sp);
        k->sa0 = atan2f(ss, sc); k->held = 1;
    }
    return d;
}
/* to a goal (stop within R of it), by the pathmap's way (coop_bot_route, again every 250 ms); the distance to the goal */
/* player 1's footsteps: where he went, a spot every 1.5 units, the last 128 (a jump of more than 16 units in a frame -- a
 * teleport, a new level -- starts them again). The P2 Bot follows them from the one nearest to him on toward player 1's
 * latest (coop_bot_follow), so it goes where player 1 went, round what he went round */
static struct { float x, y; } coop_steps[128]; static int coop_steps_n, coop_steps_i; static unsigned coop_steps_lv; static unsigned coop_steps_seq;
static void coop_steps_frame(uint32_t p1)
{
    float x, y;
    if (p1 < 0x10000u) return;
    x = MEMF(p1 + 0xF0); y = MEMF(p1 + 0xF4);
    if (coop_steps_lv != coop_level_n) { coop_steps_lv = coop_level_n; coop_steps_n = coop_steps_i = 0; }
    if (coop_steps_n) { int l = (coop_steps_i + 127) & 127; float dx = x - coop_steps[l].x, dy = y - coop_steps[l].y, dd = dx * dx + dy * dy;
        if (dd > 64.0f * 4.0f) { coop_steps_n = coop_steps_i = 0; coop_steps_lv = ~0u - 1u; }   /* (16 units: a jump, not a walk: begin again) */
        else if (dd < 2.25f) return; }
    coop_steps[coop_steps_i].x = x; coop_steps[coop_steps_i].y = y; coop_steps_i = (coop_steps_i + 1) & 127; if (coop_steps_n < 128) coop_steps_n++; coop_steps_seq++;
}
/* the next footstep for the bot at (bx, by), forward only: the one it is going to (counted from the level's first, so it
 * never goes back to an older one -- player 1's way can cross itself, and "the one past the nearest" flipped between two
 * at Gates of Moria's start, the bot stuck behind the Fellowship, a bot run); reached (1.2 units): the next; no nearer to
 * it for 1.5 s: four on; further than 12 units from all of them, or past the last: 0 (then straight at player 1) */
/* (coop_steps_seq: footsteps kept since the trail began; the newest is number seq - 1) */
static int coop_steps_next(float bx, float by, float *gx, float *gy)
{
    static unsigned want, lv = ~0u; static uint32_t prog_t; static float prog_d;
    unsigned first = coop_steps_seq - (unsigned)coop_steps_n, k; uint32_t now = coop_now(); float bd = 144.0f;
    if (!coop_steps_n) return 0;
    if (lv != coop_steps_lv || want < first || want > coop_steps_seq) {   /* (new level, a new trail, or too old: from the nearest) */
        int best = -1, j;
        lv = coop_steps_lv;
        for (j = 0; j < coop_steps_n; j++) { int r = (coop_steps_i - coop_steps_n + j + 128) & 127; float dx = coop_steps[r].x - bx, dy = coop_steps[r].y - by, d = dx * dx + dy * dy; if (d <= bd) { bd = d; best = j; } }
        if (best < 0) return 0;
        want = first + (unsigned)best; prog_t = now; prog_d = 1e9f;
    }
    for (k = want; k < coop_steps_seq; k++) {
        int r = (int)(k - first + (unsigned)(coop_steps_i - coop_steps_n + 128)) & 127; float dx = coop_steps[r].x - bx, dy = coop_steps[r].y - by, d = dx * dx + dy * dy;
        if (d <= 1.44f) continue;   /* (reached) */
        if (d > 144.0f) return 0;   /* (lost the trail) */
        if (k != want) { want = k; prog_t = now; prog_d = d; }
        if (d < prog_d - 0.25f) { prog_d = d; prog_t = now; }
        else if (now - prog_t > 1500u) { want = k + 4 < coop_steps_seq ? k + 4 : coop_steps_seq - 1; prog_t = now; prog_d = 1e9f; continue; }
        *gx = coop_steps[r].x; *gy = coop_steps[r].y; return 1;
    }
    return 0;
}
static float coop_bot_go(CoopSeek *k, CoopPad *o, uint32_t hero, float gx, float gy, float R)
{
    static struct { uint32_t hero, t; float wx, wy, lgx, lgy; int how; } rs[2]; static int nlog;   /* (a route per bot: player 2's, player 1's test bot) */
    int b = hero == coop_p2 ? 0 : 1;
    float dx = gx - MEMF(hero + 0xF0), dy = gy - MEMF(hero + 0xF4), d = sqrtf(dx * dx + dy * dy);
    if (d <= R) return d;
    if (rs[b].hero != hero || coop_now() - rs[b].t >= 250u || fabsf(gx - rs[b].lgx) + fabsf(gy - rs[b].lgy) > 2.0f) {
        rs[b].hero = hero; rs[b].t = coop_now(); rs[b].lgx = gx; rs[b].lgy = gy; rs[b].how = coop_bot_route(hero, gx, gy, &rs[b].wx, &rs[b].wy);
        if (rs[b].how == 2 && nlog++ < 40) tt_dbg("[P2BOT] %s round an obstacle: goal %.1f %.1f, by %.1f %.1f\n", b ? "(player 1's test bot)" : "", gx, gy, rs[b].wx, rs[b].wy);
    }
    if (rs[b].how == 2) { coop_bot_steer(k, o, hero, rs[b].wx, rs[b].wy, 0.4f); return d; }
    coop_bot_steer(k, o, hero, gx, gy, R);
    return d;
}
/* the bot's ranged shot at e: LT held, the stick (camera-relative) toward it, so the game's targeting (its target pick
 * for player 2, his component +0x98) locks onto it; A only once that lock is on it (a playtest: "only fire his ranged
 * weapon with his dedicated targeting system"; the throws follow the lock, a shot at another lock missed). While he aims
 * our own target filler (coop_p2_target) stands aside, so the lock is the game's. (Before: also after 1.2 s of LT with
 * no lock.) fire 0: aim only (a look). Returns nonzero while the lock is on e. */
uint32_t coop_bot_aim_until;   /* (coop_p2_target leaves player 2's target alone till then) */
static const char *coop_nm(uint32_t a);
static int coop_bot_shoot(CoopPad *o, uint32_t p2, uint32_t e, int fire)
{
    static uint32_t lt_t, lt_last, lt_e; uint32_t now = coop_now(), cm = MEM32(0x43B164u), c2 = MEM32(p2 + 0x494);
    float wx = MEMF(e + 0xF0) - MEMF(p2 + 0xF0), wy = MEMF(e + 0xF4) - MEMF(p2 + 0xF4), wn = sqrtf(wx * wx + wy * wy);
    int locked = c2 >= 0x10000u && MEM32(c2 + 0x98) == e;
    if (wn > 1e-3f) { wx /= wn; wy /= wn; }
    if (cm >= 0x10000u) {
        float fx = MEMF(cm + 0x40) - MEMF(cm + 0x50), fy = MEMF(cm + 0x44) - MEMF(cm + 0x54), fn = sqrtf(fx * fx + fy * fy);
        if (fn > 1e-3f) { fx /= fn; fy /= fn; o->ly = (int16_t)((wx * fx + wy * fy) * 20000.0f); o->lx = (int16_t)((wx * fy - wy * fx) * 20000.0f); }
    }
    o->analog[6] = 255;   /* LT held: the ranged weapon */
    coop_bot_aim_until = now + 250u;
    if (getenv("TT_COOP_AIMTGTLOG")) { static uint32_t al; uint32_t t = c2 >= 0x10000u ? MEM32(c2 + 0x98) : 0;   /* (research) what his lock is while he aims */
        if (now - al >= 250u) { al = now; tt_dbg("[AIMTGT] aiming at %08X (%.1f away), LT for %u ms | his target %08X %s%s\n", e, wn, now - lt_t, t,
            t == e ? "(the aimed one)" : !t ? "(none)" : "(another)", t && t != e && t >= 0x10000u && t < 0x84000000u ? coop_nm(t) : ""); } }
    if (now - lt_last > 150u || e != lt_e) lt_t = now; lt_last = now; lt_e = e;
    /* A only once LT has been held 0.4 s (A before the bow is up is a swing in the air) */
    if (fire && now - lt_t >= 400u && locked && now % 700 < 200) o->analog[0] = 255;
    { static uint32_t tl; static unsigned shots, onlock; if (o->analog[0] && now - tl > 300u) { tl = now; shots++; onlock += locked;
        if ((shots & 7) == 1) tt_dbg("[P2BOT] shots %u, %u of them with his lock on the enemy\n", shots, onlock); } }
    return locked;
}
/* the bot's melee (not always the same attack): a combo picked at random (A fast, Y fierce), its presses 0.28-0.4 s
 * apart, then a 0.35-0.95 s pause before the next one. Called every frame it may swing. (X, used before in place of
 * A, never makes an attack box -- for player 1 either --, so half the bot's presses did nothing; player 1's A made the
 * hits on the Watcher's tentacles.) */
static void coop_bot_attack(CoopPad *o, uint32_t now)
{
    static const char *const combos[] = { "AAA", "AAY", "AYY", "YY", "AY", "AAAY", "Y", "AA", "YA" };
    static const char *c; static int k, cur; static uint32_t next, hold, last, seed = 4242u;
    if (now - last > 400u) { c = 0; next = 0; hold = 0; }   /* (not swinging lately: a fresh combo) */
    last = now;
    if (now < hold) { o->analog[cur] = 255; return; }
    if (now < next) return;
    seed = seed * 1103515245u + 12345u;
    if (c && !c[k]) { c = 0; next = now + 350u + (seed >> 16) % 600u; return; }   /* (the combo done: a pause) */
    if (!c) { c = combos[(seed >> 16) % (sizeof combos / sizeof combos[0])]; k = 0; }
    cur = c[k] == 'A' ? 0 : c[k] == 'X' ? 2 : 3; k++;
    hold = now + 120u; next = now + 280u + (seed >> 8) % 120u; o->analog[cur] = 255;
}
/* now and then a block (White) instead of a swing when an enemy within 4 units is after player 2 (defending
 * himself now and then): every 1.5 s a 1 in 15 chance of 0.7 s of block. Nonzero while it blocks. */
static int coop_bot_defend(CoopPad *o, uint32_t p2, uint32_t now)
{
    static uint32_t next, until, seed = 12345u, nb; int i, near = 0;
    if (now < until) { o->analog[5] = 255; return 1; }
    if (now < next) return 0;
    next = now + 1500u;
    for (i = 0; i < coop_nchars && !near; i++) { uint32_t e = coop_chars[i], ce = MEM32(e + 0x494); float dx, dy;
        if (MEM32(e + 0x114) != 3 || (int32_t)MEM32(e + 0x2FC) <= 0 || ce < 0x10000u || MEM32(ce + 0x98) != p2) continue;
        dx = MEMF(e + 0xF0) - MEMF(p2 + 0xF0); dy = MEMF(e + 0xF4) - MEMF(p2 + 0xF4); near = dx * dx + dy * dy < 16.0f; }
    seed = seed * 1103515245u + 12345u;
    if (near && (seed >> 16) % 15 == 0) {   /* (1 in 15: parrying less, then a third less again) */ until = now + 700u; o->analog[5] = 255; if (nb++ < 30) tt_dbg("[P2BOT] blocks (an enemy after him within 4 units)\n"); return 1; }
    return 0;
}
/* melee only beside the enemy (no swings at the air): within 2 units, or within 5 with the distance held (within
 * 0.12 units for 0.3 s) while the enemy moves (0.2 units or more meanwhile): their bodies touch and push. Bodies stop
 * two characters at the sum of their radii, and there the distance holds while both push and slide round each other
 * (observed, TT_COOP_BESIDELOG: Weathertop, Legolas and a Ringwraith at 4.05 sample after sample, both moving 1-2 units
 * every 250 ms; Balin's Tomb, Gimli and the bigger enemies at 4.5-4.8, both moving about 1). Balin's Tomb's small orcs
 * let him within 2 units; Gates of Moria's enemies stop him at about 4.4-4.7, where melee hits land (a playtest there
 * with only "within 2": no swing at all, the bot ran in place). A held distance with the enemy still is not contact:
 * small orcs standing 2-3.5 units off while he was rooted in his own attack (all of player 2's 15 logged hits in that
 * run came under 2 units or at 4.5-4.8) -- unless it is held at about the closest he has ever been to that enemy (0.3
 * units more at most): a Ringwraith standing still against him at 4.1 (he never got closer than about 4) is contact,
 * a small orc 2.3 off that he once stood 0.5 from is not. Walking up, or after one that backs off, the distance
 * changes every frame.
 * Once beside, he stays so while the enemy is no more than 0.6 units further than it was then, through his swings'
 * lunges. (Tried and dropped: "no closer for 0.6 s", "he stands still", "the distance held" alone.) Nonzero when
 * beside; then the stick toward it, gently (as the boss's tentacle attack does), so his swing goes at it. Called
 * every frame he is after e in melee. */
static uint32_t coop_bot_unreach_e[8], coop_bot_unreach_until[8];   /* enemies he could not get to: each left out of his melee picks till then
                                                                     * (8: with one, two blocked orcs took turns as his pick, a bot run) */
static int coop_bot_unreach(uint32_t e, uint32_t now) { int i; for (i = 0; i < 8; i++) if (coop_bot_unreach_e[i] == e && (int32_t)(coop_bot_unreach_until[i] - now) > 0) return 1; return 0; }
static void coop_bot_unreach_set(uint32_t e, uint32_t now) { int i, k = 0; for (i = 0; i < 8; i++) { if (coop_bot_unreach_e[i] == e) { k = i; break; } if ((int32_t)(coop_bot_unreach_until[i] - coop_bot_unreach_until[k]) < 0) k = i; }
    coop_bot_unreach_e[k] = e; coop_bot_unreach_until[k] = now + 5000u; }
static int coop_bot_beside(CoopPad *o, uint32_t p2, uint32_t e, float df, uint32_t now)
{
    static uint32_t ap_e, ap_t, ap_last, hd_t, mn_e[16]; static float ap_d, hd_d, hd_ex, hd_ey, cd, mn_d[16]; static int was, mn_k;
    uint32_t cm = MEM32(0x43B164u); int b, k, fresh = e != ap_e || now - ap_last > 200u;
    float px = MEMF(p2 + 0xF0), py = MEMF(p2 + 0xF4), ex = MEMF(e + 0xF0), ey = MEMF(e + 0xF4);
    for (k = 0; k < 16 && mn_e[k] != e; k++) ;   /* (the closest he has been to e) */
    if (k == 16) { k = mn_k; mn_k = (mn_k + 1) % 16; mn_e[k] = e; mn_d[k] = df; }
    if (df < mn_d[k]) mn_d[k] = df;
    if (fresh || df < ap_d - 0.3f || df > ap_d + 1.0f) { ap_e = e; ap_d = df; ap_t = now; }
    if (fresh || fabsf(df - hd_d) > 0.12f) { hd_d = df; hd_t = now; hd_ex = ex; hd_ey = ey; }   /* (the distance held since hd_t, and where the enemy was then) */
    if (fresh) was = 0;
    ap_last = now;
    b = df <= 2.0f || (df <= 5.0f && now - hd_t >= 300u && ((ex - hd_ex) * (ex - hd_ex) + (ey - hd_ey) * (ey - hd_ey) >= 0.04f || df <= mn_d[k] + 0.3f))
        || (was && df <= cd + 0.6f);
    if (b && !was) cd = df;
    was = b;
    if (getenv("TT_COOP_BESIDELOG")) { static uint32_t bl; static float lpx, lpy, lex, ley;   /* (research) every 250 ms: both places, his state, the stick */
        if (now - bl >= 250u) {
            tt_dbg("[BESIDE] %08X d %.2f | P2 %.2f %.2f moved %.2f state %u | enemy %.2f %.2f moved %.2f state %u | stick %d %d | beside %d\n", e, df, px, py,
                   sqrtf((px - lpx) * (px - lpx) + (py - lpy) * (py - lpy)), MEM32(p2 + 0x408), ex, ey, sqrtf((ex - lex) * (ex - lex) + (ey - ley) * (ey - ley)),
                   MEM32(e + 0x408), o->lx, o->ly, b);
            bl = now; lpx = px; lpy = py; lex = ex; ley = ey; } }
    if (!b && now - ap_t >= 2500u) {   /* no closer in 2.5 s: out of his reach (a run walked 10 s at an orc 4.2-5.4 off), left 5 s */
        coop_bot_unreach_set(e, now); ap_t = now;
        tt_dbg("[P2BOT] can't get to %08X (%.1f away, height %.1f): left out of melee for 5 s\n", e, df, MEMF(e + 0xF8) - MEMF(p2 + 0xF8));
    }
    if (b && cm >= 0x10000u) {
        float wx = MEMF(e + 0xF0) - MEMF(p2 + 0xF0), wy = MEMF(e + 0xF4) - MEMF(p2 + 0xF4), wn = sqrtf(wx * wx + wy * wy);
        float fx = MEMF(cm + 0x40) - MEMF(cm + 0x50), fy = MEMF(cm + 0x44) - MEMF(cm + 0x54), fn = sqrtf(fx * fx + fy * fy);
        if (wn > 1e-3f && fn > 1e-3f) { wx /= wn; wy /= wn; fx /= fn; fy /= fn; o->ly = (int16_t)((wx * fx + wy * fy) * 9000.0f); o->lx = (int16_t)((wx * fy - wy * fx) * 9000.0f); }
    }
    return b;
}
static int bot_lx, bot_ly;   /* (the last stick, for the stuck log) */
extern unsigned coop_leash_stops;
static int coop_boss_kind(uint32_t a);
static const char *coop_nm(uint32_t a);
static int coop_p2aim_on;   /* (bot test TT_COOP_P2AIMTEST) player 2 aiming now */
uint32_t coop_bot_tgt_hold, coop_bot_tgt_until;   /* the bot's own target pick (a stretched tentacle), kept from coop_p2_target until then */
/* the Watcher's fight is on: the Watcher within 60 units of (x, y), and it or one of its tentacles out of state 0 (as
 * the boss block in coop_p2bot decides it) */
static int coop_boss_fight_on(float x, float y)
{
    int i, near_w = 0, awake = 0;
    for (i = 0; i < coop_nchars; i++) {
        uint32_t e = coop_chars[i]; int k = coop_boss_kind(e); float dx, dy;
        if (!k || (int32_t)MEM32(e + 0x2FC) <= 0) continue;
        if (MEM32(e + 0x408)) awake = 1;
        dx = MEMF(e + 0xF0) - x; dy = MEMF(e + 0xF4) - y;
        if (k == 2 && dx * dx + dy * dy <= 3600.0f) near_w = 1;
    }
    return near_w && awake;
}
/* ---- the P2 Bot ----
 * Player 2 played by the co-op code through his pad. Co-op Options' "P2 Bot": Off; When Idle (the bot plays, and any
 * real input on player 2's controller hands it to the player for 3 s); Always (the controller is ignored). Each frame:
 *  1. whose pad it is (coop_bot_pad_free);
 *  2. what he sees (BotView: both heroes, the group's circle round player 1 and the companions, the enemy for melee);
 *  3. the first task that applies: player 1 down (roam), the Watcher's fight (its tentacles), ranged, melee, follow;
 *  4. the task presses his pad (coop_bot_go / _steer / _attack / _shoot / _beside / _defend, above).
 * The rules came from playtests (NOTES.md 6.11). */
extern int tt_coop_setting(int i); extern int tt_in_scene(void); extern int coop_hero_down(int);
typedef struct {
    uint32_t now, p1, p2;
    float qx, qy, bx, by, d1;      /* player 1, player 2, their distance */
    float gcx, gcy, gr, dc;        /* the group's circle (centre, radius) and player 2's distance from its centre */
    uint32_t foe; float bd;        /* the enemy he is after and its distance squared */
    int near;                      /* the melee pick is near him (melee, not ranged) */
    int mode, boss, bossfight;     /* mode: 0 beside player 1, 1 following, 2 fighting, 3 shooting; boss: the Watcher's task ran */
} BotView;
static CoopSeek s_bk = { 1, 0 };
static int s_bot_following;
static const char *s_bot_why;
static const char *s_bot_how;

/* 1. his pad: the bot's (1) or the player's (0) */
static int coop_bot_pad_free(CoopPad *o, uint32_t now)
{
    static uint32_t human_t;
    int i, any;
    if (tt_coop_setting(4) == 2) return 1;   /* Always */
    any = o->buttons != 0 || o->lx > 5000 || o->lx < -5000 || o->ly > 5000 || o->ly < -5000 || o->rx > 5000 || o->rx < -5000 || o->ry > 5000 || o->ry < -5000;   /* (5000: a gentle walk, 6000 in a bot run, was under 8000 and the bot cut in; still above a resting stick's drift) */
    for (i = 0; i < 8; i++) if (o->analog[i] > 30) any = 1;
    if (any) { if (!human_t || now - human_t > 3000) tt_dbg("[P2BOT] pad 2 used: the player's for 3 s\n"); human_t = now ? now : 1; }   /* (not now | 1: on an even
        * millisecond that is now + 1, now - human_t wrapped round to over 4 billion and the bot took the frame -- with the stick held, every other
        * frame was the bot's, which inside its circle stood him still: a playtest, "player 2 constantly stops"; a bot run, 45 of 68 samples) */
    if (getenv("TT_COOP_PADFREELOG")) { static uint32_t lt; if (now - lt > 500u) { lt = now; tt_dbg("[PADFREE] t %u: pad 2 stick %d %d buttons %04X: %s\n", now - coop_p2_t0, o->lx, o->ly, o->buttons, (human_t && now - human_t < 3000) ? "the player's" : "the bot's"); } }   /* (research) */
    return !(human_t && now - human_t < 3000);
}

/* (bot test, Legolas' left weapon) player 2 aims at the Watcher (LT, the stick at it, no shot) 3 s on, 3 s off;
 * TT_COOP_P2AIMTEST=npc: at the furthest companion within 25 units instead (someone up on the rocks) */
static void coop_bot_aimtest(CoopPad *o, uint32_t now, uint32_t p1, uint32_t p2)
{
    uint32_t wt = 0; int i;
    for (i = 0; i < coop_nchars; i++) if (coop_boss_kind(coop_chars[i]) == 2) wt = coop_chars[i];
    if (!strcmp(getenv("TT_COOP_P2AIMTEST"), "npc")) {
        float bdd = 0; wt = 0;
        for (i = 0; i < coop_nchars; i++) { uint32_t e = coop_chars[i]; float dx, dy, dd;
            if (e == p1 || e == p2 || MEM32(e + 0x114) != 2) continue;
            dx = MEMF(e + 0xF0) - MEMF(p2 + 0xF0); dy = MEMF(e + 0xF4) - MEMF(p2 + 0xF4); dd = dx * dx + dy * dy;
            if (dd < 625.0f && dd > bdd) { bdd = dd; wt = e; } }
    }
    coop_p2aim_on = wt && (now / 3000u) % 2u == 1u;
    if (coop_p2aim_on) coop_bot_shoot(o, p2, wt, 0);
}

/* 2. what he sees. Far behind for 3 s, whatever the cause: put beside player 1 (a playtest's Gates of Moria lake:
 * wading, held by an unseen wall, he was 50-75 units behind for long; 22 units / 3 s: still held at 30 / 4 s).
 * The group: player 1 and the companions (role 2) within 20 units of him; its centre their mean, its radius the
 * furthest member + 4, kept in 10-18 (6-14 was in player 1's way); with no companion near, 8 round player 1 */
static void coop_bot_view(BotView *v)
{
    static uint32_t far_t, glog;
    float sx, sy, far2; int i, n = 1;
    v->qx = MEMF(v->p1 + 0xF0); v->qy = MEMF(v->p1 + 0xF4); v->bx = MEMF(v->p2 + 0xF0); v->by = MEMF(v->p2 + 0xF4);
    v->d1 = sqrtf((v->qx - v->bx) * (v->qx - v->bx) + (v->qy - v->by) * (v->qy - v->by));
    if (v->d1 > 22.0f && coop_place_frames <= 0) {
        if (!far_t) far_t = v->now | 1;
        else if (v->now - far_t > 3000u) { far_t = 0; coop_place_frames = 3; s_bot_following = 0; tt_dbg("[P2BOT] %.1f behind for 3 s: put beside player 1\n", v->d1); }
    } else far_t = 0;
    sx = v->qx; sy = v->qy;
    for (i = 0; i < coop_nchars; i++) { uint32_t e = coop_chars[i]; float ex, ey;
        if (e == v->p1 || e == v->p2 || MEM32(e + 0x114) != 2 || (int32_t)MEM32(e + 0x2FC) <= 0) continue;
        ex = MEMF(e + 0xF0); ey = MEMF(e + 0xF4); if ((ex - v->qx) * (ex - v->qx) + (ey - v->qy) * (ey - v->qy) > 400.0f) continue;
        sx += ex; sy += ey; n++; }
    v->gcx = sx / n; v->gcy = sy / n;
    far2 = (v->qx - v->gcx) * (v->qx - v->gcx) + (v->qy - v->gcy) * (v->qy - v->gcy);
    for (i = 0; i < coop_nchars; i++) { uint32_t e = coop_chars[i]; float ex, ey, dd;
        if (e == v->p1 || e == v->p2 || MEM32(e + 0x114) != 2 || (int32_t)MEM32(e + 0x2FC) <= 0) continue;
        ex = MEMF(e + 0xF0); ey = MEMF(e + 0xF4); if ((ex - v->qx) * (ex - v->qx) + (ey - v->qy) * (ey - v->qy) > 400.0f) continue;
        dd = (ex - v->gcx) * (ex - v->gcx) + (ey - v->gcy) * (ey - v->gcy); if (dd > far2) far2 = dd; }
    v->gr = sqrtf(far2) + 4.0f; if (v->gr < 10.0f) v->gr = 10.0f; if (v->gr > 18.0f) v->gr = 18.0f;
    if (n == 1) v->gr = 8.0f;
    v->dc = sqrtf((v->bx - v->gcx) * (v->bx - v->gcx) + (v->by - v->gcy) * (v->by - v->gcy));
    if (v->now - glog > 5000u) { glog = v->now; tt_dbg("[P2BOT] group: player 1 and %d companions, centre %.1f %.1f, radius %.1f, player 2 %.1f from the centre\n", n - 1, v->gcx, v->gcy, v->gr, v->dc); }
}

/* the enemy for melee: his melee radius (TT_COOP_BOTMELEER, 10 units round him; one leaves it 2 units further out) keeps
 * the enemies in the order they came into it, and he fights the first still in it; with none in it, no melee at all -- he
 * shoots if he can, else follows (a playtest: "a radius to p2 bot that made him attack enemies in order they entered the
 * radius and the bot doesnt do melee when nothing is in the radius"). Until then: the nearest within 6 units, else the
 * nearest in the group's circle, walked up to (TT_COOP_BOTOLDPICK: as before). Not the Watcher, not the troll's chain, not
 * one he couldn't get to lately (coop_bot_beside) */
static void coop_bot_pick_melee_old(BotView *v);
static void coop_bot_pick_melee(BotView *v)
{
    static uint32_t qe[32], qt[32], ge[32], gt[32]; static int qn, gn; static uint32_t lfoe;
    float r = getenv("TT_COOP_BOTMELEER") ? (float)atof(getenv("TT_COOP_BOTMELEER")) : 10.0f, ro = r + 2.0f; int i, j;
    if (getenv("TT_COOP_BOTOLDPICK")) { coop_bot_pick_melee_old(v); return; }
    v->foe = 0; v->bd = 1e18f; v->near = 0;
    for (j = 0; j < qn; ) {   /* out: dead, gone, past the outer edge */
        uint32_t e = qe[j]; float de = 1e18f; int ok = 0;
        for (i = 0; i < coop_nchars; i++) if (coop_chars[i] == e) { ok = 1; break; }
        if (ok && MEM32(e) == 0x1741C0u && MEM32(e + 0x114) == 3 && (int32_t)MEM32(e + 0x2FC) > 0)
            de = (MEMF(e + 0xF0) - v->bx) * (MEMF(e + 0xF0) - v->bx) + (MEMF(e + 0xF4) - v->by) * (MEMF(e + 0xF4) - v->by);
        if (de > ro * ro || fabsf(MEMF(e + 0xF8) - MEMF(v->p2 + 0xF8)) > 4.0f) { int q; for (q = j; q + 1 < qn; q++) { qe[q] = qe[q + 1]; qt[q] = qt[q + 1]; } qn--; } else j++;   /* (in order kept) */
    }
    for (i = 0; i < coop_nchars; i++) {   /* in: the ones that came within the radius, stamped */
        uint32_t e = coop_chars[i]; float de;
        if (e == v->p1 || e == v->p2 || MEM32(e) != 0x1741C0u || MEM32(e + 0x114) != 3 || (int32_t)MEM32(e + 0x2FC) <= 0 || coop_boss_kind(e) == 2 || coop_boss_kind(e) == 4) continue;
        de = (MEMF(e + 0xF0) - v->bx) * (MEMF(e + 0xF0) - v->bx) + (MEMF(e + 0xF4) - v->by) * (MEMF(e + 0xF4) - v->by);
        if (de > r * r || fabsf(MEMF(e + 0xF8) - MEMF(v->p2 + 0xF8)) > 3.0f) continue;   /* (not one on a ledge over him: a bot run, two orcs 4.8 up) */
        for (j = 0; j < qn && qe[j] != e; j++) ;
        if (j == qn && qn < 32) { qe[qn] = e; qt[qn] = v->now | 1; qn++; }
    }
    {   /* the first in, still in */
        int best = -1;
        for (j = 0; j < qn; j++) { if (coop_bot_unreach(qe[j], v->now)) continue; if (best < 0 || (int32_t)(qt[j] - qt[best]) < 0) best = j; }
        if (best >= 0) { uint32_t e2 = qe[best]; v->foe = e2; v->near = 1;
            v->bd = (MEMF(e2 + 0xF0) - v->bx) * (MEMF(e2 + 0xF0) - v->bx) + (MEMF(e2 + 0xF4) - v->by) * (MEMF(e2 + 0xF4) - v->by); }
    }
    /* the group's circle (player 1 and the companions, coop_bot_view): with none in his own radius he goes only toward one that
     * came into it, the first in first (left 2 units past its edge); his radius then takes over (a playtest: "the p2 bot only
     * go toward an enemies that entered that radius, our new p2 bot attack radius should take care of the rest") */
    for (j = 0; j < gn; ) {
        uint32_t e = ge[j]; float dg = 1e18f; int ok = 0;
        for (i = 0; i < coop_nchars; i++) if (coop_chars[i] == e) { ok = 1; break; }
        if (ok && MEM32(e) == 0x1741C0u && MEM32(e + 0x114) == 3 && (int32_t)MEM32(e + 0x2FC) > 0)
            dg = (MEMF(e + 0xF0) - v->gcx) * (MEMF(e + 0xF0) - v->gcx) + (MEMF(e + 0xF4) - v->gcy) * (MEMF(e + 0xF4) - v->gcy);
        if (dg > (v->gr + 2.0f) * (v->gr + 2.0f)) { int q; for (q = j; q + 1 < gn; q++) { ge[q] = ge[q + 1]; gt[q] = gt[q + 1]; } gn--; } else j++;
    }
    for (i = 0; i < coop_nchars; i++) {
        uint32_t e = coop_chars[i]; float dg;
        if (e == v->p1 || e == v->p2 || MEM32(e) != 0x1741C0u || MEM32(e + 0x114) != 3 || (int32_t)MEM32(e + 0x2FC) <= 0 || coop_boss_kind(e) == 2 || coop_boss_kind(e) == 4) continue;
        dg = (MEMF(e + 0xF0) - v->gcx) * (MEMF(e + 0xF0) - v->gcx) + (MEMF(e + 0xF4) - v->gcy) * (MEMF(e + 0xF4) - v->gcy);
        if (dg > v->gr * v->gr) continue;
        for (j = 0; j < gn && ge[j] != e; j++) ;
        if (j == gn && gn < 32) { ge[gn] = e; gt[gn] = v->now | 1; gn++; }
    }
    if (!v->foe) for (j = 0; j < gn; j++) { uint32_t e = ge[j];
        if (coop_bot_unreach(e, v->now)) continue;
        v->foe = e; v->near = 0; v->bd = (MEMF(e + 0xF0) - v->bx) * (MEMF(e + 0xF0) - v->bx) + (MEMF(e + 0xF4) - v->by) * (MEMF(e + 0xF4) - v->by); break; }
    if (v->foe != lfoe) { if (v->foe && !v->near) tt_dbg("[P2BOT] group circle %.1f: %u in it, going to the first in %08X '%s' %.1f away\n", v->gr, (unsigned)gn, v->foe, coop_nm(v->foe), sqrtf(v->bd));
        else if (v->foe) tt_dbg("[P2BOT] melee radius %.0f: %u in it, the first in %08X '%s' %.1f away\n", r, (unsigned)qn, v->foe, coop_nm(v->foe), sqrtf(v->bd));
        else if (lfoe) tt_dbg("[P2BOT] melee radius %.0f: none in it\n", r); lfoe = v->foe; }
}
static void coop_bot_pick_melee_old(BotView *v)
{
    float bn = 1e18f, near_r = getenv("TT_COOP_BOTNEAR") ? (float)atoi(getenv("TT_COOP_BOTNEAR")) : 12.0f; uint32_t en = 0; int i;
    v->foe = 0; v->bd = 1e18f; v->near = 0;
    for (i = 0; i < coop_nchars; i++) {
        uint32_t e = coop_chars[i]; float ex, ey, de, dg;
        if (e == v->p1 || e == v->p2 || MEM32(e + 0x114) != 3 || (int32_t)MEM32(e + 0x2FC) <= 0 || coop_boss_kind(e) == 2 || coop_boss_kind(e) == 4) continue;   /* (not the Watcher, nor the troll's chain) */
        if (coop_bot_unreach(e, v->now)) continue;
        ex = MEMF(e + 0xF0); ey = MEMF(e + 0xF4);
        de = (ex - v->bx) * (ex - v->bx) + (ey - v->by) * (ey - v->by); dg = (ex - v->gcx) * (ex - v->gcx) + (ey - v->gcy) * (ey - v->gcy);
        if (de < bn) { bn = de; en = e; }
        if (dg <= (v->gr + 2.0f) * (v->gr + 2.0f) && de < v->bd) { v->bd = de; v->foe = e; }
    }
    if (en && bn <= 36.0f) { v->foe = en; v->bd = bn; v->near = 1; }
    else if (v->foe && v->bd <= near_r * near_r) v->near = 1;
}

/* 3a. player 1 down (not in the Watcher's fight, where the boss rules stay -- a playtest: "once p1 is dead, p2 bot
 * reverts back to his old non boss behaviour"): he roams after the nearest enemy within 30 units -- each 1.6 s beat 2
 * in 15 a parry (White, 0.5 s, up to 0.3 s late), 9 in 15 a combo (beside it only), 4 in 15 nothing: parrying less, so
 * he struggles a little --, and with none near walks to a point 10 units off, a new one every 4 s. TT_COOP_BOTNOROAM: off */
static int coop_bot_roam(BotView *v, CoopPad *o)
{
    static uint32_t wt, rlog, seed = 777u, beat = ~0u; static float wx, wy; static int act, late;
    uint32_t e2 = 0; float be = 900.0f; int i;
    if (!coop_hero_down(0) || getenv("TT_COOP_BOTNOROAM") || coop_boss_fight_on(v->bx, v->by)) return 0;
    for (i = 0; i < coop_nchars; i++) { uint32_t e = coop_chars[i]; float de;
        if (e == v->p1 || e == v->p2 || MEM32(e + 0x114) != 3 || (int32_t)MEM32(e + 0x2FC) <= 0 || coop_boss_kind(e) == 4) continue;
        if (coop_bot_unreach(e, v->now)) continue;
        de = (MEMF(e + 0xF0) - v->bx) * (MEMF(e + 0xF0) - v->bx) + (MEMF(e + 0xF4) - v->by) * (MEMF(e + 0xF4) - v->by);
        if (de < be) { be = de; e2 = e; } }
    if (e2) {
        float df = sqrtf(be); int bs;
        if (df > 1.2f) coop_bot_go(&s_bk, o, v->p2, MEMF(e2 + 0xF0), MEMF(e2 + 0xF4), 1.2f);
        bs = coop_bot_beside(o, v->p2, e2, df, v->now);
        if (df <= 4.6f) {
            uint32_t bt = v->now / 1600u, ph = v->now % 1600u;
            if (bt != beat) { beat = bt; seed = seed * 1103515245u + 12345u; act = (int)((seed >> 16) % 15); late = (int)((seed >> 8) % 300); }
            if (act < 2) { if (ph >= (uint32_t)late && ph < (uint32_t)late + 500u) o->analog[5] = 255; }
            else if (act < 11 && bs) coop_bot_attack(o, v->now);
        }
    } else {
        float dw = sqrtf((wx - v->bx) * (wx - v->bx) + (wy - v->by) * (wy - v->by));
        if (!wt || v->now - wt > 4000u || dw < 1.5f) { float ang; wt = v->now; seed = seed * 1103515245u + 12345u; ang = (float)((seed >> 16) % 628) / 100.0f; wx = v->bx + cosf(ang) * 10.0f; wy = v->by + sinf(ang) * 10.0f; }
        coop_bot_go(&s_bk, o, v->p2, wx, wy, 1.5f);
    }
    if (v->now - rlog > 2000u) { rlog = v->now; tt_dbg("[P2BOT] player 1 down: roaming, %s\n", e2 ? "parrying the nearest enemy" : "no enemy within 30 units: walking"); }
    return 1;
}

/* 3b. Gates of Moria's boss: only the tentacles, in melee, never the Watcher; an ordinary enemy after him within 3.5
 * units is fought first (the melee task). A tentacle (observed, TT_COOP_BOSSLOG) rises (state 5), strikes (2, 3, 4 with
 * +0x49C bit 0x400, at where its target stood as the strike began), lies stretched toward that point (9, 3-5 s), strikes
 * again or pulls back (14) and sinks (1). Here:
 *  - he goes to the nearest place a tentacle has struck (else the group's edge toward the nearest tentacle) and taps
 *    White (0.2 s of each 0.45 s) only within 6 units of it, or when a strike is at him (a playtest: "he just kept on
 *    spamming parry without being close to the tentacles");
 *  - a strike at him that ends without costing him health counts as parried: then 3.5 s attacking the nearest
 *    stretched tentacle, beside the middle of its body out of the water, facing it, his target on it (the contact
 *    handler 0xBE0D0 reads the attacker's target).
 * The log keeps the Watcher's state and both heroes' locks every 2 s. TT_COOP_BOTNOBOSS: off. 1 = the frame is his */
static int coop_bot_boss(BotView *v, CoopPad *o)
{
    static uint32_t blog, to[4], hit_until, strike_seen, pick, pick_until, seed = 12345u; static float spx[4], spy[4]; static int was[4], was_me; static int32_t hp_at;
    uint32_t w = 0, tn = 0, strike_on_me = 0, stretched = 0, nearest = 0; float tbd = 1e18f, sbest = 1e18f, nbest = 1e18f, sx = 0, sy = 0, rx = 0, ry = 0;
    uint32_t cand[4]; float csx[4], csy[4], crx[4], cry[4], cd2[4]; int nc = 0;
    int i, k, awake = 0, selfdef = 0, bstate = 0;
    if (v->foe && v->bd < 12.25f) { uint32_t cf = MEM32(v->foe + 0x494); selfdef = cf >= 0x10000u && MEM32(cf + 0x98) == v->p2; }
    for (i = 0; i < coop_nchars && !getenv("TT_COOP_BOTNOBOSS"); i++) {
        uint32_t e = coop_chars[i]; int kd = coop_boss_kind(e); float de;
        if (!kd || (int32_t)MEM32(e + 0x2FC) <= 0) continue;
        de = (MEMF(e + 0xF0) - v->bx) * (MEMF(e + 0xF0) - v->bx) + (MEMF(e + 0xF4) - v->by) * (MEMF(e + 0xF4) - v->by);
        if (de > 3600.0f) continue;
        if (MEM32(e + 0x408)) awake = 1;   /* (the fight is on once the Watcher or a tentacle leaves state 0: after a checkpoint reload the tentacles move first) */
        if (kd == 2) w = e;
        else { uint32_t ce = MEM32(e + 0x494), t = ce >= 0x10000u ? MEM32(ce + 0x98) : 0;
            if (t == v->p1 || t == v->p2) de -= 2500.0f;   /* (one after a hero first) */
            if (de < tbd) { tbd = de; tn = e; } }
    }
    if (!awake) w = 0;
    v->bossfight = w != 0;
    if (w && !selfdef) {
        for (i = 0; i < coop_nchars; i++) {
            uint32_t e = coop_chars[i], ce, t; int striking; float dx, dy, d2, rd2;
            if (coop_boss_kind(e) != 1 || (int32_t)MEM32(e + 0x2FC) <= 0 || !MEM32(e + 0x408)) continue;
            for (k = 0; k < 4 && to[k] != e; k++) ;
            if (k == 4) { for (k = 0; k < 4 && to[k]; k++) ; if (k == 4) continue; to[k] = e; was[k] = 0; spx[k] = spy[k] = 0; }
            ce = MEM32(e + 0x494); t = ce >= 0x10000u ? MEM32(ce + 0x98) : 0;
            striking = (MEM32(e + 0x49C) & 0x400u) != 0;
            if (striking && !was[k] && (t == v->p1 || t == v->p2)) { spx[k] = MEMF(t + 0xF0); spy[k] = MEMF(t + 0xF4); }
            was[k] = striking;
            dx = spx[k] - v->bx; dy = spy[k] - v->by; d2 = dx * dx + dy * dy;
            {   float qx = MEMF(e + 0xF0) - v->bx, qy = MEMF(e + 0xF4) - v->by;   /* (a strike from a tentacle whose root is within 14 units: he
                 * stands 8 from the root to attack it, and its strikes at player 1 hit him there -- 15 times down in 200 s) */
                if (striking && (t == v->p2 || (spx[k] != 0 && d2 < 25.0f) || qx * qx + qy * qy < 196.0f)) strike_on_me = e; }
            if (MEM32(e + 0x408) == 9 && !striking && (spx[k] != 0 || spy[k] != 0) && d2 < 900.0f && nc < 4) {   /* an idle (stretched) one in reach */
                cand[nc] = e; csx[nc] = spx[k]; csy[nc] = spy[k]; crx[nc] = MEMF(e + 0xF0); cry[nc] = MEMF(e + 0xF4); cd2[nc] = d2; nc++; }
            rd2 = (MEMF(e + 0xF0) - v->bx) * (MEMF(e + 0xF0) - v->bx) + (MEMF(e + 0xF4) - v->by) * (MEMF(e + 0xF4) - v->by);
            if (rd2 < nbest) { nbest = rd2; nearest = e; }
        }
        if (strike_on_me && !was_me) { hp_at = (int32_t)MEM32(v->p2 + 0x2FC); strike_seen = strike_on_me; }
        if (!strike_on_me && was_me) {
            int32_t hp = (int32_t)MEM32(v->p2 + 0x2FC);
            if (hp >= hp_at && !coop_hero_down(1)) { hit_until = v->now + 3500u; tt_dbg("[P2BOT] boss: parried %s's strike (health %d): attacking\n", coop_nm(strike_seen), hp); }
            else tt_dbg("[P2BOT] boss: %s's strike got through (health %d -> %d)\n", coop_nm(strike_seen), hp_at, hp);
        }
        was_me = strike_on_me != 0;
        {   /* which idle tentacle: one at random among those stretched, kept 3-6 s, then picked again (a playtest: "randomly
             * switch target between idle tentacles"); the one picked while it stays stretched */
            int q, have = -1;
            for (q = 0; q < nc; q++) if (cand[q] == pick) have = q;
            if (nc && (have < 0 || v->now >= pick_until)) {
                seed = seed * 1103515245u + 12345u + v->now; have = (int)((seed >> 16) % (uint32_t)nc);
                if (cand[have] != pick) tt_dbg("[P2BOT] boss: on %s (one of %d idle tentacles, at random)\n", coop_nm(cand[have]), nc);
                pick = cand[have]; pick_until = v->now + 3000u + (seed >> 8) % 3000u;
            }
            if (have >= 0) { stretched = cand[have]; sx = csx[have]; sy = csy[have]; rx = crx[have]; ry = cry[have]; sbest = cd2[have]; }
        }
        tn = stretched ? stretched : nearest;
        if ((v->now < hit_until || !strike_on_me) && stretched) {   /* attacking an idle tentacle (and after a parry, 3.5 s) */
            float vx = rx - sx, vy = ry - sy, vn = sqrtf(vx * vx + vy * vy), mx, my, px, py, gx, gy, d, side;
            uint32_t c2 = MEM32(v->p2 + 0x494);
            if (vn > 1e-3f) { vx /= vn; vy /= vn; }
            {   /* (where: on the line from its root toward where it struck, TT_COOP_BOTTENTREACH (8) units from the root, or the
                 * strike point if nearer -- player 1's sword hit a stretched tentacle 8.9 units from its root; at the strike
                 * point, 24-34 units out when the target stood far, the bot's 154 swings in the hit window touched nothing;
                 * facing the root) */
                float reach = getenv("TT_COOP_BOTTENTREACH") ? (float)atof(getenv("TT_COOP_BOTTENTREACH")) : 8.0f;
                if (vn > reach) { mx = rx - vx * reach; my = ry - vy * reach; } else { mx = sx; my = sy; }
            }
            px = -vy; py = vx; side = 0;
            gx = mx; gy = my;
            v->boss = 1; v->mode = 2; s_bot_following = 0; v->foe = stretched; v->bd = sbest;
            if (c2 >= 0x10000u && !getenv("TT_COOP_BOTNOTENTLOCK")) { MEM32(c2 + 0x98) = stretched; coop_bot_tgt_hold = stretched; coop_bot_tgt_until = v->now + 400u; }
            d = coop_bot_go(&s_bk, o, v->p2, gx, gy, 0.8f);
            if (getenv("TT_COOP_TENTATKLOG")) { static uint32_t tl; if (v->now - tl >= 250u) { tl = v->now;   /* (research) the attack on a stretched tentacle */
                tt_dbg("[TENTATK] %s state %u | P2 at %.1f %.1f, %.1f from the goal, %.1f from the strike point, %.1f from the root | his state %u +466 %u | heading %.2f | list %08X %08X %08X\n",
                       coop_nm(stretched), MEM32(stretched + 0x408), v->bx, v->by, d, sqrtf((v->bx - sx) * (v->bx - sx) + (v->by - sy) * (v->by - sy)),
                       sqrtf((v->bx - rx) * (v->bx - rx) + (v->by - ry) * (v->by - ry)), MEM32(v->p2 + 0x408), MEM8(v->p2 + 0x466), MEMF(v->p2 + 0x60),
                       MEM32(0x43B0E8u), MEM32(0x43B0ECu), MEM32(0x43B0F0u));
                {   extern uint32_t coop_wobjs[]; extern int coop_nwobjs; int q;   /* its own weapon objects: where the stretched body is */
                    for (q = 0; q < coop_nwobjs; q++) { uint32_t wo = coop_wobjs[q]; if (MEM32(wo + 0x11C) != stretched) continue;
                        tt_dbg("[TENTATK]   its %s at %.1f %.1f %.1f: %.1f from the root, %.1f from P2\n", coop_nm(wo), MEMF(wo + 0xF0), MEMF(wo + 0xF4), MEMF(wo + 0xF8),
                               sqrtf((MEMF(wo + 0xF0) - rx) * (MEMF(wo + 0xF0) - rx) + (MEMF(wo + 0xF4) - ry) * (MEMF(wo + 0xF4) - ry)),
                               sqrtf((MEMF(wo + 0xF0) - v->bx) * (MEMF(wo + 0xF0) - v->bx) + (MEMF(wo + 0xF4) - v->by) * (MEMF(wo + 0xF4) - v->by))); } } } }
            if (d <= 0.8f) { uint32_t cm = MEM32(0x43B164u); float fxw = vx, fyw = vy; (void)px; (void)py; (void)side;
                if (cm >= 0x10000u) { float fx = MEMF(cm + 0x40) - MEMF(cm + 0x50), fy = MEMF(cm + 0x44) - MEMF(cm + 0x54), fn = sqrtf(fx * fx + fy * fy);
                    if (fn > 1e-3f) { fx /= fn; fy /= fn; o->ly = (int16_t)((fxw * fx + fyw * fy) * 9000.0f); o->lx = (int16_t)((fxw * fy - fyw * fx) * 9000.0f); } }
                coop_bot_attack(o, v->now); }
            bstate = 3;
        } else if (nearest) {   /* to where a tentacle strikes, parrying only there: its strike point, or the point
                                 * 8 units from its root toward it when that is further (TT_COOP_BOTTENTREACH; a strike point
                                 * is where its target stood, often out of its reach) */
            float gx = 0, gy = 0, ux = MEMF(nearest + 0xF0) - v->gcx, uy = MEMF(nearest + 0xF4) - v->gcy, un = sqrtf(ux * ux + uy * uy), kd2 = 1e18f, d;
            float reach = getenv("TT_COOP_BOTTENTREACH") ? (float)atof(getenv("TT_COOP_BOTTENTREACH")) : 8.0f;
            int known = 0;
            for (k = 0; k < 4; k++) if (to[k] && (spx[k] != 0 || spy[k] != 0)) {
                float ox = MEMF(to[k] + 0xF0), oy = MEMF(to[k] + 0xF4), lx = spx[k] - ox, ly = spy[k] - oy, ln = sqrtf(lx * lx + ly * ly), cx = spx[k], cy = spy[k], dx, dy;
                if (ln > reach) { cx = ox + lx / ln * reach; cy = oy + ly / ln * reach; }
                dx = cx - v->bx; dy = cy - v->by;
                if (dx * dx + dy * dy < kd2) { kd2 = dx * dx + dy * dy; gx = cx; gy = cy; known = 1; } }
            if (!known) { if (un > 1e-3f) { ux /= un; uy /= un; } gx = v->gcx + ux * v->gr * 0.6f; gy = v->gcy + uy * v->gr * 0.6f; }
            v->boss = 1; v->mode = 2; s_bot_following = 0; v->foe = nearest; v->bd = nbest;
            d = coop_bot_go(&s_bk, o, v->p2, gx, gy, 2.0f);
            if (strike_on_me) { if (v->now % 450u < 200u) o->analog[5] = 255; bstate = 2; }   /* (parrying only a strike at him: a playtest -- "parry less often") */
            else bstate = 5;
            (void)known;
        } else bstate = 4;   /* (no tentacle up: the other tasks) */
    }
    if (w && v->now - blog > 2000u) { uint32_t c1 = MEM32(v->p1 + 0x494), c2 = MEM32(v->p2 + 0x494); blog = v->now;
        tt_dbg("[P2BOT] boss: %s | Watcher %08X state %u hp %d z %.1f | locks: P1 %08X P2 %08X | tentacle %08X %.1f away (state %u)\n",
               selfdef ? "an enemy on him first" : bstate == 2 ? "parrying by a tentacle" : bstate == 3 ? "hitting a stretched tentacle" : bstate == 5 ? "going to a tentacle" : "no tentacle up", w, MEM32(w + 0x408), (int32_t)MEM32(w + 0x2FC), MEMF(w + 0xF8),
               c1 >= 0x10000u ? MEM32(c1 + 0x98) : 0, c2 >= 0x10000u ? MEM32(c2 + 0x98) : 0, tn, tn ? sqrtf(tbd < 0 ? tbd + 2500.0f : tbd) : 0.0f, tn ? MEM32(tn + 0x408) : 0); }
    return v->boss;
}

/* (test) TT_COOP_PAD2SEEK=2: player 2 to the nearest living enemy, X and Y in turn beside it -- as TT_COOP_P1SEEK=2 drives
 * player 1, so the two can be compared under one driver */
/* (test) TT_COOP_SEEKSEQ="2,2,3": the seek bots' buttons in that order (analog index: 2 X, 3 Y), one a beat, instead of X and Y in turn */
int coop_seek_btn(uint32_t now)
{
    const char *s = getenv("TT_COOP_SEEKSEQ"); int v[16], n = 0;
    if (!s) return (now / 600) & 1 ? 3 : 2;
    while (*s && n < 16) { v[n++] = atoi(s); while (*s && *s != ',') s++; if (*s) s++; }
    return n ? v[(now / 600) % (uint32_t)n] : 2;
}
void coop_seek_near(CoopPad *o)
{
    static CoopSeek k2 = { 1, 0 }; uint32_t g = 0, h = coop_p2; float bd = 1e18f; int i;
    memset(o, 0, sizeof *o);
    for (i = 0; i < coop_nchars; i++) { uint32_t e = coop_chars[i]; float ex, ey;
        if (e == h || e == coop_first_char || MEM32(e) != 0x1741C0u || MEM32(e + 0x114) != 3 || (int32_t)MEM32(e + 0x2FC) <= 0) continue;
        ex = MEMF(e + 0xF0) - MEMF(h + 0xF0); ey = MEMF(e + 0xF4) - MEMF(h + 0xF4); if (ex * ex + ey * ey < bd) { bd = ex * ex + ey * ey; g = e; } }
    if (g && coop_bot_go(&k2, o, h, MEMF(g + 0xF0), MEMF(g + 0xF4), 4.6f) <= 4.6f && coop_now() % 600 < 200) o->analog[coop_seek_btn(coop_now())] = 255;
}

/* 3c. ranged: inside the group's circle with no enemy near him and ammo left, a shot from where he stands at one within
 * 35 units (45 was tried and set back), never at the boss or during the Watcher's fight. The game's own target for him
 * first (a playtest: "if he can target an enemy with the targeting system he can shoot at the target"): a living enemy,
 * not the boss, in range. Else those after a hero first (others count as twice as far). He fires only on the game's
 * lock (coop_bot_shoot); an enemy it doesn't lock within 2 s of aiming is left out of his shots for 5 s.
 * TT_COOP_BOTNORANGED: off. 1 = the frame is his */
static int coop_bot_ranged(BotView *v, CoopPad *o)
{
    static uint32_t nl_e, nl_until, aim_e, aim_t;
    uint32_t c2, gt, rf = 0; float rb = 1e18f, rd = 0; int i;
    if (v->near || v->bossfight || v->dc > v->gr || (int16_t)MEM16(v->p2 + 0x304) <= 0 || getenv("TT_COOP_BOTNORANGED")) return 0;
    c2 = MEM32(v->p2 + 0x494); gt = c2 >= 0x10000u ? MEM32(c2 + 0x98) : 0;
    if (gt >= 0x10000u && gt < 0x84000000u && gt != v->p1 && gt != v->p2 && MEM32(gt) == 0x1741C0u && MEM32(gt + 0x114) == 3 && (int32_t)MEM32(gt + 0x2FC) > 0
        && !coop_boss_kind(gt) && !(gt == nl_e && v->now < nl_until)) {
        float de = (MEMF(gt + 0xF0) - v->bx) * (MEMF(gt + 0xF0) - v->bx) + (MEMF(gt + 0xF4) - v->by) * (MEMF(gt + 0xF4) - v->by);
        if (de <= 1225.0f) { rf = gt; rd = de; }
    }
    if (!rf) for (i = 0; i < coop_nchars; i++) {
        uint32_t e = coop_chars[i], ce, t; float de, s;
        if (e == v->p1 || e == v->p2 || MEM32(e) != 0x1741C0u || MEM32(e + 0x114) != 3 || (int32_t)MEM32(e + 0x2FC) <= 0 || coop_boss_kind(e)) continue;
        if (e == nl_e && v->now < nl_until) continue;
        de = (MEMF(e + 0xF0) - v->bx) * (MEMF(e + 0xF0) - v->bx) + (MEMF(e + 0xF4) - v->by) * (MEMF(e + 0xF4) - v->by);
        if (de > 1225.0f) continue;
        ce = MEM32(e + 0x494); t = ce >= 0x10000u ? MEM32(ce + 0x98) : 0;
        s = t == v->p1 || t == v->p2 ? de : de * 4.0f;
        if (s < rb) { rb = s; rd = de; rf = e; }
    }
    if (!rf) return 0;
    {   float ox = MEMF(rf + 0xF0) - v->gcx, oy = MEMF(rf + 0xF4) - v->gcy;
        if (rf != aim_e) { aim_e = rf; aim_t = v->now; }
        v->mode = 3; s_bot_following = 0; v->foe = rf; v->bd = rd;
        if (coop_bot_shoot(o, v->p2, rf, 1)) aim_t = v->now;
        else if (v->now - aim_t > 2000u) { nl_e = rf; nl_until = v->now + 5000u; aim_e = 0; tt_dbg("[P2BOT] the game didn't lock %08X in 2 s of aiming: left out of his shots for 5 s\n", rf); }
        s_bot_why = rf == gt ? "ranged: the game's target for him" : ox * ox + oy * oy > v->gr * v->gr ? "ranged: he is in the circle, the enemy out of it" : "ranged: the enemy is in the circle but not near him";
    }
    return 1;
}

/* 3d. melee: he walks right up to the enemy and swings only beside it (coop_bot_beside: no swings at the air), with
 * now and then a block (coop_bot_defend). Out of the circle with none near him and ammo left he goes back in first,
 * to shoot from there (not a long walk to a melee, which "seems too far" in play). 1 = the frame is his */
static int coop_bot_melee(BotView *v, CoopPad *o)
{
    float df;
    if (!v->foe) return 0;
    if (!v->near && v->dc > v->gr && (int16_t)MEM16(v->p2 + 0x304) > 0 && !v->bossfight && !getenv("TT_COOP_BOTNORANGED")) { v->foe = 0; return 0; }
    s_bot_why = v->near ? "melee: an enemy near him" : "melee: walking up (no shot: no ammo, or nothing to shoot)";
    df = sqrtf(v->bd);
    coop_bot_go(&s_bk, o, v->p2, MEMF(v->foe + 0xF0), MEMF(v->foe + 0xF4), 1.2f);
    v->mode = 2; s_bot_following = 0;
    if (coop_bot_beside(o, v->p2, v->foe, df, v->now)) { s_bot_how = ", beside it: swinging"; if (!coop_bot_defend(o, v->p2, v->now)) coop_bot_attack(o, v->now); }
    else s_bot_how = ", walking to it";
    return 1;
}

/* 3d. the cave troll (Balin's Tomb; a playtest: the bot swung at its chain, TrollChainObj, an enemy character of its
 * own beside it, and never at the troll). Within 30 units and awake (its state not 0): while it swings (+0x49C bit
 * 0x400 at him, or an attack box of its) at player 2, he parries (White, 0.2 s of each 0.45 s) and goes
 * back after; else he stands TT_COOP_BOTTROLLREACH (6.5) units from its middle on his side -- its body stops a hero at
 * about 6, and player 1's hits (85-105 each) landed from 6 to 10 -- facing it, and attacks. A swing at player 1 isn't
 * parried (it swung at him every 0.4 s in the runs: the bot parried all the time and never hit). TT_COOP_BOTNOTROLL: off. 1 = the frame is his */
static int coop_bot_troll(BotView *v, CoopPad *o)
{
    static uint32_t tlog, swing_seen; uint32_t t = 0, c; int i, striking, at_me; float tx, ty, dx, dy, d, reach, gx, gy, dg;
    if (getenv("TT_COOP_BOTNOTROLL")) return 0;
    for (i = 0; i < coop_nchars; i++) { uint32_t e = coop_chars[i]; float de;
        if (coop_boss_kind(e) != 3 || (int32_t)MEM32(e + 0x2FC) <= 0 || !MEM32(e + 0x408)) continue;
        de = (MEMF(e + 0xF0) - v->bx) * (MEMF(e + 0xF0) - v->bx) + (MEMF(e + 0xF4) - v->by) * (MEMF(e + 0xF4) - v->by);
        if (de <= 900.0f) { t = e; break; } }
    if (!t) return 0;
    reach = getenv("TT_COOP_BOTTROLLREACH") ? (float)atof(getenv("TT_COOP_BOTTROLLREACH")) : 6.5f;
    tx = MEMF(t + 0xF0); ty = MEMF(t + 0xF4); dx = v->bx - tx; dy = v->by - ty; d = sqrtf(dx * dx + dy * dy);
    c = MEM32(t + 0x494); at_me = c >= 0x10000u && MEM32(c + 0x98) == v->p2;
    {   /* its swing: an attack box of its tested in the last 0.3 s (sub_000BE0D0; its weapons' live flag +0x110 0x20000 was on
         * nearly all the time too); +0x49C bit 0x400 is on for most of
         * its state 15 (65 of 117 samples in a run) -- the bot parried all the time near it and never hit -- so it counts
         * only when he is its target */
        extern uint32_t coop_troll_seen, coop_troll_box_t; int live;
        coop_troll_seen = t; live = coop_troll_box_t && v->now - coop_troll_box_t < 300u;
        striking = live || ((MEM32(t + 0x49C) & 0x400u) != 0 && at_me);
    }
    v->mode = 2; s_bot_following = 0; v->foe = t; v->bd = d * d; v->boss = 1;
    if (striking && at_me) {   /* its arm up at him: a parry until it is down again (one at player 1: he keeps attacking) */
        if (!swing_seen) { swing_seen = v->now | 1; tt_dbg("[P2BOT] troll swings (%s, %.1f away): parrying\n", at_me ? "at him" : "near him", d); }
        if (v->now % 450u < 200u) o->analog[5] = 255;
        s_bot_why = "troll: parrying its swing"; s_bot_how = "";
    } else {
        if (swing_seen) { swing_seen = 0; tt_dbg("[P2BOT] troll's swing over: back at it\n"); }
        if (d > 1e-3f) { gx = tx + dx / d * reach; gy = ty + dy / d * reach; } else { gx = tx + reach; gy = ty; }
        dg = coop_bot_go(&s_bk, o, v->p2, gx, gy, 0.8f);
        if (dg <= 0.8f || d <= reach + 1.0f) {   /* at its edge: face it, attack */
            uint32_t cm = MEM32(0x43B164u);
            if (cm >= 0x10000u && d > 1e-3f) { float wx = -dx / d, wy = -dy / d, fx = MEMF(cm + 0x40) - MEMF(cm + 0x50), fy = MEMF(cm + 0x44) - MEMF(cm + 0x54), fn = sqrtf(fx * fx + fy * fy);
                if (fn > 1e-3f) { fx /= fn; fy /= fn; o->ly = (int16_t)((wx * fx + wy * fy) * 9000.0f); o->lx = (int16_t)((wx * fy - wy * fx) * 9000.0f); } }
            coop_bot_attack(o, v->now);
            s_bot_why = "troll: attacking it"; s_bot_how = "";
        } else { s_bot_why = "troll: going to it"; s_bot_how = ""; }
    }
    if (v->now - tlog > 2000u) { tlog = v->now; tt_dbg("[P2BOT] troll %08X state %u hp %d, %.1f away, swinging %d at %s: %s\n", t, MEM32(t + 0x408), (int32_t)MEM32(t + 0x2FC), d, striking, at_me ? "him" : coop_nm(c >= 0x10000u ? MEM32(c + 0x98) : 0), s_bot_why); }
    return 1;
}

/* 3e. nothing to fight. Closer than 3 units to player 1: a step away (he was in his way). Out of the circle: follow,
 * half a second after (the goal just inside the circle on his side, not onto player 1). Stuck (under 1 unit in 1.5 s
 * with the stick held): a 0.7 s sidestep, left then right (the Fellowship stands on Gates of Moria's narrow path);
 * still stuck 3 s and over 12 units behind: put beside player 1 (coop_place, on the path) */
static void coop_bot_follow(BotView *v, CoopPad *o)
{
    static uint32_t fol_t, chk_t, stuck_t, direct_t; static float chk_x, chk_y; static int side;
    float gx = v->qx, gy = v->qy;
    if (v->d1 < 3.0f && !s_bot_following) {
        float ux = v->bx - v->qx, uy = v->by - v->qy, un = sqrtf(ux * ux + uy * uy); if (un < 1e-3f) { ux = 1.0f; uy = 0; un = 1.0f; }
        coop_bot_steer(&s_bk, o, v->p2, v->qx + ux / un * 5.0f, v->qy + uy / un * 5.0f, 0.5f); v->mode = 1;
        return;
    }
    if (v->dc > 1e-3f) { gx = v->gcx + (v->bx - v->gcx) / v->dc * v->gr * 0.75f; gy = v->gcy + (v->by - v->gcy) / v->dc * v->gr * 0.75f; }
    if (v->dc > v->gr && !s_bot_following) { s_bot_following = 1; fol_t = v->now; chk_t = v->now; chk_x = v->bx; chk_y = v->by; stuck_t = 0; }
    if (v->dc < v->gr * 0.8f) s_bot_following = 0;
    v->mode = s_bot_following;
    if (!s_bot_following || v->now - fol_t < 500u) return;
    if (v->now - chk_t >= 1500u) {
        float mv = sqrtf((v->bx - chk_x) * (v->bx - chk_x) + (v->by - chk_y) * (v->by - chk_y));
        if (mv < 1.0f) { if (!stuck_t) stuck_t = v->now; direct_t = v->now; side ^= 1; tt_dbg("[P2BOT] stuck (%.2f units in 1.5 s, player 1 %.1f away): a sidestep | last stick %d %d, state %u, +110 %08X, pathmap %d, leash stops %u\n",
            mv, v->d1, bot_lx, bot_ly, MEM32(v->p2 + 0x408), MEM32(v->p2 + 0x110), (int)MEM32(v->p2 + 0x2EC), coop_leash_stops); }
        else stuck_t = 0;
        chk_t = v->now; chk_x = v->bx; chk_y = v->by;
    }
    if (stuck_t && v->now - stuck_t >= 3000u && v->d1 > 12.0f) {
        stuck_t = 0; coop_place_frames = 3; s_bot_following = 0;
        tt_dbg("[P2BOT] still stuck, %.1f from player 1: put beside him\n", v->d1);
    } else if (v->now - direct_t < 700u) {
        float ux = gx - v->bx, uy = gy - v->by, un = sqrtf(ux * ux + uy * uy);
        if (un > 1e-3f) { ux /= un; uy /= un; }
        coop_bot_steer(&s_bk, o, v->p2, v->bx + (side ? -uy : uy) * 3.0f + ux, v->by + (side ? ux : -ux) * 3.0f + uy, 0.2f);
    } else {   /* player 1's footsteps, from the nearest on (coop_steps); none near, or past the last: straight to the goal */
        float sx, sy;
        if (getenv("TT_COOP_BOTFOOTSTEPS") && v->d1 > 4.0f && coop_steps_next(v->bx, v->by, &sx, &sy)) {
            static uint32_t sl; if (getenv("TT_COOP_STEPSLOG") && v->now - sl > 1000u) { sl = v->now; tt_dbg("[P2BOT] following player 1's footsteps: to %.1f %.1f (%d kept), player 1 %.1f away\n", sx, sy, coop_steps_n, v->d1); }
            coop_bot_steer(&s_bk, o, v->p2, sx, sy, 0.3f);
        } else if (coop_bot_go(&s_bk, o, v->p2, gx, gy, 2.0f) <= 2.0f) s_bot_following = 0;   /* (at the goal: in the circle) */
    }
}

/* (1.9.7, Versus) the P2 Bot fights player 1: walks up, swings beside him in beats, parries now and then. 1 = the frame is his */
static int coop_bot_versus(CoopPad *o, uint32_t now, uint32_t p1, uint32_t p2)
{
    if (!tt_vs_on() || coop_hero_down(0) || (int32_t)MEM32(p1 + 0x2FC) <= 0) return 0;
    {
        static uint32_t seed = 4242u, beat = ~0u, vlog; static int act, late;
        float df = sqrtf((MEMF(p1 + 0xF0) - MEMF(p2 + 0xF0)) * (MEMF(p1 + 0xF0) - MEMF(p2 + 0xF0)) + (MEMF(p1 + 0xF4) - MEMF(p2 + 0xF4)) * (MEMF(p1 + 0xF4) - MEMF(p2 + 0xF4)));
        int bs; uint32_t bt = now / 1600u, ph = now % 1600u;
        if (getenv("TT_VS_BOTSHOOT")) {   /* (testing) the bot only shoots at player 1, from where it stands */
            coop_bot_shoot(o, p2, p1, 1);
            if (now - vlog > 3000u) { uint32_t c2 = MEM32(p2 + 0x494); vlog = now; tt_dbg("[VSBOT] shooting at player 1 (%.1f away), his lock %08X\n", df, c2 >= 0x10000u ? MEM32(c2 + 0x98) : 0); }
            bot_lx = o->lx; bot_ly = o->ly;
            return 1;
        }
        if (df > 3.2f) coop_bot_go(&s_bk, o, p2, MEMF(p1 + 0xF0), MEMF(p1 + 0xF4), 3.2f);   /* (a hero's swing reaches about 4 units) */
        bs = df <= 4.3f;
        if (bs) {   /* in reach: turned toward him (the stick a little his way, camera-relative), swinging in beats */
            uint32_t cm = MEM32(0x43B164u);
            float wx = MEMF(p1 + 0xF0) - MEMF(p2 + 0xF0), wy = MEMF(p1 + 0xF4) - MEMF(p2 + 0xF4), wn = sqrtf(wx * wx + wy * wy);
            if (cm >= 0x10000u && wn > 1e-3f) {
                float fx = MEMF(cm + 0x40) - MEMF(cm + 0x50), fy = MEMF(cm + 0x44) - MEMF(cm + 0x54), fn = sqrtf(fx * fx + fy * fy);
                if (fn > 1e-3f) { wx /= wn; wy /= wn; fx /= fn; fy /= fn; o->ly = (int16_t)((wx * fx + wy * fy) * 6000.0f); o->lx = (int16_t)((wx * fy - wy * fx) * 6000.0f); }
            }
        }
        if (bt != beat) { beat = bt; seed = seed * 1103515245u + 12345u; act = (int)((seed >> 16) % 15); late = (int)((seed >> 8) % 300); }
        if (bs) {
            if (act < 3) { if (ph >= (uint32_t)late && ph < (uint32_t)late + 500u) o->analog[5] = 255; }
            else if (act < 12) coop_bot_attack(o, now);
        }
        if (now - vlog > 3000u) { vlog = now; tt_dbg("[VSBOT] player 1 %.1f away, %s\n", df, df <= 4.6f ? (act < 3 ? "parrying" : "attacking") : "walking up"); }
        bot_lx = o->lx; bot_ly = o->ly;
        return 1;
    }
}
void coop_p2bot(CoopPad *o)
{
    static uint32_t tlog; static int lastmode = -1;
    BotView v;
    memset(&v, 0, sizeof v);
    v.now = coop_now(); v.p1 = coop_first_char; v.p2 = coop_p2;
    if (!tt_coop_setting(4) || v.p1 < 0x10000u || v.p2 < 0x10000u || v.p1 == v.p2 || !coop_p2_t0) return;
    if ((getenv("TT_COOP_PAD2SCRIPT") || getenv("TT_COOP_PAD2SEEK")) && !getenv("TT_COOP_P2BOT")) return;
    if (!coop_bot_pad_free(o, v.now)) return;
    memset(o, 0, sizeof *o);
    coop_bot_drove_t = v.now;
    if (tt_in_scene() || coop_hero_down(1)) return;
    if (coop_bot_versus(o, v.now, v.p1, v.p2)) return;
    if (getenv("TT_COOP_P2AIMTEST")) { coop_bot_aimtest(o, v.now, v.p1, v.p2); return; }
    v.bx = MEMF(v.p2 + 0xF0); v.by = MEMF(v.p2 + 0xF4);
    if (coop_bot_roam(&v, o)) { bot_lx = o->lx; bot_ly = o->ly; return; }
    coop_bot_view(&v);
    coop_bot_pick_melee(&v);
    s_bot_how = "";
    if (!coop_bot_boss(&v, o) && !coop_bot_troll(&v, o) && !coop_bot_ranged(&v, o) && !coop_bot_melee(&v, o)) coop_bot_follow(&v, o);
    bot_lx = o->lx; bot_ly = o->ly;
    if (v.mode != lastmode || v.now - tlog > 1000) { tlog = v.now; lastmode = v.mode;
        tt_dbg("[P2BOT] %s: player 1 %.1f away, rotation %.0f deg (%u samples)\n", v.mode == 3 ? "shooting" : v.mode == 2 ? "fighting" : v.mode ? "following" : "beside player 1", v.d1,
               atan2f(s_bk.s, s_bk.c) * 57.29578f, s_bk.samples);
        if (v.foe) tt_dbg("[P2BOT]   enemy %08X %.1f away%s%s%s\n", v.foe, sqrtf(v.bd), s_bot_why && !v.boss ? " -- " : "", s_bot_why && !v.boss ? s_bot_why : "", v.boss ? "" : s_bot_how); }
}
static int coop_p1pad_ry, coop_p1pad_lt;   /* (bot test) player 1's idle pad as last read: right stick Y, LT */
/* (research, the Watcher's fight) a character's name, and whether it is the boss: 1 a tentacle, 2 the Watcher */
static const char *coop_nm(uint32_t a)
{
    uint32_t h;
    if (a < 0x10000u || a >= 0x84000000u) return "-";
    if (a == coop_first_char) return "P1";
    if (a == coop_p2) return "P2";
    h = MEM32(a + 0x18);
    return h >= 0x10000u && h < 0x84000000u ? (const char *)XBOX_PTR(h) : "?";
}
const char *coop_nm_x(uint32_t a) { return coop_nm(a); }
static int coop_boss_kind(uint32_t a)
{
    const char *n;
    if (a < 0x10000u || a >= 0x84000000u || MEM32(a) != 0x1741C0u) return 0;
    n = coop_nm(a);
    return !strncmp(n, "Tentacle", 8) ? 1 : !strncmp(n, "WatcherObj", 10) ? 2 : !strncmp(n, "TrollBoss", 9) ? 3 : !strncmp(n, "TrollChain", 10) ? 4 : 0;
}
static int coop_who_kind(uint32_t a) { int b = coop_boss_kind(a); return b ? b : a == coop_first_char ? 3 : a == coop_p2 ? 4 : a >= 0x80000000u && a < 0x84000000u && MEM32(a) == 0x174980u ? 5 : 6; }
/* XInputGetState (XAPI, stdcall handle, XINPUT_STATE*) */
extern void sub_001697D9_gen(void);
/* (Versus) the pads' ranged rules: no bow at all on Weathertop and the prologue (the levels have none in the story);
 * elsewhere a shot (A with LT held) only once a second (tt_vs_shot_t, the last launch per player) */
uint32_t tt_vs_shot_t[2];
int tt_vs_no_ranged(void) { uint32_t lv = MEM32(0x1BB2A4u); return lv == 0u || lv == 1u; }
int16_t tt_vs_stick[2][2];
uint8_t tt_vs_lt[2];   /* (Versus) each player's left trigger (the bow drawn) this frame */
static void vs_pad_rules(int who, uint32_t st)
{
    extern int tt_vs_on(void);
    if (who >= 0 && who < 2) { extern int16_t tt_vs_stick[2][2]; tt_vs_lt[who] = MEM8(st + 6 + 6); tt_vs_stick[who][0] = (int16_t)MEM16(st + 0xE); tt_vs_stick[who][1] = (int16_t)MEM16(st + 0x10); }
    if (!tt_vs_on() || getenv("TT_VS_FREERANGED")) return;
    if (tt_vs_no_ranged()) { MEM8(st + 6 + 6) = 0; return; }
    if (tt_vs_shot_t[who] && coop_now() - tt_vs_shot_t[who] < 1000u) MEM8(st + 6 + 0) = 0;
}
void sub_001697D9(void)
{
    uint32_t h = MEM32(esp + 4), st = MEM32(esp + 8);
    if (getenv("TT_COOP_PADPOLLLOG")) {   /* (research) the handles the game polls, per screen, once a second each */
        static uint32_t lt[8], lh[8]; int q; uint32_t now = coop_now(), scr = MEM32(0x4AD050u);
        for (q = 0; q < 8 && lh[q] && lh[q] != (h ^ (scr << 24)); q++) ;
        if (q < 8 && now - lt[q] > 1000u) { lh[q] = h ^ (scr << 24); lt[q] = now; tt_dbg("[PADPOLL] screen %u handle %08X primary port %u%c", scr, h, MEM32(0x1CB8B4u), 10); }
    }
    {   /* (1.9.6) the devices: two players' input in a co-op game with a human player 2 (options.c) */
        extern void tt_coop_input_sync(void); uint32_t sa = eax, sc = ecx, sd = edx, sp = esp;
        tt_coop_input_sync(); eax = sa; ecx = sc; edx = sd; esp = sp; }
    if (tt_vs_on() && MEM32(0x4AD050u) == 6 && !getenv("TT_VS_KEEPUPGRADES")) {   /* (Versus) the pause list (cursor 0x4B5800, items 0x4B5808: Options, Exit to Menu,
                                                                                   * Show Upgrades) without its last item: every move is anyone's in Versus */
        if (MEM32(0x4B5808u) == 3u) MEM32(0x4B5808u) = 2u;
        if (MEM32(0x4B5800u) >= 2u && MEM32(0x4B5800u) < 8u) MEM32(0x4B5800u) = 0u;
    }
    if (h == 0xFEED0000u) {   /* (bot test, TT_COOP_P1PORT) player 1's idle pad on port 0: a pad at rest */
        uint32_t i; static uint32_t packet; MEM32(st) = ++packet; for (i = 4; i < 0x16; i += 2) MEM16(st + i) = 0;
        { extern void (*volatile xbox_pad_filter)(unsigned long port, void *st); if (xbox_pad_filter) xbox_pad_filter(0, (void *)XBOX_PTR(st)); }   /* (TT_P1SCRIPT_INPLAY scripts it) */
        vs_pad_rules(0, st);
        coop_p1pad_ry = (int16_t)MEM16(st + 0x14); coop_p1pad_lt = MEM8(st + 6 + 6);
        eax = 0; esp += 12; return;
    }
    if (h == COOP_VPAD) {
        static uint32_t packet; CoopPad p; uint32_t i;
        coop_pad2(&p);
        { extern int coop_hero_down(int); extern int tt_vs_frozen(void); if (coop_hero_down(1) || tt_vs_frozen()) memset(&p, 0, sizeof p); }   /* no input while down (or in Versus' countdown) */
        MEM32(st) = ++packet;
        MEM16(st + 4) = p.buttons;
        for (i = 0; i < 8; i++) MEM8(st + 6 + i) = p.analog[i];
        MEM16(st + 0xE) = (uint16_t)p.lx; MEM16(st + 0x10) = (uint16_t)p.ly; MEM16(st + 0x12) = (uint16_t)p.rx; MEM16(st + 0x14) = (uint16_t)p.ry;
        vs_pad_rules(1, st);
        eax = 0; esp += 12; return;   /* ret 8 */
    }
    if (coop_mode()) {   /* (research) which handles the game reads, against the port records' +0x144 */
        static uint32_t seen[4]; static int ns, lg; int i; for (i = 0; i < ns && seen[i] != h; i++) ;
        if (i == ns && ns < 4) { seen[ns++] = h; if (lg++ < 6) tt_dbg("[COOPPADH] XInputGetState handle %08X | port records +144: %08X %08X %08X %08X\n", h, MEM32(COOP_PADREC(0) + 0x144), MEM32(COOP_PADREC(1) + 0x144), MEM32(COOP_PADREC(2) + 0x144), MEM32(COOP_PADREC(3) + 0x144)); }
    }
    extern int coop_hero_down(int); extern int coop_is_gameover(void);
    extern int tt_vs_frozen(void);
    if (!getenv("TT_COOP_P1SEEK") && h && ((coop_hero_down(0) && coop_p2 && coop_p2_t0 && coop_first_char >= 0x10000u)
                                          || (tt_vs_frozen() && MEM32(0x4AD050u) == 6 && MEM32(0x43B15Cu) >= 0x10000u))) {   /* (Versus: from the level's first frame, before player 2 is in too -- player 1 walked off in the first seconds) */
        /* player 1 down: his sticks and fight buttons held back, the rest kept (Start, Back, the D-pad, A: the pause menu).
         * At game over nothing is held back: the whole pad zeroed left "Replay mission / Exit to menu" deaf -- "Try
         * again freezes the game" in a playtest */
        sub_001697D9_hle();
        if (!coop_is_gameover()) {
            uint16_t b = MEM16(st + 4); uint32_t i;
            MEM16(st + 4) = (uint16_t)(b & 0x003Fu);   /* d-pad, Start, Back */
            for (i = tt_vs_frozen() ? 0 : 1; i < 8; i++) MEM8(st + 6 + i) = 0;   /* (analog 0 = A kept; not in Versus' countdown) */
            MEM16(st + 0xE) = 0; MEM16(st + 0x10) = 0; MEM16(st + 0x12) = 0; MEM16(st + 0x14) = 0;
        }
        return;
    }
    if (getenv("TT_COOP_P1SEEK") && !coop_is_gameover() && coop_p2 && coop_p2_t0 && coop_first_char >= 0x10000u && h) {   /* player 1's pad (the only real handle; port record 2 here): zeroed while he is down; with TT_COOP_P1SEEK (bot test) his steering bot */
        CoopPad p; uint32_t i;
        sub_001697D9_hle();
        { extern volatile int tt_vs_return; if (tt_vs_return) return; }   /* (the way back to the Versus menu presses on this pad) */
        memset(&p, 0, sizeof p); { extern int coop_hero_down(int); if (!coop_hero_down(0) && !tt_vs_frozen()) {
            const char *sv = getenv("TT_COOP_P1SEEK");
            if (sv && !strcmp(sv, "2")) {   /* (test) TT_COOP_P1SEEK=2: player 1 to the nearest living enemy as the P2 Bot steers (camera, pathmap route) */
                static CoopSeek k1 = { 1, 0 }; uint32_t g = 0, h = coop_first_char; float bd = 1e18f; int i;
                for (i = 0; i < coop_nchars; i++) { uint32_t e = coop_chars[i]; float ex, ey;
                    if (e == h || e == coop_p2 || MEM32(e) != 0x1741C0u || MEM32(e + 0x114) != 3 || (int32_t)MEM32(e + 0x2FC) <= 0) continue;
                    ex = MEMF(e + 0xF0) - MEMF(h + 0xF0); ey = MEMF(e + 0xF4) - MEMF(h + 0xF4); if (ex * ex + ey * ey < bd) { bd = ex * ex + ey * ey; g = e; } }
                if (tt_vs_on() && coop_p2 >= 0x10000u) g = coop_p2;   /* (Versus: player 2) */
                if (g && coop_bot_go(&k1, &p, h, MEMF(g + 0xF0), MEMF(g + 0xF4), 4.6f) <= 4.6f && coop_now() % 600 < 200) { extern int coop_seek_btn(uint32_t now); p.analog[getenv("TT_COOP_P1SEEKBTN") ? atoi(getenv("TT_COOP_P1SEEKBTN")) : coop_seek_btn(coop_now())] = 255; }
            } else coop_seek_for(&p, coop_first_char, 0); } }
        MEM16(st + 4) = p.buttons; for (i = 0; i < 8; i++) MEM8(st + 6 + i) = p.analog[i];
        MEM16(st + 0xE) = (uint16_t)p.lx; MEM16(st + 0x10) = (uint16_t)p.ly; MEM16(st + 0x12) = 0; MEM16(st + 0x14) = 0;
        vs_pad_rules(0, st);
        return;
    }
    sub_001697D9_hle();
    if (h && h != COOP_VPAD) vs_pad_rules(0, st);
}

/* the game's pad poll: keep port 1's virtual device in place */
extern void sub_000E7DB0_gen(void);
void sub_000E7DB0(void)
{
    if (getenv("TT_COOP_P1PORT") && MEM32(COOP_PADREC(0) + 0x144) == 0) { MEM32(COOP_PADREC(0) + 0x144) = 0xFEED0000u; tt_dbg("[COOP2] test: port 0: an idle pad for player 1\n"); }
    if (coop_mode() && coop_p2 && MEM32(COOP_PADREC(1) + 0x144) == 0) {
        MEM32(COOP_PADREC(1) + 0x144) = COOP_VPAD;
        tt_dbg("[COOP2] port 1: virtual pad attached\n");
    }
    { extern void tt_mm_guard(void); tt_mm_guard(); }
    sub_000E7DB0_gen();
}

/* -- the per-player globals of the control step -- */
static struct { uint32_t a, n; } coop_blk[] = {
    { 0x43B114u, 0x48 }, { 0x43B168u, 0x58 }, { 0x43B1C1u, 0x5F }, { 0x43B298u, 0xF8 }, { 0x43B3A4u, 0x10 },
    { 0x1B0C40u, 0x20 }, { 0x1B1078u, 4 }, { 0x1B1080u, 0x24 }, { 0x43B15Cu, 4 }, { 0x43B160u, 4 }, { 0x1CB8B4u, 4 },
    { 0x43B264u, 0x20 } };
    /* (0x43B264..0x43B283: the combo block -- presses counted since the last attack and their timers, read by the combo
     * matcher 0x9A1F0 from the control step, which raises INP_COMBO1-5 (24, 39, 54, 56, 60) on [0x43B15C]. Shared, both
     * players' presses went into one count: an idle player 2 had 16 combo inputs raised on him from player 1's presses
     * (a bot run; a playtest asked player 2 his own combos, "p2 still affect p1 input reading for combo") */
    /* (0x1B1078: the grade of the player's current attack, set by his moves (0x98980 sets 3); a hit on an enemy raises the
     * combat meter by it (0x99E00 from 0x849F0: 0, 1 and 3 a raise, 2 the 0xB event). Shared, player 2's hits were graded by
     * player 1's last attack -- his killing blows raised nothing while player 1's raised 30 (bot runs, one driver for both;
     * a playtest: "p2 still gains very little experience after 8 kills") */
    /* (0x43B0E8, once swapped here as "the player's weapons list", is not per player: it is the list of objects the
     * level script puts up for the player's weapon test, @ForcePlayerWeaponObbTestOn -- the Watcher's stretched
     * tentacles. It was empty at every take-over, and player 2's copy of it stayed empty in the fight, so his swings
     * were never tested against a tentacle. Shared now.) */
#define COOP_NBLK (sizeof coop_blk / sizeof coop_blk[0])
static uint8_t coop_s1[0x800], coop_s2[0x800];
/* + the current hero's record (0x4AB598 + hero*0x348: upgrades, experience, the level's running experience and combat
 * rating), at +0x400 of each state: whatever runs in player 2's context uses his own copy (set at the take-over) */
uint32_t coop_rec_addr;
static void coop_blk_opts(void) { static int done; if (!done) { done = 1; if (getenv("TT_COOP_NOCOMBOSWAP")) coop_blk[COOP_NBLK - 1].n = 0; } }   /* (test: the combo block shared, as before) */
static void coop_save(uint8_t *s) { uint32_t i, j, o = 0; coop_blk_opts(); for (i = 0; i < COOP_NBLK; i++) for (j = 0; j < coop_blk[i].n; j++) s[o++] = MEM8(coop_blk[i].a + j); if (coop_rec_addr) for (j = 0; j < 0x348; j += 4) *(uint32_t *)(s + 0x400 + j) = MEM32(coop_rec_addr + j); }
static void coop_load(const uint8_t *s) { uint32_t i, j, o = 0; coop_blk_opts(); for (i = 0; i < COOP_NBLK; i++) for (j = 0; j < coop_blk[i].n; j++) MEM8(coop_blk[i].a + j) = s[o++]; if (coop_rec_addr) for (j = 0; j < 0x348; j += 4) MEM32(coop_rec_addr + j) = *(const uint32_t *)(s + 0x400 + j); }
const uint8_t *coop_p2_state(void) { return coop_s2; }
uint8_t *coop_p2_state_w(void) { return coop_s2; }

/* (bot test) "p1,p2,k1,k2": player 2's per-level stats +0x98 and +0x9C (record) set to k1, k2 as well */
static void coop_test_kc(const char *v)
{
    const char *c = strchr(v, ','); int32_t k[2]; int i;
    if (!c || !(c = strchr(c + 1, ','))) return;
    k[0] = atoi(c + 1); c = strchr(c + 1, ','); k[1] = c ? atoi(c + 1) : 0;
    for (i = 0; i < 2; i++) memcpy(coop_s2 + 0x400 + 0x98 + 4 * i, &k[i], 4);
    tt_dbg("[COOPEXP] test: player 2's +0x98 %d, +0x9C %d\n", k[0], k[1]);
}
static float coop_kept_p1_90, coop_kept_p2_90; static uint32_t coop_kept_level; static int coop_kept;   /* the level experience at a level's reset (coop_level_reset) */
/* both players' level experience (player 1's, player 2's) each time either changes, through a level and its checkpoint
 * reloads (cleared on another level): a checkpoint puts player 1's back to an earlier value, and player 2's goes back to
 * his at that time (coop_xph_note from the tick, read in coop_take) */
static float coop_xph[256][2]; static int coop_xph_n; static uint32_t coop_xph_level;
/* the last checkpoint taken in play (@SetRespawnId keeps player 1's level experience in it, and a reload gives it back):
 * both players' level experience then */
static float coop_ck_p1, coop_ck_p2; static uint32_t coop_ck_level; static int coop_ck_valid;
static uint8_t coop_ck_p2blk[32], coop_kept_p2blk[32];   /* player 2's other per-level stats, record +0x94..+0xB3 (the kill counts: @SetRespawnId keeps
                                                         * player 1's +0x90..+0xB3 in the checkpoint and a reload gives them back) */
void coop_ckpt_note(void)
{
    if (!coop_p2 || !coop_p2_t0 || !coop_rec_addr || coop_depth) return;
    coop_ck_p1 = MEMF(coop_rec_addr + 0x90); memcpy(&coop_ck_p2, coop_s2 + 0x400 + 0x90, 4); coop_ck_level = MEM32(0x1BB2A4u); coop_ck_valid = 1;
    memcpy(coop_ck_p2blk, coop_s2 + 0x400 + 0x94, 32);
    tt_dbg("[CHECKPT] checkpoint: level experience player 1 %.0f, player 2 %.0f kept for a reload\n", coop_ck_p1, coop_ck_p2);
}
void coop_xph_note(void)
{
    float a, b;
    if (!coop_p2 || !coop_rec_addr || coop_depth) return;
    if (MEM32(0x1BB2A4u) != coop_xph_level) { coop_xph_level = MEM32(0x1BB2A4u); coop_xph_n = 0; }
    a = MEMF(coop_rec_addr + 0x90); memcpy(&b, coop_s2 + 0x400 + 0x90, 4);
    if (coop_xph_n && coop_xph[coop_xph_n - 1][0] == a && coop_xph[coop_xph_n - 1][1] == b) return;
    if (coop_xph_n == 256) { memmove(coop_xph, coop_xph[1], sizeof coop_xph[0] * 255); coop_xph_n = 255; }
    coop_xph[coop_xph_n][0] = a; coop_xph[coop_xph_n][1] = b; coop_xph_n++;
}
/* player 2 takes over actor a: his own copy of the state, set up as SetPlayer (0x00099000) does */
static void coop_take(uint32_t a)
{
    uint32_t c = MEM32(a + 0x494), k;
    if (!coop_rec_addr && !getenv("TT_COOP_NOP2EXP")) { uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx; PUSH32(esp, 0x000D2310u); sub_000D2310(); coop_rec_addr = 0x4AB598u + eax * 0x348u; esp = sp; eax = r0; ecx = r1; edx = r2; }
    coop_save(coop_s1);
    MEM32(0x43B15Cu) = a; MEM32(0x43B160u) = c; MEM32(0x1CB8B4u) = 1;
    if (c) { MEM32(c) |= 0x8000u; MEM32(c + 0x114) = 0; }
    for (k = 0; k < 4; k++) { MEM32(0x43B3A4u + 4 * k) = MEM32(a + 0xF0 + 4 * k); MEM32(0x1B1080u + 4 * k) = MEM32(a + 0xF0 + 4 * k); }
    for (k = 0; k < 15; k++) { uint32_t e = 0x43B298u + 0x10 * k, j; for (j = 0; j < 4; j++) MEM32(e + 4 * j) = MEM32(a + 0xF0 + 4 * j); MEM32(0x43B114u + 4 * k) = 0x3F800000u; }
    MEM8(0x43B190u) = 0; MEM32(0x1B0C40u) = 0x447A0000u; MEM32(0x43B198u) = MEM32(0x43B19Cu) = MEM32(0x43B1A0u) = MEM32(0x43B1A4u) = 0;
    MEM8(0x43B214u) = MEM8(0x43B215u) = MEM8(0x43B216u) = 0; MEM32(0x1B0C5Cu) = 0xFFFFFFFFu;
    coop_save(coop_s2);
    if (coop_rec_addr) {   /* player 2's record: his hero's (TT_COOP_P2HERO = hero index; default player 1's hero), the level's running experience from 0 */
        uint32_t src = getenv("TT_COOP_P2HERO") ? 0x4AB598u + (uint32_t)atoi(getenv("TT_COOP_P2HERO")) * 0x348u : coop_rec_addr, j; float z = 0;
        for (j = 0; j < 0x348; j += 4) *(uint32_t *)(coop_s2 + 0x400 + j) = MEM32(src + j);
        if (getenv("TT_COOP_TESTEXP")) z = (float)atof(getenv("TT_COOP_TESTEXP"));   /* bot test: start his level's running experience here, to see the gauge fill */
        {   /* his character's own record when he is another hero (heroes.c's extra heroes: heroes\records.dat), from +0x64 on */
            extern const char *tt_coop_p2_char_now; extern int tt_heroes_record_of(const char *skin, unsigned char *out);
            if (tt_coop_p2_char_now) {
                int got = tt_heroes_record_of(tt_coop_p2_char_now, coop_s2 + 0x400);
                if (!got) memset(coop_s2 + 0x400 + 0x64, 0, 0x348 - 0x64);   /* a new hero: no experience, nothing bought (as heroes.c's rec_switch) */
                tt_dbg("[COOPEXP] player 2 is %s: his record %s (experience %d, level %d)\n", tt_coop_p2_char_now, got ? (strcmp(tt_coop_p2_char_now, "aragorn") && strcmp(tt_coop_p2_char_now, "legolas") && strcmp(tt_coop_p2_char_now, "gimli") && strcmp(tt_coop_p2_char_now, "isildur") ? "from heroes\\records.dat" : "from the save (his hero record)") : "new (none saved yet)",
                       *(int32_t *)(coop_s2 + 0x400 + 0x6C), *(int32_t *)(coop_s2 + 0x400 + 0x78));
            }
        }
        if (coop_kept && !getenv("TT_COOP_TESTEXP")) {   /* a retry of the same level (it used to erase his kills): his level experience
            * kept when the game kept player 1's, else from 0 as player 1's. TT_COOP_NORETRYKEEP: from 0 always */
            float p1now = MEMF(coop_rec_addr + 0x90); int keep = 0, k; const uint8_t *blk = NULL;
            if (MEM32(0x1BB2A4u) == coop_kept_level && coop_ck_valid && coop_ck_level == coop_kept_level && fabsf(p1now - (float)(int32_t)coop_ck_p1) < 1.0f && !getenv("TT_COOP_NORETRYKEEP")) {
                keep = 1; z = coop_ck_p2; blk = coop_ck_p2blk;   /* a checkpoint reload: player 1's back to his at the checkpoint, player 2's to his then (his kill counts too) */
            } else if (MEM32(0x1BB2A4u) == coop_kept_level && p1now > 0.5f && !getenv("TT_COOP_NORETRYKEEP")) {
                if (fabsf(p1now - coop_kept_p1_90) < 0.5f) { keep = 1; z = coop_kept_p2_90; blk = coop_kept_p2blk; }   /* player 1's kept as it was */
                else for (k = coop_xph_n - 1; k >= 0; k--) if (fabsf(coop_xph[k][0] - p1now) < 0.5f) { keep = 1; z = coop_xph[k][1]; break; }   /* a checkpoint: player 1's back to an earlier value, player 2's back to his at that time */
            }
            tt_dbg("[COOPEXP] start of the level: player 1's level experience %.0f (was %.0f, level %u, was %u): player 2's %s (%.0f)\n", p1now, coop_kept_p1_90, MEM32(0x1BB2A4u), coop_kept_level, keep ? "kept, as player 1's" : "from 0", z);
            coop_kept = 0;
            if (blk) { memcpy(coop_s2 + 0x400 + 0x94, blk, 32);
                tt_dbg("[COOPEXP]   and his other per-level stats (record +0x94..+0xB3): %08X %08X %08X %08X %08X %08X %08X %08X\n", ((const uint32_t *)blk)[0], ((const uint32_t *)blk)[1], ((const uint32_t *)blk)[2], ((const uint32_t *)blk)[3],
                       ((const uint32_t *)blk)[4], ((const uint32_t *)blk)[5], ((const uint32_t *)blk)[6], ((const uint32_t *)blk)[7]); }
        }
        memcpy(coop_s2 + 0x400 + 0x90, &z, 4);
        { extern const char *tt_coop_p2_char_now; tt_dbg("[COOPREC] player 2's record sum %08X (loaded, %s)\n", coop_rec_sum(coop_s2 + 0x400), tt_coop_p2_char_now ? tt_coop_p2_char_now : "?"); }
        tt_dbg("[COOPEXP] player 2's record: his own copy of %08X (experience %d, level %d), swapped in with his context, never saved\n", src, (int32_t)MEM32(src + 0x6C), (int32_t)MEM32(src + 0x78));
    }
    coop_load(coop_s1);
    MEM32(a + 0x49C) = (MEM32(a + 0x49C) & ~6u) | 1u;   /* as the loader marks the player */
    if (!getenv("TT_COOP_KEEP49C") && MEM32(0x43B15Cu) >= 0x10000u) {   /* and player 1's other +0x49C bits (the twin has 0x8000 set, player 1 0x4000) */
        uint32_t o = MEM32(a + 0x49C); MEM32(a + 0x49C) = MEM32(MEM32(0x43B15Cu) + 0x49C);
        tt_dbg("[COOP2] player 2 +49C %08X -> %08X (player 1's)\n", o, MEM32(a + 0x49C)); }
    coop_p2 = a; coop_p2_t0 = coop_now();
    { int i; for (i = 0; i < coop_nwpn; i++) tt_dbg("[COOPWSRC] weapon %d: player 1's %08X +578 %08X +5EC %08X | player 2's %08X +578 %08X +5EC %08X\n", i, coop_wpn_orig[i], MEM32(coop_wpn_orig[i] + 0x578), MEM32(coop_wpn_orig[i] + 0x5EC), coop_wpn_copy[i], MEM32(coop_wpn_copy[i] + 0x578), MEM32(coop_wpn_copy[i] + 0x5EC)); }
    tt_dbg("[COOP2] player 2 = actor %08X (vt %08X +114 %u) at %.1f %.1f %.1f, player 1 %08X\n", a, MEM32(a), MEM32(a + 0x114),
           MEMF(a + 0xF0), MEMF(a + 0xF4), MEMF(a + 0xF8), MEM32(0x43B15Cu));
}

/* the loader's "create object from placement record" (thiscall world; record, key; ret 8): builds it
 * (characters through 0x000F3430) and links it into the world (0x00019880). Player 1's call is kept. */
extern void sub_000F5BC0_gen(void);
int snprintf(char *, size_t, const char *, ...);
static int vs_left_out(uint32_t ty) { const char *e = getenv("TT_VS_OUT"); char b[8];
    if (e) { snprintf(b, sizeof b, ",%u,", ty); { char l[128]; snprintf(l, sizeof l, ",%s,", e); return strstr(l, b) != NULL; } }
    return ty == 2u || ty == 3u || ty == 5u || ty == 11u || ty == 16u || ty == 19u || ty == 29u; }
static uint32_t coop_skin_sw[8][2]; static int coop_skin_nsw;
static int vs_keep_type(uint32_t t, const char *n)   /* (research) TT_VS_KEEPTYPES=5,11 keeps those types; TT_VS_KEEPNAME=prefix,prefix */
{
    const char *e = getenv("TT_VS_KEEPTYPES"), *k = getenv("TT_VS_KEEPNAME");
    if (e) { const char *p = e; while (*p) { if ((uint32_t)atoi(p) == t) return 1; p = strchr(p, ','); if (!p) break; p++; } }
    if (k) { const char *p = k; while (*p) { const char *c = strchr(p, ','); size_t L = c ? (size_t)(c - p) : strlen(p); if (L && !_strnicmp(n, p, L)) return 1; if (!c) break; p = c + 1; } }
    return 0;
}
static int vs_name_has_cam(const char *n) { int i; for (i = 0; n[i] && i < 40; i++) if ((n[i] | 0x20) == 'c' && (n[i + 1] | 0x20) == 'a' && (n[i + 2] | 0x20) == 'm') return 1; return 0; }
void sub_000F5BC0(void)
{
    uint32_t w = ecx, rec = MEM32(esp + 4), key = MEM32(esp + 8), sel = MEM32(0x1BB354u);
    { extern void coop_note_reckey(uint32_t, uint32_t); coop_note_reckey(rec, key); }
    if (tt_vs_on() && rec >= 0x10000u && vs_left_out(MEM32(rec)) && !getenv("TT_VS_KEEPCAST") && !vs_keep_type(MEM32(rec), (const char *)XBOX_PTR(rec + 8))
        && !(MEM32(rec) == 16u && !_strnicmp((const char *)XBOX_PTR(rec + 8), "missionStart", 12) && getenv("TT_VS_KEEPSTART"))
        && !(MEM32(rec) == 11u && !_strnicmp((const char *)XBOX_PTR(rec + 8), "battleManager", 13) && !getenv("TT_VS_NOBATTLEMGR"))
        && !((MEM32(rec) == 5u || MEM32(rec) == 11u || MEM32(rec) == 19u) && (!_strnicmp((const char *)XBOX_PTR(rec + 8), "cam", 3) || !_strnicmp((const char *)XBOX_PTR(rec + 8), "CenterCam", 9) || (!_strnicmp((const char *)XBOX_PTR(rec + 8), "dfxChange", 9) && !getenv("TT_VS_NODFX")) || !_strnicmp((const char *)XBOX_PTR(rec + 8), "activateCollision", 17) || (!getenv("TT_VS_NOANYCAM") && vs_name_has_cam((const char *)XBOX_PTR(rec + 8))) || !_strnicmp((const char *)XBOX_PTR(rec + 8), "TrackVolume", 11)
            || (!_strnicmp((const char *)XBOX_PTR(rec + 8), "sfxVolume", 9) && !getenv("TT_VS_NOSFXVOL"))) && !getenv("TT_VS_NOCAMS"))) {   /* (the level's camera: its paths, parameters, transitions and trigger planes; 1.9.7: the display changes as the heroes move between parts (dfxChange*: left out, the parts past where they began were never shown -- Gates of Moria, Fangorn); and Orthanc's CenterCam_FloorN, the middle its camera circles -- left out, his camera swung round him and he spun on the spot) */   /* (and the battle manager: its target arbitration gives the heroes their aim picks) */   /* (the opening cut-scene stays: it hands the heroes their controls) */
        /* (Versus) the level's cast and story are not made: its other characters (type 2 companions, 3 enemies), the
         * story's logic (5 damage zones, 11 the script's objects: triggers, waypoints, the battle manager; 19 trigger
         * volumes), its cut-scenes (16) and the companion's HUD (29). The arena is the two heroes'; the scenery,
         * pickups, music and the heroes' weapons and machines stay. The loader takes 0 as "nothing made" (as for a
         * record it has no maker for) */
        static int n; if (n++ < 400 && getenv("TT_VS_LOG")) tt_dbg("[VS] arena: type %u \"%.24s\" left out\n", MEM32(rec), (const char *)XBOX_PTR(rec + 8));
        eax = 0; esp += 12; return;
    }
    if (tt_vs_on() && rec >= 0x10000u && MEM32(rec) == 14u && getenv("TT_VS_PICKLOG")) {   /* (research) the level's pickups' records */
        char b[600]; int o = 0, k; for (k = 0x28; k < 0x100 && o < 560; k += 4) o += snprintf(b + o, sizeof b - o, " %X:%08X", k, MEM32(rec + k));
        tt_dbg("[VSPICK] record %08X \"%.24s\" key %08X:%s\n", rec, (const char *)XBOX_PTR(rec + 8), key, b); }
    coop_last_char = 0;
    sub_000F5BC0_gen();
    if (rec >= 0x10000u && MEM32(rec) == 14u && eax >= 0x10000u) { extern void tt_vs_note_pickup(uint32_t obj, uint32_t rec); uint32_t sv = eax; tt_vs_note_pickup(eax, rec); eax = sv; }
    if (coop_mode() == 3 && !coop_rec && coop_last_char >= 0x10000u && MEM32(coop_last_char) == 0x1741C0u && MEM32(coop_last_char + 0x114) == 1) {
        coop_world = w; coop_rec = rec; coop_key = key; coop_hero_sel = sel; coop_first_char = coop_last_char;
        tt_dbg("[COOP2] player 1's placement record %08X (key %08X, type %u, world %08X, hero id [0x1BB354] %08X)\n", rec, key, MEM32(rec), w, sel);
        {   uint32_t k; for (k = 0; k < 0xC8; k += 16) tt_dbg("[COOPREC] +%02X %08X %08X %08X %08X\n", k, MEM32(rec + k), MEM32(rec + k + 4), MEM32(rec + k + 8), MEM32(rec + k + 12));   /* (research) the record and the next one */
        }
        {   /* the actor heap only gives memory during the load (0xE6B50 returns 0 in play): build the twin now */
            uint32_t first = coop_last_char, sp = esp, ra = eax, rc = ecx, rd = edx, trec = rec, tkey = key;
            if (getenv("TT_COOP_OWNREC")) {   /* (testing) the twin gets its own record, aragornHeroOb2: the level script has no object for that name, and a script event reaching the twin (an enemy's hit on player 1 with player 2 close by) then runs on a garbage VM context and crashes. Default: the twin shares player 1's record and script handle. */
                uint32_t k, m;
                PUSH32(esp, 0x100u); PUSH32(esp, 0x000E6B50u); sub_000E6B50(); esp += 4; m = eax;   /* the actor heap, open during the load */
                if (m >= 0x10000u) {
                    for (k = 0; k < 0x64; k += 4) MEM32(m + k) = MEM32(rec + k);
                    for (k = 0x64; k < 0x100; k += 4) MEM32(m + k) = 0;
                    if (MEM8(m + 8 + 13) == 'j') MEM8(m + 8 + 13) = '2';   /* aragornHeroObj -> aragornHeroOb2 */
                    tkey = key ^ 0x00020000u; MEM32(m + 0x28) = tkey; trec = m;
                }
                tt_dbg("[COOP2] twin record copy at %08X (name %.20s, key %08X)\n", m, m >= 0x10000u ? (const char *)XBOX_PTR(m + 8) : "-", tkey);
            }
            {   /* player 2's own model (TT_COOP_P2SKIN, heroes.c): for this build only, the record's model and texture keys are the skin's */
                extern uint32_t tt_coop_skin_pairs[8][2]; extern int tt_coop_skin_n; int q; uint32_t k;
                coop_skin_nsw = 0;
                for (k = 0; k < 0x64 && coop_skin_nsw < 8; k += 4) for (q = 0; q < tt_coop_skin_n; q++)
                    if (tt_coop_skin_pairs[q][0] != 0xFFFFFFFFu && tt_coop_skin_pairs[q][0] != 0x4586A62Cu && MEM32(trec + k) == tt_coop_skin_pairs[q][0]) {
                        coop_skin_sw[coop_skin_nsw][0] = trec + k; coop_skin_sw[coop_skin_nsw][1] = MEM32(trec + k); coop_skin_nsw++;
                        MEM32(trec + k) = tt_coop_skin_pairs[q][1]; tt_dbg("[COOP2] twin's record +%02X: %08X -> %08X (his own model)\n", k, tt_coop_skin_pairs[q][0], tt_coop_skin_pairs[q][1]); break; }
                if (tt_coop_skin_n && !coop_skin_nsw) tt_dbg("[COOP2] twin's record: none of the skin's keys found (skin pairs %d)\n", tt_coop_skin_n);
            }
            PUSH32(esp, tkey); PUSH32(esp, trec); PUSH32(esp, 0x000F5BC0u); ecx = w;
            sub_000F5BC0_gen();   /* ret 8 */
            while (coop_skin_nsw > 0) { coop_skin_nsw--; MEM32(coop_skin_sw[coop_skin_nsw][0]) = coop_skin_sw[coop_skin_nsw][1]; }   /* player 1's record as it was */
            coop_twin = coop_last_char;
            if (coop_twin >= 0x10000u && MEM32(coop_twin + 0x114) == 1 && !getenv("TT_COOP_TWIN_P1")) MEM32(coop_twin + 0x114) = 2;   /* TT_COOP_TWIN_P1 (testing): leave it, the twin becomes player 1 */   /* so the loader's walk keeps the original as player 1 (it takes the first +114 == 1) */
            tt_dbg("[COOP2] twin built during the load: %08X (vt %08X +114 %u), player 1's record gave %08X; esp %s\n", coop_twin,
                   coop_twin >= 0x10000u ? MEM32(coop_twin) : 0, coop_twin >= 0x10000u ? MEM32(coop_twin + 0x114) : 0, first, esp == sp ? "balanced" : "NOT balanced");
            esp = sp; eax = ra; ecx = rc; edx = rd;
            coop_last_char = first;
        }
    }
}
/* player 2 = a second character built from player 1's record, set up as the loader does */
static uint32_t coop_clone(void)
{
    uint32_t a, p1 = MEM32(0x43B15Cu), sp = esp, ra = eax, rc = ecx, rd = edx;
    if (!coop_rec) { tt_dbg("[COOP2] clone: player 1's record was not seen\n"); return 0; }
    { uint32_t now_sel = MEM32(0x1BB354u);   /* 0x000F3430 builds a hero record only for the hero id there */
      MEM32(0x1BB354u) = coop_hero_sel;
      PUSH32(esp, coop_key); PUSH32(esp, coop_rec); PUSH32(esp, 0x000F5BC0u);
      ecx = coop_world; sub_000F5BC0();          /* ret 8: pops all three */
      tt_dbg("[COOP2] clone: hero id [0x1BB354] was %08X, %08X for the call\n", now_sel, coop_hero_sel);
      MEM32(0x1BB354u) = now_sel; }
    a = coop_last_char;
    if (a < 0x10000u || a == p1 || MEM32(a) != 0x1741C0u) { tt_dbg("[COOP2] clone: no character built (%08X)\n", a); esp = sp; eax = ra; ecx = rc; edx = rd; return 0; }
    PUSH32(esp, 0x00082D70u); ecx = a; sub_00082D70();                      /* vtable slot 0, as the loader does */
    PUSH32(esp, a); PUSH32(esp, 0x000C5E20u); ecx = 0x43C5E8u; sub_000C5E20();   /* register with 0x43C5E8 (ret 4) */
    MEMF(a + 0xF0) = MEMF(p1 + 0xF0) + 1.5f; MEMF(a + 0xF4) = MEMF(p1 + 0xF4); MEMF(a + 0xF8) = MEMF(p1 + 0xF8);
    tt_dbg("[COOP2] clone: actor %08X built from player 1's record (+114 %u, hp %d), placed beside player 1; esp %s\n", a, MEM32(a + 0x114),
           (int32_t)MEM32(a + 0x2FC), esp == sp ? "balanced" : "NOT balanced");
    esp = sp; eax = ra; ecx = rc; edx = rd;
    return a;
}

/* player control step *//* research: the original and the twin side by side, once, at the first frame of play */
static void coop_twin_dump(uint32_t p1)
{
    { int i; for (i = 0; i < coop_nchars; i++) { uint32_t q = coop_chars[i], c = MEM32(q + 0x494), l = c >= 0x10000u ? MEM32(c + 0x50) : 0;
        tt_dbg("[COOPBRAINOBJ] char %08X +114 %u: component %08X +50 %08X (vt %08X) +3F8 %08X%s\n", q, MEM32(q + 0x114), c, l, l >= 0x10000u ? MEM32(l) : 0, MEM32(q + 0x3F8), q == coop_twin ? "  <- twin" : q == p1 ? "  <- player 1" : ""); } }
    uint32_t a = p1 == coop_twin ? coop_first_char : p1, t = coop_twin, k, same = 0, diff = 0;
    if (a < 0x10000u || t < 0x10000u || a == t) return;
    tt_dbg("[COOPDIFF] original %08X twin %08X (0x540 bytes): offset original twin\n", a, t);
    for (k = 0; k < 0x540; k += 4) {
        uint32_t x = MEM32(a + k), y = MEM32(t + k);
        int px = x >= 0x80000000u && x < 0x84000000u, py = y >= 0x80000000u && y < 0x84000000u;
        if (x == y && px) { same++; tt_dbg("[COOPDIFF] +%03X %08X %08X  SHARED pointer\n", k, x, y); }
        else if (x != y && !(x >= a && x < a + 0x540 && y >= t && y < t + 0x540 && x - a == y - t)) { diff++; tt_dbg("[COOPDIFF] +%03X %08X %08X%s\n", k, x, y, px != py ? "  (pointer in one only)" : ""); }
    }
    tt_dbg("[COOPDIFF] %u shared pointers, %u differing words (self-pointers at the same offset not counted)\n", same, diff);
    {   /* and their +0x494 components */
        uint32_t ca = MEM32(a + 0x494), ct = MEM32(t + 0x494);
        if (ca >= 0x10000u && ct >= 0x10000u) {
            tt_dbg("[COOPDIFF] component +494: original %08X twin %08X\n", ca, ct);
            for (k = 0; k < 0x400; k += 4) {
                uint32_t x = MEM32(ca + k), y = MEM32(ct + k);
                if (x != y && !(x >= ca && x < ca + 0x400 && y >= ct && y < ct + 0x400 && x - ca == y - ct) && !(x >= a && x < a + 0x540 && y >= t && y < t + 0x540 && x - a == y - t))
                    tt_dbg("[COOPDIFF] c+%03X %08X %08X%s\n", k, x, y, ((x >= 0x80000000u) != (y >= 0x80000000u)) ? "  (pointer in one only)" : "");
            }
        }
    }
}/* the twin lacks what the level's set-up gives player 1 after the build (stats from +0x2AC on, hp,
 * +0x118, +0x464, the -1s at +0x2F0/+0x41C): copy player 1's values, not his pointers.
 * TT_COOP_TWINPTR=1 also shares +0x3F8 and +0x53C (testing). */
static void coop_twin_fix(uint32_t p1)
{
    uint32_t a = p1 == coop_twin ? coop_first_char : p1, t = coop_twin, k, n = 0;
    if (a < 0x10000u || t < 0x10000u || a == t) return;
    for (k = 0x2AC; k < 0x3F0; k += 4) {
        uint32_t x = MEM32(a + k);
        if (x >= 0x80000000u && x < 0x84000000u) continue;
        if (MEM32(t + k) != x) { MEM32(t + k) = x; n++; }
    }
    {   /* (1.9.7) +0x118 is the object's place in the level's draw tree (0x42ED0 gives one: entry 0x3B1B24 + 72 * the low
         * word, its +0x30 the object, +0x40 the high word): player 1's copied made player 2 share player 1's, and his own
         * was never moved again -- he was drawn only while the spot where he was made was in view (Fangorn, the Road to
         * Helm's Deep: his tag over nothing). He keeps his own when it is his, else gets one. TT_COOP_SHARE118: as before */
        extern uint32_t tt_gcall(uint32_t va, uint32_t thisptr, int n, const uint32_t *args);
        uint32_t v = MEM32(t + 0x118), e = 0x3B1B24u + 72u * (v & 0xFFFFu), va = MEM32(a + 0x118);
        int own = (v & 0xFFFFu) < 2048u && v != va && MEM32(e + 0x30) == t && (MEM32(e + 0x40) & 0xFFFFu) == (v >> 16);
        tt_dbg("[COOP2] twin's draw-tree place +118 %08X (entry obj %08X gen %X), player 1's %08X: %s\n", v, MEM32(e + 0x30), MEM32(e + 0x40), va, getenv("TT_COOP_SHARE118") ? "shared (test)" : own ? "his own, kept" : "a new one");
        if (getenv("TT_COOP_SHARE118")) MEM32(t + 0x118) = va;
        else if (!own) { uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx, ar[1]; ar[0] = t; tt_gcall(0x00042ED0u, 0, 1, ar); esp = sp; eax = r0; ecx = r1; edx = r2;
            tt_dbg("[COOP2] twin's new draw-tree place +118 %08X\n", MEM32(t + 0x118)); }
    }
    MEM32(t + 0x41C) = MEM32(a + 0x41C); MEM32(t + 0x464) = MEM32(a + 0x464); n += 3;
    if (getenv("TT_COOP_TWINPTR")) { MEM32(t + 0x3F8) = MEM32(a + 0x3F8); MEM32(t + 0x53C) = MEM32(a + 0x53C); n += 2; }
    {   /* the +0x494 component: its link at +0x50 (null in the twin: messages 9 and 10 then go to a null target) */
        uint32_t ca = MEM32(a + 0x494), ct = MEM32(t + 0x494), l = ca >= 0x10000u ? MEM32(ca + 0x50) : 0;
        tt_dbg("[COOP2] component link +50: original %08X (vt %08X, its +0..+C %08X %08X %08X %08X), twin %08X\n", l, l >= 0x10000u ? MEM32(l) : 0,
               l >= 0x10000u ? MEM32(l) : 0, l >= 0x10000u ? MEM32(l + 4) : 0, l >= 0x10000u ? MEM32(l + 8) : 0, l >= 0x10000u ? MEM32(l + 12) : 0, ct >= 0x10000u ? MEM32(ct + 0x50) : 0);
        if (!getenv("TT_COOP_NOSHARE50") && ct >= 0x10000u) { MEM32(ct + 0x50) = l; MEM32(ct) |= MEM32(ca) & 0x10000u; MEM32(ct + 0x30) |= MEM32(ca + 0x30) & 0x400000u; MEM32(ct + 0x58) = MEM32(ca + 0x58); tt_dbg("[COOP2] twin component: +50 shared, flags copied\n"); }
    }
    tt_dbg("[COOP2] twin %08X: %u words copied from player 1 %08X (hp %d)%s\n", t, n, a, (int32_t)MEM32(t + 0x2FC), getenv("TT_COOP_TWINPTR") ? ", pointers shared" : "");
}
/* the player's +0x494 component update (thiscall, run twice a frame by the world update on [0x43B160]):
 * after player 1's, the same for player 2's component with his globals in place */
extern void sub_00054E70_gen(void);
void sub_00054E70(void)
{
    uint32_t self = ecx;
    tt_coop_p1_pre(self == MEM32(0x43B160u) ? MEM32(0x43B15Cu) : 0, 0x00054E70u); sub_00054E70_gen(); tt_coop_p1_post();
    if (coop_p2 && !coop_depth && self == MEM32(0x43B160u) && MEM32(0x43B15Cu) != coop_p2) {
        uint32_t ra = eax, rc = ecx, rd = edx, c1 = MEM8(0x43B1C0u);
        coop_save(coop_s1); coop_load(coop_s2); MEM8(0x43B1C0u) = (uint8_t)c1; coop_depth = 1;
        { float x = MEMF(coop_p2 + 0xF0), y = MEMF(coop_p2 + 0xF4); static unsigned nmv; static float dsum;
          PUSH32(esp, 0x00054E70u); ecx = MEM32(0x43B160u); sub_00054E70_gen();
          dsum += (MEMF(coop_p2 + 0xF0) - x) * (MEMF(coop_p2 + 0xF0) - x) + (MEMF(coop_p2 + 0xF4) - y) * (MEMF(coop_p2 + 0xF4) - y);
          if (++nmv % 120 == 0) { tt_dbg("[COOP2]          P2 component update: sum of squared steps over 120 calls %.4f\n", dsum); dsum = 0; } }
        coop_save(coop_s2); coop_load(coop_s1); coop_depth = 0;
        eax = ra; ecx = rc; edx = rd;
    }
}
/* put player 2 beside player 1 for a few frames, the way the port's TT_PTP test does (+0xF0 and +0x90) */
/* coop_place_frames: above, before the P2 Bot */
/* the AI's pathmaps (the debug switch "AI/Draw Pathmaps", 0x8D24DD, drawn by 0x0005CC90): a grid over the ground
 * plane per pathmap; an actor's is the manager's (0x41F428) +0x1088 [actor +0x2EC]. Pathmap: +0x48 / +0x4C the
 * origin x / y, +0x68 the cell size (int), +0x6C the cells (a byte each), +0x70 columns, +0x74 rows; a cell's state is
 * (byte >> 2) & 3, or the override table 0x418C00's (same bits) where that is non-zero. */
static uint32_t coop_pathmap(uint32_t actor)
{
    uint32_t i, pm;
    if (actor < 0x10000u) return 0;
    i = MEM32(actor + 0x2ECu);
    if (i > 255u) return 0;
    pm = MEM32(0x41F428u + 0x1088u + 4u * i);
    if (pm < 0x10000u || MEM32(pm + 0x6Cu) < 0x10000u || (int32_t)MEM32(pm + 0x68u) <= 0 || MEM32(pm + 0x70u) - 1u > 4095u || MEM32(pm + 0x74u) - 1u > 4095u) return 0;
    return pm;
}
static int coop_pm_cell(uint32_t pm, int c, int r)   /* -1 outside */
{
    int w = (int)MEM32(pm + 0x70u), h = (int)MEM32(pm + 0x74u), k, o;
    if (c < 0 || r < 0 || c >= w || r >= h) return -1;
    k = (uint16_t)(w * r + c);
    o = (MEM8(0x418C00u + (uint32_t)k) >> 2) & 3;
    return o ? o : (MEM8(MEM32(pm + 0x6Cu) + (uint32_t)k) >> 2) & 3;
}
static void coop_pm_probe(uint32_t p1)
{
    static uint32_t last;
    uint32_t pm;
    if (!getenv("TT_COOP_PMPROBE") || coop_now() - last < 2000u) return;
    last = coop_now();
    if (!(pm = coop_pathmap(p1))) { tt_dbg("[PATHMAP] player 1 %08X: no pathmap (+2EC %u)\n", p1, p1 >= 0x10000u ? MEM32(p1 + 0x2ECu) : 0); return; }
    {
        float ox = MEMF(pm + 0x48u), oy = MEMF(pm + 0x4Cu); int sz = (int)MEM32(pm + 0x68u), w = (int)MEM32(pm + 0x70u), h = (int)MEM32(pm + 0x74u);
        int c = (int)floorf((MEMF(p1 + 0xF0u) - ox) / sz), r = (int)floorf((MEMF(p1 + 0xF4u) - oy) / sz), dr, dc;
        tt_dbg("[PATHMAP] player 1 at %.1f %.1f %.1f: pathmap %u = %08X origin %.1f %.1f cell %d, %dx%d; his cell %d,%d = %d\n",
               MEMF(p1 + 0xF0u), MEMF(p1 + 0xF4u), MEMF(p1 + 0xF8u), MEM32(p1 + 0x2ECu), pm, ox, oy, sz, w, h, c, r, coop_pm_cell(pm, c, r));
        for (dr = 7; dr >= -7; dr--) {
            char line[20]; for (dc = -7; dc <= 7; dc++) { int v = coop_pm_cell(pm, c + dc, r + dr); line[dc + 7] = dc == 0 && dr == 0 ? '@' : v < 0 ? ' ' : (char)('0' + v); }
            line[15] = 0; tt_dbg("[PATHMAP]   %s\n", line);
        }
        if (coop_p2 >= 0x10000u) { int c2 = (int)floorf((MEMF(coop_p2 + 0xF0u) - ox) / sz), r2 = (int)floorf((MEMF(coop_p2 + 0xF4u) - oy) / sz);
            tt_dbg("[PATHMAP] player 2's cell %d,%d = %d (pathmap %u)\n", c2, r2, coop_pm_cell(pm, c2, r2), MEM32(coop_p2 + 0x2ECu)); }
    }
}
/* (research) TT_COOP_PMLOG: both heroes' pathmap index (+0x2EC) and place each second, and every pathmap's bounds
 * when either index changes */
static void coop_pm_log(uint32_t p1)
{
    static uint32_t last, i1 = 0xFFFFFFFFu, i2 = 0xFFFFFFFFu;
    uint32_t a = coop_p2, j;
    if (getenv("TT_COOP_PMTP") && p1 >= 0x10000u && p1 != a && a >= 0x10000u) {   /* (test) TT_COOP_PMTP="ms,w,x,y,z;...": at ms after the first call, hero w (1, 2, or 3 both, player 2 then 3 units east) put at x y z (with TT_COOP_NOJUMPFOLLOW) */
        static int k; static uint32_t t0; const char *e = getenv("TT_COOP_PMTP"); int i;
        if (!t0) t0 = coop_now();
        for (i = 0; i < k && e; i++) { e = strchr(e, ';'); if (e) e++; }
        if (e && *e) {
            char *q; int ms = (int)strtol(e, &q, 10), w = (int)strtol(q + 1, &q, 10); float x = (float)strtod(q + 1, &q), y = (float)strtod(q + 1, &q), z = (float)strtod(q + 1, &q);
            if (coop_now() - t0 >= (uint32_t)ms) {
                uint32_t h[2] = { p1, a }; int j; k++;
                for (j = 0; j < 2; j++) if (w & (1 << j)) { float xx = x + (w == 3 && j ? 3.0f : 0.0f);
                    MEMF(h[j] + 0xF0u) = xx; MEMF(h[j] + 0xF4u) = y; MEMF(h[j] + 0xF8u) = z; MEMF(h[j] + 0x90u) = xx; MEMF(h[j] + 0x94u) = y; MEMF(h[j] + 0x98u) = z;
                    MEMF(h[j] + 0x140u) = xx; MEMF(h[j] + 0x144u) = y; MEMF(h[j] + 0x148u) = z; }
                tt_dbg("[PMLOG] test: hero(es) %d put at %.1f %.1f %.1f\n", w, x, y, z);
            }
        }
    }
    if (!getenv("TT_COOP_PMLOG") || p1 < 0x10000u || a < 0x10000u || coop_now() - last < 1000u) return;
    last = coop_now();
    tt_dbg("[PMLOG] P1 %.2f %.2f %.2f pathmap %d | P2 %.2f %.2f %.2f pathmap %d state %u\n", MEMF(p1 + 0xF0u), MEMF(p1 + 0xF4u), MEMF(p1 + 0xF8u),
           (int)MEM32(p1 + 0x2ECu), MEMF(a + 0xF0u), MEMF(a + 0xF4u), MEMF(a + 0xF8u), (int)MEM32(a + 0x2ECu), MEM32(a + 0x408u));
    if (MEM32(p1 + 0x2ECu) == i1 && MEM32(a + 0x2ECu) == i2) return;
    i1 = MEM32(p1 + 0x2ECu); i2 = MEM32(a + 0x2ECu);
    for (j = 0; j < 64; j++) {
        uint32_t pm = MEM32(0x41F428u + 0x1088u + 4u * j); int sz, w, h; float ox, oy;
        if (pm < 0x10000u || pm >= 0x84000000u || (int32_t)MEM32(pm + 0x68u) <= 0 || MEM32(pm + 0x70u) - 1u > 4095u || MEM32(pm + 0x74u) - 1u > 4095u) continue;
        ox = MEMF(pm + 0x48u); oy = MEMF(pm + 0x4Cu); sz = (int)MEM32(pm + 0x68u); w = (int)MEM32(pm + 0x70u); h = (int)MEM32(pm + 0x74u);
        tt_dbg("[PMLOG]   pathmap %u: x %.1f .. %.1f, y %.1f .. %.1f (%dx%d of %d)\n", j, ox, ox + (float)(w * sz), oy, oy + (float)(h * sz), w, h, sz);
    }
}
/* player 1's trail: spots he stood on in play, on solid ground (his height steady for 15 frames, less than 0.15 a frame)
 * on a clear walkable cell of his pathmap (walkable, and its eight neighbours), at least 2 units apart, the last 64. A
 * hero coming back from down goes to the one nearest the other hero (a playtest at the cave troll: player 2, put 1.5
 * units along x from player 1, came back inside a wall; player 1, put beside him later, fell with him into the void).
 * TT_COOP_NOTRAIL: as before; TT_COOP_TRAILLOG: each spot kept. */
typedef struct { float x, y, z; uint32_t pm; } CoopStood;
static CoopStood coop_trail[64]; static int coop_trail_n, coop_trail_i;
static int coop_cell_clear(uint32_t pm, float x, float y)
{
    float sz = (float)(int)MEM32(pm + 0x68u); int c = (int)floorf((x - MEMF(pm + 0x48u)) / sz), r = (int)floorf((y - MEMF(pm + 0x4Cu)) / sz), i, j;
    for (i = -1; i <= 1; i++) for (j = -1; j <= 1; j++) if (!COOP_PM_WALK(coop_pm_cell(pm, c + i, r + j))) return 0;
    return 1;
}
static void coop_trail_frame(uint32_t p1)
{
    extern int tt_in_scene(void); extern int coop_hero_down(int);
    static unsigned lv; static uint32_t lp1, still; static float lz;
    uint32_t pm = coop_pathmap(p1); float x, y, z;
    if (lv != coop_level_n || p1 != lp1) { lv = coop_level_n; lp1 = p1; coop_trail_n = coop_trail_i = 0; still = 0; }
    if (p1 < 0x10000u) return;
    x = MEMF(p1 + 0xF0u); y = MEMF(p1 + 0xF4u); z = MEMF(p1 + 0xF8u);
    if (fabsf(z - lz) < 0.15f) still++; else still = 0;
    lz = z;
    if (!pm || still < 15 || tt_in_scene() || coop_hero_down(0) || !coop_cell_clear(pm, x, y)) return;
    if (coop_trail_n) { CoopStood *l = &coop_trail[(coop_trail_i + 63) & 63]; if ((l->x - x) * (l->x - x) + (l->y - y) * (l->y - y) < 4.0f) return; }
    coop_trail[coop_trail_i].x = x; coop_trail[coop_trail_i].y = y; coop_trail[coop_trail_i].z = z; coop_trail[coop_trail_i].pm = MEM32(p1 + 0x2ECu);
    coop_trail_i = (coop_trail_i + 1) & 63; if (coop_trail_n < 64) coop_trail_n++;
    if (getenv("TT_COOP_TRAILLOG")) tt_dbg("[TRAIL] player 1 stood at %.1f %.1f %.1f (pathmap %u): kept (%d)\n", x, y, z, MEM32(p1 + 0x2ECu), coop_trail_n);
}
/* the trail spot for a hero coming back beside (ox, oy): within 15 units, the one nearest 3 units from it (not on top
 * of the other hero): 2; none that near: the newest, 1. 0: no trail yet */
static int coop_trail_pick(float ox, float oy, CoopStood *out)
{
    int k, best = -1; float bd = 1e9f;
    if (!coop_trail_n || getenv("TT_COOP_NOTRAIL")) return 0;
    for (k = 0; k < coop_trail_n; k++) {
        float dx = coop_trail[k].x - ox, dy = coop_trail[k].y - oy, d = sqrtf(dx * dx + dy * dy);
        if (d <= 15.0f && fabsf(d - 3.0f) < bd) { bd = fabsf(d - 3.0f); best = k; }
    }
    *out = coop_trail[best < 0 ? (coop_trail_i + 63) & 63 : best];
    return best < 0 ? 1 : 2;
}
#define COOP_PLACE_GAP 3.0f      /* player 2 put beside player 1: this far to the side (1.5 overlapped them and their markers) */
/* where player 2 goes beside player 1: on player 1's pathmap, the cell reached from his over walkable cells (state 0,
 * 4-neighbour steps, up to 5 cells) whose eight neighbours are walkable too (off the walls), its middle nearest
 * COOP_PLACE_GAP from him (not his own cell); without a pathmap or such a cell, COOP_PLACE_GAP along +x as before.
 * TT_COOP_NOPATHPLACE: always to the side. */
static void coop_place_target(uint32_t p1, float *x, float *y)
{
    /* (walkable: state 0, or 2 -- player 1 stands on 2s, e.g. Gates of Moria's second area; a 2 was never searched
     * from, and the blind fallback 3 units along +x put player 2 off a ledge into the void in a playtest. Now the fallback
     * with a pathmap is player 1's own spot, solid ground: they push apart) */

    uint32_t pm = coop_pathmap(p1);
    float px = MEMF(p1 + 0xF0u), py = MEMF(p1 + 0xF4u);
    *x = px + COOP_PLACE_GAP; *y = py;
    if (pm && !getenv("TT_COOP_NOPATHPLACE")) {
        enum { M = 5, N = 2 * M + 1 };   /* the cells within M of his */
        static const int dc4[4] = { 1, -1, 0, 0 }, dr4[4] = { 0, 0, 1, -1 };
        float ox = MEMF(pm + 0x48u), oy = MEMF(pm + 0x4Cu), sz = (float)(int)MEM32(pm + 0x68u), best = 1e9f, bx = 0, by = 0;
        int c0 = (int)floorf((px - ox) / sz), r0 = (int)floorf((py - oy) / sz), qc[N * N], qr[N * N], qn = 0, qi = 0, i, j, found = 0, v0 = coop_pm_cell(pm, c0, r0);
        unsigned char seen[N][N];
        memset(seen, 0, sizeof seen);
        *x = px; *y = py;   /* (no clear cell: his own spot) */
        { qc[qn] = c0; qr[qn++] = r0; seen[M][M] = 1; }   /* (from his cell whatever it says: he stands there) */
        while (qi < qn) {
            int c = qc[qi], r = qr[qi++], d, nb = 0, v = coop_pm_cell(pm, c, r);
            for (i = -1; i <= 1; i++) for (j = -1; j <= 1; j++) if (COOP_PM_WALK(coop_pm_cell(pm, c + i, r + j))) nb++;
            if (nb == 9 && COOP_PM_WALK(v) && (c != c0 || r != r0)) {
                float cx = ox + (c + 0.5f) * sz, cy = oy + (r + 0.5f) * sz, e = sqrtf((cx - px) * (cx - px) + (cy - py) * (cy - py)) - COOP_PLACE_GAP;
                if (e < 0) e = -e;
                if (v == 2) e += 1.0f;   /* (0s first) */
                if (e < best) { best = e; bx = cx; by = cy; found = 1; }
            }
            for (d = 0; d < 4; d++) {
                int nc = c + dc4[d], nr = r + dr4[d];
                if (nc - c0 < -M || nc - c0 > M || nr - r0 < -M || nr - r0 > M || seen[nr - r0 + M][nc - c0 + M]) continue;
                seen[nr - r0 + M][nc - c0 + M] = 1;
                if (COOP_PM_WALK(coop_pm_cell(pm, nc, nr)) && qn < N * N) { qc[qn] = nc; qr[qn++] = nr; }
            }
        }
        if (found) { *x = bx; *y = by; }
        tt_dbg("[COOP2] place: pathmap %u, player 1's cell %d,%d = %d, %d cells walked: %s %.2f %.2f\n", MEM32(p1 + 0x2ECu), c0, r0, v0, qn,
               found ? "on the path at" : "no clear cell, on his own spot at", *x, *y);
    } else tt_dbg("[COOP2] place: no pathmap for player 1 (+2EC %u): to the side\n", p1 >= 0x10000u ? MEM32(p1 + 0x2ECu) : 0);
}
static int coop_start_place = 1;   /* play (re)starting: player 2 to be put beside player 1 (coop_place) */
static void coop_place(void)
{
    uint32_t p1 = MEM32(0x43B15Cu), a = coop_p2, k;
    static int after, place_have, place_nopm, place_off_ok; static float place_x, place_y, place_ox, place_oy;
    coop_pm_probe(p1); coop_pm_log(p1); coop_trail_frame(p1); coop_steps_frame(p1); { extern void coop_own_machines_x(void); coop_own_machines_x(); } { extern void coop_pdmg_frame_x(void); coop_pdmg_frame_x(); }
    if (getenv("TT_COOP_AIMSTATE") && p1 >= 0x10000u && a >= 0x10000u) {   /* (research) each hero's state, lock and pad record, 4 times a second */
        static uint32_t lt; if (coop_now() - lt >= 250u) { int w, j; lt = coop_now();
            for (w = 0; w < 2; w++) { uint32_t h = w ? a : p1, c = MEM32(h + 0x494), t = c >= 0x10000u ? MEM32(c + 0x98) : 0, r = COOP_PADREC(w ? 1 : 0);
                tt_dbg("[AIMSTATE] t %u P%d state %u lock %08X%s rec:", coop_now(), w + 1, MEM32(h + 0x408), t, t >= 0x10000u && t < 0x84000000u && MEM32(t) == 0x1741C0u ? " (char)" : "");
                for (j = 0; j < 0x60; j += 4) tt_dbg(" %08X", MEM32(r + j)); tt_dbg("\n"); } } }
    if (getenv("TT_COOP_CAMJUMP") && MEM32(0x43B164u) >= 0x10000u && p1 >= 0x10000u && a >= 0x10000u) {   /* (research) the camera's eye (+0x50) or look-at (+0x40) moving more than 1.5 units in a frame */
        extern int coop_hero_down(int); static float le[3], ll[3]; static int n; uint32_t cm = MEM32(0x43B164u); float de = 0, dl = 0; int k;
        for (k = 0; k < 3; k++) { de += (MEMF(cm + 0x50 + 4 * k) - le[k]) * (MEMF(cm + 0x50 + 4 * k) - le[k]); dl += (MEMF(cm + 0x40 + 4 * k) - ll[k]) * (MEMF(cm + 0x40 + 4 * k) - ll[k]); }
        if ((de > 2.25f || dl > 2.25f) && n++ < 300) tt_dbg("[CAMJUMP] t %u eye %.1f %.1f %.1f (moved %.1f) look-at %.1f %.1f %.1f (moved %.1f) | P1 %.1f %.1f %.1f state %u down %d | P2 %.1f %.1f down %d | context %s\n", coop_now(),
            MEMF(cm + 0x50), MEMF(cm + 0x54), MEMF(cm + 0x58), sqrtf(de), MEMF(cm + 0x40), MEMF(cm + 0x44), MEMF(cm + 0x48), sqrtf(dl), MEMF(p1 + 0xF0), MEMF(p1 + 0xF4), MEMF(p1 + 0xF8), MEM32(p1 + 0x408), coop_hero_down(0), MEMF(a + 0xF0), MEMF(a + 0xF4), coop_hero_down(1), coop_depth ? "player 2" : "player 1");
        for (k = 0; k < 3; k++) { le[k] = MEMF(cm + 0x50 + 4 * k); ll[k] = MEMF(cm + 0x40 + 4 * k); }
    }
    if (a >= 0x10000u && a == coop_twin) { coop_p2_own_type("in play"); coop_p2_weapon_ids("in play"); }   /* (something after the take-over set player 1's type on him again: kept his own) */
    {   /* fallen: player 2 more than 12 units below player 1 for a second, neither down, not in a cutscene: back beside
         * him, whatever the settings (put off a ledge in a playtest, he fell into the void and the camera broke) */
        extern int tt_in_scene(void); extern int coop_hero_down(int);
        static uint32_t low_t;
        if (a >= 0x10000u && p1 >= 0x10000u && coop_place_frames <= 0 && !tt_in_scene() && !coop_hero_down(0) && !coop_hero_down(1) && MEMF(p1 + 0xF8) - MEMF(a + 0xF8) > 12.0f) {
            if (!low_t) low_t = coop_now() | 1;
            else if (coop_now() - low_t > 1000u) { low_t = 0; coop_place_frames = 3; tt_dbg("[COOP2] player 2 fell (%.1f units below player 1): put beside him\n", MEMF(p1 + 0xF8) - MEMF(a + 0xF8)); }
        } else low_t = 0;
    }
    {   /* held at his pathmap's edge (Gates of Moria's lake: player 2 kept pathmap 8, x -208..-148, after player 1 went on
         * to 9, and stood 49 s at x -148.55 with the stick toward him; the game moves an actor to the next pathmap only
         * when a step crosses a portal, 0x5D630): within 1 unit of his pathmap's x/y bounds and inside player 1's
         * (another one), moving less than 1 unit in 1.5 s: player 1's pathmap index given to him; still held 1.5 s
         * later: put beside player 1. Only while his stick (port 1's record, the P2 Bot's too) is pushed past half.
         * TT_COOP_NOEDGEFIX: off. */
        extern int tt_in_scene(void); extern int coop_hero_down(int);
        static uint32_t t0; static int step; static float sx, sy;
        uint32_t pa = coop_pathmap(a), pb = coop_pathmap(p1);
        int held = 0, push = MEMF(COOP_PADREC(1) + 0x14u) * MEMF(COOP_PADREC(1) + 0x14u) + MEMF(COOP_PADREC(1) + 0x18u) * MEMF(COOP_PADREC(1) + 0x18u) > 0.09f;   /* (past 0.3: in a playtest the bot's stick was at 0.37 part of the time) */
        if (push && a >= 0x10000u && p1 >= 0x10000u && pa && pb && pa != pb && coop_place_frames <= 0 && !tt_in_scene() && !coop_hero_down(0) && !coop_hero_down(1)
            && !getenv("TT_COOP_NOEDGEFIX")) {
            float x = MEMF(a + 0xF0u), y = MEMF(a + 0xF4u);
            int edge = x - MEMF(pa + 0x48u) < 1.0f || MEMF(pa + 0x58u) - x < 1.0f || y - MEMF(pa + 0x4Cu) < 1.0f || MEMF(pa + 0x5Cu) - y < 1.0f;
            int in1 = x >= MEMF(pb + 0x48u) && x <= MEMF(pb + 0x58u) && y >= MEMF(pb + 0x4Cu) && y <= MEMF(pb + 0x5Cu)
                      && MEMF(a + 0xF8u) >= MEMF(pb + 0x50u) && MEMF(a + 0xF8u) <= MEMF(pb + 0x60u);
            held = edge && (in1 || fabsf(MEMF(a + 0xF8u) - MEMF(p1 + 0xF8u)) < 8.0f);   /* (or near player 1's height: a playtest held him 12 s at x -148.55 and the rule never ran, in1 false there) */
        }
        if (!held && step == 0) t0 = 0;
        else if (!t0) { t0 = coop_now() | 1; sx = MEMF(a + 0xF0u); sy = MEMF(a + 0xF4u); }
        else if (coop_now() - t0 > 1500u) {
            float dx = MEMF(a + 0xF0u) - sx, dy = MEMF(a + 0xF4u) - sy;
            if (dx * dx + dy * dy >= 1.0f || !push) { step = 0; t0 = 0; }
            else if (step == 0) {
                tt_dbg("[COOP2] player 2 held at his pathmap's edge (%d) at %.2f %.2f, inside player 1's (%d): given player 1's\n",
                       (int)MEM32(a + 0x2ECu), MEMF(a + 0xF0u), MEMF(a + 0xF4u), (int)MEM32(p1 + 0x2ECu));
                MEM32(a + 0x2ECu) = MEM32(p1 + 0x2ECu); step = 1; t0 = coop_now() | 1; sx = MEMF(a + 0xF0u); sy = MEMF(a + 0xF4u);
            } else { step = 0; t0 = 0; coop_place_frames = 3; tt_dbg("[COOP2] player 2 still held at %.2f %.2f (pathmap %d): put beside player 1\n", MEMF(a + 0xF0u), MEMF(a + 0xF4u), (int)MEM32(a + 0x2ECu)); }
        }
    }
    {   /* the level placing player 1 (its script's @TeleportToStartPoint / @TeleportPlayerTo: Gates of Moria puts him at
         * his start point while its opening runs, 47 units from where he was built): more than 6 units in a frame.
         * Player 2 goes beside the new spot in that same frame, still inside the cutscene, so play starts with both
         * there and nothing jumps when it ends. If player 1 has no pathmap yet (+0x2EC -1 in the opening), the
         * spot is worked out again once he has one, while player 2 still stands where he was put.
         * TT_COOP_NOJUMPFOLLOW: off. */
        extern int tt_in_scene(void); extern int xbox_MoviePlaying(void);
        static uint32_t prev_p1; static float lx, ly;
        if (p1 >= 0x10000u && a >= 0x10000u && p1 == prev_p1 && !getenv("TT_COOP_NOJUMPFOLLOW")) {
            float dx = MEMF(p1 + 0xF0u) - lx, dy = MEMF(p1 + 0xF4u) - ly;
            if (dx * dx + dy * dy > 36.0f) {
                tt_dbg("[COOP2] player 1 moved %.1f units in a frame (to %.1f %.1f %.1f, %s%s, pathmap %d): player 2 with him\n", sqrtf(dx * dx + dy * dy),
                       MEMF(p1 + 0xF0u), MEMF(p1 + 0xF4u), MEMF(p1 + 0xF8u), tt_in_scene() ? "in a cutscene" : "in play", xbox_MoviePlaying() ? ", movie" : "", (int)MEM32(p1 + 0x2ECu));
                coop_place_frames = 3; place_have = 0; place_nopm = !coop_pathmap(p1);
            }
        }
        if (place_nopm && coop_place_frames <= 0 && a >= 0x10000u && p1 >= 0x10000u && coop_pathmap(p1)) {
            float dx = MEMF(a + 0xF0u) - place_x, dy = MEMF(a + 0xF4u) - place_y;
            place_nopm = 0;
            if (dx * dx + dy * dy < 1.0f) { coop_place_frames = 3; place_have = 0; tt_dbg("[COOP2] player 1's pathmap now there: player 2 put on the path\n"); }
        }
        if (getenv("TT_COOP_GOALSCAN") && p1 >= 0x10000u && tt_in_scene()) {   /* (testing) "x,y": where in player 1 (and one pointer deep) a pair of floats within 1 unit of x, y sits */
            static uint32_t lt; float gx = 0, gy = 0;
            const char *gs = getenv("TT_COOP_GOALSCAN"), *cm = strchr(gs, ','); if (cm) { gx = (float)atof(gs); gy = (float)atof(cm + 1); }
            {   /* once: all of memory (the heap 0x18B000-0x4000000 and the contiguous 0x80000000-0x84000000) */
                static int done; uint32_t va; int n = 0;
                static const uint32_t rg[2][2] = { { 0x18B000u, 0x3FFFFF0u }, { 0x80000000u, 0x83FFFFF0u } }; int k;
                if (!done && cm && coop_now() - coop_p2_t0 > 2000u) { done = 1;
                    for (k = 0; k < 2; k++) for (va = rg[k][0]; va < rg[k][1] && n < 60; va += 4) {
                        float fx = MEMF(va), fy;
                        if (fabsf(fx - gx) >= 1.5f) continue;
                        fy = MEMF(va + 4);
                        if (fabsf(fy - gy) < 1.5f) { tt_dbg("[GOALSCAN] memory %08X: %.2f %.2f %.2f  (-8: %08X %08X, -0x20: %08X)\n", va, fx, fy, MEMF(va + 8), MEM32(va - 8), MEM32(va - 4), MEM32(va - 0x20)); n++; }
                    }
                    tt_dbg("[GOALSCAN] memory scan done: %d hits\n", n);
                }
            }
            if (coop_now() - lt > 1000u && cm) {
                uint32_t o, o2; int n = 0; lt = coop_now();
                for (o = 0; o < 0x1400u && n < 24; o += 4) {
                    float fx = MEMF(p1 + o), fy = MEMF(p1 + o + 4);
                    uint32_t q = MEM32(p1 + o);
                    if (fabsf(fx - gx) < 1.0f && fabsf(fy - gy) < 1.0f) { tt_dbg("[GOALSCAN] t %u player 1 +%03X: %.2f %.2f %.2f\n", coop_now(), o, fx, fy, MEMF(p1 + o + 8)); n++; }
                    if (q >= 0x80000000u && q < 0x83F00000u && q != p1)
                        for (o2 = 0; o2 < 0x400u && n < 24; o2 += 4) { float gx2 = MEMF(q + o2), gy2 = MEMF(q + o2 + 4);
                            if (fabsf(gx2 - gx) < 1.0f && fabsf(gy2 - gy) < 1.0f) { tt_dbg("[GOALSCAN] t %u player 1 +%03X -> %08X +%03X: %.2f %.2f %.2f\n", coop_now(), o, q, o2, gx2, gy2, MEMF(q + o2 + 8)); n++; } }
                }
                if (!n) tt_dbg("[GOALSCAN] t %u nothing near %.1f %.1f (player 1 at %.1f %.1f)\n", coop_now(), gx, gy, MEMF(p1 + 0xF0u), MEMF(p1 + 0xF4u));
            }
        }
        if (getenv("TT_COOP_P1TRACE") && p1 >= 0x10000u) {   /* (testing) player 1 through a level's opening */
            static uint32_t lt; if (coop_now() - lt > 250u || p1 != prev_p1) { lt = coop_now();
                tt_dbg("[P1TRACE] t %u player 1 %08X at %.1f %.1f %.1f, %s%s, pathmap %d, player 2 %08X at %.1f %.1f\n", coop_now(), p1, MEMF(p1 + 0xF0u), MEMF(p1 + 0xF4u), MEMF(p1 + 0xF8u),
                       tt_in_scene() ? "cutscene" : "play", xbox_MoviePlaying() ? " movie" : "", (int)MEM32(p1 + 0x2ECu), a, a >= 0x10000u ? MEMF(a + 0xF0u) : 0.0f, a >= 0x10000u ? MEMF(a + 0xF4u) : 0.0f); } }
        /* in a cutscene that walks player 1 (Gates of Moria's opening: about 46 units in two legs over ~20 s, no jump;
         * player 2's stick does nothing then), player 2 keeps his offset from player 1 (from his last placement), so they
         * come out of it side by side; where that spot isn't walkable on the pathmap, a new one. TT_COOP_NOSCENEFOLLOW: off. */
        if (p1 >= 0x10000u && a >= 0x10000u && p1 == prev_p1 && coop_place_frames <= 0 && tt_in_scene() && !xbox_MoviePlaying() && place_off_ok && !getenv("TT_COOP_NOSCENEFOLLOW")) {
            float mx = MEMF(p1 + 0xF0u) - lx, my = MEMF(p1 + 0xF4u) - ly;
            if (mx * mx + my * my > 1e-4f && mx * mx + my * my <= 36.0f) {
                float tx = MEMF(p1 + 0xF0u) + place_ox, ty = MEMF(p1 + 0xF4u) + place_oy; uint32_t pm = coop_pathmap(p1);
                if (pm) { int c = (int)floorf((tx - MEMF(pm + 0x48u)) / (float)(int)MEM32(pm + 0x68u)), r = (int)floorf((ty - MEMF(pm + 0x4Cu)) / (float)(int)MEM32(pm + 0x68u));
                    int c1 = (int)floorf((MEMF(p1 + 0xF0u) - MEMF(pm + 0x48u)) / (float)(int)MEM32(pm + 0x68u)), r1 = (int)floorf((MEMF(p1 + 0xF4u) - MEMF(pm + 0x4Cu)) / (float)(int)MEM32(pm + 0x68u));
                    if (coop_pm_cell(pm, c, r) != 0 && coop_pm_cell(pm, c1, r1) == 0) { coop_place_target(p1, &tx, &ty);   /* (player 1 himself off the walkable cells, as in parts of that walk: the offset kept) */ place_ox = tx - MEMF(p1 + 0xF0u); place_oy = ty - MEMF(p1 + 0xF4u); } }
                MEMF(a + 0xF0u) = tx; MEMF(a + 0xF4u) = ty; MEMF(a + 0xF8u) = MEMF(p1 + 0xF8u);
                { uint32_t k2; for (k2 = 0; k2 < 3; k2++) MEM32(a + 0x90 + 4 * k2) = MEM32(a + 0xF0 + 4 * k2); }
                { static uint32_t lt2; if (coop_now() - lt2 > 1000u) { lt2 = coop_now(); tt_dbg("[COOP2] cutscene: player 1 walking (%.1f %.1f), player 2 with him at %.1f %.1f\n", MEMF(p1 + 0xF0u), MEMF(p1 + 0xF4u), tx, ty); } }
            }
        }
        prev_p1 = p1;
        if (p1 >= 0x10000u) { lx = MEMF(p1 + 0xF0u); ly = MEMF(p1 + 0xF4u); }
    }
    if (after > 0 && a >= 0x10000u) { after--; tt_dbg("[COOP2] place: next frame reads %.2f %.2f %.2f (+90: %.2f %.2f)\n", MEMF(a + 0xF0), MEMF(a + 0xF4), MEMF(a + 0xF8), MEMF(a + 0x90), MEMF(a + 0x94)); }
    {   /* the mission's start, and the end of each cutscene or movie: the level puts player 1 at his own start point
         * (Gates of Moria: player 2 Legolas was left where the level's Legolas stands, the shared camera on a rock).
         * Once play has run 0.3 s, player 2 further than 8 units from player 1 (he walked with him through a cutscene: nearby is fine) is put beside him, whatever
         * the P2 Teleport setting (that one is the catch-up in play). TT_COOP_NOSTARTPLACE: off. */
        extern int tt_in_scene(void); extern int xbox_MoviePlaying(void); extern int coop_hero_down(int);
        static uint32_t play_t;
        if (tt_in_scene() || xbox_MoviePlaying() || a < 0x10000u || p1 < 0x10000u) { coop_start_place = 1; play_t = 0; }
        else if (coop_start_place && coop_place_frames <= 0 && !getenv("TT_COOP_NOSTARTPLACE")) {
            if (!play_t) play_t = coop_now() | 1;
            else if (coop_now() - play_t > 300u) {
                float dx = MEMF(p1 + 0xF0) - MEMF(a + 0xF0), dy = MEMF(p1 + 0xF4) - MEMF(a + 0xF4);
                coop_start_place = 0; play_t = 0;
                int clear = 1; uint32_t pm2 = coop_pathmap(p1);
                if (pm2) {   /* on a clear cell (walkable, and its eight neighbours)? Following player 1 through a cutscene can leave
                              * him in the rock where player 1 walked off the walkable cells: there he couldn't move at all (Gates of
                              * Moria, the "falling behind" seen in a playtest: 0.00 units in 1.5 s with the stick held) */
                    int c = (int)floorf((MEMF(a + 0xF0) - MEMF(pm2 + 0x48u)) / (float)(int)MEM32(pm2 + 0x68u)), r = (int)floorf((MEMF(a + 0xF4) - MEMF(pm2 + 0x4Cu)) / (float)(int)MEM32(pm2 + 0x68u)), i2, j2;
                    for (i2 = -1; i2 <= 1; i2++) for (j2 = -1; j2 <= 1; j2++) if (coop_pm_cell(pm2, c + i2, r + j2) != 0) clear = 0;
                }
                if ((dx * dx + dy * dy > 64.0f || !clear) && !coop_hero_down(0) && !coop_hero_down(1)) { coop_place_frames = 3; tt_dbg("[COOP2] start of play: player 2 %.1f units from player 1%s, put beside him\n", sqrtf(dx * dx + dy * dy), clear ? "" : ", not on a clear walkable cell"); }
                else tt_dbg("[COOP2] start of play: player 2 %.1f units from player 1, left there\n", sqrtf(dx * dx + dy * dy));
            }
        }
    }
    {   /* catch-up: the level moves player 1 on its own (the Gates of Moria's opening puts him at his start point after
         * the movie, 47 units from where the twin was built: the shared camera looked at a rock, observed). Player 2
         * further out than 1.5 x the leash for 0.5 s, neither hero down: put him beside player 1. TT_COOP_NOCATCHUP: off. */
        static uint32_t far_t; extern float coop_leash_radius(void); extern int coop_hero_down(int);
        extern int tt_coop_setting(int i);
        if (a >= 0x10000u && p1 >= 0x10000u && coop_place_frames <= 0 && !coop_hero_down(0) && !coop_hero_down(1) && !getenv("TT_COOP_NOCATCHUP") && tt_coop_setting(3)) {   /* (the Co-op menu's P2 Teleport) */
            float dx = MEMF(p1 + 0xF0) - MEMF(a + 0xF0), dy = MEMF(p1 + 0xF4) - MEMF(a + 0xF4), R = coop_leash_radius() * 1.5f;
            if (dx * dx + dy * dy > R * R) {
                if (!far_t) far_t = coop_now() | 1;
                else if (coop_now() - far_t > 500u) { far_t = 0; coop_place_frames = 3; tt_dbg("[COOP2] catch-up: player 2 %.1f units from player 1, put beside him\n", sqrtf(dx * dx + dy * dy)); }
            } else far_t = 0;
        } else far_t = 0;
    }
    if (coop_place_frames <= 0 || a < 0x10000u || p1 < 0x10000u) return;
    if (!place_have) { place_have = 1; coop_place_target(p1, &place_x, &place_y); place_ox = place_x - MEMF(p1 + 0xF0); place_oy = place_y - MEMF(p1 + 0xF4); place_off_ok = 1; }   /* (worked out once per placement; the offset kept in cutscenes) */
    coop_place_frames--; after = 2;
    tt_dbg("[COOP2] place: writing %.2f %.2f %.2f (was %.2f %.2f)\n", place_x, place_y, MEMF(p1 + 0xF8), MEMF(a + 0xF0), MEMF(a + 0xF4));
    MEMF(a + 0xF0) = place_x; MEMF(a + 0xF4) = place_y; MEMF(a + 0xF8) = MEMF(p1 + 0xF8);
    if (coop_place_frames <= 0) place_have = 0;
    for (k = 0; k < 3; k++) MEM32(a + 0x90 + 4 * k) = MEM32(a + 0xF0 + 4 * k);
}static void coop_gate_log(int who)
{
    static uint32_t last[3];
    uint32_t now = coop_now();
    if (now - last[who] < 1000) return;
    last[who] = now;
    tt_dbg("[COOPGATE] P%d step: dt %.4f  43B1B4 %u  1B0C44 %.3f  43B1B8 %.3f  1B0C48 %.3f  43B194 %.3f  43B1C0 %u  player %08X port %u\n", who,
           MEMF(esp + 4), MEM8(0x43B1B4u), MEMF(0x1B0C44u), MEMF(0x43B1B8u), MEMF(0x1B0C48u), MEMF(0x43B194u), MEM8(0x43B1C0u), MEM32(0x43B15Cu), MEM32(0x1CB8B4u));
}/* leash: player 2 stays within TT_COOP_LEASH units (default 20) of player 1 on the ground plane: a step
 * that takes him further out is undone where he moves (slot 1, tt_coop_leave). No teleport.
 * TT_COOP_NOLEASH turns it off. */
unsigned coop_leash_stops;
float coop_leash_radius(void) { static float R = -1; if (R < 0) { const char *v = getenv("TT_COOP_LEASH"); R = v ? (float)atof(v) : 20.0f; if (R < 2) R = 2; } return R; }/* player 2's hits: a hero raises its hit events on its own event source (+0x3D0; slots 17/18 = 0x86120/0x86140
 * -> 0x26580/0x265D0), and the level script's hit handlers listen on player 1's source only (listeners at +0x74,
 * count at +0x174). Player 2's source gets the same listeners, kept in sync. TT_COOP_NOHITLISTENERS turns it off. */
static void coop_sync_listeners(void)
{
    uint32_t p1 = MEM32(0x43B15Cu), so, sc, n, k;
    static uint32_t lastn = 0xFFFFFFFFu;
    if (coop_p2 && coop_twin >= 0x10000u && coop_first_char >= 0x10000u) {   /* (research) both heroes' listener lists, when they change */
        static uint32_t sig[2]; static int lines; int h;
        for (h = 0; h < 2; h++) {
            uint32_t ch = h ? coop_twin : coop_first_char, s = MEM32(ch + 0x3D0), m = s >= 0x10000u ? MEM32(s + 0x174) : 0, x = m, i; char b[400]; int o = 0;
            if (m > 32) continue;
            for (i = 0; i < m; i++) x = x * 31u + MEM32(s + 0x74 + 8 * i);
            if (x == sig[h] || lines > 60) continue;
            sig[h] = x; lines++;
            for (i = 0; i < m && o < 360; i++) { uint32_t e = MEM32(s + 0x74 + 8 * i), j; const char *tag = "";
                for (j = 0; j < (uint32_t)coop_nwpn; j++) { if (e == coop_wpn_orig[j]) tag = "=P1w"; if (e == coop_wpn_copy[j]) tag = "=P2w"; }
                { uint32_t v = e >= 0x10000u ? MEM32(e) : 0; int d; const char *t;
                  b[o++] = ' '; for (d = 28; d >= 0; d -= 4) b[o++] = "0123456789ABCDEF"[(e >> d) & 15];
                  b[o++] = '/'; for (d = 20; d >= 0; d -= 4) b[o++] = "0123456789ABCDEF"[(v >> d) & 15];
                  for (t = tag; *t; t++) b[o++] = *t; } }
            b[o] = 0;
            tt_dbg("[COOPLL] t %u %s's listeners (%u):%s\n", coop_now(), h ? "twin" : "P1", m, b);
        }
    }
    if (!coop_p2 || p1 < 0x10000u || p1 == coop_p2 || !getenv("TT_COOP_HITLISTENERS")) return;   /* off by default: the list holds the hero's weapons
                                                                                                    (0x86500 updates each listener, vt+0xC), so a copy of player 1's list drops the twin's own weapon copies */
    so = MEM32(p1 + 0x3D0); sc = MEM32(coop_p2 + 0x3D0);
    if (so < 0x10000u || sc < 0x10000u || so == sc) return;
    n = MEM32(so + 0x174); if (n > 32) return;   /* entries are 8 bytes (0x265D0 steps by 8), room for 32 */
    if (MEM32(sc + 0x174) == n) { for (k = 0; k < 2 * n && MEM32(sc + 0x74 + 4 * k) == MEM32(so + 0x74 + 4 * k); k++) ; if (k == 2 * n) return; }
    for (k = 0; k < 2 * n; k++) MEM32(sc + 0x74 + 4 * k) = MEM32(so + 0x74 + 4 * k);
    if (n != lastn) tt_dbg("[COOPLISTEN] player 2's event source %08X: %u listeners from player 1's %08X (had %u)\n", sc, n, so, MEM32(sc + 0x174));
    MEM32(sc + 0x174) = n; lastn = n;
}/* player 2's target. A hero's target is component +0x98 (@ActorGetTarget, @LockHeadingToTarget turns him to it,
 * and the contact handler 0xBE0D0 reads the attacker's). Player 1's is set by a message the game only sends him;
 * player 2's stays empty, so he swings at the air and his attack boxes never land. Here: keep a living target
 * that is still within 10 units, else take the nearest living enemy within 10, else none.
 * TT_COOP_NOP2TARGET turns it off. */
static unsigned coop_p2tgt_set; static void coop_watch50(const char *where);
/* (Versus) each hero's target (component +0x98) is the other hero: his ranged shots follow it (a lock-on, as on an
 * enemy) and his swings turn to him. Kept every frame (the game sets player 1's by a message of its own) */
/* (1.9.7) not while he draws his bow (LT): then the game's own aim pick (0x5A150, the stick and his heading) locks him
 * onto the other or onto nothing, as on an enemy -- a shot counts only with that lock (tt_vs_shot_launched): before,
 * the lock was kept on the other whatever he aimed at, and every arrow found him */
static int vs_aiming(int w) { extern uint8_t tt_vs_lt[2]; return !getenv("TT_VS_OLDAIM") && tt_vs_lt[w & 1] > 30; }
static void vs_targets(void)
{
    int w; static int was[2];
    for (w = 0; w < 2; w++) {
        uint32_t a = w ? coop_p2 : coop_first_char, o = w ? coop_first_char : coop_p2, c;
        if (a < 0x10000u || o < 0x10000u) continue;
        c = MEM32(a + 0x494); if (c < 0x10000u) continue;
        if (getenv("TT_VS_NOTGT")) { MEM32(c + 0x98) = 0; continue; }   /* (test) */
        if (vs_aiming(w)) {   /* the bow up: locked on the other only while he aims at him -- within TT_VS_AIMCONE (30) degrees of
                               * where he faces, 35 units at most (the game's own aim pick never runs for the other hero) */
            float fx = MEMF(a + 0x70), fy = MEMF(a + 0x74), dx = MEMF(o + 0xF0) - MEMF(a + 0xF0), dy = MEMF(o + 0xF4) - MEMF(a + 0xF4);
            float fn, d = sqrtf(dx * dx + dy * dy), cone = getenv("TT_VS_AIMCONE") ? (float)atof(getenv("TT_VS_AIMCONE")) : 35.0f;
            {   /* the stick pushed: the way it points (the camera's view: forward up, right to the right), as the game's own
                 * aim pick turns the aim with it; else the way he faces */
                extern int16_t tt_vs_stick[2][2]; float lx = tt_vs_stick[w][0] / 32767.0f, ly = tt_vs_stick[w][1] / 32767.0f; uint32_t cm = MEM32(0x43B164u);
                if (lx * lx + ly * ly > 0.25f * 0.25f && cm >= 0x10000u) {
                    float cx = MEMF(cm + 0x40) - MEMF(cm + 0x50), cy = MEMF(cm + 0x44) - MEMF(cm + 0x54), cn = sqrtf(cx * cx + cy * cy);
                    if (cn > 1e-3f) { cx /= cn; cy /= cn; fx = ly * cx + lx * cy; fy = ly * cy - lx * cx; }
                }
            }
            fn = sqrtf(fx * fx + fy * fy);
            int on = fn > 1e-3f && d > 0.5f && d < 35.0f && (fx * dx + fy * dy) / (fn * d) >= cosf(cone * 0.0174533f);
            if (getenv("TT_VS_AIMLOG")) { static uint32_t tl[2]; if (coop_now() - tl[w] > 700u) { extern int16_t tt_vs_stick[2][2]; tl[w] = coop_now();
                tt_dbg("[VSAIM] P%d aim dir %.2f %.2f (stick %d %d, faces %.2f %.2f), to the other %.2f %.2f\n", w + 1, fx, fy, tt_vs_stick[w][0], tt_vs_stick[w][1], MEMF(a + 0x70), MEMF(a + 0x74), dx, dy); } }
            if (!was[w] || (MEM32(c + 0x98) == o) != on) {
                if (getenv("TT_VS_AIMLOG")) tt_dbg("[VSAIM] P%d aims: %s (%.1f away, %.0f degrees off)\n", w + 1, on ? "locked on the other" : "no lock", d,
                                                   fn > 1e-3f && d > 1e-3f ? acosf(fmaxf(-1.0f, fminf(1.0f, (fx * dx + fy * dy) / (fn * d)))) * 57.29578f : -1.0f);
                MEM32(c + 0x98) = on ? o : 0; if (!on) MEM32(c + 0x9C) = 0;
            }
            was[w] = 1;
            continue;
        }
        was[w] = 0;
        if (MEM32(c + 0x98) != o) {
            static unsigned n[2]; static uint32_t lastlog;
            if (getenv("TT_VS_AIMLOG") && (n[w]++ < 6 || coop_now() - lastlog > 5000u)) { lastlog = coop_now();
                tt_dbg("[VSAIM] t %u %s's target was %08X (rewrite %u)\n", coop_now(), w ? "P2" : "P1", MEM32(c + 0x98), n[w]); }
            MEM32(c + 0x98) = o;
        }
    }
}
/* 0x00053B30 (thiscall component; ret): a character's target cleared (+0x98, +0x9C). The player's world pass (0x5A450)
 * clears it on every character outside its lists, the other hero included; in Versus a hero keeps his lock on the
 * other (vs_targets) */
extern void sub_00053B30_gen(void);
void sub_00053B30(void)
{
    if (tt_vs_on() && ecx >= 0x10000u && ((coop_first_char >= 0x10000u && ecx == MEM32(coop_first_char + 0x494) && !vs_aiming(0)) || (coop_p2 >= 0x10000u && ecx == MEM32(coop_p2 + 0x494) && !vs_aiming(1)))) { esp += 4; return; }
    sub_00053B30_gen();
}
uint32_t tt_vs_proj_foe(uint32_t pj)
{
    uint32_t o;
    if (!tt_vs_on() || pj < 0x10000u || coop_first_char < 0x10000u || coop_p2 < 0x10000u || getenv("TT_VS_NOPROJ")) return 0;
    o = MEM32(pj + 0x11C);
    { static int n; if (getenv("TT_VS_AIMLOG") && n < 400 && (o == coop_first_char || o == coop_p2) && (n++ % 4) == 0) { uint32_t f = o == coop_p2 ? coop_first_char : coop_p2;
        tt_dbg("[VSAIM] projectile %08X of P%d at %.1f %.1f %.1f, foe at %.1f %.1f %.1f\n", pj, o == coop_p2 ? 2 : 1, MEMF(pj + 0xF0), MEMF(pj + 0xF4), MEMF(pj + 0xF8), MEMF(f + 0xF0), MEMF(f + 0xF4), MEMF(f + 0xF8)); } }
    return o == coop_first_char ? coop_p2 : o == coop_p2 ? coop_first_char : 0;
}
static void coop_p2_target(void)
{
    static int off = -1; uint32_t c, t, best = 0; float bd = 100.0f; int i;
    if (off < 0) off = getenv("TT_COOP_NOP2TARGET") != 0;
    if (off || coop_mode() != 3 || coop_p2 < 0x10000u) return;
    { extern int coop_hero_down(int); if (coop_hero_down(1)) return; }
    c = MEM32(coop_p2 + 0x494); if (c < 0x10000u) return;
    if (tt_vs_on()) { vs_targets(); return; }   /* (Versus: each hero's target is the other) */
    { extern uint32_t coop_bot_aim_until; if (coop_now() < coop_bot_aim_until) return; }   /* (the P2 Bot aiming: the lock is the game's own) */
    { extern uint32_t coop_bot_tgt_hold, coop_bot_tgt_until; if (coop_bot_tgt_hold && coop_now() < coop_bot_tgt_until) { MEM32(c + 0x98) = coop_bot_tgt_hold; return; } }   /* (the P2 Bot's own pick: a stretched tentacle) */
    t = MEM32(c + 0x98);
    for (i = 0; i < coop_nchars; i++) {
        uint32_t e = coop_chars[i]; float dx, dy, d2;
        if (MEM32(e + 0x114) != 3 || (int32_t)MEM32(e + 0x2FC) <= 0) continue;
        dx = MEMF(e + 0xF0) - MEMF(coop_p2 + 0xF0); dy = MEMF(e + 0xF4) - MEMF(coop_p2 + 0xF4); d2 = dx * dx + dy * dy;
        if (e == t && d2 < 1225.0f) return;   /* current target still good (the game's own aim pick, 0x5A150, reaches 35) */
        if (d2 < bd) { bd = d2; best = e; }
    }
    if (best != t) { MEM32(c + 0x98) = best; coop_p2tgt_set++; }
}
/* experience per player. A hero's record is 0x4AB598 + hero*0x348 (0xD2310 = the current hero, 0xD26F0 = its record):
 * +0x6C committed experience, +0x78 level, +0x90 the level's running experience (float, committed at the level's
 * end); the gauge (0xCF9A0) shows (int)+0x90 + +0x6C - the level's threshold (0x9D6D0), out of the next threshold
 * (0x9D6A0) minus it (10000 from level 9). Player 2 gets a shadow record: a copy of his hero's record
 * (TT_COOP_P2HERO = hero index, default player 1's), in guest memory nobody uses, never written to the save. When
 * player 1's running experience rises within 2 s of a kill by player 2 (last weapon hit), the rise goes to the
 * shadow instead. TT_COOP_NOP2EXP turns it off. */
/* (2026-10-19) in memory of our own: at 0x4BB300 (after our HUD elements) it was found in use ("HUD copy area not
 * zero") on some runs, and then player 2 had neither his gauge nor his Perfect ring (a playtest: "the experience bar is
 * still missing", "the circular meter only appears for P1") */
static uint32_t coop_p2rec_mem;
static uint32_t coop_p2rec_va(void)
{
    extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment);
    if (!coop_p2rec_mem) { coop_p2rec_mem = xbox_ContiguousAlloc(0x400u, 16u); if (coop_p2rec_mem) { uint32_t j; for (j = 0; j < 0x400u; j += 4) MEM32(coop_p2rec_mem + j) = 0; } }
    return coop_p2rec_mem ? coop_p2rec_mem : 0x4BB300u;
}
#define COOP_P2REC coop_p2rec_va()
int coop_p2rec_ok;
static int coop_kill_who; static uint32_t coop_kill_t; uint32_t coop_p2pop_until;
uint32_t coop_p2_drain_t;   /* the game's own damage drain (0x9DEC0) on player 2's record, last time (coop_ratelog) */
static unsigned coop_exp_moved[2];
static uint32_t coop_hero_rec(void) { uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx, h; PUSH32(esp, 0x000D2310u); sub_000D2310(); h = eax; esp = sp; eax = r0; ecx = r1; edx = r2; return 0x4AB598u + h * 0x348u; }
/* a kill by player 2 (his weapon hit it last): the game's own kill award (0x99D40, cdecl enemy: rating from the
 * record's combat meter, the rating popup, experience by enemy type and rating, kill counts), run at the death
 * with player 2's context in place, so it goes to his record */
void coop_kill_by(int who, uint32_t enemy)
{
    extern int tt_coop_enter_ctx(void); extern void tt_coop_leave(void); extern uint32_t coop_rec_addr;
    coop_kill_who = who; coop_kill_t = coop_now();
    if (who != 2 || !coop_rec_addr || getenv("TT_COOP_NOP2EXP")) return;
    if (!getenv("TT_COOP_OLDKILL")) return;   /* (now the game's own award decides, sub_00099D40 below) */
    coop_p2pop_until = coop_now() + 2600;
    {   uint32_t ty = MEM32(enemy + 0x2F0), sp0 = esp, d; PUSH32(esp, ty); PUSH32(esp, 0x0006CAB0u); sub_0006CAB0(); d = eax; esp = sp0;
        tt_dbg("[COOPEXP] kill check: enemy %08X hp %d state %u +2F0 %08X -> type data %08X (base experience %d) | record +4 %.2f +5C %.2f\n", enemy, (int32_t)MEM32(enemy + 0x2FC), MEM32(enemy + 0x408), ty, d,
               d >= 0x10000u ? (int32_t)MEM32(d + 0xA4) : -1, MEMF(coop_rec_addr + 4), MEMF(coop_rec_addr + 0x5C)); }
    {   uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx; int sw = tt_coop_enter_ctx(); float before = MEMF(coop_rec_addr + 0x90), after;
        tt_dbg("[COOPEXP]   in his context: record %08X +4 %.2f +5C %.2f +6C %d +78 %d (player %08X)\n", coop_rec_addr, MEMF(coop_rec_addr + 4), MEMF(coop_rec_addr + 0x5C), (int32_t)MEM32(coop_rec_addr + 0x6C), (int32_t)MEM32(coop_rec_addr + 0x78), MEM32(0x43B15Cu));
        PUSH32(esp, enemy); PUSH32(esp, 0x00099D40u); sub_00099D40();
        esp = sp; eax = r0; ecx = r1; edx = r2;
        after = MEMF(coop_rec_addr + 0x90);
        if (sw) tt_coop_leave();
        coop_exp_moved[1] += (unsigned)(after - before + 0.5f);
        tt_dbg("[COOPEXP] kill by player 2 (enemy %08X%s): the game's award on his record, +%.0f (now %.0f)\n", enemy, sw ? "" : ", NOT in his context", after - before, after);
    }
}
/* the game's kill award 0x00099D40 (cdecl enemy; ret): at an enemy's death the character message handler (from
 * 0x00085706, gated by the enemy's +0x526 and +0x538 only, not by who killed it) runs it with player 1's context, so
 * player 2's kills went into player 1's record as kill counts (rating from player 1's meter; a playtest run: Aragorn
 * "Fair 8" without attacking), while coop_kill_by gave player 2 his award a second time from a health watch (one
 * enemy credited 3 times). Now, when the enemy's last attacker (+0x4BC, set by the hit, see sub_000849F0) is player
 * 2, the game's own call runs in his context, once, and the health watch gives nothing (TT_COOP_OLDKILL: as before).
 * TT_COOP_KILLLOG: every call. */
/* who did each enemy's last damage (its health lowered by his hit: 0x849F0 below; 1 player 1, 2 player 2, 0 another): the kill
 * award goes to the player whose damage took it to 0 (the owner asked: "a check on who did the damage that lead the enemy to
 * have 0 hp"); the last attacker (+0x4BC) is set by any hit, damage or not, and not by every damage */
static struct { uint32_t e, t; int who; } coop_lastdmg[64]; static int coop_lastdmg_i;
static void coop_lastdmg_note(uint32_t e, int who)
{
    int i; for (i = 0; i < 64 && coop_lastdmg[i].e != e; i++) ;
    if (i == 64) { i = coop_lastdmg_i; coop_lastdmg_i = (coop_lastdmg_i + 1) & 63; }
    coop_lastdmg[i].e = e; coop_lastdmg[i].who = who; coop_lastdmg[i].t = coop_now();
}
static int coop_lastdmg_now(uint32_t e) { int i; for (i = 0; i < 64; i++) if (coop_lastdmg[i].e == e) return coop_now() - coop_lastdmg[i].t < 20u; return 0; }   /* (noted this frame) */
static int coop_lastdmg_who(uint32_t e)   /* -1: not known (none within 5 s) */
{
    int i; for (i = 0; i < 64; i++) if (coop_lastdmg[i].e == e && coop_now() - coop_lastdmg[i].t < 5000u) return coop_lastdmg[i].who;
    return -1;
}
extern void sub_00099D40_gen(void);
void sub_00099D40(void)
{
    extern int tt_coop_enter_ctx(void); extern void tt_coop_leave(void); extern uint32_t coop_rec_addr;
    uint32_t en = MEM32(esp + 4), ra = MEM32(esp), la = en >= 0x10000u ? MEM32(en + 0x4BC) : 0; int sw = 0, mine;
    float b1 = coop_rec_addr ? MEMF(coop_rec_addr + 0x90) : 0, a1;
    {   int lw = en >= 0x10000u && !getenv("TT_COOP_KILLBYLAST") ? coop_lastdmg_who(en) : -1;   /* (whose damage brought it to 0; not known: its last attacker) */
        mine = coop_mode() == 3 && coop_p2 >= 0x10000u && ra == 0x00085711u && (lw >= 0 ? lw == 2 : la == coop_p2) && coop_rec_addr && !getenv("TT_COOP_OLDKILL") && !getenv("TT_COOP_NOP2EXP");
        if (lw >= 0 && (lw == 2) != (la == coop_p2) && coop_mode() == 3) { static int n; if (n++ < 20) tt_dbg("[COOPEXP] kill of %08X '%s': the last damage player %d's, its last attacker %s -- the award to player %d\n", en, coop_nm(en), lw, la == coop_p2 ? "player 2" : la == coop_first_char ? "player 1" : "another", mine ? 2 : 1); } }
    if (mine) { sw = tt_coop_enter_ctx(); b1 = MEMF(coop_rec_addr + 0x90); coop_p2pop_until = coop_now() + 2600; if (getenv("TT_COOP_KILLLOG")) tt_dbg("[KILLLOG] player 2's kill: his rating meter +4 %.2f (+5C %.2f)\n", MEMF(coop_rec_addr + 4), MEMF(coop_rec_addr + 0x5C)); }
    sub_00099D40_gen();
    a1 = coop_rec_addr ? MEMF(coop_rec_addr + 0x90) : 0;
    if (mine) coop_exp_moved[1] += (unsigned)(a1 - b1 + 0.5f);
    if (sw) tt_coop_leave();
    {   /* Lurtz: player 2's hits on him count as player 1's (coop_is_lurtz), so the game's award is player 1's; if player 2 hit him in
         * the fight, the award runs once more in his context (a playtest: "make it so p2 also earn reward on lurtz death").
         * TT_COOP_NOLURTZSHARE: player 1's only */
        extern int coop_is_lurtz(uint32_t e); extern uint32_t coop_lurtz_p2_hits;
        if (!mine && coop_mode() == 3 && coop_p2 >= 0x10000u && coop_rec_addr && ra == 0x00085711u && coop_is_lurtz(en) && coop_lurtz_p2_hits && !getenv("TT_COOP_NOLURTZSHARE")) {
            uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx; int sw2 = tt_coop_enter_ctx(); float c1 = MEMF(coop_rec_addr + 0x90);
            PUSH32(esp, en); PUSH32(esp, 0x00085711u); sub_00099D40_gen(); esp = sp;
            tt_dbg("[COOPEXP] Lurtz's award for player 2 too (%u hits of his): +%.0f\n", coop_lurtz_p2_hits, MEMF(coop_rec_addr + 0x90) - c1);
            coop_p2pop_until = coop_now() + 2600; coop_lurtz_p2_hits = 0;
            if (sw2) tt_coop_leave(); eax = r0; ecx = r1; edx = r2;
        }
    }
    if (mine) tt_dbg("[COOPEXP] kill by player 2 (enemy %08X, the game's award%s): +%.0f (now %.0f)\n", en, sw ? " in his context" : ", already his context", a1 - b1, a1);
    if (getenv("TT_COOP_KILLLOG") && coop_p2) { static unsigned k; if (k++ < 200)
        tt_dbg("[KILLLOG] award for %08X from %06X | last attacker %08X (%s) | player %08X | +526 %u +538 %.2f | +%.0f\n", en, ra, la, la == coop_p2 ? "P2" : la == coop_first_char ? "P1" : "other",
               MEM32(0x43B15Cu), en >= 0x10000u ? MEM8(en + 0x526) : 0, en >= 0x10000u ? MEMF(en + 0x538) : 0.0f, a1 - b1); }
}
/* player 2's combat rating meter (record +4). @CauseDamage feeds the damage the player takes into it
 * (0x98EF0(amount), only when the victim is [0x43B15C]), so the twin's never moved and his kills were worth 0.
 * Here: when player 2's health drops (not a respawn), the same call with his context (and record) in place. */
static void coop_p2_meter(void)
{
    static int32_t prev = -1; int32_t hp; extern int tt_coop_enter_ctx(void); extern void tt_coop_leave(void); extern uint32_t coop_rec_addr;
    if (!coop_rec_addr || coop_p2 < 0x10000u || getenv("TT_COOP_NOP2METER")) return;
    hp = (int32_t)MEM32(coop_p2 + 0x2FC);
    /* (the game drains it itself now that enemy boxes on him run in his context, sub_000BAD10: then this call was a second
     * drain -- a bot run: 5 hits taken, 10 drains, player 1 one a hit; a playtest: "his combo meter never raise") */
    if (prev > 0 && hp < prev && coop_p2_drain_t && coop_now() - coop_p2_drain_t < 250u && !getenv("TT_COOP_P2DRAIN2")) {
        static int n; if (n++ < 4) tt_dbg("[COOPEXP] player 2 took %d: the game drained his meter already (now %.2f)\n", prev - hp, coop_p2_state() ? *(const float *)(coop_p2_state() + 0x400 + 4) : -1.0f);
        prev = hp; return; }
    if (prev > 0 && hp < prev) {
        uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx; int sw = tt_coop_enter_ctx();
        PUSH32(esp, (uint32_t)(prev - (hp < 0 ? 0 : hp))); PUSH32(esp, 0x00098EF0u); sub_00098EF0();
        esp = sp; eax = r0; ecx = r1; edx = r2;
        { static int n; if (n++ < 8) tt_dbg("[COOPEXP] player 2 took %d: his rating meter +4 now %.2f%s\n", prev - hp, MEMF(coop_rec_addr + 4), sw ? "" : " (NOT in his context)"); }
        if (sw) tt_coop_leave();
    }
    prev = hp;
}
static void coop_exp_frame(void)   /* the HUD's copy of player 2's record (his gauge), from his saved state */
{
    coop_p2_meter();
    extern uint32_t coop_rec_addr; extern const uint8_t *coop_p2_state(void); uint32_t j; const uint8_t *s;
    if (!coop_rec_addr || coop_mode() != 3 || coop_p2 < 0x10000u) { coop_p2rec_ok = 0; return; }
    if (!coop_p2rec_ok) { uint32_t nz = 0; for (j = 0; j < 0x348; j += 4) if (MEM32(COOP_P2REC + j)) nz++; if (nz) { static int said; if (!said++) tt_dbg("[COOPEXP] HUD copy area %08X not zero: no gauge for player 2\n", COOP_P2REC); return; } }
    if (getenv("TT_COOP_RECSYNC") && !coop_depth && MEM32(0x43B15Cu) != coop_p2) {   /* (opt-in) his copy follows player 1's record (the hero's settings, e.g. +4 set at the level's start) except his own progression: +0x6C..+0x7F experience and level, +0x90..+0x1FF the level's running experience and kill counts */
        extern uint8_t *coop_p2_state_w(void); uint8_t *w = coop_p2_state_w() + 0x400;
        for (j = 0; j < 0x348; j += 4) if (!((j >= 0x6C && j < 0x80) || (j >= 0x90 && j < 0x200))) *(uint32_t *)(w + j) = MEM32(coop_rec_addr + j);
    }
    { static uint32_t lg; if (coop_now() - lg > 2000 && getenv("TT_COOP_RECDUMP")) { const uint8_t *q = coop_p2_state() + 0x400; uint32_t o; lg = coop_now(); tt_dbg("[COOPREC] P1:"); for (o = 0; o < 0x68; o += 4) tt_dbg(" %08X", MEM32(coop_rec_addr + o)); tt_dbg("\n[COOPREC] P2:"); for (o = 0; o < 0x68; o += 4) tt_dbg(" %08X", *(const uint32_t *)(q + o)); tt_dbg("\n"); } }
    s = coop_p2_state() + 0x400;
    for (j = 0; j < 0x348; j += 4) MEM32(COOP_P2REC + j) = *(const uint32_t *)(s + j);
    coop_p2rec_ok = 1;
}/* the gauge's numbers for a record (as 0xCF9A0): *cur out of *max */
static void coop_rec_gauge(uint32_t rec, int32_t *cur, int32_t *max)
{
    uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx; int32_t lo, hi, e = (int32_t)MEMF(rec + 0x90) + (int32_t)MEM32(rec + 0x6C);
    { extern int tt_vs_on(void); if (tt_vs_on() && !getenv("TT_VS_OWNEXP")) { *max = 10000; *cur = 10000; return; } }   /* (1.9.7, Versus: at the top level, the bar full) */
    if ((int32_t)MEM32(rec + 0x78) >= 9) { *max = 10000; *cur = e % 10000; return; }
    PUSH32(esp, 0x0009D6D0u); ecx = rec; sub_0009D6D0(); lo = (int32_t)eax; esp = sp;
    PUSH32(esp, 0x0009D6A0u); ecx = rec; sub_0009D6A0(); hi = (int32_t)eax; esp = sp;
    eax = r0; ecx = r1; edx = r2;
    *max = hi - lo; *cur = e - lo; if (*max <= 0) *max = 1;
}
/* threat-based aggro. The game's target arbitration (0x5A920 → 0x57A70 → 0x5A330) builds its candidates from the
 * player [0x43B15C] first, then the engagement lists the pass 0x5A450 fills, so the enemies only ever chose player 1.
 * Here each living enemy keeps a threat score per player: the health it loses goes to the player whose weapon last
 * hit it (weapon-hit message 3 from one of his weapons, within 1.5 s), and the scores halve every TT_COOP_THREATHALF
 * seconds (default 6). Right after each arbitration (hook on 0x5A920) an enemy whose top threat is a player is
 * retargeted (the game's setter 0x53AE0) to that player; it switches only when the other player's threat passes the
 * current one by 25 % and 60 health. No threat yet: the game's own choice stands. TT_COOP_NOTHREAT turns it off. */
static struct { uint32_t e, lastt; int32_t hp; float th[2]; int lastw, cur; } coop_thr[COOP_MAXCHARS];
static unsigned coop_thr_switch[2], coop_thr_set;
static int coop_thr_idx(uint32_t e) { int i; for (i = 0; i < coop_nchars && i < COOP_MAXCHARS; i++) if (coop_chars[i] == e) { if (coop_thr[i].e != e) { memset(&coop_thr[i], 0, sizeof coop_thr[i]); coop_thr[i].e = e; coop_thr[i].hp = (int32_t)MEM32(e + 0x2FC); coop_thr[i].cur = -1; } return i; } return -1; }
/* Lurtz (Amon Hen) is run by his script: his bow phase takes arrows, then he charges a pillar near "the player" and, stuck in
 * it, takes blows (a playtest). His checks are player 1's, so for him: no threat steering, no forced arrow damage, and player
 * 2's hits count as player 1's (a playtest: Lurtz at 0 health "just froze when struck by p2"). TT_COOP_LURTZOWN: as other enemies */
int coop_is_lurtz(uint32_t e)
{
    const char *n; extern const char *coop_nm_x(uint32_t a);
    if (e < 0x10000u || e >= 0x84000000u || MEM32(e) != 0x1741C0u || getenv("TT_COOP_LURTZOWN")) return 0;
    n = coop_nm_x(e); return n && (strstr(n, "urtz") != NULL);
}
void coop_threat_hit(uint32_t enemy, uint32_t weapon)
{
    int i = coop_thr_idx(enemy), j, w = -1;
    if (i < 0) return;
    for (j = 0; j < coop_nwpn; j++) { if (weapon == coop_wpn_orig[j]) w = 0; if (weapon == coop_wpn_copy[j]) w = 1; }
    for (j = 0; j < coop_np2w; j++) if (weapon && weapon == coop_p2w[j]) w = 1;   /* (every weapon of his: his own melee weapons and his ranged copies are
        * not in the pairs above, so his hits gave no threat and the cave troll stayed on player 1 -- a playtest; a bot run: 0 switches while
        * player 2 took its health from 1000 to 961) */
    if (weapon == coop_first_char) w = 0; if (weapon == coop_p2 && weapon) w = 1;
    if (w < 0) return;
    coop_thr[i].lastw = w + 1; coop_thr[i].lastt = coop_now();
    {   /* a hit draws the enemy even when it takes no health (blocked, or a phase that takes none): player 2's 129 hits on
         * Lurtz left him on player 1 (a playtest log). TT_COOP_THREATHIT: per hit (8) */
        static float ph = -1; if (ph < 0) ph = getenv("TT_COOP_THREATHIT") ? (float)atof(getenv("TT_COOP_THREATHIT")) : 8.0f;
        coop_thr[i].th[w] += ph; }
}
static void coop_threat_track(void)   /* once a frame: health lost since last frame goes to the last hitter; decay */
{
    static uint32_t tprev; static float half = -1; uint32_t now = coop_now(); float f; int i;
    if (half < 0) { const char *v = getenv("TT_COOP_THREATHALF"); half = v ? (float)atof(v) : 6.0f; if (half < 0.5f) half = 0.5f; }
    f = tprev ? powf(0.5f, (float)(now - tprev) / (1000.0f * half)) : 1.0f; tprev = now;
    for (i = 0; i < coop_nchars && i < COOP_MAXCHARS; i++) {
        uint32_t e = coop_chars[i]; int32_t hp; int k = coop_thr_idx(e);
        if (k < 0 || MEM32(e + 0x114) != 3) continue;
        hp = (int32_t)MEM32(e + 0x2FC);
        if (getenv("TT_COOP_TESTWEAK") && hp > 5) { MEM32(e + 0x2FC) = 5; hp = 5; coop_thr[k].hp = 5; }   /* (bot test: every enemy at 5 health, any blow a kill) */
        if (hp < coop_thr[k].hp && coop_thr[k].lastw && now - coop_thr[k].lastt < 1500) coop_thr[k].th[coop_thr[k].lastw - 1] += (float)(coop_thr[k].hp - hp);
        if (hp < coop_thr[k].hp && !coop_lastdmg_now(e)) {   /* (a drop no hit noted this frame -- the game's pending damage, taken later: its attacker +0x3F4, else the last attacker) */
            uint32_t a = MEM32(e + 0x3F4); if (a != coop_first_char && a != coop_p2) a = MEM32(e + 0x4BC);
            if (a && (a == coop_first_char || a == coop_p2)) coop_lastdmg_note(e, a == coop_p2 ? 2 : 1); }
        if (coop_thr[k].hp > 0 && hp <= 0 && coop_thr[k].lastw && now - coop_thr[k].lastt < 3000) { extern void coop_kill_by(int who, uint32_t enemy); coop_kill_by(coop_thr[k].lastw, e); }   /* who killed it */
        coop_thr[k].hp = hp; coop_thr[k].th[0] *= f; coop_thr[k].th[1] *= f;
    }
}
static void coop_threat_apply(void)   /* after the game's arbitration: point each enemy at its top-threat player */
{
    static int off = -1; int i;
    if (off < 0) off = getenv("TT_COOP_NOTHREAT") != 0;
    if (off || coop_mode() != 3 || coop_p2 < 0x10000u || coop_first_char < 0x10000u) return;
    for (i = 0; i < coop_nchars && i < COOP_MAXCHARS; i++) {
        uint32_t e = coop_chars[i], c, want, cur; int k = coop_thr_idx(e), w;
        if (k < 0 || MEM32(e + 0x114) != 3 || (int32_t)MEM32(e + 0x2FC) <= 0 || coop_is_lurtz(e)) continue;
        if (coop_thr[k].th[0] < 1 && coop_thr[k].th[1] < 1) { coop_thr[k].cur = -1; continue; }   /* no threat: the game decides */
        w = coop_thr[k].th[1] > coop_thr[k].th[0] ? 1 : 0;
        if (coop_thr[k].cur >= 0 && w != coop_thr[k].cur) {   /* hysteresis */
            float a = coop_thr[k].th[coop_thr[k].cur], b = coop_thr[k].th[w];
            if (!(b > 1.25f * a && b - a > 60.0f)) w = coop_thr[k].cur;
        }
        if (w != coop_thr[k].cur) { if (coop_thr[k].cur >= 0) coop_thr_switch[w]++; tt_dbg("[COOPTHREAT] enemy %08X now on P%d (threat P1 %.0f, P2 %.0f)\n", e, w + 1, coop_thr[k].th[0], coop_thr[k].th[1]); coop_thr[k].cur = w; }
        { extern int coop_hero_down(int); if (coop_hero_down(w)) continue; }   /* never onto a downed hero */
        want = w ? coop_p2 : coop_first_char; c = MEM32(e + 0x494); if (c < 0x10000u) continue;
        cur = MEM32(c + 0x98);
        if (cur != want) {   /* the game's setter 0x53AE0 (thiscall component, target; ret 4): +0x98 and +0x9C */
            uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx;
            PUSH32(esp, want); PUSH32(esp, 0x00053AE0u); ecx = c; sub_00053AE0();
            esp = sp; eax = r0; ecx = r1; edx = r2; coop_thr_set++;
        }
    }
}
/* death and respawn (the rules: a hero who goes down respawns beside the other after TT_COOP_RESPAWN ms,
 * default 30000; game over only when both are down). The game never kills player 2 by itself (its death check is
 * the level script's, by player 1's name), so here: health <= 0 → the hero's die method 0x838E0, which for an actor
 * that is neither an enemy nor [0x43B15C] runs only the common death (0x81790, 0x816A0, 0x82180): no death cam, no
 * mission end. (research for now: the fields the death changes are logged, to build the revive.) */
static uint32_t coop_snap[2][0x140], coop_csnap[2][0xC0];
static int coop_down[2]; static uint32_t coop_down_t[2], coop_upwatch[2];
static void coop_snap_take(int k, uint32_t a) { uint32_t c = MEM32(a + 0x494), i; for (i = 0; i < 0x140; i++) coop_snap[k][i] = MEM32(a + 0x100 + 4 * i); for (i = 0; i < 0xC0; i++) coop_csnap[k][i] = c >= 0x10000u ? MEM32(c + 4 * i) : 0; }
static void coop_snap_diff(const char *what, int k, uint32_t a)
{
    uint32_t c = MEM32(a + 0x494), i; int n = 0;
    for (i = 0; i < 0x140 && n < 40; i++) if (MEM32(a + 0x100 + 4 * i) != coop_snap[k][i] && (0x100 + 4 * i < 0xF0 || 0x100 + 4 * i > 0xFC)) { tt_dbg("[COOPDIFF] %s actor +%03X %08X -> %08X\n", what, 0x100 + 4 * i, coop_snap[k][i], MEM32(a + 0x100 + 4 * i)); n++; }
    for (i = 0; i < 0xC0 && n < 70 && c >= 0x10000u; i++) if (MEM32(c + 4 * i) != coop_csnap[k][i]) { tt_dbg("[COOPDIFF] %s component +%03X %08X -> %08X\n", what, 4 * i, coop_csnap[k][i], MEM32(c + 4 * i)); n++; }
}
static void coop_die_call(uint32_t a)   /* the hero's die method (thiscall, no args, ret) */
{
    uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx;
    PUSH32(esp, 0x000838E0u); ecx = a; sub_000838E0();
    esp = sp; eax = r0; ecx = r1; edx = r2;
}
static void coop_deathcam_p1(void)   /* the game's own end: player 1's death cam (as 0x838E0 calls it), then code 5 */
{
    uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx, p1 = coop_first_char, pos;
    esp -= 16; pos = esp; MEM32(pos) = MEM32(p1 + 0xF0); MEM32(pos + 4) = MEM32(p1 + 0xF4); MEM32(pos + 8) = MEM32(p1 + 0xF8); MEM32(pos + 12) = MEM32(p1 + 0xFC);
    PUSH32(esp, pos); PUSH32(esp, 0); PUSH32(esp, 0); PUSH32(esp, 0x00069130u); ecx = MEM32(0x43B164u); sub_00069130();
    esp = sp; eax = r0; ecx = r1; edx = r2;
}
static void coop_hero_state(uint32_t a, uint32_t st)   /* the hero's state machine (+0x3D8): 0x53090 (thiscall, state; ret 4) */
{
    uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx;
    PUSH32(esp, st); PUSH32(esp, 0x00053090u); ecx = a + 0x3D8; sub_00053090();
    esp = sp; eax = r0; ecx = r1; edx = r2;
}
int coop_hero_down(int who) { return coop_down[who]; }
uint32_t coop_p1_actor(void) { return coop_mode() == 3 ? coop_first_char : 0; }
/* Player 2's character, decided in one place (asked by heroes.c when a level opens). In order: TT_COOP_P2CHAR
 * (testing: a skin name, or "same"), the levels that force one (the prologue: Isildur, as player 1; Weathertop:
 * Legolas, with player 1's weapons -- Aragorn's sword and torch, coop_p2_p1wpn), then the character select's choice
 * for player 2 (tt_coop_p2_selected, set by the select screen once player 2 has one). NULL: a copy of player 1's hero. A name is a heroes.c skin (frodo, boromir, gandalf, lurtz, elrond, sauron, uruk); his model,
 * texture and weapons go into the level, and the twin is built with them (sub_000F5BC0, coop_build_weapon). */
char tt_coop_p2_selected[32]; const char *tt_coop_p2_char_now;
const char *tt_coop_p2_char(const char *level)
{
    static const struct { const char *level, *who; int p1wpn; } forced[] = { { "00ISUL1", "isildur", 0 }, { "01WEAT1", "legolas", 1 } };
    const char *e = getenv("TT_COOP_P2CHAR"), *r = NULL; int i;
    if (coop_mode() != 3) return NULL;
    coop_p2_p1wpn = 0;
    if (e) r = (*e && _stricmp(e, "same")) ? e : NULL;
    else {   /* a level that forces one first (the story's; not in Versus), then the character select's */
        for (i = 0; i < (int)(sizeof forced / sizeof forced[0]) && !tt_vs_on(); i++)
            if (level && !_strnicmp(level, forced[i].level, strlen(forced[i].level))) { r = forced[i].who; coop_p2_p1wpn = forced[i].p1wpn && !getenv("TT_COOP_NOP1WPN"); break; }
        if (!r && tt_coop_p2_selected[0] && _stricmp(tt_coop_p2_selected, "same")) r = tt_coop_p2_selected;
    }
    tt_coop_p2_char_now = r;   /* this level's, for his record (coop_take) */
    return r;
}
uint32_t coop_p2_actor(void) { return coop_mode() == 3 ? coop_p2 : 0; }
/* (research, TT_COOP_INPLOG) the state machine's inputs (0x53090, thiscall sm = actor + 0x3D8; input) raised on either
 * hero: which button gives which (INP_SHOOT 38, INP_DRAWBOW 50, INP_BOWCHARGE 51, INP_THROWN 47, INP_ARROWS 37) */
/* (1.9.7, Versus) blocks can't hold forever: a hero who blocks TT_VS_GUARDN (4) blows in a row (INP_BLOCKREACT 18, each
 * within 3 s of the last) has his guard broken by the next -- it reaches him as a hit (INP_TAKEDAMAGE 5) and costs him
 * TT_VS_GUARDPCT (6) per cent of his health (vs_frame). A blow taken ends the run */
static int vs_guard_n[2]; static uint32_t vs_guard_t[2], vs_guard_broke[2];
extern void sub_00053090_gen(void);
void sub_00053090(void)
{
    uint32_t a = ecx - 0x3D8u, in = MEM32(esp + 4);
    if ((in == 18u || in == 5u) && (a == coop_first_char || (coop_p2 >= 0x10000u && a == coop_p2)) && a >= 0x10000u && !getenv("TT_VS_NOGUARDBREAK")) {
        extern int tt_vs_on(void); int w = a == coop_p2; uint32_t now = coop_now();
        if (tt_vs_on()) {
            if (in == 5u) vs_guard_n[w] = 0;
            else {
                int lim = getenv("TT_VS_GUARDN") ? atoi(getenv("TT_VS_GUARDN")) : 4;
                if (now - vs_guard_t[w] > 3000u) vs_guard_n[w] = 0;
                vs_guard_t[w] = now;
                if (++vs_guard_n[w] > lim) { vs_guard_n[w] = 0; vs_guard_broke[w] = now | 1u; MEM32(esp + 4) = 5u; tt_dbg("[VS] P%d's guard broken (%d blocks in a row)\n", w + 1, lim); }
            }
        }
    }
    if (in == 3u && (a == coop_first_char || (coop_p2 >= 0x10000u && a == coop_p2))) { extern int tt_vs_on(void); extern void tt_vs_kb_in(int v); if (tt_vs_on()) tt_vs_kb_in(a == coop_p2); }
    if (coop_mode() == 3 && getenv("TT_COOP_COMBOLOG") && (in == 24u || in == 39u || in == 54u || in == 56u || in == 60u) && a >= 0x10000u) {   /* (research) INP_COMBO1-5: whose, from where, the combo block */
        static int n; if (n++ < 5000) tt_dbg("[COMBO] t %u INP_COMBO%d on %s (from %06X) in %s context | block 264..283: %d %d %d %d %d %d %d %d | grade 1B1078 %d\n", coop_now(),
            in == 24u ? 1 : in == 39u ? 2 : in == 54u ? 3 : in == 56u ? 4 : 5, a == coop_p2 ? "player 2" : a == coop_first_char ? "player 1" : "other", MEM32(esp), coop_depth ? "player 2's" : "player 1's",
            (int32_t)MEM32(0x43B264u), (int32_t)MEM32(0x43B268u), (int32_t)MEM32(0x43B26Cu), (int32_t)MEM32(0x43B270u), (int32_t)MEM32(0x43B274u), (int32_t)MEM32(0x43B278u), (int32_t)MEM32(0x43B27Cu), (int32_t)MEM32(0x43B280u), (int32_t)MEM32(0x1B1078u));
    }
    if (coop_mode() == 3 && getenv("TT_COOP_INPLOG") && (a == coop_p2 || a == coop_first_char) && in != 63u && in != 37u && in != 9u && in != 59u) {   /* (37 INP_ARROWS: every frame) */
        static int n; if (n++ < 3000) tt_dbg("[COOPINP] t %lu P%d input %u state %u\n", (unsigned long)coop_now(), a == coop_p2 ? 2 : 1, in, MEM32(a + 0x408));
    }
    sub_00053090_gen();
}
uint32_t coop_down_time(void) { return coop_down[1] ? coop_now() - coop_down_t[1] : coop_down[0] ? coop_now() - coop_down_t[0] : 0; }   /* the downed hero's time down (player 2's first) */
static int coop_gameover; static uint32_t coop_gameover_t; int coop_end_seen; unsigned coop_herohits[2];
int coop_is_gameover(void) { return coop_gameover; }
int tt_coop_post_on(void) { return coop_mode() == 3 && coop_p2 >= 0x10000u; }
int tt_coop_post_ph;   /* the post-level screens' phase (the tick above): 0 player 1's stats, 1 player 2's stats, 2 player 1's shop, 3 player 2's shop */   /* (the main-loop tick, above: player 2's post-level screens) */
static void coop_respawn(int who)
{
    uint32_t a = who ? coop_p2 : coop_first_char, o = who ? coop_first_char : coop_p2, c = MEM32(a + 0x494);
    CoopStood tp; uint32_t opm = coop_pathmap(o); int tk = coop_trail_pick(MEMF(o + 0xF0), MEMF(o + 0xF4), &tp);
    if (tk == 1 && opm && coop_cell_clear(opm, MEMF(o + 0xF0), MEMF(o + 0xF4))) tk = 0;   /* (none near the other, who stands on clear ground: beside him, below) */
    if (tk) {   /* (a spot player 1 stood on: above) */
        MEMF(a + 0xF0) = tp.x; MEMF(a + 0xF4) = tp.y; MEMF(a + 0xF8) = tp.z; MEM32(a + 0x2EC) = tp.pm;
        tt_dbg("[COOPDEATH] P%d back at %.1f %.1f %.1f (pathmap %u), a spot player 1 stood on, %.1f from P%d\n", who + 1, tp.x, tp.y, tp.z, tp.pm,
               sqrtf((tp.x - MEMF(o + 0xF0)) * (tp.x - MEMF(o + 0xF0)) + (tp.y - MEMF(o + 0xF4)) * (tp.y - MEMF(o + 0xF4))), 2 - who);
    } else { float x, y; coop_place_target(o, &x, &y); MEMF(a + 0xF0) = x; MEMF(a + 0xF4) = y; MEMF(a + 0xF8) = MEMF(o + 0xF8); MEM32(a + 0x2EC) = MEM32(o + 0x2EC); }   /* (no trail yet: beside the other on his pathmap) */
    MEM32(a + 0x90) = MEM32(a + 0xF0); MEM32(a + 0x94) = MEM32(a + 0xF4); MEM32(a + 0x98) = MEM32(a + 0xF8);
    MEM32(a + 0x2FC) = MEM32(a + 0x300);   /* full health */
    if (!getenv("TT_COOP_NOHITFLAGS")) {   /* the death sequence leaves +0x49C bit 0x40 set and +0x110 bit 0x20 clear: no hit reached him after a respawn (observed). Put them back. */
        uint32_t f1 = MEM32(a + 0x49C), f2 = MEM32(a + 0x110);
        MEM32(a + 0x49C) = f1 & ~0x40u; MEM32(a + 0x110) = f2 | 0x20u;
        if (f1 != MEM32(a + 0x49C) || f2 != MEM32(a + 0x110)) tt_dbg("[COOPDEATH] P%d hit flags restored: +49C %08X -> %08X, +110 %08X -> %08X\n", who + 1, f1, MEM32(a + 0x49C), f2, MEM32(a + 0x110));
    }
    if (MEM32(a + 0x408) == 50 && !getenv("TT_COOP_NOGETUP")) {   /* the full death's end (50, lying): no input takes him out (scan of 1-45, observed). The state machine (+0x3D8) ignores inputs whose bit is in its block mask (+0x50/+0x54, 0x53090): cleared, the state set to 47 (dead, the one INP_RESSURECT gets up from) with its time at 0 */
        uint32_t sm = a + 0x3D8, to = getenv("TT_COOP_UPSET") ? (uint32_t)atoi(getenv("TT_COOP_UPSET")) : 47u;
        tt_dbg("[COOPDEATH] P%d up from 50: block mask %08X %08X, inputs %08X %08X -> state %u\n", who + 1, MEM32(sm + 0x54), MEM32(sm + 0x50), MEM32(sm + 0xC), MEM32(sm + 8), to);
        MEM32(sm + 0x50) = 0; MEM32(sm + 0x54) = 0; MEM32(a + 0x408) = to; MEMF(a + 0x420) = 0.0f;
    }
    {   uint32_t st = MEM32(a + 0x408);   /* the downed pose (dying 44-46, held) or dead 47: INP_RESSURECT (28) gets him up (from 47: 48, then idle; found by an event scan) */
        if ((st == 47 || (st >= 44 && st <= 46)) && !getenv("TT_COOP_NOGETUP")) { coop_hero_state(a, getenv("TT_COOP_UPEV") ? (uint32_t)atoi(getenv("TT_COOP_UPEV")) : 28u); coop_upwatch[who] = coop_now(); }
        tt_dbg("[COOPDEATH] P%d get-up from state %u -> %u\n", who + 1, st, MEM32(a + 0x408)); }
    coop_down[who] = 0;
    tt_dbg("[COOPDEATH] P%d respawned beside P%d (health %d, state %u)\n", who + 1, 2 - who, (int32_t)MEM32(a + 0x2FC), MEM32(a + 0x408));
}
/* Downed model: the game's die method tears down too much to undo (frozen heroes, attacks that never connect after
 * a revive, observed), so a hero who goes down while the other is alive does NOT die: he is "down" (his pad reads
 * are zeroed, enemies on him go to the other), then respawns beside the other with full health. Only when both
 * are down does player 1 get the game's own death (die method → death cam → code 5). No death animation yet. */
static int coop_real_death_pending;
static void coop_p1_real_death(void)   /* the game's own player death: die method with [0x43B15C] = player 1. The die method starts the
                                         * death cam (slow motion, the Eye, then EndMission 5) only when its hero is [0x43B15C]: player 2's
                                         * fatal hit runs in his context ([0x43B15C] = him), so from there it is only queued, and runs at
                                         * the next frame step in player 1's (no death cam happened before, observed) */
{
    uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx;
    coop_gameover = 1; coop_gameover_t = coop_now();
    if (coop_depth || MEM32(0x43B15Cu) != coop_first_char) { coop_real_death_pending = 1; tt_dbg("[COOPDEATH] game over: player 1's death queued (in player 2's context now)\n"); return; }
    coop_real_death_pending = 0;
    MEM32(coop_first_char + 0x2FC) = 0;
    tt_dbg("[COOPDEATH] game over: player 1's death (die method, death cam)\n");
    PUSH32(esp, 0x000838E0u); ecx = coop_first_char; sub_000838E0_gen();
    esp = sp; eax = r0; ecx = r1; edx = r2;
}
/* called from the die method's hook for player 1 (the level script's @ObjectDie): 1 = handled here */
int coop_p1_die_hook(void)
{
    if (coop_mode() != 3 || coop_p2 < 0x10000u || getenv("TT_COOP_NODEATH") || coop_gameover) return 0;
    if (tt_vs_on()) { if (!coop_down[0]) { coop_down[0] = 1; coop_down_t[0] = coop_now(); } if (coop_first_char >= 0x10000u) MEM32(coop_first_char + 0x2FC) = 1; return 1; }   /* (Versus: never the game's death; the round ends) */
    if (coop_down[1]) { coop_gameover = 1; coop_gameover_t = coop_now(); tt_dbg("[COOPDEATH] P1 down while P2 is down: game over (the game's own death)\n"); return 0; }
    if (!coop_down[0]) { coop_down[0] = 1; coop_down_t[0] = coop_now(); tt_dbg("[COOPDEATH] P1 down (P2 alive): no death; respawn in %u ms\n", (unsigned)coop_respawn_wait()); }
    return 1;
}
void coop_p2_down_now(void)   /* player 2 goes down (health 0 seen in coop_death_frame, or a hit that would take him to 0: the health guard in tt_coop_p1_post) */
{
    uint32_t wait = coop_respawn_wait();
    if (coop_down[1] || coop_gameover || coop_p2 < 0x10000u) return;
    coop_down[1] = 1; coop_down_t[1] = coop_now(); MEM32(coop_p2 + 0x2FC) = getenv("TT_COOP_NOHPGUARD") ? 0 : 1;
    if (tt_vs_on()) return;   /* (Versus: the round ends, vs_frame) */
    if (coop_down[0]) { tt_dbg("[COOPDEATH] P2 down while P1 is down: game over (player 1's own death)\n"); coop_p1_real_death(); return; }
    tt_dbg("[COOPDEATH] P2 down: respawn in %u ms\n", wait);
}
void coop_p1_down_now(void)   /* player 1 goes down while player 2 is up (the health guard: a hit that would take him to 0) */
{
    uint32_t wait = coop_respawn_wait();
    if (coop_down[0] || coop_down[1] || coop_gameover || coop_first_char < 0x10000u) return;
    MEM32(coop_first_char + 0x2FC) = 1; coop_down[0] = 1; coop_down_t[0] = coop_now();
    tt_dbg("[COOPDEATH] P1 down (P2 alive): held at 1 health, respawn in %u ms\n", wait);
}
/* players' health by the difficulty: each hero's max health (+0x300) scaled, and his health with it; again whenever
 * the game sets the max anew (0x8C850: a level's start, an upgrade) */
static int32_t coop_hpmax_set[2];
static void coop_diff_health(void)
{
    int w; float f = coop_hp_scale();
    if (coop_mode() != 3) return;
    for (w = 0; w < 2; w++) {
        uint32_t a = w ? coop_p2 : coop_first_char; int32_t m, h, nm;
        if (a < 0x10000u) continue;
        m = (int32_t)MEM32(a + 0x300); h = (int32_t)MEM32(a + 0x2FC);
        if (m <= 0 || m == coop_hpmax_set[w]) continue;
        if (w && !coop_hpmax_set[1] && m == coop_hpmax_set[0]) { coop_hpmax_set[1] = m; continue; }   /* the twin built from player 1 copies his max, already scaled (observed: 750 scaled again to 563) */
        nm = (int32_t)(m * f + 0.5f); if (nm < 1) nm = 1;
        coop_hpmax_set[w] = nm;
        if (nm == m) continue;
        MEM32(a + 0x300) = (uint32_t)nm;
        if (h > 1) MEM32(a + 0x2FC) = (uint32_t)((int64_t)h * nm / m);
        tt_dbg("[COOPLEVEL] P%d max health %d -> %d (x%.2f), health %d -> %d\n", w + 1, m, nm, f, h, (int32_t)MEM32(a + 0x2FC));
    }
}
/* ---- (Versus) rounds: best of three ----
 * At the first frame of play both heroes' spots are kept, both get the same health (VS_HP) and every move (sub_0009CAB0).
 * A hero at 0 health is down (held at 1, his pad zeroed: the co-op's downed model, never the game's death): the other
 * wins the round. 4 s later both stand again at their spots with full health; the first to two rounds wins the match,
 * shown for 6 s, then a new match starts. The banner (round, score, winner) is the respawn counter's element at the top
 * (tt_vs_banner, coop's HUD). TT_VS_HP: the health (default 1000) */
#define VS_HP 1500   /* (1.9.7: from 1000 -- falls came too fast) */
static int vs_score[2], vs_round, vs_state, vs_have, vs_last_win = -1;   /* state: 0 fighting, 1 a round won (wait), 2 the match won (wait), 3 the countdown (frozen) */
/* the match's numbers per player, for the results (tt_vs_stats) */
typedef struct { int hits, dmg, taken, shots, shots_hit, rounds, downs, best_hit, parried; uint32_t fastest_ms; } VsStats;
VsStats tt_vs_st[2]; static uint32_t vs_round_t0; static int vs_was_down[2];
int tt_vs_frozen(void) { return tt_vs_on() && (vs_state == 3 || !vs_have); }   /* (and before the first round: nobody moves until the countdown ends) */
static int vs_skip_movies(void) { extern volatile int tt_vs_menu_on; int r = tt_vs_menu_on && !getenv("TT_VS_MOVIES"); { static int n; if (n++ < 20) tt_dbg("[VS] a movie opened: %s\n", r ? "skipped (Versus)" : "played"); } return r; }
void tt_vs_install(void) { extern int (*volatile xbox_movie_skip_cb)(void); if (!xbox_movie_skip_cb) xbox_movie_skip_cb = vs_skip_movies; }
int tt_vs_res_win, tt_vs_res_score[2], tt_vs_res_award[2][2] = { { -1, 0 }, { -1, 0 } };
#define VS_RESULTS_MS 11000u
int tt_vs_results_shown(void) { return tt_vs_on() && vs_state == 2 && !getenv("TT_VS_NORESULTS"); }
static void vs_pick_awards(int win)   /* two of the awards the match earned (a clear leader in it), picked at random */
{
    int c[6][2], nc = 0, i; uint32_t seed = coop_now() * 2654435761u;
    const VsStats *a = &tt_vs_st[0], *b = &tt_vs_st[1];
    #define VS_AW(id, cond0, cond1) do { if (cond0) { c[nc][0] = id; c[nc][1] = 0; nc++; } else if (cond1) { c[nc][0] = id; c[nc][1] = 1; nc++; } } while (0)
    {   int acc0 = a->shots >= 2 ? a->shots_hit * 1000 / a->shots : -1, acc1 = b->shots >= 2 ? b->shots_hit * 1000 / b->shots : -1;
        VS_AW(0, acc0 > acc1 && a->shots_hit > 0, acc1 > acc0 && b->shots_hit > 0); }
    VS_AW(1, a->best_hit > b->best_hit, b->best_hit > a->best_hit);
    VS_AW(2, a->taken < b->taken, b->taken < a->taken);
    VS_AW(3, a->fastest_ms && (!b->fastest_ms || a->fastest_ms < b->fastest_ms), b->fastest_ms && (!a->fastest_ms || b->fastest_ms < a->fastest_ms));
    VS_AW(4, a->hits > b->hits, b->hits > a->hits);
    VS_AW(5, a->dmg > b->dmg, b->dmg > a->dmg);
    #undef VS_AW
    tt_vs_res_award[0][0] = tt_vs_res_award[1][0] = -1;
    for (i = 0; i < 2 && nc > 0; i++) { int k = (int)((seed >> (8 + 5 * i)) % (uint32_t)nc); tt_vs_res_award[i][0] = c[k][0]; tt_vs_res_award[i][1] = c[k][1]; c[k][0] = c[nc - 1][0]; c[k][1] = c[nc - 1][1]; nc--; }
    tt_vs_res_win = win; tt_vs_res_score[0] = vs_score[0]; tt_vs_res_score[1] = vs_score[1];
    tt_dbg("[VS] results: P1 hits %d dmg %d shots %d/%d | P2 hits %d dmg %d shots %d/%d | awards %d (P%d) %d (P%d)\n", a->hits, a->dmg, a->shots_hit, a->shots, b->hits, b->dmg, b->shots_hit, b->shots,
           tt_vs_res_award[0][0], tt_vs_res_award[0][1] + 1, tt_vs_res_award[1][0], tt_vs_res_award[1][1] + 1);
}
/* the hero a player is, as a skin's name (retail heroes too) */
static const char *vs_skin_name(int w)
{
    extern int tt_heroes_guest(void); extern const char *tt_heroes_guest_name(int g); extern const char *tt_coop_p2_char_now; static const char *const rh[5] = { "aragorn", "aragorn", "legolas", "gimli", "isildur" };
    if (w) return tt_coop_p2_char_now && *tt_coop_p2_char_now ? tt_coop_p2_char_now : "aragorn";
    if (tt_heroes_guest() >= 0) return tt_heroes_guest_name(tt_heroes_guest());
    { uint8_t hb = MEM8(0x4AB69Du); return hb <= 4 ? rh[hb] : "aragorn"; }
}
void tt_vs_note_hit(int victim, int dmg, int ranged)
{
    VsStats *a = &tt_vs_st[victim ^ 1], *v = &tt_vs_st[victim];
    if (victim < 0 || victim > 1 || dmg <= 0) return;
    a->hits++; a->dmg += dmg; v->taken += dmg; if (dmg > a->best_hit) a->best_hit = dmg; if (ranged) a->shots_hit++;
    {   /* (his voice: a blow taken, now and then -- not every blow, not more than one in 0.7 s) */
        extern int tt_vs_voice(const char *who, int slot, float vol); static uint32_t last[2]; uint32_t now = coop_now();
        if (now - last[victim] > 700u && (now / 7u) % 3u) { last[victim] = now; tt_vs_voice(vs_skin_name(victim), 4 + (int)((now / 13u) % 3u), 0.85f); }
    }
}
static int vs_taken0[2];
static uint32_t vs_t, vs_banner_until; static float vs_spot[2][3]; static unsigned vs_level_n = ~0u;
static char vs_text[48];
const char *tt_vs_banner(void) { return tt_vs_on() && vs_text[0] && coop_now() < vs_banner_until ? vs_text : NULL; }
static void vs_say(const char *t, uint32_t ms) { snprintf(vs_text, sizeof vs_text, "%s", t); vs_banner_until = coop_now() + ms; tt_dbg("[VS] %s\n", t); }
/* (Versus) a hero's arrow or throw at the other: the game's own projectiles pass through a hero of the shooter's side,
 * so the shot is judged here. At the launch the target's spot is noted; when the shot would get there (the distance at
 * 40 units a second) it hits if he is still within 2.2 units of that spot (a step aside dodges it) and not down.
 * TT_VS_ARROWDMG the damage (45). TT_VS_NOVSHOTS turns it off */
static struct { uint32_t t, foe; int who, dmg, pid; float x, y; } vs_shot[16];
static uint32_t vs_bolt_t[2];   /* the last bolt (15) at each player: the game's own hit of it is not counted (vs_absorb) */
/* a shot's damage by its projectile (the ids as heroes.c lists them): Gimli's axe 16 and the bolts 15 hit harder, a stone 10 less */
/* (ten shots take at most half of the 1000: arrows 40, an axe or a bolt 50, a stone 30) */
static int vs_shot_dmg(uint32_t pid) { int d = getenv("TT_VS_ARROWDMG") ? atoi(getenv("TT_VS_ARROWDMG")) : 40; return pid == 16u || pid == 15u ? d * 5 / 4 : pid == 10u ? d * 3 / 4 : d; }
/* a hit through the game's own path on hero v that is a bolt's (Gandalf's and Sauron's land as the game's hits as well
 * as ours): not counted */
int tt_vs_absorb(int v)
{
    int i;
    if (!tt_vs_on() || v < 0 || v > 1) return 0;
    for (i = 0; i < 16; i++) if (vs_shot[i].t && vs_shot[i].who == (v ^ 1) && vs_shot[i].pid == 15) return 1;
    return vs_bolt_t[v] && coop_now() - vs_bolt_t[v] < 1500u;
}
static void coop_hero_state(uint32_t a, uint32_t st);
void tt_vs_shot_launched(uint32_t o, uint32_t pid)   /* o: the shooter (the launch's [esp+0x18]), pid the projectile's id ([esp+0x10]) */
{
    uint32_t f; float dx, dy, d; int i;
    if (!tt_vs_on() || o < 0x10000u || getenv("TT_VS_NOVSHOTS")) return;
    { static int n; if (n++ < 30 && getenv("TT_VS_AIMLOG")) tt_dbg("[VS] launch by %08X (P1 %08X P2 %08X) state %d\n", o, coop_first_char, coop_p2, vs_state); }
    if (o == coop_first_char || o == coop_p2) { extern uint32_t tt_vs_shot_t[2]; tt_vs_shot_t[o == coop_p2] = coop_now() | 1u; }
    if (vs_state != 0) return;
    if (o != coop_first_char && o != coop_p2) return;
    f = o == coop_first_char ? coop_p2 : coop_first_char; if (f < 0x10000u) return;
    tt_vs_st[o == coop_p2].shots++;
    dx = MEMF(f + 0xF0) - MEMF(o + 0xF0); dy = MEMF(f + 0xF4) - MEMF(o + 0xF4); d = sqrtf(dx * dx + dy * dy);
    if (d > 40.0f) return;
    {   /* in front of him (within 50 degrees of where he faces: his lock-on turns him to the other) */
        uint32_t c = MEM32(o + 0x494); if (c >= 0x10000u && MEM32(c + 0x98) != f) { static int n; if (n++ < 20) tt_dbg("[VS] P%d shoots without his lock on the other (%08X)\n", (o == coop_p2) + 1, MEM32(c + 0x98)); return; } }
    for (i = 0; i < 16 && vs_shot[i].t; i++) ;
    if (i == 16) return;
    vs_shot[i].dmg = vs_shot_dmg(pid); vs_shot[i].pid = (int)pid; if (pid == 15u) vs_bolt_t[o == coop_first_char] = coop_now() | 1u;
    vs_shot[i].t = coop_now() + (uint32_t)(d * 25.0f) + 1u; vs_shot[i].foe = f; vs_shot[i].who = o == coop_p2; vs_shot[i].x = MEMF(f + 0xF0); vs_shot[i].y = MEMF(f + 0xF4);
    { static int n; if (n++ < 20) tt_dbg("[VS] P%d shoots at P%d, %.1f away\n", vs_shot[i].who + 1, 2 - vs_shot[i].who, d); }
}
int tt_vs_no_blows(void); int tt_vs_dmg_scale_pct(int victim); int tt_vs_shield_take(int victim, int dmg);
/* a swing judged here as well: on some arenas (the prologue's Mount Doom, Weathertop) the game's contact test never
 * gives the other hero player 1's blows (his boxes hit, no hit came: observed). A swing (its first attack box, 0xBA020;
 * the box's damage) with the other within 3.4 units counts 0.18 s later -- unless the game's own hit on him came
 * meanwhile (tt_vs_real_hit). TT_VS_NOSWINGJUDGE off */
static struct { uint32_t t, t0; int dmg; } vs_sw[2]; static uint32_t vs_realhit_t[2]; static int vs_real_ok[2]; static unsigned vs_real_lvl = ~0u;
void tt_vs_real_hit(int victim)   /* the game's own hit on victim: his attacker's swings count by themselves on this arena */
{
    if (victim < 0 || victim > 1) return;
    if (vs_real_lvl != coop_level_n) { vs_real_lvl = coop_level_n; vs_real_ok[0] = vs_real_ok[1] = 0; }
    vs_realhit_t[victim] = coop_now() | 1u;
    { extern int tt_vs_last_box_kick(int who); if (tt_vs_last_box_kick(victim ^ 1)) return; }   /* (a knock back's contact says nothing of the blows) */
    if (!vs_real_ok[victim ^ 1]) { vs_real_ok[victim ^ 1] = 1; if (vs_sw[victim ^ 1].dmg >= 0) vs_sw[victim ^ 1].t = 0; tt_dbg("[VS] P%d's blows land by themselves here: no judging\n", (victim ^ 1) + 1); }
}
/* (1.9.7) the box's kind: the knock back (B: no damage, kind 3) and the killing move (A: kind 2, flag 0) -- the game's
 * contact gives the other hero the knock back's push only as a small flinch, and the killing move's low box missed a
 * hero on his feet: judged here every time (not only on the arenas where blows don't land by themselves), and the knock
 * back throws him back as it does an enemy (vs_push) */
static int vs_box_k4[2], vs_box_k2[2];
static uint32_t vs_kb_in_t[2];   /* the game's own knock back reached him (INP_KNOCKBACK) */
void tt_vs_kb_in(int v) { if (v >= 0 && v < 2) vs_kb_in_t[v] = coop_now() | 1u; }
int tt_vs_last_box_kick(int who) { return who >= 0 && who < 2 && vs_box_k4[who] == 3; }
void tt_vs_box_kind(int who, uint32_t k2, uint32_t k4) { if (who >= 0 && who < 2) { vs_box_k2[who] = (int)k2; vs_box_k4[who] = (int)k4; } }
static struct { uint32_t t0; float dx, dy, done; } vs_push[2];
static void vs_push_start(int v, uint32_t from)
{
    uint32_t h = v ? coop_p2 : coop_first_char; float dx, dy, d;
    if (h < 0x10000u || from < 0x10000u || getenv("TT_VS_NOKNOCKPUSH")) return;
    dx = MEMF(h + 0xF0) - MEMF(from + 0xF0); dy = MEMF(h + 0xF4) - MEMF(from + 0xF4); d = sqrtf(dx * dx + dy * dy);
    if (d < 0.01f) { dx = MEMF(from + 0x70); dy = MEMF(from + 0x74); d = sqrtf(dx * dx + dy * dy); if (d < 0.01f) return; }
    vs_push[v].t0 = coop_now() | 1u; vs_push[v].dx = dx / d; vs_push[v].dy = dy / d; vs_push[v].done = 0;
    { static int n; if (n++ < 20) tt_dbg("[VS] P%d knocked back\n", v + 1); }
}
static void vs_push_frame(void)
{
    int v; uint32_t now = coop_now(); float dist = getenv("TT_VS_KNOCKDIST") ? (float)atof(getenv("TT_VS_KNOCKDIST")) : 1.2f;
    for (v = 0; v < 2; v++) if (vs_push[v].t0) {
        uint32_t h = v ? coop_p2 : coop_first_char; float f = (float)(now - vs_push[v].t0) / 380.0f, want, step;
        if (f > 1.0f) f = 1.0f;
        want = dist * (1.0f - (1.0f - f) * (1.0f - f));   /* (eased out) */
        step = want - vs_push[v].done; vs_push[v].done = want;
        if (h >= 0x10000u && step > 0) { MEMF(h + 0xF0) += vs_push[v].dx * step; MEMF(h + 0xF4) += vs_push[v].dy * step; MEMF(h + 0x90) = MEMF(h + 0xF0); MEMF(h + 0x94) = MEMF(h + 0xF4); }
        if (f >= 1.0f) vs_push[v].t0 = 0;
    }
}
void tt_vs_swing(int who, int dmg)
{
    uint32_t a = who ? coop_p2 : coop_first_char, o = who ? coop_first_char : coop_p2, now = coop_now(); float dx, dy;
    if (vs_real_lvl != coop_level_n) { vs_real_lvl = coop_level_n; vs_real_ok[0] = vs_real_ok[1] = 0; }
    {   /* (his effort as he swings, now and then: one swing in two at most, not within 1.2 s) */
        extern int tt_vs_voice(const char *who, int slot, float vol); static uint32_t last[2];
        if (who >= 0 && who < 2 && vs_state == 0 && !coop_down[who] && now - last[who] > 1200u && (now / 11u) & 1u) { last[who] = now; tt_vs_voice(vs_skin_name(who), (int)((now / 17u) % 4u), 0.7f); }
    }
    if (vs_real_ok[who] && vs_box_k4[who & 1] != 3 && !(vs_box_k4[who & 1] == 2 && vs_box_k2[who & 1] == 0)) return;
    if (getenv("TT_VS_NOSWINGJUDGE") || a < 0x10000u || o < 0x10000u || vs_state != 0 || coop_down[who] || coop_down[who ^ 1]) return;
    if (vs_sw[who].t0 && now - vs_sw[who].t0 < 450u) return;   /* (one swing makes several boxes) */
    vs_sw[who].t0 = now;
    dx = MEMF(o + 0xF0) - MEMF(a + 0xF0); dy = MEMF(o + 0xF4) - MEMF(a + 0xF4);
    if (getenv("TT_VS_SWINGLOG")) tt_dbg("[VS] P%d swings (%d) at %.1f from the other, state %u\n", who + 1, dmg, sqrtf(dx * dx + dy * dy), MEM32(a + 0x408));
    if (vs_box_k4[who & 1] == 3) {   /* the knock back: no damage; he is thrown back (the game's own push, if it came, is kept) */
        if (dx * dx + dy * dy <= 4.4f * 4.4f) { vs_sw[who].t = now + 200u; vs_sw[who].dmg = -1; }
        return;
    }
    if (dx * dx + dy * dy > 3.4f * 3.4f || dmg <= 0 || dmg > 1000) return;
    vs_sw[who].t = now + 450u; vs_sw[who].dmg = dmg;
}
/* (1.9.7) a blow judged here (or an arrow of ours) lands with the game's own sound of a hit at the victim: the last
 * flesh hit the game played this session (0x000C3B90 from 0x85340, kept by its hook), else TT_VS_HITSND (default 0x35) */
static void vs_impact(uint32_t victim)
{
    extern uint32_t tt_snd3d_hit_id, tt_snd3d_hit_vol, tt_snd3d_hit_pitch; extern uint32_t tt_gcall(uint32_t va, uint32_t thisptr, int n, const uint32_t *args);
    uint32_t a[10], sp = esp, r0 = eax, r1 = ecx, r2 = edx; float one = 1.0f, m1 = -1.0f, vol = 1.0f;
    if (victim < 0x10000u || getenv("TT_VS_NOHITSND")) return;
    { extern int tt_vs_hero_sound(const char *skin, uint32_t id, float vol); if (tt_vs_hero_sound("blow", getenv("TT_VS_HITSND") ? (uint32_t)strtoul(getenv("TT_VS_HITSND"), NULL, 0) : 0xE1u, 1.0f)) return; }   /* (1.9.7: through the port's mixer, at full strength) */
    (void)tt_snd3d_hit_id; (void)tt_snd3d_hit_vol; (void)tt_snd3d_hit_pitch;
    a[0] = getenv("TT_VS_HITSND") ? (uint32_t)strtoul(getenv("TT_VS_HITSND"), NULL, 0) : 0xE1u;   /* (the game's blow landing: 0x82EB0's 0xE1) */
    { float v75 = 0.75f, p0 = 0.0f; (void)vol; (void)one; memcpy(&a[1], &v75, 4); memcpy(&a[2], &p0, 4); }
    a[3] = MEM32(victim + 0xF0); a[4] = MEM32(victim + 0xF4); a[5] = MEM32(victim + 0xF8); memcpy(&a[6], &one, 4);
    memcpy(&a[7], &m1, 4); a[8] = 0; a[9] = 0;
    tt_gcall(0x000C3B90u, 0, 10, a);
    esp = sp; eax = r0; ecx = r1; edx = r2;
}
static void vs_swings_due(uint32_t now)
{
    int w;
    for (w = 0; w < 2; w++) if (vs_sw[w].t && (int32_t)(now - vs_sw[w].t) >= 0) {
        int v = w ^ 1, dmg = vs_sw[w].dmg; uint32_t f = v ? coop_p2 : coop_first_char;
        vs_sw[w].t = 0;
        if (dmg < 0) {   /* the knock back */
            if (vs_state == 0 && !coop_down[v]) { extern void tt_ach_vs_event(int kind, int who, int a, int b, const char *skin); tt_ach_vs_event(2, w, 0, 0, NULL); }
            if (vs_state != 0 || coop_down[v]) { if (getenv("TT_VS_SWINGLOG")) tt_dbg("[VS] knock back on P%d not now (state %d, down %d)\n", v + 1, vs_state, coop_down[v]); continue; }
            if (!(vs_kb_in_t[v] && now - vs_kb_in_t[v] < 600u)) {   /* the game's didn't reach him: INP_KNOCKBACK, and a short shove (the game's own throws him down where he stands) */
                coop_hero_state(f, 3u); vs_push_start(v, v ? coop_first_char : coop_p2); }
            else if (getenv("TT_VS_SWINGLOG")) tt_dbg("[VS] P%d knocked back by the game's own contact\n", v + 1);
            { extern int tt_vs_voice(const char *who, int slot, float vol); tt_vs_voice(vs_skin_name(v), 4 + (int)(now % 3u), 0.9f); }
            continue;
        }
        if (vs_realhit_t[v] && now - vs_realhit_t[v] < 500u) continue;   /* (the game's own hit came: it counted) */
        if (vs_state != 0 || coop_down[v] || tt_vs_no_blows()) continue;
        dmg = tt_vs_shield_take(v, dmg * tt_vs_dmg_scale_pct(v) / 100);
        {   int32_t h = (int32_t)MEM32(f + 0x2FC) - dmg;
            tt_vs_note_hit(v, dmg, 0);
            vs_impact(f);
            if (h <= 0) { MEM32(f + 0x2FC) = 1; coop_down[v] = 1; coop_down_t[v] = now; }
            else { MEM32(f + 0x2FC) = (uint32_t)h; coop_hero_state(f, 5); }
            { static int n; if (n++ < 20) tt_dbg("[VS] P%d's swing judged a hit on P%d: health %d\n", w + 1, v + 1, h); } }
    }
}
static void vs_shots_due(uint32_t now)
{
    int i;
    for (i = 0; i < 16; i++) if (vs_shot[i].t && (int32_t)(now - vs_shot[i].t) >= 0) {
        int dmg = vs_shot[i].dmg;
        uint32_t f = vs_shot[i].foe; int v = vs_shot[i].who ^ 1; float dx = MEMF(f + 0xF0) - vs_shot[i].x, dy = MEMF(f + 0xF4) - vs_shot[i].y;
        vs_shot[i].t = 0;
        if (vs_state != 0 || coop_down[v] || dx * dx + dy * dy > 2.2f * 2.2f) { static int n; if (n++ < 20) tt_dbg("[VS] P%d's shot missed (moved %.1f)\n", 2 - v, sqrtf(dx * dx + dy * dy)); continue; }
        if (tt_vs_no_blows()) continue;
        dmg = tt_vs_shield_take(v, dmg * tt_vs_dmg_scale_pct(v) / 100);
        {   int32_t h = (int32_t)MEM32(f + 0x2FC) - dmg;
            tt_vs_note_hit(v, dmg, 1);
            vs_impact(f);
            { extern void tt_ach_vs_event(int kind, int who, int a, int b, const char *skin); tt_ach_vs_event(3, v ^ 1, 0, 0, NULL); }
            if (h <= 0) { MEM32(f + 0x2FC) = 1; coop_down[v] = 1; coop_down_t[v] = now; }
            else { MEM32(f + 0x2FC) = (uint32_t)h; coop_hero_state(f, 5); }   /* (INP_TAKEDAMAGE: the hit reaction) */
            { static int n; if (n++ < 20) tt_dbg("[VS] P%d's shot hits P%d: health %d\n", 2 - v, v + 1, h); } }
    }
}
/* (Versus) the level's pickup objects (type 14), as the loader makes them */
static uint32_t vs_pk[64], vs_pk_rec[64]; static int vs_npk; static unsigned vs_pk_level = ~0u;
void tt_vs_note_pickup(uint32_t obj, uint32_t rec)
{
    if (vs_pk_level != coop_level_n) { vs_pk_level = coop_level_n; vs_npk = 0; }
    if (vs_npk < 64) { vs_pk[vs_npk] = obj; vs_pk_rec[vs_npk] = rec; vs_npk++; }
}
static void vs_pk_dump(const char *when)
{
    int i; if (!getenv("TT_VS_PICKLOG")) return;
    for (i = 0; i < vs_npk; i++) { uint32_t o = vs_pk[i]; char b[700]; int q = 0, k;
        for (k = 0xF0; k < 0x100; k += 4) q += snprintf(b + q, sizeof b - q, " %X:%.2f", k, MEMF(o + k));
        for (k = 0x3D0; k < 0x420 && q < 640; k += 4) q += snprintf(b + q, sizeof b - q, " %X:%08X", k, MEM32(o + k));
        tt_dbg("[VSPICK] %s: %d %08X vt %08X \"%.20s\" +110 %08X +114 %08X%s\n", when, i, o, MEM32(o), (const char *)XBOX_PTR(vs_pk_rec[i] + 8), MEM32(o + 0x110), MEM32(o + 0x114), b); }
}
/* a dynamic pickup (the level's pool, "dynamicPickupObj..."; the enemies' drops) put out: of kind k at x y z
 * (0x8F410, thiscall pickup; position, rotation, kind; ret 12 -- the kind picks its model, effect and refill), then
 * shown (0x8F640: only from flags +0x3F8 = 0). One already out is taken back first (0x8F700). Returns it or 0 */
static uint32_t vs_pk_mem, vs_pk_out[24]; static int vs_npk_out, vs_pk_kind[24], vs_pk_main[24];   /* (vs_pk_main: -1 a pickup, else the one a glow copy stands with) */
static uint32_t vs_pk_spawn(float x, float y, float z, int kind)
{
    extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment);
    int i; uint32_t o = 0, sp = esp, r0 = eax, r1 = ecx, r2 = edx, a[3];
    if (!vs_pk_mem && !(vs_pk_mem = xbox_ContiguousAlloc(64u, 16u))) return 0;
    if (vs_pk_level != coop_level_n) { vs_npk = 0; vs_pk_level = coop_level_n; }   /* (another level's: gone; Mount Doom has none of its own, and the last arena's were taken for its, a crash) */
    for (i = 0; i < vs_npk && !o; i++) { int k, used = 0; const char *nm;
        if (MEM32(vs_pk[i]) != 0x1748B0u) continue;
        nm = (const char *)XBOX_PTR(vs_pk_rec[i] + 8);
        if (strncmp(nm, "dynamicPickupObj", 16)) continue;
        for (k = 0; k < vs_npk_out; k++) if (vs_pk_out[k] == vs_pk[i]) used = 1;
        if (!used && !(MEM32(vs_pk[i] + 0x3F8) & 2u)) o = vs_pk[i]; }
    if (!o) return 0;
    if (MEM32(o + 0x3F8)) { ecx = o; PUSH32(esp, 0x0008F700u); sub_0008F700(); MEM32(o + 0x3F8) = 0; }
    MEMF(vs_pk_mem) = x; MEMF(vs_pk_mem + 4) = y; MEMF(vs_pk_mem + 8) = z; MEMF(vs_pk_mem + 12) = 1.0f;
    MEMF(vs_pk_mem + 16) = 0; MEMF(vs_pk_mem + 20) = 0; MEMF(vs_pk_mem + 24) = 0; MEMF(vs_pk_mem + 28) = 1.0f;
    a[0] = vs_pk_mem; a[1] = vs_pk_mem + 16; a[2] = (uint32_t)kind;
    PUSH32(esp, a[2]); PUSH32(esp, a[1]); PUSH32(esp, a[0]); PUSH32(esp, 0x0008F410u); ecx = o; sub_0008F410();   /* (ret 12) */
    esp = sp;
    MEMF(o + 0xF0) = x; MEMF(o + 0xF4) = y; MEMF(o + 0xF8) = z;
    MEM32(o + 0x3F8) = 0; ecx = o; PUSH32(esp, 0x0008F640u); sub_0008F640();
    esp = sp; eax = r0; ecx = r1; edx = r2;
    MEMF(o + 0x430) = 1.0e7f; MEMF(o + 0x434) = 1.0e7f;   /* (a drop's life, 0x8F410: 4 s, then it blinks and at 6 s its item is gone -- only the glow was left: it stays) */
    if (vs_npk_out < 24) { vs_pk_kind[vs_npk_out] = kind; vs_pk_main[vs_npk_out] = -1; vs_pk_out[vs_npk_out++] = o; }
    if (getenv("TT_VS_PICKLOG")) tt_dbg("[VSPICK] spawned %08X kind %d: 3D0 %08X 3D4 %08X 3D8 %08X 3DC %08X 3E0 %08X 3E4 %08X 3E8 %08X 3EC %08X 3F0 %08X 3F8 %08X\n", o, kind,
        MEM32(o + 0x3D0), MEM32(o + 0x3D4), MEM32(o + 0x3D8), MEM32(o + 0x3DC), MEM32(o + 0x3E0), MEM32(o + 0x3E4), MEM32(o + 0x3E8), MEM32(o + 0x3EC), MEM32(o + 0x3F0), MEM32(o + 0x3F8));
    if (getenv("TT_VS_PICKLOG")) { char b[600]; int q = 0, k; for (k = 0; k < 14; k++) q += snprintf(b + q, sizeof b - q, " [%d] %08X %08X %08X %08X", k, MEM32(0x43ADCCu + 4 * k), MEM32(0x43AE04u + 4 * k), MEM32(0x43AD94u + 4 * k), MEM32(0x43AE3Cu + 4 * k)); tt_dbg("[VSPICK] tables:%s\n", b); }
    { static int n; if (n++ < 30) tt_dbg("[VS] pickup %08X of kind %d out at %.1f %.1f %.1f (flags %08X, +110 %08X)\n", o, kind, x, y, z, MEM32(o + 0x3F8), MEM32(o + 0x110)); }
    return o;
}
/* taking them: the dynamic pickups' own touch never comes for the heroes here, so a hero within 1.4 units of one that
 * shows (+0x3F8 bit 1) takes it: kinds 0-3 health (a third of his most), 4-6 ammo (5, up to 10); first come, first
 * served. It goes (0x8F700) with the game's pickup sound */
static void vs_pk_touch(void)
{
    int i, w;
    for (i = 0; i < vs_npk_out; i++) {
        uint32_t o = vs_pk_out[i];
        if (vs_pk_main[i] >= 0) continue;   /* (a glow copy: taken with its pickup) */
        if (!(MEM32(o + 0x3F8) & 2u)) continue;
        if (getenv("TT_VS_PKAGE0")) MEM32(o + 0x400) = 0;   /* (test: its age kept new -- the item never grew in: only the glow showed) */
        for (w = 0; w < 2; w++) {
            uint32_t a = w ? coop_p2 : coop_first_char; float dx, dy, dz; int k = vs_pk_kind[i];
            if (a < 0x10000u || coop_down[w]) continue;
            dx = MEMF(a + 0xF0) - MEMF(o + 0xF0); dy = MEMF(a + 0xF4) - MEMF(o + 0xF4); dz = MEMF(a + 0xF8) - MEMF(o + 0xF8);
            if (dx * dx + dy * dy > 1.4f * 1.4f || dz > 3.0f || dz < -3.0f) continue;
            if (k <= 3) { int32_t mx = (int32_t)MEM32(a + 0x300), h = (int32_t)MEM32(a + 0x2FC); if (h >= mx) continue; h += mx / 3; MEM32(a + 0x2FC) = (uint32_t)(h > mx ? mx : h); }
            else { int am = (int)(MEM32(a + 0x304) & 0xFFFF), cap = getenv("TT_VS_AMMO") ? atoi(getenv("TT_VS_AMMO")) : 10; if (am >= cap) continue; am += 5; if (am > cap) am = cap; MEM32(a + 0x304) = (MEM32(a + 0x304) & 0xFFFF0000u) | (uint32_t)am; }
            {   uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx, sa[6]; float one = 1.0f, zero = 0.0f; int c;
                ecx = o; PUSH32(esp, 0x0008F700u); sub_0008F700(); esp = sp;
                for (c = 0; c < vs_npk_out; c++) if (vs_pk_main[c] == i && (MEM32(vs_pk_out[c] + 0x3F8) & 2u)) { ecx = vs_pk_out[c]; PUSH32(esp, 0x0008F700u); sub_0008F700(); esp = sp; }
                sa[0] = k <= 3 ? 0x9C5u : 0x9D1u; memcpy(&sa[1], &one, 4); memcpy(&sa[2], &zero, 4); memcpy(&sa[3], &zero, 4); memcpy(&sa[4], &one, 4); sa[5] = 0;
                tt_gcall(0x000C3640u, 0, 6, sa);
                esp = sp; eax = r0; ecx = r1; edx = r2; }
            { extern void tt_ach_vs_event(int kind, int who, int a, int b, const char *skin); tt_ach_vs_event(4, w, 0, 0, NULL); }
            tt_dbg("[VS] P%d takes the %s pickup: health %d, ammo %u\n", w + 1, k <= 3 ? "health" : "ammo", (int32_t)MEM32(a + 0x2FC), MEM32(a + 0x304) & 0xFFFF);
            break;
        }
    }
}
/* (1.9.7) a pickup with glow copies on it (TT_VS_PKGLOW, default 4 in all): the drops are only a glow -- the game's own
 * pickup table has no item model for any of them, on any level -- and one glow alone was faint */
static void vs_pk_spawn_bright(float x, float y, float z, int kind)
{
    int n0 = vs_npk_out, main, k, g = getenv("TT_VS_PKGLOW") ? atoi(getenv("TT_VS_PKGLOW")) : 4;
    if (!vs_pk_spawn(x, y, z, kind)) return;
    main = n0;
    for (k = 1; k < g && vs_npk_out < 24; k++) { int at = vs_npk_out; if (!vs_pk_spawn(x, y, z + 0.02f * (float)k, kind)) break; if (vs_npk_out > at) vs_pk_main[at] = main; }
}
/* each round's pickups: ammo on either side of the middle, one each player's way (not on Weathertop or the prologue:
 * no bows there) and health in the middle. TT_VS_PKAMMO / TT_VS_PKHEALTH their kinds; TT_VS_PICKTEST: kinds 0-11 in a
 * row by player 1 */
static void vs_round_pickups(void)
{
    extern int tt_vs_no_ranged(void); int k; float mx, my, mz, dx, dy, d, px, py;
    int ka = getenv("TT_VS_PKAMMO") ? atoi(getenv("TT_VS_PKAMMO")) : 4, kh = getenv("TT_VS_PKHEALTH") ? atoi(getenv("TT_VS_PKHEALTH")) : 0;
    if (getenv("TT_VS_NOPICKUPS")) return;
    for (k = 0; k < vs_npk_out; k++) if (MEM32(vs_pk_out[k]) == 0x1748B0u) { uint32_t o = vs_pk_out[k], sp = esp, r0 = eax, r1 = ecx, r2 = edx; if (MEM32(o + 0x3F8)) { ecx = o; PUSH32(esp, 0x0008F700u); sub_0008F700(); } MEM32(o + 0x3F8) = 0; esp = sp; eax = r0; ecx = r1; edx = r2; }
    vs_npk_out = 0;
    if (getenv("TT_VS_PICKTEST")) { int k0 = atoi(getenv("TT_VS_PICKTEST")); for (k = 0; k < 6; k++) { float f = 0.2f + 0.12f * (float)k;
            vs_pk_spawn(vs_spot[0][0] + (vs_spot[1][0] - vs_spot[0][0]) * f, vs_spot[0][1] + (vs_spot[1][1] - vs_spot[0][1]) * f, vs_spot[0][2] + 0.3f, k0 + k); }
        MEM32(coop_first_char + 0x2FC) = 300; MEM32(coop_first_char + 0x304) = (MEM32(coop_first_char + 0x304) & 0xFFFF0000u) | 3u; return; }
    mx = (vs_spot[0][0] + vs_spot[1][0]) * 0.5f; my = (vs_spot[0][1] + vs_spot[1][1]) * 0.5f; mz = (vs_spot[0][2] + vs_spot[1][2]) * 0.5f;
    dx = vs_spot[1][0] - vs_spot[0][0]; dy = vs_spot[1][1] - vs_spot[0][1]; d = sqrtf(dx * dx + dy * dy); if (d < 0.1f) { dx = 1; dy = 0; d = 1; }
    px = -dy / d; py = dx / d;
    vs_pk_spawn_bright(mx, my, mz + 0.5f, kh);
    if (!tt_vs_no_ranged()) {
        vs_pk_spawn_bright(mx + px * 6.0f - dx / d * 2.0f, my + py * 6.0f - dy / d * 2.0f, mz + 0.5f, ka);
        vs_pk_spawn_bright(mx - px * 6.0f + dx / d * 2.0f, my - py * 6.0f + dy / d * 2.0f, mz + 0.5f, ka);
    }
}
static void vs_place(uint32_t a, const float *p)
{
    uint32_t a3[3], sp = esp, r0 = eax, r1 = ecx, r2 = edx;
    memcpy(&a3[0], &p[0], 4); memcpy(&a3[1], &p[1], 4); memcpy(&a3[2], &p[2], 4);
    tt_gcall(0x000865D0u, a, 3, a3);   /* (the character's SetPosition) */
    esp = sp; eax = r0; ecx = r1; edx = r2;
}
static void vs_full(uint32_t a)
{
    int hp = getenv("TT_VS_HP") ? atoi(getenv("TT_VS_HP")) : VS_HP;
    MEM32(a + 0x300) = (uint32_t)hp; MEM32(a + 0x2FC) = (uint32_t)hp;
}
/* ---- (Versus) Find the Shield: the bigger arenas. The shield lies somewhere on the map (a pickup of the level's own,
 * put on a walkable cell of the pathmap 14-30 units from the heroes); until someone takes it no blow counts. Its
 * carrier deals a third more; beating the carrier is a point (the shield goes to a new place), the carrier beating the
 * other is two (he keeps it). After a fall, a short countdown where they stand; first to TT_VS_GOAL (20) wins. */
extern int tt_vs_mode(void);
static int vs_carrier = -1; static uint32_t vs_shield; static float vs_sh_x, vs_sh_y, vs_sh_z;
static int vs_shield_mode(void) { return tt_vs_on() && tt_vs_mode() == 1; }
static int vs_goal(void) { extern int tt_vs_goal_pick(void); return getenv("TT_VS_GOAL") ? atoi(getenv("TT_VS_GOAL")) : tt_vs_goal_pick(); }   /* (1.9.7: the Versus menu's Rounds to win -- first to 3, 5 (the default: a match of up to 9) or 7) */
static uint32_t vs_pk_spawn(float x, float y, float z, int kind);
static float vs_arena_r(void);
static void vs_shield_place(void)
{
    uint32_t pm = coop_pathmap(coop_first_char), seed = coop_now() * 2246822519u + 7u; int tries;
    float mx = 0.5f * (MEMF(coop_first_char + 0xF0) + MEMF(coop_p2 + 0xF0)), my = 0.5f * (MEMF(coop_first_char + 0xF4) + MEMF(coop_p2 + 0xF4)), mz = MEMF(coop_first_char + 0xF8);
    float bx = mx + 10.0f, by = my;
    if (pm) {
        float ox = MEMF(pm + 0x48), oy = MEMF(pm + 0x4C), sz = (float)(int32_t)MEM32(pm + 0x68);
        for (tries = 0; tries < 400; tries++) {
            float ang, r, x, y; int c, rr, v;
            seed = seed * 1103515245u + 12345u; ang = (float)((seed >> 8) & 1023) * (6.2831853f / 1024.0f);
            seed = seed * 1103515245u + 12345u; r = 14.0f + (float)((seed >> 8) % 1600u) / 100.0f;
            x = mx + r * cosf(ang); y = my + r * sinf(ang);
            if (vs_arena_r() > 0) { float cx = 0.5f * (vs_spot[0][0] + vs_spot[1][0]), cy = 0.5f * (vs_spot[0][1] + vs_spot[1][1]);
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > (vs_arena_r() - 6.0f) * (vs_arena_r() - 6.0f)) continue; }   /* (inside the arena's edge) */
            c = (int)floorf((x - ox) / sz); rr = (int)floorf((y - oy) / sz); v = coop_pm_cell(pm, c, rr);
            if (v >= 0 && COOP_PM_WALK(v)) { bx = x; by = y; break; }
        }
    }
    vs_sh_x = bx; vs_sh_y = by; vs_sh_z = mz + 0.5f;
    vs_npk_out = 0; vs_shield = vs_pk_spawn(vs_sh_x, vs_sh_y, vs_sh_z, getenv("TT_VS_SHIELDKIND") ? atoi(getenv("TT_VS_SHIELDKIND")) : 7);
    vs_carrier = -1;
    tt_dbg("[VS] the shield is at %.1f %.1f (%.1f from the heroes)\n", bx, by, sqrtf((bx - mx) * (bx - mx) + (by - my) * (by - my)));
}
int tt_vs_no_blows(void) { return vs_shield_mode() && vs_carrier < 0; }   /* (no blow counts while nobody has the shield) */
int tt_vs_dmg_scale_pct(int victim) { return vs_shield_mode() && vs_carrier == (victim ^ 1) ? 133 : 100; }
static void vs_shield_frame(uint32_t now)
{
    int w;
    if (vs_carrier >= 0 || !vs_shield) return;
    if (getenv("TT_VS_SHIELDTEST") && vs_state == 0 && now - vs_round_t0 > 3000u) {   /* (testing: player 1 to the shield) */
        MEMF(coop_first_char + 0xF0) = vs_sh_x; MEMF(coop_first_char + 0xF4) = vs_sh_y; }
    if (coop_now() >= vs_banner_until) {   /* where it is, from the camera's view: ahead, behind, left, right, and how far */
        uint32_t cm = MEM32(0x43B164u); float mx = 0.5f * (MEMF(coop_first_char + 0xF0) + MEMF(coop_p2 + 0xF0)), my = 0.5f * (MEMF(coop_first_char + 0xF4) + MEMF(coop_p2 + 0xF4));
        float dx = vs_sh_x - mx, dy = vs_sh_y - my, d = sqrtf(dx * dx + dy * dy), fx = 0, fy = 1, fn, fwd, side; const char *where = "";
        if (cm >= 0x10000u) { fx = MEMF(cm + 0x40) - MEMF(cm + 0x50); fy = MEMF(cm + 0x44) - MEMF(cm + 0x54); }
        fn = sqrtf(fx * fx + fy * fy); if (fn > 1e-3f && d > 1e-3f) { fx /= fn; fy /= fn;
            fwd = (dx * fx + dy * fy) / d; side = (dx * fy - dy * fx) / d;
            where = fwd > 0.7f ? "AHEAD" : fwd < -0.7f ? "BEHIND" : side > 0 ? "TO THE RIGHT" : "TO THE LEFT"; }
        { char t[48]; snprintf(t, sizeof t, "FIND THE SHIELD: %d %s", (int)(d + 0.5f), where); snprintf(vs_text, sizeof vs_text, "%s", t); vs_banner_until = coop_now() + 300u; }
    }
    for (w = 0; w < 2; w++) {
        uint32_t a = w ? coop_p2 : coop_first_char; float dx = MEMF(a + 0xF0) - vs_sh_x, dy = MEMF(a + 0xF4) - vs_sh_y;
        if (coop_down[w] || dx * dx + dy * dy > 1.8f * 1.8f) continue;
        vs_carrier = w;
        if (MEM32(vs_shield + 0x3F8)) { uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx; ecx = vs_shield; PUSH32(esp, 0x0008F700u); sub_0008F700(); esp = sp; eax = r0; ecx = r1; edx = r2; }
        { char t[48]; snprintf(t, sizeof t, "PLAYER %d HAS THE SHIELD!", w + 1); vs_say(t, 2500u); }
        { extern void tt_vs_play_wav(const char *name, float vol); tt_vs_play_wav("fight.wav", 0.7f); }
        (void)now;
        break;
    }
}
static void vs_orc_shield_round(void);
/* (1.9.7) how far apart the heroes start: TT_VS_SPREAD (12 units) */
static float vs_spacing(void) { return getenv("TT_VS_SPREAD") ? (float)atof(getenv("TT_VS_SPREAD")) : 30.0f; }
/* the bigger arenas (Gates of Moria, Amon Hen, Fangorn, the Plains of Rohan, the Road to Helm's Deep, the Deeping
 * Wall): after the first round the heroes go on from where they are */
static int vs_big_arena(void) { uint32_t lv = MEM32(0x1BB2A4u); return !getenv("TT_VS_RESETSPOTS") && (lv == 2u || lv == 4u || lv == 5u || lv == 6u || lv == 8u || lv == 9u); }
/* a spot d units from (ox, oy), the way (dx, dy) if the ground there and on the way is walkable (the level's pathmap),
 * else the nearest way round that is; none walkable (or no pathmap): the way asked */
static void vs_free_spot(uint32_t hero, float ox, float oy, float dx, float dy, float d, float *x, float *y)
{
    uint32_t pm = coop_pathmap(hero); float n = sqrtf(dx * dx + dy * dy), d0 = d; int k, tries;
    if (n < 1e-3f) { dx = 1.0f; dy = 0.0f; } else { dx /= n; dy /= n; }
    if (!pm && d > 12.0f) d = 12.0f;   /* (no pathmap: nothing to tell a wall or a drop by -- not far) */
    *x = ox + dx * d; *y = oy + dy * d;
    if (!pm) return;
    for (tries = 0; tries < 5; tries++, d = d0 * (1.0f - 0.15f * (float)tries)) {   /* (the whole way first, then a little nearer) */
    {   float px = MEMF(pm + 0x48), py = MEMF(pm + 0x4C), sz = (float)(int32_t)MEM32(pm + 0x68);
        int c0 = (int)floorf((ox - px) / sz), r0 = (int)floorf((oy - py) / sz);
        if (sz <= 0) return;
        for (k = 0; k < 24; k++) {   /* 0, +15, -15, +30, ... degrees */
            float a = (float)((k + 1) / 2) * 0.2617994f * (k & 1 ? 1.0f : -1.0f), ca = cosf(a), sa = sinf(a);
            float ex = dx * ca - dy * sa, ey = dx * sa + dy * ca, tx = ox + ex * d, ty = oy + ey * d;
            int c1 = (int)floorf((tx - px) / sz), r1 = (int)floorf((ty - py) / sz);
            if (coop_pm_line(pm, c0, r0, c1, r1)) { *x = tx; *y = ty; return; }
        }
    }
    }
    *x = ox + dx * 12.0f; *y = oy + dy * 12.0f;   /* (nowhere clear: as before) */
}
/* (1.9.7) a hero off the edge of the map (the campaign's fall volumes and collision planes are the story's scripts,
 * left out in Versus: he fell through the sky forever, his tag over a grey nothing -- Gates of Moria, Fangorn, Amon
 * Hen): put back where he last stood on walkable ground once he is 10 units below it */
static float vs_safe[2][3]; static int vs_safe_ok[2];
static void vs_edge_guard(void)
{
    int w;
    for (w = 0; w < 2; w++) {
        uint32_t h = w ? coop_p2 : coop_first_char, pm; float x, y, z; int walk = 0;
        if (h < 0x10000u) continue;
        x = MEMF(h + 0xF0); y = MEMF(h + 0xF4); z = MEMF(h + 0xF8);
        { static unsigned lv = ~0u; if (lv != coop_level_n) { lv = coop_level_n; vs_safe_ok[0] = vs_safe_ok[1] = 0; } }
        if (!vs_have || !vs_safe_ok[w]) { vs_safe[w][0] = x; vs_safe[w][1] = y; vs_safe[w][2] = z; vs_safe_ok[w] = 1; continue; }
        pm = coop_pathmap(h);
        if (pm) { float px = MEMF(pm + 0x48), py = MEMF(pm + 0x4C), sz = (float)(int32_t)MEM32(pm + 0x68);
            if (sz > 0) { int v = coop_pm_cell(pm, (int)floorf((x - px) / sz), (int)floorf((y - py) / sz)); walk = v >= 0 && COOP_PM_WALK(v); } }
        {   /* not falling: over the last 0.4 s down less than 4 units a second, and not in a fall or a throw (states 44-46,
             * 49, 52) -- where he stands is safe */
            static float wz[2][8]; static uint32_t wt[2][8]; static int wi[2]; uint32_t now = coop_now(), st = MEM32(h + 0x408); int q, ok = 1; float zmax = z;
            if (now - wt[w][(wi[w] + 7) & 7] >= 50u || now < wt[w][(wi[w] + 7) & 7]) { wz[w][wi[w]] = z; wt[w][wi[w]] = now; wi[w] = (wi[w] + 1) & 7; }
            for (q = 0; q < 8; q++) { if (now - wt[w][q] > 450u) { ok = 0; break; } if (wz[w][q] > zmax) zmax = wz[w][q]; }
            if (ok && zmax - z < 2.5f && !(st >= 44 && st <= 46) && st != 49 && st != 52) {   /* (not by the pathmap: its cells miss walkable ground -- a run down Amon Hen's slope was taken for a fall) */ vs_safe[w][0] = x; vs_safe[w][1] = y; vs_safe[w][2] = z; continue; }
            if (zmax - z < 2.5f && z > vs_safe[w][2] - 40.0f) continue;   /* (landed lower, not falling still: his new ground becomes safe once he stands) */
        }
        if (0) ;
        else if (z < vs_safe[w][2] - (getenv("TT_VS_FALLDROP") ? (float)atof(getenv("TT_VS_FALLDROP")) : 25.0f) && !getenv("TT_VS_NOEDGEGUARD")) {
            float p[3]; p[0] = vs_safe[w][0]; p[1] = vs_safe[w][1]; p[2] = vs_safe[w][2] + 0.5f;
            vs_place(h, p); MEMF(h + 0x90) = p[0]; MEMF(h + 0x94) = p[1]; MEMF(h + 0x98) = p[2];
            tt_dbg("[VS] P%d fell off the map at %.1f %.1f %.1f: back at %.1f %.1f %.1f\n", w + 1, x, y, z, p[0], p[1], p[2]);
        }
    }
}
static void vs_round_start(void)
{
    char t[48]; int w, keep = vs_round > 0 && (vs_shield_mode() || vs_big_arena());
    for (w = 0; w < 2; w++) {
        uint32_t a = w ? coop_p2 : coop_first_char;
        if (a < 0x10000u) continue;
        if (coop_down[w]) coop_respawn(w);
        vs_full(a);
        if (!keep) vs_place(a, vs_spot[w]);   /* (the bigger arenas: where they are, after the first) */
        {   extern int tt_vs_no_ranged(void); int am = tt_vs_no_ranged() ? 0 : getenv("TT_VS_AMMO") ? atoi(getenv("TT_VS_AMMO")) : 10;   /* ammo: 10 a round (none on Weathertop and the prologue) */
            MEM32(a + 0x304) = (MEM32(a + 0x304) & 0xFFFF0000u) | (uint32_t)(am & 0xFFFF); }
        tt_dbg("[VS] P%d at the start: ammo +304 %u +308 %u +30C %u\n", w + 1, MEM32(a + 0x304) & 0xFFFF, MEM32(a + 0x308), MEM32(a + 0x30C));
    }
    if (keep && coop_first_char >= 0x10000u && coop_p2 >= 0x10000u) {   /* going on where they are: the one who fell (or player 2) set
                                                                           * apart from the other if they stand too close */
        int l = vs_last_win == 0 ? 1 : vs_last_win == 1 ? 0 : 1; uint32_t L = l ? coop_p2 : coop_first_char, W = l ? coop_first_char : coop_p2;
        float dx = MEMF(L + 0xF0) - MEMF(W + 0xF0), dy = MEMF(L + 0xF4) - MEMF(W + 0xF4), sp = vs_spacing(), p[3];
        if (dx * dx + dy * dy < sp * sp) {
            vs_free_spot(W, MEMF(W + 0xF0), MEMF(W + 0xF4), dx, dy, sp, &p[0], &p[1]); p[2] = MEMF(W + 0xF8);
            vs_place(L, p); tt_dbg("[VS] P%d set %.0f units from P%d to go on\n", l + 1, sp, 2 - l);
        }
    }
    if (!vs_shield_mode()) vs_round_pickups();
    else if (vs_round == 0 || vs_carrier < 0) vs_shield_place();
    vs_orc_shield_round();   /* (the shield orc's shield whole again) */
    vs_taken0[0] = tt_vs_st[0].taken; vs_taken0[1] = tt_vs_st[1].taken;   /* (the round's blows taken, for the achievements) */
    vs_round++; vs_state = 3; vs_t = coop_now();   /* the countdown first: both frozen (tt_vs_frozen) */
    if (vs_shield_mode()) snprintf(t, sizeof t, vs_carrier < 0 ? "FIND THE SHIELD   P1 %d - %d P2" : "P1 %d - %d P2", vs_score[0], vs_score[1]);
    else snprintf(t, sizeof t, "ROUND %d   P1 %d - %d P2", vs_round, vs_score[0], vs_score[1]);
    vs_say(t, 1000u);
}
/* (Versus) the arena's music: the donor level's track (versus.c; its bank in place of the arena's, lang.c), started as
 * the front end starts its own (0x000C31C0: name, 1 = play, 1, 0 -- the stream player [0x43BCF8]) once a level, at
 * the first FIGHT. The arena's scripts, which would start it, are not run in Versus */
static void vs_music_start(void)
{
    extern const char *tt_vs_music_track_for_stream(const char *stream); extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment);
    static uint32_t lvl_done = ~0u, g; uint32_t li = MEM32(0x1BB2A4u), sp, r0, r1, r2; const char *t; int i;
    if (lvl_done == li || getenv("TT_VS_NOMUSIC")) return;
    lvl_done = li;
    t = tt_vs_music_track_for_stream("0ALT");   /* (any of the arena's music names maps to the track) */
    if (!t || MEM32(0x43BCF8u) < 0x10000u) { tt_dbg("[VS] music: none for level %u\n", li); return; }
    if (!g && !(g = xbox_ContiguousAlloc(64u, 16u))) return;
    for (i = 0; i < 63 && t[i]; i++) MEM8(g + i) = (uint8_t)t[i]; MEM8(g + i) = 0;
    sp = esp; r0 = eax; r1 = ecx; r2 = edx;
    PUSH32(esp, 0); PUSH32(esp, 1); PUSH32(esp, 1); PUSH32(esp, g);
    PUSH32(esp, 0x000C31C0u); sub_000C31C0();
    esp = sp; eax = r0; ecx = r1; edx = r2;
    tt_dbg("[VS] music: \"%s\" started (level %u)\n", t, li);
}
/* the countdown: the round's banner 1 s, then 3, 2, 1 a second each (the Options' tick), then FIGHT (versus/fight.wav) */
static void vs_countdown(uint32_t now)
{
    extern void tt_opt_move_sound(void); extern void tt_vs_play_wav(const char *name, float vol);
    static int shown = -1; static uint32_t t_of = ~0u; uint32_t e = now - vs_t; int k = (int)(e / 1000u);
    if (t_of != vs_t) { t_of = vs_t; shown = 0; }
    if (k > shown && k >= 1 && k <= 3) { char t[8]; shown = k; snprintf(t, sizeof t, "%d", 4 - k); vs_say(t, 1000u);
        { extern int tt_vs_play_ring(void); if (!tt_vs_play_ring()) { uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx; tt_opt_move_sound(); esp = sp; eax = r0; ecx = r1; edx = r2; } } }
    if (k >= 4) { vs_state = 0; vs_round_t0 = now; vs_say("FIGHT!", 1200u); tt_vs_play_wav("fight.wav", 1.0f); vs_music_start(); }
}
static void coop_down_pose(uint32_t now)
{
    int w;
    for (w = 0; w < 2; w++) {   /* the downed pose: on the ground until the respawn. The state machine's inputs are the INP_ names
                                  * (in the executable, enum order = reverse file order): 1 INP_DIE, 2 INP_KNOCKDOWN, 3 INP_KNOCKBACK,
                                  * 5 INP_TAKEDAMAGE, 28 INP_RESSURECT. INP_DIE plays the death (44/45); left alone it moves on to 50
                                  * (weapons dropped, observed), so the state's time (+0x420, seconds) is held below that
                                  * (TT_COOP_DOWNHOLD, 3.0) while he is down; the respawn raises INP_RESSURECT. Raised again if he is
                                  * out of the death states. No hits reach him meanwhile (the flags below). TT_COOP_NODOWNPOSE off;
                                  * TT_COOP_DOWNEV1 the input (1). */
        static int stage[2], tries[2]; static uint32_t ts[2], lst[2];
        uint32_t h = w ? coop_p2 : coop_first_char, st;
        if (!coop_down[w] || h < 0x10000u) { stage[w] = 0; tries[w] = 0; continue; }
        st = MEM32(h + 0x408);
        if (!getenv("TT_COOP_NOHITFLAGS")) { MEM32(h + 0x49C) |= 0x40u; MEM32(h + 0x110) &= ~0x20u; }   /* no hits reach him while down (the flags the game's own death sets; the respawn restores them): a hit stood him up (observed) */
        if (st != lst[w]) { if (getenv("TT_COOP_DOWNLOG")) tt_dbg("[COOPDOWN] P%d down %u ms: state %u -> %u (stage %d, time %.2f)\n", w + 1, now - coop_down_t[w], lst[w], st, stage[w], MEMF(h + 0x420)); lst[w] = st; }
        if (getenv("TT_COOP_NODOWNPOSE")) continue;
        if (st == 50) { stage[w] = 3; continue; }   /* dead, lying (the full death's end) */
        if (st == 44 || st == 45 || st == 46) {   /* dying (the death's first states; 44/45 are also the hit reactions) */
            if (getenv("TT_COOP_DOWNHOLD")) { float hold = (float)atof(getenv("TT_COOP_DOWNHOLD")); if (MEMF(h + 0x420) > hold) MEMF(h + 0x420) = hold; }
            stage[w] = 2; continue; }
        if (stage[w] >= 2 && now - ts[w] < 8000u) continue;   /* the death runs through other states for a few seconds (P2: 1, 2, then 50 at 4.5 s, observed) */
        if (stage[w] == 0 || (now - ts[w] > 1500u && tries[w] < 30)) {
            stage[w] = 1; ts[w] = now; tries[w]++; coop_hero_state(h, getenv("TT_COOP_DOWNEV1") ? (uint32_t)atoi(getenv("TT_COOP_DOWNEV1")) : tt_vs_on() && !getenv("TT_VS_REALDEATH") ? 2u : 1u); }   /* (Versus: INP_KNOCKDOWN: he falls and lies until the next round, then stands by himself; the death (INP_DIE) could not be undone in time for it, observed) */
    }
}
/* (Versus) the camera, where the level's own does not follow the heroes (its tracks and paths are the story's: on the
 * Plains of Rohan it stayed on the opening scene's spot, the Deeping Wall's sat still): once its look-at (camera +0x40)
 * is more than 8 units from the heroes' midpoint, ours for the rest of the level -- the look-at on the midpoint, the eye
 * (+0x50) 11 units back and 6 up along the camera's own heading (TT_VS_CAMDIST / TT_VS_CAMUP), more as the heroes part.
 * TT_VS_NOCAM: never; TT_VS_CAM: always */
static int vs_cam_ours; static unsigned vs_cam_level = ~0u; static float vs_cam_dx = 0.0f, vs_cam_dy = -1.0f;
static void vs_camera(void)
{
    uint32_t cm = MEM32(0x43B164u), a = coop_first_char, b = coop_p2; float mx, my, mz, lx, ly, ex, ey, hx, hy, hn, d, sep, back, up;
    if (cm < 0x10000u || a < 0x10000u || b < 0x10000u || getenv("TT_VS_NOCAM")) return;
    if (vs_cam_level != coop_level_n) { uint32_t lv = MEM32(0x1BB2A4u); vs_cam_level = coop_level_n;
        vs_cam_ours = getenv("TT_VS_CAM") != NULL || lv == 6u || lv == 8u || lv == 9u || lv == 4u || lv == 5u;   /* (the Plains of Rohan, the Road to Helm's Deep, the Deeping Wall, Amon Hen, Fangorn: ours from the start) */
        vs_cam_dx = 0.0f; vs_cam_dy = -1.0f;
        if (vs_cam_ours) { float hx = MEMF(cm + 0x40) - MEMF(cm + 0x50), hy = MEMF(cm + 0x44) - MEMF(cm + 0x54), hn = sqrtf(hx * hx + hy * hy); if (hn > 0.01f) { vs_cam_dx = hx / hn; vs_cam_dy = hy / hn; } } }
    mx = 0.5f * (MEMF(a + 0xF0) + MEMF(b + 0xF0)); my = 0.5f * (MEMF(a + 0xF4) + MEMF(b + 0xF4)); mz = 0.5f * (MEMF(a + 0xF8) + MEMF(b + 0xF8));
    if (coop_down[0] && !coop_down[1]) { mx = MEMF(b + 0xF0); my = MEMF(b + 0xF4); mz = MEMF(b + 0xF8); }
    if (coop_down[1] && !coop_down[0]) { mx = MEMF(a + 0xF0); my = MEMF(a + 0xF4); mz = MEMF(a + 0xF8); }
    lx = MEMF(cm + 0x40); ly = MEMF(cm + 0x44); ex = MEMF(cm + 0x50); ey = MEMF(cm + 0x54);
    d = sqrtf((lx - mx) * (lx - mx) + (ly - my) * (ly - my));
    if (!vs_cam_ours && vs_have && sqrtf((ex - mx) * (ex - mx) + (ey - my) * (ey - my)) > 26.0f) d = 99.0f;   /* (the eye far off: as stuck) */
    if (!vs_cam_ours && d > 8.0f && vs_have) { vs_cam_ours = 1; tt_dbg("[VS] the level's camera is %.1f from the heroes: ours from now\n", d);
        hx = lx - ex; hy = ly - ey; hn = sqrtf(hx * hx + hy * hy); if (hn > 0.01f) { vs_cam_dx = hx / hn; vs_cam_dy = hy / hn; } }
    if (MEM32(0x1BB2A4u) == 12u && !getenv("TT_VS_ORTHLEVELCAM")) {   /* (1.9.7) Orthanc: the story's own circling camera
         * (the level's, without its scripts, had no height and swung about the heroes): ours, looking in at the tower's
         * middle (0, 0) from outside the heroes, so left and right go round the floor as in the story */
        float r = sqrtf(mx * mx + my * my); vs_cam_ours = 1;
        if (r > 2.0f) { vs_cam_dx = -mx / r; vs_cam_dy = -my / r; }
    }
    if (!vs_cam_ours) return;
    sep = sqrtf((MEMF(a + 0xF0) - MEMF(b + 0xF0)) * (MEMF(a + 0xF0) - MEMF(b + 0xF0)) + (MEMF(a + 0xF4) - MEMF(b + 0xF4)) * (MEMF(a + 0xF4) - MEMF(b + 0xF4)));
    back = (getenv("TT_VS_CAMDIST") ? (float)atof(getenv("TT_VS_CAMDIST")) : MEM32(0x1BB2A4u) == 12u ? 20.0f : 17.0f) + 0.5f * (sep > 6.0f ? sep - 6.0f : 0.0f);
    up = (getenv("TT_VS_CAMUP") ? (float)atof(getenv("TT_VS_CAMUP")) : MEM32(0x1BB2A4u) == 12u ? 8.5f : 9.5f) + 0.25f * (sep > 6.0f ? sep - 6.0f : 0.0f);
    MEMF(cm + 0x40) = mx; MEMF(cm + 0x44) = my; MEMF(cm + 0x48) = mz + 1.5f;
    {   /* (1.9.7) the eye kept inside the level: past its edge (the pathmap's grid ends) stands the brown wall the level
         * is walled in by, and an eye behind it saw only that wall -- the map "cut off", the heroes in it unseen (Amon Hen,
         * near the start's stairs). Brought in along the line to the heroes until over the level's ground */
        uint32_t pm = getenv("TT_VS_CAMFREE") ? 0 : coop_pathmap(a); int k;
        if (pm) { float ox = MEMF(pm + 0x48), oy = MEMF(pm + 0x4C), sz = (float)(int32_t)MEM32(pm + 0x68);
            for (k = 0; k < 12 && sz > 0 && back > 3.0f; k++) {
                float ex2 = mx - vs_cam_dx * back, ey2 = my - vs_cam_dy * back;
                if (coop_pm_cell(pm, (int)floorf((ex2 - ox) / sz), (int)floorf((ey2 - oy) / sz)) >= 0) break;
                back *= 0.82f; up *= 0.92f;
            } }
    }
    MEMF(cm + 0x50) = mx - vs_cam_dx * back; MEMF(cm + 0x54) = my - vs_cam_dy * back; MEMF(cm + 0x58) = mz + 1.5f + up;
    if (!getenv("TT_VS_KEEPCAMANGLE")) { float th = atan2f(vs_cam_dy, vs_cam_dx) - 1.5707963f + (getenv("TT_VS_CAMANGOFS") ? (float)atof(getenv("TT_VS_CAMANGOFS")) : 0.0f); if (getenv("TT_VS_CAMANGNEG")) th = -th; MEMF(cm + 0x60) = th; MEMF(cm + 0x84) = th; }   /* (1.9.7) the level camera's turn (+0x60, last +0x84; about 0 elsewhere): on the Road to Helm's Deep it swung all about without the level's camera records, and the stick goes by it -- every hero ran in circles there */
}
/* (Versus) our camera again right after the game's camera update (0x68DC0), so the view it makes is ours: it was
 * written later in the frame, and the level's culling still went by the level's own camera -- on Amon Hen the ground
 * past a point was not drawn (the level's camera elsewhere) */
int tt_vs_camera_after(void) { if (!tt_vs_on() || !vs_cam_ours || !vs_have) return 0; vs_camera(); return 1; }
static float vs_arena_r(void) { if (getenv("TT_VS_ARENAR")) return (float)atof(getenv("TT_VS_ARENAR")); return MEM32(0x1BB2A4u) == 4u ? 50.0f : 0.0f; }   /* (Amon Hen: its edge, the rest of the map not yet playable in Versus) */
/* (Versus) the heroes stay within TT_VS_LEASH (24) units of each other, so one camera keeps both: a step that takes
 * them further apart is undone for whoever made it (both, if both did) */
static void vs_leash(void)
{
    static float px[2], py[2]; static unsigned lvl = ~0u; float R = getenv("TT_VS_LEASH") ? (float)atof(getenv("TT_VS_LEASH")) : 36.0f, s1, s0;   /* (1.9.7: 36, the heroes start 30 apart) */
    uint32_t h[2] = { coop_first_char, coop_p2 }; int w;
    if (h[0] < 0x10000u || h[1] < 0x10000u || getenv("TT_VS_NOLEASH")) return;
    if (lvl != coop_level_n || vs_state == 3) { lvl = coop_level_n; for (w = 0; w < 2; w++) { px[w] = MEMF(h[w] + 0xF0); py[w] = MEMF(h[w] + 0xF4); } return; }
    s1 = sqrtf((MEMF(h[0] + 0xF0) - MEMF(h[1] + 0xF0)) * (MEMF(h[0] + 0xF0) - MEMF(h[1] + 0xF0)) + (MEMF(h[0] + 0xF4) - MEMF(h[1] + 0xF4)) * (MEMF(h[0] + 0xF4) - MEMF(h[1] + 0xF4)));
    s0 = sqrtf((px[0] - px[1]) * (px[0] - px[1]) + (py[0] - py[1]) * (py[0] - py[1]));
    if (s1 > R && s1 > s0) for (w = 0; w < 2; w++) {
        float ox = px[w ^ 1], oy = py[w ^ 1], dnew = sqrtf((MEMF(h[w] + 0xF0) - ox) * (MEMF(h[w] + 0xF0) - ox) + (MEMF(h[w] + 0xF4) - oy) * (MEMF(h[w] + 0xF4) - oy)), dold = sqrtf((px[w] - ox) * (px[w] - ox) + (py[w] - oy) * (py[w] - oy));
        if (dnew > dold + 0.001f) { MEMF(h[w] + 0xF0) = px[w]; MEMF(h[w] + 0xF4) = py[w]; MEMF(h[w] + 0x90) = px[w]; MEMF(h[w] + 0x94) = py[w]; }
    }
    {   /* (1.9.7) the map's edge: a hero stepping off the level's walkable ground (its pathmap: no cell, or one that is not
         * walkable, after a walkable one) is stopped there -- the campaign keeps its heroes in with gates and scripts; past
         * them the ground stops at a brown wall the heroes walked into and were lost in (Amon Hen). TT_VS_NOMAPWALL */
        static int was_walk[2];
        if (vs_have) for (w = 0; w < 2; w++) {
            uint32_t pm = coop_pathmap(h[w]); int v = -1, walk;
            if (!pm) continue;
            { float ox = MEMF(pm + 0x48), oy = MEMF(pm + 0x4C), sz = (float)(int32_t)MEM32(pm + 0x68);
              if (sz > 0) v = coop_pm_cell(pm, (int)floorf((MEMF(h[w] + 0xF0) - ox) / sz), (int)floorf((MEMF(h[w] + 0xF4) - oy) / sz)); }
            walk = v >= 0 && COOP_PM_WALK(v);
            if (getenv("TT_VS_WALLLOG")) { static uint32_t tl[2]; static int lv[2] = { -9, -9 }; if (v != lv[w] && coop_now() - tl[w] > 100u) { tl[w] = coop_now(); lv[w] = v; tt_dbg("[VSWALL] P%d at %.1f %.1f cell %d\n", w + 1, MEMF(h[w] + 0xF0), MEMF(h[w] + 0xF4), v); } }
            if (!walk && was_walk[w] && v < 0 && !getenv("TT_VS_NOMAPWALL")) { MEMF(h[w] + 0xF0) = px[w]; MEMF(h[w] + 0xF4) = py[w]; MEMF(h[w] + 0x90) = px[w]; MEMF(h[w] + 0x94) = py[w]; }
            else was_walk[w] = walk || v >= 0;
        }
    }
    {   /* the arena's edge: on Amon Hen no further than 50 units from where the match began (past it the level is
         * unfinished -- the campaign's gates keep you out: a brown wall where the ground stops). TT_VS_ARENAR */
        float A = vs_arena_r();
        if (A > 0 && vs_have) for (w = 0; w < 2; w++) {
            float cx = 0.5f * (vs_spot[0][0] + vs_spot[1][0]), cy = 0.5f * (vs_spot[0][1] + vs_spot[1][1]);
            float dx = MEMF(h[w] + 0xF0) - cx, dy = MEMF(h[w] + 0xF4) - cy, d = sqrtf(dx * dx + dy * dy), d0 = sqrtf((px[w] - cx) * (px[w] - cx) + (py[w] - cy) * (py[w] - cy));
            if (d > A && d > d0) { MEMF(h[w] + 0xF0) = px[w]; MEMF(h[w] + 0xF4) = py[w]; MEMF(h[w] + 0x90) = px[w]; MEMF(h[w] + 0x94) = py[w]; }
        }
    }
    for (w = 0; w < 2; w++) { px[w] = MEMF(h[w] + 0xF0); py[w] = MEMF(h[w] + 0xF4); }
}
/* the hero a player is (his skin's name: "nazgul", ...; "" for a retail hero) */
static const char *vs_skin_of(int w)
{
    extern int tt_heroes_guest(void); extern const char *tt_heroes_guest_name(int g); extern const char *tt_coop_p2_char_now;
    if (w) return tt_coop_p2_char_now ? tt_coop_p2_char_now : "";
    return tt_heroes_guest_name(tt_heroes_guest());
}
/* (Versus) the shield orc's shield: it takes the blows -- each on him does half while it holds, and wears it (a heavy
 * one, 100 or more, twice); the sixth breaks it (gone from his arm, heroes.c tt_heroes_shield_show) for the rest of the
 * round. TT_VS_SHIELDHITS: the blows it takes (6) */
static int vs_shp[2]; static uint32_t vs_sh_broke_t;
static void vs_orc_shield_round(void)
{
    extern int tt_heroes_shield_show(int on, int level); int w, orc = 0;
    for (w = 0; w < 2; w++) { vs_shp[w] = !strcmp(vs_skin_of(w), "orc") ? (getenv("TT_VS_SHIELDHITS") ? atoi(getenv("TT_VS_SHIELDHITS")) : 6) : 0; if (vs_shp[w]) orc = 1; }
    if (orc) tt_heroes_shield_show(1, coop_level_n);
}
int tt_vs_shield_take(int v, int dmg)
{
    extern int tt_heroes_shield_show(int on, int level); int full = getenv("TT_VS_SHIELDHITS") ? atoi(getenv("TT_VS_SHIELDHITS")) : 6;
    if (v < 0 || v > 1 || dmg <= 0 || vs_shp[v] <= 0 || !tt_vs_on()) return dmg;
    if (getenv("TT_VS_SHSHOT")) { extern void xbox_PresentDumpRequest(const char *path); static char pth[300]; static int k; snprintf(pth, sizeof pth, "%s/sh_%02d.bmp", getenv("TT_VS_SHSHOT"), k++); xbox_PresentDumpRequest(pth); }   /* (testing) */
    vs_shp[v] -= dmg >= 100 ? 2 : 1;
    if (vs_shp[v] > 0) { if (vs_shp[v] <= full / 2 && vs_shp[v] + (dmg >= 100 ? 2 : 1) > full / 2) { char t[40]; snprintf(t, sizeof t, "P%d'S SHIELD CRACKS!", v + 1); vs_say(t, 1200u); } return dmg / 2; }
    vs_shp[v] = 0;
    { char t[40]; snprintf(t, sizeof t, "P%d'S SHIELD IS BROKEN!", v + 1); vs_say(t, 2000u); }
    tt_heroes_shield_show(0, coop_level_n);
    vs_sh_broke_t = coop_now();
    tt_dbg("[VS] P%d, the shield orc: his shield broken\n", v + 1);
    { extern void tt_ach_unlock(const char *id); tt_ach_unlock("vs_shieldbreak"); }
    return dmg / 2;
}
/* (Versus) the Nazgul's fall: he flees, as the Ringwraiths from Aragorn's fire on Weathertop -- his run (his knockdown
 * is his run, heroes.c) away from the other for 1.7 s, turned the way he goes (his matrix: right +0x60, forward +0x70),
 * then he is gone (not drawn: +0x110 bit 0x20000, his weapons' too) until the next round stands him up again */
static void vs_show(uint32_t h, int on)
{
    extern uint32_t coop_wobjs[]; extern int coop_nwobjs; int i;
    if (h < 0x10000u) return;
    if (on) MEM32(h + 0x110) |= 0x20000u; else MEM32(h + 0x110) &= ~0x20000u;
    for (i = 0; i < coop_nwobjs; i++) { uint32_t q = coop_wobjs[i]; if (q >= 0x10000u && MEM32(q) == 0x174980u && MEM32(q + 0x11C) == h) { if (on) MEM32(q + 0x110) |= 0x20000u; else MEM32(q + 0x110) &= ~0x20000u; } }
    for (i = 0; i < coop_nwpn; i++) { uint32_t q = coop_wpn_copy[i]; if (q >= 0x10000u && MEM32(q) == 0x174980u && MEM32(q + 0x11C) == h) { if (on) MEM32(q + 0x110) |= 0x20000u; else MEM32(q + 0x110) &= ~0x20000u; } }
}
static void vs_flee(uint32_t now)
{
    static int on[2], hid[2]; static uint32_t t0[2], tl[2], who[2]; static float dx[2], dy[2];
    int w;
    for (w = 0; w < 2; w++) {
        uint32_t h = w ? coop_p2 : coop_first_char, o = w ? coop_first_char : coop_p2;
        if (on[w] && (who[w] != h || !coop_down[w])) { if (hid[w]) vs_show(who[w], 1); on[w] = hid[w] = 0; }   /* (stood up again: seen again) */
        if (!coop_down[w] || h < 0x10000u || o < 0x10000u || strcmp(vs_skin_of(w), "nazgul") || getenv("TT_VS_NOFLEE")) continue;
        if (!on[w]) {
            float x = MEMF(h + 0xF0) - MEMF(o + 0xF0), y = MEMF(h + 0xF4) - MEMF(o + 0xF4), d = sqrtf(x * x + y * y);
            if (d < 0.05f) { x = MEMF(h + 0x70); y = MEMF(h + 0x74); d = sqrtf(x * x + y * y); if (d < 0.05f) { x = 1; y = 0; d = 1; } }
            on[w] = 1; hid[w] = 0; who[w] = h; t0[w] = tl[w] = now; dx[w] = x / d; dy[w] = y / d;
            tt_dbg("[VS] P%d, the Nazgul, flees (%.2f %.2f)\n", w + 1, dx[w], dy[w]);
        }
        if (getenv("TT_VS_FLEESHOT")) {   /* (testing) frames of the flight */
            static uint32_t nexts[2]; extern void xbox_PresentDumpRequest(const char *path); char pth[300];
            if (now - t0[w] < 50u) nexts[w] = 0;
            if (now - t0[w] >= nexts[w] && nexts[w] <= 2400u) { snprintf(pth, sizeof pth, "%s/flee_%04u.bmp", getenv("TT_VS_FLEESHOT"), nexts[w]); xbox_PresentDumpRequest(pth); nexts[w] += 300u; }
        }
        if (now - t0[w] < 1700u) {
            float dt = (float)(now - tl[w]) / 1000.0f, p[3];
            if (dt > 0.1f) dt = 0.1f;
            p[0] = MEMF(h + 0xF0) + dx[w] * 7.5f * dt; p[1] = MEMF(h + 0xF4) + dy[w] * 7.5f * dt; p[2] = MEMF(h + 0xF8);
            if (dt > 0) vs_place(h, p);
            MEMF(h + 0x70) = dx[w]; MEMF(h + 0x74) = dy[w]; MEMF(h + 0x60) = dy[w]; MEMF(h + 0x64) = -dx[w];
        } else if (!hid[w]) { hid[w] = 1; vs_show(h, 0); }
        tl[w] = now;
    }
}
/* (1.9.7) Versus: player 1 at the top level with his bar exactly full (the game's gauge, 0xCF9A0, drew a maxed save's
 * experience past the top of the screen: the level-10 bar is out of 10000 above the threshold and his was far over).
 * His record's experience, level and the level's running experience are kept and put back once the game leaves the
 * level (tt_vs_exp_restore from the main tick), before anything can be saved. TT_VS_OWNEXP: as it was */
static uint32_t vs_xrec; static int32_t vs_x6c, vs_x78; static uint32_t vs_x90;
static void vs_exp_max(void)
{
    extern uint32_t coop_rec_addr; uint32_t r = coop_rec_addr; int32_t thr;
    if (getenv("TT_VS_OWNEXP") || r < 0x4AB598u || r >= 0x4AB598u + 8u * 0x348u) return;
    if (!vs_xrec) { vs_xrec = r; vs_x6c = (int32_t)MEM32(r + 0x6C); vs_x78 = (int32_t)MEM32(r + 0x78); vs_x90 = MEM32(r + 0x90);
        tt_dbg("[VS] player 1's record %08X kept (experience %d, level %d, the level's %.0f): at the top level for the match | thresholds %d %d %d %d / %d %d %d\n", r, vs_x6c, vs_x78, MEMF(r + 0x90),
               (int32_t)MEM32(0x1B10B4u + 7u * 12u), (int32_t)MEM32(0x1B10B4u + 8u * 12u), (int32_t)MEM32(0x1B10B4u + 9u * 12u), (int32_t)MEM32(0x1B10B4u + 10u * 12u),
               (int32_t)MEM32(0x1B10C0u + 8u * 12u), (int32_t)MEM32(0x1B10C0u + 9u * 12u), (int32_t)MEM32(0x1B10C0u + 10u * 12u)); }
    thr = (int32_t)MEM32(0x1B10B4u + 9u * 12u);
    MEM32(r + 0x78) = 9u; MEM32(r + 0x6C) = (uint32_t)(thr + 9999); MEMF(r + 0x90) = 0.0f;   /* (the top level's gauge is the experience past it modulo 10000: 9999 of 10000, full) */
}
void tt_vs_exp_restore(void)
{
    if (!vs_xrec || MEM32(0x4AD050u) == 6) return;
    MEM32(vs_xrec + 0x6C) = (uint32_t)vs_x6c; MEM32(vs_xrec + 0x78) = (uint32_t)vs_x78; MEM32(vs_xrec + 0x90) = vs_x90;
    tt_dbg("[VS] player 1's record %08X put back (experience %d, level %d)\n", vs_xrec, vs_x6c, vs_x78); vs_xrec = 0;
}
static void vs_frame(void)
{
    { extern int (*volatile xbox_movie_skip_cb)(void); xbox_movie_skip_cb = vs_skip_movies; }
    extern int tt_in_scene(void);
    uint32_t now = coop_now(); int w; char t[48];
    if (vs_level_n != coop_level_n) { vs_level_n = coop_level_n; vs_have = 0; vs_npk_out = 0; if (vs_pk_level != coop_level_n) { vs_npk = 0; vs_pk_level = coop_level_n; } vs_score[0] = vs_score[1] = 0; vs_round = 0; vs_state = 0; vs_text[0] = 0; }
    if (!getenv("TT_VS_KEEPSTART") && coop_first_char >= 0x10000u && MEM8(0x43B1B4u) && MEM32(0x4AD050u) == 6) {   /* (test) no opening cut-scene: the controls the scene would hand over */
        static int n; if (n++ < 3) tt_dbg("[VS] t %u controls unlocked (43B1B4 was %u, state %d)\n", coop_now(), MEM8(0x43B1B4u), vs_state); MEM8(0x43B1B4u) = 0; }
    if (coop_first_char < 0x10000u || coop_p2 < 0x10000u || !coop_p2_t0) return;
    { extern volatile int tt_vs_played; tt_vs_played = 1; }
    vs_targets();
    vs_exp_max();
    vs_push_frame();
    {   /* (1.9.7) the arena's battle music under the fight: its streams at TT_VS_MUSICGAIN (0.45), the characters' 3D
         * sounds at TT_VS_SFXGAIN (1.4); back to 1 out of Versus (tt_vs_audio_restore from the main tick) */
        extern volatile float xbox_apu_stream_gain, xbox_apu_3d_gain;
        xbox_apu_stream_gain = getenv("TT_VS_MUSICGAIN") ? (float)atof(getenv("TT_VS_MUSICGAIN")) : 0.45f;
        xbox_apu_3d_gain = getenv("TT_VS_SFXGAIN") ? (float)atof(getenv("TT_VS_SFXGAIN")) : 1.4f;
    }
    for (w = 0; w < 2; w++) if (vs_guard_broke[w]) {   /* a guard broken (sub_00053090): the blow's harm */
        uint32_t f = w ? coop_p2 : coop_first_char; int32_t mx = (int32_t)MEM32(f + 0x300), dmg = mx * (getenv("TT_VS_GUARDPCT") ? atoi(getenv("TT_VS_GUARDPCT")) : 6) / 100, h;
        vs_guard_broke[w] = 0;
        if (vs_state != 0 || coop_down[w] || f < 0x10000u) continue;
        h = (int32_t)MEM32(f + 0x2FC) - dmg; tt_vs_note_hit(w, dmg, 0); vs_impact(f);
        if (h <= 0) { MEM32(f + 0x2FC) = 1; coop_down[w] = 1; coop_down_t[w] = now; } else MEM32(f + 0x2FC) = (uint32_t)h;
        snprintf(t, sizeof t, "P%d GUARD BROKEN", w + 1); vs_say(t, 1200u);
    }
    if (getenv("TT_VS_GAMEVOICETEST")) { static uint32_t tv; if (now - tv > 2000u) { uint32_t cm = MEM32(0x43B164u), a[10]; float one = 1.0f, m1 = -1.0f, z = 0.0f; extern uint32_t tt_gcall(uint32_t va, uint32_t thisptr, int n, const uint32_t *args);
        tv = now; a[0] = (uint32_t)atoi(getenv("TT_VS_GAMEVOICETEST")); memcpy(&a[1], &one, 4); memcpy(&a[2], &z, 4); a[3] = MEM32(cm + 0x50); a[4] = MEM32(cm + 0x54); a[5] = MEM32(cm + 0x58); memcpy(&a[6], &one, 4); memcpy(&a[7], &m1, 4); a[8] = 0; a[9] = 0;
        tt_dbg("[VS] game voice test: %08X\n", tt_gcall(0x000C3B90u, 0, 10, a)); } }
    if (getenv("TT_VS_VOICETEST")) { static uint32_t tv; extern int tt_vs_voice(const char *who, int slot, float vol); if (now - tv > 2000u) { tv = now; tt_dbg("[VS] voice test: %d\n", tt_vs_voice(vs_skin_name(0), 0, 1.0f)); } }
    if (getenv("TT_VS_P2FOLLOW") && vs_state == 0) { MEMF(coop_p2 + 0xF0) = MEMF(coop_first_char + 0xF0) + 2.0f; MEMF(coop_p2 + 0xF4) = MEMF(coop_first_char + 0xF4); MEMF(coop_p2 + 0xF8) = MEMF(coop_first_char + 0xF8); }   /* (test) */
    vs_leash();
    vs_edge_guard();
    vs_camera();
    if (!vs_have) {   /* the first frame of play: the spots, then round 1 */
        if (tt_in_scene()) return;
        for (w = 0; w < 2; w++) { uint32_t a = w ? coop_p2 : coop_first_char; vs_spot[w][0] = MEMF(a + 0xF0); vs_spot[w][1] = MEMF(a + 0xF4); vs_spot[w][2] = MEMF(a + 0xF8); }
        {   /* the heroes a little apart, facing off: player 2 put 8 units from player 1 the way he stands from him */
            float dx = vs_spot[1][0] - vs_spot[0][0], dy = vs_spot[1][1] - vs_spot[0][1], d = sqrtf(dx * dx + dy * dy);
            if (d < vs_spacing() && !getenv("TT_VS_NOSPREAD")) vs_free_spot(coop_first_char, vs_spot[0][0], vs_spot[0][1], d > 0.1f ? dx : -MEMF(coop_first_char + 0x70), d > 0.1f ? dy : -MEMF(coop_first_char + 0x74), vs_spacing(), &vs_spot[1][0], &vs_spot[1][1]);   /* (1.9.7: 12 units apart, was 8; on walkable ground) */
        }
        if (MEM32(0x1BB2A4u) == 12u && !getenv("TT_VS_ORTHSTART")) {   /* (1.9.7) Orthanc: its camera circles the tower's middle
             * (0, 0), looking in from outside the hero (as in the story, observed: the eye 20 units further out, 8.5 up), so
             * the stick's left and right take him round the floor. The level starts him 5 units from the middle, where
             * that circle is so small he all but turned on the spot: the heroes start TT_VS_ORTHR (20) out instead, 8 apart */
            float R = getenv("TT_VS_ORTHR") ? (float)atof(getenv("TT_VS_ORTHR")) : 20.0f, half = asinf(0.5f * vs_spacing() / R), base = -1.5707963f;
            vs_spot[0][0] = R * cosf(base - half); vs_spot[0][1] = R * sinf(base - half);
            vs_spot[1][0] = R * cosf(base + half); vs_spot[1][1] = R * sinf(base + half);
        }
        { extern void tt_ach_vs_event(int kind, int who, int a, int b, const char *skin); tt_ach_vs_event(6, 0, 0, 0, NULL); }
        vs_have = 1; vs_score[0] = vs_score[1] = 0; vs_round = 0; memset(tt_vs_st, 0, sizeof tt_vs_st); vs_carrier = -1;
        tt_dbg("[VS] match: player 1 at %.1f %.1f, player 2 at %.1f %.1f\n", vs_spot[0][0], vs_spot[0][1], vs_spot[1][0], vs_spot[1][1]);
        vs_pk_dump("at the start");
        vs_round_start();
        return;
    }
    for (w = 0; w < 2; w++) {   /* health at 0: down (held at 1) */
        uint32_t a = w ? coop_p2 : coop_first_char;
        if (!coop_down[w] && (int32_t)MEM32(a + 0x2FC) <= 0) { coop_down[w] = 1; coop_down_t[w] = now; MEM32(a + 0x2FC) = 1; }
    }
    if (getenv("TT_VS_HEADLOG")) { static uint32_t tl; if (now - tl > (getenv("TT_VS_HEADMS") ? (uint32_t)atoi(getenv("TT_VS_HEADMS")) : 100u) && coop_p2 >= 0x10000u && coop_first_char >= 0x10000u) { tl = now;
        tt_dbg("[VSHEAD] t %u P1 st %u fwd %.2f %.2f pos %.2f %.2f | P2 st %u fwd %.2f %.2f pos %.2f %.2f\n", now, MEM32(coop_first_char + 0x408), MEMF(coop_first_char + 0x70), MEMF(coop_first_char + 0x74), MEMF(coop_first_char + 0xF0), MEMF(coop_first_char + 0xF4),
               MEM32(coop_p2 + 0x408), MEMF(coop_p2 + 0x70), MEMF(coop_p2 + 0x74), MEMF(coop_p2 + 0xF0), MEMF(coop_p2 + 0xF4)); } }
    if (getenv("TT_VS_DISTLOG")) { static uint32_t tl; if (now - tl > 500u) { uint32_t cm = MEM32(0x43B164u); int w; tl = now;
        for (w = 0; w < 2; w++) { uint32_t h = w ? coop_p2 : coop_first_char; float ex = MEMF(cm + 0x50) - MEMF(h + 0xF0), ey = MEMF(cm + 0x54) - MEMF(h + 0xF4), ez = MEMF(cm + 0x58) - MEMF(h + 0xF8);
            tt_dbg("[VSDIST] t %u P%d 448 %.2f 504 %.2f 534 %.2f 2C8 %.2f eye %.2f | cam %.1f %.1f %.1f\n", now, w + 1, MEMF(h + 0x448), MEMF(h + 0x504), MEMF(h + 0x534), MEMF(h + 0x2C8), sqrtf(ex * ex + ey * ey + ez * ez), MEMF(cm + 0x50), MEMF(cm + 0x54), MEMF(cm + 0x58)); } } }
    if (getenv("TT_VS_HDIFF")) { static uint32_t tl; if (now - tl > 3000u) { uint32_t o; char b2[8000]; int k = 0; tl = now;
        for (o = 0; o < 0x600 && k < 7800; o += 4) if (MEM32(coop_first_char + o) != MEM32(coop_p2 + o)) k += snprintf(b2 + k, sizeof b2 - k, " %X:%08X/%08X", o, MEM32(coop_first_char + o), MEM32(coop_p2 + o));
        tt_dbg("[VSHDIFF] t %u%s\n", now, b2);
        { uint32_t n1 = MEM32(coop_first_char + 0x168), n2 = MEM32(coop_p2 + 0x168); k = 0;
          if (n1 >= 0x10000u && n2 >= 0x10000u) for (o = 0; o < 0x200 && k < 7800; o += 4) if (MEM32(n1 + o) != MEM32(n2 + o)) k += snprintf(b2 + k, sizeof b2 - k, " %X:%08X/%08X", o, MEM32(n1 + o), MEM32(n2 + o));
          tt_dbg("[VSNDIFF] %08X %08X%s\n", n1, n2, k ? b2 : ""); } } }
    if (getenv("TT_VS_MATLOG")) { static uint32_t tl; if (now - tl > 700u) { uint32_t a = coop_first_char, o; char b2[5200]; int k = 0; tl = now;
        for (o = 0x0; o < 0x480 && k < 5000; o += 4) k += snprintf(b2 + k, sizeof b2 - k, " %X:%.3g", o, MEMF(a + o));
        tt_dbg("[VSMAT] P1 +80..FC:%s\n", b2); } }
    {   /* (a fall: his death cry, of our own -- the knockdown the fall is plays none) */
        static int was[2]; extern int tt_vs_voice(const char *who, int slot, float vol);
        for (w = 0; w < 2; w++) { if (coop_down[w] && !was[w]) tt_vs_voice(vs_skin_name(w), 7, 1.0f); was[w] = coop_down[w]; }
    }
    coop_down_pose(now);   /* a downed hero plays his death and lies until the next round */
    vs_flee(now);   /* (the Nazgul: his fall is his flight, as from Weathertop's fire) */
    if (getenv("TT_VS_TPTO") && vs_state == 0 && now - vs_round_t0 > 2000u && now - vs_round_t0 < 2600u) {   /* (testing) both heroes to x,y,z */
        float p[3] = { 0, 0, 0 }; { const char *q = getenv("TT_VS_TPTO"); p[0] = (float)atof(q); q = strchr(q, 0x2C); if (q) { p[1] = (float)atof(++q); q = strchr(q, 0x2C); if (q) p[2] = (float)atof(++q); } } vs_place(coop_first_char, p); p[0] += 3.0f; vs_place(coop_p2, p); }
    if (getenv("TT_VS_SHSHOT") && vs_sh_broke_t && now - vs_sh_broke_t < 4000u) { static uint32_t nx; extern void xbox_PresentDumpRequest(const char *path); char pth[300];   /* (testing) after the break */
        if (now - vs_sh_broke_t < 100u) nx = 400u; if (now - vs_sh_broke_t >= nx) { snprintf(pth, sizeof pth, "%s/after_%04u.bmp", getenv("TT_VS_SHSHOT"), nx); xbox_PresentDumpRequest(pth); nx += 600u; } }
    for (w = 0; w < 2; w++) {   /* the same most health for both, whoever they are (Sauron's 1600, upgrades: no) */
        uint32_t a = w ? coop_p2 : coop_first_char; int32_t mx = getenv("TT_VS_HP") ? atoi(getenv("TT_VS_HP")) : VS_HP;
        if ((int32_t)MEM32(a + 0x300) != mx) MEM32(a + 0x300) = (uint32_t)mx;
        if ((int32_t)MEM32(a + 0x2FC) > mx && (int32_t)MEM32(a + 0x2FC) < 50000) MEM32(a + 0x2FC) = (uint32_t)mx;   /* (not during a hit: the health guard holds it 100000 up) */
    }
    if (!getenv("TT_VS_REALDEATH")) for (w = 0; w < 2; w++) if (coop_down[w]) MEM32((w ? coop_p2 : coop_first_char) + 0x2FC) = 0;
    for (w = 0; w < 2; w++) {   /* a hero downed last round, in the countdown and still in his death (dying 44-46, dead 47, lying 50):
                                 * put to 47 (the inputs' block mask cleared, as coop_respawn), then INP_RESSURECT (28) stands him up */
        static uint32_t tu[2]; uint32_t a = w ? coop_p2 : coop_first_char, st = MEM32(a + 0x408);
        if (coop_down[w]) { vs_was_down[w] = 1; continue; }
        if (!vs_was_down[w] || now - tu[w] < 400u) continue;
        if ((vs_state != 3 && vs_state != 0) || (vs_state == 0 && now - vs_round_t0 > 8000u) || !((st >= 44 && st <= 47) || st == 50)) {
            if (vs_state != 3 && (vs_state != 0 || now - vs_round_t0 > 8000u || st < 44 || st > 50)) vs_was_down[w] = 0; continue; }
        tu[w] = now; MEM32(a + 0x3D8 + 0x50) = 0; MEM32(a + 0x3D8 + 0x54) = 0;
        /* the death runs to its end now (44-47, their time pushed on), and from its last state (50, lying) he is put
         * to 47 with the block mask cleared and gets INP_RESSURECT (28): he stands (51, then idle), as coop_respawn.
         * (28 in the 47 the death passes through does nothing, observed) */
        if (st != 50) { MEMF(a + 0x420) = 20.0f; continue; }
        MEM32(a + 0x408) = 47; MEMF(a + 0x420) = 0.0f; coop_hero_state(a, 28u);
        { static int n; if (n++ < 10) tt_dbg("[VS] P%d gets up (%u -> %u)\n", w + 1, st, MEM32(a + 0x408)); }
    }
    vs_shots_due(now);
    vs_swings_due(now);
    if (getenv("TT_VS_PKAGE0")) { int i; for (i = 0; i < vs_npk_out; i++) if (vs_pk_out[i] >= 0x10000u && MEM32(vs_pk_out[i]) == 0x1748B0u && (MEM32(vs_pk_out[i] + 0x3F8) & 2u)) MEM32(vs_pk_out[i] + 0x400) = 0; }   /* (the pickups out, the shield too: their age kept new -- a drop's item goes at 6 s) */
    if (vs_state == 0) { if (vs_shield_mode()) vs_shield_frame(now); else vs_pk_touch(); }
    if (getenv("TT_VS_PKTEST3") && vs_state == 0 && vs_npk_out >= 2) {   /* (test) player 1 hurt and short, put on the health then an ammo pickup */
        static int st; uint32_t e = now - vs_round_t0, p1 = coop_first_char;
        if (st == 0 && e > 2000u) { st = 1; MEM32(p1 + 0x2FC) = 100; MEM32(p1 + 0x304) = (MEM32(p1 + 0x304) & 0xFFFF0000u) | 2u; { float q[3] = { MEMF(vs_pk_out[0] + 0xF0), MEMF(vs_pk_out[0] + 0xF4), MEMF(vs_pk_out[0] + 0xF8) }; vs_place(p1, q); } }
        if (st == 1 && e > 5000u) { st = 2; float q[3] = { MEMF(vs_pk_out[1] + 0xF0), MEMF(vs_pk_out[1] + 0xF4), MEMF(vs_pk_out[1] + 0xF8) }; vs_place(p1, q); }
    }
    if (getenv("TT_VS_PICKTEST2") && vs_state == 0) {   /* (test) each 4 s a pickup of the next kind under player 1, his health and ammo lowered first */
        static uint32_t t; static int k; uint32_t p1 = coop_first_char;
        if (now - t > 4000u && k < 12) { t = now;
            tt_dbg("[VSPICK] before kind %d: P1 health %d ammo %u\n", k - 1, (int32_t)MEM32(p1 + 0x2FC), MEM32(p1 + 0x304) & 0xFFFF);
            MEM32(p1 + 0x2FC) = 300; MEM32(p1 + 0x304) = (MEM32(p1 + 0x304) & 0xFFFF0000u) | 3u;
            vs_npk_out = 0; { uint32_t o = vs_pk_spawn(MEMF(p1 + 0xF0), MEMF(p1 + 0xF4), MEMF(p1 + 0xF8) + 0.3f, k);
                if (o && vs_npk) tt_dbg("[VSPICK] spawned +110 %08X +124 %08X +F8 %.2f | static %08X +110 %08X +124 %08X\n", MEM32(o + 0x110), MEM32(o + 0x124), MEMF(o + 0xF8), vs_pk[0], MEM32(vs_pk[0] + 0x110), MEM32(vs_pk[0] + 0x124)); }
            if (k == 6) { float sp3[3] = { MEMF(vs_pk[0] + 0xF0), MEMF(vs_pk[0] + 0xF4), MEMF(vs_pk[0] + 0xF8) }; vs_place(p1, sp3); tt_dbg("[VSPICK] P1 put on the level's own pickup\n"); }
            k++; }
    }
    if (vs_state == 3) { vs_countdown(now); return; }
    if (getenv("TT_VS_TESTEND") && vs_state == 0 && now - vs_round_t0 > (uint32_t)atoi(getenv("TT_VS_TESTEND"))) {   /* (test) the match won by player 1 now */
        vs_score[0] = 2; vs_state = 2; vs_t = now; vs_say("PLAYER 1 WINS THE MATCH  2 - 0", 1u); vs_pick_awards(0); }
    if (vs_state == 0) {
        int d0 = coop_down[0], d1 = coop_down[1];
        if (d0 || d1) {
            int win = d0 && d1 ? -1 : d0 ? 1 : 0, pts = 1;
            if (vs_shield_mode() && win >= 0) { pts = vs_carrier == win ? 2 : 1; if (vs_carrier == (win ^ 1)) vs_carrier = -1; }   /* (the carrier beaten: the shield goes) */
            if (win >= 0) { extern void tt_ach_vs_round(int win, int hp, int maxhp, int taken); uint32_t wa = win ? coop_p2 : coop_first_char;
                tt_ach_vs_round(win, (int32_t)MEM32(wa + 0x2FC), (int32_t)MEM32(wa + 0x300), tt_vs_st[win].taken - vs_taken0[win]); }
            { extern void tt_ach_vs_scores(int s1, int s2); tt_ach_vs_scores(vs_score[0], vs_score[1]); }
            if (win >= 0) { vs_score[win] += pts; tt_vs_st[win].rounds++; tt_vs_st[win ^ 1].downs++;
                { extern void tt_ach_vs_event(int kind, int who, int a, int b, const char *skin); tt_ach_vs_event(1, win, vs_score[0], vs_score[1], NULL); }
                if (now - vs_round_t0 > 500u && (!tt_vs_st[win].fastest_ms || now - vs_round_t0 < tt_vs_st[win].fastest_ms)) tt_vs_st[win].fastest_ms = now - vs_round_t0; }
            vs_last_win = win; vs_state = 1; vs_t = now;
            if (win >= 0 && vs_score[win] >= vs_goal()) { extern void tt_ach_vs_match(const char *skin, int shield_mode); extern void tt_ach_vs_event(int kind, int who, int a, int b, const char *skin);
                extern void tt_vs_play_wav(const char *name, float vol);
                tt_ach_vs_match(vs_skin_name(win), vs_shield_mode()); tt_ach_vs_event(5, win, (int)MEM32(0x1BB2A4u), vs_score[win ^ 1], vs_skin_name(win));
                tt_vs_play_wav("match_win.wav", 1.0f);   /* (1.9.7) the user's sounds, beside the game in versus\ (none: silent) */ vs_state = 2; snprintf(t, sizeof t, "PLAYER %d WINS THE MATCH  %d - %d", win + 1, vs_score[win], vs_score[win ^ 1]); vs_say(t, getenv("TT_VS_NORESULTS") ? 6000u : 1u); vs_pick_awards(win); }
            else if (win >= 0 && vs_shield_mode()) { snprintf(t, sizeof t, "PLAYER %d +%d   P1 %d - %d P2", win + 1, pts, vs_score[0], vs_score[1]); vs_say(t, 3000u); }
            else if (win >= 0) { extern void tt_vs_play_wav(const char *name, float vol); snprintf(t, sizeof t, "PLAYER %d WINS ROUND %d", win + 1, vs_round); vs_say(t, 4000u); tt_vs_play_wav("round_win.wav", 1.0f); }
            else { snprintf(t, sizeof t, "ROUND %d: A DRAW", vs_round); vs_say(t, 4000u); }
        }
    } else if (vs_state == 1 && now - vs_t >= (vs_shield_mode() ? 3000u : 4000u)) vs_round_start();
    else if (vs_state == 2 && now - vs_t >= (getenv("TT_VS_NORESULTS") ? 6000u : VS_RESULTS_MS) && !getenv("TT_VS_LOOP")) {
        /* the match over: back to the Versus menu (the level ended as a quit, EndMission(code); the front end's
         * screens on the way skipped, menus.c tt_vs_return) */
        extern volatile int tt_vs_return; static unsigned done_n = ~0u;
        if (done_n != coop_level_n) { uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx, code = getenv("TT_VS_ENDCODE") ? (uint32_t)atoi(getenv("TT_VS_ENDCODE")) : 6u;
            done_n = coop_level_n; tt_vs_return = 1;
            tt_dbg("[VS] the match is over: back to the Versus menu\n");
            {   /* the play screen ([0x4AD054], update 0x7B950) leaves as on the pause menu's Exit to Menu, Yes: its fade out
                 * (+0x4DB8 = 3, +0x4DBC = 1.0), the game's sound stopped (0xC3030, 0xE35F0), then the main menu (+4 = 4) */
                uint32_t pl = MEM32(0x4AD054u);
                if (pl >= 0x10000u) { PUSH32(esp, 0x000C3030u); sub_000C3030(); esp = sp; PUSH32(esp, 0x000E35F0u); sub_000E35F0(); esp = sp;
                    MEM32(pl + 0x4DB8) = 3; MEM32(pl + 0x4DBC) = 0x3F800000u; MEM32(pl + 4) = getenv("TT_VS_TOMAP") ? 1u : 4u; } }   /* (4: straight to the main menu, as the game does when there is nothing to save) */
            if (getenv("TT_VS_ENDCODE")) { PUSH32(esp, code); PUSH32(esp, 0x000F1230u); ecx = MEM32(0x8D2484u); sub_000F1230(); } esp = sp; eax = r0; ecx = r1; edx = r2; }
    }
    else if (vs_state == 2 && now - vs_t >= (getenv("TT_VS_NORESULTS") ? 6000u : VS_RESULTS_MS)) { vs_score[0] = vs_score[1] = 0; vs_round = 0; memset(tt_vs_st, 0, sizeof tt_vs_st); vs_carrier = -1; tt_dbg("[VS] a new match\n"); vs_round_start(); }
}
static void coop_death_frame(void)
{
    if (getenv("TT_P2DUMP") && coop_p2 >= 0x10000u) { static uint32_t tl; if (coop_now() - tl > 3000u) { uint32_t o; char b2[9000]; int k = 0; tl = coop_now();
        for (o = 0; o < 0x600 && k < 8800; o += 4) k += snprintf(b2 + k, sizeof b2 - k, " %X:%08X", o, MEM32(coop_p2 + o));
        tt_dbg("[P2DUMP]%s\n", b2); } }
    if (tt_vs_on()) { vs_frame(); return; }
    uint32_t now = coop_now(), a = coop_p2, wait = coop_respawn_wait(); int w;
    static uint32_t tlog;
    if (coop_mode() != 3 || a < 0x10000u || getenv("TT_COOP_NODEATH")) return;
    if (coop_gameover) {
        if (coop_real_death_pending && !coop_depth && MEM32(0x43B15Cu) == coop_first_char) coop_p1_real_death();
        if (coop_gameover_t && !coop_end_seen && now - coop_gameover_t > (getenv("TT_COOP_GAMEOVERWAIT") ? (uint32_t)atoi(getenv("TT_COOP_GAMEOVERWAIT")) : 20000u)) {   /* the death cam didn't end it: EndMission(5) (0xF1230) */
            uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx; coop_gameover_t = 0; tt_dbg("[COOPDEATH] game over: EndMission(5)\n");
            PUSH32(esp, 5); PUSH32(esp, 0x000F1230u); ecx = MEM32(0x8D2484u); sub_000F1230(); esp = sp; eax = r0; ecx = r1; edx = r2;
        }
        return;
    }
    if (!coop_down[1] && (int32_t)MEM32(a + 0x2FC) <= 0) { coop_p2_down_now(); if (coop_gameover) return; }
    if (coop_down[1] && coop_first_char >= 0x10000u && (int32_t)MEM32(coop_first_char + 0x2FC) <= 0) {   /* player 1 at 0 while player 2 is down: game over. The level
        * script's @ObjectDie for him landed on the twin's die method (lookups redirected to the twin), which the respawn skips:
        * player 1 went on at 0 health and the game never ended (observed with TT_COOP_TESTDOWN + TT_COOP_TESTHP1) */
        tt_dbg("[COOPDEATH] P1 at 0 while P2 is down: game over (player 1's own death)\n");
        coop_p1_real_death(); return;
    }
    if (!coop_down[0] && coop_first_char >= 0x10000u && (int32_t)MEM32(coop_first_char + 0x2FC) <= 0 && !coop_down[1]) {   /* player 1 at 0 while player 2 is up: down at 1 health, so the level script's death check (health 0 → @ObjectDie, dead state 47) never fires */
        MEM32(coop_first_char + 0x2FC) = 1; coop_down[0] = 1; coop_down_t[0] = now;
        tt_dbg("[COOPDEATH] P1 down (P2 alive): held at 1 health, respawn in %u ms\n", wait);
    }
    if (getenv("TT_COOP_DOWNSCAN") && coop_down[0] && coop_first_char >= 0x10000u) {   /* (research) player 1 held down: each 3.5 s raise event e, record the states over 2 s, then event 28 (get up from 47), record again */
        static uint32_t t0; static int ev = -1, ph, ns; static uint32_t seen[24];
        uint32_t st = MEM32(coop_first_char + 0x408); int i;
        for (i = 0; i < ns && seen[i] != st; i++) ; if (i == ns && ns < 24) seen[ns++] = st;
        if (ev < 0 || now - t0 >= (ph ? 1500u : 2000u)) {
            if (ev >= 0) { for (i = ns; i < 8; i++) seen[i] = 0;
                tt_dbg("[COOPDOWNSCAN] event %d%s: states %u %u %u %u %u %u %u %u (%d seen, now %u, hp %d)\n", ev, ph ? " then 28" : "", seen[0], seen[1], seen[2], seen[3], seen[4], seen[5], seen[6], seen[7], ns, st, (int32_t)MEM32(coop_first_char + 0x2FC)); }
            ns = 0; t0 = now;
            if (ph == 0 && ev >= 0) { ph = 1; coop_hero_state(coop_first_char, 28); }
            else { ph = 0; if (ev < 63) { ev++; coop_hero_state(coop_first_char, (uint32_t)ev); } }
        }
    }
    if (getenv("TT_COOP_DEADSCAN") && coop_down[1] && coop_p2 >= 0x10000u) {   /* (research) player 2 down in the full death (50): which input takes him out. Each 2 s one input; out of 50 -> logged, then INP_DIE again and a 7 s wait */
        static uint32_t t0; static int ev = -1, waitdie; uint32_t st = MEM32(coop_p2 + 0x408);
        if (ev < 0 && st == 50) { ev = 0; t0 = now; tt_dbg("[COOPDEADSCAN] P2 in 50 at %u ms down: scan starts\n", now - coop_down_t[1]); }
        else if (ev >= 0 && ev < 64 && now - t0 >= (waitdie ? 7000u : 2000u)) {
            if (!waitdie && st != 50) { tt_dbg("[COOPDEADSCAN] input %d: 50 -> %u (hp %d)\n", ev, st, (int32_t)MEM32(coop_p2 + 0x2FC)); waitdie = 1; t0 = now; coop_hero_state(coop_p2, 1); }
            else { if (waitdie) tt_dbg("[COOPDEADSCAN] back: state %u\n", st); waitdie = 0; ev++; t0 = now; if (ev < 64) coop_hero_state(coop_p2, (uint32_t)ev); }
        }
    }
    if (getenv("TT_COOP_REVIVESCAN") && !coop_down[0] && coop_first_char >= 0x10000u && MEM32(coop_first_char + 0x408) == 47) {   /* (research) which state-machine event takes player 1 out of the dead state 47 */
        static uint32_t tn; static int ev = -1;
        if (now - tn > 300 && ev < 63) { tn = now; ev++; tt_dbg("[COOPREVIVE] state %u: raising event %d\n", MEM32(coop_first_char + 0x408), ev); coop_hero_state(coop_first_char, (uint32_t)ev); }
    }
    {   static uint32_t ls = 0xFFFFFFFFu; if (getenv("TT_COOP_REVIVESCAN") && coop_first_char >= 0x10000u && MEM32(coop_first_char + 0x408) != ls) { ls = MEM32(coop_first_char + 0x408); tt_dbg("[COOPREVIVE] P1 state now %u\n", ls); } }
    coop_down_pose(now);
    if (getenv("TT_COOP_TESTNEAR")) for (w = 0; w < 2; w++) {   /* bot test: 5 s after a hero goes down, the other one is put 2 units beside him, so the camera (on the one standing) shows the body */
        static uint32_t done_t[2]; uint32_t h = w ? coop_p2 : coop_first_char, o = w ? coop_first_char : coop_p2;
        if (!coop_down[w] || h < 0x10000u || o < 0x10000u) { done_t[w] = 0; continue; }
        if (!done_t[w] && now - coop_down_t[w] >= 5000u) { done_t[w] = now;   /* once he lies (50 at about 4.5 s) */
            MEMF(o + 0xF0) = MEMF(h + 0xF0) + 2.0f; MEMF(o + 0xF4) = MEMF(h + 0xF4); MEMF(o + 0xF8) = MEMF(h + 0xF8);
            MEM32(o + 0x90) = MEM32(o + 0xF0); MEM32(o + 0x94) = MEM32(o + 0xF4); MEM32(o + 0x98) = MEM32(o + 0xF8);
            tt_dbg("[COOPDEATH] test: P%d put beside the downed P%d\n", 2 - w, w + 1); }
    }
    if (!getenv("TT_COOP_NOWPNSHOW")) {   /* a revived hero's weapons: the get-up left one of player 1's weapons with +0x110 bit
        * 0x1000 clear (Balin's Tomb, Aragorn's weaponObj69: 0x17D40 -> 0x16D40 at the respawn, and his hand empty in the frames
        * after it; a playtest: "after p1 revive, his weapons were invisible"). While a hero is up his weapons' bits are kept;
        * 2.5 s into his get-up a cleared one is set again. TT_COOP_NOWPNSHOW: off */
        extern uint32_t coop_wobjs[]; extern int coop_nwobjs;
        static uint32_t ws[2][8], wb[2][8]; static int wn[2]; int k, j;
        for (w = 0; w < 2; w++) {
            uint32_t h = w ? coop_p2 : coop_first_char;
            if (h < 0x10000u) continue;
            if (!coop_down[w] && !coop_upwatch[w]) {
                wn[w] = 0;
                for (k = 0; k < coop_nwobjs && wn[w] < 8; k++) { uint32_t q = coop_wobjs[k];
                    if (MEM32(q + 0x11C) != h) continue;
                    for (j = 0; j < wn[w] && ws[w][j] != q; j++) ;
                    if (j == wn[w]) { ws[w][wn[w]] = q; wb[w][wn[w]++] = MEM32(q + 0x110) & 0x1000u; } }
            } else if (coop_upwatch[w] && now - coop_upwatch[w] >= 2500u)
                for (k = 0; k < wn[w]; k++) if (wb[w][k] && !(MEM32(ws[w][k] + 0x110) & 0x1000u)) {
                    MEM32(ws[w][k] + 0x110) |= 0x1000u;
                    tt_dbg("[COOPDEATH] P%d's weapon %08X '%s' shown again after his get-up (+110 bit 0x1000)\n", w + 1, ws[w][k], coop_nm(ws[w][k])); }
        }
    }
    for (w = 0; w < 2; w++) if (coop_upwatch[w]) {   /* the get-up after a respawn: logged for 3 s (from 50 set to 47: 51, then idle 2, observed) */
        static uint32_t lst2[2]; uint32_t h = w ? coop_p2 : coop_first_char, st = h >= 0x10000u ? MEM32(h + 0x408) : 0;
        if (now - coop_upwatch[w] > 3000u || coop_down[w]) { coop_upwatch[w] = 0; continue; }
        if (st != lst2[w]) { tt_dbg("[COOPDEATH] P%d getting up, %u ms: state %u -> %u\n", w + 1, now - coop_upwatch[w], lst2[w], st); lst2[w] = st; }
    }
    for (w = 0; w < 2; w++) if (coop_down[w]) {
        uint32_t h = w ? coop_p2 : coop_first_char, o = w ? coop_first_char : coop_p2;
        if (w == 0 && (int32_t)MEM32(h + 0x2FC) != 1 && !getenv("TT_COOP_NOHPGUARD")) MEM32(h + 0x2FC) = 1;   /* player 1 kept at 1 while down (the death sequence raised it back to 1000: a full bar while down, observed) */
        if (w == 1 && !getenv("TT_COOP_NOHPGUARD") && (int32_t)MEM32(h + 0x2FC) != 1) MEM32(h + 0x2FC) = 1;   /* player 2 too */
        if ((int32_t)MEM32(h + 0x2FC) < 0) MEM32(h + 0x2FC) = 0;
        if (now - coop_down_t[w] >= wait) coop_respawn(w);
        else if (now - tlog > 5000) { tlog = now; tt_dbg("[COOPDEATH] P%d down, respawn in %d ms\n", w + 1, (int)(wait - (now - coop_down_t[w]))); }
        (void)o;
    }
    {   /* enemies on a downed hero go to the other one */
        int i; for (i = 0; i < coop_nchars; i++) { uint32_t e = coop_chars[i], ce = MEM32(e + 0x494), t;
            if (MEM32(e + 0x114) != 3 || ce < 0x10000u) continue; t = MEM32(ce + 0x98);
            if ((t == coop_p2 && coop_down[1]) || (t == coop_first_char && coop_down[0])) { uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx; PUSH32(esp, t == coop_p2 ? coop_first_char : coop_p2); PUSH32(esp, 0x00053AE0u); ecx = ce; sub_00053AE0(); esp = sp; eax = r0; ecx = r1; edx = r2; } }
    }
}/* player 2's HUD panel (top right): "PLAYER 2", his health as a number and a bar, and while he is down "DOWN" and the
 * respawn countdown. Drawn by the renderer's overlay hook (xbox_overlay_cb: BGRA quads in the game's 640x480 space,
 * drawn over the finished frame), chained after the port's own overlay (e3hud.c). A 5x7 font of our own, no game
 * assets. TT_COOP_NOHUD turns it off; TT_COOP_HUDSHOT="ms:file,..." dumps the shown frame at those times (bot test). */
typedef struct { const uint32_t *px; int w, h; unsigned ver; float x, y, dw, dh; } CoopOvlQuad;
int tt_vs_res_overlay(CoopOvlQuad *q);
extern int (*volatile tt_ovl_extra)(CoopOvlQuad *q, int max);   /* menus.c: extra quads before the cursor (its overlay re-hooks itself, so no chaining from here) */
extern int xbox_MoviePlaying(void);
extern void xbox_PresentDumpRequest(const char *path);
#define COOP_HUD_W 320
#define COOP_HUD_H 80
static uint32_t coop_hud_px[COOP_HUD_W * COOP_HUD_H];
static unsigned coop_hud_ver; static int coop_hud_show;
static volatile int coop_hud_hp, coop_hud_max, coop_hud_down, coop_hud_secs;
static const struct { char c; unsigned char r[7]; } coop_font[] = {
    { '0', { 14, 17, 19, 21, 25, 17, 14 } }, { '1', { 4, 12, 4, 4, 4, 4, 14 } }, { '2', { 14, 17, 1, 2, 4, 8, 31 } },
    { '3', { 31, 2, 4, 2, 1, 17, 14 } }, { '4', { 2, 6, 10, 18, 31, 2, 2 } }, { '5', { 31, 16, 30, 1, 1, 17, 14 } },
    { '6', { 6, 8, 16, 30, 17, 17, 14 } }, { '7', { 31, 1, 2, 4, 8, 8, 8 } }, { '8', { 14, 17, 17, 14, 17, 17, 14 } },
    { '9', { 14, 17, 17, 15, 1, 2, 12 } }, { 'A', { 14, 17, 17, 31, 17, 17, 17 } }, { 'D', { 30, 17, 17, 17, 17, 17, 30 } },
    { 'E', { 31, 16, 16, 30, 16, 16, 31 } }, { 'I', { 14, 4, 4, 4, 4, 4, 14 } }, { 'L', { 16, 16, 16, 16, 16, 16, 31 } },
    { 'N', { 17, 25, 21, 19, 17, 17, 17 } }, { 'O', { 14, 17, 17, 17, 17, 17, 14 } }, { 'P', { 30, 17, 17, 30, 16, 16, 16 } },
    { 'R', { 30, 17, 17, 30, 20, 18, 17 } }, { 'S', { 15, 16, 16, 14, 1, 1, 30 } }, { 'W', { 17, 17, 17, 21, 21, 21, 10 } },
    { 'Y', { 17, 17, 10, 4, 4, 4, 4 } }, { ':', { 0, 12, 12, 0, 12, 12, 0 } }, { '/', { 1, 1, 2, 4, 8, 16, 16 } } };
static void coop_hud_rect(int x0, int y0, int x1, int y1, uint32_t c)
{
    int x, y; for (y = y0 < 0 ? 0 : y0; y < y1 && y < COOP_HUD_H; y++) for (x = x0 < 0 ? 0 : x0; x < x1 && x < COOP_HUD_W; x++) coop_hud_px[y * COOP_HUD_W + x] = c;
}
static int coop_hud_text(int x, int y, const char *s, int k, uint32_t c)   /* 5x7 glyphs scaled by k; returns the end x */
{
    for (; *s; s++, x += 6 * k) { int g, r, b; if (*s == ' ') continue;
        for (g = 0; g < (int)(sizeof coop_font / sizeof coop_font[0]) && coop_font[g].c != *s; g++) ;
        if (g == (int)(sizeof coop_font / sizeof coop_font[0])) continue;
        for (r = 0; r < 7; r++) for (b = 0; b < 5; b++) if (coop_font[g].r[r] & (16 >> b)) coop_hud_rect(x + b * k, y + r * k, x + (b + 1) * k, y + (r + 1) * k, c); }
    return x;
}
static char *coop_put_int(char *o, int v) { char b[12]; int n = 0; if (v < 0) v = 0; do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11); while (n) *o++ = b[--n]; *o = 0; return o; }
static void coop_hud_redraw(void)
{
    int hp = coop_hud_hp, mx = coop_hud_max > 0 ? coop_hud_max : 1000, w; char t[32];
    float f = hp <= 0 ? 0.0f : hp >= mx ? 1.0f : (float)hp / (float)mx;
    uint32_t bar = coop_hud_down ? 0xFF606060u : f > 0.5f ? 0xFF30C040u : f > 0.25f ? 0xFFE0B020u : 0xFFD03020u;   /* BGRA as 0xAARRGGBB */
    coop_hud_rect(0, 0, COOP_HUD_W, COOP_HUD_H, 0x90000000u);
    coop_hud_text(10, 8, "PLAYER 2", 2, 0xFFFFFFFFu);
    { char *o = coop_put_int(t, hp); *o++ = '/'; coop_put_int(o, mx); } w = (int)strlen(t) * 12;
    coop_hud_text(COOP_HUD_W - 10 - w, 8, t, 2, 0xFFE0E0E0u);
    coop_hud_rect(10, 34, COOP_HUD_W - 10, 70, 0xFFC8C8C8u);
    coop_hud_rect(12, 36, COOP_HUD_W - 12, 68, 0xFF202020u);
    coop_hud_rect(12, 36, 12 + (int)((COOP_HUD_W - 24) * f), 68, bar);
    if (coop_hud_down) { memcpy(t, "DOWN  RESPAWN ", 14); coop_put_int(t + 14, coop_hud_secs); coop_hud_text(18, 45, t, 2, 0xFFFF5050u); }
    coop_hud_ver++;
}
/* each player's Perfect ring, round his gauge's medallion (new: retail shows the meter only through the rating word).
 * The hero record's +4 is the combat meter: good hits raise it (0x9D9D0), it decays every frame (0x9DF70), and the
 * rating is its band (0x9DE10: above [0x175324] Perfect, [0x175320] Excellent, [0x171084] Good, else Fair). Reaching
 * Perfect starts Perfect mode: +0x5C = +0x60 (its time), counted down each frame (damage taken drains it too,
 * 0x9DEC0), the gauge's glow (list 1: 7-9) pulsing meanwhile, and +4 back to 0 when it ends. The ring fills clockwise
 * from the top with +4 toward Perfect, in the rating word's colour of its band; in Perfect mode it shows the time
 * left, pulsing. Player 1's from his record, player 2's from his copy. TT_COOP_NORING off; TT_COOP_RINGR = outer
 * radius (screen units, 25), TT_COOP_RINGY = centre y (398). */
#define COOP_RING_PX 128
static uint32_t coop_ring_px[2][COOP_RING_PX * COOP_RING_PX];
static volatile unsigned coop_ring_ver[2]; static volatile int coop_ring_on[2]; static volatile uint32_t coop_ring_t;   /* (coop_ring_t: the last frame of play that set them) */
static float coop_ring_x[2] = { 81.9f, 558.1f }, coop_ring_y = 398.0f, coop_ring_r = 25.0f;
static uint32_t coop_bgra(uint32_t c) { return (c & 0xFF00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16); }   /* the game's AABBGGRR to the overlay's 0xAARRGGBB */
static void coop_ring_draw(uint32_t *p, float f, uint32_t col, float bright)
{
    const float c = (COOP_RING_PX - 1) * 0.5f, r1 = COOP_RING_PX * 0.5f - 1.5f, r0 = r1 - COOP_RING_PX * 0.13f, pi2 = 6.2831853f;
    int x, y;
    for (y = 0; y < COOP_RING_PX; y++) for (x = 0; x < COOP_RING_PX; x++) {
        float dx = x - c, dy = y - c, d = sqrtf(dx * dx + dy * dy), cov, a; uint32_t v, al, r, g, b;
        cov = d - r0 + 0.5f; if (r1 - d + 0.5f < cov) cov = r1 - d + 0.5f; if (cov > 1) cov = 1;
        if (cov <= 0) { p[y * COOP_RING_PX + x] = 0; continue; }
        a = atan2f(dx, -dy); if (a < 0) a += pi2;
        if (a / pi2 <= f) { v = col; r = (v >> 16) & 0xFF; g = (v >> 8) & 0xFF; b = v & 0xFF;
            r = (uint32_t)(r + (255 - r) * bright); g = (uint32_t)(g + (255 - g) * bright); b = (uint32_t)(b + (255 - b) * bright); al = 0xF0; }
        else { r = g = b = 0x18; al = 0x90; }   /* the empty track */
        if (d < r0 + 1.2f || d > r1 - 1.2f) { r = r * 3 / 5; g = g * 3 / 5; b = b * 3 / 5; }   /* a dark rim */
        al = (uint32_t)(al * cov);
        p[y * COOP_RING_PX + x] = (al << 24) | (r << 16) | (g << 8) | b;
    }
}
/* aim dots: while a hero aims (LT held; the P2 Bot's aim too) at the enemy the game has locked for him (component
 * +0x98), a dot over its head in his colour (player 1 blue, player 2 amber, as the head markers), smaller the further
 * the enemy is from the camera. Both on one enemy: side by side. A HUD element of our own (a copy of the quiver's,
 * with a dot texture built like the quiver's, 64x128 as its: the dot drawn twice as tall, round in the square element) drawn before the game's lists, so under every HUD piece
 * (coop_aimdot_draw). TT_COOP_NOAIMDOT: off; TT_COOP_AIMDOTZ: the height over the enemy's feet. */
#define COOP_DOT_PX 64
#define COOP_DOT_PH 128
static void coop_dot_draw(uint32_t *p)   /* white (the element's colour tints it), a dark rim, straight alpha */
{
    const float c = (COOP_DOT_PX - 1) * 0.5f, r = COOP_DOT_PX * 0.5f - 2.0f;
    int x, y;
    for (y = 0; y < COOP_DOT_PH; y++) for (x = 0; x < COOP_DOT_PX; x++) {
        float dx = x - c, dy = (y - (COOP_DOT_PH - 1) * 0.5f) * 0.5f, d = sqrtf(dx * dx + dy * dy), cov = r - d + 0.5f; uint32_t v = 0xFFFFFFu, al;
        if (cov <= 0) { p[y * COOP_DOT_PX + x] = 0; continue; }
        if (cov > 1) cov = 1;
        if (d > r - 7.0f) v = 0x181818u;   /* (a dark rim: seen on snow and in torchlight) */
        al = (uint32_t)(0xF0 * cov);
        p[y * COOP_DOT_PX + x] = (al << 24) | v;
    }
}
static int coop_overlay(CoopOvlQuad *q, int max);
static void coop_ring_frame(void)   /* once a frame, game side */
{
    static int key[2] = { -1, -1 }, logged; int i; extern int coop_p2rec_ok; extern uint32_t coop_rec_addr;
    float tp = MEMF(0x175324u), te = MEMF(0x175320u), tg = MEMF(0x171084u);
    extern int tt_in_scene(void);
    int show = !getenv("TT_COOP_NORING") && coop_mode() == 3 && coop_p2 >= 0x10000u && !coop_depth && coop_rec_addr && MEM8(0x4A559Cu + 1) && !tt_in_scene();   /* while the game shows player 1's gauge (HUD list 1 on), not in a cutscene (they used to show in them) */
    if (!logged && show) { logged = 1; tt_dbg("[COOPRING] rating bands on +4: Good %.1f Excellent %.1f Perfect %.1f\n", tg, te, tp);
        if (getenv("TT_COOP_RINGR")) coop_ring_r = (float)atof(getenv("TT_COOP_RINGR")); if (getenv("TT_COOP_RINGY")) coop_ring_y = (float)atof(getenv("TT_COOP_RINGY")); }
    if (show && getenv("TT_COOP_TESTPERFECT") && coop_p2_t0) {   /* bot test: "ms1,ms2" = player 1's, player 2's meter set just past Perfect at those times (the game's own step then starts Perfect mode) */
        static int done[2]; const char *v = getenv("TT_COOP_TESTPERFECT"); char *e; uint32_t t1 = (uint32_t)strtoul(v, &e, 10), t2 = *e == ',' ? (uint32_t)strtoul(e + 1, NULL, 10) : t1; float pv = tp + 0.5f;
        if (!done[0] && coop_now() - coop_p2_t0 >= t1) { done[0] = 1; MEMF(coop_rec_addr + 4) = pv; tt_dbg("[COOPRING] test: player 1's meter set to %.1f\n", pv); }
        if (!done[1] && coop_now() - coop_p2_t0 >= t2) { done[1] = 1; memcpy(coop_s2 + 0x400 + 4, &pv, 4); tt_dbg("[COOPRING] test: player 2's meter set to %.1f\n", pv); } }
    for (i = 0; i < 2; i++) {
        uint32_t rec = i ? COOP_P2REC : coop_rec_addr, col; float m, pm, pmax, f, br = 0; int k, perf;
        if (!show || (i && !coop_p2rec_ok)) { coop_ring_on[i] = 0; continue; }
        m = MEMF(rec + 4); pm = MEMF(rec + 0x5C); pmax = MEMF(rec + 0x60); perf = pm > 0;
        if (perf) { f = pmax > 0 ? pm / pmax : 1; col = 0xFF3E6E66u; br = 0.25f + 0.25f * sinf((float)coop_now() * 0.008f); }
        else { f = tp > 0 ? m / tp : 0; col = m > te ? 0xFFC1C5B9u : m > tg ? 0xFF5175A7u : 0xFF383994u; }
        if (f < 0) f = 0; if (f > 1) f = 1;
        k = (perf ? 100000 + (int)(br * 40) * 1000 : (int)(col & 0xFF) * 1000) + (int)(f * 240);
        if (k != key[i]) { key[i] = k; coop_ring_draw(coop_ring_px[i], f, coop_bgra(col), br); coop_ring_ver[i]++; }
        coop_ring_on[i] = 1; coop_ring_t = coop_now() | 1;
        if (getenv("TT_COOP_RINGLOG")) { static int lp[2] = { -1, -1 }; static uint32_t lt[2]; if ((perf != lp[i] || coop_now() - lt[i] > 1000) && (m > 0 || perf)) { lp[i] = perf; lt[i] = coop_now();
            tt_dbg("[COOPRING] P%d t %u: meter %.2f of %.1f, Perfect %.2f of %.2f -> ring %.2f %s\n", i + 1, coop_now() - coop_p2_t0, m, tp, pm, pmax, f, perf ? "PERFECT" : ""); } }
    }
    if (show && !tt_ovl_extra) tt_ovl_extra = coop_overlay;
}
static int coop_overlay(CoopOvlQuad *q, int max)
{
    int n = 0, i;
    if (xbox_MoviePlaying()) return 0;
    if (n < max) n += tt_vs_res_overlay(&q[n]);   /* (Versus: the results' panel, under the rest) */
    for (i = 0; i < 2 && n < max; i++) if (coop_ring_on[i] && coop_now() - coop_ring_t < 250u) {   /* (only while play runs: after a level they stayed on through the stats, shops and menus, a playtest) */
        /* kept to the left / right edge with the gauges (the version's bits 31 / 30, the renderer's ovl_wide_x); with the
         * E3 HUD round the place of its rune orb (82.5, 404), which is left out in co-op (e3hud.c) */
        extern int xbox_GfxGet(int id); int e3 = xbox_GfxGet(27) != 0;
        float cx = e3 ? (i ? 640.0f - 82.5f : 82.5f) : coop_ring_x[i], cy = e3 ? 404.0f : coop_ring_y;
        q[n].px = coop_ring_px[i]; q[n].w = COOP_RING_PX; q[n].h = COOP_RING_PX; q[n].ver = (9100 + i * 100000 + coop_ring_ver[i]) | (i ? 0x40000000u : 0x80000000u);
        q[n].x = cx - coop_ring_r; q[n].y = cy - coop_ring_r; q[n].dw = q[n].dh = 2 * coop_ring_r; n++; }
    if (!coop_hud_show || n >= max) return n;
    q[n].px = coop_hud_px; q[n].w = COOP_HUD_W; q[n].h = COOP_HUD_H; q[n].ver = 9000 + coop_hud_ver;
    q[n].x = 470.0f; q[n].y = 14.0f; q[n].dw = 160.0f; q[n].dh = 40.0f;
    return n + 1;
}
static void coop_hud_frame(void)   /* once a frame, on the game's side: what the panel shows */
{
    extern int coop_hero_down(int); extern uint32_t coop_down_time(void);
    static int inst; int hp, mx, dn, secs = 0;
    if (getenv("TT_COOP_NOHUD") || (!getenv("TT_COOP_NOSWORD") && !getenv("TT_COOP_DEBUGPANEL") && !getenv("TT_COOP_HUDSHOT")) || coop_mode() != 3 || coop_p2 < 0x10000u) { coop_hud_show = 0; if (!getenv("TT_COOP_HUDSHOT")) return; }
    if (!inst) { inst = 1; tt_ovl_extra = coop_overlay; tt_dbg("[COOPHUD] player 2's panel installed\n"); }
    hp = (int32_t)MEM32(coop_p2 + 0x2FC); mx = (int32_t)MEM32(coop_p2 + 0x300); dn = coop_hero_down(1);
    if (dn) { uint32_t wait = coop_respawn_wait(), el = coop_down_time(); secs = el >= wait ? 0 : (int)((wait - el + 999) / 1000); }
    if (hp != coop_hud_hp || mx != coop_hud_max || dn != coop_hud_down || secs != coop_hud_secs || !coop_hud_ver) { coop_hud_hp = hp; coop_hud_max = mx; coop_hud_down = dn; coop_hud_secs = secs; coop_hud_redraw(); }
    coop_hud_show = !getenv("TT_COOP_NOHUD") && (getenv("TT_COOP_NOSWORD") || getenv("TT_COOP_DEBUGPANEL"));   /* the debug panel: only as the sword's fallback or on request */
    {   /* bot test: frame dumps */
        static char done[8]; const char *v = getenv("TT_COOP_HUDSHOT"); int i = 0;
        while (v && *v && i < 8) { char *e; uint32_t ms = (uint32_t)strtoul(v, &e, 10); const char *f = *e == ':' ? e + 1 : NULL; const char *end = f ? strchr(f, ',') : NULL; char path[260];
            if (!f) break;
            if (!done[i] && coop_p2_t0 && coop_now() - coop_p2_t0 >= ms) { size_t L = end ? (size_t)(end - f) : strlen(f); if (L > 250) L = 250; memcpy(path, f, L); path[L] = 0; xbox_PresentDumpRequest(path); done[i] = 1; tt_dbg("[COOPHUD] frame dump %s (P2 health %d, down %d, %d s | P1 %08X health %d state %u, his fill w %d, player %08X)\n", path, hp, dn, secs, coop_first_char, coop_first_char >= 0x10000u ? (int32_t)MEM32(coop_first_char + 0x2FC) : -1, coop_first_char >= 0x10000u ? MEM32(coop_first_char + 0x408) : 0, (int32_t)MEM32(0x4A0398u + 8 * 0xDC + 0x10), MEM32(0x43B15Cu)); }
            if (!end) break; v = end + 1; i++; }
    }
}
/* player 2's health sword and respawn counter, drawn by the game's own HUD code (0x000CF450), as the port's E3 HUD
 * draws its texts: element copies (0xDC bytes: +8 x, +0xC y, +0x10 w, +0x14 h in HUD space, +0x20 colour AABBGGRR,
 * +0x24 shown, +0x2C font, +0x34 text pointer or 0 for UTF-16 at +0x38) in guest memory nobody uses (after the E3
 * HUD's at 0x4BA000), handed to the HUD for one draw through a switched-off slot of its list table (0x1BAAE4,
 * on/off 0x4A559C). Player 1's sword is list 0 (0x4A0398): 0 the blade strip, 7 the red bar, 8 the green fill (171
 * wide at full health), 16 the sword art. Ours: the same four, top right (left of Frodo's portrait), mirrored, the
 * fill from player 2's health; and a font-0 text "RESPAWN n" at the top middle while he is down.
 * TT_COOP_NOSWORD turns it off (the debug panel, TT_COOP_NOHUD, is then the fallback). */
#define COOP_EL      0x4BA600u   /* after the E3 HUD's two (0x4BA000-0x4BA1B7) and menus.c's Exit (0x4BA400, its text 0x4BA500): at 0x4BA1C0 the main menu's Exit, drawn first, made the area look used and player 2's HUD stood down */
#define COOP_EL_N    15   /* 0-3 sword, 4 counter, 5-10 gauge, 11-12 rating popup, 13-14 arrows; ends at 0x4BB2E4 (player 2's record copy is at 0x4BB300) */
#define COOP_HUD_LIST 0x1BAAE4u
#define COOP_HUD_ON   0x4A559Cu
#define COOP_P1SWORD  0x4A0398u
static uint32_t coop_arrow_saved[3]; static int coop_arrow_n;
static uint32_t coop_teeth_el, coop_teeth_saved[2]; static int coop_teeth_slot = -1;
static int coop_sw_slot = -1; static uint32_t coop_sw_saved[2]; static uint8_t coop_pop_saved[2]; static int coop_pop_hid; static uint32_t coop_ally[96][2]; static int coop_ally_n;
static int coop_ammo_dirty = 1, coop_ammo_ok;   /* (player 2's ammo icon: built again for a new level) */
extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment);
static void coop_el_copy(uint32_t dst, uint32_t src) { int i; for (i = 0; i < 0xDC; i += 4) MEM32(dst + i) = MEM32(src + i); }
static void coop_el_text(uint32_t el, const char *t) { int i; for (i = 0; t[i] && i < 40; i++) MEM16(el + 0x38 + 2 * i) = (uint8_t)t[i]; MEM16(el + 0x38 + 2 * i) = 0; MEM32(el + 0x34) = 0; }
/* (research) TT_COOP_HUDLISTDUMP="ms,ms,..." (after player 2's start): every switched-on HUD list and its shown elements */
static void coop_hudlist_dump(void)
{
    static int done; const char *v = getenv("TT_COOP_HUDLISTDUMP"); int i = 0, li; uint32_t j;
    if (!v || !coop_p2_t0) return;
    while (*v && i < 16) { char *e; uint32_t ms = (uint32_t)strtoul(v, &e, 10);
        if (!(done & (1 << i)) && coop_now() - coop_p2_t0 >= ms) { extern uint32_t coop_rec_addr; done |= 1 << i;
            tt_dbg("[COOPHUDL] dump %d at t %u (player 1's rating +4 %.2f)\n", i, coop_now() - coop_p2_t0, coop_rec_addr ? MEMF(coop_rec_addr + 4) : -1.0f);
            for (li = 0; li < 21; li++) { uint32_t tb = MEM32(COOP_HUD_LIST + li * 12), n = MEM32(COOP_HUD_LIST + li * 12 + 4);
                tt_dbg("[COOPHUDL]  list %d table %08X n %u on %d\n", li, tb, n, MEM8(COOP_HUD_ON + li));
                if (li == 0 && tb >= 0x10000u && n > 24) { uint32_t q, el = tb + 24 * 0xDC; tt_dbg("[COOPHUDL]  0.24 whole:"); for (q = 0; q < 0xDC; q += 4) tt_dbg(" %X:%08X", q, MEM32(el + q)); tt_dbg("\n");
                    { uint32_t tx = MEM32(el + 0x40); if (tx >= 0x10000u && tx < 0x84000000u) { tt_dbg("[COOPHUDL]  +40 -> %08X:", tx); for (q = 0; q < 0x40; q += 4) tt_dbg(" %08X", MEM32(tx + q)); tt_dbg("\n"); } } }
                if (!MEM8(COOP_HUD_ON + li) || tb < 0x10000u || n > 64) continue;
                for (j = 0; j < n; j++) { uint32_t el = tb + j * 0xDC; char t[24]; int k; uint32_t tp = MEM32(el + 0x34) >= 0x10000u ? MEM32(el + 0x34) : el + 0x38;
                    for (k = 0; k < 20 && MEM16(tp + 2 * k); k++) t[k] = (char)MEM16(tp + 2 * k); t[k] = 0;
                    tt_dbg("[COOPHUDL]   %d.%u %s %d,%d %dx%d col %08X font %d | 00 %08X 04 %08X 18 %08X 1C %08X 28 %08X 30 %08X | '%s'\n", li, j, MEM8(el + 0x24) ? "ON " : "off",
                           (int32_t)MEM32(el + 8), (int32_t)MEM32(el + 0xC), (int32_t)MEM32(el + 0x10), (int32_t)MEM32(el + 0x14), MEM32(el + 0x20), (int32_t)MEM32(el + 0x2C),
                           MEM32(el), MEM32(el + 4), MEM32(el + 0x18), MEM32(el + 0x1C), MEM32(el + 0x28), MEM32(el + 0x30), t); } } }
        while (*e && *e != ',') e++; if (!*e) break; v = e + 1; i++; }
}
/* the player icons (Co-op Options): "P1" / "P2" above each hero's head. The head (position + TT_COOP_TAGZ, 6.8
 * units up; 2.4 sat at the chest, observed: with 0 the marker lands at the feet) goes through the device's view and projection (Xbox D3D's SetTransform 0x117230 keeps them at
 * [0x124180] + 0x810 + state * 64: 0 view, 1 projection; row vectors) to frame units (640 x 480), then back
 * through the HUD's own placing: y shrunk 0.93 about 240 (observed with the respawn counter) and, wider than 4:3,
 * x narrowed about the middle by the renderer (the markers are kept out of its edge groups, xbox_hud_excl_more).
 * Two elements of their own (the arrows' count, font 0) drawn through a second switched-off HUD slot. */
static uint32_t coop_boss_tb[4]; static int coop_boss_li[4], coop_boss_n;
/* a boss bar: the troll's is list 15, the Watcher's list 19 (observed), each 7 pieces: 0 a strip, 1-2 the health bar,
 * 3 the frame (256x64 near the top), 4 the face (64x64), 5-6 their flash. Told by that shape, not by the list */
static int coop_is_bossbar(int li)
{
    uint32_t tb = MEM32(COOP_HUD_LIST + li * 12), n = MEM32(COOP_HUD_LIST + li * 12 + 4);
    if (li < 2 || li > 20 || !MEM8(COOP_HUD_ON + li) || tb < 0x10000u || n != 7) return 0;
    return MEM32(tb + 3 * 0xDC + 0x10) == 256 && MEM32(tb + 3 * 0xDC + 0x14) == 64 && (int32_t)MEM32(tb + 3 * 0xDC + 0xC) < 40
        && MEM32(tb + 4 * 0xDC + 0x10) == 64 && MEM32(tb + 4 * 0xDC + 0x14) == 64;
}
/* the game's own HUD pieces in the top right (allies' bars, boss bars, counters) stay where the game draws them; player 2's HUD
 * goes under them instead (a playtest: "all coop hud elements should go under all the main hud elements ... moving hud
 * elements is distracting"). TT_COOP_HUDMOVE: moved under player 2's sword, as before */
static int coop_hud_move_on(void) { static int v = -1; if (v < 0) v = getenv("TT_COOP_HUDNOMOVE") == NULL; return v; }   /* (2026-10-26: below player 2's bar
    * again, at fixed offsets -- a playtest screenshot: the enemy counter and Frodo's ring on his bar; the earlier version moved
    * them by 72 or 168 as a boss bar came and went, and that moving was what distracted. TT_COOP_HUDNOMOVE: the game's places) */
static int coop_bossbar_any(void) { int li; if (getenv("TT_COOP_NOBOSSBARMOVE") || !coop_hud_move_on()) return 0; for (li = 2; li <= 20; li++) if (coop_is_bossbar(li)) return 1; return 0; }
static uint32_t coop_tag_el; static int coop_tag_slot = -1; static uint32_t coop_tag_saved[2]; static float coop_proj_z; static int coop_tag_hidden[2];
static int coop_project_old(float wx, float wy, float wz, float *fx, float *fy);
/* the frame position of a world point through the camera the game draws and culls with: SetPerspective's object
 * (0x4B2F80 in play) is a frustum -- +0 near, +4 far, +0xC / +0x10 the frame (640, 480), its four near corners in the
 * world at +0xC8.. and four far ones at +0x108.. (16 bytes each; top left, top right, bottom right, bottom left, as
 * its view-space list at +0x18). The eye from a near and a far corner (similar triangles), the ray to the point
 * through the near rectangle. It follows scripted cameras, blends and shakes, which the player camera's eye and target
 * don't (markers drifted in a playtest). 0 when it has no such frustum or the point is behind */
static int coop_project_frustum(float wx, float wy, float wz, float *fx, float *fy, float *dist)
{
    extern uint32_t coop_persp_obj_get(void);
    uint32_t c = coop_persp_obj_get(); float n, f, N[4][3], F0[3], E[3], C[3], R[3], D[3], nr[3], P[3], H[3], t, a, b, rr, dd; int i, j;
    if (c < 0x10000u || c >= 0x84000000u) return 0;
    n = MEMF(c); f = MEMF(c + 4);
    if (!(n > 0.01f && f > n && MEMF(c + 0xC) == 640.0f && MEMF(c + 0x10) == 480.0f)) return 0;
    for (i = 0; i < 4; i++) for (j = 0; j < 3; j++) N[i][j] = MEMF(c + 0xC8 + 16 * i + 4 * j);
    for (j = 0; j < 3; j++) F0[j] = MEMF(c + 0x108 + 4 * j);
    for (j = 0; j < 3; j++) { E[j] = N[0][j] - n * (F0[j] - N[0][j]) / (f - n); C[j] = 0.5f * (N[0][j] + N[2][j]); R[j] = N[1][j] - N[0][j]; D[j] = N[3][j] - N[0][j]; nr[j] = C[j] - E[j]; }
    P[0] = wx - E[0]; P[1] = wy - E[1]; P[2] = wz - E[2];
    a = nr[0] * nr[0] + nr[1] * nr[1] + nr[2] * nr[2]; b = P[0] * nr[0] + P[1] * nr[1] + P[2] * nr[2];
    if (a < 1e-6f || b <= a * 0.25f) return 0;   /* (behind, or nearer than the near plane's quarter) */
    t = a / b;
    for (j = 0; j < 3; j++) H[j] = E[j] + P[j] * t - N[0][j];
    rr = R[0] * R[0] + R[1] * R[1] + R[2] * R[2]; dd = D[0] * D[0] + D[1] * D[1] + D[2] * D[2];
    if (rr < 1e-8f || dd < 1e-8f) return 0;
    *fx = (H[0] * R[0] + H[1] * R[1] + H[2] * R[2]) / rr * 640.0f;
    *fy = (H[0] * D[0] + H[1] * D[1] + H[2] * D[2]) / dd * 480.0f;
    if (dist) *dist = b / sqrtf(a);   /* (along the view direction) */
    return 1;
}
static int coop_project(float wx, float wy, float wz, float *fx, float *fy)
{
    if (!getenv("TT_COOP_OLDPROJECT")) {   /* (the frustum, compared with the old way below while TT_COOP_PROJLOG) */
        float x2, y2, z2;
        if (coop_project_frustum(wx, wy, wz, &x2, &y2, &z2)) {
            if (getenv("TT_COOP_PROJLOG")) { static uint32_t lt; if (coop_now() - lt > 1000u) { float x1 = -1, y1 = -1; int ok; lt = coop_now();
                ok = coop_project_old(wx, wy, wz, &x1, &y1); tt_dbg("[PROJ] frustum %.1f,%.1f (%.1f away) | eye/target way %s %.1f,%.1f (%.1f away)\n", x2, y2, z2, ok ? "" : "(none)", x1, y1, coop_proj_z); } }
            *fx = x2; *fy = y2; coop_proj_z = z2; return 1;
        }
    }
    return coop_project_old(wx, wy, wz, fx, fy);
}
static int coop_project_old(float wx, float wy, float wz, float *fx, float *fy)
{
    /* the device's view is identity (observed): the game's camera [0x43B164] has what it looks at at +0x40 and its eye
     * at +0x50 (a previous frame's pair at +0x64 / +0x74); the view is built from them, Z up. The projection's scales
     * (device +0x850: [0] x, [5] y; 45 degrees, the window's aspect) carry the co-op zoom. TT_COOP_TAGFLIP mirrors x. */
    uint32_t dev = MEM32(0x124180u), cm = MEM32(0x43B164u); float e[3], f[3], r[3], u[3], d[3], n, x, y, z, sx, sy; int i;
    if (getenv("TT_COOP_CAMOBJLOG")) {   /* (research) the shot's camera (SetPerspective's object) against the player camera [0x43B164] */
        extern uint32_t coop_persp_obj_get(void); static uint32_t lt; uint32_t so = coop_persp_obj_get();
        if (coop_now() - lt > 1000u) { lt = coop_now();
            tt_dbg("[CAMOBJ] player camera %08X eye %.1f %.1f %.1f target %.1f %.1f %.1f | shot camera %08X%s eye %.1f %.1f %.1f target %.1f %.1f %.1f\n", cm,
                   MEMF(cm + 0x50), MEMF(cm + 0x54), MEMF(cm + 0x58), MEMF(cm + 0x40), MEMF(cm + 0x44), MEMF(cm + 0x48), so, so == cm ? " (same)" : "",
                   so >= 0x10000u ? MEMF(so + 0x50) : 0, so >= 0x10000u ? MEMF(so + 0x54) : 0, so >= 0x10000u ? MEMF(so + 0x58) : 0,
                   so >= 0x10000u ? MEMF(so + 0x40) : 0, so >= 0x10000u ? MEMF(so + 0x44) : 0, so >= 0x10000u ? MEMF(so + 0x48) : 0);
            if (so >= 0x10000u) { int q; tt_dbg("[CAMOBJ] shot camera floats:"); for (q = 0x98; q < 0x200; q += 16) tt_dbg(" | %X: %.2f %.2f %.2f %.2f", q, MEMF(so + q), MEMF(so + q + 4), MEMF(so + q + 8), MEMF(so + q + 12)); tt_dbg("\n"); } }
    }
    if (dev < 0x10000u || cm < 0x10000u) return 0;
    for (i = 0; i < 3; i++) { e[i] = MEMF(cm + 0x50 + 4 * i); f[i] = MEMF(cm + 0x40 + 4 * i) - e[i]; }
    n = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]); if (n < 1e-4f) return 0;
    for (i = 0; i < 3; i++) f[i] /= n;
    r[0] = f[1]; r[1] = -f[0]; r[2] = 0.0f;   /* forward x up(0,0,1): to the right */
    n = sqrtf(r[0] * r[0] + r[1] * r[1]); if (n < 1e-4f) return 0;
    r[0] /= n; r[1] /= n;
    u[0] = r[1] * f[2] - r[2] * f[1]; u[1] = r[2] * f[0] - r[0] * f[2]; u[2] = r[0] * f[1] - r[1] * f[0];   /* right x forward: up */
    d[0] = wx - e[0]; d[1] = wy - e[1]; d[2] = wz - e[2];
    x = d[0] * r[0] + d[1] * r[1] + d[2] * r[2]; y = d[0] * u[0] + d[1] * u[1] + d[2] * u[2]; z = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
    if (z < 0.5f) return 0;
    sx = MEMF(dev + 0x850); sy = MEMF(dev + 0x850 + 20); if (sx <= 0.0f || sy <= 0.0f) return 0;
    if (getenv("TT_COOP_TAGFLIP")) x = -x;
    *fx = (x * sx / z + 1.0f) * 320.0f; *fy = (1.0f - y * sy / z) * 240.0f;
    coop_proj_z = z;
    return 1;
}
/* head markers behind walls: the renderer reads the frame's depth at each head (xbox_zprobe_*, nv2a_d3d11.inc;
 * two frames late); the depth as a view distance through the projection's z terms (device +0x850: [10], [14]); a
 * marker is hidden once something is drawn more than 1.5 units in front of its hero's head for 4 results running,
 * shown again after 2 clear ones. TT_COOP_NOTAGHIDE: always shown; TT_COOP_ZPROBELOG: the numbers, and a test
 * probe 4 units under player 1's feet (the ground must hide it) */
extern volatile float xbox_zprobe_req[4][2]; extern volatile int xbox_zprobe_n; extern volatile float xbox_zprobe_res[4]; extern volatile unsigned xbox_zprobe_seq;
static float coop_depth_dist(float d)
{
    uint32_t dev = MEM32(0x124180u); float p10, p14;
    if (dev < 0x10000u || d >= 0.99999f) return 1e9f;
    p10 = MEMF(dev + 0x850 + 40); p14 = MEMF(dev + 0x850 + 56);
    if (fabsf(d - p10) < 1e-7f) return 1e9f;
    return p14 / (d - p10);
}
/* one HUD element drawn now (0x000C8940, thiscall on the element; it skips one not shown): called before the game's
 * lists (tt_coop_hud_pre runs ahead of 0x000CF450's loop), so it lies under every HUD piece */
static void coop_el_draw(uint32_t el)
{
    uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx, r3 = ebx, r4 = esi, r5 = edi;
    if (!MEM8(el + 0x24)) return;
    ecx = el; PUSH32(esp, 0x000CF48Cu); sub_000C8940();
    esp = sp; eax = r0; ecx = r1; edx = r2; ebx = r3; esi = r4; edi = r5;
}
static void coop_aimdot_draw(uint32_t quiver)   /* in tt_coop_hud_pre, before the game's lists */
{
    extern int tt_in_scene(void); extern uint32_t coop_bot_aim_until; extern int coop_hero_down(int);
    extern void xbox_PresentMap(float m[5]); extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment);
    static uint32_t tex, el, last_t[2], last_e[2]; static int failed;
    float pm[5], kk, hx[2], hy[2], sz[2]; uint32_t tg[2] = { 0, 0 }; int w, on[2] = { 0, 0 };
    if (getenv("TT_COOP_NOAIMDOT") || failed || coop_mode() != 3 || coop_p2 < 0x10000u || coop_first_char < 0x10000u || coop_gameover || tt_in_scene()) return;
    if (!tex) {   /* the texture object {vtable, 0, 0, 0, data}: data = a 0x80 header (the D3D texture at +0x2C), then swizzled ARGB */
        uint32_t so = MEM32(quiver + 0xB8), sd = so >= 0x10000u ? MEM32(so + 0x10) : 0, nd, f, i; static uint32_t px[COOP_DOT_PX * COOP_DOT_PH];
        if (sd < 0x10000u || MEM32(so) != 0x1709C4u) { failed = 1; tt_dbg("[AIMDOT] the quiver's texture object isn't as expected (%08X): no dots\n", so); return; }
        tex = xbox_ContiguousAlloc(0x80 + 0x80 + COOP_DOT_PX * COOP_DOT_PH * 4, 0x80u); el = xbox_ContiguousAlloc(2 * 0xDC, 16u);
        if (!tex || !el) { failed = 1; return; }
        nd = tex + 0x80;   /* (the object at tex, its data 0x80 on, the pixels 0x80 after that, as the quiver's) */
        for (i = 0; i < 0x14; i += 4) MEM32(tex + i) = MEM32(so + i);
        MEM32(tex + 0x10) = nd;
        for (i = 0; i < 0x80; i += 4) MEM32(nd + i) = MEM32(sd + i);
        MEM32(nd + 0x2C + 4) = MEM32(sd + 0x2C + 4) - (sd + 0x80) + (nd + 0x80);   /* the D3D texture's data: the same way from its pixels */
        f = MEM32(nd + 0x2C + 0xC);   /* (the quiver's format kept: a 64x64 one drew as the top of a 64x128, observed) */
        if (((f >> 20) & 0xF) != 6 || ((f >> 24) & 0xF) != 7) { failed = 1; tt_dbg("[AIMDOT] the quiver's texture isn't 64x128 (format %08X): no dots\n", f); return; }
        coop_dot_draw(px);
        for (i = 0; i < COOP_DOT_PX * COOP_DOT_PH; i++) {   /* swizzled: x's bits in the even places, y's in the odd, y's last on top */
            uint32_t x = i % COOP_DOT_PX, y = i / COOP_DOT_PX, o = (y >> 6) << 12, b;
            for (b = 0; b < 6; b++) o |= ((x >> b) & 1u) << (2 * b) | ((y >> b) & 1u) << (2 * b + 1);
            MEM32(nd + 0x80 + 4 * o) = px[i];
        }
        tt_dbg("[AIMDOT] dot texture built at %08X (from the quiver's %08X: data %08X, D3D data %08X, format %08X)\n", tex, so, sd, MEM32(sd + 0x2C + 4), f);
    }
    for (w = 0; w < 2; w++) {   /* no hero keeps a lock on an ally (the other hero, the fellowship: +0x114 2): cleared (0x00053B30 on the component).
                                  * A playtest: the targeting picked allies. TT_COOP_LOCKALLIES: as before */
        uint32_t h = w ? coop_p2 : coop_first_char, c = MEM32(h + 0x494), t = c >= 0x10000u ? MEM32(c + 0x98) : 0;
        extern uint32_t tt_gcall(uint32_t va, uint32_t thisptr, int n, const uint32_t *args);
        if (getenv("TT_COOP_TESTP2LOCK") && w) { uint32_t i;   /* bot test: player 2's lock held on the living character whose name starts so (e.g. TrollBoss) */
            for (i = 0; i < coop_nchars; i++) { uint32_t a = coop_chars[i], hn = MEM32(a + 0x18); if (hn >= 0x10000u && hn < 0x84000000u && !_strnicmp((const char *)XBOX_PTR(hn), getenv("TT_COOP_TESTP2LOCK"), strlen(getenv("TT_COOP_TESTP2LOCK"))) && (int32_t)MEM32(a + 0x2FC) > 0) { if (c >= 0x10000u) MEM32(c + 0x98) = a; t = a; break; } } }
        if (t >= 0x10000u && t < 0x84000000u && MEM32(t) == 0x1741C0u && (t == coop_p2 || t == coop_first_char || MEM32(t + 0x114) == 2) && c >= 0x10000u && !tt_vs_on() && !getenv("TT_COOP_LOCKALLIES")) {   /* (not in Versus) */
            tt_gcall(0x00053B30u, c, 0, NULL);
            { static uint32_t lt; if (coop_now() - lt > 1000u) { lt = coop_now(); tt_dbg("[COOPAIM] P%d's lock on an ally (%08X %s): cleared, now %08X\n", w + 1, t, coop_nm(t), MEM32(c + 0x98)); } }
        }
    }
    xbox_PresentMap(pm); kk = pm[4] > 1.0f ? pm[4] : 1.0f;
    for (w = 0; w < 2; w++) {
        uint32_t h = w ? coop_p2 : coop_first_char, c = MEM32(h + 0x494), t = c >= 0x10000u ? MEM32(c + 0x98) : 0, now = coop_now(), cm = MEM32(0x43B164u);
        uint32_t port = w ? 1u : MEM32(0x1CB8B4u); int aim, ok; float fx, fy, z, d = 20.0f;
        aim = (port < 4 && (MEM32(COOP_PADREC(port) + 0x34u) & 0x4000u)) || (w && now < coop_bot_aim_until);   /* (LT: bit 0x4000 of the record's held buttons, observed) */
        if (coop_hero_down(w)) aim = 0;
        ok = t >= 0x10000u && t < 0x84000000u && MEM32(t) == 0x1741C0u && MEM32(t + 0x114) == 3 && (int32_t)MEM32(t + 0x2FC) > 0;
        if (ok) { last_e[w] = t; last_t[w] = now; }
        else if (aim && last_e[w] && now - last_t[w] < 400u && (int32_t)MEM32(last_e[w] + 0x2FC) > 0) t = last_e[w];   /* (the lock drops to 0 for a moment now and then while aiming, observed) */
        else continue;
        if (!aim) continue;
        z = coop_boss_kind(t) == 3 ? 9.0f : 4.4f;   /* (its head, as the markers: the dot is lifted in frame units below. A height above the head, 6 / 10 before, put it off the enemy with the camera zoomed out, a playtest) */
        if (getenv("TT_COOP_AIMDOTZ")) z = (float)atof(getenv("TT_COOP_AIMDOTZ"));
        if (!coop_project(MEMF(t + 0xF0), MEMF(t + 0xF4), MEMF(t + 0xF8) + z, &fx, &fy)) continue;
        if (fx < 8.0f || fx > 632.0f || fy < 8.0f || fy > 472.0f) continue;
        if (cm >= 0x10000u) { float ex = MEMF(t + 0xF0) - MEMF(cm + 0x50), ey = MEMF(t + 0xF4) - MEMF(cm + 0x54), ez = MEMF(t + 0xF8) + z - MEMF(cm + 0x58); d = sqrtf(ex * ex + ey * ey + ez * ez); }
        sz[w] = 18.0f * 20.0f / (d > 1.0f ? d : 1.0f);   /* (18 HUD units at 20 units from the camera) */
        if (sz[w] < 6.0f) sz[w] = 6.0f; if (sz[w] > 28.0f) sz[w] = 28.0f;
        fy -= getenv("TT_COOP_AIMDOTLIFT") ? (float)atof(getenv("TT_COOP_AIMDOTLIFT")) : 10.0f; hx[w] = 320.0f + (fx - 320.0f) * kk; hy[w] = 240.0f + (fy - 240.0f) / 0.93f; tg[w] = t; on[w] = 1;
        if (getenv("TT_COOP_AIMDOTLOG")) { static uint32_t lt[2]; if (now - lt[w] > 500u) { lt[w] = now; tt_dbg("[AIMDOT] P%d aiming at %08X (%s), %.1f from the camera: dot %.0f at %.0f,%.0f%s\n", w + 1, t, coop_nm(t), d, sz[w], hx[w], hy[w], ok ? "" : " (the last lock)"); } }
    }
    if (on[0] && on[1] && tg[0] == tg[1]) { float g = (sz[0] + sz[1]) * 0.3f; hx[0] -= g; hx[1] += g; }   /* one enemy: side by side */
    for (w = 0; w < 2; w++) {
        uint32_t e = el + w * 0xDC;
        if (!on[w]) { MEM8(e + 0x24) = 0; continue; }
        coop_el_copy(e, quiver);
        MEM32(e + 0xB8) = tex;   /* (+0x18 / +0x1C the quiver's 64x128) */
        MEM32(e + 8) = (uint32_t)(int32_t)(hx[w] - sz[w] * 0.5f); MEM32(e + 0xC) = (uint32_t)(int32_t)(hy[w] - sz[w] * 0.5f);
        MEM32(e + 0x10) = (uint32_t)(int32_t)(sz[w] + 0.5f); MEM32(e + 0x14) = (uint32_t)(int32_t)(sz[w] + 0.5f);
        MEM32(e + 0x20) = w ? 0xFF30C0FFu : 0xFFFFA040u;   /* (ABGR, the markers' colours) player 1 blue, player 2 amber */
        MEM8(e + 0x24) = 1;
        {   /* out of the widescreen HUD's groups, as the head markers (boxes 2, 3): a dot near player 1's sword or a gauge joined its
             * group and moved it (a playtest). The renderer sees frame x and y: the HUD's 0.93 about 240 in y and about 0.92 about 320 in
             * x (a piece of the dot seen at 195.0 for HUD x 184.7, 175.0 for 162.3: RECOMP_HUDEDGELOG). With the box at the HUD x a dot
             * away from the middle fell out of its box, made a group of its own and was moved with the edges: off its enemy by a
             * third of the way to the edge (a playtest: 543 px right of the middle for an orc 305 px right) */
            extern volatile float xbox_hud_excl_more[4][5]; float r = sz[w] * 0.5f + 8.0f, fyc = 240.0f + (hy[w] - 240.0f) * 0.93f, fxc = 320.0f + (hx[w] - 320.0f) * 0.92f;
            xbox_hud_excl_more[2 + w][0] = fxc - r; xbox_hud_excl_more[2 + w][1] = fxc + r; xbox_hud_excl_more[2 + w][2] = fyc - r; xbox_hud_excl_more[2 + w][3] = fyc + r; xbox_hud_excl_more[2 + w][4] = 1.0f; }
        coop_el_draw(e);
    }
}
static void coop_tags(uint32_t tmpl)
{
    extern int tt_coop_setting(int); extern int coop_hero_down(int); extern volatile float xbox_hud_excl_more[4][5];
    extern void xbox_PresentMap(float m[5]); extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment);
    float pm[5], kk, z = getenv("TT_COOP_TAGZ") ? (float)atof(getenv("TT_COOP_TAGZ")) : 6.8f; int w, any = 0, k;
    if (getenv("TT_COOP_TAGLOG")) { static int n; if (n++ < 3) tt_dbg("[COOPTAG] icons setting %d, game over %d\n", tt_coop_setting(1), coop_gameover); }
    xbox_zprobe_n = 0;   /* (the depth probes only while markers are drawn: set again below) */
    if (!tt_coop_setting(1) || coop_gameover || getenv("TT_COOP_NOTAGS")) return;
    if (!coop_tag_el) { coop_tag_el = xbox_ContiguousAlloc(2 * 0xDC, 16u); if (!coop_tag_el) return; }
    xbox_PresentMap(pm); kk = pm[4] > 1.0f ? pm[4] : 1.0f;
    if (getenv("TT_COOP_ZPROBELOG")) {   /* (research) both markers' state each half second, timed from player 2's take-over (to match frames) */
        static uint32_t lt; if (coop_p2_t0 && coop_now() - lt >= 500u) { lt = coop_now(); tt_dbg("[ZPROBE] t %u ms: P1 %s, P2 %s\n", coop_now() - coop_p2_t0, coop_tag_hidden[0] ? "hidden" : "shown", coop_tag_hidden[1] ? "hidden" : "shown"); }
    }
    if (getenv("TT_COOP_ZPROBELOG") && coop_first_char >= 0x10000u) {   /* (research) the self-test probe: 4 units under player 1's feet, behind the ground */
        float px, py; static unsigned ls; static uint32_t lt; uint32_t a = coop_first_char;
        if (coop_project(MEMF(a + 0xF0), MEMF(a + 0xF4), MEMF(a + 0xF8) - 4.0f, &px, &py)) {
            float zu = coop_proj_z; xbox_zprobe_req[2][0] = px; xbox_zprobe_req[2][1] = py; xbox_zprobe_n = 3;
            if (xbox_zprobe_seq != ls && coop_now() - lt > 1000u) { float zd = coop_depth_dist(xbox_zprobe_res[2]); ls = xbox_zprobe_seq; lt = coop_now();
                tt_dbg("[ZPROBE] test: 4 under player 1's feet at %.0f,%.0f: %.1f away; depth there %.6f = %.1f away: %s\n", px, py, zu, xbox_zprobe_res[2], zd, zd < zu - 1.5f ? "hidden by the ground (right)" : "NOT hidden"); }
        }
    }
    for (w = 0; w < 2; w++) {
        uint32_t a = w ? coop_p2 : coop_first_char, e = coop_tag_el + (uint32_t)w * 0xDC; float fx, fy, hx, hy; char t[4] = { 'P', (char)('1' + w), 0, 0 }; int dimw = 0;
        coop_el_copy(e, tmpl); MEM8(e + 0x24) = 0;
        if (getenv("TT_COOP_TAGLOG")) { static uint32_t lt; if (!w && coop_now() - lt > 3000) { uint32_t dev = MEM32(0x124180u); int q; lt = coop_now();
            tt_dbg("[COOPTAG] device %08X view:", dev); if (dev >= 0x10000u) for (q = 0; q < 16; q++) tt_dbg(" %.3f", MEMF(dev + 0x810 + 4 * q));
            tt_dbg(" | proj:"); if (dev >= 0x10000u) for (q = 0; q < 16; q++) tt_dbg(" %.3f", MEMF(dev + 0x850 + 4 * q)); tt_dbg("\n");
            {   uint32_t cm = MEM32(0x43B164u); if (cm >= 0x10000u) { tt_dbg("[COOPTAG] camera %08X:", cm); for (q = 0; q < 64; q++) tt_dbg(" %X:%.2f", q * 4, MEMF(cm + 4 * q)); tt_dbg("\n"); }
                if (a >= 0x10000u) tt_dbg("[COOPTAG] P1 at %.2f %.2f %.2f\n", MEMF(a + 0xF0), MEMF(a + 0xF4), MEMF(a + 0xF8)); }
            if (a >= 0x10000u) { float fx = -1, fy = -1; int ok = coop_project(MEMF(a + 0xF0), MEMF(a + 0xF4), MEMF(a + 0xF8) + z, &fx, &fy); tt_dbg("[COOPTAG] P1 projects: %d -> %.1f %.1f\n", ok, fx, fy); } } }
        {   /* (2026-10-20) the top of the hero's own head, then a fixed lift on screen: a fixed 6.8 units above the feet
             * floated far over a hero near the camera and over short Gimli (a playtest: "icons wrong when zoomed out"; the
             * projection itself is right: TT_COOP_TAGZ=0 lands on the feet, zoomed or not). TT_COOP_TAGZ: the old way.
             * TT_COOP_TAGLIFT: the lift in frame units (480 high), default 14. */
            extern const char *tt_coop_p2_char_now; const char *who = w ? tt_coop_p2_char_now : NULL; float hh, lift;
            if (!w) { int hb = MEM8(0x4AB69Du); who = hb == 3 ? "gimli" : "tall"; }
            hh = !who ? 5.6f : !strcmp(who, "gimli") ? 3.6f : (!strcmp(who, "frodo") || !strcmp(who, "sam") || !strcmp(who, "merry") || !strcmp(who, "pippin")) ? 3.4f : 5.6f;   /* (tall heroes 4.6 sat at Legolas' chin; hobbits: a guess) */
            lift = getenv("TT_COOP_TAGLIFT") ? (float)atof(getenv("TT_COOP_TAGLIFT")) : 14.0f;
            if (getenv("TT_COOP_TAGZ")) { hh = z; lift = 0.0f; }
            if (a < 0x10000u || !coop_project(MEMF(a + 0xF0), MEMF(a + 0xF4), MEMF(a + 0xF8) + hh, &fx, &fy)) continue;
            {   /* behind a wall? (the probe at the middle of the head, its result from two frames before) */
                static int hidden[2], occ[2], clr[2]; static unsigned lseq[2]; float px, py, zh;
                if (coop_project(MEMF(a + 0xF0), MEMF(a + 0xF4), MEMF(a + 0xF8) + hh * 0.75f, &px, &py)) {
                    zh = coop_proj_z; xbox_zprobe_req[w][0] = px; xbox_zprobe_req[w][1] = py; if (xbox_zprobe_n < 2) xbox_zprobe_n = 2;
                    if (xbox_zprobe_seq != lseq[w]) { float zd = coop_depth_dist(xbox_zprobe_res[w]); int o = zd < zh - 3.0f; lseq[w] = xbox_zprobe_seq;   /* (3 units: 1.5 caught Gimli's own helmet and raised axe) */
                        if (o) { clr[w] = 0; if (++occ[w] >= 8) hidden[w] = 1; } else { occ[w] = 0; if (++clr[w] >= 3) hidden[w] = 0; }
                        coop_tag_hidden[w] = hidden[w];
                        if (getenv("TT_COOP_ZPROBELOG")) { static uint32_t lt[2]; if (coop_now() - lt[w] > 1000u) { lt[w] = coop_now();
                            tt_dbg("[ZPROBE] P%d head at %.0f,%.0f: %.1f away; depth there %.6f = %.1f away: %s%s\n", w + 1, px, py, zh, xbox_zprobe_res[w], zd, o ? "something in front" : "clear", hidden[w] ? " (hidden)" : ""); } } }
                }
                if (hidden[w] && !getenv("TT_COOP_NOTAGHIDE")) { static uint32_t lt; if (coop_now() - lt > 5000u) { lt = coop_now(); tt_dbg("[COOPTAG] P%d's marker %s: behind something\n", w + 1, getenv("TT_COOP_TAGHIDE") ? "hidden" : "dimmed"); }
                    if (getenv("TT_COOP_TAGHIDE")) continue;
                    dimw = 1; }   /* (dimmed, not hidden, by default: depth can't tell a wall from an orc passing in front, and a
                                   * hidden marker in a crowded fight loses the hero just when he is needed) */
            }
            fy -= lift; if (fy < 14.0f && fy > -40.0f) fy = 14.0f;
        }
        if (fx < 8.0f || fx > 632.0f || fy < 8.0f || fy > 472.0f) continue;
        if (fy < 124.0f && !getenv("TT_COOP_TAGHIGH")) fy = 124.0f;   /* (1.9.7) kept under the swords: a marker up among them took the pieces near it out of
                                                                          * their edge group (its exclusion box), and in widescreen part of a sword's strip was drawn beside it */
        hx = 320.0f + (fx - 320.0f) * kk; hy = 240.0f + (fy - 240.0f) / 0.93f;
        MEM32(e + 8) = (uint32_t)(int32_t)(hx - 30.0f); MEM32(e + 0xC) = (uint32_t)(int32_t)(hy - 26.0f); MEM32(e + 0x10) = 60; MEM32(e + 0x14) = 30;
        MEM32(e + 0x20) = w ? 0xFF30C0FFu : 0xFFFFA040u;   /* (ABGR) player 1 blue, player 2 amber */
        if (tt_vs_on()) MEM32(e + 0x20) = w ? 0xFF1888FFu : 0xFF2828FFu;   /* (1.9.7, Versus: player 1 red, player 2 orange -- not co-op's) */
        if (coop_hero_down(w)) MEM32(e + 0x20) = 0xFF909090u;
        if (w && getenv("TT_COOP_TAGDIMTEST")) dimw = 1;   /* (bot test: player 2's marker always dimmed, to see it) */
        if (dimw) MEM32(e + 0x20) = (MEM32(e + 0x20) & 0x00FFFFFFu) | 0x50000000u;   /* behind something: a third */
        coop_el_text(e, t); MEM8(e + 0x24) = 1; any = 1;
        xbox_hud_excl_more[w][0] = hx - 34.0f; xbox_hud_excl_more[w][1] = hx + 34.0f;   /* (the renderer sees the HUD x, before its narrowing: a box at the screen x let a letter fall in an edge group, "P" and "1" apart, observed) */ xbox_hud_excl_more[w][2] = fy - 30.0f; xbox_hud_excl_more[w][3] = fy + 10.0f; xbox_hud_excl_more[w][4] = 1.0f;
        if (getenv("TT_COOP_TAGLOG")) { static uint32_t lt[2]; if (coop_now() - lt[w] > (uint32_t)atoi(getenv("TT_COOP_TAGLOG")) * 1u) { lt[w] = coop_now(); tt_dbg("[COOPTAG] P%d head %.1f %.1f %.1f -> frame %.0f,%.0f -> HUD %.0f,%.0f (wide x%.2f)\n", w + 1, MEMF(a + 0xF0), MEMF(a + 0xF4), MEMF(a + 0xF8) + z, fx, fy, hx, hy, kk); } }
    }
    if (!any) return;
    if (!getenv("TT_COOP_TAGSOVER")) { coop_el_draw(coop_tag_el); coop_el_draw(coop_tag_el + 0xDC); return; }   /* drawn now, before the game's lists: under every HUD piece (a playtest) */
    for (k = 20; k >= 0; k--) if (!MEM8(COOP_HUD_ON + k) && k != coop_sw_slot) {
        coop_tag_slot = k; coop_tag_saved[0] = MEM32(COOP_HUD_LIST + k * 12); coop_tag_saved[1] = MEM32(COOP_HUD_LIST + k * 12 + 4);
        MEM32(COOP_HUD_LIST + k * 12) = coop_tag_el; MEM32(COOP_HUD_LIST + k * 12 + 4) = 2; MEM8(COOP_HUD_ON + k) = 1;
        break;
    }
}
/* ---- (Versus) the match's results: a panel (the overlay, coop_overlay) and its lines in the game's font (HUD text
 * elements as player 2's counter, drawn through a switched-off HUD slot as the head markers) while the match is won
 * (vs_state 2). Two awards picked from those the match earned, a different pair each match */
#define VS_RES_N 40
static uint32_t vs_res_el; static int vs_res_slot = -1; static uint32_t vs_res_saved[2]; static int vs_res_on;
static uint32_t vs_res_px[160 * 100]; static unsigned vs_res_ver;
extern int tt_vs_results_shown(void);
static void vs_res_line(int *n, uint32_t tmpl, int x, int y, int w, const char *t, uint32_t col)
{
    uint32_t e;
    if (*n >= VS_RES_N) return;
    e = vs_res_el + (uint32_t)*n * 0xDC; (*n)++;
    coop_el_copy(e, tmpl);
    MEM32(e + 8) = (uint32_t)x; MEM32(e + 0xC) = (uint32_t)y; MEM32(e + 0x10) = (uint32_t)w; MEM32(e + 0x14) = 24; MEM32(e + 0x20) = col;
    coop_el_text(e, t); MEM8(e + 0x24) = 1;
}
static void vs_res_box(int *n, uint32_t btmpl, int x, int y, int w, int h, uint32_t col)   /* a plain colour box (the health bar's element) */
{
    uint32_t e;
    if (*n >= VS_RES_N) return;
    e = vs_res_el + (uint32_t)*n * 0xDC; (*n)++;
    coop_el_copy(e, btmpl);
    MEM32(e + 8) = (uint32_t)x; MEM32(e + 0xC) = (uint32_t)y; MEM32(e + 0x10) = (uint32_t)w; MEM32(e + 0x14) = (uint32_t)h; MEM32(e + 0x20) = col; MEM8(e + 0x24) = 1;
}
/* the results' picture as a texture the HUD draws: a copy of a HUD texture's object and its data's 0x80 header, made
 * 512 x 256 swizzled A8R8G8B8 (format 0x06, mip 1, log2 sizes 9 / 8), its pixels' physical address at +0x30 (as
 * options.c's Co-op menu background) */
static uint32_t vs_res_texture(uint32_t tel)
{
    extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment); extern int tt_vs_image_pixels(const char *file, uint32_t *out, float k);
    static uint32_t obj; static int state;
    uint32_t so = MEM32(tel + 0xB8), sd = so >= 0x80000000u && so < 0x84000000u ? MEM32(so + 0x10) : 0, blk, k;
    static uint32_t px[512 * 256];
    if (state) return state > 0 ? obj : 0;
    if (sd < 0x80000000u || sd >= 0x84000000u || getenv("TT_VS_NORESPIC")) return 0;
    state = -1;
    tt_dbg("[VS] results picture: template texture %08X data %08X: +4 %u +8 %u +30 %08X +38 %08X +3C %08X\n", so, sd, MEM32(sd + 4), MEM32(sd + 8), MEM32(sd + 0x30), MEM32(sd + 0x38), MEM32(sd + 0x3C));
    { char b[900]; int q = 0; for (k = 0; k < 0xDC && q < 860; k += 4) q += snprintf(b + q, sizeof b - q, " %X:%08X", k, MEM32(tel + k)); tt_dbg("[VS] template element:%s\n", b); }
    if (!tt_vs_image_pixels("results_bg.bmp", px, 1.0f)) return 0;
    blk = xbox_ContiguousAlloc(0x80u + sizeof px, 4096u); obj = xbox_ContiguousAlloc(0x40u, 16u);
    if (!blk || !obj) return 0;
    for (k = 0; k < 0x80u; k += 4) MEM32(blk + k) = MEM32(sd + k);
    MEM32(blk + 4) = 512; MEM32(blk + 8) = 256;
    MEM32(blk + 0x38) = (MEM32(sd + 0x38) & 0xFFu) | 0x0600u | (1u << 16) | (9u << 20) | (8u << 24);
    MEM32(blk + 0x3C) = 0;
    MEM32(blk + 0x30) = (blk + 0x80u) & 0x03FFFFFFu;
    for (k = 0; k < 512u * 256u; k++) MEM32(blk + 0x80u + 4 * k) = px[k];
    for (k = 0; k < 0x20u; k += 4) MEM32(obj + k) = MEM32(so + k);
    MEM32(obj + 0x10) = blk;
    state = 1;
    return obj;
}
static void vs_results_hud(uint32_t tmpl)
{
    extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment); extern volatile float xbox_hud_excl_more[4][5];
    extern VsStats tt_vs_st[2]; extern int tt_vs_res_win, tt_vs_res_score[2], tt_vs_res_award[2][2];
    static const char *aw[] = { "SHARPSHOOTER", "HEAVY HITTER", "IRON WALL", "SPEED DEMON", "BRAWLER", "RELENTLESS" };
    static const char *awd[] = { "best aim with the bow", "the biggest single hit", "the least damage taken", "the fastest round won", "the most blows landed", "the most damage dealt" };
    int n = 0, i, k; char t[48];
    vs_res_on = tt_vs_results_shown();
    if (!vs_res_on) return;
    if (!tt_ovl_extra) tt_ovl_extra = coop_overlay;
    if (!vs_res_el) { vs_res_el = xbox_ContiguousAlloc(VS_RES_N * 0xDC, 16u); if (!vs_res_el) { vs_res_on = 0; return; } }
    snprintf(t, sizeof t, "PLAYER %d WINS  %d - %d", tt_vs_res_win + 1, tt_vs_res_score[tt_vs_res_win], tt_vs_res_score[tt_vs_res_win ^ 1]);
    {   uint32_t bt = COOP_P1SWORD + 7 * 0xDC;   /* the panel: the picture (versus\results_bg.bmp) or dark, a gold edge, a line under the title */
        uint32_t tx = vs_res_texture(COOP_P1SWORD + 16 * 0xDC);
        if (tx) { uint32_t e = vs_res_el; coop_el_copy(e, COOP_P1SWORD + 16 * 0xDC); n++;
            MEM32(e + 8) = 90; MEM32(e + 0xC) = 90; MEM32(e + 0x10) = 460; MEM32(e + 0x14) = 330; MEM32(e + 0x18) = 512; MEM32(e + 0x1C) = 256; MEM32(e + 0x20) = 0xFFFFFFFFu; MEM32(e + 0xB8) = tx; MEM8(e + 0x24) = 1;   /* (+0x18 / +0x1C: the texture's size, the quad's texture coordinates in pixels) */
            vs_res_box(&n, bt, 100, 98, 440, 314, 0x9C000000u); }   /* (a veil over it, for the text) */
        else vs_res_box(&n, bt, 90, 90, 460, 330, 0xD8080A10u);
        vs_res_box(&n, bt, 90, 90, 460, 3, 0xFF40A0C8u); vs_res_box(&n, bt, 90, 417, 460, 3, 0xFF40A0C8u);
        vs_res_box(&n, bt, 90, 90, 3, 330, 0xFF40A0C8u); vs_res_box(&n, bt, 547, 90, 3, 330, 0xFF40A0C8u);
        vs_res_box(&n, bt, 110, 154, 420, 2, 0xC040A0C8u); }
    vs_res_line(&n, tmpl, 120, 104, 400, "MATCH RESULTS", 0xFF60D8FFu);
    vs_res_line(&n, tmpl, 120, 128, 400, t, 0xFFFFFFFFu);
    vs_res_line(&n, tmpl, 360, 160, 80, "P1", 0xFFFFC060u); vs_res_line(&n, tmpl, 450, 160, 80, "P2", 0xFF60C0FFu);
    for (i = 0; i < 7; i++) {
        static const char *lab[7] = { "ROUNDS WON", "HITS LANDED", "DAMAGE DEALT", "BIGGEST HIT", "ARROWS ON TARGET", "FASTEST ROUND", "TIMES DOWNED" };
        int y = 186 + i * 22; vs_res_line(&n, tmpl, 110, y, 240, lab[i], 0xFFC8E0E8u);
        for (k = 0; k < 2; k++) { const VsStats *q = &tt_vs_st[k];
            switch (i) {
            case 0: snprintf(t, sizeof t, "%d", q->rounds); break;
            case 1: snprintf(t, sizeof t, "%d", q->hits); break;
            case 2: snprintf(t, sizeof t, "%d", q->dmg); break;
            case 3: snprintf(t, sizeof t, "%d", q->best_hit); break;
            case 4: snprintf(t, sizeof t, "%d/%d", q->shots_hit, q->shots); break;
            case 5: if (q->fastest_ms) snprintf(t, sizeof t, "%u.%us", q->fastest_ms / 1000u, (q->fastest_ms / 100u) % 10u); else snprintf(t, sizeof t, "-"); break;
            default: snprintf(t, sizeof t, "%d", q->downs); break; }
            vs_res_line(&n, tmpl, k ? 450 : 360, y, 80, t, 0xFFFFFFFFu); }
    }
    for (i = 0; i < 2; i++) { int a = tt_vs_res_award[i][0], w = tt_vs_res_award[i][1];
        if (a < 0) continue;
        snprintf(t, sizeof t, "%s: PLAYER %d", aw[a], w + 1); vs_res_line(&n, tmpl, 110, 350 + i * 22, 420, t, 0xFF40E0FFu); (void)awd; }
    for (k = 20; k >= 0; k--) if (!MEM8(COOP_HUD_ON + k) && k != coop_sw_slot && k != coop_tag_slot) {
        vs_res_slot = k; vs_res_saved[0] = MEM32(COOP_HUD_LIST + k * 12); vs_res_saved[1] = MEM32(COOP_HUD_LIST + k * 12 + 4);
        MEM32(COOP_HUD_LIST + k * 12) = vs_res_el; MEM32(COOP_HUD_LIST + k * 12 + 4) = (uint32_t)n; MEM8(COOP_HUD_ON + k) = 1;
        break;
    }
    xbox_hud_excl_more[2][0] = 80.0f; xbox_hud_excl_more[2][1] = 560.0f; xbox_hud_excl_more[2][2] = 240.0f + (80.0f - 240.0f) * 0.93f; xbox_hud_excl_more[2][3] = 240.0f + (430.0f - 240.0f) * 0.93f; xbox_hud_excl_more[2][4] = 1.0f;   /* (frame units: HUD y shrunk 0.93 about 240, as the banner's) */
    if (!vs_res_ver) {   /* the panel: dark, a gold edge, a line under the title */
        int x, y; for (y = 0; y < 100; y++) for (x = 0; x < 160; x++) {
            int edge = x < 2 || y < 2 || x >= 158 || y >= 98, line = (y == 22 || y == 23) && x > 8 && x < 152, in2 = x == 3 || y == 3 || x == 156 || y == 96;
            vs_res_px[y * 160 + x] = edge ? 0xF0C8A040u : in2 ? 0xC0806020u : line ? 0xC0A08040u : 0xD8100C08u; }
        vs_res_ver = 1; }
}
int tt_vs_res_overlay(CoopOvlQuad *q)   /* (coop_overlay) the panel behind the results */
{
    if (!vs_res_on || !getenv("TT_VS_RESOVL")) return 0;
    q->px = vs_res_px; q->w = 160; q->h = 100; q->ver = 7700 + vs_res_ver; q->x = 90.0f; q->y = 90.0f; q->dw = 460.0f; q->dh = 330.0f;
    return 1;
}
void tt_coop_hud_pre(void)
{
    { extern volatile float xbox_hud_excl_more[4][5]; int q; for (q = 0; q < 4; q++) xbox_hud_excl_more[q][4] = 0.0f; }
    static int checked, ok = 1; extern int coop_hero_down(int); extern uint32_t coop_down_time(void);
    coop_hudlist_dump();
    { extern volatile float xbox_hud_excl[5]; xbox_hud_excl[4] = 0.0f; }   /* set again below while the respawn counter shows */
    int k, mir, dn; uint32_t a = COOP_P1SWORD, A = 659; const int32_t DY = 0;   /* mirror axis: player 1's art 23..279 → 382..638, the top right, level with player 1's; all in the right half (the widescreen HUD groups pieces within 10 units and only pins a group to an edge if it stays in one half) */
    int32_t hp, mx; float f;
    coop_sw_slot = -1;
    if (getenv("TT_COOP_NOSWORD") || coop_mode() != 3 || coop_p2 < 0x10000u) return;
    if (!checked) { uint32_t i; checked = 1; for (i = 0; i < COOP_EL_N * 0xDC; i += 4) if (MEM32(COOP_EL + i)) ok = 0; tt_dbg("[COOPSWORD] element area %08X %s\n", COOP_EL, ok ? "is free (all zero)" : "is NOT zero: not used"); }
    if (!ok || !MEM8(COOP_HUD_ON + 0)) return;   /* only while the game shows player 1's sword */
    mir = getenv("TT_COOP_SWORDMIRROR") ? atoi(getenv("TT_COOP_SWORDMIRROR")) : 1;
    if (getenv("TT_COOP_SWORDTEST") && coop_p2_t0) mir = (int)(((coop_now() - coop_p2_t0) / 3000) % 3);   /* test: cycle 0, 1, 2 */
    hp = (int32_t)MEM32(coop_p2 + 0x2FC); mx = (int32_t)MEM32(coop_p2 + 0x300); if (mx <= 0) mx = 1000;
    dn = coop_hero_down(1); f = hp <= 0 || dn ? 0.0f : hp >= mx ? 1.0f : (float)hp / (float)mx;
    if (getenv("TT_VS_STRIPLOG")) { static uint32_t tl; if (coop_now() - tl > 250u) { int q; tl = coop_now(); for (q = 0; q < 17; q++) { uint32_t e = a + (uint32_t)q * 0xDC; if (!getenv("TT_VS_STRIPALL") && q && q != 7 && q != 8 && q != 16 && q != 1 && q != 2) continue;
        tt_dbg("[STRIP] t %u el %d on %u x %d y %d w %d h %d col %08X tex %08X\n", coop_now(), q, MEM8(e + 0x24), (int32_t)MEM32(e + 8), (int32_t)MEM32(e + 0xC), (int32_t)MEM32(e + 0x10), (int32_t)MEM32(e + 0x14), MEM32(e + 0x20), MEM32(e + 0x18)); } } }
    if (getenv("TT_VS_STRIPDUMP")) { static uint32_t tl; if (coop_now() - tl > 3000u) { int q, o2; tl = coop_now(); for (q = 0; q < 17; q += 16) { char b[900]; int k = 0; for (o2 = 0; o2 < 0xDC; o2 += 4) k += snprintf(b + k, sizeof b - k, " %X:%08X", o2, MEM32(a + (uint32_t)q * 0xDC + (uint32_t)o2)); tt_dbg("[STRIPD] el %d:%s\n", q, b); }
        { char b[900]; int k = 0; for (o2 = 0; o2 < 0xDC; o2 += 4) k += snprintf(b + k, sizeof b - k, " %X:%08X", o2, MEM32(COOP_EL + (uint32_t)o2)); tt_dbg("[STRIPD] copy 0:%s\n", b); } } }
    coop_el_copy(COOP_EL + 0 * 0xDC, a + 0 * 0xDC);    /* blade strip */
    coop_el_copy(COOP_EL + 1 * 0xDC, a + 7 * 0xDC);    /* red bar */
    coop_el_copy(COOP_EL + 2 * 0xDC, a + 8 * 0xDC);    /* green fill */
    coop_el_copy(COOP_EL + 3 * 0xDC, a + 16 * 0xDC);   /* sword art */
    for (k = 0; k < 4; k++) {
        uint32_t e = COOP_EL + k * 0xDC; int32_t x = (int32_t)MEM32(e + 8), w = (int32_t)MEM32(e + 0x10);
        if (k == 2) w = 171;   /* the fill's full width (player 1's own is his health) */
        if (mir == 0) MEM32(e + 8) = (uint32_t)(x + (int32_t)A - 23 - 279 + 23);   /* same way round, shifted */
        else if (k == 1 || k == 2) MEM32(e + 8) = (uint32_t)((int32_t)A - x - w);   /* plain colour bars: just placed */
        else if (k == 0 && getenv("TT_COOP_STRIPPLACE")) MEM32(e + 8) = (uint32_t)((int32_t)A - x - w);   /* (1.9.7) the blade's lower strip: placed, not mirrored -- with a negative width it was never drawn in 4:3, and in widescreen it was drawn beside player 1's sword */
        else if (mir == 1) { MEM32(e + 8) = (uint32_t)((int32_t)A - x); MEM32(e + 0x10) = (uint32_t)(-w); }   /* textured: negative width from the right edge */
        else { MEM32(e + 8) = (uint32_t)((int32_t)A - x - w); MEM32(e + 0x10) = (uint32_t)(-w); }        /* textured: negative width from the left edge */
        MEM32(e + 0xC) = (uint32_t)((int32_t)MEM32(e + 0xC) + DY); MEM8(e + 0x24) = 1;
    }
    if (getenv("TT_COOP_STRIPTEST")) { MEM32(COOP_EL + 8) = 21; MEM32(COOP_EL + 0xC) = 120; MEM32(COOP_EL + 0x10) = 256; }
    MEM32(COOP_EL + 1 * 0xDC + 0x20) = 0xFF00048Bu; MEM32(COOP_EL + 2 * 0xDC + 0x20) = 0xFF00C747u; MEM32(COOP_EL + 3 * 0xDC + 0x20) = 0xFFFFFFFFu;   /* retail red and green (player 1's fill colour follows his own health) */
    {   /* the fill: player 2's health, shrinking towards the hilt (right when mirrored) */
        /* (1.9.7) full is the red bar's length (player 1's, copied: his sword's at his top level), not a fixed 171 -- the
         * bar showed red past the green at full health, as if player 2 had less than player 1 */
        /* (1.9.7) the red bar under it as long as the fill when full (player 1's red is his own health's, so it showed
         * past player 2's green; taking its length made player 2's bar follow player 1's health) */
        uint32_t e = COOP_EL + 2 * 0xDC, r = COOP_EL + 1 * 0xDC; int32_t full = 171, x0 = (int32_t)MEM32(e + 8), wf;
        wf = (int32_t)((float)full * f + 0.5f);
        MEM32(r + 8) = (uint32_t)x0; MEM32(r + 0x10) = (uint32_t)full; MEM32(r + 0xC) = MEM32(e + 0xC); MEM32(r + 0x14) = MEM32(e + 0x14);
        MEM32(e + 0x10) = (uint32_t)wf;
        if (mir) MEM32(e + 8) = (uint32_t)(x0 + full - wf);
        MEM32(e + 0x10) = (uint32_t)wf; MEM8(e + 0x24) = wf > 0;
        if (dn) MEM32(COOP_EL + 3 * 0xDC + 0x20) = 0xFF909090u;   /* the art greyed while he is down */
    }
    {   /* the respawn counter, font 0 (centres its text in the box) */
        uint32_t e = COOP_EL + 4 * 0xDC; extern uint32_t coop_down_time(void);
        coop_el_copy(e, a + 26 * 0xDC);   /* the arrows' count "30", font 0 */
        MEM32(e + 8) = 220; MEM32(e + 0xC) = getenv("TT_COOP_CNTY") ? (uint32_t)atoi(getenv("TT_COOP_CNTY")) : 96; MEM32(e + 0x10) = 200; MEM32(e + 0x14) = 30; if (coop_bossbar_any()) MEM32(e + 0xC) = 180;   /* (the boss bar moved under the swords: the counter under it) */   /* (2026-10-18) just under the swords: kept out of the widescreen grouping by the renderer's exclusion box (xbox_hud_excl, set below), so it no longer has to stay 10 units clear of everything (it sat at 178) */   /* centred, under everything in the top band (swords y 25-89, the allies moved down to y 164): the text's drawn pieces span much wider than its box, and at y 110-160 (tested) they merged the whole top band into one centred group; 175 and 200 keep the swords in their corners: more than 10 units from both sword groups, so it neither drags them to the middle nor is pinned */
        {   extern const char *tt_vs_banner(void); const char *vb = tt_vs_banner();
            int d1 = coop_hero_down(0), who = vb ? 9 : tt_vs_on() ? 0 : dn ? 2 : d1 ? 1 : 0;   /* (Versus: no respawn counter) */   /* whichever hero is down (both down is game over); 9: Versus' banner */
            MEM8(e + 0x24) = (uint8_t)(who != 0); MEM32(e + 0x20) = 0xFF4040FFu;
            {   /* the counter's pieces out of the widescreen grouping (frame units: HUD y shrunk 0.93 about 240; x 220-405 keeps the allies' HUD, from HUD x 400, out) */
                extern volatile float xbox_hud_excl[5]; float y0 = (float)(int32_t)MEM32(e + 0xC) - 8.0f, y1 = y0 + 46.0f;
                xbox_hud_excl[0] = who == 9 ? 120.0f : 220.0f; xbox_hud_excl[1] = who == 9 ? 520.0f : 405.0f; xbox_hud_excl[2] = 240.0f + (y0 - 240.0f) * 0.93f; xbox_hud_excl[3] = 240.0f + (y1 - 240.0f) * 0.93f;
                xbox_hud_excl[4] = (who && !getenv("TT_COOP_NOHUDEXCL")) ? 1.0f : 0.0f; }
            if (who == 9) {
                int big = (vb[0] >= '1' && vb[0] <= '9' && !vb[1]) || !strcmp(vb, "FIGHT!");
                if (big && !getenv("TT_VS_SMALLCD")) {   /* the countdown in the rating word's big letters, in the middle */
                    uint32_t src = 0x4A3D80u + (uint32_t)(getenv("TT_VS_CDEL") ? atoi(getenv("TT_VS_CDEL")) : 14) * 0xDC;
                    coop_el_copy(e, src); MEM8(e + 0x24) = 1;
                    MEM32(e + 8) = 170; MEM32(e + 0xC) = 170; MEM32(e + 0x10) = 300; MEM32(e + 0x14) = 60;
                    if (getenv("TT_VS_CDFONT")) MEM32(e + 0x2C) = (uint32_t)atoi(getenv("TT_VS_CDFONT"));
                    MEM32(e + 0x20) = vb[1] ? 0xFF2020FFu : 0xFF40E0FFu;
                    {   extern volatile float xbox_hud_excl[5]; xbox_hud_excl[0] = 150.0f; xbox_hud_excl[1] = 490.0f; xbox_hud_excl[2] = 240.0f + (160.0f - 240.0f) * 0.93f; xbox_hud_excl[3] = 240.0f + (240.0f - 240.0f) * 0.93f; xbox_hud_excl[4] = 1.0f; }
                    coop_el_text(e, vb); }
                else { MEM32(e + 8) = 120; MEM32(e + 0x10) = 400; MEM32(e + 0x20) = 0xFF40E0FFu; coop_el_text(e, vb); } }
            else if (who) { uint32_t wait = coop_respawn_wait(), el = coop_down_time(); char t[24]; memcpy(t, "P1 RESPAWN ", 11); t[1] = (char)('0' + who); coop_put_int(t + 11, el >= wait ? 0 : (int)((wait - el + 999) / 1000)); coop_el_text(e, t); } }
    }
    {   /* player 2's gauge: player 1's (list 1 at 0x4A3D80: 0 tube, 1 fill, 2 flash, 3 cap, 4 art + medallion, 5 marker)
         * mirrored to the bottom right, filled from player 2's record (as 0xCF9A0 fills player 1's) */
        extern int coop_p2rec_ok; uint32_t g = 0x4A3D80u; int32_t cur = 0, mxe = 1, h; int j;
        static const int src[6] = { 0, 1, 2, 3, 4, 5 };
        if (coop_p2rec_ok) coop_rec_gauge(COOP_P2REC, &cur, &mxe);
        if (cur < 0) cur = 0; if (cur > mxe) cur = mxe;
        h = (int32_t)((float)cur / (float)mxe * MEMF(0x173C3Cu) + 0.5f);
        for (j = 0; j < 6; j++) {
            uint32_t e = COOP_EL + (5 + j) * 0xDC; int32_t x, w;
            coop_el_copy(e, g + src[j] * 0xDC); x = (int32_t)MEM32(e + 8); w = (int32_t)MEM32(e + 0x10);
            if (j == 0 || j == 3 || j == 4) { MEM32(e + 8) = (uint32_t)(640 - x); MEM32(e + 0x10) = (uint32_t)(-w); }   /* textured: flipped */
            else MEM32(e + 8) = (uint32_t)(640 - x - w);
            MEM8(e + 0x24) = MEM8(g + src[j] * 0xDC + 0x24) && coop_p2rec_ok;
        }
        MEM32(COOP_EL + 6 * 0xDC + 0xC) = (uint32_t)(0x195 - h); MEM32(COOP_EL + 6 * 0xDC + 0x14) = (uint32_t)h;   /* the fill */
        if (getenv("TT_COOP_GAUGEPROBE")) { static int32_t lh = -1; static uint32_t lt; if (h != lh && coop_now() - lt > 200) { lh = h; lt = coop_now(); tt_dbg("[COOPGAUGE] P2 t %u: exp %d of %d into his level (+90 %.1f +6C %d +78 %d, rating +4 %.2f) -> fill %d of %.0f\n", coop_now() - coop_p2_t0, cur, mxe, MEMF(COOP_P2REC + 0x90), (int32_t)MEM32(COOP_P2REC + 0x6C), (int32_t)MEM32(COOP_P2REC + 0x78), MEMF(COOP_P2REC + 4), h, MEMF(0x173C3Cu)); } }
        MEM8(COOP_EL + 7 * 0xDC + 0x24) = 0;   /* player 1's gain flash: not his */
    }
    {   /* the rating popup (list 1: 10 medallion, 14 the word): after a kill by player 2, mirrored to his side and hidden on player 1's */
        extern uint32_t coop_p2pop_until; uint32_t g = 0x4A3D80u, p10 = g + 10 * 0xDC, p14 = g + 14 * 0xDC; int mine = coop_now() < coop_p2pop_until;
        uint32_t e11 = COOP_EL + 11 * 0xDC, e12 = COOP_EL + 12 * 0xDC;
        coop_el_copy(e11, p10); coop_el_copy(e12, p14);
        MEM32(e11 + 8) = (uint32_t)(640 - (int32_t)MEM32(e11 + 8) - (int32_t)MEM32(e11 + 0x10));
        {   /* the word is drawn from its box's left edge: mirrored, it ends where player 1's starts from his gauge (about 15 units a letter) */
            uint32_t tp = MEM32(e12 + 0x34) >= 0x10000u ? MEM32(e12 + 0x34) : e12 + 0x38; int n = 0, x1 = 640 - (int32_t)MEM32(e12 + 8), cw = getenv("TT_COOP_POPCW") ? atoi(getenv("TT_COOP_POPCW")) : 15;
            while (n < 24 && MEM16(tp + 2 * n)) n++;
            MEM32(e12 + 8) = (uint32_t)(x1 - n * cw);
            if (getenv("TT_COOP_GAUGEPROBE") && coop_now() < coop_p2pop_until) { static uint32_t lp; if (tp != lp) { lp = tp; tt_dbg("[COOPGAUGE] P2 popup word at %08X: %d letters (first %04X %04X), box x %d\n", tp, n, MEM16(tp), MEM16(tp + 2), x1 - n * cw); } } }
        coop_pop_saved[0] = MEM8(p10 + 0x24); coop_pop_saved[1] = MEM8(p14 + 0x24); coop_pop_hid = 0;
        if (mine) { MEM8(p10 + 0x24) = 0; MEM8(p14 + 0x24) = 0; coop_pop_hid = 1;
            {   /* bot test: TT_COOP_KILLSHOT=file dumps the frame 0.5 s into player 2's first popup */
                static int shot; const char *f = getenv("TT_COOP_KILLSHOT"); if (f && !shot && coop_p2pop_until - coop_now() < 2100) { shot = 1; xbox_PresentDumpRequest(f); tt_dbg("[COOPHUD] popup frame dump %s (popup %s, word shown %d)\n", f, MEM8(e11 + 0x24) ? "on" : "off", MEM8(e12 + 0x24)); } } }
        else { MEM8(e11 + 0x24) = 0; MEM8(e12 + 0x24) = 0;
            {   /* bot test: TT_COOP_P1POPSHOT=file dumps the frame 0.5 s after player 1's popup word first shows */
                static int shot; static uint32_t t0; const char *f = getenv("TT_COOP_P1POPSHOT");
                if (f && !shot) { if (MEM8(p14 + 0x24)) { if (!t0) t0 = coop_now() | 1; else if (coop_now() - t0 > 500) { shot = 1; xbox_PresentDumpRequest(f); tt_dbg("[COOPHUD] player 1's popup frame dump %s\n", f); } } else t0 = 0; } } }
    }
    {   /* the arrows (list 0: 24 the quiver, 25 its flash, 26 the count "30" from [0x4A82BC] = the hero's +0x304, 16 bits):
         * retail's sit at the bottom right, where player 2's gauge now is. Player 1's move beside his gauge for this draw,
         * player 2's (13 quiver, 14 count, his own +0x304) mirrored beside his. TT_COOP_ARROWX = the quiver's x (108). */
        static const int sl[3] = { 24, 25, 26 }; int j, ax = getenv("TT_COOP_ARROWX") ? atoi(getenv("TT_COOP_ARROWX")) : 108, dx = ax - (int32_t)MEM32(a + 24 * 0xDC + 8);
        uint32_t e13 = COOP_EL + 13 * 0xDC, e14 = COOP_EL + 14 * 0xDC; char t[8];
        coop_arrow_n = 0;
        for (j = 0; j < 3; j++) { uint32_t e = a + sl[j] * 0xDC; coop_arrow_saved[coop_arrow_n++] = MEM32(e + 8); MEM32(e + 8) = (uint32_t)((int32_t)MEM32(e + 8) + dx); }
        coop_el_copy(e13, a + 24 * 0xDC); coop_el_copy(e14, a + 26 * 0xDC);
        if (getenv("TT_COOP_AMMOLOG")) { static int done; uint32_t e = a + 24 * 0xDC, k; if (!done++) {   /* (research) the quiver's texture */
            tt_dbg("[COOPAMMO] quiver element %08X: +B0 %08X +B4 %08X +B8 %08X +BC %08X +C0 %08X +C4 %08X\n", e, MEM32(e + 0xB0), MEM32(e + 0xB4), MEM32(e + 0xB8), MEM32(e + 0xBC), MEM32(e + 0xC0), MEM32(e + 0xC4));
            for (k = 0xB8; k <= 0xBC; k += 4) { uint32_t h = MEM32(e + k); if (h >= 0x10000u && h < 0x84000000u) tt_dbg("[COOPAMMO]   +%X -> %08X: %08X %08X %08X %08X %08X %08X %08X %08X\n", k, h, MEM32(h), MEM32(h + 4), MEM32(h + 8), MEM32(h + 12), MEM32(h + 16), MEM32(h + 20), MEM32(h + 24), MEM32(h + 28)); }
            { uint32_t d = MEM32(MEM32(a + 24 * 0xDC + 0xB8) + 0x10), q; if (d >= 0x80000000u && d < 0x84000000u) for (q = 0; q < 0xA0; q += 32) tt_dbg("[COOPAMMO]   data +%02X: %08X %08X %08X %08X %08X %08X %08X %08X\n", q, MEM32(d + q), MEM32(d + q + 4), MEM32(d + q + 8), MEM32(d + q + 12), MEM32(d + q + 16), MEM32(d + q + 20), MEM32(d + q + 24), MEM32(d + q + 28)); }
            e = a + 25 * 0xDC; tt_dbg("[COOPAMMO] flash element %08X: +B8 %08X +BC %08X\n", e, MEM32(e + 0xB8), MEM32(e + 0xBC)); } }
        MEM32(e13 + 8) = (uint32_t)(640 - (int32_t)MEM32(e13 + 8)); MEM32(e13 + 0x10) = (uint32_t)(-(int32_t)MEM32(e13 + 0x10));   /* textured: flipped */
        MEM32(e14 + 8) = (uint32_t)(640 - (int32_t)MEM32(e14 + 8) - (int32_t)MEM32(e14 + 0x10));
        { int32_t c = (int32_t)(int16_t)MEM16(coop_p2 + 0x304); coop_put_int(t, c); coop_el_text(e14, t); }
        MEM8(e13 + 0x24) = MEM8(a + 24 * 0xDC + 0x24) && !dn; MEM8(e14 + 0x24) = MEM8(a + 26 * 0xDC + 0x24) && !dn;
        {   /* his own ammo icon (axes, stones; heroes.c's pixels): a copy of player 1's quiver texture -- its object
             * {0x1709C4, 0, 0, 0, data} and its data (0x80 header with the D3D texture at +0x2C, its pixels' physical
             * address at +0x30; 0x8000 bytes of pixels after it) -- with his pixels */
            static uint32_t blk, obj, src_obj; extern int tt_coop_p2_ammo_pixels(uint32_t cur, unsigned char *out);
            uint32_t so = MEM32(a + 24 * 0xDC + 0xB8), sd = so >= 0x80000000u && so < 0x84000000u ? MEM32(so + 0x10) : 0;
            if (sd >= 0x80000000u && sd < 0x84000000u && MEM32(sd + 4) == 64u && MEM32(sd + 8) == 128u) {
                if (!blk) { blk = xbox_ContiguousAlloc(0x80u + 0x8000u, 4096u); obj = xbox_ContiguousAlloc(0x40u, 16u); }
                if (blk && obj && (so != src_obj || coop_ammo_dirty)) {
                    static unsigned char px[0x8000]; uint32_t k;
                    src_obj = so; coop_ammo_dirty = 0; coop_ammo_ok = 0;
                    if (tt_coop_p2_ammo_pixels(sd + 0x80u, px)) {
                        for (k = 0; k < 0x80u; k += 4) MEM32(blk + k) = MEM32(sd + k);
                        MEM32(blk + 0x30) = (blk + 0x80u) & 0x03FFFFFFu;   /* (the pixels' physical address) */
                        for (k = 0; k < 0x8000u; k++) MEM8(blk + 0x80u + k) = px[k];
                        for (k = 0; k < 0x40u; k += 4) MEM32(obj + k) = MEM32(so + k);
                        MEM32(obj + 0x10) = blk;
                        coop_ammo_ok = 1;
                        tt_dbg("[COOPAMMO] player 2's ammo icon: texture %08X (pixels %08X) for player 1's %08X\n", obj, blk + 0x80u, so);
                    }
                }
                if (coop_ammo_ok) MEM32(e13 + 0xB8) = obj;
            }
        }
    }
    {   /* allies' HUD (Frodo's portrait and bar here; any switched-on list but player 1's sword 0 and gauge 1) in the top right moves under player 2's sword for this draw */
        int li, boss = coop_bossbar_any(); int32_t ady = getenv("TT_COOP_ALLYDY") ? atoi(getenv("TT_COOP_ALLYDY")) : 72; (void)boss;   /* (fixed: the first row under his bar, boss bar or not) */ coop_ally_n = 0;   /* (under the boss bar too while it shows, which moves on its own below) */
        for (li = 2; li < 21 && coop_hud_move_on(); li++) { uint32_t tb = MEM32(COOP_HUD_LIST + li * 12), n = MEM32(COOP_HUD_LIST + li * 12 + 4), j; if (!MEM8(COOP_HUD_ON + li) || tb < 0x10000u || n > 64 || (boss && coop_is_bossbar(li))) continue;
            for (j = 0; j < n && coop_ally_n < 96; j++) { uint32_t e = tb + j * 0xDC; int32_t x = (int32_t)MEM32(e + 8), y = (int32_t)MEM32(e + 0xC);
                if (x >= 400 && y < 200) { coop_ally[coop_ally_n][0] = e; coop_ally[coop_ally_n][1] = (uint32_t)y; coop_ally_n++; MEM32(e + 0xC) = (uint32_t)(y + ady); } } }
    }
    for (k = 2; k <= 20; k++) if (!MEM8(COOP_HUD_ON + k)) {   /* a switched-off slot of the list table draws them:
        * the lowest (lists are drawn in order), so the game's own lists after it draw over player 2's HUD */
        coop_sw_slot = k; coop_sw_saved[0] = MEM32(COOP_HUD_LIST + k * 12); coop_sw_saved[1] = MEM32(COOP_HUD_LIST + k * 12 + 4);
        MEM32(COOP_HUD_LIST + k * 12) = COOP_EL; MEM32(COOP_HUD_LIST + k * 12 + 4) = COOP_EL_N; MEM8(COOP_HUD_ON + k) = 1;
        break;
    }
    { static int lg; if (!lg++) tt_dbg("[COOPSWORD] player 2's sword drawn through HUD slot %d (mirror mode %d)\n", coop_sw_slot, mir); }
    if (getenv("TT_COOP_SWLOG")) {   /* (research) player 2's sword pieces (elements 0-3) each time one's shown flag or place changes */
        static uint32_t last[4]; int q; for (q = 0; q < 4; q++) { uint32_t e = COOP_EL + q * 0xDC, key = MEM8(e + 0x24) | (MEM32(e + 8) & 0xFFF) << 8 | (MEM32(e + 0xC) & 0xFFF) << 20;
            if (key != last[q]) { last[q] = key; tt_dbg("[COOPSW] t %u piece %d shown %u at %d,%d size %d,%d texture %08X (slot %d)\n", coop_now(), q, MEM8(e + 0x24), (int32_t)MEM32(e + 8), (int32_t)MEM32(e + 0xC), (int32_t)MEM32(e + 0x10), (int32_t)MEM32(e + 0x14), MEM32(e + 0xB8), coop_sw_slot); } }
    }
    coop_aimdot_draw(a + 24 * 0xDC);
    if (!getenv("TT_COOP_NOBOSSBARMOVE") && coop_hud_move_on()) {   /* a boss bar (coop_is_bossbar: the troll's list 15, the Watcher's 19) under player 2's sword:
        * drawn from copies moved down (the list's table swapped for the draw): the game slides its pieces from where they are,
        * so moving its own ran them down the screen (observed) */
        extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment); static uint32_t copy;
        int32_t dy = getenv("TT_COOP_BOSSBARDY") ? atoi(getenv("TT_COOP_BOSSBARDY")) : 168,   /* (fixed: the second row, under the allies' row) */ dx = getenv("TT_COOP_BOSSBARDX") ? atoi(getenv("TT_COOP_BOSSBARDX")) : 24;   /* (x: all of it in the right half, from 297 to 321, and 18 under the swords: more than the 10 that groups pieces, so the widescreen HUD keeps it and player 2's sword to the right edge; at 297 and 10 under they made one group across the middle, kept in the middle, observed) */
        int li;
        if (!copy) copy = xbox_ContiguousAlloc(4 * 8 * 0xDC, 16u);
        coop_boss_n = 0;
        for (li = 2; li <= 20 && copy && coop_boss_n < 4; li++) if (coop_is_bossbar(li)) {
            uint32_t tb = MEM32(COOP_HUD_LIST + li * 12), c = copy + (uint32_t)coop_boss_n * 8u * 0xDC, j;
            for (j = 0; j < 7; j++) { coop_el_copy(c + j * 0xDC, tb + j * 0xDC); MEM32(c + j * 0xDC + 0xC) = (uint32_t)((int32_t)MEM32(tb + j * 0xDC + 0xC) + dy); MEM32(c + j * 0xDC + 8) = (uint32_t)((int32_t)MEM32(tb + j * 0xDC + 8) + dx); }
            coop_boss_tb[coop_boss_n] = tb; coop_boss_li[coop_boss_n++] = li; MEM32(COOP_HUD_LIST + li * 12) = c;
            if (getenv("TT_COOP_BOSSBARLOG")) { static uint32_t lt; if (coop_now() - lt > 2000u) { lt = coop_now(); tt_dbg("[BOSSBAR] list %d drawn %d lower; the game's y:", li, dy); for (j = 0; j < 7; j++) tt_dbg(" %d", (int32_t)MEM32(tb + j * 0xDC + 0xC)); tt_dbg("\n"); } } } }
    if (mir && !getenv("TT_COOP_NOTEETH")) {   /* (1.9.7) the silver strip under the blade (player 1's sword's elements 1-6, the
                                                 * teeth, and 9), mirrored as the sword's art, through a slot of its own */
        extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment); static const int src[13] = { 1, 2, 3, 4, 5, 6, 9, 10, 11, 12, 13, 14, 15 }; int j, n = 0;
        if (!coop_teeth_el) coop_teeth_el = xbox_ContiguousAlloc(14u * 0xDCu, 16u);
        if (coop_teeth_el) {
            for (j = 0; j < 13; j++) { uint32_t e = coop_teeth_el + (uint32_t)n * 0xDC, q = a + (uint32_t)src[j] * 0xDC; int32_t x, w;
                if (!MEM8(q + 0x24)) continue;
                coop_el_copy(e, q); x = (int32_t)MEM32(e + 8); w = (int32_t)MEM32(e + 0x10);
                if (src[j] == 9 || getenv("TT_COOP_TEETHPLACE")) MEM32(e + 8) = (uint32_t)((int32_t)A - x - w);   /* (placed: a negative width is not drawn here) */
                else { MEM32(e + 8) = (uint32_t)((int32_t)A - x); MEM32(e + 0x10) = (uint32_t)(-w); }
                MEM32(e + 0xC) = (uint32_t)((int32_t)MEM32(e + 0xC) + DY); if (dn) MEM32(e + 0x20) = 0xFF909090u; n++; }
            for (k = 20; n && k >= 0; k--) if (!MEM8(COOP_HUD_ON + k) && k != coop_sw_slot && k != coop_tag_slot && k != vs_res_slot) {
                coop_teeth_slot = k; coop_teeth_saved[0] = MEM32(COOP_HUD_LIST + k * 12); coop_teeth_saved[1] = MEM32(COOP_HUD_LIST + k * 12 + 4);
                MEM32(COOP_HUD_LIST + k * 12) = coop_teeth_el; MEM32(COOP_HUD_LIST + k * 12 + 4) = (uint32_t)n; MEM8(COOP_HUD_ON + k) = 1; break; }
        }
    }
    coop_tags(a + 26 * 0xDC);
    vs_results_hud(a + 26 * 0xDC);
}
static void coop_gauge_probe(void)   /* (research) player 1's gauge (list 1) and rating text, when they change */
{
    static uint32_t sig, last; uint32_t g = 0x4A3D80u, s = MEM32(0x4A848Cu) * 31u + MEM32(0x4A3750u), i; char t[44]; int k;
    if (!getenv("TT_COOP_GAUGEPROBE")) return;
    for (i = 0; i < 16; i++) s = s * 31u + MEM32(g + i * 0xDC + 0x14) + MEM8(g + i * 0xDC + 0x24) * 7u + MEM32(g + i * 0xDC + 0x20);
    if (s == sig || coop_now() - last < 200) return;
    sig = s; last = coop_now();
    for (k = 0; k < 40 && MEM16(g + 14 * 0xDC + 0x38 + 2 * k); k++) t[k] = (char)MEM16(g + 14 * 0xDC + 0x38 + 2 * k);
    t[k] = 0;
    {   uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx, hid, rec; PUSH32(esp, 0x000D2310u); sub_000D2310(); hid = eax; esp = sp; eax = r0; ecx = r1; edx = r2;
        rec = 0x4AB598u + hid * 0x348u; tt_dbg("[COOPGAUGE] hero %u record %08X: +6C %d +78 %d | ", hid, rec, (int32_t)MEM32(rec + 0x6C), (int32_t)MEM32(rec + 0x78)); }
    tt_dbg("[COOPGAUGE] t %u exp %d/%d | on:", coop_now() - coop_p2_t0, (int32_t)MEM32(0x4A848Cu), (int32_t)MEM32(0x4A3750u));
    for (i = 0; i < 16; i++) if (MEM8(g + i * 0xDC + 0x24)) tt_dbg(" %u(%d,%d %dx%d %08X)", i, (int32_t)MEM32(g + i * 0xDC + 8), (int32_t)MEM32(g + i * 0xDC + 0xC), (int32_t)MEM32(g + i * 0xDC + 0x10), (int32_t)MEM32(g + i * 0xDC + 0x14), MEM32(g + i * 0xDC + 0x20));
    tt_dbg(" | rating '%s' ptr %08X\n", t, MEM32(g + 14 * 0xDC + 0x34));
}
void tt_coop_hud_post(void)
{
    coop_gauge_probe();
    if (coop_pop_hid) { MEM8(0x4A3D80u + 10 * 0xDC + 0x24) = coop_pop_saved[0]; MEM8(0x4A3D80u + 14 * 0xDC + 0x24) = coop_pop_saved[1]; coop_pop_hid = 0; }
    while (coop_ally_n > 0) { coop_ally_n--; MEM32(coop_ally[coop_ally_n][0] + 0xC) = coop_ally[coop_ally_n][1]; }   /* allies back in place */
    if (coop_arrow_n == 3) { MEM32(COOP_P1SWORD + 24 * 0xDC + 8) = coop_arrow_saved[0]; MEM32(COOP_P1SWORD + 25 * 0xDC + 8) = coop_arrow_saved[1]; MEM32(COOP_P1SWORD + 26 * 0xDC + 8) = coop_arrow_saved[2]; }   /* player 1's arrows back */
    coop_arrow_n = 0;
    while (coop_boss_n > 0) { coop_boss_n--; MEM32(COOP_HUD_LIST + coop_boss_li[coop_boss_n] * 12) = coop_boss_tb[coop_boss_n]; }   /* the boss bars' own tables back */
    if (coop_tag_slot >= 0) {
        MEM32(COOP_HUD_LIST + coop_tag_slot * 12) = coop_tag_saved[0]; MEM32(COOP_HUD_LIST + coop_tag_slot * 12 + 4) = coop_tag_saved[1];
        MEM8(COOP_HUD_ON + coop_tag_slot) = 0; coop_tag_slot = -1;
    }
    if (coop_teeth_slot >= 0) {
        MEM32(COOP_HUD_LIST + coop_teeth_slot * 12) = coop_teeth_saved[0]; MEM32(COOP_HUD_LIST + coop_teeth_slot * 12 + 4) = coop_teeth_saved[1];
        MEM8(COOP_HUD_ON + coop_teeth_slot) = 0; coop_teeth_slot = -1;
    }
    if (vs_res_slot >= 0) {
        MEM32(COOP_HUD_LIST + vs_res_slot * 12) = vs_res_saved[0]; MEM32(COOP_HUD_LIST + vs_res_slot * 12 + 4) = vs_res_saved[1];
        MEM8(COOP_HUD_ON + vs_res_slot) = 0; vs_res_slot = -1;
    }
    if (coop_sw_slot < 0) return;
    MEM32(COOP_HUD_LIST + coop_sw_slot * 12) = coop_sw_saved[0]; MEM32(COOP_HUD_LIST + coop_sw_slot * 12 + 4) = coop_sw_saved[1];
    MEM8(COOP_HUD_ON + coop_sw_slot) = 0; coop_sw_slot = -1;
}
/* (cdecl, called once a frame from the world update 0x000F2FF0) */
extern void sub_0009C380_gen(void);
void sub_0009C380(void)
{
    uint32_t p1;
    if (coop_mode() && coop_p2) { coop_gate_log(1); }
    {   uint32_t port0 = MEM32(0x1CB8B4u); int on2 = coop_p2 && getenv("TT_COOP_P1ON2") != NULL;   /* (testing) player 1 read from the virtual pad */
        if (on2) MEM32(0x1CB8B4u) = 1;
        tt_coop_p1_pre(MEM32(0x43B15Cu), 0x0009C380u); sub_0009C380_gen(); tt_coop_p1_post();
        if (on2) MEM32(0x1CB8B4u) = port0; }
    if (!coop_mode()) return;
    {   /* the player's trigger flag (0x43B058: the trigger planes see [0x43B15C] only while it is set; @ToggleTriggerObjs ->
         * 0x94060 sets it): every change, with the time (a playtest: the Watcher's cutscene never came with player 1 on its spot) */
        static int lv = -1; int v = MEM8(0x43B058u);
        if (v != lv) { tt_dbg("[COOPTRIGS] t %u: the player's triggers %s (0x43B058 %d -> %d), player 1 at %.1f %.1f\n", coop_now(), v ? "on" : "off", lv, v,
            coop_first_char >= 0x10000u ? MEMF(coop_first_char + 0xF0) : 0.0f, coop_first_char >= 0x10000u ? MEMF(coop_first_char + 0xF4) : 0.0f); lv = v; }
    }
    {   /* player 2 not drawn in an in-game cutscene (a playtest: "he appears sometimes beside p1 and create funny scenarios"):
         * after 0.4 s of one (tt_in_scene), his and his weapons' draw bit (+0x110 0x20000) kept clear, each put back as it was
         * after. TT_COOP_P2INSCENE: drawn as before */
        static uint32_t obj[48], bit[48], t_in; static int n_obj, hidden;
        int sc = coop_p2 >= 0x10000u && coop_mode() == 3 && tt_in_scene() && !tt_vs_on() && !getenv("TT_COOP_P2INSCENE"); int i;
        if (sc) { if (!t_in) t_in = coop_now() | 1; } else t_in = 0;
        if (sc && !hidden && coop_now() - t_in >= 400u) {
            extern uint32_t coop_wobjs[]; extern int coop_nwobjs; n_obj = 0;
            obj[n_obj] = coop_p2; bit[n_obj++] = MEM32(coop_p2 + 0x110) & 0x20000u;
            for (i = 0; i < coop_nwobjs && n_obj < 48; i++) { uint32_t q = coop_wobjs[i]; if (q >= 0x10000u && MEM32(q) == 0x174980u && MEM32(q + 0x11C) == coop_p2) { obj[n_obj] = q; bit[n_obj++] = MEM32(q + 0x110) & 0x20000u; } }
            for (i = 0; i < coop_nwpn && n_obj < 48; i++) { uint32_t q = coop_wpn_copy[i]; int k, dup = 0; for (k = 0; k < n_obj; k++) if (obj[k] == q) dup = 1;
                if (!dup && q >= 0x10000u && MEM32(q) == 0x174980u && MEM32(q + 0x11C) == coop_p2) { obj[n_obj] = q; bit[n_obj++] = MEM32(q + 0x110) & 0x20000u; } }
            hidden = 1; tt_dbg("[COOPSCENE] a cutscene: player 2 and %d of his weapons not drawn\n", n_obj - 1);
        }
        if (hidden && sc) for (i = 0; i < n_obj; i++) MEM32(obj[i] + 0x110) &= ~0x20000u;
        if (hidden && !sc) {
            for (i = 0; i < n_obj; i++) if (obj[i] >= 0x10000u && (MEM32(obj[i]) == 0x174980u || obj[i] == coop_p2)) MEM32(obj[i] + 0x110) = (MEM32(obj[i] + 0x110) & ~0x20000u) | bit[i];
            hidden = 0; tt_dbg("[COOPSCENE] the cutscene is over: player 2 drawn again\n");
        }
    }
    if (getenv("TT_COOP_HUDVISLOG")) {   /* (research) HUD list 1 (0x4A3D80, 0xDC each): each element's shown flag +0x24 as it changes */
        static uint8_t lv[32]; static int init; int k;
        {   /* (research) TT_COOP_HUDSHOTDIR=dir: a frame the third time each element shows (dir/hudel<k>.bmp) */
            static int cnt[32]; extern void xbox_PresentDumpRequest(const char *path); const char *dd = getenv("TT_COOP_HUDSHOTDIR"); static char pth[32][300];
            for (k = 0; dd && init && k < 32; k++) { uint8_t v = MEM8(0x4A3D80u + k * 0xDCu + 0x24u); if (v && !lv[k] && ++cnt[k] == 3 && (!getenv("TT_COOP_HUDSHOTEL") || atoi(getenv("TT_COOP_HUDSHOTEL")) == k)) { snprintf(pth[k], sizeof pth[k], "%s\\hudel%d.bmp", dd, k); xbox_PresentDumpRequest(pth[k]); tt_dbg("[HUDVIS] frame for element %d: %s\n", k, pth[k]); } }
        }
        {   /* (research) the word element's text (list 1 element 14, +0x34 a UTF-16 pointer) whenever it changes */
            static uint32_t lt; static char lw[64]; uint32_t e = 0x4A3D80u + 14u * 0xDCu, tp = MEM32(e + 0x34) >= 0x10000u ? MEM32(e + 0x34) : e + 0x38; char w[64]; int q;
            for (q = 0; q < 63 && MEM16(tp + 2 * q); q++) w[q] = (char)(MEM16(tp + 2 * q) < 128 ? MEM16(tp + 2 * q) : '?'); w[q] = 0;
            if (strcmp(w, lw) || tp != lt) { tt_dbg("[HUDWORD] t %u word '%s' (text %08X, shown %u, context %d)\n", coop_now(), w, tp, MEM8(e + 0x24), coop_depth); strcpy(lw, w); lt = tp; }
        }
        for (k = 0; k < 32; k++) { uint8_t v = MEM8(0x4A3D80u + k * 0xDCu + 0x24u); if (init && v != lv[k]) tt_dbg("[HUDVIS] t %u list 1 element %d shown %u -> %u (texture %08X)\n", coop_now(), k, lv[k], v, MEM32(0x4A3D80u + k * 0xDCu)); lv[k] = v; }
        init = 1;
    }
    coop_place();
    coop_sync_listeners();
    coop_p2_target();
    coop_threat_track(); coop_threat_apply(); { extern void coop_pool_frame_x(void); coop_pool_frame_x(); } coop_diff_health(); coop_death_frame(); coop_hud_frame(); coop_exp_frame(); coop_ring_frame();
    if (getenv("TT_COOP_WPNSCAN") && coop_first_char >= 0x10000u && coop_p2 >= 0x10000u) {   /* (research) each hero's pointers to weapon objects (vtable 0x174980), every 5 s */
        static uint32_t lt; int i;
        extern uint32_t coop_wobjs[]; extern int coop_nwobjs;
        if (coop_now() - lt > 500u) { lt = coop_now();
            for (i = 0; i < coop_nwobjs; i++) { uint32_t q = coop_wobjs[i]; float d1, d2;
                if (MEM32(q) != 0x174980u) continue;
                d1 = sqrtf((MEMF(q + 0xF0) - MEMF(coop_first_char + 0xF0)) * (MEMF(q + 0xF0) - MEMF(coop_first_char + 0xF0)) + (MEMF(q + 0xF4) - MEMF(coop_first_char + 0xF4)) * (MEMF(q + 0xF4) - MEMF(coop_first_char + 0xF4)));
                d2 = sqrtf((MEMF(q + 0xF0) - MEMF(coop_p2 + 0xF0)) * (MEMF(q + 0xF0) - MEMF(coop_p2 + 0xF0)) + (MEMF(q + 0xF4) - MEMF(coop_p2 + 0xF4)) * (MEMF(q + 0xF4) - MEMF(coop_p2 + 0xF4)));
                if (d1 > 6.0f && d2 > 6.0f) continue;
                tt_dbg("[WPNSCAN] t %u %08X '%s' owner +11C %08X%s, from P1 %.2f P2 %.2f, +110 %08X | P2 state %u\n", coop_now(), q, MEM32(q + 0x18) >= 0x10000u && MEM32(q + 0x18) < 0x84000000u ? (const char *)XBOX_PTR(MEM32(q + 0x18)) : "?",
                       MEM32(q + 0x11C), MEM32(q + 0x11C) == coop_first_char ? " (P1)" : MEM32(q + 0x11C) == coop_p2 ? " (P2)" : "", d1, d2, MEM32(q + 0x110), MEM32(coop_p2 + 0x408)); } }
    }
    if (getenv("TT_COOP_NEARLOG") && coop_first_char >= 0x10000u) {   /* (research) the characters within 40 units of player 1, every 2 s */
        static uint32_t lt; int i, n = 0;
        if (coop_now() - lt > 2000u) { lt = coop_now();
            for (i = 0; i < coop_nchars && n < 20; i++) { uint32_t e = coop_chars[i]; float dx = MEMF(e + 0xF0) - MEMF(coop_first_char + 0xF0), dy = MEMF(e + 0xF4) - MEMF(coop_first_char + 0xF4), dz = MEMF(e + 0xF8) - MEMF(coop_first_char + 0xF8);
                if (dx * dx + dy * dy > 1600.0f || e == coop_first_char) continue; n++;
                tt_dbg("[NEAR] %08X vt %08X role %u hp %d/%d at %.1f %.1f (dz %.1f) state %u name '%s'\n", e, MEM32(e), MEM32(e + 0x114), (int32_t)MEM32(e + 0x2FC), (int32_t)MEM32(e + 0x300), sqrtf(dx * dx + dy * dy), atan2f(dy, dx) * 57.3f, dz, MEM32(e + 0x408),
                       MEM32(e + 0x18) >= 0x10000u && MEM32(e + 0x18) < 0x84000000u ? (const char *)XBOX_PTR(MEM32(e + 0x18)) : "?"); }
            tt_dbg("[NEAR] %d near player 1 at %.1f %.1f %.1f (%d characters listed) | +110 P1 %08X P2 %08X, +49C P1 %08X P2 %08X, state P1 %u P2 %u | ammo P1 %d/%d P2 %d/%d, type +2F0 P1 %08X P2 %08X\n", n, MEMF(coop_first_char + 0xF0), MEMF(coop_first_char + 0xF4), MEMF(coop_first_char + 0xF8), coop_nchars,
                   MEM32(coop_first_char + 0x110), coop_p2 >= 0x10000u ? MEM32(coop_p2 + 0x110) : 0, MEM32(coop_first_char + 0x49C), coop_p2 >= 0x10000u ? MEM32(coop_p2 + 0x49C) : 0, MEM32(coop_first_char + 0x408), coop_p2 >= 0x10000u ? MEM32(coop_p2 + 0x408) : 0,
                   (int16_t)MEM16(coop_first_char + 0x304), (int16_t)MEM16(coop_first_char + 0x306), coop_p2 >= 0x10000u ? (int16_t)MEM16(coop_p2 + 0x304) : 0, coop_p2 >= 0x10000u ? (int16_t)MEM16(coop_p2 + 0x306) : 0, MEM32(coop_first_char + 0x2F0), coop_p2 >= 0x10000u ? MEM32(coop_p2 + 0x2F0) : 0); }
    }
    {   /* bot test: TT_COOP_TESTEND = "ms:code": EndMission(code) (0xF1230, as the game-over fallback) ms after player 2 takes
         * over, once a level -- for the end-of-level screens without playing the level through */
        const char *v = getenv("TT_COOP_TESTEND"); static unsigned lv; static int done;
        if (lv != coop_level_n) { lv = coop_level_n; done = 0; }
        if (v && !done && coop_p2_t0 && coop_p2 >= 0x10000u && !coop_depth && MEM32(0x43B15Cu) == coop_first_char) { char *e; uint32_t ms = (uint32_t)strtoul(v, &e, 10);
            if (coop_now() - coop_p2_t0 >= ms && *e == ':') { uint32_t code = (uint32_t)strtoul(e + 1, NULL, 10), sp = esp, r0 = eax, r1 = ecx, r2 = edx; done = 1;
                tt_dbg("[COOPEND] test: EndMission(%u), level [1BB2A4] %d\n", code, (int32_t)MEM32(0x1BB2A4u));
                PUSH32(esp, code); PUSH32(esp, 0x000F1230u); ecx = MEM32(0x8D2484u); sub_000F1230(); esp = sp; eax = r0; ecx = r1; edx = r2; } }
    }
    {   /* bot test: TT_COOP_P2GOTO = "x,y,ms": player 2 put at x,y (on player 1's height) ms after he takes over, once a level */
        const char *v = getenv("TT_COOP_P2GOTO"); static unsigned lv; static int done;
        if (lv != coop_level_n) { lv = coop_level_n; done = 0; }
        if (v && !done && coop_p2_t0 && coop_p2 >= 0x10000u && coop_first_char >= 0x10000u && !coop_depth) { const char *c1 = strchr(v, ','), *c2 = c1 ? strchr(c1 + 1, ',') : NULL;
            if (c2 && coop_now() - coop_p2_t0 >= (uint32_t)atoi(c2 + 1)) { done = 1; MEMF(coop_p2 + 0xF0) = (float)atof(v); MEMF(coop_p2 + 0xF4) = (float)atof(c1 + 1); MEMF(coop_p2 + 0xF8) = MEMF(coop_first_char + 0xF8);
                MEM32(coop_p2 + 0x90) = MEM32(coop_p2 + 0xF0); MEM32(coop_p2 + 0x94) = MEM32(coop_p2 + 0xF4); MEM32(coop_p2 + 0x98) = MEM32(coop_p2 + 0xF8);
                tt_dbg("[COOPGOTO] test: player 2 put at %.1f %.1f %.1f\n", MEMF(coop_p2 + 0xF0), MEMF(coop_p2 + 0xF4), MEMF(coop_p2 + 0xF8)); } }
    }
    { extern void coop_xph_note(void); if (coop_p2_t0) coop_xph_note(); }   /* (both players' level experience, for a checkpoint reload) */
    {   /* (research) TT_COOP_P1WDUMP=dir: every 250 ms, player 1's weapon objects (owner +0x11C, up to 6, 0x300 bytes each) and
         * the anim machine he shares with the twin (+0x3F8, 0x400) to <dir>/w<n>.bin, logged with whether player 2 aims
         * (TT_COOP_P2AIMTEST): Legolas' left weapon turning while Gimli aims */
        const char *dir = getenv("TT_COOP_P1WDUMP"); static uint32_t last; static int n; extern uint32_t coop_wobjs[]; extern int coop_nwobjs;
        if (dir && coop_p2_t0 && coop_first_char >= 0x10000u && n < 300 && coop_now() - last >= 250u) {
            extern void *fopen(const char *, const char *); extern size_t fwrite(const void *, size_t, size_t, void *); extern int fclose(void *); extern int sprintf(char *, const char *, ...);
            char pth[300]; void *fp; uint32_t ws[6], am = MEM32(coop_first_char + 0x3F8); int i, k = 0; static uint8_t zero[0x400];
            last = coop_now();
            for (i = 0; i < coop_nwobjs && k < 6; i++) if (MEM32(coop_wobjs[i] + 0x11C) == coop_first_char) ws[k++] = coop_wobjs[i];
            if (strlen(dir) < 280) { sprintf(pth, "%s\\w%03d.bin", dir, n);
                if ((fp = fopen(pth, "wb"))) { for (i = 0; i < 6; i++) fwrite(i < k ? (void *)XBOX_PTR(ws[i]) : (void *)zero, 1, 0x300, fp); fwrite(am >= 0x10000u ? (void *)XBOX_PTR(am) : (void *)zero, 1, 0x400, fp);
                    fwrite((void *)XBOX_PTR(coop_first_char), 1, 0x800, fp); fwrite((void *)XBOX_PTR(coop_twin), 1, 0x800, fp);   /* +0x1600: player 1, +0x1E00: the twin */
                    fwrite((void *)XBOX_PTR(0x43A800u), 1, 0x1800, fp); fwrite((void *)XBOX_PTR(0x1B0800u), 1, 0xC00, fp);   /* +0x2600: 0x43A800.., +0x3E00: 0x1B0800.. */
                    { static uint8_t z2[0x2000]; fwrite(am >= 0x10000u ? (void *)XBOX_PTR(am) : (void *)z2, 1, 0x2000, fp); }   /* +0x4A00: the anim machine, 0x2000 */
                    fclose(fp); }
                if (!n) for (i = 0; i < k; i++) tt_dbg("[P1WDUMP] slot %d: %08X '%s'\n", i, ws[i], coop_nm(ws[i]));
                tt_dbg("[P1WDUMP] w%03d at %u ms: player 2 %s\n", n, coop_now() - coop_p2_t0, coop_p2aim_on ? "aiming" : "not aiming"); n++; }
        }
    }
    if (getenv("TT_COOP_BOSSLOG") && coop_p2_t0) {   /* (research) each state change of the Watcher and its tentacles, timed, with
         * whom they are after and how far */
        static uint32_t obj[8], st[8]; static int no; int i, k;
        for (i = 0; i < coop_nchars; i++) { uint32_t e = coop_chars[i], s, ce, tg; int kd = coop_boss_kind(e); float dx = 0, dy = 0;
            if (!kd) continue;
            for (k = 0; k < no && obj[k] != e; k++) ;
            if (k == no) { if (no == 8) continue; obj[no] = e; st[no] = 0xFFFFFFFFu; no++; }
            s = MEM32(e + 0x408); if (s == st[k]) continue; st[k] = s;
            ce = MEM32(e + 0x494); tg = ce >= 0x10000u ? MEM32(ce + 0x98) : 0;
            if (tg >= 0x10000u && tg < 0x84000000u) { dx = MEMF(tg + 0xF0) - MEMF(e + 0xF0); dy = MEMF(tg + 0xF4) - MEMF(e + 0xF4); }
            tt_dbg("[BOSS] t %u %s state %u | hp %d | after %s %.1f away | +110 %08X +49C %08X\n", coop_now() - coop_p2_t0, coop_nm(e), s, (int32_t)MEM32(e + 0x2FC),
                   coop_nm(tg), sqrtf(dx * dx + dy * dy), MEM32(e + 0x110), MEM32(e + 0x49C)); }
    }
    {   /* (research) the checkpoint: @SetRespawnId (0xB0FB0) keeps it at 0x1BB354 (0xD24E0; stored by 0xD24F0) -- the id,
         * then the player's placement. Logged at each change of its first words */
        static uint32_t l0 = 0xDEADBEEFu, l1, l2, l3; uint32_t a = MEM32(0x1BB354u), b = MEM32(0x1BB358u), c = MEM32(0x1BB35Cu), d = MEM32(0x1BB360u);
        {   /* bot test: TT_COOP_TESTCKPT = ms: a checkpoint taken ms after the first take-over of the launch, as @SetRespawnId
             * does it (the id kept, player 1's level experience as an int at +0xC) */
            const char *v = getenv("TT_COOP_TESTCKPT"); static int done; extern uint32_t coop_rec_addr;
            if (v && !done && coop_p2_t0 && coop_p2 >= 0x10000u && coop_rec_addr && !coop_depth && coop_now() - coop_p2_t0 >= (uint32_t)atoi(v)) {
                done = 1; MEM32(0x1BB360u) = (uint32_t)(int32_t)MEMF(coop_rec_addr + 0x90); d = MEM32(0x1BB360u);
                tt_dbg("[CHECKPT] test: a checkpoint taken (id %d, player 1's level experience %d)\n", (int32_t)a, (int32_t)d); }
        }
        if (a != l0 || b != l1 || c != l2 || d != l3) { l0 = a; l1 = b; l2 = c; l3 = d;
            tt_dbg("[CHECKPT] 0x1BB354: id %d, %d, %d | %08X %08X %08X %08X %08X %08X (screen %u, level %u)\n", (int32_t)a, (int32_t)b, (int32_t)c,
                   MEM32(0x1BB360u), MEM32(0x1BB364u), MEM32(0x1BB368u), MEM32(0x1BB36Cu), MEM32(0x1BB370u), MEM32(0x1BB374u), MEM32(0x4AD050u), MEM32(0x1BB2A4u));
            {   /* a checkpoint taken in play: both players' level experience then, for a reload (coop_take) */
                extern void coop_ckpt_note(void); coop_ckpt_note(); }
        }
    }
    {   /* (research) TT_COOP_P1DUMP=dir: every 250 ms of play, player 1's hero (0x800 bytes), the twin's, and the player
         * globals 0x43B000-0x43B400 and 0x1B0B00-0x1B0E00 to <dir>/d<n>.bin, with player 1's pad (RY, LT) in the log: what aims */
        const char *dir = getenv("TT_COOP_P1DUMP"); static uint32_t last; static int n;
        if (dir && coop_p2_t0 && coop_first_char >= 0x10000u && coop_twin >= 0x10000u && n < 200 && coop_now() - last >= 250u) {
            extern void *fopen(const char *, const char *); extern size_t fwrite(const void *, size_t, size_t, void *); extern int fclose(void *); extern int sprintf(char *, const char *, ...);
            char pth[300]; void *fp; last = coop_now(); if (strlen(dir) > 280) dir = "."; sprintf(pth, "%s\\d%03d.bin", dir, n);
            if ((fp = fopen(pth, "wb"))) { fwrite((void *)XBOX_PTR(coop_first_char), 1, 0x800, fp); fwrite((void *)XBOX_PTR(coop_twin), 1, 0x800, fp);
                fwrite((void *)XBOX_PTR(0x43B000u), 1, 0x400, fp); fwrite((void *)XBOX_PTR(0x1B0B00u), 1, 0x300, fp);
                { extern uint32_t coop_rng_pair(int); uint32_t ex[3], k; static uint8_t zero[0x400]; ex[0] = coop_rng_pair(0); ex[1] = coop_rng_pair(1); ex[2] = MEM32(coop_first_char + 0x3F8);
                  for (k = 0; k < 3; k++) fwrite(ex[k] >= 0x10000u ? (void *)XBOX_PTR(ex[k]) : (void *)zero, 1, 0x400, fp);
                  if (!n) tt_dbg("[P1DUMP] also: player 1's ranged weapon %08X, player 2's copy %08X, the shared anim machine %08X\n", ex[0], ex[1], ex[2]); }
                fclose(fp); }
            tt_dbg("[P1DUMP] d%03d at %u ms of play: player 1's pad RY %d LT %d\n", n, coop_now() - coop_p2_t0, coop_p1pad_ry, coop_p1pad_lt); n++; }
    }
    {   /* (research) TT_COOP_SHAREDUMP: pointer fields player 1 and the twin share (the same object in both), 5 s into play */
        static unsigned lv; static int done; if (lv != coop_level_n) { lv = coop_level_n; done = 0; }
        if (getenv("TT_COOP_SHAREDUMP") && !done && coop_p2_t0 && coop_now() - coop_p2_t0 > 5000 && coop_first_char >= 0x10000u && coop_twin >= 0x10000u) {
            uint32_t k; done = 1;
            for (k = 0; k < 0x800; k += 4) { uint32_t v = MEM32(coop_first_char + k), w = MEM32(coop_twin + k);
                if (v == w && v >= 0x80000000u && v < 0x84000000u) { uint32_t vt = MEM32(v), h = MEM32(v + 0x18);
                    tt_dbg("[SHARED] +%03X %08X vt %08X name '%s'\n", k, v, vt, h >= 0x80000000u && h < 0x84000000u && MEM8(h) >= 32 && MEM8(h) < 127 ? (const char *)XBOX_PTR(h) : "-"); }
                else if (v >= 0x80000000u && v < 0x84000000u && w >= 0x80000000u && w < 0x84000000u && MEM32(v) == MEM32(w) && MEM32(v) >= 0x170000u && MEM32(v) < 0x180000u)
                    tt_dbg("[SHARED] +%03X own: P1 %08X twin %08X (vt %08X)\n", k, v, w, MEM32(v)); }
        }
    }
    {   /* bot test: TT_COOP_TESTXP12 = "p1,p2": both players' level experience set 3 s after the first take-over of the
         * launch only (a checkpoint reload must then keep or restore them by itself) */
        const char *v = getenv("TT_COOP_TESTXP12"); static int done; extern uint32_t coop_rec_addr;
        if (v && !done && coop_p2_t0 && coop_p2 >= 0x10000u && coop_rec_addr && !coop_depth && coop_now() - coop_p2_t0 >= 3000) {
            float a = (float)atof(v), b = strchr(v, ',') ? (float)atof(strchr(v, ',') + 1) : 0; done = 1;
            MEMF(coop_rec_addr + 0x90) = a; memcpy(coop_s2 + 0x400 + 0x90, &b, 4); coop_test_kc(v);
            tt_dbg("[COOPEXP] test: level experience set: player 1 %.0f, player 2 %.0f\n", a, b); }
        {   /* TT_COOP_TESTXP12B = "p1,p2": again 9 s after the first take-over (after a TT_COOP_TESTCKPT checkpoint) */
            const char *w = getenv("TT_COOP_TESTXP12B"); static int done2;
            if (w && !done2 && coop_p2_t0 && coop_p2 >= 0x10000u && coop_rec_addr && !coop_depth && coop_now() - coop_p2_t0 >= 9000) {
                float a = (float)atof(w), b = strchr(w, ',') ? (float)atof(strchr(w, ',') + 1) : 0; done2 = 1;
                MEMF(coop_rec_addr + 0x90) = a; memcpy(coop_s2 + 0x400 + 0x90, &b, 4); coop_test_kc(w);
                tt_dbg("[COOPEXP] test: level experience set again: player 1 %.0f, player 2 %.0f\n", a, b); }
        }
    }
    {   /* bot test: TT_COOP_TESTEXP1 = experience: player 1's level running experience (his record +0x90) set 3 s after
         * player 2 takes over, once a level -- for his level-up on the post-level screens */
        const char *v = getenv("TT_COOP_TESTEXP1"); static unsigned lv; static int done; extern uint32_t coop_rec_addr;
        if (lv != coop_level_n) { lv = coop_level_n; done = 0; }
        if (v && !done && coop_p2_t0 && coop_p2 >= 0x10000u && coop_rec_addr && !coop_depth && coop_now() - coop_p2_t0 >= 3000) {
            done = 1; MEMF(coop_rec_addr + 0x90) = (float)atof(v);
            tt_dbg("[COOPEXP] test: player 1's level experience set to %s (record %08X: experience %d, level %d)\n", v, coop_rec_addr, (int32_t)MEM32(coop_rec_addr + 0x6C), (int32_t)MEM32(coop_rec_addr + 0x78)); }
    }
    {   /* bot test: TT_COOP_TESTAMMO = "p1,p2": both heroes' ammo (+0x304) set 5 s after player 2 takes over, once a level;
         * and (with it) each rise of either hero's ammo logged with where he stands, for the pickups */
        const char *v = getenv("TT_COOP_TESTAMMO"); static unsigned lv; static int done; static int16_t last[2];
        if (lv != coop_level_n) { lv = coop_level_n; done = 0; last[0] = last[1] = -1; }
        if (v && coop_p2_t0 && coop_p2 >= 0x10000u && coop_first_char >= 0x10000u && !coop_depth) {
            int w; if (!done && coop_now() - coop_p2_t0 >= 5000) { int a1 = atoi(v), a2 = strchr(v, ',') ? atoi(strchr(v, ',') + 1) : a1; done = 1;
                MEM16(coop_first_char + 0x304) = (uint16_t)a1; MEM16(coop_p2 + 0x304) = (uint16_t)a2; tt_dbg("[COOPAMMO] test: ammo set P1 %d P2 %d\n", a1, a2); }
            for (w = 0; w < 2 && done; w++) { uint32_t a = w ? coop_p2 : coop_first_char; int16_t n = (int16_t)MEM16(a + 0x304);
                if (last[w] >= 0 && n > last[w]) { uint32_t o = w ? coop_first_char : coop_p2; float dx = MEMF(a + 0xF0) - MEMF(o + 0xF0), dy = MEMF(a + 0xF4) - MEMF(o + 0xF4);
                    tt_dbg("[COOPAMMO] P%d ammo %d -> %d at %.1f %.1f (the other hero %.1f away)\n", w + 1, last[w], n, MEMF(a + 0xF0), MEMF(a + 0xF4), sqrtf(dx * dx + dy * dy)); }
                last[w] = n; }
        }
    }
    {   /* bot test: TT_COOP_TESTDOWN = "ms1,ms2": player 1 down ms1 after player 2 takes over, player 2 down at ms2 (both:
         * game over), once a level; for levels with no enemy near */
        const char *v = getenv("TT_COOP_TESTDOWN"); static int d1, d2; static unsigned lv;
        extern void coop_p1_down_now(void); extern void coop_p2_down_now(void);
        if (lv != coop_level_n) { lv = coop_level_n; d1 = d2 = 0; }
        if (v && coop_p2_t0 && coop_p2 >= 0x10000u) { char *e; uint32_t m1 = (uint32_t)strtoul(v, &e, 10), m2 = *e == ',' ? (uint32_t)strtoul(e + 1, NULL, 10) : 0xFFFFFFFFu, t = coop_now() - coop_p2_t0;
            if (!d1 && t >= m1) { d1 = 1; tt_dbg("[COOPDEATH] test: player 1 down\n"); coop_p1_down_now(); }
            if (!d2 && t >= m2) { d2 = 1; tt_dbg("[COOPDEATH] test: player 2 down\n"); coop_p2_down_now(); } }
    }
    {   /* bot test: TT_COOP_TESTUPG = "hero:upgrade,...": those upgrades bought (their byte in 0x4AC5B0 + hero * 0x50), once at
         * the first take-over (in memory only: a run that doesn't save) */
        const char *v = getenv("TT_COOP_TESTUPG"); static int done;
        if (v && !done && coop_p2_t0) { const char *p = v; done = 1;
            while (*p) { int h = atoi(p), u; const char *c = strchr(p, ':'); if (!c) break; u = atoi(c + 1);
                if (h >= 1 && h <= 4 && u >= 0 && u < 0x50) { MEM8(0x4AC5B0u + (uint32_t)h * 0x50u + (uint32_t)u) = 1; tt_dbg("[COOPMOVES] test: hero %d's upgrade %d bought\n", h, u); }
                p = strchr(c, ','); if (!p) break; p++; } }
    }
    if (getenv("TT_COOP_TESTP1HOLD") && coop_first_char >= 0x10000u && coop_p2_t0 && (int32_t)MEM32(coop_first_char + 0x2FC) > 0)   /* bot test: player 1's health kept full (player 2's tests at a boss) */
        MEM32(coop_first_char + 0x2FC) = MEM32(coop_first_char + 0x300);
    if (getenv("TT_COOP_TROLLLOG")) {   /* (research) the troll (and other big ones) once a second: state, health, flags, its target */
        static uint32_t lt, last_e; static int32_t last_hp = -99999; int i; uint32_t now = coop_now();
        for (i = 0; i < coop_nchars; i++) { uint32_t e = coop_chars[i], h = MEM32(e + 0x18), la; const char *nm = h >= 0x10000u && h < 0x84000000u ? (const char *)XBOX_PTR(h) : ""; int32_t hp;
            if (_strnicmp(nm, "TrollBoss", 9)) continue;
            hp = (int32_t)MEM32(e + 0x2FC); la = MEM32(e + 0x4BC);
            if (e == last_e && hp != last_hp) { uint32_t lh = la >= 0x10000u ? MEM32(la + 0x18) : 0;
                tt_dbg("[TROLL] health %d -> %d (%+d), state %u | its last attacker %08X '%s' (%s) state %u%c", last_hp, hp, hp - last_hp, MEM32(e + 0x408), la, lh >= 0x10000u && lh < 0x84000000u ? (const char *)XBOX_PTR(lh) : "?",
                       la == coop_first_char ? "player 1" : la == coop_p2 ? "player 2" : "other", la >= 0x10000u ? MEM32(la + 0x408) : 0, 10); }
            last_e = e; last_hp = hp; }
        if (now - lt >= 1000u) { lt = now;
            for (i = 0; i < coop_nchars; i++) { uint32_t e = coop_chars[i], h = MEM32(e + 0x18), c; const char *nm = h >= 0x10000u && h < 0x84000000u ? (const char *)XBOX_PTR(h) : "";
                if (_strnicmp(nm, "Troll", 5)) continue;
                c = MEM32(e + 0x494);
                tt_dbg("[TROLL] %s state %u (%.1f s) hp %d/%d +49C %08X +110 %08X +466 %u | target %08X | at %.1f %.1f %.1f | P1 %.1f P2 %.1f away%c", nm, MEM32(e + 0x408), MEMF(e + 0x420), (int32_t)MEM32(e + 0x2FC), (int32_t)MEM32(e + 0x300),
                       MEM32(e + 0x49C), MEM32(e + 0x110), MEM8(e + 0x466), c >= 0x10000u ? MEM32(c + 0x98) : 0, MEMF(e + 0xF0), MEMF(e + 0xF4), MEMF(e + 0xF8),
                       coop_first_char >= 0x10000u ? sqrtf((MEMF(e + 0xF0) - MEMF(coop_first_char + 0xF0)) * (MEMF(e + 0xF0) - MEMF(coop_first_char + 0xF0)) + (MEMF(e + 0xF4) - MEMF(coop_first_char + 0xF4)) * (MEMF(e + 0xF4) - MEMF(coop_first_char + 0xF4))) : -1.0f,
                       coop_p2 >= 0x10000u ? sqrtf((MEMF(e + 0xF0) - MEMF(coop_p2 + 0xF0)) * (MEMF(e + 0xF0) - MEMF(coop_p2 + 0xF0)) + (MEMF(e + 0xF4) - MEMF(coop_p2 + 0xF4)) * (MEMF(e + 0xF4) - MEMF(coop_p2 + 0xF4))) : -1.0f, 10); } } }
    {   /* bot test: TT_COOP_TESTHP1 / TT_COOP_TESTHP2 = "ms:hp": set a hero's health once, ms after player 2 takes over */
        static int done[2]; int h; for (h = 0; h < 2; h++) { const char *v = getenv(h ? "TT_COOP_TESTHP2" : "TT_COOP_TESTHP1"); uint32_t a = h ? coop_p2 : coop_first_char;
            if (v && !done[h] && coop_p2_t0 && a >= 0x10000u) { char *e; uint32_t ms = (uint32_t)strtoul(v, &e, 10); if (coop_now() - coop_p2_t0 >= ms && *e == ':') { MEM32(a + 0x2FC) = (uint32_t)strtol(e + 1, NULL, 10); done[h] = 1; tt_dbg("[COOPDEATH] test: P%d health set to %d\n", h + 1, (int32_t)MEM32(a + 0x2FC)); } } } }
    if (coop_mode() == 3) coop_watch50("9C380");
    {   /* (research) per frame: weapons' live-swing flag (+0x110 & 0x20000) and target (+0x420), originals vs copies */
        static unsigned fo, fc, to, tc, frames, so[3], sc[3]; int i; uint32_t now = coop_now(); static uint32_t last;
        for (i = 0; i < coop_nwpn; i++) { uint32_t o = coop_wpn_orig[i], w = coop_wpn_copy[i]; if (MEM32(o + 0x110) & 0x20000u) fo++; if (MEM32(w + 0x110) & 0x20000u) fc++; if (MEM32(o + 0x420)) to++; if (MEM32(w + 0x420)) tc++;
            if (MEM32(o + 0x578)) so[0]++; if (MEM32(o + 0x57C)) so[1]++; if (MEM32(o + 0x580)) so[2]++; if (MEM32(w + 0x578)) sc[0]++; if (MEM32(w + 0x57C)) sc[1]++; if (MEM32(w + 0x580)) sc[2]++; }
        frames++;
        if (coop_nwpn && now - last > 2000) { last = now; tt_dbg("[COOPSWING] %u frames: live flag %u / %u | target %u / %u | +578 %u / %u  +57C %u / %u  +580 %u / %u (player 1's / player 2's)\n", frames, fo, fc, to, tc, so[0], sc[0], so[1], sc[1], so[2], sc[2]); fo = fc = to = tc = frames = 0; memset(so, 0, sizeof so); memset(sc, 0, sizeof sc); }
    }
    if (coop_p2 && coop_p2_t0) { static int done; uint32_t t = coop_now() - coop_p2_t0; if (!done && t > 9500) { done = 1; tt_dbg("[COOPDIFF] --- mid-walk diff at %u ms ---\n", t); coop_twin_dump(MEM32(0x43B15Cu));
        {   int i; uint32_t k; for (i = 0; i < coop_nwpn && i < 1; i++) { uint32_t o = coop_wpn_orig[i], w = coop_wpn_copy[i];   /* player 1's weapon vs player 2's copy */
            tt_dbg("[COOPWDIFF] weapon %08X (player 1's) vs %08X (player 2's copy)\n", o, w);
            { uint32_t so = MEM32(o + 0x578), sc = MEM32(w + 0x578), k2;
              tt_dbg("[COOPWSRC] event source +578: original %08X (+4 %08X, listeners %d) copy %08X (+4 %08X, listeners %d)\n", so, so >= 0x10000u ? MEM32(so + 4) : 0, so >= 0x10000u ? (int32_t)MEM32(so + 0x174) : -1, sc, sc >= 0x10000u ? MEM32(sc + 4) : 0, sc >= 0x10000u ? (int32_t)MEM32(sc + 0x174) : -1);
              for (k2 = 0; so >= 0x10000u && k2 < MEM32(so + 0x174) && k2 < 8; k2++) { uint32_t li = MEM32(so + 0x74 + 4 * k2); tt_dbg("[COOPWSRC]   original listener %u: %08X vt %08X\n", k2, li, li >= 0x10000u ? MEM32(li) : 0); }
              for (k2 = 0; sc >= 0x10000u && k2 < MEM32(sc + 0x174) && k2 < 8; k2++) { uint32_t li = MEM32(sc + 0x74 + 4 * k2); tt_dbg("[COOPWSRC]   copy listener %u: %08X vt %08X\n", k2, li, li >= 0x10000u ? MEM32(li) : 0); } }
            for (k = 0; k < 0x400; k += 4) { uint32_t x = MEM32(o + k), y = MEM32(w + k); if (x != y && !(x >= o && x < o + 0x400 && y >= w && y < w + 0x400 && x - o == y - w) && !((x == coop_first_char || x == MEM32(0x43B15Cu)) && y == coop_p2)) tt_dbg("[COOPWDIFF] +%03X %08X %08X\n", k, x, y); } } } } }   /* (research) */
    if (coop_p2 >= 0x10000u && MEM32(0x43B15Cu) >= 0x10000u && !getenv("TT_COOP_NOLOCKMIRROR")) {   /* the level's lock flags (+0x464/+0x465: slot 1 only moves an actor when both are 0) follow player 1 */
        uint32_t q = MEM32(0x43B15Cu); static uint8_t last = 0xFF;
        if (MEM8(coop_p2 + 0x464) != MEM8(q + 0x464) || MEM8(coop_p2 + 0x465) != MEM8(q + 0x465)) {
            if (last != (uint8_t)(MEM8(q + 0x464) | MEM8(q + 0x465) << 1)) { last = (uint8_t)(MEM8(q + 0x464) | MEM8(q + 0x465) << 1); tt_dbg("[COOP2] lock flags +464/+465 for player 2: %u %u -> %u %u (player 1's)\n", MEM8(coop_p2 + 0x464), MEM8(coop_p2 + 0x465), MEM8(q + 0x464), MEM8(q + 0x465)); }
            MEM8(coop_p2 + 0x464) = MEM8(q + 0x464); MEM8(coop_p2 + 0x465) = MEM8(q + 0x465);
        }
        {   /* and the component's lock bits (c+0 bit 0x10000: slot 1 skips 0x868F0 while set; c+0x30 bit 0x400000) */
            uint32_t ca = MEM32(q + 0x494), ct = MEM32(coop_p2 + 0x494);
            if (ca >= 0x10000u && ct >= 0x10000u) {
                uint32_t n0 = (MEM32(ct) & ~0x18000u) | (MEM32(ca) & 0x18000u),   /* 0x8000: player-controlled (SetPlayer's), 0x10000: locked */ n30 = MEM32(ct + 0x30);   /* (c+0x30 is per-frame state: not mirrored) */
                if (n0 != MEM32(ct) || n30 != MEM32(ct + 0x30)) { static int k; if (k++ < 10) tt_dbg("[COOP2] component lock bits for player 2: c+0 %08X -> %08X, c+30 %08X -> %08X\n", MEM32(ct), n0, MEM32(ct + 0x30), n30); MEM32(ct) = n0; MEM32(ct + 0x30) = n30; }
            }
        }
    }
    p1 = MEM32(0x43B15Cu);
    if (!coop_p2 && coop_mode() == 2 && coop_p2_ally && p1 >= 0x10000u) {
        static uint32_t seen; static unsigned lv; if (lv != coop_level_n) { lv = coop_level_n; seen = 0; }
        if (!seen) seen = coop_now();
        if (coop_now() - seen > 3000) coop_take(coop_p2_ally);
    }
    if (!coop_p2 && coop_mode() == 3 && p1 >= 0x10000u) {
        /* (once per level: these were once per launch, so a co-op level after another level had no player 2:
         * co-op, then solo, then co-op) */
        static uint32_t seen, tried; static unsigned lv; if (lv != coop_level_n) { lv = coop_level_n; seen = tried = 0; }
        if (!seen) {
            seen = coop_now();
            if (coop_dbrec_seen && coop_twin >= 0x10000u && !getenv("TT_COOP_NOSTATS")) {   /* the twin's stats from the same database record */
                uint32_t sp = esp, ra = eax, rc = ecx, rd = edx;
                PUSH32(esp, coop_dbrec); PUSH32(esp, 0x00086B30u); ecx = coop_twin; sub_00086B30_gen();   /* thiscall, ret 4 */
                tt_dbg("[COOP2] twin %08X: slot 43 with %08X -> max hp %d hp %d, +3F8 %08X +53C %08X, component +50 %08X; esp %s\n", coop_twin, coop_dbrec,
                       (int32_t)MEM32(coop_twin + 0x300), (int32_t)MEM32(coop_twin + 0x2FC), MEM32(coop_twin + 0x3F8), MEM32(coop_twin + 0x53C), MEM32(MEM32(coop_twin + 0x494) + 0x50), esp == sp ? "balanced" : "NOT balanced");
                esp = sp; eax = ra; ecx = rc; edx = rd;
                coop_p2_own_type("after player 1's database record");   /* (that record put player 1's type back) */
            }
            coop_twin_dump(p1);
            if (!getenv("TT_COOP_NOFIX")) coop_twin_fix(p1);
        }
        if (!tried && coop_now() - seen > 3000) {
            uint32_t a = coop_twin == p1 ? coop_last_char : coop_twin; tried = 1;
            if (coop_twin == p1) a = coop_first_char;
            if (getenv("TT_COOP_NOTAKE")) { tt_dbg("[COOP2] TT_COOP_NOTAKE: twin %08X left alone\n", coop_twin); a = 0; }   /* (bisecting) */
            if (a >= 0x10000u && a != p1 && MEM32(a) == 0x1741C0u) {
                MEMF(a + 0xF0) = MEMF(p1 + 0xF0) + 1.5f; MEMF(a + 0xF4) = MEMF(p1 + 0xF4); MEMF(a + 0xF8) = MEMF(p1 + 0xF8);
                tt_dbg("[COOP2] twin before take-over: +114 %u hp %d (player 1 hp %d)\n", MEM32(a + 0x114), (int32_t)MEM32(a + 0x2FC), (int32_t)MEM32(p1 + 0x2FC));
                if (getenv("TT_COOP_P2_ROLE1")) MEM32(a + 0x114) = 1;   /* default: keep +114 = 2 (bisecting the HUD hang) */
                MEM32(a + 0x2FC) = MEM32(p1 + 0x2FC);
                coop_take(a); coop_place_frames = 4;
            } else tt_dbg("[COOP2] no twin to take over (twin %08X, player 1 %08X)\n", coop_twin, p1);
        }
    }
    if (coop_p2 && p1 >= 0x10000u && p1 != coop_p2 && !coop_depth) {   /* player 2's step (his pad is zeroed while he is down) */
        uint32_t ra = eax, rc = ecx, rd = edx, c1 = MEM8(0x43B1C0u), dt = MEM32(0x1B0C54u);   /* player 1's frame time as his step stored it (0x1B0C54 is in the swapped block) */
        { static int n; if (n++ < 3) tt_dbg("[COOPSTEP] player 2's step: [esp] %08X [esp+4] %08X, player 1's frame time %08X (%.4f)\n", MEM32(esp), MEM32(esp + 4), dt, *(float *)&dt); }
        coop_save(coop_s1);
        coop_load(coop_s2);
        MEM8(0x43B1C0u) = (uint8_t)c1;
        coop_depth = 1; coop_n_step++;
        PUSH32(esp, getenv("TT_COOP_OLDSTEPARG") ? MEM32(esp) : dt);   /* the same frame time as player 1's (until 2026-10-18 a stray stack word was passed, about 0: his rating meter and Perfect time never ran down) */
        PUSH32(esp, 0x0009C380u);         /* a return address for its ret */
        coop_gate_log(2);
        sub_0009C380_gen();
        esp += 4;
        coop_save(coop_s2);
        coop_load(coop_s1);
        coop_depth = 0;
        eax = ra; ecx = rc; edx = rd;
    }
    /* player 2's turning: each character's movement node (+0x164: +4 the next node, +8 its owner) holds the turn acceleration at
     * +0x34 and the top turn speed at +0x38 (rad/s; @SetTurnSpeed writes them). Player 1's are 94.2 and 9.4 on Balin's Tomb, the
     * twin's 40 and 20 (a playtest: "p2 doesnt match p1 rotation speed"). Player 1's top speed never holds him back (his 180 degree
     * turn speeds up the whole way, to 21 degrees a frame), while the twin's held him at 9.4 (a playtest: "p1 can quickly snap 180
     * rotation where p2 just steer"); with the acceleration as his top speed too, his turns matched player 1's frame for frame (bot
     * runs). Every frame. TT_COOP_NOTURNSPEED: off; TT_COOP_P2TURNMAX=rad/s: his top speed (research) */
    if (coop_p2 >= 0x10000u && p1 >= 0x10000u && p1 != coop_p2 && !getenv("TT_COOP_NOTURNSPEED")) {
        uint32_t a = MEM32(coop_p2 + 0x164), c = MEM32(p1 + 0x164);
        if (a >= 0x10000u && c >= 0x10000u && a != c && MEM32(a + 8) == coop_p2 && MEM32(c + 8) == p1) {
            float acc = MEMF(c + 0x34), top = MEMF(c + 0x38) > acc ? MEMF(c + 0x38) : acc;
            if (getenv("TT_COOP_P2TURNMAX")) top = (float)atof(getenv("TT_COOP_P2TURNMAX"));
            if (MEMF(a + 0x34) != acc || MEMF(a + 0x38) != top) {
                static int n; if (n++ < 4) tt_dbg("[COOPTURN] player 2's turn %.3f %.3f -> %.3f %.3f (player 1's %.3f %.3f)\n", MEMF(a + 0x34), MEMF(a + 0x38), acc, top, MEMF(c + 0x34), MEMF(c + 0x38));
                MEMF(a + 0x34) = acc; MEMF(a + 0x38) = top;
            }
        }
    }
    if (getenv("TT_COOP_TURNLOG") && coop_p2 >= 0x10000u && p1 >= 0x10000u) {   /* (research) each hero's facing, state and stick, every frame */
        uint32_t r1 = 0x6B2758u + 0x14Cu * 0u, r2 = 0x6B2758u + 0x14Cu * 1u;
        tt_dbg("[TURN] t %u dt %.4f | P1 f %.3f %.3f %.3f st %u stick %.2f %.2f ts %.3f %.3f (%08X) | P2 f %.3f %.3f %.3f st %u stick %.2f %.2f ts %.3f %.3f (%08X)\n", coop_now(), *(float *)&(uint32_t){ MEM32(0x1B0C54u) },
               MEMF(p1 + 0x70), MEMF(p1 + 0x74), MEMF(p1 + 0x78), MEM32(p1 + 0x408), MEMF(r1 + 0x14), MEMF(r1 + 0x18), MEM32(p1 + 0x164) >= 0x10000u ? MEMF(MEM32(p1 + 0x164) + 0x34) : 0.0f, MEM32(p1 + 0x164) >= 0x10000u ? MEMF(MEM32(p1 + 0x164) + 0x38) : 0.0f, MEM32(p1 + 0x164),
               MEMF(coop_p2 + 0x70), MEMF(coop_p2 + 0x74), MEMF(coop_p2 + 0x78), MEM32(coop_p2 + 0x408), MEMF(r2 + 0x14), MEMF(r2 + 0x18), MEM32(coop_p2 + 0x164) >= 0x10000u ? MEMF(MEM32(coop_p2 + 0x164) + 0x34) : 0.0f, MEM32(coop_p2 + 0x164) >= 0x10000u ? MEMF(MEM32(coop_p2 + 0x164) + 0x38) : 0.0f, MEM32(coop_p2 + 0x164));
    }
}

/* a placed character registered by the loader (0x000C5E20): remember the level's friendly one */
uint32_t coop_wobjs[512]; int coop_nwobjs;
void tt_coop_note_actor(uint32_t a)
{
    if (a >= 0x10000u && MEM32(a) == 0x174980u && coop_nwobjs < 512) coop_wobjs[coop_nwobjs++] = a;
    if (getenv("TT_COOP_REGLOG") && a >= 0x10000u) {   /* (research) every registered object: its class and name (TT_COOP_REGLOG=substring to filter, or 1) */
        uint32_t h = MEM32(a + 0x18); const char *nm = h >= 0x10000u && h < 0x84000000u ? (const char *)XBOX_PTR(h) : "?", *f = getenv("TT_COOP_REGLOG");
        char low[64]; int k; for (k = 0; k < 63 && nm[k]; k++) low[k] = (char)(nm[k] >= 'A' && nm[k] <= 'Z' ? nm[k] + 32 : nm[k]); low[k] = 0;
        if (!strcmp(f, "1") || strstr(low, f)) tt_dbg("[REG] %08X vt %08X +114 %u '%s' at %.1f %.1f %.1f\n", a, MEM32(a), MEM32(a + 0x114), nm, MEMF(a + 0xF0), MEMF(a + 0xF4), MEMF(a + 0xF8));
    }
    if (a >= 0x10000u && MEM32(a) == 0x1741C0u) { int i; for (i = 0; i < coop_nchars && coop_chars[i] != a; i++) ; if (i == coop_nchars) { if (coop_nchars < COOP_MAXCHARS) coop_chars[coop_nchars++] = a; else { static int full; if (!full++) tt_dbg("[COOP2] the character list is full (%d)\n", COOP_MAXCHARS); } } }
    if (coop_mode() == 2 && !coop_p2_ally && a >= 0x10000u && MEM32(a) == 0x1741C0u && MEM32(a + 0x114) == 2) {
        coop_p2_ally = a;
        tt_dbg("[COOP2] friendly character %08X at %.1f %.1f %.1f: player 2 stand-in\n", a, MEMF(a + 0xF0), MEMF(a + 0xF4), MEMF(a + 0xF8));
    }
}

static int coop_pick_kept(uint32_t pk);   /* (Pickups: Double, below) */
/* once a second: where both are, and what port 1's record holds */
void tt_coop_frame(void)
{
    static uint32_t last; uint32_t now = coop_now(), p1 = MEM32(0x43B15Cu), r = COOP_PADREC(1);
    if (!coop_mode() || now - last < 1000) return;
    last = now;
    if (getenv("TT_COOP_PICKTEST") && coop_p2 >= 0x10000u && coop_p2_t0 && now - coop_p2_t0 > 12000u && (now / 1000u) % 2u == 0) {
        /* (testing, Pickups: Double) every 2 s player 2 is put on a health or ammo pickup (vtable 0x1748B0, +0x3F8 bit 2
         * set: not yet gone), hurt and short of ammo, so his touches come */
        /* (the pickups' objects found once, in all of memory; then only those looked at) */
        static uint32_t pks[256]; static int npk = -1; static uint32_t lastpk; uint32_t va; int k;
        if (npk < 0) {
            static const uint32_t rgs[2][2] = { { 0x80000000u, 0x84000000u }, { 0x18B000u, 0x4000000u } }; int rg;
            npk = 0;
            for (rg = 0; rg < 2; rg++) for (va = rgs[rg][0]; va < rgs[rg][1] && npk < 256; va += 4) if (MEM32(va) == 0x1748B0u) pks[npk++] = va;
            tt_dbg("[COOPPICK] test: %d pickup objects\n", npk);
        }
        for (k = 0; k < npk; k++) {
            va = pks[k];
            if (MEM32(va) == 0x1748B0u && ((MEM32(va + 0x3F8) & 2) || coop_pick_kept(va)) && MEMF(va + 0xF0) != 0.0f && va != lastpk) {
                uint32_t a3[3]; float x = MEMF(va + 0xF0) + 0.2f, y = MEMF(va + 0xF4), z = MEMF(va + 0xF8);
                memcpy(&a3[0], &x, 4); memcpy(&a3[1], &y, 4); memcpy(&a3[2], &z, 4);
                if ((int32_t)MEM32(coop_p2 + 0x2FC) > 300) MEM32(coop_p2 + 0x2FC) = 300;
                MEM16(coop_p2 + 0x304) = 3;
                tt_dbg("[COOPPICK] test: player 2 put on pickup %08X (kind %u)\n", va, MEM32(va + 0x3D0));
                tt_gcall(0x000865D0u, coop_p2, 3, a3);   /* (the character's SetPosition) */
                lastpk = va;
                break;
            }
        }
    }
    if (coop_p2 && p1 >= 0x10000u)
        tt_dbg("[COOP2] t %6u  P1 %.2f %.2f %.2f hp %d | P2 %.2f %.2f %.2f hp %d +49C %08X | port1 h %08X stick %.2f %.2f held %08X\n",
               now - coop_p2_t0, MEMF(p1 + 0xF0), MEMF(p1 + 0xF4), MEMF(p1 + 0xF8), (int32_t)MEM32(p1 + 0x2FC),
               MEMF(coop_p2 + 0xF0), MEMF(coop_p2 + 0xF4), MEMF(coop_p2 + 0xF8), (int32_t)MEM32(coop_p2 + 0x2FC), MEM32(coop_p2 + 0x49C),
               MEM32(r + 0x144), MEMF(r + 0x14), MEMF(r + 0x18), MEM32(r + 0x34));
    if (coop_p2 && p1 >= 0x10000u && MEM32(0x43B164u) >= 0x10000u) {   /* the camera's look-at point (+0x40) against both heroes and their midpoint */
        uint32_t cm = MEM32(0x43B164u); float lx = MEMF(cm + 0x40), ly = MEMF(cm + 0x44), mx = 0.5f * (MEMF(p1 + 0xF0) + MEMF(coop_p2 + 0xF0)), my = 0.5f * (MEMF(p1 + 0xF4) + MEMF(coop_p2 + 0xF4));
        float d1 = sqrtf((lx - MEMF(p1 + 0xF0)) * (lx - MEMF(p1 + 0xF0)) + (ly - MEMF(p1 + 0xF4)) * (ly - MEMF(p1 + 0xF4))), d2 = sqrtf((lx - MEMF(coop_p2 + 0xF0)) * (lx - MEMF(coop_p2 + 0xF0)) + (ly - MEMF(coop_p2 + 0xF4)) * (ly - MEMF(coop_p2 + 0xF4))), dm = sqrtf((lx - mx) * (lx - mx) + (ly - my) * (ly - my));
        { static unsigned lc; if (coop_leash_stops != lc) { tt_dbg("[COOPLEASH] stopped at the leash on %u steps so far\n", coop_leash_stops); lc = coop_leash_stops; } }
        {   /* combat: player 2's state, his weapons against him and player 1's against player 1, the enemies' health */
            int i; float dw[4] = { -1, -1, -1, -1 }, ow[4] = { -1, -1, -1, -1 }; int ehp = 0, ne = 0;
            for (i = 0; i < coop_nwpn; i++) {
                uint32_t w = coop_wpn_copy[i], o = coop_wpn_orig[i];
                dw[i] = sqrtf((MEMF(w + 0xF0) - MEMF(coop_p2 + 0xF0)) * (MEMF(w + 0xF0) - MEMF(coop_p2 + 0xF0)) + (MEMF(w + 0xF4) - MEMF(coop_p2 + 0xF4)) * (MEMF(w + 0xF4) - MEMF(coop_p2 + 0xF4)));
                ow[i] = sqrtf((MEMF(o + 0xF0) - MEMF(p1 + 0xF0)) * (MEMF(o + 0xF0) - MEMF(p1 + 0xF0)) + (MEMF(o + 0xF4) - MEMF(p1 + 0xF4)) * (MEMF(o + 0xF4) - MEMF(p1 + 0xF4)));
            }
            for (i = 0; i < coop_nchars; i++) if (MEM32(coop_chars[i] + 0x114) == 3) { ehp += (int32_t)MEM32(coop_chars[i] + 0x2FC); ne++; }
            {   /* nearest living enemy from each player (ground plane) */
                float best[2] = { 1e9f, 1e9f }; uint32_t pl[2] = { coop_first_char, coop_p2 }; int h;
                for (i = 0; i < coop_nchars; i++) { uint32_t e = coop_chars[i]; if (MEM32(e + 0x114) != 3 || (int32_t)MEM32(e + 0x2FC) <= 0) continue;
                    for (h = 0; h < 2; h++) { float dx = MEMF(e + 0xF0) - MEMF(pl[h] + 0xF0), dy = MEMF(e + 0xF4) - MEMF(pl[h] + 0xF4), d = sqrtf(dx * dx + dy * dy); if (d < best[h]) best[h] = d; } }
                tt_dbg("[COOPNEAR] nearest enemy: from P1 %.1f, from P2 %.1f\n", best[0], best[1]);
                {   /* whom the living enemies target (their component +0x98), and both heroes' health */
                    int on1 = 0, on2 = 0, other = 0, none = 0;
                    for (i = 0; i < coop_nchars; i++) { uint32_t e = coop_chars[i], ce, t; if (MEM32(e + 0x114) != 3 || (int32_t)MEM32(e + 0x2FC) <= 0) continue;
                        ce = MEM32(e + 0x494); t = ce >= 0x10000u ? MEM32(ce + 0x98) : 0;
                        if (t == coop_first_char) on1++; else if (t == coop_p2) on2++; else if (t) other++; else none++;
                        if (t == coop_p2) {   /* (research) where the enemies on player 2 are: distance to each hero */
                            float a1 = MEMF(e + 0xF0) - MEMF(coop_first_char + 0xF0), b1 = MEMF(e + 0xF4) - MEMF(coop_first_char + 0xF4), a2 = MEMF(e + 0xF0) - MEMF(coop_p2 + 0xF0), b2 = MEMF(e + 0xF4) - MEMF(coop_p2 + 0xF4);
                            tt_dbg("[COOPEDIST] enemy %08X on P2: %.1f from P2, %.1f from P1, state %u\n", e, sqrtf(a2 * a2 + b2 * b2), sqrtf(a1 * a1 + b1 * b1), MEM32(e + 0x408)); } }
                    tt_dbg("[COOPEAGGRO] living enemies targeting P1 %d, P2 %d, other %d, none %d | hp P1 %d P2 %d | state P1 %u P2 %u | c+30/34 P1 %08X %08X P2 %08X %08X | hits P1 %u P2 %u | 49C P1 %08X P2 %08X 110 P1 %08X P2 %08X | threat switches to P1 %u to P2 %u, sets %u\n", on1, on2, other, none, (int32_t)MEM32(coop_first_char + 0x2FC), (int32_t)MEM32(coop_p2 + 0x2FC), MEM32(coop_first_char + 0x408), MEM32(coop_p2 + 0x408), MEM32(MEM32(coop_first_char + 0x494) + 0x30), MEM32(MEM32(coop_first_char + 0x494) + 0x34), MEM32(MEM32(coop_p2 + 0x494) + 0x30), MEM32(MEM32(coop_p2 + 0x494) + 0x34), coop_herohits[0], coop_herohits[1], MEM32(coop_first_char + 0x49C), MEM32(coop_p2 + 0x49C), MEM32(coop_first_char + 0x110), MEM32(coop_p2 + 0x110), coop_thr_switch[0], coop_thr_switch[1], coop_thr_set);
                }
                { extern void tt_coop_box_dump(void); extern void tt_coop_bt_dump(void); tt_coop_box_dump(); tt_coop_bt_dump(); }
                {   /* each hero's target: component +0x98 (@ActorGetTarget, @LockHeadingToTarget, the contact handler 0xBE0D0) */
                    uint32_t c1 = MEM32(coop_first_char + 0x494), c2 = MEM32(coop_p2 + 0x494), t1 = c1 >= 0x10000u ? MEM32(c1 + 0x98) : 0, t2 = c2 >= 0x10000u ? MEM32(c2 + 0x98) : 0;
                    { uint32_t m = MEM32(0x49E8DCu); static int k; if (m >= 0x10000u && k++ < 2) tt_dbg("[COOPMGR] contact manager %08X: +0 %08X +4 %08X +8 %08X +C %08X +10 %08X +14 %08X | first box %08X flags %08X next %08X\n", m, MEM32(m), MEM32(m + 4), MEM32(m + 8), MEM32(m + 12), MEM32(m + 16), MEM32(m + 20),
                        MEM32(m + 4), MEM32(m + 4) >= 0x10000u ? MEM32(MEM32(m + 4)) : 0, MEM32(m + 4) >= 0x10000u ? MEM32(MEM32(m + 4) + 4) : 0); }
                    tt_dbg("[COOPFLAGS] +49C P1 %08X P2 %08X | +464..467 P1 %08X P2 %08X\n", MEM32(coop_first_char + 0x49C), MEM32(coop_p2 + 0x49C), MEM32(coop_first_char + 0x464), MEM32(coop_p2 + 0x464));
                    tt_dbg("[COOPTGT] P1 target %08X (role %d hp %d) | P2 target %08X (role %d hp %d)\n", t1, t1 >= 0x10000u ? (int)MEM32(t1 + 0x114) : -1, t1 >= 0x10000u ? (int)MEM32(t1 + 0x2FC) : 0,
                           t2, t2 >= 0x10000u ? (int)MEM32(t2 + 0x114) : -1, t2 >= 0x10000u ? (int)MEM32(t2 + 0x2FC) : 0);
                }
                if (getenv("TT_COOP_TESTNEAR") && coop_p2 && best[1] > 2.2f && best[1] < 1e8f) {   /* bot test only: put player 2 1.6 units from the nearest enemy, on his side */
                    uint32_t e = 0; float bd = 1e9f;
                    for (i = 0; i < coop_nchars; i++) { uint32_t q = coop_chars[i]; float dx, dy, d; if (MEM32(q + 0x114) != 3 || (int32_t)MEM32(q + 0x2FC) <= 0) continue;
                        dx = MEMF(q + 0xF0) - MEMF(coop_p2 + 0xF0); dy = MEMF(q + 0xF4) - MEMF(coop_p2 + 0xF4); d = sqrtf(dx * dx + dy * dy); if (d < bd) { bd = d; e = q; } }
                    if (e) { float dx = MEMF(coop_p2 + 0xF0) - MEMF(e + 0xF0), dy = MEMF(coop_p2 + 0xF4) - MEMF(e + 0xF4);
                        MEMF(coop_p2 + 0xF0) = MEMF(e + 0xF0) + dx * (1.6f / bd); MEMF(coop_p2 + 0xF4) = MEMF(e + 0xF4) + dy * (1.6f / bd); MEMF(coop_p2 + 0xF8) = MEMF(e + 0xF8);
                        MEM32(coop_p2 + 0x90) = MEM32(coop_p2 + 0xF0); MEM32(coop_p2 + 0x94) = MEM32(coop_p2 + 0xF4); MEM32(coop_p2 + 0x98) = MEM32(coop_p2 + 0xF8);
                        tt_dbg("[COOPNEAR] test: player 2 put 1.6 from enemy %08X (was %.1f)\n", e, bd); }
                }
            }
            { extern unsigned coop_ispw_count(int); tt_dbg("[COOPFIGHT]   @IsPlayerWeapon calls so far %u, on player 2's weapons %u\n", coop_ispw_count(0), coop_ispw_count(1)); }
            tt_dbg("[COOPFIGHT] t %6u P2 state %u (+3E0 %08X) | P2 weapons from P2 %.2f %.2f %.2f | P1 weapons from P1 %.2f %.2f %.2f | %d enemies hp %d | pad2 held %08X\n", now - coop_p2_t0,
                   MEM32(coop_p2 + 0x408), MEM32(coop_p2 + 0x3E0), dw[0], dw[1], dw[2], ow[0], ow[1], ow[2], ne, ehp, MEM32(r + 0x34));
        }
        tt_dbg("[COOPCAM2] t %6u look-at %.2f %.2f | to P1 %.2f to P2 %.2f to midpoint %.2f | heroes apart %.2f\n", now - coop_p2_t0, lx, ly, d1, d2, dm, sqrtf((MEMF(p1 + 0xF0) - MEMF(coop_p2 + 0xF0)) * (MEMF(p1 + 0xF0) - MEMF(coop_p2 + 0xF0)) + (MEMF(p1 + 0xF4) - MEMF(coop_p2 + 0xF4)) * (MEMF(p1 + 0xF4) - MEMF(coop_p2 + 0xF4))));
    }
    if (coop_p2) { float v[5]; static const int o[5] = { 0x48, 0x50, 0x74, 0x90, 0x98 }; int k; for (k = 0; k < 5; k++) memcpy(&v[k], coop_s2 + o[k], 4);
        tt_dbg("[COOP2]          steps %u enters %u leaves %u depth %d\n", coop_n_step, coop_n_enter, coop_n_leave, coop_depth);
        { extern void tt_coop_trace_dump(void); tt_coop_trace_dump(); } tt_coop_p1_dump(); { extern void tt_coop_weapon_methods_dump(void); extern void tt_coop_brainmsg_dump(void); static unsigned k; if (++k % 5 == 0) { tt_coop_weapon_methods_dump(); tt_coop_brainmsg_dump(); } }
        { int i; for (i = 0; i < coop_mvn; i++) tt_dbg("[COOP2]          P2 moved by %08X: %u calls, sum of squared steps %.4f\n", coop_mv[i].fn, coop_mv[i].n, coop_mv[i].d); coop_mvn = 0; }
        tt_dbg("[COOP2]          P2 state 43B168 %.3f 43B170 %.3f 43B194 %.3f 43B1B0 %.3f 43B1B8 %.3f | P1 %.3f %.3f %.3f %.3f %.3f\n", v[0], v[1], v[2], v[3], v[4],
               MEMF(0x43B168u), MEMF(0x43B170u), MEMF(0x43B194u), MEMF(0x43B1B0u), MEMF(0x43B1B8u)); }
}

/* spawn research: the loader's two character builders */
extern void sub_000F5310_gen(void);
void sub_000F5310(void)
{
    uint32_t w = ecx, a1 = MEM32(esp + 4), a2 = MEM32(esp + 8), a3 = MEM32(esp + 12), ra = MEM32(esp);
    sub_000F5310_gen();
    if (coop_probe_on() || coop_mode())
        tt_dbg("[COOPSPAWN] F5310(world %08X; %08X %08X %08X) from %08X -> %08X vt %08X +114 %u\n", w, a1, a2, a3, ra, eax,
               eax >= 0x10000u ? MEM32(eax) : 0, eax >= 0x10000u ? MEM32(eax + 0x114) : 0);
}
extern void sub_000F3430_gen(void);
void sub_000F3430(void)
{
    uint32_t w = ecx, a1 = MEM32(esp + 4), a2 = MEM32(esp + 8), ra = MEM32(esp);
    sub_000F3430_gen();
    coop_last_char = eax;
    if (coop_probe_on() || coop_mode())
        tt_dbg("[COOPSPAWN] F3430(world %08X; %08X %08X) from %08X -> %08X vt %08X +114 %u\n", w, a1, a2, ra, eax,
               eax >= 0x10000u ? MEM32(eax) : 0, eax >= 0x10000u ? MEM32(eax + 0x114) : 0);
}
/* player 2's own actor methods run with his globals in place, so the branches that test
 * "this == [0x43B15C]" (applying the control step's results) treat him as a player. Called by the
 * generated slot wrappers of vtable 0x1741C0 (COOP-COUNT block). Not nested: a call inside
 * player 2's swapped context keeps the context it is in. */
/* coop_dbrec: the character-database record slot 43 (0x86B30) gave player 1 */
static uint32_t coop_s1_player(void) { uint32_t i, o = 0, v; for (i = 0; i < COOP_NBLK && coop_blk[i].a != 0x43B15Cu; i++) o += coop_blk[i].n; memcpy(&v, coop_s1 + o, 4); return v; }
/* a method on one of player 2's weapon copies: his globals in place, as for his own methods */
int tt_coop_enter_weapon(uint32_t self)
{
    int i;
    if (!coop_p2 || coop_depth || MEM32(0x43B15Cu) == coop_p2 || self < 0x10000u) return 0;
    for (i = 0; i < coop_nwpn; i++) if (coop_wpn_copy[i] == self) break;
    if (i == coop_nwpn) return 0;
    { uint32_t c1 = MEM8(0x43B1C0u); coop_save(coop_s1); coop_load(coop_s2); MEM8(0x43B1C0u) = (uint8_t)c1; }
    coop_depth = 1; coop_cur_fn = 0;
    return 1;
}
/* player 2's globals in place (as in his control step), for code that runs for him outside it */
int tt_coop_enter_ctx(void)
{
    if (!coop_p2 || coop_depth || MEM32(0x43B15Cu) == coop_p2) return 0;
    { uint32_t c1 = MEM8(0x43B1C0u); coop_save(coop_s1); coop_load(coop_s2); MEM8(0x43B1C0u) = (uint8_t)c1; }
    coop_depth = 1; coop_cur_fn = 0;
    return 1;
}
/* (research) weapon-class methods: calls on player 1's weapons vs player 2's copies */
static struct { uint32_t fn; unsigned o, c; } coop_wm[64]; static int coop_nwm;
int tt_coop_enter_weapon2(uint32_t self, uint32_t fn)
{
    int i, w = -1;
    for (i = 0; i < coop_nwpn; i++) { if (coop_wpn_orig[i] == self) w = 0; if (coop_wpn_copy[i] == self) w = 1; }
    if (w >= 0) {
        for (i = 0; i < coop_nwm && coop_wm[i].fn != fn; i++) ;
        if (i == coop_nwm && coop_nwm < 64) { coop_wm[i].fn = fn; coop_wm[i].o = coop_wm[i].c = 0; coop_nwm++; }
        if (i < 64) { if (w) coop_wm[i].c++; else coop_wm[i].o++; }
    }
    if (fn == 0x0008FF50u && w >= 0 && coop_p2 && MEM32(esp + 4)) {   /* (research) a weapon is handed a target (+0x420): by whom */
        static int k[2]; uint32_t q, cs[14] = { 0 }; int m = 0, ww = w;
        for (q = 0; q < 0x600 && m < 14; q += 4) { uint32_t v = MEM32(esp + q); if (v >= 0x12000u && v < 0x170000u) cs[m++] = v; }
        if (k[ww]++ < 5) tt_dbg("[COOPWTGT] %s %08X gets target %08X (vt %08X role %u) | player %08X depth %d | stack %X %X %X %X %X %X %X %X %X %X %X %X %X %X\n", ww ? "P2's copy" : "P1's weapon", self, MEM32(esp + 4), MEM32(MEM32(esp + 4)), MEM32(MEM32(esp + 4) + 0x114), MEM32(0x43B15Cu), coop_depth,
                                cs[0], cs[1], cs[2], cs[3], cs[4], cs[5], cs[6], cs[7], cs[8], cs[9], cs[10], cs[11], cs[12], cs[13]);
    }
    if ((fn == 0x00091AC0u || fn == 0x000905D0u || fn == 0x00090BC0u) && w == 0 && coop_p2 && 0) {   /* (research) who calls these on player 1's weapons */
        static int k; uint32_t q, cs[14] = { 0 }; int m = 0;
        for (q = 0; q < 0x600 && m < 14; q += 4) { uint32_t v = MEM32(esp + q); if (v >= 0x12000u && v < 0x170000u) cs[m++] = v; }
        if (k++ < 9) tt_dbg("[COOPWCALL] %08X on P1's weapon %08X args %08X %08X | stack %X %X %X %X %X %X %X %X %X %X %X %X %X %X\n", fn, self, MEM32(esp + 4), MEM32(esp + 8),
                            cs[0], cs[1], cs[2], cs[3], cs[4], cs[5], cs[6], cs[7], cs[8], cs[9], cs[10], cs[11], cs[12], cs[13]);
    }
    if (fn == 0x000905D0u && w >= 0 && coop_p2) {   /* (research) the update's gates: +110 bit 13, [0x1B0C21], +5EC mode, +578, +420 */
        static unsigned g[2][6]; static uint32_t last; uint32_t f = MEM32(self + 0x110);
        g[w][0]++; if ((f >> 13) & 1) g[w][1]++; if (MEM8(0x1B0C21u)) g[w][2]++; if (((MEM32(self + 0x5EC) >> 9) & 3) == 1) g[w][3]++; if (MEM32(self + 0x420)) g[w][4]++; if (f & 0x20000u) g[w][5]++;
        if (coop_now() - last > 5000) { last = coop_now(); for (i = 0; i < 2; i++) tt_dbg("[COOPWGATE] %s: updates %u, +110 bit13 %u, [1B0C21] %u, +5EC mode 1 %u, +420 set %u, +110 0x20000 %u\n", i ? "P2's copies" : "P1's weapons", g[i][0], g[i][1], g[i][2], g[i][3], g[i][4], g[i][5]); memset(g, 0, sizeof g); }
    }
    if (fn == 0x000905D0u && w >= 0 && !coop_phase) coop_phase = w ? 2 : 1;   /* COOP-TRACE: callees of weapon slot 3 by owner */
    return tt_coop_enter_weapon(self);
}
void tt_coop_weapon_post(uint32_t fn) { if (fn == 0x000905D0u) coop_phase = 0; }
void tt_coop_weapon_methods_dump(void)
{
    int i; for (i = 0; i < coop_nwm; i++) tt_dbg("[COOPWM] %08X  on player 1's weapons %u, on player 2's copies %u\n", coop_wm[i].fn, coop_wm[i].o, coop_wm[i].c);
    coop_nwm = 0;
}int tt_coop_enter(uint32_t self, uint32_t fn)
{
    if (fn == 0x00082D70u && coop_mode() == 3 && self == coop_twin && self >= 0x10000u && !getenv("TT_COOP_NOPLAYERINIT")) {   /* the loader's walk inits the twin: as it does the player, with +0x49C bit 0 */
        uint32_t v = MEM32(self + 0x49C);
        MEM32(self + 0x49C) = (v & ~6u) | 1u;
        tt_dbg("[COOP2] twin %08X: slot 0 (init) as a player: +49C %08X -> %08X\n", self, v, MEM32(self + 0x49C));
    }
    if (fn == 0x00086B30u && coop_mode() == 3 && self == coop_first_char && !coop_dbrec_seen) { coop_dbrec = MEM32(esp + 4); coop_dbrec_seen = 1; tt_dbg("[COOP2] player 1's stats: slot 43 (0x86B30) with database record %08X\n", coop_dbrec); }
    if (coop_p2 && self != coop_p2) return tt_coop_enter_weapon(self);   /* methods shared with the weapon class: his weapon copies too */
    if (!coop_p2 || self != coop_p2 || coop_depth || MEM32(0x43B15Cu) == coop_p2) return 0;
    { uint32_t c1 = MEM8(0x43B1C0u); coop_save(coop_s1); coop_load(coop_s2); MEM8(0x43B1C0u) = (uint8_t)c1; }
    coop_depth = 1; coop_n_enter++;
    coop_cur_fn = fn; coop_pre[0] = MEMF(self + 0xF0); coop_pre[1] = MEMF(self + 0xF4); coop_pre[2] = MEMF(self + 0xF8);
    return 1;
}
static int coop_ctx_out;   /* player 2's context left early (coop_scene_cmd): the leave that ends it only clears the depth */
void tt_coop_leave(void)
{
    if (coop_ctx_out) { coop_ctx_out = 0; coop_depth = 0; coop_n_leave++; return; }
    if (coop_cur_fn == 0x000843F0u && coop_p2 >= 0x10000u && !getenv("TT_COOP_NOLEASH") && coop_now() - coop_bot_drove_t > 500u) {   /* (not while the P2 Bot drives him: its way round a rock can lead away first; it has its own catch-up) */   /* leash: undo a step of player 2's that takes him out past the radius */
        extern float coop_leash_radius(void); extern unsigned coop_leash_stops;
        uint32_t q = coop_s1_player(), a = coop_p2; float R = coop_leash_radius();
        if (q >= 0x10000u && q != a) {
            float ox = coop_pre[0] - MEMF(q + 0xF0), oy = coop_pre[1] - MEMF(q + 0xF4), nx = MEMF(a + 0xF0) - MEMF(q + 0xF0), ny = MEMF(a + 0xF4) - MEMF(q + 0xF4);
            float d0 = ox * ox + oy * oy, d1 = nx * nx + ny * ny;
            if (d1 > R * R && d1 > d0) { MEMF(a + 0xF0) = coop_pre[0]; MEMF(a + 0xF4) = coop_pre[1]; MEM32(a + 0x90) = MEM32(a + 0xF0); MEM32(a + 0x94) = MEM32(a + 0xF4); coop_leash_stops++; }
        }
    }
    {   float dx = MEMF(coop_p2 + 0xF0) - coop_pre[0], dy = MEMF(coop_p2 + 0xF4) - coop_pre[1], dz = MEMF(coop_p2 + 0xF8) - coop_pre[2], d = dx * dx + dy * dy + dz * dz; int i;
        if (d > 1e-8f) { for (i = 0; i < coop_mvn && coop_mv[i].fn != coop_cur_fn; i++) ; if (i == coop_mvn && coop_mvn < 32) { coop_mv[i].fn = coop_cur_fn; coop_mv[i].n = 0; coop_mv[i].d = 0; coop_mvn++; } if (i < 32) { coop_mv[i].n++; coop_mv[i].d += d; } } }
    coop_save(coop_s2); coop_load(coop_s1);
    coop_depth = 0; coop_n_leave++;
}/* research: the message queue (0x43C628) delivers node->target->vtable[+0x54](type, node). A message to
 * a target that cannot take it is logged with the code addresses on the stack (who sent it). */
static void coop_watch50(const char *where);
extern void sub_000C6F60_gen(void);
void sub_000C6F60(void)
{
    uint32_t n = MEM32(esp + 4);
    if (n >= 0x10000u && MEM32(n + 8) < 0x10000u && tt_vs_on() && !getenv("TT_VS_KEEPNULLMSG")) {
        /* (Versus) a message to no one -- to a story object the arena left out (a hero's swing tells the battle
         * manager): dropped (sent, the game hung) */
        static int n2; if (n2++ < 8) tt_dbg("[VS] message type %u to no one: dropped\n", MEM8(n));
        eax = 0; esp += 8; return;
    }
    if (coop_mode() == 3) coop_watch50("C6F60 post");
    if (coop_mode() == 3 && coop_p2 && n >= 0x10000u && getenv("TT_COOP_HEROMSG") && (MEM32(n + 8) == coop_first_char || MEM32(n + 8) == coop_twin)) {   /* (research) messages to the heroes: type, source, poster */
        static unsigned cnt[2][64]; uint32_t t = MEM32(n + 8), ty = MEM8(n), s = MEM32(n + 4); int w = t == coop_twin;
        if (ty < 64 && ty != 10 && cnt[w][ty]++ < 3) { uint32_t q, cs[10] = { 0 }; int m = 0; for (q = 0; q < 0x400 && m < 10; q += 4) { uint32_t v = MEM32(esp + q); if (v >= 0x12000u && v < 0x170000u) cs[m++] = v; }
            tt_dbg("[COOPHMSG] type %u to %s from %08X (vt %08X role %d) +C %08X +10 %08X | stack %X %X %X %X %X %X %X %X %X %X\n", ty, w ? "twin" : "P1", s, s >= 0x10000u ? MEM32(s) : 0, s >= 0x10000u ? (int)MEM32(s + 0x114) : -1, MEM32(n + 12), MEM32(n + 16), cs[0], cs[1], cs[2], cs[3], cs[4], cs[5], cs[6], cs[7], cs[8], cs[9]); }
    }
    if (coop_mode() == 3 && coop_p2 && n >= 0x10000u && MEM8(n) == 25) {   /* (research) contact records: who posts them, attacker +8, weapon +14 */
        static int k; uint32_t a = MEM32(n + 8), wp = MEM32(n + 20), q, cs[10] = { 0 }; int m = 0, i, tag = 0;
        for (i = 0; i < coop_nwpn; i++) { if (wp == coop_wpn_orig[i]) tag = 1; if (wp == coop_wpn_copy[i]) tag = 2; }
        if (a == coop_twin || tag == 2 || k < 16) { for (q = 0; q < 0x400 && m < 10; q += 4) { uint32_t v = MEM32(esp + q); if (v >= 0x12000u && v < 0x170000u) cs[m++] = v; }
            if (k++ < 40) tt_dbg("[COOPC25] contact: attacker %08X%s weapon %08X%s +18 %08X +1C %08X | stack %X %X %X %X %X %X %X %X %X %X\n", a, a == coop_first_char ? " (P1)" : a == coop_twin ? " (twin)" : "",
                                  wp, tag == 1 ? " (P1's)" : tag == 2 ? " (P2's copy)" : "", MEM32(n + 24), MEM32(n + 28), cs[0], cs[1], cs[2], cs[3], cs[4], cs[5], cs[6], cs[7], cs[8], cs[9]); }
    }
    if (coop_mode() == 3 && coop_p2 && n >= 0x10000u) {   /* (research) messages to enemies (role 3), not ticks: type, source, poster */
        uint32_t t = MEM32(n + 8), ty = MEM8(n);
        if (t >= 0x10000u && MEM32(t) == 0x1741C0u && MEM32(t + 0x114) == 3 && ty != 10) {
            static unsigned cnt[64]; static int k; uint32_t s = MEM32(n + 4);
            if (ty == 3) { extern void coop_threat_hit(uint32_t enemy, uint32_t weapon); coop_threat_hit(t, s); }   /* threat: who hit this enemy */
            if (ty == 40 && ebx >= 0x10000u) { static int k40; if (k40++ < 12) tt_dbg("[COOPCONTACT] contact record %08X: +4 %08X +8 %08X%s +C %08X +10 %08X +14 %08X%s +18 %08X +1C %08X | victim %08X\n", ebx, MEM32(ebx + 4), MEM32(ebx + 8),
                MEM32(ebx + 8) == coop_first_char ? " (P1)" : MEM32(ebx + 8) == coop_twin ? " (twin)" : "", MEM32(ebx + 12), MEM32(ebx + 16), MEM32(ebx + 20),
                MEM32(ebx + 20) == coop_wpn_orig[0] || MEM32(ebx + 20) == coop_wpn_orig[1] || MEM32(ebx + 20) == coop_wpn_orig[2] ? " (P1's weapon)" : MEM32(ebx + 20) == coop_wpn_copy[0] || MEM32(ebx + 20) == coop_wpn_copy[1] || MEM32(ebx + 20) == coop_wpn_copy[2] ? " (P2's copy)" : "",
                MEM32(ebx + 24), MEM32(ebx + 28), esi); }
            if (ty < 64 && cnt[ty]++ < 3 && k++ < 60) { uint32_t q, cs[12] = { 0 }; int m = 0; for (q = 0; q < 0x400 && m < 12; q += 4) { uint32_t v = MEM32(esp + q); if (v >= 0x12000u && v < 0x170000u) cs[m++] = v; }
                tt_dbg("[COOPEMSG] type %u to enemy %08X from %08X (vt %08X%s) +C %08X +10 %08X | stack %X %X %X %X %X %X %X %X %X %X\n", ty, t, s, s >= 0x10000u ? MEM32(s) : 0,
                       s == coop_first_char ? " P1" : s == coop_twin ? " twin" : "", MEM32(n + 12), MEM32(n + 16), cs[0], cs[1], cs[2], cs[3], cs[4], cs[5], cs[6], cs[7], cs[8], cs[9]); }
        }
    }
    if (coop_mode() == 3 && coop_p2 && n >= 0x10000u && coop_nwpn) {   /* (research) messages to player 1's first weapon vs its copy: type, poster */
        static int k[2]; uint32_t t = MEM32(n + 8); int w = t == coop_wpn_orig[0] ? 0 : t == coop_wpn_copy[0] ? 1 : -1;
        if (w >= 0 && k[w]++ < 6) { uint32_t q, cs[12] = { 0 }; int m = 0; for (q = 0; q < 0x400 && m < 12; q += 4) { uint32_t v = MEM32(esp + q); if (v >= 0x12000u && v < 0x170000u) cs[m++] = v; }
            tt_dbg("[COOPWMSG] type %u to %s (source %08X%s) | stack %X %X %X %X %X %X %X %X %X %X %X %X\n", MEM8(n), w ? "the copy" : "P1's weapon", MEM32(n + 4), MEM32(n + 4) == coop_first_char ? " P1" : MEM32(n + 4) == coop_twin ? " twin" : "",
                   cs[0], cs[1], cs[2], cs[3], cs[4], cs[5], cs[6], cs[7], cs[8], cs[9], cs[10], cs[11]); }
    }
    if (coop_mode() && n >= 0x10000u) {   /* (research) messages to brain machines (class 0x171948), by type and by whose context posted them */
        extern void coop_note_brainmsg(uint32_t t, uint32_t type);
        uint32_t t0 = MEM32(n + 8); if (t0 >= 0x10000u && MEM32(t0) == 0x171948u) coop_note_brainmsg(t0, MEM8(n));
        if (t0 >= 0x10000u && MEM32(t0) == 0x171948u) {   /* (research) the hero brain's messages: which character, payload */
            static unsigned cnt[2][4]; static int lg; uint32_t s = MEM32(n + 4), w = s == coop_first_char ? 0 : s == coop_twin ? 1 : 2, ty = MEM8(n);
            cnt[ty == 10 ? 0 : 1][w]++;
            if ((ty != 10 || lg < 6) && lg++ < 40) tt_dbg("[COOPBM] type %u to %08X source %08X (%s) +C %08X +10 %08X +14 %08X +18 %08X\n", ty, t0, s, w == 0 ? "P1" : w == 1 ? "twin" : "other", MEM32(n + 12), MEM32(n + 16), MEM32(n + 20), MEM32(n + 24));
            if ((cnt[0][0] + cnt[0][1] + cnt[0][2]) % 1000 == 999) tt_dbg("[COOPBM] type 10 so far: from P1 %u, twin %u, other %u; other types: P1 %u twin %u other %u\n", cnt[0][0], cnt[0][1], cnt[0][2], cnt[1][0], cnt[1][1], cnt[1][2]);
        }
    }
    if (coop_mode() && n >= 0x10000u) {
        static int said;
        uint32_t t = MEM32(n + 8), vt = t >= 0x10000u ? MEM32(t) : 0, fn = vt >= 0x10000u ? MEM32(vt + 0x54) : 0;
        if (fn < 0x12000u || fn >= 0x170000u) {
            if (said++ < 12) {
                uint32_t k, c[8] = { 0 }; int m = 0;
                for (k = 0; k < 96 && m < 8; k += 4) { uint32_t v = MEM32(esp + k); if (v >= 0x12000u && v < 0x170000u) c[m++] = v; }
                tt_dbg("[COOPMSG] message type %u to %08X (vt %08X, +54 %08X), twin %08X player %08X; stack code: %X %X %X %X %X %X %X %X\n", MEM8(n), t, vt, fn, coop_twin, MEM32(0x43B15Cu),
                       c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
            }
        }
    }
    sub_000C6F60_gen();
}void tt_coop_hp_set(uint32_t self, uint32_t v, uint32_t ra)
{
    static int n;
    if (coop_mode() && self >= 0x10000u && (self == coop_twin || self == coop_first_char) && n++ < 40)
        tt_dbg("[COOPHP] %s %08X: health set to %d (from %08X), player %08X\n", self == coop_twin ? "twin" : "original", self, (int32_t)v, ra, MEM32(0x43B15Cu));
}

/* research (lead 2): which of these writes a character's max health +0x300 (original vs twin), from where */
static void coop_mh(uint32_t fn, uint32_t self, uint32_t before)
{
    static int n;
    if (coop_mode() && self >= 0x10000u && (self == coop_twin || self == coop_first_char) && n++ < 60)
        tt_dbg("[COOPMAXHP] %08X on %s: +300 %d -> %d, +2FC %d (stack %08X %08X %08X)\n", fn, self == coop_twin ? "twin" : "original", (int32_t)before, (int32_t)MEM32(self + 0x300), (int32_t)MEM32(self + 0x2FC), MEM32(esp), MEM32(esp + 4), MEM32(esp + 8));
}
extern void sub_0008C850_gen(void); void sub_0008C850(void) { uint32_t s = ecx, b0 = s >= 0x10000u ? MEM32(s + 0x300) : 0, ra = MEM32(esp), a1 = MEM32(esp + 4); if (s >= 0x10000u && MEM32(s) == 0x174980u) { static int n; uint32_t h = MEM32(s + 0x18); if (n++ < 30) tt_dbg("[COOPWSTATS] 0x8C850 on weapon %08X %s arg %08X caller %08X\n", s, h >= 0x10000u ? (const char *)XBOX_PTR(h) : "?", a1, ra); } sub_0008C850_gen(); if (s >= 0x10000u) { uint32_t sp = esp; esp -= 4; MEM32(esp) = ra; coop_mh(0x0008C850u, s, b0); esp = sp; } }
extern void sub_0008CA60_gen(void); void sub_0008CA60(void) { uint32_t s = ecx, b0 = s >= 0x10000u ? MEM32(s + 0x300) : 0, ra = MEM32(esp); sub_0008CA60_gen(); if (s >= 0x10000u) { uint32_t sp = esp; esp -= 4; MEM32(esp) = ra; coop_mh(0x0008CA60u, s, b0); esp = sp; } }
extern void sub_0008CF70_gen(void); void sub_0008CF70(void) { uint32_t s = ecx, b0 = s >= 0x10000u ? MEM32(s + 0x300) : 0, ra = MEM32(esp); sub_0008CF70_gen(); if (s >= 0x10000u) { uint32_t sp = esp; esp -= 4; MEM32(esp) = ra; coop_mh(0x0008CF70u, s, b0); esp = sp; } }
extern void sub_0008E5D0_gen(void); void sub_0008E5D0(void) { uint32_t s = ecx, b0 = s >= 0x10000u ? MEM32(s + 0x300) : 0, ra = MEM32(esp); sub_0008E5D0_gen(); if (s >= 0x10000u) { uint32_t sp = esp; esp -= 4; MEM32(esp) = ra; coop_mh(0x0008E5D0u, s, b0); esp = sp; } }
extern void sub_0008E6A0_gen(void); void sub_0008E6A0(void) { uint32_t s = ecx, b0 = s >= 0x10000u ? MEM32(s + 0x300) : 0, ra = MEM32(esp); sub_0008E6A0_gen(); if (s >= 0x10000u) { uint32_t sp = esp; esp -= 4; MEM32(esp) = ra; coop_mh(0x0008E6A0u, s, b0); esp = sp; } }
extern void sub_000A9E80_gen(void); void sub_000A9E80(void) { uint32_t s = ecx, b0 = s >= 0x10000u ? MEM32(s + 0x300) : 0, ra = MEM32(esp); sub_000A9E80_gen(); if (s >= 0x10000u) { uint32_t sp = esp; esp -= 4; MEM32(esp) = ra; coop_mh(0x000A9E80u, s, b0); esp = sp; } }
/* SetPlayer (cdecl actor; called once by the level loader). Research (lead 2): with TT_COOP=clone, the
 * twin is made the player for a moment too, while the load's heap is still open, then player 1 again,
 * so the player-only set-up also runs for it. TT_COOP_NOSETPLAYER2 turns it off. */
extern void sub_00099000_gen(void);
void sub_00099000(void)
{
    uint32_t a = MEM32(esp + 4);
    sub_00099000_gen();
    if (coop_mode() == 3 && coop_twin >= 0x10000u && a >= 0x10000u && a != coop_twin && !getenv("TT_COOP_NOSETPLAYER2")) {
        uint32_t sp = esp, ra = eax, rc = ecx, rd = edx;
        PUSH32(esp, coop_twin); PUSH32(esp, 0x00099000u); sub_00099000_gen(); esp += 4;   /* cdecl */
        tt_dbg("[COOP2] SetPlayer(twin %08X): +3F8 %08X +53C %08X component %08X +50 %08X\n", coop_twin, MEM32(coop_twin + 0x3F8), MEM32(coop_twin + 0x53C),
               MEM32(coop_twin + 0x494), MEM32(MEM32(coop_twin + 0x494) + 0x50));
        PUSH32(esp, a); PUSH32(esp, 0x00099000u); sub_00099000_gen(); esp += 4;
        tt_dbg("[COOP2] SetPlayer(player 1 %08X) again: player %08X; esp %s\n", a, MEM32(0x43B15Cu), esp == sp ? "balanced" : "NOT balanced");
        esp = sp; eax = ra; ecx = rc; edx = rd;
    }
}/* research (lead 2): every script handler turns an object argument into an actor with 0x000B4690 (cdecl id).
 * Log the ones that resolve to player 1 or the twin, with the handler's return address. */
/* 0x000265D0 (thiscall event source; ret 12) raises an event to the source's listeners (+0x74, 8 bytes each,
 * count +0x174; +4 is the owner). The twin's listeners are player 1's script callbacks (coop_sync_listeners),
 * which name player 1 ("aragornHeroObj"), so while the twin's source raises an event, object lookups that
 * answer player 1 or one of his weapons answer the twin or its weapon copy (in 0xB4690 below).
 * TT_COOP_NOEVREDIRECT turns it off. */
static int coop_twin_ev; static unsigned coop_twin_ev_n, coop_twin_ev_redir;
extern void sub_000265D0_gen(void);
void sub_000265D0(void)
{
    static int off = -1; int save = coop_twin_ev;
    if (off < 0) off = getenv("TT_COOP_NOEVREDIRECT") != 0;
    if (!off && coop_mode() == 3 && coop_p2 && coop_twin >= 0x10000u && ecx >= 0x10000u) {
        if (ecx == MEM32(coop_twin + 0x3D0)) { coop_twin_ev = 1; coop_twin_ev_n++; }
        else if (ecx == MEM32(coop_first_char + 0x3D0)) coop_twin_ev = 0;
    }
    {   /* (research) whose sources raise events */
        static unsigned a, b, c, d, tot; static uint32_t last; tot++;
        if (coop_twin >= 0x10000u && ecx >= 0x10000u) { if (ecx == MEM32(coop_twin + 0x3D0)) a++; if (MEM32(ecx + 4) == coop_twin) b++; if (ecx == MEM32(coop_first_char + 0x3D0)) c++; if (MEM32(ecx + 4) == coop_first_char) d++; }
        if (coop_p2 && coop_now() - last > 10000) { last = coop_now(); tt_dbg("[COOPEVSRC] events %u: twin's source %u, owner twin %u, P1's source %u, owner P1 %u; twin +3D0 %08X P1 +3D0 %08X, this one %08X owner %08X\n", tot, a, b, c, d,
            MEM32(coop_twin + 0x3D0), MEM32(coop_first_char + 0x3D0), ecx, ecx >= 0x10000u ? MEM32(ecx + 4) : 0); }
    }
    sub_000265D0_gen();
    coop_twin_ev = save;
}
static void coop_watch50(const char *where);
/* 0x000922A0 (thiscall brain machine, class 0x171948; type, node; ret 8): the brain machine's message handler.
 * Its scripts (attack: @FlagAttacking, @WeaponCollisionOn, ...) name their actor through the record name the
 * twin shares with player 1, so they all land on player 1. While it handles a message from the twin
 * (node +4), lookups of player 1 or his weapons answer the twin or its copies (same switch as above). */
static unsigned coop_twin_bm_n, coop_bm_src[3];
extern void sub_000922A0_gen(void);
void sub_000922A0(void)
{
    static int off = -1, noswap = -1; int save = coop_twin_ev, sw = 0; uint32_t nd = MEM32(esp + 8);
    extern int tt_coop_enter_ctx(void); extern void tt_coop_leave(void);
    if (off < 0) { off = getenv("TT_COOP_NOEVREDIRECT") != 0; noswap = getenv("TT_COOP_NOBRAINSWAP") != 0; }
    if (coop_mode() == 3 && coop_p2 && coop_twin >= 0x10000u && nd >= 0x10000u) {
        uint32_t s = MEM32(nd + 4);
        coop_bm_src[s == coop_first_char ? 0 : s == coop_twin ? 1 : 2]++;
        if (getenv("TT_COOP_BOSSLOG")) {   /* (research) messages to or from the Watcher and its tentacles: each (type, from, to) the first
             * 3 times, with the time and both states (the fight's attack / stretched / back phases) */
            static uint32_t seen[256], cnt[256]; static int ns; uint32_t t = MEM32(nd + 8), ty = MEM32(esp + 4);
            int bs = coop_boss_kind(s), bt = coop_boss_kind(t);
            if (getenv("TT_COOP_BOSSLOG")[0] == '3' && coop_who_kind(s) >= 3 && coop_who_kind(s) <= 5 && ty != 45 && ty != 43 && ty != 9 && ty != 10 && ty != 46) {
                /* (research) while a tentacle lies stretched (state 9): everything the heroes and their weapons send, to anything */
                int any9 = 0, q; for (q = 0; q < coop_nchars && !any9; q++) any9 = coop_boss_kind(coop_chars[q]) == 1 && MEM32(coop_chars[q] + 0x408) == 9;
                if (any9) { static unsigned nn; if (nn++ < 400) tt_dbg("[BOSSMSG9] t %u type %u from %s to %s (%08X vt %08X)\n", coop_p2_t0 ? coop_now() - coop_p2_t0 : 0, ty, coop_nm(s), coop_nm(t), t, t >= 0x10000u && t < 0x84000000u ? MEM32(t) : 0); }
            }
            if (bs || bt) { uint32_t key = (ty & 0xFFFu) << 8 | (uint32_t)(coop_who_kind(s) << 4) | (uint32_t)coop_who_kind(t); int i;
                for (i = 0; i < ns && seen[i] != key; i++) ;
                if (i == ns && ns < 256) { seen[ns] = key; cnt[ns] = 0; ns++; }
                if (i < 256 && (cnt[i]++ < 3 || (getenv("TT_COOP_BOSSLOG")[0] == '2' && ty != 45 && ty != 43 && ty != 9 && ty != 10 && coop_who_kind(s) >= 3 && coop_who_kind(s) <= 5 && bt == 1) || (getenv("TT_COOP_BOSSLOG")[0] == '2' && bs == 1 && ty != 45 && ty != 43 && ty != 9 && ty != 10 && ty != 46)))
                    tt_dbg("[BOSSMSG] t %u type %u from %s (state %u) to %s (state %u)\n", coop_p2_t0 ? coop_now() - coop_p2_t0 : 0, ty,
                       coop_nm(s), s >= 0x10000u ? MEM32(s + 0x408) : 0, coop_nm(t), t >= 0x10000u ? MEM32(t + 0x408) : 0); }
        }
        if (getenv("TT_COOP_BMTGT")) {   /* (research) messages to the shared brain: by source and by target (node +8), per type */
            static unsigned c[64][3][3]; static uint32_t last; uint32_t t = MEM32(nd + 8), ty = MEM32(esp + 4) & 63u;
            c[ty][s == coop_first_char ? 0 : s == coop_twin ? 1 : 2][t == coop_first_char ? 0 : t == coop_twin ? 1 : 2]++;
            if (coop_now() - last > 10000) { int q; last = coop_now();
                for (q = 0; q < 64; q++) { unsigned tot = 0; int a, b; for (a = 0; a < 3; a++) for (b = 0; b < 3; b++) tot += c[q][a][b];
                    if (tot) tt_dbg("[BMTGT] type %d: from P1 to P1 %u twin %u other %u | from twin to P1 %u twin %u other %u | from other to P1 %u twin %u other %u\n", q,
                                    c[q][0][0], c[q][0][1], c[q][0][2], c[q][1][0], c[q][1][1], c[q][1][2], c[q][2][0], c[q][2][1], c[q][2][2]); } }
        }
        /* (2026-10-20) or whose target (node +8) is the twin: the per-character messages (type 43, about one a frame for each
         * hero) come from no character, so the twin's ran as player 1's and their scripts named Legolas: he took Gimli's
         * aiming pitch and swings (TT_COOP_BMTGT counts). TT_COOP_NOTGTSWAP: by source only, as before. */
        static int tgtoff = -1; if (tgtoff < 0) tgtoff = getenv("TT_COOP_NOTGTSWAP") != 0;
        if (!off) { if (s == coop_twin || (!tgtoff && MEM32(nd + 8) == coop_twin)) { coop_twin_ev = 1; coop_twin_bm_n++; if (!noswap) sw = tt_coop_enter_ctx(); }
            else if (s >= 0x10000u && MEM32(s) == 0x1741C0u && MEM32(s + 0x114) == 3 && MEM32(s + 0x494) >= 0x10000u && MEM32(MEM32(s + 0x494) + 0x98) == coop_twin && !getenv("TT_COOP_NOENEMYSWAP")) {
                /* an enemy whose target is player 2: its brain scripts work with "the player" ([0x43B15C], @GetPlayer, @IsPlayer,
                 * the hero's name), so player 2 is the player for the length of the message */
                static unsigned ne; extern int coop_hero_down(int); if (!coop_hero_down(1)) { coop_twin_ev = 1; ne++; sw = tt_coop_enter_ctx(); if ((ne & 1023) == 1) tt_dbg("[COOPENEMY] brain messages of enemies on player 2 run with player 2 as the player: %u so far\n", ne); } }
            else if (s >= 0x10000u) coop_twin_ev = 0; }
    }
    if (coop_mode() == 3) coop_watch50("922A0 pre");
    sub_000922A0_gen();
    if (coop_mode() == 3) coop_watch50("922A0 post");
    if (sw) tt_coop_leave();
    coop_twin_ev = save;
}
/* 0x0008FC60 (thiscall pickup; type, node; ret 8): a pickup's message handler. A touch (type 13, with its +0x3F8 bit 2)
 * runs 0x0008F960, which refills "the player" [0x43B15C] (health 0x9D400, arrows 0x9D460), so player 2's touches
 * (they do come, with him as the source) refilled player 1. When the touch's source (node +4) is player 2, it runs in
 * his context: observed, player 2 Legolas 1 -> 41 -> 60 from two quivers, player 1 unchanged.
 * TT_COOP_NOP2PICKUP: off. TT_COOP_PICKLOG: each touch, by whom. */
extern void sub_0008FC60_gen(void);
static struct { uint32_t pk, at; } coop_pick[64]; static int coop_npick;   /* (Pickups: Double) the pickups touched once */
static int coop_pick_find(uint32_t pk) { int k; for (k = 0; k < coop_npick; k++) if (coop_pick[k].pk == pk) return k; return -1; }
static int coop_pick_kept(uint32_t pk) { return coop_pick_find(pk) >= 0; }
void sub_0008FC60(void)
{
    extern int tt_coop_enter_ctx(void); extern void tt_coop_leave(void);
    uint32_t ty = MEM32(esp + 4) & 0xFFFFu, nd = MEM32(esp + 8), src = nd >= 0x10000u ? MEM32(nd + 4) : 0, pk = ecx; int sw = 0; int16_t a1 = 0, a2 = 0;
    if (ty == 13u && coop_mode() == 3 && coop_p2 >= 0x10000u && src == coop_p2 && !getenv("TT_COOP_NOP2PICKUP")) sw = tt_coop_enter_ctx();
    if (ty == 13u && coop_p2 >= 0x10000u) { a1 = (int16_t)MEM16(coop_first_char + 0x304); a2 = (int16_t)MEM16(coop_p2 + 0x304); }
    if (getenv("TT_VS_PICKLOG")) { static int n; if (n++ < 40) tt_dbg("[VSPICK] touch: pickup %08X type %u from %08X ra %08X, kind %u flags %08X\n", pk, ty, src, MEM32(esp), pk >= 0x10000u ? MEM32(pk + 0x3D0) : 0, pk >= 0x10000u ? MEM32(pk + 0x3F8) : 0); }
    if (ty == 13u && coop_mode() && coop_p2 >= 0x10000u && pk >= 0x10000u && tt_coop_setting(5) && !tt_vs_on()) {   /* (not in Versus: first come, first served) */
        /* (1.9.6) Pickups: Double (Co-op Options) -- a health or ammo pickup (+0x3F8 bit 2: not yet taken) gives its
         * refill the first time it is touched (0x0008F960, as the touch would, its flags and effect kept) and stays, for one
         * more touch 1.5 s later at the soonest, which goes as usual. A touch comes every frame while a hero stands
         * on it: those in between are dropped (or the same hero had both refills at once, observed) */
        int k, r = coop_pick_find(pk);
        if (r >= 0) {
            if (coop_now() - coop_pick[r].at < 1500u) { if (sw) tt_coop_leave(); esp += 12; return; }
            coop_pick[r] = coop_pick[--coop_npick];   /* (its second: as usual, below) */
        } else if (MEM8(pk + 0x3F8) & 2) {
            if (coop_npick >= 64) { for (k = 1; k < 64; k++) coop_pick[k - 1] = coop_pick[k]; coop_npick--; }
            coop_pick[coop_npick].pk = pk; coop_pick[coop_npick].at = coop_now(); coop_npick++;
            {   /* 0x0008F960 gives the refill, then marks the pickup taken (+0x3F8: bit 1 on, bit 2 off, then 4) and
                 * lets go of its effect (+0x3F4, through 0x0008E1B0): both kept as they were, so it is still there
                 * and still sends touches */
                uint32_t fl = MEM32(pk + 0x3F8), fx = MEM32(pk + 0x3F4);
                MEM32(pk + 0x3F4) = 0;
                tt_gcall(0x0008F960u, pk, 0, NULL);
                MEM32(pk + 0x3F8) = fl; MEM32(pk + 0x3F4) = fx;
            }
            if (getenv("TT_COOP_PICKLOG")) tt_dbg("[COOPPICK] pickup %08X: its first touch (by %s) refills and it stays for a second | hp P1 %d, P2 %d | ammo P1 %d -> %d, P2 %d -> %d\n", pk,
                src == coop_p2 ? "P2" : src == coop_first_char ? "P1" : "other", (int32_t)MEM32(coop_first_char + 0x2FC), (int32_t)MEM32(coop_p2 + 0x2FC),
                a1, (int16_t)MEM16(coop_first_char + 0x304), a2, (int16_t)MEM16(coop_p2 + 0x304));
            if (sw) tt_coop_leave();
            esp += 12;   /* (ret 8) */
            return;
        }
    }
    sub_0008FC60_gen();
    if (sw) tt_coop_leave();
    if (ty == 13u && coop_p2 >= 0x10000u && getenv("TT_COOP_PICKLOG")) { static unsigned k; if (k++ < 100)
        tt_dbg("[COOPPICK] pickup %08X vt %08X at %.1f %.1f kind %u touched by %08X (%s)%s | ammo P1 %d -> %d, P2 %d -> %d\n", pk, pk >= 0x10000u ? MEM32(pk) : 0, pk >= 0x10000u ? MEMF(pk + 0xF0) : 0.0f, pk >= 0x10000u ? MEMF(pk + 0xF4) : 0.0f, pk >= 0x10000u ? MEM32(pk + 0x3D0) : 0, src,
               src == coop_p2 ? "P2" : src == coop_first_char ? "P1" : "other", sw ? ", in his context" : "", a1, (int16_t)MEM16(coop_first_char + 0x304), a2, (int16_t)MEM16(coop_p2 + 0x304)); }
}
/* (research) 0x000BA020 (thiscall contact manager [0x49E8DC]; 9 args; ret 36): an attack box (from @WeaponCollisionOn
 * via 0xC6750). Its record: +8 attacker, +0x4C collision mask. The last box per hero is kept and dumped with its
 * fields once a second, to compare player 1's and player 2's. */
uint32_t coop_box[2];
extern void sub_000BA020_gen(void);
void sub_000BA020(void)
{
    uint32_t a[9], k; for (k = 0; k < 9; k++) a[k] = MEM32(esp + 4 + 4 * k);
    if (coop_mode() == 3 && coop_p2 >= 0x10000u && a[0] == coop_twin && a[1] >= 0x10000u && a[1] < 0x84000000u && MEM32(a[1]) == 0x174980u && !tt_vs_on() && !getenv("TT_COOP_NOP2BLADE")) {
        /* player 2's attack box (the second argument its weapon): his own melee weapon, not a ranged copy. His boxes came with his bow
         * copies (the one at his set-up and another), so his swings hit as the bow and did no damage (4 of 4 hits in a bot run, health
         * unchanged; a playtest: no melee kills). His bow: his slot-0 melee weapon; another ranged copy: his slot-1 one (Legolas' second
         * knife), else slot 0 */
        extern uint32_t coop_p2_bow(void);
        uint32_t h = MEM32(a[1] + 0x18), m = h >= 0x10008u && h < 0x84000000u ? MEM32(h - 8 + 0x40) : 0, pick = 0; int want, pass, j;
        if (m == 0x0834F2D4u || m == 0x36AC3F58u || m == 0xE7EEC090u || m == 0x4586A62Cu) {
            want = a[1] == coop_p2_bow() ? 0 : 1;
            for (pass = 0; pass < 2 && !pick; pass++, want = 0)
                for (j = 0; j < coop_np2w && !pick; j++) { uint32_t q = coop_p2w[j], qh = MEM32(q + 0x18), qm = qh >= 0x10008u && qh < 0x84000000u ? MEM32(qh - 8 + 0x40) : 0;
                    if (coop_p2w_slot[j] == want && !(qm == 0x0834F2D4u || qm == 0x36AC3F58u || qm == 0xE7EEC090u || qm == 0x4586A62Cu)) pick = q; }
            if (pick) { static int n; if (n++ < 6) tt_dbg("[COOPWEAPON] player 2's attack box: his %08X in place of the ranged %08X '%s'\n", pick, a[1], coop_nm(a[1])); MEM32(esp + 8) = pick; a[1] = pick; }
        }
    }
    sub_000BA020_gen();
    if (eax >= 0x10000u && tt_vs_on() && a[0] >= 0x10000u && (a[0] == coop_first_char || a[0] == coop_twin) && !getenv("TT_VS_NOPVP")) {
        /* (Versus) a hero's attack box gets the mask of a character on neither side (0x80008E: the enemies' bit 8 and the
         * heroes' bits 2 and 4), as the box maker gives one: it hits the other hero (its own maker is never hit) */
        static int said; if (!said++) tt_dbg("[VS] attack boxes: the heroes' hit each other (mask %08X -> 0080008E)\n", MEM32(eax + 0x4C));
        MEM32(eax + 0x4C) = 0x0080008Eu;
        if (getenv("TT_VS_SWINGLOG")) tt_dbg("[VSBOX] P%d box: %08X %08X %08X %08X %08X %08X %08X %08X | state %u\n", a[0] == coop_twin ? 2 : 1, a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], MEM32(a[0] + 0x408));
        { extern void tt_vs_swing(int who, int dmg); extern void tt_vs_box_kind(int who, uint32_t k2, uint32_t k4); tt_vs_box_kind(a[0] == coop_twin, a[3], a[5]); tt_vs_swing(a[0] == coop_twin, (int)a[2]); }   /* (a[2]: the box's damage) */
    }
    if (coop_mode() == 3 && coop_p2 && eax >= 0x10000u) {
        static int n; int w = a[0] == coop_first_char ? 0 : a[0] == coop_twin ? 1 : -1;
        if (w >= 0) coop_box[w] = eax;
        if ((w >= 0 || n < 6) && n++ < 30) tt_dbg("[COOPBOX] attack box %08X for %08X%s: args %08X %08X %08X %08X %08X %08X %08X %08X | +0 %08X +4C %08X | player %08X depth %d\n", eax, a[0], w == 0 ? " (P1)" : w == 1 ? " (twin)" : "",
                                           a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], MEM32(eax), MEM32(eax + 0x4C), MEM32(0x43B15Cu), coop_depth);
    }
}
/* (research) 0x000BE0D0: the contact manager's handler, run per attack box ([esp+4] = box, box+8 = attacker),
 * and 0x0006A160 (cdecl; query*): the collision query it makes on the player path. Counted per hero. */
static int coop_bt_who = -1; static unsigned coop_bt[2][6];
extern void sub_000BE0D0_gen(void);
uint32_t coop_troll_seen, coop_troll_box_t;   /* the cave troll (coop_bot_troll) and when an attack box of its was last tested */
void sub_000BE0D0(void)
{
    uint32_t b = MEM32(esp + 4), a = b >= 0x10000u ? MEM32(b + 8) : 0; int save = coop_bt_who, w = a == coop_first_char ? 0 : (a == coop_twin && a) ? 1 : -1;
    if (w >= 0 && coop_p2) { coop_bt[w][0]++; if (MEM32(a + 0x49C) & 0x100u) coop_bt[w][1]++; if (MEM8(a + 0x466)) coop_bt[w][2]++; }
    if (a >= 0x10000u && a == coop_troll_seen) coop_troll_box_t = coop_now() | 1;   /* (the P2 Bot: the troll's attack box, its swing) */
    static int noswap = -1; int sw = 0; extern int tt_coop_enter_ctx(void); extern void tt_coop_leave(void);
    if (noswap < 0) noswap = getenv("TT_COOP_BOXSWAP") == 0;
    /* after its collision query the test walks the list at 0x43B0E8 only if attacker == [0x43B15C]: the objects the level
     * script put there with @ForcePlayerWeaponObbTestOn -- the Watcher's tentacles while they lie stretched (state 9) --,
     * which the player's weapon is tested against. So player 2's boxes run in his context while that list has anything
     * in it (observed: player 1's sword hit stretched tentacles, which woke the Watcher; player 2's 242 boxes in the fight,
     * 0 contacts). TT_COOP_BOXSWAP: in his context always; TT_COOP_NOOBBSWAP: never for the list. (Ordinary enemies don't
     * need it: damage A/B with the steering bot.) */
    if (w == 1 && coop_p2 && !noswap) sw = tt_coop_enter_ctx();
    else if (w == 1 && coop_p2 && !getenv("TT_COOP_NOOBBSWAP")) {
        int k; for (k = 0; k < 9 && !MEM32(0x43B0E8u + 4u * k); k++) ;
        if (k < 9) { static int n; sw = tt_coop_enter_ctx(); if (n++ < 6) tt_dbg("[COOPOBB] player 2's attack box %08X tested in his context: %08X '%s' on the weapon-test list\n", b, MEM32(0x43B0E8u + 4u * k), coop_nm(MEM32(0x43B0E8u + 4u * k))); }
    }
    coop_bt_who = w;
    sub_000BE0D0_gen();
    if (sw) tt_coop_leave();
    if (w >= 0 && coop_p2 && MEM32(b + 0x28)) coop_bt[w][5]++;
    if (getenv("TT_COOP_CONTACTLOG") && w >= 0 && MEM32(b + 0x28)) {   /* (research) a box that made contact: who, the box's words, the
         * tentacles near (state, position) and the weapon-test list */
        static int n; int i;
        if (n++ < 60) {
            tt_dbg("[CONTACT] %s box %08X | +20 %08X +24 %08X +28 %08X +2C %08X +30 %08X +34 %08X +38 %08X +3C %08X | at %.1f %.1f state %u | list %08X %08X\n", w ? "P2" : "P1", b,
                   MEM32(b + 0x20), MEM32(b + 0x24), MEM32(b + 0x28), MEM32(b + 0x2C), MEM32(b + 0x30), MEM32(b + 0x34), MEM32(b + 0x38), MEM32(b + 0x3C), MEMF(a + 0xF0), MEMF(a + 0xF4), MEM32(a + 0x408),
                   MEM32(0x43B0E8u), MEM32(0x43B0ECu));
            for (i = 0; i < coop_nchars; i++) { uint32_t e = coop_chars[i]; if (coop_boss_kind(e) != 1) continue;
                tt_dbg("[CONTACT]   %s state %u at %.1f %.1f (%.1f from the attacker)\n", coop_nm(e), MEM32(e + 0x408), MEMF(e + 0xF0), MEMF(e + 0xF4),
                       sqrtf((MEMF(e + 0xF0) - MEMF(a + 0xF0)) * (MEMF(e + 0xF0) - MEMF(a + 0xF0)) + (MEMF(e + 0xF4) - MEMF(a + 0xF4)) * (MEMF(e + 0xF4) - MEMF(a + 0xF4)))); }
        }
    }
    coop_bt_who = save;
}
extern void sub_0006A160_gen(void);
void sub_0006A160(void)
{
    uint32_t q = MEM32(esp + 4); int w = coop_bt_who;
    sub_0006A160_gen();
    if (w >= 0 && coop_p2) {
        static int k[2]; coop_bt[w][3]++; if (eax & 0xFF) coop_bt[w][4]++;
        if (q >= 0x10000u) {   /* geometry: the sweep's end vs the attacker's target, and the facing (attacker→sweep end vs attacker→target) */
            uint32_t at = w ? coop_twin : coop_first_char, c = MEM32(at + 0x494), t = c >= 0x10000u ? MEM32(c + 0x98) : 0;
            static float dmin[2] = { 1e9f, 1e9f }, cosmax[2] = { -2, -2 }, bmin[2] = { 1e9f, 1e9f }, bmax[2]; static unsigned ng[2]; static uint32_t last;
            if (t >= 0x10000u) {
                float ex = MEMF(q + 16), ey = MEMF(q + 20), tx = MEMF(t + 0xF0), ty = MEMF(t + 0xF4), ax = MEMF(at + 0xF0), ay = MEMF(at + 0xF4);
                float d = sqrtf((ex - tx) * (ex - tx) + (ey - ty) * (ey - ty)), ux = ex - ax, uy = ey - ay, vx = tx - ax, vy = ty - ay;
                float cs = (ux * vx + uy * vy) / (sqrtf(ux * ux + uy * uy) * sqrtf(vx * vx + vy * vy) + 1e-6f);
                if (d < dmin[w]) dmin[w] = d; if (cs > cosmax[w]) cosmax[w] = cs; ng[w]++; { float bd = sqrtf(vx * vx + vy * vy); if (bd < bmin[w]) bmin[w] = bd; if (bd > bmax[w]) bmax[w] = bd; }
            }
            if (coop_now() - last > 3000) { last = coop_now(); tt_dbg("[COOPGEO] sweeps with a target: P1 %u (closest end to target %.2f, best facing cos %.2f, body to target %.2f-%.2f) | P2 %u (%.2f, %.2f, %.2f-%.2f)\n", ng[0], dmin[0], cosmax[0], bmin[0], bmax[0], ng[1], dmin[1], cosmax[1], bmin[1], bmax[1]); bmin[0] = bmin[1] = 1e9f; bmax[0] = bmax[1] = 0;
                dmin[0] = dmin[1] = 1e9f; cosmax[0] = cosmax[1] = -2; ng[0] = ng[1] = 0; }
        }
        if (q >= 0x10000u) {   /* the result words after the input block: count non-zero ones, log a few with any non-zero */
            static int kr[2]; uint32_t j, nz = 0; for (j = 0x40; j < 0xC0; j += 4) if (MEM32(q + j)) nz++;
            if (nz && kr[w]++ < 6) tt_dbg("[COOPQR] %s query result: +40 %08X %08X %08X %08X +50 %08X %08X %08X %08X +60 %08X %08X %08X %08X +70 %08X %08X %08X %08X\n", w ? "twin" : "P1",
                MEM32(q + 0x40), MEM32(q + 0x44), MEM32(q + 0x48), MEM32(q + 0x4C), MEM32(q + 0x50), MEM32(q + 0x54), MEM32(q + 0x58), MEM32(q + 0x5C), MEM32(q + 0x60), MEM32(q + 0x64), MEM32(q + 0x68), MEM32(q + 0x6C), MEM32(q + 0x70), MEM32(q + 0x74), MEM32(q + 0x78), MEM32(q + 0x7C));
        }
        if (k[w]++ < 4 && q >= 0x10000u) tt_dbg("[COOPQ] %s box query %08X -> %08X | %08X %08X %08X %08X %08X %08X %08X %08X | %08X %08X %08X %08X %08X %08X %08X %08X\n", w ? "twin" : "P1", q, eax,
            MEM32(q), MEM32(q + 4), MEM32(q + 8), MEM32(q + 12), MEM32(q + 16), MEM32(q + 20), MEM32(q + 24), MEM32(q + 28), MEM32(q + 32), MEM32(q + 36), MEM32(q + 40), MEM32(q + 44), MEM32(q + 48), MEM32(q + 52), MEM32(q + 56), MEM32(q + 60));
    }
}
void tt_coop_bt_dump(void)
{
    int w; for (w = 0; w < 2; w++) tt_dbg("[COOPBT] %s boxes tested %u, attacker +49C&100 %u, +466 %u, queries %u, query hits %u, box +28 set after %u\n", w ? "twin" : "P1", coop_bt[w][0], coop_bt[w][1], coop_bt[w][2], coop_bt[w][3], coop_bt[w][4], coop_bt[w][5]);
}
void tt_coop_box_dump(void)
{
    int w; for (w = 0; w < 2; w++) { uint32_t b = coop_box[w]; if (b < 0x10000u) continue;
        tt_dbg("[COOPBOXF] %s's last box %08X: +0 %08X +8 %08X +C %08X +10 %08X +14 %08X +18 %08X +1C %08X +20 %08X +24 %08X +28 %08X +4C %08X +50 %08X +54 %08X +58 %08X\n", w ? "twin" : "P1", b,
               MEM32(b), MEM32(b + 8), MEM32(b + 12), MEM32(b + 16), MEM32(b + 20), MEM32(b + 24), MEM32(b + 28), MEM32(b + 32), MEM32(b + 36), MEM32(b + 40), MEM32(b + 0x4C), MEM32(b + 0x50), MEM32(b + 0x54), MEM32(b + 0x58)); }
}
static void coop_watch50(const char *where)   /* (research) when the twin's and player 1's component +0x50 change */
{
    static uint32_t last[2]; uint32_t who[2] = { coop_twin, coop_first_char }; int i;
    {   /* (research) the hero records' experience (0x4AB598 + hero*0x348, +0x6C): where it changes */
        static uint32_t lx[8]; static int k, init; uint32_t h;
        for (h = 0; h < 8; h++) { uint32_t v = MEM32(0x4AB598u + h * 0x348u + (getenv("TT_COOP_WATCHOFF") ? (uint32_t)strtoul(getenv("TT_COOP_WATCHOFF"), NULL, 16) : 0x90));   /* the running (in-level) experience, or TT_COOP_WATCHOFF */
            if (!init) lx[h] = v;
            else if (v != lx[h] && k++ < 12) { uint32_t q, cs[14] = { 0 }; int m = 0; for (q = 0; q < 0x600 && m < 14; q += 4) { uint32_t x = MEM32(esp + q); if (x >= 0x12000u && x < 0x170000u) cs[m++] = x; }
                tt_dbg("[COOPEXP] hero %u experience %d -> %d seen in %s | player %08X depth %d | stack %X %X %X %X %X %X %X %X %X %X %X %X %X %X\n", h, (int32_t)lx[h], (int32_t)v, where, MEM32(0x43B15Cu), coop_depth,
                       cs[0], cs[1], cs[2], cs[3], cs[4], cs[5], cs[6], cs[7], cs[8], cs[9], cs[10], cs[11], cs[12], cs[13]); }
            lx[h] = v; }
        init = 1;
    }
    {   /* (research) player 1's latest attack box: where its +0x28 (contact) gets set */
        extern uint32_t coop_box[2]; static uint32_t lb, lv; static int k; uint32_t b = coop_box[0], v = b >= 0x10000u ? MEM32(b + 0x28) : 0;
        if (b != lb) { lb = b; lv = v; }
        else if (v != lv && k++ < 8) { uint32_t q, cs[14] = { 0 }; int m = 0; for (q = 0; q < 0x600 && m < 14; q += 4) { uint32_t x = MEM32(esp + q); if (x >= 0x12000u && x < 0x170000u) cs[m++] = x; }
            tt_dbg("[COOPW28] P1's box %08X +28 %08X -> %08X (+C %08X +10 %08X) seen in %s | stack %X %X %X %X %X %X %X %X %X %X %X %X %X %X\n", b, lv, v, MEM32(b + 12), MEM32(b + 16), where,
                   cs[0], cs[1], cs[2], cs[3], cs[4], cs[5], cs[6], cs[7], cs[8], cs[9], cs[10], cs[11], cs[12], cs[13]); lv = v; }
    }
    if (getenv("TT_COOP_KNIFEWATCH")) {   /* (research) player 1's weapons' attachment pose (+0x48): where it changes, in whose context */
        extern uint32_t coop_wobjs[]; extern int coop_nwobjs; static uint32_t lw[8], lv[8]; static int nw, k2; int i, j;
        if (!nw) for (i = 0; i < coop_nwobjs && nw < 8; i++) if (MEM32(coop_wobjs[i] + 0x11C) == coop_first_char && coop_first_char) { for (j = 0; j < nw && lw[j] != coop_wobjs[i]; j++) ; if (j == nw) { lw[nw] = coop_wobjs[i]; lv[nw] = MEM32(coop_wobjs[i] + 0x48); nw++; } }
        for (j = 0; j < nw; j++) { uint32_t v = MEM32(lw[j] + 0x48);
            if (v != lv[j] && k2++ < 30) { uint32_t q, cs[12] = { 0 }; int m = 0; for (q = 0; q < 0x600 && m < 12; q += 4) { uint32_t x = MEM32(esp + q); if (x >= 0x12000u && x < 0x170000u) cs[m++] = x; }
                tt_dbg("[KNIFE] %s +48 %08X -> %08X seen in %s (twin event %d, P2 context %d) | stack %X %X %X %X %X %X %X %X %X %X %X %X\n", coop_nm(lw[j]), lv[j], v, where, coop_twin_ev, coop_depth,
                       cs[0], cs[1], cs[2], cs[3], cs[4], cs[5], cs[6], cs[7], cs[8], cs[9], cs[10], cs[11]); }
            lv[j] = v; }
    }
    {   /* (research) player 1's target, component +0x98: where it changes */
        static uint32_t lt; static int k; uint32_t c = coop_first_char >= 0x10000u ? MEM32(coop_first_char + 0x494) : 0, v = c >= 0x10000u ? MEM32(c + 0x98) : 0;
        if (v != lt && k++ < 12) { uint32_t q, cs[12] = { 0 }; int m = 0; for (q = 0; q < 0x400 && m < 12; q += 4) { uint32_t x = MEM32(esp + q); if (x >= 0x12000u && x < 0x170000u) cs[m++] = x; }
            tt_dbg("[COOPW98] P1 target %08X -> %08X seen in %s | stack %X %X %X %X %X %X %X %X %X %X %X %X\n", lt, v, where, cs[0], cs[1], cs[2], cs[3], cs[4], cs[5], cs[6], cs[7], cs[8], cs[9], cs[10], cs[11]); }
        lt = v;
    }
    for (i = 0; i < 2; i++) { uint32_t c = who[i] >= 0x10000u ? MEM32(who[i] + 0x494) : 0, v = c >= 0x10000u ? MEM32(c + 0x50) : 0;
        if (v != last[i]) { uint32_t k, cs[8] = { 0 }; int m = 0; for (k = 0; k < 160 && m < 8; k += 4) { uint32_t x = MEM32(esp + k); if (x >= 0x12000u && x < 0x170000u) cs[m++] = x; }
            tt_dbg("[COOPW50] %s +50 %08X -> %08X (vt %08X) seen in %s; stack code %X %X %X %X %X %X %X %X\n", i ? "player 1" : "twin", last[i], v, v >= 0x10000u ? MEM32(v) : 0, where, cs[0], cs[1], cs[2], cs[3], cs[4], cs[5], cs[6], cs[7]); last[i] = v; } }
}
extern void sub_000B4690_gen(void);
void sub_000B4690(void)
{
    uint32_t id = MEM32(esp + 4), ra = MEM32(esp);
    if (coop_mode() == 3) coop_watch50("B4690 pre");
    sub_000B4690_gen();
    { extern void tt_coop_ispw_lookup(uint32_t, uint32_t); tt_coop_ispw_lookup(ra, eax); }
    if (coop_in_obb && eax >= 0x10000u) coop_obb_obj = eax;
    if (coop_mode()) { coop_lk[coop_lkn & 63].ra = ra; coop_lk[coop_lkn & 63].obj = eax; coop_lk[coop_lkn & 63].id = id; coop_lkn++; }
    if (getenv("TT_COOP_BOSSCMDLOG") && eax >= 0x10000u && eax < 0x84000000u) {   /* (research) the script commands that look up a tentacle
         * or the Watcher: each (command's return address, object) the first time, with the object's state and flags, and
         * every 10 s the counts (the address maps to its handler through the command table, local/re/script_cmds.tsv) */
        uint32_t h = MEM32(eax + 0x18);
        if (h >= 0x10000u && h < 0x84000000u && (strstr((const char *)XBOX_PTR(h), "entacle") || strstr((const char *)XBOX_PTR(h), "atcher"))) {
            static struct { uint32_t ra, obj; unsigned n; } t[96]; static int tn; static uint32_t last; int i;
            for (i = 0; i < tn && !(t[i].ra == ra && t[i].obj == eax); i++) ;
            if (i == tn && tn < 96) { t[tn].ra = ra; t[tn].obj = eax; t[tn].n = 0; tn++;
                tt_dbg("[BOSSCMD] t %u ra %08X looks up %08X '%s' (vt %08X, role %u, state %u, +110 %08X, +324 %08X, +49C %08X, hp %d)\n", coop_now(), ra, eax, (const char *)XBOX_PTR(h),
                       MEM32(eax), MEM32(eax + 0x114), MEM32(eax + 0x408), MEM32(eax + 0x110), MEM32(eax + 0x324), MEM32(eax + 0x49C), (int32_t)MEM32(eax + 0x2FC)); }
            if (i < 96) t[i].n++;
            if (coop_now() - last > 10000u) { last = coop_now(); tt_dbg("[BOSSCMD] counts:"); for (i = 0; i < tn; i++) tt_dbg(" %X/%s:%u", t[i].ra, (const char *)XBOX_PTR(MEM32(t[i].obj + 0x18)), t[i].n); tt_dbg("\n"); }
        }
    }
    if (coop_p2 && eax == coop_first_char && (ra == 0x000A9ACDu || ra == 0x000AA632u) && 0) {   /* (research) @FlagAttacking / @WeaponCollisionOn on player 1: who runs them */
        static int k; uint32_t q, cs[24] = { 0 }; int m = 0;
        for (q = 0; q < 0xC00 && m < 24; q += 4) { uint32_t v = MEM32(esp + q); if (v >= 0x12000u && v < 0x170000u) cs[m++] = v; }
        if (k++ < 6) tt_dbg("[COOPATK] %s on P1 | P1 state %u twin state %u | stack %X %X %X %X %X %X %X %X %X %X %X %X | %X %X %X %X %X %X %X %X %X %X %X %X\n", ra == 0x000A9ACDu ? "@FlagAttacking" : "@WeaponCollisionOn",
                            MEM32(coop_first_char + 0x408), MEM32(coop_twin + 0x408), cs[0], cs[1], cs[2], cs[3], cs[4], cs[5], cs[6], cs[7], cs[8], cs[9], cs[10], cs[11], cs[12], cs[13], cs[14], cs[15], cs[16], cs[17], cs[18], cs[19], cs[20], cs[21], cs[22], cs[23]);
    }
    if (coop_p2 && eax >= 0x10000u && (eax == coop_first_char || eax == coop_twin)) {   /* (research) in play: which commands look up a hero, by return address */
        static struct { uint32_t ra; unsigned n[3]; } t[48]; static int tn; static uint32_t last; int i, w = eax == coop_twin ? 1 : coop_depth ? 2 : 0;
        for (i = 0; i < tn && t[i].ra != ra; i++) ;
        if (i == tn && tn < 48) { t[i].ra = ra; t[i].n[0] = t[i].n[1] = t[i].n[2] = 0; tn++; }
        if (i < 48) t[i].n[w]++;
        if (coop_now() - last > 10000) { last = coop_now(); tt_dbg("[COOPEV] twin's events so far %u, brain messages from P1 %u twin %u other %u, lookups redirected to the twin %u\n", coop_twin_ev_n, coop_bm_src[0], coop_bm_src[1], coop_bm_src[2], coop_twin_ev_redir); for (i = 0; i < tn; i++) tt_dbg("[COOPLKP] from %06X: player 1 %u, twin %u, player 1 inside P2 context %u\n", t[i].ra, t[i].n[0], t[i].n[1], t[i].n[2]); }
    }
    if (ra == 0x000A18BCu && coop_mode() == 3) {   /* (research) @SetBrainMachine's second object: the brain machine */
        static int k; uint32_t h = eax >= 0x10000u ? MEM32(eax + 0x18) : 0; char nm[40]; int q;
        for (q = 0; q < 39 && h >= 0x10000u && MEM8(h + q) >= 32 && MEM8(h + q) < 127; q++) nm[q] = (char)MEM8(h + q);
        nm[q] = 0;
        if (k++ < 20) tt_dbg("[COOPSBM] brain machine id %08X -> %08X vt %08X name '%s' record %08X\n", id, eax, eax >= 0x10000u ? MEM32(eax) : 0, nm, h >= 8 ? h - 8 : 0);
    }
    if (coop_mode() == 3 && eax >= 0x10000u && MEM32(eax) == 0x174980u) {   /* (research) script commands on player 1's weapons */
        uint32_t h = MEM32(eax + 0x18); static int n;
        if (h >= 0x10000u && MEM8(h) == 'w' && MEM8(h + 6) == 'O' && (MEM8(h + 9) == '6' || MEM8(h + 9) == '7' || MEM8(h + 9) == '8') && n++ < 40) tt_dbg("[COOPWSCRIPT] %s %08X looked up from %08X (+300 %d) player 2 %08X\n", (const char *)XBOX_PTR(h), eax, ra, (int32_t)MEM32(eax + 0x300), coop_p2);
    }
    if (coop_in_attach && coop_mode() == 3 && eax >= 0x10000u) {   /* (research) what @AttachToBone attaches: parent then child, with their names (+0x18 -> record name) */
        static int n; uint32_t h = MEM32(eax + 0x18); char nm[40]; int k;
        for (k = 0; k < 39 && h >= 0x10000u && MEM8(h + k) >= 32 && MEM8(h + k) < 127; k++) nm[k] = (char)MEM8(h + k);
        nm[k] = 0;
        if (n++ < 40) tt_dbg("[COOPATTACH] %s %08X vt %08X name '%s' record %08X (from %08X)%s\n", ra == 0x000A0E22u ? "parent" : "child ", eax, MEM32(eax), nm, h >= 8 ? h - 8 : 0, ra,
                             eax == coop_first_char ? "  <- player 1" : eax == coop_twin ? "  <- twin" : "");
        if (ra == 0x000A0E2Au && n < 8 && h >= 0x10008u) { uint32_t r = h - 8, q; for (q = 0; q < 0x70; q += 16) tt_dbg("[COOPATTACH]    rec +%02X %08X %08X %08X %08X\n", q, MEM32(r + q), MEM32(r + q + 4), MEM32(r + q + 8), MEM32(r + q + 12)); }
    }
    if (coop_in_attach && ra == 0x000A0E2Au && eax >= 0x10000u && getenv("TT_COOP_WLOG")) {   /* (research) the bone (the handler's ebx: its second argument) and the weapon's model */
        uint32_t h = MEM32(eax + 0x18), r = h >= 0x10008u ? h - 8 : 0;
        tt_dbg("[COOPWLOG] attach '%s' to bone %d: model %08X texture %08X%s\n", h >= 0x10000u ? (const char *)XBOX_PTR(h) : "?", (int)ebx, r ? MEM32(r + 0x40) : 0, r ? MEM32(r + 0x50) : 0, coop_replaying ? " (twin)" : "");
    }
    if (coop_wid_on && eax >= 0x10000u && MEM32(eax) == 0x174980u && !coop_wid_obj) { uint32_t k; coop_wid_obj = eax; for (k = 0; k < 0x200; k++) coop_wid_snap[k] = MEM32(eax + 4 * k); }
    if (coop_in_attach && ra == 0x000A0E2Au && eax >= 0x10000u) { if (coop_new_weapon_now) eax = coop_new_weapon_now; else { coop_attach_child_seen = eax; coop_attach_bone_seen = (int)ebx; } }   /* (ebx: the handler's second argument, the slot) */
    if (coop_head_cmd && eax >= 0x10000u && eax == coop_first_char && coop_p2 >= 0x10000u) { static int n; if (n++ < 3) tt_dbg("[COOPHEAD] a heading command in player 2's context names player 1: him instead\n"); eax = coop_p2; }
    if (coop_mode() == 3 && eax >= 0x10000u && eax == coop_first_char && coop_twin >= 0x10000u) { extern int coop_resolved_hook(void); if (coop_resolved_hook()) eax = coop_twin; }
    else if (coop_mode() == 3 && eax >= 0x10000u && MEM32(eax) == 0x174980u && !coop_in_attach) { int i; for (i = 0; i < coop_nwpn; i++) if (coop_wpn_orig[i] == eax) { extern int coop_resolved_hook(void); if (coop_resolved_hook()) eax = coop_wpn_copy[i]; break; } }
    if ((coop_twin_ev || coop_depth) && coop_mode() == 3 && coop_p2 >= 0x10000u && eax >= 0x10000u && eax < 0x84000000u && MEM32(eax) == 0x174980u
        && (ra == 0x000A5DB3u || ra == 0x000A5E53u || ra == 0x000AA632u || ra == 0x000A5CC3u) && MEM32(eax + 0x11C) != coop_p2 && !getenv("TT_COOP_NOP2BLADE")) {
        /* a weapon's hit box switched in player 2's moves (@ObjectWeaponCollisionOn / Off, @WeaponCollisionOn / Off): his own melee weapon.
         * His moves name player 1's weapons: player 2 Legolas' knives came out as player 1 Aragorn's dagger (no copy of his: the sink, his
         * bow) and bow (his bow copy), so his swings hit with his bow and did no damage -- 4 of 4 hits in a bot run, health unchanged (a
         * playtest: no damage, no melee kills). Player 1's ranged weapon: his slot-1 melee weapon, any other: his slot-0 one (none: as before) */
        uint32_t h = MEM32(eax + 0x18), m = h >= 0x10008u && h < 0x84000000u ? MEM32(h - 8 + 0x40) : 0, pick = 0; int want = (m == 0x0834F2D4u || m == 0x36AC3F58u || m == 0xE7EEC090u) ? 1 : 0, k, pass;
        for (pass = 0; pass < 2 && !pick; pass++, want = 0)
            for (k = 0; k < coop_np2w && !pick; k++) { uint32_t q = coop_p2w[k], qh = MEM32(q + 0x18), qm = qh >= 0x10008u && qh < 0x84000000u ? MEM32(qh - 8 + 0x40) : 0;
                if (coop_p2w_slot[k] == want && !(qm == 0x0834F2D4u || qm == 0x36AC3F58u || qm == 0xE7EEC090u || qm == 0x4586A62Cu)) pick = q; }
        if (pick) { static int n; if (n++ < 8) tt_dbg("[COOPWEAPON] player 2's hit box: %08X '%s' -> his %08X (from %06X)\n", eax, coop_nm(eax), pick, ra); eax = pick; }
    }
    if (coop_twin_ev && eax >= 0x10000u) {   /* inside the twin's event: player 1 -> twin, his weapons -> the copies */
        if (eax == coop_first_char) { eax = coop_twin; coop_twin_ev_redir++; }
        else if (MEM32(eax) == 0x174980u) { int i, hit = 0; for (i = 0; i < coop_nwpn; i++) if (coop_wpn_orig[i] == eax) { eax = coop_wpn_copy[i]; coop_twin_ev_redir++; hit = 1; break; }
            if (!hit) { extern uint32_t coop_p2_arrow_for(uint32_t w); uint32_t ac = coop_p2_arrow_for(eax); if (ac) { eax = ac; coop_twin_ev_redir++; hit = 1; } }   /* (player 1's arrow: his, coop_p2_arrow) */
            if (!hit) { extern uint32_t coop_p2_ranged_copy(uint32_t w); uint32_t rc = coop_p2_ranged_copy(eax);   /* (and player 1's ranged weapon: his shots are fired from it by name) */
                if (rc) { static int n; if (n++ < 5) tt_dbg("[COOPWEAPON] player 2's event names %08X (player 1's ranged weapon): his copy %08X\n", eax, rc); eax = rc; coop_twin_ev_redir++; } } }
    }
    if ((coop_twin_ev || coop_depth) && coop_mode() == 3 && eax >= 0x10000u && eax < 0x84000000u && MEM32(eax) == 0x174980u && MEM32(eax + 0x11C) == coop_first_char && coop_first_char
        && !getenv("TT_COOP_NOWSINK")) {   /* player 1's weapon he has no copy of, named in player 2's events or context: the sink (coop_p2_weapon_sink) */
        extern uint32_t coop_p2_weapon_sink(void); uint32_t sk = coop_p2_weapon_sink(); int i, paired = 0;
        for (i = 0; i < coop_nwpn; i++) if (coop_wpn_orig[i] == eax) paired = 1;
        if (sk && !paired) { static int n; if (n++ < 5) tt_dbg("[COOPWEAPON] player 2's %s names player 1's %08X '%s' (no copy of his): the sink %08X\n", coop_twin_ev ? "event" : "context", eax, coop_nm(eax), sk); eax = sk; }
    }
    if (coop_p2 && (ra == 0x000A9ACDu || ra == 0x000AA632u || ra == 0x000A5CC3u) && (eax == coop_first_char || eax == coop_twin)) {   /* after the redirect: who the attack commands hit */
        static unsigned c[3][2]; static uint32_t last; int r = ra == 0x000A9ACDu ? 0 : ra == 0x000AA632u ? 1 : 2;
        c[r][eax == coop_twin]++;
        if (coop_now() - last > 10000) { last = coop_now(); tt_dbg("[COOPATK2] @FlagAttacking P1 %u twin %u | @WeaponCollisionOn P1 %u twin %u | @WeaponCollisionOff P1 %u twin %u\n", c[0][0], c[0][1], c[1][0], c[1][1], c[2][0], c[2][1]); }
    }
    if (coop_mode() == 3 && eax >= 0x10000u && (eax == coop_first_char || eax == coop_twin)) {
        static int n;
        if (n++ < 200) tt_dbg("[COOPSCRIPT] id %08X -> %s %08X, from %08X (player %08X)\n", id, eax == coop_twin ? "twin" : "original", eax, ra, MEM32(0x43B15Cu));
    }
}
/* lead 2: the level script sets up its hero ("aragornHeroObj") with these commands; the twin is never
 * named in the script, so each one that resolved player 1 runs once more with the object lookup
 * (0x000B4690) answering the twin. Default set below; TT_COOP_REPLAY="name,name,..." replaces it
 * (AttachToBone, StartPlayerWeaponPfx and PassBrain are off by default: they hand over one object). */

static int coop_replay_on(const char *name)
{
    static const char *def = "SetAnimMachine,SetBrainMachine,SetEnemyID,UnlockHeading,OnComboBreak,PlayerLargeMass,ObjectCollisionOn,SetAlpha,AttachToBone,Activate,Deactivate,SetRenderable,SetWeaponID,ObjectCollisionOff";
    const char *l = getenv("TT_COOP_REPLAY") ? getenv("TT_COOP_REPLAY") : def, *s = strstr(l, name);
    size_t n = strlen(name);
    return s && (s == l || s[-1] == ',') && (s[n] == 0 || s[n] == ',');
}
/* player 2's own machines. The level's scripts build every hero's animation and brain machines (createLegolasAnimMachine,
 * createLegolasBrainMachine ... are in every hero's level file): the played hero's for player 1 and the others' for the
 * companions. The twin, set up as a copy of player 1, got player 1's: player 2 Legolas aimed with Aragorn's moves and
 * bow slots (a playtest). @SetAnimMachine / @SetBrainMachine (0xA0F30 / 0xA1840) queue events 0x30 / 0x31 through
 * 0x000C71F0 / 0x000C7220 (thiscall on the queue 0x43C628: machine, actor, a number; ret 12). Each character's are kept
 * here; once the companion of player 2's hero shows its hero's type (+0x2F0: Aragorn 0, Legolas 7, Gimli 8), the same
 * events are queued for the twin: he shares that hero's machines, as characters of one type share theirs. Player 1's
 * and the companion's own are left alone. No such companion (Isildur at the Gates): player 1's, as before.
 * TT_COOP_P1MACHINE: as before; TT_COOP_P2OWNBRAIN: the brain machine too (research). */
static struct { uint32_t actor, mach, num; } coop_mev[2][64]; static int coop_mevn[2]; static unsigned coop_mev_lv;
static void coop_mev_note(int k)
{
    uint32_t m = MEM32(esp + 4), a = MEM32(esp + 8), n = MEM32(esp + 0xC); int i;
    if (coop_mev_lv != coop_level_n) { coop_mev_lv = coop_level_n; coop_mevn[0] = coop_mevn[1] = 0; }
    if (a < 0x10000u || a >= 0x84000000u || MEM32(a) != 0x1741C0u || coop_mode() != 3) return;
    for (i = 0; i < coop_mevn[k]; i++) if (coop_mev[k][i].actor == a) break;
    if (i == coop_mevn[k]) { if (i >= 64) return; coop_mevn[k]++; }
    coop_mev[k][i].actor = a; coop_mev[k][i].mach = m; coop_mev[k][i].num = n;
}
extern void sub_000C71F0_gen(void); void sub_000C71F0(void) { coop_mev_note(0); sub_000C71F0_gen(); }
extern void sub_000C7220_gen(void); void sub_000C7220(void) { coop_mev_note(1); sub_000C7220_gen(); }
/* player 2's hand weapons drawn (+0x110 bit 0x20000, what @SetRenderable sets: 0x00052570). His copies start without it; with
 * player 1's moves player 1's in-play @SetRenderable on his own (player 1 Legolas' knife weaponObj133) reached the copy of it
 * and showed it; with his own hero's moves nothing names it: player 2 Aragorn's sword (a copy of player 1 Legolas' knife,
 * 133b) was attached in his hand and not drawn (a playtest, +0x110 00017D40 in a bot run). So his own melee weapons for
 * slots 0 and 1 (the hands) are drawn once he has his own moves, and at each take of one. TT_COOP_NOP2SHOW: off */
void coop_p2_show_hands(uint32_t only)
{
    int k;
    if (getenv("TT_COOP_NOP2SHOW")) return;
    for (k = 0; k < coop_np2w; k++) { uint32_t q = coop_p2w[k], qh = q >= 0x10000u ? MEM32(q + 0x18) : 0, qm = qh >= 0x10008u && qh < 0x84000000u ? MEM32(qh - 8 + 0x40) : 0;
        extern const char *tt_coop_p2_char_now;
        if (!only && coop_p2w_slot[k] == 2 && qm == 0x69DAC4B8u && tt_coop_p2_char_now && !strcmp(tt_coop_p2_char_now, "legolas") && MEM32(q) == 0x174980u) {   /* (Legolas'
            * dagger: player 1's, weaponObj135, is never drawn (+0x110 00017D40); player 2's was, by his leg while he drew: a playtest) */
            MEM32(q + 0x110) &= ~0x20000u; continue; }
        if ((only && q != only) || MEM32(q) != 0x174980u || coop_p2w_slot[k] < 0 || coop_p2w_slot[k] > 1 || qm == 0x0834F2D4u || qm == 0x36AC3F58u || qm == 0xE7EEC090u) continue;
        if (!(MEM32(q + 0x110) & 0x20000u)) { static int n; if (n++ < 8) tt_dbg("[COOPWEAPON] player 2's %08X (slot %d) drawn\n", q, coop_p2w_slot[k]); MEM32(q + 0x110) |= 0x20000u; } }
}
static void coop_own_machines(void);
void coop_own_machines_x(void) { coop_own_machines(); }
static void coop_own_machines(void)   /* each frame (coop_place): once a level */
{
    extern const char *tt_coop_p2_char_now; extern uint32_t tt_gcall(uint32_t va, uint32_t thisptr, int n, const uint32_t *args);
    static unsigned done_lv = ~0u; static uint32_t t0; const char *w = tt_coop_p2_char_now; int ty, k, i;
    {   static uint32_t chk; static unsigned clv; if (getenv("TT_COOP_MACHLOG") && coop_twin >= 0x10000u && clv != coop_level_n) { if (!chk) chk = coop_now() | 1; else if (coop_now() - chk > 3000u) { uint32_t c2 = MEM32(coop_twin + 0x494), c1 = MEM32(coop_first_char + 0x494);
        clv = coop_level_n; chk = 0; tt_dbg("[MACH] 3 s on: twin anim %08X brain %08X | player 1 anim %08X brain %08X\n", MEM32(coop_twin + 0x3F8), c2 >= 0x10000u ? MEM32(c2 + 0x50) : 0, MEM32(coop_first_char + 0x3F8), c1 >= 0x10000u ? MEM32(c1 + 0x50) : 0); } } }
    if (getenv("TT_COOP_BOWLOG")) {   /* (research) every 2 s: the bows on each player's model (instance +0x3D0: weapon list +0x74, count +0x174), their bone and transform */
        static uint32_t lt; int pl;
        if (coop_now() - lt > 2000u) { lt = coop_now();
            for (pl = 0; pl < 2; pl++) { uint32_t c = pl ? coop_twin : coop_first_char, inst = c >= 0x10000u ? MEM32(c + 0x3D0) : 0, n, j;
                if (inst < 0x10000u) continue;
                n = MEM32(inst + 0x174); if (n > 32u) n = 0;
                tt_dbg("[BOWLOG] %s yaw? +F0 %.2f %.2f %.2f, %u attached:", pl ? "P2" : "P1", MEMF(c + 0xF0), MEMF(c + 0xF4), MEMF(c + 0xF8), n);
                for (j = 0; j < n; j++) { uint32_t o = MEM32(inst + 0x74 + 8 * j), h = o >= 0x10000u ? MEM32(o + 0x18) : 0; tt_dbg(" %s@%u", h >= 0x10000u && h < 0x84000000u ? (const char *)XBOX_PTR(h) : "?", MEM16(inst + 0x78 + 8 * j)); }
                tt_dbg("\n");
                for (j = 0; j < n; j++) { uint32_t o = MEM32(inst + 0x74 + 8 * j), h = o >= 0x10000u ? MEM32(o + 0x18) : 0, m = h >= 0x10008u && h < 0x84000000u ? MEM32(h - 8 + 0x40) : 0, q;
                    if (m != 0x0834F2D4u && !getenv("TT_COOP_BOWLOGALL")) continue;
                    tt_dbg("[BOWLOG]   %s (model %08X) on bone %u:", (const char *)XBOX_PTR(h), m, MEM16(inst + 0x78 + 8 * j));
                    for (q = 0x20; q < 0x60; q += 4) tt_dbg(" %.3f", MEMF(o + q));
                    tt_dbg(" | +90 %.2f %.2f %.2f +110 %08X +124 %08X\n", MEMF(o + 0x90), MEMF(o + 0x94), MEMF(o + 0x98), MEM32(o + 0x110), MEM32(o + 0x124)); } } } }
    if (done_lv == coop_level_n || coop_mode() != 3 || coop_twin < 0x10000u || coop_first_char < 0x10000u || !w || tt_vs_on() || getenv("TT_COOP_P1MACHINE")) return;
    ty = !_stricmp(w, "aragorn") ? 0 : !_stricmp(w, "legolas") ? 7 : !_stricmp(w, "gimli") ? 8 : -1;
    if (ty < 0) { done_lv = coop_level_n; return; }
    if (coop_mev_lv != coop_level_n) return;
    if (!t0) t0 = coop_now() | 1;
    for (i = 0; i < coop_mevn[0]; i++) { uint32_t a = coop_mev[0][i].actor;
        if (a == coop_first_char || a == coop_twin || MEM32(a) != 0x1741C0u || MEM32(a + 0x114) != 2 || MEM32(a + 0x2F0) != (uint32_t)ty) continue;
        for (k = 0; k < 2; k++) { int j; if (k == 1 && !getenv("TT_COOP_P2OWNBRAIN")) continue;   /* (the brain: set, it went back to player 1's within 3 s, observed; the animation machine alone gives the moves) */
            for (j = 0; j < coop_mevn[k]; j++) if (coop_mev[k][j].actor == a) {
                uint32_t args[3] = { coop_mev[k][j].mach, coop_twin, coop_mev[k][j].num };
                if (getenv("TT_COOP_MACHEVENT")) tt_gcall(k ? 0x000C7220u : 0x000C71F0u, 0x43C628u, 3, args);   /* (queued: never taken in play, observed) */
                else if (!k) { MEM32(coop_twin + 0x3F8) = args[0]; coop_p2_show_hands(0); }   /* (what 0x00061060, the character's setter, does) */
                else { uint32_t c2 = MEM32(coop_twin + 0x494); if (c2 >= 0x10000u) MEM32(c2 + 0x50) = args[0]; }   /* (the brain machine: component +0x50) */
                tt_dbg("[COOP2] player 2 (%s) gets the %s machine %08X of %s (type %d; player 1's %08X)\n", w, k ? "brain" : "animation", args[0],
                       (const char *)XBOX_PTR(MEM32(a + 0x18)), ty, k ? 0 : MEM32(coop_first_char + 0x3F8));
            } }
        done_lv = coop_level_n; t0 = 0; return;
    }
    if (coop_now() - t0 > 10000u) {   /* no companion of his hero within 10 s of the level's start */
        tt_dbg("[COOP2] player 2 (%s): no companion of his hero (type %d) among %d machine users: player 1's machines kept\n", w, ty, coop_mevn[0]);
        if (getenv("TT_COOP_MACHLOG")) for (i = 0; i < coop_mevn[0]; i++) { uint32_t a = coop_mev[0][i].actor; if (MEM32(a + 0x114) == 2) tt_dbg("[MACH]   %s type %u\n", (const char *)XBOX_PTR(MEM32(a + 0x18)), MEM32(a + 0x2F0)); }
        done_lv = coop_level_n; t0 = 0;
    }
}
static void coop_replay(uint32_t fn, const char *name, void (*gen)(void))
{
    uint32_t vm = MEM32(esp + 4);
    coop_resolved = 0;
    unsigned mlk0 = coop_lkn; int mlog = getenv("TT_COOP_MACHLOG") && (!strcmp(name, "SetAnimMachine") || !strcmp(name, "SetBrainMachine"));
    if (getenv("TT_COOP_WPNCMDLOG") && coop_p2_t0 && (!strcmp(name, "Activate") || !strcmp(name, "Deactivate") || !strcmp(name, "SetRenderable") || !strcmp(name, "AttachToBone") || !strcmp(name, "SetRenderOffset"))) mlog = 2;   /* (research) the weapon commands in play */
    if (getenv("TT_COOP_HEADLOG") && coop_p2_t0 && (!strcmp(name, "LockHeading") || !strcmp(name, "UnlockHeading") || !strcmp(name, "LockHeadingToTarget") || !strcmp(name, "SetTurnSpeed")
        || !strcmp(name, "LockOutAttack") || !strcmp(name, "SetTargetLock") || !strcmp(name, "SetHeading") || !strcmp(name, "Rotate") || !strcmp(name, "SetRotation"))) mlog = 3;
    if (getenv("TT_COOP_ATKLOG") && coop_p2_t0 && (!strcmp(name, "FrcAttackOn") || !strcmp(name, "SpdAttackOn") || !strcmp(name, "DevAttackOn") || !strcmp(name, "DefAttackOn")
        || !strcmp(name, "KDAttackOn") || !strcmp(name, "LockOutAttack") || !strcmp(name, "WeaponCollisionOn"))) { static int an; if (an++ < 600) mlog = 4; }   /* (research) the attack kinds, [0x1B11F0] */
    { int tl = coop_triglog_on();
    if (tl && coop_p2_t0 && (!strcmp(name, "IsPlayer") || !strcmp(name, "HeroSpecificTrigger") || !strcmp(name, "SetToTriggerOnPlayer") || !strcmp(name, "ToggleTriggerObjs")
        || !strcmp(name, "Trigger") || !strcmp(name, "AreaActivate") || !strcmp(name, "GetPlayer") || !strcmp(name, "KilledByPlayer"))) { static int tn; if (tn++ < 300) mlog = 3; } }   /* (research) the trigger commands in play, the first 300 */   /* (research) the heading commands in play */
    if (getenv("TT_COOP_ATKLOG") && !strcmp(name, "WeaponCollisionOn") && coop_p2_t0) {   /* (research) its script arguments: how many, and which are objects (3) / numbers (2) */
        static int wn; uint32_t a1[1] = { vm }, cnt = tt_gcall(0x0009F210u, 0, 1, a1); char tb[40]; int q, o2 = 0;
        for (q = 1; q <= 6 && o2 < 36; q++) { uint32_t a3[3] = { vm, 3u, (uint32_t)q }, a2[3] = { vm, 2u, (uint32_t)q };   /* (0xB4590: vm, kind, index) */ int t3 = (tt_gcall(0x000B4590u, 0, 3, a3) & 0xFF) != 0, t2 = (tt_gcall(0x000B4590u, 0, 3, a2) & 0xFF) != 0;
            tb[o2++] = t3 ? 'o' : t2 ? 'n' : '-'; }
        tb[o2] = 0;
        { uint32_t k2; int tk = -1; for (k2 = 0; k2 < 8 && tk < 0; k2++) { uint32_t ak[3] = { vm, k2, 2u }; if (tt_gcall(0x000B4590u, 0, 3, ak) & 0xFF) tk = (int)k2; }
          { uint32_t ao[2] = { vm, 2u }, id2 = tk == 3 ? tt_gcall(0x0009F2E0u, 0, 2, ao) : 0; if (wn < 200) tt_dbg("[ATKARGS]   argument 2: kind %d, value %08X\n", tk, tk == 3 ? id2 : (tk >= 0 ? tt_gcall(0x0009F2E0u, 0, 2, ao) : 0)); } }
        if (wn++ < 200) tt_dbg("[ATKARGS] t %u @WeaponCollisionOn in %s context, twin event %d: %u arguments, kinds 1-6 '%s' (o object, n number)\n", coop_now(), coop_depth ? "player 2's" : "player 1's", coop_twin_ev, cnt, tb);
    }
    /* player 2's swing with no weapon: his hero's attack script names its weapon through a script variable his own hero's set-up
     * fills, and only player 1's hero's set-up runs (replayed for him) -- player 2 Aragorn beside player 1 Legolas passed an
     * empty second argument (kind 1, 0) to every @WeaponCollisionOn, so it gave up and his swings made no attack box (Amon
     * Hen, bot runs: 0 hits). An argument is 8 bytes at vm+0x10 (kind; 3: a name, its text at the pointer + 0x14). Player 1's
     * last weapon name is put in for the call; inside player 2's event it is answered with his own copy (sub_000B4690), and
     * the box maker swaps a ranged copy for his melee one (sub_000BA020). TT_COOP_NOP2WEAPONARG: as before */
    { uint32_t ent = 0, sk = 0, sp2 = 0; int patched = 0;
      if (!strcmp(name, "WeaponCollisionOn") && coop_mode() == 3 && coop_p2 >= 0x10000u && vm >= 0x10000u && !getenv("TT_COOP_NOP2WEAPONARG")) {
        uint32_t base = MEM32(vm + 0x10), cnt = (MEM32(vm) - base) >> 3;
        if (cnt >= 2 && base >= 0x10000u) { ent = base + 8u;
            if ((coop_twin_ev || coop_depth) && MEM32(ent) != 3u && coop_p2_wpnarg_p1) {
                sk = MEM32(ent); sp2 = MEM32(ent + 4); MEM32(ent) = 3u; MEM32(ent + 4) = coop_p2_wpnarg_p1; patched = 1;
                { static int n; if (n++ < 6) tt_dbg("[COOPWEAPON] player 2's swing named no weapon (kind %u): player 1's weapon name put in\n", sk); } } } }
      coop_in_attach = 0;
      if (patched) { coop_p2_wpnarg_ent = ent; coop_p2_wpnarg_k = sk; coop_p2_wpnarg_v = sp2; }
      else if (ent && !coop_twin_ev && !coop_depth && MEM32(ent) == 3u) { coop_p2_wpnarg_learn = MEM32(ent + 4); coop_p2_wpnarg_lk = coop_lkn; }   /* (kept after the call if its hero was player 1) */
    }
    coop_in_attach = !strcmp(name, "AttachToBone");
    /* player 2's own heading commands: his swings' @LockHeading / @UnlockHeading / @LockHeadingToTarget, run in his step, name
     * "the player" and found player 1 (the lookup answers him only inside his brain's messages), so his heading stayed locked
     * through a swing while player 1's was unlocked (a playtest: "cant rotate during melee attack unlike p1"; a bot run: 133
     * of them in his context, all on player 1). TT_COOP_NOHEADREDIR: as before */
    coop_head_cmd = coop_depth && !coop_ctx_out && coop_p2 >= 0x10000u && !getenv("TT_COOP_NOHEADREDIR") && (!strcmp(name, "LockHeading") || !strcmp(name, "UnlockHeading")
        || !strcmp(name, "LockHeadingToTarget") || !strcmp(name, "SetTurnSpeed") || !strcmp(name, "SetHeading"));
    { uint32_t atb3 = 0;   /* (@AttachToBone's weapon name entry, argument 3: kept for player 2's swings below) */
      if (!strcmp(name, "AttachToBone") && !coop_replaying && vm >= 0x10000u) { uint32_t base = MEM32(vm + 0x10); if (base >= 0x10000u && ((MEM32(vm) - base) >> 3) >= 3u && MEM32(base + 16u) == 3u) atb3 = MEM32(base + 20u); }
    gen();
      /* player 1's main-hand weapon (slot 0) from the level's set-up: player 2's swings then have a name before player 1 swings */
      if (atb3 && coop_resolved && coop_attach_child_seen && coop_attach_bone_seen == 0 && coop_mode() == 3 && !getenv("TT_COOP_NOP2WEAPONARG")) {
          coop_p2_wpnarg_p1 = atb3; { static int n; if (n++ < 4) tt_dbg("[COOPWEAPON] player 1's main-hand weapon name kept from the set-up (%08X '%s')\n", coop_attach_child_seen, coop_nm(coop_attach_child_seen)); } }
    }
    coop_head_cmd = 0;
    if (coop_p2_wpnarg_learn) { if (coop_lkn > coop_p2_wpnarg_lk && coop_lk[coop_p2_wpnarg_lk & 63].obj == coop_first_char && coop_first_char) coop_p2_wpnarg_p1 = coop_p2_wpnarg_learn; coop_p2_wpnarg_learn = 0; }
    if (coop_p2_wpnarg_ent) { MEM32(coop_p2_wpnarg_ent) = coop_p2_wpnarg_k; MEM32(coop_p2_wpnarg_ent + 4) = coop_p2_wpnarg_v; coop_p2_wpnarg_ent = 0; }   /* (the script's own argument back) */
    if (mlog) {   /* (research) each machine command: the objects it looked up (the actor, the machine) by name */
        unsigned q; tt_dbg("[%s] t %u @%s%s%s ret %08X:", mlog == 4 ? "ATK" : mlog == 3 ? "HEAD" : mlog == 2 ? "WPNCMD" : "MACH", coop_now(), name, coop_replaying ? " (replay)" : "", mlog >= 3 ? (coop_depth ? " (player 2's context)" : " (player 1's context)") : "", eax);
        if (mlog == 4) tt_dbg(" [1B11F0 %d, player %08X, twin event %d]", (int32_t)MEM32(0x1B11F0u), MEM32(0x43B15Cu), coop_twin_ev);
        for (q = mlk0; q < coop_lkn && q < mlk0 + 4; q++) { uint32_t o = coop_lk[q & 63].obj, h = o >= 0x10000u && o < 0x84000000u ? MEM32(o + 0x18) : 0;
            tt_dbg(" %08X '%s' (vt %08X, type +2F0 %u)", o, h >= 0x10000u && h < 0x84000000u ? (const char *)XBOX_PTR(h) : "?", o >= 0x10000u && o < 0x84000000u ? MEM32(o) : 0, o >= 0x10000u && o < 0x84000000u ? MEM32(o + 0x2F0) : 0); }
        if (!strcmp(name, "SetRenderOffset") && coop_lkn > mlk0) { uint32_t o = coop_lk[(coop_lkn - 1) & 63].obj; tt_dbg(" ->");
            for (q = 0x20; q < 0x60 && o >= 0x10000u && o < 0x84000000u; q += 4) tt_dbg(" %.3f", MEMF(o + q)); tt_dbg(" (player %08X)", MEM32(0x43B15Cu)); }
        tt_dbg("\n"); }
    coop_in_attach = 0;
    if (!strcmp(name, "SetEnemyID") && coop_p2 >= 0x10000u && coop_p2 == coop_twin) coop_p2_own_type("a later @SetEnemyID");   /* (after the take-over the script's lookups of the player can land on him) */
    if (coop_mode() == 3 && coop_resolved && coop_twin >= 0x10000u && !coop_replaying && !coop_p2 && coop_replay_on(name)) {   /* set-up only: until player 2 takes over */
        uint32_t sp = esp, ra = eax, rc = ecx, rd = edx;
        if (!strcmp(name, "AttachToBone")) { extern uint32_t coop_build_weapon_ext(uint32_t); extern void coop_p2_extra_weapons_ext(uint32_t); uint32_t wpn;
            if (coop_attach_child_seen) coop_p2_extra_weapons_ext(coop_attach_child_seen);   /* (once a level: player 2's slots player 1 has not) */
            wpn = coop_attach_child_seen ? coop_build_weapon_ext(coop_attach_child_seen) : 0; if (!wpn) { coop_attach_child_seen = 0; return; } coop_new_weapon_now = wpn; coop_in_attach = 1; }
        coop_replaying = 1;
        PUSH32(esp, vm); PUSH32(esp, fn); gen(); esp += 4;   /* cdecl (vm) */
        coop_in_attach = 0; coop_new_weapon_now = 0; coop_attach_child_seen = 0;
        coop_replaying = 0;
        tt_dbg("[COOP2] script @%s replayed for the twin (returned %08X; esp %s)\n", name, eax, esp == sp ? "balanced" : "NOT balanced");
        if (!strcmp(name, "SetEnemyID")) coop_p2_own_type("@SetEnemyID replayed");
        esp = sp; eax = ra; ecx = rc; edx = rd;
    }
}
extern void sub_000A6770_gen(void); void sub_000A6770(void) { coop_replay(0x000A6770u, "SetEnemyID", sub_000A6770_gen); }
/* the level's experience commands. @GiveExperience(amount) (0xA88E0: the current hero, 0xD2310, then 0xD25D0 -> 0x9DD90 on
 * his record) reached only player 1's record: player 2 never had the level's bonus (a playtest: "p2 legolas doesnt level up at
 * all"; at a level's end player 1 went 609 -> 5609, player 2 stayed at his kills, 88). Run once more in player 2's context
 * (his record copy in place); @AddToMaxExperience too. TT_COOP_NOP2GIVEEXP: as before. The other award commands are only logged.
 * Weathertop's script gives 1000 early (bot run: player 2 0 -> 1000 after it) */
static void coop_exp_cmd(uint32_t fn, const char *name, void (*gen)(void), int replay)
{
    extern uint32_t coop_rec_addr; extern int tt_coop_enter_ctx(void); extern void tt_coop_leave(void);
    uint32_t vm = MEM32(esp + 4); float b1 = coop_rec_addr ? MEMF(coop_rec_addr + 0x90) : 0.0f, b2;
    gen();
    b2 = coop_rec_addr ? MEMF(coop_rec_addr + 0x90) : 0.0f;
    if (coop_mode() == 3 && coop_p2 >= 0x10000u) tt_dbg("[COOPEXP] @%s in %s context: his level experience %.0f -> %.0f\n", name, coop_depth ? "player 2's" : "player 1's", b1, b2);
    if (replay && coop_mode() == 3 && coop_p2 >= 0x10000u && coop_rec_addr && !coop_depth && !getenv("TT_COOP_NOP2GIVEEXP")) {
        uint32_t sp = esp, ra = eax, rc = ecx, rd = edx; int sw = tt_coop_enter_ctx(); float c1 = MEMF(coop_rec_addr + 0x90);
        PUSH32(esp, vm); PUSH32(esp, fn); gen(); esp += 4;   /* cdecl (vm) */
        tt_dbg("[COOPEXP] @%s again for player 2: his level experience %.0f -> %.0f%s; esp %s\n", name, c1, MEMF(coop_rec_addr + 0x90), sw ? "" : " (NOT in his context)", esp == sp ? "balanced" : "NOT balanced");
        if (sw) tt_coop_leave();
        esp = sp; eax = ra; ecx = rc; edx = rd;
    }
}
/* (research) TT_COOP_EXPADDLOG: every experience add (0x9DD90, thiscall record, amount): the record, the amount, the screen, the callers */
extern void sub_0009DD90_gen(void);
void sub_0009DD90(void)
{
    extern uint32_t coop_rec_addr; uint32_t rec = ecx, a1 = MEM32(esp + 4); float before = rec >= 0x10000u ? MEMF(rec + 0x90) : 0.0f;
    uint32_t cs[8] = { 0 }; int m = 0; uint32_t q; if (getenv("TT_COOP_EXPADDLOG")) for (q = 0; q < 0x300 && m < 8; q += 4) { uint32_t x = MEM32(esp + q); if (x >= 0x12000u && x < 0x170000u) cs[m++] = x; }
    sub_0009DD90_gen();
    if (getenv("TT_COOP_EXPADDLOG")) { static int n; if (n++ < 80) tt_dbg("[EXPADD] record %08X%s +90 %.0f -> %.0f (arg %08X = %d / %.2f) screen %u depth %d | %X %X %X %X %X %X %X %X\n", rec, rec == coop_rec_addr ? " (the current hero's)" : "",
        before, rec >= 0x10000u ? MEMF(rec + 0x90) : 0.0f, a1, (int32_t)a1, *(float *)&a1, MEM32(0x4AD050u), coop_depth, cs[0], cs[1], cs[2], cs[3], cs[4], cs[5], cs[6], cs[7]); }
}
extern void sub_000A88E0_gen(void); void sub_000A88E0(void) { coop_exp_cmd(0x000A88E0u, "GiveExperience", sub_000A88E0_gen, 1); }
extern void sub_000ACA40_gen(void); void sub_000ACA40(void) { coop_exp_cmd(0x000ACA40u, "AwardSpecialPoints", sub_000ACA40_gen, 0); }
extern void sub_000A69D0_gen(void); void sub_000A69D0(void) { coop_exp_cmd(0x000A69D0u, "AwardRangerPoints", sub_000A69D0_gen, 0); }
extern void sub_000A8930_gen(void); void sub_000A8930(void) { coop_exp_cmd(0x000A8930u, "AddToMaxExperience", sub_000A8930_gen, 1); }   /* (0x9D3E0 on the current record: the level's possible experience) */
extern void sub_000B03D0_gen(void); void sub_000B03D0(void) { coop_exp_cmd(0x000B03D0u, "SetLevelExperienceMultipler", sub_000B03D0_gen, 0); }
/* an in-game cutscene started by player 2: a trigger he crosses runs its script in his context (he is "the player" there, so
 * he can set triggers off), and the cutscene it starts showed him -- the one to the cave troll's upper platform (a playtest:
 * player 1 should be seen). At the first cutscene command (@SetSceneMode, @LetterBox, @CinematicPlay) inside his context,
 * the rest of the script runs as player 1's: his globals back (as tt_coop_leave does) and the lookups no longer answered with
 * player 2. TT_COOP_P2SCENE: as before */
static void coop_scene_cmd(const char *name, void (*gen)(void))
{
    if (coop_depth && !coop_ctx_out && coop_p2 >= 0x10000u && !tt_vs_on() && !getenv("TT_COOP_P2SCENE")) {
        coop_save(coop_s2); coop_load(coop_s1); coop_ctx_out = 1; coop_twin_ev = 0;
        tt_dbg("[COOPSCENE] @%s in player 2's context: the cutscene runs as player 1's\n", name);
    }
    gen();
}
extern void sub_000A6AF0_gen(void); void sub_000A6AF0(void) { coop_scene_cmd("SetSceneMode", sub_000A6AF0_gen); }
extern void sub_000A3F20_gen(void); void sub_000A3F20(void) { coop_scene_cmd("LetterBox", sub_000A3F20_gen); }
extern void sub_000A40A0_gen(void); void sub_000A40A0(void) { coop_scene_cmd("CinematicPlay", sub_000A40A0_gen); }
extern void sub_000B2320_gen(void); void sub_000B2320(void) {
    extern int coop_p2_skip_offset(void);
    if (coop_p2_skip_offset()) { eax = 0; esp += 4; return; }   /* (player 2 Legolas' own: coop_p2_render_offset; cdecl, plain ret) */
    coop_replay(0x000B2320u, "SetRenderOffset", sub_000B2320_gen); }
extern void sub_000A0DB0_gen(void); void sub_000A0DB0(void) { coop_replay(0x000A0DB0u, "AttachToBone", sub_000A0DB0_gen); }
extern void sub_000A5CE0_gen(void); void sub_000A5CE0(void) { coop_replay(0x000A5CE0u, "ObjectCollisionOn", sub_000A5CE0_gen); }
extern void sub_000B1EF0_gen(void); void sub_000B1EF0(void) { coop_replay(0x000B1EF0u, "SetAlpha", sub_000B1EF0_gen); }
extern void sub_000A7580_gen(void); void sub_000A7580(void) { coop_replay(0x000A7580u, "OnComboBreak", sub_000A7580_gen); }
extern void sub_000B2930_gen(void); void sub_000B2930(void) { coop_replay(0x000B2930u, "StartPlayerWeaponPfx", sub_000B2930_gen); }
extern void sub_000A1840_gen(void); void sub_000A1840(void) { coop_replay(0x000A1840u, "SetBrainMachine", sub_000A1840_gen); }
extern void sub_000A7760_gen(void); void sub_000A7760(void) { coop_replay(0x000A7760u, "PlayerLargeMass", sub_000A7760_gen); }
extern void sub_000A59B0_gen(void); void sub_000A59B0(void) { coop_replay(0x000A59B0u, "UnlockHeading", sub_000A59B0_gen); }
extern void sub_000A6850_gen(void);
void sub_000A6850(void) { coop_replay(0x000A6850u, "IsPlayer", sub_000A6850_gen); }
extern void sub_000A6F60_gen(void);
void sub_000A6F60(void) { coop_replay(0x000A6F60u, "HeroSpecificTrigger", sub_000A6F60_gen); }
extern void sub_000ABF30_gen(void);
void sub_000ABF30(void) { coop_replay(0x000ABF30u, "SetToTriggerOnPlayer", sub_000ABF30_gen); }
extern void sub_000A68C0_gen(void);
void sub_000A68C0(void) { coop_replay(0x000A68C0u, "ToggleTriggerObjs", sub_000A68C0_gen); }
extern void sub_000A6300_gen(void);
void sub_000A6300(void) { coop_replay(0x000A6300u, "Trigger", sub_000A6300_gen); }
extern void sub_000B0410_gen(void);
void sub_000B0410(void) { coop_replay(0x000B0410u, "AreaActivate", sub_000B0410_gen); }
extern void sub_000AF610_gen(void);
void sub_000AF610(void) { coop_replay(0x000AF610u, "GetPlayer", sub_000AF610_gen); }
extern void sub_000A5B80_gen(void);
void sub_000A5B80(void) { coop_replay(0x000A5B80u, "FrcAttackOn", sub_000A5B80_gen); }
extern void sub_000A5AC0_gen(void);
void sub_000A5AC0(void) { coop_replay(0x000A5AC0u, "DevAttackOn", sub_000A5AC0_gen); }
extern void sub_000A5B20_gen(void);
void sub_000A5B20(void) { coop_replay(0x000A5B20u, "SpdAttackOn", sub_000A5B20_gen); }
extern void sub_000A5BE0_gen(void);
void sub_000A5BE0(void) { coop_replay(0x000A5BE0u, "DefAttackOn", sub_000A5BE0_gen); }
extern void sub_000A5C30_gen(void);
void sub_000A5C30(void) { coop_replay(0x000A5C30u, "KDAttackOn", sub_000A5C30_gen); }
extern void sub_000AA540_gen(void);
void sub_000AA540(void) { coop_replay(0x000AA540u, "WeaponCollisionOn", sub_000AA540_gen); }
extern void sub_000A5960_gen(void);
void sub_000A5960(void) { coop_replay(0x000A5960u, "LockHeading", sub_000A5960_gen); }
extern void sub_000A5850_gen(void);
void sub_000A5850(void) { coop_replay(0x000A5850u, "LockHeadingToTarget", sub_000A5850_gen); }
extern void sub_000B0310_gen(void);
void sub_000B0310(void) { coop_replay(0x000B0310u, "SetTurnSpeed", sub_000B0310_gen); }
extern void sub_000A5A70_gen(void);
void sub_000A5A70(void) { coop_replay(0x000A5A70u, "LockOutAttack", sub_000A5A70_gen); }
extern void sub_000B0670_gen(void);
void sub_000B0670(void) { coop_replay(0x000B0670u, "SetTargetLock", sub_000B0670_gen); }
extern void sub_000A56D0_gen(void);
void sub_000A56D0(void) { coop_replay(0x000A56D0u, "SetHeading", sub_000A56D0_gen); }
extern void sub_000A8E30_gen(void);
void sub_000A8E30(void) { coop_replay(0x000A8E30u, "Rotate", sub_000A8E30_gen); }
extern void sub_000A91D0_gen(void);
void sub_000A91D0(void) { coop_replay(0x000A91D0u, "SetRotation", sub_000A91D0_gen); }
extern void sub_000A0F30_gen(void); void sub_000A0F30(void) { coop_replay(0x000A0F30u, "SetAnimMachine", sub_000A0F30_gen); }
int coop_resolved_hook(void) { if (coop_replaying) return 1; coop_resolved = 1; return 0; }
/* (research, for the enemy multiplier) the spawn pool's script commands: which levels use them, on which objects.
 * TT_SPAWNLOG=1 logs each call: the object running the script (0x9E180), its name and class, and what the command returned. */
static void coop_spawnlog(const char *name, void (*gen)(void))
{
    uint32_t ctx = MEM32(esp + 4), o = 0, h, n0 = 0; char nm[40]; int q; static unsigned cnt[8]; unsigned k = (unsigned)(name[0] + name[2]) & 7;
    int on = getenv("TT_SPAWNLOG") != NULL;
    if (on) { o = tt_gcall(0x0009E180u, 0, 0, NULL); n0 = coop_lkn; }
    gen();
    if (!on) return;
    {   /* (every 10 s: each command's calls since, and the living enemies in play: where, their state) */
        static const char *nms[8]; static unsigned nn[8]; static uint32_t lt; int j;
        for (j = 0; j < 8 && nms[j] && nms[j] != name; j++) ;
        if (j < 8) { nms[j] = name; nn[j]++; }
        if (coop_now() - lt > 10000u) { unsigned i, al = 0; lt = coop_now(); tt_dbg("[SPAWNSUM] calls in 10 s:"); for (j = 0; j < 8 && nms[j]; j++) { tt_dbg(" %s %u", nms[j], nn[j]); nn[j] = 0; } tt_dbg("\n");
            for (i = 0; i < coop_nchars; i++) { uint32_t e = coop_chars[i]; if (MEM32(e + 0x114) != 3 || (int32_t)MEM32(e + 0x2FC) <= 0 || (MEM32(e + 0x110) & 0x200000u)) continue;
                if (al++ < 12) tt_dbg("[SPAWNSUM]   alive %08X '%s' hp %d state %u at %.1f %.1f %.1f\n", e, coop_nm(e), (int32_t)MEM32(e + 0x2FC), MEM32(e + 0x408), MEMF(e + 0xF0), MEMF(e + 0xF4), MEMF(e + 0xF8)); }
            tt_dbg("[SPAWNSUM]   %u enemies alive in play\n", al); }
    }
    h = o >= 0x10000u ? MEM32(o + 0x18) : 0;
    for (q = 0; q < 39 && h >= 0x10000u && MEM8(h + q) >= 32 && MEM8(h + q) < 127; q++) nm[q] = (char)MEM8(h + q);
    nm[q] = 0;
    if (cnt[k]++ < 400) {
        tt_dbg("[SPAWN] level %u @%s by %08X vt %08X '%s' ctx %08X -> %08X (lookups %u)\n", MEM32(0x1BB2A4u), name, o, o >= 0x10000u ? MEM32(o) : 0, nm, ctx, eax, coop_lkn - n0);
        { unsigned i; for (i = n0; i < coop_lkn && i < n0 + 4; i++) { uint32_t lo = coop_lk[i & 63].obj, lh = lo >= 0x10000u ? MEM32(lo + 0x18) : 0; char ln[40];
            for (q = 0; q < 39 && lh >= 0x10000u && MEM8(lh + q) >= 32 && MEM8(lh + q) < 127; q++) ln[q] = (char)MEM8(lh + q);
            ln[q] = 0; tt_dbg("[SPAWN]   looked up %08X vt %08X '%s'\n", lo, lo >= 0x10000u ? MEM32(lo) : 0, ln); } }
    }
}
/* the enemy multiplier's kinds: orcs that fight in melee -- Moria's lesser orcs and Helm's Deep's orcs, with or
 * without a shield (a playtest: "only multiply melee type of small orc and their variants, dont apply it for ranged
 * enemies"). Not archers, Uruk-hai, bombers, berserkers, scouts or trolls. Pooled kinds per level (TT_SPAWNLOG
 * survey): Gates of Moria lesserOrcMelee 40, orcArcher 14; Rohan urukHaiBomber; the Deeping Wall urukHaiBomber,
 * OrcMelee, orcShield, UrukHaiMelee, urukHaiBerserker, urukHaiScout, troll; the Courtyard UrukHaiMelee, orcShield,
 * rWarrior, OrcMelee, lesserOrcShield, urukHaiBerserker, RohanArcher. TT_COOP_MULTALL: every kind, as before. */
static const char *coop_nm(uint32_t a);
static int coop_mult_kind(uint32_t a)
{
    const char *n;
    if (getenv("TT_COOP_MULTALL")) return 1;
    if (a < 0x10000u || a >= 0x84000000u || MEM32(a) != 0x1741C0u) return 0;
    n = coop_nm(a);
    return !_strnicmp(n, "lesserOrcMelee", 14) || !_strnicmp(n, "lesserOrcShield", 15) || !_strnicmp(n, "OrcMelee", 8) || !_strnicmp(n, "orcShield", 9);
}
/* a spawner's next ready character: its ready list (+0x3D0: count, +4 the first node, +8 the last; a node's +0xC is
 * the character), which 0x930F0 takes the first of (0xD0990) */
static uint32_t coop_spawner_next(uint32_t sp)
{
    uint32_t hd = MEM32(sp + 0x3D4);
    return (int32_t)MEM32(sp + 0x3D0) > 0 && hd >= 0x10000u && hd < 0x84000000u ? MEM32(hd + 0xC) : 0;
}
/* @SpawnAnObject: the level's script asks a spawner (the object running it) for one; in co-op the spawner sends out
 * up to (multiplier - 1) more from its ready list (its vtable +0xC4, 0x930F0, as the command does), each one only if
 * it is of a multiplied kind (coop_mult_kind) */
static int coop_enemy_mult(void);
extern void sub_000AB520_gen(void); void sub_000AB520(void)
{
    uint32_t sp = coop_mode() ? tt_gcall(0x0009E180u, 0, 0, NULL) : 0, ra = eax;
    int m, i;
    coop_spawnlog("SpawnAnObject", sub_000AB520_gen);
    if (!sp || sp < 0x10000u || MEM32(sp) != 0x174B60u || getenv("TT_COOP_MULTNOSPAWN")) return;
    ra = eax; m = coop_enemy_mult();
    for (i = 1; i < m && (int32_t)MEM32(sp + 0x3D0) > 1; i++) {   /* (one always left ready for the script: its own next @SpawnAnObject finding the
        * list empty ended that spawner's loop -- at the cave troll the orcs stopped coming within a minute with the extras, and came all
        * along without them: 3-8 spawns every 10 s for 200 s, bot runs) */
        uint32_t v = 0, nx = coop_spawner_next(sp), got;
        if (!coop_mult_kind(nx)) { static int n; if (n++ < 60) tt_dbg("[COOPMULT] @SpawnAnObject on %08X: no extra, its next is '%s' (not a melee orc)\n", sp, nx ? coop_nm(nx) : "-"); break; }
        got = tt_gcall(MEM32(MEM32(sp) + 0xC4u), sp, 1, &v);
        { static int n; if (n++ < 60) tt_dbg("[COOPMULT] @SpawnAnObject on %08X: extra %d of %d sent out %08X '%s'\n", sp, i, m - 1, got, got ? coop_nm(got) : "-"); }
        if (!got) break;
    }
    eax = ra;
}
/* the enemy multiplier. A spawner (class 0x174B60) keeps its characters in two lists, ready (+0x3D0) and spawned
 * (+0x3DC); each frame it moves a spawned one whose +0x3B8 is set back to ready, hidden, to be spawned again
 * (@MakeReadyForRespawn sets it). In co-op, a pooled character dead for 10 s gets +0x3B8 again, up to
 * (multiplier - 1) times: easy 1x, medium 2x, hard 3x (TT_COOP_ENEMYMULT sets it). Placed enemies that no
 * spawner owns (Weathertop's) are not multiplied. */
static struct { uint32_t spawner, who, dead_t, sent; int revived, pend; } coop_pool[512]; static int coop_npool;
static int coop_enemy_mult(void)
{
    int d = tt_coop_setting(0), m;
    if (getenv("TT_COOP_ENEMYMULT")) m = atoi(getenv("TT_COOP_ENEMYMULT"));
    else m = d == 0 ? 1 : d >= 2 ? 3 : 2;
    return m < 1 ? 1 : m > 3 ? 3 : m;
}
extern void sub_000A6480_gen(void); void sub_000A6480(void)
{
    unsigned n0 = coop_lkn, i;
    coop_spawnlog("AddToSpawnPool", sub_000A6480_gen);
    if (!coop_mode() || eax != 1) return;
    {   uint32_t sp = 0, who = 0; int k;
        for (i = n0; i < coop_lkn && i < n0 + 4; i++) { uint32_t o = coop_lk[i & 63].obj; if (o < 0x10000u) continue; if (MEM32(o) == 0x174B60u && !sp) sp = o; else if (MEM32(o) == 0x1741C0u && !who) who = o; }
        if (!sp || !who) return;
        for (k = 0; k < coop_npool; k++) if (coop_pool[k].who == who) { coop_pool[k].spawner = sp; return; }
        if (coop_npool < 512) { coop_pool[coop_npool].spawner = sp; coop_pool[coop_npool].who = who; coop_pool[coop_npool].dead_t = 0; coop_pool[coop_npool].revived = 0; coop_pool[coop_npool].pend = 0; coop_pool[coop_npool].sent = 0; coop_npool++; }
    }
}
static void coop_pool_frame(void);
void coop_pool_frame_x(void) { coop_pool_frame(); }
static void coop_pool_frame(void)
{
    int k, m = coop_enemy_mult(); uint32_t now = coop_now();
    static uint32_t lt;
    if (!coop_npool || (m <= 1 && !getenv("TT_SPAWNLOG"))) return;
    for (k = 0; k < coop_npool; k++) {
        uint32_t a = coop_pool[k].who; int32_t hp;
        if (a < 0x10000u || MEM32(a) != 0x1741C0u) continue;
        hp = (int32_t)MEM32(a + 0x2FC);
        if (coop_pool[k].pend && (MEM32(a + 0x110) & 0x200000u)) {   /* back in the ready list: the spawner sends one out now (its vtable +0xC4, as @SpawnAnObject does; the level's script spawns in bursts and may not ask again) */
            uint32_t sp = coop_pool[k].spawner, v = 0, got;
            coop_pool[k].pend = 0;
            if (sp >= 0x10000u && MEM32(sp) == 0x174B60u && !getenv("TT_COOP_MULTNOSPAWN") && (int32_t)MEM32(sp + 0x3D0) > 1 && coop_mult_kind(coop_spawner_next(sp))) {   /* (one left ready, as above) */
                got = tt_gcall(MEM32(MEM32(sp) + 0xC4u), sp, 1, &v);
                if (got >= 0x10000u) MEM8(got + 0x3B8) = 0;
                coop_pool[k].sent = got;   /* (left set, the spawner's next update took him straight back into its pool, hidden; observed) */
                tt_dbg("[COOPMULT] spawner %08X sends out %08X (revived %08X)%s\n", sp, got, a, got == a ? "" : got ? " (another of its pool)" : " (none)");
            }
            continue;
        }
        if (hp > 0) { coop_pool[k].dead_t = 0; continue; }
        if (MEM8(a + 0x3B8)) continue;   /* already going back */
        if (!coop_mult_kind(a)) continue;   /* (only melee orcs come back: coop_mult_kind) */
        if (!coop_pool[k].dead_t) { coop_pool[k].dead_t = now | 1; continue; }
        if (now - coop_pool[k].dead_t < (getenv("TT_COOP_MULTDELAY") ? (uint32_t)atoi(getenv("TT_COOP_MULTDELAY")) : 10000u) || coop_pool[k].revived >= m - 1) continue;   /* (4 s: revived during his death sequence, whose end set +0x3B8 again, and the spawner took him back: observed) */
        MEM8(a + 0x3B8) = 1; coop_pool[k].revived++; coop_pool[k].dead_t = 0; coop_pool[k].pend = 1;
        tt_dbg("[COOPMULT] %08X '%s' back to spawner %08X's pool (revival %d of %d)\n", a, coop_nm(a), coop_pool[k].spawner, coop_pool[k].revived, m - 1);
    }
    if (getenv("TT_SPAWNLOG") && now - lt > 5000) {   /* (testing) the pooled characters' health and revivals */
        int alive = 0, dead = 0, rv = 0; lt = now;
        for (k = 0; k < coop_npool; k++) { uint32_t a = coop_pool[k].who; if (a < 0x10000u) continue; if ((int32_t)MEM32(a + 0x2FC) > 0) alive++; else dead++; rv += coop_pool[k].revived; }
        { int inplay = 0; for (k = 0; k < coop_npool; k++) { uint32_t a = coop_pool[k].who; if (a >= 0x10000u && (int32_t)MEM32(a + 0x2FC) > 0 && !(MEM32(a + 0x110) & 0x200000u)) inplay++; }
          tt_dbg("[COOPMULT] %d pooled: %d with health, %d without, %d in play; %d revivals (x%d)\n", coop_npool, alive, dead, inplay, rv, m); }
        {   int up = 0, pooled = 0, down = 0; for (k = 0; k < coop_npool; k++) { uint32_t a = coop_pool[k].who; if (a < 0x10000u || !coop_pool[k].revived) continue;
                if ((int32_t)MEM32(a + 0x2FC) > 0 && !(MEM32(a + 0x110) & 0x200000u)) up++; else if (MEM32(a + 0x110) & 0x200000u) pooled++; else down++; }
            if (rv) tt_dbg("[COOPMULT]   revived ones: %d back in play with health, %d waiting in a pool, %d dead again\n", up, pooled, down);
            for (k = 0, up = 0; k < coop_npool && up < 4; k++) { uint32_t a = coop_pool[k].who; if (a < 0x10000u || !coop_pool[k].revived) continue; up++;
                tt_dbg("[COOPMULT]     %08X hp %d +3B8 %u +110 %08X state %u at %.1f %.1f\n", a, (int32_t)MEM32(a + 0x2FC), MEM8(a + 0x3B8), MEM32(a + 0x110), MEM32(a + 0x408), MEMF(a + 0xF0), MEMF(a + 0xF4)); }
            for (k = 0, up = 0; k < coop_npool && up < 4; k++) { uint32_t a = coop_pool[k].sent; if (a < 0x10000u) continue; up++;
                tt_dbg("[COOPMULT]     sent out %08X hp %d +3B8 %u +110 %08X state %u at %.1f %.1f\n", a, (int32_t)MEM32(a + 0x2FC), MEM8(a + 0x3B8), MEM32(a + 0x110), MEM32(a + 0x408), MEMF(a + 0xF0), MEMF(a + 0xF4)); } }
        for (k = 0, dead = 0; k < coop_npool && dead < 4; k++) { uint32_t a = coop_pool[k].who; if (a < 0x10000u || (int32_t)MEM32(a + 0x2FC) > 0 || (MEM32(a + 0x110) & 0x200000u)) continue; dead++;
            tt_dbg("[COOPMULT]   dead %08X hp %d +3B8 %u +110 %08X state %u, dead for %u ms, revived %d\n", a, (int32_t)MEM32(a + 0x2FC), MEM8(a + 0x3B8), MEM32(a + 0x110), MEM32(a + 0x408), coop_pool[k].dead_t ? now - coop_pool[k].dead_t : 0, coop_pool[k].revived); }
    }
}
extern void sub_000AB5B0_gen(void); void sub_000AB5B0(void) { coop_spawnlog("MakeReadyForRespawn", sub_000AB5B0_gen); }
extern void sub_000AB630_gen(void); void sub_000AB630(void) { coop_spawnlog("SpawnedAllDead", sub_000AB630_gen); }
extern void sub_000A65B0_gen(void); void sub_000A65B0(void) { coop_spawnlog("GetNumSpawnedAlive", sub_000A65B0_gen); }
extern void sub_000A6520_gen(void); void sub_000A6520(void) { coop_spawnlog("RemoveFromSpawnPool", sub_000A6520_gen); }
extern void sub_000A8650_gen(void); void sub_000A8650(void) { coop_spawnlog("SetInstancedCharactersEnabled", sub_000A8650_gen); }
/* the actor manager's 0x000C6150 (thiscall 0x43C5E8; actor, n; ret 8): @PassBrain's work (and other callers').
 * Logged for both heroes; the @PassBrain call (return 0xA5155) on player 1 is repeated for the twin during
 * the set-up (the handler 0xA50F0 itself can't be wrapped). TT_COOP_NOPASSBRAIN turns the repeat off. */
extern void sub_000C6150_gen(void);
void sub_000C6150(void)
{
    uint32_t m = ecx, a = MEM32(esp + 4), n = MEM32(esp + 8), ra = MEM32(esp);
    sub_000C6150_gen();
    if (coop_mode() == 3 && a >= 0x10000u && (a == coop_first_char || a == coop_twin)) {
        static int k;
        if (k++ < 60) tt_dbg("[COOPBRAIN] 0xC6150(%s %08X, %u) from %08X (player 2 %08X)\n", a == coop_twin ? "twin" : "original", a, n, ra, coop_p2);
        if (a == coop_first_char && ra == 0x000A5155u && !coop_p2 && coop_twin >= 0x10000u && !getenv("TT_COOP_NOPASSBRAIN")) {
            uint32_t sp = esp, r0 = eax, r1 = ecx, r2 = edx;
            PUSH32(esp, n); PUSH32(esp, coop_twin); PUSH32(esp, 0x000C6150u); ecx = m; sub_000C6150_gen();   /* ret 8 */
            tt_dbg("[COOP2] @PassBrain(%u) repeated for the twin; esp %s\n", n, esp == sp ? "balanced" : "NOT balanced");
            esp = sp; eax = r0; ecx = r1; edx = r2;
        }
    }
}/* 0x0005A450 (cdecl, from 0x5AAC0 in the world update): a player-only pass over the world's objects with
 * [0x43B15C]/[0x43B160]. After player 1's, the same for player 2 with his globals in place.
 * TT_COOP_NO5A450 turns it off. */
extern void sub_0005A450_gen(void);
void sub_0005A450(void)
{
    tt_coop_p1_pre(MEM32(0x43B15Cu), 0x0005A450u); sub_0005A450_gen(); tt_coop_p1_post();
    if (coop_p2 && !coop_depth && MEM32(0x43B15Cu) != coop_p2 && !getenv("TT_COOP_NO5A450")) {
        uint32_t ra = eax, rc = ecx, rd = edx, c1 = MEM8(0x43B1C0u);
        float x = MEMF(coop_p2 + 0xF0), y = MEMF(coop_p2 + 0xF4); static unsigned nmv; static float dsum;
        coop_save(coop_s1); coop_load(coop_s2); MEM8(0x43B1C0u) = (uint8_t)c1; coop_depth = 1;
        PUSH32(esp, 0x0005A450u); sub_0005A450_gen();
        coop_save(coop_s2); coop_load(coop_s1); coop_depth = 0;
        dsum += (MEMF(coop_p2 + 0xF0) - x) * (MEMF(coop_p2 + 0xF0) - x) + (MEMF(coop_p2 + 0xF4) - y) * (MEMF(coop_p2 + 0xF4) - y);
        if (++nmv % 120 == 0) { tt_dbg("[COOP2]          P2 world pass 5A450: sum of squared steps over 120 calls %.4f\n", dsum); dsum = 0; }
        eax = ra; ecx = rc; edx = rd;
    }
    if (tt_vs_on()) vs_targets();   /* (Versus) the pass clears the target (0x53B30) of every character outside its lists, the
                                        other hero included: put each hero's lock on the other back */
    if (coop_p2) {   /* (research) the engagement lists 0x5A920 builds its candidates from, once every 2 s */
        static uint32_t last; if (coop_now() - last > 2000) { static const uint32_t L[4][2] = { { 0x4110C0u, 0x411394u }, { 0x411250u, 0x4114A4u }, { 0x411140u, 0x4114ACu }, { 0x411398u, 0x4110BCu } };
            int l; last = coop_now();
            for (l = 0; l < 4; l++) { uint32_t n = MEM32(L[l][1]), i; char b[200]; int o = 0; if (n > 32) n = 32;
                for (i = 0; i < n && o < 180; i++) { uint32_t a = MEM32(L[l][0] + 4 * i); const char *tag = a == coop_first_char ? "P1" : a == coop_p2 ? "P2" : a >= 0x10000u && MEM32(a + 0x114) == 3 ? "E" : a >= 0x10000u && MEM32(a + 0x114) == 2 ? "F" : "?"; b[o++] = ' '; while (*tag) b[o++] = *tag++; }
                b[o] = 0; tt_dbg("[COOPENG] list %08X (%u):%s\n", L[l][0], MEM32(L[l][1]), b); } }
    }
}/* 0x000BAD10: the handler of an enemy's attack box against its target (box = [esp+4], attacker = box+8; it compares the
 * target with [0x43B15C], and for the player sends the hero the events his combat rating meter (record +4) runs on).
 * When the attacker's target is player 2, it runs with his context in place. TT_COOP_NOENEMYBOXSWAP off. */
extern void sub_000BAD10_gen(void);
void sub_000BAD10(void)
{
    static int off = -1; int sw = 0; extern int tt_coop_enter_ctx(void); extern void tt_coop_leave(void);
    uint32_t b = MEM32(esp + 4), a = b >= 0x10000u ? MEM32(b + 8) : 0, c = a >= 0x10000u ? MEM32(a + 0x494) : 0;
    if (off < 0) off = getenv("TT_COOP_NOENEMYBOXSWAP") != 0;
    if (!off && coop_mode() == 3 && coop_p2 && c >= 0x10000u && MEM32(c + 0x98) == coop_twin && coop_twin) { sw = tt_coop_enter_ctx(); { static unsigned n; if ((n++ & 255) == 0) tt_dbg("[COOPEXP] enemy attack boxes on player 2 handled in his context: %u\n", n); } }
    sub_000BAD10_gen();
    if (sw) tt_coop_leave();
}
/* (research) the hero record's rating methods: who runs them, on whose record (player 2's while in his context), and
 * what +4 does. TT_COOP_RATELOG=1. */
static unsigned coop_rl_n[6][2];
static void coop_ratelog(int idx, uint32_t fn, void (*gen)(void))
{
    extern uint32_t coop_rec_addr; uint32_t rec = ecx, ra = MEM32(esp), a1 = MEM32(esp + 4), a2 = MEM32(esp + 8); float b, af;
    int who = MEM32(0x43B15Cu) == coop_p2 && coop_p2 ? 1 : 0, on = getenv("TT_COOP_RATELOG") != 0, isrec = rec == coop_rec_addr && rec;
    b = isrec ? MEMF(rec + 4) : 0;
    gen();
    if (idx == 1 && who) { extern uint32_t coop_p2_drain_t; coop_p2_drain_t = coop_now() | 1; }
    if (!on || !coop_rec_addr) return;
    af = MEMF(coop_rec_addr + 4);
    coop_rl_n[idx][who]++;
    if (idx == 2) { static int k2[2]; if (k2[who]++ < 4) tt_dbg("[COOPRATE] 09DF70 (frame step) for %s: dt arg %08X (%.4f), record %08X, +5C %.2f, from %06X\n", who ? "P2" : "P1", a1, *(float *)&a1, rec, MEMF(rec + 0x5C), ra); }
    { static int k; if ((af != b || idx == 3) && k++ < 60) tt_dbg("[COOPRATE] %06X on %s record%s: +4 %.2f -> %.2f, +5C %.2f | args %08X %08X | from %06X\n", fn, who ? "P2's" : "P1's", isrec ? "" : " (ecx not the hero record)", b, af, MEMF(coop_rec_addr + 0x5C), a1, a2, ra); }
    { static uint32_t last; if (coop_now() - last > 5000) { int i; last = coop_now(); for (i = 0; i < 6; i++) tt_dbg("[COOPRATE] calls %d: P1 %u P2 %u\n", i, coop_rl_n[i][0], coop_rl_n[i][1]); } }
}
extern void sub_0009D9D0_gen(void); void sub_0009D9D0(void) { coop_ratelog(0, 0x9D9D0, sub_0009D9D0_gen); }
extern void sub_0009DEC0_gen(void); void sub_0009DEC0(void) { coop_ratelog(1, 0x9DEC0, sub_0009DEC0_gen); }
extern void sub_0009DF70_gen(void); void sub_0009DF70(void) { coop_ratelog(2, 0x9DF70, sub_0009DF70_gen); }
extern void sub_00099AB0_gen(void); void sub_00099AB0(void) { coop_ratelog(3, 0x99AB0, sub_00099AB0_gen); }
extern void sub_0009DA00_gen(void); void sub_0009DA00(void) { coop_ratelog(4, 0x9DA00, sub_0009DA00_gen); }
extern void sub_0009D3E0_gen(void); void sub_0009D3E0(void) { coop_ratelog(5, 0x9D3E0, sub_0009D3E0_gen); }
/* 0x000849F0 (thiscall victim, message; called from the character message handler 0x85340): a character's hit handling.
 * At 0x84DF8 it raises the rating (0x99E00 -> 0x99AB0 -> 0x9D9D0, record +4) only when the victim's last attacker
 * (+0x4BC) is [0x43B15C], so player 2's hits never counted. When the last attacker is player 2, it runs in his context
 * ([0x43B15C] = him, his record). TT_COOP_NOHITSWAP off. */
extern void sub_000849F0_gen(void);
void sub_000849F0(void)
{
    static int off = -1; int sw = 0; extern int tt_coop_enter_ctx(void); extern void tt_coop_leave(void);
    uint32_t v = ecx;
    if (off < 0) off = getenv("TT_COOP_NOHITSWAP") != 0;
    int p2hit = 0;
    {   /* whose hit: a hit message {0x103, weapon or projectile, victim} is player 2's when he or his object (owner +0x11C)
         * made it. The victim's last attacker (+0x4BC) is only set by this call, so it named the one before: player 2's
         * first hit on each enemy was missed and the next hits by player 1 or an ally were counted his (TT_COOP_HITLOG). */
        uint32_t m = MEM32(esp + 4), src = m >= 0x10000u && MEM32(m) == 0x103u ? MEM32(m + 4) : 0;
        if (m >= 0x10000u && MEM32(m) == 0x103u) p2hit = src && (src == coop_p2 || (src >= 0x10000u && MEM32(src + 0x11C) == coop_p2));
        else p2hit = v >= 0x10000u && MEM32(v + 0x4BC) == coop_p2;
    }
    if (getenv("TT_COOP_HITSRCLOG") && coop_p2 && v >= 0x10000u && MEM32(v + 0x114) == 3) {   /* (research) each hit on an enemy: its source, the source's owner, whose */
        uint32_t m = MEM32(esp + 4), src = m >= 0x10000u && MEM32(m) == 0x103u ? MEM32(m + 4) : 0; static int k;
        uint32_t ow = src >= 0x10000u && src < 0x84000000u ? MEM32(src + 0x11C) : 0;
        if (m >= 0x10000u && MEM32(m) == 0x103u && (src == coop_p2 || src == coop_first_char || ow == coop_p2 || ow == coop_first_char) && k++ < 120) tt_dbg("[HITSRC] t %u on %08X: source %08X '%s' owner +11C %08X -> %s (depth %d, player 2 %08X, his meter %.1f)\n", coop_now(), v, src, coop_nm(src),
            src >= 0x10000u && src < 0x84000000u ? MEM32(src + 0x11C) : 0, p2hit ? "player 2's" : "not his", coop_depth, coop_p2, coop_rec_addr ? MEMF(coop_rec_addr + 4) : -1.0f);
    }
    { extern int coop_is_lurtz(uint32_t e); if (p2hit && coop_is_lurtz(v)) { static int n; if (n++ < 4) tt_dbg("[COOPLURTZ] player 2's hit on %s: handled as player 1's\n", coop_nm(v)); p2hit = 0; coop_lurtz_hit = v; coop_lurtz_p2_hits++; } }
    if (!off && coop_mode() == 3 && coop_p2 && !coop_depth && v >= 0x10000u && v != coop_p2 && p2hit) {
        sw = tt_coop_enter_ctx(); { static unsigned n; if ((n++ & 63) == 0) tt_dbg("[COOPEXP] hits by player 2 handled in his context: %u (victim %08X hp %d)\n", n, v, (int32_t)MEM32(v + 0x2FC)); } }
    {   /* (research) TT_COOP_HITLOG: each hit's victim, its last attacker and its health before and after */
        static int hl = -1; uint32_t hp0 = v >= 0x10000u ? MEM32(v + 0x2FC) : 0, at = v >= 0x10000u ? MEM32(v + 0x4BC) : 0, msg = MEM32(esp + 4);
        uint32_t st0 = v >= 0x10000u ? MEM32(v + 0x408) : 0, vh = v >= 0x10000u ? MEM32(v + 0x18) : 0;
        int troll = getenv("TT_COOP_TROLLLOG") && vh >= 0x10000u && vh < 0x84000000u && !_strnicmp((const char *)XBOX_PTR(vh), "TrollBoss", 9);
        if (hl < 0) hl = getenv("TT_COOP_HITLOG") ? atoi(getenv("TT_COOP_HITLOG")) : 0;   /* 1: player 2's hits, 2: all */
        if (getenv("TT_COOP_PROJSTACK") && msg >= 0x10000u && MEM32(msg) == 0x103u) {   /* (research) a projectile's hit: the code addresses on the stack (who called) */
            uint32_t src = MEM32(msg + 4), sh = src >= 0x10000u ? MEM32(src + 0x18) : 0; static int n;
            if (sh >= 0x10000u && sh < 0x84000000u && !_strnicmp((const char *)XBOX_PTR(sh), "dynamicProjectile", 17) && n++ < 6) { uint32_t q; int k = 0; extern unsigned coop_proj_mark; extern uint32_t coop_proj_cur;
                tt_dbg("[PROJSTACK] marks %03X (bits: 0 DB150 1 DE630 2 DAD20 3 D6B40 4 D6530 5 D62F0 6 D5820 7 D50D0 8 D5960), projectile in update %08X\n", coop_proj_mark, coop_proj_cur);
                tt_dbg("[PROJSTACK] %s (vtable %08X) hits %08X '%s': msg %08X %08X %08X %08X %08X | stack:", (const char *)XBOX_PTR(sh), MEM32(src), v, coop_nm(v), MEM32(msg), MEM32(msg + 4), MEM32(msg + 8), MEM32(msg + 0xC), MEM32(msg + 0x10));
                for (q = 0; q < 0x400 && k < 14; q += 4) { uint32_t r = MEM32(esp + q); if (r >= 0x11000u && r < 0x170000u) { tt_dbg(" +%X:%06X", q, r); k++; } } tt_dbg("\n"); } }
        if (msg >= 0x10000u && MEM32(msg) == 0x103u && v >= 0x10000u) { extern void coop_pdmg_msg(uint32_t v, int32_t dmg); coop_pdmg_msg(v, (int32_t)MEM32(msg + 0x14));   /* (the damage: msg +0x14) */ }
        static uint32_t tdb[0x180]; int tdf = troll && getenv("TT_COOP_TROLLDIFF");   /* (research) the troll's fields a hit changes */
        if (tdf) { int q; for (q = 0; q < 0x180; q++) tdb[q] = MEM32(v + 4 * q); }
        { float mb = coop_rec_addr ? MEMF(coop_rec_addr + 4) : 0.0f; uint32_t m1 = MEM32(esp + 4), wsrc = m1 >= 0x10000u && MEM32(m1) == 0x103u ? MEM32(m1 + 4) : 0;
        sub_000849F0_gen();
        if (coop_lurtz_hit && coop_lurtz_hit == v) { if (MEM32(v + 0x4BC) == coop_p2 && coop_first_char) MEM32(v + 0x4BC) = coop_first_char; coop_lurtz_hit = 0; }   /* (his last attacker player 1's) */
        if (getenv("TT_COOP_HITSRCLOG") && v >= 0x10000u && MEM32(v + 0x114) == 3 && (p2hit || wsrc == coop_first_char || (wsrc >= 0x10000u && wsrc < 0x84000000u && MEM32(wsrc + 0x11C) == coop_first_char))) { static int k; if (k++ < 150)
            tt_dbg("[HITAFT] t %u %s hit by %s %08X '%s': hp %d -> %d, state %u -> %u, last attacker %08X -> %08X, player %08X, meter %.1f -> %.1f, msg +8..+18 %08X %08X %08X %08X %08X\n", coop_now(), p2hit ? "P2" : "P1", coop_nm(wsrc), v, coop_nm(v), (int32_t)hp0, (int32_t)MEM32(v + 0x2FC), st0, MEM32(v + 0x408), at, MEM32(v + 0x4BC),
                   MEM32(0x43B15Cu), mb, coop_rec_addr ? MEMF(coop_rec_addr + 4) : 0.0f, MEM32(m1 + 8), MEM32(m1 + 0xC), MEM32(m1 + 0x10), MEM32(m1 + 0x14), MEM32(m1 + 0x18)); } }
        if (v >= 0x10000u && coop_mode() == 3 && (int32_t)MEM32(v + 0x2FC) < (int32_t)hp0 && MEM32(v + 0x114) == 3) {   /* (its health lowered by this hit: whose) */
            uint32_t src = msg >= 0x10000u && MEM32(msg) == 0x103u ? MEM32(msg + 4) : 0, own = src >= 0x10000u && src < 0x84000000u ? MEM32(src + 0x11C) : 0;
            coop_lastdmg_note(v, p2hit ? 2 : (src == coop_first_char || own == coop_first_char || (!src && MEM32(v + 0x4BC) == coop_first_char)) ? 1 : 0); }
        if (tdf) { int q, m = 0; static int n; if (n++ < 40) { tt_dbg("[TROLLDIFF] hit from %s (state %u, block mask +428 %08X +42C %08X, inputs +3E0 %08X +3E4 %08X) changed:", coop_nm(MEM32(msg + 4)), MEM32(v + 0x408), MEM32(v + 0x428), MEM32(v + 0x42C), MEM32(v + 0x3E0), MEM32(v + 0x3E4));
            for (q = 0; q < 0x180 && m < 24; q++) if (MEM32(v + 4 * q) != tdb[q]) { tt_dbg(" +%X %08X->%08X", 4 * q, tdb[q], MEM32(v + 4 * q)); m++; } tt_dbg("\n"); } }
        if (troll) {   /* (research) every hit on the troll: its state then, the source, the health change */
            uint32_t src = msg >= 0x10000u && MEM32(msg) == 0x103u ? MEM32(msg + 4) : 0, own = src >= 0x10000u ? MEM32(src + 0x11C) : 0, sh = src >= 0x10000u ? MEM32(src + 0x18) : 0;
            static unsigned k; if (k++ < 300) tt_dbg("[TROLLHIT] state %u -> %u, hp %d -> %d | msg type %X from %08X '%s' owner %08X (%s) | msg %08X %08X %08X %08X%c", st0, MEM32(v + 0x408), (int32_t)hp0, (int32_t)MEM32(v + 0x2FC),
                msg >= 0x10000u ? MEM32(msg) : 0, src, sh >= 0x10000u && sh < 0x84000000u ? (const char *)XBOX_PTR(sh) : "?", own,
                src == coop_first_char || own == coop_first_char ? "player 1" : src == coop_p2 || own == coop_p2 ? "player 2" : "other",
                msg >= 0x10000u ? MEM32(msg + 8) : 0, msg >= 0x10000u ? MEM32(msg + 0xC) : 0, msg >= 0x10000u ? MEM32(msg + 0x10) : 0, msg >= 0x10000u ? MEM32(msg + 0x14) : 0, 10);
        }
        if (hl && coop_p2 && v >= 0x10000u && (hl > 1 || p2hit || sw)) { static unsigned k; if (k++ < 400)
            tt_dbg("[HITLOG] t %u msg %08X [%08X %08X %08X] victim %08X%s hp %d -> %d | last attacker %08X (%s) -> %08X | swapped %d (src owner %08X) meter %.2f\n", coop_now(), msg,
                   msg >= 0x10000u ? MEM32(msg) : 0, msg >= 0x10000u ? MEM32(msg + 4) : 0, msg >= 0x10000u ? MEM32(msg + 8) : 0, v, v == coop_p2 ? " (P2)" : v == MEM32(0x43B15Cu) ? " (P1)" : "",
                   (int32_t)hp0, (int32_t)MEM32(v + 0x2FC), at, at == coop_p2 ? "P2" : at == MEM32(0x43B15Cu) ? "P1" : "other", MEM32(v + 0x4BC), sw, msg >= 0x10000u && MEM32(msg + 4) >= 0x10000u ? MEM32(MEM32(msg + 4) + 0x11C) : 0, coop_rec_addr ? MEMF(coop_rec_addr + 4) : 0.0f); }
    }
    if (sw) tt_coop_leave();
}
/* (research) TT_COOP_TGTLOG: the target setters on either hero's component: 0x539F0 (thiscall: target, 3 details; ret 16),
 * 0x53AE0 (thiscall: target; ret 4), 0x54410 (thiscall: 4 args; ret 16), with the caller -- who aims the player */
static void coop_tgt_log(const char *fn, uint32_t comp, uint32_t tgt)
{
    static int on = -1; static unsigned n; const char *who;
    if (on < 0) on = getenv("TT_COOP_TGTLOG") != 0;
    if (!on || n >= 400 || comp < 0x10000u) return;
    who = coop_first_char >= 0x10000u && comp == MEM32(coop_first_char + 0x494) ? "P1" : coop_twin >= 0x10000u && comp == MEM32(coop_twin + 0x494) ? "P2" : 0;
    if (!who || tgt == MEM32(comp + 0x98)) return;
    n++; tt_dbg("[COOPTGT] %s %s target %08X -> %08X from %08X, %08X (context %d)\n", fn, who, MEM32(comp + 0x98), tgt, MEM32(esp), MEM32(esp + 0x20), coop_depth);
}
extern void sub_000539F0_gen(void);
void sub_000539F0(void) { coop_tgt_log("539F0", ecx, MEM32(esp + 4)); sub_000539F0_gen(); }
extern void sub_00053AE0_gen(void);
void sub_00053AE0(void) { coop_tgt_log("53AE0", ecx, MEM32(esp + 4)); sub_00053AE0_gen(); }
extern void sub_00054410_gen(void);
void sub_00054410(void) { coop_tgt_log("54410", ecx, MEM32(esp + 4)); sub_00054410_gen(); }
/* a hero's own aim target (thiscall on the arbitration, the hero; ret 4; from 0x5A330 for each hero): the heading of
 * the camera or of the hero ([0x43B110] ? [0x43B10C] : camera +0x60) plus the stick's angle [0x43B18C] picks the enemy
 * (0x599C0 scores each, also against [0x43B15C]) and 0x539F0 sets it. For player 2 it ran with player 1's globals:
 * his lock-on followed player 1's stick and camera. Run in his context. TT_COOP_NOP2AIMCTX: as before. */
extern void sub_0005A150_gen(void);
void sub_0005A150(void)
{
    static int off = -1; uint32_t h = MEM32(esp + 4); int sw = 0;
    if (off < 0) off = getenv("TT_COOP_NOP2AIMCTX") != 0;
    if (!off && coop_mode() == 3 && coop_twin >= 0x10000u && h == coop_twin && !coop_depth) {
        uint32_t r1 = ecx; sw = tt_coop_enter_ctx(); ecx = r1;
        { static unsigned n; if (!n++) tt_dbg("[COOPAIM] player 2's aim target picked in his context (his stick and heading)\n"); }
    }
    {   /* (Versus) each hero's aim may pick the other: the aim pick (0x599C0) takes a candidate whose role's bit is in
         * the picking hero's +0x3A4 (heroes: the enemies' 3); player 1's role is 1, player 2's 2 */
        uint32_t arb = ecx;
        if (tt_vs_on() && (h == coop_first_char || h == coop_twin) && coop_first_char >= 0x10000u && coop_twin >= 0x10000u) {
            uint32_t o = h == coop_first_char ? coop_twin : coop_first_char, n = arb >= 0x10000u ? MEM32(arb + 0x340) : 0, i; int in = 0;
            MEM32(h + 0x3A4) |= 1u << (MEM32(o + 0x114) & 31u);
            for (i = 0; i < n && i < 256; i++) if (MEM32(arb + 4 * i) == o) in = 1;
            if (getenv("TT_VS_AIMLOG")) { static unsigned k; if (k++ % 300 == 0) tt_dbg("[VSAIM] %s picks: arbitration %08X, %u candidates, the other hero %s; mask %08X, other's role %u\n",
                h == coop_first_char ? "P1" : "P2", arb, n, in ? "among them" : "NOT among them", MEM32(h + 0x3A4), MEM32(o + 0x114)); }
        }
    }
    {   /* allies out of the pick: the arbitration's actors (0x411510, count 0x411850) without the other hero and the
         * fellowship (+0x114 2) while it runs (a playtest: aiming locked on allies; the other hero is on the list in co-op).
         * TT_COOP_AIMALLIES: as before */
        static uint32_t saved[0xC0]; uint32_t n = MEM32(0x411850u), i, k = 0, cut = 0;
        int strip = coop_mode() == 3 && coop_p2 >= 0x10000u && n <= 0xC0u && !tt_vs_on() && !getenv("TT_COOP_AIMALLIES");   /* (not in Versus: there the other hero is the one to aim at) */
        if (strip) {
            for (i = 0; i < n; i++) { uint32_t q = MEM32(0x411510u + 4 * i); saved[i] = q;
                if (q != h && q >= 0x10000u && q < 0x84000000u && (q == coop_p2 || q == coop_first_char || q == coop_twin || MEM32(q + 0x114) == 2)) { cut++; continue; }
                MEM32(0x411510u + 4 * k++) = q; }
            MEM32(0x411850u) = k;
        }
        sub_0005A150_gen();
        if (strip) { uint32_t r0 = eax; for (i = 0; i < n; i++) MEM32(0x411510u + 4 * i) = saved[i]; MEM32(0x411850u) = n; eax = r0;
            if (getenv("TT_COOP_AIMALLYLOG") && cut) { static uint32_t lt; if (coop_now() - lt > 1000u) { lt = coop_now(); tt_dbg("[COOPAIM] %s's pick: %u of %u left out (allies), lock now %08X\n", h == coop_first_char ? "P1" : "P2", cut, n, MEM32(MEM32(h + 0x494) + 0x98)); } } }
    }
    if (sw) { uint32_t r0 = eax; tt_coop_leave(); eax = r0; }
}
/* the game's target arbitration (cdecl, from 0x5AAC0 in the world update): threat retargeting right after it */
extern void sub_0005A920_gen(void);
void sub_0005A920(void)
{
    uint32_t r0;
    {   /* player 2 as a candidate target: appended to the engagement list 0x4110C0 (count 0x411394, 32 slots), which the
         * arbitration reads after [0x43B15C]; so the game itself can give attack slots against him. TT_COOP_NOP2CAND off. */
        extern int coop_hero_down(int); static int off = -1; if (off < 0) off = getenv("TT_COOP_NOP2CAND") != 0;
        if (!off && coop_mode() == 3 && coop_p2 >= 0x10000u && !coop_hero_down(1)) {
            uint32_t n = MEM32(0x411394u), i; for (i = 0; i < n && i < 32 && MEM32(0x4110C0u + 4 * i) != coop_p2; i++) ;
            if (i == n && n < 32) { MEM32(0x4110C0u + 4 * n) = coop_p2; MEM32(0x411394u) = n + 1; }
        }
    }
    if (getenv("TT_COOP_AIMLISTLOG") && coop_p2 >= 0x10000u) {   /* (research) every 5 s: the arbitration 0x411510's actors (count
         * +0x340), which 0x5A330 gives the aim pick 0x5A150 when their type data (0x6CAB0(type)) has +0x11F set */
        static uint32_t t; extern uint32_t tt_gcall(uint32_t va, uint32_t thisptr, int n, const uint32_t *args);
        if (coop_now() - t > 5000u) { uint32_t n = MEM32(0x411850u), i, in1 = 0, in2 = 0, a1 = MEM32(coop_first_char + 0x2F0), a2 = MEM32(coop_p2 + 0x2F0), d1, d2;
            t = coop_now();
            for (i = 0; i < n && i < 0xC0; i++) { uint32_t e = MEM32(0x411510u + 4 * i); if (e == coop_first_char) in1 = i + 1; if (e == coop_p2) in2 = i + 1; }
            d1 = tt_gcall(0x0006CAB0u, 0, 1, &a1); d2 = tt_gcall(0x0006CAB0u, 0, 1, &a2);
            tt_dbg("[AIMLIST] %u actors | P1 at %u (type %u, data %08X +11F %u) | P2 at %u (type %u, data %08X +11F %u)\n", n, in1, a1, d1, d1 >= 0x10000u ? MEM8(d1 + 0x11F) : 0,
                   in2, a2, d2, d2 >= 0x10000u ? MEM8(d2 + 0x11F) : 0); }
    }
    if (getenv("TT_VS_AIMLOG") && tt_vs_on()) { static unsigned k; if (k++ % 600 == 0) tt_dbg("[VSAIM] arbitration: enemies list %u, 411250 list %u, 411140 list %u; player %08X, P2 %08X\n", MEM32(0x411394u), MEM32(0x4114A4u), MEM32(0x4114ACu), MEM32(0x43B15Cu), coop_p2); }
    sub_0005A920_gen(); r0 = eax; coop_threat_apply(); eax = r0;
}
/* shared camera: the camera has no target of its own; about 20 camera functions read the player's
 * position [0x43B15C]+0xF0..0xF8. For the length of each camera update (0x68DC0) player 1's position is
 * the midpoint of both heroes, then it is put back. Player 2's swapped control step also calls the camera
 * update: skipped there. TT_COOP_NOSHAREDCAM turns the shared camera off. */
int tt_coop_camera_pre(uint32_t *saved)
{
    uint32_t p1 = MEM32(0x43B15Cu), k;
    if (!coop_p2 || coop_p2 < 0x10000u) return 0;
    if (coop_depth || p1 == coop_p2) return -1;
    if (p1 < 0x10000u || getenv("TT_COOP_NOSHAREDCAM") || coop_gameover) return 0;   /* game over: the game's own death cam, untouched */
    {   /* the point the camera frames: the heroes' midpoint; while one is down, the one still standing */
        float wa = coop_down[0] && !coop_down[1] ? 0.0f : coop_down[1] && !coop_down[0] ? 1.0f : 0.5f;
        if (getenv("TT_COOP_CAMMIDALWAYS")) wa = 0.5f;
        for (k = 0; k < 3; k++) { saved[k] = MEM32(p1 + 0xF0 + 4 * k); MEMF(p1 + 0xF0 + 4 * k) = wa * MEMF(p1 + 0xF0 + 4 * k) + (1.0f - wa) * MEMF(coop_p2 + 0xF0 + 4 * k); } }
    saved[3] = p1;
    return 1;
}
void tt_coop_camera_post(const uint32_t *saved)
{
    uint32_t k, cm = MEM32(0x43B164u), p1 = saved[3];
    float dx, dy, sep, z, mx, base, per;
    for (k = 0; k < 3; k++) MEM32(saved[3] + 0xF0 + 4 * k) = saved[k];
    if (cm < 0x10000u || coop_p2 < 0x10000u || getenv("TT_COOP_NOCAMZOOM")) { coop_cam_zoom = 1.0f; return; }
    /* zoom out to keep both heroes in view, by the field of view (sub_0004E890): moving the camera's own vectors fed
     * back through its smoothing and ran away into the walls (observed). The factor grows with the heroes' distance:
     * 1 up to TT_COOP_CAMBASE (4) units apart, + 1 per TT_COOP_CAMPER (14) units more, at most TT_COOP_CAMMAX (1.5: 2.0, a 79-degree view, was too wide in play) */
    dx = MEMF(p1 + 0xF0) - MEMF(coop_p2 + 0xF0); dy = MEMF(p1 + 0xF4) - MEMF(coop_p2 + 0xF4); sep = sqrtf(dx * dx + dy * dy);
    if ((coop_down[0] || coop_down[1]) && !getenv("TT_COOP_CAMMIDALWAYS")) sep = 0.0f;   /* one hero down: the camera is on the other, no zoom-out */
    base = getenv("TT_COOP_CAMBASE") ? (float)atof(getenv("TT_COOP_CAMBASE")) : 4.0f;
    per = getenv("TT_COOP_CAMPER") ? (float)atof(getenv("TT_COOP_CAMPER")) : 20.0f;   /* (was 14) */
    {   static const float zmax[3] = { 1.0f, 1.3f, 1.5f };   /* the Co-op Options' Camera Zoom: off, normal, wide (normal 1.5 was "a bit intense" in play: 1.3, wide 1.5) */
        int zs = tt_coop_setting(2);
        mx = getenv("TT_COOP_CAMMAX") ? (float)atof(getenv("TT_COOP_CAMMAX")) : zmax[zs >= 0 && zs < 3 ? zs : 1];
    }
    z = getenv("TT_COOP_CAMZOOM") ? (float)atof(getenv("TT_COOP_CAMZOOM")) : 1.0f + (sep > base ? (sep - base) / per : 0.0f);
    if (z > mx) z = mx; if (z < 1.0f) z = 1.0f;
    coop_cam_zoom += (z - coop_cam_zoom) * 0.08f;   /* eased, about half a second */
    { extern void coop_persp_reapply(void); coop_persp_reapply(); }
    {   static uint32_t lt; if (getenv("TT_COOP_CAMLOG") && coop_now() - lt > 2000) { lt = coop_now(); tt_dbg("[COOPCAM] heroes %.1f apart: zoom %.2f (eased %.2f), perspective calls %u, last fov %.3f -> %.3f\n", sep, z, coop_cam_zoom, coop_persp_n, coop_persp_fov[0], coop_persp_fov[1]); } }
}
/* a projectile's hit (0x000DA180, thiscall on the projectile: the step, the ray's result from 0x000699C0, which 0x000DAD20
 * casts with the shooter at +0x11C left out; ret 8; the port's own hook 23 wraps it): it sends the hit and stops the
 * projectile. A hero's projectile (player 1's or player 2's) hits only characters it is meant for, and flies on through
 * the rest (playtests: arrows stopped on allies; "only the target and the obstacles"): with the shooter's lock on a
 * living enemy (component +0x98), only that one; without, any but an ally (the other hero, the fellowship: +0x114 2).
 * Walls and props are left to the game. The character hit is found in the ray's result (TT_COOP_PROJLOG).
 * TT_COOP_PROJHITALL: as before. 1 = let through (no hit) */
/* every hit of a hero's projectile counts: a hit leaves its damage on the victim's state machine (+0x3E0 / +0x3E4 input
 * bits, +0x3F0 the damage, +0x3F4 the attacker), applied a frame later only in some of its states (the cave troll: 2
 * of 12 arrows in state 15, none of its block masks set; allies' arrows the same, so the game's own rule). The hit is
 * noted here (coop_proj_pass, then 0x849F0 gives the damage); if the victim's health hasn't gone down by it 3 frames
 * later, it is taken off here. A blow that would kill is left to the game (health kept at 1). A playtest wanted every
 * arrow to count. TT_COOP_NOPROJDMG: as before; TT_COOP_PROJLOG. */
static struct { uint32_t v, sh; int32_t hp, dmg; int fr; } coop_pdmg[16];
static void coop_pdmg_note(uint32_t v, uint32_t sh)
{
    int i; extern int coop_is_lurtz(uint32_t e); if (getenv("TT_COOP_NOPROJDMG") || coop_is_lurtz(v)) return;
    for (i = 0; i < 16; i++) if (!coop_pdmg[i].v) { coop_pdmg[i].v = v; coop_pdmg[i].sh = sh; coop_pdmg[i].hp = (int32_t)MEM32(v + 0x2FC); coop_pdmg[i].dmg = -1; coop_pdmg[i].fr = 0; return; }
}
void coop_pdmg_msg(uint32_t v, int32_t dmg)   /* from 0x849F0: a projectile's hit on v */
{
    int i; for (i = 0; i < 16; i++) if (coop_pdmg[i].v == v && coop_pdmg[i].dmg < 0) { coop_pdmg[i].dmg = dmg; coop_pdmg[i].hp = (int32_t)MEM32(v + 0x2FC); return; }
}
static void coop_pdmg_frame(void);
void coop_pdmg_frame_x(void) { coop_pdmg_frame(); }
static void coop_pdmg_frame(void)
{
    int i;
    for (i = 0; i < 16; i++) { uint32_t v = coop_pdmg[i].v; int32_t now;
        if (!v) continue;
        if (++coop_pdmg[i].fr < 3) continue;
        now = (int32_t)MEM32(v + 0x2FC);
        if (coop_pdmg[i].dmg > 0 && now > 0 && now > coop_pdmg[i].hp - coop_pdmg[i].dmg && MEM32(v) == 0x1741C0u) {
            int32_t nw = now - coop_pdmg[i].dmg; if (nw < 1) nw = getenv("TT_COOP_PDMGKEEP1") ? 1 : 0;   /* (a killing blow to 0: the game's own death follows -- the troll went to its
                * state 20 at once; left at 1, as before, the enemy stayed at 1 and the next hit, anyone's, got the kill: player 2 Legolas' arrow kills
                * went to others, a playtest; bot run: the troll at 1 through two more arrows) */
            MEM32(v + 0x2FC) = (uint32_t)nw; MEM32(v + 0x4BC) = coop_pdmg[i].sh; MEM32(v + 0x3F0) = 0;   /* (its pending damage gone: not taken twice later) */
            coop_lastdmg_note(v, coop_pdmg[i].sh == coop_p2 ? 2 : 1);
            if (MEM32(v + 0x114) == 3) MEM8(v + 0x526) = 1;   /* (a hero hurt it: the kill award's flag, as 0x84220 sets it -- sub_00084220) */
            if (getenv("TT_COOP_PROJLOG")) tt_dbg("[PROJDMG] %s's hit on %s (state %u) not taken by the game: health %d -> %d here%s\n", coop_pdmg[i].sh == coop_p2 ? "P2" : "P1", coop_nm(v), MEM32(v + 0x408), now, nw, now - coop_pdmg[i].dmg < 1 ? " (a killing blow)" : "");
        } else if (getenv("TT_COOP_PROJLOG")) tt_dbg("[PROJDMG] %s's hit on %s: taken by the game (health %d -> %d, damage %d)\n", coop_pdmg[i].sh == coop_p2 ? "P2" : "P1", coop_nm(v), coop_pdmg[i].hp, now, coop_pdmg[i].dmg);
        coop_pdmg[i].v = 0;
    }
}
int coop_proj_pass(void)
{
    uint32_t pr = ecx, q = MEM32(esp + 8), sh = pr >= 0x10000u ? MEM32(pr + 0x11C) : 0, hit = 0, off = 0, k, c, lk;
    int i, pass;
    if (coop_mode() != 3 || coop_p2 < 0x10000u || (sh != coop_p2 && sh != coop_first_char) || q < 0x10000u || tt_vs_on() || getenv("TT_COOP_PROJHITALL")) return 0;   /* (not in Versus: the other hero is the target there) */
    for (k = 0; k < 0xD0 && !hit; k += 4) { uint32_t a = MEM32(q + k);
        if (a < 0x10000u || a >= 0x84000000u || a == sh) continue;
        if (a == coop_p2 || a == coop_first_char) { hit = a; off = k; break; }
        for (i = 0; i < coop_nchars; i++) if (coop_chars[i] == a) { hit = a; off = k; break; } }
    c = MEM32(sh + 0x494); lk = c >= 0x10000u ? MEM32(c + 0x98) : 0;
    if (lk < 0x10000u || lk >= 0x84000000u || MEM32(lk) != 0x1741C0u || MEM32(lk + 0x114) != 3 || (int32_t)MEM32(lk + 0x2FC) <= 0) lk = 0;
    pass = hit && (hit == coop_p2 || hit == coop_first_char || MEM32(hit + 0x114) == 2 || (lk && hit != lk));
    if (hit && !pass && MEM32(hit + 0x114) == 3) coop_pdmg_note(hit, sh);
    if (getenv("TT_COOP_PROJLOG")) { static int n; if (n++ < 60) tt_dbg("[PROJHIT] %s's projectile %08X hits %08X %s (role %d, result +%X), his lock %08X: %s\n", sh == coop_p2 ? "P2" : "P1", pr,
        hit, hit ? coop_nm(hit) : "(no character)", hit ? (int)MEM32(hit + 0x114) : -1, off, lk, pass ? "let through" : "hit"); }
    return pass;
}
/* (research, TT_COOP_PROJSTACK) the projectile class (vtable 0x17BCB8): its update 0xDB150 and what it calls, marked
 * while they run, so a hit reaching 0x849F0 tells which of them sent it */
unsigned coop_proj_mark; uint32_t coop_proj_cur;
extern void sub_000DB150_gen(void); void sub_000DB150(void) { unsigned m = coop_proj_mark; uint32_t c = coop_proj_cur; coop_proj_mark |= 1u << 0; if (0 == 0) coop_proj_cur = ecx; sub_000DB150_gen(); coop_proj_mark = m; coop_proj_cur = c; }
extern void sub_000DE630_gen(void); void sub_000DE630(void) { unsigned m = coop_proj_mark; uint32_t c = coop_proj_cur; coop_proj_mark |= 1u << 1; if (1 == 0) coop_proj_cur = ecx; sub_000DE630_gen(); coop_proj_mark = m; coop_proj_cur = c; }
extern void sub_000DAD20_gen(void); void sub_000DAD20(void) { unsigned m = coop_proj_mark; uint32_t c = coop_proj_cur; coop_proj_mark |= 1u << 2; if (2 == 0) coop_proj_cur = ecx; sub_000DAD20_gen(); coop_proj_mark = m; coop_proj_cur = c; }
extern void sub_000D6B40_gen(void); void sub_000D6B40(void) { unsigned m = coop_proj_mark; uint32_t c = coop_proj_cur; coop_proj_mark |= 1u << 3; if (3 == 0) coop_proj_cur = ecx; sub_000D6B40_gen(); coop_proj_mark = m; coop_proj_cur = c; }
extern void sub_000D6530_gen(void); void sub_000D6530(void) { unsigned m = coop_proj_mark; uint32_t c = coop_proj_cur; coop_proj_mark |= 1u << 4; if (4 == 0) coop_proj_cur = ecx; sub_000D6530_gen(); coop_proj_mark = m; coop_proj_cur = c; }
extern void sub_000D62F0_gen(void); void sub_000D62F0(void) { unsigned m = coop_proj_mark; uint32_t c = coop_proj_cur; coop_proj_mark |= 1u << 5; if (5 == 0) coop_proj_cur = ecx; sub_000D62F0_gen(); coop_proj_mark = m; coop_proj_cur = c; }
extern void sub_000D5820_gen(void); void sub_000D5820(void) { unsigned m = coop_proj_mark; uint32_t c = coop_proj_cur; coop_proj_mark |= 1u << 6; if (6 == 0) coop_proj_cur = ecx; sub_000D5820_gen(); coop_proj_mark = m; coop_proj_cur = c; }
extern void sub_000D50D0_gen(void); void sub_000D50D0(void) { unsigned m = coop_proj_mark; uint32_t c = coop_proj_cur; coop_proj_mark |= 1u << 7; if (7 == 0) coop_proj_cur = ecx; sub_000D50D0_gen(); coop_proj_mark = m; coop_proj_cur = c; }
extern void sub_000D5960_gen(void); void sub_000D5960(void) { unsigned m = coop_proj_mark; uint32_t c = coop_proj_cur; coop_proj_mark |= 1u << 8; if (8 == 0) coop_proj_cur = ecx; sub_000D5960_gen(); coop_proj_mark = m; coop_proj_cur = c; }
/* trigger planes post their events from 0x00093B10 (an actor's move from one spot to the next tested against the planes;
 * the actor is let through, the player [0x43B15C] only while [0x43B058] is set) to the event queue 0x43C628 through
 * 0x000C7030 (cdecl: type 0xD / 0xE / 0xF, plane, actor, 0). Player 2's events on a plane named camera... are dropped:
 * player 1 alone switches the camera (playtests: at Gates of Moria it went back and forth as player 2 followed; at the
 * cave troll, with player 2 down on the other side of a plane, it switched without end). Other planes (encounters)
 * still see him. (0x000975E0, wrapped for this before, is the plane's on / off: it only runs at load.)
 * TT_COOP_P2CAMTRIG: as before; TT_COOP_TRIGLOG: the plane events (research). */
extern void sub_000C7030_gen(void);
void sub_000C7030(void)
{
    uint32_t ty = MEM8(esp + 4), pl = MEM32(esp + 8), ac = MEM32(esp + 0xC), h = 0; const char *nm = "?";
    if (getenv("TT_COOP_PROJSTACK")) {   /* (research) events from or about a projectile: type, the caller */
        static int n; uint32_t q[2] = { pl, ac }; int i;
        for (i = 0; i < 2; i++) { uint32_t o = q[i], hn = o >= 0x10000u && o < 0x84000000u ? MEM32(o + 0x18) : 0;
            if (hn >= 0x10000u && hn < 0x84000000u && !_strnicmp((const char *)XBOX_PTR(hn), "dynamicProjectile", 17) && n++ < 40) {
                tt_dbg("[PROJEV] type %u from %08X arg %08X %08X (the projectile is arg %d) | called from %06X, then %06X%c", ty, pl, ac, MEM32(esp + 0x10), i + 2, MEM32(esp), MEM32(esp + 0x14), 10); break; } } }
    if (getenv("TT_COOP_EVLOG")) {   /* (research) every event posted: its type, sender (and its name, if an object's) and argument */
        static int n; uint32_t h2 = pl >= 0x10000u && pl < 0x84000000u ? MEM32(pl + 0x18) : 0;
        if (ty == 21 && h2 >= 0x10000u && h2 < 0x84000000u && !_strnicmp((const char *)XBOX_PTR(h2), "camera", 6)) { static int m; if (m++ < 1) { int q; tt_dbg("[EVLOG] %s %08X: vtable %08X:", (const char *)XBOX_PTR(h2), pl, MEM32(pl)); for (q = 0; q < 40; q++) tt_dbg(" %d:%06X", q, MEM32(MEM32(pl) + 4 * q)); tt_dbg("\n"); } }
        if (ty != 6 && ty != 21 && (ty < 32 || ty > 37) && n++ < 3000) tt_dbg("[EVLOG] t %u type %u from %08X '%s' arg %08X%s %08X (ret %06X)\n", coop_now(), ty, pl, h2 >= 0x10000u && h2 < 0x84000000u ? (const char *)XBOX_PTR(h2) : "",
            ac, ac == coop_p2 && ac ? " (player 2)" : ac == coop_first_char && ac ? " (player 1)" : "", MEM32(esp + 0x10), MEM32(esp));
    }
    if (coop_mode() == 3 && coop_p2 >= 0x10000u && ty >= 0xDu && ty <= 0xFu && pl >= 0x10000u && pl < 0x84000000u) {
        h = MEM32(pl + 0x18); if (h >= 0x10000u && h < 0x84000000u) nm = (const char *)XBOX_PTR(h);
        if (coop_triglog_on()) { static int n; if (n++ < 400) tt_dbg("[TRIGLOG] t %u event %u plane %08X '%s' actor %08X (%s)\n", coop_now(), ty, pl, nm,
            ac, ac == coop_p2 ? "player 2" : ac == coop_first_char ? "player 1" : "other"); }
        /* every plane and trigger is player 1's: player 2's events on them are dropped (a playtest: "p2 seems to fire triggers it
         * shouldnt, only p1 can triggers everything"; before, only the camera planes were). TT_COOP_P2TRIGGERS: as before */
        if (getenv("TT_COOP_TESTDROPALL") && h) {   /* (test) every plane event dropped, player 1's too: the drop's own stack handling */
            static int n; if (n++ < 12) tt_dbg("[COOPTRIG] test: event %u on '%s' by %08X dropped%s\n", ty, nm, ac, getenv("TT_COOP_TESTDROPOLD") ? " (the old 4-byte pop)" : "");
            if (getenv("TT_COOP_TESTDROPOLD")) { esp += 4; return; }
            eax = 0; esp += 20; return;
        }
        if (ac == coop_p2 && h && _strnicmp(nm, "camera", 6) && _strnicmp(nm, "dynamicPickup", 13) && !getenv("TT_COOP_P2TRIGGERS")) {   /* (pickups are his to take) */
            static int n; if (n++ < 12) tt_dbg("[COOPTRIG] player 2's event %u on '%s' dropped: triggers are player 1's\n", ty, nm);
            eax = 0; esp += 20; return;   /* (thiscall, 4 arguments: ret 16) */
        }
        if (ac == coop_p2 && h && !_strnicmp(nm, "camera", 6) && !getenv("TT_COOP_P2CAMTRIG")) {
            static int n; if (n++ < 8) tt_dbg("[COOPCAM] player 2's event %u on '%s' dropped: the camera is player 1's\n", ty, nm);
            eax = 0; esp += 20; return;   /* (ret 16: with esp += 4, as until 2026-10-25, each dropped event left 16 bytes on the
                                             * stack -- with every trigger dropped, a hang at the next call: a bad VA, 0x108828) */
        }
    }
    sub_000C7030_gen();
}
/* player 2's own weapons. The level script attaches each hero's weapons with @AttachToBone(hero, bone,
 * weapon); the weapons are placed objects of their own (player 1: weaponObj6/7/8, vtable 0x174980).
 * When @AttachToBone is replayed for the twin, a copy of that weapon's placement record (renamed, own key)
 * is built with the loader's 0x000F5BC0, given the loader's init (vtable slot 0 = 0x8FFE0) and
 * registration (0xC5E20), and the replay's object lookup answers it in place of player 1's weapon.
 * No copy (no memory) -> no replay, so player 1 keeps his. */
#define COOP_NRECKEY 8192   /* (600 filled up in Gates of Moria before the heroes' weapons came: player 2 had no weapons of his own and showed player 1's sword, flickering between them) */
static struct { uint32_t rec, key; } coop_reckey[COOP_NRECKEY]; static int coop_nreckey;
static uint32_t coop_last_obj, coop_attach_child, coop_new_weapon;
extern void sub_00019880_gen(void);
void sub_00019880(void)   /* world link (thiscall world; obj, type; ret 8) */
{
    uint32_t o = MEM32(esp + 4);
    sub_00019880_gen();
    coop_last_obj = o;
}
/* player 2's weapons are his own hero's, whatever player 1 plays: each hero's weapons by the slot the level script
 * attaches them to (@AttachToBone's second argument; the same for the four heroes and their fellowship NPCs,
 * observed in Balin's Tomb, TT_COOP_WLOG). Player 1's attaches, replayed for the twin, give the copy player 2's weapon
 * for that slot, or none; player 2's slots player 1 has not get copies of their own (coop_p2_extra_weapons).
 * A guest: Aragorn's set, his own weapon (heroes.c's pair for Aragorn's 4586A62C) in the main hand. */
typedef struct { const char *who; uint32_t m[4], t[4]; } CoopWset;
static const CoopWset coop_wsets[4] = {
    { "aragorn", { 0x13F82514u, 0, 0, 0x69DAC4B8u }, { 0x29636120u, 0, 0, 0xBD9E230Bu } },          /* sword, dagger */
    { "legolas", { 0xEF8EBCECu, 0xEF8EBCECu, 0x69DAC4B8u, 0 }, { 0x9126E92Au, 0x9126E92Au, 0xBD9E230Bu, 0 } },   /* two knives, dagger */
    { "gimli",   { 0x2B9252D0u, 0, 0, 0 }, { 0xAB926BE7u, 0, 0, 0 } },                             /* axe */
    { "isildur", { 0x13F82514u, 0, 0, 0x69DAC4B8u }, { 0x29636120u, 0, 0, 0xBD9E230Bu } },          /* sword, dagger */
};
static int coop_p1_wslot(int b) { int hb = MEM8(0x4AB69Du); return b >= 0 && b < 4 && hb >= 1 && hb <= 4 && coop_wsets[hb - 1].m[b] != 0; }
static int coop_p2_wslot(int b, uint32_t *m, uint32_t *t)
{
    extern const char *tt_coop_p2_char_now; extern uint32_t tt_coop_skin_pairs[8][2]; extern int tt_coop_skin_n;
    const char *who = tt_coop_p2_char_now; int i, q;
    if (b < 0 || b > 3 || !who) return 0;
    for (i = 0; i < 4; i++) if (!_stricmp(who, coop_wsets[i].who)) { *m = coop_wsets[i].m[b]; *t = coop_wsets[i].t[b]; return *m != 0; }
    *m = coop_wsets[0].m[b]; *t = coop_wsets[0].t[b];   /* a guest */
    if (b == 0) for (q = 0; q + 1 < tt_coop_skin_n; q++) if (tt_coop_skin_pairs[q][0] == 0x4586A62Cu) { *m = tt_coop_skin_pairs[q][1]; *t = tt_coop_skin_pairs[q + 1][1]; break; }
    return *m != 0;
}
static uint32_t coop_build_weapon_as(uint32_t child, int bone, int extra);
static uint32_t coop_force_model, coop_force_tex;   /* (coop_p2_ranged_copy) the copy's model and texture, in place of the slot's */
/* player 2's ranged weapon. A hero aims and throws with a ranged weapon object he takes when he aims (0x00085FB0, the
 * second argument 0xA): Legolas' bow (model 0834F2D4), Gimli's throwing axe (E7EEC090), Aragorn's bow (36AC3F58). The
 * shot is drawn with it. Player 2 was refused player 1's (it turned player 1's), so his shots had no model: Gimli's axes
 * flew invisible. Now he gets a copy of player 1's with his own hero's ranged model (brought into the level by
 * heroes.c's rebuild), built once per weapon. Legolas and Gimli; others: none. TT_COOP_NOP2RANGED: off. */
static uint32_t coop_rng_orig[8], coop_rng_copy[8]; static int coop_rng_n; static unsigned coop_rng_lv;
static int coop_ranged_model(uint32_t w, uint32_t *km)
{
    uint32_t h = w >= 0x10000u ? MEM32(w + 0x18) : 0, rec = h >= 0x10008u ? h - 8 : 0;
    *km = rec ? MEM32(rec + 0x40) : 0;
    return *km == 0x0834F2D4u || *km == 0xE7EEC090u || *km == 0x36AC3F58u;
}
/* built while the level loads (the heap is closed in play: "no memory for a weapon copy now"), from coop_p2_extra_weapons */
static void coop_p2_ranged_build(void)
{
    extern const char *tt_coop_p2_char_now; extern uint32_t coop_wobjs[512]; extern int coop_nwobjs;
    const char *who = tt_coop_p2_char_now; uint32_t pm = 0, pt = 0; int i;
    if (coop_rng_lv != coop_level_n) { coop_rng_lv = coop_level_n; coop_rng_n = 0; }
    if (getenv("TT_COOP_NOP2RANGED") || !who) return;
    if (!strcmp(who, "legolas")) { pm = 0x0834F2D4u; pt = 0x4167DADCu; } else if (!strcmp(who, "gimli")) { pm = 0xE7EEC090u; pt = 0xAB926BE7u; }
    else if (!strcmp(who, "aragorn") || !strcmp(who, "isildur")) { pm = 0x36AC3F58u; pt = 0xD82C5CABu; } else return;   /* (Isildur: Aragorn's bow, heroes.c) */
    for (i = 0; i < coop_nwobjs && coop_rng_n < 8; i++) {
        uint32_t w = coop_wobjs[i], km, c; int q, dup = 0;
        if (w < 0x10000u || MEM32(w) != 0x174980u || !coop_ranged_model(w, &km)) continue;
        for (q = 0; q < coop_rng_n; q++) if (coop_rng_orig[q] == w) dup = 1;
        if (dup) continue;
        coop_force_model = pm; coop_force_tex = pt;
        c = coop_build_weapon_as(w, 7, 1);
        coop_force_model = coop_force_tex = 0;
        tt_dbg("[COOPWEAPON] player 2's ranged weapon: a copy of %08X '%s' (model %08X) with %s's %08X: %08X\n", w, (const char *)XBOX_PTR(MEM32(w + 0x18)), km, who, pm, c);
        if (getenv("TT_COOP_RECDUMP")) { uint32_t r = MEM32(w + 0x18) - 8, q; for (q = 0; q < 0x100; q += 16) tt_dbg("[RECDUMP] %s +%02X %08X %08X %08X %08X\n", (const char *)XBOX_PTR(MEM32(w + 0x18)), q, MEM32(r + q), MEM32(r + q + 4), MEM32(r + q + 8), MEM32(r + q + 12));
            if (c) for (q = 0; q < 0x500; q += 16) tt_dbg("[OBJDUMP] %s +%03X %08X %08X %08X %08X | copy %08X %08X %08X %08X\n", (const char *)XBOX_PTR(MEM32(w + 0x18)), q, MEM32(w + q), MEM32(w + q + 4), MEM32(w + q + 8), MEM32(w + q + 12), MEM32(c + q), MEM32(c + q + 4), MEM32(c + q + 8), MEM32(c + q + 12)); }   /* (research: the bow string) */
        if (c) { coop_rng_orig[coop_rng_n] = w; coop_rng_copy[coop_rng_n] = c; coop_rng_n++; }
    }
}
/* player 2 Legolas' arrow. Player 1 Legolas' draw (TT_COOP_TAKELOG, TT_COOP_WPNCMDLOG at the Gates of Moria): his knives into
 * 0xA (put away), the bow into slot 1 (the left hand), and an arrow, a weapon object of its own (model 4586A62C, texture
 * 3B6EEB8E: Legolas' weaponObj136, Aragorn's weaponObj135), @Activate'd and into slot 0 (the right hand); @Deactivate'd at
 * the release. Player 2's moves named player 1 Aragorn's arrow: he'd have taken it from him. So he gets a copy of one */
static uint32_t coop_p2_arrow_c; static unsigned coop_p2_arrow_lv;
static int coop_is_arrow(uint32_t w)
{
    uint32_t h = w >= 0x10000u && w < 0x84000000u && MEM32(w) == 0x174980u ? MEM32(w + 0x18) : 0, rec = h >= 0x10008u && h < 0x84000000u ? h - 8 : 0;
    return rec && MEM32(rec + 0x40) == 0x4586A62Cu && MEM32(rec + 0x50) == 0x3B6EEB8Eu;
}
uint32_t coop_p2_arrow(void) { return coop_p2_arrow_lv == coop_level_n ? coop_p2_arrow_c : 0; }
uint32_t coop_p2_arrow_for(uint32_t w) { return coop_p2_arrow() && w != coop_p2_arrow_c && coop_is_arrow(w) ? coop_p2_arrow_c : 0; }
static void coop_p2_arrow_build(void)
{
    extern const char *tt_coop_p2_char_now; extern uint32_t coop_wobjs[512]; extern int coop_nwobjs; int i;
    if (coop_p2_arrow_lv == coop_level_n) return;
    coop_p2_arrow_lv = coop_level_n; coop_p2_arrow_c = 0;
    if (!tt_coop_p2_char_now || (strcmp(tt_coop_p2_char_now, "legolas") && strcmp(tt_coop_p2_char_now, "aragorn") && strcmp(tt_coop_p2_char_now, "isildur")) || getenv("TT_COOP_NOP2ARROW")) return;   /* (the bowmen; Gimli throws) */
    for (i = 0; i < coop_nwobjs && !coop_p2_arrow_c; i++) if (coop_is_arrow(coop_wobjs[i])) {
        coop_p2_arrow_c = coop_build_weapon_as(coop_wobjs[i], 10, 1);   /* (slot 10: none of his melee slots; the copy is named ...m) */
        tt_dbg("[COOPWEAPON] player 2's arrow: a copy of %08X '%s': %08X\n", coop_wobjs[i], (const char *)XBOX_PTR(MEM32(coop_wobjs[i] + 0x18)), coop_p2_arrow_c); }
}
uint32_t coop_p2_ranged_copy(uint32_t w)
{
    uint32_t km; int i;
    if (getenv("TT_COOP_NOP2RANGED") || coop_rng_lv != coop_level_n || !coop_ranged_model(w, &km)) return 0;
    for (i = 0; i < coop_rng_n; i++) if (coop_rng_orig[i] == w) return coop_rng_copy[i];
    return 0;
}
int coop_rng_n_x(void) { return coop_rng_n; } uint32_t coop_rng_copy_x(int i) { return coop_rng_copy[i]; }
uint32_t coop_rng_pair(int copy) { return coop_rng_n ? (copy ? coop_rng_copy[0] : coop_rng_orig[0]) : 0; }   /* (research dumps) */
/* a weapon copy nobody holds or draws: a ranged copy of someone else's weapon (an NPC's bow), never taken by player 2.
 * Player 2's events name player 1's weapons he has no copy of (Legolas' second knife, for Gimli): those lookups get
 * this sink, so his aim's "hold the knife aside" lands on it and not on Legolas' knife (seen in play: Legolas' left weapon
 * turned while Gimli aimed). 0: none */
uint32_t coop_p2_weapon_sink(void)
{
    int i;
    if (coop_rng_lv != coop_level_n) return 0;
    for (i = coop_rng_n - 1; i >= 0; i--) if (coop_rng_orig[i] >= 0x10000u && MEM32(coop_rng_orig[i] + 0x11C) != coop_first_char) return coop_rng_copy[i];
    return 0;
}
/* player 2's bow: of his copies of the level's ranged weapons (each dressed in his hero's ranged model), a copy of one of
 * player 1's hero's kind first (hero byte 0x4AB69D: 1 Aragorn's bow 36AC3F58, 2 Legolas' 0834F2D4, 3 Gimli's axe
 * E7EEC090), then of any bow, the sink last. Observed with player 1 Aragorn at the Gates of Moria: the copies of his
 * bows (134j, 150j) were drawn right in player 2 Legolas' hand; the copy of the companion Gimli's throwing axe (140j) wasn't
 * drawn at all (a playtest: he shot with his blade), the companion Legolas' bow's (142j) hung across his shoulders.
 * 0: none */
static uint32_t coop_p2_bow_att; static unsigned coop_p2_bow_lv;
void coop_p2_bow_attached(uint32_t w) { if (coop_p2_bow_lv != coop_level_n || !coop_p2_bow_att) { coop_p2_bow_lv = coop_level_n; coop_p2_bow_att = w; tt_dbg("[COOPWEAPON] player 2's bow: %08X (attached at his set-up)\n", w); } }
uint32_t coop_p2_bow(void)
{
    if (coop_p2_bow_lv == coop_level_n && coop_p2_bow_att) return coop_p2_bow_att;   /* (the one attached at his set-up, above) */
    static const uint32_t kind[5] = { 0, 0x36AC3F58u, 0x0834F2D4u, 0xE7EEC090u, 0 };
    uint32_t want = kind[MEM8(0x4AB69Du) < 5 ? MEM8(0x4AB69Du) : 0], sink = coop_p2_weapon_sink(), best = 0; int i, pass;
    if (coop_rng_lv != coop_level_n) return 0;
    for (pass = 0; pass < 3 && !best; pass++)
        for (i = 0; i < coop_rng_n && !best; i++) { uint32_t o = coop_rng_orig[i], h = o >= 0x10000u ? MEM32(o + 0x18) : 0, m = h >= 0x10008u && h < 0x84000000u ? MEM32(h - 8 + 0x40) : 0, c = coop_rng_copy[i];
            if (!c || (pass < 2 && c == sink)) continue;
            if (pass == 0 ? (want && m == want) : (m == 0x0834F2D4u || m == 0x36AC3F58u)) best = c; }
    return best;
}
static uint32_t coop_build_weapon(uint32_t child) { return coop_build_weapon_as(child, coop_attach_bone_seen, 0); }
static int coop_extra_done;
/* player 2's slots player 1 has not: copies of the weapon being attached (its record as the model's carrier), attached
 * to the twin directly (his vtable +0xC4: weapon, slot), at the first replayed attach (the heap is open then) */
static void coop_p2_extra_weapons(uint32_t tmpl)
{
    extern int tt_coop_skin_on(void); int b;
    if (coop_extra_done || !tt_coop_skin_on() || coop_twin < 0x10000u || getenv("TT_COOP_NOEXTRAWPN") || coop_p2_p1wpn) return;   /* (player 1's set: nothing of his own's added) */
    coop_extra_done = 1;
    coop_p2_ranged_build(); coop_p2_arrow_build();
    for (b = 0; b < 4; b++) {
        uint32_t m2, t2, w, a[2];
        if (coop_p1_wslot(b) || !coop_p2_wslot(b, &m2, &t2)) continue;
        w = coop_build_weapon_as(tmpl, b, 1);
        if (!w) continue;
        a[0] = w; a[1] = (uint32_t)b;
        tt_gcall(MEM32(MEM32(coop_twin) + 0xC4u), coop_twin, 2, a);
        tt_dbg("[COOPWEAPON] player 2's own slot %d: %08X attached (model %08X)\n", b, w, m2);
    }
}
static uint32_t coop_build_weapon_as(uint32_t child, int bone, int extra)
{
    uint32_t h = MEM32(child + 0x18), rec = h - 8, key = 0, m, k, n, w, sp = esp, ra = eax, rc = ecx, rd = edx; int i;
    if (h < 0x10008u || !coop_world) return 0;
    if (!extra) for (i = 0; i < coop_nwpn; i++) if (coop_wpn_orig[i] == child) return coop_wpn_copy[i];   /* built already: the same copy again */
    {   extern int tt_coop_skin_on(void); uint32_t m2, t2;
        if (!extra && !coop_p2_p1wpn && tt_coop_skin_on() && !getenv("TT_COOP_OLDWPN") && !coop_p2_wslot(bone, &m2, &t2)) {
            tt_dbg("[COOPWEAPON] player 2 has nothing in slot %d: %s not copied\n", bone, (const char *)XBOX_PTR(h)); return 0; }
    }
    for (i = 0; i < coop_nreckey; i++) if (coop_reckey[i].rec == rec) { key = coop_reckey[i].key; break; }
    if (i == coop_nreckey) { tt_dbg("[COOPWEAPON] no loader key for record %08X\n", rec); return 0; }
    PUSH32(esp, 0x100u); PUSH32(esp, 0x000E6B50u); sub_000E6B50(); esp += 4; m = eax;
    if (m < 0x10000u) { tt_dbg("[COOPWEAPON] no memory for a weapon copy now (0xE6B50 gave %08X): not replayed\n", m); esp = sp; eax = ra; ecx = rc; edx = rd; return 0; }
    for (k = 0; k < 0x100; k += 4) MEM32(m + k) = MEM32(rec + k);
    for (n = 0; n < 0x1E && MEM8(m + 8 + n); n++) ;
    MEM8(m + 8 + n) = extra ? 'c' + (char)bone : 'b'; MEM8(m + 9 + n) = 0;   /* weaponObj6 -> weaponObj6b (an extra one: c, d, e, f by slot) */
    if (coop_force_model) { tt_dbg("[COOPWEAPON] copy's model %08X -> %08X, texture %08X -> %08X\n", MEM32(m + 0x40), coop_force_model, MEM32(m + 0x50), coop_force_tex);
        MEM32(m + 0x40) = coop_force_model; MEM32(m + 0x50) = coop_force_tex;
        if (!getenv("TT_COOP_KEEPREC48")) {   /* the record's +0x44..+0x4C go with the model: Aragorn's bow 0 044F7822 C51F591F, Legolas'
            * bow (player 1 Legolas' weaponObj132, the companion's weaponObj142) and Gimli's throwing axe zeros (observed,
            * TT_COOP_RECDUMP). Player 2 Legolas' bow, a copy of Aragorn's dressed in Legolas' model, kept Aragorn's pair; a playtest
            * saw it without its string (whether this brings it back: not checked) */
            uint32_t r44 = 0, r48 = 0, r4c = 0;
            if (coop_force_model == 0x36AC3F58u) { r48 = 0x044F7822u; r4c = 0xC51F591Fu; }
            MEM32(m + 0x44) = r44; MEM32(m + 0x48) = r48; MEM32(m + 0x4C) = r4c; } } else
    {   extern int tt_coop_skin_on(void); uint32_t m2, t2;   /* player 2's own for this slot */
        if (coop_p2_p1wpn) tt_dbg("[COOPWEAPON] slot %d: player 1's weapon kept (model %08X): this level gives player 2 player 1's\n", bone, MEM32(m + 0x40)); else
        if (tt_coop_skin_on() && !getenv("TT_COOP_OLDWPN") && coop_p2_wslot(bone, &m2, &t2)) {
            tt_dbg("[COOPWEAPON] slot %d: player 2's own, model %08X -> %08X, texture %08X -> %08X\n", bone, MEM32(m + 0x40), m2, MEM32(m + 0x50), t2);
            MEM32(m + 0x40) = m2; MEM32(m + 0x50) = t2; }
    }
    if (getenv("TT_COOP_OLDWPN")) {   /* (the earlier way: Aragorn's sword (model key +0x40) -> the skin's, its texture (+0x50) too) */
        extern uint32_t tt_coop_skin_pairs[8][2]; extern int tt_coop_skin_n; int q;
        tt_dbg("[COOPWEAPON] copy of %s: model %08X texture %08X\n", (const char *)XBOX_PTR(m + 8), MEM32(m + 0x40), MEM32(m + 0x50));
        for (q = 0; q + 1 < tt_coop_skin_n; q++) if (tt_coop_skin_pairs[q][0] == 0x4586A62Cu && (MEM32(m + 0x40) == 0x4586A62Cu || MEM32(m + 0x40) == 0x13F82514u)) {   /* Aragorn's sword, or the ranger's long sword he draws (heroes.c's skin_apply does the same) */
            tt_dbg("[COOPWEAPON] his own weapon: model %08X -> %08X, texture %08X -> %08X\n", MEM32(m + 0x40), tt_coop_skin_pairs[q][1], MEM32(m + 0x50), tt_coop_skin_pairs[q + 1][1]);
            MEM32(m + 0x40) = tt_coop_skin_pairs[q][1]; MEM32(m + 0x50) = tt_coop_skin_pairs[q + 1][1]; break; }
    }
    coop_last_obj = 0;
    PUSH32(esp, key ^ (extra ? 0x00040000u + ((uint32_t)bone << 12) : 0x00020000u)); PUSH32(esp, m); PUSH32(esp, 0x000F5BC0u); ecx = coop_world; sub_000F5BC0_gen();   /* ret 8 */
    w = coop_last_obj;
    if (w < 0x10000u || MEM32(w) != MEM32(child)) { tt_dbg("[COOPWEAPON] building a copy of %08X gave %08X (vt %08X): not replayed\n", rec, w, w >= 0x10000u ? MEM32(w) : 0); esp = sp; eax = ra; ecx = rc; edx = rd; return 0; }
    PUSH32(esp, 0x0008FFE0u); ecx = w; sub_0008FFE0();                                   /* init, as the loader's walk */
    PUSH32(esp, w); PUSH32(esp, 0x000C5E20u); ecx = 0x43C5E8u; sub_000C5E20();           /* register (ret 4) */
    if (getenv("TT_COOP_WEAPONSTATS")) { PUSH32(esp, 0);   /* (testing; gives the hero's stats: @SetWeaponID, replayed, gives the weapon's) */ PUSH32(esp, 0x0008C850u); ecx = w; sub_0008C850(); }   /* its stats: weapon vtable slot 43 = 0x8C850 (health 10, damage values), as the loader gives the originals */
    if (!extra && coop_nwpn < 4) { coop_wpn_copy[coop_nwpn] = w; coop_wpn_orig[coop_nwpn] = child; coop_nwpn++; }
    if (coop_np2w < 16) { coop_p2w_slot[coop_np2w] = bone; coop_p2w[coop_np2w++] = w; }   /* (every weapon of his: the only ones he may take, sub_00085FB0) */
    tt_dbg("[COOPWEAPON] player 2's copy of %s (%08X): %08X, record %08X key %08X; esp %s\n", (const char *)XBOX_PTR(h), child, w, m, key ^ 0x20000u, esp == sp ? "balanced" : "NOT balanced");
    esp = sp; eax = ra; ecx = rc; edx = rd;
    return w;
}void coop_note_reckey(uint32_t rec, uint32_t key)   /* (a record seen again, in a later level: its new key) */
{
    int i; for (i = 0; i < coop_nreckey; i++) if (coop_reckey[i].rec == rec) { coop_reckey[i].key = key; return; }
    if (coop_nreckey < COOP_NRECKEY) { coop_reckey[coop_nreckey].rec = rec; coop_reckey[coop_nreckey].key = key; coop_nreckey++; }
    else { static int full; if (!full++) tt_dbg("[COOPWEAPON] the record key table is full (%d)\n", COOP_NRECKEY); }
}
uint32_t coop_build_weapon_ext(uint32_t child) { return coop_build_weapon(child); }
void coop_p2_extra_weapons_ext(uint32_t tmpl) { coop_p2_extra_weapons(tmpl); }
/* @IsPlayerWeapon (0xABD00): true for a weapon (+0x114 0x16 or 0x11) whose owner (+0x11C) is [0x43B15C].
 * The enemies' hit triggers ask it with player 1's globals, so player 2's weapons never counted: when the
 * object it looks up is owned by player 2, [0x43B15C] is player 2 for the rest of that one call.
 * TT_COOP_NOWEAPONHIT turns it off. */
static int coop_in_ispw; static uint32_t coop_ispw_saved; static unsigned coop_ispw_p2, coop_ispw_all;
extern void sub_000ABD00_gen(void);
void sub_000ABD00(void)
{
    coop_in_ispw = 1; coop_ispw_saved = 0; coop_ispw_all++;
    sub_000ABD00_gen();
    coop_in_ispw = 0;
    if (coop_ispw_saved) { MEM32(0x43B15Cu) = coop_ispw_saved; coop_ispw_saved = 0; }
}
unsigned coop_ispw_count(int w) { return w ? coop_ispw_p2 : coop_ispw_all; }
void tt_coop_ispw_lookup(uint32_t ra, uint32_t obj)
{
    if (!coop_in_ispw || ra != 0x000ABD3Cu || obj < 0x10000u || !coop_p2) return;
    if (MEM32(obj + 0x11C) == coop_p2 && !getenv("TT_COOP_NOWEAPONHIT")) {
        coop_ispw_saved = MEM32(0x43B15Cu); MEM32(0x43B15Cu) = coop_p2; coop_ispw_p2++;
        if (coop_ispw_p2 <= 10) tt_dbg("[COOPWEAPONHIT] @IsPlayerWeapon on player 2's %08X (type %X): counted as the player's\n", obj, MEM32(obj + 0x114));
    }
}
/* player 2's weapon copies get the level script's weapon set-up too: these commands, when they looked up one of
 * player 1's weapons that has a copy, run again with the lookup answering the copy (set-up phase only). */
extern void sub_000A4330_gen(void);
void sub_000A4330(void) { coop_replay(0x000A4330u, "Activate", sub_000A4330_gen); }
extern void sub_000B1DC0_gen(void);
void sub_000B1DC0(void) { coop_replay(0x000B1DC0u, "SetRenderable", sub_000B1DC0_gen); }
extern void sub_000A4390_gen(void);
void sub_000A4390(void) { coop_replay(0x000A4390u, "Deactivate", sub_000A4390_gen); }
extern void sub_000A67C0_gen(void);
void sub_000A67C0(void)
{
    int on = getenv("TT_COOP_WLOG") != NULL; uint32_t k;
    coop_wid_on = on; coop_wid_obj = 0;
    coop_replay(0x000A67C0u, "SetWeaponID", sub_000A67C0_gen);
    coop_p2_weapon_ids("@SetWeaponID");
    if (on && coop_wid_obj) {   /* (research) what @SetWeaponID wrote in the weapon */
        uint32_t o = coop_wid_obj, h = MEM32(o + 0x18), r = h >= 0x10008u ? h - 8 : 0;
        tt_dbg("[COOPWLOG] @SetWeaponID on '%s' (model %08X):", h >= 0x10000u ? (const char *)XBOX_PTR(h) : "?", r ? MEM32(r + 0x40) : 0);
        for (k = 0; k < 0x200; k++) if (MEM32(o + 4 * k) != coop_wid_snap[k]) tt_dbg(" +%03X %08X->%08X", 4 * k, coop_wid_snap[k], MEM32(o + 4 * k));
        tt_dbg("\n");
    }
    coop_wid_on = 0;
}
extern void sub_000A5D30_gen(void);
void sub_000A5D30(void) { coop_replay(0x000A5D30u, "ObjectCollisionOff", sub_000A5D30_gen); }
/* (research) @ForcePlayerWeaponObbTestOn/Off (0xAFE10/0xAFEA0): which weapon they put into / take out of the
 * player's weapon list 0x43B0E8, and in whose context */
static void coop_obb(const char *what, void (*gen)(void))
{
    static int n; uint32_t p = MEM32(0x43B15Cu), ra = MEM32(esp);
    coop_in_obb = 1; coop_obb_obj = 0;
    gen();
    coop_in_obb = 0;
    if (coop_mode() && n++ < 40)
        tt_dbg("[COOPOBB] %s weapon %08X (owner %08X%s) | player %08X%s | list %08X %08X %08X %08X %08X | from %08X\n", what, coop_obb_obj,
               coop_obb_obj >= 0x10000u ? MEM32(coop_obb_obj + 0x11C) : 0, coop_obb_obj >= 0x10000u && MEM32(coop_obb_obj + 0x11C) == coop_p2 ? " = P2" : "", p, p == coop_p2 ? " = P2" : "",
               MEM32(0x43B0E8u), MEM32(0x43B0ECu), MEM32(0x43B0F0u), MEM32(0x43B0F4u), MEM32(0x43B0F8u), ra);
}
extern void sub_000AFE10_gen(void); void sub_000AFE10(void) { coop_obb("ObbTestOn ", sub_000AFE10_gen); }
extern void sub_000AFEA0_gen(void); void sub_000AFEA0(void) { coop_obb("ObbTestOff", sub_000AFEA0_gen); }static unsigned coop_bm[2][64];
void coop_note_brainmsg(uint32_t t, uint32_t type) { (void)t; if (type < 64) coop_bm[coop_depth ? 1 : 0][type]++; }
void tt_coop_brainmsg_dump(void) { int i; for (i = 0; i < 64; i++) if (coop_bm[0][i] || coop_bm[1][i]) tt_dbg("[COOPBMSG] type %2d: from player 1's context %u, from player 2's %u\n", i, coop_bm[0][i], coop_bm[1][i]); memset(coop_bm, 0, sizeof coop_bm); }
/* ---- COOP-P2 end ---- */



/* ---- COOP-COUNT begin (generated by local/re/gen_count_hooks.py; research only) ---- */
int tt_coop_enter(uint32_t self, uint32_t fn); void tt_coop_leave(void); void tt_coop_p1_pre(uint32_t self, uint32_t fn); void tt_coop_p1_post(void);
#define COOP_NH 55
static const char *const coop_hname[COOP_NH] = {"000524E0 slot9", "00052520 slot13", "00052550 slot14", "00052570 slot15", "000525A0 slot19", "00061230 slot33/slot34", "00061240 slot38", "00061250 slot39", "00061260 slot40", "000685E0 camera 685E0 (called by slot 42)", "000695F0 camera 695F0 (called by slot 42)", "0006CAB0 6CAB0 (called by slot 42)", "0007FB90 slot3", "000801A0 slot20", "00080E50 slot37", "000813F0 slot50", "00081440 slot51", "000815F0 slot12", "00081C60 slot16", "000821F0 slot48", "00082670 slot41", "000826A0 slot49", "00082D60 slot52", "00082D70 slot0", "000840C0 840C0 (called by slot 42)", "00084220 slot42", "000843F0 slot1", "00085340 slot21", "00086120 slot17", "00086140 slot18", "00086490 slot4", "000865D0 slot10", "00086900 slot27", "00086980 slot5", "000869A0 slot7", "000869C0 slot6", "00086AF0 slot53", "00086B30 slot43", "0008CEF0 slot46", "0008CF20 slot47", "0008CF50 slot8", "0008D070 slot45", "00092300 slot22", "00092310 slot24", "00092320 slot23", "00092330 slot25", "00092360 slot26", "00097860 slot11", "0009D3B0 slot2/slot44/slot54", "000DD6A0 slot28", "000DE700 slot29", "000DE710 slot30", "000DEF00 slot31", "000DF6D0 slot32", "000E9100 slot35"};
static unsigned coop_hn[COOP_NH], coop_hp[COOP_NH], coop_hd[COOP_NH]; static uint32_t coop_hlast[COOP_NH];
static void coop_count(int i) { uint32_t t = ecx; coop_hn[i]++; if (t == MEM32(0x43B15Cu)) coop_hp[i]++; if (t != coop_hlast[i]) { coop_hd[i]++; coop_hlast[i] = t; } }
void tt_coopprobe_count_dump(unsigned frames)
{
    int i;
    tt_dbg("[COOPCNT] window: %u HUD frames\n", frames);
    for (i = 0; i < COOP_NH; i++) if (coop_hn[i]) tt_dbg("[COOPCNT]   %s n=%u pl=%u this-changes=%u\n", coop_hname[i], coop_hn[i], coop_hp[i], coop_hd[i]);
    memset(coop_hn, 0, sizeof coop_hn); memset(coop_hp, 0, sizeof coop_hp); memset(coop_hd, 0, sizeof coop_hd);
}
extern void sub_000524E0_gen(void); void sub_000524E0(void) { int sw = tt_coop_enter(ecx, 0x000524E0u); tt_coop_p1_pre(ecx, 0x000524E0u); if (coop_probe_on()) coop_count(0); sub_000524E0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00052520_gen(void); void sub_00052520(void) { int sw = tt_coop_enter(ecx, 0x00052520u); tt_coop_p1_pre(ecx, 0x00052520u); if (coop_probe_on()) coop_count(1); sub_00052520_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00052550_gen(void); void sub_00052550(void) { int sw = tt_coop_enter(ecx, 0x00052550u); tt_coop_p1_pre(ecx, 0x00052550u); if (coop_probe_on()) coop_count(2); sub_00052550_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00052570_gen(void); void sub_00052570(void) {
    if (getenv("TT_COOP_SHOWLOG")) { int k; for (k = 0; k < coop_np2w; k++) if (coop_p2w[k] == ecx) { static int n; if (n++ < 30) tt_dbg("[SHOWLOG] t %u %08X (slot %d) renderable %u, from %08X %08X %08X %08X, player %08X\n",   /* (research) who shows or hides player 2's weapons */
        coop_now(), ecx, coop_p2w_slot[k], MEM8(esp + 4), MEM32(esp), MEM32(esp + 0x10), MEM32(esp + 0x20), MEM32(esp + 0x30), MEM32(0x43B15Cu)); } }
    if (!MEM8(esp + 4) && MEM32(esp) == 0x000B1E39u && coop_twin >= 0x10000u && MEM32(0x43B15Cu) == coop_twin && coop_first_char >= 0x10000u
        && MEM32(coop_twin + 0x3F8) != MEM32(coop_first_char + 0x3F8) && !getenv("TT_COOP_NOP2SHOW")) {   /* player 2's own hero's moves hiding one of his hand
        * weapons (@SetRenderable in his event): they name player 1's list, and in player 1 Legolas' level player 2 Aragorn's moves
        * hid his sword, the copy of Legolas' knife (a bot run: from 0xB1E39, player the twin) -- not hidden (thiscall, ret 4) */
        int k, hand = 0; for (k = 0; k < coop_np2w; k++) if (coop_p2w[k] == ecx && coop_p2w_slot[k] >= 0 && coop_p2w_slot[k] <= 1) hand = 1;
        if (hand) { static int n; if (n++ < 4) tt_dbg("[COOPWEAPON] player 2's moves hide his %08X: kept drawn\n", ecx); esp += 8; return; } }
    int sw = tt_coop_enter(ecx, 0x00052570u); tt_coop_p1_pre(ecx, 0x00052570u); if (coop_probe_on()) coop_count(3); sub_00052570_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_000525A0_gen(void); void sub_000525A0(void) { int sw = tt_coop_enter(ecx, 0x000525A0u); tt_coop_p1_pre(ecx, 0x000525A0u); if (coop_probe_on()) coop_count(4); sub_000525A0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00061230_gen(void); void sub_00061230(void) { if (coop_probe_on()) coop_count(5); sub_00061230_gen(); }
extern void sub_00061240_gen(void); void sub_00061240(void) { if (coop_probe_on()) coop_count(6); sub_00061240_gen(); }
extern void sub_00061250_gen(void); void tt_coop_hp_set(uint32_t, uint32_t, uint32_t); void sub_00061250(void) { tt_coop_hp_set(ecx, MEM32(esp + 4), MEM32(esp)); if (coop_probe_on()) coop_count(7); sub_00061250_gen(); }
extern void sub_00061260_gen(void); void sub_00061260(void) { if (coop_probe_on()) coop_count(8); sub_00061260_gen(); }
extern void sub_000685E0_gen(void); void sub_000685E0(void) { int sw = tt_coop_enter(ecx, 0x000685E0u); tt_coop_p1_pre(ecx, 0x000685E0u); if (coop_probe_on()) coop_count(9); sub_000685E0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_000695F0_gen(void); void sub_000695F0(void) { int sw = tt_coop_enter(ecx, 0x000695F0u); tt_coop_p1_pre(ecx, 0x000695F0u); if (coop_probe_on()) coop_count(10); sub_000695F0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_0006CAB0_gen(void); void sub_0006CAB0(void) { int sw = tt_coop_enter(ecx, 0x0006CAB0u); tt_coop_p1_pre(ecx, 0x0006CAB0u); if (coop_probe_on()) coop_count(11); sub_0006CAB0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
/* (1.9.7) the credits (the main menu's Credits): their scroll (0x3F9A0, called from 0x710E1 with the frame's seconds,
 * which it never reads) moved its speed (+0x14: 2 units, 8 with a button held) every frame, right only at 30 frames a
 * second -- at 144 they went by almost five times too fast. The speed is scaled by the frame's time over a 30th of a
 * second. TT_CREDITS_PERFRAME: as the game had it */
extern void sub_0003F9A0_gen(void);
void sub_0003F9A0(void)
{
    uint32_t sc = ecx, w; float dt, k, sp0;
    if (MEM32(esp) != 0x000710E1u || sc < 0x10000u || getenv("TT_CREDITS_PERFRAME")) { sub_0003F9A0_gen(); return; }
    w = MEM32(0x4B8BA0u); memcpy(&dt, &w, 4);
    k = dt * 30.0f; if (!(k > 0.02f)) k = 0.02f; if (k > 4.0f) k = 4.0f;
    sp0 = MEMF(sc + 0x14); MEMF(sc + 0x14) = sp0 * k;
    sub_0003F9A0_gen();
    MEMF(sc + 0x14) = sp0;
}
/* (1.9.7, Versus) the scene's visibility test (0xF79C0: a box against the view's regions -- 0 out, 1 in, 2 partly) from
 * the draw walk (0x42AF0): in Versus every part of the level is in. The level shows its parts by the room the camera
 * is in, the rooms joined as the story's scripts open them; with the scripts left out, parts of the map the heroes
 * walked into were never shown -- the ground cut off at a brown nothing, the heroes in it unseen (Amon Hen, Gates of
 * Moria, Fangorn). TT_VS_CULL: the level's own */
extern void sub_000F79C0_gen(void);
void sub_000F79C0(void)
{
    uint32_t ra = MEM32(esp);
    if (getenv("TT_F79LOG")) { static uint32_t ras[16], c0[16], c1[16], tl; int i; uint32_t sp0 = esp; sub_000F79C0_gen(); (void)sp0;
        for (i = 0; i < 16 && ras[i] && ras[i] != ra; i++) ; if (i < 16) { ras[i] = ra; if (eax) c1[i]++; else c0[i]++; }
        if (coop_now() - tl > 3000u) { char b2[600]; int k = 0; tl = coop_now(); for (i = 0; i < 16 && ras[i]; i++) k += snprintf(b2 + k, sizeof b2 - k, " %06X:%u/%u", ras[i], c0[i], c1[i]); tt_dbg("[F79] out/in by caller:%s\n", b2); memset(c0, 0, sizeof c0); memset(c1, 0, sizeof c1); }
        return; }
    if ((ra == 0x00042D05u || ra == 0x00042C4Eu || ra == 0x00042C09u || ra == 0x000485FAu) && tt_vs_on() && !getenv("TT_VS_CULL")) {   /* (0x485FA: the ground's own pieces, 0x485D0) */
        sub_000F79C0_gen();
        if (eax == 0) eax = ra == 0x00042D05u ? 2u : 1u;
        return;
    }
    sub_000F79C0_gen();
}
/* (1.9.7 debug) the scene's draw walk (0x42AF0: a node of the level's tree, its objects list at +0x4C, the moving
 * objects' list [0x3C3474] handed down the tree): which nodes hold the heroes. TT_DRAWTREE */
extern void sub_00042AF0_gen(void);
void sub_00042AF0(void)
{
    if (getenv("TT_DRAWTREE2")) { static unsigned fv, fr; static uint32_t lroot;
        if (MEM32(esp) == 0x000430D9u) { if (fr++ % 10 == 0) { uint32_t cm = MEM32(0x43B164u); tt_dbg("[DT2] t %u frame visits %u root %08X arg2 %08X arg3 %08X | cam %08X eye %.1f %.1f %.1f\n", coop_now(), fv, MEM32(esp + 4), MEM32(esp + 8), MEM32(esp + 12), cm, MEMF(cm + 0x50), MEMF(cm + 0x54), MEMF(cm + 0x58)); } fv = 0; lroot = MEM32(esp + 4); }
        fv++; (void)lroot; }
    if (getenv("TT_DRAWTREE")) { static uint32_t tl; static unsigned n, s1, s2, d1, d2; uint32_t node = MEM32(esp + 4), it; int k = 0;
        n++;
        if (node >= 0x10000u) for (it = MEM32(node + 0x4C); it >= 0x10000u && k < 500; it = MEM32(it + 4), k++) { if (MEM32(it) == coop_first_char) s1++; if (MEM32(it) == coop_p2) s2++; }
        for (it = MEM32(0x3C3474u), k = 0; it >= 0x10000u && k < 500; it = MEM32(it), k++) { if (MEM32(it + 4) == coop_first_char) d1++; if (MEM32(it + 4) == coop_p2) d2++; }
        if (coop_now() - tl > 2000u) { tl = coop_now(); tt_dbg("[DRAWTREE] %u node visits: P1 in a node's list %u, P2 %u | in the moving list P1 %u P2 %u | P1 +118 %08X P2 +118 %08X player %08X +118 %08X | root %08X\n", n, s1, s2, d1, d2, MEM32(coop_first_char + 0x118), coop_p2 >= 0x10000u ? MEM32(coop_p2 + 0x118) : 0, MEM32(0x43B15Cu), MEM32(MEM32(0x43B15Cu) + 0x118), MEM32(MEM32(0x3C35A0u) + 0x44)); n = s1 = s2 = d1 = d2 = 0; } }
    if (getenv("TT_DRAWTREE") && MEM32(esp) == 0x000430D9u) { static uint32_t tl; if (coop_now() - tl > 3000u) { uint32_t st[512]; int sp = 0, nn = 0; tl = coop_now(); st[sp++] = MEM32(esp + 4);
        while (sp > 0 && nn < 5000) { uint32_t nd = st[--sp], it; int k, c; nn++; if (nd < 0x10000u) continue;
            for (it = MEM32(nd + 0x4C), k = 0; it >= 0x10000u && k < 500; it = MEM32(it + 4), k++) if (MEM32(it) == coop_first_char || MEM32(it) == coop_p2)
                tt_dbg("[DRAWTREE] P%d in node %08X box %.1f %.1f %.1f - %.1f %.1f %.1f (58 %d 5C %d 44 %d) | he is at %.1f %.1f %.1f\n", MEM32(it) == coop_p2 ? 2 : 1, nd, MEMF(nd), MEMF(nd + 4), MEMF(nd + 8), MEMF(nd + 0x10), MEMF(nd + 0x14), MEMF(nd + 0x18), (int)MEM32(nd + 0x58), (int)MEM32(nd + 0x5C), (int)MEM32(nd + 0x44), MEMF(MEM32(it) + 0xF0), MEMF(MEM32(it) + 0xF4), MEMF(MEM32(it) + 0xF8));
            c = (int)MEM32(nd + 0x44); for (k = 0; k < c && k < 8 && sp < 510; k++) st[sp++] = MEM32(nd + 0x20 + 4 * k); }
        tt_dbg("[DRAWTREE] root %08X: %d nodes\n", MEM32(esp + 4), nn); } }
    sub_00042AF0_gen();
}
extern void sub_0007FB90_gen(void); void sub_0007FB90(void) { if (getenv("TT_DRAWLOG")) { static unsigned n1, n2, nall; static uint32_t tl; nall++; if (ecx == coop_first_char) { n1++; if (n1 == 1) tt_dbg("[DRAWLOG] P1 drawn from %06X, %06X\n", MEM32(esp), MEM32(esp + 4)); } else if (ecx == coop_p2) n2++;
        if (coop_now() - tl > 2000u) { tl = coop_now(); tt_dbg("[DRAWLOG] 7FB90 calls: all %u P1 %u P2 %u\n", nall, n1, n2); nall = n1 = n2 = 0; } }
    int sw = tt_coop_enter(ecx, 0x0007FB90u); tt_coop_p1_pre(ecx, 0x0007FB90u); if (coop_probe_on()) coop_count(12); sub_0007FB90_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_000801A0_gen(void); void sub_000801A0(void) { int sw = tt_coop_enter(ecx, 0x000801A0u); tt_coop_p1_pre(ecx, 0x000801A0u); if (coop_probe_on()) coop_count(13); sub_000801A0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00080E50_gen(void); void sub_00080E50(void) { int sw = tt_coop_enter(ecx, 0x00080E50u); tt_coop_p1_pre(ecx, 0x00080E50u); if (coop_probe_on()) coop_count(14); sub_00080E50_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_000813F0_gen(void); void sub_000813F0(void) { int sw = tt_coop_enter(ecx, 0x000813F0u); tt_coop_p1_pre(ecx, 0x000813F0u); if (coop_probe_on()) coop_count(15); sub_000813F0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00081440_gen(void); void sub_00081440(void) { int sw = tt_coop_enter(ecx, 0x00081440u); tt_coop_p1_pre(ecx, 0x00081440u); if (coop_probe_on()) coop_count(16); sub_00081440_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_000815F0_gen(void); void sub_000815F0(void) { int sw = tt_coop_enter(ecx, 0x000815F0u); tt_coop_p1_pre(ecx, 0x000815F0u); if (coop_probe_on()) coop_count(17); sub_000815F0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00081C60_gen(void); void sub_00081C60(void) { int sw = tt_coop_enter(ecx, 0x00081C60u); tt_coop_p1_pre(ecx, 0x00081C60u); if (coop_probe_on()) coop_count(18); sub_00081C60_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_000821F0_gen(void); void sub_000821F0(void) { int sw = tt_coop_enter(ecx, 0x000821F0u); tt_coop_p1_pre(ecx, 0x000821F0u); if (coop_probe_on()) coop_count(19); sub_000821F0_log(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00082670_gen(void); void sub_00082670(void) { int sw = tt_coop_enter(ecx, 0x00082670u); tt_coop_p1_pre(ecx, 0x00082670u); if (coop_probe_on()) coop_count(20); sub_00082670_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_000826A0_gen(void); void sub_000826A0(void) { int sw = tt_coop_enter(ecx, 0x000826A0u); tt_coop_p1_pre(ecx, 0x000826A0u); if (coop_probe_on()) coop_count(21); sub_000826A0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00082D60_gen(void); void sub_00082D60(void) { int sw = tt_coop_enter(ecx, 0x00082D60u); tt_coop_p1_pre(ecx, 0x00082D60u); if (coop_probe_on()) coop_count(22); sub_00082D60_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00082D70_gen(void); void sub_00082D70(void) { int sw = tt_coop_enter(ecx, 0x00082D70u); tt_coop_p1_pre(ecx, 0x00082D70u); if (coop_probe_on()) coop_count(23); sub_00082D70_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_000840C0_gen(void); void sub_000840C0(void) { int sw = tt_coop_enter(ecx, 0x000840C0u); tt_coop_p1_pre(ecx, 0x000840C0u); if (coop_probe_on()) coop_count(24); sub_000840C0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
/* (1.9.7) Impossible: any enemy's second blow fells a hero. A blow on a hero (0x84220, the hit) from full: his health
 * after it no more than half his most (run with his health raised, so the game never sees the drop as his death);
 * a blow at half or less: run with his health at 1, so the game's own death follows (back as it was if the blow did
 * nothing: parried). Solo by Options > Gameplay Options' Impossible (or the New Game's difficulty screen); co-op by
 * its Difficulty (Impossible), there on top of the co-op's own guard (tt_coop_p1_post) */
int tt_impossible(void)
{
    extern int tt_coop_setting(int i);
    if (tt_vs_on()) return 0;
    if (getenv("TT_IMPOSSIBLE_FORCE")) return 1;
    if (coop_mode() && !getenv("TT_COOP_SOLODIFF")) return tt_coop_setting(0) == 3;   /* co-op: its own Difficulty only -- the solo game's Impossible
        * (General Options, the New Game's screen) turned it on in co-op too (a playtest asked them apart) */
    return tt_coop_setting(6) == 1 || (coop_mode() && tt_coop_setting(0) == 3);
}
/* the game's difficulty (0x4AB69C: 0 Easy, 1 Normal, 2 Expert; set by 0xD2380, asked by 0xD2390, and by 0xD23A0 / 0xD23B0
 * through their tables at 0x1BB2B8 / 0x1BB2C4) is the solo game's: the New Game's screen, General Options, the save. The
 * game's play asks it only through the tables, from 0x84220 (a hero's damage); the byte getter only from the menus and
 * 0xD0C30. In co-op the tables are asked with the co-op Difficulty (Impossible: Expert, the co-op's rule on top) and the
 * solo value stays as it was, shown and set by the menus (a General Options page opened from the pause showed Easy, the
 * co-op's, when the byte getter answered it too: a bot run). TT_COOP_SOLODIFF: as before */
static int coop_diff_swap(void)
{
    extern int tt_opt_up(void); int d = tt_coop_setting(0), fe = (int)MEM32(0x4AD050u);
    if (!coop_mode() || tt_vs_on() || getenv("TT_COOP_SOLODIFF") || fe == 4 || tt_opt_up()) return -1;   /* (4: the main menu; the Options pages, the pause's too) */
    { int solo = MEM8(0x4AB69Cu); static unsigned lv = ~0u; MEM8(0x4AB69Cu) = (uint8_t)(d < 0 ? 1 : d > 2 ? 2 : d);
      if (lv != coop_level_n) { lv = coop_level_n; tt_dbg("[COOPDIFF] the game asks its difficulty: the co-op's %d (the solo game's %d kept)\n", d, solo); }
      return solo; }
}
extern void sub_000D23A0_gen(void); void sub_000D23A0(void) { int s = coop_diff_swap(); sub_000D23A0_gen(); if (s >= 0) MEM8(0x4AB69Cu) = (uint8_t)s; }
extern void sub_000D23B0_gen(void); void sub_000D23B0(void) { int s = coop_diff_swap(); sub_000D23B0_gen(); if (s >= 0) MEM8(0x4AB69Cu) = (uint8_t)s; }
static int imp_rule(int32_t h0, int32_t mx, int32_t dmg) { int32_t h = h0 - dmg; if (dmg <= 0) return h0; if (h0 > mx / 2 + 1) return h < mx / 2 ? (h < 1 ? 1 : h) : mx / 2; return 0; }
extern void sub_00084220_gen(void); void sub_00084220(void)
{
    uint32_t self = ecx, p1 = MEM32(0x43B15Cu); int mode = 0; int32_t h0 = 0, mx = 0;
    int sw = tt_coop_enter(ecx, 0x00084220u);
    if (self >= 0x10000u && tt_impossible() && (self == p1 || (coop_p2 && self == coop_p2))) {
        extern int coop_hero_down(int);
        int guarded = coop_mode() == 3 && coop_p2 && (self == coop_p2 || (self == p1 && !coop_hero_down(1)));   /* (the co-op's guard takes it: imp_rule there) */
        h0 = (int32_t)MEM32(self + 0x2FC); mx = (int32_t)MEM32(self + 0x300);
        if (!guarded && h0 > 0 && mx > 0) {
            if (h0 > mx / 2 + 1) { mode = 1; MEM32(self + 0x2FC) = (uint32_t)(h0 + 100000); }
            else { mode = 2; MEM32(self + 0x2FC) = 1; }
        }
    }
    int32_t hb = self >= 0x10000u ? (int32_t)MEM32(self + 0x2FC) : 0;   /* (1.9.7) for the More page's damage percentages */
    uint32_t gra = MEM32(esp), garg[4] = { MEM32(esp + 4), MEM32(esp + 8), MEM32(esp + 12), MEM32(esp + 16) };
    tt_coop_p1_pre(ecx, 0x00084220u); if (coop_probe_on()) coop_count(25); sub_00084220_gen(); tt_coop_p1_post();
    if (coop_mode() == 3 && coop_p2 >= 0x10000u && self >= 0x10000u && MEM32(self + 0x114) == 3 && MEM32(self + 0x4BC) == coop_p2 && !MEM8(self + 0x526) && !tt_vs_on() && !getenv("TT_COOP_NOP2REWARD")) {
        /* an enemy hurt by player 2: its "hurt by the player" flag (+0x526), which the kill award at its death needs (0x856D9: +0x526 set,
         * +0x538 the award's own "given" mark still 0). 0x84220 sets it only when the last attacker (+0x4BC) is [0x43B15C], player 1 outside
         * player 2's context -- his arrows are applied outside it: an enemy only player 2 hurt died without an award (a playtest: no reward
         * after 3 arrow kills) */
        MEM8(self + 0x526) = 1;
    }
    {   static int gl = -1; if (gl < 0) gl = getenv("TT_GATELOG") != NULL;
        if (gl && self >= 0x10000u && self == MEM32(0x4ACBACu)) tt_dbg("[GATEHIT] t %u ra %08X args %08X %08X %08X %08X: %d -> %d\n", tt_game_ms, gra, garg[0], garg[1], garg[2], garg[3], hb, (int32_t)MEM32(self + 0x2FC)); }
    if (mode == 0 && self >= 0x10000u && hb > 0 && !tt_vs_on()) {   /* (1.9.7) Damage taken / Enemy damage (F10, More): the blow's damage scaled */
        extern float xbox_GfxGetF(int id); extern int xbox_GfxFind(const char *key);
        static int it_t = -2, it_e = -2;
        int hero = self == p1 || (coop_p2 && self == coop_p2), pc;
        int32_t now = (int32_t)MEM32(self + 0x2FC), dmg = hb - now;
        if (it_t == -2) { it_t = xbox_GfxFind("damage_taken"); it_e = xbox_GfxFind("enemy_damage"); }
        pc = (int)xbox_GfxGetF(hero ? it_t : it_e);
        if (pc > 0 && pc != 100 && dmg > 0 && now > 0) {
            int32_t h = hb - (int32_t)((int64_t)dmg * pc / 100);
            if (h < 1) h = 1;
            MEM32(self + 0x2FC) = (uint32_t)h;
            { static int n; if (n++ < 10) tt_dbg("[DAMAGE] %s: a blow of %d at %d%%: health %d -> %d\n", hero ? "a hero" : "an enemy", dmg, pc, hb, h); }
        }
    }
    if (mode == 1) { int32_t now = (int32_t)MEM32(self + 0x2FC), dmg = h0 + 100000 - now; MEM32(self + 0x2FC) = (uint32_t)imp_rule(h0, mx, dmg);
        { static int n; if (n++ < 20 && dmg > 0) tt_dbg("[IMPOSSIBLE] a blow of %d: health %d -> %d of %d\n", dmg, h0, (int32_t)MEM32(self + 0x2FC), mx); } }
    else if (mode == 2) { int32_t now = (int32_t)MEM32(self + 0x2FC); if (now == 1) MEM32(self + 0x2FC) = (uint32_t)h0;   /* (nothing done: as it was) */
        { static int n; if (n++ < 20) tt_dbg("[IMPOSSIBLE] the second blow: health %d -> %d\n", h0, now); } }
    if (sw) tt_coop_leave();
}
extern void sub_000843F0_gen(void); void sub_000843F0(void) { int sw = tt_coop_enter(ecx, 0x000843F0u); tt_coop_p1_pre(ecx, 0x000843F0u); if (coop_probe_on()) coop_count(26); sub_000843F0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00085340_gen(void); void sub_00085340(void) { int sw = tt_coop_enter(ecx, 0x00085340u); tt_coop_p1_pre(ecx, 0x00085340u); if (coop_probe_on()) coop_count(27); sub_00085340_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00086120_gen(void); void sub_00086120(void) { int sw = tt_coop_enter(ecx, 0x00086120u); tt_coop_p1_pre(ecx, 0x00086120u); if (coop_probe_on()) coop_count(28); sub_00086120_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00086140_gen(void); void sub_00086140(void) { int sw = tt_coop_enter(ecx, 0x00086140u); tt_coop_p1_pre(ecx, 0x00086140u); if (coop_probe_on()) coop_count(29); sub_00086140_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00086490_gen(void); void sub_00086490(void) { int sw = tt_coop_enter(ecx, 0x00086490u); tt_coop_p1_pre(ecx, 0x00086490u); if (coop_probe_on()) coop_count(30); sub_00086490_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_000865D0_gen(void); void sub_000865D0(void) { int sw = tt_coop_enter(ecx, 0x000865D0u); tt_coop_p1_pre(ecx, 0x000865D0u); if (coop_probe_on()) coop_count(31); sub_000865D0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00086900_gen(void); void sub_00086900(void) { int sw = tt_coop_enter(ecx, 0x00086900u); tt_coop_p1_pre(ecx, 0x00086900u); if (coop_probe_on()) coop_count(32); sub_00086900_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00086980_gen(void); void sub_00086980(void) { int sw = tt_coop_enter(ecx, 0x00086980u); tt_coop_p1_pre(ecx, 0x00086980u); if (coop_probe_on()) coop_count(33); sub_00086980_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_000869A0_gen(void); void sub_000869A0(void) { int sw = tt_coop_enter(ecx, 0x000869A0u); tt_coop_p1_pre(ecx, 0x000869A0u); if (coop_probe_on()) coop_count(34); sub_000869A0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_000869C0_gen(void); void sub_000869C0(void) { int sw = tt_coop_enter(ecx, 0x000869C0u); tt_coop_p1_pre(ecx, 0x000869C0u); if (coop_probe_on()) coop_count(35); sub_000869C0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00086AF0_gen(void); void sub_00086AF0(void) { int sw = tt_coop_enter(ecx, 0x00086AF0u); tt_coop_p1_pre(ecx, 0x00086AF0u); if (coop_probe_on()) coop_count(36); sub_00086AF0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00086B30_gen(void); void sub_00086B30(void) { int sw = tt_coop_enter(ecx, 0x00086B30u); tt_coop_p1_pre(ecx, 0x00086B30u); if (coop_probe_on()) coop_count(37); sub_00086B30_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_0008CEF0_gen(void); void sub_0008CEF0(void) { int sw = tt_coop_enter(ecx, 0x0008CEF0u); tt_coop_p1_pre(ecx, 0x0008CEF0u); if (coop_probe_on()) coop_count(38); sub_0008CEF0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_0008CF20_gen(void); void sub_0008CF20(void) { int sw = tt_coop_enter(ecx, 0x0008CF20u); tt_coop_p1_pre(ecx, 0x0008CF20u); if (coop_probe_on()) coop_count(39); sub_0008CF20_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_0008CF50_gen(void); void sub_0008CF50(void) { int sw = tt_coop_enter(ecx, 0x0008CF50u); tt_coop_p1_pre(ecx, 0x0008CF50u); if (coop_probe_on()) coop_count(40); sub_0008CF50_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_0008D070_gen(void); void sub_0008D070(void) { int sw = tt_coop_enter(ecx, 0x0008D070u); tt_coop_p1_pre(ecx, 0x0008D070u); if (coop_probe_on()) coop_count(41); sub_0008D070_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00092300_gen(void); void sub_00092300(void) { int sw = tt_coop_enter(ecx, 0x00092300u); tt_coop_p1_pre(ecx, 0x00092300u); if (coop_probe_on()) coop_count(42); sub_00092300_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00092310_gen(void); void sub_00092310(void) { int sw = tt_coop_enter(ecx, 0x00092310u); tt_coop_p1_pre(ecx, 0x00092310u); if (coop_probe_on()) coop_count(43); sub_00092310_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00092320_gen(void); void sub_00092320(void) { int sw = tt_coop_enter(ecx, 0x00092320u); tt_coop_p1_pre(ecx, 0x00092320u); if (coop_probe_on()) coop_count(44); sub_00092320_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00092330_gen(void); void sub_00092330(void) { int sw = tt_coop_enter(ecx, 0x00092330u); tt_coop_p1_pre(ecx, 0x00092330u); if (coop_probe_on()) coop_count(45); sub_00092330_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00092360_gen(void); void sub_00092360(void) { int sw = tt_coop_enter(ecx, 0x00092360u); tt_coop_p1_pre(ecx, 0x00092360u); if (coop_probe_on()) coop_count(46); sub_00092360_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_00097860_gen(void); void sub_00097860(void) { int sw = tt_coop_enter(ecx, 0x00097860u); tt_coop_p1_pre(ecx, 0x00097860u); if (coop_probe_on()) coop_count(47); sub_00097860_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_0009D3B0_gen(void); void sub_0009D3B0(void) { if (coop_probe_on()) coop_count(48); sub_0009D3B0_gen(); }
extern void sub_000DD6A0_gen(void); void sub_000DD6A0(void) { int sw = tt_coop_enter(ecx, 0x000DD6A0u); tt_coop_p1_pre(ecx, 0x000DD6A0u); if (coop_probe_on()) coop_count(49); sub_000DD6A0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_000DE700_gen(void); void sub_000DE700(void) { int sw = tt_coop_enter(ecx, 0x000DE700u); tt_coop_p1_pre(ecx, 0x000DE700u); if (coop_probe_on()) coop_count(50); sub_000DE700_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_000DE710_gen(void); void sub_000DE710(void) { int sw = tt_coop_enter(ecx, 0x000DE710u); tt_coop_p1_pre(ecx, 0x000DE710u); if (coop_probe_on()) coop_count(51); sub_000DE710_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_000DEF00_gen(void); void sub_000DEF00(void) { int sw = tt_coop_enter(ecx, 0x000DEF00u); tt_coop_p1_pre(ecx, 0x000DEF00u); if (coop_probe_on()) coop_count(52); sub_000DEF00_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_000DF6D0_gen(void); void sub_000DF6D0(void) { int sw = tt_coop_enter(ecx, 0x000DF6D0u); tt_coop_p1_pre(ecx, 0x000DF6D0u); if (coop_probe_on()) coop_count(53); sub_000DF6D0_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
extern void sub_000E9100_gen(void); void sub_000E9100(void) { int sw = tt_coop_enter(ecx, 0x000E9100u); tt_coop_p1_pre(ecx, 0x000E9100u); if (coop_probe_on()) coop_count(54); sub_000E9100_gen(); tt_coop_p1_post(); if (sw) tt_coop_leave(); }
/* ---- COOP-COUNT end ---- */


/* ---- COOP-WEAPON begin (generated by local/re/gen_weapon_hooks.py) ---- */
int tt_coop_enter_weapon2(uint32_t self, uint32_t fn); void tt_coop_leave(void); void tt_coop_weapon_post(uint32_t fn);
extern void sub_000524D0_gen(void); void sub_000524D0(void) { int sw = tt_coop_enter_weapon2(ecx, 0x000524D0u); sub_000524D0_gen(); tt_coop_weapon_post(0x000524D0u); if (sw) tt_coop_leave(); }
extern void sub_00061270_gen(void); void sub_00061270(void) { int sw = tt_coop_enter_weapon2(ecx, 0x00061270u); sub_00061270_gen(); tt_coop_weapon_post(0x00061270u); if (sw) tt_coop_leave(); }
extern void sub_0008CB30_gen(void); void sub_0008CB30(void) { int sw = tt_coop_enter_weapon2(ecx, 0x0008CB30u); sub_0008CB30_gen(); tt_coop_weapon_post(0x0008CB30u); if (sw) tt_coop_leave(); }
extern void sub_0008FF50_gen(void); void sub_0008FF50(void) { int sw = tt_coop_enter_weapon2(ecx, 0x0008FF50u); sub_0008FF50_gen(); tt_coop_weapon_post(0x0008FF50u); if (sw) tt_coop_leave(); }
extern void sub_0008FF80_gen(void); void sub_0008FF80(void) { int sw = tt_coop_enter_weapon2(ecx, 0x0008FF80u); sub_0008FF80_gen(); tt_coop_weapon_post(0x0008FF80u); if (sw) tt_coop_leave(); }
extern void sub_0008FFA0_gen(void); void sub_0008FFA0(void) { int sw = tt_coop_enter_weapon2(ecx, 0x0008FFA0u); sub_0008FFA0_gen(); tt_coop_weapon_post(0x0008FFA0u); if (sw) tt_coop_leave(); }
extern void sub_0008FFC0_gen(void); void sub_0008FFC0(void) { int sw = tt_coop_enter_weapon2(ecx, 0x0008FFC0u); sub_0008FFC0_gen(); tt_coop_weapon_post(0x0008FFC0u); if (sw) tt_coop_leave(); }
extern void sub_0008FFD0_gen(void); void sub_0008FFD0(void) { int sw = tt_coop_enter_weapon2(ecx, 0x0008FFD0u); sub_0008FFD0_gen(); tt_coop_weapon_post(0x0008FFD0u); if (sw) tt_coop_leave(); }
extern void sub_0008FFE0_gen(void); void sub_0008FFE0(void) { int sw = tt_coop_enter_weapon2(ecx, 0x0008FFE0u); sub_0008FFE0_gen(); tt_coop_weapon_post(0x0008FFE0u); if (sw) tt_coop_leave(); }
extern void sub_00090240_gen(void); void sub_00090240(void) { int sw = tt_coop_enter_weapon2(ecx, 0x00090240u); sub_00090240_gen(); tt_coop_weapon_post(0x00090240u); if (sw) tt_coop_leave(); }
extern void sub_000905D0_gen(void); void sub_000905D0(void) { int sw = tt_coop_enter_weapon2(ecx, 0x000905D0u); sub_000905D0_gen(); tt_coop_weapon_post(0x000905D0u); if (sw) tt_coop_leave(); }
extern void sub_000909E0_gen(void); void sub_000909E0(void) { int sw = tt_coop_enter_weapon2(ecx, 0x000909E0u); sub_000909E0_gen(); tt_coop_weapon_post(0x000909E0u); if (sw) tt_coop_leave(); }
extern void sub_00090B10_gen(void); void sub_00090B10(void) { int sw = tt_coop_enter_weapon2(ecx, 0x00090B10u); sub_00090B10_gen(); tt_coop_weapon_post(0x00090B10u); if (sw) tt_coop_leave(); }
extern void sub_00090B70_gen(void); void sub_00090B70(void) { int sw = tt_coop_enter_weapon2(ecx, 0x00090B70u); sub_00090B70_gen(); tt_coop_weapon_post(0x00090B70u); if (sw) tt_coop_leave(); }
extern void sub_00090B90_gen(void); void sub_00090B90(void) { int sw = tt_coop_enter_weapon2(ecx, 0x00090B90u); sub_00090B90_gen(); tt_coop_weapon_post(0x00090B90u); if (sw) tt_coop_leave(); }
extern void sub_00090BB0_gen(void); void sub_00090BB0(void) { int sw = tt_coop_enter_weapon2(ecx, 0x00090BB0u); sub_00090BB0_gen(); tt_coop_weapon_post(0x00090BB0u); if (sw) tt_coop_leave(); }
extern void sub_00090BC0_gen(void); void sub_00090BC0(void) { int sw = tt_coop_enter_weapon2(ecx, 0x00090BC0u); sub_00090BC0_gen(); tt_coop_weapon_post(0x00090BC0u); if (sw) tt_coop_leave(); }
extern void sub_00090C00_gen(void); void sub_00090C00(void) { int sw = tt_coop_enter_weapon2(ecx, 0x00090C00u); sub_00090C00_gen(); tt_coop_weapon_post(0x00090C00u); if (sw) tt_coop_leave(); }
extern void sub_00090C10_gen(void); void sub_00090C10(void) { int sw = tt_coop_enter_weapon2(ecx, 0x00090C10u); sub_00090C10_gen(); tt_coop_weapon_post(0x00090C10u); if (sw) tt_coop_leave(); }
extern void sub_00090EB0_gen(void); void sub_00090EB0(void) { int sw = tt_coop_enter_weapon2(ecx, 0x00090EB0u); sub_00090EB0_gen(); tt_coop_weapon_post(0x00090EB0u); if (sw) tt_coop_leave(); }
extern void sub_00091200_gen(void); void sub_00091200(void) { int sw = tt_coop_enter_weapon2(ecx, 0x00091200u); sub_00091200_gen(); tt_coop_weapon_post(0x00091200u); if (sw) tt_coop_leave(); }
extern void sub_00091AC0_gen(void); void sub_00091AC0(void) { int sw = tt_coop_enter_weapon2(ecx, 0x00091AC0u); sub_00091AC0_gen(); tt_coop_weapon_post(0x00091AC0u); if (sw) tt_coop_leave(); }
extern void sub_00091BA0_gen(void); void sub_00091BA0(void) { int sw = tt_coop_enter_weapon2(ecx, 0x00091BA0u); sub_00091BA0_gen(); tt_coop_weapon_post(0x00091BA0u); if (sw) tt_coop_leave(); }
extern void sub_00091FD0_gen(void); void sub_00091FD0(void) { int sw = tt_coop_enter_weapon2(ecx, 0x00091FD0u); sub_00091FD0_gen(); tt_coop_weapon_post(0x00091FD0u); if (sw) tt_coop_leave(); }
/* (1.9.7) Fire and blasts at high frame rates: a character's or a gate's update (0x843F0, 0x90240) asks 0x87B80 how much
 * fire and blast damage reaches it and takes that much -- every frame, while the damage it reports fades with time. At 30
 * fps that is the game's damage; at 300 fps ten times as much: on the Breached Wall the catapults' blasts broke the gate
 * long before it could be held (60000 gone in under a minute at 500 fps, about a tenth of it at 30, in tests). The damage
 * is now taken at most once per 1/30 s of game time for each one hit -- what 30 fps gave. TT_FIRE_PERFRAME: as the game */
static int fire_due(uint32_t self)
{
    static struct { uint32_t self, frame; float acc; } t[64]; static int nt;
    static int off = -1; float ms; uint32_t w = MEM32(0x4B8B98u); int i;
    if (off < 0) off = getenv("TT_FIRE_PERFRAME") != NULL;
    if (off) return 1;
    memcpy(&ms, &w, 4);
    if (!(ms > 0.0f) || ms >= 33.3f) return 1;
    for (i = 0; i < nt && t[i].self != self; i++) {}
    if (i == nt) { if (nt < 64) nt++; else { i = 0; memmove(&t[0], &t[1], sizeof t[0] * 63); i = 63; } t[i].self = self; t[i].frame = tt_game_ms - 1u; t[i].acc = 33.34f; }
    if (t[i].frame != tt_game_ms) { t[i].frame = tt_game_ms; t[i].acc += ms; if (t[i].acc > 66.7f) t[i].acc = 66.7f; }
    if (t[i].acc < 33.333f) return 0;
    t[i].acc -= 33.333f; return 1;
}
extern void sub_000832F0_gen(void);
extern void sub_000834B0_gen(void);   /* (on a line of its own: the generator only sees a wrapper's extern at a line's start) */
void sub_000832F0(void) { if (MEM32(esp) == 0x0008490Du && !fire_due(ecx)) { esp += 4 + 16; eax = 0; return; } sub_000832F0_gen(); }
void sub_000834B0(void) { if (MEM32(esp) == 0x000848EEu && !fire_due(ecx)) { esp += 4 + 20; eax = 0; return; } sub_000834B0_gen(); }
extern void sub_00092000_gen(void); void sub_00092000(void)
{
    if (MEM32(esp) == 0x00090503u && MEM32(esp + 8) == 0x10u && !fire_due(ecx)) { esp += 4 + 8; eax = 0; return; }
    uint32_t self = ecx, gra = MEM32(esp), g1 = MEM32(esp + 4), g2 = MEM32(esp + 8), g3 = MEM32(esp + 12); int32_t hb = self >= 0x10000u ? (int32_t)MEM32(self + 0x2FC) : 0;
    int sw = tt_coop_enter_weapon2(ecx, 0x00092000u); sub_00092000_gen(); tt_coop_weapon_post(0x00092000u); if (sw) tt_coop_leave();
    {   static int gl = -1; if (gl < 0) gl = getenv("TT_GATELOG") != NULL;
        if (gl && self >= 0x10000u && self == MEM32(0x4ACBACu) && hb != (int32_t)MEM32(self + 0x2FC)) tt_dbg("[GATEHIT2] t %u ra %08X args %08X %08X %08X: %d -> %d\n", tt_game_ms, gra, g1, g2, g3, hb, (int32_t)MEM32(self + 0x2FC)); }
}
/* a character takes a weapon (0x00085FB0, thiscall character, weapon, a second argument; ret 8: 0x97920 then the weapon's +0x40 =
 * 0x97A10, which makes it the weapon's owner +0x11C). Player 2's character, built from player 1's record, asked for
 * player 1's own weapon objects (Legolas' blades and bow, weaponObj132-136, by their ids): aiming with LT, Gimli held
 * Legolas' bow and each throw turned Legolas' sword and bow. Player 2 takes only weapons built for him
 * (coop_p2w); the rest is left. TT_COOP_P2TAKEALL: as before. */
extern void sub_00085FB0_gen(void);
static int coop_take_nested;   /* (player 2 Legolas' extra takes, below) */
/* player 2 Legolas with his own moves (coop_own_machines): the draw and put-away below are his */
static int coop_p2_draw_on(void)
{
    extern const char *tt_coop_p2_char_now;
    return coop_p2 >= 0x10000u && coop_first_char >= 0x10000u && MEM32(coop_p2 + 0x3F8) != MEM32(coop_first_char + 0x3F8)
        && tt_coop_p2_char_now && !strcmp(tt_coop_p2_char_now, "legolas") && !getenv("TT_COOP_NOP2SLOTPICK") && !getenv("TT_COOP_NOP2DRAW");
}
/* each weapon's place on its bone: a matrix at +0x20..+0x5C, set by @SetRenderOffset (0xB2320) after each attach. Player 1
 * Legolas' (TT_COOP_WPNCMDLOG at the Gates of Moria): the bow put away (0xA) the first below, in his hand the identity; his
 * knives put away the second (weaponObj133) and third (134), in his hands the identity. Player 2's moves name the bow with
 * nothing (no lookup) and the knives as player 1 Aragorn's dagger and bow, which lead to his bow copy: his put-away bow
 * kept the identity and lay across his shoulders, and his drawn bow got a knife's (bot runs). So his @SetRenderOffset calls
 * are skipped (sub_000B2320) and each of his takes sets the weapon's here. TT_COOP_NOP2OFFSET: as before */
static const float coop_off_bow_away[16] = { 0.000f, -0.000f, -1.000f, 0, -0.996f, -0.087f, 0.000f, 0, -0.087f, 0.996f, -0.000f, 0, 0.800f, 0.200f, 0.000f, 1 };
static const float coop_off_knife_away[2][16] = { { 0.122f, 0.993f, 0.000f, 0, 0.993f, -0.122f, -0.000f, 0, -0.000f, 0.000f, -1.000f, 0, -0.600f, -0.350f, 0.450f, 1 },
                                                  { -0.000f, 1.000f, 0.000f, 0, 1.000f, 0.000f, -0.000f, 0, -0.000f, -0.000f, -1.000f, 0, -0.600f, -0.450f, 0.250f, 1 } };
static const float coop_off_none[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
static void coop_p2_render_offset(uint32_t w, uint32_t sl)
{
    extern uint32_t coop_p2_bow(void); const float *m = coop_off_none; int k, q;
    if (getenv("TT_COOP_NOP2OFFSET") || w < 0x10000u || MEM32(w) != 0x174980u) return;
    if (sl == 0xAu) {
        if (w == coop_p2_bow()) m = coop_off_bow_away;
        else for (k = 0; k < coop_np2w; k++) if (coop_p2w[k] == w && coop_p2w_slot[k] >= 0 && coop_p2w_slot[k] <= 1) m = coop_off_knife_away[coop_p2w_slot[k]];
    }
    for (q = 0; q < 16; q++) MEMF(w + 0x20 + 4 * q) = m[q];
}
int coop_p2_skip_offset(void) { return !getenv("TT_COOP_NOP2OFFSET") && coop_p2_draw_on() && MEM32(0x43B15Cu) == coop_p2; }
void sub_00085FB0(void)
{
    uint32_t w = MEM32(esp + 4); int nested = coop_take_nested, after_bow_back = 0;
    if (getenv("TT_COOP_TAKELOG") && w >= 0x10000u && MEM32(w) == 0x174980u && (ecx == coop_p2 || ecx == coop_first_char)) {   /* (research) every take: who, what, its model and id */
        static int n; uint32_t h = MEM32(w + 0x18), m = MEM32(w + 0x494), rec = h >= 0x10008u ? h - 8 : 0;
        if (n++ < 60) tt_dbg("[TAKE] %s takes %08X '%s' arg2 %08X | model res %08X key %08X | record model %08X texture %08X | id +2F4 %X\n",
            ecx == coop_p2 ? "P2" : ecx == coop_first_char ? "P1" : "other", w, h >= 0x10000u && h < 0x84000000u ? (const char *)XBOX_PTR(h) : "?", MEM32(esp + 8),
            m, m >= 0x10000u && m < 0x84000000u ? MEM32(m + 4) : 0, rec ? MEM32(rec + 0x40) : 0, rec ? MEM32(rec + 0x50) : 0, MEM32(w + 0x2F4));
    }
    {   /* the bow copy the twin's set-up attaches (@AttachToBone replayed: coop_in_attach) is his bow: only a weapon attached
         * that way is drawn right in his hand (frames at the Gates of Moria: that copy upright in player 2 Legolas' bow hand
         * as player 1 Legolas', the other copies sideways or not at all -- a playtest: he shot with his blade) */
        uint32_t hh = w >= 0x10000u ? MEM32(w + 0x18) : 0, mm = hh >= 0x10008u && hh < 0x84000000u ? MEM32(hh - 8 + 0x40) : 0;
        if (coop_in_attach && coop_twin >= 0x10000u && ecx == coop_twin && MEM32(w) == 0x174980u && (mm == 0x0834F2D4u || mm == 0x36AC3F58u || mm == 0xE7EEC090u)) {
            extern void coop_p2_bow_attached(uint32_t w); coop_p2_bow_attached(w); }
    }
    {   extern int coop_rng_n_x(void); extern uint32_t coop_rng_copy_x(int i);
        /* player 2 with his own hero's animation machine (coop_own_machines) takes his weapons by the slot: that hero's
         * moves attach "weapon n into slot n" from the actor's list, and the twin's list is player 1's: player 2 Legolas
         * putting his bow away took player 1 Aragorn's dagger and bow (answered with his bow copies) into slots 0 and 1, and
         * slot 2 -- where the bow sits, 0xA being slot 2 with the hand bit 8 -- was never filled again: the bow stayed in his
         * hand (a playtest). So slot 0-3: his own melee weapon built for that slot (coop_p2w_slot); 8 and up (the bow hand):
         * his ranged copy (the one-bow rule below picks which). TT_COOP_NOP2SLOTPICK: as before */
        uint32_t sl = MEM32(esp + 8);
        static int stow_n;
        if (coop_p2 >= 0x10000u && ecx == coop_p2 && coop_first_char >= 0x10000u && MEM32(coop_p2 + 0x3F8) != MEM32(coop_first_char + 0x3F8)
            && w >= 0x10000u && MEM32(w) == 0x174980u && !getenv("TT_COOP_NOP2SLOTPICK") && !nested) {
            extern const char *tt_coop_p2_char_now; extern uint32_t coop_p2_bow(void); extern uint32_t coop_p2_arrow(void); extern uint32_t coop_p2_arrow_for(uint32_t w);
            int k; uint32_t pick = 0, wh = MEM32(w + 0x18), wm = wh >= 0x10008u && wh < 0x84000000u ? MEM32(wh - 8 + 0x40) : 0;
            int wr = wm == 0x0834F2D4u || wm == 0x36AC3F58u || wm == 0xE7EEC090u;
            if (tt_coop_p2_char_now && !strcmp(tt_coop_p2_char_now, "legolas") && !getenv("TT_COOP_NOP2DRAW")) {
                /* player 2 Legolas, as player 1 Legolas (his takes at the Gates of Moria): the draw puts the knives into 0xA, the bow
                 * into slot 1 and the arrow into slot 0; putting the bow away, the bow into 0xA and the knives into 0 and 1. His moves
                 * name player 1's weapons by player 1's list (Aragorn's dagger and bow, for the knives) and their "bow into slot 1"
                 * and "bow into 0xA" attach nothing for him (no lookup: the command's argument check fails), so the knives stayed in
                 * his hands and the bow on his back (a playtest). Here: the first 0xA take of a draw his slot-0 knife, the second his
                 * slot-1 knife; the arrow (his copy, coop_p2_arrow) with the bow into slot 1 first; his knife into slot 1, then the bow
                 * into 0xA. TT_COOP_NOP2DRAW: as before */
                uint32_t bow = coop_p2_bow();
                if (sl == 0xAu) { int want = stow_n++ & 1;
                    for (k = 0; k < coop_np2w && !pick; k++) { uint32_t q = coop_p2w[k], qh = MEM32(q + 0x18), qm = qh >= 0x10008u && qh < 0x84000000u ? MEM32(qh - 8 + 0x40) : 0;
                        if (!(qm == 0x0834F2D4u || qm == 0x36AC3F58u || qm == 0xE7EEC090u) && coop_p2w_slot[k] == want) pick = q; } }
                else { stow_n = 0;
                    if (sl == 0u && (coop_p2_arrow_for(w) || w == coop_p2_arrow())) {
                        pick = coop_p2_arrow() ? coop_p2_arrow() : w;
                        if (bow) { uint32_t a[2] = { bow, 1u }; coop_take_nested = 1; tt_gcall(0x00085FB0u, coop_p2, 2, a); coop_take_nested = 0; } }
                    else if (sl == 1u && bow) after_bow_back = 1; }
                if (pick || sl == 0xAu || sl == 0u || sl == 1u) {
                    static int n; if (n++ < 40) tt_dbg("[COOPWEAPON] player 2 Legolas: slot %X takes %08X for %08X%s\n", sl, pick ? pick : w, w, after_bow_back ? " (then the bow into 0xA)" : ""); }
            }
            if (!pick && sl < 8u && coop_p2_arrow() && (coop_p2_arrow_for(w) || w == coop_p2_arrow())) pick = coop_p2_arrow();   /* (the arrow: his copy -- player 2
                * Aragorn's draw had his sword in place of it, no arrow in his hand: a playtest) */
            if (sl < 8u && !pick)   /* slots 0-3: his own melee weapon for that slot (putting the bow away fills 0, 1 and 2) */
                for (k = 0; k < coop_np2w && !pick; k++) { uint32_t q = coop_p2w[k], qh = MEM32(q + 0x18), qm = qh >= 0x10008u && qh < 0x84000000u ? MEM32(qh - 8 + 0x40) : 0;
                    int ranged = qm == 0x0834F2D4u || qm == 0x36AC3F58u || qm == 0xE7EEC090u;
                    if (!ranged && (uint32_t)coop_p2w_slot[k] == sl) pick = q; }
            if (!pick && (sl >= 8u || wr)) {   /* (never the sink, coop_p2_weapon_sink: a copy nobody draws; the P2 Bot's moves had made
                                                 * him its owner, and he aimed with it, his blade showing: a playtest) */
                extern uint32_t coop_p2_weapon_sink(void); extern uint32_t coop_p2_bow(void); uint32_t sink = coop_p2_weapon_sink();
                pick = coop_p2_bow();   /* (his hero's kind first: coop_p2_bow; the rules below if none) */
                /* the bow (his hand, or a slot he has no melee weapon for: his back): the ranged copy
                                                 * he already holds (owner +0x11C), so the one on his back goes to his hand (a playtest
                                                 * from the level's start: one stayed on his back while another copy went to his hand,
                                                 * and he shot with his blade); none held: the one asked for if it is his, else his first */
                for (k = 0; k < coop_np2w && !pick; k++) { uint32_t q = coop_p2w[k], qh = MEM32(q + 0x18), qm = qh >= 0x10008u && qh < 0x84000000u ? MEM32(qh - 8 + 0x40) : 0;
                    if ((qm == 0x0834F2D4u || qm == 0x36AC3F58u || qm == 0xE7EEC090u) && MEM32(q + 0x11C) == coop_p2 && (q != sink || q == coop_p2_bow())) pick = q; }
                for (k = 0; k < coop_np2w && !pick && wr && w != sink; k++) if (coop_p2w[k] == w) pick = w;
                for (k = 0; k < coop_np2w && !pick; k++) { uint32_t q = coop_p2w[k], qh = MEM32(q + 0x18), qm = qh >= 0x10008u && qh < 0x84000000u ? MEM32(qh - 8 + 0x40) : 0;
                    if ((qm == 0x0834F2D4u || qm == 0x36AC3F58u || qm == 0xE7EEC090u) && q != sink) pick = q; }
            }
            if (pick && pick != w) {
                { extern uint32_t coop_p2_weapon_sink(void); static int n; if (n++ < 40) tt_dbg("[COOPWEAPON] player 2 (own moves): slot %X takes his %08X for %08X (the sink %08X)\n", sl, pick, w, coop_p2_weapon_sink()); }
                MEM32(esp + 4) = pick; w = pick;
                if (getenv("TT_COOP_BOWFLAGS")) { int q; for (q = 0; q < coop_rng_n_x(); q++) { uint32_t c = coop_rng_copy_x(q); if (c >= 0x10000u) tt_dbg("[BOWFLAGS] copy %08X '%s': +110 %08X +114 %u +11C %08X +124 %08X +1C %08X +20 %08X +24 %08X%s\n", c, (const char *)XBOX_PTR(MEM32(c + 0x18)), MEM32(c + 0x110), MEM32(c + 0x114), MEM32(c + 0x11C), MEM32(c + 0x124), MEM32(c + 0x1C), MEM32(c + 0x20), MEM32(c + 0x24), c == pick ? " <- picked" : ""); } }
            }
        }
    }
    {   /* one bow for player 2: he has a copy of each of player 1's ranged weapon objects (two at the Gates of Moria), and
         * with his own hero's animation machine (coop_own_machines) Legolas took both into his bow hand (0xA): two bows
         * drawn. One copy stands for all (coop_p2_bow, else the first he takes). TT_COOP_P2ALLBOWS: as before */
        static uint32_t first; static unsigned lv; extern const char *tt_coop_p2_char_now;
        uint32_t h = w >= 0x10000u ? MEM32(w + 0x18) : 0;
        if (lv != coop_level_n) { lv = coop_level_n; first = 0; }
        if (((coop_p2 >= 0x10000u && ecx == coop_p2) || (coop_twin >= 0x10000u && ecx == coop_twin && coop_mode() == 3)) && h >= 0x10008u && h < 0x84000000u && MEM32(w) == 0x174980u && !getenv("TT_COOP_P2ALLBOWS")) {   /* (the twin too: his set-up, before the take-over, puts one on his back) */
            uint32_t m = MEM32(h - 8 + 0x40);
            if (m == 0x0834F2D4u || m == 0x36AC3F58u || m == 0xE7EEC090u) {   /* (Legolas' bow, Aragorn's bow, Gimli's throwing axe: a ranged copy) */
                if (!first) { extern uint32_t coop_p2_bow(void); first = coop_p2_bow(); if (!first) first = w; }   /* (coop_p2_bow: the copy drawn right; the same one on his back and in his hand) */
                if (w != first) { static int n; if (n++ < 8) tt_dbg("[COOPWEAPON] player 2's ranged %08X stands in for %08X (one bow)\n", first, w); MEM32(esp + 4) = first; w = first; }
            }
        }
    }
    if (coop_p2 >= 0x10000u && ecx == coop_p2 && w >= 0x10000u && MEM32(w) == 0x174980u && !getenv("TT_COOP_P2TAKEALL")) {
        int i, mine = 0; for (i = 0; i < coop_np2w; i++) if (coop_p2w[i] == w) mine = 1;
        if (!mine) {   /* player 1's ranged weapon: player 2's own copy of it with his hero's ranged model */
            extern uint32_t coop_p2_ranged_copy(uint32_t w); uint32_t c = coop_p2_ranged_copy(w);
            if (c) { MEM32(esp + 4) = c; sub_00085FB0_gen(); return; }
        }
        if (!mine) {
            static int n; uint32_t h = MEM32(w + 0x18);
            if (n++ < 20) tt_dbg("[COOPWEAPON] player 2 doesn't take %08X '%s' (not his)\n", w, h >= 0x10000u && h < 0x84000000u ? (const char *)XBOX_PTR(h) : "?");
            eax = 0; esp += 12; return;   /* (thiscall, two arguments: ret 8) */
        }
    }
    if (getenv("TT_COOP_TAGLOG") && ecx == coop_p2 && coop_p2 >= 0x10000u) {   /* (research) the attach point (0x000282E0 on the character model's resource, instance +0x3D0 -> +4) for this slot: player 2's, player 1's, each companion's */
        extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment); static uint32_t tmp; static int seen[16]; uint32_t sl2 = MEM32(esp + 8); int i;
        if (!tmp) tmp = xbox_ContiguousAlloc(16, 16u);
        if (tmp && sl2 < 16u && !seen[sl2]++) {
            uint32_t who[8]; const char *nm[8]; int n = 0;
            who[n] = coop_p2; nm[n++] = "P2"; who[n] = coop_first_char; nm[n++] = "P1";
            for (i = 0; i < coop_mevn[0] && n < 8; i++) { uint32_t a2 = coop_mev[0][i].actor; if (a2 != coop_p2 && a2 != coop_first_char && MEM32(a2 + 0x114) == 2) { who[n] = a2; nm[n++] = (const char *)XBOX_PTR(MEM32(a2 + 0x18)); } }
            for (i = 0; i < n; i++) { uint32_t inst = MEM32(who[i] + 0x3D0), res = inst >= 0x10000u ? MEM32(inst + 4) : 0, args[2], t, q;
                if (res < 0x10000u) continue;
                MEM32(tmp) = sl2; args[0] = sl2; args[1] = tmp; t = tt_gcall(0x000282E0u, res, 2, args);
                tt_dbg("[TAG] slot %X %s (type %u) inst %08X res %08X -> tag %08X (out %08X):", sl2, nm[i], MEM32(who[i] + 0x2F0), inst, res, t, MEM32(tmp));
                for (q = 0; q < 0x50 && t >= 0x10000u && t < 0x84000000u; q += 4) tt_dbg(" %08X", MEM32(t + q));
                tt_dbg("\n"); } } }
    {   uint32_t fw = MEM32(esp + 4), fsl = MEM32(esp + 8); int mine = ecx == coop_p2 && coop_p2_draw_on();
        if (ecx == coop_p2 && coop_p2 >= 0x10000u && coop_first_char >= 0x10000u && MEM32(coop_p2 + 0x3F8) != MEM32(coop_first_char + 0x3F8) && fsl < 8u) { extern void coop_p2_show_hands(uint32_t only); coop_p2_show_hands(fw); }
        if (mine && fw >= 0x10000u && !getenv("TT_COOP_NOP2DETACH")) {   /* off its bone first: the attach (0x26140) leaves a weapon already on the
            * model where it is, and player 1 Legolas' moves free each one with @DetachFromBone (0xA0E50 -> 0x860C0) before moving it; player
            * 2's name player 1 Aragorn's dagger and bow, so his knives stayed in his hands while he drew (a playtest, bot run: bones 31/23) */
            uint32_t a1[1]; a1[0] = fw; tt_gcall(0x000860C0u, coop_p2, 1, a1); }
        sub_00085FB0_gen();
        if (mine) coop_p2_render_offset(fw, fsl); }
    if (after_bow_back) { uint32_t a[2] = { 0, 0xAu }; extern uint32_t coop_p2_bow(void); a[0] = coop_p2_bow(); coop_take_nested = 1; tt_gcall(0x00085FB0u, coop_p2, 2, a); coop_take_nested = 0; }
}
extern void sub_00097A10_gen(void); void sub_00097A10(void) {
    if (getenv("TT_COOP_WPNSCAN") && coop_p2 >= 0x10000u && MEM32(esp + 4) == coop_p2) {   /* (research) player 2 made the owner of a weapon that isn't his copy */
        int i, mine = 0; for (i = 0; i < coop_nwpn; i++) if (coop_wpn_copy[i] == ecx) mine = 1;
        if (!mine) { static int n; uint32_t h = MEM32(ecx + 0x18); if (n++ < 40) tt_dbg("[WPNOWN] %08X '%s' -> owner P2, from %08X %08X %08X %08X (stack)\n", ecx, h >= 0x10000u && h < 0x84000000u ? (const char *)XBOX_PTR(h) : "?", MEM32(esp), MEM32(esp + 8), MEM32(esp + 12), MEM32(esp + 16)); } }
    { int sw = tt_coop_enter_weapon2(ecx, 0x00097A10u); sub_00097A10_gen(); tt_coop_weapon_post(0x00097A10u); if (sw) tt_coop_leave(); } }
/* ---- COOP-WEAPON end ---- */

/* ---- COOP-TRACE begin (generated by local/re/gen_trace_hooks.py; research only) ---- */
extern int coop_phase;
#define COOP_NT 11
static const uint32_t coop_tfn[COOP_NT] = {0x00018160u, 0x000181A0u, 0x00025E10u, 0x000264E0u, 0x00026580u, 0x00026C20u, 0x000278C0u, 0x0003D8A0u, 0x0003D920u, 0x0003D990u, 0x0003D9B0u};
static unsigned coop_tn[3][COOP_NT];
void tt_coop_trace_dump(void)
{
    int i; for (i = 0; i < COOP_NT; i++) if (coop_tn[1][i] || coop_tn[2][i]) tt_dbg("[COOPTRACE] %08X  P1 step %u  P2 step %u\n", coop_tfn[i], coop_tn[1][i], coop_tn[2][i]);
    memset(coop_tn, 0, sizeof coop_tn);
}
extern void sub_00018160_gen(void); void sub_00018160(void) { if (coop_phase) coop_tn[coop_phase][0]++; sub_00018160_gen(); }
extern void sub_000181A0_gen(void); void sub_000181A0(void) { if (coop_phase) coop_tn[coop_phase][1]++; sub_000181A0_gen(); }
extern void sub_00025E10_gen(void); void sub_00025E10(void) { if (coop_phase) coop_tn[coop_phase][2]++; sub_00025E10_gen(); }
extern void sub_000264E0_gen(void); void sub_000264E0(void) { if (coop_phase) coop_tn[coop_phase][3]++; sub_000264E0_gen(); }
extern void sub_00026580_gen(void); void sub_00026580(void) { if (coop_phase) coop_tn[coop_phase][4]++; sub_00026580_gen(); }
extern void sub_00026C20_gen(void); void sub_00026C20(void) { if (coop_phase) coop_tn[coop_phase][5]++; sub_00026C20_gen(); }
extern void sub_000278C0_gen(void); void sub_000278C0(void) { if (coop_phase) coop_tn[coop_phase][6]++; sub_000278C0_gen(); }
extern void sub_0003D8A0_gen(void); void sub_0003D8A0(void) { if (coop_phase) coop_tn[coop_phase][7]++; sub_0003D8A0_gen(); }
extern void sub_0003D920_gen(void); void sub_0003D920(void) { if (coop_phase) coop_tn[coop_phase][8]++; sub_0003D920_gen(); }
extern void sub_0003D990_gen(void); void sub_0003D990(void) { if (coop_phase) coop_tn[coop_phase][9]++; sub_0003D990_gen(); }
extern void sub_0003D9B0_gen(void); void sub_0003D9B0(void) { if (coop_phase) coop_tn[coop_phase][10]++; sub_0003D9B0_gen(); }
/* ---- COOP-TRACE end ---- */

/* A new level is loading (its file opens: heroes.c's redirect, once per load, a restart too): the last level's co-op
 * state goes, in co-op or not (a solo level after a co-op one must not see the old twin). Until the menu, every test
 * ran one level per launch, so nothing reset this. */
void coop_level_reset(void)
{
    tt_dbg("[COOP] new level: co-op state reset (player 2 was %08X, mode now %d)\n", coop_p2, coop_mode());
    if (coop_p2 && coop_rec_addr && !coop_depth) {   /* (a retry: kept for coop_take) */
        memcpy(&coop_kept_p2_90, coop_s2 + 0x400 + 0x90, 4); coop_kept_p1_90 = MEMF(coop_rec_addr + 0x90); coop_kept_level = MEM32(0x1BB2A4u); coop_kept = 1;
        memcpy(coop_kept_p2blk, coop_s2 + 0x400 + 0x94, 32);
        tt_dbg("[COOPEXP] level ends or restarts: level experience player 1 %.0f, player 2 %.0f (level %u)\n", coop_kept_p1_90, coop_kept_p2_90, coop_kept_level); }
    if (getenv("TT_COOP_RESPAWNID")) {   /* bot test: start the level from a checkpoint (the respawn id @SetRespawnId keeps at 0x1BB354) */
        static int done; if (!done) { done = 1; MEM32(0x1BB354u) = (uint32_t)atoi(getenv("TT_COOP_RESPAWNID")); tt_dbg("[CHECKPT] test: respawn id set to %d\n", (int32_t)MEM32(0x1BB354u)); } }
    coop_p2 = coop_p2_ally = coop_p2_t0 = 0; coop_depth = 0; coop_level_n++;
    coop_world = coop_rec = coop_key = coop_last_char = coop_hero_sel = coop_twin = coop_first_char = 0;
    coop_nwpn = 0; memset(coop_wpn_copy, 0, sizeof coop_wpn_copy); memset(coop_wpn_orig, 0, sizeof coop_wpn_orig); coop_np2w = 0;
    coop_nchars = 0; memset(coop_thr, 0, sizeof coop_thr); { extern int coop_nwobjs; coop_nwobjs = 0; }
    coop_rec_addr = 0; coop_p2rec_ok = 0; coop_p2pop_until = 0; coop_p2_wpnarg_p1 = 0;   /* (player 1's weapon name: the level's script data) */ { extern uint32_t coop_lurtz_p2_hits; coop_lurtz_p2_hits = 0; }
    if (coop_p2rec_mem) { uint32_t j; for (j = 0; j < 0x400u; j += 4) MEM32(coop_p2rec_mem + j) = 0; }   /* (his gauge's copy, ours: zeroed again, or the next co-op level refused it as in use and showed no gauge) */
    memset(coop_down, 0, sizeof coop_down); memset(coop_down_t, 0, sizeof coop_down_t); memset(coop_upwatch, 0, sizeof coop_upwatch);
    coop_gameover = 0; coop_gameover_t = 0; coop_end_seen = 0; coop_real_death_pending = 0;
    coop_persp_obj = 0; coop_cam_zoom = 1.0f; coop_persp_applied = 1.0f;
    coop_place_frames = 0; coop_start_place = 1; coop_attach_child_seen = 0; coop_new_weapon_now = 0; coop_last_obj = 0; coop_attach_child = 0; coop_new_weapon = 0;
    coop_ring_on[0] = coop_ring_on[1] = 0; coop_hud_show = 0;
    coop_hpmax_set[0] = coop_hpmax_set[1] = 0;
    coop_npool = 0;
    coop_extra_done = 0;
    coop_ammo_dirty = 1;
    coop_npick = 0;
}
int tt_coop_p2_gauge_on(void) { extern int coop_p2rec_ok; return coop_mode() == 3 && coop_p2 >= 0x10000u && coop_p2rec_ok; }   /* (e3hud.c: player 2's gauge outline) */

/* testing: TT_UPGXP=n -- the hero's experience set to n as the upgrade screen works out his points (0x000D2630, cdecl hero index) */
extern void sub_000D2630_gen(void);
void sub_000D2630(void)
{
    static int xp = -2;
    if (xp == -2) { const char *e = getenv("TT_UPGXP"); xp = e ? atoi(e) : -1; }
    if (xp >= 0) { uint32_t h = MEM32(esp + 4); float f = (float)xp; uint32_t w; memcpy(&w, &f, 4); if (h < 8) MEM32(0x4AB628u + h * 0x348u) = w; }
    sub_000D2630_gen();
}

/* (1.9.5) The upgrade screen's points counting down after a purchase, at any
 * frame rate. The screen's update (0x0006E830, thiscall) takes 100 off the
 * points it shows (+0x5A4) each frame until they reach the hero's points,
 * with the purchase sound playing until then -- 6000 a second at the menus'
 * 60 fps, and ten times that at 600 fps: the count and its sound over in a
 * blink. Here the count goes at 6000 a second whatever the frame rate.
 * TT_NO_UPGCOUNTFIX=1: as before. */
extern void sub_0006E830_gen(void);
void sub_0006E830(void)
{
    static int off = -1;
    static long long fq, last; static double acc;
    uint32_t self = ecx;
    int32_t d0, d1, target, allowed;
    long long now;
    double ms;
    if (off < 0) off = getenv("TT_NO_UPGCOUNTFIX") != NULL;
    if (off || self < 0x10000u) { sub_0006E830_gen(); return; }
    d0 = (int32_t)MEM32(self + 0x5A4u);
    sub_0006E830_gen();
    d1 = (int32_t)MEM32(self + 0x5A4u);
    {
        extern __declspec(dllimport) int __stdcall QueryPerformanceCounter(long long *);
        extern __declspec(dllimport) int __stdcall QueryPerformanceFrequency(long long *);
        if (!fq) QueryPerformanceFrequency(&fq);
        QueryPerformanceCounter(&now);
        ms = last && fq ? (double)(now - last) * 1000.0 / (double)fq : 1000.0 / 60.0;
        last = now;
        if (ms > 100.0 || ms < 0.0) ms = 1000.0 / 60.0;
    }
    if (d1 >= d0 || d0 - d1 > 100) { acc = 0; return; }   /* (not counting down this frame) */
    target = (int32_t)MEM32(0x4AB67Cu);                     /* (the hero's points: what the count goes to) */
    acc += ms * 6.0;                                        /* 6000 points a second */
    allowed = (int32_t)acc; acc -= allowed;
    {
        int32_t v = d0 - allowed;
        if (v < target) v = target;
        if (v > d0) v = d0;
        MEM32(self + 0x5A4u) = (uint32_t)v;
    }
}

/* (1.9.7, Versus) the game's "new game" (0x000D1C10: the progress and the hero reset, as New Game and the developer
 * switches' start do). Versus' Start goes through New Game (options.c): right after it, the match is set up -- the
 * arena's level (0x1BB2A4, the level the play screen loads), player 1's hero (0x4AB69D; an extra hero is hero 4 with
 * heroes.c's guest) and player 2's (the co-op's choice, tt_coop_p2_selected) */
extern void sub_000D1C10_gen(void);
void sub_000D1C10(void)
{
    extern volatile int tt_vs_start_pending; extern const char *tt_vs_hero_name(int pl); extern int tt_vs_arena_level(void);
    extern int tt_heroes_p1_pick(const char *name); extern char tt_coop_p2_selected[32];
    sub_000D1C10_gen();
    if (tt_vs_start_pending && tt_vs_menu_on) {
        uint32_t sa = eax, sc = ecx, sd = edx, sp = esp; int hb;
        tt_vs_start_pending = 0;
        MEM32(0x1BB2A4u) = (uint32_t)tt_vs_arena_level();
        hb = tt_heroes_p1_pick(tt_vs_hero_name(0));
        if (hb >= 1 && hb <= 4) MEM8(0x4AB69Du) = (uint8_t)hb;
        snprintf(tt_coop_p2_selected, 32, "%s", tt_vs_hero_name(1));
        tt_dbg("[VS] new game as a match: level %u, player 1 %s (hero %d), player 2 %s\n", MEM32(0x1BB2A4u), tt_vs_hero_name(0), hb, tt_coop_p2_selected);
        { extern int (*volatile xbox_movie_skip_cb)(void); xbox_movie_skip_cb = vs_skip_movies; }
        eax = sa; ecx = sc; edx = sd; esp = sp;
    }
}

/* 0x000EBF10 (thiscall sound; 10 arguments, ret 40): a stream played -- music and ambience, by a base name the
 * function suffixes ("_L", "_AMBIENT", "_AMBIENT_L"). (Versus) the arena's own streams (its level's code at the
 * name's start) are another level's track (versus.c tt_vs_music_track; its streaming bank loaded in place of the
 * arena's, lang.c). TT_VS_MUSICLOG: the calls */
extern void sub_000EBF10_gen(void);
void sub_000EBF10(void)
{
    static char name[64]; static uint32_t gname;
    extern const char *tt_vs_music_track_for_stream(const char *stream);
    int k;
    if (getenv("TT_VS_MUSICLOG")) { static int n; if (n++ < 60) { char b[400]; int o = 0;
        for (k = 1; k <= 10 && o < 360; k++) { uint32_t a = MEM32(esp + 4 * k);
            if (a >= 0x10000u && a < 0x84000000u && MEM8(a) >= '0' && MEM8(a) <= 'z') o += snprintf(b + o, sizeof b - o, " [%d]\"%.24s\"", k, (const char *)XBOX_PTR(a));
            else o += snprintf(b + o, sizeof b - o, " [%d]%X", k, a); }
        { uint32_t k2, cs[12]; int m = 0; for (k2 = 0; k2 < 0x400 && m < 12; k2 += 4) { uint32_t v = MEM32(esp + k2); if (v >= 0x12000u && v < 0x170000u) cs[m++] = v; }
          o += snprintf(b + o, sizeof b - o, " | ecx %08X ra", ecx); for (k2 = 0; k2 < (uint32_t)m && o < 390; k2++) o += snprintf(b + o, sizeof b - o, " %X", cs[k2]); }
        tt_dbg("[VSMUSIC] stream:%s\n", b); } }
    if (tt_vs_on()) for (k = 1; k <= 10; k++) {
        uint32_t a = MEM32(esp + 4 * k); const char *r;
        if (a < 0x10000u || a >= 0x84000000u || MEM8(a) < '0' || MEM8(a) > '9') continue;
        r = tt_vs_music_track_for_stream((const char *)XBOX_PTR(a));
        if (!r) continue;
        if (!gname) { extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment); gname = xbox_ContiguousAlloc(64u, 16u); }
        if (!gname) break;
        snprintf(name, sizeof name, "%s", r);
        { int i; for (i = 0; i < 63 && name[i]; i++) MEM8(gname + i) = (uint8_t)name[i]; MEM8(gname + i) = 0; }
        { static int n; if (n++ < 10) tt_dbg("[VS] music: stream \"%.24s\" -> \"%s\"\n", (const char *)XBOX_PTR(a), name); }
        MEM32(esp + 4 * k) = gname;
        break;
    }
    sub_000EBF10_gen();
}
uint32_t tt_vs_level_index(void) { return MEM32(0x1BB2A4u); }
/* 0x000C3640: a sound played by its id (the level's bank; [esp+4] the id). TT_SNDLOG: each, with its caller */
/* (1.9.7) 0x000C3B90 (cdecl; id, volume, pitch, position x y z w, -1.0, 0, 0): a sound at a place in the world -- the
 * blows' clangs (0x82EB0: 0xE1 / 0xDD) and the hits' own (0x85340: from the hit's record). TT_SNDLOG logs them, and the
 * last flesh-hit id each hero's blow played is kept for our own judged hits (tt_vs_impact) */
uint32_t tt_snd3d_last_id, tt_snd3d_last_from; uint32_t tt_snd3d_hit_id = 0, tt_snd3d_hit_vol, tt_snd3d_hit_pitch;
extern void sub_000C3B90_gen(void);
void sub_000C3B90(void)
{
    static int on = -1; if (on < 0) on = getenv("TT_SNDLOG") != NULL;
    tt_snd3d_last_id = MEM32(esp + 4); tt_snd3d_last_from = MEM32(esp);
    if (on) { float x = MEMF(esp + 16), y = MEMF(esp + 20); int who = -1; float b = 9.0f, d; int w;
        for (w = 0; w < 2; w++) { uint32_t h = w ? coop_p2 : coop_first_char; if (h < 0x10000u) continue; d = (MEMF(h + 0xF0) - x) * (MEMF(h + 0xF0) - x) + (MEMF(h + 0xF4) - y) * (MEMF(h + 0xF4) - y); if (d < b) { b = d; who = w; } }
        tt_dbg("[SNDWHO] id %u by %s t %u from %08X\n", MEM32(esp + 4), who < 0 ? "other" : who ? "P2" : "P1", tt_game_ms, MEM32(esp)); }
    {   /* (1.9.7, Versus) its camera stands 9-15 units off to keep both heroes in view: the characters' sounds (voices,
         * blows, steps), placed at them, came out faint from there (the listener is the camera). Brought towards the
         * camera's eye to TT_VS_SNDNEAR of the way (0.35), as loud as the campaign's close camera has them */
        extern int tt_vs_on(void);
        uint32_t cm = MEM32(0x43B164u);
        if (tt_vs_on() && cm >= 0x10000u && !getenv("TT_VS_SNDFAR")) {
            float k = getenv("TT_VS_SNDNEAR") ? (float)atof(getenv("TT_VS_SNDNEAR")) : 0.35f, ex = MEMF(cm + 0x50), ey = MEMF(cm + 0x54), ez = MEMF(cm + 0x58);
            float x = MEMF(esp + 16), y = MEMF(esp + 20), z = MEMF(esp + 24), d2 = (x - ex) * (x - ex) + (y - ey) * (y - ey) + (z - ez) * (z - ez);
            if (d2 > 4.0f && d2 < 3600.0f) { MEMF(esp + 16) = ex + (x - ex) * k; MEMF(esp + 20) = ey + (y - ey) * k; MEMF(esp + 24) = ez + (z - ez) * k; }
        }
    }
    {   /* (1.9.7, Versus) the heroes' own motions' sounds: each his own voice, the blows at full strength (versus.c) */
        extern int tt_vs_on(void); extern int tt_vs_hero_sound(const char *skin, uint32_t id, float vol);
        if (MEM32(esp) == 0x00085B90u && tt_vs_on() && (esi == coop_first_char || esi == coop_p2) && esi >= 0x10000u) {
            int w = esi == coop_p2; float vol = MEMF(esp + 8);
            if (tt_vs_hero_sound(vs_skin_name(w), MEM32(esp + 4), vol)) { if (on) tt_dbg("[SNDRET] id %u: ours (P%d)\n", MEM32(esp + 4), w + 1); eax = 0; esp += 4; return; }
        }
    }
    if (MEM32(esp) == 0x00085B90u) { tt_snd3d_hit_id = MEM32(esp + 4); tt_snd3d_hit_vol = MEM32(esp + 8); tt_snd3d_hit_pitch = MEM32(esp + 12); }
    if (on) { static int n; if (n++ < 3000) tt_dbg("[SND3D] t %u id %u (0x%X) from %08X vol %.2f pitch %.2f at %.1f %.1f %.1f\n", coop_now(), MEM32(esp + 4), MEM32(esp + 4), MEM32(esp), MEMF(esp + 8), MEMF(esp + 12), MEMF(esp + 16), MEMF(esp + 20), MEMF(esp + 24)); }
    { uint32_t id = MEM32(esp + 4); sub_000C3B90_gen(); if (on) tt_dbg("[SNDRET] id %u -> %08X\n", id, eax); }
    return;
}
/* (1.9.7, research) 0x000F4380 (thiscall resources; the pickups' chunk): its first bytes, for the pickups' model keys */
extern void sub_000F4380_gen(void);
void sub_000F4380(void)
{
    if (getenv("TT_PKCHUNK")) { uint32_t d = MEM32(esp + 4), k; char b[1400]; int q = 0;
        for (k = 0; k < 0x180 && d >= 0x10000u && q < 1300; k += 4) q += snprintf(b + q, sizeof b - q, " %08X", MEM32(d + k));
        tt_dbg("[PKCHUNK] %08X:%s\n", d, b); }
    sub_000F4380_gen();
}
extern void sub_000C3640_gen(void);
void sub_000C3640(void)
{
    static int on = -1; if (on < 0) on = getenv("TT_SNDLOG") != NULL;
    if (on) { static int n; if (n++ < 3000) tt_dbg("[SNDID] t %u id %u from %08X args %08X %08X %08X\n", coop_now(), MEM32(esp + 4), MEM32(esp), MEM32(esp + 8), MEM32(esp + 12), MEM32(esp + 16)); }
    sub_000C3640_gen();
}
