/*
 * win32_compat.c - POSIX implementation of generic Win32 host primitives.
 *
 * See win32_compat.h. This is deliberately a *generic* OS-primitive layer
 * (threads/events/mutexes/atomics/heap/timers) -- it carries no Xbox
 * semantics. The Xbox kernel HLE in src/kernel builds on top of it.
 *
 * POSIX (Linux/MacOS) only.
 */

#if !defined(_WIN32)

/* Enable memfd_create, MAP_FIXED_NOREPLACE, timegm. Must precede all #includes. */
#define _GNU_SOURCE
/* Darwin: exposes memset_s, its explicit_bzero equivalent. */
#define __STDC_WANT_LIB_EXT1__ 1

#include "win32_compat.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sched.h>
#include <fenv.h>
#include <sys/mman.h>

/* address -> length registry for mapped views (defined further down) */
void view_register(void *addr, size_t len);
size_t view_take(const void *addr);
#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#else
#include <sys/sysinfo.h>
#endif

/* ===================================================================== */
/* Last-error (thread-local)                                             */
/* ===================================================================== */

static __thread DWORD t_last_error = 0;

DWORD GetLastError(void)            { return t_last_error; }
VOID  SetLastError(DWORD code)      { t_last_error = code; }

/* ===================================================================== */
/* Interlocked atomics                                                   */
/* ===================================================================== */

LONG InterlockedIncrement(volatile LONG *p)        { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
LONG InterlockedDecrement(volatile LONG *p)        { return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST); }
LONG InterlockedExchange(volatile LONG *p, LONG v) { return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST); }
LONG InterlockedExchangeAdd(volatile LONG *p, LONG v) { return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST); }

LONG InterlockedCompareExchange(volatile LONG *p, LONG xchg, LONG cmp)
{
    __atomic_compare_exchange_n(p, &cmp, xchg, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return cmp;
}

LONGLONG InterlockedCompareExchange64(volatile LONGLONG *p, LONGLONG xchg, LONGLONG cmp)
{
    __atomic_compare_exchange_n(p, &cmp, xchg, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return cmp;
}

PVOID InterlockedCompareExchangePointer(PVOID volatile *p, PVOID xchg, PVOID cmp)
{
    __atomic_compare_exchange_n(p, &cmp, xchg, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return cmp;
}

/* ===================================================================== */
/* Critical sections (recursive pthread mutex)                           */
/* ===================================================================== */

VOID InitializeCriticalSection(LPCRITICAL_SECTION cs)
{
    memset(cs, 0, sizeof(*cs));
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &attr);
    pthread_mutexattr_destroy(&attr);
    cs->LockSemaphore = m;
}

VOID InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD spin)
{
    InitializeCriticalSection(cs);
    cs->SpinCount = spin;
}

VOID EnterCriticalSection(LPCRITICAL_SECTION cs)
{
    if (!cs->LockSemaphore) InitializeCriticalSection(cs);
    pthread_mutex_lock((pthread_mutex_t *)cs->LockSemaphore);
    cs->RecursionCount++;
}

VOID LeaveCriticalSection(LPCRITICAL_SECTION cs)
{
    if (!cs->LockSemaphore) return;
    cs->RecursionCount--;
    pthread_mutex_unlock((pthread_mutex_t *)cs->LockSemaphore);
}

BOOL TryEnterCriticalSection(LPCRITICAL_SECTION cs)
{
    if (!cs->LockSemaphore) InitializeCriticalSection(cs);
    if (pthread_mutex_trylock((pthread_mutex_t *)cs->LockSemaphore) == 0) {
        cs->RecursionCount++;
        return TRUE;
    }
    return FALSE;
}

VOID DeleteCriticalSection(LPCRITICAL_SECTION cs)
{
    if (cs->LockSemaphore) {
        pthread_mutex_destroy((pthread_mutex_t *)cs->LockSemaphore);
        free(cs->LockSemaphore);
        cs->LockSemaphore = NULL;
    }
}

/* ===================================================================== */
/* Slim reader/writer locks                                              */
/* ===================================================================== */

/* An SRWLOCK is usable straight from SRWLOCK_INIT, so the pthread_rwlock_t
 * behind it has to appear on first use. Unlike the condition variables below
 * -- whose lazy init is covered by the caller holding the paired CRITICAL
 * SECTION -- an SRWLOCK is by definition taken from several threads at once
 * with nothing else held, so first use genuinely races. Serialise just that:
 * once Ptr is published, every acquire is a plain atomic load. */
static pthread_rwlock_t *srw_lazy_init(PSRWLOCK lock)
{
    pthread_rwlock_t *rw = __atomic_load_n((pthread_rwlock_t **)&lock->Ptr,
                                           __ATOMIC_ACQUIRE);
    if (!rw) {
        static pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;
        pthread_mutex_lock(&init_lock);
        rw = (pthread_rwlock_t *)lock->Ptr;
        if (!rw) {
            rw = (pthread_rwlock_t *)malloc(sizeof(*rw));
            pthread_rwlock_init(rw, NULL);
            __atomic_store_n((pthread_rwlock_t **)&lock->Ptr, rw, __ATOMIC_RELEASE);
        }
        pthread_mutex_unlock(&init_lock);
    }
    return rw;
}

VOID InitializeSRWLock(PSRWLOCK lock)
{
    lock->Ptr = NULL;
    srw_lazy_init(lock);
}

VOID AcquireSRWLockShared(PSRWLOCK lock)     { pthread_rwlock_rdlock(srw_lazy_init(lock)); }
VOID ReleaseSRWLockShared(PSRWLOCK lock)     { pthread_rwlock_unlock(srw_lazy_init(lock)); }
VOID AcquireSRWLockExclusive(PSRWLOCK lock)  { pthread_rwlock_wrlock(srw_lazy_init(lock)); }
VOID ReleaseSRWLockExclusive(PSRWLOCK lock)  { pthread_rwlock_unlock(srw_lazy_init(lock)); }

/* ===================================================================== */
/* One-time initialisation                                               */
/* ===================================================================== */

/* Win32 semantics: the callback runs at most once for a given INIT_ONCE, and
 * a callback returning FALSE leaves it un-run so a later call retries. Ptr
 * doubles as the "done" flag. One global mutex covers every INIT_ONCE --
 * initialisation is rare, and the fast path never touches it. */
BOOL InitOnceExecuteOnce(PINIT_ONCE once, PINIT_ONCE_FN fn, PVOID param, PVOID *context)
{
    static pthread_mutex_t once_lock = PTHREAD_MUTEX_INITIALIZER;

    if (__atomic_load_n(&once->Ptr, __ATOMIC_ACQUIRE))
        return TRUE;

    pthread_mutex_lock(&once_lock);
    BOOL ok = TRUE;
    if (!once->Ptr) {
        ok = fn(once, param, context);
        if (ok)
            __atomic_store_n(&once->Ptr, (PVOID)1, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&once_lock);
    return ok;
}

/* ===================================================================== */
/* Condition variables (paired with a CRITICAL_SECTION)                  */
/* ===================================================================== */

/* Forward decl; the definition lives further down with the wait helpers. */
static void deadline_from_ms(DWORD ms, struct timespec *ts);

static void cv_lazy_init(PCONDITION_VARIABLE cv)
{
    if (!cv->Ptr) {
        pthread_cond_t *c = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
        pthread_cond_init(c, NULL);
        /* Race window OK for typical Win32 usage (the CS is held). */
        cv->Ptr = c;
    }
}

VOID InitializeConditionVariable(PCONDITION_VARIABLE cv)
{
    cv->Ptr = NULL;
    cv_lazy_init(cv);
}

BOOL SleepConditionVariableCS(PCONDITION_VARIABLE cv, PCRITICAL_SECTION cs, DWORD ms)
{
    cv_lazy_init(cv);
    if (!cs->LockSemaphore) InitializeCriticalSection(cs);
    pthread_cond_t  *c = (pthread_cond_t  *)cv->Ptr;
    pthread_mutex_t *m = (pthread_mutex_t *)cs->LockSemaphore;
    if (ms == INFINITE) {
        pthread_cond_wait(c, m);
        return TRUE;
    }
    struct timespec ts;
    deadline_from_ms(ms, &ts);
    int rc = pthread_cond_timedwait(c, m, &ts);
    if (rc == ETIMEDOUT) { SetLastError(WAIT_TIMEOUT); return FALSE; }
    return TRUE;
}

VOID WakeConditionVariable(PCONDITION_VARIABLE cv)
{
    cv_lazy_init(cv);
    pthread_cond_signal((pthread_cond_t *)cv->Ptr);
}

VOID WakeAllConditionVariable(PCONDITION_VARIABLE cv)
{
    cv_lazy_init(cv);
    pthread_cond_broadcast((pthread_cond_t *)cv->Ptr);
}

/* ===================================================================== */
/* Waitable kernel objects                                               */
/* ===================================================================== */

typedef enum { K_EVENT, K_SEM, K_MUTEX, K_THREAD, K_TIMER, K_HEAP,
               K_FILEMAP, K_FILE, K_WAITABLE_TIMER } w32_kind;

#define W32_MAX_APC 16

typedef struct w32_object {
    w32_kind        kind;
    LONG            refcount;
    pthread_mutex_t lock;
    pthread_cond_t  cond;

    /* event */
    int             signaled;
    int             manual_reset;

    /* semaphore */
    long            sem_count;
    long            sem_max;

    /* mutex */
    DWORD           mtx_owner;
    int             mtx_recursion;

    /* thread */
    pthread_t       thread;
    int             thread_joinable;
    DWORD           tid;
    int             exited;
    DWORD           exit_code;
    int             suspend_count;
    pthread_cond_t  gate;
    LPTHREAD_START_ROUTINE start;
    LPVOID          start_param;
    int             priority;
    PAPCFUNC        apc_func[W32_MAX_APC];
    ULONG_PTR       apc_data[W32_MAX_APC];
    int             apc_count;

    /* timer-queue timer */
    int             timer_cancel;
    DWORD           timer_due;
    DWORD           timer_period;
    WAITORTIMERCALLBACK timer_cb;
    PVOID           timer_param;

    /* waitable timer */
    int             waitable_manual_reset;
    struct timespec waitable_due_time;
    int             waitable_triggered;
    int             waitable_armed;

    /* file mapping / fd-backed file handle */
    int             fd;
    SIZE_T          map_size;
    char           *file_path;
} w32_object;

/* pseudo handles for "current thread"/"current process" */
#define PSEUDO_CURRENT_PROCESS ((HANDLE)(LONG_PTR)-1)
#define PSEUDO_CURRENT_THREAD  ((HANDLE)(LONG_PTR)-2)
#define STILL_ACTIVE 259u

static __thread w32_object *t_self_obj = NULL;
static __thread DWORD       t_tid      = 0;
static volatile LONG        s_next_tid = 1000;

DWORD GetCurrentThreadId(void)
{
    if (t_tid == 0)
        t_tid = (DWORD)InterlockedIncrement(&s_next_tid);
    return t_tid;
}

DWORD GetCurrentProcessId(void) { return (DWORD)getpid(); }
HANDLE GetCurrentThread(void)   { return t_self_obj ? (HANDLE)t_self_obj : PSEUDO_CURRENT_THREAD; }
HANDLE GetCurrentProcess(void)  { return PSEUDO_CURRENT_PROCESS; }

/* Live objects, so a handle that is not one -- a stale or never-written
 * value the guest passes, which Windows answers with ERROR_INVALID_HANDLE --
 * is refused instead of dereferenced (a crash in pthread_mutex_lock: the
 * Gate of Moria's loader thread waiting on an event handle its creator had
 * not stored yet). Open addressing on the pointer, tombstones on release. */
#define OBJ_SET_SIZE 65536u
static w32_object *volatile s_objs[OBJ_SET_SIZE];
static pthread_mutex_t s_objs_lock = PTHREAD_MUTEX_INITIALIZER;
#define OBJ_TOMB ((w32_object *)(uintptr_t)1)
static unsigned obj_slot(const void *p) { return (unsigned)(((uintptr_t)p >> 4) * 2654435761u) & (OBJ_SET_SIZE - 1); }
static void obj_register(w32_object *o)
{
    unsigned i = obj_slot(o), n;
    pthread_mutex_lock(&s_objs_lock);
    for (n = 0; n < OBJ_SET_SIZE; n++, i = (i + 1) & (OBJ_SET_SIZE - 1))
        if (!s_objs[i] || s_objs[i] == OBJ_TOMB) { s_objs[i] = o; break; }
    pthread_mutex_unlock(&s_objs_lock);
}
static void obj_unregister(w32_object *o)
{
    unsigned i = obj_slot(o), n;
    pthread_mutex_lock(&s_objs_lock);
    for (n = 0; n < OBJ_SET_SIZE && s_objs[i]; n++, i = (i + 1) & (OBJ_SET_SIZE - 1))
        if (s_objs[i] == o) { s_objs[i] = OBJ_TOMB; break; }
    pthread_mutex_unlock(&s_objs_lock);
}
static w32_object *obj_get(HANDLE h)
{
    unsigned i, n;
    if (!h || (uintptr_t)h < 0x10000u) return NULL;
    i = obj_slot(h);
    for (n = 0; n < OBJ_SET_SIZE && s_objs[i]; n++, i = (i + 1) & (OBJ_SET_SIZE - 1))
        if (s_objs[i] == (w32_object *)h) return (w32_object *)h;
    {
        static volatile LONG said;
        if (InterlockedIncrement(&said) <= 10)
            fprintf(stderr, "  [W32] invalid handle %p refused\n", h);
    }
    return NULL;
}

static w32_object *obj_alloc(w32_kind kind)
{
    w32_object *o = (w32_object *)calloc(1, sizeof(w32_object));
    o->kind     = kind;
    o->refcount = 1;
    pthread_mutex_init(&o->lock, NULL);
    pthread_cond_init(&o->cond, NULL);
    pthread_cond_init(&o->gate, NULL);
    obj_register(o);
    return o;
}

static void obj_release(w32_object *o)
{
    if (InterlockedDecrement(&o->refcount) > 0)
        return;
    obj_unregister(o);
    if (o->kind == K_FILE) {
        if (o->fd >= 0) close(o->fd);
        free(o->file_path);
    } else if (o->kind == K_FILEMAP) {
        if (o->fd >= 0) close(o->fd);
    } else if (o->kind == K_WAITABLE_TIMER) {
        /* Waitable timers: no special cleanup needed */
    }
    pthread_mutex_destroy(&o->lock);
    pthread_cond_destroy(&o->cond);
    pthread_cond_destroy(&o->gate);
    free(o);
}

/* ---- fd-backed file handle (for the file-I/O HLE) -------------------- */
HANDLE w32_open_handle(int fd, const char *host_path)
{
    w32_object *o = obj_alloc(K_FILE);
    o->fd        = fd;
    o->file_path = host_path ? strdup(host_path) : NULL;
    return (HANDLE)o;
}

int w32_handle_fd(HANDLE h)
{
    w32_object *o = obj_get(h);
    return (o && o->kind == K_FILE) ? o->fd : -1;
}

const char *w32_handle_path(HANDLE h)
{
    w32_object *o = obj_get(h);
    return (o && o->kind == K_FILE) ? o->file_path : NULL;
}

BOOL CloseHandle(HANDLE h)
{
    if (!h || h == PSEUDO_CURRENT_THREAD || h == PSEUDO_CURRENT_PROCESS ||
        h == INVALID_HANDLE_VALUE)
        return TRUE;
    {
        w32_object *o = obj_get(h);
        if (!o) return FALSE;
        obj_release(o);
    }
    return TRUE;
}

BOOL DuplicateHandle(HANDLE srcProc, HANDLE src, HANDLE dstProc, PHANDLE dst,
                     DWORD access, BOOL inherit, DWORD options)
{
    (void)srcProc; (void)dstProc; (void)access; (void)inherit;
    if (!dst) return FALSE;
    if (src == PSEUDO_CURRENT_THREAD)  src = GetCurrentThread();
    if (src == PSEUDO_CURRENT_PROCESS) { *dst = src; return TRUE; }
    if (src == PSEUDO_CURRENT_THREAD || !src) { *dst = src; return TRUE; }
    w32_object *o = obj_get(src);
    if (!o) return FALSE;
    InterlockedIncrement(&o->refcount);
    *dst = src;
    if (options & DUPLICATE_CLOSE_SOURCE)
        obj_release(o);
    return TRUE;
}

/* ---- deadline helper -------------------------------------------------- */
static void deadline_from_ms(DWORD ms, struct timespec *ts)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec  += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec++; ts->tv_nsec -= 1000000000L; }
}

