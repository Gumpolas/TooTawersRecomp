/**
 * The Lord of the Rings: The Two Towers (Xbox, USA) - Recompiled Game Entry Point
 *
 * This is the Windows executable that hosts the recompiled game code.
 * It performs the following initialization sequence:
 *
 * 1. Load the original XBE file from disk
 * 2. Initialize the Xbox memory layout (map data sections to original VAs)
 * 3. Initialize the Xbox kernel replacement layer
 * 4. Initialize the kernel bridge (thunk table in Xbox memory)
 * 5. Set up game file paths for I/O redirection
 * 6. Initialize the stack pointer
 * 7. Install VEH crash handler for diagnostics
 * 8. Call the game's original entry point (recompiled)
 *
 * Customize this file for your game:
 *   - Set YOUR_GAME_ENTRY_POINT to the XBE entry point address
 *   - Set YOUR_GAME_XBE_PATH to where the XBE file lives
 *   - Set YOUR_GAME_DIR to the game data directory
 *   - Add any CRT global pre-initialization your game needs
 *   - Customize the VEH handler for game-specific crash diagnosis
 *
 * XBE Details (fill in from xbe_parser output):
 *   Title:       The Lord of the Rings: The Two Towers (Xbox, USA)
 *   Title ID:    0x00000000
 *   Base addr:   0x00010000
 *   Entry point: 0x00000000
 *   Code size:   ~??? KB (.text)
 *   Sections:    ?? (list them)
 *   Kernel imports: ??
 */

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE   /* dladdr */
#endif
#include <windows.h>
#if defined(_WIN32)
#include <dbghelp.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

/* xboxrecomp runtime headers */
#include <xbox/xboxrecomp.h>

/*
 * If xboxrecomp.h is not an umbrella header in your setup, include
 * the individual headers directly:
 *
 * #include "kernel.h"
 * #include "xbox_memory_layout.h"
 * #include "d3d8_xbox.h"
 * #include "dsound_xbox.h"
 * #include "xinput_xbox.h"
 */

/* ── Global register state (defined in xbox_memory_layout.c) ── */

/* RECOMP_TLS is not optional here. The runtime defines these thread-local, and
 * a plain `extern` referencing a __declspec(thread) variable does not resolve
 * to the calling thread's copy -- it resolves to the image's TLS template. The
 * host side then writes g_esp somewhere the generated code never reads, so the
 * guest starts with every register at zero and faults immediately, having
 * apparently ignored the setup that visibly ran. */
extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
extern RECOMP_TLS uint32_t g_ebx, g_esi, g_edi;
extern RECOMP_TLS uint32_t g_seh_ebp;
/* x87 and SSE state. Global for the same reason the volatile GPRs are: one
 * guest routine can lift to several C functions, so a value written in one
 * body is read in the next. Defined in xbox_memory_layout.c like the rest of
 * the register file. */
extern RECOMP_TLS double g_fp_stack[8];
extern RECOMP_TLS int g_fp_top;
extern RECOMP_TLS uint16_t g_fp_control_word;
extern RECOMP_TLS int g_fp_cmp;
extern RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3;
extern RECOMP_TLS RecompXmm g_xmm4, g_xmm5, g_xmm6, g_xmm7;
extern ptrdiff_t g_xbox_mem_offset;

/* ── XBE Constants ─────────────────────────────────────────── */

/*
 * TODO: Set these from your xbe_parser output.
 * Run: py -3 -m tools.xbe_parser game/default.xbe
 */
#define YOUR_GAME_ENTRY_POINT   0x000FB71D  /* XBE entry point VA (xbe_parser) */
/* The game folder comes from twotowers_recomp.ini [Paths] game_dir (written by
 * the launcher), else game_files\ beside the exe. */
static char g_game_dir[MAX_PATH], g_xbe_path[MAX_PATH], g_save_dir[MAX_PATH];
static char g_exe_dir[MAX_PATH], g_ini_path[MAX_PATH];
#define YOUR_GAME_XBE_PATH      g_xbe_path
#define YOUR_GAME_DIR           g_game_dir

/* ── Forward declarations ──────────────────────────────────── */

static BOOL load_xbe(const char *path, void **out_data, size_t *out_size);
/* Thread-start hook (TLS callback): pin each new thread of the game's to
 * CPU 0 while the single-CPU mode is on. Only threads that start in this
 * program are pinned -- the game's own, which must not overlap -- not the
 * ones Windows libraries start for themselves: XAudio2's mixing thread, the
 * graphics driver's, XInput's. Pinned, those had to share CPU 0 with the
 * game, and at 60 fps or unlocked (the game busy all the time) the sound
 * crackled and the music dropped out. A thread of ours that should run
 * elsewhere (the raster threads) sets its own affinity after it starts. */
volatile int g_tt_pin_threads;
#if defined(_WIN32)
static void NTAPI tt_tls_pin(PVOID h, DWORD reason, PVOID r)
{
    static uintptr_t lo, hi;
    static LONG (NTAPI *qit)(HANDLE, int, PVOID, ULONG, PULONG);
    (void)h; (void)r;
    if (reason != DLL_THREAD_ATTACH) return;
    if (!lo) {
        HMODULE m = GetModuleHandleA(NULL);
        const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)m;
        const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const char *)m + dos->e_lfanew);
        lo = (uintptr_t)m; hi = lo + nt->OptionalHeader.SizeOfImage;
        qit = (LONG (NTAPI *)(HANDLE, int, PVOID, ULONG, PULONG))(void *)
              GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    }
    if (qit) {
        PVOID start = NULL;
        /* 9 = ThreadQuerySetWin32StartAddress */
        if (qit(GetCurrentThread(), 9, &start, sizeof start, NULL) == 0
                && ((uintptr_t)start < lo || (uintptr_t)start >= hi))
            return;                        /* a library's thread: leave it free */
    }
    /* stack kept back for the crash handler, so a stack overflow on a
     * game thread still leaves a [CRASH] line in the log */
    { ULONG g = 64 * 1024; SetThreadStackGuarantee(&g); }
    if (!g_tt_pin_threads) return;
    SetThreadAffinityMask(GetCurrentThread(), 1);
}
__attribute__((section(".CRT$XLF"), used)) const PIMAGE_TLS_CALLBACK tt_tls_pin_ptr = tt_tls_pin;
#else
/* POSIX: every thread the runtime starts (CreateThread in win32_compat.c)
 * runs this first. Threads that libraries start (SDL's audio thread, the GL
 * driver's) do not come through here and stay free, as on Windows. */
static void tt_thread_start_pin(void)
{
    if (g_tt_pin_threads) SetThreadAffinityMask(GetCurrentThread(), 1);
}
#endif

#include <signal.h>
#include "twotowers_host.h"
#include "dlss5.inc"
#include "xbe_version.inc"

/* (1.9.3) getenv, looked up once per name (tt_env.h). The cache is keyed by
 * the name's address (the names are string literals); the value is copied.
 * Lookups take no lock; a new name is added under one, its value written
 * before its key. */
