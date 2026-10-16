/*
 * kernel_hal.c - Hardware Abstraction Layer
 *
 * Implements IRQL simulation, performance counters, system time,
 * processor stalls, bug checks, floating point state, and hardware stubs.
 *
 * The Xbox HAL provides low-level hardware access that doesn't exist on
 * a standard Windows PC. Most of these functions are either:
 *   - Directly mappable (perf counters, system time)
 *   - Simulated (IRQL tracking via TLS)
 *   - Stubbed (PCI access, SMC, interrupts)
 */

#include "kernel.h"
#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <intrin.h>
#endif
#include "xbox_memory_layout.h"
#if defined(_MSC_VER) && !defined(__clang__)
#pragma intrinsic(_ReturnAddress)
#define IRQL_CALLER() _ReturnAddress()
#else
#define IRQL_CALLER() __builtin_return_address(0)
#endif

/* ============================================================================
 * IRQL Simulation
 *
 * Xbox uses IRQL (Interrupt Request Level) for synchronization:
 *   PASSIVE_LEVEL (0) - normal thread execution
 *   APC_LEVEL (1) - APC delivery
 *   DISPATCH_LEVEL (2) - scheduler/DPC level, no page faults allowed
 *
 * On Windows, we simulate IRQL with a thread-local variable. Raising to
 * DISPATCH_LEVEL doesn't actually prevent preemption, but the tracking
 * allows code that checks IRQL to function correctly.
 * ============================================================================ */

static XBOX_THREAD_LOCAL KIRQL g_current_irql = PASSIVE_LEVEL;

/* The guest reads its IRQL from the KPCR: fs:[0x24]. DirectSound's lock
 * helper (0x00128C07) skips its critical section when that byte says
 * DISPATCH_LEVEL -- its DPC runs with the section possibly held by the
 * thread it interrupted. With the byte always 0 the DPC, run from the timer
 * thread under the dispatch lock, waited for a game thread that was waiting
 * for the dispatch lock: the multi-second freezes when the sound interrupt
 * was on (v10/v11). Mirror the level there. */
static void irql_mirror(KIRQL level)
{
    if (g_fs_base)
        *(volatile uint8_t *)((uintptr_t)xbox_GetMemoryOffset() + g_fs_base + 0x24) = level;
}

/* How many threads are holding IRQL at or above DISPATCH_LEVEL.
 *
 * The level itself is per-thread, which is right for a guest that asks "what
 * is my IRQL". It is wrong for the question a device model has to answer:
 * raising IRQL on hardware masks the interrupt for the whole processor, and
 * the title raises it precisely to keep an ISR out of structures it is in the
 * middle of editing. With the level thread-local, a controller thread sees
 * PASSIVE_LEVEL, calls the ISR anyway, and the two race over exactly the
 * state the guest was protecting -- which surfaces as an intermittent fault
 * on a garbage pointer, far from the code that dropped it.
 *
 * A count rather than a flag, because several threads can be raised at once
 * and the last one out is what re-opens the gate. */
static volatile LONG g_irql_raised_count = 0;

/* Non-zero while any thread is at or above DISPATCH_LEVEL. Device models call
 * this before delivering an interrupt; OHCI and the NV2A are level-triggered,
 * so a deferred interrupt is delivered on the next poll rather than lost. */
int xbox_IrqlBlocksInterrupts(void)
{
    return InterlockedCompareExchange(&g_irql_raised_count, 0, 0) != 0;
}

/* The raw depth, for callers that want to report it. A count that only ever
 * grows is a leak somewhere in the raise/lower pairs, and the number says so
 * where a yes/no cannot. */
int xbox_IrqlRaisedCount(void)
{
    return (int)InterlockedCompareExchange(&g_irql_raised_count, 0, 0);
}

static int  s_trace = -1;
static volatile LONG s_traced = 0;

/* Every crossing of the DISPATCH boundary, ever.
 *
 * The depth on its own cannot tell a leaked raise from a guest that is
 * genuinely sitting there: both read non-zero for as long as you look. A
 * count that stops moving says the first; one that races says the second.
 * That distinction is the whole difference between "the title is busy" and
 * "no device will ever get an interrupt again". */
static volatile LONG s_transitions = 0;

int xbox_IrqlTransitions(void)
{
    return (int)InterlockedCompareExchange(&s_transitions, 0, 0);
}

/* Which threads are holding the boundary up, and where they raised.
 *
 * A depth that stops changing has to be attributed to code, and the raise
 * that did it happened seconds earlier on a thread that has since gone
 * quiet -- there is nothing left to look at by the time anyone notices.
 * Keeping the caller's address per raised thread turns the number into a
 * name: these are host addresses inside the recompiled image, so nm resolves
 * them to the generated function, which is the guest function. */
#define IRQL_HOLDERS 8
static struct { volatile LONG tid; void *ra; } s_holders[IRQL_HOLDERS];

static void irql_holder_add(void *ra)
{
    LONG me = (LONG)GetCurrentThreadId();
    int i;

    for (i = 0; i < IRQL_HOLDERS; i++)
        if (InterlockedCompareExchange(&s_holders[i].tid, me, 0) == 0) {
            s_holders[i].ra = ra;
            return;
        }
}

static void irql_holder_drop(void)
{
    LONG me = (LONG)GetCurrentThreadId();
    int i;

    for (i = 0; i < IRQL_HOLDERS; i++)
        if (InterlockedCompareExchange(&s_holders[i].tid, 0, me) == me)
            return;
}

void xbox_IrqlDumpHolders(void)
{
    int i;

    fprintf(stderr, "  [IRQLHOLD] depth=%d transitions=%d, raised threads:\n",
            xbox_IrqlRaisedCount(), xbox_IrqlTransitions());
    for (i = 0; i < IRQL_HOLDERS; i++) {
        LONG t = InterlockedCompareExchange(&s_holders[i].tid, 0, 0);
        if (t)
            fprintf(stderr, "  [IRQLHOLD]   tid %lu raised from host %p\n",
                    (unsigned long)t, s_holders[i].ra);
    }
    fflush(stderr);
}

