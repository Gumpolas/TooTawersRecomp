/*
 * kernel_thread.c - Xbox Threading Subsystem
 *
 * Implements Xbox thread creation, termination, delays, and priority
 * management using Win32 threading APIs.
 *
 * Xbox threading model:
 *   - PsCreateSystemThreadEx creates kernel-mode threads (→ CreateThread)
 *   - Thread start routines are __stdcall with a single PVOID context
 *   - Time intervals use NT 100-nanosecond units (negative = relative)
 *   - Thread priorities use NT KPRIORITY increments
 */

#if !defined(_WIN32)
#include <time.h>
#endif
#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include "kernel.h"
#include "xbox_memory_layout.h"   /* XBOX_WORKER_STACK_* + worker-stack decls */
#include <mmsystem.h>

/* ============================================================================
 * Thread Start Wrapper
 *
 * Xbox start routines are __stdcall void(*)(PVOID), but Win32 CreateThread
 * expects DWORD WINAPI (*)(LPVOID). We wrap the Xbox routine to bridge
 * the calling convention and return type.
 * ============================================================================ */

typedef struct _XBOX_THREAD_START_INFO {
    PXBOX_SYSTEM_ROUTINE StartRoutine;
    PVOID                StartContext;
} XBOX_THREAD_START_INFO;

static DWORD WINAPI xbox_thread_wrapper(LPVOID lpParameter)
{
    XBOX_THREAD_START_INFO info = *(XBOX_THREAD_START_INFO*)lpParameter;

    /* Free the start info before calling the routine - the routine may
     * never return (calling PsTerminateSystemThread instead) */
    HeapFree(GetProcessHeap(), 0, lpParameter);

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD, "Thread %u starting at %p",
        GetCurrentThreadId(), info.StartRoutine);

    info.StartRoutine(info.StartContext);

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD, "Thread %u returned normally",
        GetCurrentThreadId());

    return 0;
}

/* ============================================================================
 * PsCreateSystemThreadEx
 *
 * Xbox signature:
 *   PsCreateSystemThreadEx(
 *     OUT PHANDLE ThreadHandle,
 *     IN ULONG ThreadExtraSize,      // extra bytes in thread object (ignored)
 *     IN ULONG KernelStackSize,      // stack size (0 = default)
 *     IN ULONG TlsDataSize,          // TLS data size (Xbox-specific, ignored)
 *     OUT PULONG ThreadId,           // optional thread ID
 *     IN PVOID StartContext1,        // context passed to StartRoutine
 *     IN PVOID StartContext2,        // alternate context (unused by game code)
 *     IN BOOLEAN CreateSuspended,
 *     IN BOOLEAN DebugStack,         // debug stack (ignored)
 *     IN PXBOX_SYSTEM_ROUTINE StartRoutine
 *   )
 *
 * Maps to: CreateThread with a wrapper for calling convention adaptation.
 * ============================================================================ */

NTSTATUS __stdcall xbox_PsCreateSystemThreadEx(
    PHANDLE ThreadHandle,
    ULONG ThreadExtraSize,
    ULONG KernelStackSize,
    ULONG TlsDataSize,
    PULONG ThreadId,
    PVOID StartContext1,
    PVOID StartContext2,
    BOOLEAN CreateSuspended,
    BOOLEAN DebugStack,
    PXBOX_SYSTEM_ROUTINE StartRoutine)
{
    XBOX_THREAD_START_INFO* info;
    HANDLE hThread;
    DWORD dwThreadId;
    DWORD dwCreationFlags;

    (void)ThreadExtraSize;
    (void)TlsDataSize;
    (void)StartContext2;
    (void)DebugStack;

    if (!ThreadHandle || !StartRoutine)
        return STATUS_INVALID_PARAMETER;

    /* Allocate start info - freed by the wrapper thread */
    info = (XBOX_THREAD_START_INFO*)HeapAlloc(GetProcessHeap(), 0, sizeof(XBOX_THREAD_START_INFO));
    if (!info)
        return STATUS_NO_MEMORY;

    info->StartRoutine = StartRoutine;
    info->StartContext = StartContext1;

    dwCreationFlags = CreateSuspended ? CREATE_SUSPENDED : 0;

    /* Use default stack size if 0 (Xbox default is 64KB) */
    if (KernelStackSize == 0)
        KernelStackSize = 65536;

    hThread = CreateThread(NULL, KernelStackSize, xbox_thread_wrapper, info,
                           dwCreationFlags, &dwThreadId);
    if (hThread && getenv("RECOMP_GUEST_AFFINITY")) SetThreadAffinityMask(hThread, 1);
    if (!hThread) {
        HeapFree(GetProcessHeap(), 0, info);
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_THREAD,
            "PsCreateSystemThreadEx: CreateThread failed (error %u)", GetLastError());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *ThreadHandle = hThread;
    if (ThreadId)
        *ThreadId = dwThreadId;

    xbox_log(XBOX_LOG_INFO, XBOX_LOG_THREAD,
        "PsCreateSystemThreadEx: created thread %u (handle=%p, routine=%p, suspended=%d)",
        dwThreadId, hThread, StartRoutine, CreateSuspended);

    return STATUS_SUCCESS;
}