#define TT_ENV_SLOTS 1024
static struct { const char *volatile key; const char *volatile val; } s_env[TT_ENV_SLOTS];
static SRWLOCK s_env_lock = SRWLOCK_INIT;
const char *tt_getenv(const char *name)
{
    uintptr_t h;
    unsigned i;
    if (!name || !strcmp(name, "TT_FIGSLOT")) return getenv(name);
    h = ((uintptr_t)name >> 3) * 2654435761u;
    for (i = 0; i < 64; i++) {
        unsigned q = (unsigned)((h + i) % TT_ENV_SLOTS);
        const char *k = s_env[q].key;
        if (k == name) return s_env[q].val;
        if (!k) {
            const char *r;
            AcquireSRWLockExclusive(&s_env_lock);
            if (!s_env[q].key) {
                const char *v = getenv(name);
                char *c = NULL;
                if (v) { size_t n = strlen(v) + 1; c = (char *)malloc(n); if (c) memcpy(c, v, n); }
                s_env[q].val = c;
                MemoryBarrier();
                s_env[q].key = name;
            }
            r = s_env[q].key == name ? s_env[q].val : NULL;
            k = s_env[q].key;
            ReleaseSRWLockExclusive(&s_env_lock);
            if (k == name) return r;
        }
    }
    return getenv(name);
}

/* Recompiled entry point (generated by recomp pipeline) */
extern void xbe_entry_point(void);

/* ── VEH crash handler ─────────────────────────────────────── */

/*
 * Vectored Exception Handler for crash diagnostics.
 *
 * When the recompiled game hits an access violation, this handler prints
 * the faulting address, all Xbox register values, and a native stack trace.
 * This is your primary debugging tool during bring-up.
 *
 * Customize this for your game:
 *   - Add game-specific address checks (GPU register probes, etc.)
 *   - Add dumps of game-specific globals (heap handles, state flags)
 *   - Add SEH simulation if your game uses __try/__except
 */
/* Name the guest function a fault happened in, and recover the call chain.
 *
 * Recompiled code faults as ordinary native code, so the exception record
 * carries a host RIP and nothing else -- there is no guest program counter to
 * report, and the host address changes every build. Two things recover the
 * guest view:
 *
 *   - every generated function is a real symbol in the image (sub_005A03C0
 *     and so on), so the linker's PDB already maps host address back to guest
 *     function. dbghelp turns an anonymous RIP into that name.
 *
 *   - every lifted call pushes its guest return address onto the guest stack
 *     before jumping, so the stack still holds the chain. Scanning up from esp
 *     for values inside the code sections recovers it.
 *
 * ponytail: the stack scan is a scan, not a frame walk -- these are FPO frames
 * with no reliable ebp chain, so there is nothing to walk. It over-reports,
 * since addresses from returned-from calls linger below esp, but naming the
 * guest function is the whole question at a fault.
 *
 * Requires linking dbghelp and keeping the .pdb beside the .exe.
 */
static void print_guest_context(void *rip)
{
    /* SYMBOL_INFO is variable-length: the name is written past the struct, so
     * it must be over-allocated with MaxNameLen set to the slack. */
#if defined(_WIN32)
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
    DWORD64 disp = 0;

    memset(buf, 0, sizeof(buf));
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    if (SymFromAddr(GetCurrentProcess(), (DWORD64)(uintptr_t)rip, &disp, sym))
        fprintf(stderr, "  in %s+0x%llX\n",
                sym->Name, (unsigned long long)disp);
#else
    {
        Dl_info di;
        if (dladdr(rip, &di) && di.dli_fbase)
            fprintf(stderr, "  in %s+0x%llX (%s)\n", di.dli_sname ? di.dli_sname : "exe",
                    (unsigned long long)((uintptr_t)rip - (uintptr_t)(di.dli_saddr ? di.dli_saddr : di.dli_fbase)),
                    di.dli_fname ? di.dli_fname : "?");
    }
#endif

    if (g_xbox_mem_offset && g_esp) {
        const uint32_t *sp =
            (const uint32_t *)((uintptr_t)g_xbox_mem_offset + g_esp);
        int shown = 0, i;
        fprintf(stderr, "  guest stack (return addresses, innermost first):\n");
        for (i = 0; i < 256 && shown < 24; i++) {
            uint32_t v = sp[i];
            if (v > g_xbox_code_lo && v < g_xbox_code_hi) {
                fprintf(stderr, "    [esp+%-4d] 0x%08X\n", i * 4, v);
                shown++;
            }
        }
        /* When the frame is gone, say so, and fall back on what is left.
         *
         * A fault inside a block copy leaves no return address in range: the
         * copy is a host memmove and its caller's frame is further up than
         * this reaches. Worse, a copy that ran past its buffer overwrites the
         * stack itself, so the list comes out empty exactly when it is most
         * needed -- and an empty list under a promising heading reads like
         * "nothing to see" rather than "the evidence is destroyed". */
        if (!shown)
            fprintf(stderr, "    (no return addresses in range)\n");

        /* Everything below only pays for itself when the scan came back
         * empty. A crash where the chain was recovered already says what
         * happened, and burying that under another twenty lines makes the
         * common report worse to read. */
        if (!shown) {
            uint32_t k;

            /* The indirect-call history, which the stack cannot overwrite.
             * recomp_manual.c prints this too, but only from
             * recomp_icall_fail_log -- a page fault inside a block copy
             * never reaches that path, and this is the same information by
             * the route the fault actually took.
             *
             * Declared here rather than included: main.c does not pull in
             * recomp_types.h, and recomp_manual.c spells it the same way. */
            extern volatile uint32_t g_icall_trace[16];
            extern volatile uint32_t g_icall_trace_idx;

            fprintf(stderr, "  recent ICALL targets:");
            for (k = 0; k < 16; k++)
                fprintf(stderr, " %08X",
                        g_icall_trace[(g_icall_trace_idx + k) & 15]);
            fprintf(stderr, "\n");

            /* The frame unfiltered. The pointers and lengths handed to
             * whatever faulted live here and look nothing like code, so the
             * filter above drops exactly what is wanted. On the crash this
             * was written for, one of these words was the end of the mapped
             * RAM mirror -- which is what identified the fault as a copy
             * running off its buffer rather than a stray pointer. */
            for (i = 0; i < 20; i++)
                fprintf(stderr, "    raw[esp+%-4d] 0x%08X\n", i * 4, sp[i]);
        }
    }
}

/* The emulated APU.
 *
 * Two entry points that had no callers anywhere in the tree. apu_mmio_hook.c
 * documents apu_hook_handle_mmio as "called from VEH in main.c" and no main.c
 * called it; apu_decode_and_handle's first line is `if (!g_apu_state) return
 * false` and nothing assigned g_apu_state. So with RECOMP_AC97_READY set the
 * APU's registers were unmapped to be trapped, and every trap was then
 * declined and surfaced as an access violation on the first register
 * DirectSound touched.
 *
 * Declared rather than included so this does not depend on src/apu being on
 * the include path. */