/* One processor.
 *
 * On the console, a thread that raises to DISPATCH_LEVEL cannot be preempted
 * by another thread or a DPC until it lowers again; that is how DirectSound,
 * XAPI and the title's own code protect state shared with their DPCs and
 * worker threads. Here every guest thread is a host thread, so a per-thread
 * IRQL protected nothing (The Two Towers: DirectSound objects corrupted
 * between its worker and the main thread, then a call through a data
 * pointer). Crossing into DISPATCH_LEVEL now takes a process-wide lock that
 * DPC and timer delivery also take, which restores the exclusion the code was
 * written against.
 *
 * Bounded, as the interrupt gate below is: a leaked raise on one thread must
 * not freeze every other one forever, so a raise that waits too long goes
 * ahead without the lock and says so once. RECOMP_CPU_LOCK=0 turns it off. */
void xbox_IrqlDumpHolders(void);
static CRITICAL_SECTION g_cpu_lock;
static INIT_ONCE g_cpu_lock_once = INIT_ONCE_STATIC_INIT;
static XBOX_THREAD_LOCAL int g_cpu_lock_held;
static int g_cpu_lock_mode = -1;

static BOOL CALLBACK cpu_lock_init(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_cpu_lock);
    return TRUE;
}

void xbox_CpuLock(void)
{
    int i;

    if (g_cpu_lock_mode < 0) {
        const char *v = getenv("RECOMP_CPU_LOCK");
        g_cpu_lock_mode = (v && v[0] == '0') ? 0 : 1;
    }
    if (!g_cpu_lock_mode)
        return;
    InitOnceExecuteOnce(&g_cpu_lock_once, cpu_lock_init, NULL, NULL);
    for (i = 0; i < 2000; i++) {                 /* ~2 s */
        if (TryEnterCriticalSection(&g_cpu_lock)) {
            g_cpu_lock_held++;
            return;
        }
        Sleep(i < 50 ? 0 : 1);
    }
    {
        /* Go ahead without it this once. Three times over, and it is a leaked
         * raise rather than a slow holder: stop waiting on it altogether. */
        static volatile LONG warned, timeouts;
        if (InterlockedIncrement(&timeouts) >= 3)
            g_cpu_lock_mode = 0;
        if (!InterlockedExchange(&warned, 1)) {
            extern volatile int g_timer_phase;
            extern void xbox_KernelCallsDump(void);
            fprintf(stderr, "  [CPU] dispatch lock held for over 2 s by another "
                            "thread; going ahead without it (timer thread phase %d)\n", g_timer_phase);
            xbox_IrqlDumpHolders();
            xbox_KernelCallsDump();
            fflush(stderr);
        }
    }
}

void xbox_CpuUnlock(void)
{
    if (g_cpu_lock_held > 0) {
        g_cpu_lock_held--;
        LeaveCriticalSection(&g_cpu_lock);
    }
}

/* Lets go of the dispatch lock -- every hold this thread has on it -- for a
 * moment and takes it back. For code at DISPATCH_LEVEL that has to wait for
 * a guest lock held by a thread at PASSIVE_LEVEL: on the console that thread
 * could not be running, here it is, and it may need the dispatch lock itself
 * before it lets the guest lock go. */
void xbox_CpuLockYield(void)
{
    int n = g_cpu_lock_held, k;
    for (k = 0; k < n; k++) xbox_CpuUnlock();
    Sleep(0);
    for (k = 0; k < n; k++) xbox_CpuLock();
}

/* For a guest lock helper that skips its critical section at DISPATCH_LEVEL
 * (the title's own "a DPC cannot block" rule): take the section anyway, since
 * here the DPC runs beside the thread holding it rather than in its place,
 * letting the dispatch lock go while waiting. Gives up after max_ms (the
 * holder may be waiting on this very DPC) and returns 0: the caller then goes
 * ahead unlocked, as on the console. 1 when taken. */
int xbox_DpcLockGuestCs(uint32_t cs_va, int max_ms)
{
    extern BOOLEAN __stdcall xbox_RtlTryEnterCriticalSection(PRTL_CRITICAL_SECTION CriticalSection);
    PRTL_CRITICAL_SECTION cs = (PRTL_CRITICAL_SECTION)((uintptr_t)xbox_GetMemoryOffset() + cs_va);
    DWORD t0 = GetTickCount();
    while (!xbox_RtlTryEnterCriticalSection(cs)) {
        if ((int)(GetTickCount() - t0) > max_ms) {
            static volatile LONG warned;
            if (!InterlockedExchange(&warned, 1)) {
                fprintf(stderr, "  [DPC] went ahead without guest lock 0x%08X (held over %d ms)\n",
                        cs_va, max_ms);
                fflush(stderr);
            }
            return 0;
        }
        xbox_CpuLockYield();
    }
    return 1;
}

/* RECOMP_IRQL_HIST: each thread's last IRQL changes, printed with an
 * [IRQLBUG], so an unpaired raise/lower can be traced to the guest functions
 * that made it (host addresses, relative to the exe: resolve with nm). */