/* Run any pending user-APCs for the calling thread. Returns count run. */
static int drain_apcs(void)
{
    w32_object *o = t_self_obj;
    int run = 0;
    if (!o) return 0;
    pthread_mutex_lock(&o->lock);
    while (o->apc_count > 0) {
        PAPCFUNC  f = o->apc_func[0];
        ULONG_PTR d = o->apc_data[0];
        memmove(o->apc_func, o->apc_func + 1, sizeof(PAPCFUNC) * (o->apc_count - 1));
        memmove(o->apc_data, o->apc_data + 1, sizeof(ULONG_PTR) * (o->apc_count - 1));
        o->apc_count--;
        pthread_mutex_unlock(&o->lock);
        f(d);
        run++;
        pthread_mutex_lock(&o->lock);
    }
    pthread_mutex_unlock(&o->lock);
    return run;
}

static int timespec_before(const struct timespec *a, const struct timespec *b)
{
    return a->tv_sec != b->tv_sec ? a->tv_sec < b->tv_sec : a->tv_nsec < b->tv_nsec;
}

/* Signalled once the due time passes; latched, so a manual-reset timer stays
 * signalled until it is set or cancelled again. Caller holds o->lock. */
static int waitable_due(w32_object *o)
{
    struct timespec now;
    if (o->waitable_triggered) return 1;
    if (!o->waitable_armed) return 0;
    clock_gettime(CLOCK_REALTIME, &now);
    if (timespec_before(&now, &o->waitable_due_time)) return 0;
    o->waitable_triggered = 1;
    return 1;
}

/*
 * Wait on a single object. The object lock must NOT be held.
 * Returns WAIT_OBJECT_0 / WAIT_TIMEOUT.
 */
static DWORD wait_single(w32_object *o, DWORD ms)
{
    struct timespec ts;
    int timed = (ms != INFINITE);
    if (timed) deadline_from_ms(ms, &ts);

    pthread_mutex_lock(&o->lock);
    DWORD result = WAIT_OBJECT_0;

    for (;;) {
        int ready = 0;
        switch (o->kind) {
        case K_EVENT:  ready = o->signaled; break;
        case K_THREAD: ready = o->exited;   break;
        case K_SEM:    ready = (o->sem_count > 0); break;
        case K_MUTEX:
            ready = (o->mtx_owner == 0 || o->mtx_owner == GetCurrentThreadId());
            break;
        case K_WAITABLE_TIMER: ready = waitable_due(o); break;
        default:       ready = 1; break;
        }
        if (ready) break;

        /* An armed timer has its own deadline. Waiting on the caller's alone
         * would sleep straight past the due time, so take whichever comes
         * first and re-test. */
        struct timespec until = ts;
        int bounded = timed;
        if (o->kind == K_WAITABLE_TIMER && o->waitable_armed &&
            (!timed || timespec_before(&o->waitable_due_time, &ts))) {
            until = o->waitable_due_time;
            bounded = 1;
        }
        int rc = bounded ? pthread_cond_timedwait(&o->cond, &o->lock, &until)
                         : pthread_cond_wait(&o->cond, &o->lock);
        if (rc == ETIMEDOUT && timed && !timespec_before(&until, &ts)) {
            result = WAIT_TIMEOUT; break;
        }
    }

    if (result == WAIT_OBJECT_0) {
        switch (o->kind) {
        case K_EVENT: if (!o->manual_reset) o->signaled = 0; break;
        case K_WAITABLE_TIMER:
            if (!o->waitable_manual_reset) { o->waitable_triggered = 0; o->waitable_armed = 0; }
            break;
        case K_SEM:   o->sem_count--; break;
        case K_MUTEX: o->mtx_owner = GetCurrentThreadId(); o->mtx_recursion++; break;
        default: break;
        }
    }
    pthread_mutex_unlock(&o->lock);
    return result;
}

DWORD WaitForSingleObject(HANDLE h, DWORD ms)
{
    if (!h || h == PSEUDO_CURRENT_THREAD || h == PSEUDO_CURRENT_PROCESS)
        return WAIT_OBJECT_0;
    {
        w32_object *o = obj_get(h);
        if (!o) { SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED; }
        return wait_single(o, ms);
    }
}

DWORD WaitForSingleObjectEx(HANDLE h, DWORD ms, BOOL alertable)
{
    if (alertable && drain_apcs() > 0)
        return WAIT_IO_COMPLETION;
    return WaitForSingleObject(h, ms);
}

/*
 * WaitForMultipleObjects: polling implementation. Adequate for the light
 * multi-object waits the Xbox kernel HLE issues; not a high-throughput path.
 */
DWORD WaitForMultipleObjects(DWORD count, const HANDLE *handles, BOOL waitAll, DWORD ms)
{
    return WaitForMultipleObjectsEx(count, handles, waitAll, ms, FALSE);
}

DWORD WaitForMultipleObjectsEx(DWORD count, const HANDLE *handles, BOOL waitAll,
                               DWORD ms, BOOL alertable)
{
    struct timespec ts;
    int timed = (ms != INFINITE);
    if (timed) deadline_from_ms(ms, &ts);

    for (;;) {
        if (alertable && drain_apcs() > 0)
            return WAIT_IO_COMPLETION;

        if (waitAll) {
            DWORD got = 0;
            for (DWORD i = 0; i < count; i++)
                if (WaitForSingleObject(handles[i], 0) == WAIT_OBJECT_0) got++;
            if (got == count) return WAIT_OBJECT_0;
        } else {
            for (DWORD i = 0; i < count; i++)
                if (WaitForSingleObject(handles[i], 0) == WAIT_OBJECT_0)
                    return WAIT_OBJECT_0 + i;
        }

        if (timed) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            if (now.tv_sec > ts.tv_sec ||
                (now.tv_sec == ts.tv_sec && now.tv_nsec >= ts.tv_nsec))
                return WAIT_TIMEOUT;
        }
        usleep(1000);
    }
}

/* ===================================================================== */
/* Events                                                                */
/* ===================================================================== */