typedef struct MCPXAPUState MCPXAPUState;
extern MCPXAPUState *mcpx_apu_init_standalone(uint8_t *ram_ptr);
extern MCPXAPUState *g_apu_state;
extern bool apu_hook_handle_mmio(PCONTEXT ctx, uintptr_t fault_addr,
                                 uint32_t fault_xbox_va, int is_write);

/* The trapped span, matching what MemoryLayoutInit unmaps: the APU's own
 * 512 KB, not the whole MCPX aperture. AC'97 above it stays plain memory,
 * which is what the codec-ready bit needs. */
#define APU_TRAP_BASE 0xFE800000u
#define APU_TRAP_END  0xFE880000u


/* RECOMP_PROFILE=1: sample the game thread's host RIP every millisecond and
 * print the hottest addresses every 20 s (symbolise with llvm-symbolizer or
 * the map). Cheap enough to leave in; off unless asked. */
#if defined(_WIN32)
#define PROF_SLOTS 4096
static HANDLE s_prof_thread_h;
static struct { uint64_t rip; uint32_t n; } s_prof[PROF_SLOTS];
static struct { uint64_t rip; uint32_t n; } s_prof_caller[PROF_SLOTS];
static DWORD WINAPI prof_thread(LPVOID p)
{
    DWORD last = GetTickCount();
    (void)p;
    for (;;) {
        CONTEXT c;
        Sleep(1);
        if (SuspendThread(s_prof_thread_h) == (DWORD)-1) break;
        c.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(s_prof_thread_h, &c)) {
            uint64_t r = c.Rip;
            /* Walk out of system DLLs to the first frame in this exe, so time
             * spent inside a Windows call is charged to the code that made it. */
            {
                HMODULE exe = GetModuleHandleA(NULL);
                uint64_t lo = (uint64_t)exe, hi = lo + 0x4000000ull;
                CONTEXT u = c;
                int depth;
                for (depth = 0; depth < 24 && !(u.Rip >= lo && u.Rip < hi); depth++) {
                    DWORD64 base = 0; PVOID hd; DWORD64 ef;
                    PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(u.Rip, &base, NULL);
                    if (!fe) { u.Rip = *(DWORD64 *)u.Rsp; u.Rsp += 8; }
                    else RtlVirtualUnwind(0, base, u.Rip, fe, &u, &hd, &ef, NULL);
                    if (!u.Rip) break;
                }
                if (u.Rip >= lo && u.Rip < hi && u.Rip != c.Rip) {
                    uint32_t h2 = (uint32_t)((u.Rip >> 4) * 2654435761u) % PROF_SLOTS, i2;
                    for (i2 = 0; i2 < 64; i2++) {
                        uint32_t k = (h2 + i2) % PROF_SLOTS;
                        if (s_prof_caller[k].rip == u.Rip || s_prof_caller[k].rip == 0) { s_prof_caller[k].rip = u.Rip; s_prof_caller[k].n++; break; }
                    }
                }
            }
            uint32_t h = (uint32_t)((r >> 4) * 2654435761u) % PROF_SLOTS, i;
            for (i = 0; i < 64; i++) {
                uint32_t k = (h + i) % PROF_SLOTS;
                if (s_prof[k].rip == r || s_prof[k].rip == 0) { s_prof[k].rip = r; s_prof[k].n++; break; }
            }
        }
        ResumeThread(s_prof_thread_h);
        if (GetTickCount() - last > 20000) {
            int top, j;
            last = GetTickCount();
            fprintf(stderr, "[PROF] hottest host RIPs:\n");
            for (top = 0; top < (getenv("RECOMP_REPLAY") ? 600 : 25); top++) {
                int best = -1;
                for (j = 0; j < PROF_SLOTS; j++)
                    if (s_prof[j].n && (best < 0 || s_prof[j].n > s_prof[best].n)) best = j;
                if (best < 0) break;
                {
                    HMODULE m = NULL; char nm[MAX_PATH] = "?";
                    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           (LPCSTR)(uintptr_t)s_prof[best].rip, &m))
                        GetModuleFileNameA(m, nm, sizeof nm);
                    fprintf(stderr, "[PROF]   %6u  0x%llX  %s+0x%llX\n", s_prof[best].n, (unsigned long long)s_prof[best].rip,
                            strrchr(nm, '\\') ? strrchr(nm, '\\') + 1 : nm,
                            (unsigned long long)(s_prof[best].rip - (uint64_t)(uintptr_t)m));
                }
                s_prof[best].n = 0;   /* reset as we report */
            }
            fprintf(stderr, "[PROF] exe callers of time spent in DLLs:\n");
            for (top = 0; top < 15; top++) {
                int best = -1;
                for (j = 0; j < PROF_SLOTS; j++)
                    if (s_prof_caller[j].n && (best < 0 || s_prof_caller[j].n > s_prof_caller[best].n)) best = j;
                if (best < 0) break;
                fprintf(stderr, "[PROF]   %6u  exe+0x%llX\n", s_prof_caller[best].n,
                        (unsigned long long)(s_prof_caller[best].rip - (uint64_t)(uintptr_t)GetModuleHandleA(NULL)));
                s_prof_caller[best].n = 0;
            }
            memset(s_prof, 0, sizeof s_prof);
            memset(s_prof_caller, 0, sizeof s_prof_caller);
        }
    }
    return 0;
}
#endif /* _WIN32 profiler */
#include <stdarg.h>
int tt_dbg_on(void) { static int on = -1; if (on < 0) on = getenv("TT_DEBUG") != NULL; return on; }
const char *tt_game_dir(void) { return g_game_dir; }
const char *tt_exe_dir(void) { return g_exe_dir; }
/* Open dir/rel ('/' separated, any case on POSIX) for reading. */
static FILE *tt_fopen_under(const char *dir, const char *rel)
{
    char path[MAX_PATH * 2];
    char *p;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, rel);
#if defined(_WIN32)
    for (p = path; *p; p++) if (*p == '/') *p = '\\';
    return fopen(path, "rb");
#else
    (void)p;
    if ((f = fopen(path, "rb")) != NULL) return f;
    {
        extern void xbox_host_path_fix_case(char *path, const char *base);
        xbox_host_path_fix_case(path, dir);
    }
    return fopen(path, "rb");
#endif
}
FILE *tt_game_fopen(const char *rel) { return tt_fopen_under(g_game_dir, rel); }
FILE *tt_exe_fopen(const char *rel) { return tt_fopen_under(g_exe_dir, rel); }
void tt_dbg(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fflush(stderr); }
/* All-threads sampler (ini [Developer] profile=1 or RECOMP_PROFILE=all):
 * every millisecond, where is each thread of the process? Time inside a DLL
 * is charged to the exe frame that called it (marked W = waiting/system).
 * Printed every 20 s per thread as exe offsets; map them with llvm-nm. */