extern XBOX_THREAD_LOCAL void *g_kernel_host_ra;
#define IRQL_HIST 24
static XBOX_THREAD_LOCAL struct { DWORD t; uint8_t o, n; void *ra; uint32_t g[3]; } s_ih[IRQL_HIST];
extern XBOX_THREAD_LOCAL uint32_t g_esp;
static XBOX_THREAD_LOCAL unsigned s_ih_n;
static void irql_hist(KIRQL o, KIRQL n)
{
    unsigned i = s_ih_n++ % IRQL_HIST;
    s_ih[i].t = GetTickCount(); s_ih[i].o = o; s_ih[i].n = n; s_ih[i].ra = g_kernel_host_ra;
    {   /* guest return addresses on the guest stack: the calls that led here */
        const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset();
        uint32_t sp = g_esp, k, f = 0;
        s_ih[i].g[0] = s_ih[i].g[1] = s_ih[i].g[2] = 0;
        for (k = 0; sp && k < 96 && f < 3; k++) {
            uint32_t v = *(const uint32_t *)(m + sp + k * 4);
            if (v >= 0x00011000u && v < 0x00178000u) s_ih[i].g[f++] = v;
        }
    }
}
static void irql_hist_dump(void)
{
    unsigned i = s_ih_n > IRQL_HIST ? s_ih_n - IRQL_HIST : 0;
    DWORD now = GetTickCount();
    for (; i < s_ih_n; i++) {
        unsigned k = i % IRQL_HIST;
        fprintf(stderr, "  [IRQLHIST]   -%4lu ms %d -> %d guest stack %08X %08X %08X\n", (unsigned long)(now - s_ih[k].t),
                s_ih[k].o, s_ih[k].n, s_ih[k].g[0], s_ih[k].g[1], s_ih[k].g[2]);
    }
}

static void irql_track(KIRQL old_level, KIRQL new_level, void *ra)
{
    int was = (old_level >= DISPATCH_LEVEL);
    int now = (new_level >= DISPATCH_LEVEL);
    LONG d;

    if (now == was)
        return;

    if (now) xbox_CpuLock(); else xbox_CpuUnlock();
    d = now ? InterlockedIncrement(&g_irql_raised_count)
            : InterlockedDecrement(&g_irql_raised_count);
    s_transitions++;                 /* statistics only */
    if (now)
        irql_holder_add(ra);
    else
        irql_holder_drop();

    /* RECOMP_IRQL_TRACE prints the first few transitions. The pairing is what
     * matters: a raise to 2 followed by a lower from 2 nets out, and a lower
     * whose old level is not the level the raise set is a calling-convention
     * bug upstream of here, not a title doing something exotic. */
    if (s_trace < 0)
        s_trace = getenv("RECOMP_IRQL_TRACE") ? 1 : 0;
    if (s_trace && InterlockedIncrement(&s_traced) <= 20) {
        fprintf(stderr, "  [IRQL] tid %lu %s %d->%d depth=%ld\n",
                (unsigned long)GetCurrentThreadId(),
                now ? "raise" : "lower", old_level, new_level, (long)d);
        fflush(stderr);
    }
}

/*
 * KfRaiseIrql - Raises IRQL to the specified level.
 * Returns the previous IRQL. Uses __fastcall (ECX = NewIrql).
 */
KIRQL __fastcall xbox_KfRaiseIrql(KIRQL NewIrql)
{
    KIRQL old = g_current_irql;
    irql_hist(old, NewIrql);

    if (NewIrql < old) {
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL,
            "KfRaiseIrql: attempt to lower IRQL from %d to %d (use KfLowerIrql)",
            old, NewIrql);
    }

    irql_track(old, NewIrql, IRQL_CALLER());
    g_current_irql = NewIrql;
    irql_mirror(NewIrql);
    return old;
}

/*
 * KfLowerIrql - Lowers IRQL to the specified level.
 * Uses __fastcall (ECX = NewIrql).
 */
VOID __fastcall xbox_KfLowerIrql(KIRQL NewIrql)
{
    irql_hist(g_current_irql, NewIrql);
    if (NewIrql > g_current_irql) {
        /* A lower that raises the count.
         *
         * irql_track works on the edge, so this call crosses the boundary
         * upwards and increments. The pair that would bring it back down has
         * already happened, so the depth is now permanently one too high --
         * and the count is what every device model reads before delivering.
         * One of these ends interrupts for the rest of the run.
         *
         * Loud rather than a filtered warning because of that consequence:
         * the symptom is a device going silent minutes later, with nothing
         * connecting it back to here. */
        static volatile LONG n;
        if (InterlockedIncrement(&n) <= 20) {
            extern XBOX_THREAD_LOCAL uint32_t g_xbox_kernel_caller;
            fprintf(stderr, "  [IRQLBUG] tid %lu KfLowerIrql(%d) while at %d"
                    " (guest call site %08X) -- ignored\n",
                    (unsigned long)GetCurrentThreadId(),
                    (int)NewIrql, (int)g_current_irql, g_xbox_kernel_caller);
            irql_hist_dump();
            fflush(stderr);
        }
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL,
            "KfLowerIrql: attempt to raise IRQL from %d to %d (use KfRaiseIrql)",
            g_current_irql, NewIrql);
    }
    /* ...and then ignored: it is the pair of a raise this thread never made
     * (DPC code run at PASSIVE lowering "back" to DISPATCH), and honouring
     * it left the global depth -- and the dispatch lock -- held for good. */
    if (NewIrql > g_current_irql)
        return;

    irql_track(g_current_irql, NewIrql, IRQL_CALLER());
    g_current_irql = NewIrql;
    irql_mirror(NewIrql);
}

/*
 * KeRaiseIrqlToDpcLevel - Convenience function to raise to DISPATCH_LEVEL.
 */
KIRQL __stdcall xbox_KeRaiseIrqlToDpcLevel(void)
{
    KIRQL old = g_current_irql;
    irql_hist(old, DISPATCH_LEVEL);

    irql_track(old, DISPATCH_LEVEL, IRQL_CALLER());
    g_current_irql = DISPATCH_LEVEL;
    irql_mirror(DISPATCH_LEVEL);
    return old;
}