HANDLE CreateEventA(LPSECURITY_ATTRIBUTES sa, BOOL manualReset, BOOL initialState, LPCSTR name)
{
    (void)sa; (void)name;
    w32_object *o = obj_alloc(K_EVENT);
    o->manual_reset = manualReset ? 1 : 0;
    o->signaled     = initialState ? 1 : 0;
    return (HANDLE)o;
}
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES sa, BOOL manualReset, BOOL initialState, LPCWSTR name)
{
    (void)name;
    return CreateEventA(sa, manualReset, initialState, NULL);
}

BOOL SetEvent(HANDLE h)
{
    w32_object *o = obj_get(h);
    if (!o || o->kind != K_EVENT) return FALSE;
    pthread_mutex_lock(&o->lock);
    o->signaled = 1;
    pthread_cond_broadcast(&o->cond);
    pthread_mutex_unlock(&o->lock);
    return TRUE;
}

BOOL ResetEvent(HANDLE h)
{
    w32_object *o = obj_get(h);
    if (!o || o->kind != K_EVENT) return FALSE;
    pthread_mutex_lock(&o->lock);
    o->signaled = 0;
    pthread_mutex_unlock(&o->lock);
    return TRUE;
}

BOOL PulseEvent(HANDLE h)
{
    w32_object *o = obj_get(h);
    if (!o || o->kind != K_EVENT) return FALSE;
    pthread_mutex_lock(&o->lock);
    o->signaled = 1;
    pthread_cond_broadcast(&o->cond);
    o->signaled = 0;
    pthread_mutex_unlock(&o->lock);
    return TRUE;
}

/* ===================================================================== */
/* Semaphores                                                            */
/* ===================================================================== */

HANDLE CreateSemaphoreA(LPSECURITY_ATTRIBUTES sa, LONG initial, LONG maximum, LPCSTR name)
{
    (void)sa; (void)name;
    w32_object *o = obj_alloc(K_SEM);
    o->sem_count = initial;
    o->sem_max   = maximum;
    return (HANDLE)o;
}
HANDLE CreateSemaphoreW(LPSECURITY_ATTRIBUTES sa, LONG initial, LONG maximum, LPCWSTR name)
{
    (void)name;
    return CreateSemaphoreA(sa, initial, maximum, NULL);
}

BOOL ReleaseSemaphore(HANDLE h, LONG releaseCount, PLONG previousCount)
{
    w32_object *o = obj_get(h);
    if (!o || o->kind != K_SEM) return FALSE;
    pthread_mutex_lock(&o->lock);
    if (previousCount) *previousCount = (LONG)o->sem_count;
    o->sem_count += releaseCount;
    if (o->sem_max && o->sem_count > o->sem_max) o->sem_count = o->sem_max;
    pthread_cond_broadcast(&o->cond);
    pthread_mutex_unlock(&o->lock);
    return TRUE;
}

/* ===================================================================== */
/* Mutexes                                                               */
/* ===================================================================== */

HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL initialOwner, LPCSTR name)
{
    (void)sa; (void)name;
    w32_object *o = obj_alloc(K_MUTEX);
    if (initialOwner) { o->mtx_owner = GetCurrentThreadId(); o->mtx_recursion = 1; }
    return (HANDLE)o;
}
HANDLE CreateMutexW(LPSECURITY_ATTRIBUTES sa, BOOL initialOwner, LPCWSTR name)
{
    (void)name;
    return CreateMutexA(sa, initialOwner, NULL);
}

BOOL ReleaseMutex(HANDLE h)
{
    w32_object *o = obj_get(h);
    if (!o || o->kind != K_MUTEX) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    pthread_mutex_lock(&o->lock);
    if (o->mtx_owner != GetCurrentThreadId()) {
        pthread_mutex_unlock(&o->lock);
        SetLastError(ERROR_NOT_OWNER);
        return FALSE;
    }
    if (--o->mtx_recursion <= 0) {
        o->mtx_owner = 0;
        o->mtx_recursion = 0;
        pthread_cond_broadcast(&o->cond);
    }
    pthread_mutex_unlock(&o->lock);
    return TRUE;
}

/* ===================================================================== */
/* Threads                                                               */
/* ===================================================================== */

/* Run at the start of every thread made by CreateThread (the host program
 * pins its guest threads with it, as a Windows TLS callback would). */
void (*w32_thread_start_hook)(void);

static void *thread_trampoline(void *arg)
{
    w32_object *o = (w32_object *)arg;
    t_self_obj = o;
    t_tid      = o->tid;
    if (w32_thread_start_hook) w32_thread_start_hook();

    /* CREATE_SUSPENDED gate */
    pthread_mutex_lock(&o->lock);
    while (o->suspend_count > 0)
        pthread_cond_wait(&o->gate, &o->lock);
    pthread_mutex_unlock(&o->lock);

    DWORD rc = o->start ? o->start(o->start_param) : 0;

    pthread_mutex_lock(&o->lock);
    o->exit_code = rc;
    o->exited    = 1;
    o->signaled  = 1;
    pthread_cond_broadcast(&o->cond);
    pthread_mutex_unlock(&o->lock);

    obj_release(o);   /* drop the trampoline's reference */
    return NULL;
}

HANDLE CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T stackSize,
                    LPTHREAD_START_ROUTINE start, LPVOID param,
                    DWORD flags, LPDWORD threadId)
{
    (void)sa;
    w32_object *o = obj_alloc(K_THREAD);
    o->start         = start;
    o->start_param   = param;
    o->tid           = (DWORD)InterlockedIncrement(&s_next_tid);
    o->suspend_count = (flags & CREATE_SUSPENDED) ? 1 : 0;
    o->refcount      = 2;   /* one for caller, one for the trampoline */

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    /* Translated code nests deeply on the native stack: the Windows build
     * links with a 64 MB default stack, which every thread created with a
     * size of 0 inherits. Same here (address space only until touched). */
    pthread_attr_setstacksize(&attr, stackSize < 65536 ? (64u << 20) : stackSize);

    if (pthread_create(&o->thread, &attr, thread_trampoline, o) != 0) {
        pthread_attr_destroy(&attr);
        o->refcount = 1;
        obj_release(o);
        SetLastError(8 /* ERROR_NOT_ENOUGH_MEMORY */);
        return NULL;
    }
    pthread_attr_destroy(&attr);
    o->thread_joinable = 1;

    if (threadId) *threadId = o->tid;
    return (HANDLE)o;
}

VOID ExitThread(DWORD exitCode)
{
    w32_object *o = t_self_obj;
    if (o) {
        pthread_mutex_lock(&o->lock);
        o->exit_code = exitCode;
        o->exited    = 1;
        o->signaled  = 1;
        pthread_cond_broadcast(&o->cond);
        pthread_mutex_unlock(&o->lock);
        obj_release(o);
    }
    pthread_exit(NULL);
}

BOOL GetExitCodeThread(HANDLE h, LPDWORD exitCode)
{
    w32_object *o = obj_get(h);
    if (!o || o->kind != K_THREAD || !exitCode) return FALSE;
    pthread_mutex_lock(&o->lock);
    *exitCode = o->exited ? o->exit_code : STILL_ACTIVE;
    pthread_mutex_unlock(&o->lock);
    return TRUE;
}

DWORD ResumeThread(HANDLE h)
{
    w32_object *o = obj_get(h);
    if (!o || o->kind != K_THREAD) return (DWORD)-1;
    pthread_mutex_lock(&o->lock);
    DWORD prev = (DWORD)o->suspend_count;
    if (o->suspend_count > 0 && --o->suspend_count == 0)
        pthread_cond_broadcast(&o->gate);
    pthread_mutex_unlock(&o->lock);
    return prev;
}

DWORD SuspendThread(HANDLE h)
{
    /* True mid-run suspension is not supported on POSIX; only the
     * CREATE_SUSPENDED start gate is. Track the count for ResumeThread. */
    w32_object *o = obj_get(h);
    if (!o || o->kind != K_THREAD) return (DWORD)-1;
    pthread_mutex_lock(&o->lock);
    DWORD prev = (DWORD)o->suspend_count;
    o->suspend_count++;
    pthread_mutex_unlock(&o->lock);
    return prev;
}

BOOL TerminateThread(HANDLE h, DWORD exitCode)
{
    w32_object *o = obj_get(h);
    if (!o || o->kind != K_THREAD) return FALSE;
    pthread_cancel(o->thread);
    pthread_mutex_lock(&o->lock);
    o->exit_code = exitCode;
    o->exited    = 1;
    o->signaled  = 1;
    pthread_cond_broadcast(&o->cond);
    pthread_mutex_unlock(&o->lock);
    return TRUE;
}

BOOL SetThreadPriority(HANDLE h, int priority)
{
    w32_object *o = (h == PSEUDO_CURRENT_THREAD) ? t_self_obj : obj_get(h);
    if (o && o->kind == K_THREAD) o->priority = priority;
    return TRUE;   /* real RT priorities need privileges; tracked only */
}

int GetThreadPriority(HANDLE h)
{
    w32_object *o = (h == PSEUDO_CURRENT_THREAD) ? t_self_obj : obj_get(h);
    return (o && o->kind == K_THREAD) ? o->priority : THREAD_PRIORITY_NORMAL;
}

VOID SwitchToThread(void) { sched_yield(); }

DWORD QueueUserAPC(PAPCFUNC func, HANDLE thread, ULONG_PTR data)
{
    w32_object *o = (thread == PSEUDO_CURRENT_THREAD) ? t_self_obj : (w32_object *)thread;
    if (!o || o->kind != K_THREAD) return 0;
    pthread_mutex_lock(&o->lock);
    DWORD ok = 0;
    if (o->apc_count < W32_MAX_APC) {
        o->apc_func[o->apc_count] = func;
        o->apc_data[o->apc_count] = data;
        o->apc_count++;
        ok = 1;
    }
    pthread_mutex_unlock(&o->lock);
    return ok;
}

/* ===================================================================== */
/* Sleep                                                                 */
/* ===================================================================== */

VOID Sleep(DWORD ms)
{
    if (ms == 0) { sched_yield(); return; }
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { }
}

DWORD SleepEx(DWORD ms, BOOL alertable)
{
    if (alertable && drain_apcs() > 0)
        return WAIT_IO_COMPLETION;
    Sleep(ms);
    if (alertable && drain_apcs() > 0)
        return WAIT_IO_COMPLETION;
    return 0;
}

/* ===================================================================== */
/* Timer queues (one helper thread per timer)                            */
/* ===================================================================== */

static void *timer_thread(void *arg)
{
    w32_object *o = (w32_object *)arg;
    int once = (o->timer_period == 0);

    /* initial due time */
    if (o->timer_due) Sleep(o->timer_due);
    if (!o->timer_cancel && o->timer_cb)
        o->timer_cb(o->timer_param, TRUE);

    while (!once && !o->timer_cancel) {
        Sleep(o->timer_period);
        if (o->timer_cancel) break;
        if (o->timer_cb) o->timer_cb(o->timer_param, TRUE);
    }
    obj_release(o);
    return NULL;
}

HANDLE CreateTimerQueue(void)
{
    /* A timer queue is just a grouping token here. */
    return (HANDLE)obj_alloc(K_TIMER);
}

BOOL DeleteTimerQueue(HANDLE timerQueue)
{
    return CloseHandle(timerQueue);
}