#if defined(_WIN32)
#include <tlhelp32.h>
#define PA_THREADS 48
#define PA_SLOTS 4096
static struct { DWORD tid; HANDLE h; uint32_t total, sys; struct { uint64_t rip; uint32_t n; } s[PA_SLOTS]; } *s_pa;
static int s_pa_n;
static const char (*s_pa_modname)[40];   /* (1.9.3) the DLLs time was spent in */
static void pa_refresh(void)
{
    HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te;
    DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    if (sn == INVALID_HANDLE_VALUE) return;
    te.dwSize = sizeof te;
    if (Thread32First(sn, &te)) do {
        int i;
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        for (i = 0; i < s_pa_n; i++) if (s_pa[i].tid == te.th32ThreadID) break;
        if (i == s_pa_n && s_pa_n < PA_THREADS) {
            HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (h) { s_pa[s_pa_n].tid = te.th32ThreadID; s_pa[s_pa_n].h = h; s_pa_n++; }
        }
    } while (Thread32Next(sn, &te));
    CloseHandle(sn);
}
/* The exe's code section, so stack words that are data pointers into the
 * exe are not reported as return addresses. */
static void exe_text_range(uint64_t *lo, uint64_t *hi)
{
    static uint64_t L, H;
    if (!L) {
        uint8_t *b = (uint8_t *)GetModuleHandleA(NULL);
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
        IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
        int i;
        L = (uint64_t)(uintptr_t)b; H = L + 0x4000000ull;
        for (i = 0; i < nt->FileHeader.NumberOfSections; i++)
            if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
                L = (uint64_t)(uintptr_t)b + sec[i].VirtualAddress;
                H = L + sec[i].Misc.VirtualSize;
                break;
            }
    }
    *lo = L; *hi = H;
}

/* Suspend, take the registers and a copy of the top of the stack, resume.
 * Nothing that can take a lock runs while the thread is stopped:
 * ReadProcessMemory is a plain system call and fails instead of faulting. */
static int pa_grab(HANDLE h, CONTEXT *c, uint64_t *stk, size_t *nstk)
{
    SIZE_T got = 0;
    if (SuspendThread(h) == (DWORD)-1) return 0;
    c->ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(h, c)) { ResumeThread(h); return 0; }
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)c->Rsp, stk, 512 * 8, &got)) {
        /* near the stack's end: try a smaller copy */
        if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)c->Rsp, stk, 64 * 8, &got)) got = 0;
    }
    ResumeThread(h);
    *nstk = got / 8;
    return 1;
}

/* Hang watchdog (always on). If the game stops presenting frames for 6 s
 * after it has started, write where every thread is: the instruction it is
 * on (module+offset) and the exe return addresses found on its stack, so a
 * freeze report says what everything was waiting for. */
static void wd_dump(void)
{
    HMODULE exe = GetModuleHandleA(NULL);
    uint64_t lo = (uint64_t)exe, hi = lo + 0x4000000ull;
    HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te;
    DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    static uint64_t stk[512];
    uint64_t tlo, thi;
    exe_text_range(&tlo, &thi);
    if (sn == INVALID_HANDLE_VALUE) return;
    fprintf(stderr, "[HANG] no new frame for a while; where every thread is (map exe+ offsets with the _syms.txt file):\n");
    {
        extern void xbox_KernelCallsDump(void);
        extern void xbox_IrqlDumpHolders(void);
        extern volatile int g_timer_phase;
        fprintf(stderr, "[HANG] timer thread phase %d; kernel calls in progress:\n", g_timer_phase);
        xbox_KernelCallsDump();
        xbox_IrqlDumpHolders();
    }
    te.dwSize = sizeof te;
    if (Thread32First(sn, &te)) do {
        HANDLE h; CONTEXT c; size_t n = 0, q; int shown = 0;
        HMODULE m = NULL; char nm[MAX_PATH] = "?";
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) continue;
        if (pa_grab(h, &c, stk, &n)) {
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)(uintptr_t)c.Rip, &m))
                GetModuleFileNameA(m, nm, sizeof nm);
            fprintf(stderr, "[HANG] thread %lu at %s+0x%llX; exe frames:", (unsigned long)te.th32ThreadID,
                    strrchr(nm, '\\') ? strrchr(nm, '\\') + 1 : nm,
                    (unsigned long long)(c.Rip - (uint64_t)(uintptr_t)m));
            for (q = 0; q < n && shown < 12; q++)
                if (stk[q] >= tlo && stk[q] < thi) { fprintf(stderr, " %llX", (unsigned long long)(stk[q] - lo)); shown++; }
            fprintf(stderr, "\n");
        }
        CloseHandle(h);
    } while (Thread32Next(sn, &te));
    CloseHandle(sn);
    {
        extern void tt_dump_dev(void);
        tt_dump_dev();
    }
    fflush(stderr);
}
#else
/* POSIX: no way to stop another thread and read its registers from here, so
 * a hang report is the title's own state. */
static void wd_dump(void)
{
    extern void tt_dump_dev(void);
    fprintf(stderr, "[HANG] no new frame for a while\n");
    tt_dump_dev();
    fflush(stderr);
}
#endif
static DWORD WINAPI wd_thread(LPVOID p)
{
    extern volatile unsigned g_nv2a_flips;
    extern void xbox_UnpinServiceThread(void);
    extern void tt_dump_dev(void);
    unsigned last = 0, dumps = 0;
    DWORD since = GetTickCount(), lastdump = 0, lastdev = GetTickCount();
    (void)p;
    xbox_UnpinServiceThread();
    for (;;) {
        unsigned f = g_nv2a_flips;
        Sleep(500);
        if (GetTickCount() - lastdev > 20000) { extern void xbox_DelayReport(void); lastdev = GetTickCount(); tt_dump_dev(); xbox_DelayReport(); }
        if (getenv("TT_WD_TEST") && f > 200 && !dumps) { wd_dump(); dumps++; }
        if (f != last) { last = f; since = GetTickCount(); continue; }
        /* No new frame for 8 s (loading screens flip several times a
         * second; movies and gameplay more). Before the first frame, allow
         * 20 s for start-up. */
        if (GetTickCount() - since > (f ? 8000u : 20000u) && dumps < 4 &&
            (!lastdump || GetTickCount() - lastdump > 30000)) {
            wd_dump();
            dumps++;
            lastdump = GetTickCount();
        }
    }
    return 0;
}

#if defined(_WIN32)
/* (1.9.5) The exe's function names, from the symbol table the build leaves
 * in the file: the profile below is then reported per function, by name.
 * (One address at a time, a busy thread's work was spread over hundreds of
 * entries and the 14 shown were mostly its waits.) */