/* ============================================================================
 * KeTickCount
 *
 * Exported as a data pointer, not a function. The Xbox kernel increments
 * this every ~1ms (approximating the Xbox tick interval).
 * Updated lazily when read, using GetTickCount.
 * ============================================================================ */

volatile ULONG xbox_KeTickCount = 0;

/* Call this periodically or on-demand to update KeTickCount */
static void xbox_update_tick_count(void)
{
    xbox_KeTickCount = GetTickCount();
}

/* ============================================================================
 * Performance Counters
 *
 * Direct 1:1 mapping to Win32 QueryPerformanceCounter/Frequency.
 * Both Xbox and Windows return LARGE_INTEGER.
 * ============================================================================ */

LARGE_INTEGER __stdcall xbox_KeQueryPerformanceCounter(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter;
}

LARGE_INTEGER __stdcall xbox_KeQueryPerformanceFrequency(void)
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return freq;
}

/* ============================================================================
 * System Time
 *
 * KeQuerySystemTime returns the current time as a FILETIME (100ns since
 * January 1, 1601). Direct Win32 mapping.
 * ============================================================================ */

static LONGLONG s_time_anchor_100ns;
static LONGLONG s_time_anchor_counts;
static LONGLONG s_time_freq_counts;
static INIT_ONCE s_time_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK anchor_system_time(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    FILETIME ft;
    LARGE_INTEGER f, now;
    (void)once; (void)param; (void)ctx;
    QueryPerformanceFrequency(&f);
    GetSystemTimeAsFileTime(&ft);
    QueryPerformanceCounter(&now);
    s_time_freq_counts = f.QuadPart ? f.QuadPart : 1;
    s_time_anchor_100ns = ((LONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    s_time_anchor_counts = now.QuadPart;
    return TRUE;
}

VOID __stdcall xbox_KeQuerySystemTime(PLARGE_INTEGER CurrentTime)
{
    /* Anchored once to the wall clock, advanced by the performance counter.
     *
     * GetSystemTimeAsFileTime alone moves in steps of about 15.6 ms, the
     * host's scheduler tick. The console's clock is far finer, and a title
     * that busy-waits on this -- reading it until enough time has passed --
     * spins for the whole of each step instead of a few iterations.
     *
     * Measured on Shin Megami Tensei: Nine: one such wait called this
     * **11.8 million times in two seconds**, which is most of what the
     * title was doing at that moment, and it came out of the spin in a
     * state where it no longer polled the gamepad.
     *
     * The anchor keeps the absolute value right; the counter supplies the
     * resolution between ticks.
     *
     * Whole seconds and the remainder are scaled separately: scaling the
     * whole count by 10^7 overflows after about a day at 10 MHz. */
    LARGE_INTEGER now;
    LONGLONG delta;

    if (!CurrentTime)
        return;

    InitOnceExecuteOnce(&s_time_once, anchor_system_time, NULL, NULL);
    QueryPerformanceCounter(&now);
    delta = now.QuadPart - s_time_anchor_counts;
    CurrentTime->QuadPart = s_time_anchor_100ns
        + (delta / s_time_freq_counts) * 10000000LL
        + (delta % s_time_freq_counts) * 10000000LL / s_time_freq_counts;
}

/* ============================================================================
 * Processor Stall
 *
 * KeStallExecutionProcessor performs a busy-wait for the given number
 * of microseconds. Used for hardware timing (e.g., waiting for GPU).
 * ============================================================================ */

VOID __stdcall xbox_KeStallExecutionProcessor(ULONG MicroSeconds)
{
    LARGE_INTEGER freq, start, now;

    if (MicroSeconds == 0)
        return;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    LONGLONG target_counts = (freq.QuadPart * MicroSeconds) / 1000000;

    do {
        QueryPerformanceCounter(&now);
    } while ((now.QuadPart - start.QuadPart) < target_counts);
}

/* ============================================================================
 * Floating Point State
 *
 * Xbox kernel requires saving/restoring FP state when kernel code uses
 * floating point. On Windows user-mode this is handled automatically by
 * the OS, so these are no-ops.
 * ============================================================================ */

NTSTATUS __stdcall xbox_KeSaveFloatingPointState(PVOID FloatingPointState)
{
    (void)FloatingPointState;
    /* No-op: Windows user-mode preserves FP state across context switches */
    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_KeRestoreFloatingPointState(PVOID FloatingPointState)
{
    (void)FloatingPointState;
    return STATUS_SUCCESS;
}

/* ============================================================================
 * Bug Check (Blue Screen of Death)
 *
 * KeBugCheck/KeBugCheckEx are the Xbox equivalent of BSOD. In our
 * recompilation, we log the error and terminate the process.
 * ============================================================================ */

VOID __stdcall xbox_KeBugCheck(ULONG BugCheckCode)
{
    xbox_log(XBOX_LOG_ERROR, XBOX_LOG_HAL,
        "*** KeBugCheck: code=0x%08X ***", BugCheckCode);

#ifdef _DEBUG
    DebugBreak();
#endif

    ExitProcess(BugCheckCode);
}

VOID __stdcall xbox_KeBugCheckEx(
    ULONG BugCheckCode,
    ULONG_PTR Param1,
    ULONG_PTR Param2,
    ULONG_PTR Param3,
    ULONG_PTR Param4)
{
    xbox_log(XBOX_LOG_ERROR, XBOX_LOG_HAL,
        "*** KeBugCheckEx: code=0x%08X, params=(0x%p, 0x%p, 0x%p, 0x%p) ***",
        BugCheckCode, (void*)Param1, (void*)Param2, (void*)Param3, (void*)Param4);

#ifdef _DEBUG
    DebugBreak();
#endif

    ExitProcess(BugCheckCode);
}

/* ============================================================================
 * HAL PCI Access
 *
 * HalReadWritePCISpace reads/writes PCI configuration space. The Xbox uses
 * this for GPU and southbridge setup. Not needed on Windows - stub it.
 * ============================================================================ */

VOID __stdcall xbox_HalReadWritePCISpace(
    ULONG BusNumber,
    ULONG SlotNumber,
    ULONG RegisterNumber,
    PVOID Buffer,
    ULONG Length,
    BOOLEAN WritePCISpace)
{
    (void)BusNumber;
    (void)SlotNumber;
    (void)RegisterNumber;
    (void)Length;
    (void)WritePCISpace;

    /* Return zeroed buffer for reads */
    if (!WritePCISpace && Buffer)
        memset(Buffer, 0, Length);

    xbox_log(XBOX_LOG_TRACE, XBOX_LOG_HAL,
        "HalReadWritePCISpace: bus=%u slot=%u reg=0x%X len=%u %s (stubbed)",
        BusNumber, SlotNumber, RegisterNumber, Length,
        WritePCISpace ? "WRITE" : "READ");
}

/* ============================================================================
 * HAL Firmware & Shutdown
 *
 * HalReturnToFirmware returns to the Xbox dashboard. For us, this means
 * exit the game cleanly.
 * ============================================================================ */

VOID __stdcall xbox_HalReturnToFirmware(ULONG Routine)
{
    xbox_log(XBOX_LOG_INFO, XBOX_LOG_HAL,
        "HalReturnToFirmware: routine=%u (exiting)", Routine);
    ExitProcess(0);
}

VOID __stdcall xbox_HalInitiateShutdown(void)
{
    xbox_log(XBOX_LOG_INFO, XBOX_LOG_HAL, "HalInitiateShutdown (exiting)");
    ExitProcess(0);
}

BOOLEAN __stdcall xbox_HalIsResetOrShutdownPending(void)
{
    return FALSE;
}

/* ============================================================================
 * SMC (System Management Controller)
 *
 * HalReadSMCTrayState reads the DVD tray state. No disc tray on PC.
 * ============================================================================ */

ULONG __stdcall xbox_HalReadSMCTrayState(PULONG TrayState, PULONG TrayStateChangeCount)
{
    /* Tray state: 0x10 = media detected (disc present) */
    if (TrayState)
        *TrayState = 0x10;
    if (TrayStateChangeCount)
        *TrayStateChangeCount = 0;
    return 0; /* Success */
}

/* ============================================================================
 * Software Interrupts
 *
 * Used for APC/DPC delivery on Xbox. Stubbed since we don't have real
 * interrupt-driven DPC delivery.
 * ============================================================================ */

VOID __stdcall xbox_HalClearSoftwareInterrupt(KIRQL RequestIrql)
{
    (void)RequestIrql;
}

VOID __stdcall xbox_HalRequestSoftwareInterrupt(KIRQL RequestIrql)
{
    (void)RequestIrql;
}

VOID __stdcall xbox_HalDisableSystemInterrupt(ULONG BusInterruptLevel, KIRQL Irql)
{
    (void)BusInterruptLevel;
    (void)Irql;
}

ULONG __stdcall xbox_HalGetInterruptVector(ULONG BusInterruptLevel, PKIRQL Irql)
{
    (void)BusInterruptLevel;
    if (Irql)
        *Irql = PASSIVE_LEVEL;
    return 0;
}

/* ============================================================================
 * Interrupt Objects
 *
 * Used by DSOUND and other drivers for hardware interrupt handling.
 * Since we replace the audio/graphics subsystems entirely, these are stubs.
 * ============================================================================ */

VOID __stdcall xbox_KeInitializeInterrupt(
    PXBOX_KINTERRUPT Interrupt,
    PVOID ServiceRoutine,
    PVOID ServiceContext,
    ULONG Vector,
    KIRQL Irql,
    ULONG InterruptMode,
    BOOLEAN ShareVector)
{
    (void)Vector;
    (void)InterruptMode;
    (void)ShareVector;

    if (!Interrupt)
        return;

    Interrupt->ServiceRoutine = ServiceRoutine;
    Interrupt->ServiceContext = ServiceContext;
    Interrupt->Irql = Irql;
    Interrupt->Connected = FALSE;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_HAL,
        "KeInitializeInterrupt: interrupt=%p, routine=%p, vector=%u",
        Interrupt, ServiceRoutine, Vector);
}

BOOLEAN __stdcall xbox_KeConnectInterrupt(PXBOX_KINTERRUPT Interrupt)
{
    if (!Interrupt)
        return FALSE;

    Interrupt->Connected = TRUE;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_HAL,
        "KeConnectInterrupt: interrupt=%p (stubbed - no real HW interrupts)",
        Interrupt);

    return TRUE;
}

BOOLEAN __stdcall xbox_KeDisconnectInterrupt(PXBOX_KINTERRUPT Interrupt)
{
    BOOLEAN was_connected;

    if (!Interrupt)
        return FALSE;

    /* Returns the PREVIOUS connected state, not success. */
    was_connected = Interrupt->Connected;
    Interrupt->Connected = FALSE;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_HAL,
        "KeDisconnectInterrupt: interrupt=%p was_connected=%d",
        Interrupt, (int)was_connected);

    return was_connected;
}