BOOL CreateTimerQueueTimer(PHANDLE newTimer, HANDLE timerQueue,
                           WAITORTIMERCALLBACK callback, PVOID param,
                           DWORD dueTime, DWORD period, ULONG flags)
{
    (void)timerQueue;
    if (flags & WT_EXECUTEONLYONCE) period = 0;
    w32_object *o = obj_alloc(K_TIMER);
    o->timer_cb     = callback;
    o->timer_param  = param;
    o->timer_due    = dueTime;
    o->timer_period = period;
    o->refcount     = 2;   /* caller + timer thread */

    if (pthread_create(&o->thread, NULL, timer_thread, o) != 0) {
        o->refcount = 1;
        obj_release(o);
        return FALSE;
    }
    pthread_detach(o->thread);
    if (newTimer) *newTimer = (HANDLE)o;
    return TRUE;
}

BOOL ChangeTimerQueueTimer(HANDLE timerQueue, HANDLE timer, ULONG dueTime, ULONG period)
{
    (void)timerQueue;
    w32_object *o = (w32_object *)timer;
    if (!o || o->kind != K_TIMER) return FALSE;
    o->timer_due    = dueTime;
    o->timer_period = period;
    return TRUE;
}

BOOL DeleteTimerQueueTimer(HANDLE timerQueue, HANDLE timer, HANDLE completionEvent)
{
    (void)timerQueue;
    w32_object *o = (w32_object *)timer;
    if (!o || o->kind != K_TIMER) return FALSE;
    o->timer_cancel = 1;
    if (completionEvent) SetEvent(completionEvent);
    obj_release(o);
    return TRUE;
}

struct w32_tp_args { PTP_SIMPLE_CALLBACK cb; PVOID ctx; };

static void *w32_tp_trampoline(void *arg)
{
    struct w32_tp_args *a = (struct w32_tp_args *)arg;
    a->cb(NULL, a->ctx);
    free(a);
    return NULL;
}

BOOL TrySubmitThreadpoolCallback(PTP_SIMPLE_CALLBACK callback,
                                 PVOID context, PVOID env)
{
    (void)env;
    /* Run on a throwaway detached thread. */
    struct w32_tp_args *a = (struct w32_tp_args *)malloc(sizeof(*a));
    a->cb = callback; a->ctx = context;
    pthread_t th;
    if (pthread_create(&th, NULL, w32_tp_trampoline, a) != 0) { free(a); return FALSE; }
    pthread_detach(th);
    return TRUE;
}

/* ===================================================================== */
/* Waitable timers                                                       */
/* ===================================================================== */

HANDLE CreateWaitableTimerW(LPSECURITY_ATTRIBUTES sa, BOOL manualReset, LPCWSTR name)
{
    (void)sa;
    (void)name;
    w32_object *o = obj_alloc(K_WAITABLE_TIMER);
    o->waitable_manual_reset = manualReset;
    return (HANDLE)o;
}

BOOL SetWaitableTimer(HANDLE h, const LARGE_INTEGER *dueTime, LONG period,
                      PTIMERAPCROUTINE completion, PVOID arg, BOOL resume)
{
    w32_object *o = obj_get(h);
    (void)completion; (void)arg; (void)resume;
    if (!o || o->kind != K_WAITABLE_TIMER || !dueTime) return FALSE;
    pthread_mutex_lock(&o->lock);
    /* Win32 100ns units: negative is relative to now, positive is an absolute
     * FILETIME. ponytail: period is ignored -- one-shot only, revisit if a
     * title actually arms a repeating timer. */
    if (dueTime->QuadPart <= 0) {
        clock_gettime(CLOCK_REALTIME, &o->waitable_due_time);
        LONGLONG ns = -dueTime->QuadPart * 100LL;
        o->waitable_due_time.tv_sec  += (time_t)(ns / 1000000000LL);
        o->waitable_due_time.tv_nsec += (long)(ns % 1000000000LL);
        if (o->waitable_due_time.tv_nsec >= 1000000000L) {
            o->waitable_due_time.tv_sec++;
            o->waitable_due_time.tv_nsec -= 1000000000L;
        }
    } else {
        /* FILETIME epoch is 1601-01-01; Unix is 1970-01-01. */
        LONGLONG unix100ns = dueTime->QuadPart - 116444736000000000LL;
        o->waitable_due_time.tv_sec  = (time_t)(unix100ns / 10000000LL);
        o->waitable_due_time.tv_nsec = (long)((unix100ns % 10000000LL) * 100LL);
    }
    (void)period;
    o->waitable_armed = 1;
    o->waitable_triggered = 0;
    pthread_mutex_unlock(&o->lock);
    pthread_cond_broadcast(&o->cond);
    return TRUE;
}

BOOL CancelWaitableTimer(HANDLE h)
{
    w32_object *o = obj_get(h);
    if (!o || o->kind != K_WAITABLE_TIMER) return FALSE;
    pthread_mutex_lock(&o->lock);
    o->waitable_triggered = 0;
    o->waitable_armed = 0;
    pthread_mutex_unlock(&o->lock);
    return TRUE;
}

/* ===================================================================== */
/* Heap (thin wrapper over malloc; the single process heap)              */
/* ===================================================================== */

static w32_object s_process_heap = { .kind = K_HEAP };

HANDLE GetProcessHeap(void)                       { return (HANDLE)&s_process_heap; }
HANDLE HeapCreate(DWORD o, SIZE_T i, SIZE_T m)    { (void)o;(void)i;(void)m; return (HANDLE)&s_process_heap; }
BOOL   HeapDestroy(HANDLE h)                      { (void)h; return TRUE; }

LPVOID HeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes)
{
    (void)heap;
    return (flags & HEAP_ZERO_MEMORY) ? calloc(1, bytes ? bytes : 1)
                                      : malloc(bytes ? bytes : 1);
}
LPVOID HeapReAlloc(HANDLE heap, DWORD flags, LPVOID mem, SIZE_T bytes)
{
    (void)heap; (void)flags;
    return realloc(mem, bytes ? bytes : 1);
}
BOOL HeapFree(HANDLE heap, DWORD flags, LPVOID mem)
{
    (void)heap; (void)flags;
    free(mem);
    return TRUE;
}
SIZE_T HeapSize(HANDLE heap, DWORD flags, LPCVOID mem)
{
    (void)heap; (void)flags; (void)mem;
    return 0;   /* glibc malloc_usable_size could be used; not needed yet */
}

/* ===================================================================== */
/* Virtual memory                                                        */
/* ===================================================================== */

static int prot_from_page(DWORD protect)
{
    switch (protect & 0xFF) {
    case PAGE_NOACCESS:          return PROT_NONE;
    case PAGE_READONLY:          return PROT_READ;
    case PAGE_READWRITE:         return PROT_READ | PROT_WRITE;
    case PAGE_EXECUTE:           return PROT_EXEC;
    case PAGE_EXECUTE_READ:      return PROT_READ | PROT_EXEC;
    case PAGE_EXECUTE_READWRITE: return PROT_READ | PROT_WRITE | PROT_EXEC;
    default:                     return PROT_READ | PROT_WRITE;
    }
}

#if defined(__APPLE__)
/* Darwin has no MAP_FIXED_NOREPLACE, and the two mmap options are both wrong
 * for VirtualAlloc: MAP_FIXED silently unmaps whatever already occupies the
 * range, and a bare address hint can be relocated by the kernel for reasons
 * other than the range being taken -- so "we got a different address" is only
 * an approximation of "it was occupied", and a racy one.
 *
 * mach_vm_map with VM_FLAGS_FIXED is the exact primitive: it maps at the
 * address given, and returns KERN_NO_SPACE rather than displacing an existing
 * mapping. That is what Win32 promises, and this layer exists to keep the Xbox
 * HLE above it honest -- a VirtualAlloc that quietly replaced a live mapping
 * would corrupt whatever held it, far from the call that did it.
 *
 * Memory from mach_vm_map is released by munmap like any other, because the
 * BSD and Mach halves of Darwin share one VM map, so VirtualFree is unchanged.
 */
/* Length registry, defined with the view helpers below. Win32 frees by address
 * alone -- UnmapViewOfFile takes no length and VirtualFree(MEM_RELEASE) is
 * documented to take size 0 -- so the length has to be recoverable here or
 * munmap cannot be called at all. */
void view_register(void *addr, size_t len);
size_t view_take(const void *addr);

static void *mach_map_fixed(void *address, size_t size, int prot)
{
    mach_vm_address_t addr = (mach_vm_address_t)(uintptr_t)address;
    mach_vm_size_t len = (size + vm_page_size - 1) & ~((mach_vm_size_t)vm_page_size - 1);
    vm_prot_t vmprot = VM_PROT_NONE;

    if (prot & PROT_READ)  vmprot |= VM_PROT_READ;
    if (prot & PROT_WRITE) vmprot |= VM_PROT_WRITE;
    if (prot & PROT_EXEC)  vmprot |= VM_PROT_EXECUTE;

    kern_return_t kr = mach_vm_map(
        mach_task_self(),
        &addr,
        len,
        0,
        VM_FLAGS_FIXED,
        MEMORY_OBJECT_NULL,
        0,
        FALSE,
        vmprot,
        VM_PROT_ALL,
        VM_INHERIT_DEFAULT
    );
    if (kr != KERN_SUCCESS) {
        return MAP_FAILED;
    }
    return (void *)(uintptr_t)addr;
}
#endif

/* ---- The guest's address space ------------------------------------------
 *
 * The Xbox memory layout puts views at fixed offsets from one base across
 * 4 GB (RAM and its mirrors, the contiguous window at +2 GB, the GPU and
 * MCPX apertures near +4 GB). Windows hands out address space bottom-up and
 * leaves most of that free; Linux hands out mmap space top-down, right below
 * the libraries and thread stacks, so a range picked for the RAM could have
 * a library sitting where the contiguous window must go. w32_reserve_arena
 * claims the whole span up front (address space only, PROT_NONE, nothing
 * committed); allocations and views at addresses inside it then take the
 * place of the reservation, and releasing them puts the reservation back
 * rather than leaving a hole for something else to land in. Inside the
 * arena an address already in use still refuses a second mapping, as
 * Windows does. */
static uintptr_t s_arena_lo, s_arena_hi;
static uint8_t *s_arena_used;          /* one byte per 4 KB page */
static pthread_mutex_t s_arena_lock = PTHREAD_MUTEX_INITIALIZER;
#define ARENA_PG 4096u

void *w32_reserve_arena(size_t size, size_t align)
{
    size_t span = size + align;
    uintptr_t raw, lo;
    void *p;
    if (s_arena_lo) return (void *)s_arena_lo;
    p = mmap(NULL, span, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) return NULL;
    raw = (uintptr_t)p;
    lo = align ? (raw + align - 1) & ~(uintptr_t)(align - 1) : raw;
    /* trim the alignment slack */
    if (lo > raw) munmap((void *)raw, lo - raw);
    if (raw + span > lo + size) munmap((void *)(lo + size), raw + span - (lo + size));
    s_arena_used = (uint8_t *)calloc(size / ARENA_PG + 1, 1);
    if (!s_arena_used) { munmap((void *)lo, size); return NULL; }
    s_arena_lo = lo;
    s_arena_hi = lo + size;
    return (void *)lo;
}

static int arena_has(const void *a, size_t len)
{
    uintptr_t x = (uintptr_t)a;
    return s_arena_lo && x >= s_arena_lo && x + len <= s_arena_hi && x + len >= x;
}