static struct { uint32_t rva; const char *name; } *s_fn;
static int s_fn_n;
static int fn_cmp(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}
static void fn_load(void)
{
    char path[MAX_PATH];
    FILE *f;
    long sz;
    uint8_t *b;
    if (!GetModuleFileNameA(NULL, path, sizeof path) || !(f = fopen(path, "rb"))) return;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc(sz > 0 ? (size_t)sz : 1);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); return; }
    fclose(f);
    {
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
        IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
        uint32_t ps = nt->FileHeader.PointerToSymbolTable, ns = nt->FileHeader.NumberOfSymbols, i;
        const char *strtab;
        if (!ps || !ns || (long)ps + (long)ns * 18 > sz) return;
        strtab = (const char *)b + ps + ns * 18;
        s_fn = calloc(ns, sizeof *s_fn);
        if (!s_fn) return;
        for (i = 0; i < ns; i++) {
            const uint8_t *e = b + ps + i * 18;
            int16_t secn; uint16_t type; uint8_t cls, aux;
            uint32_t val;
            memcpy(&val, e + 8, 4); memcpy(&secn, e + 12, 2); memcpy(&type, e + 14, 2); cls = e[16]; aux = e[17];
            if (secn > 0 && secn <= nt->FileHeader.NumberOfSections && (type & 0x30) == 0x20 && (cls == 2 || cls == 3)
                && (sec[secn - 1].Characteristics & IMAGE_SCN_CNT_CODE)) {
                char *nm;
                if (*(const uint32_t *)e == 0) nm = _strdup(strtab + *(const uint32_t *)(e + 4));
                else { nm = (char *)calloc(1, 9); if (nm) memcpy(nm, e, 8); }
                if (nm) { s_fn[s_fn_n].rva = sec[secn - 1].VirtualAddress + val; s_fn[s_fn_n].name = nm; s_fn_n++; }
            }
            i += aux;
        }
        qsort(s_fn, s_fn_n, sizeof *s_fn, fn_cmp);
    }
    free(b);
}
static int fn_find(uint32_t rva)
{
    int lo = 0, hi = s_fn_n - 1, r = -1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (s_fn[m].rva <= rva) { r = m; lo = m + 1; } else hi = m - 1; }
    return r;
}
static DWORD WINAPI pa_thread(LPVOID p)
{
    extern void xbox_UnpinServiceThread(void);
    HMODULE exe;
    xbox_UnpinServiceThread();   /* on the guest's core it got ~1 sample in 40 on Windows */
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    exe = GetModuleHandleA(NULL);
    uint64_t lo = (uint64_t)exe, hi = lo + 0x4000000ull;
    DWORD last = GetTickCount(), lastr = 0;
    (void)p;
    s_pa = calloc(PA_THREADS, sizeof *s_pa);
    if (!s_pa) return 0;
    fn_load();
    for (;;) {
        int t;
        Sleep(2);
        if (GetTickCount() - lastr > 500) { pa_refresh(); lastr = GetTickCount(); }
        for (t = 0; t < s_pa_n; t++) {
            CONTEXT c;
            static uint64_t stk[512];
            size_t nstk;
            uint64_t key; int sys = 0;
            if (!pa_grab(s_pa[t].h, &c, stk, &nstk)) continue;
            key = c.Rip;
            int mod = 0;
            if (!(key >= lo && key < hi)) {
                /* (1.9.3) which DLL: looked up now the thread runs again */
                static HMODULE mods[63]; static char modn[63][40]; static int nm;
                HMODULE m = NULL;
                if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       (LPCSTR)(uintptr_t)c.Rip, &m) && m) {
                    int z;
                    for (z = 0; z < nm && mods[z] != m; z++) {}
                    if (z == nm && nm < 63) {
                        char path[MAX_PATH] = "?", *b;
                        GetModuleFileNameA(m, path, sizeof path);
                        b = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;
                        mods[nm] = m; snprintf(modn[nm], sizeof modn[nm], "%s", b); nm++;
                    }
                    if (z < nm) mod = z + 1;
                }
                s_pa_modname = (const char (*)[40])modn;
                /* In a DLL or the kernel: charge the time to the nearest exe
                 * return address on the copied stack. No unwinding while the
                 * thread is suspended: the unwinder takes loader locks, and a
                 * suspended graphics-driver thread holding one froze the
                 * whole game under the D3D11 renderer (v20). */
                size_t q;
                sys = 1;
                key = 0;
                uint64_t tlo, thi;
                exe_text_range(&tlo, &thi);
                for (q = 0; q < nstk; q++)
                    if (stk[q] >= tlo && stk[q] < thi) { key = stk[q]; break; }
            }
            s_pa[t].total++;
            if (sys) s_pa[t].sys++;
            {
                uint64_t k2 = (key & 0x00FFFFFFFFFFFFFFull) | ((uint64_t)sys << 63) | ((uint64_t)mod << 56);
                uint32_t h = (uint32_t)((key >> 3) * 2654435761u) % PA_SLOTS, i;
                for (i = 0; i < 64; i++) {
                    uint32_t q = (h + i) % PA_SLOTS;
                    if (s_pa[t].s[q].rip == k2 || !s_pa[t].s[q].n) { s_pa[t].s[q].rip = k2; s_pa[t].s[q].n++; break; }
                }
            }
        }
        if (GetTickCount() - last > 20000) {
            last = GetTickCount();
            for (t = 0; t < s_pa_n; t++) {
                int top, j;
                if (s_pa[t].total < 200) { s_pa[t].total = s_pa[t].sys = 0; memset(s_pa[t].s, 0, sizeof s_pa[t].s); continue; }
                fprintf(stderr, "[PROFT] thread %lu: %u samples, %u in system calls\n", (unsigned long)s_pa[t].tid, s_pa[t].total, s_pa[t].sys);
                if (s_fn_n && s_pa[t].sys < s_pa[t].total * 97 / 100) {
                    /* (1.9.5) by function: the samples of each address added up under the
                     * function it is in (W: waiting in, or calling, a system DLL) */
                    static struct { int fn; uint32_t n; int sys, md; } agg[512];
                    int na = 0, q;
                    for (j = 0; j < PA_SLOTS; j++) {
                        uint64_t r = s_pa[t].s[j].rip, a = r & 0x00FFFFFFFFFFFFFFull;
                        int f, sy = (int)(r >> 63), md = (int)((r >> 56) & 0x7F);
                        if (!s_pa[t].s[j].n) continue;
                        f = a ? fn_find((uint32_t)(a - lo)) : -1;
                        for (q = 0; q < na && !(agg[q].fn == f && agg[q].sys == sy && agg[q].md == md); q++) {}
                        if (q == na) { if (na == 512) continue; agg[na].fn = f; agg[na].n = 0; agg[na].sys = sy; agg[na].md = md; na++; }
                        agg[q].n += s_pa[t].s[j].n;
                    }
                    static int all = -1;
                    if (all < 0) all = getenv("TT_PROF_ALL") != NULL;   /* testing: every function, however small */
                    for (top = 0; top < (all ? 512 : 18); top++) {
                        int best = -1;
                        for (q = 0; q < na; q++) if (agg[q].n && (best < 0 || agg[q].n > agg[best].n)) best = q;
                        if (best < 0 || (!all && agg[best].n * 400 < s_pa[t].total)) break;
                        fprintf(stderr, "[PROFF]   %5.1f%% %s%s%s%s%s\n", agg[best].n * 100.0 / s_pa[t].total,
                                agg[best].fn >= 0 ? s_fn[agg[best].fn].name : "?",
                                agg[best].sys ? " (W" : "", agg[best].sys && agg[best].md && s_pa_modname ? " in " : "",
                                agg[best].sys ? (agg[best].md && s_pa_modname ? s_pa_modname[agg[best].md - 1] : "") : "",
                                agg[best].sys ? ")" : "");
                        agg[best].n = 0;
                    }
                }
                for (top = 0; top < 14; top++) {
                    int best = -1;
                    for (j = 0; j < PA_SLOTS; j++)
                        if (s_pa[t].s[j].n && (best < 0 || s_pa[t].s[j].n > s_pa[t].s[best].n)) best = j;
                    if (best < 0) break;
                    {
                        uint64_t r = s_pa[t].s[best].rip, a = r & 0x00FFFFFFFFFFFFFFull;
                        int md = (int)((r >> 56) & 0x7F);
                        fprintf(stderr, "[PROFT]   %6u %s exe+0x%llX%s%s\n", s_pa[t].s[best].n,
                                (r >> 63) ? "W" : " ", (unsigned long long)(a ? a - lo : 0),
                                md && s_pa_modname ? " in " : "", md && s_pa_modname ? s_pa_modname[md - 1] : "");
                    }
                    s_pa[t].s[best].n = 0;
                }
                s_pa[t].total = s_pa[t].sys = 0;
                memset(s_pa[t].s, 0, sizeof s_pa[t].s);
            }
            fflush(stderr);
        }
    }
    return 0;
}