/* ============================================================================
 * Miscellaneous Port I/O Stubs
 * ============================================================================ */

VOID __stdcall xbox_WRITE_PORT_BUFFER_ULONG(PULONG Port, PULONG Buffer, ULONG Count)
{
    (void)Port;
    (void)Buffer;
    (void)Count;
}

VOID __stdcall xbox_WRITE_PORT_BUFFER_USHORT(PUSHORT Port, PUSHORT Buffer, ULONG Count)
{
    (void)Port;
    (void)Buffer;
    (void)Count;
}

/* ============================================================================
 * System Time (Set)
 *
 * NtSetSystemTime - we don't actually change the system clock, just log it.
 * ============================================================================ */

NTSTATUS __stdcall xbox_NtSetSystemTime(PLARGE_INTEGER SystemTime, PLARGE_INTEGER PreviousTime)
{
    if (PreviousTime)
        GetSystemTimeAsFileTime((LPFILETIME)PreviousTime);

    xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL,
        "NtSetSystemTime: ignored (not setting system clock)");

    return STATUS_SUCCESS;
}

/* ============================================================================
 * Display / AV
 *
 * These are declared in kernel.h for the thunk table but will be fully
 * implemented by the D3D replacement layer. We provide realistic AV pack
 * detection so games can query display capabilities (480p, 720p, widescreen).
 * ============================================================================ */