/* ============================================================================
 * PsTerminateSystemThread
 *
 * Terminates the calling thread. On Xbox this is the standard way for
 * system threads to exit. Maps directly to ExitThread.
 * ============================================================================ */

NTSTATUS __stdcall xbox_PsTerminateSystemThread(NTSTATUS ExitStatus)
{
    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "PsTerminateSystemThread: thread %u exiting with status 0x%08X",
        GetCurrentThreadId(), ExitStatus);

    ExitThread((DWORD)ExitStatus);

    /* ExitThread never returns, but the compiler needs this */
    return STATUS_SUCCESS;
}

/* ============================================================================
 * KeDelayExecutionThread
 *
 * Delays the current thread. The interval uses NT 100-nanosecond units:
 *   - Negative values = relative delay (most common)
 *   - Positive values = absolute time (rare)
 *   - Zero = yield
 *
 * Maps to: SleepEx (for alertable waits) or Sleep
 * ============================================================================ */


/* Sleep with sub-millisecond accuracy where Windows offers it.
 *
 * Sleep() is at the mercy of the system timer resolution: 15.6 ms unless
 * timeBeginPeriod was honoured, and on Windows 11 that request is not
 * guaranteed for every process. A high-resolution waitable timer (Windows 10
 * 1803+) wakes within about half a millisecond regardless. Falls back to
 * Sleep when it cannot be created (Wine, older Windows). */
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
static XBOX_THREAD_LOCAL HANDLE t_hr_timer;
static XBOX_THREAD_LOCAL int t_hr_state;   /* 0 untried, 1 ok, -1 unavailable */
DWORD xbox_hr_sleep_100ns(LONGLONG t, BOOL alertable)
{
    if (t <= 0) { SwitchToThread(); return 0; }
    if (t > 200000)      /* long waits: precision does not matter, plain sleep */
        return SleepEx((DWORD)((t + 9999) / 10000), alertable);
    if (!t_hr_state && getenv("RECOMP_NO_HR")) t_hr_state = -1;
    if (!t_hr_state) {
        t_hr_timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        t_hr_state = t_hr_timer ? 1 : -1;
    }
    if (t_hr_state > 0) {
        LARGE_INTEGER due;
        due.QuadPart = -t;
        if (SetWaitableTimer(t_hr_timer, &due, 0, NULL, NULL, FALSE)) {
            /* Bounded: a timer that never fires (seen under Wine with
             * thousands of short waits a second from other threads) must not
             * stop this thread for good -- the timer thread delivers vblank,
             * and losing it froze the game. */
            DWORD r = WaitForSingleObjectEx(t_hr_timer, (DWORD)(t / 10000) + 5, alertable);
            if (r == WAIT_TIMEOUT) {
                static volatile LONG stalls;
                CancelWaitableTimer(t_hr_timer);
                if (InterlockedIncrement(&stalls) <= 5) {
                    fprintf(stderr, "  [TIMER] a %.2f ms wait did not end on time (thread %lu)\n",
                            t / 10000.0, (unsigned long)GetCurrentThreadId());
                    fflush(stderr);
                }
            }
            return r == WAIT_IO_COMPLETION ? WAIT_IO_COMPLETION : 0;
        }
    }
    {
        DWORD ms = (DWORD)((t + 9999) / 10000);
        return SleepEx(ms, alertable);
    }
}