/* Fine timer resolution for the whole run, and no Windows power throttling.
 *
 * A log from Windows 11 showed Sleep(1) taking 15.5 ms and a "1 ms"
 * high-resolution wait taking 4.3 ms: the process was getting the default
 * 15.6 ms timer and EcoQoS-style throttling. The emulated machine's clock
 * (vblank, sound interrupt, GPU callbacks) runs on short waits, so ask for
 * 1 ms and opt out of timer-resolution and execution-speed throttling. */
#endif /* _WIN32 */
#include <mmsystem.h>
static void tt_timer_setup(void)
{
    typedef BOOL (WINAPI *SPI)(HANDLE, int, LPVOID, DWORD);
    struct { ULONG Version, ControlMask, StateMask; } pt = { 1, 0x1 | 0x4, 0 };  /* EXECUTION_SPEED | IGNORE_TIMER_RESOLUTION, both off */
#if defined(_WIN32)
    SPI spi = (SPI)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetProcessInformation");
    timeBeginPeriod(1);
    if (spi) spi(GetCurrentProcess(), 4 /* ProcessPowerThrottling */, &pt, sizeof pt);
#else
    (void)pt;
#endif
}

#if defined(__linux__)
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <sys/time.h>
#define LXP_N (1u << 24)
static uint64_t *lxp_buf; static volatile uint32_t lxp_n, lxp_done;
static void lxp_handler(int sig, siginfo_t *si, void *uc)
{
    (void)sig; (void)si;
    uint32_t i = __sync_fetch_and_add(&lxp_n, 2);
    if (i + 1 < LXP_N) { lxp_buf[i] = (uint64_t)syscall(SYS_gettid); lxp_buf[i + 1] = (uint64_t)((ucontext_t *)uc)->uc_mcontext.gregs[REG_RIP]; }
}
static void *lxp_dump(void *a)
{
    (void)a;
    for (;;) {
        uint32_t n;
        sleep(10);
        n = lxp_n; if (n > LXP_N) n = LXP_N; n &= ~1u;
        if (n > lxp_done) {
            FILE *f = fopen("lxprof.bin", "ab");
            if (f) { uint64_t h[2] = { 0, (uint64_t)(uintptr_t)lxp_handler }; fwrite(h, 8, 2, f); fwrite(lxp_buf + lxp_done, 8, n - lxp_done, f); fclose(f); }
            {   static int maps; if (!maps++) { FILE *i = fopen("/proc/self/maps", "r"), *o = fopen("lxprof.maps", "w"); char ln[512]; if (i && o) while (fgets(ln, sizeof ln, i)) fputs(ln, o); if (i) fclose(i); if (o) fclose(o); } }
            lxp_done = n;
        }
    }
    return NULL;
}
#endif
static void prof_start(void)
{
    {
        static int once;
        if (!once++) tt_timer_setup();
    }
    const char *e = getenv("RECOMP_PROFILE");
    {
        static int wd;
        if (!wd++ && !getenv("TT_NO_WATCHDOG")) CreateThread(NULL, 0, wd_thread, NULL, 0, NULL);
    }
    if (!e) return;
#if defined(_WIN32)
    if (!strcmp(e, "all")) { CreateThread(NULL, 0, pa_thread, NULL, 0, NULL); return; }
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                    &s_prof_thread_h, 0, FALSE, DUPLICATE_SAME_ACCESS);
    CreateThread(NULL, 0, prof_thread, NULL, 0, NULL);
#elif defined(__linux__)
    /* testing (Linux): RECOMP_PROFILE=lx -- this thread's program counter,
     * every 200 us of its CPU time, appended to lxprof.bin (resolve with nm) */
    if (!strcmp(e, "lx")) {
        clockid_t cid; struct sigevent sev; struct sigaction sa; int tm = 0; struct itimerspec its;
        pthread_t th;
        lxp_buf = (uint64_t *)malloc(sizeof(uint64_t) * LXP_N);
        memset(&sa, 0, sizeof sa); sa.sa_sigaction = lxp_handler; sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigaction(SIGPROF, &sa, NULL);
        (void)cid; (void)sev; (void)tm; (void)its;
        {   struct itimerval iv; iv.it_interval.tv_sec = 0; iv.it_interval.tv_usec = 200; iv.it_value = iv.it_interval;
            setitimer(ITIMER_PROF, &iv, NULL);
            pthread_create(&th, NULL, lxp_dump, NULL);
            fprintf(stderr, "[PROF] sampling this thread (main.c lxp)\n");
        }
    }
#else
    fprintf(stderr, "[PROF] the sampling profiler is Windows-only\n");
#endif
}
#include "ohci.h"