static ULONG g_av_saved_data_address = 0;
static ULONG g_av_display_mode = 0;

ULONG __stdcall xbox_AvGetSavedDataAddress(void)
{
    return g_av_saved_data_address;
}

VOID __stdcall xbox_AvSendTVEncoderOption(
    PVOID RegisterBase, ULONG Option, ULONG Param, PULONG Result)
{
    (void)RegisterBase;
    (void)Param;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_HAL,
        "AvSendTVEncoderOption: option=0x%02X param=0x%X", Option, Param);

    if (!Result)
        return;

    switch (Option) {
    case AV_OPTION_QUERY_AVPACK:
        /* Pack type, video standard and refresh rate, in one word.
         *
         * D3D keys its display-mode table on all three: the row flags carry
         * the pack in 0x000000FF, the standard in 0x0000FF00 and the refresh
         * in 0x00C00000 (Half-Life 2's 640x480 60Hz row is 0x00480104).
         * Returning the pack alone left the standard as 0, which matches no
         * row, so the mode scan ran off the end of the table and device
         * creation failed with E_FAIL.
         *
         * HDTV pack keeps 480p/720p available to titles that offer them;
         * NTSC-M and 60Hz are the North American retail default, and match
         * the region reported by ExQueryNonVolatileSetting. */
        *Result = AV_PACK_HDTV
                | (AV_STANDARD_NTSC_M << AV_STANDARD_SHIFT)
                | AV_REFRESH_60Hz;
        break;

    case AV_OPTION_QUERY_MODE:
        /* Return current display mode */
        *Result = g_av_display_mode;
        break;

    case AV_OPTION_QUERY_AV_CAPABILITIES:
        /* Report support for 480i, 480p, 720p, and widescreen */
        *Result = AV_FLAGS_HDTV_480i | AV_FLAGS_HDTV_480p
                | AV_FLAGS_HDTV_720p | AV_FLAGS_WIDESCREEN
                | AV_FLAGS_60Hz;
        break;

    case AV_OPTION_QUERY_ENCODER_TYPE:
        /* Conexant CX25871 (common in retail Xboxes) */
        *Result = 4;
        break;

    case AV_OPTION_QUERY_MODE_CAPS:
        /* Same as capabilities for our purposes */
        *Result = AV_FLAGS_HDTV_480i | AV_FLAGS_HDTV_480p
                | AV_FLAGS_HDTV_720p | AV_FLAGS_WIDESCREEN
                | AV_FLAGS_60Hz;
        break;

    case AV_OPTION_SET_MODE:
        g_av_display_mode = Param;
        *Result = 0;
        break;

    case AV_OPTION_BLANK_SCREEN:
    case AV_OPTION_MACROVISION_MODE:
    case AV_OPTION_FLICKER_FILTER:
    case AV_OPTION_ZERO_MODE:
        *Result = 0;
        break;

    default:
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL,
            "AvSendTVEncoderOption: unknown option 0x%02X", Option);
        *Result = 0;
        break;
    }
}

VOID __stdcall xbox_AvSetSavedDataAddress(ULONG Address)
{
    g_av_saved_data_address = Address;
}

VOID __stdcall xbox_AvSetDisplayMode(
    PVOID RegisterBase, ULONG Step, ULONG Mode,
    ULONG Format, ULONG Pitch, ULONG FrameBuffer)
{
    (void)RegisterBase;
    (void)Step;
    (void)Format;
    (void)Pitch;
    (void)FrameBuffer;

    g_av_display_mode = Mode;

    xbox_log(XBOX_LOG_INFO, XBOX_LOG_HAL,
        "AvSetDisplayMode: step=%u mode=0x%X format=0x%X pitch=%u fb=0x%X",
        Step, Mode, Format, Pitch, FrameBuffer);
}

/* ============================================================================
 * SMBus - HalReadSMBusValue / HalWriteSMBusValue
 *
 * The Xbox SMBus connects the CPU to the System Management Controller (SMC),
 * EEPROM, temperature sensor, and TV encoder. Games use these to detect
 * AV pack type, read EEPROM settings, and check hardware state.
 *
 * We simulate responses for the most commonly queried devices:
 *   - SMC (0x20): firmware version, tray state, AV pack, temperatures
 *   - EEPROM (0xA8): handled separately via ExQueryNonVolatileSetting
 *   - Temperature sensor (0x98): CPU/board temperatures
 * ============================================================================ */