/* Sleep until a performance-counter deadline, precisely.
 *
 * The timer thread is the emulated machine's clock (vblank, sound IRQ, GPU
 * callbacks, DPCs). On some PCs the OS sleeps run long -- a log showed a
 * "1 ms" high-resolution wait taking 3.3 ms and Sleep(1) 5 ms, and the timer
 * thread ticking at 50 Hz instead of 500 Hz, which made loads crawl. Sleep
 * for the part of the interval we can trust (the measured overshoot is
 * learned), then yield-spin to the deadline. */
void xbox_precise_wait_until(LONGLONG deadline)
{
    static LARGE_INTEGER fq;
    static LONGLONG over = 0;            /* learned sleep overshoot, QPC ticks */
    LARGE_INTEGER now;
    if (!fq.QuadPart) { QueryPerformanceFrequency(&fq); over = fq.QuadPart / 1000; }
    QueryPerformanceCounter(&now);
    {
        LONGLONG rem = deadline - now.QuadPart, margin = over + fq.QuadPart / 4000;   /* + 0.25 ms */
        if (rem > margin) {
            LONGLONG want = rem - margin, t0 = now.QuadPart, took;
            xbox_hr_sleep_100ns(want * 10000000 / fq.QuadPart, FALSE);
            QueryPerformanceCounter(&now);
            took = now.QuadPart - t0;
            /* overshoot = how much longer than asked; average it */
            over = (over * 7 + (took > want ? took - want : 0)) / 8;
            if (over > fq.QuadPart / 50) over = fq.QuadPart / 50;   /* cap 20 ms */
        }
    }
    while (now.QuadPart < deadline) {
        LONGLONG rem = deadline - now.QuadPart;
        if (rem > fq.QuadPart / 5000) SwitchToThread(); else YieldProcessor();
        QueryPerformanceCounter(&now);
    }
}

/* (1.9.3) The timer thread's tick wait, ended early when a guest thread
 * waiting on the GPU finds a D3D callback queued (xbox_PbWakeIfPending sets
 * *poke and signals wake). Returns 1 when woken that way. */
void xbox_hr_wait_or_wake(LONGLONG t, HANDLE wake);
int xbox_precise_wait_until_or(LONGLONG deadline, volatile LONG *poke, HANDLE wake)
{
    static LARGE_INTEGER fq;
    static LONGLONG over = 0;
    LARGE_INTEGER now;
    if (!fq.QuadPart) { QueryPerformanceFrequency(&fq); over = fq.QuadPart / 1000; }
    if (*poke) return 1;
    QueryPerformanceCounter(&now);
    {
        LONGLONG rem = deadline - now.QuadPart, margin = over + fq.QuadPart / 4000;
        if (rem > margin) {
            LONGLONG want = rem - margin, t0 = now.QuadPart, took;
            xbox_hr_wait_or_wake(want * 10000000 / fq.QuadPart, wake);
            QueryPerformanceCounter(&now);
            if (*poke) return 1;
            took = now.QuadPart - t0;
            over = (over * 7 + (took > want ? took - want : 0)) / 8;
            if (over > fq.QuadPart / 50) over = fq.QuadPart / 50;
        }
    }
    while (now.QuadPart < deadline) {
        LONGLONG rem = deadline - now.QuadPart;
        if (*poke) return 1;
        if (rem > fq.QuadPart / 5000) SwitchToThread(); else YieldProcessor();
        QueryPerformanceCounter(&now);
    }
    return 0;
}

/* The timer thread's wait: the usual ~2 ms tick, cut short when the GPU
 * executor queues a D3D callback so the game's "GPU reached the fence" wait
 * ends as soon as the GPU really gets there instead of up to a tick later. */