void tt_abort_handler(int sig)
{
    fprintf(stderr, "[CRASH] abort (signal %d): guest eax=%08X ecx=%08X esp=%08X\n", sig, g_eax, g_ecx, g_esp);
    fflush(stderr);
    _exit(3);
}
static LONG WINAPI tt_unhandled(PEXCEPTION_POINTERS ep)
{
    fprintf(stderr, "[CRASH] unhandled exception 0x%08lX at RIP=0x%llX (guest esp=%08X eax=%08X ecx=%08X)\n",
            (unsigned long)ep->ExceptionRecord->ExceptionCode, (unsigned long long)ep->ContextRecord->Rip,
            g_esp, g_eax, g_ecx);
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG CALLBACK veh_handler(PEXCEPTION_POINTERS ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        uintptr_t fault_addr = ep->ExceptionRecord->ExceptionInformation[1];

        /*
         * GPU register probe at 0xFD000000 range.
         * Some games probe NV2A registers directly. On real hardware this
         * returns GPU state; here we just skip the instruction.
         * TODO: Implement mini x86-64 decoder for instruction skipping,
         * or connect to the xbox_nv2a library for proper handling.
         */
        if (fault_addr >= 0xFD000000 && fault_addr < 0xFE000000) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        /* APU registers -> the emulated APU.
         *
         * The other half of RECOMP_AC97_READY: reporting the codec ready is
         * what lets DirectSoundCreate past its first gate, and from there it
         * drives the APU directly, so the registers have to fault to be seen.
         * "Handled" means the access was decoded and the instruction stepped
         * over, so execution resumes instead of unwinding. */
        {
            uint32_t xbox_va =
                (uint32_t)(fault_addr - (uintptr_t)g_xbox_mem_offset);
            {   /* testing: TT_VEH_STATS=1 -- which device registers trap, every 10 s */
                static int on = -1; static unsigned cnt[64][2], tot; static DWORD t;
                if (on < 0) on = getenv("TT_VEH_STATS") != NULL;
                if (on) {
                    unsigned k = (xbox_va >> 12) & 63;
                    cnt[k][xbox_OhciOwnsAddress(xbox_va) ? 1 : 0]++; tot++;
                    if (GetTickCount() - t > 10000) {
                        unsigned i;
                        t = GetTickCount();
                        fprintf(stderr, "[VEHSTAT] %u traps:", tot);
                        for (i = 0; i < 64; i++) if (cnt[i][0] + cnt[i][1]) fprintf(stderr, " page%02X:%u/%u", i, cnt[i][0], cnt[i][1]);
                        fprintf(stderr, " (last %08X)\n", xbox_va);
                        memset(cnt, 0, sizeof cnt); tot = 0;
                    }
                }
            }

            /* USB host controllers -> the OHCI model (RECOMP_USB). */
            if (xbox_OhciOwnsAddress(xbox_va)
                    && xbox_OhciHandleMmio(ep->ContextRecord, xbox_va))
                return EXCEPTION_CONTINUE_EXECUTION;

            if (xbox_va >= APU_TRAP_BASE && xbox_va < APU_TRAP_END
                    && apu_hook_handle_mmio(
                           ep->ContextRecord, fault_addr, xbox_va,
                           ep->ExceptionRecord->ExceptionInformation[0]
                               ? 1 : 0)) {
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }

        fprintf(stderr, "[CRASH] Access violation at RIP=0x%llX, fault addr=0x%llX (%s)\n",
            (unsigned long long)ep->ContextRecord->Rip,
            (unsigned long long)fault_addr,
            ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read");
        fprintf(stderr, "  Xbox regs: eax=0x%08X ecx=0x%08X edx=0x%08X esp=0x%08X\n",
            g_eax, g_ecx, g_edx, g_esp);
        fprintf(stderr, "  Xbox regs: ebx=0x%08X esi=0x%08X edi=0x%08X\n",
            g_ebx, g_esi, g_edi);
        fprintf(stderr, "  Xbox VA of fault: 0x%08X\n",
            (uint32_t)(fault_addr - (uintptr_t)g_xbox_mem_offset));
        print_guest_context((void *)ep->ContextRecord->Rip);

        /*
         * TODO: Add game-specific diagnostics here. Examples:
         *
         * Dump CRT heap handle:
         *   uint32_t heap = *(uint32_t *)((uint8_t *)g_xbox_mem_offset + HEAP_HANDLE_VA);
         *   fprintf(stderr, "  CRT heap handle: 0x%08X\n", heap);
         *
         * Dump game state:
         *   uint32_t state = *(uint32_t *)((uint8_t *)g_xbox_mem_offset + GAME_STATE_VA);
         *   fprintf(stderr, "  Game state: %u\n", state);
         */

        /* Print native stack return addresses for debugging */
        {
            uintptr_t *sp = (uintptr_t *)ep->ContextRecord->Rsp;
            fprintf(stderr, "  Native stack (first 8 return addrs):\n");
            for (int i = 0; i < 64 && sp[i]; i++) {
                if (sp[i] >= 0x140000000ULL && sp[i] < 0x150000000ULL) {
                    fprintf(stderr, "    [%d] 0x%llX\n", i, (unsigned long long)sp[i]);
                }
            }
        }
        fflush(stderr);
    }

    /* Everything else that ends the process: report it the same way rather
     * than dying silently (an integer divide by zero inside DirectSound took
     * the game down with an empty log). Only the first few, since a handler
     * further down may still deal with it. */
    {
        DWORD code = ep->ExceptionRecord->ExceptionCode;
        static int other_reports;
        if ((code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_INT_OVERFLOW
             || code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_PRIV_INSTRUCTION
             || code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_FLT_DIVIDE_BY_ZERO)
                && other_reports++ < 4) {
            fprintf(stderr, "[CRASH] Exception 0x%08lX at RIP=0x%llX\n",
                    (unsigned long)code, (unsigned long long)ep->ContextRecord->Rip);
            fprintf(stderr, "  Xbox regs: eax=0x%08X ecx=0x%08X edx=0x%08X esp=0x%08X\n",
                    g_eax, g_ecx, g_edx, g_esp);
            fprintf(stderr, "  Xbox regs: ebx=0x%08X esi=0x%08X edi=0x%08X\n",
                    g_ebx, g_esi, g_edi);
            if (code != EXCEPTION_STACK_OVERFLOW)
                print_guest_context((void *)ep->ContextRecord->Rip);
            fflush(stderr);
        }
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

/* ── WinMain ───────────────────────────────────────────────── */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    void *xbe_data = NULL;
    size_t xbe_size = 0;

    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    twotowers_setup_paths_and_log();

    /* Unbuffered output for immediate visibility during debugging */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("=== The Lord of the Rings: The Two Towers (Xbox, USA) - Static Recompilation ===\n");
    {   /* (1.9.5) indirect calls (C++ virtual functions, the game's function
         * tables) find their target in one table read instead of a binary
         * search over the 8,700 functions */
        extern int recomp_dispatch_init(void);
        if (!getenv("TT_NO_FLAT_DISPATCH")) recomp_dispatch_init();
    }
    printf("Loading XBE...\n");

    /* Install VEH handler (first handler in chain) */
    /* Load symbols up front rather than from inside the handler: at fault
     * time the process is already in a bad way, and SymInitialize
     * allocates. Failure is not fatal -- the handler prints no name. */
#if defined(_WIN32)
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
#else
    {
        extern void (*w32_thread_start_hook)(void);
        w32_thread_start_hook = tt_thread_start_pin;
    }
#endif
    AddVectoredExceptionHandler(1, veh_handler);
    { ULONG g = 64 * 1024; SetThreadStackGuarantee(&g); }
    {
        /* Ways out that pass no exception handler: abort() (a failed check
         * in a library) and a fault nothing above handles. Each leaves a line
         * in the log instead of an empty ending. */
        extern void tt_abort_handler(int sig);
        signal(SIGABRT, tt_abort_handler);
        SetUnhandledExceptionFilter(tt_unhandled);
    }

    /* Step 1: Load XBE */
    if (!load_xbe(YOUR_GAME_XBE_PATH, &xbe_data, &xbe_size)) {
        MessageBoxA(NULL, "Failed to load default.xbe.\n\n"
                    "Run \"The Two Towers Recompiled.exe\" and install from your ISO,\n"
                    "or set [Paths] game_dir in twotowers_recomp.ini.",
                    "Recomp", MB_ICONERROR);
        return 1;
    }
    printf("XBE loaded: %zu bytes\n", xbe_size);
    if (!tt_xbe_supported(xbe_data, xbe_size)) {
        free(xbe_data);
        if (!getenv("TT_VALIDAR_XBE"))
            MessageBoxA(NULL, "Este port requiere el XBE USA original.\n\nEl disco PAL aporta idiomas, pero no sustituye al XBE USA.\nConsulta LEEME_IDIOMAS.txt.", "Version de disco incompatible", MB_ICONERROR);
        return 2;
    }
    if (getenv("TT_VALIDAR_XBE")) { free(xbe_data); printf("[XBE] USA verificado; no se inicia el juego.\n"); return 0; }

    /* Step 2: Initialize Xbox memory layout */
    printf("Initializing Xbox memory layout...\n");
    if (!xbox_MemoryLayoutInit(xbe_data, xbe_size)) {
        MessageBoxA(NULL, "Failed to initialize Xbox memory layout.\n"
                    "The required virtual address range may be unavailable.",
                    "Recomp", MB_ICONERROR);
        free(xbe_data);
        return 1;
    }

    g_xbox_mem_offset = xbox_GetMemoryOffset();
    printf("Xbox memory mapped. Offset: 0x%llX\n", (unsigned long long)g_xbox_mem_offset);
    tt_dlss5_init();
    if (getenv("RECOMP_REPLAY")) {
        extern int nv2a_replay(const char *path);
        prof_start();
        return nv2a_replay(getenv("RECOMP_REPLAY"));
    }

    /* Bring up the emulated APU.
     *
     * Gated on the same variable that unmaps its registers, because the two
     * halves are useless apart: a trap with no model declines every access,
     * and a model nothing traps into never sees a register. The APU walks
     * Xbox physical RAM to find voice buffers, so it gets the guest RAM
     * base. */
    if (getenv("RECOMP_AC97_READY")) {
        g_apu_state = mcpx_apu_init_standalone((uint8_t *)xbox_GetMemoryBase());
        fprintf(stderr, "[BOOT] emulated APU %s\n",
                g_apu_state ? "up" : "FAILED to initialise");
    }

    /* Emulated USB controllers, so the title's own XAPI driver can find a
     * pad (fed by XInput and, under RECOMP_KEYBOARD, the keyboard). */
    xbox_OhciInit();

    /* Step 3: Initialize Xbox kernel */
    printf("Initializing Xbox kernel replacement...\n");
    xbox_kernel_init();

    /* Step 4: Set game directory for file I/O path translation */
    {
        extern void xbox_path_init(const char *game_dir, const char *save_dir);
        xbox_path_init(YOUR_GAME_DIR, g_save_dir);
    }

    /* Step 5: Initialize kernel bridge (thunk table in Xbox memory) */
    printf("Initializing kernel bridge...\n");
    xbox_kernel_bridge_init();

    /* Step 6: Initialize stack */
    g_esp = XBOX_STACK_TOP;

    /*
     * TODO: Pre-initialize CRT globals if needed.
     *
     * Many Xbox games use the MSVC CRT. The CRT's __heap_init sets up a
     * heap descriptor at a game-specific address. You may need to
     * pre-initialize __active_heap to avoid small-block heap issues:
     *
     *   uint32_t *active_heap = (uint32_t *)((uint8_t *)g_xbox_mem_offset + ACTIVE_HEAP_VA);
     *   *active_heap = 1;  // 1 = system heap (HeapAlloc), avoids SBH init
     *
     * Find ACTIVE_HEAP_VA by searching the disassembly for __heap_init
     * or by looking for the CRT's __active_heap global in the data section.
     */

    printf("\n=== Initialization complete ===\n");
    printf("Entry point: 0x%08X\n", YOUR_GAME_ENTRY_POINT);
    printf("ESP: 0x%08X\n", g_esp);

    /* Build the flat dispatch table before the guest runs.
     *
     * Without this recomp_lookup falls back to a binary search over the
     * whole function table -- roughly log2(n) branches on *every*
     * indirect call, which for a 45,000-function C++ title is about 16
     * every time the game goes through a vtable. Half-Life 2 spent most
     * of its static initialisation inside recomp_lookup for exactly this
     * reason, and it read as a hang.
     *
     * Optional by design: if the allocation fails the search still works,
     * so a failure is worth one line and not a fatal error. */
    if (!recomp_dispatch_init())
        fprintf(stderr, "[BOOT] flat dispatch unavailable; "
                        "indirect calls will use the binary search\n");

    /* Arm the hang watchdog. Does nothing unless RECOMP_WATCHDOG_SECS is set,
     * and must be called from this thread -- the guest registers it samples are
     * thread-local, so it has to be handed the copies belonging to the thread
     * that runs guest code.
     *
     * Not optional boilerplate: without this call RECOMP_WATCHDOG_SECS is
     * silently inert, and the one diagnostic that tells a hang from slowness
     * does nothing while appearing to be set. */
    xbox_WatchdogStart();

    /* Step 7: Call the recompiled entry point */
    printf("\nStarting game...\n");
    prof_start();
#if defined(__linux__)
    { extern long long g_cpustat_tid; g_cpustat_tid = (long long)syscall(SYS_gettid); }
#endif
    { extern void tt_fps_init(void); tt_fps_init(); }
    { extern void tt_lang_init(void); tt_lang_init(); }   /* (1.9.2) the language: before the game opens a file */
    fflush(stdout);

    xbe_entry_point();

    printf("\nGame returned. Cleaning up...\n");

    /* Cleanup */
    xbox_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(xbe_data);

    return 0;
}

/* ── XBE Loading ───────────────────────────────────────────── */

static BOOL load_xbe(const char *path, void **out_data, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open XBE: %s\n", path);
        return FALSE;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return FALSE;
    }

    void *data = malloc((size_t)size);
    if (!data) {
        fclose(f);
        return FALSE;
    }

    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return FALSE;
    }

    fclose(f);
    *out_data = data;
    *out_size = (size_t)size;
    return TRUE;
}

#if !defined(_WIN32)
#include <pthread.h>
static void *tt_main_thread(void *p)
{
    (void)p;
    return (void *)(intptr_t)WinMain(NULL, NULL, (LPSTR)"", 0);
}
#endif
/* Console entry point (for debugging -- lets you see printf output) */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
#if defined(_WIN32)
    return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOW);
#else
    {
        /* The guest runs on this thread, and translated code nests deeply on
         * the native stack: the Windows build links a 64 MB stack; the main
         * thread here has whatever ulimit -s gives (often 8 MB). Run the game
         * on a thread with 64 MB instead. */
        pthread_attr_t at;
        pthread_t th;
        void *rv = NULL;
        pthread_attr_init(&at);
        pthread_attr_setstacksize(&at, 64u << 20);
        if (pthread_create(&th, &at, tt_main_thread, NULL) == 0) {
            pthread_join(th, &rv);
            return (int)(intptr_t)rv;
        }
        return WinMain(NULL, NULL, (LPSTR)"", 0);
    }
#endif
}