NTSTATUS __stdcall xbox_HalReadSMBusValue(
    UCHAR SlaveAddress, UCHAR CommandCode, BOOLEAN ReadWordValue, PULONG DataValue)
{
    if (!DataValue)
        return STATUS_INVALID_PARAMETER;

    *DataValue = 0;

    switch (SlaveAddress) {
    case SMC_SLAVE_ADDRESS:  /* 0x20 - System Management Controller */
        switch (CommandCode) {
        case SMC_CMD_FIRMWARE_VER:
            /* "P01" = production SMC, return 'P' for first byte.
             * Games read version byte-by-byte: P(0x50), 0(0x30), 1(0x31) */
            *DataValue = 0x50; /* 'P' */
            break;
        case SMC_CMD_TRAY_STATE:
            /* 0x60 = media present, tray closed */
            *DataValue = 0x60;
            break;
        case SMC_CMD_AV_PACK:
            /* HDTV/Component pack */
            *DataValue = AV_PACK_HDTV;
            break;
        case SMC_CMD_CPU_TEMP:
            *DataValue = 40; /* 40 degrees C */
            break;
        case SMC_CMD_MB_TEMP:
            *DataValue = 35; /* 35 degrees C */
            break;
        case SMC_CMD_FAN_SPEED:
            *DataValue = 50; /* ~50% fan speed */
            break;
        case SMC_CMD_INTERRUPT_REASON:
            *DataValue = 0;  /* No pending interrupt */
            break;
        case SMC_CMD_ERROR_CODE:
            *DataValue = 0;  /* No error */
            break;
        default:
            xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_HAL,
                "HalReadSMBusValue: SMC unknown cmd=0x%02X", CommandCode);
            break;
        }
        break;

    case TEMP_SLAVE_ADDRESS:  /* 0x98 - ADM1032 temperature sensor */
        /* CommandCode 0x00 = local temp, 0x01 = remote temp */
        if (CommandCode == 0x00)
            *DataValue = 35;  /* Board: 35C */
        else if (CommandCode == 0x01)
            *DataValue = 40;  /* CPU: 40C */
        else
            *DataValue = 30;
        break;

    case ENCODER_SLAVE_ADDRESS:  /* 0xD4 - TV encoder */
        /* Return 0 for most encoder register reads */
        *DataValue = 0;
        break;

    default:
        xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_HAL,
            "HalReadSMBusValue: unknown slave=0x%02X cmd=0x%02X",
            SlaveAddress, CommandCode);
        break;
    }

    xbox_log(XBOX_LOG_TRACE, XBOX_LOG_HAL,
        "HalReadSMBusValue: slave=0x%02X cmd=0x%02X word=%d -> 0x%X",
        SlaveAddress, CommandCode, ReadWordValue, *DataValue);

    (void)ReadWordValue;
    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_HalWriteSMBusValue(
    UCHAR SlaveAddress, UCHAR CommandCode, BOOLEAN WriteWordValue, ULONG DataValue)
{
    (void)WriteWordValue;

    xbox_log(XBOX_LOG_TRACE, XBOX_LOG_HAL,
        "HalWriteSMBusValue: slave=0x%02X cmd=0x%02X word=%d val=0x%X (ignored)",
        SlaveAddress, CommandCode, WriteWordValue, DataValue);

    /* Writes to SMC (LED control, fan speed, etc.) are silently accepted */
    return STATUS_SUCCESS;
}

/* ============================================================================
 * HAL Data Exports
 *
 * Ordinals 40, 41 and 42 are variables, not functions. Games read them
 * directly through the thunk table, so the thunk must hand back the address
 * of real storage -- pointing these at a function is what produced garbage
 * disk metadata before.
 *
 * The strings are counted (Length/MaximumLength), not NUL-terminated, matching
 * the kernel's STRING type. Values describe the virtual disk we present; no
 * real hardware is queried.
 * ============================================================================ */

ULONG xbox_HalDiskCachePartitionCount = 3;

static char g_disk_model[]  = "XBOXRECOMP VIRTUAL HDD";
static char g_disk_serial[] = "XR0000000000";

XBOX_ANSI_STRING xbox_HalDiskModelNumber = {
    sizeof(g_disk_model) - 1,
    sizeof(g_disk_model) - 1,
    g_disk_model
};

XBOX_ANSI_STRING xbox_HalDiskSerialNumber = {
    sizeof(g_disk_serial) - 1,
    sizeof(g_disk_serial) - 1,
    g_disk_serial
};

/*
 * Video mode the SMC reported at boot. 0 lets title code fall back to querying
 * the AV pack, which we answer properly in AvGetSavedDataAddress/SMBus.
 */
ULONG xbox_HalBootSMCVideoMode = 0;

/*
 * IDE channel object. Real kernels export a device object for the ATA channel;
 * drivers only ever pass it back to us, so identity is all that is required.
 */
static ULONG g_idex_channel_data = 0x49444558; /* 'IDEX' */
PVOID xbox_IdexChannelObject = &g_idex_channel_data;

/* ============================================================================
 * Shutdown Notification
 * ============================================================================ */

VOID __stdcall xbox_HalRegisterShutdownNotification(
    PVOID ShutdownRegistration,
    BOOLEAN Register)
{
    /*
     * Registers a callback to run on reboot/shutdown. We never initiate an
     * Xbox-style shutdown -- HalReturnToFirmware terminates the process -- so
     * the callback would never fire. Recorded in the log so a title relying on
     * shutdown cleanup is visible rather than silently ignored.
     */
    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_HAL,
        "HalRegisterShutdownNotification: %s registration=%p (never invoked)",
        Register ? "register" : "unregister", ShutdownRegistration);
}

/* ============================================================================
 * Unknown Ordinal Stubs
 * ============================================================================ */

VOID __stdcall xbox_Unknown_8(void)
{
    xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL, "Unknown ordinal 8 called (stubbed)");
}

VOID __stdcall xbox_Unknown_23(void)
{
    xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL, "Unknown ordinal 23 called (stubbed)");
}

VOID __stdcall xbox_Unknown_42(void)
{
    xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL, "Unknown ordinal 42 called (stubbed)");
}

/* ============================================================================
 * Debug / Timing
 * ============================================================================ */

VOID __stdcall xbox_DbgBreakPoint(void)
{
    /*
     * Titles call this from assertion paths. Under a debugger this should
     * break; without one, raising a breakpoint exception would terminate the
     * process on a condition the title may well survive. Log loudly and
     * continue, and only actually break when a debugger is attached to catch it.
     */
    xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL, "DbgBreakPoint called by title");

    if (IsDebuggerPresent())
        DebugBreak();
}