void xbox_hr_wait_or_wake(LONGLONG t, HANDLE wake)
{
    static HANDLE tm; static int st;
    if (!st) {
        tm = getenv("RECOMP_NO_HR") ? NULL
           : CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!tm) tm = CreateWaitableTimerW(NULL, FALSE, NULL);
        st = tm ? 1 : -1;
    }
    if (st > 0 && wake) {
        LARGE_INTEGER due; HANDLE hs[2];
        due.QuadPart = -t;
        if (SetWaitableTimer(tm, &due, 0, NULL, NULL, FALSE)) {
            hs[0] = wake; hs[1] = tm;
            WaitForMultipleObjects(2, hs, FALSE, (DWORD)(t / 10000) + 5);
            return;
        }
    }
    xbox_hr_sleep_100ns(t, FALSE);
}

/* Who sleeps, and how much: the guest call sites (first three code
 * addresses on the guest stack) of KeDelayExecutionThread, counted, with the
 * total time asked for. Printed and reset by xbox_DelayReport (the game's
 * watchdog calls it every 20 s). Cheap: a small open-addressed table. */
#define DN_SLOTS 64
static struct { uint32_t a[3]; uint32_t n, zero; long long t100, us; } s_dn[DN_SLOTS];
static CRITICAL_SECTION s_dn_cs; static int s_dn_init;
static int xbox_delay_note(long long interval)
{
    int slot = -1;
    extern RECOMP_TLS uint32_t g_esp;
    const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t a[3] = {0, 0, 0}, i, k = 0, h;
    if (!s_dn_init) { InitializeCriticalSection(&s_dn_cs); s_dn_init = 1; }
    for (i = 0; i < 128 && k < 3; i++) {
        uint32_t w = *(const uint32_t *)(m + g_esp + i * 4);
        if (w >= 0x11000 && w < 0x170000) a[k++] = w;
    }
    h = (a[0] * 2654435761u ^ a[1] * 40503u ^ a[2]) % DN_SLOTS;
    EnterCriticalSection(&s_dn_cs);
    for (i = 0; i < DN_SLOTS; i++) {
        uint32_t q = (h + i) % DN_SLOTS;
        if (!s_dn[q].n || (s_dn[q].a[0] == a[0] && s_dn[q].a[1] == a[1] && s_dn[q].a[2] == a[2])) {
            memcpy(s_dn[q].a, a, sizeof a);
            s_dn[q].n++;
            if (interval == 0) s_dn[q].zero++;
            s_dn[q].t100 += interval < 0 ? -interval : 0;
            slot = (int)q;
            break;
        }
    }
    LeaveCriticalSection(&s_dn_cs);
    return slot;
}
static void xbox_delay_done(int slot, LARGE_INTEGER t0)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER t1;
    if (slot < 0) return;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t1);
    EnterCriticalSection(&s_dn_cs);
    s_dn[slot].us += (t1.QuadPart - t0.QuadPart) * 1000000 / f.QuadPart;
    LeaveCriticalSection(&s_dn_cs);
}
void xbox_DelayReport(void)
{
    int top, j;
    if (!s_dn_init) return;
    EnterCriticalSection(&s_dn_cs);
    fprintf(stderr, "  [SLEEPS] guest sleeps in the last period (calls, of which 0-length, ms asked, ms actually spent; call sites):\n");
    for (top = 0; top < 8; top++) {
        int best = -1;
        for (j = 0; j < DN_SLOTS; j++)
            if (s_dn[j].n && (best < 0 || s_dn[j].n > s_dn[best].n)) best = j;
        if (best < 0) break;
        fprintf(stderr, "  [SLEEPS] %8u %8u %8lld %8lld  %06X %06X %06X\n", s_dn[best].n, s_dn[best].zero,
                s_dn[best].t100 / 10000, s_dn[best].us / 1000, s_dn[best].a[0], s_dn[best].a[1], s_dn[best].a[2]);
        s_dn[best].n = 0;
    }
    memset(s_dn, 0, sizeof s_dn);
    LeaveCriticalSection(&s_dn_cs);
}

static NTSTATUS kdelay_inner(KPROCESSOR_MODE WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Interval);
/* A title hook run at every guest sleep (the game's frame-rate options). */
void (*volatile xbox_guest_delay_hook)(void);
long long g_cpustat_tid;
volatile long long g_cpustat_pb_ns;
/* this thread's processor time, ns (testing: TT_CPUSTAT) */
long long xbox_thread_cpu_ns(void)
{
#if defined(_WIN32)
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) return 0;
    return (long long)((((unsigned long long)k.dwHighDateTime << 32) | k.dwLowDateTime) + (((unsigned long long)u.dwHighDateTime << 32) | u.dwLowDateTime)) * 100;