/* mark [a, a+len) used; 0 if any of it already is */
static int arena_claim(const void *a, size_t len)
{
    size_t i, p0 = ((uintptr_t)a - s_arena_lo) / ARENA_PG, n = (len + ARENA_PG - 1) / ARENA_PG;
    pthread_mutex_lock(&s_arena_lock);
    for (i = 0; i < n; i++) if (s_arena_used[p0 + i]) { pthread_mutex_unlock(&s_arena_lock); return 0; }
    for (i = 0; i < n; i++) s_arena_used[p0 + i] = 1;
    pthread_mutex_unlock(&s_arena_lock);
    return 1;
}

static void arena_unclaim(const void *a, size_t len)
{
    size_t i, p0 = ((uintptr_t)a - s_arena_lo) / ARENA_PG, n = (len + ARENA_PG - 1) / ARENA_PG;
    pthread_mutex_lock(&s_arena_lock);
    for (i = 0; i < n; i++) s_arena_used[p0 + i] = 0;
    pthread_mutex_unlock(&s_arena_lock);
}

/* give [a, a+len) back to the reservation */
static int arena_release(void *a, size_t len)
{
    void *p = mmap(a, len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
    arena_unclaim(a, len);
    return p == a;
}

LPVOID VirtualAlloc(LPVOID address, SIZE_T size, DWORD allocationType, DWORD protect)
{
    int prot  = prot_from_page(protect);
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;

    /* MEM_COMMIT on a region already reserved by a prior VirtualAlloc:
     * just adjust protection. */
    if ((allocationType & MEM_COMMIT) && !(allocationType & MEM_RESERVE) && address) {
        if (mprotect(address, size, prot) == 0)
            return address;
        /* fall through to a fresh mapping */
    }

    if (address && arena_has(address, size)) {
        void *q;
        if (!arena_claim(address, size)) { SetLastError(ERROR_INVALID_ADDRESS); return NULL; }
        q = mmap(address, size, prot ? prot : PROT_READ | PROT_WRITE, flags | MAP_FIXED, -1, 0);
        if (q == MAP_FAILED) { arena_unclaim(address, size); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
        view_register(q, size);
        return q;
    }
#if defined(MAP_FIXED_NOREPLACE)
    if (address) flags |= MAP_FIXED_NOREPLACE;
#endif

    void *p;
#if defined(__APPLE__)
    if (address) {
        p = mach_map_fixed(address, size, prot ? prot : PROT_READ | PROT_WRITE);
    } else
#endif
    p = mmap(address, size, prot ? prot : PROT_READ | PROT_WRITE, flags, -1, 0);
    if (p == MAP_FAILED) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    /* Remember the length: VirtualFree(MEM_RELEASE) is passed size 0 by every
     * Win32 caller, and munmap cannot be called without one. */
    view_register(p, size);
#if !defined(MAP_FIXED_NOREPLACE) && !defined(__APPLE__)
    /* Older kernels without MAP_FIXED_NOREPLACE: plain MAP_FIXED would silently
     * unmap whatever already lives there, so we pass the address as a hint and
     * treat a different result as "taken". Apple goes through mach_map_fixed
     * above, which reports that properly instead of inferring it. */
    if (address && p != address) {
        munmap(p, size);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
#endif
    return p;
}

BOOL VirtualFree(LPVOID address, SIZE_T size, DWORD freeType)
{
    if (freeType & MEM_RELEASE) {
        /* Win32 MEM_RELEASE passes size 0 and frees the whole allocation, so
         * the length comes from the registry VirtualAlloc filled in. Returning
         * TRUE without unmapping -- as this used to -- made every release a
         * silent no-op: the caller believed the address was free, the next
         * allocation there failed, and nothing connected the two. */
        size_t len = view_take(address);
        if (size == 0) size = len;
        if (size == 0) return FALSE;
        if (arena_has(address, size)) return arena_release(address, size);
        return munmap(address, size) == 0;
    }
    if (freeType & MEM_DECOMMIT)
        return mprotect(address, size, PROT_NONE) == 0;
    return TRUE;
}

BOOL VirtualProtect(LPVOID address, SIZE_T size, DWORD newProtect, PDWORD oldProtect)
{
    if (oldProtect) *oldProtect = PAGE_READWRITE;
    return mprotect(address, size, prot_from_page(newProtect)) == 0;
}

/* ===================================================================== */
/* Time                                                                  */
/* ===================================================================== */

/* 100-ns intervals between 1601-01-01 and 1970-01-01 */
#define FILETIME_EPOCH_DIFF 116444736000000000ULL

VOID GetSystemTimeAsFileTime(LPFILETIME ft)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ULONGLONG t = FILETIME_EPOCH_DIFF
                + (ULONGLONG)ts.tv_sec * 10000000ULL
                + (ULONGLONG)ts.tv_nsec / 100ULL;
    ft->dwLowDateTime  = (DWORD)(t & 0xFFFFFFFFULL);
    ft->dwHighDateTime = (DWORD)(t >> 32);
}

static void fill_systemtime(LPSYSTEMTIME st, const struct tm *tm, long nsec)
{
    st->wYear         = (WORD)(tm->tm_year + 1900);
    st->wMonth        = (WORD)(tm->tm_mon + 1);
    st->wDayOfWeek    = (WORD)tm->tm_wday;
    st->wDay          = (WORD)tm->tm_mday;
    st->wHour         = (WORD)tm->tm_hour;
    st->wMinute       = (WORD)tm->tm_min;
    st->wSecond       = (WORD)tm->tm_sec;
    st->wMilliseconds = (WORD)(nsec / 1000000L);
}

VOID GetSystemTime(LPSYSTEMTIME st)
{
    struct timespec ts; struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts);
    gmtime_r(&ts.tv_sec, &tm);
    fill_systemtime(st, &tm, ts.tv_nsec);
}

VOID GetLocalTime(LPSYSTEMTIME st)
{
    struct timespec ts; struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    fill_systemtime(st, &tm, ts.tv_nsec);
}

ULONGLONG GetTickCount64(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ULONGLONG)ts.tv_sec * 1000ULL + (ULONGLONG)ts.tv_nsec / 1000000ULL;
}

DWORD GetTickCount(void) { return (DWORD)GetTickCount64(); }

BOOL QueryPerformanceCounter(PLARGE_INTEGER count)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    count->QuadPart = (LONGLONG)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return TRUE;
}

BOOL QueryPerformanceFrequency(PLARGE_INTEGER freq)
{
    freq->QuadPart = 1000000000LL;   /* QPC is in nanoseconds */
    return TRUE;
}

/* ===================================================================== */
/* Misc                                                                  */
/* ===================================================================== */

VOID OutputDebugStringA(LPCSTR str)
{
    if (str) fputs(str, stderr);
}

VOID ExitProcess(UINT exitCode) { exit((int)exitCode); }

BOOL IsDebuggerPresent(void)
{
#if defined(__APPLE__)
    /* Darwin: KERN_PROC_PID reports P_TRACED when a debugger is attached. */
    struct kinfo_proc info;
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
    size_t size = sizeof(info);
    memset(&info, 0, size);
    if (sysctl(mib, sizeof(mib), &info, &size, NULL, 0) != 0) return FALSE;
    return (info.kp_proc.p_flag & P_TRACED) != 0;
#else
    /* Linux: a non-zero TracerPid in /proc/self/status means ptrace is attached. */
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return FALSE;
    char line[256];
    BOOL traced = FALSE;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "TracerPid:", 10) == 0) {
            traced = strtol(line + 10, NULL, 10) != 0;
            break;
        }
    }
    fclose(f);
    return traced;
#endif
}

VOID DebugBreak(void) {
    /* Not __debugbreak(): that is an MSVC intrinsic, and this file is the
     * half that MSVC never compiles. SIGTRAP is the POSIX equivalent --
     * continuable under a debugger, fatal without one, as on Windows. */
    raise(SIGTRAP);
}

VOID SecureZeroMemory(PVOID ptr, SIZE_T cnt)
{
#if defined(__APPLE__)
    memset_s(ptr, cnt, 0, cnt);
#else
    explicit_bzero(ptr, cnt);
#endif
}

unsigned int _clearfp(void)
{
    feclearexcept(FE_ALL_EXCEPT);
    return 0;
}

/* ===================================================================== */
/* Win32 file API on POSIX (open/read/write/fstat-backed)                */
/* ===================================================================== */

#include <fcntl.h>
#include <sys/stat.h>

HANDLE CreateFileA(LPCSTR name, DWORD access, DWORD share,
                   LPSECURITY_ATTRIBUTES sa, DWORD disp,
                   DWORD flags, HANDLE templ)
{
    (void)share; (void)sa; (void)flags; (void)templ;
    if (!name) { SetLastError(ERROR_INVALID_PARAMETER); return INVALID_HANDLE_VALUE; }

    int rw = O_RDONLY;
    int wantW = (access & (GENERIC_WRITE | GENERIC_ALL)) != 0;
    int wantR = (access & (GENERIC_READ  | GENERIC_ALL)) != 0;
    if (wantW && wantR) rw = O_RDWR;
    else if (wantW)     rw = O_WRONLY;

    int extra = 0;
    switch (disp) {
    case CREATE_NEW:        extra = O_CREAT | O_EXCL;  break;
    case CREATE_ALWAYS:     extra = O_CREAT | O_TRUNC; break;
    case OPEN_EXISTING:     extra = 0;                 break;
    case OPEN_ALWAYS:       extra = O_CREAT;           break;
    case TRUNCATE_EXISTING: extra = O_TRUNC;           break;
    default:                extra = 0;                 break;
    }
    if ((extra & (O_CREAT | O_TRUNC)) && rw == O_RDONLY) rw = O_RDWR;

    /* Normalise embedded Windows-style backslashes before open(). */
    char norm[1024];
    snprintf(norm, sizeof(norm), "%s", name);
    xbox_path_normalize(norm);
    int fd = open(norm, rw | extra, 0644);
    if (fd < 0) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; }
    return w32_open_handle(fd, name);
}

HANDLE CreateFileW(LPCWSTR name, DWORD access, DWORD share,
                   LPSECURITY_ATTRIBUTES sa, DWORD disp,
                   DWORD flags, HANDLE templ)
{
    /* Basic UTF-16 -> UTF-8 (ASCII path of WideCharToMultiByte). */
    char buf[1024];
    int len = WideCharToMultiByte(CP_UTF8, 0, name, -1, buf, sizeof(buf), NULL, NULL);
    if (len <= 0) buf[0] = '\0';
    return CreateFileA(buf, access, share, sa, disp, flags, templ);
}