ULONGLONG __stdcall xbox_KeQueryInterruptTime(void)
{
    /*
     * Time since boot in NT 100ns units. GetTickCount64 is milliseconds, so
     * scale by 10,000. Resolution is coarser than the real kernel's, but it is
     * monotonic, which is the property callers actually depend on.
     */
    return (ULONGLONG)GetTickCount64() * 10000ULL;
}

/* ============================================================================
 * Time stamp counter
 *
 * Xbox's QueryPerformanceCounter is a bare `rdtsc`, and its
 * QueryPerformanceFrequency returns the CPU clock as a constant the title
 * compiles in: Half-Life 2's is 0x2BB5C755 (733,333,333 Hz) at 0x0059C6C7.
 * So a frame timer computes seconds as counter / 733333333.
 *
 * Returning the host's own TSC would make that division wrong by the ratio of
 * the two clocks -- a 3.5 GHz host would have the guest believe nearly five
 * seconds had passed for every real one. Scaling the host's performance
 * counter to the console's rate keeps the guest's arithmetic honest.
 *
 * Monotonic and shared by every thread, which is what a TSC is. The first
 * call establishes the origin so the counter starts near zero rather than at
 * whatever the host had been running for.
 * ========================================================================= */
#define XBOX_TSC_HZ 733333333ull

/* EFLAGS as pushfd sees it. Only what software can observe without the
 * lifter's flag tracking: the reserved bit 1, IF, DF (kept in g_df) and
 * whatever a popfd put in the ID/AC bits -- which is all a CPUID-availability
 * probe needs to see its write stick. */
extern RECOMP_TLS int g_df;
static RECOMP_TLS uint32_t s_eflags_image = 0x00000202u;

uint32_t recomp_eflags_read(void)
{
    return (s_eflags_image & ~0x400u) | (g_df ? 0x400u : 0u) | 0x2u;
}

void recomp_eflags_write(uint32_t value)
{
    s_eflags_image = value;
    g_df = (value >> 10) & 1;
}

/* The console's CPU: a 733 MHz Pentium III (Coppermine), family 6 model 8. */
void recomp_cpuid(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    switch (*a) {
    case 0:
        *a = 2;
        *b = 0x756E6547u; *d = 0x49656E69u; *c = 0x6C65746Eu;  /* GenuineIntel */
        break;
    case 1:
        *a = 0x00000683u;                 /* family 6, model 8, stepping 3 */
        *b = 0;
        *c = 0;
        /* FPU VME DE PSE TSC MSR PAE MCE CX8 SEP MTRR PGE MCA CMOV PAT PSE36
         * MMX FXSR SSE */
        *d = 0x0383F9FFu;
        break;
    case 2:
        *a = 0x03020101u; *b = 0; *c = 0; *d = 0x0C040843u;
        break;
    default:
        *a = *b = *c = *d = 0;
        break;
    }
}

uint64_t xbox_ReadTimeStampCounter(void)
{
    static LARGE_INTEGER freq;
    static LARGE_INTEGER origin;
    LARGE_INTEGER now;

    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&origin);
        if (freq.QuadPart == 0)
            freq.QuadPart = 1;
    }
    QueryPerformanceCounter(&now);

    {
        uint64_t ticks = (uint64_t)(now.QuadPart - origin.QuadPart);
        {   /* testing: RECOMP_TSC_SCALE=f -- the guest's clock at f times real time
             * (below 1: each slow frame here is a short one to the game, as on a
             * fast PC) */
            static double sc = -1, after;   /* (RECOMP_TSC_SCALE_AFTER=s: real time first, scaled from then on) */
            if (sc < 0) {
                const char *e = getenv("RECOMP_TSC_SCALE"), *a2 = getenv("RECOMP_TSC_SCALE_AFTER");
                sc = e ? atof(e) : 0; if (!(sc > 0)) sc = 0;
                after = a2 ? atof(a2) * (double)freq.QuadPart : 0;
            }
            if (sc > 0 && (double)ticks > after) ticks = (uint64_t)(after + ((double)ticks - after) * sc);
        }
        /* Split the scaling so a long run cannot overflow: whole seconds
         * first, then the remainder. */
        uint64_t secs = ticks / (uint64_t)freq.QuadPart;
        uint64_t rem  = ticks % (uint64_t)freq.QuadPart;
        return secs * XBOX_TSC_HZ
             + (rem * XBOX_TSC_HZ) / (uint64_t)freq.QuadPart;
    }
}

/* getenv, remembered. The CRT's getenv takes a lock and walks the whole
 * environment; diagnostics switches read with it on every pushbuffer method
 * and every APU fetch were a measurable share of the frame. Keyed on the
 * literal's address (every caller passes one), compared by name on a miss. */
const char *recomp_cgetenv(const char *name)
{
    /* Open-addressed on the pointer: the linear scan of 128 entries was
     * itself 2-3% of a replayed frame. */
    static struct { const char *volatile key; const char *val; } t[1024];
    uintptr_t h = ((uintptr_t)name >> 3) * 2654435761u;
    unsigned i, k;
    for (i = 0; i < 1024; i++) {
        k = (unsigned)((h + i) & 1023);
        if (t[k].key == name) return t[k].val;
        if (!t[k].key) break;
    }
    {
        static volatile LONG lock;
        const char *v = getenv(name);
        if (i < 1024 && !InterlockedExchange(&lock, 1)) {
            /* Inserts are rare (once per name): one at a time. */
            for (i = 0; i < 1024; i++) {
                k = (unsigned)((h + i) & 1023);
                if (t[k].key == name) break;
                if (!t[k].key) { t[k].val = v; MemoryBarrier(); t[k].key = name; break; }
            }
            InterlockedExchange(&lock, 0);
        }
        return v;
    }
}