#else
    struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
#endif
}   /* (the thread that runs the game's entry: twotowers main.c) */
NTSTATUS __stdcall xbox_KeDelayExecutionThread(
    KPROCESSOR_MODE WaitMode,
    BOOLEAN Alertable,
    PLARGE_INTEGER Interval)
{
    LARGE_INTEGER t0;
    int slot;
    NTSTATUS r;
    if (!Interval)
        return STATUS_INVALID_PARAMETER;
    if (xbox_guest_delay_hook) xbox_guest_delay_hook();
    { extern void xbox_PbWakeIfPending(void); xbox_PbWakeIfPending(); }
    slot = xbox_delay_note(Interval->QuadPart);
    QueryPerformanceCounter(&t0);
    {   /* testing: TT_CPUSTAT=1 -- the game thread's processor time a frame
         * outside its sleeps and the GPU command runs, every 5 s */
        static int on = -1;
        extern volatile DWORD xbox_perf_game_tid;
        if (on < 0) on = getenv("TT_CPUSTAT") != NULL;
        if (on && xbox_perf_game_tid && GetCurrentThreadId() == xbox_perf_game_tid) {
            extern volatile unsigned g_nv2a_flips;
            extern long long xbox_thread_cpu_ns(void);
            static long long in_sleep, c_last, t_last, pb_last; static unsigned f_last;
            long long a = xbox_thread_cpu_ns(), b;
            r = kdelay_inner(WaitMode, Alertable, Interval);
            b = xbox_thread_cpu_ns();
            in_sleep += b - a;
            {
                long long wall = (long long)GetTickCount() / 1000;
                unsigned fl = g_nv2a_flips;
                if (!t_last) { t_last = wall; c_last = b - in_sleep; f_last = fl; pb_last = g_cpustat_pb_ns; }
                else if (wall - t_last >= 5 && fl != f_last) {
                    double work = (double)(b - in_sleep - c_last) / 1e6, pb = (double)(g_cpustat_pb_ns - pb_last) / 1e6;
                    fprintf(stderr, "  [CPUSTAT] game thread: %.3f ms a frame of processor time outside sleeps (%.3f of it running GPU commands), %u frames\n",
                            work / (fl - f_last), pb / (fl - f_last), fl - f_last);
                    t_last = wall; c_last = b - in_sleep; f_last = fl; pb_last = g_cpustat_pb_ns;
                }
            }
            xbox_delay_done(slot, t0);
            {
                extern volatile long long xbox_perf_wait_ticks;
                LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
                xbox_perf_wait_ticks += t1.QuadPart - t0.QuadPart;
            }
            return r;
        }
    }
    r = kdelay_inner(WaitMode, Alertable, Interval);
    xbox_delay_done(slot, t0);
    {   /* (1.9.5) [PERF]: the game's frame thread waiting */
        extern volatile DWORD xbox_perf_game_tid;
        extern volatile long long xbox_perf_wait_ticks;
        if (xbox_perf_game_tid && GetCurrentThreadId() == xbox_perf_game_tid) {
            LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
            xbox_perf_wait_ticks += t1.QuadPart - t0.QuadPart;
        }
    }
    return r;
}
static NTSTATUS kdelay_inner(KPROCESSOR_MODE WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Interval)
{
    DWORD ms;

    (void)WaitMode;

    static int dtrace = -1;
    if (dtrace < 0) dtrace = getenv("RECOMP_DELAY_TRACE") != NULL;
    if (dtrace) {
        static volatile LONG n;
        LONG k = InterlockedIncrement(&n);
        if (k < 60 || k % 2000 == 0) {
            extern RECOMP_TLS uint32_t g_esp;
            const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset();
            uint32_t sp = g_esp, i, shown = 0;
            fprintf(stderr, "  [DELAY] tid %lu alertable %d interval %lld stack:", (unsigned long)GetCurrentThreadId(), Alertable, (long long)Interval->QuadPart);
            for (i = 0; i < 256 && shown < 12; i++) {
                uint32_t w = *(const uint32_t *)(m + sp + i * 4);
                if (w >= 0x11000 && w < 0x170000) { fprintf(stderr, " %06X", w); shown++; }
            }
            fprintf(stderr, "\n");
        }
    }

    if (Interval->QuadPart == 0) {
        /* Zero interval = yield the thread's time slice.
         *
         * The Two Towers waits for its vblank and GPU flags by calling this
         * in a loop -- millions of times a second, a whole core held at 100%
         * (and on a laptop, heat the CPU then throttles for). After the
         * first few hundred yields in a row a thread sleeps 0.25 ms per
         * call instead: the flags still land within a fraction of a
         * vblank, at a small fraction of the power. RECOMP_SPIN_YIELD=1
         * keeps pure yielding. */
        static int spin = -1;
        static XBOX_THREAD_LOCAL unsigned run;
        static XBOX_THREAD_LOCAL LONGLONG last;
        LARGE_INTEGER now;
        static LARGE_INTEGER fq;
        if (spin < 0) spin = getenv("RECOMP_SPIN_YIELD") != NULL;
        if (!fq.QuadPart) QueryPerformanceFrequency(&fq);
        QueryPerformanceCounter(&now);
        if (now.QuadPart - last > fq.QuadPart / 500) run = 0;   /* > 2 ms since the last: a new wait */
        last = now.QuadPart;
        /* Only while short sleeps really are short: on a PC whose timer
         * stays coarse (seen: a "0.25 ms" wait taking 4-15 ms) each one
         * cost a large part of a frame -- gameplay fell to 12 fps. The
         * running average of what the sleeps actually took decides. */
        static volatile LONG coarse_avg_us = 300;
        if (!spin && ++run > 200 && coarse_avg_us < 900) {
            LONGLONG t0 = now.QuadPart, us;
            xbox_hr_sleep_100ns(2500, FALSE);
            QueryPerformanceCounter(&now);
            us = (now.QuadPart - t0) * 1000000 / fq.QuadPart;
            coarse_avg_us = (LONG)((coarse_avg_us * 15 + (us > 5000 ? 5000 : us)) / 16);
            last = now.QuadPart;
            return STATUS_SUCCESS;
        }
        SwitchToThread();
        return STATUS_SUCCESS;
    }

    if (Interval->QuadPart < 0) {
        /* Negative = relative time in 100ns units. Convert to milliseconds. */
        LONGLONG relative_100ns = -Interval->QuadPart;
        ms = (DWORD)(relative_100ns / 10000);
        /* Ensure at least 1ms for very short intervals */
        if (ms == 0 && relative_100ns > 0)
            ms = 1;
    } else {
        /* Positive = absolute time. Calculate relative delay from now. */
        LARGE_INTEGER now;
        GetSystemTimeAsFileTime((LPFILETIME)&now);
        LONGLONG diff = Interval->QuadPart - now.QuadPart;
        if (diff <= 0)
            return STATUS_SUCCESS; /* Already past */
        ms = (DWORD)(diff / 10000);
    }

    /* Windows rounds Sleep up to the timer resolution -- 15.6 ms unless
     * someone asked for better -- and a title's 1 ms poll loop then runs 15x
     * slower than on the Xbox. Ask for 1 ms once, and finish short waits
     * against the performance counter instead of a whole timer tick. */
    {
        static int period_set;
        if (!period_set) { period_set = 1; timeBeginPeriod(1); }
    }
    {
        /* Same length the old Sleep(ms) gave, ms truncated to a DWORD as
         * before: a title's "forever" interval has to keep behaving as it
         * did (a finite wait), or the thread it wakes never runs. */
        LONGLONG want = (Interval->QuadPart < 0 && -Interval->QuadPart / 10000 <= 0xFFFFFFFFLL)
                        ? -Interval->QuadPart : (LONGLONG)ms * 10000;
        if (xbox_hr_sleep_100ns(want, Alertable) == WAIT_IO_COMPLETION)
            return STATUS_ALERTED;
    }
    return STATUS_SUCCESS;
}