BOOL ReadFile(HANDLE h, LPVOID buf, DWORD len, LPDWORD nread, void *overlapped)
{
    (void)overlapped;
    int fd = w32_handle_fd(h);
    if (fd < 0) { if (nread) *nread = 0; SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    ssize_t n = read(fd, buf, len);
    if (n < 0)  { if (nread) *nread = 0; SetLastError(ERROR_GEN_FAILURE);    return FALSE; }
    if (nread)  *nread = (DWORD)n;
    return TRUE;
}

BOOL WriteFile(HANDLE h, LPCVOID buf, DWORD len, LPDWORD nwritten, void *overlapped)
{
    (void)overlapped;
    int fd = w32_handle_fd(h);
    if (fd < 0) { if (nwritten) *nwritten = 0; SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    ssize_t n = write(fd, buf, len);
    if (n < 0)  { if (nwritten) *nwritten = 0; SetLastError(ERROR_GEN_FAILURE);    return FALSE; }
    if (nwritten) *nwritten = (DWORD)n;
    return TRUE;
}

DWORD GetFileSize(HANDLE h, LPDWORD high)
{
    int fd = w32_handle_fd(h);
    if (fd < 0) return INVALID_FILE_SIZE;
    struct stat st;
    if (fstat(fd, &st) != 0) return INVALID_FILE_SIZE;
    if (high) *high = (DWORD)(((uint64_t)st.st_size >> 32) & 0xFFFFFFFFu);
    return (DWORD)(st.st_size & 0xFFFFFFFFu);
}

BOOL GetFileSizeEx(HANDLE h, PLARGE_INTEGER size)
{
    int fd = w32_handle_fd(h);
    if (fd < 0 || !size) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    struct stat st;
    if (fstat(fd, &st) != 0) { SetLastError(ERROR_GEN_FAILURE); return FALSE; }
    size->QuadPart = (LONGLONG)st.st_size;
    return TRUE;
}

BOOL FlushFileBuffers(HANDLE h)
{
    int fd = w32_handle_fd(h);
    if (fd < 0) return FALSE;
    return fsync(fd) == 0;
}

/* ===================================================================== */
/* Keyboard + window helpers -- stubs. Real keyboard polling will come   */
/* via SDL_GetKeyboardState when main.c gets its SDL2 port.              */
/* ===================================================================== */

SHORT GetAsyncKeyState(int vKey)          { (void)vKey; return 0; }
HWND  FindWindowA(LPCSTR c, LPCSTR w)     { (void)c; (void)w; return NULL; }
HWND  GetActiveWindow(void)               { return NULL; }
BOOL  SetWindowTextA(HWND h, LPCSTR t)    { (void)h; (void)t; return TRUE; }
int   GetWindowTextA(HWND h, LPSTR t, int n) { (void)h; (void)t; (void)n; return 0; }
BOOL  EnumWindows(WNDENUMPROC p, LPARAM l) { (void)p; (void)l; return FALSE; }

int MessageBoxA(HWND h, LPCSTR text, LPCSTR caption, UINT type)
{
    (void)h; (void)type;
    fprintf(stderr, "[%s] %s\n", caption ? caption : "MessageBox",
                                   text    ? text    : "");
    return 1;   /* IDOK */
}

/* Message-loop stubs: no Win32 messages on POSIX (SDL events drive the
 * d3d8_gl backend; this layer is just for the game's Win32 message pump). */
BOOL    PeekMessageA(LPMSG m, HWND w, UINT a, UINT b, UINT f)
{ (void)m; (void)w; (void)a; (void)b; (void)f; return FALSE; }
BOOL    TranslateMessage(const MSG *m) { (void)m; return TRUE; }
LRESULT DispatchMessageA(const MSG *m) { (void)m; return 0; }

/* XInput stub: real gamepad is wired through input_compat (SDL2). */
DWORD XInputGetState(DWORD idx, XINPUT_STATE *state)
{ (void)idx; if (state) memset(state, 0, sizeof(*state)); return ERROR_DEVICE_NOT_CONNECTED; }

BOOL TerminateProcess(HANDLE process, UINT exitCode)
{
    (void)process;
    exit((int)exitCode);
}

VOID OutputDebugStringW(LPCWSTR str)
{
    if (!str) return;
    for (const WCHAR *p = str; *p; p++)
        fputc((*p < 128) ? (int)*p : '?', stderr);
}

/*
 * Minimal MultiByteToWideChar / WideCharToMultiByte. Handles UTF-8 and a
 * latin-1 interpretation of CP_ACP -- enough for path/name strings.
 */
int MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR mb, int mbCount,
                        LPWSTR wide, int wideCount)
{
    (void)flags;
    if (!mb) return 0;
    int srcLen = (mbCount < 0) ? (int)strlen(mb) + 1 : mbCount;
    int out = 0;

    for (int i = 0; i < srcLen; ) {
        unsigned int cpval;
        unsigned char c = (unsigned char)mb[i];

        if (cp == CP_UTF8 && c >= 0x80) {
            if ((c & 0xE0) == 0xC0 && i + 1 < srcLen) {
                cpval = ((c & 0x1F) << 6) | (mb[i+1] & 0x3F); i += 2;
            } else if ((c & 0xF0) == 0xE0 && i + 2 < srcLen) {
                cpval = ((c & 0x0F) << 12) | ((mb[i+1] & 0x3F) << 6) |
                        (mb[i+2] & 0x3F); i += 3;
            } else if ((c & 0xF8) == 0xF0 && i + 3 < srcLen) {
                cpval = ((c & 0x07) << 18) | ((mb[i+1] & 0x3F) << 12) |
                        ((mb[i+2] & 0x3F) << 6) | (mb[i+3] & 0x3F); i += 4;
            } else { cpval = c; i += 1; }
        } else {
            cpval = c; i += 1;   /* ASCII / latin-1 */
        }

        if (cpval > 0xFFFF) cpval = '?';   /* no surrogate pairs */
        if (wideCount > 0) {
            if (out >= wideCount) return 0;
            wide[out] = (WCHAR)cpval;
        }
        out++;
    }
    return out;
}

int WideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR wide, int wideCount,
                        LPSTR mb, int mbCount, LPCSTR defChar, PBOOL usedDef)
{
    (void)flags; (void)defChar; (void)usedDef;
    if (!wide) return 0;
    int srcLen = wideCount;
    if (srcLen < 0) { srcLen = 0; while (wide[srcLen]) srcLen++; srcLen++; }
    int out = 0;

    for (int i = 0; i < srcLen; i++) {
        unsigned int cpval = wide[i];
        char buf[4]; int n;
        if (cp == CP_UTF8 && cpval >= 0x80) {
            if (cpval < 0x800) {
                buf[0] = (char)(0xC0 | (cpval >> 6));
                buf[1] = (char)(0x80 | (cpval & 0x3F)); n = 2;
            } else {
                buf[0] = (char)(0xE0 | (cpval >> 12));
                buf[1] = (char)(0x80 | ((cpval >> 6) & 0x3F));
                buf[2] = (char)(0x80 | (cpval & 0x3F)); n = 3;
            }
        } else {
            buf[0] = (char)(cpval > 0xFF ? '?' : cpval); n = 1;
        }
        if (mbCount > 0) {
            if (out + n > mbCount) return 0;
            for (int k = 0; k < n; k++) mb[out + k] = buf[k];
        }
        out += n;
    }
    return out;
}

/* ===================================================================== */
/* File mapping (memfd-backed) -- true aliased mirror views              */
/* ===================================================================== */

/* Registry of active views: UnmapViewOfFile takes no length, so we must
 * recover the mapping length here for munmap. */
typedef struct { void *addr; size_t len; } w32_view;
static w32_view        s_views[512];
static pthread_mutex_t s_views_lock = PTHREAD_MUTEX_INITIALIZER;

void view_register(void *addr, size_t len)
{
    pthread_mutex_lock(&s_views_lock);
    for (int i = 0; i < 512; i++)
        if (!s_views[i].addr) { s_views[i].addr = addr; s_views[i].len = len; break; }
    pthread_mutex_unlock(&s_views_lock);
}

/* Non-destructive counterpart to view_take, and interior-aware: VirtualQuery
 * is asked about addresses *within* a region at least as often as about its
 * base -- a translated guest VA lands in the middle of the 64 MB window.
 * Picks the containing region and reports where it starts. */