/* ============================================================================
 * Thread Priority
 *
 * Xbox uses NT KPRIORITY base priority increments relative to the process.
 * We map these to Win32 thread priority levels.
 * ============================================================================ */

/*
 * Map Xbox priority increment to Win32 priority level.
 * Xbox base priorities typically range from -2 to +2 for game threads.
 */
static int xbox_priority_to_win32(LONG increment)
{
    if (increment <= -15)       return THREAD_PRIORITY_IDLE;
    else if (increment <= -2)   return THREAD_PRIORITY_LOWEST;
    else if (increment == -1)   return THREAD_PRIORITY_BELOW_NORMAL;
    else if (increment == 0)    return THREAD_PRIORITY_NORMAL;
    else if (increment == 1)    return THREAD_PRIORITY_ABOVE_NORMAL;
    else if (increment <= 2)    return THREAD_PRIORITY_HIGHEST;
    else                        return THREAD_PRIORITY_TIME_CRITICAL;
}

static LONG win32_priority_to_xbox(int priority)
{
    switch (priority) {
        case THREAD_PRIORITY_IDLE:          return -15;
        case THREAD_PRIORITY_LOWEST:        return -2;
        case THREAD_PRIORITY_BELOW_NORMAL:  return -1;
        case THREAD_PRIORITY_NORMAL:        return 0;
        case THREAD_PRIORITY_ABOVE_NORMAL:  return 1;
        case THREAD_PRIORITY_HIGHEST:       return 2;
        case THREAD_PRIORITY_TIME_CRITICAL: return 15;
        default:                            return 0;
    }
}