int view_lookup(const void *addr, void **base_out, size_t *len_out)
{
    int found = 0;
    pthread_mutex_lock(&s_views_lock);
    for (int i = 0; i < 512; i++) {
        if (!s_views[i].addr)
            continue;
        uintptr_t lo = (uintptr_t)s_views[i].addr;
        uintptr_t hi = lo + s_views[i].len;
        if ((uintptr_t)addr >= lo && (uintptr_t)addr < hi) {
            if (base_out) *base_out = s_views[i].addr;
            if (len_out)  *len_out  = s_views[i].len;
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&s_views_lock);
    return found;
}

size_t view_take(const void *addr)
{
    size_t len = 0;
    pthread_mutex_lock(&s_views_lock);
    for (int i = 0; i < 512; i++)
        if (s_views[i].addr == addr) { len = s_views[i].len; s_views[i].addr = NULL; break; }
    pthread_mutex_unlock(&s_views_lock);
    return len;
}

/* An unnamed file descriptor that ftruncate and mmap both accept. Linux has
 * memfd_create for this; elsewhere an immediately-unlinked temp file does. */
static int anon_map_fd(const char *name)
{
#if defined(__APPLE__)
    static volatile LONG map_counter = 0;
    char shm_name[32];
    LONG seq = InterlockedIncrement(&map_counter);
    const char *base = name ? name : "xbox_map";
    snprintf(shm_name, sizeof(shm_name), "/%s_%ld", base, seq);
    int fd = shm_open(shm_name, O_CREAT | O_RDWR | O_EXCL, 0600);
    if (fd >= 0) shm_unlink(shm_name);
    return fd;
#else
    return memfd_create(name ? name : "xbox_map", 0);
#endif
}

HANDLE CreateFileMappingA(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD protect,
                          DWORD maxSizeHigh, DWORD maxSizeLow, LPCSTR name)
{
    (void)file; (void)sa; (void)protect;
    SIZE_T size = ((SIZE_T)maxSizeHigh << 32) | maxSizeLow;
    if (size == 0) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }

    int fd = anon_map_fd(name);
    if (fd < 0) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    if (ftruncate(fd, (off_t)size) != 0) {
        close(fd);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    w32_object *o = obj_alloc(K_FILEMAP);
    o->fd       = fd;
    o->map_size = size;
    return (HANDLE)o;
}

HANDLE CreateFileMappingW(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD protect,
                          DWORD maxSizeHigh, DWORD maxSizeLow, LPCWSTR name)
{
    (void)name;
    return CreateFileMappingA(file, sa, protect, maxSizeHigh, maxSizeLow, NULL);
}

LPVOID MapViewOfFileEx(HANDLE mapping, DWORD access, DWORD offHigh, DWORD offLow,
                       SIZE_T count, LPVOID baseAddr)
{
    w32_object *o = (w32_object *)mapping;
    if (!o || o->kind != K_FILEMAP) { SetLastError(ERROR_INVALID_HANDLE); return NULL; }

    off_t  off = ((off_t)offHigh << 32) | offLow;
    SIZE_T len = count ? count : (o->map_size - (SIZE_T)off);
    int prot   = PROT_READ | ((access != FILE_MAP_READ) ? PROT_WRITE : 0);
    int flags  = MAP_SHARED;

    /* Win32 MapViewOfFileEx *fails* when the requested address is unavailable.
     * Plain MAP_FIXED does the opposite: it silently unmaps whatever is there
     * and succeeds. The Xbox memory model asks for 28 mirror views at computed
     * addresses, so with a base the OS chose rather than one we picked, that
     * difference is the process quietly destroying its own libraries and heap
     * and dying somewhere unrelated a moment later. */
    if (baseAddr && arena_has(baseAddr, len)) {
        void *q;
        if (!arena_claim(baseAddr, len)) { SetLastError(ERROR_INVALID_ADDRESS); return NULL; }
        q = mmap(baseAddr, len, prot, flags | MAP_FIXED, o->fd, off);
        if (q == MAP_FAILED) { arena_unclaim(baseAddr, len); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
        view_register(q, len);
        return q;
    }
    if (baseAddr) {
#if defined(MAP_FIXED_NOREPLACE)
        flags |= MAP_FIXED_NOREPLACE;
#elif defined(__APPLE__)
        /* Darwin has no MAP_FIXED_NOREPLACE. Claim the range first with
         * mach_vm_map(VM_FLAGS_FIXED), which refuses rather than displaces;
         * MAP_FIXED below can then only replace the placeholder we now own. */
        if (mach_map_fixed(baseAddr, len, PROT_READ | PROT_WRITE) == MAP_FAILED) {
            SetLastError(ERROR_INVALID_ADDRESS);
            return NULL;
        }
        flags |= MAP_FIXED;
#else
        flags |= MAP_FIXED;
#endif
    }

    void *p = mmap(baseAddr, len, prot, flags, o->fd, off);
    if (p == MAP_FAILED) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    if (baseAddr && p != baseAddr) {
        /* MAP_FIXED_NOREPLACE hands back a different address instead of
         * failing on some kernels; treat that as the refusal it means. */
        munmap(p, len);
        SetLastError(ERROR_INVALID_ADDRESS);
        return NULL;
    }
    view_register(p, len);
    return p;
}

LPVOID MapViewOfFile(HANDLE mapping, DWORD access, DWORD offHigh, DWORD offLow, SIZE_T count)
{
    return MapViewOfFileEx(mapping, access, offHigh, offLow, count, NULL);
}

BOOL UnmapViewOfFile(LPCVOID baseAddr)
{
    size_t len = view_take(baseAddr);
    if (len == 0) return FALSE;
    if (arena_has(baseAddr, len)) return arena_release((void *)baseAddr, len);
    return munmap((void *)baseAddr, len) == 0;
}

/* ===================================================================== */
/* VirtualQuery                                                           */
/* ===================================================================== */

/*
 * Answers from the view registry rather than from a constant.
 *
 * This used to report RegionSize 0x1000, MEM_COMMIT, PAGE_READWRITE and
 * AllocationBase NULL for every address it was handed, mapped or not. Four
 * kernel entry points are built on it, and one of them chooses a deallocator
 * with it: MmFreeContiguousMemory frees via VirtualFree only when
 * AllocationBase equals the pointer, so a hardcoded NULL sent every
 * contiguous buffer -- all of which come from VirtualAlloc, i.e. mmap -- to
 * _aligned_free, which is free(). The allocator aborts on the foreign
 * pointer. MmQueryAllocationSize answered 0x1000 for everything and
 * NtQueryVirtualMemory called unmapped addresses committed and readable.
 *
 * Everything the shim maps -- VirtualAlloc and MapViewOfFileEx alike -- is in
 * the registry, so it can answer for exactly the memory it owns and say
 * MEM_FREE for the rest. Saying MEM_FREE for an address it did not map is the
 * honest answer: a host heap pointer is not a Win32 reservation, and the
 * callers that branch on this want to know which allocator owns the pointer.
 */
SIZE_T VirtualQuery(LPCVOID address, PMEMORY_BASIC_INFORMATION buffer, SIZE_T length)
{
    if (!buffer || length < sizeof(*buffer)) return 0;
    memset(buffer, 0, sizeof(*buffer));

    void  *base = NULL;
    size_t len  = 0;

    if (!view_lookup(address, &base, &len)) {
        /* Not ours. Report it free rather than inventing a committed page. */
        buffer->BaseAddress    = (PVOID)address;
        buffer->AllocationBase = NULL;
        buffer->RegionSize     = 0;
        buffer->State          = MEM_FREE;
        buffer->Protect        = PAGE_NOACCESS;
        buffer->Type           = 0;
        return sizeof(*buffer);
    }

    buffer->BaseAddress      = (PVOID)address;
    buffer->AllocationBase   = base;
    buffer->AllocationProtect = PAGE_READWRITE;
    /* From the queried address to the end of the region, which is what Win32
     * reports and what callers sizing a copy out of it depend on. */
    buffer->RegionSize       = len - (size_t)((uintptr_t)address - (uintptr_t)base);
    buffer->State            = MEM_COMMIT;
    buffer->Protect          = PAGE_READWRITE;
    buffer->Type             = MEM_PRIVATE;
    return sizeof(*buffer);
}

BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX b)
{
    if (!b) return FALSE;
#if defined(__APPLE__)
    /* Darwin has no sysinfo(2): physical memory comes from sysctl hw.memsize,
     * swap from vm.swapusage, the free page count from the Mach VM statistics. */
    uint64_t memsize = 0;
    size_t   len     = sizeof(memsize);
    int oid_memsize[] = { CTL_HW, HW_MEMSIZE };
    if (sysctl(oid_memsize, 2, &memsize, &len, NULL, 0) != 0) return FALSE;

    vm_size_t page = 0;
    if (host_page_size(mach_host_self(), &page) != KERN_SUCCESS) page = 4096;

    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    ULONGLONG avail = 0;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vm, &count) == KERN_SUCCESS)
        avail = ((ULONGLONG)vm.free_count + vm.inactive_count) * (ULONGLONG)page;

    struct xsw_usage swap;
    len = sizeof(swap);
    int oid_swapusage[] = { CTL_VM, VM_SWAPUSAGE };
    if (sysctl(oid_swapusage, 2, &swap, &len, NULL, 0) != 0)
        memset(&swap, 0, sizeof(swap));

    b->ullTotalPhys     = (ULONGLONG)memsize;
    b->ullAvailPhys     = avail;
    b->ullTotalPageFile = b->ullTotalPhys + (ULONGLONG)swap.xsu_total;
    b->ullAvailPageFile = b->ullAvailPhys + (ULONGLONG)swap.xsu_avail;
#else
    struct sysinfo si;
    if (sysinfo(&si) != 0) return FALSE;

    ULONGLONG unit = si.mem_unit ? si.mem_unit : 1;
    b->ullTotalPhys     = (ULONGLONG)si.totalram  * unit;
    b->ullAvailPhys     = (ULONGLONG)si.freeram   * unit;
    b->ullTotalPageFile = b->ullTotalPhys + (ULONGLONG)si.totalswap * unit;
    b->ullAvailPageFile = b->ullAvailPhys + (ULONGLONG)si.freeswap  * unit;
#endif
    b->ullTotalVirtual  = b->ullTotalPhys;
    b->ullAvailVirtual  = b->ullAvailPhys;
    b->ullAvailExtendedVirtual = 0;
    b->dwMemoryLoad = b->ullTotalPhys
        ? (DWORD)(100 - (b->ullAvailPhys * 100 / b->ullTotalPhys)) : 0;
    return TRUE;
}

/* ===================================================================== */
/* Aligned allocation                                                     */
/* ===================================================================== */

void *_aligned_malloc(SIZE_T size, SIZE_T alignment)
{
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    /* round alignment up to a power of two */
    SIZE_T a = sizeof(void *);
    while (a < alignment) a <<= 1;
    void *p = NULL;
    if (posix_memalign(&p, a, size ? size : 1) != 0) return NULL;
    return p;
}

void _aligned_free(void *ptr) { free(ptr); }

/* ===================================================================== */
/* Case-insensitive string compare                                        */
/* ===================================================================== */

int _stricmp(const char *a, const char *b)            { return strcasecmp(a, b); }
int _strnicmp(const char *a, const char *b, SIZE_T n) { return strncasecmp(a, b, n); }

/* ===================================================================== */
/* Wide-string helpers (16-bit Xbox WCHAR)                                */
/* ===================================================================== */

SIZE_T xbox_wcslen(const WCHAR *s)
{
    SIZE_T n = 0;
    if (s) while (s[n]) n++;
    return n;
}

int xbox_wcsncmp(const WCHAR *a, const WCHAR *b, SIZE_T n)
{
    for (SIZE_T i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
        if (a[i] == 0)    return 0;
    }
    return 0;
}

WCHAR *xbox_wcscat(WCHAR *dst, const WCHAR *src)
{
    SIZE_T d = xbox_wcslen(dst), i = 0;
    while (src[i]) { dst[d + i] = src[i]; i++; }
    dst[d + i] = 0;
    return dst;
}

WCHAR *xbox_wcscpy(WCHAR *dst, const WCHAR *src)
{
    SIZE_T i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
    return dst;
}

/* ===================================================================== */
/* Time conversion                                                        */
/* ===================================================================== */

BOOL SystemTimeToFileTime(const SYSTEMTIME *st, LPFILETIME ft)
{
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = st->wYear - 1900;
    tm.tm_mon  = st->wMonth - 1;
    tm.tm_mday = st->wDay;
    tm.tm_hour = st->wHour;
    tm.tm_min  = st->wMinute;
    tm.tm_sec  = st->wSecond;
    time_t t = timegm(&tm);
    ULONGLONG ticks = FILETIME_EPOCH_DIFF
                    + (ULONGLONG)t * 10000000ULL
                    + (ULONGLONG)st->wMilliseconds * 10000ULL;
    ft->dwLowDateTime  = (DWORD)(ticks & 0xFFFFFFFFULL);
    ft->dwHighDateTime = (DWORD)(ticks >> 32);
    return TRUE;
}