LONG __stdcall xbox_KeSetBasePriorityThread(PVOID Thread, LONG Increment)
{
    HANDLE hThread = (HANDLE)Thread;
    LONG previous;

    /* Get previous priority before setting new one */
    int prev_win32 = GetThreadPriority(hThread);
    previous = win32_priority_to_xbox(prev_win32);

    int new_win32 = xbox_priority_to_win32(Increment);
    SetThreadPriority(hThread, new_win32);

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "KeSetBasePriorityThread: thread=%p, increment=%d (win32=%d), prev=%d",
        Thread, Increment, new_win32, previous);

    return previous;
}

LONG __stdcall xbox_KeQueryBasePriorityThread(PVOID Thread)
{
    HANDLE hThread = (HANDLE)Thread;
    int win32_priority = GetThreadPriority(hThread);
    return win32_priority_to_xbox(win32_priority);
}

/* ============================================================================
 * KeAlertThread
 *
 * Sends an alert to a thread, which can wake it from an alertable wait.
 * On Xbox, this sets the alerted flag on the thread object.
 * We approximate this with QueueUserAPC using a no-op APC routine.
 * ============================================================================ */

static VOID CALLBACK xbox_alert_apc(ULONG_PTR dwParam)
{
    (void)dwParam;
    /* No-op - the purpose is just to wake the thread from alertable wait */
}

NTSTATUS __stdcall xbox_KeAlertThread(PVOID Thread, KPROCESSOR_MODE AlertMode)
{
    HANDLE hThread = (HANDLE)Thread;

    (void)AlertMode;

    if (!QueueUserAPC(xbox_alert_apc, hThread, 0)) {
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_THREAD,
            "KeAlertThread: QueueUserAPC failed (error %u)", GetLastError());
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

/* ============================================================================
 * NtYieldExecution
 *
 * Yields the current thread's remaining time slice.
 * Maps directly to SwitchToThread.
 * ============================================================================ */

NTSTATUS __stdcall xbox_NtYieldExecution(void)
{
    SwitchToThread();
    return STATUS_SUCCESS;
}

/* ============================================================================
 * NtDuplicateObject
 *
 * Duplicates a kernel handle. On Xbox this is simpler than Win32 since
 * there's only one process. Maps to DuplicateHandle within the same process.
 * ============================================================================ */

NTSTATUS __stdcall xbox_NtDuplicateObject(
    HANDLE SourceHandle,
    PHANDLE TargetHandle,
    ULONG Options)
{
    HANDLE hProcess = GetCurrentProcess();
    DWORD dwOptions = 0;

    if (!TargetHandle)
        return STATUS_INVALID_PARAMETER;

    /* Xbox DUPLICATE_CLOSE_SOURCE = 0x1, same as Win32 */
    if (Options & 0x1)
        dwOptions |= DUPLICATE_CLOSE_SOURCE;
    /* Xbox DUPLICATE_SAME_ACCESS = 0x2, same as Win32 */
    if (Options & 0x2)
        dwOptions |= DUPLICATE_SAME_ACCESS;

    if (!DuplicateHandle(hProcess, SourceHandle, hProcess, TargetHandle,
                         0, FALSE, dwOptions)) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_THREAD,
            "NtDuplicateObject: DuplicateHandle failed (error %u)", GetLastError());
        return STATUS_UNSUCCESSFUL;
    }

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "NtDuplicateObject: source=%p → target=%p (options=0x%X)",
        SourceHandle, *TargetHandle, Options);

    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtSuspendThread(
    HANDLE ThreadHandle,
    PULONG PreviousSuspendCount)
{
    DWORD prev;

    /*
     * SuspendThread returns the previous suspend count, or (DWORD)-1 on
     * failure. The Xbox call reports that count through an out-parameter, so
     * the two are not interchangeable: -1 must become an error status rather
     * than a suspend count of 0xFFFFFFFF.
     */
    prev = SuspendThread(ThreadHandle);
    if (prev == (DWORD)-1) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_THREAD,
            "NtSuspendThread: SuspendThread failed (error %u)", GetLastError());
        return STATUS_UNSUCCESSFUL;
    }

    if (PreviousSuspendCount)
        *PreviousSuspendCount = (ULONG)prev;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "NtSuspendThread: thread=%p previous_count=%u", ThreadHandle, prev);

    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtResumeThread(
    HANDLE ThreadHandle,
    PULONG PreviousSuspendCount)
{
    DWORD prev;

    /* Mirror of NtSuspendThread: (DWORD)-1 is failure, not a count. */
    prev = ResumeThread(ThreadHandle);
    if (prev == (DWORD)-1) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_THREAD,
            "NtResumeThread: ResumeThread failed (error %u)", GetLastError());
        return STATUS_UNSUCCESSFUL;
    }

    if (PreviousSuspendCount)
        *PreviousSuspendCount = (ULONG)prev;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "NtResumeThread: thread=%p previous_count=%u", ThreadHandle, prev);

    return STATUS_SUCCESS;
}


/* ================================================================
 * Worker stack slices + game-thread tracking (host-tick-driven titles)
 * ================================================================
 *
 * Ported from the Burnout 3 fork as the runtimes reunite. A host-driven title
 * returns from its entry after spawning an init thread and expects the host's
 * own thread to drive the per-frame tick; to call recompiled code from there it
 * needs a guest stack, which a worker slice provides. Additive: a default-model
 * title never calls any of this, so it is inert for Halo, Crimson Skies, etc.
 * See docs/technical/burnout3-reunification.md and XBOX_WORKER_STACK_* in
 * xbox_memory_layout.h.
 */

/* The game's own thread, kept so a wedged boot can be inspected from the host
 * watchdog. Set when a title's game thread is spawned; NULL under the default
 * inline model, where there is no separate thread to sample. */
static HANDLE g_game_thread = NULL;

void  xbox_set_game_thread(void *h) { g_game_thread = (HANDLE)h; }
void *xbox_thread_debug_handle(void) { return (void *)g_game_thread; }

/* One bit per worker stack slice. Interlocked because a host-driven title can
 * allocate a slice from the host thread while recompiled code allocates one for
 * a spawned worker, so the allocator itself must be thread-safe. */
static volatile LONG g_worker_stack_used[XBOX_WORKER_STACK_COUNT];

int xbox_worker_stack_alloc(void)
{
    int i;
    for (i = 0; i < XBOX_WORKER_STACK_COUNT; i++) {
        if (InterlockedCompareExchange(&g_worker_stack_used[i], 1, 0) == 0)
            return i;
    }
    return -1;  /* all slices in use */
}

void xbox_worker_stack_free(int slot)
{
    if (slot >= 0 && slot < XBOX_WORKER_STACK_COUNT)
        InterlockedExchange(&g_worker_stack_used[slot], 0);
}