BOOL FileTimeToSystemTime(const FILETIME *ft, LPSYSTEMTIME st)
{
    ULONGLONG ticks = ((ULONGLONG)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
    if (ticks < FILETIME_EPOCH_DIFF) { memset(st, 0, sizeof(*st)); return FALSE; }
    ULONGLONG since = ticks - FILETIME_EPOCH_DIFF;
    time_t t = (time_t)(since / 10000000ULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    fill_systemtime(st, &tm, (long)((since % 10000000ULL) * 100ULL));
    return TRUE;
}

/* ===================================================================== */
/* Exception handling (compile-shim -- SEH not yet emulated on Linux)     */
/* ===================================================================== */

VOID RtlUnwind(PVOID TargetFrame, PVOID TargetIp,
               PEXCEPTION_RECORD ExceptionRecord, PVOID ReturnValue)
{
    (void)TargetFrame; (void)TargetIp; (void)ExceptionRecord; (void)ReturnValue;
    /* TODO: Windows SEH unwinding is not yet emulated on Linux. */
}

VOID RaiseException(DWORD code, DWORD flags, DWORD nargs, const ULONG_PTR *args)
{
    (void)flags; (void)nargs; (void)args;
    fprintf(stderr, "[win32_compat] RaiseException(0x%08X): SEH not emulated\n", code);
    /* TODO: on Windows this does not return; SEH dispatch unimplemented. */
}

/* ===================================================================== */
/* Vectored exception handling on signals                                */
/* ===================================================================== */

#define VEH_MAX 16
static PVECTORED_EXCEPTION_HANDLER s_veh[VEH_MAX];
static volatile int s_veh_n;
static LPTOP_LEVEL_EXCEPTION_FILTER s_unhandled_filter;
static pthread_mutex_t s_veh_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_veh_installed;

#if defined(__x86_64__) && defined(__APPLE__)
/* macOS (Intel, or Apple silicon under Rosetta 2): the same, from Darwin's
 * thread state */
#include <sys/ucontext.h>
#define VEH_DARWIN 1
static void veh_from_uc(CONTEXT *c, const ucontext_t *uc)
{
    const _STRUCT_X86_THREAD_STATE64 *g = &uc->uc_mcontext->__ss;
    c->Rax = g->__rax; c->Rcx = g->__rcx; c->Rdx = g->__rdx; c->Rbx = g->__rbx; c->Rsp = g->__rsp; c->Rbp = g->__rbp;
    c->Rsi = g->__rsi; c->Rdi = g->__rdi; c->R8 = g->__r8; c->R9 = g->__r9; c->R10 = g->__r10; c->R11 = g->__r11;
    c->R12 = g->__r12; c->R13 = g->__r13; c->R14 = g->__r14; c->R15 = g->__r15;
    c->Rip = g->__rip; c->EFlags = (uint32_t)g->__rflags;
}
static void veh_to_uc(ucontext_t *uc, const CONTEXT *c)
{
    _STRUCT_X86_THREAD_STATE64 *g = &uc->uc_mcontext->__ss;
    g->__rax = c->Rax; g->__rcx = c->Rcx; g->__rdx = c->Rdx; g->__rbx = c->Rbx; g->__rsp = c->Rsp; g->__rbp = c->Rbp;
    g->__rsi = c->Rsi; g->__rdi = c->Rdi; g->__r8 = c->R8; g->__r9 = c->R9; g->__r10 = c->R10; g->__r11 = c->R11;
    g->__r12 = c->R12; g->__r13 = c->R13; g->__r14 = c->R14; g->__r15 = c->R15;
    g->__rip = c->Rip; g->__rflags = c->EFlags;
}
#define VEH_ERR(uc) ((uc)->uc_mcontext->__es.__err)
#elif defined(__x86_64__) && defined(__linux__)
#include <ucontext.h>
#define VEH_ERR(uc) ((uc)->uc_mcontext.gregs[REG_ERR])
static void veh_from_uc(CONTEXT *c, const ucontext_t *uc)
{
    const greg_t *g = uc->uc_mcontext.gregs;
    c->Rax = (uint64_t)g[REG_RAX]; c->Rcx = (uint64_t)g[REG_RCX]; c->Rdx = (uint64_t)g[REG_RDX];
    c->Rbx = (uint64_t)g[REG_RBX]; c->Rsp = (uint64_t)g[REG_RSP]; c->Rbp = (uint64_t)g[REG_RBP];
    c->Rsi = (uint64_t)g[REG_RSI]; c->Rdi = (uint64_t)g[REG_RDI];
    c->R8 = (uint64_t)g[REG_R8];   c->R9 = (uint64_t)g[REG_R9];   c->R10 = (uint64_t)g[REG_R10];
    c->R11 = (uint64_t)g[REG_R11]; c->R12 = (uint64_t)g[REG_R12]; c->R13 = (uint64_t)g[REG_R13];
    c->R14 = (uint64_t)g[REG_R14]; c->R15 = (uint64_t)g[REG_R15];
    c->Rip = (uint64_t)g[REG_RIP]; c->EFlags = (uint32_t)g[REG_EFL];
}
static void veh_to_uc(ucontext_t *uc, const CONTEXT *c)
{
    greg_t *g = uc->uc_mcontext.gregs;
    g[REG_RAX] = (greg_t)c->Rax; g[REG_RCX] = (greg_t)c->Rcx; g[REG_RDX] = (greg_t)c->Rdx;
    g[REG_RBX] = (greg_t)c->Rbx; g[REG_RSP] = (greg_t)c->Rsp; g[REG_RBP] = (greg_t)c->Rbp;
    g[REG_RSI] = (greg_t)c->Rsi; g[REG_RDI] = (greg_t)c->Rdi;
    g[REG_R8] = (greg_t)c->R8;   g[REG_R9] = (greg_t)c->R9;   g[REG_R10] = (greg_t)c->R10;
    g[REG_R11] = (greg_t)c->R11; g[REG_R12] = (greg_t)c->R12; g[REG_R13] = (greg_t)c->R13;
    g[REG_R14] = (greg_t)c->R14; g[REG_R15] = (greg_t)c->R15;
    g[REG_RIP] = (greg_t)c->Rip; g[REG_EFL] = (greg_t)c->EFlags;
}
#endif

#if defined(__x86_64__) && (defined(__APPLE__) || defined(__linux__))
static void veh_signal(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = (ucontext_t *)ucv;
    EXCEPTION_RECORD er;
    CONTEXT ctx;
    EXCEPTION_POINTERS ep;
    int i, n, saved_errno = errno;

    memset(&er, 0, sizeof er);
    veh_from_uc(&ctx, uc);
    er.ExceptionAddress = (PVOID)(uintptr_t)ctx.Rip;
    switch (sig) {
    case SIGSEGV: case SIGBUS:
        er.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
        er.NumberParameters = 2;
        /* page-fault error code bit 1: the access was a write */
        er.ExceptionInformation[0] = (VEH_ERR(uc) & 2) ? 1 : 0;
        er.ExceptionInformation[1] = (ULONG_PTR)si->si_addr;
        break;
    case SIGTRAP:
        er.ExceptionCode = (si->si_code == TRAP_BRKPT) ? EXCEPTION_BREAKPOINT : EXCEPTION_SINGLE_STEP;
        break;
    case SIGFPE:
        er.ExceptionCode = (si->si_code == FPE_INTOVF) ? EXCEPTION_INT_OVERFLOW
                         : (si->si_code == FPE_INTDIV) ? EXCEPTION_INT_DIVIDE_BY_ZERO
                         : EXCEPTION_FLT_INVALID_OPERATION;
        break;
    default:
        er.ExceptionCode = EXCEPTION_ILLEGAL_INSTRUCTION;
        break;
    }
    ep.ExceptionRecord = &er;
    ep.ContextRecord = &ctx;
    n = s_veh_n;
    for (i = 0; i < n; i++) {
        PVECTORED_EXCEPTION_HANDLER h = s_veh[i];
        if (h && h(&ep) == EXCEPTION_CONTINUE_EXECUTION) {
            veh_to_uc(uc, &ctx);
            errno = saved_errno;
            return;
        }
    }
    if (s_unhandled_filter && s_unhandled_filter(&ep) == EXCEPTION_CONTINUE_EXECUTION) {
        veh_to_uc(uc, &ctx);
        errno = saved_errno;
        return;
    }
    /* Nobody took it: let the signal do what it would have done (the
     * instruction runs again and dies with the default action). */
    signal(sig, SIG_DFL);
    errno = saved_errno;
}

static void veh_install(void)
{
    static const int sigs[] = { SIGSEGV, SIGBUS, SIGTRAP, SIGFPE, SIGILL };
    struct sigaction sa;
    size_t i;
    if (s_veh_installed) return;
    s_veh_installed = 1;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = veh_signal;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < sizeof sigs / sizeof sigs[0]; i++) sigaction(sigs[i], &sa, NULL);
}
#else
static void veh_install(void) {}
#endif

PVOID AddVectoredExceptionHandler(ULONG First, PVECTORED_EXCEPTION_HANDLER Handler)
{
    int i;
    if (!Handler) return NULL;
    pthread_mutex_lock(&s_veh_lock);
    if (s_veh_n >= VEH_MAX) { pthread_mutex_unlock(&s_veh_lock); return NULL; }
    if (First) {
        for (i = s_veh_n; i > 0; i--) s_veh[i] = s_veh[i - 1];
        s_veh[0] = Handler;
    } else s_veh[s_veh_n] = Handler;
    __sync_synchronize();
    s_veh_n++;
    veh_install();
    pthread_mutex_unlock(&s_veh_lock);
    return (PVOID)Handler;
}
ULONG RemoveVectoredExceptionHandler(PVOID h)
{
    int i, j;
    pthread_mutex_lock(&s_veh_lock);
    for (i = 0; i < s_veh_n; i++)
        if ((PVOID)s_veh[i] == h) {
            for (j = i; j + 1 < s_veh_n; j++) s_veh[j] = s_veh[j + 1];
            s_veh_n--;
            s_veh[s_veh_n] = NULL;
            pthread_mutex_unlock(&s_veh_lock);
            return 1;
        }
    pthread_mutex_unlock(&s_veh_lock);
    return 0;
}
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter)
{
    LPTOP_LEVEL_EXCEPTION_FILTER old = s_unhandled_filter;
    s_unhandled_filter = filter;
    veh_install();
    return old;
}
BOOL SetThreadStackGuarantee(PULONG size) { (void)size; return TRUE; }

/* ===================================================================== */
/* Affinity, high-resolution timers                                      */
/* ===================================================================== */

/* The process's CPUs as they were at start-up: Linux has only per-thread
 * masks, and the thread asking may have been pinned to one core since (the
 * single-CPU mode pins the game's threads; Windows' process mask stays). */
#if defined(__APPLE__)
/* macOS: no affinity masks; every CPU the system has */
BOOL GetProcessAffinityMask(HANDLE process, DWORD_PTR *procMask, DWORD_PTR *sysMask)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    DWORD_PTR m;
    (void)process;
    if (n < 1) n = 1;
    if (n > 63) n = 63;
    m = ((DWORD_PTR)1 << n) - 1;
    if (procMask) *procMask = m;
    if (sysMask) *sysMask = m;
    return TRUE;
}
#else
static cpu_set_t s_start_cpus;
static int s_start_cpus_ok;
__attribute__((constructor)) static void w32_note_start_cpus(void)
{
    CPU_ZERO(&s_start_cpus);
    s_start_cpus_ok = sched_getaffinity(0, sizeof s_start_cpus, &s_start_cpus) == 0;
}
BOOL GetProcessAffinityMask(HANDLE process, DWORD_PTR *procMask, DWORD_PTR *sysMask)
{
    cpu_set_t cs;
    DWORD_PTR m = 0;
    int i;
    (void)process;
    CPU_ZERO(&cs);
    if (s_start_cpus_ok) cs = s_start_cpus;
    else if (sched_getaffinity(0, sizeof cs, &cs) != 0) return FALSE;
    for (i = 0; i < (int)(sizeof(DWORD_PTR) * 8) && i < CPU_SETSIZE; i++)
        if (CPU_ISSET(i, &cs)) m |= (DWORD_PTR)1 << i;
    if (procMask) *procMask = m;
    if (sysMask) *sysMask = m;
    return TRUE;
}
#endif
DWORD_PTR SetThreadAffinityMask(HANDLE thread, DWORD_PTR mask)
{
#if defined(__APPLE__)
    /* macOS has no thread affinity (only hints): the mask is accepted */
    (void)thread; (void)mask;
    return 1;
}
#else
    /* Only the calling thread (GetCurrentThread()) is supported. */
    cpu_set_t cs;
    int i;
    DWORD_PTR old = 0;
    (void)thread;
    CPU_ZERO(&cs);
    if (pthread_getaffinity_np(pthread_self(), sizeof cs, &cs) == 0)
        for (i = 0; i < (int)(sizeof(DWORD_PTR) * 8) && i < CPU_SETSIZE; i++)
            if (CPU_ISSET(i, &cs)) old |= (DWORD_PTR)1 << i;
    CPU_ZERO(&cs);
    for (i = 0; i < (int)(sizeof(DWORD_PTR) * 8) && i < CPU_SETSIZE; i++)
        if (mask & ((DWORD_PTR)1 << i)) CPU_SET(i, &cs);
    if (pthread_setaffinity_np(pthread_self(), sizeof cs, &cs) != 0) return 0;
    return old ? old : 1;
}
#endif
#include <dlfcn.h>
HMODULE GetModuleHandleA(LPCSTR name)
{
    Dl_info di;
    static char anchor;
    if (name) return NULL;
    if (dladdr(&anchor, &di) && di.dli_fbase) return (HMODULE)di.dli_fbase;
    return NULL;
}
HANDLE CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access)
{
    (void)access;
    return CreateWaitableTimerW(sa, (flags & CREATE_WAITABLE_TIMER_MANUAL_RESET) != 0, name);
}

#endif /* !_WIN32 */
